/*	$NetBSD: vis.c,v 1.37 2008/07/25 22:29:23 dsl Exp $	*/

/*-
 * Copyright (c) 1989, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*-
 * Copyright (c) 1999, 2005 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#if 1
#include <config.h>
#include "roken.h"
#ifndef _DIAGASSERT
#define _DIAGASSERT(X)
#endif
#else /* heimdal */
#include <sys/cdefs.h>
#if defined(LIBC_SCCS) && !defined(lint)
__RCSID("$NetBSD: vis.c,v 1.37 2008/07/25 22:29:23 dsl Exp $");
#endif /* LIBC_SCCS and not lint */

#include "namespace.h"
#endif /* heimdal */

#include <sys/types.h>

#include <assert.h>
#include <ctype.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <vis.h>
#include <stdlib.h>

#if 0
#ifdef __weak_alias
__weak_alias(strsvis,_strsvis)
__weak_alias(strsvisx,_strsvisx)
__weak_alias(strvis,_strvis)
__weak_alias(strvisx,_strvisx)
__weak_alias(svis,_svis)
__weak_alias(vis,_vis)
#endif
#endif

#if !HAVE_VIS || !HAVE_SVIS
#include <ctype.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>

static char *do_svis(char *, int, int, int, const char *);

#undef BELL
#if defined(__STDC__)
#define BELL '\a'
#else
#define BELL '\007'
#endif

ROKEN_LIB_FUNCTION char * ROKEN_LIB_CALL
	rk_vis (char *, int, int, int);
ROKEN_LIB_FUNCTION char * ROKEN_LIB_CALL
	rk_svis (char *, int, int, int, const char *);
ROKEN_LIB_FUNCTION int ROKEN_LIB_CALL
	rk_strvis (char *, const char *, int);
ROKEN_LIB_FUNCTION int ROKEN_LIB_CALL
	rk_strsvis (char *, const char *, int, const char *);
ROKEN_LIB_FUNCTION int ROKEN_LIB_CALL
	rk_strvisx (char *, const char *, size_t, int);
ROKEN_LIB_FUNCTION int ROKEN_LIB_CALL
	rk_strsvisx (char *, const char *, size_t, int, const char *);


#define isoctal(c)	(((u_char)(c)) >= '0' && ((u_char)(c)) <= '7')
#define iswhite(c)	(c == ' ' || c == '\t' || c == '\n')
#define issafe(c)	(c == '\b' || c == BELL || c == '\r')
#define xtoa(c)		"0123456789abcdef"[c]

#define MAXEXTRAS	5

#define MAKEEXTRALIST(flag, extra, orig_str)				      \
do {									      \
	const char *orig = orig_str;					      \
	const char *o = orig;						      \
	char *e;							      \
	while (*o++)							      \
		continue;						      \
	extra = malloc((size_t)((o - orig) + MAXEXTRAS));		      \
	if (!extra) break;						      \
	for (o = orig, e = extra; (*e++ = *o++) != '\0';)		      \
		continue;						      \
	e--;								      \
	if (flag & VIS_SP) *e++ = ' ';					      \
	if (flag & VIS_TAB) *e++ = '\t';				      \
	if (flag & VIS_NL) *e++ = '\n';					      \
	if ((flag & VIS_NOSLASH) == 0) *e++ = '\\';			      \
	*e = '\0';							      \
} while (/*CONSTCOND*/0)

/*
 * This is do_hvis, for HTTP style (RFC 1808)
 */
static char *
do_hvis(char *dst, int c, int flag, int nextc, const char *extra)
{
	if (!isascii(c) || !isalnum(c) || strchr("$-_.+!*'(),", c) != NULL) {
		*dst++ = '%';
		*dst++ = xtoa(((unsigned int)c >> 4) & 0xf);
		*dst++ = xtoa((unsigned int)c & 0xf);
	} else {
		dst = do_svis(dst, c, flag, nextc, extra);
	}
	return dst;
}

