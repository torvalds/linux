/*	$Id: mandoc.c,v 1.104 2018/07/28 18:34:15 schwarze Exp $ */
/*
 * Copyright (c) 2008-2011, 2014 Kristaps Dzonsons <kristaps@bsd.lv>
 * Copyright (c) 2011-2015, 2017, 2018 Ingo Schwarze <schwarze@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHORS DISCLAIM ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */
#include "config.h"

#include <sys/types.h>

#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "mandoc_aux.h"
#include "mandoc.h"
#include "roff.h"
#include "libmandoc.h"

static	int	 a2time(time_t *, const char *, const char *);
static	char	*time2a(time_t);


enum mandoc_esc
mandoc_escape(const char **end, const char **start, int *sz)
{
	const char	*local_start;
	int		 local_sz;
	char		 term;
	enum mandoc_esc	 gly;

	/*
	 * When the caller doesn't provide return storage,
	 * use local storage.
	 */

	if (NULL == start)
		start = &local_start;
	if (NULL == sz)
		sz = &local_sz;

	/*
	 * Beyond the backslash, at least one input character
	 * is part of the escape sequence.  With one exception
	 * (see below), that character won't be returned.
	 */

	gly = ESCAPE_ERROR;
	*start = ++*end;
	*sz = 0;
	term = '\0';

	switch ((*start)[-1]) {
	/*
	 * First the glyphs.  There are several different forms of
	 * these, but each eventually returns a substring of the glyph
	 * name.
	 */
	case '(':
		gly = ESCAPE_SPECIAL;
		*sz = 2;
		break;
	case '[':
		gly = ESCAPE_SPECIAL;
		term = ']';
		break;
	case 'C':
		if ('\'' != **start)
			return ESCAPE_ERROR;
		*start = ++*end;
		gly = ESCAPE_SPECIAL;
		term = '\'';
		break;

	/*
	 * Escapes taking no arguments at all.
	 */
	case 'd':
	case 'u':
	case ',':
	case '/':
		return ESCAPE_IGNORE;
	case 'p':
		return ESCAPE_BREAK;

	/*
	 * The \z escape is supposed to output the following
	 * character without advancing the cursor position.
	 * Since we are mostly dealing with terminal mode,
	 * let us just skip the next character.
	 */
	case 'z':
		return ESCAPE_SKIPCHAR;

	/*
	 * Handle all triggers matching \X(xy, \Xx, and \X[xxxx], where
	 * 'X' is the trigger.  These have opaque sub-strings.
	 */
	case 'F':
	case 'g':
	case 'k':
	case 'M':
	case 'm':
	case 'n':
	case 'V':
	case 'Y':
		gly = ESCAPE_IGNORE;
		/* FALLTHROUGH */
	case 'f':
		if (ESCAPE_ERROR == gly)
			gly = ESCAPE_FONT;
		switch (**start) {
		case '(':
			*start = ++*end;
			*sz = 2;
			break;
		case '[':
			*start = ++*end;
			term = ']';
			break;
		default:
			*sz = 1;
			break;
		}
		break;

	/*
	 * These escapes are of the form \X'Y', where 'X' is the trigger
	 * and 'Y' is any string.  These have opaque sub-strings.
	 * The \B and \w escapes are handled in roff.c, roff_res().
	 */
	case 'A':
	case 'b':
	case 'D':
	case 'R':
	case 'X':
	case 'Z':
		gly = ESCAPE_IGNORE;
		/* FALLTHROUGH */
	case 'o':
		if (**start == '\0')
			return ESCAPE_ERROR;
		if (gly == ESCAPE_ERROR)
			gly = ESCAPE_OVERSTRIKE;
		term = **start;
		*start = ++*end;
		break;

	/*
	 * These escapes are of the form \X'N', where 'X' is the trigger
	 * and 'N' resolves to a numerical expression.
	 */
	case 'h':
	case 'H':
	case 'L':
	case 'l':
	case 'S':
	case 'v':
	case 'x':
		if (strchr(" %&()*+-./0123456789:<=>", **start)) {
			if ('\0' != **start)
				++*end;
			return ESCAPE_ERROR;
		}
		switch ((*start)[-1]) {
		case 'h':
			gly = ESCAPE_HORIZ;
			break;
		case 'l':
			gly = ESCAPE_HLINE;
			break;
		default:
			gly = ESCAPE_IGNORE;
			break;
		}
		term = **start;
		*start = ++*end;
		break;

	/*
	 * Special handling for the numbered character escape.
	 * XXX Do any other escapes need similar handling?
	 */
	case 'N':
		if ('\0' == **start)
			return ESCAPE_ERROR;
		(*end)++;
		if (isdigit((unsigned char)**start)) {
			*sz = 1;
			return ESCAPE_IGNORE;
		}
		(*start)++;
		while (isdigit((unsigned char)**end))
			(*end)++;
		*sz = *end - *start;
		if ('\0' != **end)
			(*end)++;
		return ESCAPE_NUMBERED;

	/*
	 * Sizes get a special category of their own.
	 */
	case 's':
		gly = ESCAPE_IGNORE;

		/* See +/- counts as a sign. */
		if ('+' == **end || '-' == **end || ASCII_HYPH == **end)
			*start = ++*end;

		switch (**end) {
		case '(':
			*start = ++*end;
			*sz = 2;
			break;
		case '[':
			*start = ++*end;
			term = ']';
			break;
		case '\'':
			*start = ++*end;
			term = '\'';
			break;
		case '3':
		case '2':
		case '1':
			*sz = (*end)[-1] == 's' &&
			    isdigit((unsigned char)(*end)[1]) ? 2 : 1;
			break;
		default:
			*sz = 1;
			break;
		}

		break;

	/*
	 * Anything else is assumed to be a glyph.
	 * In this case, pass back the character after the backslash.
	 */
	default:
		gly = ESCAPE_SPECIAL;
		*start = --*end;
		*sz = 1;
		break;
	}

	assert(ESCAPE_ERROR != gly);

	/*
	 * Read up to the terminating character,
	 * paying attention to nested escapes.
	 */

	if ('\0' != term) {
		while (**end != term) {
			switch (**end) {
			case '\0':
				return ESCAPE_ERROR;
			case '\\':
				(*end)++;
				if (ESCAPE_ERROR ==
				    mandoc_escape(end, NULL, NULL))
					return ESCAPE_ERROR;
				break;
			default:
				(*end)++;
				break;
			}
		}
		*sz = (*end)++ - *start;
	} else {
		assert(*sz > 0);
		if ((size_t)*sz > strlen(*start))
			return ESCAPE_ERROR;
		*end += *sz;
	}

	/* Run post-processors. */

	switch (gly) {
	case ESCAPE_FONT:
		if (2 == *sz) {
			if ('C' == **start) {
				/*
				 * Treat constant-width font modes
				 * just like regular font modes.
				 */
				(*start)++;
				(*sz)--;
			} else {
				if ('B' == (*start)[0] && 'I' == (*start)[1])
					gly = ESCAPE_FONTBI;
				break;
			}
		} else if (1 != *sz)
			break;

		switch (**start) {
		case '3':
		case 'B':
			gly = ESCAPE_FONTBOLD;
			break;
		case '2':
		case 'I':
			gly = ESCAPE_FONTITALIC;
			break;
		case 'P':
			gly = ESCAPE_FONTPREV;
			break;
		case '1':
		case 'R':
			gly = ESCAPE_FONTROMAN;
			break;
		}
		break;
	case ESCAPE_SPECIAL:
		if (1 == *sz && 'c' == **start)
			gly = ESCAPE_NOSPACE;
		/*
		 * Unicode escapes are defined in groff as \[u0000]
		 * to \[u10FFFF], where the contained value must be
		 * a valid Unicode codepoint.  Here, however, only
		 * check the length and range.
		 */
		if (**start != 'u' || *sz < 5 || *sz > 7)
			break;
		if (*sz == 7 && ((*start)[1] != '1' || (*start)[2] != '0'))
			break;
		if (*sz == 6 && (*start)[1] == '0')
			break;
		if (*sz == 5 && (*start)[1] == 'D' &&
		    strchr("89ABCDEF", (*start)[2]) != NULL)
			break;
		if ((int)strspn(*start + 1, "0123456789ABCDEFabcdef")
		    + 1 == *sz)
			gly = ESCAPE_UNICODE;
		break;
	default:
		break;
	}

	return gly;
}

