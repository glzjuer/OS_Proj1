/***************************************************************************
 *  Title: Runtime environment 
 * -------------------------------------------------------------------------
 *    Purpose: Runs commands
 *    Author: Stefan Birrer
 *    Version: $Revision: 1.1 $
 *    Last Modification: $Date: 2005/10/13 05:24:59 $
 *    File: $RCSfile: runtime.c,v $
 *    Copyright: (C) 2002 by Stefan Birrer
 ***************************************************************************/
/***************************************************************************
 *  ChangeLog:
 * -------------------------------------------------------------------------
 *    $Log: runtime.c,v $
 *    Revision 1.1  2005/10/13 05:24:59  sbirrer
 *    - added the skeleton files
 *
 *    Revision 1.6  2002/10/24 21:32:47  sempi
 *    final release
 *
 *    Revision 1.5  2002/10/23 21:54:27  sempi
 *    beta release
 *
 *    Revision 1.4  2002/10/21 04:49:35  sempi
 *    minor correction
 *
 *    Revision 1.3  2002/10/21 04:47:05  sempi
 *    Milestone 2 beta
 *
 *    Revision 1.2  2002/10/15 20:37:26  sempi
 *    Comments updated
 *
 *    Revision 1.1  2002/10/15 20:20:56  sempi
 *    Milestone 1
 *
 ***************************************************************************/
#define __RUNTIME_IMPL__

/************System include***********************************************/
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>

/************Private include**********************************************/
#include "runtime.h"
#include "io.h"

/************Defines and Typedefs*****************************************/
/*  #defines and typedefs should have their names in all caps.
 *  Global variables begin with g. Global constants with k. Local
 *  variables should be in all lower case. When initializing
 *  structures and arrays, line everything up in neat columns.
 */

/************Global Variables*********************************************/

#define NBUILTINCOMMANDS (sizeof BuiltInCommands / sizeof(char*))

typedef struct bgjob_l {
  pid_t pid;
  int lid;
  char* status;
  commandT* cmd;
  struct bgjob_l* next;
} bgjobL;




/* the pids of the background processes */
bgjobL *bgjobs = NULL;
bgjobL *bgtail = NULL;
//simplify the bg job list handle
//#define MAXJOBTOTAL 32
//bgjobL bgjobs[MAXJOBTOTAL];
int bgc = 0;

pid_t fgpid = 0;
commandT* fgcmd = NULL;

/************Function Prototypes******************************************/
/* run command */
static void RunCmdFork(commandT*, bool);
/* runs an external program command after some checks */
static void RunExternalCmd(commandT*, bool);
/* resolves the path and checks for exutable flag */
static bool ResolveExternalCmd(commandT*);
/* forks and runs a external program */
static void Exec(commandT*, bool);
/* runs a builtin command */
static void RunBuiltInCmd(commandT*);
/* checks whether a command is a builtin command */
static bool IsBuiltIn(char*);

/* adds jobs to bgjobs */
static void AddJob(pid_t pid, commandT* cmd, char* status);
/* change jobs status */
static void ChangeJob(pid_t pid, char* status);
/* find latest job Stopped, otherwise running */
static bgjobL*  FindLatest();
/* Movd bg job to fg */
static void BgToFg(bgjobL* job);


/* delete and free the bgjobs*/
static void FreeBgL(pid_t pid);
/* free single Bg node*/
static void FreeSingle(bgjobL* job);

/************External Declaration*****************************************/

/**************Implementation***********************************************/
int total_task;
void RunCmd(commandT** cmd, int n)
{
  int i;
  total_task = n;
  if(n == 1)
    RunCmdFork(cmd[0], TRUE);
  else{
    RunCmdPipe(cmd[0], cmd[1]);
    for(i = 0; i < n; i++)
      ReleaseCmdT(&cmd[i]);
  }
}

void RunCmdFork(commandT* cmd, bool fork)
{
  if (cmd->argc<=0)
    return;
  if (IsBuiltIn(cmd->argv[0]))
  {
    RunBuiltInCmd(cmd);
  }
  else
  {
    RunExternalCmd(cmd, fork);
  }
}


void RunCmdFg(commandT* cmd){
	/* Avoid sig influence?*/ 	
	/*
	sigset_t sigset;
	sigemptyset(&sigset);
	sigaddset(&sigset,SIGCHLD);
	sigprocmask(SIG_BLOCK, &sigset, NULL);
	*/
	//int status;
	if(cmd->argc == 1){
		bgjobL* ljob = FindLatest();
		if(ljob == NULL){
			printf("fg: current: no such job\n");
			fflush(stdout);
		}else{
			BgToFg(ljob);
			//sigprocmask(SIG_UNBLOCK, &sigset, NULL);
			//waitpid(ljob->pid, &status, WUNTRACED);
		}
	}else{
		int clid = atoi(cmd->argv[1]);		
		bgjobL* cur = bgjobs;
			
		while(cur != NULL){
			if(cur->lid == clid){
				break;
			}
			cur = cur->next;
		}
		if(cur == NULL){
			printf("fg: %d no such job\n", clid);
			fflush(stdout);
	    }else if(strcmp(cur->status, "Done") == 0){
			printf("fg: job has terminated\n");
			fflush(stdout);
		}else{
			BgToFg(cur);	
			//sigprocmask(SIG_UNBLOCK,&sigset,NULL);
			//waitpid(cur->pid, &status, WUNTRACED);
		}
	}
}


