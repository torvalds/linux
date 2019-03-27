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
static const char sccsid[] = "$Id: vs_smap.c,v 10.31 2011/02/26 13:56:21 skimo Exp $";
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
#include "vi.h"

static int	vs_deleteln(SCR *, int);
static int	vs_insertln(SCR *, int);
static int	vs_sm_delete(SCR *, recno_t);
static int	vs_sm_down(SCR *, MARK *, recno_t, scroll_t, SMAP *);
static int	vs_sm_erase(SCR *);
static int	vs_sm_insert(SCR *, recno_t);
static int	vs_sm_reset(SCR *, recno_t);
static int	vs_sm_up(SCR *, MARK *, recno_t, scroll_t, SMAP *);

/*
 * vs_change --
 *	Make a change to the screen.
 *
 * PUBLIC: int vs_change(SCR *, recno_t, lnop_t);
 */
int
vs_change(SCR *sp, recno_t lno, lnop_t op)
{
	VI_PRIVATE *vip;
	SMAP *p;
	size_t cnt, oldy, oldx;

	vip = VIP(sp);

	/*
	 * XXX
	 * Very nasty special case.  The historic vi code displays a single
	 * space (or a '$' if the list option is set) for the first line in
	 * an "empty" file.  If we "insert" a line, that line gets scrolled
	 * down, not repainted, so it's incorrect when we refresh the screen.
	 * The vi text input functions detect it explicitly and don't insert
	 * a new line.
	 *
	 * Check for line #2 before going to the end of the file.
	 */
	if (((op == LINE_APPEND && lno == 0) || 
	    (op == LINE_INSERT && lno == 1)) &&
	    !db_exist(sp, 2)) {
		lno = 1;
		op = LINE_RESET;
	}

	/* Appending is the same as inserting, if the line is incremented. */
	if (op == LINE_APPEND) {
		++lno;
		op = LINE_INSERT;
	}

	/* Ignore the change if the line is after the map. */
	if (lno > TMAP->lno)
		return (0);

	/*
	 * If the line is before the map, and it's a decrement, decrement
	 * the map.  If it's an increment, increment the map.  Otherwise,
	 * ignore it.
	 */
	if (lno < HMAP->lno) {
		switch (op) {
		case LINE_APPEND:
			abort();
			/* NOTREACHED */
		case LINE_DELETE:
			for (p = HMAP, cnt = sp->t_rows; cnt--; ++p)
				--p->lno;
			if (sp->lno >= lno)
				--sp->lno;
			F_SET(vip, VIP_N_RENUMBER);
			break;
		case LINE_INSERT:
			for (p = HMAP, cnt = sp->t_rows; cnt--; ++p)
				++p->lno;
			if (sp->lno >= lno)
				++sp->lno;
			F_SET(vip, VIP_N_RENUMBER);
			break;
		case LINE_RESET:
			break;
		}
		return (0);
	}

	F_SET(vip, VIP_N_REFRESH);

	/*
	 * Invalidate the line size cache, and invalidate the cursor if it's
	 * on this line,
	 */
	VI_SCR_CFLUSH(vip);
	if (sp->lno == lno)
		F_SET(vip, VIP_CUR_INVALID);

	/*
	 * If ex modifies the screen after ex output is already on the screen
	 * or if we've switched into ex canonical mode, don't touch it -- we'll
	 * get scrolling wrong, at best.
	 */
	if (!F_ISSET(sp, SC_TINPUT_INFO) &&
	    (F_ISSET(sp, SC_SCR_EXWROTE) || VIP(sp)->totalcount > 1)) {
		F_SET(vip, VIP_N_EX_REDRAW);
		return (0);
	}

	/* Save and restore the cursor for these routines. */
	(void)sp->gp->scr_cursor(sp, &oldy, &oldx);

	switch (op) {
	case LINE_DELETE:
		if (vs_sm_delete(sp, lno))
			return (1);
		if (sp->lno > lno)
			--sp->lno;
		F_SET(vip, VIP_N_RENUMBER);
		break;
	case LINE_INSERT:
		if (vs_sm_insert(sp, lno))
			return (1);
		if (sp->lno > lno)
			++sp->lno;
		F_SET(vip, VIP_N_RENUMBER);
		break;
	case LINE_RESET:
		if (vs_sm_reset(sp, lno))
			return (1);
		break;
	default:
		abort();
	}

	(void)sp->gp->scr_move(sp, oldy, oldx);
	return (0);
}

/*
 * vs_sm_fill --
 *	Fill in the screen map, placing the specified line at the
 *	right position.  There isn't any way to tell if an SMAP
 *	entry has been filled in, so this routine had better be
 *	called with P_FILL set before anything else is done.
 *
 * !!!
 * Unexported interface: if lno is OOBLNO, P_TOP means that the HMAP
 * slot is already filled in, P_BOTTOM means that the TMAP slot is
 * already filled in, and we just finish up the job.
 *
 * PUBLIC: int vs_sm_fill(SCR *, recno_t, pos_t);
 */
