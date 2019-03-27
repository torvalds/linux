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
static const char sccsid[] = "$Id: ex_shift.c,v 10.17 2001/06/25 15:19:20 skimo Exp $";
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

enum which {LEFT, RIGHT};
static int shift(SCR *, EXCMD *, enum which);

/*
 * ex_shiftl -- :<[<...]
 *
 *
 * PUBLIC: int ex_shiftl(SCR *, EXCMD *);
 */
int
ex_shiftl(SCR *sp, EXCMD *cmdp)
{
	return (shift(sp, cmdp, LEFT));
}

/*
 * ex_shiftr -- :>[>...]
 *
 * PUBLIC: int ex_shiftr(SCR *, EXCMD *);
 */
int
ex_shiftr(SCR *sp, EXCMD *cmdp)
{
	return (shift(sp, cmdp, RIGHT));
}

/*
 * shift --
 *	Ex shift support.
 */
static int
shift(SCR *sp, EXCMD *cmdp, enum which rl)
{
	recno_t from, to;
	size_t blen, len, newcol, newidx, oldcol, oldidx, sw;
	int curset;
	CHAR_T *p;
	CHAR_T *bp, *tbp;

	NEEDFILE(sp, cmdp);

	if (O_VAL(sp, O_SHIFTWIDTH) == 0) {
		msgq(sp, M_INFO, "152|shiftwidth option set to 0");
		return (0);
	}

	/* Copy the lines being shifted into the unnamed buffer. */
	if (cut(sp, NULL, &cmdp->addr1, &cmdp->addr2, CUT_LINEMODE))
		return (1);

	/*
	 * The historic version of vi permitted the user to string any number
	 * of '>' or '<' characters together, resulting in an indent of the
	 * appropriate levels.  There's a special hack in ex_cmd() so that
	 * cmdp->argv[0] points to the string of '>' or '<' characters.
	 *
	 * Q: What's the difference between the people adding features
	 *    to vi and the Girl Scouts?
	 * A: The Girl Scouts have mint cookies and adult supervision.
	 */
	for (p = cmdp->argv[0]->bp, sw = 0; *p == '>' || *p == '<'; ++p)
		sw += O_VAL(sp, O_SHIFTWIDTH);

	GET_SPACE_RETW(sp, bp, blen, 256);

	curset = 0;
	for (from = cmdp->addr1.lno, to = cmdp->addr2.lno; from <= to; ++from) {
		if (db_get(sp, from, DBG_FATAL, &p, &len))
			goto err;
		if (!len) {
			if (sp->lno == from)
				curset = 1;
			continue;
		}

		/*
		 * Calculate the old indent amount and the number of
		 * characters it used.
		 */
		for (oldidx = 0, oldcol = 0; oldidx < len; ++oldidx)
			if (p[oldidx] == ' ')
				++oldcol;
			else if (p[oldidx] == '\t')
				oldcol += O_VAL(sp, O_TABSTOP) -
				    oldcol % O_VAL(sp, O_TABSTOP);
			else
				break;

		/* Calculate the new indent amount. */
		if (rl == RIGHT)
			newcol = oldcol + sw;
		else {
			newcol = oldcol < sw ? 0 : oldcol - sw;
			if (newcol == oldcol) {
				if (sp->lno == from)
					curset = 1;
				continue;
			}
		}

		/* Get a buffer that will hold the new line. */
		ADD_SPACE_RETW(sp, bp, blen, newcol + len);

		/*
		 * Build a new indent string and count the number of
		 * characters it uses.
		 */
		for (tbp = bp, newidx = 0;
		    newcol >= O_VAL(sp, O_TABSTOP); ++newidx) {
			*tbp++ = '\t';
			newcol -= O_VAL(sp, O_TABSTOP);
		}
		for (; newcol > 0; --newcol, ++newidx)
			*tbp++ = ' ';

		/* Add the original line. */
		MEMCPY(tbp, p + oldidx, len - oldidx);

		/* Set the replacement line. */
		if (db_set(sp, from, bp, (tbp + (len - oldidx)) - bp)) {
err:			FREE_SPACEW(sp, bp, blen);
			return (1);
		}

		/*
		 * !!!
		 * The shift command in historic vi had the usual bizarre
		 * collection of cursor semantics.  If called from vi, the
		 * cursor was repositioned to the first non-blank character
		 * of the lowest numbered line shifted.  If called from ex,
		 * the cursor was repositioned to the first non-blank of the
		 * highest numbered line shifted.  Here, if the cursor isn't
		 * part of the set of lines that are moved, move it to the
		 * first non-blank of the last line shifted.  (This makes
		 * ":3>>" in vi work reasonably.)  If the cursor is part of
		 * the shifted lines, it doesn't get moved at all.  This
		 * permits shifting of marked areas, i.e. ">'a." shifts the
		 * marked area twice, something that couldn't be done with
		 * historic vi.
		 */
		if (sp->lno == from) {
			curset = 1;
			if (newidx > oldidx)
				sp->cno += newidx - oldidx;
			else if (sp->cno >= oldidx - newidx)
				sp->cno -= oldidx - newidx;
		}
	}
	if (!curset) {
		sp->lno = to;
		sp->cno = 0;
		(void)nonblank(sp, to, &sp->cno);
	}

	FREE_SPACEW(sp, bp, blen);

	sp->rptlines[L_SHIFT] += cmdp->addr2.lno - cmdp->addr1.lno + 1;
	return (0);
}
