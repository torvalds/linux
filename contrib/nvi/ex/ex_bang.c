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
static const char sccsid[] = "$Id: ex_bang.c,v 10.36 2001/06/25 15:19:14 skimo Exp $";
#endif /* not lint */

#include <sys/types.h>
#include <sys/queue.h>
#include <sys/time.h>

#include <bitstring.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../common/common.h"
#include "../vi/vi.h"

/*
 * ex_bang -- :[line [,line]] ! command
 *
 * Pass the rest of the line after the ! character to the program named by
 * the O_SHELL option.
 *
 * Historical vi did NOT do shell expansion on the arguments before passing
 * them, only file name expansion.  This means that the O_SHELL program got
 * "$t" as an argument if that is what the user entered.  Also, there's a
 * special expansion done for the bang command.  Any exclamation points in
 * the user's argument are replaced by the last, expanded ! command.
 *
 * There's some fairly amazing slop in this routine to make the different
 * ways of getting here display the right things.  It took a long time to
 * get it right (wrong?), so be careful.
 *
 * PUBLIC: int ex_bang(SCR *, EXCMD *);
 */
int
ex_bang(SCR *sp, EXCMD *cmdp)
{
	enum filtertype ftype;
	ARGS *ap;
	EX_PRIVATE *exp;
	MARK rm;
	recno_t lno;
	int rval;
	const char *msg;
	char *np;
	size_t nlen;

	ap = cmdp->argv[0];
	if (ap->len == 0) {
		ex_emsg(sp, cmdp->cmd->usage, EXM_USAGE);
		return (1);
	}

	/* Set the "last bang command" remembered value. */
	exp = EXP(sp);
	if (exp->lastbcomm != NULL)
		free(exp->lastbcomm);
	if ((exp->lastbcomm = v_wstrdup(sp, ap->bp, ap->len)) == NULL) {
		msgq(sp, M_SYSERR, NULL);
		return (1);
	}

	/*
	 * If the command was modified by the expansion, it was historically
	 * redisplayed.
	 */
	if (F_ISSET(cmdp, E_MODIFY) && !F_ISSET(sp, SC_EX_SILENT)) {
		/*
		 * Display the command if modified.  Historic ex/vi displayed
		 * the command if it was modified due to file name and/or bang
		 * expansion.  If piping lines in vi, it would be immediately
		 * overwritten by any error or line change reporting.
		 */
		if (F_ISSET(sp, SC_VI))
			vs_update(sp, "!", ap->bp);
		else {
			(void)ex_printf(sp, "!"WS"\n", ap->bp);
			(void)ex_fflush(sp);
		}
	}

	/*
	 * If no addresses were specified, run the command.  If there's an
	 * underlying file, it's been modified and autowrite is set, write
	 * the file back.  If the file has been modified, autowrite is not
	 * set and the warn option is set, tell the user about the file.
	 */
	if (cmdp->addrcnt == 0) {
		msg = NULL;
		if (sp->ep != NULL && F_ISSET(sp->ep, F_MODIFIED))
			if (O_ISSET(sp, O_AUTOWRITE)) {
				if (file_aw(sp, FS_ALL))
					return (0);
			} else if (O_ISSET(sp, O_WARN) &&
			    !F_ISSET(sp, SC_EX_SILENT))
				msg = msg_cat(sp,
				    "303|File modified since last write.",
				    NULL);

		/* If we're still in a vi screen, move out explicitly. */
		INT2CHAR(sp, ap->bp, ap->len+1, np, nlen);
		(void)ex_exec_proc(sp,
		    cmdp, np, msg, !F_ISSET(sp, SC_EX | SC_SCR_EXWROTE));
	}

	/*
	 * If addresses were specified, pipe lines from the file through the
	 * command.
	 *
	 * Historically, vi lines were replaced by both the stdout and stderr
	 * lines of the command, but ex lines by only the stdout lines.  This
	 * makes no sense to me, so nvi makes it consistent for both, and
	 * matches vi's historic behavior.
	 */
	else {
		NEEDFILE(sp, cmdp);

		/* Autoprint is set historically, even if the command fails. */
		F_SET(cmdp, E_AUTOPRINT);

		/*
		 * !!!
		 * Historical vi permitted "!!" in an empty file.  When this
		 * happens, we arrive here with two addresses of 1,1 and a
		 * bad attitude.  The simple solution is to turn it into a
		 * FILTER_READ operation, with the exception that stdin isn't
		 * opened for the utility, and the cursor position isn't the
		 * same.  The only historic glitch (I think) is that we don't
		 * put an empty line into the default cut buffer, as historic
		 * vi did.  Imagine, if you can, my disappointment.
		 */
		ftype = FILTER_BANG;
		if (cmdp->addr1.lno == 1 && cmdp->addr2.lno == 1) {
			if (db_last(sp, &lno))
				return (1);
			if (lno == 0) {
				cmdp->addr1.lno = cmdp->addr2.lno = 0;
				ftype = FILTER_RBANG;
			}
		}
		rval = ex_filter(sp, cmdp,
		    &cmdp->addr1, &cmdp->addr2, &rm, ap->bp, ftype);

		/*
		 * If in vi mode, move to the first nonblank.
		 *
		 * !!!
		 * Historic vi wasn't consistent in this area -- if you used
		 * a forward motion it moved to the first nonblank, but if you
		 * did a backward motion it didn't.  And, if you followed a
		 * backward motion with a forward motion, it wouldn't move to
		 * the nonblank for either.  Going to the nonblank generally
		 * seems more useful and consistent, so we do it.
		 */
		sp->lno = rm.lno;
		if (F_ISSET(sp, SC_VI)) {
			sp->cno = 0;
			(void)nonblank(sp, sp->lno, &sp->cno);
		} else
			sp->cno = rm.cno;
	}

	/* Ex terminates with a bang, even if the command fails. */
	if (!F_ISSET(sp, SC_VI) && !F_ISSET(sp, SC_EX_SILENT))
		(void)ex_puts(sp, "!\n");

	/*
	 * XXX
	 * The ! commands never return an error, so that autoprint always
	 * happens in the ex parser.
	 */
	return (0);
}
