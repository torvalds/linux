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
static const char sccsid[] = "$Id: v_left.c,v 10.9 2001/06/25 15:19:32 skimo Exp $";
#endif /* not lint */

#include <sys/types.h>
#include <sys/queue.h>
#include <sys/time.h>

#include <bitstring.h>
#include <limits.h>
#include <stdio.h>

#include "../common/common.h"
#include "vi.h"

/*
 * v_left -- [count]^H, [count]h
 *	Move left by columns.
 *
 * PUBLIC: int v_left(SCR *, VICMD *);
 */
int
v_left(SCR *sp, VICMD *vp)
{
	recno_t cnt;

	/*
	 * !!!
	 * The ^H and h commands always failed in the first column.
	 */
	if (vp->m_start.cno == 0) {
		v_sol(sp);
		return (1);
	}

	/* Find the end of the range. */
	cnt = F_ISSET(vp, VC_C1SET) ? vp->count : 1;
	if (vp->m_start.cno > cnt)
		vp->m_stop.cno = vp->m_start.cno - cnt;
	else
		vp->m_stop.cno = 0;

	/*
	 * All commands move to the end of the range.  Motion commands
	 * adjust the starting point to the character before the current
	 * one.
	 */
	if (ISMOTION(vp))
		--vp->m_start.cno;
	vp->m_final = vp->m_stop;
	return (0);
}

/*
 * v_cfirst -- [count]_
 *	Move to the first non-blank character in a line.
 *
 * PUBLIC: int v_cfirst(SCR *, VICMD *);
 */
int
v_cfirst(SCR *sp, VICMD *vp)
{
	recno_t cnt, lno;

	/*
	 * !!!
	 * If the _ is a motion component, it makes the command a line motion
	 * e.g. "d_" deletes the line.  It also means that the cursor doesn't
	 * move.
	 *
	 * The _ command never failed in the first column.
	 */
	if (ISMOTION(vp))
		F_SET(vp, VM_LMODE);
	/*
	 * !!!
	 * Historically a specified count makes _ move down count - 1
	 * rows, so, "3_" is the same as "2j_".
	 */
	cnt = F_ISSET(vp, VC_C1SET) ? vp->count : 1;
	if (cnt != 1) {
		--vp->count;
		return (v_down(sp, vp));
	}

	/*
	 * Move to the first non-blank.
	 *
	 * Can't just use RCM_SET_FNB, in case _ is used as the motion
	 * component of another command.
	 */
	vp->m_stop.cno = 0;
	if (nonblank(sp, vp->m_stop.lno, &vp->m_stop.cno))
		return (1);

	/*
	 * !!!
	 * The _ command has to fail if the file is empty and we're doing
	 * a delete.  If deleting line 1, and 0 is the first nonblank,
	 * make the check.
	 */
	if (vp->m_stop.lno == 1 &&
	    vp->m_stop.cno == 0 && ISCMD(vp->rkp, 'd')) {
		if (db_last(sp, &lno))
			return (1);
		if (lno == 0) {
			v_sol(sp);
			return (1);
		}
	}

	/*
	 * Delete and non-motion commands move to the end of the range,
	 * yank stays at the start.  Ignore others.
	 */
	vp->m_final =
	    ISMOTION(vp) && ISCMD(vp->rkp, 'y') ? vp->m_start : vp->m_stop;
	return (0);
}

/*
 * v_first -- ^
 *	Move to the first non-blank character in this line.
 *
 * PUBLIC: int v_first(SCR *, VICMD *);
 */
