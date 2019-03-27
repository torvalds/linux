/****************************************************************************
 * Copyright (c) 1998-2010,2011 Free Software Foundation, Inc.              *
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
 *     and: Juergen Pfeifer                                                 *
 ****************************************************************************/

/*
 *	lib_refresh.c
 *
 *	The routines wrefresh() and wnoutrefresh().
 *
 */

#include <curses.priv.h>

MODULE_ID("$Id: lib_refresh.c,v 1.45 2011/06/25 19:02:22 Vassili.Courzakis Exp $")

NCURSES_EXPORT(int)
wrefresh(WINDOW *win)
{
    int code;
#if NCURSES_SP_FUNCS
    SCREEN *SP_PARM = _nc_screen_of(win);
#endif

    T((T_CALLED("wrefresh(%p)"), (void *) win));

    if (win == 0) {
	code = ERR;
    } else if (win == CurScreen(SP_PARM)) {
	CurScreen(SP_PARM)->_clear = TRUE;
	code = NCURSES_SP_NAME(doupdate) (NCURSES_SP_ARG);
    } else if ((code = wnoutrefresh(win)) == OK) {
	if (win->_clear)
	    NewScreen(SP_PARM)->_clear = TRUE;
	code = NCURSES_SP_NAME(doupdate) (NCURSES_SP_ARG);
	/*
	 * Reset the clearok() flag in case it was set for the special
	 * case in hardscroll.c (if we don't reset it here, we'll get 2
	 * refreshes because the flag is copied from stdscr to newscr).
	 * Resetting the flag shouldn't do any harm, anyway.
	 */
	win->_clear = FALSE;
    }
    returnCode(code);
}

