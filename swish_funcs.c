#define _GNU_SOURCE

#include "swish_funcs.h"

#include <assert.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "job_list.h"
#include "string_vector.h"

#define MAX_ARGS 10

int tokenize(char *s, strvec_t *tokens) {
    // TODO Task 0: Tokenize string s
    // Assume each token is separated by a single space (" ")
    // Use the strtok() function to accomplish this
    // Add each token to the 'tokens' parameter (a string vector)
    // Return 0 on success, -1 on error

    // initialize tokenization
    char *token = strtok(s, " ");

    // loop - continues until string is fully tokenized
    while (token != NULL) {
        if (strvec_add(tokens, token) == -1) {
            printf("Failed to add token");
            return -1;
        }
        token = strtok(NULL, " ");
    }
    return 0;
}

int run_command(strvec_t *tokens) {
    // TODO Task 2: Execute the specified program (token 0) with the
    // specified command-line arguments
    // THIS FUNCTION SHOULD BE CALLED FROM A CHILD OF THE MAIN SHELL PROCESS
    // Hint: Build a string array from the 'tokens' vector and pass this into execvp()
    // Another Hint: You have a guarantee of the longest possible needed array, so you
    // won't have to use malloc.

    // create backups for stdout and stdin - restore later
    int stdout_bak = dup(STDOUT_FILENO);
    int stdin_bak = dup(STDIN_FILENO);

    // flags for type output/input redirection - append to output, output+overwrite, input
    // redirection
    int out_flag = -1;
    int in_flag = -1;
    int append_flag = -1;

    // index in token for the input/output redirection
    int out_loc = 0;
    int in_loc = 0;
    // total number of arguments
    int num_args = 0;

    char *args[MAX_ARGS];
    for (int i = 0; i < (*tokens).length; i++) {
        // if tokens ends before reaching its length, error
        if (strvec_get(tokens, i) == NULL) {
            return -1;
        }

        // overwrite and redirect output file - set flags
        if (strcmp(strvec_get(tokens, i), ">") == 0) {
            out_loc = i;
            out_flag = 1;
            i++;
            // redirect input file - set flags
        } else if (strcmp(strvec_get(tokens, i), "<") == 0) {
            in_loc = i;
            in_flag = 1;
            i++;
            // append and redirect output file - set flags
        } else if (strcmp(strvec_get(tokens, i), ">>") == 0) {
            out_loc = i;
            out_flag = 1;
            append_flag = 1;
            i++;
        } else {
            // not a redirection argument - adds to command arguments
            args[i] = strvec_get(tokens, i);
            num_args++;
        }
    }
    // null terminated arguments
    args[num_args++] = NULL;

    // check for output redirection
    if (out_flag != -1) {
        int out_fd;
        // if not appending, truncate/create file, redirect output, and close
        if (append_flag == -1) {
            if ((out_fd = open(strvec_get(tokens, out_loc + 1), O_WRONLY | O_CREAT | O_TRUNC,
                               S_IRUSR | S_IWUSR)) == -1) {
                perror("Failed to open output file");
                return -1;
            }
            if (dup2(out_fd, STDOUT_FILENO) == -1) {
                close(out_fd);
                return -1;
            }
            if (close(out_fd) == -1) {
                dup2(stdout_bak, STDOUT_FILENO);
                return -1;
            }
            // if appending, open file, redirect output, and close
        } else {
            if ((out_fd = open(strvec_get(tokens, out_loc + 1), O_WRONLY | O_CREAT | O_APPEND,
                               S_IRUSR | S_IWUSR)) == -1) {
                perror("Failed to open output file");
                return -1;
            }

            if (dup2(out_fd, STDOUT_FILENO) == -1) {
                close(out_fd);
                return -1;
            }
            if (close(out_fd) == -1) {
                dup2(stdout_bak, STDOUT_FILENO);
                return -1;
            }
        }
    }
    // if redirecting input, open file for reading, redirect, and close
    int in_fd;
    if (in_flag != -1) {
        if ((in_fd = open(strvec_get(tokens, in_loc + 1), O_RDONLY, S_IRUSR)) == -1) {
            perror("Failed to open input file");
            if (out_flag != 1) {
                dup2(stdout_bak, STDOUT_FILENO);
            }
            return -1;
        }

        if (dup2(in_fd, STDIN_FILENO) == -1) {
            close(in_fd);
            if (out_flag != 1) {
                dup2(stdout_bak, STDOUT_FILENO);
            }
            return -1;
        }
        if (close(in_fd) == -1) {
            dup2(stdin_bak, STDIN_FILENO);
            if (out_flag != 1) {
                dup2(stdout_bak, STDOUT_FILENO);
            }
            return -1;
        }
    }

    // child process - make child own process group
    if (setpgid(0, getpid()) == -1) {
        return -1;
    }

    // set new mask for child process, with own mask
    struct sigaction sac;
    sac.sa_handler = SIG_DFL;
    if (sigfillset(&sac.sa_mask) == -1) {
        return -1;
    }
    sac.sa_flags = SA_RESTART;
    if (sigaction(SIGTTIN, &sac, NULL) == -1 || sigaction(SIGTTOU, &sac, NULL) == -1) {
        return -1;
    }

    // execute the command (with redirection executed prior)
    if (execvp(args[0], args) == -1) {
        perror("exec");
        return -1;
    }

    // TODO Task 3: Extend this function to perform output redirection before exec()'ing
    // Check for '<' (redirect input), '>' (redirect output), '>>' (redirect and append output)
    // entries inside of 'tokens' (the strvec_find() function will do this for you)
    // Open the necessary file for reading (<), writing (>), or appending (>>)
    // Use dup2() to redirect stdin (<), stdout (> or >>)
    // DO NOT pass redirection operators and file names to exec()'d program
    // E.g., "ls -l > out.txt" should be exec()'d with strings "ls", "-l", NULL

    // TODO Task 4: You need to do two items of setup before exec()'ing
    // 1. Restore the signal handlers for SIGTTOU and SIGTTIN to their defaults.
    // The code in main() within swish.c sets these handlers to the SIG_IGN value.
    // Adapt this code to use sigaction() to set the handlers to the SIG_DFL value.
    // 2. Change the process group of this process (a child of the main shell).
    // Call getpid() to get its process ID then call setpgid() and use this process
    // ID as the value for the new process group ID

    return 0;
}

