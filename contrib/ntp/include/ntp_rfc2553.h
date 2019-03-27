/*
 * Copyright (C) 1995, 1996, 1997, and 1998 WIDE Project.
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
 * 3. Neither the name of the project nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE PROJECT AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE PROJECT OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * Copyright (c) 1982, 1986, 1990, 1993
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
 *
 *	@(#)in.h	8.3 (Berkeley) 1/3/94
 */

/*
 * Compatability shims with the rfc2553 API to simplify ntp.
 */
#ifndef NTP_RFC2553_H
#define NTP_RFC2553_H

#include <netdb.h>
#include <isc/net.h>

#include "ntp_types.h"
#include "ntp_malloc.h"

struct addrinfo *copy_addrinfo_impl(const struct addrinfo *
#ifdef EREALLOC_CALLSITE	/* from ntp_malloc.h */
							   ,
				    const char *, int
#endif
					 );
struct addrinfo *copy_addrinfo_list_impl(const struct addrinfo *
#ifdef EREALLOC_CALLSITE	/* from ntp_malloc.h */
								,
					 const char *, int
#endif
					 );
#ifdef EREALLOC_CALLSITE
# define copy_addrinfo(l) \
	 copy_addrinfo_impl((l), __FILE__, __LINE__)
# define copy_addrinfo_list(l) \
	 copy_addrinfo_list_impl((l), __FILE__, __LINE__)
#else
# define copy_addrinfo(l)	copy_addrinfo_impl(l)
# define copy_addrinfo_list(l)	copy_addrinfo_list_impl(l)
#endif

/*
 * If various macros are not defined we need to define them
 */

#ifndef AF_INET6
# define AF_INET6	AF_MAX
# define PF_INET6	AF_INET6
#endif

#if !defined(_SS_MAXSIZE) && !defined(_SS_ALIGNSIZE)

# define	_SS_MAXSIZE	128
# define	_SS_ALIGNSIZE	(sizeof(ntp_uint64_t))
# ifdef ISC_PLATFORM_HAVESALEN
#  define	_SS_PAD1SIZE	(_SS_ALIGNSIZE - sizeof(u_char) - sizeof(ntp_u_int8_t))
#  define	_SS_PAD2SIZE	(_SS_MAXSIZE - sizeof(u_char) - sizeof(ntp_u_int8_t) - \
				_SS_PAD1SIZE - _SS_ALIGNSIZE)
# else
#  define	_SS_PAD1SIZE	(_SS_ALIGNSIZE - sizeof(short))
#  define	_SS_PAD2SIZE	(_SS_MAXSIZE - sizeof(short) - \
				_SS_PAD1SIZE - _SS_ALIGNSIZE)
# endif /* ISC_PLATFORM_HAVESALEN */
#endif

#ifndef INET6_ADDRSTRLEN
# define	INET6_ADDRSTRLEN	46	/* max len of IPv6 addr in ascii */
#endif

/*
 * If we don't have the sockaddr_storage structure
 * we need to define it
 */

#ifndef HAVE_STRUCT_SOCKADDR_STORAGE
struct sockaddr_storage {
#ifdef ISC_PLATFORM_HAVESALEN
	ntp_u_int8_t	ss_len;		/* address length */
	ntp_u_int8_t	ss_family;	/* address family */
#else
	short		ss_family;	/* address family */
#endif
	char		__ss_pad1[_SS_PAD1SIZE];
	ntp_uint64_t	__ss_align;	/* force desired structure storage alignment */
	char		__ss_pad2[_SS_PAD2SIZE];
};
#endif

/*
 * Finally if the platform doesn't support IPv6 we need some
 * additional definitions
 */

/*
 * Flag values for getaddrinfo()
 */
#ifndef AI_PASSIVE
#define	AI_PASSIVE	0x00000001 /* get address to use bind() */
#define	AI_CANONNAME	0x00000002 /* fill ai_canonname */
#define	AI_NUMERICHOST	0x00000004 /* prevent name resolution */
/* valid flags for addrinfo */
#define AI_MASK \
    (AI_PASSIVE | AI_CANONNAME | AI_NUMERICHOST | AI_ADDRCONFIG)

