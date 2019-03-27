/* Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.
 * The ASF licenses this file to You under the Apache License, Version 2.0
 * (the "License"); you may not use this file except in compliance with
 * the License.  You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
/*
 * Copyright (c) 1990, 1993
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
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
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

#include "apr.h"
#include "apr_strings.h"
#include "apr_general.h"
#include "apr_private.h"
#include "apr_lib.h"
#define APR_WANT_STDIO
#define APR_WANT_STRFUNC
#include "apr_want.h"

#ifdef HAVE_STDDEF_H
#include <stddef.h> /* NULL */
#endif

#ifdef HAVE_STDLIB_H
#include <stdlib.h> /* strtol and strtoll */
#endif

/** this is used to cache lengths in apr_pstrcat */
#define MAX_SAVED_LENGTHS  6

APR_DECLARE(char *) apr_pstrdup(apr_pool_t *a, const char *s)
{
    char *res;
    apr_size_t len;

    if (s == NULL) {
        return NULL;
    }
    len = strlen(s) + 1;
    res = apr_pmemdup(a, s, len);
    return res;
}

APR_DECLARE(char *) apr_pstrndup(apr_pool_t *a, const char *s, apr_size_t n)
{
    char *res;
    const char *end;

    if (s == NULL) {
        return NULL;
    }
    end = memchr(s, '\0', n);
    if (end != NULL)
        n = end - s;
    res = apr_palloc(a, n + 1);
    memcpy(res, s, n);
    res[n] = '\0';
    return res;
}

APR_DECLARE(char *) apr_pstrmemdup(apr_pool_t *a, const char *s, apr_size_t n)
{
    char *res;

    if (s == NULL) {
        return NULL;
    }
    res = apr_palloc(a, n + 1);
    memcpy(res, s, n);
    res[n] = '\0';
    return res;
}

APR_DECLARE(void *) apr_pmemdup(apr_pool_t *a, const void *m, apr_size_t n)
{
    void *res;

    if (m == NULL)
	return NULL;
    res = apr_palloc(a, n);
    memcpy(res, m, n);
    return res;
}

APR_DECLARE_NONSTD(char *) apr_pstrcat(apr_pool_t *a, ...)
{
    char *cp, *argp, *res;
    apr_size_t saved_lengths[MAX_SAVED_LENGTHS];
    int nargs = 0;

    /* Pass one --- find length of required string */

    apr_size_t len = 0;
    va_list adummy;

    va_start(adummy, a);

    while ((cp = va_arg(adummy, char *)) != NULL) {
        apr_size_t cplen = strlen(cp);
        if (nargs < MAX_SAVED_LENGTHS) {
            saved_lengths[nargs++] = cplen;
        }
        len += cplen;
    }

    va_end(adummy);

    /* Allocate the required string */

    res = (char *) apr_palloc(a, len + 1);
    cp = res;

    /* Pass two --- copy the argument strings into the result space */

    va_start(adummy, a);

    nargs = 0;
    while ((argp = va_arg(adummy, char *)) != NULL) {
        if (nargs < MAX_SAVED_LENGTHS) {
            len = saved_lengths[nargs++];
        }
        else {
            len = strlen(argp);
        }
 
        memcpy(cp, argp, len);
        cp += len;
    }

    va_end(adummy);

    /* Return the result string */

    *cp = '\0';

    return res;
}

APR_DECLARE(char *) apr_pstrcatv(apr_pool_t *a, const struct iovec *vec,
                                 apr_size_t nvec, apr_size_t *nbytes)
{
    apr_size_t i;
    apr_size_t len;
    const struct iovec *src;
    char *res;
    char *dst;

    /* Pass one --- find length of required string */
    len = 0;
    src = vec;
    for (i = nvec; i; i--) {
        len += src->iov_len;
        src++;
    }
    if (nbytes) {
        *nbytes = len;
    }

    /* Allocate the required string */
    res = (char *) apr_palloc(a, len + 1);
    
    /* Pass two --- copy the argument strings into the result space */
    src = vec;
    dst = res;
    for (i = nvec; i; i--) {
        memcpy(dst, src->iov_base, src->iov_len);
        dst += src->iov_len;
        src++;
    }

    /* Return the result string */
    *dst = '\0';

    return res;
}

