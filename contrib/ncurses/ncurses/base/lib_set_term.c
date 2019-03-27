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
**	lib_set_term.c
**
**	The routine set_term().
**
*/

#include <curses.priv.h>
#include <tic.h>

#ifndef CUR
#define CUR SP_TERMTYPE
#endif

MODULE_ID("$Id: lib_set_term.c,v 1.148 2013/08/31 13:33:06 tom Exp $")

#ifdef USE_TERM_DRIVER
#define MaxColors      InfoOf(sp).maxcolors
#define NumLabels      InfoOf(sp).numlabels
#else
#define MaxColors      max_colors
#define NumLabels      num_labels
#endif

NCURSES_EXPORT(SCREEN *)
set_term(SCREEN *screenp)
{
    SCREEN *oldSP;
    SCREEN *newSP;

    T((T_CALLED("set_term(%p)"), (void *) screenp));

    _nc_lock_global(curses);

    oldSP = CURRENT_SCREEN;
    _nc_set_screen(screenp);
    newSP = screenp;

    if (newSP != 0) {
	TINFO_SET_CURTERM(newSP, newSP->_term);
#if !USE_REENTRANT
	curscr = CurScreen(newSP);
	newscr = NewScreen(newSP);
	stdscr = StdScreen(newSP);
	COLORS = newSP->_color_count;
	COLOR_PAIRS = newSP->_pair_count;
#endif
    } else {
	TINFO_SET_CURTERM(oldSP, 0);
#if !USE_REENTRANT
	curscr = 0;
	newscr = 0;
	stdscr = 0;
	COLORS = 0;
	COLOR_PAIRS = 0;
#endif
    }

    _nc_unlock_global(curses);

    T((T_RETURN("%p"), (void *) oldSP));
    return (oldSP);
}

static void
_nc_free_keytry(TRIES * kt)
{
    if (kt != 0) {
	_nc_free_keytry(kt->child);
	_nc_free_keytry(kt->sibling);
	free(kt);
    }
}

static bool
delink_screen(SCREEN *sp)
{
    SCREEN *last = 0;
    SCREEN *temp;
    bool result = FALSE;

    for (each_screen(temp)) {
	if (temp == sp) {
	    if (last)
		last->_next_screen = sp->_next_screen;
	    else
		_nc_screen_chain = sp->_next_screen;
	    result = TRUE;
	    break;
	}
	last = temp;
    }
    return result;
}

/*
 * Free the storage associated with the given SCREEN sp.
 */
NCURSES_EXPORT(void)
delscreen(SCREEN *sp)
{
    int i;

    T((T_CALLED("delscreen(%p)"), (void *) sp));

    _nc_lock_global(curses);
    if (delink_screen(sp)) {
#ifdef USE_SP_RIPOFF
	ripoff_t *rop;
	if (safe_ripoff_sp && safe_ripoff_sp != safe_ripoff_stack) {
	    for (rop = safe_ripoff_stack;
		 rop != safe_ripoff_sp && (rop - safe_ripoff_stack) < N_RIPS;
		 rop++) {
		if (rop->win) {
		    (void) delwin(rop->win);
		    rop->win = 0;
		}
	    }
	}
#endif

	(void) _nc_freewin(CurScreen(sp));
	(void) _nc_freewin(NewScreen(sp));
	(void) _nc_freewin(StdScreen(sp));

	if (sp->_slk != 0) {
	    if (sp->_slk->ent != 0) {
		for (i = 0; i < sp->_slk->labcnt; ++i) {
		    FreeIfNeeded(sp->_slk->ent[i].ent_text);
		    FreeIfNeeded(sp->_slk->ent[i].form_text);
		}
		free(sp->_slk->ent);
	    }
	    free(sp->_slk);
	    sp->_slk = 0;
	}

	_nc_free_keytry(sp->_keytry);
	sp->_keytry = 0;

	_nc_free_keytry(sp->_key_ok);
	sp->_key_ok = 0;

	FreeIfNeeded(sp->_current_attr);

	FreeIfNeeded(sp->_color_table);
	FreeIfNeeded(sp->_color_pairs);

	FreeIfNeeded(sp->oldhash);
	FreeIfNeeded(sp->newhash);
	FreeIfNeeded(sp->hashtab);

	FreeIfNeeded(sp->_acs_map);
	FreeIfNeeded(sp->_screen_acs_map);

	NCURSES_SP_NAME(_nc_flush) (NCURSES_SP_ARG);
	NCURSES_SP_NAME(del_curterm) (NCURSES_SP_ARGx sp->_term);
	free(sp);

	/*
	 * If this was the current screen, reset everything that the
	 * application might try to use (except cur_term, which may have
	 * multiple references in different screens).
	 */
	if (sp == CURRENT_SCREEN) {
#if !USE_REENTRANT
	    curscr = 0;
	    newscr = 0;
	    stdscr = 0;
	    COLORS = 0;
	    COLOR_PAIRS = 0;
#endif
	    _nc_set_screen(0);
	}
    }
    _nc_unlock_global(curses);

    returnVoid;
}

