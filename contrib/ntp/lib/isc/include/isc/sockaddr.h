/*
 * Copyright (C) 2004-2007, 2009  Internet Systems Consortium, Inc. ("ISC")
 * Copyright (C) 1998-2003  Internet Software Consortium.
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

/* $Id: sockaddr.h,v 1.57 2009/01/18 23:48:14 tbox Exp $ */

#ifndef ISC_SOCKADDR_H
#define ISC_SOCKADDR_H 1

/*! \file isc/sockaddr.h */

#include <isc/lang.h>
#include <isc/net.h>
#include <isc/types.h>
#ifdef ISC_PLATFORM_HAVESYSUNH
#include <sys/un.h>
#endif

struct isc_sockaddr {
	union {
		struct sockaddr		sa;
		struct sockaddr_in	sin;
		struct sockaddr_in6	sin6;
#ifdef ISC_PLATFORM_HAVESYSUNH
		struct sockaddr_un	sunix;
#endif
	}				type;
	unsigned int			length;		/* XXXRTH beginning? */
	ISC_LINK(struct isc_sockaddr)	link;
};

typedef ISC_LIST(struct isc_sockaddr)	isc_sockaddrlist_t;

#define ISC_SOCKADDR_CMPADDR	  0x0001	/*%< compare the address
						 *   sin_addr/sin6_addr */
#define ISC_SOCKADDR_CMPPORT 	  0x0002	/*%< compare the port
						 *   sin_port/sin6_port */
#define ISC_SOCKADDR_CMPSCOPE     0x0004	/*%< compare the scope
						 *   sin6_scope */
#define ISC_SOCKADDR_CMPSCOPEZERO 0x0008	/*%< when comparing scopes
						 *   zero scopes always match */

ISC_LANG_BEGINDECLS

isc_boolean_t
isc_sockaddr_compare(const isc_sockaddr_t *a, const isc_sockaddr_t *b,
		     unsigned int flags);
/*%<
 * Compare the elements of the two address ('a' and 'b') as specified
 * by 'flags' and report if they are equal or not.
 *
 * 'flags' is set from ISC_SOCKADDR_CMP*.
 */

isc_boolean_t
isc_sockaddr_equal(const isc_sockaddr_t *a, const isc_sockaddr_t *b);
/*%<
 * Return ISC_TRUE iff the socket addresses 'a' and 'b' are equal.
 */

isc_boolean_t
isc_sockaddr_eqaddr(const isc_sockaddr_t *a, const isc_sockaddr_t *b);
/*%<
 * Return ISC_TRUE iff the address parts of the socket addresses
 * 'a' and 'b' are equal, ignoring the ports.
 */

isc_boolean_t
isc_sockaddr_eqaddrprefix(const isc_sockaddr_t *a, const isc_sockaddr_t *b,
			  unsigned int prefixlen);
/*%<
 * Return ISC_TRUE iff the most significant 'prefixlen' bits of the
 * socket addresses 'a' and 'b' are equal, ignoring the ports.
 * If 'b''s scope is zero then 'a''s scope will be ignored.
 */

unsigned int
isc_sockaddr_hash(const isc_sockaddr_t *sockaddr, isc_boolean_t address_only);
/*%<
 * Return a hash value for the socket address 'sockaddr'.  If 'address_only'
 * is ISC_TRUE, the hash value will not depend on the port.
 *
 * IPv6 addresses containing mapped IPv4 addresses generate the same hash
 * value as the equivalent IPv4 address.
 */

void
isc_sockaddr_any(isc_sockaddr_t *sockaddr);
/*%<
 * Return the IPv4 wildcard address.
 */

void
isc_sockaddr_any6(isc_sockaddr_t *sockaddr);
/*%<
 * Return the IPv6 wildcard address.
 */

void
isc_sockaddr_anyofpf(isc_sockaddr_t *sockaddr, int family);
/*%<
 * Set '*sockaddr' to the wildcard address of protocol family
 * 'family'.
 *
 * Requires:
 * \li	'family' is AF_INET or AF_INET6.
 */

