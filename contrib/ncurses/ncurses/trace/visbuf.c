/****************************************************************************
 * Copyright (c) 2001-2012,2014 Free Software Foundation, Inc.              *
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
 *  Author: Thomas E. Dickey                        1996-on                 *
 *     and: Zeyd M. Ben-Halim <zmbenhal@netcom.com> 1992,1995               *
 *     and: Eric S. Raymond <esr@snark.thyrsus.com>                         *
 ****************************************************************************/

/*
 *	visbuf.c - Tracing/Debugging support routines
 */

#define NEED_NCURSES_CH_T
#include <curses.priv.h>

#include <tic.h>
#include <ctype.h>

MODULE_ID("$Id: visbuf.c,v 1.43 2014/02/23 01:21:08 tom Exp $")

#define NUM_VISBUFS 4

#define NormalLen(len) (size_t) (((size_t)(len) + 1) * 4)
#define WideLen(len)   (size_t) (((size_t)(len) + 1) * 4 * MB_CUR_MAX)

#ifdef TRACE
static const char d_quote[] = StringOf(D_QUOTE);
static const char l_brace[] = StringOf(L_BRACE);
static const char r_brace[] = StringOf(R_BRACE);
#endif

#if USE_STRING_HACKS && HAVE_SNPRINTF
#define VisChar(tp, chr, limit) _nc_vischar(tp, chr, limit)
#define LIMIT_ARG ,size_t limit
#else
#define VisChar(tp, chr, limit) _nc_vischar(tp, chr)
#define LIMIT_ARG		/* nothing */
#endif

static char *
_nc_vischar(char *tp, unsigned c LIMIT_ARG)
{
    if (c == '"' || c == '\\') {
	*tp++ = '\\';
	*tp++ = (char) c;
    } else if (is7bits((int)c) && (isgraph((int)c) || c == ' ')) {
	*tp++ = (char) c;
    } else if (c == '\n') {
	*tp++ = '\\';
	*tp++ = 'n';
    } else if (c == '\r') {
	*tp++ = '\\';
	*tp++ = 'r';
    } else if (c == '\b') {
	*tp++ = '\\';
	*tp++ = 'b';
    } else if (c == '\033') {
	*tp++ = '\\';
	*tp++ = 'e';
    } else if (UChar(c) == 0x7f) {
	*tp++ = '\\';
	*tp++ = '^';
	*tp++ = '?';
    } else if (is7bits(c) && iscntrl(UChar(c))) {
	*tp++ = '\\';
	*tp++ = '^';
	*tp++ = (char) ('@' + c);
    } else {
	_nc_SPRINTF(tp, _nc_SLIMIT(limit)
		    "\\%03lo", (unsigned long) ChCharOf(c));
	tp += strlen(tp);
    }
    *tp = 0;
    return tp;
}

static const char *
_nc_visbuf2n(int bufnum, const char *buf, int len)
{
    const char *vbuf = 0;
    char *tp;
    int c;
    int count;

    if (buf == 0)
	return ("(null)");
    if (buf == CANCELLED_STRING)
	return ("(cancelled)");

    if (len < 0)
	len = (int) strlen(buf);

    count = len;
#ifdef TRACE
    vbuf = tp = _nc_trace_buf(bufnum, NormalLen(len));
#else
    {
	static char *mybuf[NUM_VISBUFS];
	if (bufnum < 0) {
	    for (c = 0; c < NUM_VISBUFS; ++c) {
		FreeAndNull(mybuf[c]);
	    }
	    tp = 0;
	} else {
	    mybuf[bufnum] = typeRealloc(char, NormalLen(len), mybuf[bufnum]);
	    vbuf = tp = mybuf[bufnum];
	}
    }
#endif
    if (tp != 0) {
	*tp++ = D_QUOTE;
	while ((--count >= 0) && (c = *buf++) != '\0') {
	    tp = VisChar(tp, UChar(c), NormalLen(len));
	}
	*tp++ = D_QUOTE;
	*tp = '\0';
    } else {
	vbuf = ("(_nc_visbuf2n failed)");
    }
    return (vbuf);
}

NCURSES_EXPORT(const char *)
_nc_visbuf2(int bufnum, const char *buf)
{
    return _nc_visbuf2n(bufnum, buf, -1);
}

NCURSES_EXPORT(const char *)
_nc_visbuf(const char *buf)
{
    return _nc_visbuf2(0, buf);
}

NCURSES_EXPORT(const char *)
_nc_visbufn(const char *buf, int len)
{
    return _nc_visbuf2n(0, buf, len);
}

#ifdef TRACE
#if USE_WIDEC_SUPPORT

#if defined(USE_TERMLIB)
#define _nc_wchstrlen _my_wchstrlen
static int
_nc_wchstrlen(const cchar_t *s)
{
    int result = 0;
    while (CharOf(s[result]) != L'\0') {
	result++;
    }
    return result;
}
#endif