void RunCmdBg(commandT* cmd)
{
	if(cmd->argc == 1){
		bgjobL* cur = bgjobs;
		//int maxlid = 1;
		bgjobL* toRunJob = NULL;
		while(cur != NULL){
			if(strcmp(cur->status, "Stopped") == 0){   	
				//maxlid = cur->lid;
				toRunJob =  cur;
			}	
			cur = cur->next;
		}
		
		if(toRunJob == NULL){
			printf("bg: current: no such job\n");
			fflush(stdout);
		}else{
			toRunJob->status = "Running";
			kill(toRunJob->pid,SIGCONT);
			//printf("[%d] %s &\n", maxlid, toRunJob->cmd->cmdline);
			fflush(stdout);
		}
	}else{
		int clid = atoi(cmd->argv[1]);
		bgjobL* cur = bgjobs;
			
		while(cur != NULL){
			if(cur->lid == clid){
				break;
			}
			cur = cur->next;
		}
		if(cur == NULL){
			printf("bg: %d: no such job\n", clid);
			fflush(stdout);
		}else if(strcmp(cur->status, "Running") == 0){
			printf("job %d already in background\n", clid);	
			fflush(stdout);
		}else if(strcmp(cur->status, "Done") == 0){
			printf("job has terminated\n");
			fflush(stdout);
		}else if(strcmp(cur->status, "Stopped") == 0){
			cur->status = "Running";
			kill(cur->pid,SIGCONT);
			//printf("[%d] %s &\n", clid, cur->cmd->cmdline);
			//fflush(stdout);
		}else{
			printf("process status unrecognized!\n");
		}
	}
}

void RunCmdPipe(commandT* cmd1, commandT* cmd2)
{
}

void RunCmdRedirOut(commandT* cmd, char* file)
{
}

void RunCmdRedirIn(commandT* cmd, char* file)
{
}


/*Try to run an external command*/
static void RunExternalCmd(commandT* cmd, bool fork)
{
  if (ResolveExternalCmd(cmd)){
    Exec(cmd, fork);
  }
  else {
    printf("%s: command not found\n", cmd->argv[0]);
    fflush(stdout);
    ReleaseCmdT(&cmd);
  }
}

/*Find the executable based on search list provided by environment variable PATH*/
static bool ResolveExternalCmd(commandT* cmd)
{
  char *pathlist, *c;
  char buf[1024];
  int i, j;
  struct stat fs;

  if(strchr(cmd->argv[0],'/') != NULL){
    if(stat(cmd->argv[0], &fs) >= 0){
      if(S_ISDIR(fs.st_mode) == 0)
        if(access(cmd->argv[0],X_OK) == 0){/*Whether it's an executable or the user has required permisson to run it*/
          cmd->name = strdup(cmd->argv[0]);
          return TRUE;
        }
    }
    return FALSE;
  }
  pathlist = getenv("PATH");
  if(pathlist == NULL) return FALSE;
  i = 0;
  while(i<strlen(pathlist)){
    c = strchr(&(pathlist[i]),':');
    if(c != NULL){
      for(j = 0; c != &(pathlist[i]); i++, j++)
        buf[j] = pathlist[i];
      i++;
    }
    else{
      for(j = 0; i < strlen(pathlist); i++, j++)
        buf[j] = pathlist[i];
    }
    buf[j] = '\0';
    strcat(buf, "/");
    strcat(buf,cmd->argv[0]);
    if(stat(buf, &fs) >= 0){
      if(S_ISDIR(fs.st_mode) == 0)
        if(access(buf,X_OK) == 0){/*Whether it's an executable or the user has required permisson to run it*/
          cmd->name = strdup(buf); 
          return TRUE;
        }
    }
  }
  return FALSE; /*The command is not found or the user don't have enough priority to run.*/
}

