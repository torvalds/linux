/****************************************************************************
 * Copyright (c) 1998-2009,2010 Free Software Foundation, Inc.              *
 *                                                                          *
 * Permission is hereby granted, free of charge, to any person obtaining a  *
 * copy of this software and associated documentation files (the            *
 * "Software"), to deal in the Software without restriction, including      *
 * without limitation the rights to use, copy, modify, merge, publish,      *
 * distribute, distribute with modifications, sublicense, and/or sell       *
 * copies of the Software, and to permit persons to whom the Software is    *
 * furnished to do so, subject to the following conditions:                 *
 *                                                                          *
 * The above copyright notice and this permission notice shall be included  *
 * in all copies or substantial portions of the Software.                   *
 *                                                                          *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS  *
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF               *
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.   *
 * IN NO EVENT SHALL THE ABOVE COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,   *
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR    *
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR    *
 * THE USE OR OTHER DEALINGS IN THE SOFTWARE.                               *
 *                                                                          *
 * Except as contained in this notice, the name(s) of the above copyright   *
 * holders shall not be used in advertising or otherwise to promote the     *
 * sale, use or other dealings in this Software without prior written       *
 * authorization.                                                           *
 ****************************************************************************/

/****************************************************************************
 *  Author: Zeyd M. Ben-Halim <zmbenhal@netcom.com> 1992,1995               *
 *     and: Eric S. Raymond <esr@snark.thyrsus.com>                         *
 ****************************************************************************/

/*
**	lib_window.c
**
**
*/

#include <curses.priv.h>

MODULE_ID("$Id: lib_window.c,v 1.29 2010/12/19 01:47:22 tom Exp $")

NCURSES_EXPORT(void)
_nc_synchook(WINDOW *win)
/* hook to be called after each window change */
{
    if (win->_immed)
	wrefresh(win);
    if (win->_sync)
	wsyncup(win);
}

NCURSES_EXPORT(int)
mvderwin(WINDOW *win, int y, int x)
/* move a derived window */
{
    WINDOW *orig;
    int i;
    int rc = ERR;

    T((T_CALLED("mvderwin(%p,%d,%d)"), (void *) win, y, x));

    if (win != 0
	&& (orig = win->_parent) != 0
	&& (x >= 0 && y >= 0)
	&& (x + getmaxx(win) <= getmaxx(orig))
	&& (y + getmaxy(win) <= getmaxy(orig))) {
	wsyncup(win);
	win->_parx = x;
	win->_pary = y;
	for (i = 0; i < getmaxy(win); i++)
	    win->_line[i].text = &(orig->_line[y++].text[x]);
	rc = OK;
    }
    returnCode(rc);
}

NCURSES_EXPORT(int)
syncok(WINDOW *win, bool bf)
/* enable/disable automatic wsyncup() on each change to window */
{
    T((T_CALLED("syncok(%p,%d)"), (void *) win, bf));

    if (win) {
	win->_sync = bf;
	returnCode(OK);
    } else
	returnCode(ERR);
}

NCURSES_EXPORT(void)
wsyncup(WINDOW *win)
/* mark changed every cell in win's ancestors that is changed in win */
/* Rewritten by J. Pfeifer, 1-Apr-96 (don't even think that...)      */
{
    WINDOW *wp;

    T((T_CALLED("wsyncup(%p)"), (void *) win));
    if (win && win->_parent) {
	for (wp = win; wp->_parent; wp = wp->_parent) {
	    int y;
	    WINDOW *pp = wp->_parent;

	    assert((wp->_pary <= pp->_maxy) &&
		   ((wp->_pary + wp->_maxy) <= pp->_maxy));

	    for (y = 0; y <= wp->_maxy; y++) {
		int left = wp->_line[y].firstchar;
		if (left >= 0) {	/* line is touched */
		    struct ldat *line = &(pp->_line[wp->_pary + y]);
		    /* left & right character in parent window coordinates */
		    int right = wp->_line[y].lastchar + wp->_parx;
		    left += wp->_parx;

		    CHANGED_RANGE(line, left, right);
		}
	    }
	}
    }
    returnVoid;
}