NCURSES_EXPORT(int)
wnoutrefresh(WINDOW *win)
{
    int limit_x;
    int src_row, src_col;
    int begx;
    int begy;
    int dst_row, dst_col;
#if USE_SCROLL_HINTS
    bool wide;
#endif
#if NCURSES_SP_FUNCS
    SCREEN *SP_PARM = _nc_screen_of(win);
#endif

    T((T_CALLED("wnoutrefresh(%p)"), (void *) win));

    /*
     * This function will break badly if we try to refresh a pad.
     */
    if ((win == 0)
	|| (win->_flags & _ISPAD))
	returnCode(ERR);

#ifdef TRACE
    if (USE_TRACEF(TRACE_UPDATE)) {
	_tracedump("...win", win);
	_nc_unlock_global(tracef);
    }
#endif /* TRACE */

    /* put them here so "win == 0" won't break our code */
    begx = win->_begx;
    begy = win->_begy;

    NewScreen(SP_PARM)->_nc_bkgd = win->_nc_bkgd;
    WINDOW_ATTRS(NewScreen(SP_PARM)) = WINDOW_ATTRS(win);

    /* merge in change information from all subwindows of this window */
    wsyncdown(win);

#if USE_SCROLL_HINTS
    /*
     * For pure efficiency, we'd want to transfer scrolling information
     * from the window to newscr whenever the window is wide enough that
     * its update will dominate the cost of the update for the horizontal
     * band of newscr that it occupies.  Unfortunately, this threshold
     * tends to be complex to estimate, and in any case scrolling the
     * whole band and rewriting the parts outside win's image would look
     * really ugly.  So.  What we do is consider the window "wide" if it
     * either (a) occupies the whole width of newscr, or (b) occupies
     * all but at most one column on either vertical edge of the screen
     * (this caters to fussy people who put boxes around full-screen
     * windows).  Note that changing this formula will not break any code,
     * merely change the costs of various update cases.
     */
    wide = (begx <= 1 && win->_maxx >= (NewScreen(SP_PARM)->_maxx - 1));
#endif

    win->_flags &= ~_HASMOVED;

    /*
     * Microtweaking alert!  This double loop is one of the genuine
     * hot spots in the code.  Even gcc doesn't seem to do enough
     * common-subexpression chunking to make it really tense,
     * so we'll force the issue.
     */

    /* limit(dst_col) */
    limit_x = win->_maxx;
    /* limit(src_col) */
    if (limit_x > NewScreen(SP_PARM)->_maxx - begx)
	limit_x = NewScreen(SP_PARM)->_maxx - begx;

    for (src_row = 0, dst_row = begy + win->_yoffset;
	 src_row <= win->_maxy && dst_row <= NewScreen(SP_PARM)->_maxy;
	 src_row++, dst_row++) {
	struct ldat *nline = &(NewScreen(SP_PARM)->_line[dst_row]);
	struct ldat *oline = &win->_line[src_row];

	if (oline->firstchar != _NOCHANGE) {
	    int last_src = oline->lastchar;

	    if (last_src > limit_x)
		last_src = limit_x;

	    src_col = oline->firstchar;
	    dst_col = src_col + begx;

	    if_WIDEC({
		int j;

		/*
		 * Ensure that we will copy complete multi-column characters
		 * on the left-boundary.
		 */
		if (isWidecExt(oline->text[src_col])) {
		    j = 1 + dst_col - WidecExt(oline->text[src_col]);
		    if (j < 0)
			j = 0;
		    if (dst_col > j) {
			src_col -= (dst_col - j);
			dst_col = j;
		    }
		}

		/*
		 * Ensure that we will copy complete multi-column characters
		 * on the right-boundary.
		 */
		j = last_src;
		if (WidecExt(oline->text[j])) {
		    ++j;
		    while (j <= limit_x) {
			if (isWidecBase(oline->text[j])) {
			    break;
			} else {
			    last_src = j;
			}
			++j;
		    }
		}
	    });

	    if_WIDEC({
		static cchar_t blank = BLANK;
		int last_dst = begx + ((last_src < win->_maxx)
				       ? last_src
				       : win->_maxx);
		int fix_left = dst_col;
		int fix_right = last_dst;
		int j;

		/*
		 * Check for boundary cases where we may overwrite part of a
		 * multi-column character.  For those, wipe the remainder of
		 * the character to blanks.
		 */
		j = dst_col;
		if (isWidecExt(nline->text[j])) {
		    /*
		     * On the left, we only care about multi-column characters
		     * that extend into the changed region.
		     */
		    fix_left = 1 + j - WidecExt(nline->text[j]);
		    if (fix_left < 0)
			fix_left = 0;	/* only if cell is corrupt */
		}

		j = last_dst;
		if (WidecExt(nline->text[j]) != 0) {
		    /*
		     * On the right, any multi-column character is a problem,
		     * unless it happens to be contained in the change, and
		     * ending at the right boundary of the change.  The
		     * computation for 'fix_left' accounts for the left-side of
		     * this character.  Find the end of the character.
		     */
		    ++j;
		    while (j <= NewScreen(SP_PARM)->_maxx &&
			   isWidecExt(nline->text[j])) {
			fix_right = j++;
		    }
		}

		/*
		 * The analysis is simpler if we do the clearing afterwards.
		 * Do that now.
		 */
		if (fix_left < dst_col || fix_right > last_dst) {
		    for (j = fix_left; j <= fix_right; ++j) {
			nline->text[j] = blank;
			CHANGED_CELL(nline, j);
		    }
		}
	    });

	    /*
	     * Copy the changed text.
	     */
	    for (; src_col <= last_src; src_col++, dst_col++) {
		if (!CharEq(oline->text[src_col], nline->text[dst_col])) {
		    nline->text[dst_col] = oline->text[src_col];
		    CHANGED_CELL(nline, dst_col);
		}
	    }

	}
#if USE_SCROLL_HINTS
	if (wide) {
	    int oind = oline->oldindex;

	    nline->oldindex = ((oind == _NEWINDEX)
			       ? _NEWINDEX
			       : (begy + oind + win->_yoffset));
	}
#endif /* USE_SCROLL_HINTS */

	oline->firstchar = oline->lastchar = _NOCHANGE;
	if_USE_SCROLL_HINTS(oline->oldindex = src_row);
    }

    if (win->_clear) {
	win->_clear = FALSE;
	NewScreen(SP_PARM)->_clear = TRUE;
    }

    if (!win->_leaveok) {
	NewScreen(SP_PARM)->_cury = (NCURSES_SIZE_T) (win->_cury +
						      win->_begy + win->_yoffset);
	NewScreen(SP_PARM)->_curx = (NCURSES_SIZE_T) (win->_curx + win->_begx);
    }
    NewScreen(SP_PARM)->_leaveok = win->_leaveok;

#ifdef TRACE
    if (USE_TRACEF(TRACE_UPDATE)) {
	_tracedump("newscr", NewScreen(SP_PARM));
	_nc_unlock_global(tracef);
    }
#endif /* TRACE */
    returnCode(OK);
}