int
vs_sm_fill(SCR *sp, recno_t lno, pos_t pos)
{
	SMAP *p, tmp;
	size_t cnt;

	/* Flush all cached information from the SMAP. */
	for (p = HMAP, cnt = sp->t_rows; cnt--; ++p)
		SMAP_FLUSH(p);

	/*
	 * If the map is filled, the screen must be redrawn.
	 *
	 * XXX
	 * This is a bug.  We should try and figure out if the desired line
	 * is already in the map or close by -- scrolling the screen would
	 * be a lot better than redrawing.
	 */
	F_SET(sp, SC_SCR_REDRAW);

	switch (pos) {
	case P_FILL:
		tmp.lno = 1;
		tmp.coff = 0;
		tmp.soff = 1;

		/* See if less than half a screen from the top. */
		if (vs_sm_nlines(sp,
		    &tmp, lno, HALFTEXT(sp)) <= HALFTEXT(sp)) {
			lno = 1;
			goto top;
		}

		/* See if less than half a screen from the bottom. */
		if (db_last(sp, &tmp.lno))
			return (1);
		tmp.coff = 0;
		tmp.soff = vs_screens(sp, tmp.lno, NULL);
		if (vs_sm_nlines(sp,
		    &tmp, lno, HALFTEXT(sp)) <= HALFTEXT(sp)) {
			TMAP->lno = tmp.lno;
			TMAP->coff = tmp.coff;
			TMAP->soff = tmp.soff;
			goto bottom;
		}
		goto middle;
	case P_TOP:
		if (lno != OOBLNO) {
top:			HMAP->lno = lno;
			HMAP->coff = 0;
			HMAP->soff = 1;
		} else {
			/*
			 * If number of lines HMAP->lno (top line) spans
			 * changed due to, say reformatting, and now is
			 * fewer than HMAP->soff, reset so the line is
			 * redrawn at the top of the screen.
			 */
			cnt = vs_screens(sp, HMAP->lno, NULL);
			if (cnt < HMAP->soff)
				HMAP->soff = 1;
		}
		/* If we fail, just punt. */
		for (p = HMAP, cnt = sp->t_rows; --cnt; ++p)
			if (vs_sm_next(sp, p, p + 1))
				goto err;
		break;
	case P_MIDDLE:
		/* If we fail, guess that the file is too small. */
middle:		p = HMAP + sp->t_rows / 2;
		p->lno = lno;
		p->coff = 0;
		p->soff = 1;
		for (; p > HMAP; --p)
			if (vs_sm_prev(sp, p, p - 1)) {
				lno = 1;
				goto top;
			}

		/* If we fail, just punt. */
		p = HMAP + sp->t_rows / 2;
		for (; p < TMAP; ++p)
			if (vs_sm_next(sp, p, p + 1))
				goto err;
		break;
	case P_BOTTOM:
		if (lno != OOBLNO) {
			TMAP->lno = lno;
			TMAP->coff = 0;
			TMAP->soff = vs_screens(sp, lno, NULL);
		}
		/* If we fail, guess that the file is too small. */
bottom:		for (p = TMAP; p > HMAP; --p)
			if (vs_sm_prev(sp, p, p - 1)) {
				lno = 1;
				goto top;
			}
		break;
	default:
		abort();
	}
	return (0);

	/*
	 * Try and put *something* on the screen.  If this fails, we have a
	 * serious hard error.
	 */
err:	HMAP->lno = 1;
	HMAP->coff = 0;
	HMAP->soff = 1;
	for (p = HMAP; p < TMAP; ++p)
		if (vs_sm_next(sp, p, p + 1))
			return (1);
	return (0);
}

/*
 * For the routines vs_sm_reset, vs_sm_delete and vs_sm_insert: if the
 * screen contains only a single line (whether because the screen is small
 * or the line large), it gets fairly exciting.  Skip the fun, set a flag
 * so the screen map is refilled and the screen redrawn, and return.  This
 * is amazingly slow, but it's not clear that anyone will care.
 */
#define	HANDLE_WEIRDNESS(cnt) {						\
	if (cnt >= sp->t_rows) {					\
		F_SET(sp, SC_SCR_REFORMAT);				\
		return (0);						\
	}								\
}

/*
 * vs_sm_delete --
 *	Delete a line out of the SMAP.
 */
