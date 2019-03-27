/* $OpenBSD: utf8.c,v 1.8 2018/08/21 13:56:27 schwarze Exp $ */
/*
 * Copyright (c) 2016 Ingo Schwarze <schwarze@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

/*
 * Utility functions for multibyte-character handling,
 * in particular to sanitize untrusted strings for terminal output.
 */

#include "includes.h"

#include <sys/types.h>
#ifdef HAVE_LANGINFO_H
# include <langinfo.h>
#endif
#include <limits.h>
#include <locale.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if defined(HAVE_STRNVIS) && defined(HAVE_VIS_H) && !defined(BROKEN_STRNVIS)
# include <vis.h>
#endif
#ifdef HAVE_WCHAR_H
# include <wchar.h>
#endif

#include "utf8.h"

static int	 dangerous_locale(void);
static int	 grow_dst(char **, size_t *, size_t, char **, size_t);
static int	 vasnmprintf(char **, size_t, int *, const char *, va_list);


/*
 * For US-ASCII and UTF-8 encodings, we can safely recover from
 * encoding errors and from non-printable characters.  For any
 * other encodings, err to the side of caution and abort parsing:
 * For state-dependent encodings, recovery is impossible.
 * For arbitrary encodings, replacement of non-printable
 * characters would be non-trivial and too fragile.
 * The comments indicate what nl_langinfo(CODESET)
 * returns for US-ASCII on various operating systems.
 */

static int
dangerous_locale(void) {
	char	*loc;

	loc = nl_langinfo(CODESET);
	return strcmp(loc, "UTF-8") != 0 &&
	    strcmp(loc, "US-ASCII") != 0 &&		/* OpenBSD */
	    strcmp(loc, "ANSI_X3.4-1968") != 0 &&	/* Linux */
	    strcmp(loc, "ISO8859-1") != 0 &&		/* AIX */
	    strcmp(loc, "646") != 0 &&			/* Solaris, NetBSD */
	    strcmp(loc, "") != 0;			/* Solaris 6 */
}

static int
grow_dst(char **dst, size_t *sz, size_t maxsz, char **dp, size_t need)
{
	char	*tp;
	size_t	 tsz;

	if (*dp + need < *dst + *sz)
		return 0;
	tsz = *sz + 128;
	if (tsz > maxsz)
		tsz = maxsz;
	if ((tp = recallocarray(*dst, *sz, tsz, 1)) == NULL)
		return -1;
	*dp = tp + (*dp - *dst);
	*dst = tp;
	*sz = tsz;
	return 0;
}

/*
 * The following two functions limit the number of bytes written,
 * including the terminating '\0', to sz.  Unless wp is NULL,
 * they limit the number of display columns occupied to *wp.
 * Whichever is reached first terminates the output string.
 * To stay close to the standard interfaces, they return the number of
 * non-NUL bytes that would have been written if both were unlimited.
 * If wp is NULL, newline, carriage return, and tab are allowed;
 * otherwise, the actual number of columns occupied by what was
 * written is returned in *wp.
 */