int
v_first(SCR *sp, VICMD *vp)
{
	/*
	 * !!!
	 * Yielding to none in our quest for compatibility with every
	 * historical blemish of vi, no matter how strange it might be,
	 * we permit the user to enter a count and then ignore it.
	 */

	/*
	 * Move to the first non-blank.
	 *
	 * Can't just use RCM_SET_FNB, in case ^ is used as the motion
	 * component of another command.
	 */
	vp->m_stop.cno = 0;
	if (nonblank(sp, vp->m_stop.lno, &vp->m_stop.cno))
		return (1);

	/*
	 * !!!
	 * The ^ command succeeded if used as a command when the cursor was
	 * on the first non-blank in the line, but failed if used as a motion
	 * component in the same situation.
	 */
	if (ISMOTION(vp) && vp->m_start.cno == vp->m_stop.cno) {
		v_sol(sp);
		return (1);
	}

	/*
	 * If moving right, non-motion commands move to the end of the range.
	 * Delete and yank stay at the start.  Motion commands adjust the
	 * ending point to the character before the current ending charcter.
	 *
	 * If moving left, all commands move to the end of the range.  Motion
	 * commands adjust the starting point to the character before the
	 * current starting character.
	 */
	if (vp->m_start.cno < vp->m_stop.cno)
		if (ISMOTION(vp)) {
			--vp->m_stop.cno;
			vp->m_final = vp->m_start;
		} else
			vp->m_final = vp->m_stop;
	else {
		if (ISMOTION(vp))
			--vp->m_start.cno;
		vp->m_final = vp->m_stop;
	}
	return (0);
}

/*
 * v_ncol -- [count]|
 *	Move to column count or the first column on this line.  If the
 *	requested column is past EOL, move to EOL.  The nasty part is
 *	that we have to know character column widths to make this work.
 *
 * PUBLIC: int v_ncol(SCR *, VICMD *);
 */
int
v_ncol(SCR *sp, VICMD *vp)
{
	if (F_ISSET(vp, VC_C1SET) && vp->count > 1) {
		--vp->count;
		vp->m_stop.cno =
		    vs_colpos(sp, vp->m_start.lno, (size_t)vp->count);
		/*
		 * !!!
		 * The | command succeeded if used as a command and the cursor
		 * didn't move, but failed if used as a motion component in the
		 * same situation.
		 */
		if (ISMOTION(vp) && vp->m_stop.cno == vp->m_start.cno) {
			v_nomove(sp);
			return (1);
		}
	} else {
		/*
		 * !!!
		 * The | command succeeded if used as a command in column 0
		 * without a count, but failed if used as a motion component
		 * in the same situation.
		 */
		if (ISMOTION(vp) && vp->m_start.cno == 0) {
			v_sol(sp);
			return (1);
		}
		vp->m_stop.cno = 0;
	}

	/*
	 * If moving right, non-motion commands move to the end of the range.
	 * Delete and yank stay at the start.  Motion commands adjust the
	 * ending point to the character before the current ending charcter.
	 *
	 * If moving left, all commands move to the end of the range.  Motion
	 * commands adjust the starting point to the character before the
	 * current starting character.
	 */
	if (vp->m_start.cno < vp->m_stop.cno)
		if (ISMOTION(vp)) {
			--vp->m_stop.cno;
			vp->m_final = vp->m_start;
		} else
			vp->m_final = vp->m_stop;
	else {
		if (ISMOTION(vp))
			--vp->m_start.cno;
		vp->m_final = vp->m_stop;
	}
	return (0);
}

/*
 * v_zero -- 0
 *	Move to the first column on this line.
 *
 * PUBLIC: int v_zero(SCR *, VICMD *);
 */
int
v_zero(SCR *sp, VICMD *vp)
{
	/*
	 * !!!
	 * The 0 command succeeded if used as a command in the first column
	 * but failed if used as a motion component in the same situation.
	 */
	if (ISMOTION(vp) && vp->m_start.cno == 0) {
		v_sol(sp);
		return (1);
	}

	/*
	 * All commands move to the end of the range.  Motion commands
	 * adjust the starting point to the character before the current
	 * one.
	 */
	vp->m_stop.cno = 0;
	if (ISMOTION(vp))
		--vp->m_start.cno;
	vp->m_final = vp->m_stop;
	return (0);
}