static int
vs_sm_delete(SCR *sp, recno_t lno)
{
	SMAP *p, *t;
	size_t cnt_orig;

	/*
	 * Find the line in the map, and count the number of screen lines
	 * which display any part of the deleted line.
	 */
	for (p = HMAP; p->lno != lno; ++p);
	if (O_ISSET(sp, O_LEFTRIGHT))
		cnt_orig = 1;
	else
		for (cnt_orig = 1, t = p + 1;
		    t <= TMAP && t->lno == lno; ++cnt_orig, ++t);

	HANDLE_WEIRDNESS(cnt_orig);

	/* Delete that many lines from the screen. */
	(void)sp->gp->scr_move(sp, p - HMAP, 0);
	if (vs_deleteln(sp, cnt_orig))
		return (1);

	/* Shift the screen map up. */
	memmove(p, p + cnt_orig, (((TMAP - p) - cnt_orig) + 1) * sizeof(SMAP));

	/* Decrement the line numbers for the rest of the map. */
	for (t = TMAP - cnt_orig; p <= t; ++p)
		--p->lno;

	/* Display the new lines. */
	for (p = TMAP - cnt_orig;;) {
		if (p < TMAP && vs_sm_next(sp, p, p + 1))
			return (1);
		/* vs_sm_next() flushed the cache. */
		if (vs_line(sp, ++p, NULL, NULL))
			return (1);
		if (p == TMAP)
			break;
	}
	return (0);
}

/*
 * vs_sm_insert --
 *	Insert a line into the SMAP.
 */
static int
vs_sm_insert(SCR *sp, recno_t lno)
{
	SMAP *p, *t;
	size_t cnt_orig, cnt, coff;

	/* Save the offset. */
	coff = HMAP->coff;

	/*
	 * Find the line in the map, find out how many screen lines
	 * needed to display the line.
	 */
	for (p = HMAP; p->lno != lno; ++p);

	cnt_orig = vs_screens(sp, lno, NULL);
	HANDLE_WEIRDNESS(cnt_orig);

	/*
	 * The lines left in the screen override the number of screen
	 * lines in the inserted line.
	 */
	cnt = (TMAP - p) + 1;
	if (cnt_orig > cnt)
		cnt_orig = cnt;

	/* Push down that many lines. */
	(void)sp->gp->scr_move(sp, p - HMAP, 0);
	if (vs_insertln(sp, cnt_orig))
		return (1);

	/* Shift the screen map down. */
	memmove(p + cnt_orig, p, (((TMAP - p) - cnt_orig) + 1) * sizeof(SMAP));

	/* Increment the line numbers for the rest of the map. */
	for (t = p + cnt_orig; t <= TMAP; ++t)
		++t->lno;

	/* Fill in the SMAP for the new lines, and display. */
	for (cnt = 1, t = p; cnt <= cnt_orig; ++t, ++cnt) {
		t->lno = lno;
		t->coff = coff;
		t->soff = cnt;
		SMAP_FLUSH(t);
		if (vs_line(sp, t, NULL, NULL))
			return (1);
	}
	return (0);
}

/*
 * vs_sm_reset --
 *	Reset a line in the SMAP.
 */
static int
vs_sm_reset(SCR *sp, recno_t lno)
{
	SMAP *p, *t;
	size_t cnt_orig, cnt_new, cnt, diff;

	/*
	 * See if the number of on-screen rows taken up by the old display
	 * for the line is the same as the number needed for the new one.
	 * If so, repaint, otherwise do it the hard way.
	 */
	for (p = HMAP; p->lno != lno; ++p);
	if (O_ISSET(sp, O_LEFTRIGHT)) {
		t = p;
		cnt_orig = cnt_new = 1;
	} else {
		for (cnt_orig = 0,
		    t = p; t <= TMAP && t->lno == lno; ++cnt_orig, ++t);
		cnt_new = vs_screens(sp, lno, NULL);
	}

	HANDLE_WEIRDNESS(cnt_orig);

	if (cnt_orig == cnt_new) {
		do {
			SMAP_FLUSH(p);
			if (vs_line(sp, p, NULL, NULL))
				return (1);
		} while (++p < t);
		return (0);
	}

	if (cnt_orig < cnt_new) {
		/* Get the difference. */
		diff = cnt_new - cnt_orig;

		/*
		 * The lines left in the screen override the number of screen
		 * lines in the inserted line.
		 */
		cnt = (TMAP - p) + 1;
		if (diff > cnt)
			diff = cnt;

		/* If there are any following lines, push them down. */
		if (cnt > 1) {
			(void)sp->gp->scr_move(sp, p - HMAP, 0);
			if (vs_insertln(sp, diff))
				return (1);

			/* Shift the screen map down. */
			memmove(p + diff, p,
			    (((TMAP - p) - diff) + 1) * sizeof(SMAP));
		}

		/* Fill in the SMAP for the replaced line, and display. */
		for (cnt = 1, t = p; cnt_new-- && t <= TMAP; ++t, ++cnt) {
			t->lno = lno;
			t->soff = cnt;
			SMAP_FLUSH(t);
			if (vs_line(sp, t, NULL, NULL))
				return (1);
		}
	} else {
		/* Get the difference. */
		diff = cnt_orig - cnt_new;

		/* Delete that many lines from the screen. */
		(void)sp->gp->scr_move(sp, p - HMAP, 0);
		if (vs_deleteln(sp, diff))
			return (1);

		/* Shift the screen map up. */
		memmove(p, p + diff, (((TMAP - p) - diff) + 1) * sizeof(SMAP));

		/* Fill in the SMAP for the replaced line, and display. */
		for (cnt = 1, t = p; cnt_new--; ++t, ++cnt) {
			t->lno = lno;
			t->soff = cnt;
			SMAP_FLUSH(t);
			if (vs_line(sp, t, NULL, NULL))
				return (1);
		}

		/* Display the new lines at the bottom of the screen. */
		for (t = TMAP - diff;;) {
			if (t < TMAP && vs_sm_next(sp, t, t + 1))
				return (1);
			/* vs_sm_next() flushed the cache. */
			if (vs_line(sp, ++t, NULL, NULL))
				return (1);
			if (t == TMAP)
				break;
		}
	}
	return (0);
}

