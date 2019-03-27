/*
 * Copyright (C) 2004-2005
 *	Hartmut Brandt.
 *	All rights reserved.
 *
 * Author: Harti Brandt <harti@freebsd.org>
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
 * THIS SOFTWARE IS PROVIDED BY AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $Begemot: bsnmp/lib/support.h,v 1.2 2005/10/06 07:14:59 brandt_h Exp $
 *
 * Functions that are missing on certain systems. This header file is not
 * to be installed.
 */
#ifndef bsnmp_support_h_
#define bsnmp_support_h_

#include <sys/cdefs.h>

#ifndef HAVE_ERR_H
void err(int, const char *, ...) __printflike(2, 3) __dead2;
void errx(int, const char *, ...) __printflike(2, 3) __dead2;

void warn(const char *, ...) __printflike(1, 2);
void warnx(const char *, ...) __printflike(1, 2);
#endif

#ifndef HAVE_STRLCPY
size_t strlcpy(char *, const char *, size_t);
#endif

#ifndef HAVE_GETADDRINFO

struct addrinfo {
	u_int	ai_flags;
	int	ai_family;
	int	ai_socktype;
	int	ai_protocol;
	struct sockaddr *ai_addr;
	int	ai_addrlen;
	struct addrinfo *ai_next;
};
#define	AI_CANONNAME	0x0001

int getaddrinfo(const char *, const char *, const struct addrinfo *,
    struct addrinfo **);
const char *gai_strerror(int);
void freeaddrinfo(struct addrinfo *);

#endif

/*
 * For systems with missing stdint.h or inttypes.h
 */
#if !defined(INT32_MIN)
#define	INT32_MIN	(-0x7fffffff-1)
#endif
#if !defined(INT32_MAX)
#define	INT32_MAX	(0x7fffffff)
#endif
#if !defined(UINT32_MAX)
#define	UINT32_MAX	(0xffffffff)
#endif

/*
 * Systems missing SA_SIZE(). Taken from FreeBSD net/route.h:1.63
 */
#ifndef SA_SIZE

#define SA_SIZE(sa)						\
    (  (!(sa) || ((struct sockaddr *)(sa))->sa_len == 0) ?	\
	sizeof(long)		:				\
	1 + ( (((struct sockaddr *)(sa))->sa_len - 1) | (sizeof(long) - 1) ) )

#endif

#endif
