/****************************************************************************
 * Copyright (c) 1998-2009,2011 Free Software Foundation, Inc.              *
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
 *     and: Thomas E. Dickey                        1996 on                 *
 *     and: Juergen Pfeifer                         2009                    *
 ****************************************************************************/

#include <curses.priv.h>

#ifndef CUR
#define CUR SP_TERMTYPE
#endif

MODULE_ID("$Id: lib_screen.c,v 1.41 2011/10/22 15:03:11 tom Exp $")

#define MAX_SIZE 0x3fff		/* 16k is big enough for a window or pad */

NCURSES_EXPORT(WINDOW *)
NCURSES_SP_NAME(getwin) (NCURSES_SP_DCLx FILE *filep)
{
    WINDOW tmp, *nwin;
    int n;

    T((T_CALLED("getwin(%p)"), (void *) filep));

    if (filep == 0) {
	returnWin(0);
    }
    clearerr(filep);
    if (fread(&tmp, (size_t) 1, sizeof(WINDOW), filep) < sizeof(WINDOW)
	|| ferror(filep)
	|| tmp._maxy == 0
	|| tmp._maxy > MAX_SIZE
	|| tmp._maxx == 0
	|| tmp._maxx > MAX_SIZE) {
	returnWin(0);
    }

    if (tmp._flags & _ISPAD) {
	nwin = NCURSES_SP_NAME(newpad) (NCURSES_SP_ARGx
					tmp._maxy + 1,
					tmp._maxx + 1);
    } else {
	nwin = NCURSES_SP_NAME(newwin) (NCURSES_SP_ARGx
					tmp._maxy + 1,
					tmp._maxx + 1, 0, 0);
    }

    /*
     * We deliberately do not restore the _parx, _pary, or _parent
     * fields, because the window hierarchy within which they
     * made sense is probably gone.
     */
    if (nwin != 0) {
	size_t linesize = sizeof(NCURSES_CH_T) * (size_t) (tmp._maxx + 1);

	nwin->_curx = tmp._curx;
	nwin->_cury = tmp._cury;
	nwin->_maxy = tmp._maxy;
	nwin->_maxx = tmp._maxx;
	nwin->_begy = tmp._begy;
	nwin->_begx = tmp._begx;
	nwin->_yoffset = tmp._yoffset;
	nwin->_flags = tmp._flags & ~(_SUBWIN);

	WINDOW_ATTRS(nwin) = WINDOW_ATTRS(&tmp);
	nwin->_nc_bkgd = tmp._nc_bkgd;

	nwin->_notimeout = tmp._notimeout;
	nwin->_clear = tmp._clear;
	nwin->_leaveok = tmp._leaveok;
	nwin->_idlok = tmp._idlok;
	nwin->_idcok = tmp._idcok;
	nwin->_immed = tmp._immed;
	nwin->_scroll = tmp._scroll;
	nwin->_sync = tmp._sync;
	nwin->_use_keypad = tmp._use_keypad;
	nwin->_delay = tmp._delay;

	nwin->_regtop = tmp._regtop;
	nwin->_regbottom = tmp._regbottom;

	if (tmp._flags & _ISPAD)
	    nwin->_pad = tmp._pad;

	for (n = 0; n <= nwin->_maxy; n++) {
	    clearerr(filep);
	    if (fread(nwin->_line[n].text, (size_t) 1, linesize, filep) < linesize
		|| ferror(filep)) {
		delwin(nwin);
		returnWin(0);
	    }
	}
	touchwin(nwin);
    }
    returnWin(nwin);
}

#if NCURSES_SP_FUNCS
NCURSES_EXPORT(WINDOW *)
getwin(FILE *filep)
{
    return NCURSES_SP_NAME(getwin) (CURRENT_SCREEN, filep);
}
#endif