/*
 * vs_sm_scroll
 *	Scroll the SMAP up/down count logical lines.  Different
 *	semantics based on the vi command, *sigh*.
 *
 * PUBLIC: int vs_sm_scroll(SCR *, MARK *, recno_t, scroll_t);
 */
int
vs_sm_scroll(SCR *sp, MARK *rp, recno_t count, scroll_t scmd)
{
	SMAP *smp;

	/*
	 * Invalidate the cursor.  The line is probably going to change,
	 * (although for ^E and ^Y it may not).  In any case, the scroll
	 * routines move the cursor to draw things.
	 */
	F_SET(VIP(sp), VIP_CUR_INVALID);

	/* Find the cursor in the screen. */
	if (vs_sm_cursor(sp, &smp))
		return (1);

	switch (scmd) {
	case CNTRL_B:
	case CNTRL_U:
	case CNTRL_Y:
	case Z_CARAT:
		if (vs_sm_down(sp, rp, count, scmd, smp))
			return (1);
		break;
	case CNTRL_D:
	case CNTRL_E:
	case CNTRL_F:
	case Z_PLUS:
		if (vs_sm_up(sp, rp, count, scmd, smp))
			return (1);
		break;
	default:
		abort();
	}

	/*
	 * !!!
	 * If we're at the start of a line, go for the first non-blank.
	 * This makes it look like the old vi, even though we're moving
	 * around by logical lines, not physical ones.
	 *
	 * XXX
	 * In the presence of a long line, which has more than a screen
	 * width of leading spaces, this code can cause a cursor warp.
	 * Live with it.
	 */
	if (scmd != CNTRL_E && scmd != CNTRL_Y &&
	    rp->cno == 0 && nonblank(sp, rp->lno, &rp->cno))
		return (1);

	return (0);
}

/*
 * vs_sm_up --
 *	Scroll the SMAP up count logical lines.
 */
