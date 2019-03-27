/*-
 * Copyright (c) 1993, 1994
 *	The Regents of the University of California.  All rights reserved.
 * Copyright (c) 1993, 1994, 1995, 1996
 *	Keith Bostic.  All rights reserved.
 *
 * See the LICENSE file for redistribution information.
 */

#include "config.h"

#ifndef lint
static const char sccsid[] = "$Id: ex_screen.c,v 10.12 2001/06/25 15:19:19 skimo Exp $";
#endif /* not lint */

#include <sys/types.h>
#include <sys/queue.h>
#include <sys/time.h>

#include <bitstring.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../common/common.h"
#include "../vi/vi.h"

/*
 * ex_bg --	:bg
 *	Hide the screen.
 *
 * PUBLIC: int ex_bg(SCR *, EXCMD *);
 */
int
ex_bg(SCR *sp, EXCMD *cmdp)
{
	return (vs_bg(sp));
}

/*
 * ex_fg --	:fg [file]
 *	Show the screen.
 *
 * PUBLIC: int ex_fg(SCR *, EXCMD *);
 */
int
ex_fg(SCR *sp, EXCMD *cmdp)
{
	SCR *nsp;
	int newscreen;

	newscreen = F_ISSET(cmdp, E_NEWSCREEN);
	if (vs_fg(sp, &nsp, cmdp->argc ? cmdp->argv[0]->bp : NULL, newscreen))
		return (1);

	/* Set up the switch. */
	if (newscreen) {
		sp->nextdisp = nsp;
		F_SET(sp, SC_SSWITCH);
	}
	return (0);
}

/*
 * ex_resize --	:resize [+-]rows
 *	Change the screen size.
 *
 * PUBLIC: int ex_resize(SCR *, EXCMD *);
 */
int
ex_resize(SCR *sp, EXCMD *cmdp)
{
	adj_t adj;

	switch (FL_ISSET(cmdp->iflags,
	    E_C_COUNT | E_C_COUNT_NEG | E_C_COUNT_POS)) {
	case E_C_COUNT:
		adj = A_SET;
		break;
	case E_C_COUNT | E_C_COUNT_NEG:
		adj = A_DECREASE;
		break;
	case E_C_COUNT | E_C_COUNT_POS:
		adj = A_INCREASE;
		break;
	default:
		ex_emsg(sp, cmdp->cmd->usage, EXM_USAGE);
		return (1);
	}
	return (vs_resize(sp, cmdp->count, adj));
}

/*
 * ex_sdisplay --
 *	Display the list of screens.
 *
 * PUBLIC: int ex_sdisplay(SCR *);
 */
int
ex_sdisplay(SCR *sp)
{
	GS *gp;
	SCR *tsp;
	int cnt, col, len, sep;

	gp = sp->gp;
	if ((tsp = TAILQ_FIRST(gp->hq)) == NULL) {
		msgq(sp, M_INFO, "149|No background screens to display");
		return (0);
	}

	col = len = sep = 0;
	for (cnt = 1; tsp != NULL && !INTERRUPTED(sp);
	    tsp = TAILQ_NEXT(tsp, q)) {
		col += len = strlen(tsp->frp->name) + sep;
		if (col >= sp->cols - 1) {
			col = len;
			sep = 0;
			(void)ex_puts(sp, "\n");
		} else if (cnt != 1) {
			sep = 1;
			(void)ex_puts(sp, " ");
		}
		(void)ex_puts(sp, tsp->frp->name);
		++cnt;
	}
	if (!INTERRUPTED(sp))
		(void)ex_puts(sp, "\n");
	return (0);
}
