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
static const char sccsid[] = "$Id: vs_split.c,v 10.43 2015/04/05 15:21:55 zy Exp $";
#endif /* not lint */

#include <sys/types.h>
#include <sys/queue.h>
#include <sys/time.h>

#include <bitstring.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../common/common.h"
#include "vi.h"

typedef enum { HORIZ_FOLLOW, HORIZ_PRECEDE, VERT_FOLLOW, VERT_PRECEDE } jdir_t;

static SCR	*vs_getbg(SCR *, char *);
static void      vs_insert(SCR *sp, GS *gp);
static int	 vs_join(SCR *, SCR **, jdir_t *);

/*
 * vs_split --
 *	Create a new screen, horizontally.
 *
 * PUBLIC: int vs_split(SCR *, SCR *, int);
 */
int
vs_split(
	SCR *sp,
	SCR *new,
	int ccl)		/* Colon-command line split. */
{
	GS *gp;
	SMAP *smp;
	size_t half;
	int issmallscreen, splitup;

	gp = sp->gp;

	/* Check to see if it's possible. */
	/* XXX: The IS_ONELINE fix will change this, too. */
	if (sp->rows < 4) {
		msgq(sp, M_ERR,
		    "222|Screen must be larger than %d lines to split", 4 - 1);
		return (1);
	}

	/* Wait for any messages in the screen. */
	vs_resolve(sp, NULL, 1);

	/* Get a new screen map. */
	CALLOC(sp, _HMAP(new), SMAP *, SIZE_HMAP(sp), sizeof(SMAP));
	if (_HMAP(new) == NULL)
		return (1);
	_HMAP(new)->lno = sp->lno;
	_HMAP(new)->coff = 0;
	_HMAP(new)->soff = 1;

	/* Split the screen in half. */
	half = sp->rows / 2;
	if (ccl && half > 6)
		half = 6;

	/*
	 * Small screens: see vs_refresh.c section 6a.  Set a flag so
	 * we know to fix the screen up later.
	 */
	issmallscreen = IS_SMALL(sp);

	/* The columns in the screen don't change. */
	new->coff = sp->coff;
	new->cols = sp->cols;

	/*
	 * Split the screen, and link the screens together.  If creating a
	 * screen to edit the colon command line or the cursor is in the top
	 * half of the current screen, the new screen goes under the current
	 * screen.  Else, it goes above the current screen.
	 *
	 * Recalculate current cursor position based on sp->lno, we're called
	 * with the cursor on the colon command line.  Then split the screen
	 * in half and update the shared information.
	 */
	splitup =
	    !ccl && (vs_sm_cursor(sp, &smp) ? 0 : (smp - HMAP) + 1) >= half;
	if (splitup) {				/* Old is bottom half. */
		new->rows = sp->rows - half;	/* New. */
		new->roff = sp->roff;
		sp->rows = half;		/* Old. */
		sp->roff += new->rows;

		/*
		 * If the parent is the bottom half of the screen, shift
		 * the map down to match on-screen text.
		 */
		memcpy(_HMAP(sp), _HMAP(sp) + new->rows,
		    (sp->t_maxrows - new->rows) * sizeof(SMAP));
	} else {				/* Old is top half. */
		new->rows = half;		/* New. */
		sp->rows -= half;		/* Old. */
		new->roff = sp->roff + sp->rows;
	}

	/* Adjust maximum text count. */
	sp->t_maxrows = IS_ONELINE(sp) ? 1 : sp->rows - 1;
	new->t_maxrows = IS_ONELINE(new) ? 1 : new->rows - 1;

	/*
	 * Small screens: see vs_refresh.c, section 6a.
	 *
	 * The child may have different screen options sizes than the parent,
	 * so use them.  Guarantee that text counts aren't larger than the
	 * new screen sizes.
	 */
	if (issmallscreen) {
		/* Fix the text line count for the parent. */
		if (splitup)
			sp->t_rows -= new->rows;

		/* Fix the parent screen. */
		if (sp->t_rows > sp->t_maxrows)
			sp->t_rows = sp->t_maxrows;
		if (sp->t_minrows > sp->t_maxrows)
			sp->t_minrows = sp->t_maxrows;

		/* Fix the child screen. */
		new->t_minrows = new->t_rows = O_VAL(sp, O_WINDOW);
		if (new->t_rows > new->t_maxrows)
			new->t_rows = new->t_maxrows;
		if (new->t_minrows > new->t_maxrows)
			new->t_minrows = new->t_maxrows;
	} else {
		sp->t_minrows = sp->t_rows = IS_ONELINE(sp) ? 1 : sp->rows - 1;

		/*
		 * The new screen may be a small screen, even if the parent
		 * was not.  Don't complain if O_WINDOW is too large, we're
		 * splitting the screen so the screen is much smaller than
		 * normal.
		 */
		new->t_minrows = new->t_rows = O_VAL(sp, O_WINDOW);
		if (new->t_rows > new->rows - 1)
			new->t_minrows = new->t_rows =
			    IS_ONELINE(new) ? 1 : new->rows - 1;
	}

	/* Adjust the ends of the new and old maps. */
	_TMAP(sp) = IS_ONELINE(sp) ?
	    _HMAP(sp) : _HMAP(sp) + (sp->t_rows - 1);
	_TMAP(new) = IS_ONELINE(new) ?
	    _HMAP(new) : _HMAP(new) + (new->t_rows - 1);

	/* Reset the length of the default scroll. */
	if ((sp->defscroll = sp->t_maxrows / 2) == 0)
		sp->defscroll = 1;
	if ((new->defscroll = new->t_maxrows / 2) == 0)
		new->defscroll = 1;

	/* Fit the screen into the logical chain. */
	vs_insert(new, sp->gp);

	/* Tell the display that we're splitting. */
	(void)gp->scr_split(sp, new);

	/*
	 * Initialize the screen flags:
	 *
	 * If we're in vi mode in one screen, we don't have to reinitialize.
	 * This isn't just a cosmetic fix.  The path goes like this:
	 *
	 *	return into vi(), SC_SSWITCH set
	 *	call vs_refresh() with SC_STATUS set
	 *	call vs_resolve to display the status message
	 *	call vs_refresh() because the SC_SCR_VI bit isn't set
	 *
	 * Things go downhill at this point.
	 *
	 * Draw the new screen from scratch, and add a status line.
	 */
	F_SET(new,
	    SC_SCR_REFORMAT | SC_STATUS |
	    F_ISSET(sp, SC_EX | SC_VI | SC_SCR_VI | SC_SCR_EX | SC_READONLY));
	return (0);
}

