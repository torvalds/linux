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
 *     and: Juergen Pfeifer                         2009                    *
 ****************************************************************************/

/*
 * Terminal setup routines common to termcap and terminfo:
 *
 *		use_env(bool)
 *		use_tioctl(bool)
 *		setupterm(char *, int, int *)
 */

#include <curses.priv.h>
#include <tic.h>		/* for MAX_NAME_SIZE */

#if HAVE_LOCALE_H
#include <locale.h>
#endif

MODULE_ID("$Id: lib_setup.c,v 1.158 2013/06/22 19:59:08 tom Exp $")

/****************************************************************************
 *
 * Terminal size computation
 *
 ****************************************************************************/

#if HAVE_SIZECHANGE
# if !defined(sun) || !TERMIOS
#  if HAVE_SYS_IOCTL_H
#   include <sys/ioctl.h>
#  endif
# endif
#endif

#if NEED_PTEM_H
 /* On SCO, they neglected to define struct winsize in termios.h -- it's only
  * in termio.h and ptem.h (the former conflicts with other definitions).
  */
# include <sys/stream.h>
# include <sys/ptem.h>
#endif

#if HAVE_LANGINFO_CODESET
#include <langinfo.h>
#endif

/*
 * SCO defines TIOCGSIZE and the corresponding struct.  Other systems (SunOS,
 * Solaris, IRIX) define TIOCGWINSZ and struct winsize.
 */
#ifdef TIOCGSIZE
# define IOCTL_WINSIZE TIOCGSIZE
# define STRUCT_WINSIZE struct ttysize
# define WINSIZE_ROWS(n) (int)n.ts_lines
# define WINSIZE_COLS(n) (int)n.ts_cols
#else
# ifdef TIOCGWINSZ
#  define IOCTL_WINSIZE TIOCGWINSZ
#  define STRUCT_WINSIZE struct winsize
#  define WINSIZE_ROWS(n) (int)n.ws_row
#  define WINSIZE_COLS(n) (int)n.ws_col
# endif
#endif

/*
 * Reduce explicit use of "cur_term" global variable.
 */
#undef CUR
#define CUR termp->type.

/*
 * Wrap global variables in this module.
 */
#if USE_REENTRANT

NCURSES_EXPORT(char *)
NCURSES_PUBLIC_VAR(ttytype) (void)
{
    static char empty[] = "";
    char *result = empty;

#if NCURSES_SP_FUNCS
    if (CURRENT_SCREEN) {
	TERMINAL *termp = TerminalOf(CURRENT_SCREEN);
	if (termp != 0) {
	    result = termp->type.term_names;
	}
    }
#else
    if (cur_term != 0) {
	result = cur_term->type.term_names;
    }
#endif
    return result;
}

NCURSES_EXPORT(int *)
_nc_ptr_Lines(SCREEN *sp)
{
    return ptrLines(sp);
}

NCURSES_EXPORT(int)
NCURSES_PUBLIC_VAR(LINES) (void)
{
    return *_nc_ptr_Lines(CURRENT_SCREEN);
}

NCURSES_EXPORT(int *)
_nc_ptr_Cols(SCREEN *sp)
{
    return ptrCols(sp);
}

NCURSES_EXPORT(int)
NCURSES_PUBLIC_VAR(COLS) (void)
{
    return *_nc_ptr_Cols(CURRENT_SCREEN);
}

NCURSES_EXPORT(int *)
_nc_ptr_Tabsize(SCREEN *sp)
{
    return ptrTabsize(sp);
}

NCURSES_EXPORT(int)
NCURSES_PUBLIC_VAR(TABSIZE) (void)
{
    return *_nc_ptr_Tabsize(CURRENT_SCREEN);
}
#else
NCURSES_EXPORT_VAR(char) ttytype[NAMESIZE] = "";
NCURSES_EXPORT_VAR(int) LINES = 0;
NCURSES_EXPORT_VAR(int) COLS = 0;
NCURSES_EXPORT_VAR(int) TABSIZE = 8;
#endif

#if NCURSES_EXT_FUNCS
NCURSES_EXPORT(int)
NCURSES_SP_NAME(set_tabsize) (NCURSES_SP_DCLx int value)
{
    int code = OK;
#if USE_REENTRANT
    if (SP_PARM) {
	SP_PARM->_TABSIZE = value;
    } else {
	code = ERR;
    }
#else
    (void) SP_PARM;
    TABSIZE = value;
#endif
    return code;
}