NCURSES_EXPORT(int)
putwin(WINDOW *win, FILE *filep)
{
    int code = ERR;
    int n;

    T((T_CALLED("putwin(%p,%p)"), (void *) win, (void *) filep));

    if (win != 0) {
	size_t len = (size_t) (win->_maxx + 1);

	clearerr(filep);
	if (fwrite(win, sizeof(WINDOW), (size_t) 1, filep) != 1
	    || ferror(filep))
	      returnCode(code);

	for (n = 0; n <= win->_maxy; n++) {
	    if (fwrite(win->_line[n].text,
		       sizeof(NCURSES_CH_T), len, filep) != len
		|| ferror(filep)) {
		returnCode(code);
	    }
	}
	code = OK;
    }
    returnCode(code);
}

NCURSES_EXPORT(int)
NCURSES_SP_NAME(scr_restore) (NCURSES_SP_DCLx const char *file)
{
    FILE *fp = 0;

    T((T_CALLED("scr_restore(%p,%s)"), (void *) SP_PARM, _nc_visbuf(file)));

    if (_nc_access(file, R_OK) < 0
	|| (fp = fopen(file, "rb")) == 0) {
	returnCode(ERR);
    } else {
	delwin(NewScreen(SP_PARM));
	NewScreen(SP_PARM) = getwin(fp);
#if !USE_REENTRANT
	newscr = NewScreen(SP_PARM);
#endif
	(void) fclose(fp);
	returnCode(OK);
    }
}

#if NCURSES_SP_FUNCS
NCURSES_EXPORT(int)
scr_restore(const char *file)
{
    return NCURSES_SP_NAME(scr_restore) (CURRENT_SCREEN, file);
}
#endif

NCURSES_EXPORT(int)
scr_dump(const char *file)
{
    int result;
    FILE *fp = 0;

    T((T_CALLED("scr_dump(%s)"), _nc_visbuf(file)));

    if (_nc_access(file, W_OK) < 0
	|| (fp = fopen(file, "wb")) == 0) {
	result = ERR;
    } else {
	(void) putwin(newscr, fp);
	(void) fclose(fp);
	result = OK;
    }
    returnCode(result);
}

NCURSES_EXPORT(int)
NCURSES_SP_NAME(scr_init) (NCURSES_SP_DCLx const char *file)
{
    FILE *fp = 0;
    int code = ERR;

    T((T_CALLED("scr_init(%p,%s)"), (void *) SP_PARM, _nc_visbuf(file)));

    if (SP_PARM != 0 &&
#ifdef USE_TERM_DRIVER
	InfoOf(SP_PARM).caninit
#else
	!(exit_ca_mode && non_rev_rmcup)
#endif
	) {
	if (_nc_access(file, R_OK) >= 0
	    && (fp = fopen(file, "rb")) != 0) {
	    delwin(CurScreen(SP_PARM));
	    CurScreen(SP_PARM) = getwin(fp);
#if !USE_REENTRANT
	    curscr = CurScreen(SP_PARM);
#endif
	    (void) fclose(fp);
	    code = OK;
	}
    }
    returnCode(code);
}

#if NCURSES_SP_FUNCS
NCURSES_EXPORT(int)
scr_init(const char *file)
{
    return NCURSES_SP_NAME(scr_init) (CURRENT_SCREEN, file);
}
#endif

NCURSES_EXPORT(int)
NCURSES_SP_NAME(scr_set) (NCURSES_SP_DCLx const char *file)
{
    T((T_CALLED("scr_set(%p,%s)"), (void *) SP_PARM, _nc_visbuf(file)));

    if (NCURSES_SP_NAME(scr_init) (NCURSES_SP_ARGx file) == ERR) {
	returnCode(ERR);
    } else {
	delwin(NewScreen(SP_PARM));
	NewScreen(SP_PARM) = dupwin(curscr);
#if !USE_REENTRANT
	newscr = NewScreen(SP_PARM);
#endif
	returnCode(OK);
    }
}

#if NCURSES_SP_FUNCS
NCURSES_EXPORT(int)
scr_set(const char *file)
{
    return NCURSES_SP_NAME(scr_set) (CURRENT_SCREEN, file);
}
#endif
