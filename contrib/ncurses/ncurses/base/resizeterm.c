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
 *  Author: Thomas E. Dickey                                                *
 *     and: Juergen Pfeifer                                                 *
 ****************************************************************************/

/*
 * This is an extension to the curses library.  It provides callers with a hook
 * into the NCURSES data to resize windows, primarily for use by programs
 * running in an X Window terminal (e.g., xterm).  I abstracted this module
 * from my application library for NCURSES because it must be compiled with
 * the private data structures -- T.Dickey 1995/7/4.
 */

#include <curses.priv.h>

#ifndef CUR
#define CUR SP_TERMTYPE
#endif

MODULE_ID("$Id: resizeterm.c,v 1.45 2012/07/07 17:07:23 tom Exp $")

/*
 * If we're trying to be reentrant, do not want any local statics.
 */
#if USE_REENTRANT
#define EXTRA_ARGS ,     CurLines,     CurCols
#define EXTRA_DCLS , int CurLines, int CurCols
#else
static int current_lines;
static int current_cols;
#define CurLines current_lines
#define CurCols  current_cols
#define EXTRA_ARGS		/* nothing */
#define EXTRA_DCLS		/* nothing */
#endif

#ifdef TRACE
static void
show_window_sizes(const char *name)
{
    SCREEN *sp;
    WINDOWLIST *wp;

    _nc_lock_global(curses);
    for (each_screen(sp)) {
	_tracef("%s resizing: %p: %2d x %2d (%2d x %2d)", name, (void *) sp,
		*(ptrLines(sp)),
		*(ptrCols(sp)),
		screen_lines(sp), screen_columns(sp));
	for (each_window(sp, wp)) {
	    _tracef("  window %p is %2ld x %2ld at %2ld,%2ld",
		    (void *) &(wp->win),
		    (long) wp->win._maxy + 1,
		    (long) wp->win._maxx + 1,
		    (long) wp->win._begy,
		    (long) wp->win._begx);
	}
    }
    _nc_unlock_global(curses);
}
#endif

/*
 * Return true if the given dimensions do not match the internal terminal
 * structure's size.
 */
NCURSES_EXPORT(bool)
NCURSES_SP_NAME(is_term_resized) (NCURSES_SP_DCLx int ToLines, int ToCols)
{
    T((T_CALLED("is_term_resized(%p, %d, %d)"), (void *) SP_PARM, ToLines, ToCols));
    returnCode(ToLines > 0
	       && ToCols > 0
	       && (ToLines != screen_lines(SP_PARM)
		   || ToCols != screen_columns(SP_PARM)));
}

#if NCURSES_SP_FUNCS
NCURSES_EXPORT(bool)
is_term_resized(int ToLines, int ToCols)
{
    return NCURSES_SP_NAME(is_term_resized) (CURRENT_SCREEN, ToLines, ToCols);
}
#endif

/*
 */
static ripoff_t *
ripped_window(WINDOW *win)
{
    ripoff_t *result = 0;
    ripoff_t *rop;

    if (win != 0) {
#ifdef USE_SP_RIPOFF
	SCREEN *sp = _nc_screen_of(win);
#endif
	for (each_ripoff(rop)) {
	    if (rop->win == win && rop->line != 0) {
		result = rop;
		break;
	    }
	}
    }
    return result;
}

/*
 * Returns the number of lines from the bottom for the beginning of a ripped
 * off window.
 */
static int
ripped_bottom(WINDOW *win)
{
    int result = 0;
    ripoff_t *rop;

    if (win != 0) {
#ifdef USE_SP_RIPOFF
	SCREEN *sp = _nc_screen_of(win);
#endif
	for (each_ripoff(rop)) {
	    if (rop->line < 0) {
		result -= rop->line;
		if (rop->win == win) {
		    break;
		}
	    }
	}
    }
    return result;
}

/*
 * Return the number of levels of child-windows under the current window.
 */
