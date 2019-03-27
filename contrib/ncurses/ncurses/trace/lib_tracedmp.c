/****************************************************************************
 * Copyright (c) 1998-2009,2012 Free Software Foundation, Inc.              *
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
 *  Author: Thomas E. Dickey 1996-on                                        *
 *     and: Zeyd M. Ben-Halim <zmbenhal@netcom.com> 1992,1995               *
 *     and: Eric S. Raymond <esr@snark.thyrsus.com>                         *
 ****************************************************************************/

/*
 *	lib_tracedmp.c - Tracing/Debugging routines
 */

#include <curses.priv.h>
#include <ctype.h>

MODULE_ID("$Id: lib_tracedmp.c,v 1.34 2012/10/27 20:54:42 tom Exp $")

#ifdef TRACE

#define my_buffer _nc_globals.tracedmp_buf
#define my_length _nc_globals.tracedmp_used

NCURSES_EXPORT(void)
_tracedump(const char *name, WINDOW *win)
{
    int i, j, n, width;

    /* compute narrowest possible display width */
    for (width = i = 0; i <= win->_maxy; ++i) {
	n = 0;
	for (j = 0; j <= win->_maxx; ++j) {
	    if (CharOf(win->_line[i].text[j]) != L(' ')
		|| AttrOf(win->_line[i].text[j]) != A_NORMAL
		|| GetPair(win->_line[i].text[j]) != 0) {
		n = j;
	    }
	}

	if (n > width)
	    width = n;
    }
    if (width < win->_maxx)
	++width;
    if (++width + 1 > (int) my_length) {
	my_length = (unsigned) (2 * (width + 1));
	my_buffer = typeRealloc(char, my_length, my_buffer);
	if (my_buffer == 0)
	    return;
    }

    for (n = 0; n <= win->_maxy; ++n) {
	char *ep = my_buffer;
	bool haveattrs, havecolors;

	/*
	 * Dump A_CHARTEXT part.  It is more important to make the grid line up
	 * in the trace file than to represent control- and wide-characters, so
	 * we map those to '.' and '?' respectively.
	 */
	for (j = 0; j < width; ++j) {
	    chtype test = (chtype) CharOf(win->_line[n].text[j]);
	    ep[j] = (char) ((UChar(test) == test
#if USE_WIDEC_SUPPORT
			     && (win->_line[n].text[j].chars[1] == 0)
#endif
			    )
			    ? (iscntrl(UChar(test))
			       ? '.'
			       : UChar(test))
			    : '?');
	}
	ep[j] = '\0';
	_tracef("%s[%2d] %3ld%3ld ='%s'",
		name, n,
		(long) win->_line[n].firstchar,
		(long) win->_line[n].lastchar,
		ep);

	/* if there are multi-column characters on the line, print them now */
	if_WIDEC({
	    bool multicolumn = FALSE;
	    for (j = 0; j < width; ++j)
		if (WidecExt(win->_line[n].text[j]) != 0) {
		    multicolumn = TRUE;
		    break;
		}
	    if (multicolumn) {
		ep = my_buffer;
		for (j = 0; j < width; ++j) {
		    int test = WidecExt(win->_line[n].text[j]);
		    if (test) {
			ep[j] = (char) (test + '0');
		    } else {
			ep[j] = ' ';
		    }
		}
		ep[j] = '\0';
		_tracef("%*s[%2d]%*s='%s'", (int) strlen(name),
			"widec", n, 8, " ", my_buffer);
	    }
	});

	/* dump A_COLOR part, will screw up if there are more than 96 */
	havecolors = FALSE;
	for (j = 0; j < width; ++j)
	    if (GetPair(win->_line[n].text[j]) != 0) {
		havecolors = TRUE;
		break;
	    }
	if (havecolors) {
	    ep = my_buffer;
	    for (j = 0; j < width; ++j) {
		int pair = GetPair(win->_line[n].text[j]);
		if (pair >= 52)
		    ep[j] = '?';
		else if (pair >= 36)
		    ep[j] = (char) (pair + 'A');
		else if (pair >= 10)
		    ep[j] = (char) (pair + 'a');
		else if (pair >= 1)
		    ep[j] = (char) (pair + '0');
		else
		    ep[j] = ' ';
	    }
	    ep[j] = '\0';
	    _tracef("%*s[%2d]%*s='%s'", (int) strlen(name),
		    "colors", n, 8, " ", my_buffer);
	}

	for (i = 0; i < 4; ++i) {
	    const char *hex = " 123456789ABCDEF";
	    attr_t mask = (attr_t) (0xf << ((i + 4) * 4));

	    haveattrs = FALSE;
	    for (j = 0; j < width; ++j)
		if (AttrOf(win->_line[n].text[j]) & mask) {
		    haveattrs = TRUE;
		    break;
		}
	    if (haveattrs) {
		ep = my_buffer;
		for (j = 0; j < width; ++j)
		    ep[j] = hex[(AttrOf(win->_line[n].text[j]) & mask) >>
				((i + 4) * 4)];
		ep[j] = '\0';
		_tracef("%*s%d[%2d]%*s='%s'", (int) strlen(name) -
			1, "attrs", i, n, 8, " ", my_buffer);
	    }
	}
    }
#if NO_LEAKS
    free(my_buffer);
    my_buffer = 0;
    my_length = 0;
#endif
}

#else
EMPTY_MODULE(_nc_lib_tracedmp)
#endif /* TRACE */