/*
 * vs_vsplit --
 *	Create a new screen, vertically.
 *
 * PUBLIC: int vs_vsplit(SCR *, SCR *);
 */
int
vs_vsplit(SCR *sp, SCR *new)
{
	GS *gp;
	size_t cols;

	gp = sp->gp;

	/* Check to see if it's possible. */
	if (sp->cols / 2 <= MINIMUM_SCREEN_COLS) {
		msgq(sp, M_ERR,
		    "288|Screen must be larger than %d columns to split",
		    MINIMUM_SCREEN_COLS * 2);
		return (1);
	}

	/* Wait for any messages in the screen. */
	vs_resolve(sp, NULL, 1);

	/* Get a new screen map. */
	CALLOC(sp, _HMAP(new), SMAP *, SIZE_HMAP(sp), sizeof(SMAP));
	if (_HMAP(new) == NULL)
		return (1);
	_HMAP(new)->lno = sp->lno;
	_HMAP(new)->coff = 0;
	_HMAP(new)->soff = 1;

	/*
	 * Split the screen in half; we have to sacrifice a column to delimit
	 * the screens.
	 *
	 * XXX
	 * We always split to the right... that makes more sense to me, and
	 * I don't want to play the stupid games that I play when splitting
	 * horizontally.
	 *
	 * XXX
	 * We reserve a column for the screen, "knowing" that curses needs
	 * one.  This should be worked out with the display interface.
	 */
	cols = sp->cols / 2;
	new->cols = sp->cols - cols - 1;
	sp->cols = cols;
	new->coff = sp->coff + cols + 1;
	sp->cno = 0;

	/* Nothing else changes. */
	new->rows = sp->rows;
	new->t_rows = sp->t_rows;
	new->t_maxrows = sp->t_maxrows;
	new->t_minrows = sp->t_minrows;
	new->roff = sp->roff;
	new->defscroll = sp->defscroll;
	_TMAP(new) = _HMAP(new) + (new->t_rows - 1);

	/* Fit the screen into the logical chain. */
	vs_insert(new, sp->gp);

	/* Tell the display that we're splitting. */
	(void)gp->scr_split(sp, new);

	/* Redraw the old screen from scratch. */
	F_SET(sp, SC_SCR_REFORMAT | SC_STATUS);

	/*
	 * Initialize the screen flags:
	 *
	 * If we're in vi mode in one screen, we don't have to reinitialize.
	 * This isn't just a cosmetic fix.  The path goes like this:
	 *
	 *	return into vi(), SC_SSWITCH set
	 *	call vs_refresh() with SC_STATUS set
	 *	call vs_resolve to display the status message
	 *	call vs_refresh() because the SC_SCR_VI bit isn't set
	 *
	 * Things go downhill at this point.
	 *
	 * Draw the new screen from scratch, and add a status line.
	 */
	F_SET(new,
	    SC_SCR_REFORMAT | SC_STATUS |
	    F_ISSET(sp, SC_EX | SC_VI | SC_SCR_VI | SC_SCR_EX | SC_READONLY));
	return (0);
}

