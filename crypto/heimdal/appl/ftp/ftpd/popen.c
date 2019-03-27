/*
 * Copyright (c) 1988, 1993, 1994
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software written by Ken Arnold and
 * published in UNIX Review, Vol. 6, No. 8.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
RCSID("$Id$");
#endif

#include <sys/types.h>
#ifdef TIME_WITH_SYS_TIME
#include <sys/time.h>
#include <time.h>
#elif defined(HAVE_SYS_TIME_H)
#include <sys/time.h>
#else
#include <time.h>
#endif
#ifdef HAVE_SYS_RESOURCE_H
#include <sys/resource.h>
#endif
#include <sys/wait.h>

#include <errno.h>
#include <glob.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <roken.h>
#include "extern.h"


/*
 * Special version of popen which avoids call to shell.  This ensures
 * no one may create a pipe to a hidden program as a side effect of a
 * list or dir command.
 */
static int *pids;
static int fds;

/* return path prepended with ~ftp if that file exists, otherwise
 * return path unchanged
 */

const char *
ftp_rooted(const char *path)
{
    static char home[MaxPathLen] = "";
    static char newpath[MaxPathLen];
    struct passwd *pwd;

    if(!home[0])
	if((pwd = k_getpwnam("ftp")))
	    strlcpy(home, pwd->pw_dir, sizeof(home));
    snprintf(newpath, sizeof(newpath), "%s/%s", home, path);
    if(access(newpath, X_OK))
	strlcpy(newpath, path, sizeof(newpath));
    return newpath;
}


#define MAXARGS	100
#define MAXGLOBS 1000

FILE *
ftpd_popen(char *program, char *type, int do_stderr, int no_glob)
{
	char *cp;
	FILE *iop;
	int argc, gargc, pdes[2], pid;
	char **pop, *argv[MAXARGS], *gargv[MAXGLOBS];
	char *foo;

	if (strcmp(type, "r") && strcmp(type, "w"))
		return (NULL);

	if (!pids) {

	    /* This function is ugly and should be rewritten, in
	     * modern unices there is no such thing as a maximum
	     * filedescriptor.
	     */

	    fds = getdtablesize();
	    pids = (int*)calloc(fds, sizeof(int));
	    if(!pids)
		return NULL;
	}
	if (pipe(pdes) < 0)
		return (NULL);

	/* break up string into pieces */
	foo = NULL;
	for (argc = 0, cp = program; argc < MAXARGS - 1; cp = NULL) {
		if (!(argv[argc++] = strtok_r(cp, " \t\n", &foo)))
			break;
	}
	argv[MAXARGS - 1] = NULL;

	gargv[0] = (char*)ftp_rooted(argv[0]);
	/* glob each piece */
	for (gargc = argc = 1; argv[argc] && gargc < MAXGLOBS - 1; argc++) {
		glob_t gl;
		int flags = GLOB_BRACE|GLOB_NOCHECK|GLOB_QUOTE|GLOB_TILDE
		    |
#ifdef GLOB_MAXPATH
	GLOB_MAXPATH
#else
	GLOB_LIMIT
#endif
		    ;

		memset(&gl, 0, sizeof(gl));
		if (no_glob ||
		    glob(argv[argc], flags, NULL, &gl) ||
		    gl.gl_pathc == 0)
			gargv[gargc++] = strdup(argv[argc]);
		else
			for (pop = gl.gl_pathv;
			     *pop && gargc < MAXGLOBS - 1;
			     pop++)
				gargv[gargc++] = strdup(*pop);
		globfree(&gl);
	}
	gargv[gargc] = NULL;

	iop = NULL;
	switch(pid = fork()) {
	case -1:			/* error */
		close(pdes[0]);
		close(pdes[1]);
		goto pfree;
		/* NOTREACHED */
	case 0:				/* child */
		if (*type == 'r') {
			if (pdes[1] != STDOUT_FILENO) {
				dup2(pdes[1], STDOUT_FILENO);
				close(pdes[1]);
			}
			if(do_stderr)
			    dup2(STDOUT_FILENO, STDERR_FILENO);
			close(pdes[0]);
		} else {
			if (pdes[0] != STDIN_FILENO) {
				dup2(pdes[0], STDIN_FILENO);
				close(pdes[0]);
			}
			close(pdes[1]);
		}
		execv(gargv[0], gargv);
		gargv[0] = argv[0];
		execv(gargv[0], gargv);
		_exit(1);
	}
	/* parent; assume fdopen can't fail...  */
	if (*type == 'r') {
		iop = fdopen(pdes[0], type);
		close(pdes[1]);
	} else {
		iop = fdopen(pdes[1], type);
		close(pdes[0]);
	}
	pids[fileno(iop)] = pid;

pfree:
	for (argc = 1; gargv[argc] != NULL; argc++)
	    free(gargv[argc]);


	return (iop);
}

int
ftpd_pclose(FILE *iop)
{
	int fdes, status;
	pid_t pid;
	sigset_t sigset, osigset;

	/*
	 * pclose returns -1 if stream is not associated with a
	 * `popened' command, or, if already `pclosed'.
	 */
	if (pids == 0 || pids[fdes = fileno(iop)] == 0)
		return (-1);
	fclose(iop);
	sigemptyset(&sigset);
	sigaddset(&sigset, SIGINT);
	sigaddset(&sigset, SIGQUIT);
	sigaddset(&sigset, SIGHUP);
	sigprocmask(SIG_BLOCK, &sigset, &osigset);
	while ((pid = waitpid(pids[fdes], &status, 0)) < 0 && errno == EINTR)
		continue;
	sigprocmask(SIG_SETMASK, &osigset, NULL);
	pids[fdes] = 0;
	if (pid < 0)
		return (pid);
	if (WIFEXITED(status))
		return (WEXITSTATUS(status));
	return (1);
}
