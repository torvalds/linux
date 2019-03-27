/****************************************************************************
 * Copyright (c) 1998-2013,2014 Free Software Foundation, Inc.              *
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

/*-----------------------------------------------------------------
 *
 *	lib_doupdate.c
 *
 * 	The routine doupdate() and its dependents.
 * 	All physical output is concentrated here (except _nc_outch()
 *	in lib_tputs.c).
 *
 *-----------------------------------------------------------------*/

#include <curses.priv.h>

#ifndef CUR
#define CUR SP_TERMTYPE
#endif

#if defined __HAIKU__ && defined __BEOS__
#undef __BEOS__
#endif

#ifdef __BEOS__
#undef false
#undef true
#include <OS.h>
#endif

#if defined(TRACE) && HAVE_SYS_TIMES_H && HAVE_TIMES
#define USE_TRACE_TIMES 1
#else
#define USE_TRACE_TIMES 0
#endif

#if HAVE_SYS_TIME_H && HAVE_SYS_TIME_SELECT
#include <sys/time.h>
#endif

#if USE_TRACE_TIMES
#include <sys/times.h>
#endif

#if USE_FUNC_POLL
#elif HAVE_SELECT
#if HAVE_SYS_SELECT_H
#include <sys/select.h>
#endif
#endif

#include <ctype.h>

MODULE_ID("$Id: tty_update.c,v 1.277 2014/02/01 22:09:27 tom Exp $")

/*
 * This define controls the line-breakout optimization.  Every once in a
 * while during screen refresh, we want to check for input and abort the
 * update if there's some waiting.  CHECK_INTERVAL controls the number of
 * changed lines to be emitted between input checks.
 *
 * Note: Input-check-and-abort is no longer done if the screen is being
 * updated from scratch.  This is a feature, not a bug.
 */
#define CHECK_INTERVAL	5

#define FILL_BCE(sp) (sp->_coloron && !sp->_default_color && !back_color_erase)

static const NCURSES_CH_T blankchar = NewChar(BLANK_TEXT);
static NCURSES_CH_T normal = NewChar(BLANK_TEXT);

/*
 * Enable checking to see if doupdate and friends are tracking the true
 * cursor position correctly.  NOTE: this is a debugging hack which will
 * work ONLY on ANSI-compatible terminals!
 */
/* #define POSITION_DEBUG */

static NCURSES_INLINE NCURSES_CH_T ClrBlank(NCURSES_SP_DCLx WINDOW *win);

#if NCURSES_SP_FUNCS
static int ClrBottom(SCREEN *, int total);
static void ClearScreen(SCREEN *, NCURSES_CH_T blank);
static void ClrUpdate(SCREEN *);
static void DelChar(SCREEN *, int count);
static void InsStr(SCREEN *, NCURSES_CH_T * line, int count);
static void TransformLine(SCREEN *, int const lineno);
#else
static int ClrBottom(int total);
static void ClearScreen(NCURSES_CH_T blank);
static void ClrUpdate(void);
static void DelChar(int count);
static void InsStr(NCURSES_CH_T * line, int count);
static void TransformLine(int const lineno);
#endif

#ifdef POSITION_DEBUG
/****************************************************************************
 *
 * Debugging code.  Only works on ANSI-standard terminals.
 *
 ****************************************************************************/

static void
position_check(NCURSES_SP_DCLx int expected_y, int expected_x, char *legend)
/* check to see if the real cursor position matches the virtual */
{
    char buf[20];
    char *s;
    int y, x;

    if (!_nc_tracing || (expected_y < 0 && expected_x < 0))
	return;

    NCURSES_SP_NAME(_nc_flush) (NCURSES_SP_ARG);
    memset(buf, '\0', sizeof(buf));
    NCURSES_PUTP2_FLUSH("cpr", "\033[6n");	/* only works on ANSI-compatibles */
    *(s = buf) = 0;
    do {
	int ask = sizeof(buf) - 1 - (s - buf);
	int got = read(0, s, ask);
	if (got == 0)
	    break;
	s += got;
    } while (strchr(buf, 'R') == 0);
    _tracef("probe returned %s", _nc_visbuf(buf));

    /* try to interpret as a position report */
    if (sscanf(buf, "\033[%d;%dR", &y, &x) != 2) {
	_tracef("position probe failed in %s", legend);
    } else {
	if (expected_x < 0)
	    expected_x = x - 1;
	if (expected_y < 0)
	    expected_y = y - 1;
	if (y - 1 != expected_y || x - 1 != expected_x) {
	    NCURSES_SP_NAME(beep) (NCURSES_SP_ARG);
	    NCURSES_SP_NAME(tputs) (NCURSES_SP_ARGx
				    tparm("\033[%d;%dH",
					  expected_y + 1,
					  expected_x + 1),
				    1, NCURSES_SP_NAME(_nc_outch));
	    _tracef("position seen (%d, %d) doesn't match expected one (%d, %d) in %s",
		    y - 1, x - 1, expected_y, expected_x, legend);
	} else {
	    _tracef("position matches OK in %s", legend);
	}
    }
}
#else
#define position_check(sp, expected_y, expected_x, legend)	/* nothing */
#endif /* POSITION_DEBUG */

/****************************************************************************
 *
 * Optimized update code
 *
 ****************************************************************************/

static NCURSES_INLINE void
GoTo(NCURSES_SP_DCLx int const row, int const col)
{
    TR(TRACE_MOVE, ("GoTo(%p, %d, %d) from (%d, %d)",
		    (void *) SP_PARM, row, col, SP_PARM->_cursrow, SP_PARM->_curscol));

    position_check(SP_PARM, SP_PARM->_cursrow, SP_PARM->_curscol, "GoTo");

    TINFO_MVCUR(NCURSES_SP_ARGx
		SP_PARM->_cursrow,
		SP_PARM->_curscol,
		row, col);
    position_check(SP_PARM, SP_PARM->_cursrow, SP_PARM->_curscol, "GoTo2");
}

static NCURSES_INLINE void
PutAttrChar(NCURSES_SP_DCLx CARG_CH_T ch)
{
    int chlen = 1;
    NCURSES_CH_T my_ch;
#if USE_WIDEC_SUPPORT
    PUTC_DATA;
#endif
    NCURSES_CH_T tilde;
    NCURSES_CH_T attr = CHDEREF(ch);

    TR(TRACE_CHARPUT, ("PutAttrChar(%s) at (%d, %d)",
		       _tracech_t(ch),
		       SP_PARM->_cursrow, SP_PARM->_curscol));
#if USE_WIDEC_SUPPORT
    /*
     * If this is not a valid character, there is nothing more to do.
     */
    if (isWidecExt(CHDEREF(ch))) {
	TR(TRACE_CHARPUT, ("...skip"));
	return;
    }
    /*
     * Determine the number of character cells which the 'ch' value will use
     * on the screen.  It should be at least one.
     */
    if ((chlen = wcwidth(CharOf(CHDEREF(ch)))) <= 0) {
	static const NCURSES_CH_T blank = NewChar(BLANK_TEXT);

	/*
	 * If the character falls into any of these special cases, do
	 * not force the result to a blank:
	 *
	 * a) it is printable (this works around a bug in wcwidth()).
	 * b) use_legacy_coding() has been called to modify the treatment
	 *    of codes 128-255.
	 * c) the acs_map[] has been initialized to allow codes 0-31
	 *    to be rendered.  This supports Linux console's "PC"
	 *    characters.  Codes 128-255 are allowed though this is
	 *    not checked.
	 */
	if (is8bits(CharOf(CHDEREF(ch)))
	    && (isprint(CharOf(CHDEREF(ch)))
		|| (SP_PARM->_legacy_coding > 0 && CharOf(CHDEREF(ch)) >= 160)
		|| (SP_PARM->_legacy_coding > 1 && CharOf(CHDEREF(ch)) >= 128)
		|| (AttrOf(attr) & A_ALTCHARSET
		    && ((CharOfD(ch) < ACS_LEN
			 && SP_PARM->_acs_map != 0
			 && SP_PARM->_acs_map[CharOfD(ch)] != 0)
			|| (CharOfD(ch) >= 128))))) {
	    ;
	} else {
	    ch = CHREF(blank);
	    TR(TRACE_CHARPUT, ("forced to blank"));
	}
	chlen = 1;
    }
#endif

    if ((AttrOf(attr) & A_ALTCHARSET)
	&& SP_PARM->_acs_map != 0
	&& CharOfD(ch) < ACS_LEN) {
	my_ch = CHDEREF(ch);	/* work around const param */
#if USE_WIDEC_SUPPORT
	/*
	 * This is crude & ugly, but works most of the time.  It checks if the
	 * acs_chars string specified that we have a mapping for this
	 * character, and uses the wide-character mapping when we expect the
	 * normal one to be broken (by mis-design ;-).
	 */
	if (SP_PARM->_screen_acs_fix
	    && SP_PARM->_screen_acs_map[CharOf(my_ch)]) {
	    RemAttr(attr, A_ALTCHARSET);
	    my_ch = _nc_wacs[CharOf(my_ch)];
	} else if (SP_PARM->_screen_unicode
		   && !SP_PARM->_screen_acs_map[CharOf(my_ch)]
		   && _nc_wacs[CharOf(my_ch)].chars[0]) {
	    RemAttr(attr, A_ALTCHARSET);
	    my_ch = _nc_wacs[CharOf(my_ch)];
	}
#endif
	/*
	 * If we (still) have alternate character set, it is the normal 8bit
	 * flavor.  The _screen_acs_map[] array tells if the character was
	 * really in acs_chars, needed because of the way wide/normal line
	 * drawing flavors are integrated.
	 */
	if (AttrOf(attr) & A_ALTCHARSET) {
	    int j = CharOfD(ch);
	    chtype temp = UChar(SP_PARM->_acs_map[j]);

	    if (temp != 0) {
		SetChar(my_ch, temp, AttrOf(attr));
	    } else {
		my_ch = CHDEREF(ch);
		RemAttr(attr, A_ALTCHARSET);
	    }
	}
	ch = CHREF(my_ch);
    }
    if (tilde_glitch && (CharOfD(ch) == L('~'))) {
	SetChar(tilde, L('`'), AttrOf(attr));
	ch = CHREF(tilde);
    }

    UpdateAttrs(SP_PARM, attr);
    PUTC(CHDEREF(ch));
#if !USE_WIDEC_SUPPORT
    COUNT_OUTCHARS(1);
#endif
    SP_PARM->_curscol += chlen;
    if (char_padding) {
	NCURSES_PUTP2("char_padding", char_padding);
    }
}

