/*
	Project 1: 352> Shell
	COM S 352 Spring 2021

	* Data Structure *
	A container based on vectors in c++ to handle job control. The double-pointer, pJobList references a dynamically allocated array of Cmd pointers. The function addJob adds a new Cmd pointer to the list and handles runtime array resizing. pJobList starts with size of 10. Whenever it is full, addJob function doubles the size. When an element (pointer) is removed, the array entry is set to NULL. The first NULL entry in pJobList will store the next new Cmd pointer. 


	* runCmd Function *
	Parses a cmd pointer and runs the command. Execute a pipe command in the first block and other commands in the second block. The reason for two blocks is: Pipe commands create 2 child processes and only runs on foreground. Other commands (basic and incl. file redirection) uses mostly similar code; creates one child process and supports running in background.

	* main Function *
	Majority of the control flow follows the original starter code. It parses user input and executes the given command. The built-in commands jobs and bg are implemented inside main(). Other basic commands are delegated to runCmd funciton. At the end of main program loop, checks for completed jobs and prints their status.

	** Inline comments for detailed documentation.
*/
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <signal.h>
#include <sys/wait.h>
#include <stdbool.h>

#define MAX_LINE 80
#define MAX_ARGS (MAX_LINE/2 + 1)
#define REDIRECT_OUT_OP '>'
#define REDIRECT_IN_OP '<'
#define PIPE_OP '|'
#define BG_OP '&'

/* Holds a single command. */
typedef struct Cmd {
	/* The command as input by the user. */
	char line[MAX_LINE + 1];
	/* The command as null terminated tokens. */
	char tokenLine[MAX_LINE + 1];
	/* Pointers to each argument in tokenLine, non-arguments are NULL. */
	char* args[MAX_ARGS];
	/* Pointers to each symbol in tokenLine, non-symbols are NULL. */
	char* symbols[MAX_ARGS];
	/* The process id of the executing command. */
	pid_t pid;

	/* Additional fields. */
	/* Pass as parameter to waitpid(). */
	int status;
	/* Used by jobs command to differentiate running and stopped jobs.
		(1) Running,  (2) Stopped. */
	int state;		
} Cmd;

/* The process of the currently executing foreground command, or 0. */
pid_t foregroundPid = 0;

/* Implementation of data structure to handle jobs */
Cmd **pJobList = NULL; 	// Double-pointer to array of Cmd pointers
int JOBS_MAX_SIZE = 10; // Maximum capacity of pJobList
int jobCount = 0;		// Number of jobs (running or stopped)
/* Add a job to dynamic array of job list and handles array resizing. */
int addJob(Cmd *cmd) {
	if (jobCount == JOBS_MAX_SIZE) {
		/* pJobList is full -- double the current size */
		Cmd **pNewJobList = calloc(2 * JOBS_MAX_SIZE, sizeof(Cmd*)); // Allocate memory
		memcpy(pNewJobList, pJobList, JOBS_MAX_SIZE * sizeof(Cmd*)); // Copy existing jobs
		JOBS_MAX_SIZE *= 2;
		free(pJobList);
		pJobList = pNewJobList;
	}
	/* Find first NULL element to replace with new Cmd pointer */
	for (int i = 0; i < JOBS_MAX_SIZE; i++) {
		if (pJobList[i] == NULL) {
			pJobList[i] = cmd;
            jobCount++;		// Number of jobs
			return (i+1); 	// Job number 
		}
	}
}

/* Parses the command string contained in cmd->line.
 * * Assumes all fields in cmd (except cmd->line) are initailized to zero.
 * * On return, all fields of cmd are appropriatly populated. */
void parseCmd(Cmd* cmd) {
	char* token;
	int i=0;
	strcpy(cmd->tokenLine, cmd->line);
	strtok(cmd->tokenLine, "\n");
	token = strtok(cmd->tokenLine, " ");
	while (token != NULL) {
		if (*token == '\n') {
			cmd->args[i] = NULL;
		} else if (*token == REDIRECT_OUT_OP || *token == REDIRECT_IN_OP
				|| *token == PIPE_OP || *token == BG_OP) {
			cmd->symbols[i] = token;
			cmd->args[i] = NULL;
		} else {
			cmd->args[i] = token;
		}
		token = strtok(NULL, " ");
		i++;
	}
	cmd->args[i] = NULL;
}

