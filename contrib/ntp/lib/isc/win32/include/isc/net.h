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

#include <errno.h>

/*
 * Also define LWRES_IPV6_H to keep it from being included if liblwres is
 * being used, or redefinition errors will occur.
 */
#define LWRES_IPV6_H 1



/*****
 ***** Module Info
 *****/

/*
 * Basic Networking Types
 *
 * This module is responsible for defining the following basic networking
 * types:
 *
 *		struct in_addr
 *		struct in6_addr
 *		struct in6_pktinfo
 *		struct sockaddr
 *		struct sockaddr_in
 *		struct sockaddr_in6
 *		in_port_t
 *
 * It ensures that the AF_ and PF_ macros are defined.
 *
 * It declares ntoh[sl]() and hton[sl]().
 *
 * It declares inet_aton(), inet_ntop(), and inet_pton().
 *
 * It ensures that INADDR_ANY, IN6ADDR_ANY_INIT, in6addr_any, and
 * in6addr_loopback are available.
 *
 * It ensures that IN_MULTICAST() is available to check for multicast
 * addresses.
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
 *	BSD Socket API
 *	RFC2553
 */

/***
 *** Imports.
 ***/
#include <isc/platform.h>

/*
 * Because of some sort of problem in the MS header files, this cannot
 * be simple "#include <winsock2.h>", because winsock2.h tries to include
 * windows.h, which then generates an error out of mswsock.h.  _You_
 * figure it out.
 */
#ifndef _WINSOCKAPI_
#define _WINSOCKAPI_   /* Prevent inclusion of winsock.h in windows.h */
#endif

#include <winsock2.h>

#include <sys/types.h>

#include <isc/lang.h>
#include <isc/types.h>

#include <ws2tcpip.h>
#include <isc/ipv6.h>

/*
 * This is here because named client, interfacemgr.c, etc. use the name as
 * a variable
 */
#undef interface

#ifndef INADDR_LOOPBACK
#define INADDR_LOOPBACK 0x7f000001UL
#endif

#ifndef ISC_PLATFORM_HAVEIN6PKTINFO
struct in6_pktinfo {
	struct in6_addr ipi6_addr;    /* src/dst IPv6 address */
	unsigned int    ipi6_ifindex; /* send/recv interface index */
};
#endif

#if _MSC_VER < 1300
#define in6addr_any isc_net_in6addrany
#define in6addr_loopback isc_net_in6addrloop
#endif

/*
 * Ensure type in_port_t is defined.
 */
#ifdef ISC_PLATFORM_NEEDPORTT
typedef isc_uint16_t in_port_t;
#endif

/*
 * If this system does not have MSG_TRUNC (as returned from recvmsg())
 * ISC_PLATFORM_RECVOVERFLOW will be defined.  This will enable the MSG_TRUNC
 * faking code in socket.c.
 */
#ifndef MSG_TRUNC
#define ISC_PLATFORM_RECVOVERFLOW
#endif

#define ISC__IPADDR(x)	((isc_uint32_t)htonl((isc_uint32_t)(x)))

#define ISC_IPADDR_ISMULTICAST(i) \
		(((isc_uint32_t)(i) & ISC__IPADDR(0xf0000000)) \
		 == ISC__IPADDR(0xe0000000))

#define ISC_IPADDR_ISEXPERIMENTAL(i) \
		(((isc_uint32_t)(i) & ISC__IPADDR(0xf0000000)) \
		 == ISC__IPADDR(0xf0000000))

/*
 * Fix the FD_SET and FD_CLR Macros to properly cast
 */
#undef FD_CLR
#define FD_CLR(fd, set) do { \
    u_int __i; \
    for (__i = 0; __i < ((fd_set FAR *)(set))->fd_count; __i++) { \
	if (((fd_set FAR *)(set))->fd_array[__i] == (SOCKET) fd) { \
	    while (__i < ((fd_set FAR *)(set))->fd_count-1) { \
		((fd_set FAR *)(set))->fd_array[__i] = \
		    ((fd_set FAR *)(set))->fd_array[__i+1]; \
		__i++; \
	    } \
	    ((fd_set FAR *)(set))->fd_count--; \
	    break; \
	} \
    } \
} while (0)

#undef FD_SET
#define FD_SET(fd, set) do { \
    u_int __i; \
    for (__i = 0; __i < ((fd_set FAR *)(set))->fd_count; __i++) { \
	if (((fd_set FAR *)(set))->fd_array[__i] == (SOCKET)(fd)) { \
	    break; \
	} \
    } \
    if (__i == ((fd_set FAR *)(set))->fd_count) { \
	if (((fd_set FAR *)(set))->fd_count < FD_SETSIZE) { \
	    ((fd_set FAR *)(set))->fd_array[__i] = (SOCKET)(fd); \
	    ((fd_set FAR *)(set))->fd_count++; \
	} \
    } \
} while (0)

/*
 * Windows Sockets errors redefined as regular Berkeley error constants.
 * These are usually commented out in Windows NT to avoid conflicts with errno.h.
 * Use the WSA constants instead.
 * Starting with VC9 (VC++ 2010) many of these are defined incompatibly
 * by errno.h -- #undef the conflicting values, which are not relevant to
 * Win32.
 */