static bool
check_pending(NCURSES_SP_DCL0)
/* check for pending input */
{
    bool have_pending = FALSE;

    /*
     * Only carry out this check when the flag is zero, otherwise we'll
     * have the refreshing slow down drastically (or stop) if there's an
     * unread character available.
     */
    if (SP_PARM->_fifohold != 0)
	return FALSE;

    if (SP_PARM->_checkfd >= 0) {
#if USE_FUNC_POLL
	struct pollfd fds[1];
	fds[0].fd = SP_PARM->_checkfd;
	fds[0].events = POLLIN;
	if (poll(fds, (size_t) 1, 0) > 0) {
	    have_pending = TRUE;
	}
#elif defined(__BEOS__)
	/*
	 * BeOS's select() is declared in socket.h, so the configure script does
	 * not see it.  That's just as well, since that function works only for
	 * sockets.  This (using snooze and ioctl) was distilled from Be's patch
	 * for ncurses which uses a separate thread to simulate select().
	 *
	 * FIXME: the return values from the ioctl aren't very clear if we get
	 * interrupted.
	 */
	int n = 0;
	int howmany = ioctl(0, 'ichr', &n);
	if (howmany >= 0 && n > 0) {
	    have_pending = TRUE;
	}
#elif HAVE_SELECT
	fd_set fdset;
	struct timeval ktimeout;

	ktimeout.tv_sec =
	    ktimeout.tv_usec = 0;

	FD_ZERO(&fdset);
	FD_SET(SP_PARM->_checkfd, &fdset);
	if (select(SP_PARM->_checkfd + 1, &fdset, NULL, NULL, &ktimeout) != 0) {
	    have_pending = TRUE;
	}
#endif
    }
    if (have_pending) {
	SP_PARM->_fifohold = 5;
	NCURSES_SP_NAME(_nc_flush) (NCURSES_SP_ARG);
    }
    return FALSE;
}

/* put char at lower right corner */
static void
PutCharLR(NCURSES_SP_DCLx const ARG_CH_T ch)
{
    if (!auto_right_margin) {
	/* we can put the char directly */
	PutAttrChar(NCURSES_SP_ARGx ch);
    } else if (enter_am_mode && exit_am_mode) {
	/* we can suppress automargin */
	NCURSES_PUTP2("exit_am_mode", exit_am_mode);

	PutAttrChar(NCURSES_SP_ARGx ch);
	SP_PARM->_curscol--;
	position_check(SP_PARM,
		       SP_PARM->_cursrow,
		       SP_PARM->_curscol,
		       "exit_am_mode");

	NCURSES_PUTP2("enter_am_mode", enter_am_mode);
    } else if ((enter_insert_mode && exit_insert_mode)
	       || insert_character || parm_ich) {
	GoTo(NCURSES_SP_ARGx
	     screen_lines(SP_PARM) - 1,
	     screen_columns(SP_PARM) - 2);
	PutAttrChar(NCURSES_SP_ARGx ch);
	GoTo(NCURSES_SP_ARGx
	     screen_lines(SP_PARM) - 1,
	     screen_columns(SP_PARM) - 2);
	InsStr(NCURSES_SP_ARGx
	       NewScreen(SP_PARM)->_line[screen_lines(SP_PARM) - 1].text +
	       screen_columns(SP_PARM) - 2, 1);
    }
}

/*
 * Wrap the cursor position, i.e., advance to the beginning of the next line.
 */
static void
wrap_cursor(NCURSES_SP_DCL0)
{
    if (eat_newline_glitch) {
	/*
	 * xenl can manifest two different ways.  The vt100 way is that, when
	 * you'd expect the cursor to wrap, it stays hung at the right margin
	 * (on top of the character just emitted) and doesn't wrap until the
	 * *next* graphic char is emitted.  The c100 way is to ignore LF
	 * received just after an am wrap.
	 *
	 * An aggressive way to handle this would be to emit CR/LF after the
	 * char and then assume the wrap is done, you're on the first position
	 * of the next line, and the terminal out of its weird state.  Here
	 * it's safe to just tell the code that the cursor is in hyperspace and
	 * let the next mvcur() call straighten things out.
	 */
	SP_PARM->_curscol = -1;
	SP_PARM->_cursrow = -1;
    } else if (auto_right_margin) {
	SP_PARM->_curscol = 0;
	SP_PARM->_cursrow++;
	/*
	 * We've actually moved - but may have to work around problems with
	 * video attributes not working.
	 */
	if (!move_standout_mode && AttrOf(SCREEN_ATTRS(SP_PARM))) {
	    TR(TRACE_CHARPUT, ("turning off (%#lx) %s before wrapping",
			       (unsigned long) AttrOf(SCREEN_ATTRS(SP_PARM)),
			       _traceattr(AttrOf(SCREEN_ATTRS(SP_PARM)))));
	    (void) VIDATTR(SP_PARM, A_NORMAL, 0);
	}
    } else {
	SP_PARM->_curscol--;
    }
    position_check(SP_PARM,
		   SP_PARM->_cursrow,
		   SP_PARM->_curscol,
		   "wrap_cursor");
}

static NCURSES_INLINE void
PutChar(NCURSES_SP_DCLx const ARG_CH_T ch)
/* insert character, handling automargin stuff */
{
    if (SP_PARM->_cursrow == screen_lines(SP_PARM) - 1 &&
	SP_PARM->_curscol == screen_columns(SP_PARM) - 1) {
	PutCharLR(NCURSES_SP_ARGx ch);
    } else {
	PutAttrChar(NCURSES_SP_ARGx ch);
    }

    if (SP_PARM->_curscol >= screen_columns(SP_PARM))
	wrap_cursor(NCURSES_SP_ARG);

    position_check(SP_PARM, SP_PARM->_cursrow, SP_PARM->_curscol, "PutChar");
}

/*
 * Check whether the given character can be output by clearing commands.  This
 * includes test for being a space and not including any 'bad' attributes, such
 * as A_REVERSE.  All attribute flags which don't affect appearance of a space
 * or can be output by clearing (A_COLOR in case of bce-terminal) are excluded.
 */
static NCURSES_INLINE bool
can_clear_with(NCURSES_SP_DCLx ARG_CH_T ch)
{
    if (!back_color_erase && SP_PARM->_coloron) {
#if NCURSES_EXT_FUNCS
	int pair;

	if (!SP_PARM->_default_color)
	    return FALSE;
	if (SP_PARM->_default_fg != C_MASK || SP_PARM->_default_bg != C_MASK)
	    return FALSE;
	if ((pair = GetPair(CHDEREF(ch))) != 0) {
	    NCURSES_COLOR_T fg, bg;
	    if (NCURSES_SP_NAME(pair_content) (NCURSES_SP_ARGx
					       (short) pair,
					       &fg, &bg) == ERR
		|| (fg != C_MASK || bg != C_MASK)) {
		return FALSE;
	    }
	}
#else
	if (AttrOfD(ch) & A_COLOR)
	    return FALSE;
#endif
    }
    return (ISBLANK(CHDEREF(ch)) &&
	    (AttrOfD(ch) & ~(NONBLANK_ATTR | A_COLOR)) == BLANK_ATTR);
}

/*
 * Issue a given span of characters from an array.
 * Must be functionally equivalent to:
 *	for (i = 0; i < num; i++)
 *	    PutChar(ntext[i]);
 * but can leave the cursor positioned at the middle of the interval.
 *
 * Returns: 0 - cursor is at the end of interval
 *	    1 - cursor is somewhere in the middle
 *
 * This code is optimized using ech and rep.
 */
static int
EmitRange(NCURSES_SP_DCLx const NCURSES_CH_T * ntext, int num)
{
    int i;

    TR(TRACE_CHARPUT, ("EmitRange %d:%s", num, _nc_viscbuf(ntext, num)));

    if (erase_chars || repeat_char) {
	while (num > 0) {
	    int runcount;
	    NCURSES_CH_T ntext0;

	    while (num > 1 && !CharEq(ntext[0], ntext[1])) {
		PutChar(NCURSES_SP_ARGx CHREF(ntext[0]));
		ntext++;
		num--;
	    }
	    ntext0 = ntext[0];
	    if (num == 1) {
		PutChar(NCURSES_SP_ARGx CHREF(ntext0));
		return 0;
	    }
	    runcount = 2;

	    while (runcount < num && CharEq(ntext[runcount], ntext0))
		runcount++;

	    /*
	     * The cost expression in the middle isn't exactly right.
	     * _cup_ch_cost is an upper bound on the cost for moving to the
	     * end of the erased area, but not the cost itself (which we
	     * can't compute without emitting the move).  This may result
	     * in erase_chars not getting used in some situations for
	     * which it would be marginally advantageous.
	     */
	    if (erase_chars
		&& runcount > SP_PARM->_ech_cost + SP_PARM->_cup_ch_cost
		&& can_clear_with(NCURSES_SP_ARGx CHREF(ntext0))) {
		UpdateAttrs(SP_PARM, ntext0);
		NCURSES_PUTP2("erase_chars", TPARM_1(erase_chars, runcount));

		/*
		 * If this is the last part of the given interval,
		 * don't bother moving cursor, since it can be the
		 * last update on the line.
		 */
		if (runcount < num) {
		    GoTo(NCURSES_SP_ARGx
			 SP_PARM->_cursrow,
			 SP_PARM->_curscol + runcount);
		} else {
		    return 1;	/* cursor stays in the middle */
		}
	    } else if (repeat_char && runcount > SP_PARM->_rep_cost) {
		bool wrap_possible = (SP_PARM->_curscol + runcount >=
				      screen_columns(SP_PARM));
		int rep_count = runcount;

		if (wrap_possible)
		    rep_count--;

		UpdateAttrs(SP_PARM, ntext0);
		NCURSES_SP_NAME(tputs) (NCURSES_SP_ARGx
					TPARM_2(repeat_char,
						CharOf(ntext0),
						rep_count),
					rep_count,
					NCURSES_SP_NAME(_nc_outch));
		SP_PARM->_curscol += rep_count;

		if (wrap_possible)
		    PutChar(NCURSES_SP_ARGx CHREF(ntext0));
	    } else {
		for (i = 0; i < runcount; i++)
		    PutChar(NCURSES_SP_ARGx CHREF(ntext[i]));
	    }
	    ntext += runcount;
	    num -= runcount;
	}
	return 0;
    }

    for (i = 0; i < num; i++)
	PutChar(NCURSES_SP_ARGx CHREF(ntext[i]));
    return 0;
}

