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
 *  Author: Thomas Dickey                           1996-on                 *
 *     and: Zeyd M. Ben-Halim <zmbenhal@netcom.com> 1992,1995               *
 *     and: Eric S. Raymond <esr@snark.thyrsus.com>                         *
 *     and: Juergen Pfeifer                                                 *
 ****************************************************************************/

/*
 *	lib_traceatr.c - Tracing/Debugging routines (attributes)
 */

#include <curses.priv.h>

#ifndef CUR
#define CUR SP_TERMTYPE
#endif

MODULE_ID("$Id: lib_traceatr.c,v 1.81 2014/02/01 22:09:27 tom Exp $")

#define COLOR_OF(c) ((c < 0) ? "default" : (c > 7 ? color_of(c) : colors[c].name))

#define TRACE_BUF_SIZE(num) (_nc_globals.tracebuf_ptr[num].size)
#define COLOR_BUF_SIZE(num) (sizeof(my_buffer[num]))

#ifdef TRACE

static const char l_brace[] = StringOf(L_BRACE);
static const char r_brace[] = StringOf(R_BRACE);

#ifndef USE_TERMLIB

#define my_buffer _nc_globals.traceatr_color_buf
#define my_select _nc_globals.traceatr_color_sel
#define my_cached _nc_globals.traceatr_color_last

static char *
color_of(int c)
{
    if (c != my_cached) {
	my_cached = c;
	my_select = !my_select;
	if (c == COLOR_DEFAULT)
	    _nc_STRCPY(my_buffer[my_select], "default",
		       COLOR_BUF_SIZE(my_select));
	else
	    _nc_SPRINTF(my_buffer[my_select],
			_nc_SLIMIT(COLOR_BUF_SIZE(my_select))
			"color%d", c);
    }
    return my_buffer[my_select];
}

#undef my_buffer
#undef my_select
#endif /* !USE_TERMLIB */

NCURSES_EXPORT(char *)
_traceattr2(int bufnum, chtype newmode)
{
    static const struct {
	unsigned int val;
	const char *name;
    } names[] =
    {
	/* *INDENT-OFF* */
	{ A_STANDOUT,		"A_STANDOUT" },
	{ A_UNDERLINE,		"A_UNDERLINE" },
	{ A_REVERSE,		"A_REVERSE" },
	{ A_BLINK,		"A_BLINK" },
	{ A_DIM,		"A_DIM" },
	{ A_BOLD,		"A_BOLD" },
	{ A_ALTCHARSET,		"A_ALTCHARSET" },
	{ A_INVIS,		"A_INVIS" },
	{ A_PROTECT,		"A_PROTECT" },
	{ A_CHARTEXT,		"A_CHARTEXT" },
	{ A_NORMAL,		"A_NORMAL" },
	{ A_COLOR,		"A_COLOR" },
#if USE_ITALIC
	{ A_ITALIC,		"A_ITALIC" },
#endif
	/* *INDENT-ON* */

    }
#ifndef USE_TERMLIB
    ,
	colors[] =
    {
	/* *INDENT-OFF* */
	{ COLOR_BLACK,		"COLOR_BLACK" },
	{ COLOR_RED,		"COLOR_RED" },
	{ COLOR_GREEN,		"COLOR_GREEN" },
	{ COLOR_YELLOW,		"COLOR_YELLOW" },
	{ COLOR_BLUE,		"COLOR_BLUE" },
	{ COLOR_MAGENTA,	"COLOR_MAGENTA" },
	{ COLOR_CYAN,		"COLOR_CYAN" },
	{ COLOR_WHITE,		"COLOR_WHITE" },
	/* *INDENT-ON* */

    }
#endif /* !USE_TERMLIB */
    ;
    size_t n;
    char temp[80];
    char *result = _nc_trace_buf(bufnum, (size_t) BUFSIZ);

    if (result != 0) {
	unsigned save_nc_tracing = _nc_tracing;

	_nc_tracing = 0;

	_nc_STRCPY(result, l_brace, TRACE_BUF_SIZE(bufnum));

	for (n = 0; n < SIZEOF(names); n++) {
	    if ((newmode & names[n].val) != 0) {
		if (result[1] != '\0')
		    (void) _nc_trace_bufcat(bufnum, "|");
		result = _nc_trace_bufcat(bufnum, names[n].name);

		if (names[n].val == A_COLOR) {
		    short pairnum = (short) PairNumber(newmode);
#ifdef USE_TERMLIB
		    /* pair_content lives in libncurses */
		    _nc_SPRINTF(temp, _nc_SLIMIT(sizeof(temp))
				"{%d}", pairnum);
#else
		    NCURSES_COLOR_T fg, bg;

		    if (pair_content(pairnum, &fg, &bg) == OK) {
			_nc_SPRINTF(temp, _nc_SLIMIT(sizeof(temp))
				    "{%d = {%s, %s}}",
				    pairnum,
				    COLOR_OF(fg),
				    COLOR_OF(bg));
		    } else {
			_nc_SPRINTF(temp, _nc_SLIMIT(sizeof(temp))
				    "{%d}", pairnum);
		    }
#endif
		    result = _nc_trace_bufcat(bufnum, temp);
		}
	    }
	}
	if (ChAttrOf(newmode) == A_NORMAL) {
	    if (result != 0 && result[1] != '\0')
		(void) _nc_trace_bufcat(bufnum, "|");
	    (void) _nc_trace_bufcat(bufnum, "A_NORMAL");
	}

	_nc_tracing = save_nc_tracing;
	result = _nc_trace_bufcat(bufnum, r_brace);
    }
    return result;
}