NCURSES_EXPORT(void)
wsyncdown(WINDOW *win)
/* mark changed every cell in win that is changed in any of its ancestors */
/* Rewritten by J. Pfeifer, 1-Apr-96 (don't even think that...)           */
{
    T((T_CALLED("wsyncdown(%p)"), (void *) win));

    if (win && win->_parent) {
	WINDOW *pp = win->_parent;
	int y;

	/* This recursion guarantees, that the changes are propagated down-
	   wards from the root to our direct parent. */
	wsyncdown(pp);

	/* and now we only have to propagate the changes from our direct
	   parent, if there are any. */
	assert((win->_pary <= pp->_maxy) &&
	       ((win->_pary + win->_maxy) <= pp->_maxy));

	for (y = 0; y <= win->_maxy; y++) {
	    if (pp->_line[win->_pary + y].firstchar >= 0) {	/* parent changed */
		struct ldat *line = &(win->_line[y]);
		/* left and right character in child coordinates */
		int left = pp->_line[win->_pary + y].firstchar - win->_parx;
		int right = pp->_line[win->_pary + y].lastchar - win->_parx;
		/* The change may be outside the child's range */
		if (left < 0)
		    left = 0;
		if (right > win->_maxx)
		    right = win->_maxx;
		CHANGED_RANGE(line, left, right);
	    }
	}
    }
    returnVoid;
}

NCURSES_EXPORT(void)
wcursyncup(WINDOW *win)
/* sync the cursor in all derived windows to its value in the base window */
{
    WINDOW *wp;

    T((T_CALLED("wcursyncup(%p)"), (void *) win));
    for (wp = win; wp && wp->_parent; wp = wp->_parent) {
	wmove(wp->_parent, wp->_pary + wp->_cury, wp->_parx + wp->_curx);
    }
    returnVoid;
}

NCURSES_EXPORT(WINDOW *)
dupwin(WINDOW *win)
/* make an exact duplicate of the given window */
{
    WINDOW *nwin = 0;
    size_t linesize;
    int i;

    T((T_CALLED("dupwin(%p)"), (void *) win));

    if (win != 0) {
#if NCURSES_SP_FUNCS
	SCREEN *sp = _nc_screen_of(win);
#endif
	_nc_lock_global(curses);
	if (win->_flags & _ISPAD) {
	    nwin = NCURSES_SP_NAME(newpad) (NCURSES_SP_ARGx
					    win->_maxy + 1,
					    win->_maxx + 1);
	} else {
	    nwin = NCURSES_SP_NAME(newwin) (NCURSES_SP_ARGx
					    win->_maxy + 1,
					    win->_maxx + 1,
					    win->_begy,
					    win->_begx);
	}

	if (nwin != 0) {

	    nwin->_curx = win->_curx;
	    nwin->_cury = win->_cury;
	    nwin->_maxy = win->_maxy;
	    nwin->_maxx = win->_maxx;
	    nwin->_begy = win->_begy;
	    nwin->_begx = win->_begx;
	    nwin->_yoffset = win->_yoffset;

	    nwin->_flags = win->_flags & ~_SUBWIN;
	    /* Due to the use of newwin(), the clone is not a subwindow.
	     * The text is really copied into the clone.
	     */

	    WINDOW_ATTRS(nwin) = WINDOW_ATTRS(win);
	    nwin->_nc_bkgd = win->_nc_bkgd;

	    nwin->_notimeout = win->_notimeout;
	    nwin->_clear = win->_clear;
	    nwin->_leaveok = win->_leaveok;
	    nwin->_scroll = win->_scroll;
	    nwin->_idlok = win->_idlok;
	    nwin->_idcok = win->_idcok;
	    nwin->_immed = win->_immed;
	    nwin->_sync = win->_sync;
	    nwin->_use_keypad = win->_use_keypad;
	    nwin->_delay = win->_delay;

	    nwin->_parx = 0;
	    nwin->_pary = 0;
	    nwin->_parent = (WINDOW *) 0;
	    /* See above: the clone isn't a subwindow! */

	    nwin->_regtop = win->_regtop;
	    nwin->_regbottom = win->_regbottom;

	    if (win->_flags & _ISPAD)
		nwin->_pad = win->_pad;

	    linesize = (unsigned) (win->_maxx + 1) * sizeof(NCURSES_CH_T);
	    for (i = 0; i <= nwin->_maxy; i++) {
		memcpy(nwin->_line[i].text, win->_line[i].text, linesize);
		nwin->_line[i].firstchar = win->_line[i].firstchar;
		nwin->_line[i].lastchar = win->_line[i].lastchar;
	    }
	}
	_nc_unlock_global(curses);
    }
    returnWin(nwin);
}
