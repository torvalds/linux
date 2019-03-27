/* Copyright (c) 2008 The NetBSD Foundation, Inc.
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
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND
 * CONTRIBUTORS ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES,
 * INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
 * GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
 * IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
 * IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.  */

#if !defined(ATF_C_DETAIL_SANITY_H)
#define ATF_C_DETAIL_SANITY_H

void atf_sanity_inv(const char *, int, const char *);
void atf_sanity_pre(const char *, int, const char *);
void atf_sanity_post(const char *, int, const char *);

#if !defined(NDEBUG)

#define INV(x) \
    do { \
        if (!(x)) \
            atf_sanity_inv(__FILE__, __LINE__, #x); \
    } while (0)
#define PRE(x) \
    do { \
        if (!(x)) \
            atf_sanity_pre(__FILE__, __LINE__, #x); \
    } while (0)
#define POST(x) \
    do { \
        if (!(x)) \
            atf_sanity_post(__FILE__, __LINE__, #x); \
    } while (0)

#else /* defined(NDEBUG) */

#define INV(x) \
    do { \
    } while (0)

#define PRE(x) \
    do { \
    } while (0)

#define POST(x) \
    do { \
    } while (0)

#endif /* !defined(NDEBUG) */

#define UNREACHABLE INV(0)

#endif /* !defined(ATF_C_DETAIL_SANITY_H) */