NCURSES_EXPORT(char *)
_traceattr(attr_t newmode)
{
    return _traceattr2(0, newmode);
}

/* Trace 'int' return-values */
NCURSES_EXPORT(int)
_nc_retrace_int_attr_t(attr_t code)
{
    T((T_RETURN("%s"), _traceattr(code)));
    return (int) code;
}

/* Trace 'attr_t' return-values */
NCURSES_EXPORT(attr_t)
_nc_retrace_attr_t(attr_t code)
{
    T((T_RETURN("%s"), _traceattr(code)));
    return code;
}

const char *
_nc_altcharset_name(attr_t attr, chtype ch)
{
    typedef struct {
	unsigned int val;
	const char *name;
    } ALT_NAMES;
#if NCURSES_SP_FUNCS
    SCREEN *sp = CURRENT_SCREEN;
#endif
    static const ALT_NAMES names[] =
    {
	{'l', "ACS_ULCORNER"},	/* upper left corner */
	{'m', "ACS_LLCORNER"},	/* lower left corner */
	{'k', "ACS_URCORNER"},	/* upper right corner */
	{'j', "ACS_LRCORNER"},	/* lower right corner */
	{'t', "ACS_LTEE"},	/* tee pointing right */
	{'u', "ACS_RTEE"},	/* tee pointing left */
	{'v', "ACS_BTEE"},	/* tee pointing up */
	{'w', "ACS_TTEE"},	/* tee pointing down */
	{'q', "ACS_HLINE"},	/* horizontal line */
	{'x', "ACS_VLINE"},	/* vertical line */
	{'n', "ACS_PLUS"},	/* large plus or crossover */
	{'o', "ACS_S1"},	/* scan line 1 */
	{'s', "ACS_S9"},	/* scan line 9 */
	{'`', "ACS_DIAMOND"},	/* diamond */
	{'a', "ACS_CKBOARD"},	/* checker board (stipple) */
	{'f', "ACS_DEGREE"},	/* degree symbol */
	{'g', "ACS_PLMINUS"},	/* plus/minus */
	{'~', "ACS_BULLET"},	/* bullet */
	{',', "ACS_LARROW"},	/* arrow pointing left */
	{'+', "ACS_RARROW"},	/* arrow pointing right */
	{'.', "ACS_DARROW"},	/* arrow pointing down */
	{'-', "ACS_UARROW"},	/* arrow pointing up */
	{'h', "ACS_BOARD"},	/* board of squares */
	{'i', "ACS_LANTERN"},	/* lantern symbol */
	{'0', "ACS_BLOCK"},	/* solid square block */
	{'p', "ACS_S3"},	/* scan line 3 */
	{'r', "ACS_S7"},	/* scan line 7 */
	{'y', "ACS_LEQUAL"},	/* less/equal */
	{'z', "ACS_GEQUAL"},	/* greater/equal */
	{'{', "ACS_PI"},	/* Pi */
	{'|', "ACS_NEQUAL"},	/* not equal */
	{'}', "ACS_STERLING"},	/* UK pound sign */
	{'\0', (char *) 0}
    };

    const char *result = 0;

#if NCURSES_SP_FUNCS
    (void) sp;
#endif
    if (SP_PARM != 0 && (attr & A_ALTCHARSET) && (acs_chars != 0)) {
	char *cp;
	char *found = 0;
	const ALT_NAMES *strp;

	for (cp = acs_chars; cp[0] && cp[1]; cp += 2) {
	    if (ChCharOf(UChar(cp[1])) == ChCharOf(ch)) {
		found = cp;
		/* don't exit from loop - there may be redefinitions */
	    }
	}

	if (found != 0) {
	    ch = ChCharOf(UChar(*found));
	    for (strp = names; strp->val; strp++)
		if (strp->val == ch) {
		    result = strp->name;
		    break;
		}
	}
    }
    return result;
}

