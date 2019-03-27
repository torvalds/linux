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
static const char sccsid[] = "$Id: ex_edit.c,v 10.15 2011/12/22 23:26:50 zy Exp $";
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

#include "../common/common.h"
#include "../vi/vi.h"

static int ex_N_edit(SCR *, EXCMD *, FREF *, int);

/*
 * ex_edit --	:e[dit][!] [+cmd] [file]
 *		:ex[!] [+cmd] [file]
 *		:vi[sual][!] [+cmd] [file]
 *
 * Edit a file; if none specified, re-edit the current file.  The third
 * form of the command can only be executed while in vi mode.  See the
 * hack in ex.c:ex_cmd().
 *
 * !!!
 * Historic vi didn't permit the '+' command form without specifying
 * a file name as well.  This seems unreasonable, so we support it
 * regardless.
 *
 * PUBLIC: int ex_edit(SCR *, EXCMD *);
 */
int
ex_edit(SCR *sp, EXCMD *cmdp)
{
	FREF *frp;
	int attach, setalt;
	char *np;
	size_t nlen;

	switch (cmdp->argc) {
	case 0:
		/*
		 * If the name has been changed, we edit that file, not the
		 * original name.  If the user was editing a temporary file
		 * (or wasn't editing any file), create another one.  The
		 * reason for not reusing temporary files is that there is
		 * special exit processing of them, and reuse is tricky.
		 */
		frp = sp->frp;
		if (sp->ep == NULL || F_ISSET(frp, FR_TMPFILE)) {
			if ((frp = file_add(sp, NULL)) == NULL)
				return (1);
			attach = 0;
		} else
			attach = 1;
		setalt = 0;
		break;
	case 1:
		INT2CHAR(sp, cmdp->argv[0]->bp, cmdp->argv[0]->len + 1, 
			 np, nlen);
		if ((frp = file_add(sp, np)) == NULL)
			return (1);
		attach = 0;
		setalt = 1;
		set_alt_name(sp, np);
		break;
	default:
		abort();
	}

	if (F_ISSET(cmdp, E_NEWSCREEN) || cmdp->cmd == &cmds[C_VSPLIT])
		return (ex_N_edit(sp, cmdp, frp, attach));

	/*
	 * Check for modifications.
	 *
	 * !!!
	 * Contrary to POSIX 1003.2-1992, autowrite did not affect :edit.
	 */
	if (file_m2(sp, FL_ISSET(cmdp->iflags, E_C_FORCE)))
		return (1);

	/* Switch files. */
	if (file_init(sp, frp, NULL, (setalt ? FS_SETALT : 0) |
	    (FL_ISSET(cmdp->iflags, E_C_FORCE) ? FS_FORCE : 0)))
		return (1);

	F_SET(sp, SC_FSWITCH);
	return (0);
}

/*
 * ex_N_edit --
 *	New screen version of ex_edit.
 */
static int
ex_N_edit(SCR *sp, EXCMD *cmdp, FREF *frp, int attach)
{
	SCR *new;

	/* Get a new screen. */
	if (screen_init(sp->gp, sp, &new))
		return (1);
	if ((cmdp->cmd == &cmds[C_VSPLIT] && vs_vsplit(sp, new)) ||
	    (cmdp->cmd != &cmds[C_VSPLIT] && vs_split(sp, new, 0))) {
		(void)screen_end(new);
		return (1);
	}

	/* Get a backing file. */
	if (attach) {
		/* Copy file state, keep the screen and cursor the same. */
		new->ep = sp->ep;
		++new->ep->refcnt;

		new->frp = frp;
		new->frp->flags = sp->frp->flags;

		new->lno = sp->lno;
		new->cno = sp->cno;

#if defined(USE_WIDECHAR) && defined(USE_ICONV)
		/* Synchronize the iconv environments. */
		o_set(new, O_FILEENCODING, OS_STRDUP,
		    O_STR(sp, O_FILEENCODING), 0);
		conv_enc(new, O_FILEENCODING, 0);
#endif
	} else if (file_init(new, frp, NULL,
	    (FL_ISSET(cmdp->iflags, E_C_FORCE) ? FS_FORCE : 0))) {
		(void)vs_discard(new, NULL);
		(void)screen_end(new);
		return (1);
	}

	/* Create the argument list. */
	new->cargv = new->argv = ex_buildargv(sp, NULL, frp->name);

	/* Set up the switch. */
	sp->nextdisp = new;
	F_SET(sp, SC_SSWITCH);

	return (0);
}