#if NCURSES_SP_FUNCS
NCURSES_EXPORT(int)
set_tabsize(int value)
{
    return NCURSES_SP_NAME(set_tabsize) (CURRENT_SCREEN, value);
}
#endif
#endif /* NCURSES_EXT_FUNCS */

#if USE_SIGWINCH
/*
 * If we have a pending SIGWINCH, set the flag in each screen.
 */
NCURSES_EXPORT(int)
_nc_handle_sigwinch(SCREEN *sp)
{
    SCREEN *scan;

    if (_nc_globals.have_sigwinch) {
	_nc_globals.have_sigwinch = 0;

	for (each_screen(scan)) {
	    scan->_sig_winch = TRUE;
	}
    }

    return (sp ? sp->_sig_winch : 0);
}

#endif

NCURSES_EXPORT(void)
NCURSES_SP_NAME(use_env) (NCURSES_SP_DCLx bool f)
{
    T((T_CALLED("use_env(%p,%d)"), (void *) SP_PARM, (int) f));
#if NCURSES_SP_FUNCS
    START_TRACE();
    if (IsPreScreen(SP_PARM)) {
	SP_PARM->_use_env = f;
    }
#else
    _nc_prescreen.use_env = f;
#endif
    returnVoid;
}

NCURSES_EXPORT(void)
NCURSES_SP_NAME(use_tioctl) (NCURSES_SP_DCLx bool f)
{
    T((T_CALLED("use_tioctl(%p,%d)"), (void *) SP_PARM, (int) f));
#if NCURSES_SP_FUNCS
    START_TRACE();
    if (IsPreScreen(SP_PARM)) {
	SP_PARM->_use_tioctl = f;
    }
#else
    _nc_prescreen.use_tioctl = f;
#endif
    returnVoid;
}

#if NCURSES_SP_FUNCS
NCURSES_EXPORT(void)
use_env(bool f)
{
    T((T_CALLED("use_env(%d)"), (int) f));
    START_TRACE();
    _nc_prescreen.use_env = f;
    returnVoid;
}

NCURSES_EXPORT(void)
use_tioctl(bool f)
{
    T((T_CALLED("use_tioctl(%d)"), (int) f));
    START_TRACE();
    _nc_prescreen.use_tioctl = f;
    returnVoid;
}
#endif

NCURSES_EXPORT(void)
_nc_get_screensize(SCREEN *sp,
#ifdef USE_TERM_DRIVER
		   TERMINAL * termp,
#endif
		   int *linep, int *colp)
