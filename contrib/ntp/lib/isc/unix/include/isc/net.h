/*
 * Copyright (C) 2004, 2005, 2007, 2008, 2012  Internet Systems Consortium, Inc. ("ISC")
 * Copyright (C) 1999-2003  Internet Software Consortium.
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

#ifndef ISC_NET_H
#define ISC_NET_H 1

/*****
 ***** Module Info
 *****/

/*! \file
 * \brief
 * Basic Networking Types
 *
 * This module is responsible for defining the following basic networking
 * types:
 *
 *\li		struct in_addr
 *\li		struct in6_addr
 *\li		struct in6_pktinfo
 *\li		struct sockaddr
 *\li		struct sockaddr_in
 *\li		struct sockaddr_in6
 *\li		in_port_t
 *
 * It ensures that the AF_ and PF_ macros are defined.
 *
 * It declares ntoh[sl]() and hton[sl]().
 *
 * It declares inet_aton(), inet_ntop(), and inet_pton().
 *
 * It ensures that #INADDR_LOOPBACK, #INADDR_ANY, #IN6ADDR_ANY_INIT,
 * in6addr_any, and in6addr_loopback are available.
 *
 * It ensures that IN_MULTICAST() is available to check for multicast
 * addresses.
 *
 * MP:
 *\li	No impact.
 *
 * Reliability:
 *\li	No anticipated impact.
 *
 * Resources:
 *\li	N/A.
 *
 * Security:
 *\li	No anticipated impact.
 *
 * Standards:
 *\li	BSD Socket API
 *\li	RFC2553
 */

/***
 *** Imports.
 ***/
#include <isc/platform.h>

#include <sys/types.h>
#include <sys/socket.h>		/* Contractual promise. */

#include <net/if.h>

#include <netinet/in.h>		/* Contractual promise. */
#include <arpa/inet.h>		/* Contractual promise. */
#ifdef ISC_PLATFORM_NEEDNETINETIN6H
#include <netinet/in6.h>	/* Required on UnixWare. */
#endif
#ifdef ISC_PLATFORM_NEEDNETINET6IN6H
#include <netinet6/in6.h>	/* Required on BSD/OS for in6_pktinfo. */
#endif

#ifndef ISC_PLATFORM_HAVEIPV6
#include <isc/ipv6.h>		/* Contractual promise. */
#endif

#include <isc/lang.h>
#include <isc/types.h>

#ifdef ISC_PLATFORM_HAVEINADDR6
#define in6_addr in_addr6	/*%< Required for pre RFC2133 implementations. */
#endif

#ifdef ISC_PLATFORM_HAVEIPV6
#ifndef IN6ADDR_ANY_INIT
#ifdef s6_addr
/*%
 * Required for some pre RFC2133 implementations.
 * IN6ADDR_ANY_INIT and IN6ADDR_LOOPBACK_INIT were added in
 * draft-ietf-ipngwg-bsd-api-04.txt or draft-ietf-ipngwg-bsd-api-05.txt.
 * If 's6_addr' is defined then assume that there is a union and three
 * levels otherwise assume two levels required.
 */
#define IN6ADDR_ANY_INIT { { { 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0 } } }
#else
#define IN6ADDR_ANY_INIT { { 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0 } }
#endif
#endif

#ifndef IN6ADDR_LOOPBACK_INIT
#ifdef s6_addr
/*% IPv6 address loopback init */
#define IN6ADDR_LOOPBACK_INIT { { { 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1 } } }
#else
#define IN6ADDR_LOOPBACK_INIT { { 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1 } }
#endif
#endif

#ifndef IN6_IS_ADDR_V4MAPPED
/*% Is IPv6 address V4 mapped? */
#define IN6_IS_ADDR_V4MAPPED(x) \
	 (memcmp((x)->s6_addr, in6addr_any.s6_addr, 10) == 0 && \
	  (x)->s6_addr[10] == 0xff && (x)->s6_addr[11] == 0xff)
#endif

#ifndef IN6_IS_ADDR_V4COMPAT
/*% Is IPv6 address V4 compatible? */
#define IN6_IS_ADDR_V4COMPAT(x) \
	 (memcmp((x)->s6_addr, in6addr_any.s6_addr, 12) == 0 && \
	 ((x)->s6_addr[12] != 0 || (x)->s6_addr[13] != 0 || \
	  (x)->s6_addr[14] != 0 || \
	  ((x)->s6_addr[15] != 0 && (x)->s6_addr[15] != 1)))