/*
 * vs_insert --
 *	Insert the new screen into the correct place in the logical
 *	chain.
 */
static void
vs_insert(SCR *sp, GS *gp)
{
	SCR *tsp;

	gp = sp->gp;

	/* Move past all screens with lower row numbers. */
	TAILQ_FOREACH(tsp, gp->dq, q)
		if (tsp->roff >= sp->roff)
			break;
	/*
	 * Move past all screens with the same row number and lower
	 * column numbers.
	 */
	for (; tsp != NULL; tsp = TAILQ_NEXT(tsp, q))
		if (tsp->roff != sp->roff || tsp->coff > sp->coff)
			break;

	/*
	 * If we reached the end, this screen goes there.  Otherwise,
	 * put it before or after the screen where we stopped.
	 */
	if (tsp == NULL) {
		TAILQ_INSERT_TAIL(gp->dq, sp, q);
	} else if (tsp->roff < sp->roff ||
	    (tsp->roff == sp->roff && tsp->coff < sp->coff)) {
		TAILQ_INSERT_AFTER(gp->dq, tsp, sp, q);
	} else
		TAILQ_INSERT_BEFORE(tsp, sp, q);
}

/*
 * vs_discard --
 *	Discard the screen, folding the real-estate into a related screen,
 *	if one exists, and return that screen.
 *
 * PUBLIC: int vs_discard(SCR *, SCR **);
 */