/* Obtain lines/columns values from the environment and/or terminfo entry */
{
#ifdef USE_TERM_DRIVER
    TERMINAL_CONTROL_BLOCK *TCB;
    int my_tabsize;

    assert(termp != 0 && linep != 0 && colp != 0);
    TCB = (TERMINAL_CONTROL_BLOCK *) termp;

    my_tabsize = TCB->info.tabsize;
    TCB->drv->size(TCB, linep, colp);

#if USE_REENTRANT
    if (sp != 0) {
	sp->_TABSIZE = my_tabsize;
    }
#else
    (void) sp;
    TABSIZE = my_tabsize;
#endif
    T(("TABSIZE = %d", my_tabsize));
#else /* !USE_TERM_DRIVER */
    TERMINAL *termp = cur_term;
    int my_tabsize;

    /* figure out the size of the screen */
    T(("screen size: terminfo lines = %d columns = %d", lines, columns));

    *linep = (int) lines;
    *colp = (int) columns;

    if (_nc_prescreen.use_env || _nc_prescreen.use_tioctl) {
	int value;

#ifdef __EMX__
	{
	    int screendata[2];
	    _scrsize(screendata);
	    *colp = screendata[0];
	    *linep = ((sp != 0 && sp->_filtered)
		      ? 1
		      : screendata[1]);
	    T(("EMX screen size: environment LINES = %d COLUMNS = %d",
	       *linep, *colp));
	}
#endif
#if HAVE_SIZECHANGE
	/* try asking the OS */
	if (isatty(cur_term->Filedes)) {
	    STRUCT_WINSIZE size;

	    errno = 0;
	    do {
		if (ioctl(cur_term->Filedes, IOCTL_WINSIZE, &size) >= 0) {
		    *linep = ((sp != 0 && sp->_filtered)
			      ? 1
			      : WINSIZE_ROWS(size));
		    *colp = WINSIZE_COLS(size);
		    T(("SYS screen size: environment LINES = %d COLUMNS = %d",
		       *linep, *colp));
		    break;
		}
	    } while
		(errno == EINTR);
	}
#endif /* HAVE_SIZECHANGE */

	if (_nc_prescreen.use_env) {
	    if (_nc_prescreen.use_tioctl) {
		/*
		 * If environment variables are used, update them.
		 */
		if ((sp == 0 || !sp->_filtered) && _nc_getenv_num("LINES") > 0) {
		    _nc_setenv_num("LINES", *linep);
		}
		if (_nc_getenv_num("COLUMNS") > 0) {
		    _nc_setenv_num("COLUMNS", *colp);
		}
	    }

	    /*
	     * Finally, look for environment variables.
	     *
	     * Solaris lets users override either dimension with an environment
	     * variable.
	     */
	    if ((value = _nc_getenv_num("LINES")) > 0) {
		*linep = value;
		T(("screen size: environment LINES = %d", *linep));
	    }
	    if ((value = _nc_getenv_num("COLUMNS")) > 0) {
		*colp = value;
		T(("screen size: environment COLUMNS = %d", *colp));
	    }
	}

	/* if we can't get dynamic info about the size, use static */
	if (*linep <= 0) {
	    *linep = (int) lines;
	}
	if (*colp <= 0) {
	    *colp = (int) columns;
	}

	/* the ultimate fallback, assume fixed 24x80 size */
	if (*linep <= 0) {
	    *linep = 24;
	}
	if (*colp <= 0) {
	    *colp = 80;
	}

	/*
	 * Put the derived values back in the screen-size caps, so
	 * tigetnum() and tgetnum() will do the right thing.
	 */
	lines = (short) (*linep);
	columns = (short) (*colp);
    }

    T(("screen size is %dx%d", *linep, *colp));

    if (VALID_NUMERIC(init_tabs))
	my_tabsize = (int) init_tabs;
    else
	my_tabsize = 8;

#if USE_REENTRANT
    if (sp != 0)
	sp->_TABSIZE = my_tabsize;
#else
    TABSIZE = my_tabsize;
#endif
    T(("TABSIZE = %d", TABSIZE));
#endif /* USE_TERM_DRIVER */
}

#if USE_SIZECHANGE
NCURSES_EXPORT(void)
_nc_update_screensize(SCREEN *sp)
{
    int new_lines;
    int new_cols;

#ifdef USE_TERM_DRIVER
    int old_lines;
    int old_cols;

    assert(sp != 0);

    CallDriver_2(sp, getsize, &old_lines, &old_cols);

#else
    TERMINAL *termp = cur_term;
    int old_lines = lines;
    int old_cols = columns;
#endif

    TINFO_GET_SIZE(sp, sp->_term, &new_lines, &new_cols);

    /*
     * See is_term_resized() and resizeterm().
     * We're doing it this way because those functions belong to the upper
     * ncurses library, while this resides in the lower terminfo library.
     */
    if (sp != 0 && sp->_resize != 0) {
	if ((new_lines != old_lines) || (new_cols != old_cols)) {
	    sp->_resize(NCURSES_SP_ARGx new_lines, new_cols);
	} else if (sp->_sig_winch && (sp->_ungetch != 0)) {
	    sp->_ungetch(SP_PARM, KEY_RESIZE);	/* so application can know this */
	}
	sp->_sig_winch = FALSE;
    }
}
#endif

/****************************************************************************
 *
 * Terminal setup
 *
 ****************************************************************************/

#if NCURSES_USE_DATABASE || NCURSES_USE_TERMCAP
/*
 * Return 1 if entry found, 0 if not found, -1 if database not accessible,
 * just like tgetent().
 */