static bool
no_mouse_event(SCREEN *sp GCC_UNUSED)
{
    return FALSE;
}

static bool
no_mouse_inline(SCREEN *sp GCC_UNUSED)
{
    return FALSE;
}

static bool
no_mouse_parse(SCREEN *sp GCC_UNUSED, int code GCC_UNUSED)
{
    return TRUE;
}

static void
no_mouse_resume(SCREEN *sp GCC_UNUSED)
{
}

static void
no_mouse_wrap(SCREEN *sp GCC_UNUSED)
{
}

#if NCURSES_EXT_FUNCS && USE_COLORFGBG
static char *
extract_fgbg(char *src, int *result)
{
    char *dst = 0;
    long value = strtol(src, &dst, 0);

    if (dst == 0) {
	dst = src;
    } else if (value >= 0) {
	*result = value;
    }
    while (*dst != 0 && *dst != ';')
	dst++;
    if (*dst == ';')
	dst++;
    return dst;
}
#endif

#define ReturnScreenError() { _nc_set_screen(0); \
                            returnCode(ERR); } while (0)

/* OS-independent screen initializations */
NCURSES_EXPORT(int)
NCURSES_SP_NAME(_nc_setupscreen) (
#if NCURSES_SP_FUNCS
				     SCREEN **spp,
#endif
				     int slines,
				     int scolumns,
				     FILE *output,
				     int filtered,
				     int slk_format)
{
    char *env;
    int bottom_stolen = 0;
    ripoff_t *rop;
    SCREEN *sp;
#ifndef USE_TERM_DRIVER
    bool support_cookies = USE_XMC_SUPPORT;
#endif

    T((T_CALLED("_nc_setupscreen(%d, %d, %p, %d, %d)"),
       slines, scolumns, (void *) output, filtered, slk_format));

    assert(CURRENT_SCREEN == 0);	/* has been reset in newterm() ! */

#if NCURSES_SP_FUNCS
    assert(spp != 0);
    sp = *spp;

    if (!sp) {
	sp = _nc_alloc_screen_sp();
	*spp = sp;
    }
    if (!sp
	|| ((sp->_acs_map = typeCalloc(chtype, ACS_LEN)) == 0)
	|| ((sp->_screen_acs_map = typeCalloc(bool, ACS_LEN)) == 0)) {
	ReturnScreenError();
    }

    T(("created SP %p", (void *) sp));
    sp->_next_screen = _nc_screen_chain;
    _nc_screen_chain = sp;

    if ((sp->_current_attr = typeCalloc(NCURSES_CH_T, 1)) == 0) {
	ReturnScreenError();
    }
#else
    if (!_nc_alloc_screen()
	|| ((SP->_acs_map = typeCalloc(chtype, ACS_LEN)) == 0)
	|| ((SP->_screen_acs_map = typeCalloc(bool, ACS_LEN)) == 0)) {
	returnCode(ERR);
    }

    T(("created SP %p", (void *) SP));

    sp = SP;			/* fixup so SET_LINES and SET_COLS works */
    sp->_next_screen = _nc_screen_chain;
    _nc_screen_chain = sp;

    if ((sp->_current_attr = typeCalloc(NCURSES_CH_T, 1)) == 0)
	returnCode(ERR);
#endif

    /*
     * We should always check the screensize, just in case.
     */
    _nc_set_screen(sp);
    sp->_term = cur_term;
#ifdef USE_TERM_DRIVER
    TCBOf(sp)->csp = sp;
    _nc_get_screensize(sp, sp->_term, &slines, &scolumns);
#else
    _nc_get_screensize(sp, &slines, &scolumns);
#endif
    SET_LINES(slines);
    SET_COLS(scolumns);

    T((T_CREATE("screen %s %dx%d"),
       NCURSES_SP_NAME(termname) (NCURSES_SP_ARG), slines, scolumns));

    sp->_filtered = filtered;

    /* implement filter mode */
    if (filtered) {
	slines = 1;
	SET_LINES(slines);
#ifdef USE_TERM_DRIVER
	CallDriver(sp, setfilter);
#else
	clear_screen = 0;
	cursor_down = parm_down_cursor = 0;
	cursor_address = 0;
	cursor_up = parm_up_cursor = 0;
	row_address = 0;

	cursor_home = carriage_return;
#endif
	T(("filter screensize %dx%d", slines, scolumns));
    }
#ifdef __DJGPP__
    T(("setting output mode to binary"));
    fflush(output);
    setmode(output, O_BINARY);
#endif
    NCURSES_SP_NAME(_nc_set_buffer) (NCURSES_SP_ARGx output, TRUE);
    sp->_lines = (NCURSES_SIZE_T) slines;
    sp->_lines_avail = (NCURSES_SIZE_T) slines;
    sp->_columns = (NCURSES_SIZE_T) scolumns;

    fflush(output);
    sp->_ofd = output ? fileno(output) : -1;
    sp->_ofp = output;
    sp->out_limit = (size_t) ((2 + slines) * (6 + scolumns));
    if ((sp->out_buffer = malloc(sp->out_limit)) == 0)
	sp->out_limit = 0;
    sp->out_inuse = 0;

    SP_PRE_INIT(sp);
    SetNoPadding(sp);

#if NCURSES_EXT_FUNCS
    sp->_default_color = FALSE;
    sp->_has_sgr_39_49 = FALSE;

    /*
     * Set our assumption of the terminal's default foreground and background
     * colors.  The curs_color man-page states that we can assume that the
     * background is black.  The origin of this assumption appears to be
     * terminals that displayed colored text, but no colored backgrounds, e.g.,
     * the first colored terminals around 1980.  More recent ones with better
     * technology can display not only colored backgrounds, but all
     * combinations.  So a terminal might be something other than "white" on
     * black (green/black looks monochrome too), but black on white or even
     * on ivory.
     *
     * White-on-black is the simplest thing to use for monochrome.  Almost
     * all applications that use color paint both text and background, so
     * the distinction is moot.  But a few do not - which is why we leave this
     * configurable (a better solution is to use assume_default_colors() for
     * the rare applications that do require that sort of appearance, since
     * is appears that more users expect to be able to make a white-on-black
     * or black-on-white display under control of the application than not).
     */
#ifdef USE_ASSUMED_COLOR
    sp->_default_fg = COLOR_WHITE;
    sp->_default_bg = COLOR_BLACK;
#else
    sp->_default_fg = C_MASK;
    sp->_default_bg = C_MASK;
#endif

    /*
     * Allow those assumed/default color assumptions to be overridden at
     * runtime:
     */
    if ((env = getenv("NCURSES_ASSUMED_COLORS")) != 0) {
	int fg, bg;
	char sep1, sep2;
	int count = sscanf(env, "%d%c%d%c", &fg, &sep1, &bg, &sep2);
	if (count >= 1) {
	    sp->_default_fg = ((fg >= 0 && fg < MaxColors) ? fg : C_MASK);
	    if (count >= 3) {
		sp->_default_bg = ((bg >= 0 && bg < MaxColors) ? bg : C_MASK);
	    }
	    TR(TRACE_CHARPUT | TRACE_MOVE,
	       ("from environment assumed fg=%d, bg=%d",
		sp->_default_fg,
		sp->_default_bg));
	}
    }
#if USE_COLORFGBG
    /*
     * If rxvt's $COLORFGBG variable is set, use it to specify the assumed
     * default colors.  Note that rxvt (mis)uses bold colors, equating a bold
     * color to that value plus 8.  We'll only use the non-bold color for now -
     * decide later if it is worth having default attributes as well.
     */
    if (getenv("COLORFGBG") != 0) {
	char *p = getenv("COLORFGBG");
	TR(TRACE_CHARPUT | TRACE_MOVE, ("decoding COLORFGBG %s", p));
	p = extract_fgbg(p, &(sp->_default_fg));
	p = extract_fgbg(p, &(sp->_default_bg));
	if (*p)			/* assume rxvt was compiled with xpm support */
	    p = extract_fgbg(p, &(sp->_default_bg));
	TR(TRACE_CHARPUT | TRACE_MOVE, ("decoded fg=%d, bg=%d",
					sp->_default_fg, sp->_default_bg));
	if (sp->_default_fg >= MaxColors) {
	    if (set_a_foreground != ABSENT_STRING
		&& !strcmp(set_a_foreground, "\033[3%p1%dm")) {
		set_a_foreground = "\033[3%?%p1%{8}%>%t9%e%p1%d%;m";
	    } else {
		sp->_default_fg %= MaxColors;
	    }
	}
	if (sp->_default_bg >= MaxColors) {
	    if (set_a_background != ABSENT_STRING
		&& !strcmp(set_a_background, "\033[4%p1%dm")) {
		set_a_background = "\033[4%?%p1%{8}%>%t9%e%p1%d%;m";
	    } else {
		sp->_default_bg %= MaxColors;
	    }
	}
    }
#endif
#endif /* NCURSES_EXT_FUNCS */

    sp->_maxclick = DEFAULT_MAXCLICK;
    sp->_mouse_event = no_mouse_event;
    sp->_mouse_inline = no_mouse_inline;
    sp->_mouse_parse = no_mouse_parse;
    sp->_mouse_resume = no_mouse_resume;
    sp->_mouse_wrap = no_mouse_wrap;
    sp->_mouse_fd = -1;

    /*
     * If we've no magic cookie support, we suppress attributes that xmc would
     * affect, i.e., the attributes that affect the rendition of a space.
     */
    sp->_ok_attributes = NCURSES_SP_NAME(termattrs) (NCURSES_SP_ARG);
    if (NCURSES_SP_NAME(has_colors) (NCURSES_SP_ARG)) {
	sp->_ok_attributes |= A_COLOR;
    }
#ifdef USE_TERM_DRIVER
    _nc_cookie_init(sp);
#else
#if USE_XMC_SUPPORT
    /*
     * If we have no magic-cookie support compiled-in, or if it is suppressed
     * in the environment, reset the support-flag.
     */
    if (magic_cookie_glitch >= 0) {
	if (getenv("NCURSES_NO_MAGIC_COOKIE") != 0) {
	    support_cookies = FALSE;
	}
    }
#endif

    if (!support_cookies && magic_cookie_glitch >= 0) {
	T(("will disable attributes to work w/o magic cookies"));
    }

    if (magic_cookie_glitch > 0) {	/* tvi, wyse */

	sp->_xmc_triggers = sp->_ok_attributes & XMC_CONFLICT;
#if 0
	/*
	 * We "should" treat colors as an attribute.  The wyse350 (and its
	 * clones) appear to be the only ones that have both colors and magic
	 * cookies.
	 */
	if (has_colors()) {
	    sp->_xmc_triggers |= A_COLOR;
	}
#endif
	sp->_xmc_suppress = sp->_xmc_triggers & (chtype) ~(A_BOLD);

	T(("magic cookie attributes %s", _traceattr(sp->_xmc_suppress)));
	/*
	 * Supporting line-drawing may be possible.  But make the regular
	 * video attributes work first.
	 */
	acs_chars = ABSENT_STRING;
	ena_acs = ABSENT_STRING;
	enter_alt_charset_mode = ABSENT_STRING;
	exit_alt_charset_mode = ABSENT_STRING;
#if USE_XMC_SUPPORT
	/*
	 * To keep the cookie support simple, suppress all of the optimization
	 * hooks except for clear_screen and the cursor addressing.
	 */
	if (support_cookies) {
	    clr_eol = ABSENT_STRING;
	    clr_eos = ABSENT_STRING;
	    set_attributes = ABSENT_STRING;
	}
#endif
    } else if (magic_cookie_glitch == 0) {	/* hpterm */
    }

    /*
     * If magic cookies are not supported, cancel the strings that set
     * video attributes.
     */
    if (!support_cookies && magic_cookie_glitch >= 0) {
	magic_cookie_glitch = ABSENT_NUMERIC;
	set_attributes = ABSENT_STRING;
	enter_blink_mode = ABSENT_STRING;
	enter_bold_mode = ABSENT_STRING;
	enter_dim_mode = ABSENT_STRING;
	enter_reverse_mode = ABSENT_STRING;
	enter_standout_mode = ABSENT_STRING;
	enter_underline_mode = ABSENT_STRING;
    }

    /* initialize normal acs before wide, since we use mapping in the latter */
#if !USE_WIDEC_SUPPORT
    if (_nc_unicode_locale() && _nc_locale_breaks_acs(sp->_term)) {
	acs_chars = NULL;
	ena_acs = NULL;
	enter_alt_charset_mode = NULL;
	exit_alt_charset_mode = NULL;
	set_attributes = NULL;
    }
#endif
#endif

    NCURSES_SP_NAME(_nc_init_acs) (NCURSES_SP_ARG);
#if USE_WIDEC_SUPPORT
    sp->_screen_unicode = _nc_unicode_locale();
    _nc_init_wacs();
    if (_nc_wacs == 0) {
	ReturnScreenError();
    }

    sp->_screen_acs_fix = (sp->_screen_unicode
			   && _nc_locale_breaks_acs(sp->_term));
#endif
    env = _nc_get_locale();
    sp->_legacy_coding = ((env == 0)
			  || !strcmp(env, "C")
			  || !strcmp(env, "POSIX"));
    T(("legacy-coding %d", sp->_legacy_coding));

    sp->_nc_sp_idcok = TRUE;
    sp->_nc_sp_idlok = FALSE;

    sp->oldhash = 0;
    sp->newhash = 0;

    T(("creating newscr"));
    NewScreen(sp) = NCURSES_SP_NAME(newwin) (NCURSES_SP_ARGx slines, scolumns,
					     0, 0);
    if (NewScreen(sp) == 0) {
	ReturnScreenError();
    }
    T(("creating curscr"));
    CurScreen(sp) = NCURSES_SP_NAME(newwin) (NCURSES_SP_ARGx slines, scolumns,
					     0, 0);
    if (CurScreen(sp) == 0) {
	ReturnScreenError();
    }
#if !USE_REENTRANT
    newscr = NewScreen(sp);
    curscr = CurScreen(sp);
#endif
#if USE_SIZECHANGE
    sp->_resize = NCURSES_SP_NAME(resizeterm);
    sp->_ungetch = safe_ungetch;
#endif

    NewScreen(sp)->_clear = TRUE;
    CurScreen(sp)->_clear = FALSE;

    NCURSES_SP_NAME(def_shell_mode) (NCURSES_SP_ARG);
    NCURSES_SP_NAME(def_prog_mode) (NCURSES_SP_ARG);

    if (safe_ripoff_sp && safe_ripoff_sp != safe_ripoff_stack) {
	for (rop = safe_ripoff_stack;
	     rop != safe_ripoff_sp && (rop - safe_ripoff_stack) < N_RIPS;
	     rop++) {

	    /* If we must simulate soft labels, grab off the line to be used.
	       We assume that we must simulate, if it is none of the standard
	       formats (4-4 or 3-2-3) for which there may be some hardware
	       support. */
	    if (rop->hook == _nc_slk_initialize) {
		if (!(NumLabels <= 0 || !SLK_STDFMT(slk_format))) {
		    continue;
		}
	    }
	    if (rop->hook) {
		int count;
		WINDOW *w;

		count = (rop->line < 0) ? -rop->line : rop->line;
		T(("ripping off %i lines at %s", count,
		   ((rop->line < 0)
		    ? "bottom"
		    : "top")));

		w = NCURSES_SP_NAME(newwin) (NCURSES_SP_ARGx
					     count, scolumns,
					     ((rop->line < 0)
					      ? sp->_lines_avail - count
					      : 0),
					     0);
		if (w) {
		    rop->win = w;
		    rop->hook(w, scolumns);
		} else {
		    ReturnScreenError();
		}
		if (rop->line < 0) {
		    bottom_stolen += count;
		} else {
		    sp->_topstolen = (NCURSES_SIZE_T) (sp->_topstolen + count);
		}
		sp->_lines_avail = (NCURSES_SIZE_T) (sp->_lines_avail - count);
	    }
	}
	/* reset the stack */
	safe_ripoff_sp = safe_ripoff_stack;
    }

    T(("creating stdscr"));
    assert((sp->_lines_avail + sp->_topstolen + bottom_stolen) == slines);
    if ((StdScreen(sp) = NCURSES_SP_NAME(newwin) (NCURSES_SP_ARGx
						  sp->_lines_avail,
						  scolumns, 0, 0)) == 0) {
	ReturnScreenError();
    }
    SET_LINES(sp->_lines_avail);
#if !USE_REENTRANT
    stdscr = StdScreen(sp);
#endif
    sp->_prescreen = FALSE;
    returnCode(OK);
}