int
vs_discard(SCR *sp, SCR **spp)
{
	GS *gp;
	SCR *tsp, **lp, *list[100];
	jdir_t jdir;

	gp = sp->gp;

	/*
	 * Save the old screen's cursor information.
	 *
	 * XXX
	 * If called after file_end(), and the underlying file was a tmp
	 * file, it may have gone away.
	 */
	if (sp->frp != NULL) {
		sp->frp->lno = sp->lno;
		sp->frp->cno = sp->cno;
		F_SET(sp->frp, FR_CURSORSET);
	}

	/* If no other screens to join, we're done. */
	if (!IS_SPLIT(sp)) {
		(void)gp->scr_discard(sp, NULL);

		if (spp != NULL)
			*spp = NULL;
		return (0);
	}

	/*
	 * Find a set of screens that cover one of the screen's borders.
	 * Check the vertical axis first, for no particular reason.
	 *
	 * XXX
	 * It's possible (I think?), to create a screen that shares no full
	 * border with any other set of screens, so we can't discard it.  We
	 * just complain at the user until they clean it up.
	 */
	if (vs_join(sp, list, &jdir))
		return (1);

	/*
	 * Modify the affected screens.  Redraw the modified screen(s) from
	 * scratch, setting a status line.  If this is ever a performance
	 * problem we could play games with the map, but I wrote that code
	 * before and it was never clean or easy.
	 *
	 * Don't clean up the discarded screen's information.  If the screen
	 * isn't exiting, we'll do the work when the user redisplays it.
	 */
	switch (jdir) {
	case HORIZ_FOLLOW:
	case HORIZ_PRECEDE:
		for (lp = &list[0]; (tsp = *lp) != NULL; ++lp) {
			/*
			 * Small screens: see vs_refresh.c section 6a.  Adjust
			 * text line info, unless it's a small screen.
			 *
			 * Reset the length of the default scroll.
			 *
			 * Reset the map references.
			 */
			tsp->rows += sp->rows;
			if (!IS_SMALL(tsp))
				tsp->t_rows = tsp->t_minrows = tsp->rows - 1;
			tsp->t_maxrows = tsp->rows - 1;

			tsp->defscroll = tsp->t_maxrows / 2;

			*(_HMAP(tsp) + (tsp->t_rows - 1)) = *_TMAP(tsp);
			_TMAP(tsp) = _HMAP(tsp) + (tsp->t_rows - 1);

			switch (jdir) {
			case HORIZ_FOLLOW:
				tsp->roff = sp->roff;
				vs_sm_fill(tsp, OOBLNO, P_TOP);
				break;
			case HORIZ_PRECEDE:
				vs_sm_fill(tsp, OOBLNO, P_BOTTOM);
				break;
			default:
				abort();
			}
			F_SET(tsp, SC_STATUS);
		}
		break;
	case VERT_FOLLOW:
	case VERT_PRECEDE:
		for (lp = &list[0]; (tsp = *lp) != NULL; ++lp) {
			if (jdir == VERT_FOLLOW)
				tsp->coff = sp->coff;
			tsp->cols += sp->cols + 1;	/* XXX: DIVIDER */
			vs_sm_fill(tsp, OOBLNO, P_TOP);
			F_SET(tsp, SC_STATUS);
		}
		break;
	default:
		abort();
	}

	/* Find the closest screen that changed and move to it. */
	tsp = list[0];
	if (spp != NULL)
		*spp = tsp;

	/* Tell the display that we're discarding a screen. */
	(void)gp->scr_discard(sp, list);

	return (0);
}

/*
 * vs_join --
 *	Find a set of screens that covers a screen's border.
 */
static int
vs_join(SCR *sp, SCR **listp, jdir_t *jdirp)
{
	GS *gp;
	SCR **lp, *tsp;
	int first;
	size_t tlen;

	gp = sp->gp;

	/* Check preceding vertical. */
	for (lp = listp, tlen = sp->rows,
	    tsp = TAILQ_FIRST(gp->dq);
	    tsp != NULL; tsp = TAILQ_NEXT(tsp, q)) {
		if (sp == tsp)
			continue;
		/* Test if precedes the screen vertically. */
		if (tsp->coff + tsp->cols + 1 != sp->coff)
			continue;
		/*
		 * Test if a subset on the vertical axis.  If overlaps the
		 * beginning or end, we can't join on this axis at all.
		 */
		if (tsp->roff > sp->roff + sp->rows)
			continue;
		if (tsp->roff < sp->roff) {
			if (tsp->roff + tsp->rows >= sp->roff)
				break;
			continue;
		}
		if (tsp->roff + tsp->rows > sp->roff + sp->rows)
			break;
#ifdef DEBUG
		if (tlen < tsp->rows)
			abort();
#endif
		tlen -= tsp->rows;
		*lp++ = tsp;
	}
	if (tlen == 0) {
		*lp = NULL;
		*jdirp = VERT_PRECEDE;
		return (0);
	}

	/* Check following vertical. */
	for (lp = listp, tlen = sp->rows,
	    tsp = TAILQ_FIRST(gp->dq);
	    tsp != NULL; tsp = TAILQ_NEXT(tsp, q)) {
		if (sp == tsp)
			continue;
		/* Test if follows the screen vertically. */
		if (tsp->coff != sp->coff + sp->cols + 1)
			continue;
		/*
		 * Test if a subset on the vertical axis.  If overlaps the
		 * beginning or end, we can't join on this axis at all.
		 */
		if (tsp->roff > sp->roff + sp->rows)
			continue;
		if (tsp->roff < sp->roff) {
			if (tsp->roff + tsp->rows >= sp->roff)
				break;
			continue;
		}
		if (tsp->roff + tsp->rows > sp->roff + sp->rows)
			break;
#ifdef DEBUG
		if (tlen < tsp->rows)
			abort();
#endif
		tlen -= tsp->rows;
		*lp++ = tsp;
	}
	if (tlen == 0) {
		*lp = NULL;
		*jdirp = VERT_FOLLOW;
		return (0);
	}

	/* Check preceding horizontal. */
	for (first = 0, lp = listp, tlen = sp->cols,
	    tsp = TAILQ_FIRST(gp->dq);
	    tsp != NULL; tsp = TAILQ_NEXT(tsp, q)) {
		if (sp == tsp)
			continue;
		/* Test if precedes the screen horizontally. */
		if (tsp->roff + tsp->rows != sp->roff)
			continue;
		/*
		 * Test if a subset on the horizontal axis.  If overlaps the
		 * beginning or end, we can't join on this axis at all.
		 */
		if (tsp->coff > sp->coff + sp->cols)
			continue;
		if (tsp->coff < sp->coff) {
			if (tsp->coff + tsp->cols >= sp->coff)
				break;
			continue;
		}
		if (tsp->coff + tsp->cols > sp->coff + sp->cols)
			break;
#ifdef DEBUG
		if (tlen < tsp->cols)
			abort();
#endif
		tlen -= tsp->cols + first;
		first = 1;
		*lp++ = tsp;
	}
	if (tlen == 0) {
		*lp = NULL;
		*jdirp = HORIZ_PRECEDE;
		return (0);
	}

	/* Check following horizontal. */
	for (first = 0, lp = listp, tlen = sp->cols,
	    tsp = TAILQ_FIRST(gp->dq);
	    tsp != NULL; tsp = TAILQ_NEXT(tsp, q)) {
		if (sp == tsp)
			continue;
		/* Test if precedes the screen horizontally. */
		if (tsp->roff != sp->roff + sp->rows)
			continue;
		/*
		 * Test if a subset on the horizontal axis.  If overlaps the
		 * beginning or end, we can't join on this axis at all.
		 */
		if (tsp->coff > sp->coff + sp->cols)
			continue;
		if (tsp->coff < sp->coff) {
			if (tsp->coff + tsp->cols >= sp->coff)
				break;
			continue;
		}
		if (tsp->coff + tsp->cols > sp->coff + sp->cols)
			break;
#ifdef DEBUG
		if (tlen < tsp->cols)
			abort();
#endif
		tlen -= tsp->cols + first;
		first = 1;
		*lp++ = tsp;
	}
	if (tlen == 0) {
		*lp = NULL;
		*jdirp = HORIZ_FOLLOW;
		return (0);
	}
	return (1);
}

