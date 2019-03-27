/* popen.c: A "safe" pipe open routine.

%%% portions-copyright-cmetz-96
Portions of this software are Copyright 1996-1999 by Craig Metz, All Rights
Reserved. The Inner Net License Version 2 applies to these portions of
the software.
You should have received a copy of the license with this software. If
you didn't get a copy, you may request one from <license@inner.net>.

Portions of this software are Copyright 1995 by Randall Atkinson and Dan
McDonald, All Rights Reserved. All Rights under this copyright are assigned
to the U.S. Naval Research Laboratory (NRL). The NRL Copyright Notice and
License Agreement applies to this software.

	History:

	Modified by cmetz for OPIE 2.31. Merged in some 4.4BSD-Lite fixes.
	Modified by cmetz for OPIE 2.2. Use FUNCTION declaration et al.
                Removed useless string. ifdef around some headers.
        Modified at NRL for OPIE 2.1. Optimized for only one pipe at a time.
                Added minimal version of sigprocmask(). Moved some pid_t
		dancing to the config headers.
	Modified at NRL for OPIE 2.0.
	Originally from BSD.

$FreeBSD$
*/
/*
 * Copyright (c) 1988, 1993, 1994
 *     The Regents of the University of California.  All rights reserved.
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
 *      This product includes software developed by the University of
 *      California, Berkeley and its contributors.
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

#include "opie_cfg.h"

#include <sys/types.h>
#include <sys/wait.h>
#if HAVE_SIGNAL_H
#include <signal.h>
#endif /* HAVE_SIGNAL_H */
#if HAVE_SYS_SIGNAL_H
#include <sys/signal.h>
#endif /* HAVE_SYS_SIGNAL_H */
#if HAVE_UNISTD_H
#include <unistd.h>
#endif /* HAVE_UNISTD_H */
#include <stdio.h>
#if HAVE_STDLIB_H
#include <stdlib.h>
#endif /* HAVE_STDLIB_H */
#if HAVE_STRING_H
#include <string.h>
#endif /* HAVE_STRING_H */

#include "opie.h"

#define MAXUSRARGS	100
#define MAXGLOBARGS	1000

char **ftpglob __P((register char *));
char **copyblk __P((char **));
VOIDRET blkfree __P((char **));

/*
 * Special version of popen which avoids call to shell.  This ensures noone
 * may create a pipe to a hidden program as a side effect of a list or dir
 * command.
 */
static pid_t child_pid = -1;
static int pipe_fd;

extern char **environ;

FILE *ftpd_popen FUNCTION((program, type), char *program AND char *type)
{
  char *cp;
  FILE *iop;
  int argc, gargc, pdes[2];
  char **pop, *argv[MAXUSRARGS], *gargv[MAXGLOBARGS], *vv[2];

  if ((*type != 'r' && *type != 'w') || type[1])
    return (NULL);

  if (pipe(pdes) < 0)
    return (NULL);

  /* break up string into pieces */
  for (argc = 0, cp = program; argc < MAXUSRARGS-1; cp = NULL) {
    if (!(argv[argc++] = strtok(cp, " \t\n")))
      break;
  }
  argv[argc - 1] = NULL;

  /* glob each piece */
  gargv[0] = argv[0];
  for (gargc = argc = 1; argv[argc] && gargc < (MAXGLOBARGS-1); argc++) {
    if (!(pop = (char **) ftpglob(argv[argc]))) {
      /* globbing failed */
      vv[0] = argv[argc];
      vv[1] = NULL;
      pop = (char **) copyblk(vv);
    }
    argv[argc] = (char *) pop;	/* save to free later */
    while (*pop && gargc < MAXGLOBARGS-1)
      gargv[gargc++] = *pop++;
  }
  gargv[gargc] = NULL;

  iop = NULL;
  switch (child_pid = fork()) {
  case -1:	/* error */
    close(pdes[0]);
    close(pdes[1]);
    goto pfree;
    /* NOTREACHED */
  case 0:	/* child */
    if (*type == 'r') {
      if (pdes[1] != 1) {
	dup2(pdes[1], 1);
	dup2(pdes[1], 2);	/* stderr, too! */
	close(pdes[1]);
      }
      close(pdes[0]);
    } else {
      if (pdes[0] != 0) {
	dup2(pdes[0], 0);
	close(pdes[0]);
      }
      close(pdes[1]);
    }
    environ = NULL;
    execv(gargv[0], gargv);
    _exit(1);
  }

  /* parent; assume fdopen can't fail...  */
  if (*type == 'r') {
    iop = fdopen(pipe_fd = pdes[0], type);
    close(pdes[1]);
  } else {
    iop = fdopen(pipe_fd = pdes[1], type);
    close(pdes[0]);
  }

pfree: for (argc = 1; argv[argc] != NULL; argc++) {
    blkfree((char **) argv[argc]);
    free((char *) argv[argc]);
  }
  return (iop);
}

int ftpd_pclose FUNCTION((iop), FILE *iop)
{
  int status;
  pid_t pid;
  sigset_t omask, mask;

  sigemptyset(&mask);
  sigaddset(&mask, SIGINT);
  sigaddset(&mask, SIGQUIT);
  sigaddset(&mask, SIGHUP);

  /* pclose returns -1 if stream is not associated with a `popened' command,
     or, if already `pclosed'. */
  if ((child_pid < 0) || (fileno(iop) != pipe_fd))
    return (-1);

  fclose(iop);
  sigprocmask(SIG_BLOCK, &mask, &omask);

  while ((pid = wait(&status)) != child_pid && (pid != -1));
  sigprocmask(SIG_SETMASK, &omask, NULL);

  child_pid = -1;
  pipe_fd = -1;

#if defined(WEXITSTATUS) && defined(WIFEXITED)
  if ((pid > 0) && WIFEXITED(status))
    return WEXITSTATUS(status);

  return -1;
#else /* defined(WEXITSTATUS) && defined(WIFEXITED) */
  return (pid == -1 ? -1 : status.w_status);
#endif /* defined(WEXITSTATUS) && defined(WIFEXITED) */
}