/* Finds the index of the first occurance of symbol in cmd->symbols.
 * * Returns -1 if not found. */
int findSymbol(Cmd* cmd, char symbol) {
	for (int i = 0; i < MAX_ARGS; i++) {
		if (cmd->symbols[i] && *cmd->symbols[i] == symbol) {
			return i;
		}
	}
	return -1;
}

/* Signal handler for SIGTSTP (SIGnal - Terminal SToP),
 * which is caused by the user pressing control+z. */
void sigtstpHandler(int sig_num) {
	/* Reset handler to catch next SIGTSTP. */
	signal(SIGTSTP, sigtstpHandler);
	if (foregroundPid > 0) {
		/* Foward SIGTSTP to the currently running foreground process. */
		kill(foregroundPid, SIGTSTP);
		/* Reset to prevent potentially unintended behavior.
			The process is possibly a background process now. */
		foregroundPid = 0; 
	}
}

/* Parse cmd struct and runs the command. Handle all commands except built-ins (exit, jobs, bg). Add *stopped* foreground and background commands to job list. */
void runCmd(Cmd *cmd)
{
	int index; // Used to check for special symbol/operator
	/* foreground == 1,  background == 0 */
	int isForeground = (findSymbol(cmd, BG_OP) == -1) ? 1 : 0; 

	/* First if-block runs pipe commands, second if-block for the rest. */ 
	if ((index = findSymbol(cmd, PIPE_OP)) != -1) {
		/* If-block for commands with pipe operator -- spawns 2 child process. */
		pid_t pidA, pidB;
		int pipes[2];
		pipe(pipes);
		/* 	pipes[0] == read-end for pidB
		    pipes[1] == write_end for pidA */
		pidA = fork();
		if (pidA == 0) {
			/* 	pidA (child) -- left-hand side args.
				Redirect own stdout to pidB's stdin. */
			dup2(pipes[1], 1);
			close(pipes[0]);
			close(pipes[1]);
			execvp(cmd->args[0], cmd->args); // left args
			printf("execvp error: left-hand side of pipe\n");
			exit(1);
		} else {
			/* Parent creates pidB (second child) then wait for their completion. */
			pidB = fork();
			if (pidB == 0) {
				/* 	pidB child process -- right-hand size args.
					Receives pidA's stdout as stdin */
				dup2(pipes[0], 0);
				close(pipes[0]);
				close(pipes[1]);
				execvp(cmd->args[index+1], (char * const*) &cmd->args[index+1]);
				printf("execvp error: right-hand side of pipe\n");
				exit(1);
			}
		}
		/* Parent (only) reaches here -- blocks until both children process completes. */
		close(pipes[0]);
		close(pipes[1]);
		waitpid(pidA, NULL, 0);
		waitpid(pidB, NULL, 0);
		/* End of pipe command -- return control to main */
	} else {
		/* if-block for basic commands (incl. file redir). In common, all commands here spawns one child process and supports running cmd as background job. */
		pid_t pid = fork();
		if (pid == 0) {
			/* 	Child process block -- Handle file redirection operators if present. Then, execute the command */
			if ((index = findSymbol(cmd, REDIRECT_IN_OP)) != -1) {
				/* Redirect file to stdin */
				char *inFile = cmd->args[index+1]; // Input file path
				int fd = open(inFile, O_RDONLY);
				dup2(fd, 0); // Redirect inFile to stdin
				close(fd);
			} else if ((index = findSymbol(cmd, REDIRECT_OUT_OP)) != -1) {
				/* Redirect stdout to file */
				char *outFile = cmd->args[index+1]; // Output file path
				int fd = open(outFile, O_WRONLY|O_TRUNC|O_CREAT, S_IRWXU|S_IROTH);
				dup2(fd, 1); // Redirect stdout to outFile
				close(fd);
			}
			execvp(cmd->args[0], cmd->args);
			printf("execvp error: non pipe commands");
			exit(1);
		} else { 
			/* 	Parent (only) block -- Handle b/w foreground (FG) and background (BG) commands. Add FG *that exited gracefully not added to job control. FG stopped by ctrl+Z* and *all* BG to job control. */  
			cmd->pid = pid;
			if (isForeground) {
				/* FG -- parent block until child process completes or stopped. */
				foregroundPid = pid;
				if (waitpid(pid, &(cmd->status), WUNTRACED) == cmd->pid) {
					if (WIFSTOPPED(cmd->status)) {
						/* Stopped by ctrl+Z. Set to "Stopped" state (used by jobs) and add to job control. */
						cmd->state = 2; // 2 == Stopped
						addJob(cmd);
					} else {
						/* Exited gracefully. */
						free(cmd);
					}
				}
			} else {
				/* BG -- parent doesn't block/wait. Set to "Running" state (used by jobs) and add to job control. Additionally, prints job number and pid. */
				if (setpgid(pid, 0) != 0) perror("setpgid() error");
				cmd->state = 1; // 1 == Running
                int bgJobNum = addJob(cmd);
                printf("[%d] %d\n", bgJobNum, cmd->pid); // print job number and pid
			}
		}
	}
}