/*
 * vs_fg --
 *	Background the current screen, and foreground a new one.
 *
 * PUBLIC: int vs_fg(SCR *, SCR **, CHAR_T *, int);
 */
int
vs_fg(SCR *sp, SCR **nspp, CHAR_T *name, int newscreen)
{
	GS *gp;
	SCR *nsp;
	char *np;
	size_t nlen;

	gp = sp->gp;

	if (name)
	    INT2CHAR(sp, name, STRLEN(name) + 1, np, nlen);
	else
	    np = NULL;
	if (newscreen)
		/* Get the specified background screen. */
		nsp = vs_getbg(sp, np);
	else
		/* Swap screens. */
		if (vs_swap(sp, &nsp, np))
			return (1);

	if ((*nspp = nsp) == NULL) {
		msgq_wstr(sp, M_ERR, name,
		    name == NULL ?
		    "223|There are no background screens" :
		    "224|There's no background screen editing a file named %s");
		return (1);
	}

	if (newscreen) {
		/* Remove the new screen from the background queue. */
		TAILQ_REMOVE(gp->hq, nsp, q);

		/* Split the screen; if we fail, hook the screen back in. */
		if (vs_split(sp, nsp, 0)) {
			TAILQ_INSERT_TAIL(gp->hq, nsp, q);
			return (1);
		}
	} else {
		/* Move the old screen to the background queue. */
		TAILQ_REMOVE(gp->dq, sp, q);
		TAILQ_INSERT_TAIL(gp->hq, sp, q);
	}
	return (0);
}

/*
 * vs_bg --
 *	Background the screen, and switch to the next one.
 *
 * PUBLIC: int vs_bg(SCR *);
 */
