/****************************************************************************
 * Copyright (c) 1998-2009,2011 Free Software Foundation, Inc.              *
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
**	lib_getstr.c
**
**	The routine wgetstr().
**
*/

#include <curses.priv.h>

MODULE_ID("$Id: lib_getstr.c,v 1.30 2011/10/22 16:31:35 tom Exp $")

/*
 * This wipes out the last character, no matter whether it was a tab, control
 * or other character, and handles reverse wraparound.
 */
static char *
WipeOut(WINDOW *win, int y, int x, char *first, char *last, int echoed)
{
    if (last > first) {
	*--last = '\0';
	if (echoed) {
	    int y1 = win->_cury;
	    int x1 = win->_curx;

	    wmove(win, y, x);
	    waddstr(win, first);
	    getyx(win, y, x);
	    while (win->_cury < y1
		   || (win->_cury == y1 && win->_curx < x1))
		waddch(win, (chtype) ' ');

	    wmove(win, y, x);
	}
    }
    return last;
}

NCURSES_EXPORT(int)
wgetnstr_events(WINDOW *win,
		char *str,
		int maxlen,
		EVENTLIST_1st(_nc_eventlist * evl))
{
    SCREEN *sp = _nc_screen_of(win);
    TTY buf;
    bool oldnl, oldecho, oldraw, oldcbreak;
    char erasec;
    char killc;
    char *oldstr;
    int ch;
    int y, x;

    T((T_CALLED("wgetnstr(%p,%p,%d)"), (void *) win, (void *) str, maxlen));

    if (!win)
	returnCode(ERR);

    NCURSES_SP_NAME(_nc_get_tty_mode) (NCURSES_SP_ARGx &buf);

    oldnl = sp->_nl;
    oldecho = sp->_echo;
    oldraw = sp->_raw;
    oldcbreak = sp->_cbreak;
    NCURSES_SP_NAME(nl) (NCURSES_SP_ARG);
    NCURSES_SP_NAME(noecho) (NCURSES_SP_ARG);
    NCURSES_SP_NAME(noraw) (NCURSES_SP_ARG);
    NCURSES_SP_NAME(cbreak) (NCURSES_SP_ARG);

    erasec = NCURSES_SP_NAME(erasechar) (NCURSES_SP_ARG);
    killc = NCURSES_SP_NAME(killchar) (NCURSES_SP_ARG);

    oldstr = str;
    getyx(win, y, x);

    if (is_wintouched(win) || (win->_flags & _HASMOVED))
	wrefresh(win);

    while ((ch = wgetch_events(win, evl)) != ERR) {
	/*
	 * Some terminals (the Wyse-50 is the most common) generate
	 * a \n from the down-arrow key.  With this logic, it's the
	 * user's choice whether to set kcud=\n for wgetch();
	 * terminating *getstr() with \n should work either way.
	 */
	if (ch == '\n'
	    || ch == '\r'
	    || ch == KEY_DOWN
	    || ch == KEY_ENTER) {
	    if (oldecho == TRUE
		&& win->_cury == win->_maxy
		&& win->_scroll)
		wechochar(win, (chtype) '\n');
	    break;
	}
#ifdef KEY_EVENT
	if (ch == KEY_EVENT)
	    break;
#endif
#ifdef KEY_RESIZE
	if (ch == KEY_RESIZE)
	    break;
#endif
	if (ch == erasec || ch == KEY_LEFT || ch == KEY_BACKSPACE) {
	    if (str > oldstr) {
		str = WipeOut(win, y, x, oldstr, str, oldecho);
	    }
	} else if (ch == killc) {
	    while (str > oldstr) {
		str = WipeOut(win, y, x, oldstr, str, oldecho);
	    }
	} else if (ch >= KEY_MIN
		   || (maxlen >= 0 && str - oldstr >= maxlen)) {
	    NCURSES_SP_NAME(beep) (NCURSES_SP_ARG);
	} else {
	    *str++ = (char) ch;
	    if (oldecho == TRUE) {
		int oldy = win->_cury;
		if (waddch(win, (chtype) ch) == ERR) {
		    /*
		     * We can't really use the lower-right
		     * corner for input, since it'll mess
		     * up bookkeeping for erases.
		     */
		    win->_flags &= ~_WRAPPED;
		    waddch(win, (chtype) ' ');
		    str = WipeOut(win, y, x, oldstr, str, oldecho);
		    continue;
		} else if (win->_flags & _WRAPPED) {
		    /*
		     * If the last waddch forced a wrap &
		     * scroll, adjust our reference point
		     * for erasures.
		     */
		    if (win->_scroll
			&& oldy == win->_maxy
			&& win->_cury == win->_maxy) {
			if (--y <= 0) {
			    y = 0;
			}
		    }
		    win->_flags &= ~_WRAPPED;
		}
		wrefresh(win);
	    }
	}
    }

    win->_curx = 0;
    win->_flags &= ~_WRAPPED;
    if (win->_cury < win->_maxy)
	win->_cury++;
    wrefresh(win);

    /* Restore with a single I/O call, to fix minor asymmetry between
     * raw/noraw, etc.
     */
    sp->_nl = oldnl;
    sp->_echo = oldecho;
    sp->_raw = oldraw;
    sp->_cbreak = oldcbreak;

    NCURSES_SP_NAME(_nc_set_tty_mode) (NCURSES_SP_ARGx &buf);

    *str = '\0';
    if (ch == ERR)
	returnCode(ch);

    T(("wgetnstr returns %s", _nc_visbuf(oldstr)));

#ifdef KEY_EVENT
    if (ch == KEY_EVENT)
	returnCode(ch);
#endif
#ifdef KEY_RESIZE
    if (ch == KEY_RESIZE)
	returnCode(ch);
#endif

    returnCode(OK);
}

#ifdef NCURSES_WGETCH_EVENTS
NCURSES_EXPORT(int)
wgetnstr(WINDOW *win, char *str, int maxlen)
{
    returnCode(wgetnstr_events(win,
			       str,
			       maxlen,
			       EVENTLIST_1st((_nc_eventlist *) 0)));
}
#endif
