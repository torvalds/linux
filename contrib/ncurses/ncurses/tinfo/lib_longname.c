/****************************************************************************
 * Copyright (c) 1998-2009,2010 Free Software Foundation, Inc.              *
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
**	lib_longname.c
**
**	The routine longname().
**
*/

#include <curses.priv.h>

MODULE_ID("$Id: lib_longname.c,v 1.12 2010/12/20 00:31:26 tom Exp $")

#if USE_REENTRANT
NCURSES_EXPORT(char *)
NCURSES_SP_NAME(longname) (NCURSES_SP_DCL0)
{
    static char empty[] =
    {'\0'};
    char *ptr;

    T((T_CALLED("longname(%p)"), (void *) SP_PARM));

    if (SP_PARM) {
	for (ptr = SP_PARM->_ttytype + strlen(SP_PARM->_ttytype);
	     ptr > SP_PARM->_ttytype;
	     ptr--)
	    if (*ptr == '|')
		returnPtr(ptr + 1);
	returnPtr(SP_PARM->_ttytype);
    }
    return empty;
}

#if NCURSES_SP_FUNCS
NCURSES_EXPORT(char *)
longname(void)
{
    return NCURSES_SP_NAME(longname) (CURRENT_SCREEN);
}
#endif

#else
NCURSES_EXPORT(char *)
longname(void)
{
    char *ptr;

    T((T_CALLED("longname()")));

    for (ptr = ttytype + strlen(ttytype);
	 ptr > ttytype;
	 ptr--)
	if (*ptr == '|')
	    returnPtr(ptr + 1);
    returnPtr(ttytype);
}
#endif
