/****************************************************************************
 * Copyright (c) 2005-2010,2012 Free Software Foundation, Inc.              *
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
 *  Author: Thomas Dickey                                                   *
 ****************************************************************************/

#include <curses.priv.h>

#include <ctype.h>

#include <tic.h>

MODULE_ID("$Id: trim_sgr0.c,v 1.15 2012/12/15 20:57:17 tom Exp $")

#undef CUR
#define CUR tp->

#define CSI       233
#define ESC       033		/* ^[ */
#define L_BRACK   '['

static char *
set_attribute_9(TERMTYPE *tp, int flag)
{
    const char *value;
    char *result;

    value = tparm(set_attributes, 0, 0, 0, 0, 0, 0, 0, 0, flag);
    if (PRESENT(value))
	result = strdup(value);
    else
	result = 0;
    return result;
}

static int
is_csi(const char *s)
{
    int result = 0;
    if (s != 0) {
	if (UChar(s[0]) == CSI)
	    result = 1;
	else if (s[0] == ESC && s[1] == L_BRACK)
	    result = 2;
    }
    return result;
}

static char *
skip_zero(char *s)
{
    if (s[0] == '0') {
	if (s[1] == ';')
	    s += 2;
	else if (isalpha(UChar(s[1])))
	    s += 1;
    }
    return s;
}

static const char *
skip_delay(const char *s)
{
    if (s[0] == '$' && s[1] == '<') {
	s += 2;
	while (isdigit(UChar(*s)) || *s == '/')
	    ++s;
	if (*s == '>')
	    ++s;
    }
    return s;
}

/*
 * Improve similar_sgr a little by moving the attr-string from the beginning
 * to the end of the s-string.
 */
static bool
rewrite_sgr(char *s, char *attr)
{
    if (s != 0) {
	if (PRESENT(attr)) {
	    size_t len_s = strlen(s);
	    size_t len_a = strlen(attr);

	    if (len_s > len_a && !strncmp(attr, s, len_a)) {
		unsigned n;
		TR(TRACE_DATABASE, ("rewrite:\n\t%s", s));
		for (n = 0; n < len_s - len_a; ++n) {
		    s[n] = s[n + len_a];
		}
		_nc_STRCPY(s + n, attr, strlen(s) + 1);
		TR(TRACE_DATABASE, ("to:\n\t%s", s));
	    }
	}
	return TRUE;
    }
    return FALSE;		/* oops */
}

static bool
similar_sgr(char *a, char *b)
{
    bool result = FALSE;
    if (a != 0 && b != 0) {
	int csi_a = is_csi(a);
	int csi_b = is_csi(b);
	size_t len_a;
	size_t len_b;

	TR(TRACE_DATABASE, ("similar_sgr:\n\t%s\n\t%s",
			    _nc_visbuf2(1, a),
			    _nc_visbuf2(2, b)));
	if (csi_a != 0 && csi_b != 0 && csi_a == csi_b) {
	    a += csi_a;
	    b += csi_b;
	    if (*a != *b) {
		a = skip_zero(a);
		b = skip_zero(b);
	    }
	}
	len_a = strlen(a);
	len_b = strlen(b);
	if (len_a && len_b) {
	    if (len_a > len_b)
		result = (strncmp(a, b, len_b) == 0);
	    else
		result = (strncmp(a, b, len_a) == 0);
	}
	TR(TRACE_DATABASE, ("...similar_sgr: %d\n\t%s\n\t%s", result,
			    _nc_visbuf2(1, a),
			    _nc_visbuf2(2, b)));
    }
    return result;
}

static unsigned
chop_out(char *string, unsigned i, unsigned j)
{
    TR(TRACE_DATABASE, ("chop_out %d..%d from %s", i, j, _nc_visbuf(string)));
    while (string[j] != '\0') {
	string[i++] = string[j++];
    }
    string[i] = '\0';
    return i;
}

/*
 * Compare, ignoring delays.  Some of the delay values are inconsistent, and
 * we do not want to be stopped by that.
 *
 * Returns the number of chars from 'full' that we matched.  If any mismatch
 * occurs, return zero.
 */
static unsigned
compare_part(const char *part, const char *full)
{
    const char *next_part;
    const char *next_full;
    unsigned used_full = 0;
    unsigned used_delay = 0;

    while (*part != 0) {
	if (*part != *full) {
	    used_full = 0;
	    break;
	}

	/*
	 * Adjust the return-value to allow the rare case of
	 *      string<delay>string
	 * to remove the whole piece.  The most common case is a delay at the
	 * end of the string.  The adjusted string will retain the delay, which
	 * is conservative.
	 */
	if (used_delay != 0) {
	    used_full += used_delay;
	    used_delay = 0;
	}
	if (*part == '$' && *full == '$') {
	    next_part = skip_delay(part);
	    next_full = skip_delay(full);
	    if (next_part != part && next_full != full) {
		used_delay += (unsigned) (next_full - full);
		full = next_full;
		part = next_part;
		continue;
	    }
	}
	++used_full;
	++part;
	++full;
    }
    return used_full;
}

