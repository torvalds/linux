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
static const char sccsid[] = "$Id: ex_visual.c,v 10.16 2001/08/29 11:04:13 skimo Exp $";
#endif /* not lint */

#include <sys/types.h>
#include <sys/queue.h>
#include <sys/time.h>

#include <bitstring.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../common/common.h"
#include "../vi/vi.h"

/*
 * ex_visual -- :[line] vi[sual] [^-.+] [window_size] [flags]
 *	Switch to visual mode.
 *
 * PUBLIC: int ex_visual(SCR *, EXCMD *);
 */
int
ex_visual(SCR *sp, EXCMD *cmdp)
{
	SCR *tsp;
	size_t len;
	int pos;
	char buf[256];
	size_t wlen;
	CHAR_T *wp;

	/* If open option off, disallow visual command. */
	if (!O_ISSET(sp, O_OPEN)) {
		msgq(sp, M_ERR,
	    "175|The visual command requires that the open option be set");
		return (1);
	}

	/* Move to the address. */
	sp->lno = cmdp->addr1.lno == 0 ? 1 : cmdp->addr1.lno;

	/*
	 * Push a command based on the line position flags.  If no
	 * flag specified, the line goes at the top of the screen.
	 */
	switch (FL_ISSET(cmdp->iflags,
	    E_C_CARAT | E_C_DASH | E_C_DOT | E_C_PLUS)) {
	case E_C_CARAT:
		pos = '^';
		break;
	case E_C_DASH:
		pos = '-';
		break;
	case E_C_DOT:
		pos = '.';
		break;
	case E_C_PLUS:
		pos = '+';
		break;
	default:
		sp->frp->lno = sp->lno;
		sp->frp->cno = 0;
		(void)nonblank(sp, sp->lno, &sp->cno);
		F_SET(sp->frp, FR_CURSORSET);
		goto nopush;
	}

	if (FL_ISSET(cmdp->iflags, E_C_COUNT))
		len = snprintf(buf, sizeof(buf),
		     "%luz%c%lu", (u_long)sp->lno, pos, cmdp->count);
	else
		len = snprintf(buf, sizeof(buf), "%luz%c", (u_long)sp->lno, pos);
	CHAR2INT(sp, buf, len, wp, wlen);
	(void)v_event_push(sp, NULL, wp, wlen, CH_NOMAP | CH_QUOTED);

	/*
	 * !!!
	 * Historically, if no line address was specified, the [p#l] flags
	 * caused the cursor to be moved to the last line of the file, which
	 * was then positioned as described above.  This seems useless, so
	 * I haven't implemented it.
	 */
	switch (FL_ISSET(cmdp->iflags, E_C_HASH | E_C_LIST | E_C_PRINT)) {
	case E_C_HASH:
		O_SET(sp, O_NUMBER);
		break;
	case E_C_LIST:
		O_SET(sp, O_LIST);
		break;
	case E_C_PRINT:
		break;
	}

nopush:	/*
	 * !!!
	 * You can call the visual part of the editor from within an ex
	 * global command.
	 *
	 * XXX
	 * Historically, undoing a visual session was a single undo command,
	 * i.e. you could undo all of the changes you made in visual mode.
	 * We don't get this right; I'm waiting for the new logging code to
	 * be available.
	 *
	 * It's explicit, don't have to wait for the user, unless there's
	 * already a reason to wait.
	 */
	if (!F_ISSET(sp, SC_SCR_EXWROTE))
		F_SET(sp, SC_EX_WAIT_NO);

	if (F_ISSET(sp, SC_EX_GLOBAL)) {
		/*
		 * When the vi screen(s) exit, we don't want to lose our hold
		 * on this screen or this file, otherwise we're going to fail
		 * fairly spectacularly.
		 */
		++sp->refcnt;
		++sp->ep->refcnt;
		/* XXXX where is this decremented ? */

		/*
		 * Fake up a screen pointer -- vi doesn't get to change our
		 * underlying file, regardless.
		 */
		tsp = sp;
		if (vi(&tsp))
			return (1);

		/*
		 * !!!
		 * Historically, if the user exited the vi screen(s) using an
		 * ex quit command (e.g. :wq, :q) ex/vi exited, it was only if
		 * they exited vi using the Q command that ex continued.  Some
		 * early versions of nvi continued in ex regardless, but users
		 * didn't like the semantic.
		 *
		 * Reset the screen.
		 */
		if (ex_init(sp))
			return (1);

		/* Move out of the vi screen. */
		(void)ex_puts(sp, "\n");
	} else {
		F_CLR(sp, SC_EX | SC_SCR_EX);
		F_SET(sp, SC_VI);
	}
	return (0);
}