/*
 * Parse a quoted or unquoted roff-style request or macro argument.
 * Return a pointer to the parsed argument, which is either the original
 * pointer or advanced by one byte in case the argument is quoted.
 * NUL-terminate the argument in place.
 * Collapse pairs of quotes inside quoted arguments.
 * Advance the argument pointer to the next argument,
 * or to the NUL byte terminating the argument line.
 */
char *
mandoc_getarg(struct mparse *parse, char **cpp, int ln, int *pos)
{
	char	 *start, *cp;
	int	  quoted, pairs, white;

	/* Quoting can only start with a new word. */
	start = *cpp;
	quoted = 0;
	if ('"' == *start) {
		quoted = 1;
		start++;
	}

	pairs = 0;
	white = 0;
	for (cp = start; '\0' != *cp; cp++) {

		/*
		 * Move the following text left
		 * after quoted quotes and after "\\" and "\t".
		 */
		if (pairs)
			cp[-pairs] = cp[0];

		if ('\\' == cp[0]) {
			/*
			 * In copy mode, translate double to single
			 * backslashes and backslash-t to literal tabs.
			 */
			switch (cp[1]) {
			case 't':
				cp[0] = '\t';
				/* FALLTHROUGH */
			case '\\':
				pairs++;
				cp++;
				break;
			case ' ':
				/* Skip escaped blanks. */
				if (0 == quoted)
					cp++;
				break;
			default:
				break;
			}
		} else if (0 == quoted) {
			if (' ' == cp[0]) {
				/* Unescaped blanks end unquoted args. */
				white = 1;
				break;
			}
		} else if ('"' == cp[0]) {
			if ('"' == cp[1]) {
				/* Quoted quotes collapse. */
				pairs++;
				cp++;
			} else {
				/* Unquoted quotes end quoted args. */
				quoted = 2;
				break;
			}
		}
	}

	/* Quoted argument without a closing quote. */
	if (1 == quoted)
		mandoc_msg(MANDOCERR_ARG_QUOTE, parse, ln, *pos, NULL);

	/* NUL-terminate this argument and move to the next one. */
	if (pairs)
		cp[-pairs] = '\0';
	if ('\0' != *cp) {
		*cp++ = '\0';
		while (' ' == *cp)
			cp++;
	}
	*pos += (int)(cp - start) + (quoted ? 1 : 0);
	*cpp = cp;

	if ('\0' == *cp && (white || ' ' == cp[-1]))
		mandoc_msg(MANDOCERR_SPACE_EOL, parse, ln, *pos, NULL);

	return start;
}

