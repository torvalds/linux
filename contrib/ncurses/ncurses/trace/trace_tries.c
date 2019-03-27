/****************************************************************************
 * Copyright (c) 1999-2011,2012 Free Software Foundation, Inc.              *
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
 *  Author: Thomas E. Dickey 1999                                           *
 ****************************************************************************/
/*
 *	trace_tries.c - Tracing/Debugging buffers (keycode tries-trees)
 */

#include <curses.priv.h>

MODULE_ID("$Id: trace_tries.c,v 1.17 2012/10/27 20:50:50 tom Exp $")

#ifdef TRACE
#define my_buffer _nc_globals.tracetry_buf
#define my_length _nc_globals.tracetry_used

static void
recur_tries(TRIES * tree, unsigned level)
{
    if (level > my_length) {
	my_length = (level + 1) * 4;
	my_buffer = (unsigned char *) _nc_doalloc(my_buffer, my_length);
    }

    if (my_buffer != 0) {
	while (tree != 0) {
	    if ((my_buffer[level] = tree->ch) == 0)
		my_buffer[level] = 128;
	    my_buffer[level + 1] = 0;
	    if (tree->value != 0) {
		_tracef("%5d: %s (%s)", tree->value,
			_nc_visbuf((char *) my_buffer), keyname(tree->value));
	    }
	    if (tree->child)
		recur_tries(tree->child, level + 1);
	    tree = tree->sibling;
	}
    }
}

NCURSES_EXPORT(void)
_nc_trace_tries(TRIES * tree)
{
    if ((my_buffer = typeMalloc(unsigned char, my_length = 80)) != 0) {
	_tracef("BEGIN tries %p", (void *) tree);
	recur_tries(tree, 0);
	_tracef(". . . tries %p", (void *) tree);
	free(my_buffer);
    }
}

#else
EMPTY_MODULE(_nc_empty_trace_tries)
#endif