#if (!APR_HAVE_MEMCHR)
void *memchr(const void *s, int c, size_t n)
{
    const char *cp;

    for (cp = s; n > 0; n--, cp++) {
        if (*cp == c)
            return (char *) cp; /* Casting away the const here */
    }

    return NULL;
}
#endif

#ifndef INT64_MAX
#define INT64_MAX  APR_INT64_C(0x7fffffffffffffff)
#endif
#ifndef INT64_MIN
#define INT64_MIN (-APR_INT64_C(0x7fffffffffffffff) - APR_INT64_C(1))
#endif

APR_DECLARE(apr_status_t) apr_strtoff(apr_off_t *offset, const char *nptr,
                                      char **endptr, int base)
{
    errno = 0;
    *offset = APR_OFF_T_STRFN(nptr, endptr, base);
    return APR_FROM_OS_ERROR(errno);
}

APR_DECLARE(apr_int64_t) apr_strtoi64(const char *nptr, char **endptr, int base)
{
#ifdef APR_INT64_STRFN
    errno = 0;
    return APR_INT64_STRFN(nptr, endptr, base);
#else
    const char *s;
    apr_int64_t acc;
    apr_int64_t val;
    int neg, any;
    char c;

    errno = 0;
    /*
     * Skip white space and pick up leading +/- sign if any.
     * If base is 0, allow 0x for hex and 0 for octal, else
     * assume decimal; if base is already 16, allow 0x.
     */
    s = nptr;
    do {
	c = *s++;
    } while (apr_isspace(c));
    if (c == '-') {
	neg = 1;
	c = *s++;
    } else {
	neg = 0;
	if (c == '+')
	    c = *s++;
    }
    if ((base == 0 || base == 16) &&
	c == '0' && (*s == 'x' || *s == 'X')) {
	    c = s[1];
	    s += 2;
	    base = 16;
    }
    if (base == 0)
	base = c == '0' ? 8 : 10;
    acc = any = 0;
    if (base < 2 || base > 36) {
	errno = EINVAL;
        if (endptr != NULL)
	    *endptr = (char *)(any ? s - 1 : nptr);
        return acc;
    }

    /* The classic bsd implementation requires div/mod operators
     * to compute a cutoff.  Benchmarking proves that is very, very
     * evil to some 32 bit processors.  Instead, look for underflow
     * in both the mult and add/sub operation.  Unlike the bsd impl,
     * we also work strictly in a signed int64 word as we haven't
     * implemented the unsigned type in win32.
     * 
     * Set 'any' if any `digits' consumed; make it negative to indicate
     * overflow.
     */
    val = 0;
    for ( ; ; c = *s++) {
        if (c >= '0' && c <= '9')
	    c -= '0';
#if (('Z' - 'A') == 25)
	else if (c >= 'A' && c <= 'Z')
	    c -= 'A' - 10;
	else if (c >= 'a' && c <= 'z')
	    c -= 'a' - 10;
#elif APR_CHARSET_EBCDIC
	else if (c >= 'A' && c <= 'I')
	    c -= 'A' - 10;
	else if (c >= 'J' && c <= 'R')
	    c -= 'J' - 19;
	else if (c >= 'S' && c <= 'Z')
	    c -= 'S' - 28;
	else if (c >= 'a' && c <= 'i')
	    c -= 'a' - 10;
	else if (c >= 'j' && c <= 'r')
	    c -= 'j' - 19;
	else if (c >= 's' && c <= 'z')
	    c -= 'z' - 28;
#else
#error "CANNOT COMPILE apr_strtoi64(), only ASCII and EBCDIC supported" 
#endif
	else
	    break;
	if (c >= base)
	    break;
	val *= base;
        if ( (any < 0)	/* already noted an over/under flow - short circuit */
           || (neg && (val > acc || (val -= c) > acc)) /* underflow */
           || (!neg && (val < acc || (val += c) < acc))) {       /* overflow */
            any = -1;	/* once noted, over/underflows never go away */
#ifdef APR_STRTOI64_OVERFLOW_IS_BAD_CHAR
            break;
#endif
        } else {
            acc = val;
	    any = 1;
        }
    }

    if (any < 0) {
	acc = neg ? INT64_MIN : INT64_MAX;
	errno = ERANGE;
    } else if (!any) {
	errno = EINVAL;
    }
    if (endptr != NULL)
	*endptr = (char *)(any ? s - 1 : nptr);
    return (acc);
#endif
}