static int
a2time(time_t *t, const char *fmt, const char *p)
{
	struct tm	 tm;
	char		*pp;

	memset(&tm, 0, sizeof(struct tm));

	pp = NULL;
#if HAVE_STRPTIME
	pp = strptime(p, fmt, &tm);
#endif
	if (NULL != pp && '\0' == *pp) {
		*t = mktime(&tm);
		return 1;
	}

	return 0;
}

static char *
time2a(time_t t)
{
	struct tm	*tm;
	char		*buf, *p;
	size_t		 ssz;
	int		 isz;

	tm = localtime(&t);
	if (tm == NULL)
		return NULL;

	/*
	 * Reserve space:
	 * up to 9 characters for the month (September) + blank
	 * up to 2 characters for the day + comma + blank
	 * 4 characters for the year and a terminating '\0'
	 */

	p = buf = mandoc_malloc(10 + 4 + 4 + 1);

	if ((ssz = strftime(p, 10 + 1, "%B ", tm)) == 0)
		goto fail;
	p += (int)ssz;

	/*
	 * The output format is just "%d" here, not "%2d" or "%02d".
	 * That's also the reason why we can't just format the
	 * date as a whole with "%B %e, %Y" or "%B %d, %Y".
	 * Besides, the present approach is less prone to buffer
	 * overflows, in case anybody should ever introduce the bug
	 * of looking at LC_TIME.
	 */

	if ((isz = snprintf(p, 4 + 1, "%d, ", tm->tm_mday)) == -1)
		goto fail;
	p += isz;

	if (strftime(p, 4 + 1, "%Y", tm) == 0)
		goto fail;
	return buf;

fail:
	free(buf);
	return NULL;
}

