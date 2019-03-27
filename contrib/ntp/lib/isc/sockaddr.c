/*
 * Copyright (C) 2004-2007, 2010-2012  Internet Systems Consortium, Inc. ("ISC")
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

/*! \file */

#include <config.h>

#include <stdio.h>

#include <isc/buffer.h>
#include <isc/hash.h>
#include <isc/msgs.h>
#include <isc/netaddr.h>
#include <isc/print.h>
#include <isc/region.h>
#include <isc/sockaddr.h>
#include <isc/string.h>
#include <isc/util.h>

isc_boolean_t
isc_sockaddr_equal(const isc_sockaddr_t *a, const isc_sockaddr_t *b) {
	return (isc_sockaddr_compare(a, b, ISC_SOCKADDR_CMPADDR|
					   ISC_SOCKADDR_CMPPORT|
					   ISC_SOCKADDR_CMPSCOPE));
}

isc_boolean_t
isc_sockaddr_eqaddr(const isc_sockaddr_t *a, const isc_sockaddr_t *b) {
	return (isc_sockaddr_compare(a, b, ISC_SOCKADDR_CMPADDR|
					   ISC_SOCKADDR_CMPSCOPE));
}

isc_boolean_t
isc_sockaddr_compare(const isc_sockaddr_t *a, const isc_sockaddr_t *b,
		     unsigned int flags)
{
	REQUIRE(a != NULL && b != NULL);

	if (a->length != b->length)
		return (ISC_FALSE);

	/*
	 * We don't just memcmp because the sin_zero field isn't always
	 * zero.
	 */

	if (a->type.sa.sa_family != b->type.sa.sa_family)
		return (ISC_FALSE);
	switch (a->type.sa.sa_family) {
	case AF_INET:
		if ((flags & ISC_SOCKADDR_CMPADDR) != 0 &&
		    memcmp(&a->type.sin.sin_addr, &b->type.sin.sin_addr,
			   sizeof(a->type.sin.sin_addr)) != 0)
			return (ISC_FALSE);
		if ((flags & ISC_SOCKADDR_CMPPORT) != 0 &&
		    a->type.sin.sin_port != b->type.sin.sin_port)
			return (ISC_FALSE);
		break;
	case AF_INET6:
		if ((flags & ISC_SOCKADDR_CMPADDR) != 0 &&
		    memcmp(&a->type.sin6.sin6_addr, &b->type.sin6.sin6_addr,
			   sizeof(a->type.sin6.sin6_addr)) != 0)
			return (ISC_FALSE);
#ifdef ISC_PLATFORM_HAVESCOPEID
		/*
		 * If ISC_SOCKADDR_CMPSCOPEZERO is set then don't return
		 * ISC_FALSE if one of the scopes in zero.
		 */
		if ((flags & ISC_SOCKADDR_CMPSCOPE) != 0 &&
		    a->type.sin6.sin6_scope_id != b->type.sin6.sin6_scope_id &&
		    ((flags & ISC_SOCKADDR_CMPSCOPEZERO) == 0 ||
		      (a->type.sin6.sin6_scope_id != 0 &&
		       b->type.sin6.sin6_scope_id != 0)))
			return (ISC_FALSE);
#endif
		if ((flags & ISC_SOCKADDR_CMPPORT) != 0 &&
		    a->type.sin6.sin6_port != b->type.sin6.sin6_port)
			return (ISC_FALSE);
		break;
	default:
		if (memcmp(&a->type, &b->type, a->length) != 0)
			return (ISC_FALSE);
	}
	return (ISC_TRUE);
}

isc_boolean_t
isc_sockaddr_eqaddrprefix(const isc_sockaddr_t *a, const isc_sockaddr_t *b,
			  unsigned int prefixlen)
{
	isc_netaddr_t na, nb;
	isc_netaddr_fromsockaddr(&na, a);
	isc_netaddr_fromsockaddr(&nb, b);
	return (isc_netaddr_eqprefix(&na, &nb, prefixlen));
}

