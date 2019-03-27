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
 *     and: Thomas E. Dickey                        1996-on                 *
 *     and: Juergen Pfeifer                                                 *
 *                                                                          *
 * some of the code in here was contributed by:                             *
 * Magnus Bengtsson, d6mbeng@dtek.chalmers.se (Nov'93)                      *
 * (but it has changed a lot)                                               *
 ****************************************************************************/

/* $FreeBSD$ */

#define __INTERNAL_CAPS_VISIBLE
#include <curses.priv.h>

#include <termcap.h>
#include <tic.h>
#include <ctype.h>

#ifndef CUR
#define CUR SP_TERMTYPE
#endif

MODULE_ID("$Id: lib_termcap.c,v 1.80 2013/06/08 16:48:47 tom Exp $")

NCURSES_EXPORT_VAR(char *) UP = 0;
NCURSES_EXPORT_VAR(char *) BC = 0;

#ifdef FREEBSD_NATIVE
extern char _nc_termcap[];	/* buffer to copy out */
#endif

#define MyCache  _nc_globals.tgetent_cache
#define CacheInx _nc_globals.tgetent_index
#define CacheSeq _nc_globals.tgetent_sequence

#define FIX_SGR0 MyCache[CacheInx].fix_sgr0
#define LAST_TRM MyCache[CacheInx].last_term
#define LAST_BUF MyCache[CacheInx].last_bufp
#define LAST_USE MyCache[CacheInx].last_used
#define LAST_SEQ MyCache[CacheInx].sequence

/*
 * Termcap names are matched only using the first two bytes.
 * Ignore any extended names longer than two bytes, to avoid problems
 * with legacy code which passes in parameters whose use is long forgotten.
 */
#define ValidCap(cap) (((cap)[0] != '\0') && ((cap)[1] != '\0'))
#define SameCap(a,b)  (((a)[0] == (b)[0]) && ((a)[1] == (b)[1]))
#define ValidExt(ext) (ValidCap(ext) && (ext)[2] == '\0')

/***************************************************************************
 *
 * tgetent(bufp, term)
 *
 * In termcap, this function reads in the entry for terminal `term' into the
 * buffer pointed to by bufp. It must be called before any of the functions
 * below are called.
 * In this terminfo emulation, tgetent() simply calls setupterm() (which
 * does a bit more than tgetent() in termcap does), and returns its return
 * value (1 if successful, 0 if no terminal with the given name could be
 * found, or -1 if no terminal descriptions have been installed on the
 * system).  The bufp argument is ignored.
 *
 ***************************************************************************/

NCURSES_EXPORT(int)
NCURSES_SP_NAME(tgetent) (NCURSES_SP_DCLx char *bufp, const char *name)
{
    int rc = ERR;
    int n;
    bool found_cache = FALSE;
#ifdef USE_TERM_DRIVER
    TERMINAL *termp = 0;
#endif

    START_TRACE();
    T((T_CALLED("tgetent()")));

    TINFO_SETUP_TERM(&termp, (NCURSES_CONST char *) name,
		     STDOUT_FILENO, &rc, TRUE);

#ifdef USE_TERM_DRIVER
    if (termp == 0 ||
	!((TERMINAL_CONTROL_BLOCK *) termp)->drv->isTerminfo)
	returnCode(rc);
#endif

    /*
     * In general we cannot tell if the fixed sgr0 is still used by the
     * caller, but if tgetent() is called with the same buffer, that is
     * good enough, since the previous data would be invalidated by the
     * current call.
     *
     * bufp may be a null pointer, e.g., GNU termcap.  That allocates data,
     * which is good until the next tgetent() call.  The conventional termcap
     * is inconvenient because of the fixed buffer size, but because it uses
     * caller-supplied buffers, can have multiple terminal descriptions in
     * use at a given time.
     */
    for (n = 0; n < TGETENT_MAX; ++n) {
	bool same_result = (MyCache[n].last_used && MyCache[n].last_bufp == bufp);
	if (same_result) {
	    CacheInx = n;
	    if (FIX_SGR0 != 0) {
		FreeAndNull(FIX_SGR0);
	    }
	    /*
	     * Also free the terminfo data that we loaded (much bigger leak).
	     */
	    if (LAST_TRM != 0 && LAST_TRM != TerminalOf(SP_PARM)) {
		TERMINAL *trm = LAST_TRM;
		NCURSES_SP_NAME(del_curterm) (NCURSES_SP_ARGx LAST_TRM);
		for (CacheInx = 0; CacheInx < TGETENT_MAX; ++CacheInx)
		    if (LAST_TRM == trm)
			LAST_TRM = 0;
		CacheInx = n;
	    }
	    found_cache = TRUE;
	    break;
	}
    }
    if (!found_cache) {
	int best = 0;

	for (CacheInx = 0; CacheInx < TGETENT_MAX; ++CacheInx) {
	    if (LAST_SEQ < MyCache[best].sequence) {
		best = CacheInx;
	    }
	}
	CacheInx = best;
    }
    LAST_TRM = TerminalOf(SP_PARM);
    LAST_SEQ = ++CacheSeq;

    PC = 0;
    UP = 0;
    BC = 0;
    FIX_SGR0 = 0;		/* don't free it - application may still use */

    if (rc == 1) {

	if (cursor_left)
	    if ((backspaces_with_bs = (char) !strcmp(cursor_left, "\b")) == 0)
		backspace_if_not_bs = cursor_left;

	/* we're required to export these */
	if (pad_char != NULL)
	    PC = pad_char[0];
	if (cursor_up != NULL)
	    UP = cursor_up;
	if (backspace_if_not_bs != NULL)
	    BC = backspace_if_not_bs;

	if ((FIX_SGR0 = _nc_trim_sgr0(&(TerminalOf(SP_PARM)->type))) != 0) {
	    if (!strcmp(FIX_SGR0, exit_attribute_mode)) {
		if (FIX_SGR0 != exit_attribute_mode) {
		    free(FIX_SGR0);
		}
		FIX_SGR0 = 0;
	    }
	}
	LAST_BUF = bufp;
	LAST_USE = TRUE;

	SetNoPadding(SP_PARM);
	(void) NCURSES_SP_NAME(baudrate) (NCURSES_SP_ARG);	/* sets ospeed as a side-effect */

/* LINT_PREPRO
#if 0*/
#include <capdefaults.c>
/* LINT_PREPRO
#endif*/

    }

#ifdef FREEBSD_NATIVE
    /*
     * This is a REALLY UGLY hack. Basically, if we originate with
     * a termcap source, try and copy it out.
     */
    if (bufp && _nc_termcap[0])
	strncpy(bufp, _nc_termcap, 1024);
#endif

    returnCode(rc);
}

#if NCURSES_SP_FUNCS
NCURSES_EXPORT(int)
tgetent(char *bufp, const char *name)
{
    return NCURSES_SP_NAME(tgetent) (CURRENT_SCREEN, bufp, name);
}
#endif

#if 0
static bool
same_tcname(const char *a, const char *b)
{
    bool code = SameCap(a, b);
    fprintf(stderr, "compare(%s,%s) %s\n", a, b, code ? "same" : "diff");
    return code;
}

#else
#define same_tcname(a,b) SameCap(a,b)
#endif

/***************************************************************************
 *
 * tgetflag(str)
 *
 * Look up boolean termcap capability str and return its value (TRUE=1 if
 * present, FALSE=0 if not).
 *
 ***************************************************************************/

NCURSES_EXPORT(int)
NCURSES_SP_NAME(tgetflag) (NCURSES_SP_DCLx NCURSES_CONST char *id)
{
    int result = 0;		/* Solaris returns zero for missing flag */
    int j = -1;

    T((T_CALLED("tgetflag(%p, %s)"), (void *) SP_PARM, id));
    if (HasTInfoTerminal(SP_PARM) && ValidCap(id)) {
	TERMTYPE *tp = &(TerminalOf(SP_PARM)->type);
	struct name_table_entry const *entry_ptr;

	entry_ptr = _nc_find_type_entry(id, BOOLEAN, TRUE);
	if (entry_ptr != 0) {
	    j = entry_ptr->nte_index;
	}
#if NCURSES_XNAMES
	else {
	    int i;
	    for_each_ext_boolean(i, tp) {
		const char *capname = ExtBoolname(tp, i, boolcodes);
		if (same_tcname(id, capname) && ValidExt(capname)) {
		    j = i;
		    break;
		}
	    }
	}
#endif
	if (j >= 0) {
	    /* note: setupterm forces invalid booleans to false */
	    result = tp->Booleans[j];
	}
    }
    returnCode(result);
}

#if NCURSES_SP_FUNCS
NCURSES_EXPORT(int)
tgetflag(NCURSES_CONST char *id)
{
    return NCURSES_SP_NAME(tgetflag) (CURRENT_SCREEN, id);
}
#endif

/***************************************************************************
 *
 * tgetnum(str)
 *
 * Look up numeric termcap capability str and return its value, or -1 if
 * not given.
 *
 ***************************************************************************/

NCURSES_EXPORT(int)
NCURSES_SP_NAME(tgetnum) (NCURSES_SP_DCLx NCURSES_CONST char *id)
{
    int result = ABSENT_NUMERIC;
    int j = -1;

    T((T_CALLED("tgetnum(%p, %s)"), (void *) SP_PARM, id));
    if (HasTInfoTerminal(SP_PARM) && ValidCap(id)) {
	TERMTYPE *tp = &(TerminalOf(SP_PARM)->type);
	struct name_table_entry const *entry_ptr;

	entry_ptr = _nc_find_type_entry(id, NUMBER, TRUE);
	if (entry_ptr != 0) {
	    j = entry_ptr->nte_index;
	}
#if NCURSES_XNAMES
	else {
	    int i;
	    for_each_ext_number(i, tp) {
		const char *capname = ExtNumname(tp, i, numcodes);
		if (same_tcname(id, capname) && ValidExt(capname)) {
		    j = i;
		    break;
		}
	    }
	}
#endif
	if (j >= 0) {
	    if (VALID_NUMERIC(tp->Numbers[j]))
		result = tp->Numbers[j];
	}
    }
    returnCode(result);
}

#if NCURSES_SP_FUNCS
NCURSES_EXPORT(int)
tgetnum(NCURSES_CONST char *id)
{
    return NCURSES_SP_NAME(tgetnum) (CURRENT_SCREEN, id);
}
#endif

/***************************************************************************
 *
 * tgetstr(str, area)
 *
 * Look up string termcap capability str and return a pointer to its value,
 * or NULL if not given.
 *
 ***************************************************************************/

NCURSES_EXPORT(char *)
NCURSES_SP_NAME(tgetstr) (NCURSES_SP_DCLx NCURSES_CONST char *id, char **area)
{
    char *result = NULL;
    int j = -1;

    T((T_CALLED("tgetstr(%s,%p)"), id, (void *) area));
    if (HasTInfoTerminal(SP_PARM) && ValidCap(id)) {
	TERMTYPE *tp = &(TerminalOf(SP_PARM)->type);
	struct name_table_entry const *entry_ptr;

	entry_ptr = _nc_find_type_entry(id, STRING, TRUE);
	if (entry_ptr != 0) {
	    j = entry_ptr->nte_index;
	}
#if NCURSES_XNAMES
	else {
	    int i;
	    for_each_ext_string(i, tp) {
		const char *capname = ExtStrname(tp, i, strcodes);
		if (same_tcname(id, capname) && ValidExt(capname)) {
		    j = i;
		    break;
		}
	    }
	}
#endif
	if (j >= 0) {
	    result = tp->Strings[j];
	    TR(TRACE_DATABASE, ("found match %d: %s", j, _nc_visbuf(result)));
	    /* setupterm forces canceled strings to null */
	    if (VALID_STRING(result)) {
		if (result == exit_attribute_mode
		    && FIX_SGR0 != 0) {
		    result = FIX_SGR0;
		    TR(TRACE_DATABASE, ("altered to : %s", _nc_visbuf(result)));
		}
		if (area != 0
		    && *area != 0) {
		    _nc_STRCPY(*area, result, 1024);
		    result = *area;
		    *area += strlen(*area) + 1;
		}
	    }
	}
    }
    returnPtr(result);
}

#if NCURSES_SP_FUNCS
NCURSES_EXPORT(char *)
tgetstr(NCURSES_CONST char *id, char **area)
{
    return NCURSES_SP_NAME(tgetstr) (CURRENT_SCREEN, id, area);
}
#endif

#if NO_LEAKS
NCURSES_EXPORT(void)
_nc_tgetent_leaks(void)
{
    for (CacheInx = 0; CacheInx < TGETENT_MAX; ++CacheInx) {
	FreeIfNeeded(FIX_SGR0);
	if (LAST_TRM != 0)
	    del_curterm(LAST_TRM);
    }
}
#endif