static int
vs_sm_up(SCR *sp, MARK *rp, recno_t count, scroll_t scmd, SMAP *smp)
{
	int cursor_set, echanged, zset;
	SMAP *ssmp, s1, s2;

	/*
	 * Check to see if movement is possible.
	 *
	 * Get the line after the map.  If that line is a new one (and if
	 * O_LEFTRIGHT option is set, this has to be true), and the next
	 * line doesn't exist, and the cursor doesn't move, or the cursor
	 * isn't even on the screen, or the cursor is already at the last
	 * line in the map, it's an error.  If that test succeeded because
	 * the cursor wasn't at the end of the map, test to see if the map
	 * is mostly empty.
	 */
	if (vs_sm_next(sp, TMAP, &s1))
		return (1);
	if (s1.lno > TMAP->lno && !db_exist(sp, s1.lno)) {
		if (scmd == CNTRL_E || scmd == Z_PLUS || smp == TMAP) {
			v_eof(sp, NULL);
			return (1);
		}
		if (vs_sm_next(sp, smp, &s1))
			return (1);
		if (s1.lno > smp->lno && !db_exist(sp, s1.lno)) {
			v_eof(sp, NULL);
			return (1);
		}
	}

	/*
	 * Small screens: see vs_refresh.c section 6a.
	 *
	 * If it's a small screen, and the movement isn't larger than a
	 * screen, i.e some context will remain, open up the screen and
	 * display by scrolling.  In this case, the cursor moves down one
	 * line for each line displayed.  Otherwise, erase/compress and
	 * repaint, and move the cursor to the first line in the screen.
	 * Note, the ^F command is always in the latter case, for historical
	 * reasons.
	 */
	cursor_set = 0;
	if (IS_SMALL(sp)) {
		if (count >= sp->t_maxrows || scmd == CNTRL_F) {
			s1 = TMAP[0];
			if (vs_sm_erase(sp))
				return (1);
			for (; count--; s1 = s2) {
				if (vs_sm_next(sp, &s1, &s2))
					return (1);
				if (s2.lno != s1.lno && !db_exist(sp, s2.lno))
					break;
			}
			TMAP[0] = s2;
			if (vs_sm_fill(sp, OOBLNO, P_BOTTOM))
				return (1);
			return (vs_sm_position(sp, rp, 0, P_TOP));
		}
		cursor_set = scmd == CNTRL_E || vs_sm_cursor(sp, &ssmp);
		for (; count &&
		    sp->t_rows != sp->t_maxrows; --count, ++sp->t_rows) {
			if (vs_sm_next(sp, TMAP, &s1))
				return (1);
			if (TMAP->lno != s1.lno && !db_exist(sp, s1.lno))
				break;
			*++TMAP = s1;
			/* vs_sm_next() flushed the cache. */
			if (vs_line(sp, TMAP, NULL, NULL))
				return (1);

			if (!cursor_set)
				++ssmp;
		}
		if (!cursor_set) {
			rp->lno = ssmp->lno;
			rp->cno = ssmp->c_sboff;
		}
		if (count == 0)
			return (0);
	}

	for (echanged = zset = 0; count; --count) {
		/* Decide what would show up on the screen. */
		if (vs_sm_next(sp, TMAP, &s1))
			return (1);

		/* If the line doesn't exist, we're done. */
		if (TMAP->lno != s1.lno && !db_exist(sp, s1.lno))
			break;

		/* Scroll the screen cursor up one logical line. */
		if (vs_sm_1up(sp))
			return (1);
		switch (scmd) {
		case CNTRL_E:
			if (smp > HMAP)
				--smp;
			else
				echanged = 1;
			break;
		case Z_PLUS:
			if (zset) {
				if (smp > HMAP)
					--smp;
			} else {
				smp = TMAP;
				zset = 1;
			}
			/* FALLTHROUGH */
		default:
			break;
		}
	}

	if (cursor_set)
		return(0);

	switch (scmd) {
	case CNTRL_E:
		/*
		 * On a ^E that was forced to change lines, try and keep the
		 * cursor as close as possible to the last position, but also
		 * set it up so that the next "real" movement will return the
		 * cursor to the closest position to the last real movement.
		 */
		if (echanged) {
			rp->lno = smp->lno;
			rp->cno = vs_colpos(sp, smp->lno,
			    (O_ISSET(sp, O_LEFTRIGHT) ? 
			    smp->coff : (smp->soff - 1) * sp->cols) +
			    sp->rcm % sp->cols);
		}
		return (0);
	case CNTRL_F:
		/*
		 * If there are more lines, the ^F command is positioned at
		 * the first line of the screen.
		 */
		if (!count) {
			smp = HMAP;
			break;
		}
		/* FALLTHROUGH */
	case CNTRL_D:
		/*
		 * The ^D and ^F commands move the cursor towards EOF
		 * if there are more lines to move.  Check to be sure
		 * the lines actually exist.  (They may not if the
		 * file is smaller than the screen.)
		 */
		for (; count; --count, ++smp)
			if (smp == TMAP || !db_exist(sp, smp[1].lno))
				break;
		break;
	case Z_PLUS:
		 /* The z+ command moves the cursor to the first new line. */
		break;
	default:
		abort();
	}

	if (!SMAP_CACHE(smp) && vs_line(sp, smp, NULL, NULL))
		return (1);
	rp->lno = smp->lno;
	rp->cno = smp->c_scoff == 255 ? 0 : smp->c_sboff;
	return (0);
}

/*
 * vs_sm_1up --
 *	Scroll the SMAP up one.
 *
 * PUBLIC: int vs_sm_1up(SCR *);
 */
int
vs_sm_1up(SCR *sp)
{
	/*
	 * Delete the top line of the screen.  Shift the screen map
	 * up and display a new line at the bottom of the screen.
	 */
	(void)sp->gp->scr_move(sp, 0, 0);
	if (vs_deleteln(sp, 1))
		return (1);

	/* One-line screens can fail. */
	if (IS_ONELINE(sp)) {
		if (vs_sm_next(sp, TMAP, TMAP))
			return (1);
	} else {
		memmove(HMAP, HMAP + 1, (sp->rows - 1) * sizeof(SMAP));
		if (vs_sm_next(sp, TMAP - 1, TMAP))
			return (1);
	}
	/* vs_sm_next() flushed the cache. */
	return (vs_line(sp, TMAP, NULL, NULL));
}

/*
 * vs_deleteln --
 *	Delete a line a la curses, make sure to put the information
 *	line and other screens back.
 */
