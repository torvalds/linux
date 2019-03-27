/*-
 * Copyright (c) 1992, 1993, 1994
 *	The Regents of the University of California.  All rights reserved.
 * Copyright (c) 1992, 1993, 1994, 1995, 1996
 *	Keith Bostic.  All rights reserved.
 *
 * See the LICENSE file for redistribution information.
 */

#include "config.h"

#ifndef lint
static const char sccsid[] = "$Id: ex_shell.c,v 10.44 2012/07/06 06:51:26 zy Exp $";
#endif /* not lint */

#include <sys/queue.h>
#include <sys/time.h>
#include <sys/wait.h>

#include <bitstring.h>
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../common/common.h"

static const char *sigmsg(int);

/*
 * ex_shell -- :sh[ell]
 *	Invoke the program named in the SHELL environment variable
 *	with the argument -i.
 *
 * PUBLIC: int ex_shell(SCR *, EXCMD *);
 */
int
ex_shell(SCR *sp, EXCMD *cmdp)
{
	int rval;
	char *buf;

	/* We'll need a shell. */
	if (opts_empty(sp, O_SHELL, 0))
		return (1);

	/*
	 * XXX
	 * Assumes all shells use -i.
	 */
	(void)asprintf(&buf, "%s -i", O_STR(sp, O_SHELL));
	if (buf == NULL) {
		msgq(sp, M_SYSERR, NULL);
		return (1);
	}

	/* Restore the window name. */
	(void)sp->gp->scr_rename(sp, NULL, 0);

	/* If we're still in a vi screen, move out explicitly. */
	rval = ex_exec_proc(sp, cmdp, buf, NULL, !F_ISSET(sp, SC_SCR_EXWROTE));
	free(buf);

	/* Set the window name. */
	(void)sp->gp->scr_rename(sp, sp->frp->name, 1);

	/*
	 * !!!
	 * Historically, vi didn't require a continue message after the
	 * return of the shell.  Match it.
	 */
	F_SET(sp, SC_EX_WAIT_NO);

	return (rval);
}

/*
 * ex_exec_proc --
 *	Run a separate process.
 *
 * PUBLIC: int ex_exec_proc(SCR *, EXCMD *, char *, const char *, int);
 */
int
ex_exec_proc(SCR *sp, EXCMD *cmdp, char *cmd, const char *msg, int need_newline)
{
	GS *gp;
	const char *name;
	pid_t pid;

	gp = sp->gp;

	/* We'll need a shell. */
	if (opts_empty(sp, O_SHELL, 0))
		return (1);

	/* Enter ex mode. */
	if (F_ISSET(sp, SC_VI)) {
		if (gp->scr_screen(sp, SC_EX)) {
			ex_wemsg(sp, cmdp->cmd->name, EXM_NOCANON);
			return (1);
		}
		(void)gp->scr_attr(sp, SA_ALTERNATE, 0);
		F_SET(sp, SC_SCR_EX | SC_SCR_EXWROTE);
	}

	/* Put out additional newline, message. */
	if (need_newline)
		(void)ex_puts(sp, "\n");
	if (msg != NULL) {
		(void)ex_puts(sp, msg);
		(void)ex_puts(sp, "\n");
	}
	(void)ex_fflush(sp);

	switch (pid = vfork()) {
	case -1:			/* Error. */
		msgq(sp, M_SYSERR, "vfork");
		return (1);
	case 0:				/* Utility. */
		if (gp->scr_child)
			gp->scr_child(sp);
		if ((name = strrchr(O_STR(sp, O_SHELL), '/')) == NULL)
			name = O_STR(sp, O_SHELL);
		else
			++name;
		execl(O_STR(sp, O_SHELL), name, "-c", cmd, (char *)NULL);
		msgq_str(sp, M_SYSERR, O_STR(sp, O_SHELL), "execl: %s");
		_exit(127);
		/* NOTREACHED */
	default:			/* Parent. */
		return (proc_wait(sp, (long)pid, cmd, 0, 0));
	}
	/* NOTREACHED */
}

/*
 * proc_wait --
 *	Wait for one of the processes.
 *
 * !!!
 * The pid_t type varies in size from a short to a long depending on the
 * system.  It has to be cast into something or the standard promotion
 * rules get you.  I'm using a long based on the belief that nobody is
 * going to make it unsigned and it's unlikely to be a quad.
 *
 * PUBLIC: int proc_wait(SCR *, long, const char *, int, int);
 */
int
proc_wait(SCR *sp, long int pid, const char *cmd, int silent, int okpipe)
{
	size_t len;
	int nf, pstat;
	char *p;

	/* Wait for the utility, ignoring interruptions. */
	for (;;) {
		errno = 0;
		if (waitpid((pid_t)pid, &pstat, 0) != -1)
			break;
		if (errno != EINTR) {
			msgq(sp, M_SYSERR, "waitpid");
			return (1);
		}
	}

	/*
	 * Display the utility's exit status.  Ignore SIGPIPE from the
	 * parent-writer, as that only means that the utility chose to
	 * exit before reading all of its input.
	 */
	if (WIFSIGNALED(pstat) && (!okpipe || WTERMSIG(pstat) != SIGPIPE)) {
		for (; cmdskip(*cmd); ++cmd);
		p = msg_print(sp, cmd, &nf);
		len = strlen(p);
		msgq(sp, M_ERR, "%.*s%s: received signal: %s%s",
		    (int)MIN(len, 20), p, len > 20 ? " ..." : "",
		    sigmsg(WTERMSIG(pstat)),
		    WCOREDUMP(pstat) ? "; core dumped" : "");
		if (nf)
			FREE_SPACE(sp, p, 0);
		return (1);
	}

	if (WIFEXITED(pstat) && WEXITSTATUS(pstat)) {
		/*
		 * Remain silent for "normal" errors when doing shell file
		 * name expansions, they almost certainly indicate nothing
		 * more than a failure to match.
		 *
		 * Remain silent for vi read filter errors.  It's historic
		 * practice.
		 */
		if (!silent) {
			for (; cmdskip(*cmd); ++cmd);
			p = msg_print(sp, cmd, &nf);
			len = strlen(p);
			msgq(sp, M_ERR, "%.*s%s: exited with status %d",
			    (int)MIN(len, 20), p, len > 20 ? " ..." : "",
			    WEXITSTATUS(pstat));
			if (nf)
				FREE_SPACE(sp, p, 0);
		}
		return (1);
	}
	return (0);
}

/*
 * sigmsg --
 * 	Return a pointer to a message describing a signal.
 */
static const char *
sigmsg(int signo)
{
	static char buf[40];
	char *message;

	/* POSIX.1-2008 leaves strsignal(3)'s return value unspecified. */
	if ((message = strsignal(signo)) != NULL)
		return message;
	(void)snprintf(buf, sizeof(buf), "Unknown signal: %d", signo);
	return (buf);
}
