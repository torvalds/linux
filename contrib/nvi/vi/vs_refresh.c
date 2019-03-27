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
static const char sccsid[] = "$Id: vs_refresh.c,v 10.54 2015/04/08 16:32:49 zy Exp $";
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

#include "../common/common.h"
#include "vi.h"

#define	UPDATE_CURSOR	0x01			/* Update the cursor. */
#define	UPDATE_SCREEN	0x02			/* Flush to screen. */

static void	vs_modeline(SCR *);
static int	vs_paint(SCR *, u_int);

/*
 * v_repaint --
 *	Repaint selected lines from the screen.
 *
 * PUBLIC: int vs_repaint(SCR *, EVENT *);
 */
int
vs_repaint(
	SCR *sp,
	EVENT *evp)
{
	SMAP *smp;

	for (; evp->e_flno <= evp->e_tlno; ++evp->e_flno) {
		smp = HMAP + evp->e_flno - 1;
		SMAP_FLUSH(smp);
		if (vs_line(sp, smp, NULL, NULL))
			return (1);
	}
	return (0);
}

/*
 * vs_refresh --
 *	Refresh all screens.
 *
 * PUBLIC: int vs_refresh(SCR *, int);
 */
int
vs_refresh(
	SCR *sp,
	int forcepaint)
{
	GS *gp;
	SCR *tsp;
	int need_refresh = 0;
	u_int priv_paint, pub_paint;

	gp = sp->gp;

	/*
	 * 1: Refresh the screen.
	 *
	 * If SC_SCR_REDRAW is set in the current screen, repaint everything
	 * that we can find, including status lines.
	 */
	if (F_ISSET(sp, SC_SCR_REDRAW))
		TAILQ_FOREACH(tsp, gp->dq, q)
			if (tsp != sp)
				F_SET(tsp, SC_SCR_REDRAW | SC_STATUS);

	/*
	 * 2: Related or dirtied screens, or screens with messages.
	 *
	 * If related screens share a view into a file, they may have been
	 * modified as well.  Refresh any screens that aren't exiting that
	 * have paint or dirty bits set.  Always update their screens, we
	 * are not likely to get another chance.  Finally, if we refresh any
	 * screens other than the current one, the cursor will be trashed.
	 */
	pub_paint = SC_SCR_REFORMAT | SC_SCR_REDRAW;
	priv_paint = VIP_CUR_INVALID | VIP_N_REFRESH;
	if (O_ISSET(sp, O_NUMBER))
		priv_paint |= VIP_N_RENUMBER;
	TAILQ_FOREACH(tsp, gp->dq, q)
		if (tsp != sp && !F_ISSET(tsp, SC_EXIT | SC_EXIT_FORCE) &&
		    (F_ISSET(tsp, pub_paint) ||
		    F_ISSET(VIP(tsp), priv_paint))) {
			(void)vs_paint(tsp,
			    (F_ISSET(VIP(tsp), VIP_CUR_INVALID) ?
			    UPDATE_CURSOR : 0) | UPDATE_SCREEN);
			F_SET(VIP(sp), VIP_CUR_INVALID);
		}

	/*
	 * 3: Refresh the current screen.
	 *
	 * Always refresh the current screen, it may be a cursor movement.
	 * Also, always do it last -- that way, SC_SCR_REDRAW can be set
	 * in the current screen only, and the screen won't flash.
	 */
	if (vs_paint(sp, UPDATE_CURSOR | (!forcepaint &&
	    F_ISSET(sp, SC_SCR_VI) && KEYS_WAITING(sp) ? 0 : UPDATE_SCREEN)))
		return (1);

	/*
	 * 4: Paint any missing status lines.
	 *
	 * XXX
	 * This is fairly evil.  Status lines are written using the vi message
	 * mechanism, since we have no idea how long they are.  Since we may be
	 * painting screens other than the current one, we don't want to make
	 * the user wait.  We depend heavily on there not being any other lines
	 * currently waiting to be displayed and the message truncation code in
	 * the msgq_status routine working.
	 *
	 * And, finally, if we updated any status lines, make sure the cursor
	 * gets back to where it belongs.
	 */
	TAILQ_FOREACH(tsp, gp->dq, q)
		if (F_ISSET(tsp, SC_STATUS)) {
			need_refresh = 1;
			vs_resolve(tsp, sp, 0);
		}
	if (need_refresh)
		(void)gp->scr_refresh(sp, 0);

	/*
	 * A side-effect of refreshing the screen is that it's now ready
	 * for everything else, i.e. messages.
	 */
	F_SET(sp, SC_SCR_VI);
	return (0);
}