int
_nc_setup_tinfo(const char *const tn, TERMTYPE *const tp)
{
    char filename[PATH_MAX];
    int status = _nc_read_entry(tn, filename, tp);

    /*
     * If we have an entry, force all of the cancelled strings to null
     * pointers so we don't have to test them in the rest of the library.
     * (The terminfo compiler bypasses this logic, since it must know if
     * a string is cancelled, for merging entries).
     */
    if (status == TGETENT_YES) {
	unsigned n;
	for_each_boolean(n, tp) {
	    if (!VALID_BOOLEAN(tp->Booleans[n]))
		tp->Booleans[n] = FALSE;
	}
	for_each_string(n, tp) {
	    if (tp->Strings[n] == CANCELLED_STRING)
		tp->Strings[n] = ABSENT_STRING;
	}
    }
    return (status);
}
#endif

/*
**	Take the real command character out of the CC environment variable
**	and substitute it in for the prototype given in 'command_character'.
*/
void
_nc_tinfo_cmdch(TERMINAL * termp, int proto)
{
    unsigned i;
    char CC;
    char *tmp;

    /*
     * Only use the character if the string is a single character,
     * since it is fairly common for developers to set the C compiler
     * name as an environment variable - using the same symbol.
     */
    if ((tmp = getenv("CC")) != 0 && strlen(tmp) == 1) {
	CC = *tmp;
	for_each_string(i, &(termp->type)) {
	    for (tmp = termp->type.Strings[i]; tmp && *tmp; tmp++) {
		if (UChar(*tmp) == proto)
		    *tmp = CC;
	    }
	}
    }
}

/*
 * Find the locale which is in effect.
 */
NCURSES_EXPORT(char *)
_nc_get_locale(void)
{
    char *env;
#if HAVE_LOCALE_H
    /*
     * This is preferable to using getenv() since it ensures that we are using
     * the locale which was actually initialized by the application.
     */
    env = setlocale(LC_CTYPE, 0);
#else
    if (((env = getenv("LC_ALL")) != 0 && *env != '\0')
	|| ((env = getenv("LC_CTYPE")) != 0 && *env != '\0')
	|| ((env = getenv("LANG")) != 0 && *env != '\0')) {
	;
    }
#endif
    T(("_nc_get_locale %s", _nc_visbuf(env)));
    return env;
}

/*
 * Check if we are running in a UTF-8 locale.
 */
NCURSES_EXPORT(int)
_nc_unicode_locale(void)
{
    int result = 0;
#if defined(__MINGW32__) && USE_WIDEC_SUPPORT
    result = 1;
#elif HAVE_LANGINFO_CODESET
    char *env = nl_langinfo(CODESET);
    result = !strcmp(env, "UTF-8");
    T(("_nc_unicode_locale(%s) ->%d", env, result));
#else
    char *env = _nc_get_locale();
    if (env != 0) {
	if (strstr(env, ".UTF-8") != 0) {
	    result = 1;
	    T(("_nc_unicode_locale(%s) ->%d", env, result));
	}
    }
#endif
    return result;
}

#define CONTROL_N(s) ((s) != 0 && strstr(s, "\016") != 0)
#define CONTROL_O(s) ((s) != 0 && strstr(s, "\017") != 0)

/*
 * Check for known broken cases where a UTF-8 locale breaks the alternate
 * character set.
 */
NCURSES_EXPORT(int)
_nc_locale_breaks_acs(TERMINAL * termp)
{
    const char *env_name = "NCURSES_NO_UTF8_ACS";
    char *env;
    int value;
    int result = 0;

    if (getenv(env_name) != 0) {
	result = _nc_getenv_num(env_name);
    } else if ((value = tigetnum("U8")) >= 0) {
	result = value;		/* use extension feature */
    } else if ((env = getenv("TERM")) != 0) {
	if (strstr(env, "linux")) {
	    result = 1;		/* always broken */
	} else if (strstr(env, "screen") != 0
		   && ((env = getenv("TERMCAP")) != 0
		       && strstr(env, "screen") != 0)
		   && strstr(env, "hhII00") != 0) {
	    if (CONTROL_N(enter_alt_charset_mode) ||
		CONTROL_O(enter_alt_charset_mode) ||
		CONTROL_N(set_attributes) ||
		CONTROL_O(set_attributes)) {
		result = 1;
	    }
	}
    }
    return result;
}

