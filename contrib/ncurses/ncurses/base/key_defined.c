/****************************************************************************
 * Copyright (c) 2003-2006,2009 Free Software Foundation, Inc.              *
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
 *  Author: Thomas E. Dickey, 2003                                          *
 ****************************************************************************/

#include <curses.priv.h>

MODULE_ID("$Id: key_defined.c,v 1.9 2009/10/24 22:15:47 tom Exp $")

static int
find_definition(TRIES * tree, const char *str)
{
    TRIES *ptr;
    int result = OK;

    if (str != 0 && *str != '\0') {
	for (ptr = tree; ptr != 0; ptr = ptr->sibling) {
	    if (UChar(*str) == UChar(ptr->ch)) {
		if (str[1] == '\0' && ptr->child != 0) {
		    result = ERR;
		} else if ((result = find_definition(ptr->child, str + 1))
			   == OK) {
		    result = ptr->value;
		} else if (str[1] == '\0') {
		    result = ERR;
		}
	    }
	    if (result != OK)
		break;
	}
    }
    return (result);
}

/*
 * Returns the keycode associated with the given string.  If none is found,
 * return OK.  If the string is only a prefix to other strings, return ERR.
 * Otherwise, return the keycode's value (neither OK/ERR).
 */
NCURSES_EXPORT(int)
NCURSES_SP_NAME(key_defined) (NCURSES_SP_DCLx const char *str)
{
    int code = ERR;

    T((T_CALLED("key_defined(%p, %s)"), (void *) SP_PARM, _nc_visbuf(str)));
    if (SP_PARM != 0 && str != 0) {
	code = find_definition(SP_PARM->_keytry, str);
    }

    returnCode(code);
}

#if NCURSES_SP_FUNCS
NCURSES_EXPORT(int)
key_defined(const char *str)
{
    return NCURSES_SP_NAME(key_defined) (CURRENT_SCREEN, str);
}
#endif