NCURSES_EXPORT(char *)
_tracechtype2(int bufnum, chtype ch)
{
    const char *found;
    char *result = _nc_trace_buf(bufnum, (size_t) BUFSIZ);

    if (result != 0) {
	_nc_STRCPY(result, l_brace, TRACE_BUF_SIZE(bufnum));
	if ((found = _nc_altcharset_name(ChAttrOf(ch), ch)) != 0) {
	    (void) _nc_trace_bufcat(bufnum, found);
	} else
	    (void) _nc_trace_bufcat(bufnum,
				    _nc_tracechar(CURRENT_SCREEN,
						  (int) ChCharOf(ch)));

	if (ChAttrOf(ch) != A_NORMAL) {
	    (void) _nc_trace_bufcat(bufnum, " | ");
	    (void) _nc_trace_bufcat(bufnum,
				    _traceattr2(bufnum + 20, ChAttrOf(ch)));
	}

	result = _nc_trace_bufcat(bufnum, r_brace);
    }
    return result;
}

NCURSES_EXPORT(char *)
_tracechtype(chtype ch)
{
    return _tracechtype2(0, ch);
}

/* Trace 'chtype' return-values */
NCURSES_EXPORT(chtype)
_nc_retrace_chtype(chtype code)
{
    T((T_RETURN("%s"), _tracechtype(code)));
    return code;
}

#if USE_WIDEC_SUPPORT
NCURSES_EXPORT(char *)
_tracecchar_t2(int bufnum, const cchar_t *ch)
{
    char *result = _nc_trace_buf(bufnum, (size_t) BUFSIZ);
    attr_t attr;
    const char *found;

    if (result != 0) {
	_nc_STRCPY(result, l_brace, TRACE_BUF_SIZE(bufnum));
	if (ch != 0) {
	    attr = AttrOfD(ch);
	    if ((found = _nc_altcharset_name(attr, (chtype) CharOfD(ch))) != 0) {
		(void) _nc_trace_bufcat(bufnum, found);
		attr &= ~A_ALTCHARSET;
	    } else if (isWidecExt(CHDEREF(ch))) {
		(void) _nc_trace_bufcat(bufnum, "{NAC}");
		attr &= ~A_CHARTEXT;
	    } else {
		PUTC_DATA;
		int n;

		PUTC_INIT;
		(void) _nc_trace_bufcat(bufnum, "{ ");
		for (PUTC_i = 0; PUTC_i < CCHARW_MAX; ++PUTC_i) {
		    PUTC_ch = ch->chars[PUTC_i];
		    if (PUTC_ch == L'\0') {
			if (PUTC_i == 0)
			    (void) _nc_trace_bufcat(bufnum, "\\000");
			break;
		    }
		    PUTC_n = (int) wcrtomb(PUTC_buf, ch->chars[PUTC_i], &PUT_st);
		    if (PUTC_n <= 0) {
			if (PUTC_ch != L'\0') {
			    /* it could not be a multibyte sequence */
			    (void) _nc_trace_bufcat(bufnum,
						    _nc_tracechar(CURRENT_SCREEN,
								  UChar(ch->chars[PUTC_i])));
			}
			break;
		    }
		    for (n = 0; n < PUTC_n; n++) {
			if (n)
			    (void) _nc_trace_bufcat(bufnum, ", ");
			(void) _nc_trace_bufcat(bufnum,
						_nc_tracechar(CURRENT_SCREEN,
							      UChar(PUTC_buf[n])));
		    }
		}
		(void) _nc_trace_bufcat(bufnum, " }");
	    }
	    if (attr != A_NORMAL) {
		(void) _nc_trace_bufcat(bufnum, " | ");
		(void) _nc_trace_bufcat(bufnum, _traceattr2(bufnum + 20, attr));
	    }
	}

	result = _nc_trace_bufcat(bufnum, r_brace);
    }
    return result;
}

NCURSES_EXPORT(char *)
_tracecchar_t(const cchar_t *ch)
{
    return _tracecchar_t2(0, ch);
}
#endif

#else
EMPTY_MODULE(_nc_lib_traceatr)
#endif /* TRACE */