#endif

#ifndef IN6_IS_ADDR_MULTICAST
/*% Is IPv6 address multicast? */
#define IN6_IS_ADDR_MULTICAST(a)        ((a)->s6_addr[0] == 0xff)
#endif

#ifndef IN6_IS_ADDR_LINKLOCAL
/*% Is IPv6 address linklocal? */
#define IN6_IS_ADDR_LINKLOCAL(a) \
	(((a)->s6_addr[0] == 0xfe) && (((a)->s6_addr[1] & 0xc0) == 0x80))
#endif

#ifndef IN6_IS_ADDR_SITELOCAL
/*% is IPv6 address sitelocal? */
#define IN6_IS_ADDR_SITELOCAL(a) \
	(((a)->s6_addr[0] == 0xfe) && (((a)->s6_addr[1] & 0xc0) == 0xc0))
#endif


#ifndef IN6_IS_ADDR_LOOPBACK
/*% is IPv6 address loopback? */
#define IN6_IS_ADDR_LOOPBACK(x) \
	(memcmp((x)->s6_addr, in6addr_loopback.s6_addr, 16) == 0)
#endif
#endif

#ifndef AF_INET6
/*% IPv6 */
#define AF_INET6 99
#endif

#ifndef PF_INET6
/*% IPv6 */
#define PF_INET6 AF_INET6
#endif

#ifndef INADDR_LOOPBACK
/*% inaddr loopback */
#define INADDR_LOOPBACK 0x7f000001UL
#endif

#ifndef ISC_PLATFORM_HAVEIN6PKTINFO
/*% IPv6 packet info */
struct in6_pktinfo {
	struct in6_addr ipi6_addr;    /*%< src/dst IPv6 address */
	unsigned int    ipi6_ifindex; /*%< send/recv interface index */
};
#endif

#if defined(ISC_PLATFORM_NEEDIN6ADDRANY)
extern const struct in6_addr isc_net_in6addrany;
/*%
 * Cope with a missing in6addr_any and in6addr_loopback.
 */
#define in6addr_any isc_net_in6addrany
#endif

#if defined(ISC_PLATFORM_HAVEIPV6) && defined(ISC_PLATFORM_NEEDIN6ADDRLOOPBACK)
extern const struct in6_addr isc_net_in6addrloop;
#define in6addr_loopback isc_net_in6addrloop
#endif

#ifdef ISC_PLATFORM_FIXIN6ISADDR
#undef  IN6_IS_ADDR_GEOGRAPHIC
/*!
 * \brief
 * Fix UnixWare 7.1.1's broken IN6_IS_ADDR_* definitions.
 */
#define IN6_IS_ADDR_GEOGRAPHIC(a) (((a)->S6_un.S6_l[0] & 0xE0) == 0x80)
#undef  IN6_IS_ADDR_IPX
#define IN6_IS_ADDR_IPX(a)        (((a)->S6_un.S6_l[0] & 0xFE) == 0x04)
#undef  IN6_IS_ADDR_LINKLOCAL
#define IN6_IS_ADDR_LINKLOCAL(a)  (((a)->S6_un.S6_l[0] & 0xC0FF) == 0x80FE)
#undef  IN6_IS_ADDR_MULTICAST
#define IN6_IS_ADDR_MULTICAST(a)  (((a)->S6_un.S6_l[0] & 0xFF) == 0xFF)
#undef  IN6_IS_ADDR_NSAP
#define IN6_IS_ADDR_NSAP(a)       (((a)->S6_un.S6_l[0] & 0xFE) == 0x02)
#undef  IN6_IS_ADDR_PROVIDER
#define IN6_IS_ADDR_PROVIDER(a)   (((a)->S6_un.S6_l[0] & 0xE0) == 0x40)
#undef  IN6_IS_ADDR_SITELOCAL
#define IN6_IS_ADDR_SITELOCAL(a)  (((a)->S6_un.S6_l[0] & 0xC0FF) == 0xC0FE)
#endif /* ISC_PLATFORM_FIXIN6ISADDR */