/*
 * vs_paint --
 *	This is the guts of the vi curses screen code.  The idea is that
 *	the SCR structure passed in contains the new coordinates of the
 *	screen.  What makes this hard is that we don't know how big
 *	characters are, doing input can put the cursor in illegal places,
 *	and we're frantically trying to avoid repainting unless it's
 *	absolutely necessary.  If you change this code, you'd better know
 *	what you're doing.  It's subtle and quick to anger.
 */
static int
vs_paint(
	SCR *sp,
	u_int flags)
{
	GS *gp;
	SMAP *smp, tmp;
	VI_PRIVATE *vip;
	recno_t lastline, lcnt;
	size_t cwtotal, cnt, len, notused, off, y;
	int ch = 0, didpaint, isempty, leftright_warp;
	CHAR_T *p;

#define	 LNO	sp->lno			/* Current file line. */
#define	OLNO	vip->olno		/* Remembered file line. */
#define	 CNO	sp->cno			/* Current file column. */
#define	OCNO	vip->ocno		/* Remembered file column. */
#define	SCNO	vip->sc_col		/* Current screen column. */

	gp = sp->gp;
	vip = VIP(sp);
	didpaint = leftright_warp = 0;

	/*
	 * 5: Reformat the lines.
	 *
	 * If the lines themselves have changed (:set list, for example),
	 * fill in the map from scratch.  Adjust the screen that's being
	 * displayed if the leftright flag is set.
	 */
	if (F_ISSET(sp, SC_SCR_REFORMAT)) {
		/* Invalidate the line size cache. */
		VI_SCR_CFLUSH(vip);

		/* Toss vs_line() cached information. */
		if (F_ISSET(sp, SC_SCR_TOP)) {
			if (vs_sm_fill(sp, LNO, P_TOP))
				return (1);
		}
		else if (F_ISSET(sp, SC_SCR_CENTER)) {
			if (vs_sm_fill(sp, LNO, P_MIDDLE))
				return (1);
		} else
			if (vs_sm_fill(sp, OOBLNO, P_TOP))
				return (1);
		F_SET(sp, SC_SCR_REDRAW);
	}

	/*
	 * 6: Line movement.
	 *
	 * Line changes can cause the top line to change as well.  As
	 * before, if the movement is large, the screen is repainted.
	 *
	 * 6a: Small screens.
	 *
	 * Users can use the window, w300, w1200 and w9600 options to make
	 * the screen artificially small.  The behavior of these options
	 * in the historic vi wasn't all that consistent, and, in fact, it
	 * was never documented how various screen movements affected the
	 * screen size.  Generally, one of three things would happen:
	 *	1: The screen would expand in size, showing the line
	 *	2: The screen would scroll, showing the line
	 *	3: The screen would compress to its smallest size and
	 *		repaint.
	 * In general, scrolling didn't cause compression (200^D was handled
	 * the same as ^D), movement to a specific line would (:N where N
	 * was 1 line below the screen caused a screen compress), and cursor
	 * movement would scroll if it was 11 lines or less, and compress if
	 * it was more than 11 lines.  (And, no, I have no idea where the 11
	 * comes from.)
	 *
	 * What we do is try and figure out if the line is less than half of
	 * a full screen away.  If it is, we expand the screen if there's
	 * room, and then scroll as necessary.  The alternative is to compress
	 * and repaint.
	 *
	 * !!!
	 * This code is a special case from beginning to end.  Unfortunately,
	 * home modems are still slow enough that it's worth having.
	 *
	 * XXX
	 * If the line a really long one, i.e. part of the line is on the
	 * screen but the column offset is not, we'll end up in the adjust
	 * code, when we should probably have compressed the screen.
	 */
	if (IS_SMALL(sp))
		if (LNO < HMAP->lno) {
			lcnt = vs_sm_nlines(sp, HMAP, LNO, sp->t_maxrows);
			if (lcnt <= HALFSCREEN(sp))
				for (; lcnt && sp->t_rows != sp->t_maxrows;
				     --lcnt, ++sp->t_rows) {
					++TMAP;
					if (vs_sm_1down(sp))
						return (1);
				}
			else
				goto small_fill;
		} else if (LNO > TMAP->lno) {
			lcnt = vs_sm_nlines(sp, TMAP, LNO, sp->t_maxrows);
			if (lcnt <= HALFSCREEN(sp))
				for (; lcnt && sp->t_rows != sp->t_maxrows;
				     --lcnt, ++sp->t_rows) {
					if (vs_sm_next(sp, TMAP, TMAP + 1))
						return (1);
					++TMAP;
					if (vs_line(sp, TMAP, NULL, NULL))
						return (1);
				}
			else {
small_fill:			(void)gp->scr_move(sp, LASTLINE(sp), 0);
				(void)gp->scr_clrtoeol(sp);
				for (; sp->t_rows > sp->t_minrows;
				    --sp->t_rows, --TMAP) {
					(void)gp->scr_move(sp, TMAP - HMAP, 0);
					(void)gp->scr_clrtoeol(sp);
				}
				if (vs_sm_fill(sp, LNO, P_FILL))
					return (1);
				F_SET(sp, SC_SCR_REDRAW);
				goto adjust;
			}
		}

	/*
	 * 6b: Line down, or current screen.
	 */
	if (LNO >= HMAP->lno) {
		/* Current screen. */
		if (LNO <= TMAP->lno)
			goto adjust;
		if (F_ISSET(sp, SC_SCR_TOP))
			goto top;
		if (F_ISSET(sp, SC_SCR_CENTER))
			goto middle;

		/*
		 * If less than half a screen above the line, scroll down
		 * until the line is on the screen.
		 */
		lcnt = vs_sm_nlines(sp, TMAP, LNO, HALFTEXT(sp));
		if (lcnt < HALFTEXT(sp)) {
			while (lcnt--)
				if (vs_sm_1up(sp))
					return (1);
			goto adjust;
		}
		goto bottom;
	}

	/*
	 * 6c: If not on the current screen, may request center or top.
	 */
	if (F_ISSET(sp, SC_SCR_TOP))
		goto top;
	if (F_ISSET(sp, SC_SCR_CENTER))
		goto middle;

	/*
	 * 6d: Line up.
	 */
	lcnt = vs_sm_nlines(sp, HMAP, LNO, HALFTEXT(sp));
	if (lcnt < HALFTEXT(sp)) {
		/*
		 * If less than half a screen below the line, scroll up until
		 * the line is the first line on the screen.  Special check so
		 * that if the screen has been emptied, we refill it.
		 */
		if (db_exist(sp, HMAP->lno)) {
			while (lcnt--)
				if (vs_sm_1down(sp))
					return (1);
			goto adjust;
		} else
			goto top;	/* XXX No such line. */

		/*
		 * If less than a half screen from the bottom of the file,
		 * put the last line of the file on the bottom of the screen.
		 */
bottom:		if (db_last(sp, &lastline))
			return (1);
		tmp.lno = LNO;
		tmp.coff = HMAP->coff;
		tmp.soff = 1;
		lcnt = vs_sm_nlines(sp, &tmp, lastline, sp->t_rows);
		if (lcnt < HALFTEXT(sp)) {
			if (vs_sm_fill(sp, lastline, P_BOTTOM))
				return (1);
			F_SET(sp, SC_SCR_REDRAW);
			goto adjust;
		}
		/* It's not close, just put the line in the middle. */
		goto middle;
	}

	/*
	 * If less than half a screen from the top of the file, put the first
	 * line of the file at the top of the screen.  Otherwise, put the line
	 * in the middle of the screen.
	 */
	tmp.lno = 1;
	tmp.coff = HMAP->coff;
	tmp.soff = 1;
	lcnt = vs_sm_nlines(sp, &tmp, LNO, HALFTEXT(sp));
	if (lcnt < HALFTEXT(sp)) {
		if (vs_sm_fill(sp, 1, P_TOP))
			return (1);
	} else
middle:		if (vs_sm_fill(sp, LNO, P_MIDDLE))
			return (1);
	if (0) {
top:		if (vs_sm_fill(sp, LNO, P_TOP))
			return (1);
	}
	F_SET(sp, SC_SCR_REDRAW);

	/*
	 * At this point we know part of the line is on the screen.  Since
	 * scrolling is done using logical lines, not physical, all of the
	 * line may not be on the screen.  While that's not necessarily bad,
	 * if the part the cursor is on isn't there, we're going to lose.
	 * This can be tricky; if the line covers the entire screen, lno
	 * may be the same as both ends of the map, that's why we test BOTH
	 * the top and the bottom of the map.  This isn't a problem for
	 * left-right scrolling, the cursor movement code handles the problem.
	 *
	 * There's a performance issue here if editing *really* long lines.
	 * This gets to the right spot by scrolling, and, in a binary, by
	 * scrolling hundreds of lines.  If the adjustment looks like it's
	 * going to be a serious problem, refill the screen and repaint.
	 */
adjust:	if (!O_ISSET(sp, O_LEFTRIGHT) &&
	    (LNO == HMAP->lno || LNO == TMAP->lno)) {
		cnt = vs_screens(sp, LNO, &CNO);
		if (LNO == HMAP->lno && cnt < HMAP->soff)
			if ((HMAP->soff - cnt) > HALFTEXT(sp)) {
				HMAP->soff = cnt;
				vs_sm_fill(sp, OOBLNO, P_TOP);
				F_SET(sp, SC_SCR_REDRAW);
			} else
				while (cnt < HMAP->soff)
					if (vs_sm_1down(sp))
						return (1);
		if (LNO == TMAP->lno && cnt > TMAP->soff)
			if ((cnt - TMAP->soff) > HALFTEXT(sp)) {
				TMAP->soff = cnt;
				vs_sm_fill(sp, OOBLNO, P_BOTTOM);
				F_SET(sp, SC_SCR_REDRAW);
			} else
				while (cnt > TMAP->soff)
					if (vs_sm_1up(sp))
						return (1);
	}

	/*
	 * If the screen needs to be repainted, skip cursor optimization.
	 * However, in the code above we skipped leftright scrolling on
	 * the grounds that the cursor code would handle it.  Make sure
	 * the right screen is up.
	 */
	if (F_ISSET(sp, SC_SCR_REDRAW)) {
		if (O_ISSET(sp, O_LEFTRIGHT))
			goto slow;
		goto paint;
	}

	/*
	 * 7: Cursor movements (current screen only).
	 */
	if (!LF_ISSET(UPDATE_CURSOR))
		goto number;

	/*
	 * Decide cursor position.  If the line has changed, the cursor has
	 * moved over a tab, or don't know where the cursor was, reparse the
	 * line.  Otherwise, we've just moved over fixed-width characters,
	 * and can calculate the left/right scrolling and cursor movement
	 * without reparsing the line.  Note that we don't know which (if any)
	 * of the characters between the old and new cursor positions changed.
	 *
	 * XXX
	 * With some work, it should be possible to handle tabs quickly, at
	 * least in obvious situations, like moving right and encountering
	 * a tab, without reparsing the whole line.
	 *
	 * If the line we're working with has changed, reread it..
	 */
	if (F_ISSET(vip, VIP_CUR_INVALID) || LNO != OLNO)
		goto slow;

	/* Otherwise, if nothing's changed, ignore the cursor. */
	if (CNO == OCNO)
		goto fast;

	/*
	 * Get the current line.  If this fails, we either have an empty
	 * file and can just repaint, or there's a real problem.  This
	 * isn't a performance issue because there aren't any ways to get
	 * here repeatedly.
	 */
	if (db_eget(sp, LNO, &p, &len, &isempty)) {
		if (isempty)
			goto slow;
		return (1);
	}

#ifdef DEBUG
	/* Sanity checking. */
	if (CNO >= len && len != 0) {
		msgq(sp, M_ERR, "Error: %s/%d: cno (%zu) >= len (%zu)",
		     tail(__FILE__), __LINE__, CNO, len);
		return (1);
	}
#endif
	/*
	 * The basic scheme here is to look at the characters in between
	 * the old and new positions and decide how big they are on the
	 * screen, and therefore, how many screen positions to move.
	 */
	if (CNO < OCNO) {
		/*
		 * 7a: Cursor moved left.
		 *
		 * Point to the old character.  The old cursor position can
		 * be past EOL if, for example, we just deleted the rest of
		 * the line.  In this case, since we don't know the width of
		 * the characters we traversed, we have to do it slowly.
		 */
		p += OCNO;
		cnt = (OCNO - CNO) + 1;
		if (OCNO >= len)
			goto slow;

		/*
		 * Quick sanity check -- it's hard to figure out exactly when
		 * we cross a screen boundary as we do in the cursor right
		 * movement.  If cnt is so large that we're going to cross the
		 * boundary no matter what, stop now.
		 */
		if (SCNO + 1 + MAX_CHARACTER_COLUMNS < cnt)
			goto slow;

		/*
		 * Count up the widths of the characters.  If it's a tab
		 * character, go do it the the slow way.
		 */
		for (cwtotal = 0; cnt--; cwtotal += KEY_COL(sp, ch))
			if ((ch = *(UCHAR_T *)p--) == '\t')
				goto slow;

		/*
		 * Decrement the screen cursor by the total width of the
		 * characters minus 1.
		 */
		cwtotal -= 1;

		/*
		 * If we're moving left, and there's a wide character in the
		 * current position, go to the end of the character.
		 */
		if (KEY_COL(sp, ch) > 1)
			cwtotal -= KEY_COL(sp, ch) - 1;

		/*
		 * If the new column moved us off of the current logical line,
		 * calculate a new one.  If doing leftright scrolling, we've
		 * moved off of the current screen, as well.
		 */
		if (SCNO < cwtotal)
			goto slow;
		SCNO -= cwtotal;
	} else {
		/*
		 * 7b: Cursor moved right.
		 *
		 * Point to the first character to the right.
		 */
		p += OCNO + 1;
		cnt = CNO - OCNO;

		/*
		 * Count up the widths of the characters.  If it's a tab
		 * character, go do it the the slow way.  If we cross a
		 * screen boundary, we can quit.
		 */
		for (cwtotal = SCNO; cnt--;) {
			if ((ch = *(UCHAR_T *)p++) == '\t')
				goto slow;
			if ((cwtotal += KEY_COL(sp, ch)) >= SCREEN_COLS(sp))
				break;
		}

		/*
		 * Increment the screen cursor by the total width of the
		 * characters.
		 */
		SCNO = cwtotal;

		/* See screen change comment in section 6a. */
		if (SCNO >= SCREEN_COLS(sp))
			goto slow;
	}

	/*
	 * 7c: Fast cursor update.
	 *
	 * We have the current column, retrieve the current row.
	 */
fast:	(void)gp->scr_cursor(sp, &y, &notused);
	goto done_cursor;

	/*
	 * 7d: Slow cursor update.
	 *
	 * Walk through the map and find the current line.
	 */
slow:	for (smp = HMAP; smp->lno != LNO; ++smp);

	/*
	 * 7e: Leftright scrolling adjustment.
	 *
	 * If doing left-right scrolling and the cursor movement has changed
	 * the displayed screen, scroll the screen left or right, unless we're
	 * updating the info line in which case we just scroll that one line.
	 * We adjust the offset up or down until we have a window that covers
	 * the current column, making sure that we adjust differently for the
	 * first screen as compared to subsequent ones.
	 */
	if (O_ISSET(sp, O_LEFTRIGHT)) {
		/*
		 * Get the screen column for this character, and correct
		 * for the number option offset.
		 */
		cnt = vs_columns(sp, NULL, LNO, &CNO, NULL);
		if (O_ISSET(sp, O_NUMBER))
			cnt -= O_NUMBER_LENGTH;

		/* Adjust the window towards the beginning of the line. */
		off = smp->coff;
		if (off >= cnt) {
			do {
				if (off >= O_VAL(sp, O_SIDESCROLL))
					off -= O_VAL(sp, O_SIDESCROLL);
				else {
					off = 0;
					break;
				}
			} while (off >= cnt);
			goto shifted;
		}

		/* Adjust the window towards the end of the line. */
		if ((off == 0 && off + SCREEN_COLS(sp) < cnt) ||
		    (off != 0 && off + sp->cols < cnt)) {
			do {
				off += O_VAL(sp, O_SIDESCROLL);
			} while (off + sp->cols < cnt);

shifted:		/* Fill in screen map with the new offset. */
			if (F_ISSET(sp, SC_TINPUT_INFO))
				smp->coff = off;
			else {
				for (smp = HMAP; smp <= TMAP; ++smp)
					smp->coff = off;
				leftright_warp = 1;
			}
			goto paint;
		}

		/*
		 * We may have jumped here to adjust a leftright screen because
		 * redraw was set.  If so, we have to paint the entire screen.
		 */
		if (F_ISSET(sp, SC_SCR_REDRAW))
			goto paint;
	}

	/*
	 * Update the screen lines for this particular file line until we
	 * have a new screen cursor position.
	 */
	for (y = -1,
	    vip->sc_smap = NULL; smp <= TMAP && smp->lno == LNO; ++smp) {
		if (vs_line(sp, smp, &y, &SCNO))
			return (1);
		if (y != -1) {
			vip->sc_smap = smp;
			break;
		}
	}
	goto done_cursor;

	/*
	 * 8: Repaint the entire screen.
	 *
	 * Lost big, do what you have to do.  We flush the cache, since
	 * SC_SCR_REDRAW gets set when the screen isn't worth fixing, and
	 * it's simpler to repaint.  So, don't trust anything that we
	 * think we know about it.
	 */
paint:	for (smp = HMAP; smp <= TMAP; ++smp)
		SMAP_FLUSH(smp);
	for (y = -1, vip->sc_smap = NULL, smp = HMAP; smp <= TMAP; ++smp) {
		if (vs_line(sp, smp, &y, &SCNO))
			return (1);
		if (y != -1 && vip->sc_smap == NULL)
			vip->sc_smap = smp;
	}
	/*
	 * If it's a small screen and we're redrawing, clear the unused lines,
	 * ex may have overwritten them.
	 */
	if (F_ISSET(sp, SC_SCR_REDRAW) && IS_SMALL(sp))
		for (cnt = sp->t_rows; cnt <= sp->t_maxrows; ++cnt) {
			(void)gp->scr_move(sp, cnt, 0);
			(void)gp->scr_clrtoeol(sp);
		}

	didpaint = 1;

done_cursor:
	/*
	 * Sanity checking.  When the repainting code messes up, the usual
	 * result is we don't repaint the cursor and so sc_smap will be
	 * NULL.  If we're debugging, die, otherwise restart from scratch.
	 */
#ifdef DEBUG
	if (vip->sc_smap == NULL)
		abort();
#else
	if (vip->sc_smap == NULL) {
		F_SET(sp, SC_SCR_REFORMAT);
		return (vs_paint(sp, flags));
	}
#endif

	/*
	 * 9: Set the remembered cursor values.
	 */
	OCNO = CNO;
	OLNO = LNO;

	/*
	 * 10: Repaint the line numbers.
	 *
	 * If O_NUMBER is set and the VIP_N_RENUMBER bit is set, and we
	 * didn't repaint the screen, repaint all of the line numbers,
	 * they've changed.
	 */
number:	if (O_ISSET(sp, O_NUMBER) &&
	    F_ISSET(vip, VIP_N_RENUMBER) && !didpaint && vs_number(sp))
		return (1);

	/*
	 * 11: Update the mode line, position the cursor, and flush changes.
	 *
	 * If we warped the screen, we have to refresh everything.
	 */
	if (leftright_warp)
		LF_SET(UPDATE_CURSOR | UPDATE_SCREEN);

	if (LF_ISSET(UPDATE_SCREEN) && !IS_ONELINE(sp) &&
	    !F_ISSET(vip, VIP_S_MODELINE) && !F_ISSET(sp, SC_TINPUT_INFO))
		vs_modeline(sp);

	if (LF_ISSET(UPDATE_CURSOR)) {
		(void)gp->scr_move(sp, y, SCNO);

		/*
		 * XXX
		 * If the screen shifted, we recalculate the "most favorite"
		 * cursor position.  Vi won't know that we've warped the
		 * screen, so it's going to have a wrong idea about where the
		 * cursor should be.  This is vi's problem, and fixing it here
		 * is a gross layering violation.
		 */
		if (leftright_warp)
			(void)vs_column(sp, &sp->rcm);
	}

	if (LF_ISSET(UPDATE_SCREEN))
		(void)gp->scr_refresh(sp, F_ISSET(vip, VIP_N_EX_PAINT));

	/* 12: Clear the flags that are handled by this routine. */
	F_CLR(sp, SC_SCR_CENTER | SC_SCR_REDRAW | SC_SCR_REFORMAT | SC_SCR_TOP);
	F_CLR(vip, VIP_CUR_INVALID |
	    VIP_N_EX_PAINT | VIP_N_REFRESH | VIP_N_RENUMBER | VIP_S_MODELINE);

	return (0);

#undef	 LNO
#undef	OLNO
#undef	 CNO
#undef	OCNO
#undef	SCNO
}