#if NCURSES_SP_FUNCS
NCURSES_EXPORT(int)
_nc_setupscreen(int slines GCC_UNUSED,
		int scolumns GCC_UNUSED,
		FILE *output,
		int filtered,
		int slk_format)
{
    SCREEN *sp = 0;
    int rc = NCURSES_SP_NAME(_nc_setupscreen) (&sp,
					       slines,
					       scolumns,
					       output,
					       filtered,
					       slk_format);
    if (rc != OK)
	_nc_set_screen(0);
    return rc;
}
#endif

/*
 * The internal implementation interprets line as the number of lines to rip
 * off from the top or bottom.
 */
NCURSES_EXPORT(int)
NCURSES_SP_NAME(_nc_ripoffline) (NCURSES_SP_DCLx
				 int line,
				 int (*init) (WINDOW *, int))
{
    int code = ERR;

    START_TRACE();
    T((T_CALLED("ripoffline(%p,%d,%p)"), (void *) SP_PARM, line, init));

#if NCURSES_SP_FUNCS
    if (SP_PARM != 0 && SP_PARM->_prescreen)
#endif
    {
	if (line == 0) {
	    code = OK;
	} else {
	    if (safe_ripoff_sp == 0)
		safe_ripoff_sp = safe_ripoff_stack;
	    if (safe_ripoff_sp < safe_ripoff_stack + N_RIPS) {
		safe_ripoff_sp->line = line;
		safe_ripoff_sp->hook = init;
		(safe_ripoff_sp)++;
		code = OK;
	    }
	}
    }

    returnCode(code);
}

#if NCURSES_SP_FUNCS
NCURSES_EXPORT(int)
_nc_ripoffline(int line, int (*init) (WINDOW *, int))
{
    return NCURSES_SP_NAME(_nc_ripoffline) (CURRENT_SCREEN_PRE, line, init);
}
#endif

NCURSES_EXPORT(int)
NCURSES_SP_NAME(ripoffline) (NCURSES_SP_DCLx
			     int line,
			     int (*init) (WINDOW *, int))
{
    START_TRACE();
    return NCURSES_SP_NAME(_nc_ripoffline) (NCURSES_SP_ARGx
					    (line < 0) ? -1 : 1,
					    init);
}

#if NCURSES_SP_FUNCS
NCURSES_EXPORT(int)
ripoffline(int line, int (*init) (WINDOW *, int))
{
    return NCURSES_SP_NAME(ripoffline) (CURRENT_SCREEN_PRE, line, init);
}
#endif