static void Exec(commandT* cmd, bool forceFork)
{
	sigset_t sigset;
	sigemptyset(&sigset);
	sigaddset(&sigset,SIGCHLD);
	sigprocmask(SIG_BLOCK,&sigset,NULL);

	pid_t pid;
	int status;
	pid = fork();

	if(pid > 0){
		if(cmd->bg == 0){
			fgpid = pid;		
			fgcmd = cmd;
			waitpid(pid,&status, WUNTRACED);
			sigprocmask(SIG_UNBLOCK, &sigset,NULL);
		}else{
			AddJob(pid,cmd,"Running");
			sigprocmask(SIG_UNBLOCK, &sigset,NULL);
			waitpid(pid,&status, WNOHANG | WUNTRACED);
		}
	}else if(pid == 0){
		//printf("Run command %s\n", cmd->cmdline);
		//fflush(stdout);
		if(cmd->bg == 1){
				
		}else{
			setpgid(0,0);
		}	
		execv(cmd->name, cmd->argv);
		sigprocmask(SIG_UNBLOCK, &sigset,NULL);
	}else{
		printf("Error when fork()\n");
		fflush(stdout);
	} 

	/*
	pid_t pid;
	pid = fork();
	if(fork()==0){
		execv(cmd->name,cmd->argv);
	}else

	wait(NULL);
	*/
}

static bool IsBuiltIn(char* cmd)
{
	if(strcmp(cmd,"bg") == 0 || strcmp(cmd,"cd") == 0 || strcmp(cmd,"jobs") == 0 || strcmp(cmd,"fg") == 0){
		return TRUE;
	}else{
		return FALSE;
	}
}


static void RunBuiltInCmd(commandT* cmd)
{
	if(strcmp(cmd->argv[0], "cd") == 0){
		if(cmd->argc == 1){
			int flag = chdir(getenv("HOME"));
			if(flag == -1){
				printf("Can not get $HOME, please check\n");
				fflush(stdout);
				return;
			}
		}else{
			int flag = chdir(cmd->argv[1]);
			if(flag == -1){
				printf("cd: %s: No such file or directory\n",cmd->argv[1]);
				fflush(stdout);
				return;
			}
		}
	}else if(strcmp(cmd->argv[0], "bg") == 0){
		//printf("Run command bg\n");
		//fflush(stdout);
		RunCmdBg(cmd);
		return;
	}else if(strcmp(cmd->argv[0], "fg") == 0){
		/*
		sigset_t sigset;
		sigemptyset(&sigset);
		sigaddset(&sigset,SIGCHLD);
		*/
		//printf("Run command fg\n");
		//fflush(stdout);
		RunCmdFg(cmd);
		while(fgpid > 0){
			//printf("wait fgpid: %d\n", fgpid);
			//fflush(stdout);
			sleep(1);
		}

	}else if(strcmp(cmd->argv[0], "jobs") == 0){
		bgjobL* cur = bgjobs;
		while(cur != NULL){
			if(strcmp(cur->status, "Running") == 0){
				printf("[%d]   %s                 %s &\n", cur->lid, cur->status, cur->cmd->cmdline);
				fflush(stdout);
			}else{
				printf("[%d]   %s                 %s\n", cur->lid, cur->status, cur->cmd->cmdline);
				fflush(stdout);
			}
			cur = cur->next;
		}
	}

}

void CheckJobs()
{
	bgjobL* cur = bgjobs;
	//printf("begin check jobs\n");
	//fflush(stdout);
	while(cur != NULL){
		if(strcmp(cur->status, "Done") == 0){
			//printf("check job lid: %d\n", cur->lid);
			if(cur->cmd->bg == 1){
				printf("[%d]   %s                    %s\n", cur->lid, cur->status, cur->cmd->cmdline);
			}
			fflush(stdout);
			//printf("Free job: %d\n", cur->lid);
			FreeBgL(cur->pid);
		}
		cur = cur->next;
	}

}

void Sigchld_Handler(){
	pid_t pid;
	int status;
	pid = waitpid(-1,&status, WNOHANG | WUNTRACED);
	/* check whether child exited normally*/
	if(!WIFEXITED(status)){
	 	//printf("find SIGINT | SIGTSTP\n");
		//fflush(stdout);
		return;
	}
	if(WIFCONTINUED(status)){
		return;
	}
	if(fgpid == pid){
		fgpid = 0;
	}
	//printf("-Receieve signal and check pid %d\n", pid);
	//fflush(stdout);
	ChangeJob(pid, "Done");	
}

void StopFgProc(){
	if(fgpid == 0){
		return;
	}
	
	kill(fgpid, SIGTSTP);
	printf("\n");
	/*check whether the process is in the bg*/
	bgjobL* cur = bgjobs;
	int fglid = 1;
	while(cur != NULL){
		if(cur->pid == fgpid){
			fglid = cur->lid; 
			break;
		}
		cur = cur->next;
	}

	if(cur == NULL){
		fgcmd->bg = 1;
		AddJob(fgpid,fgcmd,"Stopped");
		if(bgtail != NULL){
			fglid = bgtail->lid;
		}
	}else{
		cur->status = "Stopped";
		cur->cmd->bg = 1;
	}
	printf("[%d]   Stopped                 %s\n", fglid, fgcmd->cmdline);
	fflush(stdout);
	fgpid = 0;
}


