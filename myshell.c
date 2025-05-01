#include <stdio.h>
#include "LineParser.h"
#include <linux/limits.h>
#include <stdlib.h>       
#include <unistd.h>       
#include <sys/types.h>    
#include <sys/wait.h>     
#include <string.h>       
#include <fcntl.h> 
#include <ctype.h>
#define HISTLEN 20
#define TERMINATED -1
#define RUNNING 1
#define SUSPENDED 0

typedef struct process {
    cmdLine* cmd;            
    pid_t pid;               
    int status;              
    struct process* next;    
} process;



void addProcess(process** process_list, cmdLine* cmd, pid_t pid) {
    process* new_process = (process*)malloc(sizeof(process));
    if (!new_process) {
        perror("malloc");
        exit(EXIT_FAILURE);
    }
    
    new_process->cmd = cmd;
    new_process->pid = pid;
    new_process->status = RUNNING;  //  default
    new_process->next = NULL;

    if (*process_list == NULL) {
        *process_list = new_process; // list was empty
    } else {
        process* current = *process_list;
        while (current->next != NULL) {
            current = current->next;
        }
        current->next = new_process; // append to the end
    }
}

void removeTerminatedProcesses(process** process_list) {
    process* current = *process_list;
    process* prev = NULL;

    while (current != NULL) {
        if (current->status == TERMINATED) {
            process* toDelete = current;
            if (prev == NULL) {
                // deleting head
                *process_list = current->next;
                current = *process_list;
            }
             else {
                prev->next = current->next;
                current = prev->next;
            }
            // Free safely
            if (toDelete->cmd != NULL)
                freeCmdLines(toDelete->cmd);
            free(toDelete);
            
        } else {
            prev = current;
            current = current->next;
        }
    }
}

void printProcessList(process** process_list) {
    process* current = *process_list;
    printf("PID          Command      STATUS\n");

    while (current != NULL) {
        char* status_str;
        if (current->status == TERMINATED) {
            status_str = "Terminated";
        } else if (current->status == RUNNING) {
            status_str = "Running";
        } else {
            status_str = "Suspended";
        }
        printf("%-12d %-12s %-12s\n", current->pid, current->cmd->arguments[0], status_str);
        current = current->next;
    }
}

void freeProcessList(process* process_list) {
    process* current = process_list;
    while (current != NULL) {
        process* temp = current;
        current = current->next;
        free(temp->cmd);
        free(temp);
    }
}

void updateProcessList(process** process_list) {
    process* current = *process_list;
    while (current != NULL) {
        int status;
        pid_t pid = waitpid(current->pid, &status, WNOHANG);

        if (pid == -1) {
            current = current->next;
            continue;
        }

        if (pid == 0) {
            current = current->next;
            continue;
        }

        if (WIFEXITED(status)) {
            current->status = TERMINATED;
        } else if (WIFSIGNALED(status)) {
            current->status = TERMINATED;
        } else if (WIFSTOPPED(status)) {
            current->status = SUSPENDED;
        }
          else if (WIFCONTINUED(status)) {
            current->status = RUNNING;
            }
        

        current = current->next;
    }
}

void updateProcessStatus(process* process_list, int pid, int status) {
    process* current = process_list;
    while (current != NULL) {
        if (current->pid == pid) {
            current->status = status;
            return;
        }
        current = current->next;
    }
}

int sendSignalToProcess(pid_t pid, int signal) {
    if (kill(pid, signal) == -1) {
        perror("kill");
        return -1;
    }
    return 0;
}



void handleProcsCommand(process** process_list) {
    updateProcessList(process_list);
    printProcessList(process_list);
    removeTerminatedProcesses(process_list);

}


int debug=0;


void changeDirectory(cmdLine *pCmdLine) {
    if (pCmdLine->arguments[1] == NULL) {
        fprintf(stderr, "cd: missing argument\n");
        return;
    }

    if (chdir(pCmdLine->arguments[1]) == -1) {
        perror("cd failed");
    }
}

