/****************************************************************************
 * Copyright (c) 2004-2010,2011 Free Software Foundation, Inc.              *
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

/*
**	lib_add_wch.c
**
**	The routine wadd_wch().
**
*/

#include <curses.priv.h>

#if HAVE_WCTYPE_H
#include <wctype.h>
#endif

MODULE_ID("$Id: lib_add_wch.c,v 1.12 2011/03/22 09:31:15 Petr.Pavlu Exp $")

/* clone/adapt lib_addch.c */
static const cchar_t blankchar = NewChar(BLANK_TEXT);

/*
 * Ugly microtweaking alert.  Everything from here to end of module is
 * likely to be speed-critical -- profiling data sure says it is!
 * Most of the important screen-painting functions are shells around
 * wadd_wch().  So we make every effort to reduce function-call overhead
 * by inlining stuff, even at the cost of making wrapped copies for
 * export.  Also we supply some internal versions that don't call the
 * window sync hook, for use by string-put functions.
 */

/* Return bit mask for clearing color pair number if given ch has color */
#define COLOR_MASK(ch) (~(attr_t)((ch) & A_COLOR ? A_COLOR : 0))

static NCURSES_INLINE cchar_t
render_char(WINDOW *win, cchar_t ch)
/* compute a rendition of the given char correct for the current context */
{
    attr_t a = WINDOW_ATTRS(win);
    int pair = GetPair(ch);

    if (ISBLANK(ch)
	&& AttrOf(ch) == A_NORMAL
	&& pair == 0) {
	/* color/pair in attrs has precedence over bkgrnd */
	ch = win->_nc_bkgd;
	SetAttr(ch, a | AttrOf(win->_nc_bkgd));
	if ((pair = GET_WINDOW_PAIR(win)) == 0)
	    pair = GetPair(win->_nc_bkgd);
	SetPair(ch, pair);
    } else {
	/* color in attrs has precedence over bkgrnd */
	a |= AttrOf(win->_nc_bkgd) & COLOR_MASK(a);
	/* color in ch has precedence */
	if (pair == 0) {
	    if ((pair = GET_WINDOW_PAIR(win)) == 0)
		pair = GetPair(win->_nc_bkgd);
	}
	AddAttr(ch, (a & COLOR_MASK(AttrOf(ch))));
	SetPair(ch, pair);
    }

    TR(TRACE_VIRTPUT,
       ("render_char bkg %s (%d), attrs %s (%d) -> ch %s (%d)",
	_tracech_t2(1, CHREF(win->_nc_bkgd)),
	GetPair(win->_nc_bkgd),
	_traceattr(WINDOW_ATTRS(win)),
	GET_WINDOW_PAIR(win),
	_tracech_t2(3, CHREF(ch)),
	GetPair(ch)));

    return (ch);
}

/* check if position is legal; if not, return error */
#ifndef NDEBUG			/* treat this like an assertion */
#define CHECK_POSITION(win, x, y) \
	if (y > win->_maxy \
	 || x > win->_maxx \
	 || y < 0 \
	 || x < 0) { \
		TR(TRACE_VIRTPUT, ("Alert! Win=%p _curx = %d, _cury = %d " \
				   "(_maxx = %d, _maxy = %d)", win, x, y, \
				   win->_maxx, win->_maxy)); \
		return(ERR); \
	}
#else
#define CHECK_POSITION(win, x, y)	/* nothing */
#endif

static bool
newline_forces_scroll(WINDOW *win, NCURSES_SIZE_T * ypos)
{
    bool result = FALSE;

    if (*ypos >= win->_regtop && *ypos == win->_regbottom) {
	*ypos = win->_regbottom;
	result = TRUE;
    } else {
	*ypos = (NCURSES_SIZE_T) (*ypos + 1);
    }
    return result;
}

/*
 * The _WRAPPED flag is useful only for telling an application that we've just
 * wrapped the cursor.  We don't do anything with this flag except set it when
 * wrapping, and clear it whenever we move the cursor.  If we try to wrap at
 * the lower-right corner of a window, we cannot move the cursor (since that
 * wouldn't be legal).  So we return an error (which is what SVr4 does). 
 * Unlike SVr4, we can successfully add a character to the lower-right corner
 * (Solaris 2.6 does this also, however).
 */