isc_result_t
isc_sockaddr_totext(const isc_sockaddr_t *sockaddr, isc_buffer_t *target) {
	isc_result_t result;
	isc_netaddr_t netaddr;
	char pbuf[sizeof("65000")];
	unsigned int plen;
	isc_region_t avail;

	REQUIRE(sockaddr != NULL);

	/*
	 * Do the port first, giving us the opportunity to check for
	 * unsupported address families before calling
	 * isc_netaddr_fromsockaddr().
	 */
	switch (sockaddr->type.sa.sa_family) {
	case AF_INET:
		snprintf(pbuf, sizeof(pbuf), "%u", ntohs(sockaddr->type.sin.sin_port));
		break;
	case AF_INET6:
		snprintf(pbuf, sizeof(pbuf), "%u", ntohs(sockaddr->type.sin6.sin6_port));
		break;
#ifdef ISC_PLAFORM_HAVESYSUNH
	case AF_UNIX:
		plen = (unsigned int)strlen(sockaddr->type.sunix.sun_path);
		if (plen >= isc_buffer_availablelength(target))
			return (ISC_R_NOSPACE);

		isc_buffer_putmem(target, sockaddr->type.sunix.sun_path, plen);

		/*
		 * Null terminate after used region.
		 */
		isc_buffer_availableregion(target, &avail);
		INSIST(avail.length >= 1);
		avail.base[0] = '\0';

		return (ISC_R_SUCCESS);
#endif
	default:
		return (ISC_R_FAILURE);
	}

	plen = (unsigned int)strlen(pbuf);
	INSIST(plen < sizeof(pbuf));

	isc_netaddr_fromsockaddr(&netaddr, sockaddr);
	result = isc_netaddr_totext(&netaddr, target);
	if (result != ISC_R_SUCCESS)
		return (result);

	if (1 + plen + 1 > isc_buffer_availablelength(target))
		return (ISC_R_NOSPACE);

	isc_buffer_putmem(target, (const unsigned char *)"#", 1);
	isc_buffer_putmem(target, (const unsigned char *)pbuf, plen);

	/*
	 * Null terminate after used region.
	 */
	isc_buffer_availableregion(target, &avail);
	INSIST(avail.length >= 1);
	avail.base[0] = '\0';

	return (ISC_R_SUCCESS);
}

void
isc_sockaddr_format(const isc_sockaddr_t *sa, char *array, unsigned int size) {
	isc_result_t result;
	isc_buffer_t buf;

	if (size == 0U)
		return;

	isc_buffer_init(&buf, array, size);
	result = isc_sockaddr_totext(sa, &buf);
	if (result != ISC_R_SUCCESS) {
		/*
		 * The message is the same as in netaddr.c.
		 */
		snprintf(array, size,
			 "<%s %u>",
			 isc_msgcat_get(isc_msgcat, ISC_MSGSET_NETADDR,
					ISC_MSG_UNKNOWNADDR,
					"unknown address, family"),
			 sa->type.sa.sa_family);
		array[size - 1] = '\0';
	}
}

unsigned int
isc_sockaddr_hash(const isc_sockaddr_t *sockaddr, isc_boolean_t address_only) {
	unsigned int length = 0;
	const unsigned char *s = NULL;
	unsigned int h = 0;
	unsigned int g;
	unsigned int p = 0;
	const struct in6_addr *in6;

	REQUIRE(sockaddr != NULL);

	switch (sockaddr->type.sa.sa_family) {
	case AF_INET:
		s = (const unsigned char *)&sockaddr->type.sin.sin_addr;
		p = ntohs(sockaddr->type.sin.sin_port);
		length = sizeof(sockaddr->type.sin.sin_addr.s_addr);
		break;
	case AF_INET6:
		in6 = &sockaddr->type.sin6.sin6_addr;
		if (IN6_IS_ADDR_V4MAPPED(in6)) {
			s = (const unsigned char *)&in6 + 12;
			length = sizeof(sockaddr->type.sin.sin_addr.s_addr);
		} else {
			s = (const unsigned char *)in6;
			length = sizeof(sockaddr->type.sin6.sin6_addr);
		}
		p = ntohs(sockaddr->type.sin6.sin6_port);
		break;
	default:
		UNEXPECTED_ERROR(__FILE__, __LINE__,
				 "%s: %d",
				 isc_msgcat_get(isc_msgcat,
						ISC_MSGSET_SOCKADDR,
						ISC_MSG_UNKNOWNFAMILY,
						"unknown address family"),
					     (int)sockaddr->type.sa.sa_family);
		s = (const unsigned char *)&sockaddr->type;
		length = sockaddr->length;
		p = 0;
	}

	h = isc_hash_calc(s, length, ISC_TRUE);
	if (!address_only) {
		g = isc_hash_calc((const unsigned char *)&p, sizeof(p),
				  ISC_TRUE);
		h = h ^ g; /* XXX: we should concatenate h and p first */
	}

	return (h);
}

void
isc_sockaddr_any(isc_sockaddr_t *sockaddr)
{
	memset(sockaddr, 0, sizeof(*sockaddr));
	sockaddr->type.sin.sin_family = AF_INET;
#ifdef ISC_PLATFORM_HAVESALEN
	sockaddr->type.sin.sin_len = sizeof(sockaddr->type.sin);
#endif
	sockaddr->type.sin.sin_addr.s_addr = INADDR_ANY;
	sockaddr->type.sin.sin_port = 0;
	sockaddr->length = sizeof(sockaddr->type.sin);
	ISC_LINK_INIT(sockaddr, link);
}