/*
 * Output the line in the given range [first .. last]
 *
 * If there's a run of identical characters that's long enough to justify
 * cursor movement, use that also.
 *
 * Returns: same as EmitRange
 */
static int
PutRange(NCURSES_SP_DCLx
	 const NCURSES_CH_T * otext,
	 const NCURSES_CH_T * ntext,
	 int row,
	 int first, int last)
{
    int i, j, same;

    TR(TRACE_CHARPUT, ("PutRange(%p, %p, %p, %d, %d, %d)",
		       (void *) SP_PARM,
		       (const void *) otext,
		       (const void *) ntext,
		       row, first, last));

    if (otext != ntext
	&& (last - first + 1) > SP_PARM->_inline_cost) {
	for (j = first, same = 0; j <= last; j++) {
	    if (!same && isWidecExt(otext[j]))
		continue;
	    if (CharEq(otext[j], ntext[j])) {
		same++;
	    } else {
		if (same > SP_PARM->_inline_cost) {
		    EmitRange(NCURSES_SP_ARGx ntext + first, j - same - first);
		    GoTo(NCURSES_SP_ARGx row, first = j);
		}
		same = 0;
	    }
	}
	i = EmitRange(NCURSES_SP_ARGx ntext + first, j - same - first);
	/*
	 * Always return 1 for the next GoTo() after a PutRange() if we found
	 * identical characters at end of interval
	 */
	return (same == 0 ? i : 1);
    }
    return EmitRange(NCURSES_SP_ARGx ntext + first, last - first + 1);
}

/* leave unbracketed here so 'indent' works */
#define MARK_NOCHANGE(win,row) \
		win->_line[row].firstchar = _NOCHANGE; \
		win->_line[row].lastchar = _NOCHANGE; \
		if_USE_SCROLL_HINTS(win->_line[row].oldindex = row)

NCURSES_EXPORT(int)
TINFO_DOUPDATE(NCURSES_SP_DCL0)
{
    int i;
    int nonempty;
#if USE_TRACE_TIMES
    struct tms before, after;
#endif /* USE_TRACE_TIMES */

    T((T_CALLED("_nc_tinfo:doupdate(%p)"), (void *) SP_PARM));

    if (SP_PARM == 0)
	returnCode(ERR);

#if !USE_REENTRANT
    /*
     * It is "legal" but unlikely that an application could assign a new
     * value to one of the standard windows.  Check for that possibility
     * and try to recover.
     *
     * We do not allow applications to assign new values in the reentrant
     * model.
     */
#define SyncScreens(internal,exported) \
	if (internal == 0) internal = exported; \
	if (internal != exported) exported = internal

    SyncScreens(CurScreen(SP_PARM), curscr);
    SyncScreens(NewScreen(SP_PARM), newscr);
    SyncScreens(StdScreen(SP_PARM), stdscr);
#endif

    if (CurScreen(SP_PARM) == 0
	|| NewScreen(SP_PARM) == 0
	|| StdScreen(SP_PARM) == 0)
	returnCode(ERR);

#ifdef TRACE
    if (USE_TRACEF(TRACE_UPDATE)) {
	if (CurScreen(SP_PARM)->_clear)
	    _tracef("curscr is clear");
	else
	    _tracedump("curscr", CurScreen(SP_PARM));
	_tracedump("newscr", NewScreen(SP_PARM));
	_nc_unlock_global(tracef);
    }
#endif /* TRACE */

    _nc_signal_handler(FALSE);

    if (SP_PARM->_fifohold)
	SP_PARM->_fifohold--;

#if USE_SIZECHANGE
    if (SP_PARM->_endwin || _nc_handle_sigwinch(SP_PARM)) {
	/*
	 * This is a transparent extension:  XSI does not address it,
	 * and applications need not know that ncurses can do it.
	 *
	 * Check if the terminal size has changed while curses was off
	 * (this can happen in an xterm, for example), and resize the
	 * ncurses data structures accordingly.
	 */
	_nc_update_screensize(SP_PARM);
    }
#endif

    if (SP_PARM->_endwin) {

	T(("coming back from shell mode"));
	NCURSES_SP_NAME(reset_prog_mode) (NCURSES_SP_ARG);

	NCURSES_SP_NAME(_nc_mvcur_resume) (NCURSES_SP_ARG);
	NCURSES_SP_NAME(_nc_screen_resume) (NCURSES_SP_ARG);
	SP_PARM->_mouse_resume(SP_PARM);

	SP_PARM->_endwin = FALSE;
    }
#if USE_TRACE_TIMES
    /* zero the metering machinery */
    RESET_OUTCHARS();
    (void) times(&before);
#endif /* USE_TRACE_TIMES */

    /*
     * This is the support for magic-cookie terminals.  The theory:  we scan
     * the virtual screen looking for attribute turnons.  Where we find one,
     * check to make sure it's realizable by seeing if the required number of
     * un-attributed blanks are present before and after the attributed range;
     * try to shift the range boundaries over blanks (not changing the screen
     * display) so this becomes true.  If it is, shift the beginning attribute
     * change appropriately (the end one, if we've gotten this far, is
     * guaranteed room for its cookie).  If not, nuke the added attributes out
     * of the span.
     */
#if USE_XMC_SUPPORT
    if (magic_cookie_glitch > 0) {
	int j, k;
	attr_t rattr = A_NORMAL;

	for (i = 0; i < screen_lines(SP_PARM); i++) {
	    for (j = 0; j < screen_columns(SP_PARM); j++) {
		bool failed = FALSE;
		NCURSES_CH_T *thisline = NewScreen(SP_PARM)->_line[i].text;
		attr_t thisattr = AttrOf(thisline[j]) & SP_PARM->_xmc_triggers;
		attr_t turnon = thisattr & ~rattr;

		/* is an attribute turned on here? */
		if (turnon == 0) {
		    rattr = thisattr;
		    continue;
		}

		TR(TRACE_ATTRS, ("At (%d, %d): from %s...", i, j, _traceattr(rattr)));
		TR(TRACE_ATTRS, ("...to %s", _traceattr(turnon)));

		/*
		 * If the attribute change location is a blank with a "safe"
		 * attribute, undo the attribute turnon.  This may ensure
		 * there's enough room to set the attribute before the first
		 * non-blank in the run.
		 */
#define SAFE(scr,a)	(!((a) & (scr)->_xmc_triggers))
		if (ISBLANK(thisline[j]) && SAFE(SP_PARM, turnon)) {
		    RemAttr(thisline[j], turnon);
		    continue;
		}

		/* check that there's enough room at start of span */
		for (k = 1; k <= magic_cookie_glitch; k++) {
		    if (j - k < 0
			|| !ISBLANK(thisline[j - k])
			|| !SAFE(SP_PARM, AttrOf(thisline[j - k]))) {
			failed = TRUE;
			TR(TRACE_ATTRS, ("No room at start in %d,%d%s%s",
					 i, j - k,
					 (ISBLANK(thisline[j - k])
					  ? ""
					  : ":nonblank"),
					 (SAFE(SP_PARM, AttrOf(thisline[j - k]))
					  ? ""
					  : ":unsafe")));
			break;
		    }
		}
		if (!failed) {
		    bool end_onscreen = FALSE;
		    int m, n = j;

		    /* find end of span, if it's onscreen */
		    for (m = i; m < screen_lines(SP_PARM); m++) {
			for (; n < screen_columns(SP_PARM); n++) {
			    attr_t testattr =
			    AttrOf(NewScreen(SP_PARM)->_line[m].text[n]);
			    if ((testattr & SP_PARM->_xmc_triggers) == rattr) {
				end_onscreen = TRUE;
				TR(TRACE_ATTRS,
				   ("Range attributed with %s ends at (%d, %d)",
				    _traceattr(turnon), m, n));
				goto foundit;
			    }
			}
			n = 0;
		    }
		    TR(TRACE_ATTRS,
		       ("Range attributed with %s ends offscreen",
			_traceattr(turnon)));
		  foundit:;

		    if (end_onscreen) {
			NCURSES_CH_T *lastline =
			NewScreen(SP_PARM)->_line[m].text;

			/*
			 * If there are safely-attributed blanks at the end of
			 * the range, shorten the range.  This will help ensure
			 * that there is enough room at end of span.
			 */
			while (n >= 0
			       && ISBLANK(lastline[n])
			       && SAFE(SP_PARM, AttrOf(lastline[n]))) {
			    RemAttr(lastline[n--], turnon);
			}

			/* check that there's enough room at end of span */
			for (k = 1; k <= magic_cookie_glitch; k++) {
			    if (n + k >= screen_columns(SP_PARM)
				|| !ISBLANK(lastline[n + k])
				|| !SAFE(SP_PARM, AttrOf(lastline[n + k]))) {
				failed = TRUE;
				TR(TRACE_ATTRS,
				   ("No room at end in %d,%d%s%s",
				    i, j - k,
				    (ISBLANK(lastline[n + k])
				     ? ""
				     : ":nonblank"),
				    (SAFE(SP_PARM, AttrOf(lastline[n + k]))
				     ? ""
				     : ":unsafe")));
				break;
			    }
			}
		    }
		}

		if (failed) {
		    int p, q = j;

		    TR(TRACE_ATTRS,
		       ("Clearing %s beginning at (%d, %d)",
			_traceattr(turnon), i, j));

		    /* turn off new attributes over span */
		    for (p = i; p < screen_lines(SP_PARM); p++) {
			for (; q < screen_columns(SP_PARM); q++) {
			    attr_t testattr = AttrOf(newscr->_line[p].text[q]);
			    if ((testattr & SP_PARM->_xmc_triggers) == rattr)
				goto foundend;
			    RemAttr(NewScreen(SP_PARM)->_line[p].text[q], turnon);
			}
			q = 0;
		    }
		  foundend:;
		} else {
		    TR(TRACE_ATTRS,
		       ("Cookie space for %s found before (%d, %d)",
			_traceattr(turnon), i, j));

		    /*
		     * Back up the start of range so there's room for cookies
		     * before the first nonblank character.
		     */
		    for (k = 1; k <= magic_cookie_glitch; k++)
			AddAttr(thisline[j - k], turnon);
		}

		rattr = thisattr;
	    }
	}

#ifdef TRACE
	/* show altered highlights after magic-cookie check */
	if (USE_TRACEF(TRACE_UPDATE)) {
	    _tracef("After magic-cookie check...");
	    _tracedump("newscr", NewScreen(SP_PARM));
	    _nc_unlock_global(tracef);
	}
#endif /* TRACE */
    }
#endif /* USE_XMC_SUPPORT */

    nonempty = 0;
    if (CurScreen(SP_PARM)->_clear || NewScreen(SP_PARM)->_clear) {	/* force refresh ? */
	ClrUpdate(NCURSES_SP_ARG);
	CurScreen(SP_PARM)->_clear = FALSE;	/* reset flag */
	NewScreen(SP_PARM)->_clear = FALSE;	/* reset flag */
    } else {
	int changedlines = CHECK_INTERVAL;

	if (check_pending(NCURSES_SP_ARG))
	    goto cleanup;

	nonempty = min(screen_lines(SP_PARM), NewScreen(SP_PARM)->_maxy + 1);

	if (SP_PARM->_scrolling) {
	    NCURSES_SP_NAME(_nc_scroll_optimize) (NCURSES_SP_ARG);
	}

	nonempty = ClrBottom(NCURSES_SP_ARGx nonempty);

	TR(TRACE_UPDATE, ("Transforming lines, nonempty %d", nonempty));
	for (i = 0; i < nonempty; i++) {
	    /*
	     * Here is our line-breakout optimization.
	     */
	    if (changedlines == CHECK_INTERVAL) {
		if (check_pending(NCURSES_SP_ARG))
		    goto cleanup;
		changedlines = 0;
	    }

	    /*
	     * newscr->line[i].firstchar is normally set
	     * by wnoutrefresh.  curscr->line[i].firstchar
	     * is normally set by _nc_scroll_window in the
	     * vertical-movement optimization code,
	     */
	    if (NewScreen(SP_PARM)->_line[i].firstchar != _NOCHANGE
		|| CurScreen(SP_PARM)->_line[i].firstchar != _NOCHANGE) {
		TransformLine(NCURSES_SP_ARGx i);
		changedlines++;
	    }

	    /* mark line changed successfully */
	    if (i <= NewScreen(SP_PARM)->_maxy) {
		MARK_NOCHANGE(NewScreen(SP_PARM), i);
	    }
	    if (i <= CurScreen(SP_PARM)->_maxy) {
		MARK_NOCHANGE(CurScreen(SP_PARM), i);
	    }
	}
    }

    /* put everything back in sync */
    for (i = nonempty; i <= NewScreen(SP_PARM)->_maxy; i++) {
	MARK_NOCHANGE(NewScreen(SP_PARM), i);
    }
    for (i = nonempty; i <= CurScreen(SP_PARM)->_maxy; i++) {
	MARK_NOCHANGE(CurScreen(SP_PARM), i);
    }

    if (!NewScreen(SP_PARM)->_leaveok) {
	CurScreen(SP_PARM)->_curx = NewScreen(SP_PARM)->_curx;
	CurScreen(SP_PARM)->_cury = NewScreen(SP_PARM)->_cury;

	GoTo(NCURSES_SP_ARGx CurScreen(SP_PARM)->_cury, CurScreen(SP_PARM)->_curx);
    }

  cleanup:
    /*
     * We would like to keep the physical screen in normal mode in case we get
     * other processes writing to the screen.  This goal cannot be met for
     * magic cookies since it interferes with attributes that may propagate
     * past the current position.
     */
#if USE_XMC_SUPPORT
    if (magic_cookie_glitch != 0)
#endif
	UpdateAttrs(SP_PARM, normal);

    NCURSES_SP_NAME(_nc_flush) (NCURSES_SP_ARG);
    WINDOW_ATTRS(CurScreen(SP_PARM)) = WINDOW_ATTRS(NewScreen(SP_PARM));

#if USE_TRACE_TIMES
    (void) times(&after);
    TR(TRACE_TIMES,
       ("Update cost: %ld chars, %ld clocks system time, %ld clocks user time",
	_nc_outchars,
	(long) (after.tms_stime - before.tms_stime),
	(long) (after.tms_utime - before.tms_utime)));
#endif /* USE_TRACE_TIMES */

    _nc_signal_handler(TRUE);

    returnCode(OK);
}

