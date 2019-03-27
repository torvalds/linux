/****************************************************************************
 * Copyright (c) 1998-2012,2013 Free Software Foundation, Inc.              *
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
 *     and: Sven Verdoolaege                                                *
 *     and: Thomas E. Dickey                                                *
 ****************************************************************************/

/*
**	lib_insch.c
**
**	The routine winsch().
**
*/

#include <curses.priv.h>
#include <ctype.h>

MODULE_ID("$Id: lib_insch.c,v 1.35 2013/05/18 21:58:56 tom Exp $")

/*
 * Insert the given character, updating the current location to simplify
 * inserting a string.
 */
NCURSES_EXPORT(int)
_nc_insert_ch(SCREEN *sp, WINDOW *win, chtype ch)
{
    int code = OK;
    int ch8 = (int) ChCharOf(ch);
    NCURSES_CH_T wch;
    int count;
    NCURSES_CONST char *s;
    int tabsize = (
#if USE_REENTRANT
		      sp->_TABSIZE
#else
		      TABSIZE
#endif
    );

    switch (ch) {
    case '\t':
	for (count = (tabsize - (win->_curx % tabsize)); count > 0; count--) {
	    if ((code = _nc_insert_ch(sp, win, ' ')) != OK)
		break;
	}
	break;
    case '\n':
    case '\r':
    case '\b':
	SetChar2(wch, ch);
	_nc_waddch_nosync(win, wch);
	break;
    default:
	if (
#if USE_WIDEC_SUPPORT
	       WINDOW_EXT(win, addch_used) == 0 &&
#endif
	       (isprint(ch8) ||
		(ChAttrOf(ch) & A_ALTCHARSET) ||
		(sp != 0 && sp->_legacy_coding && !iscntrl(ch8)))) {
	    if (win->_curx <= win->_maxx) {
		struct ldat *line = &(win->_line[win->_cury]);
		NCURSES_CH_T *end = &(line->text[win->_curx]);
		NCURSES_CH_T *temp1 = &(line->text[win->_maxx]);
		NCURSES_CH_T *temp2 = temp1 - 1;

		SetChar2(wch, ch);

		CHANGED_TO_EOL(line, win->_curx, win->_maxx);
		while (temp1 > end)
		    *temp1-- = *temp2--;

		*temp1 = _nc_render(win, wch);
		win->_curx++;
	    }
	} else if (iscntrl(ch8)) {
	    s = NCURSES_SP_NAME(unctrl) (NCURSES_SP_ARGx (chtype) ch8);
	    while (*s != '\0') {
		code = _nc_insert_ch(sp, win, ChAttrOf(ch) | UChar(*s));
		if (code != OK)
		    break;
		++s;
	    }
	}
#if USE_WIDEC_SUPPORT
	else {
	    /*
	     * Handle multibyte characters here
	     */
	    SetChar2(wch, ch);
	    wch = _nc_render(win, wch);
	    count = _nc_build_wch(win, &wch);
	    if (count > 0) {
		code = _nc_insert_wch(win, &wch);
	    } else if (count == -1) {
		/* handle EILSEQ */
		s = NCURSES_SP_NAME(unctrl) (NCURSES_SP_ARGx (chtype) ch8);
		if (strlen(s) > 1) {
		    while (*s != '\0') {
			code = _nc_insert_ch(sp, win,
					     ChAttrOf(ch) | UChar(*s));
			if (code != OK)
			    break;
			++s;
		    }
		} else {
		    code = ERR;
		}
	    }
	}
#endif
	break;
    }
    return code;
}

NCURSES_EXPORT(int)
winsch(WINDOW *win, chtype c)
{
    NCURSES_SIZE_T oy;
    NCURSES_SIZE_T ox;
    int code = ERR;

    T((T_CALLED("winsch(%p, %s)"), (void *) win, _tracechtype(c)));

    if (win != 0) {
	oy = win->_cury;
	ox = win->_curx;

	code = _nc_insert_ch(_nc_screen_of(win), win, c);

	win->_curx = ox;
	win->_cury = oy;
	_nc_synchook(win);
    }
    returnCode(code);
}