static int
vs_deleteln(SCR *sp, int cnt)
{
	GS *gp;
	size_t oldy, oldx;

	gp = sp->gp;

	/* If the screen is vertically split, we can't scroll it. */
	if (IS_VSPLIT(sp)) {
		F_SET(sp, SC_SCR_REDRAW);
		return (0);
	}
		
	if (IS_ONELINE(sp))
		(void)gp->scr_clrtoeol(sp);
	else {
		(void)gp->scr_cursor(sp, &oldy, &oldx);
		while (cnt--) {
			(void)gp->scr_deleteln(sp);
			(void)gp->scr_move(sp, LASTLINE(sp), 0);
			(void)gp->scr_insertln(sp);
			(void)gp->scr_move(sp, oldy, oldx);
		}
	}
	return (0);
}

/*
 * vs_sm_down --
 *	Scroll the SMAP down count logical lines.
 */
static int
vs_sm_down(SCR *sp, MARK *rp, recno_t count, scroll_t scmd, SMAP *smp)
{
	SMAP *ssmp, s1, s2;
	int cursor_set, ychanged, zset;

	/* Check to see if movement is possible. */
	if (HMAP->lno == 1 &&
	    (O_ISSET(sp, O_LEFTRIGHT) || HMAP->soff == 1) &&
	    (scmd == CNTRL_Y || scmd == Z_CARAT || smp == HMAP)) {
		v_sof(sp, NULL);
		return (1);
	}

	/*
	 * Small screens: see vs_refresh.c section 6a.
	 *
	 * If it's a small screen, and the movement isn't larger than a
	 * screen, i.e some context will remain, open up the screen and
	 * display by scrolling.  In this case, the cursor moves up one
	 * line for each line displayed.  Otherwise, erase/compress and
	 * repaint, and move the cursor to the first line in the screen.
	 * Note, the ^B command is always in the latter case, for historical
	 * reasons.
	 */
	cursor_set = scmd == CNTRL_Y;
	if (IS_SMALL(sp)) {
		if (count >= sp->t_maxrows || scmd == CNTRL_B) {
			s1 = HMAP[0];
			if (vs_sm_erase(sp))
				return (1);
			for (; count--; s1 = s2) {
				if (vs_sm_prev(sp, &s1, &s2))
					return (1);
				if (s2.lno == 1 &&
				    (O_ISSET(sp, O_LEFTRIGHT) || s2.soff == 1))
					break;
			}
			HMAP[0] = s2;
			if (vs_sm_fill(sp, OOBLNO, P_TOP))
				return (1);
			return (vs_sm_position(sp, rp, 0, P_BOTTOM));
		}
		cursor_set = scmd == CNTRL_Y || vs_sm_cursor(sp, &ssmp);
		for (; count &&
		    sp->t_rows != sp->t_maxrows; --count, ++sp->t_rows) {
			if (HMAP->lno == 1 &&
			    (O_ISSET(sp, O_LEFTRIGHT) || HMAP->soff == 1))
				break;
			++TMAP;
			if (vs_sm_1down(sp))
				return (1);
		}
		if (!cursor_set) {
			rp->lno = ssmp->lno;
			rp->cno = ssmp->c_sboff;
		}
		if (count == 0)
			return (0);
	}

	for (ychanged = zset = 0; count; --count) {
		/* If the line doesn't exist, we're done. */
		if (HMAP->lno == 1 &&
		    (O_ISSET(sp, O_LEFTRIGHT) || HMAP->soff == 1))
			break;

		/* Scroll the screen and cursor down one logical line. */
		if (vs_sm_1down(sp))
			return (1);
		switch (scmd) {
		case CNTRL_Y:
			if (smp < TMAP)
				++smp;
			else
				ychanged = 1;
			break;
		case Z_CARAT:
			if (zset) {
				if (smp < TMAP)
					++smp;
			} else {
				smp = HMAP;
				zset = 1;
			}
			/* FALLTHROUGH */
		default:
			break;
		}
	}

	if (scmd != CNTRL_Y && cursor_set)
		return(0);

	switch (scmd) {
	case CNTRL_B:
		/*
		 * If there are more lines, the ^B command is positioned at
		 * the last line of the screen.  However, the line may not
		 * exist.
		 */
		if (!count) {
			for (smp = TMAP; smp > HMAP; --smp)
				if (db_exist(sp, smp->lno))
					break;
			break;
		}
		/* FALLTHROUGH */
	case CNTRL_U:
		/*
		 * The ^B and ^U commands move the cursor towards SOF
		 * if there are more lines to move.
		 */
		if (count < smp - HMAP)
			smp -= count;
		else
			smp = HMAP;
		break;
	case CNTRL_Y:
		/*
		 * On a ^Y that was forced to change lines, try and keep the
		 * cursor as close as possible to the last position, but also
		 * set it up so that the next "real" movement will return the
		 * cursor to the closest position to the last real movement.
		 */
		if (ychanged) {
			rp->lno = smp->lno;
			rp->cno = vs_colpos(sp, smp->lno,
			    (O_ISSET(sp, O_LEFTRIGHT) ? 
			    smp->coff : (smp->soff - 1) * sp->cols) +
			    sp->rcm % sp->cols);
		}
		return (0);
	case Z_CARAT:
		 /* The z^ command moves the cursor to the first new line. */
		break;
	default:
		abort();
	}

	if (!SMAP_CACHE(smp) && vs_line(sp, smp, NULL, NULL))
		return (1);
	rp->lno = smp->lno;
	rp->cno = smp->c_scoff == 255 ? 0 : smp->c_sboff;
	return (0);
}

