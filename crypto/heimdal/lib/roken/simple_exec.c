/*
 * Copyright (c) 1998 - 2001, 2004 Kungliga Tekniska Högskolan
 * (Royal Institute of Technology, Stockholm, Sweden).
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the Institute nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE INSTITUTE AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE INSTITUTE OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <config.h>

#include <stdarg.h>
#include <stdlib.h>
#ifdef HAVE_SYS_TYPES_H
#include <sys/types.h>
#endif
#ifdef HAVE_SYS_WAIT_H
#include <sys/wait.h>
#endif
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
#include <errno.h>

#include "roken.h"

#define EX_NOEXEC	126
#define EX_NOTFOUND	127

/* return values:
   SE_E_UNSPECIFIED   on `unspecified' system errors
   SE_E_FORKFAILED    on fork failures
   SE_E_WAITPIDFAILED on waitpid errors
   SE_E_EXECTIMEOUT   exec timeout
   0-   is return value from subprocess
   SE_E_NOEXEC        if the program couldn't be executed
   SE_E_NOTFOUND      if the program couldn't be found
   128- is 128 + signal that killed subprocess

   possible values `func' can return:
   ((time_t)-2)		exit loop w/o killing child and return
   			`exec timeout'/-4 from simple_exec
   ((time_t)-1)		kill child with SIGTERM and wait for child to exit
   0			don't timeout again
   n			seconds to next timeout
   */

static int sig_alarm;

static RETSIGTYPE
sigtimeout(int sig)
{
    sig_alarm = 1;
    SIGRETURN(0);
}

ROKEN_LIB_FUNCTION int ROKEN_LIB_CALL
wait_for_process_timed(pid_t pid, time_t (*func)(void *),
		       void *ptr, time_t timeout)
{
    RETSIGTYPE (*old_func)(int sig) = NULL;
    unsigned int oldtime = 0;
    int ret;

    sig_alarm = 0;

    if (func) {
	old_func = signal(SIGALRM, sigtimeout);
	oldtime = alarm(timeout);
    }

    while(1) {
	int status;

	while(waitpid(pid, &status, 0) < 0) {
	    if (errno != EINTR) {
		ret = SE_E_WAITPIDFAILED;
		goto out;
	    }
	    if (func == NULL)
		continue;
	    if (sig_alarm == 0)
		continue;
	    timeout = (*func)(ptr);
	    if (timeout == (time_t)-1) {
		kill(pid, SIGTERM);
		continue;
	    } else if (timeout == (time_t)-2) {
		ret = SE_E_EXECTIMEOUT;
		goto out;
	    }
	    alarm(timeout);
	}
	if(WIFSTOPPED(status))
	    continue;
	if(WIFEXITED(status)) {
	    ret = WEXITSTATUS(status);
	    break;
	}
	if(WIFSIGNALED(status)) {
	    ret = WTERMSIG(status) + 128;
	    break;
	}
    }
 out:
    if (func) {
	signal(SIGALRM, old_func);
	alarm(oldtime);
    }
    return ret;
}

ROKEN_LIB_FUNCTION int ROKEN_LIB_CALL
wait_for_process(pid_t pid)
{
    return wait_for_process_timed(pid, NULL, NULL, 0);
}