char *
mandoc_normdate(struct roff_man *man, char *in, int ln, int pos)
{
	char		*cp;
	time_t		 t;

	/* No date specified: use today's date. */

	if (in == NULL || *in == '\0' || strcmp(in, "$" "Mdocdate$") == 0) {
		mandoc_msg(MANDOCERR_DATE_MISSING, man->parse, ln, pos, NULL);
		return time2a(time(NULL));
	}

	/* Valid mdoc(7) date format. */

	if (a2time(&t, "$" "Mdocdate: %b %d %Y $", in) ||
	    a2time(&t, "%b %d, %Y", in)) {
		cp = time2a(t);
		if (t > time(NULL) + 86400)
			mandoc_msg(MANDOCERR_DATE_FUTURE, man->parse,
			    ln, pos, cp);
		else if (*in != '$' && strcmp(in, cp) != 0)
			mandoc_msg(MANDOCERR_DATE_NORM, man->parse,
			    ln, pos, cp);
		return cp;
	}

	/* In man(7), do not warn about the legacy format. */

	if (a2time(&t, "%Y-%m-%d", in) == 0)
		mandoc_msg(MANDOCERR_DATE_BAD, man->parse, ln, pos, in);
	else if (t > time(NULL) + 86400)
		mandoc_msg(MANDOCERR_DATE_FUTURE, man->parse, ln, pos, in);
	else if (man->macroset == MACROSET_MDOC)
		mandoc_vmsg(MANDOCERR_DATE_LEGACY, man->parse,
		    ln, pos, "Dd %s", in);

	/* Use any non-mdoc(7) date verbatim. */

	return mandoc_strdup(in);
}

int
mandoc_eos(const char *p, size_t sz)
{
	const char	*q;
	int		 enclosed, found;

	if (0 == sz)
		return 0;

	/*
	 * End-of-sentence recognition must include situations where
	 * some symbols, such as `)', allow prior EOS punctuation to
	 * propagate outward.
	 */

	enclosed = found = 0;
	for (q = p + (int)sz - 1; q >= p; q--) {
		switch (*q) {
		case '\"':
		case '\'':
		case ']':
		case ')':
			if (0 == found)
				enclosed = 1;
			break;
		case '.':
		case '!':
		case '?':
			found = 1;
			break;
		default:
			return found &&
			    (!enclosed || isalnum((unsigned char)*q));
		}
	}

	return found && !enclosed;
}

/*
 * Convert a string to a long that may not be <0.
 * If the string is invalid, or is less than 0, return -1.
 */
int
mandoc_strntoi(const char *p, size_t sz, int base)
{
	char		 buf[32];
	char		*ep;
	long		 v;

	if (sz > 31)
		return -1;

	memcpy(buf, p, sz);
	buf[(int)sz] = '\0';

	errno = 0;
	v = strtol(buf, &ep, base);

	if (buf[0] == '\0' || *ep != '\0')
		return -1;

	if (v > INT_MAX)
		v = INT_MAX;
	if (v < INT_MIN)
		v = INT_MIN;

	return (int)v;
}
