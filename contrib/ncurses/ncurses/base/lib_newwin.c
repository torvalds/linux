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
 *     and: Juergen Pfeifer                         2009                    *
 ****************************************************************************/

/*
**	lib_newwin.c
**
**	The routines newwin(), subwin() and their dependent
**
*/

#include <curses.priv.h>
#include <stddef.h>

MODULE_ID("$Id: lib_newwin.c,v 1.71 2011/05/28 21:32:51 tom Exp $")

#define window_is(name) ((sp)->_##name == win)

#if USE_REENTRANT
#define remove_window(name) \
		sp->_##name = 0
#else
#define remove_window(name) \
		sp->_##name = 0; \
		if (win == name) \
		    name = 0
#endif

static void
remove_window_from_screen(WINDOW *win)
{
    SCREEN *sp;

#ifdef USE_SP_WINDOWLIST
    if ((sp = _nc_screen_of(win)) != 0) {
	if (window_is(curscr)) {
	    remove_window(curscr);
	} else if (window_is(stdscr)) {
	    remove_window(stdscr);
	} else if (window_is(newscr)) {
	    remove_window(newscr);
	}
    }
#else
    for (each_screen(sp)) {
	if (window_is(curscr)) {
	    remove_window(curscr);
	    break;
	} else if (window_is(stdscr)) {
	    remove_window(stdscr);
	    break;
	} else if (window_is(newscr)) {
	    remove_window(newscr);
	    break;
	}
    }
#endif
}

NCURSES_EXPORT(int)
_nc_freewin(WINDOW *win)
{
    WINDOWLIST *p, *q;
    int i;
    int result = ERR;
#ifdef USE_SP_WINDOWLIST
    SCREEN *sp = _nc_screen_of(win);	/* pretend this is parameter */
#endif

    T((T_CALLED("_nc_freewin(%p)"), (void *) win));

    if (win != 0) {
	if (_nc_nonsp_try_global(curses) == 0) {
	    q = 0;
	    for (each_window(SP_PARM, p)) {
		if (&(p->win) == win) {
		    remove_window_from_screen(win);
		    if (q == 0)
			WindowList(SP_PARM) = p->next;
		    else
			q->next = p->next;

		    if (!(win->_flags & _SUBWIN)) {
			for (i = 0; i <= win->_maxy; i++)
			    FreeIfNeeded(win->_line[i].text);
		    }
		    free(win->_line);
		    free(p);

		    result = OK;
		    T(("...deleted win=%p", (void *) win));
		    break;
		}
		q = p;
	    }
	    _nc_nonsp_unlock_global(curses);
	}
    }
    returnCode(result);
}

NCURSES_EXPORT(WINDOW *)
NCURSES_SP_NAME(newwin) (NCURSES_SP_DCLx
			 int num_lines, int num_columns, int begy, int begx)
{
    WINDOW *win;
    NCURSES_CH_T *ptr;
    int i;

    T((T_CALLED("newwin(%p, %d,%d,%d,%d)"), (void *) SP_PARM, num_lines, num_columns,
       begy, begx));

    if (begy < 0
	|| begx < 0
	|| num_lines < 0
	|| num_columns < 0
	|| SP_PARM == 0)
	returnWin(0);

    if (num_lines == 0)
	num_lines = SP_PARM->_lines_avail - begy;
    if (num_columns == 0)
	num_columns = screen_columns(SP_PARM) - begx;

    win = NCURSES_SP_NAME(_nc_makenew) (NCURSES_SP_ARGx
					num_lines, num_columns, begy, begx, 0);
    if (win == 0)
	returnWin(0);

    for (i = 0; i < num_lines; i++) {
	win->_line[i].text = typeCalloc(NCURSES_CH_T, (unsigned) num_columns);
	if (win->_line[i].text == 0) {
	    (void) _nc_freewin(win);
	    returnWin(0);
	}
	for (ptr = win->_line[i].text;
	     ptr < win->_line[i].text + num_columns;
	     ptr++)
	    SetChar(*ptr, BLANK_TEXT, BLANK_ATTR);
    }

    returnWin(win);
}