/*
 * While 'sgr0' is the "same" as termcap 'me', there is a compatibility issue. 
 * The sgr/sgr0 capabilities include setting/clearing alternate character set
 * mode.  A termcap application cannot use sgr, so sgr0 strings that reset
 * alternate character set mode will be misinterpreted.  Here, we remove those
 * from the more common ISO/ANSI/VT100 entries, which have sgr0 agreeing with
 * sgr.
 *
 * This function returns the modified sgr0 if it can be modified, a null if
 * an error occurs, or the original sgr0 if no change is needed.
 */
NCURSES_EXPORT(char *)
_nc_trim_sgr0(TERMTYPE *tp)
{
    char *result = exit_attribute_mode;

    T((T_CALLED("_nc_trim_sgr0()")));

    if (PRESENT(exit_attribute_mode)
	&& PRESENT(set_attributes)) {
	bool found = FALSE;
	char *on = set_attribute_9(tp, 1);
	char *off = set_attribute_9(tp, 0);
	char *end = strdup(exit_attribute_mode);
	char *tmp;
	size_t i, j, k;

	TR(TRACE_DATABASE, ("checking if we can trim sgr0 based on sgr"));
	TR(TRACE_DATABASE, ("sgr0       %s", _nc_visbuf(end)));
	TR(TRACE_DATABASE, ("sgr(9:off) %s", _nc_visbuf(off)));
	TR(TRACE_DATABASE, ("sgr(9:on)  %s", _nc_visbuf(on)));

	if (!rewrite_sgr(on, enter_alt_charset_mode)
	    || !rewrite_sgr(off, exit_alt_charset_mode)
	    || !rewrite_sgr(end, exit_alt_charset_mode)) {
	    FreeIfNeeded(off);
	} else if (similar_sgr(off, end)
		   && !similar_sgr(off, on)) {
	    TR(TRACE_DATABASE, ("adjusting sgr(9:off) : %s", _nc_visbuf(off)));
	    result = off;
	    /*
	     * If rmacs is a substring of sgr(0), remove that chunk.
	     */
	    if (exit_alt_charset_mode != 0) {
		TR(TRACE_DATABASE, ("scan for rmacs %s", _nc_visbuf(exit_alt_charset_mode)));
		j = strlen(off);
		k = strlen(exit_alt_charset_mode);
		if (j > k) {
		    for (i = 0; i <= (j - k); ++i) {
			unsigned k2 = compare_part(exit_alt_charset_mode,
						   off + i);
			if (k2 != 0) {
			    found = TRUE;
			    chop_out(off, (unsigned) i, (unsigned) (i + k2));
			    break;
			}
		    }
		}
	    }
	    /*
	     * SGR 10 would reset to normal font.
	     */
	    if (!found) {
		if ((i = (size_t) is_csi(off)) != 0
		    && off[strlen(off) - 1] == 'm') {
		    TR(TRACE_DATABASE, ("looking for SGR 10 in %s",
					_nc_visbuf(off)));
		    tmp = skip_zero(off + i);
		    if (tmp[0] == '1'
			&& skip_zero(tmp + 1) != tmp + 1) {
			i = (size_t) (tmp - off);
			if (off[i - 1] == ';')
			    i--;
			j = (size_t) (skip_zero(tmp + 1) - off);
			(void) chop_out(off, (unsigned) i, (unsigned) j);
			found = TRUE;
		    }
		}
	    }
	    if (!found
		&& (tmp = strstr(end, off)) != 0
		&& strcmp(end, off) != 0) {
		i = (size_t) (tmp - end);
		j = strlen(off);
		tmp = strdup(end);
		chop_out(tmp, (unsigned) i, (unsigned) j);
		free(off);
		result = tmp;
	    }
	    TR(TRACE_DATABASE, ("...adjusted sgr0 : %s", _nc_visbuf(result)));
	    if (!strcmp(result, exit_attribute_mode)) {
		TR(TRACE_DATABASE, ("...same result, discard"));
		free(result);
		result = exit_attribute_mode;
	    }
	} else {
	    /*
	     * Either the sgr does not reference alternate character set,
	     * or it is incorrect.  That's too hard to decide right now.
	     */
	    free(off);
	}
	FreeIfNeeded(end);
	FreeIfNeeded(on);
    } else {
	/*
	 * Possibly some applications are confused if sgr0 contains rmacs,
	 * but that would be a different bug report -TD
	 */
    }

    returnPtr(result);
}
