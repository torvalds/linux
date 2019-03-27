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
 *  Author: Thomas E. Dickey                    1996-on                     *
 ****************************************************************************/

#include <curses.priv.h>
#include <tic.h>

#if HAVE_NC_FREEALL

#if HAVE_LIBDBMALLOC
extern int malloc_errfd;	/* FIXME */
#endif

MODULE_ID("$Id: lib_freeall.c,v 1.62 2012/11/17 23:53:03 tom Exp $")

/*
 * Free all ncurses data.  This is used for testing only (there's no practical
 * use for it as an extension).
 */
NCURSES_EXPORT(void)
NCURSES_SP_NAME(_nc_freeall) (NCURSES_SP_DCL0)
{
    WINDOWLIST *p, *q;
    static va_list empty_va;

    T((T_CALLED("_nc_freeall()")));
#if NO_LEAKS
    if (SP_PARM != 0) {
	if (SP_PARM->_oldnum_list != 0) {
	    FreeAndNull(SP_PARM->_oldnum_list);
	}
	if (SP_PARM->_panelHook.destroy != 0) {
	    SP_PARM->_panelHook.destroy(SP_PARM->_panelHook.stdscr_pseudo_panel);
	}
    }
#endif
    if (SP_PARM != 0) {
	_nc_lock_global(curses);

	while (WindowList(SP_PARM) != 0) {
	    bool deleted = FALSE;

	    /* Delete only windows that're not a parent */
	    for (each_window(SP_PARM, p)) {
		WINDOW *p_win = &(p->win);
		bool found = FALSE;

		for (each_window(SP_PARM, q)) {
		    WINDOW *q_win = &(q->win);
		    if ((p != q)
			&& (q_win->_flags & _SUBWIN)
			&& (p_win == q_win->_parent)) {
			found = TRUE;
			break;
		    }
		}

		if (!found) {
		    if (delwin(p_win) != ERR)
			deleted = TRUE;
		    break;
		}
	    }

	    /*
	     * Don't continue to loop if the list is trashed.
	     */
	    if (!deleted)
		break;
	}
	delscreen(SP_PARM);
	_nc_unlock_global(curses);
    }

    (void) _nc_printf_string(0, empty_va);
#ifdef TRACE
    (void) _nc_trace_buf(-1, (size_t) 0);
#endif
#if USE_WIDEC_SUPPORT
    FreeIfNeeded(_nc_wacs);
#endif
    _nc_leaks_tinfo();

#if HAVE_LIBDBMALLOC
    malloc_dump(malloc_errfd);
#elif HAVE_LIBDMALLOC
#elif HAVE_LIBMPATROL
    __mp_summary();
#elif HAVE_PURIFY
    purify_all_inuse();
#endif
    returnVoid;
}

#if NCURSES_SP_FUNCS
NCURSES_EXPORT(void)
_nc_freeall(void)
{
    NCURSES_SP_NAME(_nc_freeall) (CURRENT_SCREEN);
}
#endif

NCURSES_EXPORT(void)
NCURSES_SP_NAME(_nc_free_and_exit) (NCURSES_SP_DCLx int code)
{
    char *last_buffer = (SP_PARM != 0) ? SP_PARM->out_buffer : 0;

    NCURSES_SP_NAME(_nc_flush) (NCURSES_SP_ARG);
    NCURSES_SP_NAME(_nc_freeall) (NCURSES_SP_ARG);
#ifdef TRACE
    trace(0);			/* close trace file, freeing its setbuf */
    {
	static va_list fake;
	free(_nc_varargs("?", fake));
    }
#endif
    FreeIfNeeded(last_buffer);
    exit(code);
}

#else
NCURSES_EXPORT(void)
_nc_freeall(void)
{
}

NCURSES_EXPORT(void)
NCURSES_SP_NAME(_nc_free_and_exit) (NCURSES_SP_DCLx int code)
{
    if (SP_PARM) {
	delscreen(SP_PARM);
	if (SP_PARM->_term)
	    NCURSES_SP_NAME(del_curterm) (NCURSES_SP_ARGx SP_PARM->_term);
    }
    exit(code);
}
#endif

#if NCURSES_SP_FUNCS
NCURSES_EXPORT(void)
_nc_free_and_exit(int code)
{
    NCURSES_SP_NAME(_nc_free_and_exit) (CURRENT_SCREEN, code);
}
#endif
