/****************************************************************************
 * Copyright (c) 2007-2008,2009 Free Software Foundation, Inc.              *
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
 *     Author: Thomas E. Dickey                        2007                 *
 ****************************************************************************/

#include <curses.priv.h>

MODULE_ID("$Id: use_screen.c,v 1.8 2009/10/24 22:40:20 tom Exp $")

NCURSES_EXPORT(int)
use_screen(SCREEN *screen, NCURSES_SCREEN_CB func, void *data)
{
    SCREEN *save_SP;
    int code = OK;

    T((T_CALLED("use_screen(%p,%p,%p)"), (void *) screen, func, (void *) data));

    /*
     * FIXME - add a flag so a given thread can check if _it_ has already
     * recurred through this point, return an error if so.
     */
    _nc_lock_global(curses);
    save_SP = CURRENT_SCREEN;
    set_term(screen);

    code = func(screen, data);

    set_term(save_SP);
    _nc_unlock_global(curses);
    returnCode(code);
}