void execute(cmdLine *pCmdLine, process** process_list) {

    if (strcmp(pCmdLine->arguments[0], "cd") == 0) {
        changeDirectory(pCmdLine);
        return;
    }

    

    if (strcmp(pCmdLine->arguments[0], "halt") == 0 && pCmdLine->argCount == 2) {
        int pid = atoi(pCmdLine->arguments[1]);
        if (sendSignalToProcess(pid, SIGTSTP) == 0) {
            updateProcessStatus(*process_list, pid, SUSPENDED);
            printf("Process %d suspended.\n", pid);
    }  
        return;
    }
     if (strcmp(pCmdLine->arguments[0], "wakeup") == 0 && pCmdLine->argCount == 2) {
        int pid = atoi(pCmdLine->arguments[1]);
        if (sendSignalToProcess(pid, SIGCONT) == 0) {
        updateProcessStatus(*process_list, pid, RUNNING);
        printf("Process %d resumed.\n", pid);
    }  

        return;
    }

    if (strcmp(pCmdLine->arguments[0], "ice") == 0 && pCmdLine->argCount == 2) {
        int pid = atoi(pCmdLine->arguments[1]);
        if (sendSignalToProcess(pid, SIGINT) == 0) {
            updateProcessStatus(*process_list, pid, TERMINATED);
            printf("Process %d terminated.\n", pid);
    } 

        return;
    }

    if (strcmp(pCmdLine->arguments[0], "procs") == 0) {
        handleProcsCommand(process_list);  
        return;
    }

    pid_t pid = fork();

    if (pid == -1) {
        perror("fork failed");
        exit(1);
    } else if (pid == 0) {
        // Child process
        
        if (debug){
            fprintf(stderr, "PID: %d\n", getpid());  // Print child PID
            fprintf(stderr, "Executing command: %s\n", pCmdLine->arguments[0]);
        }
        if (pCmdLine->inputRedirect){
            int fd_in = open(pCmdLine->inputRedirect,O_RDONLY);
            if (fd_in<0){
                perror("open inputRedirect\n");
                _exit(1);
            }
            // makes stdin read from fd_in
            if (dup2(fd_in,STDIN_FILENO)<0){
                perror("open inputRedirect");
                _exit(1);
            }
            
            close(fd_in);
        }


        if (pCmdLine->outputRedirect) {
            // O_CREAT- create the file if doesnt exist
            int fd_out = open(pCmdLine->outputRedirect,O_CREAT | O_WRONLY | O_TRUNC);
            if (fd_out < 0) {
                perror("open outputRedirect");
                _exit(1);
            }
            if (dup2(fd_out, STDOUT_FILENO) < 0) {
                perror("dup2 outputRedirect");
                _exit(1);
                 }
            close(fd_out);
        }
        if (execvp(pCmdLine->arguments[0], pCmdLine->arguments) == -1) {
            perror("execvp failed");
            _exit(1);  // if execvp fails
        }
    } else {  // in parent process
        addProcess(process_list, pCmdLine, pid);
        if (debug) {
            fprintf(stderr, "Parent PID: %d\n", getpid());  // Print parent PID
        }

        // wait for the child process to complete if blocking is true
         if (pCmdLine->blocking) {
             int status;
            waitpid(pid, &status, 0);  // 0- Wait for child to finish
        } else {
            // keep running in background
            if (debug) {
                fprintf(stderr, "Running in background...\n");
            }
        }
    }
}