NCURSES_EXPORT(int)
TINFO_SETUP_TERM(TERMINAL ** tp,
		 NCURSES_CONST char *tname,
		 int Filedes,
		 int *errret,
		 int reuse)
{
#ifdef USE_TERM_DRIVER
    TERMINAL_CONTROL_BLOCK *TCB = 0;
#else
    int status;
#endif
    TERMINAL *termp;
    SCREEN *sp = 0;
    int code = ERR;

    START_TRACE();

#ifdef USE_TERM_DRIVER
    T((T_CALLED("_nc_setupterm_ex(%p,%s,%d,%p)"),
       (void *) tp, _nc_visbuf(tname), Filedes, (void *) errret));

    if (tp == 0) {
	ret_error0(TGETENT_ERR,
		   "Invalid parameter, internal error.\n");
    } else
	termp = *tp;
#else
    termp = cur_term;
    T((T_CALLED("setupterm(%s,%d,%p)"), _nc_visbuf(tname), Filedes, (void *) errret));
#endif

    if (tname == 0) {
	tname = getenv("TERM");
	if (tname == 0 || *tname == '\0') {
#ifdef USE_TERM_DRIVER
	    tname = "unknown";
#else
	    ret_error0(TGETENT_ERR, "TERM environment variable not set.\n");
#endif
	}
    }

    if (strlen(tname) > MAX_NAME_SIZE) {
	ret_error(TGETENT_ERR,
		  "TERM environment must be <= %d characters.\n",
		  MAX_NAME_SIZE);
    }

    T(("your terminal name is %s", tname));

    /*
     * Allow output redirection.  This is what SVr3 does.  If stdout is
     * directed to a file, screen updates go to standard error.
     */
    if (Filedes == STDOUT_FILENO && !isatty(Filedes))
	Filedes = STDERR_FILENO;

    /*
     * Check if we have already initialized to use this terminal.  If so, we
     * do not need to re-read the terminfo entry, or obtain TTY settings.
     *
     * This is an improvement on SVr4 curses.  If an application mixes curses
     * and termcap calls, it may call both initscr and tgetent.  This is not
     * really a good thing to do, but can happen if someone tries using ncurses
     * with the readline library.  The problem we are fixing is that when
     * tgetent calls setupterm, the resulting Ottyb struct in cur_term is
     * zeroed.  A subsequent call to endwin uses the zeroed terminal settings
     * rather than the ones saved in initscr.  So we check if cur_term appears
     * to contain terminal settings for the same output file as our current
     * call - and copy those terminal settings.  (SVr4 curses does not do this,
     * however applications that are working around the problem will still work
     * properly with this feature).
     */
    if (reuse
	&& (termp != 0)
	&& termp->Filedes == Filedes
	&& termp->_termname != 0
	&& !strcmp(termp->_termname, tname)
	&& _nc_name_match(termp->type.term_names, tname, "|")) {
	T(("reusing existing terminal information and mode-settings"));
	code = OK;
#ifdef USE_TERM_DRIVER
	TCB = (TERMINAL_CONTROL_BLOCK *) termp;
#endif
    } else {
#ifdef USE_TERM_DRIVER
	TERMINAL_CONTROL_BLOCK *my_tcb;
	my_tcb = typeCalloc(TERMINAL_CONTROL_BLOCK, 1);
	termp = &(my_tcb->term);
#else
	termp = typeCalloc(TERMINAL, 1);
#endif
	if (termp == 0) {
	    ret_error0(TGETENT_ERR,
		       "Not enough memory to create terminal structure.\n");
	}
#ifdef USE_TERM_DRIVER
	INIT_TERM_DRIVER();
	TCB = (TERMINAL_CONTROL_BLOCK *) termp;
	code = _nc_globals.term_driver(TCB, tname, errret);
	if (code == OK) {
	    termp->Filedes = (short) Filedes;
	    termp->_termname = strdup(tname);
	} else {
	    ret_error0(TGETENT_ERR,
		       "Could not find any driver to handle this terminal.\n");
	}
#else
#if NCURSES_USE_DATABASE || NCURSES_USE_TERMCAP
	status = _nc_setup_tinfo(tname, &termp->type);
#else
	status = TGETENT_NO;
#endif

	/* try fallback list if entry on disk */
	if (status != TGETENT_YES) {
	    const TERMTYPE *fallback = _nc_fallback(tname);

	    if (fallback) {
		_nc_copy_termtype(&(termp->type), fallback);
		status = TGETENT_YES;
	    }
	}

	if (status != TGETENT_YES) {
	    del_curterm(termp);
	    if (status == TGETENT_ERR) {
		ret_error0(status, "terminals database is inaccessible\n");
	    } else if (status == TGETENT_NO) {
		ret_error1(status, "unknown terminal type.\n", tname);
	    }
	}
#if !USE_REENTRANT
	strncpy(ttytype, termp->type.term_names, (size_t) (NAMESIZE - 1));
	ttytype[NAMESIZE - 1] = '\0';
#endif

	termp->Filedes = (short) Filedes;
	termp->_termname = strdup(tname);

	set_curterm(termp);

	if (command_character)
	    _nc_tinfo_cmdch(termp, UChar(*command_character));

	/*
	 * If an application calls setupterm() rather than initscr() or
	 * newterm(), we will not have the def_prog_mode() call in
	 * _nc_setupscreen().  Do it now anyway, so we can initialize the
	 * baudrate.
	 */
	if (isatty(Filedes)) {
	    def_prog_mode();
	    baudrate();
	}
	code = OK;
#endif
    }

#ifdef USE_TERM_DRIVER
    *tp = termp;
    NCURSES_SP_NAME(set_curterm) (sp, termp);
    TCB->drv->init(TCB);
#else
    sp = SP;
#endif

    /*
     * We should always check the screensize, just in case.
     */
    TINFO_GET_SIZE(sp, termp, ptrLines(sp), ptrCols(sp));

    if (errret)
	*errret = TGETENT_YES;

#ifndef USE_TERM_DRIVER
    if (generic_type) {
	/*
	 * BSD 4.3's termcap contains mis-typed "gn" for wy99.  Do a sanity
	 * check before giving up.
	 */
	if ((VALID_STRING(cursor_address)
	     || (VALID_STRING(cursor_down) && VALID_STRING(cursor_home)))
	    && VALID_STRING(clear_screen)) {
	    ret_error1(TGETENT_YES, "terminal is not really generic.\n", tname);
	} else {
	    del_curterm(termp);
	    ret_error1(TGETENT_NO, "I need something more specific.\n", tname);
	}
    } else if (hard_copy) {
	ret_error1(TGETENT_YES, "I can't handle hardcopy terminals.\n", tname);
    }
#endif
    returnCode(code);
}

