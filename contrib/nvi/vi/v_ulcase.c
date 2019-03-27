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
static const char sccsid[] = "$Id: v_ulcase.c,v 10.12 2011/12/02 19:58:32 zy Exp $";
#endif /* not lint */

#include <sys/types.h>
#include <sys/queue.h>
#include <sys/time.h>

#include <bitstring.h>
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../common/common.h"
#include "vi.h"

static int ulcase(SCR *, recno_t, CHAR_T *, size_t, size_t, size_t);

/*
 * v_ulcase -- [count]~
 *	Toggle upper & lower case letters.
 *
 * !!!
 * Historic vi didn't permit ~ to cross newline boundaries.  I can
 * think of no reason why it shouldn't, which at least lets the user
 * auto-repeat through a paragraph.
 *
 * !!!
 * In historic vi, the count was ignored.  It would have been better
 * if there had been an associated motion, but it's too late to make
 * that the default now.
 *
 * PUBLIC: int v_ulcase(SCR *, VICMD *);
 */
int
v_ulcase(SCR *sp, VICMD *vp)
{
	recno_t lno;
	size_t cno, lcnt, len;
	u_long cnt;
	CHAR_T *p;

	lno = vp->m_start.lno;
	cno = vp->m_start.cno;

	for (cnt = F_ISSET(vp, VC_C1SET) ? vp->count : 1; cnt > 0; cno = 0) {
		/* SOF is an error, EOF is an infinite count sink. */
		if (db_get(sp, lno, 0, &p, &len)) {
			if (lno == 1) {
				v_emsg(sp, NULL, VIM_EMPTY);
				return (1);
			}
			--lno;
			break;
		}

		/* Empty lines decrement the count by one. */
		if (len == 0) {
			--cnt;
			vp->m_final.cno = 0;
			continue;
		}

		if (cno + cnt >= len) {
			lcnt = len - 1;
			cnt -= len - cno;

			vp->m_final.cno = len - 1;
		} else {
			lcnt = cno + cnt - 1;
			cnt = 0;

			vp->m_final.cno = lcnt + 1;
		}

		if (ulcase(sp, lno, p, len, cno, lcnt))
			return (1);

		if (cnt > 0)
			++lno;
	}

	vp->m_final.lno = lno;
	return (0);
}

/*
 * v_mulcase -- [count]~[count]motion
 *	Toggle upper & lower case letters over a range.
 *
 * PUBLIC: int v_mulcase(SCR *, VICMD *);
 */
int
v_mulcase(SCR *sp, VICMD *vp)
{
	CHAR_T *p;
	size_t len;
	recno_t lno;

	for (lno = vp->m_start.lno;;) {
		if (db_get(sp, lno, DBG_FATAL, &p, &len))
			return (1);
		if (len != 0 && ulcase(sp, lno, p, len,
		    lno == vp->m_start.lno ? vp->m_start.cno : 0,
		    !F_ISSET(vp, VM_LMODE) &&
		    lno == vp->m_stop.lno ? vp->m_stop.cno : len))
			return (1);

		if (++lno > vp->m_stop.lno)
			break;
	}

	/*
	 * XXX
	 * I didn't create a new motion command when I added motion semantics
	 * for ~.  While that's the correct way to do it, that choice would
	 * have required changes all over the vi directory for little gain.
	 * Instead, we pretend it's a yank command.  Note, this means that we
	 * follow the cursor motion rules for yank commands, but that seems
	 * reasonable to me.
	 */
	return (0);
}

/*
 * ulcase --
 *	Change part of a line's case.
 */
static int
ulcase(SCR *sp, recno_t lno, CHAR_T *lp, size_t len, size_t scno, size_t ecno)
{
	size_t blen;
	int change, rval;
	ARG_CHAR_T ch;
	CHAR_T *p, *t, *bp;

	GET_SPACE_RETW(sp, bp, blen, len);
	MEMMOVE(bp, lp, len);

	change = rval = 0;
	for (p = bp + scno, t = bp + ecno + 1; p < t; ++p) {
		ch = (UCHAR_T)*p;
		if (ISLOWER(ch)) {
			*p = TOUPPER(ch);
			change = 1;
		} else if (ISUPPER(ch)) {
			*p = TOLOWER(ch);
			change = 1;
		}
	}

	if (change && db_set(sp, lno, bp, len))
		rval = 1;

	FREE_SPACEW(sp, bp, blen);
	return (rval);
}