/*
 * vs_sm_erase --
 *	Erase the small screen area for the scrolling functions.
 */
static int
vs_sm_erase(SCR *sp)
{
	GS *gp;

	gp = sp->gp;
	(void)gp->scr_move(sp, LASTLINE(sp), 0);
	(void)gp->scr_clrtoeol(sp);
	for (; sp->t_rows > sp->t_minrows; --sp->t_rows, --TMAP) {
		(void)gp->scr_move(sp, TMAP - HMAP, 0);
		(void)gp->scr_clrtoeol(sp);
	}
	return (0);
}

/*
 * vs_sm_1down --
 *	Scroll the SMAP down one.
 *
 * PUBLIC: int vs_sm_1down(SCR *);
 */
int
vs_sm_1down(SCR *sp)
{
	/*
	 * Insert a line at the top of the screen.  Shift the screen map
	 * down and display a new line at the top of the screen.
	 */
	(void)sp->gp->scr_move(sp, 0, 0);
	if (vs_insertln(sp, 1))
		return (1);

	/* One-line screens can fail. */
	if (IS_ONELINE(sp)) {
		if (vs_sm_prev(sp, HMAP, HMAP))
			return (1);
	} else {
		memmove(HMAP + 1, HMAP, (sp->rows - 1) * sizeof(SMAP));
		if (vs_sm_prev(sp, HMAP + 1, HMAP))
			return (1);
	}
	/* vs_sm_prev() flushed the cache. */
	return (vs_line(sp, HMAP, NULL, NULL));
}

/*
 * vs_insertln --
 *	Insert a line a la curses, make sure to put the information
 *	line and other screens back.
 */
static int
vs_insertln(SCR *sp, int cnt)
{
	GS *gp;
	size_t oldy, oldx;

	gp = sp->gp;

	/* If the screen is vertically split, we can't scroll it. */
	if (IS_VSPLIT(sp)) {
		F_SET(sp, SC_SCR_REDRAW);
		return (0);
	}

	if (IS_ONELINE(sp)) {
		(void)gp->scr_move(sp, LASTLINE(sp), 0);
		(void)gp->scr_clrtoeol(sp);
	} else {
		(void)gp->scr_cursor(sp, &oldy, &oldx);
		while (cnt--) {
			(void)gp->scr_move(sp, LASTLINE(sp) - 1, 0);
			(void)gp->scr_deleteln(sp);
			(void)gp->scr_move(sp, oldy, oldx);
			(void)gp->scr_insertln(sp);
		}
	}
	return (0);
}

/*
 * vs_sm_next --
 *	Fill in the next entry in the SMAP.
 *
 * PUBLIC: int vs_sm_next(SCR *, SMAP *, SMAP *);
 */
int
vs_sm_next(SCR *sp, SMAP *p, SMAP *t)
{
	size_t lcnt;

	SMAP_FLUSH(t);
	if (O_ISSET(sp, O_LEFTRIGHT)) {
		t->lno = p->lno + 1;
		t->coff = p->coff;
	} else {
		lcnt = vs_screens(sp, p->lno, NULL);
		if (lcnt == p->soff) {
			t->lno = p->lno + 1;
			t->soff = 1;
		} else {
			t->lno = p->lno;
			t->soff = p->soff + 1;
		}
	}
	return (0);
}

/*
 * vs_sm_prev --
 *	Fill in the previous entry in the SMAP.
 *
 * PUBLIC: int vs_sm_prev(SCR *, SMAP *, SMAP *);
 */
int
vs_sm_prev(SCR *sp, SMAP *p, SMAP *t)
{
	SMAP_FLUSH(t);
	if (O_ISSET(sp, O_LEFTRIGHT)) {
		t->lno = p->lno - 1;
		t->coff = p->coff;
	} else {
		if (p->soff != 1) {
			t->lno = p->lno;
			t->soff = p->soff - 1;
		} else {
			t->lno = p->lno - 1;
			t->soff = vs_screens(sp, t->lno, NULL);
		}
	}
	return (t->lno == 0);
}

