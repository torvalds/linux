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
static const char sccsid[] = "$Id: v_scroll.c,v 10.12 2001/06/25 15:19:34 skimo Exp $";
#endif /* not lint */

#include <sys/types.h>
#include <sys/queue.h>
#include <sys/time.h>

#include <bitstring.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>

#include "../common/common.h"
#include "vi.h"

static void goto_adjust(VICMD *);

/*
 * The historic vi had a problem in that all movements were by physical
 * lines, not by logical, or screen lines.  Arguments can be made that this
 * is the right thing to do.  For example, single line movements, such as
 * 'j' or 'k', should probably work on physical lines.  Commands like "dj",
 * or "j.", where '.' is a change command, make more sense for physical lines
 * than they do for logical lines.
 *
 * These arguments, however, don't apply to scrolling commands like ^D and
 * ^F -- if the window is fairly small, using physical lines can result in
 * a half-page scroll repainting the entire screen, which is not what the
 * user wanted.  Second, if the line is larger than the screen, using physical
 * lines can make it impossible to display parts of the line -- there aren't
 * any commands that don't display the beginning of the line in historic vi,
 * and if both the beginning and end of the line can't be on the screen at
 * the same time, you lose.  This is even worse in the case of the H, L, and
 * M commands -- for large lines, they may all refer to the same line and
 * will result in no movement at all.
 *
 * Another issue is that page and half-page scrolling commands historically
 * moved to the first non-blank character in the new line.  If the line is
 * approximately the same size as the screen, this loses because the cursor
 * before and after a ^D, may refer to the same location on the screen.  In
 * this implementation, scrolling commands set the cursor to the first non-
 * blank character if the line changes because of the scroll.  Otherwise,
 * the cursor is left alone.
 *
 * This implementation does the scrolling (^B, ^D, ^F, ^U, ^Y, ^E), and the
 * cursor positioning commands (H, L, M) commands using logical lines, not
 * physical.
 */

/*
 * v_lgoto -- [count]G
 *	Go to first non-blank character of the line count, the last line
 *	of the file by default.
 *
 * PUBLIC: int v_lgoto(SCR *, VICMD *);
 */
int
v_lgoto(SCR *sp, VICMD *vp)
{
	recno_t nlines;

	if (F_ISSET(vp, VC_C1SET)) {
		if (!db_exist(sp, vp->count)) {
			/*
			 * !!!
			 * Historically, 1G was legal in an empty file.
			 */
			if (vp->count == 1) {
				if (db_last(sp, &nlines))
					return (1);
				if (nlines == 0)
					return (0);
			}
			v_eof(sp, &vp->m_start);
			return (1);
		}
		vp->m_stop.lno = vp->count;
	} else {
		if (db_last(sp, &nlines))
			return (1);
		vp->m_stop.lno = nlines ? nlines : 1;
	}
	goto_adjust(vp);
	return (0);
}

/*
 * v_home -- [count]H
 *	Move to the first non-blank character of the logical line
 *	count - 1 from the top of the screen, 0 by default.
 *
 * PUBLIC: int v_home(SCR *, VICMD *);
 */
int
v_home(SCR *sp, VICMD *vp)
{
	if (vs_sm_position(sp, &vp->m_stop,
	    F_ISSET(vp, VC_C1SET) ? vp->count - 1 : 0, P_TOP))
		return (1);
	goto_adjust(vp);
	return (0);
}

/*
 * v_middle -- M
 *	Move to the first non-blank character of the logical line
 *	in the middle of the screen.
 *
 * PUBLIC: int v_middle(SCR *, VICMD *);
 */
int
v_middle(SCR *sp, VICMD *vp)
{
	/*
	 * Yielding to none in our quest for compatibility with every
	 * historical blemish of vi, no matter how strange it might be,
	 * we permit the user to enter a count and then ignore it.
	 */
	if (vs_sm_position(sp, &vp->m_stop, 0, P_MIDDLE))
		return (1);
	goto_adjust(vp);
	return (0);
}

/*
 * v_bottom -- [count]L
 *	Move to the first non-blank character of the logical line
 *	count - 1 from the bottom of the screen, 0 by default.
 *
 * PUBLIC: int v_bottom(SCR *, VICMD *);
 */
int
v_bottom(SCR *sp, VICMD *vp)
{
	if (vs_sm_position(sp, &vp->m_stop,
	    F_ISSET(vp, VC_C1SET) ? vp->count - 1 : 0, P_BOTTOM))
		return (1);
	goto_adjust(vp);
	return (0);
}

