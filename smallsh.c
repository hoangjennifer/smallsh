#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <signal.h>
#include <errno.h>
#include <stdint.h>

#define MAX_ARGS 512
#define MAX_CHARS 2048

#define DEBUG

pid_t most_recent_bg_pid;
int last_fg_status = 0;

/* Function to perform tilde expansion */
void expand_tilde(char* args[MAX_ARGS], int argc) {
  for(int i = 0; i < argc; i++)
  {
    char *word = args[i];
    char *tild = NULL;
    if (strncmp(word, "~/", 2) == 0)
      tild = word;
    if (tild != NULL) {
        char* home_dir = getenv("HOME");
        if (home_dir != NULL) {
            memmove(tild + strlen(home_dir), tild+1, strlen(tild));
            memcpy(tild, home_dir, strlen(home_dir));
        }
        args[i] = tild;
    }
  }
}

/* Function to perform parameter expansion */
void expand_dollar(char* word) {
    char pid_str[16];
    snprintf(pid_str, sizeof(pid_str), "%jd", (intmax_t) getpid());
    char most_recent_bg_pid_str[120] = {0};
    if (most_recent_bg_pid != 0) {
        snprintf(pid_str, sizeof(pid_str), "%jd", (intmax_t)most_recent_bg_pid);
    }
    int pid_str_len = strlen(pid_str);
    int word_len = strlen(word);
    char new_word[MAX_CHARS];
    int new_word_len = 0;
    int i = 0;

    while (i < word_len) {
        if (word[i] == '$') {
            if (i + 1 < word_len) {
                if (word[i+1] == '$') {
                    memcpy(new_word + new_word_len, pid_str, pid_str_len);
                    new_word_len += pid_str_len;
                    i += 2;
                } else if (word[i+1] == '?') {
                    char status_str[16];
                    snprintf(status_str, sizeof(status_str), "%d", last_fg_status);
                    int status_str_len = strlen(status_str);
                    memcpy(new_word + new_word_len, status_str, status_str_len);
                    new_word_len += status_str_len;
                    i += 2;
                } else if (word[i+1] == '!') {
                  if (most_recent_bg_pid != 0) {
                    sprintf(most_recent_bg_pid_str, "%jd", (intmax_t) most_recent_bg_pid);
                  }
                  else {
                    sprintf(most_recent_bg_pid_str, "%s", "");
                  }
                    int most_recent_bg_pid_str_len = strlen(most_recent_bg_pid_str);
                    memcpy(new_word + new_word_len, most_recent_bg_pid_str, most_recent_bg_pid_str_len);
                    new_word_len += most_recent_bg_pid_str_len;
                    i += 2;
                } else {
                    new_word[new_word_len++] = word[i++];
                }
            } else {
                new_word[new_word_len++] = word[i++];
            }
        } else {
            new_word[new_word_len++] = word[i++];
        }
    }
    memcpy(word, new_word, new_word_len);
    word[new_word_len] = '\0';
}

int background_enabled = 0; // set to 1 when & is present

/* Signal handling*/
void signal_handler(int signo) {
  //printf("==SIG=> Catching %d\n", signo);
  if (signo == SIGINT) {
    //fprintf(stderr, "\n%s", getenv("PS1"));
  }
}

/* Built-in exit command */
void blt_exit(int st) {
      fprintf(stderr, "\nexit\n");
      exit(st);
}

/* Check for any background processes and print informative message */
void print_bg_status() {
    pid_t pid;
    int status;
    while ((pid = waitpid(-1, &status, WNOHANG | WUNTRACED | WCONTINUED)) > 0) {
        if (WIFEXITED(status)) {
            fprintf(stderr, "Child process %jd done. Exit status %d.\n", (intmax_t) pid, WEXITSTATUS(status));
        }
        else if (WIFSIGNALED(status)) {
            fprintf(stderr, "Child process %jd done. Signaled %d.\n", (intmax_t) pid, WTERMSIG(status));
        }
        else if (WIFSTOPPED(status)) {
            fprintf(stderr, "Child process %jd stopped. Continuing.\n", (intmax_t) pid);
            kill(pid, SIGCONT);
        }
    }
}