static int
child_depth(WINDOW *cmp)
{
    int depth = 0;

    if (cmp != 0) {
#ifdef USE_SP_WINDOWLIST
	SCREEN *sp = _nc_screen_of(cmp);
#endif
	WINDOWLIST *wp;

	for (each_window(sp, wp)) {
	    WINDOW *tst = &(wp->win);
	    if (tst->_parent == cmp) {
		depth = 1 + child_depth(tst);
		break;
	    }
	}
    }
    return depth;
}

/*
 * Return the number of levels of parent-windows above the current window.
 */
static int
parent_depth(WINDOW *cmp)
{
    int depth = 0;

    if (cmp != 0) {
	WINDOW *tst;
	while ((tst = cmp->_parent) != 0) {
	    ++depth;
	    cmp = tst;
	}
    }
    return depth;
}

/*
 * FIXME: must adjust position so it's within the parent!
 */
static int
adjust_window(WINDOW *win, int ToLines, int ToCols, int stolen EXTRA_DCLS)
{
    int result;
    int bottom = CurLines + _nc_screen_of(win)->_topstolen - stolen;
    int myLines = win->_maxy + 1;
    int myCols = win->_maxx + 1;
    ripoff_t *rop = ripped_window(win);

    T((T_CALLED("adjust_window(%p,%d,%d)%s depth %d/%d currently %ldx%ld at %ld,%ld"),
       (void *) win, ToLines, ToCols,
       (rop != 0) ? " (rip)" : "",
       parent_depth(win),
       child_depth(win),
       (long) getmaxy(win), (long) getmaxx(win),
       (long) getbegy(win) + win->_yoffset, (long) getbegx(win)));

    if (rop != 0 && rop->line < 0) {
	/*
	 * If it is a ripped-off window at the bottom of the screen, simply
	 * move it to the same relative position.
	 */
	win->_begy = (NCURSES_SIZE_T) (ToLines - ripped_bottom(win) - 0 - win->_yoffset);
	if (rop->hook == _nc_slk_initialize)
	    _nc_format_slks(
#if NCURSES_SP_FUNCS
			       _nc_screen_of(win),
#endif
			       ToCols);
    } else if (win->_begy >= bottom) {
	/*
	 * If it is below the bottom of the new screen, move up by the same
	 * amount that the screen shrank.
	 */
	win->_begy = (NCURSES_SIZE_T) (win->_begy + (ToLines - CurLines));
    } else {
	if (myLines == (CurLines - stolen)
	    && ToLines != CurLines) {
	    myLines = ToLines - stolen;
	} else if (myLines == CurLines
		   && ToLines != CurLines) {
	    myLines = ToLines;
	}
    }

    if (myLines > ToLines) {
	myLines = ToLines;
    }

    if (myCols > ToCols)
	myCols = ToCols;

    if (myCols == CurCols
	&& ToCols != CurCols)
	myCols = ToCols;

    result = wresize(win, myLines, myCols);
    returnCode(result);
}

/*
 * If we're decreasing size, recursively search for windows that have no
 * children, decrease those to fit, then decrease the containing window, etc.
 */
static int
decrease_size(NCURSES_SP_DCLx int ToLines, int ToCols, int stolen EXTRA_DCLS)
{
    bool found;
    int depth = 0;
    WINDOWLIST *wp;

    T((T_CALLED("decrease_size(%p, %d, %d)"), (void *) SP_PARM, ToLines, ToCols));

    do {
	found = FALSE;
	TR(TRACE_UPDATE, ("decreasing size of windows to %dx%d, depth=%d",
			  ToLines, ToCols, depth));
	for (each_window(SP_PARM, wp)) {
	    WINDOW *win = &(wp->win);

	    if (!(win->_flags & _ISPAD)) {
		if (child_depth(win) == depth) {
		    found = TRUE;
		    if (adjust_window(win, ToLines, ToCols,
				      stolen EXTRA_ARGS) != OK)
			returnCode(ERR);
		}
	    }
	}
	++depth;
    } while (found);
    returnCode(OK);
}