static int
vasnmprintf(char **str, size_t maxsz, int *wp, const char *fmt, va_list ap)
{
	char	*src;	/* Source string returned from vasprintf. */
	char	*sp;	/* Pointer into src. */
	char	*dst;	/* Destination string to be returned. */
	char	*dp;	/* Pointer into dst. */
	char	*tp;	/* Temporary pointer for dst. */
	size_t	 sz;	/* Number of bytes allocated for dst. */
	wchar_t	 wc;	/* Wide character at sp. */
	int	 len;	/* Number of bytes in the character at sp. */
	int	 ret;	/* Number of bytes needed to format src. */
	int	 width;	/* Display width of the character wc. */
	int	 total_width, max_width, print;

	src = NULL;
	if ((ret = vasprintf(&src, fmt, ap)) <= 0)
		goto fail;

	sz = strlen(src) + 1;
	if ((dst = malloc(sz)) == NULL) {
		free(src);
		ret = -1;
		goto fail;
	}

	if (maxsz > INT_MAX)
		maxsz = INT_MAX;

	sp = src;
	dp = dst;
	ret = 0;
	print = 1;
	total_width = 0;
	max_width = wp == NULL ? INT_MAX : *wp;
	while (*sp != '\0') {
		if ((len = mbtowc(&wc, sp, MB_CUR_MAX)) == -1) {
			(void)mbtowc(NULL, NULL, MB_CUR_MAX);
			if (dangerous_locale()) {
				ret = -1;
				break;
			}
			len = 1;
			width = -1;
		} else if (wp == NULL &&
		    (wc == L'\n' || wc == L'\r' || wc == L'\t')) {
			/*
			 * Don't use width uninitialized; the actual
			 * value doesn't matter because total_width
			 * is only returned for wp != NULL.
			 */
			width = 0;
		} else if ((width = wcwidth(wc)) == -1 &&
		    dangerous_locale()) {
			ret = -1;
			break;
		}

		/* Valid, printable character. */

		if (width >= 0) {
			if (print && (dp - dst >= (int)maxsz - len ||
			    total_width > max_width - width))
				print = 0;
			if (print) {
				if (grow_dst(&dst, &sz, maxsz,
				    &dp, len) == -1) {
					ret = -1;
					break;
				}
				total_width += width;
				memcpy(dp, sp, len);
				dp += len;
			}
			sp += len;
			if (ret >= 0)
				ret += len;
			continue;
		}

		/* Escaping required. */

		while (len > 0) {
			if (print && (dp - dst >= (int)maxsz - 4 ||
			    total_width > max_width - 4))
				print = 0;
			if (print) {
				if (grow_dst(&dst, &sz, maxsz,
				    &dp, 4) == -1) {
					ret = -1;
					break;
				}
				tp = vis(dp, *sp, VIS_OCTAL | VIS_ALL, 0);
				width = tp - dp;
				total_width += width;
				dp = tp;
			} else
				width = 4;
			len--;
			sp++;
			if (ret >= 0)
				ret += width;
		}
		if (len > 0)
			break;
	}
	free(src);
	*dp = '\0';
	*str = dst;
	if (wp != NULL)
		*wp = total_width;

	/*
	 * If the string was truncated by the width limit but
	 * would have fit into the size limit, the only sane way
	 * to report the problem is using the return value, such
	 * that the usual idiom "if (ret < 0 || ret >= sz) error"
	 * works as expected.
	 */

	if (ret < (int)maxsz && !print)
		ret = -1;
	return ret;

fail:
	if (wp != NULL)
		*wp = 0;
	if (ret == 0) {
		*str = src;
		return 0;
	} else {
		*str = NULL;
		return -1;
	}
}

int
snmprintf(char *str, size_t sz, int *wp, const char *fmt, ...)
{
	va_list	 ap;
	char	*cp;
	int	 ret;

	va_start(ap, fmt);
	ret = vasnmprintf(&cp, sz, wp, fmt, ap);
	va_end(ap);
	if (cp != NULL) {
		(void)strlcpy(str, cp, sz);
		free(cp);
	} else
		*str = '\0';
	return ret;
}

/*
 * To stay close to the standard interfaces, the following functions
 * return the number of non-NUL bytes written.
 */

int
vfmprintf(FILE *stream, const char *fmt, va_list ap)
{
	char	*str;
	int	 ret;

	if ((ret = vasnmprintf(&str, INT_MAX, NULL, fmt, ap)) < 0)
		return -1;
	if (fputs(str, stream) == EOF)
		ret = -1;
	free(str);
	return ret;
}

int
fmprintf(FILE *stream, const char *fmt, ...)
{
	va_list	 ap;
	int	 ret;

	va_start(ap, fmt);
	ret = vfmprintf(stream, fmt, ap);
	va_end(ap);
	return ret;
}

int
mprintf(const char *fmt, ...)
{
	va_list	 ap;
	int	 ret;

	va_start(ap, fmt);
	ret = vfmprintf(stdout, fmt, ap);
	va_end(ap);
	return ret;
}

/*
 * Set up libc for multibyte output in the user's chosen locale.
 *
 * XXX: we are known to have problems with Turkish (i/I confusion) so we
 *      deliberately fall back to the C locale for now. Longer term we should
 *      always prefer to select C.[encoding] if possible, but there's no
 *      standardisation in locales between systems, so we'll need to survey
 *      what's out there first.
 */
void
msetlocale(void)
{
	const char *vars[] = { "LC_ALL", "LC_CTYPE", "LANG", NULL };
	char *cp;
	int i;

	/*
	 * We can't yet cope with dotless/dotted I in Turkish locales,
	 * so fall back to the C locale for these.
	 */
	for (i = 0; vars[i] != NULL; i++) {
		if ((cp = getenv(vars[i])) == NULL)
			continue;
		if (strncasecmp(cp, "TR", 2) != 0)
			break;
		/*
		 * If we're in a UTF-8 locale then prefer to use
		 * the C.UTF-8 locale (or equivalent) if it exists.
		 */
		if ((strcasestr(cp, "UTF-8") != NULL ||
		    strcasestr(cp, "UTF8") != NULL) &&
		    (setlocale(LC_CTYPE, "C.UTF-8") != NULL ||
		    setlocale(LC_CTYPE, "POSIX.UTF-8") != NULL))
			return;
		setlocale(LC_CTYPE, "C");
		return;
	}
	/* We can handle this locale */
	setlocale(LC_CTYPE, "");
}