/* Words are parsed into tokens */
int parse_words(char **args, char * line,  int *background, char **infile, char **outfile)
{
    char *ifs = getenv("IFS");
    if (ifs == NULL)
       ifs = " \t\n";

    int num_args = 0;
    *background = 0;
    *infile = NULL;
    *outfile = NULL;
    char *command = strtok(line, ifs);

    while (command && num_args < MAX_ARGS - 1) {
      if(strcmp(command, "&") == 0) {
          *background = 1;
      }
      else if(strcmp("#", command) == 0) {
        break;
      }
      else {
        
        if (strcmp("<", command) == 0 || strcmp(">", command) == 0) {
          char *file = strtok(NULL, ifs);
          if(command == NULL) {
               return -1;
          }
          if (strcmp("<", command) == 0) {
            *infile = file; 
          }
          else {
            *outfile = file;
          }
        }
        else {
          args[num_args] = command;
          num_args++;
        }
      }
      command = strtok(NULL, ifs);
    }

    return num_args;
}

int main() {
  char *line = NULL;
  size_t line_capacity = 0;
  char *ps1 = getenv("PS1");

  signal(SIGINT, SIG_IGN);
 
  while (1) {
    // Check for any background processes and print informative message
    pid_t pid, wpid;
    int status;
    int background = 0;
    char *infile = NULL, *outfile = NULL;
    print_bg_status();

    // Print prompt
    fprintf(stderr, ps1);

    // Read command
    if (getline(&line, &line_capacity, stdin) == -1) {
    
      if (errno == EINTR) {
        // Reading was interrupted by a signal, so just print a new prompt and continue
        fprintf(stderr, "\n");
        clearerr(stdin);
        continue;
      } else {
        // Some other error occurred, so exit the shell
        //exit(EXIT_FAILURE);
        blt_exit(last_fg_status);
      }
    }

    signal(SIGINT, SIG_IGN);
    expand_dollar(line);

    // Tokenize command
    char *args[MAX_ARGS] = {NULL};

    int num_args = parse_words(args, line, &background, &infile, &outfile);
    if (num_args == 0) {
      // Empty command, so just print a new prompt and continue
      continue;
    }
    else if(num_args == -1) {
      fprintf(stderr,"Invalid command\n");
      continue;
    }

    args[num_args] = NULL;

    expand_tilde(args, num_args);

    // Execute command
    if (strcmp(args[0], "exit") == 0) {
      // Exit the shell
      if (num_args > 1) {
        int exit_st = atoi(args[1]);
        blt_exit(exit_st);
      }
      else {
        blt_exit(EXIT_SUCCESS);
      }
    } else if (strcmp(args[0], "cd") == 0) {
      // Change directory
      if (num_args > 2) {
        fprintf(stderr, "cd: too many arguments\n");
        continue;
      }
      if (args[1] != NULL)
        chdir(args[1]);
      else
        chdir(getenv("HOME"));
    } else {
      // Create child process to execute command
      pid_t pid = fork();

      if (pid == 0) {
        // Child process

        int in_fd, out_fd;

        // Input redirection
        if (infile != NULL) {
//            close(STDIN_FILENO);
            in_fd = open(infile, O_RDONLY);
            dup2(in_fd, STDIN_FILENO);
            close(in_fd);
        }

        // Output redirection
        if (outfile != NULL) {
            out_fd = open(outfile, O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
            dup2(out_fd, STDOUT_FILENO);
            close(out_fd);
        }

        signal(SIGINT, SIG_DFL);

        execvp(args[0], args);

        // If execvp returns, there was an error, so print error message and exit
        fprintf(stderr, "%s: command not found\n", args[0]);
        exit(EXIT_FAILURE);
      } else if (pid > 0) {
        // Parent process
        if (!background) {
          // Wait for child process to finish
          waitpid(pid, &status, 0);

          // Set exit status
          if (WIFEXITED(status)) {
            last_fg_status = WEXITSTATUS(status);
          }
        }
        else {
          most_recent_bg_pid = pid;        
        }
      }
    }
  }
}
