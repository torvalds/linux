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
static const char sccsid[] = "$Id: getc.c,v 10.13 2011/12/27 00:49:31 zy Exp $";
#endif /* not lint */

#include <sys/types.h>
#include <sys/queue.h>
#include <sys/time.h>

#include <bitstring.h>
#include <ctype.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>

#include "../common/common.h"
#include "vi.h"

/*
 * Character stream routines --
 *	These routines return the file a character at a time.  There are two
 *	special cases.  First, the end of a line, end of a file, start of a
 *	file and empty lines are returned as special cases, and no character
 *	is returned.  Second, empty lines include lines that have only white
 *	space in them, because the vi search functions don't care about white
 *	space, and this makes it easier for them to be consistent.
 */

/*
 * cs_init --
 *	Initialize character stream routines.
 *
 * PUBLIC: int cs_init(SCR *, VCS *);
 */
int
cs_init(SCR *sp, VCS *csp)
{
	int isempty;

	if (db_eget(sp, csp->cs_lno, &csp->cs_bp, &csp->cs_len, &isempty)) {
		if (isempty)
			msgq(sp, M_BERR, "177|Empty file");
		return (1);
	}
	if (csp->cs_len == 0 || v_isempty(csp->cs_bp, csp->cs_len)) {
		csp->cs_cno = 0;
		csp->cs_flags = CS_EMP;
	} else {
		csp->cs_flags = 0;
		csp->cs_ch = csp->cs_bp[csp->cs_cno];
	}
	return (0);
}

/*
 * cs_next --
 *	Retrieve the next character.
 *
 * PUBLIC: int cs_next(SCR *, VCS *);
 */
int
cs_next(SCR *sp, VCS *csp)
{
	CHAR_T *p;

	switch (csp->cs_flags) {
	case CS_EMP:				/* EMP; get next line. */
	case CS_EOL:				/* EOL; get next line. */
		if (db_get(sp, ++csp->cs_lno, 0, &p, &csp->cs_len)) {
			--csp->cs_lno;
			csp->cs_flags = CS_EOF;
		} else {
			csp->cs_bp = p;
			if (csp->cs_len == 0 ||
			    v_isempty(csp->cs_bp, csp->cs_len)) {
				csp->cs_cno = 0;
				csp->cs_flags = CS_EMP;
			} else {
				csp->cs_flags = 0;
				csp->cs_ch = csp->cs_bp[csp->cs_cno = 0];
			}
		}
		break;
	case 0:
		if (csp->cs_cno == csp->cs_len - 1)
			csp->cs_flags = CS_EOL;
		else
			csp->cs_ch = csp->cs_bp[++csp->cs_cno];
		break;
	case CS_EOF:				/* EOF. */
		break;
	default:
		abort();
		/* NOTREACHED */
	}
	return (0);
}

/*
 * cs_fspace --
 *	If on a space, eat forward until something other than a
 *	whitespace character.
 *
 * XXX
 * Semantics of checking the current character were coded for the fword()
 * function -- once the other word routines are converted, they may have
 * to change.
 *
 * PUBLIC: int cs_fspace(SCR *, VCS *);
 */
int
cs_fspace(SCR *sp, VCS *csp)
{
	if (csp->cs_flags != 0 || !ISBLANK(csp->cs_ch))
		return (0);
	for (;;) {
		if (cs_next(sp, csp))
			return (1);
		if (csp->cs_flags != 0 || !ISBLANK(csp->cs_ch))
			break;
	}
	return (0);
}

/*
 * cs_fblank --
 *	Eat forward to the next non-whitespace character.
 *
 * PUBLIC: int cs_fblank(SCR *, VCS *);
 */
int
cs_fblank(SCR *sp, VCS *csp)
{
	for (;;) {
		if (cs_next(sp, csp))
			return (1);
		if (csp->cs_flags == CS_EOL || csp->cs_flags == CS_EMP ||
		    (csp->cs_flags == 0 && ISBLANK(csp->cs_ch)))
			continue;
		break;
	}
	return (0);
}

/*
 * cs_prev --
 *	Retrieve the previous character.
 *
 * PUBLIC: int cs_prev(SCR *, VCS *);
 */
int
cs_prev(SCR *sp, VCS *csp)
{
	switch (csp->cs_flags) {
	case CS_EMP:				/* EMP; get previous line. */
	case CS_EOL:				/* EOL; get previous line. */
		if (csp->cs_lno == 1) {		/* SOF. */
			csp->cs_flags = CS_SOF;
			break;
		}
		if (db_get(sp,			/* The line should exist. */
		    --csp->cs_lno, DBG_FATAL, &csp->cs_bp, &csp->cs_len)) {
			++csp->cs_lno;
			return (1);
		}
		if (csp->cs_len == 0 || v_isempty(csp->cs_bp, csp->cs_len)) {
			csp->cs_cno = 0;
			csp->cs_flags = CS_EMP;
		} else {
			csp->cs_flags = 0;
			csp->cs_cno = csp->cs_len - 1;
			csp->cs_ch = csp->cs_bp[csp->cs_cno];
		}
		break;
	case CS_EOF:				/* EOF: get previous char. */
	case 0:
		if (csp->cs_cno == 0)
			if (csp->cs_lno == 1)
				csp->cs_flags = CS_SOF;
			else
				csp->cs_flags = CS_EOL;
		else
			csp->cs_ch = csp->cs_bp[--csp->cs_cno];
		break;
	case CS_SOF:				/* SOF. */
		break;
	default:
		abort();
		/* NOTREACHED */
	}
	return (0);
}

/*
 * cs_bblank --
 *	Eat backward to the next non-whitespace character.
 *
 * PUBLIC: int cs_bblank(SCR *, VCS *);
 */
int
cs_bblank(SCR *sp, VCS *csp)
{
	for (;;) {
		if (cs_prev(sp, csp))
			return (1);
		if (csp->cs_flags == CS_EOL || csp->cs_flags == CS_EMP ||
		    (csp->cs_flags == 0 && ISBLANK(csp->cs_ch)))
			continue;
		break;
	}
	return (0);
}
