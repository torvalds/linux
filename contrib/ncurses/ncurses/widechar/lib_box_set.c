/****************************************************************************
 * Copyright (c) 2002-2009,2011 Free Software Foundation, Inc.              *
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
 * Authors: Sven Verdoolaege and Thomas Dickey 2001,2002                    *
 ****************************************************************************/

/*
**	lib_box_set.c
**
**	The routine wborder_set().
**
*/

#include <curses.priv.h>

MODULE_ID("$Id: lib_box_set.c,v 1.6 2011/06/25 19:02:07 Vassili.Courzakis Exp $")

NCURSES_EXPORT(int)
wborder_set(WINDOW *win,
	    const ARG_CH_T ls, const ARG_CH_T rs,
	    const ARG_CH_T ts, const ARG_CH_T bs,
	    const ARG_CH_T tl, const ARG_CH_T tr,
	    const ARG_CH_T bl, const ARG_CH_T br)
{
    NCURSES_SIZE_T i;
    NCURSES_SIZE_T endx, endy;
    NCURSES_CH_T wls, wrs, wts, wbs, wtl, wtr, wbl, wbr;

    T((T_CALLED("wborder_set(%p,%s,%s,%s,%s,%s,%s,%s,%s)"),
       (void *) win,
       _tracech_t2(1, ls),
       _tracech_t2(2, rs),
       _tracech_t2(3, ts),
       _tracech_t2(4, bs),
       _tracech_t2(5, tl),
       _tracech_t2(6, tr),
       _tracech_t2(7, bl),
       _tracech_t2(8, br)));

    if (!win)
	returnCode(ERR);

#define RENDER_WITH_DEFAULT(ch,def) w ##ch = _nc_render(win, (ch == 0) ? *(const ARG_CH_T)def : *ch)

    RENDER_WITH_DEFAULT(ls, WACS_VLINE);
    RENDER_WITH_DEFAULT(rs, WACS_VLINE);
    RENDER_WITH_DEFAULT(ts, WACS_HLINE);
    RENDER_WITH_DEFAULT(bs, WACS_HLINE);
    RENDER_WITH_DEFAULT(tl, WACS_ULCORNER);
    RENDER_WITH_DEFAULT(tr, WACS_URCORNER);
    RENDER_WITH_DEFAULT(bl, WACS_LLCORNER);
    RENDER_WITH_DEFAULT(br, WACS_LRCORNER);

    T(("using %s, %s, %s, %s, %s, %s, %s, %s",
       _tracech_t2(1, CHREF(wls)),
       _tracech_t2(2, CHREF(wrs)),
       _tracech_t2(3, CHREF(wts)),
       _tracech_t2(4, CHREF(wbs)),
       _tracech_t2(5, CHREF(wtl)),
       _tracech_t2(6, CHREF(wtr)),
       _tracech_t2(7, CHREF(wbl)),
       _tracech_t2(8, CHREF(wbr))));

    endx = win->_maxx;
    endy = win->_maxy;

    for (i = 0; i <= endx; i++) {
	win->_line[0].text[i] = wts;
	win->_line[endy].text[i] = wbs;
    }
    win->_line[endy].firstchar = win->_line[0].firstchar = 0;
    win->_line[endy].lastchar = win->_line[0].lastchar = endx;

    for (i = 0; i <= endy; i++) {
	win->_line[i].text[0] = wls;
	win->_line[i].text[endx] = wrs;
	win->_line[i].firstchar = 0;
	win->_line[i].lastchar = endx;
    }
    win->_line[0].text[0] = wtl;
    win->_line[0].text[endx] = wtr;
    win->_line[endy].text[0] = wbl;
    win->_line[endy].text[endx] = wbr;

    _nc_synchook(win);
    returnCode(OK);
}
