/****************************************************************************
 * Copyright (c) 1998-2009,2013 Free Software Foundation, Inc.              *
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
**	lib_overlay.c
**
**	The routines overlay(), copywin(), and overwrite().
**
*/

#include <curses.priv.h>

MODULE_ID("$Id: lib_overlay.c,v 1.31 2013/04/06 23:47:13 tom Exp $")

static int
overlap(const WINDOW *const src, WINDOW *const dst, int const flag)
{
    int rc = ERR;
    int sx1, sy1, sx2, sy2;
    int dx1, dy1, dx2, dy2;
    int sminrow, smincol;
    int dminrow, dmincol;
    int dmaxrow, dmaxcol;

    T((T_CALLED("overlap(%p,%p,%d)"), (const void *) src, (void *) dst, flag));

    if (src != 0 && dst != 0) {
	_nc_lock_global(curses);

	T(("src : begy %ld, begx %ld, maxy %ld, maxx %ld",
	   (long) src->_begy,
	   (long) src->_begx,
	   (long) src->_maxy,
	   (long) src->_maxx));
	T(("dst : begy %ld, begx %ld, maxy %ld, maxx %ld",
	   (long) dst->_begy,
	   (long) dst->_begx,
	   (long) dst->_maxy,
	   (long) dst->_maxx));

	sx1 = src->_begx;
	sy1 = src->_begy;
	sx2 = sx1 + src->_maxx;
	sy2 = sy1 + src->_maxy;

	dx1 = dst->_begx;
	dy1 = dst->_begy;
	dx2 = dx1 + dst->_maxx;
	dy2 = dy1 + dst->_maxy;

	if (dx2 >= sx1 && dx1 <= sx2 && dy2 >= sy1 && dy1 <= sy2) {
	    sminrow = max(sy1, dy1) - sy1;
	    smincol = max(sx1, dx1) - sx1;
	    dminrow = max(sy1, dy1) - dy1;
	    dmincol = max(sx1, dx1) - dx1;
	    dmaxrow = min(sy2, dy2) - dy1;
	    dmaxcol = min(sx2, dx2) - dx1;

	    rc = copywin(src, dst,
			 sminrow, smincol,
			 dminrow, dmincol,
			 dmaxrow, dmaxcol,
			 flag);
	}
	_nc_unlock_global(curses);
    }
    returnCode(rc);
}

/*
**
**	overlay(win1, win2)
**
**
**	overlay() writes the overlapping area of win1 behind win2
**	on win2 non-destructively.
**
**/

NCURSES_EXPORT(int)
overlay(const WINDOW *win1, WINDOW *win2)
{
    T((T_CALLED("overlay(%p,%p)"), (const void *) win1, (void *) win2));
    returnCode(overlap(win1, win2, TRUE));
}

/*
**
**	overwrite(win1, win2)
**
**
**	overwrite() writes the overlapping area of win1 behind win2
**	on win2 destructively.
**
**/

NCURSES_EXPORT(int)
overwrite(const WINDOW *win1, WINDOW *win2)
{
    T((T_CALLED("overwrite(%p,%p)"), (const void *) win1, (void *) win2));
    returnCode(overlap(win1, win2, FALSE));
}

NCURSES_EXPORT(int)
copywin(const WINDOW *src, WINDOW *dst,
	int sminrow, int smincol,
	int dminrow, int dmincol,
	int dmaxrow, int dmaxcol,
	int over)
{
    int rc = ERR;
    int sx, sy, dx, dy;
    bool touched;
    attr_t bk;
    attr_t mask;

    T((T_CALLED("copywin(%p, %p, %d, %d, %d, %d, %d, %d, %d)"),
       (const void *) src,
       (void *) dst,
       sminrow, smincol,
       dminrow, dmincol,
       dmaxrow, dmaxcol, over));

    if (src != 0
	&& dst != 0
	&& dmaxrow >= dminrow
	&& dmaxcol >= dmincol) {
	_nc_lock_global(curses);

	bk = AttrOf(dst->_nc_bkgd);
	mask = ~(attr_t) ((bk & A_COLOR) ? A_COLOR : 0);

	/* make sure rectangle exists in source */
	if ((sminrow + dmaxrow - dminrow) <= (src->_maxy + 1) &&
	    (smincol + dmaxcol - dmincol) <= (src->_maxx + 1)) {
	    bool copied = FALSE;

	    T(("rectangle exists in source"));

	    /* make sure rectangle fits in destination */
	    if (dmaxrow <= dst->_maxy && dmaxcol <= dst->_maxx) {

		T(("rectangle fits in destination"));

		for (dy = dminrow, sy = sminrow;
		     dy <= dmaxrow;
		     sy++, dy++) {

		    if (dy < 0 || sy < 0)
			continue;

		    touched = FALSE;
		    for (dx = dmincol, sx = smincol;
			 dx <= dmaxcol;
			 sx++, dx++) {

			if (dx < 0 || sx < 0)
			    continue;
			copied = TRUE;

			if (over) {
			    if ((CharOf(src->_line[sy].text[sx]) != L(' ')) &&
				(!CharEq(dst->_line[dy].text[dx],
					 src->_line[sy].text[sx]))) {
				dst->_line[dy].text[dx] =
				    src->_line[sy].text[sx];
				SetAttr(dst->_line[dy].text[dx],
					((AttrOf(src->_line[sy].text[sx]) &
					  mask) | bk));
				touched = TRUE;
			    }
			} else {
			    if (!CharEq(dst->_line[dy].text[dx],
					src->_line[sy].text[sx])) {
				dst->_line[dy].text[dx] =
				    src->_line[sy].text[sx];
				touched = TRUE;
			    }
			}
		    }
		    if (touched) {
			touchline(dst, dminrow, (dmaxrow - dminrow + 1));
		    }
		}
		T(("finished copywin"));
		if (copied)
		    rc = OK;
	    }
	}
	_nc_unlock_global(curses);
    }
    returnCode(rc);
}