int resume_job(strvec_t *tokens, job_list_t *jobs, int is_foreground) {
    job_t *temp_job;
    int status;
    if (is_foreground) {
        int job_id = atoi(strvec_get(tokens, 1));
        if (job_id < 0) {
            return -1;
        }
        temp_job = job_list_get(jobs, job_id);
        if (temp_job == NULL) {
            fprintf(stderr, "Job index out of bounds\n");
            return -1;
        }
        if (tcsetpgrp(STDIN_FILENO, temp_job->pid) == -1) {
            perror("tcsetpgrp");
            return -1;
        }
        if (kill(temp_job->pid, SIGCONT) == -1) {
            perror("kill");
            return -1;
        }
        if (waitpid(temp_job->pid, &status, WUNTRACED) == -1) {
            perror("wait failed");
            return -1;
        }
        if (WIFEXITED(status) || WIFSIGNALED(status)) {
            if (job_list_remove(jobs, job_id) == -1) {
                return -1;
            }
        }
        if (tcsetpgrp(STDIN_FILENO, getpid()) == -1) {
            perror("tcsetpgrp");
            return -1;
        }
    } else {
        int job_id = atoi(strvec_get(tokens, 1));
        if (job_id < 0) {
            return -1;
        }
        temp_job = job_list_get(jobs, job_id);
        if (temp_job == NULL) {
            fprintf(stderr, "Job index out of bounds\n");
            return -1;
        }
        temp_job->status = BACKGROUND;
        if (kill(temp_job->pid, SIGCONT) == -1) {
            perror("kill");
            return -1;
        }
    }
    return 0;
    // TODO Task 5: Implement the ability to resume stopped jobs in the foreground
    // 1. Look up the relevant job information (in a job_t) from the jobs list
    //    using the index supplied by the user (in tokens index 1)
    //    Feel free to use sscanf() or atoi() to convert this string to an int
    // 2. Call tcsetpgrp(STDIN_FILENO, <job_pid>) where job_pid is the job's process ID
    // 3. Send the process the SIGCONT signal with the kill() system call
    // 4. Use the same waitpid() logic as in main -- don't forget WUNTRACED
    // 5. If the job has terminated (not stopped), remove it from the 'jobs' list
    // 6. Call tcsetpgrp(STDIN_FILENO, <shell_pid>). shell_pid is the *current*
    //    process's pid, since we call this function from the main shell process

    // TODO Task 6: Implement the ability to resume stopped jobs in the background.
    // This really just means omitting some of the steps used to resume a job in the foreground:
    // 1. DO NOT call tcsetpgrp() to manipulate foreground/background terminal process group
    // 2. DO NOT call waitpid() to wait on the job
    // 3. Make sure to modify the 'status' field of the relevant job list entry to BACKGROUND
    //    (as it was STOPPED before this)
}

int await_background_job(strvec_t *tokens, job_list_t *jobs) {
    // TODO Task 6: Wait for a specific job to stop or terminate
    // 1. Look up the relevant job information (in a job_t) from the jobs list
    //    using the index supplied by the user (in tokens index 1)
    // 2. Make sure the job's status is BACKGROUND (no sense waiting for a stopped job)
    // 3. Use waitpid() to wait for the job to terminate, as you have in resume_job() and main().
    // 4. If the process terminates (is not stopped by a signal) remove it from the jobs list

    return 0;
}

int await_all_background_jobs(job_list_t *jobs) {
    // TODO Task 6: Wait for all background jobs to stop or terminate
    // 1. Iterate through the jobs list, ignoring any stopped jobs
    // 2. For a background job, call waitpid() with WUNTRACED.
    // 3. If the job has stopped (check with WIFSTOPPED), change its
    //    status to STOPPED. If the job has terminated, do nothing until the
    //    next step (don't attempt to remove it while iterating through the list).
    // 4. Remove all background jobs (which have all just terminated) from jobs list.
    //    Use the job_list_remove_by_status() function.

    return 0;
}