static void
goto_adjust(VICMD *vp)
{
	/* Guess that it's the end of the range. */
	vp->m_final = vp->m_stop;

	/*
	 * Non-motion commands move the cursor to the end of the range, and
	 * then to the NEXT nonblank of the line.  Historic vi always moved
	 * to the first nonblank in the line; since the H, M, and L commands
	 * are logical motions in this implementation, we do the next nonblank
	 * so that it looks approximately the same to the user.  To make this
	 * happen, the VM_RCM_SETNNB flag is set in the vcmd.c command table.
	 *
	 * If it's a motion, it's more complicated.  The best possible solution
	 * is probably to display the first nonblank of the line the cursor
	 * will eventually rest on.  This is tricky, particularly given that if
	 * the associated command is a delete, we don't yet know what line that
	 * will be.  So, we clear the VM_RCM_SETNNB flag, and set the first
	 * nonblank flag (VM_RCM_SETFNB).  Note, if the lines are sufficiently
	 * long, this can cause the cursor to warp out of the screen.  It's too
	 * hard to fix.
	 *
	 * XXX
	 * The G command is always first nonblank, so it's okay to reset it.
	 */
	if (ISMOTION(vp)) {
		F_CLR(vp, VM_RCM_MASK);
		F_SET(vp, VM_RCM_SETFNB);
	} else
		return;

	/*
	 * If moving backward in the file, delete and yank move to the end
	 * of the range, unless the line didn't change, in which case yank
	 * doesn't move.  If moving forward in the file, delete and yank
	 * stay at the start of the range.  Ignore others.
	 */
	if (vp->m_stop.lno < vp->m_start.lno ||
	    (vp->m_stop.lno == vp->m_start.lno &&
	    vp->m_stop.cno < vp->m_start.cno)) {
		if (ISCMD(vp->rkp, 'y') && vp->m_stop.lno == vp->m_start.lno)
			vp->m_final = vp->m_start;
	} else
		vp->m_final = vp->m_start;
}

/*
 * v_up -- [count]^P, [count]k, [count]-
 *	Move up by lines.
 *
 * PUBLIC: int v_up(SCR *, VICMD *);
 */
int
v_up(SCR *sp, VICMD *vp)
{
	recno_t lno;

	lno = F_ISSET(vp, VC_C1SET) ? vp->count : 1;
	if (vp->m_start.lno <= lno) {
		v_sof(sp, &vp->m_start);
		return (1);
	}
	vp->m_stop.lno = vp->m_start.lno - lno;
	vp->m_final = vp->m_stop;
	return (0);
}

/*
 * v_cr -- [count]^M
 *	In a script window, send the line to the shell.
 *	In a regular window, move down by lines.
 *
 * PUBLIC: int v_cr(SCR *, VICMD *);
 */
int
v_cr(SCR *sp, VICMD *vp)
{
	/* If it's a colon command-line edit window, it's an ex command. */
	if (F_ISSET(sp, SC_COMEDIT))
		return (v_ecl_exec(sp));

	/* If it's a script window, exec the line. */
	if (F_ISSET(sp, SC_SCRIPT))
		return (sscr_exec(sp, vp->m_start.lno));

	/* Otherwise, it's the same as v_down(). */
	return (v_down(sp, vp));
}

/*
 * v_down -- [count]^J, [count]^N, [count]j, [count]^M, [count]+
 *	Move down by lines.
 *
 * PUBLIC: int v_down(SCR *, VICMD *);
 */
int
v_down(SCR *sp, VICMD *vp)
{
	recno_t lno;

	lno = vp->m_start.lno + (F_ISSET(vp, VC_C1SET) ? vp->count : 1);
	if (!db_exist(sp, lno)) {
		v_eof(sp, &vp->m_start);
		return (1);
	}
	vp->m_stop.lno = lno;
	vp->m_final = ISMOTION(vp) ? vp->m_start : vp->m_stop;
	return (0);
}

/*
 * v_hpageup -- [count]^U
 *	Page up half screens.
 *
 * PUBLIC: int v_hpageup(SCR *, VICMD *);
 */
int
v_hpageup(SCR *sp, VICMD *vp)
{
	/*
	 * Half screens always succeed unless already at SOF.
	 *
	 * !!!
	 * Half screens set the scroll value, even if the command
	 * ultimately failed, in historic vi.  Probably a don't care.
	 */
	if (F_ISSET(vp, VC_C1SET))
		sp->defscroll = vp->count;
	if (vs_sm_scroll(sp, &vp->m_stop, sp->defscroll, CNTRL_U))
		return (1);
	vp->m_final = vp->m_stop;
	return (0);
}

/*
 * v_hpagedown -- [count]^D
 *	Page down half screens.
 *
 * PUBLIC: int v_hpagedown(SCR *, VICMD *);
 */
int
v_hpagedown(SCR *sp, VICMD *vp)
{
	/*
	 * Half screens always succeed unless already at EOF.
	 *
	 * !!!
	 * Half screens set the scroll value, even if the command
	 * ultimately failed, in historic vi.  Probably a don't care.
	 */
	if (F_ISSET(vp, VC_C1SET))
		sp->defscroll = vp->count;
	if (vs_sm_scroll(sp, &vp->m_stop, sp->defscroll, CNTRL_D))
		return (1);
	vp->m_final = vp->m_stop;
	return (0);
}