static const char *
_nc_viswbuf2n(int bufnum, const wchar_t *buf, int len)
{
    const char *vbuf;
    char *tp;
    wchar_t c;
    int count;

    if (buf == 0)
	return ("(null)");

    if (len < 0)
	len = (int) wcslen(buf);

    count = len;
#ifdef TRACE
    vbuf = tp = _nc_trace_buf(bufnum, WideLen(len));
#else
    {
	static char *mybuf[NUM_VISBUFS];
	mybuf[bufnum] = typeRealloc(char, WideLen(len), mybuf[bufnum]);
	vbuf = tp = mybuf[bufnum];
    }
#endif
    if (tp != 0) {
	*tp++ = D_QUOTE;
	while ((--count >= 0) && (c = *buf++) != '\0') {
	    char temp[CCHARW_MAX + 80];
	    int j = wctomb(temp, c), k;
	    if (j <= 0) {
		_nc_SPRINTF(temp, _nc_SLIMIT(sizeof(temp))
			    "\\u%08X", (unsigned) c);
		j = (int) strlen(temp);
	    }
	    for (k = 0; k < j; ++k) {
		tp = VisChar(tp, UChar(temp[k]), WideLen(len));
	    }
	}
	*tp++ = D_QUOTE;
	*tp = '\0';
    } else {
	vbuf = ("(_nc_viswbuf2n failed)");
    }
    return (vbuf);
}

NCURSES_EXPORT(const char *)
_nc_viswbuf2(int bufnum, const wchar_t *buf)
{
    return _nc_viswbuf2n(bufnum, buf, -1);
}

NCURSES_EXPORT(const char *)
_nc_viswbuf(const wchar_t *buf)
{
    return _nc_viswbuf2(0, buf);
}

NCURSES_EXPORT(const char *)
_nc_viswbufn(const wchar_t *buf, int len)
{
    return _nc_viswbuf2n(0, buf, len);
}

/* this special case is used for wget_wstr() */
NCURSES_EXPORT(const char *)
_nc_viswibuf(const wint_t *buf)
{
    static wchar_t *mybuf;
    static unsigned mylen;
    unsigned n;

    for (n = 0; buf[n] != 0; ++n) {
	;			/* empty */
    }
    if (mylen < ++n) {
	mylen = n + 80;
	if (mybuf != 0)
	    mybuf = typeRealloc(wchar_t, mylen, mybuf);
	else
	    mybuf = typeMalloc(wchar_t, mylen);
    }
    if (mybuf != 0) {
	for (n = 0; buf[n] != 0; ++n) {
	    mybuf[n] = (wchar_t) buf[n];
	}
	mybuf[n] = L'\0';
    }

    return _nc_viswbuf2(0, mybuf);
}
#endif /* USE_WIDEC_SUPPORT */

/* use these functions for displaying parts of a line within a window */
NCURSES_EXPORT(const char *)
_nc_viscbuf2(int bufnum, const NCURSES_CH_T * buf, int len)
{
    char *result = _nc_trace_buf(bufnum, (size_t) BUFSIZ);
    int first;
    const char *found;

    if (result != 0) {
#if USE_WIDEC_SUPPORT
	if (len < 0)
	    len = _nc_wchstrlen(buf);
#endif /* USE_WIDEC_SUPPORT */

	/*
	 * Display one or more strings followed by attributes.
	 */
	first = 0;
	while (first < len) {
	    attr_t attr = AttrOf(buf[first]);
	    int last = len - 1;
	    int j;

	    for (j = first + 1; j < len; ++j) {
		if (!SameAttrOf(buf[j], buf[first])) {
		    last = j - 1;
		    break;
		}
	    }

	    (void) _nc_trace_bufcat(bufnum, l_brace);
	    (void) _nc_trace_bufcat(bufnum, d_quote);
	    for (j = first; j <= last; ++j) {
		found = _nc_altcharset_name(attr, (chtype) CharOf(buf[j]));
		if (found != 0) {
		    (void) _nc_trace_bufcat(bufnum, found);
		    attr &= ~A_ALTCHARSET;
		} else
#if USE_WIDEC_SUPPORT
		if (!isWidecExt(buf[j])) {
		    PUTC_DATA;

		    PUTC_INIT;
		    for (PUTC_i = 0; PUTC_i < CCHARW_MAX; ++PUTC_i) {
			int k;

			PUTC_ch = buf[j].chars[PUTC_i];
			if (PUTC_ch == L'\0') {
			    if (PUTC_i == 0)
				(void) _nc_trace_bufcat(bufnum, "\\000");
			    break;
			}
			PUTC_n = (int) wcrtomb(PUTC_buf,
					       buf[j].chars[PUTC_i], &PUT_st);
			if (PUTC_n <= 0)
			    break;
			for (k = 0; k < PUTC_n; k++) {
			    char temp[80];
			    VisChar(temp, UChar(PUTC_buf[k]), sizeof(temp));
			    (void) _nc_trace_bufcat(bufnum, temp);
			}
		    }
		}
#else
		{
		    char temp[80];
		    VisChar(temp, UChar(buf[j]), sizeof(temp));
		    (void) _nc_trace_bufcat(bufnum, temp);
		}
#endif /* USE_WIDEC_SUPPORT */
	    }
	    (void) _nc_trace_bufcat(bufnum, d_quote);
	    if (attr != A_NORMAL) {
		(void) _nc_trace_bufcat(bufnum, " | ");
		(void) _nc_trace_bufcat(bufnum, _traceattr2(bufnum + 20, attr));
	    }
	    result = _nc_trace_bufcat(bufnum, r_brace);
	    first = last + 1;
	}
    }
    return result;
}

NCURSES_EXPORT(const char *)
_nc_viscbuf(const NCURSES_CH_T * buf, int len)
{
    return _nc_viscbuf2(0, buf, len);
}
#endif /* TRACE */