int
vs_bg(SCR *sp)
{
	GS *gp;
	SCR *nsp;

	gp = sp->gp;

	/* Try and join with another screen. */
	if (vs_discard(sp, &nsp))
		return (1);
	if (nsp == NULL) {
		msgq(sp, M_ERR,
		    "225|You may not background your only displayed screen");
		return (1);
	}

	/* Move the old screen to the background queue. */
	TAILQ_REMOVE(gp->dq, sp, q);
	TAILQ_INSERT_TAIL(gp->hq, sp, q);

	/* Toss the screen map. */
	free(_HMAP(sp));
	_HMAP(sp) = NULL;

	/* Switch screens. */
	sp->nextdisp = nsp;
	F_SET(sp, SC_SSWITCH);

	return (0);
}

/*
 * vs_swap --
 *	Swap the current screen with a backgrounded one.
 *
 * PUBLIC: int vs_swap(SCR *, SCR **, char *);
 */
int
vs_swap(SCR *sp, SCR **nspp, char *name)
{
	GS *gp;
	SCR *nsp, *list[2];

	gp = sp->gp;

	/* Get the specified background screen. */
	if ((*nspp = nsp = vs_getbg(sp, name)) == NULL)
		return (0);

	/*
	 * Save the old screen's cursor information.
	 *
	 * XXX
	 * If called after file_end(), and the underlying file was a tmp
	 * file, it may have gone away.
	 */
	if (sp->frp != NULL) {
		sp->frp->lno = sp->lno;
		sp->frp->cno = sp->cno;
		F_SET(sp->frp, FR_CURSORSET);
	}

	/* Switch screens. */
	sp->nextdisp = nsp;
	F_SET(sp, SC_SSWITCH);

	/* Initialize terminal information. */
	VIP(nsp)->srows = VIP(sp)->srows;

	/* Initialize screen information. */
	nsp->cols = sp->cols;
	nsp->rows = sp->rows;	/* XXX: Only place in vi that sets rows. */
	nsp->roff = sp->roff;

	/*
	 * Small screens: see vs_refresh.c, section 6a.
	 *
	 * The new screens may have different screen options sizes than the
	 * old one, so use them.  Make sure that text counts aren't larger
	 * than the new screen sizes.
	 */
	if (IS_SMALL(nsp)) {
		nsp->t_minrows = nsp->t_rows = O_VAL(nsp, O_WINDOW);
		if (nsp->t_rows > sp->t_maxrows)
			nsp->t_rows = nsp->t_maxrows;
		if (nsp->t_minrows > sp->t_maxrows)
			nsp->t_minrows = nsp->t_maxrows;
	} else
		nsp->t_rows = nsp->t_maxrows = nsp->t_minrows = nsp->rows - 1;

	/* Reset the length of the default scroll. */
	nsp->defscroll = nsp->t_maxrows / 2;

	/* Allocate a new screen map. */
	CALLOC_RET(nsp, _HMAP(nsp), SMAP *, SIZE_HMAP(nsp), sizeof(SMAP));
	_TMAP(nsp) = _HMAP(nsp) + (nsp->t_rows - 1);

	/* Fill the map. */
	nsp->gp = sp->gp;
	if (vs_sm_fill(nsp, nsp->lno, P_FILL))
		return (1);

	/*
	 * The new screen replaces the old screen in the parent/child list.
	 * We insert the new screen after the old one.  If we're exiting,
	 * the exit will delete the old one, if we're foregrounding, the fg
	 * code will move the old one to the background queue.
	 */
	TAILQ_REMOVE(gp->hq, nsp, q);
	TAILQ_INSERT_AFTER(gp->dq, sp, nsp, q);

	/*
	 * Don't change the screen's cursor information other than to
	 * note that the cursor is wrong.
	 */
	F_SET(VIP(nsp), VIP_CUR_INVALID);

	/* Draw the new screen from scratch, and add a status line. */
	F_SET(nsp, SC_SCR_REDRAW | SC_STATUS);

	list[0] = nsp; list[1] = NULL;
	(void)gp->scr_discard(sp, list);

	return (0);
}

/*
 * vs_resize --
 *	Change the absolute size of the current screen.
 *
 * PUBLIC: int vs_resize(SCR *, long, adj_t);
 */