/*
 * v_pagedown -- [count]^F
 *	Page down full screens.
 * !!!
 * Historic vi did not move to the EOF if the screen couldn't move, i.e.
 * if EOF was already displayed on the screen.  This implementation does
 * move to EOF in that case, making ^F more like the the historic ^D.
 *
 * PUBLIC: int v_pagedown(SCR *, VICMD *);
 */
int
v_pagedown(SCR *sp, VICMD *vp)
{
	recno_t offset;

	/*
	 * !!!
	 * The calculation in IEEE Std 1003.2-1992 (POSIX) is:
	 *
	 *	top_line = top_line + count * (window - 2);
	 *
	 * which was historically wrong.  The correct one is:
	 *
	 *	top_line = top_line + count * window - 2;
	 *
	 * i.e. the two line "overlap" was only subtracted once.  Which
	 * makes no sense, but then again, an overlap makes no sense for
	 * any screen but the "next" one anyway.  We do it the historical
	 * way as there's no good reason to change it.
	 *
	 * If the screen has been split horizontally, use the smaller of
	 * the current window size and the window option value.
	 *
	 * It possible for this calculation to be less than 1; move at
	 * least one line.
	 */
	offset = (F_ISSET(vp, VC_C1SET) ? vp->count : 1) * (IS_SPLIT(sp) ?
	    MIN(sp->t_maxrows, O_VAL(sp, O_WINDOW)) : O_VAL(sp, O_WINDOW));
	offset = offset <= 2 ? 1 : offset - 2;
	if (vs_sm_scroll(sp, &vp->m_stop, offset, CNTRL_F))
		return (1);
	vp->m_final = vp->m_stop;
	return (0);
}

/*
 * v_pageup -- [count]^B
 *	Page up full screens.
 *
 * !!!
 * Historic vi did not move to the SOF if the screen couldn't move, i.e.
 * if SOF was already displayed on the screen.  This implementation does
 * move to SOF in that case, making ^B more like the the historic ^U.
 *
 * PUBLIC: int v_pageup(SCR *, VICMD *);
 */
int
v_pageup(SCR *sp, VICMD *vp)
{
	recno_t offset;

	/*
	 * !!!
	 * The calculation in IEEE Std 1003.2-1992 (POSIX) is:
	 *
	 *	top_line = top_line - count * (window - 2);
	 *
	 * which was historically wrong.  The correct one is:
	 *
	 *	top_line = (top_line - count * window) + 2;
	 *
	 * A simpler expression is that, as with ^F, we scroll exactly:
	 *
	 *	count * window - 2
	 *
	 * lines.
	 *
	 * Bizarre.  As with ^F, an overlap makes no sense for anything
	 * but the first screen.  We do it the historical way as there's
	 * no good reason to change it.
	 *
	 * If the screen has been split horizontally, use the smaller of
	 * the current window size and the window option value.
	 *
	 * It possible for this calculation to be less than 1; move at
	 * least one line.
	 */
	offset = (F_ISSET(vp, VC_C1SET) ? vp->count : 1) * (IS_SPLIT(sp) ?
	    MIN(sp->t_maxrows, O_VAL(sp, O_WINDOW)) : O_VAL(sp, O_WINDOW));
	offset = offset <= 2 ? 1 : offset - 2;
	if (vs_sm_scroll(sp, &vp->m_stop, offset, CNTRL_B))
		return (1);
	vp->m_final = vp->m_stop;
	return (0);
}

/*
 * v_lineup -- [count]^Y
 *	Page up by lines.
 *
 * PUBLIC: int v_lineup(SCR *, VICMD *);
 */
int
v_lineup(SCR *sp, VICMD *vp)
{
	/*
	 * The cursor moves down, staying with its original line, unless it
	 * reaches the bottom of the screen.
	 */
	if (vs_sm_scroll(sp,
	    &vp->m_stop, F_ISSET(vp, VC_C1SET) ? vp->count : 1, CNTRL_Y))
		return (1);
	vp->m_final = vp->m_stop;
	return (0);
}

/*
 * v_linedown -- [count]^E
 *	Page down by lines.
 *
 * PUBLIC: int v_linedown(SCR *, VICMD *);
 */
int
v_linedown(SCR *sp, VICMD *vp)
{
	/*
	 * The cursor moves up, staying with its original line, unless it
	 * reaches the top of the screen.
	 */
	if (vs_sm_scroll(sp,
	    &vp->m_stop, F_ISSET(vp, VC_C1SET) ? vp->count : 1, CNTRL_E))
		return (1);
	vp->m_final = vp->m_stop;
	return (0);
}