#if NCURSES_SP_FUNCS && !defined(USE_TERM_DRIVER)
NCURSES_EXPORT(int)
doupdate(void)
{
    return TINFO_DOUPDATE(CURRENT_SCREEN);
}
#endif

/*
 *	ClrBlank(win)
 *
 *	Returns the attributed character that corresponds to the "cleared"
 *	screen.  If the terminal has the back-color-erase feature, this will be
 *	colored according to the wbkgd() call.
 *
 *	We treat 'curscr' specially because it isn't supposed to be set directly
 *	in the wbkgd() call.  Assume 'stdscr' for this case.
 */
#define BCE_ATTRS (A_NORMAL|A_COLOR)
#define BCE_BKGD(sp,win) (((win) == CurScreen(sp) ? StdScreen(sp) : (win))->_nc_bkgd)

static NCURSES_INLINE NCURSES_CH_T
ClrBlank(NCURSES_SP_DCLx WINDOW *win)
{
    NCURSES_CH_T blank = blankchar;
    if (back_color_erase)
	AddAttr(blank, (AttrOf(BCE_BKGD(SP_PARM, win)) & BCE_ATTRS));
    return blank;
}

/*
**	ClrUpdate()
**
**	Update by clearing and redrawing the entire screen.
**
*/

static void
ClrUpdate(NCURSES_SP_DCL0)
{
    TR(TRACE_UPDATE, (T_CALLED("ClrUpdate")));
    if (0 != SP_PARM) {
	int i;
	NCURSES_CH_T blank = ClrBlank(NCURSES_SP_ARGx StdScreen(SP_PARM));
	int nonempty = min(screen_lines(SP_PARM),
			   NewScreen(SP_PARM)->_maxy + 1);

	ClearScreen(NCURSES_SP_ARGx blank);

	TR(TRACE_UPDATE, ("updating screen from scratch"));

	nonempty = ClrBottom(NCURSES_SP_ARGx nonempty);

	for (i = 0; i < nonempty; i++)
	    TransformLine(NCURSES_SP_ARGx i);
    }
    TR(TRACE_UPDATE, (T_RETURN("")));
}

/*
**	ClrToEOL(blank)
**
**	Clear to end of current line, starting at the cursor position
*/

static void
ClrToEOL(NCURSES_SP_DCLx NCURSES_CH_T blank, int needclear)
{
    int j;

    if (CurScreen(SP_PARM) != 0
	&& SP_PARM->_cursrow >= 0) {
	for (j = SP_PARM->_curscol; j < screen_columns(SP_PARM); j++) {
	    if (j >= 0) {
		NCURSES_CH_T *cp =
		&(CurScreen(SP_PARM)->_line[SP_PARM->_cursrow].text[j]);

		if (!CharEq(*cp, blank)) {
		    *cp = blank;
		    needclear = TRUE;
		}
	    }
	}
    }

    if (needclear) {
	UpdateAttrs(SP_PARM, blank);
	if (clr_eol && SP_PARM->_el_cost <= (screen_columns(SP_PARM) - SP_PARM->_curscol)) {
	    NCURSES_PUTP2("clr_eol", clr_eol);
	} else {
	    int count = (screen_columns(SP_PARM) - SP_PARM->_curscol);
	    while (count-- > 0)
		PutChar(NCURSES_SP_ARGx CHREF(blank));
	}
    }
}

/*
**	ClrToEOS(blank)
**
**	Clear to end of screen, starting at the cursor position
*/

static void
ClrToEOS(NCURSES_SP_DCLx NCURSES_CH_T blank)
{
    int row, col;

    row = SP_PARM->_cursrow;
    col = SP_PARM->_curscol;

    if (row < 0)
	row = 0;
    if (col < 0)
	col = 0;

    UpdateAttrs(SP_PARM, blank);
    TPUTS_TRACE("clr_eos");
    NCURSES_SP_NAME(tputs) (NCURSES_SP_ARGx
			    clr_eos,
			    screen_lines(SP_PARM) - row,
			    NCURSES_SP_NAME(_nc_outch));

    while (col < screen_columns(SP_PARM))
	CurScreen(SP_PARM)->_line[row].text[col++] = blank;

    for (row++; row < screen_lines(SP_PARM); row++) {
	for (col = 0; col < screen_columns(SP_PARM); col++)
	    CurScreen(SP_PARM)->_line[row].text[col] = blank;
    }
}