#undef EWOULDBLOCK
#define EWOULDBLOCK             WSAEWOULDBLOCK
#undef EINPROGRESS
#define EINPROGRESS             WSAEINPROGRESS
#undef EALREADY
#define EALREADY                WSAEALREADY
#undef ENOTSOCK
#define ENOTSOCK                WSAENOTSOCK
#undef EDESTADDRREQ
#define EDESTADDRREQ            WSAEDESTADDRREQ
#undef EMSGSIZE
#define EMSGSIZE                WSAEMSGSIZE
#undef EPROTOTYPE
#define EPROTOTYPE              WSAEPROTOTYPE
#undef ENOPROTOOPT
#define ENOPROTOOPT             WSAENOPROTOOPT
#undef EPROTONOSUPPORT
#define EPROTONOSUPPORT         WSAEPROTONOSUPPORT
#define ESOCKTNOSUPPORT         WSAESOCKTNOSUPPORT
#undef EOPNOTSUPP
#define EOPNOTSUPP              WSAEOPNOTSUPP
#define EPFNOSUPPORT            WSAEPFNOSUPPORT
#undef EAFNOSUPPORT
#define EAFNOSUPPORT            WSAEAFNOSUPPORT
#undef EADDRINUSE
#define EADDRINUSE              WSAEADDRINUSE
#undef EADDRNOTAVAIL
#define EADDRNOTAVAIL           WSAEADDRNOTAVAIL
#undef ENETDOWN
#define ENETDOWN                WSAENETDOWN
#undef ENETUNREACH
#define ENETUNREACH             WSAENETUNREACH
#undef ENETRESET
#define ENETRESET               WSAENETRESET
#undef ECONNABORTED
#define ECONNABORTED            WSAECONNABORTED
#undef ECONNRESET
#define ECONNRESET              WSAECONNRESET
#undef ENOBUFS
#define ENOBUFS                 WSAENOBUFS
#undef EISCONN
#define EISCONN                 WSAEISCONN
#undef ENOTCONN
#define ENOTCONN                WSAENOTCONN
#define ESHUTDOWN               WSAESHUTDOWN
#define ETOOMANYREFS            WSAETOOMANYREFS
#undef ETIMEDOUT
#define ETIMEDOUT               WSAETIMEDOUT
#undef ECONNREFUSED
#define ECONNREFUSED            WSAECONNREFUSED
#undef ELOOP
#define ELOOP                   WSAELOOP
#define EHOSTDOWN               WSAEHOSTDOWN
#undef EHOSTUNREACH
#define EHOSTUNREACH            WSAEHOSTUNREACH
#define EPROCLIM                WSAEPROCLIM
#define EUSERS                  WSAEUSERS
#define EDQUOT                  WSAEDQUOT
#define ESTALE                  WSAESTALE
#define EREMOTE                 WSAEREMOTE


/***
 *** Functions.
 ***/

ISC_LANG_BEGINDECLS

isc_result_t
isc_net_probeipv4(void);
/*
 * Check if the system's kernel supports IPv4.
 *
 * Returns:
 *
 *	ISC_R_SUCCESS		IPv4 is supported.
 *	ISC_R_NOTFOUND		IPv4 is not supported.
 *	ISC_R_DISABLED		IPv4 is disabled.
 *	ISC_R_UNEXPECTED
 */

isc_result_t
isc_net_probeipv6(void);
/*
 * Check if the system's kernel supports IPv6.
 *
 * Returns:
 *
 *	ISC_R_SUCCESS		IPv6 is supported.
 *	ISC_R_NOTFOUND		IPv6 is not supported.
 *	ISC_R_DISABLED		IPv6 is disabled.
 *	ISC_R_UNEXPECTED
 */

isc_result_t
isc_net_probeunix(void);
/*
 * Check if UNIX domain sockets are supported.
 *
 * Returns:
 *
 *	ISC_R_SUCCESS
 *	ISC_R_NOTFOUND
 */

isc_result_t
isc_net_probe_ipv6only(void);
/*
 * Check if the system's kernel supports the IPV6_V6ONLY socket option.
 *
 * Returns:
 *
 *	ISC_R_SUCCESS		the option is supported for both TCP and UDP.
 *	ISC_R_NOTFOUND		IPv6 itself or the option is not supported.
 *	ISC_R_UNEXPECTED
 */

isc_result_t
isc_net_probe_ipv6pktinfo(void);
/*
 * Check if the system's kernel supports the IPV6_(RECV)PKTINFO socket option
 * for UDP sockets.
 *
 * Returns:
 *
 *	ISC_R_SUCCESS		the option is supported.
 *	ISC_R_NOTFOUND		IPv6 itself or the option is not supported.
 *	ISC_R_UNEXPECTED
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
#define inet_pton isc_net_pton
#endif

int
isc_net_aton(const char *cp, struct in_addr *addr);
#define inet_aton isc_net_aton

ISC_LANG_ENDDECLS

#endif /* ISC_NET_H */
