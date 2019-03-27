/*
 * Copyright (C) 2004, 2005, 2007, 2011, 2012  Internet Systems Consortium, Inc. ("ISC")
 * Copyright (C) 1999-2002  Internet Software Consortium.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND ISC DISCLAIMS ALL WARRANTIES WITH
 * REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY
 * AND FITNESS.  IN NO EVENT SHALL ISC BE LIABLE FOR ANY SPECIAL, DIRECT,
 * INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM
 * LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE
 * OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 */

/* $Id$ */

#ifndef ISC_IPV6_H
#define ISC_IPV6_H 1

/*****
 ***** Module Info
 *****/

/*
 * IPv6 definitions for systems which do not support IPv6.
 *
 * MP:
 *	No impact.
 *
 * Reliability:
 *	No anticipated impact.
 *
 * Resources:
 *	N/A.
 *
 * Security:
 *	No anticipated impact.
 *
 * Standards:
 *	RFC2553.
 */

#if _MSC_VER < 1300
#define in6_addr in_addr6
#endif

#ifndef IN6ADDR_ANY_INIT
#define IN6ADDR_ANY_INIT 	{{ 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0 }}
#endif
#ifndef IN6ADDR_LOOPBACK_INIT
#define IN6ADDR_LOOPBACK_INIT 	{{ 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1 }}
#endif

LIBISC_EXTERNAL_DATA extern const struct in6_addr isc_net_in6addrany;
LIBISC_EXTERNAL_DATA extern const struct in6_addr isc_net_in6addrloop;

/*
 * Unspecified
 */
#ifndef IN6_IS_ADDR_UNSPECIFIED
#define IN6_IS_ADDR_UNSPECIFIED(a) (\
*((u_long *)((a)->s6_addr)    ) == 0 && \
*((u_long *)((a)->s6_addr) + 1) == 0 && \
*((u_long *)((a)->s6_addr) + 2) == 0 && \
*((u_long *)((a)->s6_addr) + 3) == 0 \
)
#endif

/*
 * Loopback
 */
#ifndef IN6_IS_ADDR_LOOPBACK
#define IN6_IS_ADDR_LOOPBACK(a) (\
*((u_long *)((a)->s6_addr)    ) == 0 && \
*((u_long *)((a)->s6_addr) + 1) == 0 && \
*((u_long *)((a)->s6_addr) + 2) == 0 && \
*((u_long *)((a)->s6_addr) + 3) == htonl(1) \
)
#endif

/*
 * IPv4 compatible
 */
#define IN6_IS_ADDR_V4COMPAT(a)  (\
*((u_long *)((a)->s6_addr)    ) == 0 && \
*((u_long *)((a)->s6_addr) + 1) == 0 && \
*((u_long *)((a)->s6_addr) + 2) == 0 && \
*((u_long *)((a)->s6_addr) + 3) != 0 && \
*((u_long *)((a)->s6_addr) + 3) != htonl(1) \
)

/*
 * Mapped
 */
#define IN6_IS_ADDR_V4MAPPED(a) (\
*((u_long *)((a)->s6_addr)    ) == 0 && \
*((u_long *)((a)->s6_addr) + 1) == 0 && \
*((u_long *)((a)->s6_addr) + 2) == htonl(0x0000ffff))

/*
 * Multicast
 */
#define IN6_IS_ADDR_MULTICAST(a)	\
	((a)->s6_addr[0] == 0xffU)

/*
 * Unicast link / site local.
 */
#ifndef IN6_IS_ADDR_LINKLOCAL
#define IN6_IS_ADDR_LINKLOCAL(a)	(\
	((a)->s6_addr[0] == 0xfe) && \
	(((a)->s6_addr[1] & 0xc0) == 0x80))
#endif

#ifndef IN6_IS_ADDR_SITELOCAL
#define IN6_IS_ADDR_SITELOCAL(a)	(\
	((a)->s6_addr[0] == 0xfe) && \
	(((a)->s6_addr[1] & 0xc0) == 0xc0))
#endif

#endif /* ISC_IPV6_H */