/*
 *	ClrBottom(total)
 *
 *	Test if clearing the end of the screen would satisfy part of the
 *	screen-update.  Do this by scanning backwards through the lines in the
 *	screen, checking if each is blank, and one or more are changed.
 */
static int
ClrBottom(NCURSES_SP_DCLx int total)
{
    int row;
    int col;
    int top = total;
    int last = min(screen_columns(SP_PARM), NewScreen(SP_PARM)->_maxx + 1);
    NCURSES_CH_T blank = NewScreen(SP_PARM)->_line[total - 1].text[last - 1];
    bool ok;

    if (clr_eos && can_clear_with(NCURSES_SP_ARGx CHREF(blank))) {

	for (row = total - 1; row >= 0; row--) {
	    for (col = 0, ok = TRUE; ok && col < last; col++) {
		ok = (CharEq(NewScreen(SP_PARM)->_line[row].text[col], blank));
	    }
	    if (!ok)
		break;

	    for (col = 0; ok && col < last; col++) {
		ok = (CharEq(CurScreen(SP_PARM)->_line[row].text[col], blank));
	    }
	    if (!ok)
		top = row;
	}

	/* don't use clr_eos for just one line if clr_eol available */
	if (top < total) {
	    GoTo(NCURSES_SP_ARGx top, 0);
	    ClrToEOS(NCURSES_SP_ARGx blank);
	    if (SP_PARM->oldhash && SP_PARM->newhash) {
		for (row = top; row < screen_lines(SP_PARM); row++)
		    SP_PARM->oldhash[row] = SP_PARM->newhash[row];
	    }
	}
    }
    return top;
}

#if USE_XMC_SUPPORT
#if USE_WIDEC_SUPPORT
#define check_xmc_transition(sp, a, b)					\
    ((((a)->attr ^ (b)->attr) & ~((a)->attr) & (sp)->_xmc_triggers) != 0)
#define xmc_turn_on(sp,a,b) check_xmc_transition(sp,&(a), &(b))
#else
#define xmc_turn_on(sp,a,b) ((((a)^(b)) & ~(a) & (sp)->_xmc_triggers) != 0)
#endif

#define xmc_new(sp,r,c) NewScreen(sp)->_line[r].text[c]
#define xmc_turn_off(sp,a,b) xmc_turn_on(sp,b,a)
#endif /* USE_XMC_SUPPORT */

/*
**	TransformLine(lineno)
**
**	Transform the given line in curscr to the one in newscr, using
**	Insert/Delete Character if idcok && has_ic().
**
**		firstChar = position of first different character in line
**		oLastChar = position of last different character in old line
**		nLastChar = position of last different character in new line
**
**		move to firstChar
**		overwrite chars up to min(oLastChar, nLastChar)
**		if oLastChar < nLastChar
**			insert newLine[oLastChar+1..nLastChar]
**		else
**			delete oLastChar - nLastChar spaces
*/

static void
TransformLine(NCURSES_SP_DCLx int const lineno)
{
    int firstChar, oLastChar, nLastChar;
    NCURSES_CH_T *newLine = NewScreen(SP_PARM)->_line[lineno].text;
    NCURSES_CH_T *oldLine = CurScreen(SP_PARM)->_line[lineno].text;
    int n;
    bool attrchanged = FALSE;

    TR(TRACE_UPDATE, (T_CALLED("TransformLine(%p, %d)"), (void *) SP_PARM, lineno));

    /* copy new hash value to old one */
    if (SP_PARM->oldhash && SP_PARM->newhash)
	SP_PARM->oldhash[lineno] = SP_PARM->newhash[lineno];

    /*
     * If we have colors, there is the possibility of having two color pairs
     * that display as the same colors.  For instance, Lynx does this.  Check
     * for this case, and update the old line with the new line's colors when
     * they are equivalent.
     */
    if (SP_PARM->_coloron) {
	int oldPair;
	int newPair;

	for (n = 0; n < screen_columns(SP_PARM); n++) {
	    if (!CharEq(newLine[n], oldLine[n])) {
		oldPair = GetPair(oldLine[n]);
		newPair = GetPair(newLine[n]);
		if (oldPair != newPair
		    && unColor(oldLine[n]) == unColor(newLine[n])) {
		    if (oldPair < SP_PARM->_pair_limit
			&& newPair < SP_PARM->_pair_limit
			&& (SP_PARM->_color_pairs[oldPair] ==
			    SP_PARM->_color_pairs[newPair])) {
			SetPair(oldLine[n], GetPair(newLine[n]));
		    }
		}
	    }
	}
    }

    if (ceol_standout_glitch && clr_eol) {
	firstChar = 0;
	while (firstChar < screen_columns(SP_PARM)) {
	    if (!SameAttrOf(newLine[firstChar], oldLine[firstChar])) {
		attrchanged = TRUE;
		break;
	    }
	    firstChar++;
	}
    }

    firstChar = 0;

    if (attrchanged) {		/* we may have to disregard the whole line */
	GoTo(NCURSES_SP_ARGx lineno, firstChar);
	ClrToEOL(NCURSES_SP_ARGx
		 ClrBlank(NCURSES_SP_ARGx
			  CurScreen(SP_PARM)), FALSE);
	PutRange(NCURSES_SP_ARGx
		 oldLine, newLine, lineno, 0,
		 screen_columns(SP_PARM) - 1);
#if USE_XMC_SUPPORT

	/*
	 * This is a very simple loop to paint characters which may have the
	 * magic cookie glitch embedded.  It doesn't know much about video
	 * attributes which are continued from one line to the next.  It
	 * assumes that we have filtered out requests for attribute changes
	 * that do not get mapped to blank positions.
	 *
	 * FIXME: we are not keeping track of where we put the cookies, so this
	 * will work properly only once, since we may overwrite a cookie in a
	 * following operation.
	 */
    } else if (magic_cookie_glitch > 0) {
	GoTo(NCURSES_SP_ARGx lineno, firstChar);
	for (n = 0; n < screen_columns(SP_PARM); n++) {
	    int m = n + magic_cookie_glitch;

	    /* check for turn-on:
	     * If we are writing an attributed blank, where the
	     * previous cell is not attributed.
	     */
	    if (ISBLANK(newLine[n])
		&& ((n > 0
		     && xmc_turn_on(SP_PARM, newLine[n - 1], newLine[n]))
		    || (n == 0
			&& lineno > 0
			&& xmc_turn_on(SP_PARM,
				       xmc_new(SP_PARM, lineno - 1,
					       screen_columns(SP_PARM) - 1),
				       newLine[n])))) {
		n = m;
	    }

	    PutChar(NCURSES_SP_ARGx CHREF(newLine[n]));

	    /* check for turn-off:
	     * If we are writing an attributed non-blank, where the
	     * next cell is blank, and not attributed.
	     */
	    if (!ISBLANK(newLine[n])
		&& ((n + 1 < screen_columns(SP_PARM)
		     && xmc_turn_off(SP_PARM, newLine[n], newLine[n + 1]))
		    || (n + 1 >= screen_columns(SP_PARM)
			&& lineno + 1 < screen_lines(SP_PARM)
			&& xmc_turn_off(SP_PARM,
					newLine[n],
					xmc_new(SP_PARM, lineno + 1, 0))))) {
		n = m;
	    }

	}
#endif
    } else {
	NCURSES_CH_T blank;

	/* it may be cheap to clear leading whitespace with clr_bol */
	blank = newLine[0];
	if (clr_bol && can_clear_with(NCURSES_SP_ARGx CHREF(blank))) {
	    int oFirstChar, nFirstChar;

	    for (oFirstChar = 0;
		 oFirstChar < screen_columns(SP_PARM);
		 oFirstChar++)
		if (!CharEq(oldLine[oFirstChar], blank))
		    break;
	    for (nFirstChar = 0;
		 nFirstChar < screen_columns(SP_PARM);
		 nFirstChar++)
		if (!CharEq(newLine[nFirstChar], blank))
		    break;

	    if (nFirstChar == oFirstChar) {
		firstChar = nFirstChar;
		/* find the first differing character */
		while (firstChar < screen_columns(SP_PARM)
		       && CharEq(newLine[firstChar], oldLine[firstChar]))
		    firstChar++;
	    } else if (oFirstChar > nFirstChar) {
		firstChar = nFirstChar;
	    } else {		/* oFirstChar < nFirstChar */
		firstChar = oFirstChar;
		if (SP_PARM->_el1_cost < nFirstChar - oFirstChar) {
		    if (nFirstChar >= screen_columns(SP_PARM)
			&& SP_PARM->_el_cost <= SP_PARM->_el1_cost) {
			GoTo(NCURSES_SP_ARGx lineno, 0);
			UpdateAttrs(SP_PARM, blank);
			NCURSES_PUTP2("clr_eol", clr_eol);
		    } else {
			GoTo(NCURSES_SP_ARGx lineno, nFirstChar - 1);
			UpdateAttrs(SP_PARM, blank);
			NCURSES_PUTP2("clr_bol", clr_bol);
		    }

		    while (firstChar < nFirstChar)
			oldLine[firstChar++] = blank;
		}
	    }
	} else {
	    /* find the first differing character */
	    while (firstChar < screen_columns(SP_PARM)
		   && CharEq(newLine[firstChar], oldLine[firstChar]))
		firstChar++;
	}
	/* if there wasn't one, we're done */
	if (firstChar >= screen_columns(SP_PARM)) {
	    TR(TRACE_UPDATE, (T_RETURN("")));
	    return;
	}

	blank = newLine[screen_columns(SP_PARM) - 1];

	if (!can_clear_with(NCURSES_SP_ARGx CHREF(blank))) {
	    /* find the last differing character */
	    nLastChar = screen_columns(SP_PARM) - 1;

	    while (nLastChar > firstChar
		   && CharEq(newLine[nLastChar], oldLine[nLastChar]))
		nLastChar--;

	    if (nLastChar >= firstChar) {
		GoTo(NCURSES_SP_ARGx lineno, firstChar);
		PutRange(NCURSES_SP_ARGx
			 oldLine,
			 newLine,
			 lineno,
			 firstChar,
			 nLastChar);
		memcpy(oldLine + firstChar,
		       newLine + firstChar,
		       (unsigned) (nLastChar - firstChar + 1) * sizeof(NCURSES_CH_T));
	    }
	    TR(TRACE_UPDATE, (T_RETURN("")));
	    return;
	}

	/* find last non-blank character on old line */
	oLastChar = screen_columns(SP_PARM) - 1;
	while (oLastChar > firstChar && CharEq(oldLine[oLastChar], blank))
	    oLastChar--;

	/* find last non-blank character on new line */
	nLastChar = screen_columns(SP_PARM) - 1;
	while (nLastChar > firstChar && CharEq(newLine[nLastChar], blank))
	    nLastChar--;

	if ((nLastChar == firstChar)
	    && (SP_PARM->_el_cost < (oLastChar - nLastChar))) {
	    GoTo(NCURSES_SP_ARGx lineno, firstChar);
	    if (!CharEq(newLine[firstChar], blank))
		PutChar(NCURSES_SP_ARGx CHREF(newLine[firstChar]));
	    ClrToEOL(NCURSES_SP_ARGx blank, FALSE);
	} else if ((nLastChar != oLastChar)
		   && (!CharEq(newLine[nLastChar], oldLine[oLastChar])
		       || !(SP_PARM->_nc_sp_idcok
			    && NCURSES_SP_NAME(has_ic) (NCURSES_SP_ARG)))) {
	    GoTo(NCURSES_SP_ARGx lineno, firstChar);
	    if ((oLastChar - nLastChar) > SP_PARM->_el_cost) {
		if (PutRange(NCURSES_SP_ARGx
			     oldLine,
			     newLine,
			     lineno,
			     firstChar,
			     nLastChar)) {
		    GoTo(NCURSES_SP_ARGx lineno, nLastChar + 1);
		}
		ClrToEOL(NCURSES_SP_ARGx blank, FALSE);
	    } else {
		n = max(nLastChar, oLastChar);
		PutRange(NCURSES_SP_ARGx
			 oldLine,
			 newLine,
			 lineno,
			 firstChar,
			 n);
	    }
	} else {
	    int nLastNonblank = nLastChar;
	    int oLastNonblank = oLastChar;

	    /* find the last characters that really differ */
	    /* can be -1 if no characters differ */
	    while (CharEq(newLine[nLastChar], oldLine[oLastChar])) {
		/* don't split a wide char */
		if (isWidecExt(newLine[nLastChar]) &&
		    !CharEq(newLine[nLastChar - 1], oldLine[oLastChar - 1]))
		    break;
		nLastChar--;
		oLastChar--;
		if (nLastChar == -1 || oLastChar == -1)
		    break;
	    }

	    n = min(oLastChar, nLastChar);
	    if (n >= firstChar) {
		GoTo(NCURSES_SP_ARGx lineno, firstChar);
		PutRange(NCURSES_SP_ARGx
			 oldLine,
			 newLine,
			 lineno,
			 firstChar,
			 n);
	    }

	    if (oLastChar < nLastChar) {
		int m = max(nLastNonblank, oLastNonblank);
#if USE_WIDEC_SUPPORT
		while (isWidecExt(newLine[n + 1]) && n) {
		    --n;
		    --oLastChar;
		}
#endif
		GoTo(NCURSES_SP_ARGx lineno, n + 1);
		if ((nLastChar < nLastNonblank)
		    || InsCharCost(SP_PARM, nLastChar - oLastChar) > (m - n)) {
		    PutRange(NCURSES_SP_ARGx
			     oldLine,
			     newLine,
			     lineno,
			     n + 1,
			     m);
		} else {
		    InsStr(NCURSES_SP_ARGx &newLine[n + 1], nLastChar - oLastChar);
		}
	    } else if (oLastChar > nLastChar) {
		GoTo(NCURSES_SP_ARGx lineno, n + 1);
		if (DelCharCost(SP_PARM, oLastChar - nLastChar)
		    > SP_PARM->_el_cost + nLastNonblank - (n + 1)) {
		    if (PutRange(NCURSES_SP_ARGx oldLine, newLine, lineno,
				 n + 1, nLastNonblank))
			  GoTo(NCURSES_SP_ARGx lineno, nLastNonblank + 1);
		    ClrToEOL(NCURSES_SP_ARGx blank, FALSE);
		} else {
		    /*
		     * The delete-char sequence will
		     * effectively shift in blanks from the
		     * right margin of the screen.  Ensure
		     * that they are the right color by
		     * setting the video attributes from
		     * the last character on the row.
		     */
		    UpdateAttrs(SP_PARM, blank);
		    DelChar(NCURSES_SP_ARGx oLastChar - nLastChar);
		}
	    }
	}
    }

    /* update the code's internal representation */
    if (screen_columns(SP_PARM) > firstChar)
	memcpy(oldLine + firstChar,
	       newLine + firstChar,
	       (unsigned) (screen_columns(SP_PARM) - firstChar) * sizeof(NCURSES_CH_T));
    TR(TRACE_UPDATE, (T_RETURN("")));
    return;
}