#define	AI_ADDRCONFIG	0x00000400 /* only if any address is assigned */
#endif	/* !AI_PASSIVE */

#ifndef AI_NUMERICHOST		/* such as AIX 4.3 */
# define Z_AI_NUMERICHOST	0
#else
# define Z_AI_NUMERICHOST	AI_NUMERICHOST
#endif

#ifndef AI_NUMERICSERV		/* not in RFC 2553 */
# define Z_AI_NUMERICSERV	0
#else
# define Z_AI_NUMERICSERV	AI_NUMERICSERV
#endif

#ifndef ISC_PLATFORM_HAVEIPV6

#ifdef SYS_WINNT
# define in6_addr in_addr6
#endif

struct addrinfo {
	int	ai_flags;	/* AI_PASSIVE, AI_CANONNAME, AI_NUMERICHOST */
	int	ai_family;	/* PF_xxx */
	int	ai_socktype;	/* SOCK_xxx */
	int	ai_protocol;	/* 0 or IPPROTO_xxx for IPv4 and IPv6 */
	size_t	ai_addrlen;	/* length of ai_addr */
	char	*ai_canonname;	/* canonical name for hostname */
	struct	sockaddr *ai_addr;	/* binary address */
	struct	addrinfo *ai_next;	/* next structure in linked list */
};

/*
 * Error return codes from getaddrinfo()
 */
#define	EAI_ADDRFAMILY	 1	/* address family for hostname not supported */
#define	EAI_AGAIN	 2	/* temporary failure in name resolution */
#define	EAI_BADFLAGS	 3	/* invalid value for ai_flags */
#define	EAI_FAIL	 4	/* non-recoverable failure in name resolution */
#define	EAI_FAMILY	 5	/* ai_family not supported */
#define	EAI_MEMORY	 6	/* memory allocation failure */
#define	EAI_NODATA	 7	/* no address associated with hostname */
#define	EAI_NONAME	 8	/* hostname nor servname provided, or not known */
#define	EAI_SERVICE	 9	/* servname not supported for ai_socktype */
#define	EAI_SOCKTYPE	10	/* ai_socktype not supported */
#define	EAI_SYSTEM	11	/* system error returned in errno */
#define	EAI_BADHINTS	12
#define	EAI_PROTOCOL	13
#define	EAI_MAX		14


int	getaddrinfo (const char *, const char *,
			 const struct addrinfo *, struct addrinfo **);
int	getnameinfo (const struct sockaddr *, u_int, char *,
			 size_t, char *, size_t, int);
void	freeaddrinfo (struct addrinfo *);
char	*gai_strerror (int);

/*
 * Constants for getnameinfo()
 */
#ifndef NI_MAXHOST
#define	NI_MAXHOST	1025
#define	NI_MAXSERV	32
#endif

/*
 * Flag values for getnameinfo()
 */
#ifndef NI_NUMERICHOST
#define	NI_NOFQDN	0x00000001
#define	NI_NUMERICHOST	0x00000002
#define	NI_NAMEREQD	0x00000004
#define	NI_NUMERICSERV	0x00000008
#define	NI_DGRAM	0x00000010
#define NI_WITHSCOPEID	0x00000020
#endif

#endif /* !ISC_PLATFORM_HAVEIPV6 */

/* 
 * Set up some macros to look for IPv6 and IPv6 multicast
 */

#if defined(ISC_PLATFORM_HAVEIPV6) && defined(WANT_IPV6)
# define INCLUDE_IPV6_SUPPORT
# if defined(IPV6_JOIN_GROUP) && defined(IPV6_LEAVE_GROUP)
#  define INCLUDE_IPV6_MULTICAST_SUPPORT
# endif	/* IPV6 Multicast Support */
#endif  /* IPv6 Support */

#endif /* !NTP_RFC2553_H */