APR_DECLARE(apr_int64_t) apr_atoi64(const char *buf)
{
    return apr_strtoi64(buf, NULL, 10);
}

APR_DECLARE(char *) apr_itoa(apr_pool_t *p, int n)
{
    const int BUFFER_SIZE = sizeof(int) * 3 + 2;
    char *buf = apr_palloc(p, BUFFER_SIZE);
    char *start = buf + BUFFER_SIZE - 1;
    int negative;
    if (n < 0) {
	negative = 1;
	n = -n;
    }
    else {
	negative = 0;
    }
    *start = 0;
    do {
	*--start = '0' + (n % 10);
	n /= 10;
    } while (n);
    if (negative) {
	*--start = '-';
    }
    return start;
}

APR_DECLARE(char *) apr_ltoa(apr_pool_t *p, long n)
{
    const int BUFFER_SIZE = sizeof(long) * 3 + 2;
    char *buf = apr_palloc(p, BUFFER_SIZE);
    char *start = buf + BUFFER_SIZE - 1;
    int negative;
    if (n < 0) {
	negative = 1;
	n = -n;
    }
    else {
	negative = 0;
    }
    *start = 0;
    do {
	*--start = (char)('0' + (n % 10));
	n /= 10;
    } while (n);
    if (negative) {
	*--start = '-';
    }
    return start;
}

APR_DECLARE(char *) apr_off_t_toa(apr_pool_t *p, apr_off_t n)
{
    const int BUFFER_SIZE = sizeof(apr_off_t) * 3 + 2;
    char *buf = apr_palloc(p, BUFFER_SIZE);
    char *start = buf + BUFFER_SIZE - 1;
    int negative;
    if (n < 0) {
	negative = 1;
	n = -n;
    }
    else {
	negative = 0;
    }
    *start = 0;
    do {
	*--start = '0' + (char)(n % 10);
	n /= 10;
    } while (n);
    if (negative) {
	*--start = '-';
    }
    return start;
}

APR_DECLARE(char *) apr_strfsize(apr_off_t size, char *buf)
{
    const char ord[] = "KMGTPE";
    const char *o = ord;
    int remain;

    if (size < 0) {
        return strcpy(buf, "  - ");
    }
    if (size < 973) {
        if (apr_snprintf(buf, 5, "%3d ", (int) size) < 0)
            return strcpy(buf, "****");
        return buf;
    }
    do {
        remain = (int)(size & 1023);
        size >>= 10;
        if (size >= 973) {
            ++o;
            continue;
        }
        if (size < 9 || (size == 9 && remain < 973)) {
            if ((remain = ((remain * 5) + 256) / 512) >= 10)
                ++size, remain = 0;
            if (apr_snprintf(buf, 5, "%d.%d%c", (int) size, remain, *o) < 0)
                return strcpy(buf, "****");
            return buf;
        }
        if (remain >= 512)
            ++size;
        if (apr_snprintf(buf, 5, "%3d%c", (int) size, *o) < 0)
            return strcpy(buf, "****");
        return buf;
    } while (1);
}