/*
**	ClearScreen(blank)
**
**	Clear the physical screen and put cursor at home
**
*/

static void
ClearScreen(NCURSES_SP_DCLx NCURSES_CH_T blank)
{
    int i, j;
    bool fast_clear = (clear_screen || clr_eos || clr_eol);

    TR(TRACE_UPDATE, ("ClearScreen() called"));

#if NCURSES_EXT_FUNCS
    if (SP_PARM->_coloron
	&& !SP_PARM->_default_color) {
	NCURSES_SP_NAME(_nc_do_color) (NCURSES_SP_ARGx
				       (short) GET_SCREEN_PAIR(SP_PARM),
				       0,
				       FALSE,
				       NCURSES_SP_NAME(_nc_outch));
	if (!back_color_erase) {
	    fast_clear = FALSE;
	}
    }
#endif

    if (fast_clear) {
	if (clear_screen) {
	    UpdateAttrs(SP_PARM, blank);
	    NCURSES_PUTP2("clear_screen", clear_screen);
	    SP_PARM->_cursrow = SP_PARM->_curscol = 0;
	    position_check(SP_PARM,
			   SP_PARM->_cursrow,
			   SP_PARM->_curscol,
			   "ClearScreen");
	} else if (clr_eos) {
	    SP_PARM->_cursrow = SP_PARM->_curscol = -1;
	    GoTo(NCURSES_SP_ARGx 0, 0);
	    UpdateAttrs(SP_PARM, blank);
	    TPUTS_TRACE("clr_eos");
	    NCURSES_SP_NAME(tputs) (NCURSES_SP_ARGx
				    clr_eos,
				    screen_lines(SP_PARM),
				    NCURSES_SP_NAME(_nc_outch));
	} else if (clr_eol) {
	    SP_PARM->_cursrow = SP_PARM->_curscol = -1;
	    UpdateAttrs(SP_PARM, blank);
	    for (i = 0; i < screen_lines(SP_PARM); i++) {
		GoTo(NCURSES_SP_ARGx i, 0);
		NCURSES_PUTP2("clr_eol", clr_eol);
	    }
	    GoTo(NCURSES_SP_ARGx 0, 0);
	}
    } else {
	UpdateAttrs(SP_PARM, blank);
	for (i = 0; i < screen_lines(SP_PARM); i++) {
	    GoTo(NCURSES_SP_ARGx i, 0);
	    for (j = 0; j < screen_columns(SP_PARM); j++)
		PutChar(NCURSES_SP_ARGx CHREF(blank));
	}
	GoTo(NCURSES_SP_ARGx 0, 0);
    }

    for (i = 0; i < screen_lines(SP_PARM); i++) {
	for (j = 0; j < screen_columns(SP_PARM); j++)
	    CurScreen(SP_PARM)->_line[i].text[j] = blank;
    }

    TR(TRACE_UPDATE, ("screen cleared"));
}

/*
**	InsStr(line, count)
**
**	Insert the count characters pointed to by line.
**
*/

static void
InsStr(NCURSES_SP_DCLx NCURSES_CH_T * line, int count)
{
    TR(TRACE_UPDATE, ("InsStr(%p, %p,%d) called",
		      (void *) SP_PARM,
		      (void *) line, count));

    /* Prefer parm_ich as it has the smallest cost - no need to shift
     * the whole line on each character. */
    /* The order must match that of InsCharCost. */
    if (parm_ich) {
	TPUTS_TRACE("parm_ich");
	NCURSES_SP_NAME(tputs) (NCURSES_SP_ARGx
				TPARM_1(parm_ich, count),
				count,
				NCURSES_SP_NAME(_nc_outch));
	while (count) {
	    PutAttrChar(NCURSES_SP_ARGx CHREF(*line));
	    line++;
	    count--;
	}
    } else if (enter_insert_mode && exit_insert_mode) {
	NCURSES_PUTP2("enter_insert_mode", enter_insert_mode);
	while (count) {
	    PutAttrChar(NCURSES_SP_ARGx CHREF(*line));
	    if (insert_padding) {
		NCURSES_PUTP2("insert_padding", insert_padding);
	    }
	    line++;
	    count--;
	}
	NCURSES_PUTP2("exit_insert_mode", exit_insert_mode);
    } else {
	while (count) {
	    NCURSES_PUTP2("insert_character", insert_character);
	    PutAttrChar(NCURSES_SP_ARGx CHREF(*line));
	    if (insert_padding) {
		NCURSES_PUTP2("insert_padding", insert_padding);
	    }
	    line++;
	    count--;
	}
    }
    position_check(SP_PARM, SP_PARM->_cursrow, SP_PARM->_curscol, "InsStr");
}

/*
**	DelChar(count)
**
**	Delete count characters at current position
**
*/

