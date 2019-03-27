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
static const char sccsid[] = "$Id: v_util.c,v 10.14 2001/06/25 15:19:36 skimo Exp $";
#endif /* not lint */

#include <sys/types.h>
#include <sys/queue.h>
#include <sys/time.h>

#include <bitstring.h>
#include <ctype.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../common/common.h"
#include "vi.h"

/*
 * v_eof --
 *	Vi end-of-file error.
 *
 * PUBLIC: void v_eof(SCR *, MARK *);
 */
void
v_eof(SCR *sp, MARK *mp)
{
	recno_t lno;

	if (mp == NULL)
		v_emsg(sp, NULL, VIM_EOF);
	else {
		if (db_last(sp, &lno))
			return;
		if (mp->lno >= lno)
			v_emsg(sp, NULL, VIM_EOF);
		else
			msgq(sp, M_BERR, "195|Movement past the end-of-file");
	}
}

/*
 * v_eol --
 *	Vi end-of-line error.
 *
 * PUBLIC: void v_eol(SCR *, MARK *);
 */
void
v_eol(SCR *sp, MARK *mp)
{
	size_t len;

	if (mp == NULL)
		v_emsg(sp, NULL, VIM_EOL);
	else {
		if (db_get(sp, mp->lno, DBG_FATAL, NULL, &len))
			return;
		if (mp->cno == len - 1)
			v_emsg(sp, NULL, VIM_EOL);
		else
			msgq(sp, M_BERR, "196|Movement past the end-of-line");
	}
}

/*
 * v_nomove --
 *	Vi no cursor movement error.
 *
 * PUBLIC: void v_nomove(SCR *);
 */
void
v_nomove(SCR *sp)
{
	msgq(sp, M_BERR, "197|No cursor movement made");
}

/*
 * v_sof --
 *	Vi start-of-file error.
 *
 * PUBLIC: void v_sof(SCR *, MARK *);
 */
void
v_sof(SCR *sp, MARK *mp)
{
	if (mp == NULL || mp->lno == 1)
		msgq(sp, M_BERR, "198|Already at the beginning of the file");
	else
		msgq(sp, M_BERR, "199|Movement past the beginning of the file");
}

/*
 * v_sol --
 *	Vi start-of-line error.
 *
 * PUBLIC: void v_sol(SCR *);
 */
void
v_sol(SCR *sp)
{
	msgq(sp, M_BERR, "200|Already in the first column");
}

/*
 * v_isempty --
 *	Return if the line contains nothing but white-space characters.
 *
 * PUBLIC: int v_isempty(CHAR_T *, size_t);
 */
int
v_isempty(CHAR_T *p, size_t len)
{
	for (; len--; ++p)
		if (!isblank(*p))
			return (0);
	return (1);
}

/*
 * v_emsg --
 *	Display a few common vi messages.
 *
 * PUBLIC: void v_emsg(SCR *, char *, vim_t);
 */
void
v_emsg(SCR *sp, char *p, vim_t which)
{
	switch (which) {
	case VIM_COMBUF:
		msgq(sp, M_ERR,
		    "201|Buffers should be specified before the command");
		break;
	case VIM_EMPTY:
		msgq(sp, M_BERR, "209|The file is empty");
		break;
	case VIM_EOF:
		msgq(sp, M_BERR, "202|Already at end-of-file");
		break;
	case VIM_EOL:
		msgq(sp, M_BERR, "203|Already at end-of-line");
		break;
	case VIM_NOCOM:
	case VIM_NOCOM_B:
		msgq(sp,
		    which == VIM_NOCOM_B ? M_BERR : M_ERR,
		    "204|%s isn't a vi command", p);
		break;
	case VIM_WRESIZE:
		msgq(sp, M_ERR, "Window resize interrupted text input mode");
		break;
	case VIM_USAGE:
		msgq(sp, M_ERR, "205|Usage: %s", p);
		break;
	}
}