int main(void) {
    pJobList = calloc(JOBS_MAX_SIZE, sizeof(Cmd*)); // Pointer to array of pointers
	/* Listen for control+z (suspend process). */
	signal(SIGTSTP, sigtstpHandler);
	while (1) {
		printf("352> ");
		fflush(stdout);
		Cmd *cmd = (Cmd*) calloc(1, sizeof(Cmd));
		fgets(cmd->line, MAX_LINE, stdin);
		parseCmd(cmd);
		if (!cmd->args[0]) { // empty cmd
			free(cmd);
		} else if (strcmp(cmd->args[0], "exit") == 0) {
			free(cmd);
			exit(0);
		} else if (strcmp(cmd->args[0], "jobs") == 0) {
			/* Iterate pJobList and print *Running* and *Stopped* jobs. Each job state is updated in runCmd or job status check at end of main while loop.  */
			for (int i = 0; i < JOBS_MAX_SIZE; i++) {
				Cmd *job = pJobList[i];
				if (job != NULL) {
					/* Print job number, state, and command. */
					if (job->state == 1) {
						printf("[%d]\tRunning\t\t%s", i+1, job->line);
					} else if (job->state == 2) {
						printf("[%d]\tStopped\t\t%s", i+1, job->line);
					}
				}
			}
		} else if (strcmp(cmd->args[0], "bg") == 0) {
			/* Continue a stopped foreground process as background job. Validate job number and state (is Stopped). */
            int i = atoi(cmd->args[1]) - 1; // job_number - 1 == array_index
			Cmd *job = pJobList[i];
            if (job != NULL && job->state == 2) {
                kill(pJobList[i]->pid, SIGCONT);
				job->state = 1;
                printf("[%d]\tRunning\t\t%s\n", i+1, job->line);
            } else if (job != NULL && job->state == 1) {
				printf("Job [%d] is already running\n", i+1);
			} else { printf("Invalid Job Number\n"); }
		} else {
			/* Run basic commands (handles file redirection and pipe operators too) */
			runCmd(cmd);
		}

		/* 	Job status check -- A job can exit in one of three condition
			[1] gracefully (success) -- print done.
			[2] error -- print error code.
			[3] killed by user -- print terminated */
		for (int i = 0; i < JOBS_MAX_SIZE; i++) {
			if (pJobList[i] != NULL) {
				Cmd *job = pJobList[i];
				pid_t w = waitpid(job->pid, &(job->status), WNOHANG);
				if (w != 0) {
					/* (waitpid() != 0) implies new job state. Handle job completion and remove job. */
					int status = job->status;
					bool isRemove = false; // Ensure job is completed
					if (WIFEXITED(status)) {
						int exitCode = WEXITSTATUS(status);
						if (exitCode == 0) { 
							printf("[%d]\tDone\t%s\n", i+1, job->line); // success
						} else { 
							printf("[%d]\tExit %d\t%s\n", i+1, exitCode, *job->args); // exit with error
						}
						isRemove = true;
					} else if (WIFSIGNALED(status)) {
						printf("[%d]\tTerminated\t%s\n", i+1, job->line); // user kill
						isRemove = true; 
					}
					if (isRemove) {
						/* Remove from job list. */
						free(job);
						pJobList[i] = NULL;
						jobCount--;
					}
				}
			}
		}
	}
	return 0;
}
