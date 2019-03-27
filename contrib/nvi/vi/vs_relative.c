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
static const char sccsid[] = "$Id: vs_relative.c,v 10.19 2011/12/01 15:22:59 zy Exp $";
#endif /* not lint */

#include <sys/types.h>
#include <sys/queue.h>
#include <sys/time.h>

#include <bitstring.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>

#include "../common/common.h"
#include "vi.h"

/*
 * vs_column --
 *	Return the logical column of the cursor in the line.
 *
 * PUBLIC: int vs_column(SCR *, size_t *);
 */
int
vs_column(SCR *sp, size_t *colp)
{
	VI_PRIVATE *vip;

	vip = VIP(sp);

	*colp = (O_ISSET(sp, O_LEFTRIGHT) ?
	    vip->sc_smap->coff : (vip->sc_smap->soff - 1) * sp->cols) +
	    vip->sc_col - (O_ISSET(sp, O_NUMBER) ? O_NUMBER_LENGTH : 0);
	return (0);
}

/*
 * vs_screens --
 *	Return the screens necessary to display the line, or if specified,
 *	the physical character column within the line, including space
 *	required for the O_NUMBER and O_LIST options.
 *
 * PUBLIC: size_t vs_screens(SCR *, recno_t, size_t *);
 */
size_t
vs_screens(SCR *sp, recno_t lno, size_t *cnop)
{
	size_t cols, screens;

	/* Left-right screens are simple, it's always 1. */
	if (O_ISSET(sp, O_LEFTRIGHT))
		return (1);

	/*
	 * Check for a cached value.  We maintain a cache because, if the
	 * line is large, this routine gets called repeatedly.  One other
	 * hack, lots of time the cursor is on column one, which is an easy
	 * one.
	 */
	if (cnop == NULL) {
		if (VIP(sp)->ss_lno == lno)
			return (VIP(sp)->ss_screens);
	} else if (*cnop == 0)
		return (1);

	/* Figure out how many columns the line/column needs. */
	cols = vs_columns(sp, NULL, lno, cnop, NULL);

	screens = (cols / sp->cols + (cols % sp->cols ? 1 : 0));
	if (screens == 0)
		screens = 1;

	/* Cache the value. */
	if (cnop == NULL) {
		VIP(sp)->ss_lno = lno;
		VIP(sp)->ss_screens = screens;
	}
	return (screens);
}

/*
 * vs_columns --
 *	Return the screen columns necessary to display the line, or,
 *	if specified, the physical character column within the line.
 *
 * PUBLIC: size_t vs_columns(SCR *, CHAR_T *, recno_t, size_t *, size_t *);
 */
size_t
vs_columns(SCR *sp, CHAR_T *lp, recno_t lno, size_t *cnop, size_t *diffp)
{
	size_t chlen, cno, curoff, last = 0, len, scno;
	int ch, leftright, listset;
	CHAR_T *p;

	/*
	 * Initialize the screen offset.
	 */
	scno = 0;

	/* Leading number if O_NUMBER option set. */
	if (O_ISSET(sp, O_NUMBER))
		scno += O_NUMBER_LENGTH;

	/* Need the line to go any further. */
	if (lp == NULL) {
		(void)db_get(sp, lno, 0, &lp, &len);
		if (len == 0)
			goto done;
	}

	/* Missing or empty lines are easy. */
	if (lp == NULL) {
done:		if (diffp != NULL)		/* XXX */
			*diffp = 0;
		return scno;
	}

	/* Store away the values of the list and leftright edit options. */
	listset = O_ISSET(sp, O_LIST);
	leftright = O_ISSET(sp, O_LEFTRIGHT);

	/*
	 * Initialize the pointer into the buffer and current offset.
	 */
	p = lp;
	curoff = scno;

	/* Macro to return the display length of any signal character. */
#define	CHLEN(val) (ch = *(UCHAR_T *)p++) == '\t' &&			\
	    !listset ? TAB_OFF(val) : KEY_COL(sp, ch);

	/*
	 * If folding screens (the historic vi screen format), past the end
	 * of the current screen, and the character was a tab, reset the
	 * current screen column to 0, and the total screen columns to the
	 * last column of the screen.  Otherwise, display the rest of the
	 * character in the next screen.
	 */
#define	TAB_RESET {							\
	curoff += chlen;						\
	if (!leftright && curoff >= sp->cols)				\
		if (ch == '\t') {					\
			curoff = 0;					\
			scno -= scno % sp->cols;			\
		} else							\
			curoff -= sp->cols;				\
}
	if (cnop == NULL)
		while (len--) {
			chlen = CHLEN(curoff);
			last = scno;
			scno += chlen;
			TAB_RESET;
		}
	else
		for (cno = *cnop;; --cno) {
			chlen = CHLEN(curoff);
			last = scno;
			scno += chlen;
			TAB_RESET;
			if (cno == 0)
				break;
		}

	/* Add the trailing '$' if the O_LIST option set. */
	if (listset && cnop == NULL)
		scno += KEY_LEN(sp, '$');

	/*
	 * The text input screen code needs to know how much additional
	 * room the last two characters required, so that it can handle
	 * tab character displays correctly.
	 */
	if (diffp != NULL)
		*diffp = scno - last;
	return (scno);
}

