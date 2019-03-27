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
 *  Author: Thomas E. Dickey 1996-2003                                      *
 *     and: Zeyd M. Ben-Halim <zmbenhal@netcom.com> 1992,1995               *
 *     and: Eric S. Raymond <esr@snark.thyrsus.com>                         *
 ****************************************************************************/

/*
**	lib_scroll.c
**
**	The routine wscrl(win, n).
**  positive n scroll the window up (ie. move lines down)
**  negative n scroll the window down (ie. move lines up)
**
*/

#include <curses.priv.h>

MODULE_ID("$Id: lib_scroll.c,v 1.29 2011/10/22 16:34:50 tom Exp $")

NCURSES_EXPORT(void)
_nc_scroll_window(WINDOW *win,
		  int const n,
		  int const top,
		  int const bottom,
		  NCURSES_CH_T blank)
{
    int limit;
    int line;
    int j;
    size_t to_copy = (sizeof(NCURSES_CH_T) * (size_t) (win->_maxx + 1));

    TR(TRACE_MOVE, ("_nc_scroll_window(%p, %d, %ld, %ld)",
		    (void *) win, n, (long) top, (long) bottom));

    if (top < 0
	|| bottom < top
	|| bottom > win->_maxy) {
	TR(TRACE_MOVE, ("nothing to scroll"));
	return;
    }

    /*
     * This used to do a line-text pointer-shuffle instead of text copies.
     * That (a) doesn't work when the window is derived and doesn't have
     * its own storage, (b) doesn't save you a lot on modern machines
     * anyway.  Your typical memcpy implementations are coded in
     * assembler using a tight BLT loop; for the size of copies we're
     * talking here, the total execution time is dominated by the one-time
     * setup cost.  So there is no point in trying to be excessively
     * clever -- esr.
     */

    /* shift n lines downwards */
    if (n < 0) {
	limit = top - n;
	for (line = bottom; line >= limit && line >= 0; line--) {
	    TR(TRACE_MOVE, ("...copying %d to %d", line + n, line));
	    memcpy(win->_line[line].text,
		   win->_line[line + n].text,
		   to_copy);
	    if_USE_SCROLL_HINTS(win->_line[line].oldindex =
				win->_line[line + n].oldindex);
	}
	for (line = top; line < limit && line <= win->_maxy; line++) {
	    TR(TRACE_MOVE, ("...filling %d", line));
	    for (j = 0; j <= win->_maxx; j++)
		win->_line[line].text[j] = blank;
	    if_USE_SCROLL_HINTS(win->_line[line].oldindex = _NEWINDEX);
	}
    }

    /* shift n lines upwards */
    if (n > 0) {
	limit = bottom - n;
	for (line = top; line <= limit && line <= win->_maxy; line++) {
	    memcpy(win->_line[line].text,
		   win->_line[line + n].text,
		   to_copy);
	    if_USE_SCROLL_HINTS(win->_line[line].oldindex =
				win->_line[line + n].oldindex);
	}
	for (line = bottom; line > limit && line >= 0; line--) {
	    for (j = 0; j <= win->_maxx; j++)
		win->_line[line].text[j] = blank;
	    if_USE_SCROLL_HINTS(win->_line[line].oldindex = _NEWINDEX);
	}
    }
    touchline(win, top, bottom - top + 1);

    if_WIDEC({
	if (WINDOW_EXT(win, addch_used) != 0) {
	    int next = WINDOW_EXT(win, addch_y) + n;
	    if (next < 0 || next > win->_maxy) {
		TR(TRACE_VIRTPUT,
		   ("Alert discarded multibyte on scroll"));
		WINDOW_EXT(win, addch_y) = 0;
	    } else {
		TR(TRACE_VIRTPUT, ("scrolled working position to %d,%d",
				   WINDOW_EXT(win, addch_y),
				   WINDOW_EXT(win, addch_x)));
		WINDOW_EXT(win, addch_y) = next;
	    }
	}
    })
}

NCURSES_EXPORT(int)
wscrl(WINDOW *win, int n)
{
    T((T_CALLED("wscrl(%p,%d)"), (void *) win, n));

    if (!win || !win->_scroll) {
	TR(TRACE_MOVE, ("...scrollok is false"));
	returnCode(ERR);
    }

    if (n != 0) {
	_nc_scroll_window(win, n, win->_regtop, win->_regbottom, win->_nc_bkgd);
	_nc_synchook(win);
    }
    returnCode(OK);
}