/*
 * If we're increasing size, recursively search for windows that have no
 * parent, increase those to fit, then increase the contained window, etc.
 */
static int
increase_size(NCURSES_SP_DCLx int ToLines, int ToCols, int stolen EXTRA_DCLS)
{
    bool found;
    int depth = 0;
    WINDOWLIST *wp;

    T((T_CALLED("increase_size(%p, %d, %d)"), (void *) SP_PARM, ToLines, ToCols));

    do {
	found = FALSE;
	TR(TRACE_UPDATE, ("increasing size of windows to %dx%d, depth=%d",
			  ToLines, ToCols, depth));
	for (each_window(SP_PARM, wp)) {
	    WINDOW *win = &(wp->win);

	    if (!(win->_flags & _ISPAD)) {
		if (parent_depth(win) == depth) {
		    found = TRUE;
		    if (adjust_window(win, ToLines, ToCols,
				      stolen EXTRA_ARGS) != OK)
			returnCode(ERR);
		}
	    }
	}
	++depth;
    } while (found);
    returnCode(OK);
}

/*
 * This function reallocates NCURSES window structures, with no side-effects
 * such as ungetch().
 */
NCURSES_EXPORT(int)
NCURSES_SP_NAME(resize_term) (NCURSES_SP_DCLx int ToLines, int ToCols)
{
    int result = OK EXTRA_ARGS;
    int was_stolen;

    T((T_CALLED("resize_term(%p,%d,%d) old(%d,%d)"),
       (void *) SP_PARM, ToLines, ToCols,
       (SP_PARM == 0) ? -1 : screen_lines(SP_PARM),
       (SP_PARM == 0) ? -1 : screen_columns(SP_PARM)));

    if (SP_PARM == 0) {
	returnCode(ERR);
    }

    _nc_nonsp_lock_global(curses);

    was_stolen = (screen_lines(SP_PARM) - SP_PARM->_lines_avail);
    if (NCURSES_SP_NAME(is_term_resized) (NCURSES_SP_ARGx ToLines, ToCols)) {
	int myLines = CurLines = screen_lines(SP_PARM);
	int myCols = CurCols = screen_columns(SP_PARM);

#ifdef TRACE
	if (USE_TRACEF(TRACE_UPDATE)) {
	    show_window_sizes("before");
	    _nc_unlock_global(tracef);
	}
#endif
	if (ToLines > screen_lines(SP_PARM)) {
	    result = increase_size(NCURSES_SP_ARGx
				   myLines = ToLines,
				   myCols,
				   was_stolen EXTRA_ARGS);
	    CurLines = myLines;
	    CurCols = myCols;
	}

	if ((result == OK)
	    && (ToCols > screen_columns(SP_PARM))) {
	    result = increase_size(NCURSES_SP_ARGx
				   myLines,
				   myCols = ToCols,
				   was_stolen EXTRA_ARGS);
	    CurLines = myLines;
	    CurCols = myCols;
	}

	if ((result == OK)
	    && (ToLines < myLines ||
		ToCols < myCols)) {
	    result = decrease_size(NCURSES_SP_ARGx
				   ToLines,
				   ToCols,
				   was_stolen EXTRA_ARGS);
	}

	if (result == OK) {
	    screen_lines(SP_PARM) = (NCURSES_SIZE_T) ToLines;
	    screen_columns(SP_PARM) = (NCURSES_SIZE_T) ToCols;

#ifdef USE_TERM_DRIVER
	    CallDriver_2(SP_PARM, setsize, ToLines, ToCols);
#else
	    lines = (NCURSES_SIZE_T) ToLines;
	    columns = (NCURSES_SIZE_T) ToCols;
#endif

	    SP_PARM->_lines_avail = (NCURSES_SIZE_T) (ToLines - was_stolen);

	    if (SP_PARM->oldhash) {
		FreeAndNull(SP_PARM->oldhash);
	    }
	    if (SP_PARM->newhash) {
		FreeAndNull(SP_PARM->newhash);
	    }
#ifdef TRACE
	    if (USE_TRACEF(TRACE_UPDATE)) {
		SET_LINES(ToLines - was_stolen);
		SET_COLS(ToCols);
		show_window_sizes("after");
		_nc_unlock_global(tracef);
	    }
#endif
	}
    }

    if (result == OK) {
	/*
	 * Always update LINES, to allow for call from lib_doupdate.c which
	 * needs to have the count adjusted by the stolen (ripped off) lines.
	 */
	SET_LINES(ToLines - was_stolen);
	SET_COLS(ToCols);
    }

    _nc_nonsp_unlock_global(curses);

    returnCode(result);
}