static int
wrap_to_next_line(WINDOW *win)
{
    win->_flags |= _WRAPPED;
    if (newline_forces_scroll(win, &(win->_cury))) {
	win->_curx = win->_maxx;
	if (!win->_scroll)
	    return (ERR);
	scroll(win);
    }
    win->_curx = 0;
    return (OK);
}

static int wadd_wch_literal(WINDOW *, cchar_t);
/*
 * Fill the given number of cells with blanks using the current background
 * rendition.  This saves/restores the current x-position.
 */
static void
fill_cells(WINDOW *win, int count)
{
    cchar_t blank = blankchar;
    int save_x = win->_curx;
    int save_y = win->_cury;

    while (count-- > 0) {
	if (wadd_wch_literal(win, blank) == ERR)
	    break;
    }
    win->_curx = (NCURSES_SIZE_T) save_x;
    win->_cury = (NCURSES_SIZE_T) save_y;
}

static int
wadd_wch_literal(WINDOW *win, cchar_t ch)
{
    int x;
    int y;
    struct ldat *line;

    x = win->_curx;
    y = win->_cury;

    CHECK_POSITION(win, x, y);

    ch = render_char(win, ch);

    line = win->_line + y;

    CHANGED_CELL(line, x);

    /*
     * Non-spacing characters are added to the current cell.
     *
     * Spacing characters that are wider than one column require some display
     * adjustments.
     */
    {
	int len = wcwidth(CharOf(ch));
	int i;
	int j;
	wchar_t *chars;

	if (len == 0) {		/* non-spacing */
	    if ((x > 0 && y >= 0)
		|| (win->_maxx >= 0 && win->_cury >= 1)) {
		if (x > 0 && y >= 0)
		    chars = (win->_line[y].text[x - 1].chars);
		else
		    chars = (win->_line[y - 1].text[win->_maxx].chars);
		for (i = 0; i < CCHARW_MAX; ++i) {
		    if (chars[i] == 0) {
			TR(TRACE_VIRTPUT,
			   ("added non-spacing %d: %x",
			    x, (int) CharOf(ch)));
			chars[i] = CharOf(ch);
			break;
		    }
		}
	    }
	    goto testwrapping;
	} else if (len > 1) {	/* multi-column characters */
	    /*
	     * Check if the character will fit on the current line.  If it does
	     * not fit, fill in the remainder of the line with blanks.  and
	     * move to the next line.
	     */
	    if (len > win->_maxx + 1) {
		TR(TRACE_VIRTPUT, ("character will not fit"));
		return ERR;
	    } else if (x + len > win->_maxx + 1) {
		int count = win->_maxx + 1 - x;
		TR(TRACE_VIRTPUT, ("fill %d remaining cells", count));
		fill_cells(win, count);
		if (wrap_to_next_line(win) == ERR)
		    return ERR;
		x = win->_curx;
		y = win->_cury;
		line = win->_line + y;
	    }
	    /*
	     * Check for cells which are orphaned by adding this character, set
	     * those to blanks.
	     *
	     * FIXME: this actually could fill j-i cells, more complicated to
	     * setup though.
	     */
	    for (i = 0; i < len; ++i) {
		if (isWidecBase(win->_line[y].text[x + i])) {
		    break;
		} else if (isWidecExt(win->_line[y].text[x + i])) {
		    for (j = i; x + j <= win->_maxx; ++j) {
			if (!isWidecExt(win->_line[y].text[x + j])) {
			    TR(TRACE_VIRTPUT, ("fill %d orphan cells", j));
			    fill_cells(win, j);
			    break;
			}
		    }
		    break;
		}
	    }
	    /*
	     * Finally, add the cells for this character.
	     */
	    for (i = 0; i < len; ++i) {
		cchar_t value = ch;
		SetWidecExt(value, i);
		TR(TRACE_VIRTPUT, ("multicolumn %d:%d (%d,%d)",
				   i + 1, len,
				   win->_begy + y, win->_begx + x));
		line->text[x] = value;
		CHANGED_CELL(line, x);
		++x;
	    }
	    goto testwrapping;
	}
    }

    /*
     * Single-column characters.
     */
    line->text[x++] = ch;
    /*
     * This label is used only for wide-characters.
     */
  testwrapping:

    TR(TRACE_VIRTPUT, ("cell (%ld, %ld..%d) = %s",
		       (long) win->_cury, (long) win->_curx, x - 1,
		       _tracech_t(CHREF(ch))));

    if (x > win->_maxx) {
	return wrap_to_next_line(win);
    }
    win->_curx = (NCURSES_SIZE_T) x;
    return OK;
}