#ifdef ISC_PLATFORM_NEEDPORTT
/*%
 * Ensure type in_port_t is defined.
 */
typedef isc_uint16_t in_port_t;
#endif

#ifndef MSG_TRUNC
/*%
 * If this system does not have MSG_TRUNC (as returned from recvmsg())
 * ISC_PLATFORM_RECVOVERFLOW will be defined.  This will enable the MSG_TRUNC
 * faking code in socket.c.
 */
#define ISC_PLATFORM_RECVOVERFLOW
#endif

/*% IP address. */
#define ISC__IPADDR(x)	((isc_uint32_t)htonl((isc_uint32_t)(x)))

/*% Is IP address multicast? */
#define ISC_IPADDR_ISMULTICAST(i) \
		(((isc_uint32_t)(i) & ISC__IPADDR(0xf0000000)) \
		 == ISC__IPADDR(0xe0000000))

#define ISC_IPADDR_ISEXPERIMENTAL(i) \
		(((isc_uint32_t)(i) & ISC__IPADDR(0xf0000000)) \
		 == ISC__IPADDR(0xf0000000))

/***
 *** Functions.
 ***/

ISC_LANG_BEGINDECLS

isc_result_t
isc_net_probeipv4(void);
/*%<
 * Check if the system's kernel supports IPv4.
 *
 * Returns:
 *
 *\li	#ISC_R_SUCCESS		IPv4 is supported.
 *\li	#ISC_R_NOTFOUND		IPv4 is not supported.
 *\li	#ISC_R_DISABLED		IPv4 is disabled.
 *\li	#ISC_R_UNEXPECTED
 */

isc_result_t
isc_net_probeipv6(void);
/*%<
 * Check if the system's kernel supports IPv6.
 *
 * Returns:
 *
 *\li	#ISC_R_SUCCESS		IPv6 is supported.
 *\li	#ISC_R_NOTFOUND		IPv6 is not supported.
 *\li	#ISC_R_DISABLED		IPv6 is disabled.
 *\li	#ISC_R_UNEXPECTED
 */

isc_result_t
isc_net_probe_ipv6only(void);
/*%<
 * Check if the system's kernel supports the IPV6_V6ONLY socket option.
 *
 * Returns:
 *
 *\li	#ISC_R_SUCCESS		the option is supported for both TCP and UDP.
 *\li	#ISC_R_NOTFOUND		IPv6 itself or the option is not supported.
 *\li	#ISC_R_UNEXPECTED
 */

isc_result_t
isc_net_probe_ipv6pktinfo(void);
/*
 * Check if the system's kernel supports the IPV6_(RECV)PKTINFO socket option
 * for UDP sockets.
 *
 * Returns:
 *
 * \li	#ISC_R_SUCCESS		the option is supported.
 * \li	#ISC_R_NOTFOUND		IPv6 itself or the option is not supported.
 * \li	#ISC_R_UNEXPECTED
 */

void
isc_net_disableipv4(void);

void
isc_net_disableipv6(void);

void
isc_net_enableipv4(void);

void
isc_net_enableipv6(void);

isc_result_t
isc_net_probeunix(void);
/*
 * Returns whether UNIX domain sockets are supported.
 */

isc_result_t
isc_net_getudpportrange(int af, in_port_t *low, in_port_t *high);
/*%<
 * Returns system's default range of ephemeral UDP ports, if defined.
 * If the range is not available or unknown, ISC_NET_PORTRANGELOW and
 * ISC_NET_PORTRANGEHIGH will be returned.
 *
 * Requires:
 *
 *\li	'low' and 'high' must be non NULL.
 *
 * Returns:
 *
 *\li	*low and *high will be the ports specifying the low and high ends of
 *	the range.
 */

#ifdef ISC_PLATFORM_NEEDNTOP
const char *
isc_net_ntop(int af, const void *src, char *dst, size_t size);
#define inet_ntop isc_net_ntop
#endif

#ifdef ISC_PLATFORM_NEEDPTON
int
isc_net_pton(int af, const char *src, void *dst);
#undef inet_pton
#define inet_pton isc_net_pton
#endif

int
isc_net_aton(const char *cp, struct in_addr *addr);
#undef inet_aton
#define inet_aton isc_net_aton

ISC_LANG_ENDDECLS

#endif /* ISC_NET_H */