int
vs_resize(SCR *sp, long int count, adj_t adj)
{
	GS *gp;
	SCR *g, *s, *prev, *next, *list[3] = {NULL, NULL, NULL};
	size_t g_off, s_off;

	gp = sp->gp;

	/*
	 * Figure out which screens will grow, which will shrink, and
	 * make sure it's possible.
	 */
	if (count == 0)
		return (0);
	if (adj == A_SET) {
		if (sp->t_maxrows == count)
			return (0);
		if (sp->t_maxrows > count) {
			adj = A_DECREASE;
			count = sp->t_maxrows - count;
		} else {
			adj = A_INCREASE;
			count = count - sp->t_maxrows;
		}
	}

	/* Find first overlapping screen */
	for (next = TAILQ_NEXT(sp, q); next != NULL &&
	     (next->coff >= sp->coff + sp->cols ||
	      next->coff + next->cols <= sp->coff);
	     next = TAILQ_NEXT(next, q));
	/* See if we can use it */
	if (next != NULL &&
	    (sp->coff != next->coff || sp->cols != next->cols))
		next = NULL;
	for (prev = TAILQ_PREV(sp, _dqh, q); prev != NULL &&
	     (prev->coff >= sp->coff + sp->cols ||
	      prev->coff + prev->cols <= sp->coff);
	     prev = TAILQ_PREV(prev, _dqh, q));
	if (prev != NULL &&
	    (sp->coff != prev->coff || sp->cols != prev->cols))
		prev = NULL;

	g_off = s_off = 0;
	if (adj == A_DECREASE) {
		if (count < 0)
			count = -count;
		s = sp;
		if (s->t_maxrows < MINIMUM_SCREEN_ROWS + count)
			goto toosmall;
		if ((g = prev) == NULL) {
			if ((g = next) == NULL)
				goto toobig;
			g_off = -count;
		} else
			s_off = count;
	} else {
		g = sp;
		if ((s = next) != NULL &&
		    s->t_maxrows >= MINIMUM_SCREEN_ROWS + count)
				s_off = count;
		else
			s = NULL;
		if (s == NULL) {
			if ((s = prev) == NULL) {
toobig:				msgq(sp, M_BERR, adj == A_DECREASE ?
				    "227|The screen cannot shrink" :
				    "228|The screen cannot grow");
				return (1);
			}
			if (s->t_maxrows < MINIMUM_SCREEN_ROWS + count) {
toosmall:			msgq(sp, M_BERR,
				    "226|The screen can only shrink to %d rows",
				    MINIMUM_SCREEN_ROWS);
				return (1);
			}
			g_off = -count;
		}
	}

	/*
	 * Fix up the screens; we could optimize the reformatting of the
	 * screen, but this isn't likely to be a common enough operation
	 * to make it worthwhile.
	 */
	s->rows += -count;
	s->roff += s_off;
	g->rows += count;
	g->roff += g_off;

	g->t_rows += count;
	if (g->t_minrows == g->t_maxrows)
		g->t_minrows += count;
	g->t_maxrows += count;
	_TMAP(g) += count;
	F_SET(g, SC_SCR_REFORMAT | SC_STATUS);

	s->t_rows -= count;
	s->t_maxrows -= count;
	if (s->t_minrows > s->t_maxrows)
		s->t_minrows = s->t_maxrows;
	_TMAP(s) -= count;
	F_SET(s, SC_SCR_REFORMAT | SC_STATUS);

	/* XXXX */
	list[0] = g; list[1] = s;
	gp->scr_discard(0, list);

	return (0);
}

/*
 * vs_getbg --
 *	Get the specified background screen, or, if name is NULL, the first
 *	background screen.
 */
static SCR *
vs_getbg(SCR *sp, char *name)
{
	GS *gp;
	SCR *nsp;
	char *p;

	gp = sp->gp;

	/* If name is NULL, return the first background screen on the list. */
	if (name == NULL)
		return (TAILQ_FIRST(gp->hq));

	/* Search for a full match. */
	TAILQ_FOREACH(nsp, gp->hq, q)
		if (!strcmp(nsp->frp->name, name))
			break;
	if (nsp != NULL)
		return (nsp);

	/* Search for a last-component match. */
	TAILQ_FOREACH(nsp, gp->hq, q) {
		if ((p = strrchr(nsp->frp->name, '/')) == NULL)
			p = nsp->frp->name;
		else
			++p;
		if (!strcmp(p, name))
			break;
	}
	if (nsp != NULL)
		return (nsp);

	return (NULL);
}