#if NCURSES_SP_FUNCS
NCURSES_EXPORT(WINDOW *)
newwin(int num_lines, int num_columns, int begy, int begx)
{
    WINDOW *win;
    _nc_sp_lock_global(curses);
    win = NCURSES_SP_NAME(newwin) (CURRENT_SCREEN,
				   num_lines, num_columns, begy, begx);
    _nc_sp_unlock_global(curses);
    return (win);
}
#endif

NCURSES_EXPORT(WINDOW *)
derwin(WINDOW *orig, int num_lines, int num_columns, int begy, int begx)
{
    WINDOW *win;
    int i;
    int flags = _SUBWIN;
#if NCURSES_SP_FUNCS
    SCREEN *sp = _nc_screen_of(orig);
#endif

    T((T_CALLED("derwin(%p,%d,%d,%d,%d)"), (void *) orig, num_lines, num_columns,
       begy, begx));

    /*
     * make sure window fits inside the original one
     */
    if (begy < 0 || begx < 0 || orig == 0 || num_lines < 0 || num_columns < 0)
	returnWin(0);
    if (begy + num_lines > orig->_maxy + 1
	|| begx + num_columns > orig->_maxx + 1)
	returnWin(0);

    if (num_lines == 0)
	num_lines = orig->_maxy + 1 - begy;

    if (num_columns == 0)
	num_columns = orig->_maxx + 1 - begx;

    if (orig->_flags & _ISPAD)
	flags |= _ISPAD;

    win = NCURSES_SP_NAME(_nc_makenew) (NCURSES_SP_ARGx num_lines, num_columns,
					orig->_begy + begy,
					orig->_begx + begx, flags);
    if (win == 0)
	returnWin(0);

    win->_pary = begy;
    win->_parx = begx;
    WINDOW_ATTRS(win) = WINDOW_ATTRS(orig);
    win->_nc_bkgd = orig->_nc_bkgd;

    for (i = 0; i < num_lines; i++)
	win->_line[i].text = &orig->_line[begy++].text[begx];

    win->_parent = orig;

    returnWin(win);
}

NCURSES_EXPORT(WINDOW *)
subwin(WINDOW *w, int l, int c, int y, int x)
{
    WINDOW *result = 0;

    T((T_CALLED("subwin(%p, %d, %d, %d, %d)"), (void *) w, l, c, y, x));
    if (w != 0) {
	T(("parent has begy = %ld, begx = %ld", (long) w->_begy, (long) w->_begx));

	result = derwin(w, l, c, y - w->_begy, x - w->_begx);
    }
    returnWin(result);
}

static bool
dimension_limit(int value)
{
    NCURSES_SIZE_T test = (NCURSES_SIZE_T) value;
    return (test == value && value > 0);
}