void IntFgProc(){
	if(fgpid == 0){
		printf("\n");	
		return;
	}
	kill((-1)*fgpid,SIGINT);
	/* make sure error and memory leak */
	FreeBgL(fgpid);
	fgpid = 0;
}


static bgjobL* FindLatest(){
	bgjobL* res = NULL;
	bgjobL* cur = bgjobs;
	if(bgjobs == NULL || bgtail == NULL) return NULL;
	if(strcmp(bgtail->status, "Stopped") == 0){
		return bgtail;
	}
	while(cur != bgtail){
		if(strcmp(cur->status, "Stopped") == 0){
			res = cur;
		}
		cur = cur->next;
	}
	if(res != NULL){
		return res;
	}
	if(strcmp(bgtail->status, "Running") == 0){
		res = bgtail;
		return res;
	}else{
		while(cur != bgtail){
			if(strcmp(cur->status, "Running") == 0){
				res = cur;
			}	
			cur = cur->next;
		}
		if(res != NULL){
			return res;
		}
	}
	return NULL;
}

static void FreeBgL(pid_t pid){
	if(pid == 0){
		return;
	}
	if(bgjobs == NULL){
		return;
	}
	bgjobL* cur = bgjobs;
	bgjobL* pre = bgjobs;
	
	if(bgjobs->pid == pid){
		if(bgtail == bgjobs){
			FreeSingle(cur);
			bgtail = bgjobs = NULL;
			return;
		}else{
			bgjobs = bgjobs->next;
			FreeSingle(cur);
			return;
		}
	}

	while(cur != NULL){
		if(cur->pid == pid){
			break;
		}
		pre = cur;
		cur = cur->next;
	}
	if(cur != NULL){
		if(cur == bgtail){
			FreeSingle(cur);
			bgtail = bgjobs;
			while(bgtail->next){
				bgtail = bgtail->next;
			}
			return;
		}else{
			pre->next = cur->next;
			FreeSingle(cur);
			return;
		}			
	}else{
		//printf("-Job Not Found!\n");
	}

}

static void BgToFg(bgjobL* job){
	//printf("Deal BgToFg Fun with %d\n", job->pid);
	if(job == NULL){
		printf("Error bg job to Fg\n");
	}
	if(strcmp(job->status, "Stopped") == 0){
		job->status = "Running";
		//printf("Continue job %d\n", job->pid);
		kill(job->pid, SIGCONT);
	}
	job->cmd->bg = 0;
	fgpid = job->pid;
	fgcmd = job->cmd;
}

static void ChangeJob(pid_t pid, char* status){
	bgjobL* cur = bgjobs;
	while(cur != NULL){
		if(cur->pid == pid){
			break;
		}
		cur = cur->next;
	}
	if(cur != NULL){
		cur->status = status;
		//printf("find the job: %d and change into %s\n", pid, status);
	}else{
		// printf("-Can not find the Job to change\n");
		// fflush(stdout);
	}
}

static void AddJob(pid_t pid, commandT* cmd, char* status){
	bgjobL* cur = bgjobs;
	
	bgjobL * newjob = malloc(sizeof(bgjobL));
	newjob->pid = pid;
	newjob->cmd = cmd;
	newjob->status = status;
	newjob->next = NULL;
	if(cur != NULL){	
		newjob->lid = bgtail->lid+1;
		bgtail->next = newjob;
		bgtail = newjob;
	}else{
		newjob->lid = 1;
		bgjobs = newjob;
		bgtail = newjob;
	}
}


commandT* CreateCmdT(int n)
{
  int i;
  commandT * cd = malloc(sizeof(commandT) + sizeof(char *) * (n + 1));
  cd -> name = NULL;
  cd -> cmdline = NULL;
  cd -> is_redirect_in = cd -> is_redirect_out = 0;
  cd -> redirect_in = cd -> redirect_out = NULL;
  cd -> argc = n;
  for(i = 0; i <=n; i++)
    cd -> argv[i] = NULL;
  return cd;
}

/*Release and collect the space of a commandT struct*/
void ReleaseCmdT(commandT **cmd){
  int i;
  if((*cmd)->name != NULL) free((*cmd)->name);
  if((*cmd)->cmdline != NULL) free((*cmd)->cmdline);
  if((*cmd)->redirect_in != NULL) free((*cmd)->redirect_in);
  if((*cmd)->redirect_out != NULL) free((*cmd)->redirect_out);
  for(i = 0; i < (*cmd)->argc; i++)
    if((*cmd)->argv[i] != NULL) free((*cmd)->argv[i]);
  free(*cmd);
}

/* Release and collect the space of a bgjobL struct */
void FreeSingle(bgjobL* job){
	if(job != NULL)
		free(job);	
}