/*
 * This is do_vis, the central code of vis.
 * dst:	      Pointer to the destination buffer
 * c:	      Character to encode
 * flag:      Flag word
 * nextc:     The character following 'c'
 * extra:     Pointer to the list of extra characters to be
 *	      backslash-protected.
 */
static char *
do_svis(char *dst, int c, int flag, int nextc, const char *extra)
{
	int isextra;
	isextra = strchr(extra, c) != NULL;
	if (!isextra && isascii(c) && (isgraph(c) || iswhite(c) ||
	    ((flag & VIS_SAFE) && issafe(c)))) {
		*dst++ = c;
		return dst;
	}
	if (flag & VIS_CSTYLE) {
		switch (c) {
		case '\n':
			*dst++ = '\\'; *dst++ = 'n';
			return dst;
		case '\r':
			*dst++ = '\\'; *dst++ = 'r';
			return dst;
		case '\b':
			*dst++ = '\\'; *dst++ = 'b';
			return dst;
		case BELL:
			*dst++ = '\\'; *dst++ = 'a';
			return dst;
		case '\v':
			*dst++ = '\\'; *dst++ = 'v';
			return dst;
		case '\t':
			*dst++ = '\\'; *dst++ = 't';
			return dst;
		case '\f':
			*dst++ = '\\'; *dst++ = 'f';
			return dst;
		case ' ':
			*dst++ = '\\'; *dst++ = 's';
			return dst;
		case '\0':
			*dst++ = '\\'; *dst++ = '0';
			if (isoctal(nextc)) {
				*dst++ = '0';
				*dst++ = '0';
			}
			return dst;
		default:
			if (isgraph(c)) {
				*dst++ = '\\'; *dst++ = c;
				return dst;
			}
		}
	}
	if (isextra || ((c & 0177) == ' ') || (flag & VIS_OCTAL)) {
		*dst++ = '\\';
		*dst++ = (u_char)(((unsigned int)(u_char)c >> 6) & 03) + '0';
		*dst++ = (u_char)(((unsigned int)(u_char)c >> 3) & 07) + '0';
		*dst++ = (u_char)(			 c       & 07) + '0';
	} else {
		if ((flag & VIS_NOSLASH) == 0) *dst++ = '\\';
		if (c & 0200) {
			c &= 0177; *dst++ = 'M';
		}
		if (iscntrl(c)) {
			*dst++ = '^';
			if (c == 0177)
				*dst++ = '?';
			else
				*dst++ = c + '@';
		} else {
			*dst++ = '-'; *dst++ = c;
		}
	}
	return dst;
}


/*
 * svis - visually encode characters, also encoding the characters
 *	  pointed to by `extra'
 */
ROKEN_LIB_FUNCTION char * ROKEN_LIB_CALL
rk_svis(char *dst, int c, int flag, int nextc, const char *extra)
{
	char *nextra = NULL;

	_DIAGASSERT(dst != NULL);
	_DIAGASSERT(extra != NULL);
	MAKEEXTRALIST(flag, nextra, extra);
	if (!nextra) {
		*dst = '\0';		/* can't create nextra, return "" */
		return dst;
	}
	if (flag & VIS_HTTPSTYLE)
		dst = do_hvis(dst, c, flag, nextc, nextra);
	else
		dst = do_svis(dst, c, flag, nextc, nextra);
	free(nextra);
	*dst = '\0';
	return dst;
}


/*
 * strsvis, strsvisx - visually encode characters from src into dst
 *
 *	Extra is a pointer to a \0-terminated list of characters to
 *	be encoded, too. These functions are useful e. g. to
 *	encode strings in such a way so that they are not interpreted
 *	by a shell.
 *
 *	Dst must be 4 times the size of src to account for possible
 *	expansion.  The length of dst, not including the trailing NULL,
 *	is returned.
 *
 *	Strsvisx encodes exactly len bytes from src into dst.
 *	This is useful for encoding a block of data.
 */

