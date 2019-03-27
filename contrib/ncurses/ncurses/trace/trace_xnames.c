/****************************************************************************
 * Copyright (c) 1999-2000,2010 Free Software Foundation, Inc.              *
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
 *  Author: Thomas E. Dickey <dickey@clark.net> 1999                        *
 ****************************************************************************/
/*
 *	trace_xnames.c - Tracing/Debugging buffers (TERMTYPE extended names)
 */

#include <curses.priv.h>

MODULE_ID("$Id: trace_xnames.c,v 1.6 2010/01/23 17:59:27 tom Exp $")

NCURSES_EXPORT(void)
_nc_trace_xnames(TERMTYPE *tp GCC_UNUSED)
{
#ifdef TRACE
#if NCURSES_XNAMES
    int limit = tp->ext_Booleans + tp->ext_Numbers + tp->ext_Strings;
    int n, m;
    if (limit) {
	int begin_num = tp->ext_Booleans;
	int begin_str = tp->ext_Booleans + tp->ext_Numbers;

	_tracef("extended names (%s) %d = %d+%d+%d of %d+%d+%d",
		tp->term_names,
		limit,
		tp->ext_Booleans, tp->ext_Numbers, tp->ext_Strings,
		tp->num_Booleans, tp->num_Numbers, tp->num_Strings);
	for (n = 0; n < limit; n++) {
	    if ((m = n - begin_str) >= 0) {
		_tracef("[%d] %s = %s", n,
			tp->ext_Names[n],
			_nc_visbuf(tp->Strings[tp->num_Strings + m - tp->ext_Strings]));
	    } else if ((m = n - begin_num) >= 0) {
		_tracef("[%d] %s = %d (num)", n,
			tp->ext_Names[n],
			tp->Numbers[tp->num_Numbers + m - tp->ext_Numbers]);
	    } else {
		_tracef("[%d] %s = %d (bool)", n,
			tp->ext_Names[n],
			tp->Booleans[tp->num_Booleans + n - tp->ext_Booleans]);
	    }
	}
    }
#endif
#endif
}
