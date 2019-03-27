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
static const char sccsid[] = "$Id: ex_display.c,v 10.15 2001/06/25 15:19:15 skimo Exp $";
#endif /* not lint */

#include <sys/types.h>
#include <sys/queue.h>
#include <sys/time.h>

#include <bitstring.h>
#include <ctype.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>

#include "../common/common.h"
#include "tag.h"

static int	is_prefix(ARGS *, CHAR_T *);
static int	bdisplay(SCR *);
static void	db(SCR *, CB *, const char *);

/*
 * ex_display -- :display b[uffers] | c[onnections] | s[creens] | t[ags]
 *
 *	Display cscope connections, buffers, tags or screens.
 *
 * PUBLIC: int ex_display(SCR *, EXCMD *);
 */
int
ex_display(SCR *sp, EXCMD *cmdp)
{
	ARGS *arg;

	arg = cmdp->argv[0];

	switch (arg->bp[0]) {
	case 'b':
		if (!is_prefix(arg, L("buffers")))
			break;
		return (bdisplay(sp));
	case 'c':
		if (!is_prefix(arg, L("connections")))
			break;
		return (cscope_display(sp));
	case 's':
		if (!is_prefix(arg, L("screens")))
			break;
		return (ex_sdisplay(sp));
	case 't':
		if (!is_prefix(arg, L("tags")))
			break;
		return (ex_tag_display(sp));
	}
	ex_emsg(sp, cmdp->cmd->usage, EXM_USAGE);
	return (1);
}

/*
 * is_prefix --
 *
 *	Check that a command argument matches a prefix of a given string.
 */
static int
is_prefix(ARGS *arg, CHAR_T *str)
{
	return arg->len <= STRLEN(str) && !MEMCMP(arg->bp, str, arg->len);
}

/*
 * bdisplay --
 *
 *	Display buffers.
 */
static int
bdisplay(SCR *sp)
{
	CB *cbp;

	if (SLIST_EMPTY(sp->gp->cutq) && sp->gp->dcbp == NULL) {
		msgq(sp, M_INFO, "123|No cut buffers to display");
		return (0);
	}

	/* Display regular cut buffers. */
	SLIST_FOREACH(cbp, sp->gp->cutq, q) {
		if (isdigit(cbp->name))
			continue;
		if (!TAILQ_EMPTY(cbp->textq))
			db(sp, cbp, NULL);
		if (INTERRUPTED(sp))
			return (0);
	}
	/* Display numbered buffers. */
	SLIST_FOREACH(cbp, sp->gp->cutq, q) {
		if (!isdigit(cbp->name))
			continue;
		if (!TAILQ_EMPTY(cbp->textq))
			db(sp, cbp, NULL);
		if (INTERRUPTED(sp))
			return (0);
	}
	/* Display default buffer. */
	if ((cbp = sp->gp->dcbp) != NULL)
		db(sp, cbp, "default buffer");
	return (0);
}

/*
 * db --
 *	Display a buffer.
 */
static void
db(SCR *sp, CB *cbp, const char *name)
{
	CHAR_T *p;
	GS *gp;
	TEXT *tp;
	size_t len;

	gp = sp->gp;
	(void)ex_printf(sp, "********** %s%s\n",
	    name == NULL ? KEY_NAME(sp, cbp->name) : name,
	    F_ISSET(cbp, CB_LMODE) ? " (line mode)" : " (character mode)");
	TAILQ_FOREACH(tp, cbp->textq, q) {
		for (len = tp->len, p = tp->lb; len--; ++p) {
			(void)ex_puts(sp, KEY_NAME(sp, *p));
			if (INTERRUPTED(sp))
				return;
		}
		(void)ex_puts(sp, "\n");
	}
}
