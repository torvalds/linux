/* $Header: /p/tcsh/cvsroot/tcsh/tc.str.c,v 3.47 2015/06/06 21:19:08 christos Exp $ */
/*
 * tc.str.c: Short string package
 * 	     This has been a lesson of how to write buggy code!
 */
/*-
 * Copyright (c) 1980, 1991 The Regents of the University of California.
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
#include "sh.h"

#include <assert.h>
#include <limits.h>

RCSID("$tcsh: tc.str.c,v 3.47 2015/06/06 21:19:08 christos Exp $")

#define MALLOC_INCR	128
#ifdef WIDE_STRINGS
#define MALLOC_SURPLUS	MB_LEN_MAX /* Space for one multibyte character */
#else
#define MALLOC_SURPLUS	0
#endif

#ifdef WIDE_STRINGS
size_t
one_mbtowc(Char *pwc, const char *s, size_t n)
{
    int len;

    len = rt_mbtowc(pwc, s, n);
    if (len == -1) {
        reset_mbtowc();
	*pwc = (unsigned char)*s | INVALID_BYTE;
    }
    if (len <= 0)
	len = 1;
    return len;
}

size_t
one_wctomb(char *s, Char wchar)
{
    int len;

#if INVALID_BYTE != 0
    if ((wchar & INVALID_BYTE) == INVALID_BYTE) {    /* wchar >= INVALID_BYTE */
	/* invalid char
	 * exmaple)
	 * if wchar = f0000090(=90|INVALID_BYTE), then *s = ffffff90 */
	*s = (char)wchar;
	len = 1;
#else
    if (wchar & (CHAR & INVALID_BYTE)) {
	s[0] = wchar & (CHAR & 0xFF);
	len = 1;
#endif
    } else {
#if INVALID_BYTE != 0
	wchar &= MAX_UTF32;
#else
	wchar &= CHAR;
#endif
#ifdef UTF16_STRINGS
	if (wchar >= 0x10000) {
	    /* UTF-16 systems can't handle these values directly in calls to
	       wctomb.  Convert value to UTF-16 surrogate and call wcstombs to
	       convert the "string" to the correct multibyte representation,
	       if any. */
	    wchar_t ws[3];
	    wchar -= 0x10000;
	    ws[0] = 0xd800 | (wchar >> 10);
	    ws[1] = 0xdc00 | (wchar & 0x3ff);
	    ws[2] = 0;
	    /* The return value of wcstombs excludes the trailing 0, so len is
	       the correct number of multibytes for the Unicode char. */
	    len = wcstombs (s, ws, MB_CUR_MAX + 1);
	} else
#endif
	len = wctomb(s, (wchar_t) wchar);
	if (len == -1)
	    s[0] = wchar;
	if (len <= 0)
	    len = 1;
    }
    return len;
}

int
rt_mbtowc(Char *pwc, const char *s, size_t n)
{
    int ret;
    char back[MB_LEN_MAX];
    wchar_t tmp;
#if defined(UTF16_STRINGS) && defined(HAVE_MBRTOWC)
# if defined(AUTOSET_KANJI)
    static mbstate_t mb_zero, mb;
    /*
     * Workaround the Shift-JIS endcoding that translates unshifted 7 bit ASCII!
     */
    if (!adrof(STRnokanji) && n && pwc && s && (*s == '\\' || *s == '~') &&
	!memcmp(&mb, &mb_zero, sizeof(mb)))
    {
	*pwc = *s;
	return 1;
    }
# else
    mbstate_t mb;
# endif

    memset (&mb, 0, sizeof mb);
    ret = mbrtowc(&tmp, s, n, &mb);
#else
    ret = mbtowc(&tmp, s, n);
#endif
    if (ret > 0) {
	*pwc = tmp;
#if defined(UTF16_STRINGS) && defined(HAVE_MBRTOWC)
	if (tmp >= 0xd800 && tmp <= 0xdbff) {
	    /* UTF-16 surrogate pair.  Fetch second half and compute
	       UTF-32 value.  Dispense with the inverse test in this case. */
	    size_t n2 = mbrtowc(&tmp, s + ret, n - ret, &mb);
	    if (n2 == 0 || n2 == (size_t)-1 || n2 == (size_t)-2)
		ret = -1;
	    else {
		*pwc = (((*pwc & 0x3ff) << 10) | (tmp & 0x3ff)) + 0x10000;
		ret += n2;
	    }
	} else
#endif
      	if (wctomb(back, *pwc) != ret || memcmp(s, back, ret) != 0)
	    ret = -1;

    } else if (ret == -2)
	ret = -1;
    else if (ret == 0)
	*pwc = '\0';

    return ret;
}
#endif

#ifdef SHORT_STRINGS
Char  **
blk2short(char **src)
{
    size_t     n;
    Char **sdst, **dst;

    /*
     * Count
     */
    for (n = 0; src[n] != NULL; n++)
	continue;
    sdst = dst = xmalloc((n + 1) * sizeof(Char *));

    for (; *src != NULL; src++)
	*dst++ = SAVE(*src);
    *dst = NULL;
    return (sdst);
}

char  **
short2blk(Char **src)
{
    size_t     n;
    char **sdst, **dst;

    /*
     * Count
     */
    for (n = 0; src[n] != NULL; n++)
	continue;
    sdst = dst = xmalloc((n + 1) * sizeof(char *));

    for (; *src != NULL; src++)
	*dst++ = strsave(short2str(*src));
    *dst = NULL;
    return (sdst);
}

Char   *
str2short(const char *src)
{
    static struct Strbuf buf; /* = Strbuf_INIT; */

    if (src == NULL)
	return (NULL);

    buf.len = 0;
    while (*src) {
	Char wc;

	src += one_mbtowc(&wc, src, MB_LEN_MAX);
	Strbuf_append1(&buf, wc);
    }
    Strbuf_terminate(&buf);
    return buf.s;
}

char   *
short2str(const Char *src)
{
    static char *sdst = NULL;
    static size_t dstsize = 0;
    char *dst, *edst;

    if (src == NULL)
	return (NULL);

    if (sdst == NULL) {
	dstsize = MALLOC_INCR;
	sdst = xmalloc((dstsize + MALLOC_SURPLUS) * sizeof(char));
    }
    dst = sdst;
    edst = &dst[dstsize];
    while (*src) {
	dst += one_wctomb(dst, *src);
	src++;
	if (dst >= edst) {
	    char *wdst = dst;
	    char *wedst = edst;

	    dstsize += MALLOC_INCR;
	    sdst = xrealloc(sdst, (dstsize + MALLOC_SURPLUS) * sizeof(char));
	    edst = &sdst[dstsize];
	    dst = &edst[-MALLOC_INCR];
	    while (wdst > wedst) {
		dst++;
		wdst--;
	    }
	}
    }
    *dst = 0;
    return (sdst);
}

#if !defined (WIDE_STRINGS) || defined (UTF16_STRINGS)
Char   *
s_strcpy(Char *dst, const Char *src)
{
    Char *sdst;

    sdst = dst;
    while ((*dst++ = *src++) != '\0')
	continue;
    return (sdst);
}

Char   *
s_strncpy(Char *dst, const Char *src, size_t n)
{
    Char *sdst;

    if (n == 0)
	return(dst);

    sdst = dst;
    do 
	if ((*dst++ = *src++) == '\0') {
	    while (--n != 0)
		*dst++ = '\0';
	    return(sdst);
	}
    while (--n != 0);
    return (sdst);
}

Char   *
s_strcat(Char *dst, const Char *src)
{
    Strcpy(Strend(dst), src);
    return dst;
}

#ifdef NOTUSED
Char   *
s_strncat(Char *dst, const Char *src, size_t n)
{
    Char *sdst;

    if (n == 0) 
	return (dst);

    sdst = dst;

    while (*dst)
	dst++;

    do 
	if ((*dst++ = *src++) == '\0')
	    return(sdst);
    while (--n != 0)
	continue;

    *dst = '\0';
    return (sdst);
}

#endif

Char   *
s_strchr(const Char *str, int ch)
{
    do
	if (*str == ch)
	    return ((Char *)(intptr_t)str);
    while (*str++);
    return (NULL);
}

Char   *
s_strrchr(const Char *str, int ch)
{
    const Char *rstr;

    rstr = NULL;
    do
	if (*str == ch)
	    rstr = str;
    while (*str++);
    return ((Char *)(intptr_t)rstr);
}

size_t
s_strlen(const Char *str)
{
    size_t n;

    for (n = 0; *str++; n++)
	continue;
    return (n);
}

int
s_strcmp(const Char *str1, const Char *str2)
{
    for (; *str1 && *str1 == *str2; str1++, str2++)
	continue;
    /*
     * The following case analysis is necessary so that characters which look
     * negative collate low against normal characters but high against the
     * end-of-string NUL.
     */
    if (*str1 == '\0' && *str2 == '\0')
	return (0);
    else if (*str1 == '\0')
	return (-1);
    else if (*str2 == '\0')
	return (1);
    else
	return (*str1 - *str2);
}

int
s_strncmp(const Char *str1, const Char *str2, size_t n)
{
    if (n == 0)
	return (0);
    do {
	if (*str1 != *str2) {
	    /*
	     * The following case analysis is necessary so that characters 
	     * which look negative collate low against normal characters
	     * but high against the end-of-string NUL.
	     */
	    if (*str1 == '\0')
		return (-1);
	    else if (*str2 == '\0')
		return (1);
	    else
		return (*str1 - *str2);
	}
        if (*str1 == '\0')
	    return(0);
	str1++, str2++;
    } while (--n != 0);
    return(0);
}
#endif /* not WIDE_STRINGS */

int
s_strcasecmp(const Char *str1, const Char *str2)
{
#ifdef WIDE_STRINGS
    wint_t l1 = 0, l2 = 0;
    for (; *str1; str1++, str2++)
	if (*str1 == *str2)
	    l1 = l2 = 0;
	else if ((l1 = towlower(*str1)) != (l2 = towlower(*str2)))
	    break;
#else
    unsigned char l1 = 0, l2 = 0;
    for (; *str1; str1++, str2++)
	if (*str1 == *str2)
		l1 = l2 = 0;
	else if ((l1 = tolower((unsigned char)*str1)) !=
	    (l2 = tolower((unsigned char)*str2)))
	    break;
#endif
    /*
     * The following case analysis is necessary so that characters which look
     * negative collate low against normal characters but high against the
     * end-of-string NUL.
     */
    if (*str1 == '\0' && *str2 == '\0')
	return (0);
    else if (*str1 == '\0')
	return (-1);
    else if (*str2 == '\0')
	return (1);
    else if (l1 == l2)	/* They are zero when they are equal */
	return (*str1 - *str2);
    else
	return (l1 - l2);
}

Char   *
s_strnsave(const Char *s, size_t len)
{
    Char *n;

    n = xmalloc((len + 1) * sizeof (*n));
    memcpy(n, s, len * sizeof (*n));
    n[len] = '\0';
    return n;
}

Char   *
s_strsave(const Char *s)
{
    Char   *n;
    size_t size;

    if (s == NULL)
	s = STRNULL;
    size = (Strlen(s) + 1) * sizeof(*n);
    n = xmalloc(size);
    memcpy(n, s, size);
    return (n);
}

Char   *
s_strspl(const Char *cp, const Char *dp)
{
    Char *res, *ep;
    const Char *p, *q;

    if (!cp)
	cp = STRNULL;
    if (!dp)
	dp = STRNULL;
    for (p = cp; *p++;)
	continue;
    for (q = dp; *q++;)
	continue;
    res = xmalloc(((p - cp) + (q - dp) - 1) * sizeof(Char));
    for (ep = res, q = cp; (*ep++ = *q++) != '\0';)
	continue;
    for (ep--, q = dp; (*ep++ = *q++) != '\0';)
	continue;
    return (res);
}

Char   *
s_strend(const Char *cp)
{
    if (!cp)
	return ((Char *)(intptr_t) cp);
    while (*cp)
	cp++;
    return ((Char *)(intptr_t) cp);
}

Char   *
s_strstr(const Char *s, const Char *t)
{
    do {
	const Char *ss = s;
	const Char *tt = t;

	do
	    if (*tt == '\0')
		return ((Char *)(intptr_t) s);
	while (*ss++ == *tt++);
    } while (*s++ != '\0');
    return (NULL);
}

#else /* !SHORT_STRINGS */
char *
caching_strip(const char *s)
{
    static char *buf = NULL;
    static size_t buf_size = 0;
    size_t size;

    if (s == NULL)
      return NULL;
    size = strlen(s) + 1;
    if (buf_size < size) {
	buf = xrealloc(buf, size);
	buf_size = size;
    }
    memcpy(buf, s, size);
    strip(buf);
    return buf;
}
#endif

char   *
short2qstr(const Char *src)
{
    static char *sdst = NULL;
    static size_t dstsize = 0;
    char *dst, *edst;

    if (src == NULL)
	return (NULL);

    if (sdst == NULL) {
	dstsize = MALLOC_INCR;
	sdst = xmalloc((dstsize + MALLOC_SURPLUS) * sizeof(char));
    }
    dst = sdst;
    edst = &dst[dstsize];
    while (*src) {
	if (*src & QUOTE) {
	    *dst++ = '\\';
	    if (dst == edst) {
		dstsize += MALLOC_INCR;
		sdst = xrealloc(sdst,
				(dstsize + MALLOC_SURPLUS) * sizeof(char));
		edst = &sdst[dstsize];
		dst = &edst[-MALLOC_INCR];
	    }
	}
	dst += one_wctomb(dst, *src);
	src++;
	if (dst >= edst) {
	    ptrdiff_t i = dst - edst;
	    dstsize += MALLOC_INCR;
	    sdst = xrealloc(sdst, (dstsize + MALLOC_SURPLUS) * sizeof(char));
	    edst = &sdst[dstsize];
	    dst = &edst[-MALLOC_INCR + i];
	}
    }
    *dst = 0;
    return (sdst);
}

struct blk_buf *
bb_alloc(void)
{
    return xcalloc(1, sizeof(struct blk_buf));
}

static void
bb_store(struct blk_buf *bb, Char *str)
{
    if (bb->len == bb->size) { /* Keep space for terminating NULL */
	if (bb->size == 0)
	    bb->size = 16; /* Arbitrary */
	else
	    bb->size *= 2;
	bb->vec = xrealloc(bb->vec, bb->size * sizeof (*bb->vec));
    }
    bb->vec[bb->len] = str;
}

void
bb_append(struct blk_buf *bb, Char *str)
{
    bb_store(bb, str);
    bb->len++;
}

void
bb_cleanup(void *xbb)
{
    struct blk_buf *bb;
    size_t i;

    bb = (struct blk_buf *)xbb;
    if (bb->vec) {
	for (i = 0; i < bb->len; i++)
	    xfree(bb->vec[i]);
	xfree(bb->vec);
    }
    bb->vec = NULL;
    bb->len = 0;
}

void
bb_free(void *bb)
{
    bb_cleanup(bb);
    xfree(bb);
}

Char **
bb_finish(struct blk_buf *bb)
{
    bb_store(bb, NULL);
    return xrealloc(bb->vec, (bb->len + 1) * sizeof (*bb->vec));
}

#define DO_STRBUF(STRBUF, CHAR, STRLEN)				\
								\
struct STRBUF *							\
STRBUF##_alloc(void)						\
{								\
    return xcalloc(1, sizeof(struct STRBUF));			\
}								\
								\
static void							\
STRBUF##_store1(struct STRBUF *buf, CHAR c)			\
{								\
    if (buf->size == buf->len) {				\
	if (buf->size == 0)					\
	    buf->size = 64; /* Arbitrary */			\
	else							\
	    buf->size *= 2;					\
	buf->s = xrealloc(buf->s, buf->size * sizeof(*buf->s));	\
    }								\
    assert(buf->s);						\
    buf->s[buf->len] = c;					\
}								\
								\
/* Like strbuf_append1(buf, '\0'), but don't advance len */	\
void								\
STRBUF##_terminate(struct STRBUF *buf)				\
{								\
    STRBUF##_store1(buf, '\0');					\
}								\
								\
void								\
STRBUF##_append1(struct STRBUF *buf, CHAR c)			\
{								\
    STRBUF##_store1(buf, c);					\
    buf->len++;							\
}								\
								\
void								\
STRBUF##_appendn(struct STRBUF *buf, const CHAR *s, size_t len)	\
{								\
    if (buf->size < buf->len + len) {				\
	if (buf->size == 0)					\
	    buf->size = 64; /* Arbitrary */			\
	while (buf->size < buf->len + len)			\
	    buf->size *= 2;					\
	buf->s = xrealloc(buf->s, buf->size * sizeof(*buf->s));	\
    }								\
    memcpy(buf->s + buf->len, s, len * sizeof(*buf->s));	\
    buf->len += len;						\
}								\
								\
void								\
STRBUF##_append(struct STRBUF *buf, const CHAR *s)		\
{								\
    STRBUF##_appendn(buf, s, STRLEN(s));			\
}								\
								\
CHAR *								\
STRBUF##_finish(struct STRBUF *buf)				\
{								\
    STRBUF##_append1(buf, 0);					\
    return xrealloc(buf->s, buf->len * sizeof(*buf->s));	\
}								\
								\
void								\
STRBUF##_cleanup(void *xbuf)					\
{								\
    struct STRBUF *buf;						\
								\
    buf = xbuf;							\
    xfree(buf->s);						\
}								\
								\
void								\
STRBUF##_free(void *xbuf)					\
{								\
    STRBUF##_cleanup(xbuf);					\
    xfree(xbuf);						\
}								\
								\
const struct STRBUF STRBUF##_init /* = STRBUF##_INIT; */

DO_STRBUF(strbuf, char, strlen);
DO_STRBUF(Strbuf, Char, Strlen);
