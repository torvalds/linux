/****************************************************************************
 * Copyright (c) 1998-2012,2013 Free Software Foundation, Inc.              *
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
 *     and: Juergen Pfeifer                         1996-on                 *
 *     and: Thomas E. Dickey                                                *
 ****************************************************************************/

/*
 *	lib_slkrefr.c
 *	Write SLK window to the (virtual) screen.
 */
#include <curses.priv.h>

#ifndef CUR
#define CUR SP_TERMTYPE
#endif

MODULE_ID("$Id: lib_slkrefr.c,v 1.29 2013/01/12 17:25:48 tom Exp $")

#ifdef USE_TERM_DRIVER
#define NumLabels    InfoOf(SP_PARM).numlabels
#else
#define NumLabels    num_labels
#endif

/*
 * Paint the info line for the PC style SLK emulation.
 */
static void
slk_paint_info(WINDOW *win)
{
    SCREEN *sp = _nc_screen_of(win);

    if (win && sp && (sp->slk_format == 4)) {
	int i;

	(void) mvwhline(win, 0, 0, 0, getmaxx(win));
	wmove(win, 0, 0);

	for (i = 0; i < sp->_slk->maxlab; i++) {
	    mvwprintw(win, 0, sp->_slk->ent[i].ent_x, "F%d", i + 1);
	}
    }
}

/*
 * Write the soft labels to the soft-key window.
 */
static void
slk_intern_refresh(SCREEN *sp)
{
    int i;
    int fmt;
    SLK *slk;
    int numlab;

    if (sp == 0)
	return;

    slk = sp->_slk;
    fmt = sp->slk_format;
    numlab = NumLabels;

    if (slk->hidden)
	return;

    for (i = 0; i < slk->labcnt; i++) {
	if (slk->dirty || slk->ent[i].dirty) {
	    if (slk->ent[i].visible) {
		if (numlab > 0 && SLK_STDFMT(fmt)) {
#ifdef USE_TERM_DRIVER
		    CallDriver_2(sp, hwlabel, i + 1, slk->ent[i].form_text);
#else
		    if (i < num_labels) {
			NCURSES_PUTP2("plab_norm",
				      TPARM_2(plab_norm,
					      i + 1,
					      slk->ent[i].form_text));
		    }
#endif
		} else {
		    if (fmt == 4)
			slk_paint_info(slk->win);
		    wmove(slk->win, SLK_LINES(fmt) - 1, slk->ent[i].ent_x);
		    (void) wattrset(slk->win, (int) AttrOf(slk->attr));
		    waddstr(slk->win, slk->ent[i].form_text);
		    /* if we simulate SLK's, it's looking much more
		       natural to use the current ATTRIBUTE also
		       for the label window */
		    (void) wattrset(slk->win, (int) WINDOW_ATTRS(StdScreen(sp)));
		}
	    }
	    slk->ent[i].dirty = FALSE;
	}
    }
    slk->dirty = FALSE;

    if (numlab > 0) {
#ifdef USE_TERM_DRIVER
	CallDriver_1(sp, hwlabelOnOff, slk->hidden ? FALSE : TRUE);
#else
	if (slk->hidden) {
	    NCURSES_PUTP2("label_off", label_off);
	} else {
	    NCURSES_PUTP2("label_on", label_on);
	}
#endif
    }
}

/*
 * Refresh the soft labels.
 */
NCURSES_EXPORT(int)
NCURSES_SP_NAME(slk_noutrefresh) (NCURSES_SP_DCL0)
{
    T((T_CALLED("slk_noutrefresh(%p)"), (void *) SP_PARM));

    if (SP_PARM == 0 || SP_PARM->_slk == 0)
	returnCode(ERR);
    if (SP_PARM->_slk->hidden)
	returnCode(OK);
    slk_intern_refresh(SP_PARM);

    returnCode(wnoutrefresh(SP_PARM->_slk->win));
}

#if NCURSES_SP_FUNCS
NCURSES_EXPORT(int)
slk_noutrefresh(void)
{
    return NCURSES_SP_NAME(slk_noutrefresh) (CURRENT_SCREEN);
}
#endif

/*
 * Refresh the soft labels.
 */
NCURSES_EXPORT(int)
NCURSES_SP_NAME(slk_refresh) (NCURSES_SP_DCL0)
{
    T((T_CALLED("slk_refresh(%p)"), (void *) SP_PARM));

    if (SP_PARM == 0 || SP_PARM->_slk == 0)
	returnCode(ERR);
    if (SP_PARM->_slk->hidden)
	returnCode(OK);
    slk_intern_refresh(SP_PARM);

    returnCode(wrefresh(SP_PARM->_slk->win));
}

#if NCURSES_SP_FUNCS
NCURSES_EXPORT(int)
slk_refresh(void)
{
    return NCURSES_SP_NAME(slk_refresh) (CURRENT_SCREEN);
}
#endif