void execute_pipeline(cmdLine *first_cmd, cmdLine *second_cmd,process** process_list ) {
    if (first_cmd->outputRedirect != NULL) {
        fprintf(stderr, "Error: Cannot redirect output of the left-hand-side command in a pipeline.\n");
        return;
}
    if (second_cmd->inputRedirect != NULL) {
        fprintf(stderr, "Error: Cannot redirect input of the right-hand-side command in a pipeline.\n");
        return;
}
    
    int pipe_fd[2];

    if (pipe(pipe_fd) == -1) {
        perror("pipe");
        freeCmdLines(first_cmd);
        freeCmdLines(second_cmd);
        exit(EXIT_FAILURE);
    }
    pid_t pid1 = fork();
    if (pid1 == -1) {
        perror("fork");
        exit(EXIT_FAILURE);
    }

    if (pid1 == 0) {

        fprintf(stderr, "(child1>redirecting stdout to the write end of the pipe...)\n");
        close(STDOUT_FILENO);             
        dup(pipe_fd[1]);                    // duplicate write end of pipe to stdout
        close(pipe_fd[0]);                  
        close(pipe_fd[1]);                 
        fprintf(stderr, "(child1>going to execute cmd: %s)\n", first_cmd->arguments[0]);
        execvp(first_cmd->arguments[0], first_cmd->arguments);
        perror("execvp");
        exit(EXIT_FAILURE);
    }
    // Parent process
    fprintf(stderr, "(parent_process>created process with id: %d)\n", pid1);

    fprintf(stderr, "(parent_process>closing the write end of the pipe...)\n");
    close(pipe_fd[1]); 

    fprintf(stderr, "(parent_process>forking...)\n");
    pid_t pid2 = fork();

    if (pid2 == -1) {
        perror("fork");
        exit(EXIT_FAILURE);
    }

    if (pid2 == 0) {
        // Child 2
        fprintf(stderr, "(child2>redirecting stdin from the read end of the pipe...)\n");
        close(STDIN_FILENO);               
        dup(pipe_fd[0]);                    // duplicate read end of pipe to stdin
        close(pipe_fd[0]);                  
        fprintf(stderr, "(child2>going to execute cmd: %s)\n",second_cmd->arguments[0]);
        execvp(second_cmd->arguments[0], second_cmd->arguments);
        perror("execvp");
        exit(EXIT_FAILURE);  
    }
        // parent: Close both ends of the pipe and wait for both children
        addProcess(process_list, first_cmd, pid1);
        addProcess(process_list, second_cmd, pid2);
        fprintf(stderr, "(parent_process>created process with id: %d)\n", pid2);
        fprintf(stderr, "(parent_process>closing the read end of the pipe...)\n");
        close(pipe_fd[0]); 
        close(pipe_fd[1]);
        fprintf(stderr, "(parent_process>waiting for child processes to terminate...)\n");
        waitpid(pid1, NULL, 0);
        waitpid(pid2, NULL, 0);
        
        fprintf(stderr, "(parent_process>exiting...)\n");
    }

    void executeCommandLine(cmdLine *pCmdLine, process** process_list) {
    if (pCmdLine->next != NULL) {
        // Pipe case
        execute_pipeline(pCmdLine,pCmdLine->next, process_list);
    } else
     {
        execute(pCmdLine,  process_list);    // no pipe, just execute normally   
    }
}
//PART4
static char* history[HISTLEN]; //init the history array
static int history_start=0; //place of the latest hist command
static int history_count=0; //command count
void add_history(const char* cmd){ //adding history to the list
    char *copy=strdup(cmd); // duplicating the cmd
    if (!copy){// does the dup succeed?
        perror("strdup failed");
        return;
    }
    if (history_count<HISTLEN){
        history[(history_start+history_count)%HISTLEN]=copy;
        history_count++;
    }
    else{
        free(history[history_start]);
        history[history_start]=copy;
        history_start++;
        history_start=history_start%HISTLEN;
    }
}
void printHistory(){ // traverse over history array and print each command
    for (int i=0;i<history_count;i++){
        printf ("%2d %s\n",i+1,history[(history_start+i)%HISTLEN]);
    }
}
char *get_history_entry(int n){ // PRINT CMD IN N PLACE
    if(n<1 || n>history_count){
        return NULL;
    }
    return history[(history_start+n-1)%HISTLEN];
}
void freeHistory(){
    for (int i=0;i<history_count;i++){
        free (history[(history_start+i)%HISTLEN]);
    }
}
int main (int argc, char **argv){
    char inputBuffer[2048];
    char cwd[PATH_MAX];
    cmdLine *line;
    process* process_list = NULL;

    while (1){
        updateProcessList(&process_list);
        if (getcwd(cwd, sizeof(cwd)) != NULL) {
            printf("%s> ", cwd);
        } 
        else {
            perror("getcwd error");
            exit(1);
        }
        for (int i = 1; i < argc; i++) {
            if (strcmp(argv[i], "-d") == 0) {
                debug = 1;
                break;
            }
        }
        // read user input
        if (fgets(inputBuffer, sizeof(inputBuffer), stdin) == NULL) {
            break; // EOF or error
        }

        inputBuffer[strcspn(inputBuffer,"\n")]=0;// cut the \n
        if (strcmp(inputBuffer,"hist")==0){
            printHistory();
            continue;
        }
        if (strcmp(inputBuffer,"!!")==0){
            if (history_count==0){
                printf("no commands");
                continue;
            }
            char* lastCommand=get_history_entry(history_count);
            printf("%s\n",lastCommand);
            strcpy(inputBuffer,lastCommand);
        }
        else if (inputBuffer[0]=='!' && isdigit((unsigned char)inputBuffer[1])){
            int num = atoi(inputBuffer+1);
            char *selectedCommand=get_history_entry(num);
            if (!selectedCommand){
                perror("no command");
                continue;
            }
            printf("%s\n",selectedCommand);
            strcpy(inputBuffer,selectedCommand);
        }
        else{
            add_history(inputBuffer);
        }
        // parse input line
        line = parseCmdLines(inputBuffer);
        if (!line){
            continue;
        }
        if (strcmp(line->arguments[0], "quit") == 0) {
            freeCmdLines(line);
            break;
        }
        executeCommandLine(line, &process_list);

    }
     freeProcessList(process_list);
       

    return 0;

}