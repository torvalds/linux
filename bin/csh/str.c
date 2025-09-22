/*	$OpenBSD: str.c,v 1.22 2018/09/18 17:48:22 millert Exp $	*/
/*	$NetBSD: str.c,v 1.6 1995/03/21 09:03:24 cgd Exp $	*/

/*-
 * Copyright (c) 1991, 1993
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

#define MALLOC_INCR	128

/*
 * tc.str.c: Short string package
 *	     This has been a lesson of how to write buggy code!
 */

#include <sys/types.h>
#include <stdarg.h>
#include <vis.h>

#include "csh.h"
#include "extern.h"

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
    sdst = dst = xreallocarray(NULL, n + 1, sizeof(Char *));

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
    sdst = dst = xreallocarray(NULL, n + 1, sizeof(char *));

    for (; *src != NULL; src++)
	*dst++ = xstrdup(short2str(*src));
    *dst = NULL;
    return (sdst);
}

Char   *
str2short(char *src)
{
    static Char *sdst;
    static size_t dstsize = 0;
    Char *dst, *edst;

    if (src == NULL)
	return (NULL);

    if (sdst == (NULL)) {
	dstsize = MALLOC_INCR;
	sdst = xreallocarray(NULL, dstsize, sizeof(Char));
    }

    dst = sdst;
    edst = &dst[dstsize];
    while (*src) {
	*dst++ = (Char) ((unsigned char) *src++);
	if (dst == edst) {
	    dstsize += MALLOC_INCR;
	    sdst = xreallocarray(sdst, dstsize, sizeof(Char));
	    edst = &sdst[dstsize];
	    dst = &edst[-MALLOC_INCR];
	}
    }
    *dst = 0;
    return (sdst);
}

char   *
short2str(Char *src)
{
    static char *sdst = NULL;
    static size_t dstsize = 0;
    char *dst, *edst;

    if (src == NULL)
	return (NULL);

    if (sdst == NULL) {
	dstsize = MALLOC_INCR;
	sdst = xreallocarray(NULL, dstsize, sizeof(char));
    }
    dst = sdst;
    edst = &dst[dstsize];
    while (*src) {
	*dst++ = (char) *src++;
	if (dst == edst) {
	    dstsize += MALLOC_INCR;
	    sdst = xreallocarray(sdst, dstsize, sizeof(char));
	    edst = &sdst[dstsize];
	    dst = &edst[-MALLOC_INCR];
	}
    }
    *dst = 0;
    return (sdst);
}

size_t
Strlcpy(Char *dst, const Char *src, size_t siz)
{
        Char *d = dst;
        const Char *s = src;
        size_t n = siz;

        /* Copy as many bytes as will fit */
        if (n != 0 && --n != 0) {
                do {
                        if ((*d++ = *s++) == 0)
                                break;
                } while (--n != 0);
        }

        /* Not enough room in dst, add NUL and traverse rest of src */
        if (n == 0) {
                if (siz != 0)
                        *d = '\0';              /* NUL-terminate dst */
                while (*s++)
                        ;
        }

        return(s - src - 1);    /* count does not include NUL */
}

size_t
Strlcat(Char *dst, const Char *src, size_t siz)
{
        Char *d = dst;
        const Char *s = src;
        size_t n = siz;
        size_t dlen;

        /* Find the end of dst and adjust bytes left but don't go past end */
        while (n-- != 0 && *d != '\0')
                d++;
        dlen = d - dst;
        n = siz - dlen;

        if (n == 0)
                return(dlen + Strlen((Char *)s));
        while (*s != '\0') {
                if (n != 1) {
                        *d++ = *s;
                        n--;
                }
                s++;
        }
        *d = '\0';

        return(dlen + (s - src));       /* count does not include NUL */
}

Char   *
Strchr(Char *str, int ch)
{
    do
	if (*str == ch)
	    return (str);
    while (*str++)
	;
    return (NULL);
}

Char   *
Strrchr(Char *str, int ch)
{
    Char *rstr;

    rstr = NULL;
    do
	if (*str == ch)
	    rstr = str;
    while (*str++)
	;
    return (rstr);
}

size_t
Strlen(Char *str)
{
    size_t n;

    for (n = 0; *str++; n++)
	continue;
    return (n);
}

int
Strcmp(Char *str1, Char *str2)
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
Strncmp(Char *str1, Char *str2, size_t n)
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
	    break;
	}
	if (*str1 == '\0')
	    return(0);
	str1++, str2++;
    } while (--n != 0);
    return(0);
}

Char   *
Strsave(Char *s)
{
    Char   *n;
    Char *p;

    if (s == 0)
	s = STRNULL;
    for (p = s; *p++;)
	continue;
    n = p = xreallocarray(NULL, p - s, sizeof(Char));
    while ((*p++ = *s++) != '\0')
	continue;
    return (n);
}

Char   *
Strspl(Char *cp, Char *dp)
{
    Char   *ep;
    Char *p, *q;

    if (!cp)
	cp = STRNULL;
    if (!dp)
	dp = STRNULL;
    for (p = cp; *p++;)
	continue;
    for (q = dp; *q++;)
	continue;
    ep = xreallocarray(NULL, ((p - cp) + (q - dp) - 1), sizeof(Char));
    for (p = ep, q = cp; (*p++ = *q++) != '\0';)
	continue;
    for (p--, q = dp; (*p++ = *q++) != '\0';)
	continue;
    return (ep);
}

Char   *
Strend(Char *cp)
{
    if (!cp)
	return (cp);
    while (*cp)
	cp++;
    return (cp);
}

Char   *
Strstr(Char *s, Char *t)
{
    do {
	Char *ss = s;
	Char *tt = t;

	do
	    if (*tt == '\0')
		return (s);
	while (*ss++ == *tt++);
    } while (*s++ != '\0');
    return (NULL);
}

char   *
short2qstr(Char *src)
{
    static char *sdst = NULL;
    static size_t dstsize = 0;
    char *dst, *edst;

    if (src == NULL)
	return (NULL);

    if (sdst == NULL) {
	dstsize = MALLOC_INCR;
	sdst = xreallocarray(NULL, dstsize, sizeof(char));
    }
    dst = sdst;
    edst = &dst[dstsize];
    while (*src) {
	if (*src & QUOTE) {
	    *dst++ = '\\';
	    if (dst == edst) {
		dstsize += MALLOC_INCR;
		sdst = xreallocarray(sdst, dstsize, sizeof(char));
		edst = &sdst[dstsize];
		dst = &edst[-MALLOC_INCR];
	    }
	}
	*dst++ = (char) *src++;
	if (dst == edst) {
	    dstsize += MALLOC_INCR;
	    sdst = xreallocarray(sdst, dstsize, sizeof(char));
	    edst = &sdst[dstsize];
	    dst = &edst[-MALLOC_INCR];
	}
    }
    *dst = 0;
    return (sdst);
}

/*
 * XXX: Should we worry about QUOTE'd chars?
 */
char *
vis_str(Char *cp)
{
    static char *sdst = NULL;
    static size_t dstsize = 0;
    size_t n;
    Char *dp;

    if (cp == NULL)
	return (NULL);

    for (dp = cp; *dp++;)
	continue;
    n = ((dp - cp) << 2) + 1; /* 4 times + NUL */
    if (dstsize < n) {
	sdst = xreallocarray(sdst, n, sizeof(char));
	dstsize = n;
    }
    (void) strnvis(sdst, short2str(cp), dstsize, VIS_NOSLASH);
    return (sdst);
}