/*
 * vs_modeline --
 *	Update the mode line.
 */
static void
vs_modeline(SCR *sp)
{
	static char * const modes[] = {
		"215|Append",			/* SM_APPEND */
		"216|Change",			/* SM_CHANGE */
		"217|Command",			/* SM_COMMAND */
		"218|Insert",			/* SM_INSERT */
		"219|Replace",			/* SM_REPLACE */
	};
	GS *gp;
	size_t cols, curcol, curlen, endpoint, len, midpoint;
	const char *t = NULL;
	int ellipsis;
	char buf[20];

	gp = sp->gp;

	/*
	 * We put down the file name, the ruler, the mode and the dirty flag.
	 * If there's not enough room, there's not enough room, we don't play
	 * any special games.  We try to put the ruler in the middle and the
	 * mode and dirty flag at the end.
	 *
	 * !!!
	 * Leave the last character blank, in case it's a really dumb terminal
	 * with hardware scroll.  Second, don't paint the last character in the
	 * screen, SunOS 4.1.1 and Ultrix 4.2 curses won't let you.
	 *
	 * Move to the last line on the screen.
	 */
	(void)gp->scr_move(sp, LASTLINE(sp), 0);

	/* If more than one screen in the display, show the file name. */
	curlen = 0;
	if (IS_SPLIT(sp)) {
		CHAR_T *wp, *p;
		size_t l;

		CHAR2INT(sp, sp->frp->name, strlen(sp->frp->name) + 1, wp, l);
		p = wp + l;
		for (ellipsis = 0, cols = sp->cols / 2; --p > wp;) {
			if (*p == '/') {
				++p;
				break;
			}
			if ((curlen += KEY_COL(sp, *p)) > cols) {
				ellipsis = 3;
				curlen +=
				    KEY_LEN(sp, '.') * 3 + KEY_LEN(sp, ' ');
				while (curlen > cols) {
					++p;
					curlen -= KEY_COL(sp, *p);
				}
				break;
			}
		}
		if (ellipsis) {
			while (ellipsis--)
				(void)gp->scr_addstr(sp,
				    KEY_NAME(sp, '.'), KEY_LEN(sp, '.'));
			(void)gp->scr_addstr(sp,
			    KEY_NAME(sp, ' '), KEY_LEN(sp, ' '));
		}
		for (; *p != '\0'; ++p)
			(void)gp->scr_addstr(sp,
			    KEY_NAME(sp, *p), KEY_COL(sp, *p));
	}

	/* Clear the rest of the line. */
	(void)gp->scr_clrtoeol(sp);

	/*
	 * Display the ruler.  If we're not at the midpoint yet, move there.
	 * Otherwise, add in two extra spaces.
	 *
	 * Adjust the current column for the fact that the editor uses it as
	 * a zero-based number.
	 *
	 * XXX
	 * Assume that numbers, commas, and spaces only take up a single
	 * column on the screen.
	 */
	cols = sp->cols - 1;
	if (O_ISSET(sp, O_RULER)) {
		vs_column(sp, &curcol);
		len = snprintf(buf, sizeof(buf), "%lu,%lu",
		    (u_long)sp->lno, (u_long)(curcol + 1));

		midpoint = (cols - ((len + 1) / 2)) / 2;
		if (curlen < midpoint) {
			(void)gp->scr_move(sp, LASTLINE(sp), midpoint);
			curlen += len;
		} else if (curlen + 2 + len < cols) {
			(void)gp->scr_addstr(sp, "  ", 2);
			curlen += 2 + len;
		}
		(void)gp->scr_addstr(sp, buf, len);
	}

	/*
	 * Display the mode and the modified flag, as close to the end of the
	 * line as possible, but guaranteeing at least two spaces between the
	 * ruler and the modified flag.
	 */
#define	MODESIZE	9
	endpoint = cols;
	if (O_ISSET(sp, O_SHOWMODE)) {
		if (F_ISSET(sp->ep, F_MODIFIED))
			--endpoint;
		t = msg_cat(sp, modes[sp->showmode], &len);
		endpoint -= len;
	}

	if (endpoint > curlen + 2) {
		(void)gp->scr_move(sp, LASTLINE(sp), endpoint);
		if (O_ISSET(sp, O_SHOWMODE)) {
			if (F_ISSET(sp->ep, F_MODIFIED))
				(void)gp->scr_addstr(sp,
				    KEY_NAME(sp, '*'), KEY_LEN(sp, '*'));
			(void)gp->scr_addstr(sp, t, len);
		}
	}
}