void
isc_sockaddr_fromin(isc_sockaddr_t *sockaddr, const struct in_addr *ina,
		    in_port_t port);
/*%<
 * Construct an isc_sockaddr_t from an IPv4 address and port.
 */

void
isc_sockaddr_fromin6(isc_sockaddr_t *sockaddr, const struct in6_addr *ina6,
		     in_port_t port);
/*%<
 * Construct an isc_sockaddr_t from an IPv6 address and port.
 */

void
isc_sockaddr_v6fromin(isc_sockaddr_t *sockaddr, const struct in_addr *ina,
		      in_port_t port);
/*%<
 * Construct an IPv6 isc_sockaddr_t representing a mapped IPv4 address.
 */

void
isc_sockaddr_fromnetaddr(isc_sockaddr_t *sockaddr, const isc_netaddr_t *na,
			 in_port_t port);
/*%<
 * Construct an isc_sockaddr_t from an isc_netaddr_t and port.
 */

int
isc_sockaddr_pf(const isc_sockaddr_t *sockaddr);
/*%<
 * Get the protocol family of 'sockaddr'.
 *
 * Requires:
 *
 *\li	'sockaddr' is a valid sockaddr with an address family of AF_INET
 *	or AF_INET6.
 *
 * Returns:
 *
 *\li	The protocol family of 'sockaddr', e.g. PF_INET or PF_INET6.
 */

void
isc_sockaddr_setport(isc_sockaddr_t *sockaddr, in_port_t port);
/*%<
 * Set the port of 'sockaddr' to 'port'.
 */

in_port_t
isc_sockaddr_getport(const isc_sockaddr_t *sockaddr);
/*%<
 * Get the port stored in 'sockaddr'.
 */

isc_result_t
isc_sockaddr_totext(const isc_sockaddr_t *sockaddr, isc_buffer_t *target);
/*%<
 * Append a text representation of 'sockaddr' to the buffer 'target'.
 * The text will include both the IP address (v4 or v6) and the port.
 * The text is null terminated, but the terminating null is not
 * part of the buffer's used region.
 *
 * Returns:
 * \li	ISC_R_SUCCESS
 * \li	ISC_R_NOSPACE	The text or the null termination did not fit.
 */

void
isc_sockaddr_format(const isc_sockaddr_t *sa, char *array, unsigned int size);
/*%<
 * Format a human-readable representation of the socket address '*sa'
 * into the character array 'array', which is of size 'size'.
 * The resulting string is guaranteed to be null-terminated.
 */

isc_boolean_t
isc_sockaddr_ismulticast(const isc_sockaddr_t *sa);
/*%<
 * Returns #ISC_TRUE if the address is a multicast address.
 */

isc_boolean_t
isc_sockaddr_isexperimental(const isc_sockaddr_t *sa);
/*
 * Returns ISC_TRUE if the address is a experimental (CLASS E) address.
 */

isc_boolean_t
isc_sockaddr_islinklocal(const isc_sockaddr_t *sa);
/*%<
 * Returns ISC_TRUE if the address is a link local address.
 */

isc_boolean_t
isc_sockaddr_issitelocal(const isc_sockaddr_t *sa);
/*%<
 * Returns ISC_TRUE if the address is a sitelocal address.
 */

isc_result_t
isc_sockaddr_frompath(isc_sockaddr_t *sockaddr, const char *path);
/*
 *  Create a UNIX domain sockaddr that refers to path.
 *
 * Returns:
 * \li	ISC_R_NOSPACE
 * \li	ISC_R_NOTIMPLEMENTED
 * \li	ISC_R_SUCCESS
 */

#define ISC_SOCKADDR_FORMATSIZE \
	sizeof("xxxx:xxxx:xxxx:xxxx:xxxx:xxxx:XXX.XXX.XXX.XXX%SSSSSSSSSS#YYYYY")
/*%<
 * Minimum size of array to pass to isc_sockaddr_format().
 */

ISC_LANG_ENDDECLS

#endif /* ISC_SOCKADDR_H */
