/****************************************************************************
 * Copyright (c) 1999-2009,2010 Free Software Foundation, Inc.              *
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

#include <curses.priv.h>
#include <tic.h>		/* struct tinfo_fkeys */

MODULE_ID("$Id: init_keytry.c,v 1.17 2010/04/24 22:29:56 tom Exp $")

/*
**      _nc_init_keytry()
**
**      Construct the try for the current terminal's keypad keys.
**
*/

/*
 * Internal entrypoints use SCREEN* parameter to obtain capabilities rather
 * than cur_term.
 */
#undef CUR
#define CUR SP_TERMTYPE

#if	BROKEN_LINKER
#undef	_nc_tinfo_fkeys
#endif

/* LINT_PREPRO
#if 0*/
#include <init_keytry.h>
/* LINT_PREPRO
#endif*/

#if	BROKEN_LINKER
const struct tinfo_fkeys *
_nc_tinfo_fkeysf(void)
{
    return _nc_tinfo_fkeys;
}
#endif

NCURSES_EXPORT(void)
_nc_init_keytry(SCREEN *sp)
{
    unsigned n;

    /* The sp->_keytry value is initialized in newterm(), where the sp
     * structure is created, because we can not tell where keypad() or
     * mouse_activate() (which will call keyok()) are first called.
     */

    if (sp != 0) {
	for (n = 0; _nc_tinfo_fkeys[n].code; n++) {
	    if (_nc_tinfo_fkeys[n].offset < STRCOUNT) {
		(void) _nc_add_to_try(&(sp->_keytry),
				      CUR Strings[_nc_tinfo_fkeys[n].offset],
				      _nc_tinfo_fkeys[n].code);
	    }
	}
#if NCURSES_XNAMES
	/*
	 * Add any of the extended strings to the tries if their name begins
	 * with 'k', i.e., they follow the convention of other terminfo key
	 * names.
	 */
	{
	    TERMTYPE *tp = &(sp->_term->type);
	    for (n = STRCOUNT; n < NUM_STRINGS(tp); ++n) {
		const char *name = ExtStrname(tp, (int) n, strnames);
		char *value = tp->Strings[n];
		if (name != 0
		    && *name == 'k'
		    && value != 0
		    && NCURSES_SP_NAME(key_defined) (NCURSES_SP_ARGx
						     value) == 0) {
		    (void) _nc_add_to_try(&(sp->_keytry),
					  value,
					  n - STRCOUNT + KEY_MAX);
		}
	    }
	}
#endif
#ifdef TRACE
	_nc_trace_tries(sp->_keytry);
#endif
    }
}