ROKEN_LIB_FUNCTION int ROKEN_LIB_CALL
pipe_execv(FILE **stdin_fd, FILE **stdout_fd, FILE **stderr_fd,
	   const char *file, ...)
{
    int in_fd[2], out_fd[2], err_fd[2];
    pid_t pid;
    va_list ap;
    char **argv;

    if(stdin_fd != NULL)
	pipe(in_fd);
    if(stdout_fd != NULL)
	pipe(out_fd);
    if(stderr_fd != NULL)
	pipe(err_fd);
    pid = fork();
    switch(pid) {
    case 0:
	va_start(ap, file);
	argv = vstrcollect(&ap);
	va_end(ap);
	if(argv == NULL)
	    exit(-1);

	/* close pipes we're not interested in */
	if(stdin_fd != NULL)
	    close(in_fd[1]);
	if(stdout_fd != NULL)
	    close(out_fd[0]);
	if(stderr_fd != NULL)
	    close(err_fd[0]);

	/* pipe everything caller doesn't care about to /dev/null */
	if(stdin_fd == NULL)
	    in_fd[0] = open(_PATH_DEVNULL, O_RDONLY);
	if(stdout_fd == NULL)
	    out_fd[1] = open(_PATH_DEVNULL, O_WRONLY);
	if(stderr_fd == NULL)
	    err_fd[1] = open(_PATH_DEVNULL, O_WRONLY);

	/* move to proper descriptors */
	if(in_fd[0] != STDIN_FILENO) {
	    dup2(in_fd[0], STDIN_FILENO);
	    close(in_fd[0]);
	}
	if(out_fd[1] != STDOUT_FILENO) {
	    dup2(out_fd[1], STDOUT_FILENO);
	    close(out_fd[1]);
	}
	if(err_fd[1] != STDERR_FILENO) {
	    dup2(err_fd[1], STDERR_FILENO);
	    close(err_fd[1]);
	}

	closefrom(3);

	execv(file, argv);
	exit((errno == ENOENT) ? EX_NOTFOUND : EX_NOEXEC);
    case -1:
	if(stdin_fd != NULL) {
	    close(in_fd[0]);
	    close(in_fd[1]);
	}
	if(stdout_fd != NULL) {
	    close(out_fd[0]);
	    close(out_fd[1]);
	}
	if(stderr_fd != NULL) {
	    close(err_fd[0]);
	    close(err_fd[1]);
	}
	return SE_E_FORKFAILED;
    default:
	if(stdin_fd != NULL) {
	    close(in_fd[0]);
	    *stdin_fd = fdopen(in_fd[1], "w");
	}
	if(stdout_fd != NULL) {
	    close(out_fd[1]);
	    *stdout_fd = fdopen(out_fd[0], "r");
	}
	if(stderr_fd != NULL) {
	    close(err_fd[1]);
	    *stderr_fd = fdopen(err_fd[0], "r");
	}
    }
    return pid;
}

ROKEN_LIB_FUNCTION int ROKEN_LIB_CALL
simple_execvp_timed(const char *file, char *const args[],
		    time_t (*func)(void *), void *ptr, time_t timeout)
{
    pid_t pid = fork();
    switch(pid){
    case -1:
	return SE_E_FORKFAILED;
    case 0:
	execvp(file, args);
	exit((errno == ENOENT) ? EX_NOTFOUND : EX_NOEXEC);
    default:
	return wait_for_process_timed(pid, func, ptr, timeout);
    }
}

ROKEN_LIB_FUNCTION int ROKEN_LIB_CALL
simple_execvp(const char *file, char *const args[])
{
    return simple_execvp_timed(file, args, NULL, NULL, 0);
}

/* gee, I'd like a execvpe */
ROKEN_LIB_FUNCTION int ROKEN_LIB_CALL
simple_execve_timed(const char *file, char *const args[], char *const envp[],
		    time_t (*func)(void *), void *ptr, time_t timeout)
{
    pid_t pid = fork();
    switch(pid){
    case -1:
	return SE_E_FORKFAILED;
    case 0:
	execve(file, args, envp);
	exit((errno == ENOENT) ? EX_NOTFOUND : EX_NOEXEC);
    default:
	return wait_for_process_timed(pid, func, ptr, timeout);
    }
}

ROKEN_LIB_FUNCTION int ROKEN_LIB_CALL
simple_execve(const char *file, char *const args[], char *const envp[])
{
    return simple_execve_timed(file, args, envp, NULL, NULL, 0);
}

ROKEN_LIB_FUNCTION int ROKEN_LIB_CALL
simple_execlp(const char *file, ...)
{
    va_list ap;
    char **argv;
    int ret;

    va_start(ap, file);
    argv = vstrcollect(&ap);
    va_end(ap);
    if(argv == NULL)
	return SE_E_UNSPECIFIED;
    ret = simple_execvp(file, argv);
    free(argv);
    return ret;
}

ROKEN_LIB_FUNCTION int ROKEN_LIB_CALL
simple_execle(const char *file, ... /* ,char *const envp[] */)
{
    va_list ap;
    char **argv;
    char *const* envp;
    int ret;

    va_start(ap, file);
    argv = vstrcollect(&ap);
    envp = va_arg(ap, char **);
    va_end(ap);
    if(argv == NULL)
	return SE_E_UNSPECIFIED;
    ret = simple_execve(file, argv, envp);
    free(argv);
    return ret;
}
