/****************************************************************************
 * Copyright (c) 1998-2008,2009 Free Software Foundation, Inc.              *
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
 *     and: Thomas E. Dickey                        1996-on                 *
 *     and: Juergen Pfeifer                         2009                    *
 ****************************************************************************/

/*
 *	lib_slkinit.c
 *	Soft key routines.
 *      Initialize soft labels.  Called by the user before initscr().
 */
#include <curses.priv.h>

MODULE_ID("$Id: lib_slkinit.c,v 1.13 2009/10/31 00:10:46 tom Exp $")

#ifdef USE_SP_RIPOFF
#define SoftkeyFormat SP_PARM->slk_format
#else
#define SoftkeyFormat _nc_globals.slk_format
#endif

NCURSES_EXPORT(int)
NCURSES_SP_NAME(slk_init) (NCURSES_SP_DCLx int format)
{
    int code = ERR;

    START_TRACE();
    T((T_CALLED("slk_init(%p,%d)"), (void *) SP_PARM, format));

    if (format >= 0
	&& format <= 3
#ifdef USE_SP_RIPOFF
	&& SP_PARM
	&& SP_PARM->_prescreen
#endif
	&& !SoftkeyFormat) {
	SoftkeyFormat = 1 + format;
	code = NCURSES_SP_NAME(_nc_ripoffline) (NCURSES_SP_ARGx
						-SLK_LINES(SoftkeyFormat),
						_nc_slk_initialize);
    }
    returnCode(code);
}

#if NCURSES_SP_FUNCS
NCURSES_EXPORT(int)
slk_init(int format)
{
    return NCURSES_SP_NAME(slk_init) (CURRENT_SCREEN_PRE, format);
}
#endif
