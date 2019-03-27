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
 *     and: Thomas E. Dickey                        1996-on                 *
 *     and: Juergen Pfeifer                         2009                    *
 ****************************************************************************/

/*
 * lib_pad.c
 * newpad	-- create a new pad
 * pnoutrefresh -- refresh a pad, no update
 * pechochar	-- add a char to a pad and refresh
 */

#include <curses.priv.h>

MODULE_ID("$Id: lib_pad.c,v 1.46 2010/04/24 23:50:45 tom Exp $")

NCURSES_EXPORT(WINDOW *)
NCURSES_SP_NAME(newpad) (NCURSES_SP_DCLx int l, int c)
{
    WINDOW *win;
    NCURSES_CH_T *ptr;
    int i;

    T((T_CALLED("newpad(%p,%d, %d)"), (void *) SP_PARM, l, c));

    if (l <= 0 || c <= 0)
	returnWin(0);

    win = NCURSES_SP_NAME(_nc_makenew) (NCURSES_SP_ARGx l, c, 0, 0, _ISPAD);
    if (win == NULL)
	returnWin(0);

    for (i = 0; i < l; i++) {
	if_USE_SCROLL_HINTS(win->_line[i].oldindex = _NEWINDEX);
	if ((win->_line[i].text = typeCalloc(NCURSES_CH_T, ((size_t) c))) == 0) {
	    (void) _nc_freewin(win);
	    returnWin(0);
	}
	for (ptr = win->_line[i].text; ptr < win->_line[i].text + c; ptr++)
	    SetChar(*ptr, BLANK_TEXT, BLANK_ATTR);
    }

    returnWin(win);
}

#if NCURSES_SP_FUNCS
NCURSES_EXPORT(WINDOW *)
newpad(int l, int c)
{
    return NCURSES_SP_NAME(newpad) (CURRENT_SCREEN, l, c);
}
#endif

NCURSES_EXPORT(WINDOW *)
subpad(WINDOW *orig, int l, int c, int begy, int begx)
{
    WINDOW *win = (WINDOW *) 0;

    T((T_CALLED("subpad(%d, %d)"), l, c));

    if (orig) {
	if (!(orig->_flags & _ISPAD)
	    || ((win = derwin(orig, l, c, begy, begx)) == NULL))
	    returnWin(0);
    }
    returnWin(win);
}

NCURSES_EXPORT(int)
prefresh(WINDOW *win,
	 int pminrow,
	 int pmincol,
	 int sminrow,
	 int smincol,
	 int smaxrow,
	 int smaxcol)
{
#if NCURSES_SP_FUNCS
    SCREEN *sp = _nc_screen_of(win);
#endif

    T((T_CALLED("prefresh()")));
    if (pnoutrefresh(win, pminrow, pmincol, sminrow, smincol, smaxrow,
		     smaxcol) != ERR
	&& NCURSES_SP_NAME(doupdate) (NCURSES_SP_ARG) != ERR) {
	returnCode(OK);
    }
    returnCode(ERR);
}