ROKEN_LIB_FUNCTION int ROKEN_LIB_CALL
rk_strsvis(char *dst, const char *csrc, int flag, const char *extra)
{
	int c;
	char *start;
	char *nextra = NULL;
	const unsigned char *src = (const unsigned char *)csrc;

	_DIAGASSERT(dst != NULL);
	_DIAGASSERT(src != NULL);
	_DIAGASSERT(extra != NULL);
	MAKEEXTRALIST(flag, nextra, extra);
	if (!nextra) {
		*dst = '\0';		/* can't create nextra, return "" */
		return 0;
	}
	if (flag & VIS_HTTPSTYLE) {
		for (start = dst; (c = *src++) != '\0'; /* empty */)
			dst = do_hvis(dst, c, flag, *src, nextra);
	} else {
		for (start = dst; (c = *src++) != '\0'; /* empty */)
			dst = do_svis(dst, c, flag, *src, nextra);
	}
	free(nextra);
	*dst = '\0';
	return (dst - start);
}


ROKEN_LIB_FUNCTION int ROKEN_LIB_CALL
rk_strsvisx(char *dst, const char *csrc, size_t len, int flag, const char *extra)
{
	unsigned char c;
	char *start;
	char *nextra = NULL;
	const unsigned char *src = (const unsigned char *)csrc;

	_DIAGASSERT(dst != NULL);
	_DIAGASSERT(src != NULL);
	_DIAGASSERT(extra != NULL);
	MAKEEXTRALIST(flag, nextra, extra);
	if (! nextra) {
		*dst = '\0';		/* can't create nextra, return "" */
		return 0;
	}

	if (flag & VIS_HTTPSTYLE) {
		for (start = dst; len > 0; len--) {
			c = *src++;
			dst = do_hvis(dst, c, flag, len ? *src : '\0', nextra);
		}
	} else {
		for (start = dst; len > 0; len--) {
			c = *src++;
			dst = do_svis(dst, c, flag, len ? *src : '\0', nextra);
		}
	}
	free(nextra);
	*dst = '\0';
	return (dst - start);
}
#endif

#if !HAVE_VIS
/*
 * vis - visually encode characters
 */
ROKEN_LIB_FUNCTION char * ROKEN_LIB_CALL
rk_vis(char *dst, int c, int flag, int nextc)
{
	char *extra = NULL;
	unsigned char uc = (unsigned char)c;

	_DIAGASSERT(dst != NULL);

	MAKEEXTRALIST(flag, extra, "");
	if (! extra) {
		*dst = '\0';		/* can't create extra, return "" */
		return dst;
	}
	if (flag & VIS_HTTPSTYLE)
		dst = do_hvis(dst, uc, flag, nextc, extra);
	else
		dst = do_svis(dst, uc, flag, nextc, extra);
	free(extra);
	*dst = '\0';
	return dst;
}


/*
 * strvis, strvisx - visually encode characters from src into dst
 *
 *	Dst must be 4 times the size of src to account for possible
 *	expansion.  The length of dst, not including the trailing NULL,
 *	is returned.
 *
 *	Strvisx encodes exactly len bytes from src into dst.
 *	This is useful for encoding a block of data.
 */
ROKEN_LIB_FUNCTION int ROKEN_LIB_CALL
rk_strvis(char *dst, const char *src, int flag)
{
	char *extra = NULL;
	int rv;

	MAKEEXTRALIST(flag, extra, "");
	if (!extra) {
		*dst = '\0';		/* can't create extra, return "" */
		return 0;
	}
	rv = strsvis(dst, src, flag, extra);
	free(extra);
	return rv;
}


ROKEN_LIB_FUNCTION int ROKEN_LIB_CALL
rk_strvisx(char *dst, const char *src, size_t len, int flag)
{
	char *extra = NULL;
	int rv;

	MAKEEXTRALIST(flag, extra, "");
	if (!extra) {
		*dst = '\0';		/* can't create extra, return "" */
		return 0;
	}
	rv = strsvisx(dst, src, len, flag, extra);
	free(extra);
	return rv;
}
#endif
