/****************************************************************************
 * Copyright (c) 1998-2006,2009 Free Software Foundation, Inc.              *
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
 *  Author: Thomas E. Dickey                        1997-on                 *
 *     and: Juergen Pfeifer                         2009                    *
 ****************************************************************************/

#include <curses.priv.h>

MODULE_ID("$Id: define_key.c,v 1.20 2009/11/28 22:53:17 tom Exp $")

NCURSES_EXPORT(int)
NCURSES_SP_NAME(define_key) (NCURSES_SP_DCLx const char *str, int keycode)
{
    int code = ERR;

    T((T_CALLED("define_key(%p, %s,%d)"), (void *) SP_PARM, _nc_visbuf(str), keycode));
    if (SP_PARM == 0 || !HasTInfoTerminal(SP_PARM)) {
	code = ERR;
    } else if (keycode > 0) {
	unsigned ukey = (unsigned) keycode;

#ifdef USE_TERM_DRIVER
#define CallHasKey(keycode) CallDriver_1(SP_PARM, kyExist, keycode)
#else
#define CallHasKey(keycode) NCURSES_SP_NAME(has_key)(NCURSES_SP_ARGx keycode)
#endif

	if (str != 0) {
	    NCURSES_SP_NAME(define_key) (NCURSES_SP_ARGx str, 0);
	} else if (CallHasKey(keycode)) {
	    while (_nc_remove_key(&(SP_PARM->_keytry), ukey))
		code = OK;
	}
	if (str != 0) {
	    if (NCURSES_SP_NAME(key_defined) (NCURSES_SP_ARGx str) == 0) {
		if (_nc_add_to_try(&(SP_PARM->_keytry), str, ukey) == OK) {
		    code = OK;
		} else {
		    code = ERR;
		}
	    } else {
		code = ERR;
	    }
	}
    } else {
	while (_nc_remove_string(&(SP_PARM->_keytry), str))
	    code = OK;
    }
    returnCode(code);
}

#if NCURSES_SP_FUNCS
NCURSES_EXPORT(int)
define_key(const char *str, int keycode)
{
    return NCURSES_SP_NAME(define_key) (CURRENT_SCREEN, str, keycode);
}
#endif