NCURSES_EXPORT(int)
pnoutrefresh(WINDOW *win,
	     int pminrow,
	     int pmincol,
	     int sminrow,
	     int smincol,
	     int smaxrow,
	     int smaxcol)
{
    int i, j;
    int m, n;
    int pmaxrow;
    int pmaxcol;
    SCREEN *sp;

#if USE_SCROLL_HINTS
    const int my_len = 2;	/* parameterize the threshold for hardscroll */
    NCURSES_SIZE_T displaced;
    bool wide;
#endif

    T((T_CALLED("pnoutrefresh(%p, %d, %d, %d, %d, %d, %d)"),
       (void *) win, pminrow, pmincol, sminrow, smincol, smaxrow, smaxcol));

    if (win == 0)
	returnCode(ERR);

    if (!(win->_flags & _ISPAD))
	returnCode(ERR);

    sp = _nc_screen_of(win);

    /* negative values are interpreted as zero */
    if (pminrow < 0)
	pminrow = 0;
    if (pmincol < 0)
	pmincol = 0;
    if (sminrow < 0)
	sminrow = 0;
    if (smincol < 0)
	smincol = 0;

    pmaxrow = pminrow + smaxrow - sminrow;
    pmaxcol = pmincol + smaxcol - smincol;

    T((" pminrow + smaxrow - sminrow %ld, win->_maxy %ld",
       (long) pmaxrow, (long) win->_maxy));
    T((" pmincol + smaxcol - smincol %ld, win->_maxx %ld",
       (long) pmaxcol, (long) win->_maxx));

    /*
     * Trim the caller's screen size back to the actual limits.
     */
    if (pmaxrow > win->_maxy) {
	smaxrow -= (pmaxrow - win->_maxy);
	pmaxrow = pminrow + smaxrow - sminrow;
    }
    if (pmaxcol > win->_maxx) {
	smaxcol -= (pmaxcol - win->_maxx);
	pmaxcol = pmincol + smaxcol - smincol;
    }

    if (smaxrow >= screen_lines(sp)
	|| smaxcol >= screen_columns(sp)
	|| sminrow > smaxrow
	|| smincol > smaxcol)
	returnCode(ERR);

    T(("pad being refreshed"));

#if USE_SCROLL_HINTS
    if (win->_pad._pad_y >= 0) {
	displaced = pminrow - win->_pad._pad_y
	    - (sminrow - win->_pad._pad_top);
	T(("pad being shifted by %d line(s)", displaced));
    } else
	displaced = 0;
#endif

    /*
     * For pure efficiency, we'd want to transfer scrolling information
     * from the pad to newscr whenever the window is wide enough that
     * its update will dominate the cost of the update for the horizontal
     * band of newscr that it occupies.  Unfortunately, this threshold
     * tends to be complex to estimate, and in any case scrolling the
     * whole band and rewriting the parts outside win's image would look
     * really ugly.  So.  What we do is consider the pad "wide" if it
     * either (a) occupies the whole width of newscr, or (b) occupies
     * all but at most one column on either vertical edge of the screen
     * (this caters to fussy people who put boxes around full-screen
     * windows).  Note that changing this formula will not break any code,
     * merely change the costs of various update cases.
     */
#if USE_SCROLL_HINTS
    wide = (smincol < my_len && smaxcol > (NewScreen(sp)->_maxx - my_len));
#endif

    for (i = pminrow, m = sminrow + win->_yoffset;
	 i <= pmaxrow && m <= NewScreen(sp)->_maxy;
	 i++, m++) {
	register struct ldat *nline = &NewScreen(sp)->_line[m];
	register struct ldat *oline = &win->_line[i];
	for (j = pmincol, n = smincol; j <= pmaxcol; j++, n++) {
	    NCURSES_CH_T ch = oline->text[j];
#if USE_WIDEC_SUPPORT
	    /*
	     * Special case for leftmost character of the displayed area.
	     * Only half of a double-width character may be visible.
	     */
	    if (j == pmincol
		&& j > 0
		&& isWidecExt(ch)) {
		SetChar(ch, L(' '), AttrOf(oline->text[j - 1]));
	    }
#endif
	    if (!CharEq(ch, nline->text[n])) {
		nline->text[n] = ch;
		CHANGED_CELL(nline, n);
	    }
	}

#if USE_SCROLL_HINTS
	if (wide) {
	    int nind = m + displaced;
	    if (oline->oldindex < 0
		|| nind < sminrow
		|| nind > smaxrow) {
		nind = _NEWINDEX;
	    } else if (displaced) {
		register struct ldat *pline = &CurScreen(sp)->_line[nind];
		for (j = 0; j <= my_len; j++) {
		    int k = NewScreen(sp)->_maxx - j;
		    if (pline->text[j] != nline->text[j]
			|| pline->text[k] != nline->text[k]) {
			nind = _NEWINDEX;
			break;
		    }
		}
	    }

	    nline->oldindex = nind;
	}
#endif /* USE_SCROLL_HINTS */
	oline->firstchar = oline->lastchar = _NOCHANGE;
	if_USE_SCROLL_HINTS(oline->oldindex = i);
    }

    /*
     * Clean up debris from scrolling or resizing the pad, so we do not
     * accidentally pick up the index value during the next call to this
     * procedure.  The only rows that should have an index value are those
     * that are displayed during this cycle.
     */
#if USE_SCROLL_HINTS
    for (i = pminrow - 1; (i >= 0) && (win->_line[i].oldindex >= 0); i--)
	win->_line[i].oldindex = _NEWINDEX;
    for (i = pmaxrow + 1; (i <= win->_maxy)
	 && (win->_line[i].oldindex >= 0); i++)
	win->_line[i].oldindex = _NEWINDEX;
#endif

    win->_begx = (NCURSES_SIZE_T) smincol;
    win->_begy = (NCURSES_SIZE_T) sminrow;

    if (win->_clear) {
	win->_clear = FALSE;
	NewScreen(sp)->_clear = TRUE;
    }

    /*
     * Use the pad's current position, if it will be visible.
     * If not, don't do anything; it's not an error.
     */
    if (win->_leaveok == FALSE
	&& win->_cury >= pminrow
	&& win->_curx >= pmincol
	&& win->_cury <= pmaxrow
	&& win->_curx <= pmaxcol) {
	NewScreen(sp)->_cury = (NCURSES_SIZE_T) (win->_cury - pminrow
						 + win->_begy + win->_yoffset);
	NewScreen(sp)->_curx = (NCURSES_SIZE_T) (win->_curx - pmincol
						 + win->_begx);
    }
    NewScreen(sp)->_leaveok = win->_leaveok;
    win->_flags &= ~_HASMOVED;

    /*
     * Update our cache of the line-numbers that we displayed from the pad.
     * We will use this on subsequent calls to this function to derive
     * values to stuff into 'oldindex[]' -- for scrolling optimization.
     */
    win->_pad._pad_y = (NCURSES_SIZE_T) pminrow;
    win->_pad._pad_x = (NCURSES_SIZE_T) pmincol;
    win->_pad._pad_top = (NCURSES_SIZE_T) sminrow;
    win->_pad._pad_left = (NCURSES_SIZE_T) smincol;
    win->_pad._pad_bottom = (NCURSES_SIZE_T) smaxrow;
    win->_pad._pad_right = (NCURSES_SIZE_T) smaxcol;

    returnCode(OK);
}

NCURSES_EXPORT(int)
pechochar(WINDOW *pad, const chtype ch)
{
    T((T_CALLED("pechochar(%p, %s)"), (void *) pad, _tracechtype(ch)));

    if (pad == 0)
	returnCode(ERR);

    if (!(pad->_flags & _ISPAD))
	returnCode(wechochar(pad, ch));

    waddch(pad, ch);
    prefresh(pad, pad->_pad._pad_y,
	     pad->_pad._pad_x,
	     pad->_pad._pad_top,
	     pad->_pad._pad_left,
	     pad->_pad._pad_bottom,
	     pad->_pad._pad_right);

    returnCode(OK);
}