static void
DelChar(NCURSES_SP_DCLx int count)
{
    int n;

    TR(TRACE_UPDATE, ("DelChar(%p, %d) called, position = (%ld,%ld)",
		      (void *) SP_PARM, count,
		      (long) NewScreen(SP_PARM)->_cury,
		      (long) NewScreen(SP_PARM)->_curx));

    if (parm_dch) {
	TPUTS_TRACE("parm_dch");
	NCURSES_SP_NAME(tputs) (NCURSES_SP_ARGx
				TPARM_1(parm_dch, count),
				count,
				NCURSES_SP_NAME(_nc_outch));
    } else {
	for (n = 0; n < count; n++) {
	    NCURSES_PUTP2("delete_character", delete_character);
	}
    }
}

/*
 * Physical-scrolling support
 *
 * This code was adapted from Keith Bostic's hardware scrolling
 * support for 4.4BSD curses.  I (esr) translated it to use terminfo
 * capabilities, narrowed the call interface slightly, and cleaned
 * up some convoluted tests.  I also added support for the memory_above
 * memory_below, and non_dest_scroll_region capabilities.
 *
 * For this code to work, we must have either
 * change_scroll_region and scroll forward/reverse commands, or
 * insert and delete line capabilities.
 * When the scrolling region has been set, the cursor has to
 * be at the last line of the region to make the scroll up
 * happen, or on the first line of region to scroll down.
 *
 * This code makes one aesthetic decision in the opposite way from
 * BSD curses.  BSD curses preferred pairs of il/dl operations
 * over scrolls, allegedly because il/dl looked faster.  We, on
 * the other hand, prefer scrolls because (a) they're just as fast
 * on many terminals and (b) using them avoids bouncing an
 * unchanged bottom section of the screen up and down, which is
 * visually nasty.
 *
 * (lav): added more cases, used dl/il when bot==maxy and in csr case.
 *
 * I used assumption that capabilities il/il1/dl/dl1 work inside
 * changed scroll region not shifting screen contents outside of it.
 * If there are any terminals behaving different way, it would be
 * necessary to add some conditions to scroll_csr_forward/backward.
 */

/* Try to scroll up assuming given csr (miny, maxy). Returns ERR on failure */
static int
scroll_csr_forward(NCURSES_SP_DCLx
		   int n,
		   int top,
		   int bot,
		   int miny,
		   int maxy,
		   NCURSES_CH_T blank)
{
    int i;

    if (n == 1 && scroll_forward && top == miny && bot == maxy) {
	GoTo(NCURSES_SP_ARGx bot, 0);
	UpdateAttrs(SP_PARM, blank);
	NCURSES_PUTP2("scroll_forward", scroll_forward);
    } else if (n == 1 && delete_line && bot == maxy) {
	GoTo(NCURSES_SP_ARGx top, 0);
	UpdateAttrs(SP_PARM, blank);
	NCURSES_PUTP2("delete_line", delete_line);
    } else if (parm_index && top == miny && bot == maxy) {
	GoTo(NCURSES_SP_ARGx bot, 0);
	UpdateAttrs(SP_PARM, blank);
	TPUTS_TRACE("parm_index");
	NCURSES_SP_NAME(tputs) (NCURSES_SP_ARGx
				TPARM_2(parm_index, n, 0),
				n,
				NCURSES_SP_NAME(_nc_outch));
    } else if (parm_delete_line && bot == maxy) {
	GoTo(NCURSES_SP_ARGx top, 0);
	UpdateAttrs(SP_PARM, blank);
	TPUTS_TRACE("parm_delete_line");
	NCURSES_SP_NAME(tputs) (NCURSES_SP_ARGx
				TPARM_2(parm_delete_line, n, 0),
				n,
				NCURSES_SP_NAME(_nc_outch));
    } else if (scroll_forward && top == miny && bot == maxy) {
	GoTo(NCURSES_SP_ARGx bot, 0);
	UpdateAttrs(SP_PARM, blank);
	for (i = 0; i < n; i++) {
	    NCURSES_PUTP2("scroll_forward", scroll_forward);
	}
    } else if (delete_line && bot == maxy) {
	GoTo(NCURSES_SP_ARGx top, 0);
	UpdateAttrs(SP_PARM, blank);
	for (i = 0; i < n; i++) {
	    NCURSES_PUTP2("delete_line", delete_line);
	}
    } else
	return ERR;

#if NCURSES_EXT_FUNCS
    if (FILL_BCE(SP_PARM)) {
	int j;
	for (i = 0; i < n; i++) {
	    GoTo(NCURSES_SP_ARGx bot - i, 0);
	    for (j = 0; j < screen_columns(SP_PARM); j++)
		PutChar(NCURSES_SP_ARGx CHREF(blank));
	}
    }
#endif
    return OK;
}

/* Try to scroll down assuming given csr (miny, maxy). Returns ERR on failure */
/* n > 0 */
static int
scroll_csr_backward(NCURSES_SP_DCLx
		    int n,
		    int top,
		    int bot,
		    int miny,
		    int maxy,
		    NCURSES_CH_T blank)
{
    int i;

    if (n == 1 && scroll_reverse && top == miny && bot == maxy) {
	GoTo(NCURSES_SP_ARGx top, 0);
	UpdateAttrs(SP_PARM, blank);
	NCURSES_PUTP2("scroll_reverse", scroll_reverse);
    } else if (n == 1 && insert_line && bot == maxy) {
	GoTo(NCURSES_SP_ARGx top, 0);
	UpdateAttrs(SP_PARM, blank);
	NCURSES_PUTP2("insert_line", insert_line);
    } else if (parm_rindex && top == miny && bot == maxy) {
	GoTo(NCURSES_SP_ARGx top, 0);
	UpdateAttrs(SP_PARM, blank);
	TPUTS_TRACE("parm_rindex");
	NCURSES_SP_NAME(tputs) (NCURSES_SP_ARGx
				TPARM_2(parm_rindex, n, 0),
				n,
				NCURSES_SP_NAME(_nc_outch));
    } else if (parm_insert_line && bot == maxy) {
	GoTo(NCURSES_SP_ARGx top, 0);
	UpdateAttrs(SP_PARM, blank);
	TPUTS_TRACE("parm_insert_line");
	NCURSES_SP_NAME(tputs) (NCURSES_SP_ARGx
				TPARM_2(parm_insert_line, n, 0),
				n,
				NCURSES_SP_NAME(_nc_outch));
    } else if (scroll_reverse && top == miny && bot == maxy) {
	GoTo(NCURSES_SP_ARGx top, 0);
	UpdateAttrs(SP_PARM, blank);
	for (i = 0; i < n; i++) {
	    NCURSES_PUTP2("scroll_reverse", scroll_reverse);
	}
    } else if (insert_line && bot == maxy) {
	GoTo(NCURSES_SP_ARGx top, 0);
	UpdateAttrs(SP_PARM, blank);
	for (i = 0; i < n; i++) {
	    NCURSES_PUTP2("insert_line", insert_line);
	}
    } else
	return ERR;

#if NCURSES_EXT_FUNCS
    if (FILL_BCE(SP_PARM)) {
	int j;
	for (i = 0; i < n; i++) {
	    GoTo(NCURSES_SP_ARGx top + i, 0);
	    for (j = 0; j < screen_columns(SP_PARM); j++)
		PutChar(NCURSES_SP_ARGx CHREF(blank));
	}
    }
#endif
    return OK;
}

/* scroll by using delete_line at del and insert_line at ins */
/* n > 0 */
static int
scroll_idl(NCURSES_SP_DCLx int n, int del, int ins, NCURSES_CH_T blank)
{
    int i;

    if (!((parm_delete_line || delete_line) && (parm_insert_line || insert_line)))
	return ERR;

    GoTo(NCURSES_SP_ARGx del, 0);
    UpdateAttrs(SP_PARM, blank);
    if (n == 1 && delete_line) {
	NCURSES_PUTP2("delete_line", delete_line);
    } else if (parm_delete_line) {
	TPUTS_TRACE("parm_delete_line");
	NCURSES_SP_NAME(tputs) (NCURSES_SP_ARGx
				TPARM_2(parm_delete_line, n, 0),
				n,
				NCURSES_SP_NAME(_nc_outch));
    } else {			/* if (delete_line) */
	for (i = 0; i < n; i++) {
	    NCURSES_PUTP2("delete_line", delete_line);
	}
    }

    GoTo(NCURSES_SP_ARGx ins, 0);
    UpdateAttrs(SP_PARM, blank);
    if (n == 1 && insert_line) {
	NCURSES_PUTP2("insert_line", insert_line);
    } else if (parm_insert_line) {
	TPUTS_TRACE("parm_insert_line");
	NCURSES_SP_NAME(tputs) (NCURSES_SP_ARGx
				TPARM_2(parm_insert_line, n, 0),
				n,
				NCURSES_SP_NAME(_nc_outch));
    } else {			/* if (insert_line) */
	for (i = 0; i < n; i++) {
	    NCURSES_PUTP2("insert_line", insert_line);
	}
    }

    return OK;
}

/*
 * Note:  some terminals require the cursor to be within the scrolling margins
 * before setting them.  Generally, the cursor must be at the appropriate end
 * of the scrolling margins when issuing an indexing operation (it is not
 * apparent whether it must also be at the left margin; we do this just to be
 * safe).  To make the related cursor movement a little faster, we use the
 * save/restore cursor capabilities if the terminal has them.
 */
NCURSES_EXPORT(int)
NCURSES_SP_NAME(_nc_scrolln) (NCURSES_SP_DCLx
			      int n,
			      int top,
			      int bot,
			      int maxy)
