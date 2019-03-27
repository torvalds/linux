/****************************************************************************
 * Copyright (c) 1998-2011,2012 Free Software Foundation, Inc.              *
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
 *     and: Juergen Pfeifer                                                 *
 ****************************************************************************/

#include <curses.priv.h>

#ifndef CUR
#define CUR SP_TERMTYPE
#endif

MODULE_ID("$Id: lib_print.c,v 1.23 2012/02/22 22:34:31 tom Exp $")

NCURSES_EXPORT(int)
NCURSES_SP_NAME(mcprint) (NCURSES_SP_DCLx char *data, int len)
/* ship binary character data to the printer via mc4/mc5/mc5p */
{
    int result;
    char *mybuf, *switchon;
    size_t onsize, offsize;
    size_t need;

    errno = 0;
    if (!HasTInfoTerminal(SP_PARM)
	|| len <= 0
	|| (!prtr_non && (!prtr_on || !prtr_off))) {
	errno = ENODEV;
	return (ERR);
    }

    if (prtr_non) {
	switchon = TPARM_1(prtr_non, len);
	onsize = strlen(switchon);
	offsize = 0;
    } else {
	switchon = prtr_on;
	onsize = strlen(prtr_on);
	offsize = strlen(prtr_off);
    }

    need = onsize + (size_t) len + offsize;

    if (switchon == 0
	|| (mybuf = typeMalloc(char, need + 1)) == 0) {
	errno = ENOMEM;
	return (ERR);
    }

    _nc_STRCPY(mybuf, switchon, need);
    memcpy(mybuf + onsize, data, (size_t) len);
    if (offsize)
	_nc_STRCPY(mybuf + onsize + len, prtr_off, need);

    /*
     * We're relying on the atomicity of UNIX writes here.  The
     * danger is that output from a refresh() might get interspersed
     * with the printer data after the write call returns but before the
     * data has actually been shipped to the terminal.  If the write(2)
     * operation is truly atomic we're protected from this.
     */
    result = (int) write(TerminalOf(SP_PARM)->Filedes, mybuf, need);

    /*
     * By giving up our scheduler slot here we increase the odds that the
     * kernel will ship the contiguous clist items from the last write
     * immediately.
     */
#ifndef __MINGW32__
    (void) sleep(0);
#endif
    free(mybuf);
    return (result);
}

#if NCURSES_SP_FUNCS && !defined(USE_TERM_DRIVER)
NCURSES_EXPORT(int)
mcprint(char *data, int len)
{
    return NCURSES_SP_NAME(mcprint) (CURRENT_SCREEN, data, len);
}
#endif