void
isc_sockaddr_any6(isc_sockaddr_t *sockaddr)
{
	memset(sockaddr, 0, sizeof(*sockaddr));
	sockaddr->type.sin6.sin6_family = AF_INET6;
#ifdef ISC_PLATFORM_HAVESALEN
	sockaddr->type.sin6.sin6_len = sizeof(sockaddr->type.sin6);
#endif
	sockaddr->type.sin6.sin6_addr = in6addr_any;
	sockaddr->type.sin6.sin6_port = 0;
	sockaddr->length = sizeof(sockaddr->type.sin6);
	ISC_LINK_INIT(sockaddr, link);
}

void
isc_sockaddr_fromin(isc_sockaddr_t *sockaddr, const struct in_addr *ina,
		    in_port_t port)
{
	memset(sockaddr, 0, sizeof(*sockaddr));
	sockaddr->type.sin.sin_family = AF_INET;
#ifdef ISC_PLATFORM_HAVESALEN
	sockaddr->type.sin.sin_len = sizeof(sockaddr->type.sin);
#endif
	sockaddr->type.sin.sin_addr = *ina;
	sockaddr->type.sin.sin_port = htons(port);
	sockaddr->length = sizeof(sockaddr->type.sin);
	ISC_LINK_INIT(sockaddr, link);
}

void
isc_sockaddr_anyofpf(isc_sockaddr_t *sockaddr, int pf) {
     switch (pf) {
     case AF_INET:
	     isc_sockaddr_any(sockaddr);
	     break;
     case AF_INET6:
	     isc_sockaddr_any6(sockaddr);
	     break;
     default:
	     INSIST(0);
     }
}

void
isc_sockaddr_fromin6(isc_sockaddr_t *sockaddr, const struct in6_addr *ina6,
		     in_port_t port)
{
	memset(sockaddr, 0, sizeof(*sockaddr));
	sockaddr->type.sin6.sin6_family = AF_INET6;
#ifdef ISC_PLATFORM_HAVESALEN
	sockaddr->type.sin6.sin6_len = sizeof(sockaddr->type.sin6);
#endif
	sockaddr->type.sin6.sin6_addr = *ina6;
	sockaddr->type.sin6.sin6_port = htons(port);
	sockaddr->length = sizeof(sockaddr->type.sin6);
	ISC_LINK_INIT(sockaddr, link);
}

void
isc_sockaddr_v6fromin(isc_sockaddr_t *sockaddr, const struct in_addr *ina,
		      in_port_t port)
{
	memset(sockaddr, 0, sizeof(*sockaddr));
	sockaddr->type.sin6.sin6_family = AF_INET6;
#ifdef ISC_PLATFORM_HAVESALEN
	sockaddr->type.sin6.sin6_len = sizeof(sockaddr->type.sin6);
#endif
	sockaddr->type.sin6.sin6_addr.s6_addr[10] = 0xff;
	sockaddr->type.sin6.sin6_addr.s6_addr[11] = 0xff;
	memcpy(&sockaddr->type.sin6.sin6_addr.s6_addr[12], ina, 4);
	sockaddr->type.sin6.sin6_port = htons(port);
	sockaddr->length = sizeof(sockaddr->type.sin6);
	ISC_LINK_INIT(sockaddr, link);
}

int
isc_sockaddr_pf(const isc_sockaddr_t *sockaddr) {

	/*
	 * Get the protocol family of 'sockaddr'.
	 */

#if (AF_INET == PF_INET && AF_INET6 == PF_INET6)
	/*
	 * Assume that PF_xxx == AF_xxx for all AF and PF.
	 */
	return (sockaddr->type.sa.sa_family);
#else
	switch (sockaddr->type.sa.sa_family) {
	case AF_INET:
		return (PF_INET);
	case AF_INET6:
		return (PF_INET6);
	default:
		FATAL_ERROR(__FILE__, __LINE__,
			    isc_msgcat_get(isc_msgcat, ISC_MSGSET_SOCKADDR,
					   ISC_MSG_UNKNOWNFAMILY,
					   "unknown address family: %d"),
			    (int)sockaddr->type.sa.sa_family);
	}
#endif
}