NCURSES_EXPORT(WINDOW *)
NCURSES_SP_NAME(_nc_makenew) (NCURSES_SP_DCLx
			      int num_lines,
			      int num_columns,
			      int begy,
			      int begx,
			      int flags)
{
    int i;
    WINDOWLIST *wp;
    WINDOW *win;
    bool is_padwin = (flags & _ISPAD);

    T((T_CALLED("_nc_makenew(%p,%d,%d,%d,%d)"),
       (void *) SP_PARM, num_lines, num_columns, begy, begx));

    if (SP_PARM == 0)
	returnWin(0);

    if (!dimension_limit(num_lines) || !dimension_limit(num_columns))
	returnWin(0);

    if ((wp = typeCalloc(WINDOWLIST, 1)) == 0)
	returnWin(0);

    win = &(wp->win);

    if ((win->_line = typeCalloc(struct ldat, ((unsigned) num_lines))) == 0) {
	free(wp);
	returnWin(0);
    }

    _nc_nonsp_lock_global(curses);

    win->_curx = 0;
    win->_cury = 0;
    win->_maxy = (NCURSES_SIZE_T) (num_lines - 1);
    win->_maxx = (NCURSES_SIZE_T) (num_columns - 1);
    win->_begy = (NCURSES_SIZE_T) begy;
    win->_begx = (NCURSES_SIZE_T) begx;
    win->_yoffset = SP_PARM->_topstolen;

    win->_flags = (short) flags;
    WINDOW_ATTRS(win) = A_NORMAL;
    SetChar(win->_nc_bkgd, BLANK_TEXT, BLANK_ATTR);

    win->_clear = (is_padwin
		   ? FALSE
		   : (num_lines == screen_lines(SP_PARM)
		      && num_columns == screen_columns(SP_PARM)));
    win->_idlok = FALSE;
    win->_idcok = TRUE;
    win->_scroll = FALSE;
    win->_leaveok = FALSE;
    win->_use_keypad = FALSE;
    win->_delay = -1;
    win->_immed = FALSE;
    win->_sync = 0;
    win->_parx = -1;
    win->_pary = -1;
    win->_parent = 0;

    win->_regtop = 0;
    win->_regbottom = (NCURSES_SIZE_T) (num_lines - 1);

    win->_pad._pad_y = -1;
    win->_pad._pad_x = -1;
    win->_pad._pad_top = -1;
    win->_pad._pad_bottom = -1;
    win->_pad._pad_left = -1;
    win->_pad._pad_right = -1;

    for (i = 0; i < num_lines; i++) {
	/*
	 * This used to do
	 *
	 * win->_line[i].firstchar = win->_line[i].lastchar = _NOCHANGE;
	 *
	 * which marks the whole window unchanged.  That's how
	 * SVr1 curses did it, but SVr4 curses marks the whole new
	 * window changed.
	 *
	 * With the old SVr1-like code, say you have stdscr full of
	 * characters, then create a new window with newwin(),
	 * then do a printw(win, "foo        ");, the trailing spaces are
	 * completely ignored by the following refreshes.  So, you
	 * get "foojunkjunk" on the screen instead of "foo        " as
	 * you actually intended.
	 *
	 * SVr4 doesn't do this.  Instead the spaces are actually written.
	 * So that's how we want ncurses to behave.
	 */
	win->_line[i].firstchar = 0;
	win->_line[i].lastchar = (NCURSES_SIZE_T) (num_columns - 1);

	if_USE_SCROLL_HINTS(win->_line[i].oldindex = i);
    }

    if (!is_padwin && (begx + num_columns == screen_columns(SP_PARM))) {
	win->_flags |= _ENDLINE;

	if (begx == 0 && num_lines == screen_lines(SP_PARM) && begy == 0)
	    win->_flags |= _FULLWIN;

	if (begy + num_lines == screen_lines(SP_PARM))
	    win->_flags |= _SCROLLWIN;
    }

    wp->next = WindowList(SP_PARM);
    wp->screen = SP_PARM;
    WindowList(SP_PARM) = wp;

    T((T_CREATE("window %p"), (void *) win));

    _nc_nonsp_unlock_global(curses);
    returnWin(win);
}

/*
 * wgetch() and other functions with a WINDOW* parameter may use a SCREEN*
 * internally, and it is useful to allow those to be invoked without switching
 * SCREEN's, e.g., for multi-threaded applications.
 */
#if NCURSES_SP_FUNCS
NCURSES_EXPORT(WINDOW *)
_nc_curscr_of(SCREEN *sp)
{
    return sp == 0 ? 0 : CurScreen(sp);
}

NCURSES_EXPORT(WINDOW *)
_nc_newscr_of(SCREEN *sp)
{
    return sp == 0 ? 0 : NewScreen(sp);
}

NCURSES_EXPORT(WINDOW *)
_nc_stdscr_of(SCREEN *sp)
{
    return sp == 0 ? 0 : StdScreen(sp);
}
#endif