/*
 * vs_sm_cursor --
 *	Return the SMAP entry referenced by the cursor.
 *
 * PUBLIC: int vs_sm_cursor(SCR *, SMAP **);
 */
int
vs_sm_cursor(SCR *sp, SMAP **smpp)
{
	SMAP *p;

	/* See if the cursor is not in the map. */
	if (sp->lno < HMAP->lno || sp->lno > TMAP->lno)
		return (1);

	/* Find the first occurence of the line. */
	for (p = HMAP; p->lno != sp->lno; ++p);

	/* Fill in the map information until we find the right line. */
	for (; p <= TMAP; ++p) {
		/* Short lines are common and easy to detect. */
		if (p != TMAP && (p + 1)->lno != p->lno) {
			*smpp = p;
			return (0);
		}
		if (!SMAP_CACHE(p) && vs_line(sp, p, NULL, NULL))
			return (1);
		if (p->c_eboff >= sp->cno) {
			*smpp = p;
			return (0);
		}
	}

	/* It was past the end of the map after all. */
	return (1);
}

/*
 * vs_sm_position --
 *	Return the line/column of the top, middle or last line on the screen.
 *	(The vi H, M and L commands.)  Here because only the screen routines
 *	know what's really out there.
 *
 * PUBLIC: int vs_sm_position(SCR *, MARK *, u_long, pos_t);
 */
int
vs_sm_position(SCR *sp, MARK *rp, u_long cnt, pos_t pos)
{
	SMAP *smp;
	recno_t last;

	switch (pos) {
	case P_TOP:
		/*
		 * !!!
		 * Historically, an invalid count to the H command failed.
		 * We do nothing special here, just making sure that H in
		 * an empty screen works.
		 */
		if (cnt > TMAP - HMAP)
			goto sof;
		smp = HMAP + cnt;
		if (cnt && !db_exist(sp, smp->lno)) {
sof:			msgq(sp, M_BERR, "220|Movement past the end-of-screen");
			return (1);
		}
		break;
	case P_MIDDLE:
		/*
		 * !!!
		 * Historically, a count to the M command was ignored.
		 * If the screen isn't filled, find the middle of what's
		 * real and move there.
		 */
		if (!db_exist(sp, TMAP->lno)) {
			if (db_last(sp, &last))
				return (1);
			for (smp = TMAP; smp->lno > last && smp > HMAP; --smp);
			if (smp > HMAP)
				smp -= (smp - HMAP) / 2;
		} else
			smp = (HMAP + (TMAP - HMAP) / 2) + cnt;
		break;
	case P_BOTTOM:
		/*
		 * !!!
		 * Historically, an invalid count to the L command failed.
		 * If the screen isn't filled, find the bottom of what's
		 * real and try to offset from there.
		 */
		if (cnt > TMAP - HMAP)
			goto eof;
		smp = TMAP - cnt;
		if (!db_exist(sp, smp->lno)) {
			if (db_last(sp, &last))
				return (1);
			for (; smp->lno > last && smp > HMAP; --smp);
			if (cnt > smp - HMAP) {
eof:				msgq(sp, M_BERR,
			    "221|Movement past the beginning-of-screen");
				return (1);
			}
			smp -= cnt;
		}
		break;
	default:
		abort();
	}

	/* Make sure that the cached information is valid. */
	if (!SMAP_CACHE(smp) && vs_line(sp, smp, NULL, NULL))
		return (1);
	rp->lno = smp->lno;
	rp->cno = smp->c_sboff;

	return (0);
}

/*
 * vs_sm_nlines --
 *	Return the number of screen lines from an SMAP entry to the
 *	start of some file line, less than a maximum value.
 *
 * PUBLIC: recno_t vs_sm_nlines(SCR *, SMAP *, recno_t, size_t);
 */
recno_t
vs_sm_nlines(SCR *sp, SMAP *from_sp, recno_t to_lno, size_t max)
{
	recno_t lno, lcnt;

	if (O_ISSET(sp, O_LEFTRIGHT))
		return (from_sp->lno > to_lno ?
		    from_sp->lno - to_lno : to_lno - from_sp->lno);

	if (from_sp->lno == to_lno)
		return (from_sp->soff - 1);

	if (from_sp->lno > to_lno) {
		lcnt = from_sp->soff - 1;	/* Correct for off-by-one. */
		for (lno = from_sp->lno; --lno >= to_lno && lcnt <= max;)
			lcnt += vs_screens(sp, lno, NULL);
	} else {
		lno = from_sp->lno;
		lcnt = (vs_screens(sp, lno, NULL) - from_sp->soff) + 1;
		for (; ++lno < to_lno && lcnt <= max;)
			lcnt += vs_screens(sp, lno, NULL);
	}
	return (lcnt);
}