/*
 * vs_rcm --
 *	Return the physical column from the line that will display a
 *	character closest to the currently most attractive character
 *	position (which is stored as a screen column).
 *
 * PUBLIC: size_t vs_rcm(SCR *, recno_t, int);
 */
size_t
vs_rcm(SCR *sp, recno_t lno, int islast)
{
	size_t len;

	/* Last character is easy, and common. */
	if (islast) {
		if (db_get(sp, lno, 0, NULL, &len) || len == 0)
			return (0);
		return (len - 1);
	}

	/* First character is easy, and common. */
	if (sp->rcm == 0)
		return (0);

	return (vs_colpos(sp, lno, sp->rcm));
}

/*
 * vs_colpos --
 *	Return the physical column from the line that will display a
 *	character closest to the specified screen column.
 *
 * PUBLIC: size_t vs_colpos(SCR *, recno_t, size_t);
 */
size_t
vs_colpos(SCR *sp, recno_t lno, size_t cno)
{
	size_t chlen, curoff, len, llen, off, scno;
	int ch = 0, leftright, listset;
	CHAR_T *lp, *p;

	/* Need the line to go any further. */
	(void)db_get(sp, lno, 0, &lp, &llen);

	/* Missing or empty lines are easy. */
	if (lp == NULL || llen == 0)
		return (0);

	/* Store away the values of the list and leftright edit options. */
	listset = O_ISSET(sp, O_LIST);
	leftright = O_ISSET(sp, O_LEFTRIGHT);

	/* Discard screen (logical) lines. */
	off = cno / sp->cols;
	cno %= sp->cols;
	for (scno = 0, p = lp, len = llen; off--;) {
		for (; len && scno < sp->cols; --len)
			scno += CHLEN(scno);

		/*
		 * If reached the end of the physical line, return the last
		 * physical character in the line.
		 */
		if (len == 0)
			return (llen - 1);

		/*
		 * If folding screens (the historic vi screen format), past
		 * the end of the current screen, and the character was a tab,
		 * reset the current screen column to 0.  Otherwise, the rest
		 * of the character is displayed in the next screen.
		 */
		if (leftright && ch == '\t')
			scno = 0;
		else
			scno -= sp->cols;
	}

	/* Step through the line until reach the right character or EOL. */
	for (curoff = scno; len--;) {
		chlen = CHLEN(curoff);

		/*
		 * If we've reached the specific character, there are three
		 * cases.
		 *
		 * 1: scno == cno, i.e. the current character ends at the
		 *    screen character we care about.
		 *	a: off < llen - 1, i.e. not the last character in
		 *	   the line, return the offset of the next character.
		 *	b: else return the offset of the last character.
		 * 2: scno != cno, i.e. this character overruns the character
		 *    we care about, return the offset of this character.
		 */
		if ((scno += chlen) >= cno) {
			off = p - lp;
			return (scno == cno ?
			    (off < llen - 1 ? off : llen - 1) : off - 1);
		}

		TAB_RESET;
	}

	/* No such character; return the start of the last character. */
	return (llen - 1);
}
