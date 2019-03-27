/*-
 * Copyright (c) 1991, 1993, 1994
 *	The Regents of the University of California.  All rights reserved.
 * Copyright (c) 1991, 1993, 1994, 1995, 1996
 *	Keith Bostic.  All rights reserved.
 *
 * See the LICENSE file for redistribution information.
 */

#include "config.h"

#ifndef lint
static const char sccsid[] = "$Id: ex_filter.c,v 10.44 2003/11/05 17:11:54 skimo Exp $";
#endif /* not lint */

#include <sys/types.h>
#include <sys/queue.h>

#include <bitstring.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../common/common.h"

static int filter_ldisplay(SCR *, FILE *);

/*
 * ex_filter --
 *	Run a range of lines through a filter utility and optionally
 *	replace the original text with the stdout/stderr output of
 *	the utility.
 *
 * PUBLIC: int ex_filter(SCR *, 
 * PUBLIC:    EXCMD *, MARK *, MARK *, MARK *, CHAR_T *, enum filtertype);
 */
int
ex_filter(SCR *sp, EXCMD *cmdp, MARK *fm, MARK *tm, MARK *rp, CHAR_T *cmd, enum filtertype ftype)
{
	FILE *ifp, *ofp;
	pid_t parent_writer_pid, utility_pid;
	recno_t nread;
	int input[2], output[2], rval;
	char *name;
	char *np;
	size_t nlen;

	rval = 0;

	/* Set return cursor position, which is never less than line 1. */
	*rp = *fm;
	if (rp->lno == 0)
		rp->lno = 1;

	/* We're going to need a shell. */
	if (opts_empty(sp, O_SHELL, 0))
		return (1);

	/*
	 * There are three different processes running through this code.
	 * They are the utility, the parent-writer and the parent-reader.
	 * The parent-writer is the process that writes from the file to
	 * the utility, the parent reader is the process that reads from
	 * the utility.
	 *
	 * Input and output are named from the utility's point of view.
	 * The utility reads from input[0] and the parent(s) write to
	 * input[1].  The parent(s) read from output[0] and the utility
	 * writes to output[1].
	 *
	 * !!!
	 * Historically, in the FILTER_READ case, the utility reads from
	 * the terminal (e.g. :r! cat works).  Otherwise open up utility
	 * input pipe.
	 */
	ofp = NULL;
	input[0] = input[1] = output[0] = output[1] = -1;
	if (ftype != FILTER_READ && pipe(input) < 0) {
		msgq(sp, M_SYSERR, "pipe");
		goto err;
	}

	/* Open up utility output pipe. */
	if (pipe(output) < 0) {
		msgq(sp, M_SYSERR, "pipe");
		goto err;
	}
	if ((ofp = fdopen(output[0], "r")) == NULL) {
		msgq(sp, M_SYSERR, "fdopen");
		goto err;
	}

	/* Fork off the utility process. */
	switch (utility_pid = vfork()) {
	case -1:			/* Error. */
		msgq(sp, M_SYSERR, "vfork");
err:		if (input[0] != -1)
			(void)close(input[0]);
		if (input[1] != -1)
			(void)close(input[1]);
		if (ofp != NULL)
			(void)fclose(ofp);
		else if (output[0] != -1)
			(void)close(output[0]);
		if (output[1] != -1)
			(void)close(output[1]);
		return (1);
	case 0:				/* Utility. */
		/*
		 * Redirect stdin from the read end of the input pipe, and
		 * redirect stdout/stderr to the write end of the output pipe.
		 *
		 * !!!
		 * Historically, ex only directed stdout into the input pipe,
		 * letting stderr come out on the terminal as usual.  Vi did
		 * not, directing both stdout and stderr into the input pipe.
		 * We match that practice in both ex and vi for consistency.
		 */
		if (input[0] != -1)
			(void)dup2(input[0], STDIN_FILENO);
		(void)dup2(output[1], STDOUT_FILENO);
		(void)dup2(output[1], STDERR_FILENO);

		/* Close the utility's file descriptors. */
		if (input[0] != -1)
			(void)close(input[0]);
		if (input[1] != -1)
			(void)close(input[1]);
		(void)close(output[0]);
		(void)close(output[1]);

		if ((name = strrchr(O_STR(sp, O_SHELL), '/')) == NULL)
			name = O_STR(sp, O_SHELL);
		else
			++name;

		INT2CHAR(sp, cmd, STRLEN(cmd)+1, np, nlen);
		execl(O_STR(sp, O_SHELL), name, "-c", np, (char *)NULL);
		msgq_str(sp, M_SYSERR, O_STR(sp, O_SHELL), "execl: %s");
		_exit (127);
		/* NOTREACHED */
	default:			/* Parent-reader, parent-writer. */
		/* Close the pipe ends neither parent will use. */
		if (input[0] != -1)
			(void)close(input[0]);
		(void)close(output[1]);
		break;
	}

	/*
	 * FILTER_RBANG, FILTER_READ:
	 *
	 * Reading is the simple case -- we don't need a parent writer,
	 * so the parent reads the output from the read end of the output
	 * pipe until it finishes, then waits for the child.  Ex_readfp
	 * appends to the MARK, and closes ofp.
	 *
	 * For FILTER_RBANG, there is nothing to write to the utility.
	 * Make sure it doesn't wait forever by closing its standard
	 * input.
	 *
	 * !!!
	 * Set the return cursor to the last line read in for FILTER_READ.
	 * Historically, this behaves differently from ":r file" command,
	 * which leaves the cursor at the first line read in.  Check to
	 * make sure that it's not past EOF because we were reading into an
	 * empty file.
	 */
	if (ftype == FILTER_RBANG || ftype == FILTER_READ) {
		if (ftype == FILTER_RBANG)
			(void)close(input[1]);

		if (ex_readfp(sp, "filter", ofp, fm, &nread, 1))
			rval = 1;
		sp->rptlines[L_ADDED] += nread;
		if (ftype == FILTER_READ)
			if (fm->lno == 0)
				rp->lno = nread;
			else
				rp->lno += nread;
		goto uwait;
	}

	/*
	 * FILTER_BANG, FILTER_WRITE
	 *
	 * Here we need both a reader and a writer.  Temporary files are
	 * expensive and we'd like to avoid disk I/O.  Using pipes has the
	 * obvious starvation conditions.  It's done as follows:
	 *
	 *	fork
	 *	child
	 *		write lines out
	 *		exit
	 *	parent
	 *		FILTER_BANG:
	 *			read lines into the file
	 *			delete old lines
	 *		FILTER_WRITE
	 *			read and display lines
	 *		wait for child
	 *
	 * XXX
	 * We get away without locking the underlying database because we know
	 * that none of the records that we're reading will be modified until
	 * after we've read them.  This depends on the fact that the current
	 * B+tree implementation doesn't balance pages or similar things when
	 * it inserts new records.  When the DB code has locking, we should
	 * treat vi as if it were multiple applications sharing a database, and
	 * do the required locking.  If necessary a work-around would be to do
	 * explicit locking in the line.c:db_get() code, based on the flag set
	 * here.
	 */
	F_SET(sp->ep, F_MULTILOCK);
	switch (parent_writer_pid = fork()) {
	case -1:			/* Error. */
		msgq(sp, M_SYSERR, "fork");
		(void)close(input[1]);
		(void)close(output[0]);
		rval = 1;
		break;
	case 0:				/* Parent-writer. */
		/*
		 * Write the selected lines to the write end of the input
		 * pipe.  This instance of ifp is closed by ex_writefp.
		 */
		(void)close(output[0]);
		if ((ifp = fdopen(input[1], "w")) == NULL)
			_exit (1);
		_exit(ex_writefp(sp, "filter", ifp, fm, tm, NULL, NULL, 1));

		/* NOTREACHED */
	default:			/* Parent-reader. */
		(void)close(input[1]);
		if (ftype == FILTER_WRITE) {
			/*
			 * Read the output from the read end of the output
			 * pipe and display it.  Filter_ldisplay closes ofp.
			 */
			if (filter_ldisplay(sp, ofp))
				rval = 1;
		} else {
			/*
			 * Read the output from the read end of the output
			 * pipe.  Ex_readfp appends to the MARK and closes
			 * ofp.
			 */
			if (ex_readfp(sp, "filter", ofp, tm, &nread, 1))
				rval = 1;
			sp->rptlines[L_ADDED] += nread;
		}

		/* Wait for the parent-writer. */
		if (proc_wait(sp,
		    (long)parent_writer_pid, "parent-writer", 0, 1))
			rval = 1;

		/* Delete any lines written to the utility. */
		if (rval == 0 && ftype == FILTER_BANG &&
		    (cut(sp, NULL, fm, tm, CUT_LINEMODE) ||
		    del(sp, fm, tm, 1))) {
			rval = 1;
			break;
		}

		/*
		 * If the filter had no output, we may have just deleted
		 * the cursor.  Don't do any real error correction, we'll
		 * try and recover later.
		 */
		 if (rp->lno > 1 && !db_exist(sp, rp->lno))
			--rp->lno;
		break;
	}
	F_CLR(sp->ep, F_MULTILOCK);

	/*
	 * !!!
	 * Ignore errors on vi file reads, to make reads prettier.  It's
	 * completely inconsistent, and historic practice.
	 */
uwait:	INT2CHAR(sp, cmd, STRLEN(cmd) + 1, np, nlen);
	return (proc_wait(sp, (long)utility_pid, np,
	    ftype == FILTER_READ && F_ISSET(sp, SC_VI) ? 1 : 0, 0) || rval);
}

/*
 * filter_ldisplay --
 *	Display output from a utility.
 *
 * !!!
 * Historically, the characters were passed unmodified to the terminal.
 * We use the ex print routines to make sure they're printable.
 */
static int
filter_ldisplay(SCR *sp, FILE *fp)
{
	size_t len;
	size_t wlen;
	CHAR_T *wp;

	EX_PRIVATE *exp;

	for (exp = EXP(sp); !ex_getline(sp, fp, &len) && !INTERRUPTED(sp);) {
		FILE2INT5(sp, exp->ibcw, exp->ibp, len, wp, wlen);
		if (ex_ldisplay(sp, wp, wlen, 0, 0))
			break;
	}
	if (ferror(fp))
		msgq(sp, M_SYSERR, "filter read");
	(void)fclose(fp);
	return (0);
}