#if NCURSES_SP_FUNCS
NCURSES_EXPORT(int)
resize_term(int ToLines, int ToCols)
{
    int res = ERR;
    _nc_sp_lock_global(curses);
    res = NCURSES_SP_NAME(resize_term) (CURRENT_SCREEN, ToLines, ToCols);
    _nc_sp_unlock_global(curses);
    return (res);
}
#endif

/*
 * This function reallocates NCURSES window structures.  It is invoked in
 * response to a SIGWINCH interrupt.  Other user-defined windows may also need
 * to be reallocated.
 *
 * Because this performs memory allocation, it should not (in general) be
 * invoked directly from the signal handler.
 */
NCURSES_EXPORT(int)
NCURSES_SP_NAME(resizeterm) (NCURSES_SP_DCLx int ToLines, int ToCols)
{
    int result = ERR;

    T((T_CALLED("resizeterm(%p, %d,%d) old(%d,%d)"),
       (void *) SP_PARM, ToLines, ToCols,
       (SP_PARM == 0) ? -1 : screen_lines(SP_PARM),
       (SP_PARM == 0) ? -1 : screen_columns(SP_PARM)));

    if (SP_PARM != 0) {
	result = OK;
	SP_PARM->_sig_winch = FALSE;

	if (NCURSES_SP_NAME(is_term_resized) (NCURSES_SP_ARGx ToLines, ToCols)) {
#if USE_SIGWINCH
	    ripoff_t *rop;
	    bool slk_visible = (SP_PARM != 0
				&& SP_PARM->_slk != 0
				&& !(SP_PARM->_slk->hidden));

	    if (slk_visible) {
		slk_clear();
	    }
#endif
	    result = NCURSES_SP_NAME(resize_term) (NCURSES_SP_ARGx ToLines, ToCols);

#if USE_SIGWINCH
	    clearok(CurScreen(SP_PARM), TRUE);	/* screen contents are unknown */

	    /* ripped-off lines are a special case: if we did not lengthen
	     * them, we haven't moved them either.  repaint them, too.
	     *
	     * for the rest - stdscr and other windows - the client has to
	     * decide which to repaint, since without panels, ncurses does
	     * not know which are really on top.
	     */
	    for (each_ripoff(rop)) {
		if (rop->win != StdScreen(SP_PARM)
		    && rop->win != 0
		    && rop->line < 0) {

		    if (rop->hook != _nc_slk_initialize) {
			touchwin(rop->win);
			wnoutrefresh(rop->win);
		    }
		}
	    }

	    /* soft-keys are a special case: we _know_ how to repaint them */
	    if (slk_visible) {
		NCURSES_SP_NAME(slk_restore) (NCURSES_SP_ARG);
		NCURSES_SP_NAME(slk_touch) (NCURSES_SP_ARG);
		NCURSES_SP_NAME(slk_refresh) (NCURSES_SP_ARG);
	    }
#endif
	}
#if USE_SIGWINCH
	safe_ungetch(SP_PARM, KEY_RESIZE);	/* so application can know this */
#endif
    }

    returnCode(result);
}

#if NCURSES_SP_FUNCS
NCURSES_EXPORT(int)
resizeterm(int ToLines, int ToCols)
{
    return NCURSES_SP_NAME(resizeterm) (CURRENT_SCREEN, ToLines, ToCols);
}
#endif