static NCURSES_INLINE int
wadd_wch_nosync(WINDOW *win, cchar_t ch)
/* the workhorse function -- add a character to the given window */
{
    NCURSES_SIZE_T x, y;
    wchar_t *s;
    int tabsize = 8;
#if USE_REENTRANT
    SCREEN *sp = _nc_screen_of(win);
#endif

    /*
     * If we are using the alternate character set, forget about locale.
     * Otherwise, if the locale claims the code is printable, treat it that
     * way.
     */
    if ((AttrOf(ch) & A_ALTCHARSET)
	|| iswprint((wint_t) CharOf(ch)))
	return wadd_wch_literal(win, ch);

    /*
     * Handle carriage control and other codes that are not printable, or are
     * known to expand to more than one character according to unctrl().
     */
    x = win->_curx;
    y = win->_cury;

    switch (CharOf(ch)) {
    case '\t':
#if USE_REENTRANT
	tabsize = *ptrTabsize(sp);
#else
	tabsize = TABSIZE;
#endif
	x = (NCURSES_SIZE_T) (x + (tabsize - (x % tabsize)));
	/*
	 * Space-fill the tab on the bottom line so that we'll get the
	 * "correct" cursor position.
	 */
	if ((!win->_scroll && (y == win->_regbottom))
	    || (x <= win->_maxx)) {
	    cchar_t blank = blankchar;
	    AddAttr(blank, AttrOf(ch));
	    while (win->_curx < x) {
		if (wadd_wch_literal(win, blank) == ERR)
		    return (ERR);
	    }
	    break;
	} else {
	    wclrtoeol(win);
	    win->_flags |= _WRAPPED;
	    if (newline_forces_scroll(win, &y)) {
		x = win->_maxx;
		if (win->_scroll) {
		    scroll(win);
		    x = 0;
		}
	    } else {
		x = 0;
	    }
	}
	break;
    case '\n':
	wclrtoeol(win);
	if (newline_forces_scroll(win, &y)) {
	    if (win->_scroll)
		scroll(win);
	    else
		return (ERR);
	}
	/* FALLTHRU */
    case '\r':
	x = 0;
	win->_flags &= ~_WRAPPED;
	break;
    case '\b':
	if (x == 0)
	    return (OK);
	x--;
	win->_flags &= ~_WRAPPED;
	break;
    default:
	if ((s = wunctrl(&ch)) != 0) {
	    while (*s) {
		cchar_t sch;
		SetChar(sch, *s++, AttrOf(ch));
		if_EXT_COLORS(SetPair(sch, GetPair(ch)));
		if (wadd_wch_literal(win, sch) == ERR)
		    return ERR;
	    }
	    return OK;
	}
	return ERR;
    }

    win->_curx = x;
    win->_cury = y;

    return OK;
}

/*
 * The versions below call _nc_synchook().  We wanted to avoid this in the
 * version exported for string puts; they'll call _nc_synchook once at end
 * of run.
 */

/* These are actual entry points */

NCURSES_EXPORT(int)
wadd_wch(WINDOW *win, const cchar_t *wch)
{
    int code = ERR;

    TR(TRACE_VIRTPUT | TRACE_CCALLS, (T_CALLED("wadd_wch(%p, %s)"),
				      (void *) win,
				      _tracecchar_t(wch)));

    if (win && (wadd_wch_nosync(win, *wch) != ERR)) {
	_nc_synchook(win);
	code = OK;
    }

    TR(TRACE_VIRTPUT | TRACE_CCALLS, (T_RETURN("%d"), code));
    return (code);
}

NCURSES_EXPORT(int)
wecho_wchar(WINDOW *win, const cchar_t *wch)
{
    int code = ERR;

    TR(TRACE_VIRTPUT | TRACE_CCALLS, (T_CALLED("wechochar(%p, %s)"),
				      (void *) win,
				      _tracecchar_t(wch)));

    if (win && (wadd_wch_nosync(win, *wch) != ERR)) {
	bool save_immed = win->_immed;
	win->_immed = TRUE;
	_nc_synchook(win);
	win->_immed = save_immed;
	code = OK;
    }
    TR(TRACE_VIRTPUT | TRACE_CCALLS, (T_RETURN("%d"), code));
    return (code);
}
