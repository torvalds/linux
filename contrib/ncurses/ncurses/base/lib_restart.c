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
 *     and: Juergen Pfeifer                         2008                    *
 ****************************************************************************/

/*
 * Terminfo-only terminal setup routines:
 *
 *		int restartterm(const char *, int, int *)
 */

#include <curses.priv.h>

MODULE_ID("$Id: lib_restart.c,v 1.15 2012/12/08 20:40:06 tom Exp $")

NCURSES_EXPORT(int)
NCURSES_SP_NAME(restartterm) (NCURSES_SP_DCLx
			      NCURSES_CONST char *termp,
			      int filenum,
			      int *errret)
{
    int result;
#ifdef USE_TERM_DRIVER
    TERMINAL *new_term = 0;
#endif

    T((T_CALLED("restartterm(%p,%s,%d,%p)"),
       (void *) SP_PARM,
       termp,
       filenum,
       (void *) errret));

    if (TINFO_SETUP_TERM(&new_term, termp, filenum, errret, FALSE) != OK) {
	result = ERR;
    } else if (SP_PARM != 0) {
	int saveecho = SP_PARM->_echo;
	int savecbreak = SP_PARM->_cbreak;
	int saveraw = SP_PARM->_raw;
	int savenl = SP_PARM->_nl;

#ifdef USE_TERM_DRIVER
	SP_PARM->_term = new_term;
#endif
	if (saveecho) {
	    NCURSES_SP_NAME(echo) (NCURSES_SP_ARG);
	} else {
	    NCURSES_SP_NAME(noecho) (NCURSES_SP_ARG);
	}

	if (savecbreak) {
	    NCURSES_SP_NAME(cbreak) (NCURSES_SP_ARG);
	    NCURSES_SP_NAME(noraw) (NCURSES_SP_ARG);
	} else if (saveraw) {
	    NCURSES_SP_NAME(nocbreak) (NCURSES_SP_ARG);
	    NCURSES_SP_NAME(raw) (NCURSES_SP_ARG);
	} else {
	    NCURSES_SP_NAME(nocbreak) (NCURSES_SP_ARG);
	    NCURSES_SP_NAME(noraw) (NCURSES_SP_ARG);
	}
	if (savenl) {
	    NCURSES_SP_NAME(nl) (NCURSES_SP_ARG);
	} else {
	    NCURSES_SP_NAME(nonl) (NCURSES_SP_ARG);
	}

	NCURSES_SP_NAME(reset_prog_mode) (NCURSES_SP_ARG);

#if USE_SIZECHANGE
	_nc_update_screensize(SP_PARM);
#endif

	result = OK;
    } else {
	result = ERR;
    }
    returnCode(result);
}

#if NCURSES_SP_FUNCS
NCURSES_EXPORT(int)
restartterm(NCURSES_CONST char *termp, int filenum, int *errret)
{
    return NCURSES_SP_NAME(restartterm) (CURRENT_SCREEN, termp, filenum, errret);
}
#endif