#if NCURSES_SP_FUNCS
/*
 * In case of handling multiple screens, we need to have a screen before
 * initialization in setupscreen takes place.  This is to extend the substitute
 * for some of the stuff in _nc_prescreen, especially for slk and ripoff
 * handling which should be done per screen.
 */
NCURSES_EXPORT(SCREEN *)
new_prescr(void)
{
    static SCREEN *sp;

    START_TRACE();
    T((T_CALLED("new_prescr()")));

    if (sp == 0) {
	sp = _nc_alloc_screen_sp();
	if (sp != 0) {
	    sp->rsp = sp->rippedoff;
	    sp->_filtered = _nc_prescreen.filter_mode;
	    sp->_use_env = _nc_prescreen.use_env;
#if NCURSES_NO_PADDING
	    sp->_no_padding = _nc_prescreen._no_padding;
#endif
	    sp->slk_format = 0;
	    sp->_slk = 0;
	    sp->_prescreen = TRUE;
	    SP_PRE_INIT(sp);
#if USE_REENTRANT
	    sp->_TABSIZE = _nc_prescreen._TABSIZE;
	    sp->_ESCDELAY = _nc_prescreen._ESCDELAY;
#endif
	}
    }
    returnSP(sp);
}
#endif

#ifdef USE_TERM_DRIVER
/*
 * This entrypoint is called from tgetent() to allow a special case of reusing
 * the same TERMINAL data (see comment).
 */
NCURSES_EXPORT(int)
_nc_setupterm(NCURSES_CONST char *tname,
	      int Filedes,
	      int *errret,
	      int reuse)
{
    int res;
    TERMINAL *termp = 0;
    res = TINFO_SETUP_TERM(&termp, tname, Filedes, errret, reuse);
    if (ERR != res)
	NCURSES_SP_NAME(set_curterm) (CURRENT_SCREEN_PRE, termp);
    return res;
}
#endif

/*
 *	setupterm(termname, Filedes, errret)
 *
 *	Find and read the appropriate object file for the terminal
 *	Make cur_term point to the structure.
 */
NCURSES_EXPORT(int)
setupterm(NCURSES_CONST char *tname, int Filedes, int *errret)
{
    return _nc_setupterm(tname, Filedes, errret, FALSE);
}