void
isc_sockaddr_fromnetaddr(isc_sockaddr_t *sockaddr, const isc_netaddr_t *na,
		    in_port_t port)
{
	memset(sockaddr, 0, sizeof(*sockaddr));
	sockaddr->type.sin.sin_family = (short)na->family;
	switch (na->family) {
	case AF_INET:
		sockaddr->length = sizeof(sockaddr->type.sin);
#ifdef ISC_PLATFORM_HAVESALEN
		sockaddr->type.sin.sin_len = sizeof(sockaddr->type.sin);
#endif
		sockaddr->type.sin.sin_addr = na->type.in;
		sockaddr->type.sin.sin_port = htons(port);
		break;
	case AF_INET6:
		sockaddr->length = sizeof(sockaddr->type.sin6);
#ifdef ISC_PLATFORM_HAVESALEN
		sockaddr->type.sin6.sin6_len = sizeof(sockaddr->type.sin6);
#endif
		memcpy(&sockaddr->type.sin6.sin6_addr, &na->type.in6, 16);
#ifdef ISC_PLATFORM_HAVESCOPEID
		sockaddr->type.sin6.sin6_scope_id = isc_netaddr_getzone(na);
#endif
		sockaddr->type.sin6.sin6_port = htons(port);
		break;
	default:
		INSIST(0);
	}
	ISC_LINK_INIT(sockaddr, link);
}

void
isc_sockaddr_setport(isc_sockaddr_t *sockaddr, in_port_t port) {
	switch (sockaddr->type.sa.sa_family) {
	case AF_INET:
		sockaddr->type.sin.sin_port = htons(port);
		break;
	case AF_INET6:
		sockaddr->type.sin6.sin6_port = htons(port);
		break;
	default:
		FATAL_ERROR(__FILE__, __LINE__,
			    "%s: %d",
			    isc_msgcat_get(isc_msgcat, ISC_MSGSET_SOCKADDR,
					   ISC_MSG_UNKNOWNFAMILY,
					   "unknown address family"),
			    (int)sockaddr->type.sa.sa_family);
	}
}

in_port_t
isc_sockaddr_getport(const isc_sockaddr_t *sockaddr) {
	in_port_t port = 0;

	switch (sockaddr->type.sa.sa_family) {
	case AF_INET:
		port = ntohs(sockaddr->type.sin.sin_port);
		break;
	case AF_INET6:
		port = ntohs(sockaddr->type.sin6.sin6_port);
		break;
	default:
		FATAL_ERROR(__FILE__, __LINE__,
			    "%s: %d",
			    isc_msgcat_get(isc_msgcat, ISC_MSGSET_SOCKADDR,
					   ISC_MSG_UNKNOWNFAMILY,
					   "unknown address family"),
			    (int)sockaddr->type.sa.sa_family);
	}

	return (port);
}

isc_boolean_t
isc_sockaddr_ismulticast(const isc_sockaddr_t *sockaddr) {
	isc_netaddr_t netaddr;

	if (sockaddr->type.sa.sa_family == AF_INET ||
	    sockaddr->type.sa.sa_family == AF_INET6) {
		isc_netaddr_fromsockaddr(&netaddr, sockaddr);
		return (isc_netaddr_ismulticast(&netaddr));
	}
	return (ISC_FALSE);
}

isc_boolean_t
isc_sockaddr_isexperimental(const isc_sockaddr_t *sockaddr) {
	isc_netaddr_t netaddr;

	if (sockaddr->type.sa.sa_family == AF_INET) {
		isc_netaddr_fromsockaddr(&netaddr, sockaddr);
		return (isc_netaddr_isexperimental(&netaddr));
	}
	return (ISC_FALSE);
}

isc_boolean_t
isc_sockaddr_issitelocal(const isc_sockaddr_t *sockaddr) {
	isc_netaddr_t netaddr;

	if (sockaddr->type.sa.sa_family == AF_INET6) {
		isc_netaddr_fromsockaddr(&netaddr, sockaddr);
		return (isc_netaddr_issitelocal(&netaddr));
	}
	return (ISC_FALSE);
}

isc_boolean_t
isc_sockaddr_islinklocal(const isc_sockaddr_t *sockaddr) {
	isc_netaddr_t netaddr;

	if (sockaddr->type.sa.sa_family == AF_INET6) {
		isc_netaddr_fromsockaddr(&netaddr, sockaddr);
		return (isc_netaddr_islinklocal(&netaddr));
	}
	return (ISC_FALSE);
}

isc_result_t
isc_sockaddr_frompath(isc_sockaddr_t *sockaddr, const char *path) {
#ifdef ISC_PLATFORM_HAVESYSUNH
	if (strlen(path) >= sizeof(sockaddr->type.sunix.sun_path))
		return (ISC_R_NOSPACE);
	memset(sockaddr, 0, sizeof(*sockaddr));
	sockaddr->length = sizeof(sockaddr->type.sunix);
	sockaddr->type.sunix.sun_family = AF_UNIX;
#ifdef ISC_PLATFORM_HAVESALEN
	sockaddr->type.sunix.sun_len =
			(unsigned char)sizeof(sockaddr->type.sunix);
#endif
	strcpy(sockaddr->type.sunix.sun_path, path);
	return (ISC_R_SUCCESS);
#else
	UNUSED(sockaddr);
	UNUSED(path);
	return (ISC_R_NOTIMPLEMENTED);
#endif
}