/* scroll region from top to bot by n lines */
{
    NCURSES_CH_T blank;
    int i;
    bool cursor_saved = FALSE;
    int res;

    TR(TRACE_MOVE, ("_nc_scrolln(%p, %d, %d, %d, %d)",
		    (void *) SP_PARM, n, top, bot, maxy));

    if (!IsValidScreen(SP_PARM))
	return (ERR);

    blank = ClrBlank(NCURSES_SP_ARGx StdScreen(SP_PARM));

#if USE_XMC_SUPPORT
    /*
     * If we scroll, we might remove a cookie.
     */
    if (magic_cookie_glitch > 0) {
	return (ERR);
    }
#endif

    if (n > 0) {		/* scroll up (forward) */
	/*
	 * Explicitly clear if stuff pushed off top of region might
	 * be saved by the terminal.
	 */
	res = scroll_csr_forward(NCURSES_SP_ARGx n, top, bot, 0, maxy, blank);

	if (res == ERR && change_scroll_region) {
	    if ((((n == 1 && scroll_forward) || parm_index)
		 && (SP_PARM->_cursrow == bot || SP_PARM->_cursrow == bot - 1))
		&& save_cursor && restore_cursor) {
		cursor_saved = TRUE;
		NCURSES_PUTP2("save_cursor", save_cursor);
	    }
	    NCURSES_PUTP2("change_scroll_region",
			  TPARM_2(change_scroll_region, top, bot));
	    if (cursor_saved) {
		NCURSES_PUTP2("restore_cursor", restore_cursor);
	    } else {
		SP_PARM->_cursrow = SP_PARM->_curscol = -1;
	    }

	    res = scroll_csr_forward(NCURSES_SP_ARGx n, top, bot, top, bot, blank);

	    NCURSES_PUTP2("change_scroll_region",
			  TPARM_2(change_scroll_region, 0, maxy));
	    SP_PARM->_cursrow = SP_PARM->_curscol = -1;
	}

	if (res == ERR && SP_PARM->_nc_sp_idlok)
	    res = scroll_idl(NCURSES_SP_ARGx n, top, bot - n + 1, blank);

	/*
	 * Clear the newly shifted-in text.
	 */
	if (res != ERR
	    && (non_dest_scroll_region || (memory_below && bot == maxy))) {
	    static const NCURSES_CH_T blank2 = NewChar(BLANK_TEXT);
	    if (bot == maxy && clr_eos) {
		GoTo(NCURSES_SP_ARGx bot - n + 1, 0);
		ClrToEOS(NCURSES_SP_ARGx blank2);
	    } else {
		for (i = 0; i < n; i++) {
		    GoTo(NCURSES_SP_ARGx bot - i, 0);
		    ClrToEOL(NCURSES_SP_ARGx blank2, FALSE);
		}
	    }
	}

    } else {			/* (n < 0) - scroll down (backward) */
	res = scroll_csr_backward(NCURSES_SP_ARGx -n, top, bot, 0, maxy, blank);

	if (res == ERR && change_scroll_region) {
	    if (top != 0
		&& (SP_PARM->_cursrow == top ||
		    SP_PARM->_cursrow == top - 1)
		&& save_cursor && restore_cursor) {
		cursor_saved = TRUE;
		NCURSES_PUTP2("save_cursor", save_cursor);
	    }
	    NCURSES_PUTP2("change_scroll_region",
			  TPARM_2(change_scroll_region, top, bot));
	    if (cursor_saved) {
		NCURSES_PUTP2("restore_cursor", restore_cursor);
	    } else {
		SP_PARM->_cursrow = SP_PARM->_curscol = -1;
	    }

	    res = scroll_csr_backward(NCURSES_SP_ARGx
				      -n, top, bot, top, bot, blank);

	    NCURSES_PUTP2("change_scroll_region",
			  TPARM_2(change_scroll_region, 0, maxy));
	    SP_PARM->_cursrow = SP_PARM->_curscol = -1;
	}

	if (res == ERR && SP_PARM->_nc_sp_idlok)
	    res = scroll_idl(NCURSES_SP_ARGx -n, bot + n + 1, top, blank);

	/*
	 * Clear the newly shifted-in text.
	 */
	if (res != ERR
	    && (non_dest_scroll_region || (memory_above && top == 0))) {
	    static const NCURSES_CH_T blank2 = NewChar(BLANK_TEXT);
	    for (i = 0; i < -n; i++) {
		GoTo(NCURSES_SP_ARGx i + top, 0);
		ClrToEOL(NCURSES_SP_ARGx blank2, FALSE);
	    }
	}
    }

    if (res == ERR)
	return (ERR);

    _nc_scroll_window(CurScreen(SP_PARM), n,
		      (NCURSES_SIZE_T) top,
		      (NCURSES_SIZE_T) bot,
		      blank);

    /* shift hash values too - they can be reused */
    NCURSES_SP_NAME(_nc_scroll_oldhash) (NCURSES_SP_ARGx n, top, bot);

    return (OK);
}

#if NCURSES_SP_FUNCS
NCURSES_EXPORT(int)
_nc_scrolln(int n, int top, int bot, int maxy)
{
    return NCURSES_SP_NAME(_nc_scrolln) (CURRENT_SCREEN, n, top, bot, maxy);
}
#endif

NCURSES_EXPORT(void)
NCURSES_SP_NAME(_nc_screen_resume) (NCURSES_SP_DCL0)
{
    assert(SP_PARM);

    /* make sure terminal is in a sane known state */
    SetAttr(SCREEN_ATTRS(SP_PARM), A_NORMAL);
    NewScreen(SP_PARM)->_clear = TRUE;

    /* reset color pairs and definitions */
    if (SP_PARM->_coloron || SP_PARM->_color_defs)
	NCURSES_SP_NAME(_nc_reset_colors) (NCURSES_SP_ARG);

    /* restore user-defined colors, if any */
    if (SP_PARM->_color_defs < 0) {
	int n;
	SP_PARM->_color_defs = -(SP_PARM->_color_defs);
	for (n = 0; n < SP_PARM->_color_defs; ++n) {
	    if (SP_PARM->_color_table[n].init) {
		NCURSES_SP_NAME(init_color) (NCURSES_SP_ARGx
					     (short) n,
					     SP_PARM->_color_table[n].r,
					     SP_PARM->_color_table[n].g,
					     SP_PARM->_color_table[n].b);
	    }
	}
    }

    if (exit_attribute_mode)
	NCURSES_PUTP2("exit_attribute_mode", exit_attribute_mode);
    else {
	/* turn off attributes */
	if (exit_alt_charset_mode)
	    NCURSES_PUTP2("exit_alt_charset_mode", exit_alt_charset_mode);
	if (exit_standout_mode)
	    NCURSES_PUTP2("exit_standout_mode", exit_standout_mode);
	if (exit_underline_mode)
	    NCURSES_PUTP2("exit_underline_mode", exit_underline_mode);
    }
    if (exit_insert_mode)
	NCURSES_PUTP2("exit_insert_mode", exit_insert_mode);
    if (enter_am_mode && exit_am_mode) {
	if (auto_right_margin) {
	    NCURSES_PUTP2("enter_am_mode", enter_am_mode);
	} else {
	    NCURSES_PUTP2("exit_am_mode", exit_am_mode);
	}
    }
}

#if NCURSES_SP_FUNCS
NCURSES_EXPORT(void)
_nc_screen_resume(void)
{
    NCURSES_SP_NAME(_nc_screen_resume) (CURRENT_SCREEN);
}
#endif

NCURSES_EXPORT(void)
NCURSES_SP_NAME(_nc_screen_init) (NCURSES_SP_DCL0)
{
    NCURSES_SP_NAME(_nc_screen_resume) (NCURSES_SP_ARG);
}

#if NCURSES_SP_FUNCS
NCURSES_EXPORT(void)
_nc_screen_init(void)
{
    NCURSES_SP_NAME(_nc_screen_init) (CURRENT_SCREEN);
}
#endif

/* wrap up screen handling */
NCURSES_EXPORT(void)
NCURSES_SP_NAME(_nc_screen_wrap) (NCURSES_SP_DCL0)
{
    if (SP_PARM != 0) {

	UpdateAttrs(SP_PARM, normal);
#if NCURSES_EXT_FUNCS
	if (SP_PARM->_coloron
	    && !SP_PARM->_default_color) {
	    static const NCURSES_CH_T blank = NewChar(BLANK_TEXT);
	    SP_PARM->_default_color = TRUE;
	    NCURSES_SP_NAME(_nc_do_color) (NCURSES_SP_ARGx
					   -1,
					   0,
					   FALSE,
					   NCURSES_SP_NAME(_nc_outch));
	    SP_PARM->_default_color = FALSE;

	    TINFO_MVCUR(NCURSES_SP_ARGx
			SP_PARM->_cursrow,
			SP_PARM->_curscol,
			screen_lines(SP_PARM) - 1,
			0);

	    ClrToEOL(NCURSES_SP_ARGx blank, TRUE);
	}
#endif
	if (SP_PARM->_color_defs) {
	    NCURSES_SP_NAME(_nc_reset_colors) (NCURSES_SP_ARG);
	}
    }
}

#if NCURSES_SP_FUNCS
NCURSES_EXPORT(void)
_nc_screen_wrap(void)
{
    NCURSES_SP_NAME(_nc_screen_wrap) (CURRENT_SCREEN);
}
#endif

#if USE_XMC_SUPPORT
NCURSES_EXPORT(void)
NCURSES_SP_NAME(_nc_do_xmc_glitch) (NCURSES_SP_DCLx attr_t previous)
{
    if (SP_PARM != 0) {
	attr_t chg = XMC_CHANGES(previous ^ AttrOf(SCREEN_ATTRS(SP_PARM)));

	while (chg != 0) {
	    if (chg & 1) {
		SP_PARM->_curscol += magic_cookie_glitch;
		if (SP_PARM->_curscol >= SP_PARM->_columns)
		    wrap_cursor(NCURSES_SP_ARG);
		TR(TRACE_UPDATE, ("bumped to %d,%d after cookie",
				  SP_PARM->_cursrow, SP_PARM->_curscol));
	    }
	    chg >>= 1;
	}
    }
}

#if NCURSES_SP_FUNCS
NCURSES_EXPORT(void)
_nc_do_xmc_glitch(attr_t previous)
{
    NCURSES_SP_NAME(_nc_do_xmc_glitch) (CURRENT_SCREEN, previous);
}
#endif

#endif /* USE_XMC_SUPPORT */
