/*
 * Copyright (C) 2004, 2005, 2007, 2010-2012  Internet Systems Consortium, Inc. ("ISC")
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

/*! \file */

#include <config.h>

#include <stdio.h>

#include <isc/buffer.h>
#include <isc/msgs.h>
#include <isc/net.h>
#include <isc/netaddr.h>
#include <isc/print.h>
#include <isc/sockaddr.h>
#include <isc/string.h>
#include <isc/util.h>
#include "ntp_stdlib.h"		/* NTP change for strlcpy, strlcat */

isc_boolean_t
isc_netaddr_equal(const isc_netaddr_t *a, const isc_netaddr_t *b) {
	REQUIRE(a != NULL && b != NULL);

	if (a->family != b->family)
		return (ISC_FALSE);

	if (a->zone != b->zone)
		return (ISC_FALSE);

	switch (a->family) {
	case AF_INET:
		if (a->type.in.s_addr != b->type.in.s_addr)
			return (ISC_FALSE);
		break;
	case AF_INET6:
		if (memcmp(&a->type.in6, &b->type.in6,
			   sizeof(a->type.in6)) != 0 ||
		    a->zone != b->zone)
			return (ISC_FALSE);
		break;
#ifdef ISC_PLATFORM_HAVESYSUNH
	case AF_UNIX:
		if (strcmp(a->type.un, b->type.un) != 0)
			return (ISC_FALSE);
		break;
#endif
	default:
		return (ISC_FALSE);
	}
	return (ISC_TRUE);
}

isc_boolean_t
isc_netaddr_eqprefix(const isc_netaddr_t *a, const isc_netaddr_t *b,
		     unsigned int prefixlen)
{
	const unsigned char *pa = NULL, *pb = NULL;
	unsigned int ipabytes = 0; /* Length of whole IP address in bytes */
	unsigned int nbytes;       /* Number of significant whole bytes */
	unsigned int nbits;        /* Number of significant leftover bits */

	REQUIRE(a != NULL && b != NULL);

	if (a->family != b->family)
		return (ISC_FALSE);

	if (a->zone != b->zone && b->zone != 0)
		return (ISC_FALSE);

	switch (a->family) {
	case AF_INET:
		pa = (const unsigned char *) &a->type.in;
		pb = (const unsigned char *) &b->type.in;
		ipabytes = 4;
		break;
	case AF_INET6:
		pa = (const unsigned char *) &a->type.in6;
		pb = (const unsigned char *) &b->type.in6;
		ipabytes = 16;
		break;
	default:
		return (ISC_FALSE);
	}

	/*
	 * Don't crash if we get a pattern like 10.0.0.1/9999999.
	 */
	if (prefixlen > ipabytes * 8)
		prefixlen = ipabytes * 8;

	nbytes = prefixlen / 8;
	nbits = prefixlen % 8;

	if (nbytes > 0) {
		if (memcmp(pa, pb, nbytes) != 0)
			return (ISC_FALSE);
	}
	if (nbits > 0) {
		unsigned int bytea, byteb, mask;
		INSIST(nbytes < ipabytes);
		INSIST(nbits < 8);
		bytea = pa[nbytes];
		byteb = pb[nbytes];
		mask = (0xFF << (8-nbits)) & 0xFF;
		if ((bytea & mask) != (byteb & mask))
			return (ISC_FALSE);
	}
	return (ISC_TRUE);
}

isc_result_t
isc_netaddr_totext(const isc_netaddr_t *netaddr, isc_buffer_t *target) {
	char abuf[sizeof("xxxx:xxxx:xxxx:xxxx:xxxx:xxxx:255.255.255.255")];
	char zbuf[sizeof("%4294967295")];
	unsigned int alen;
	int zlen;
	const char *r;
	const void *type;

	REQUIRE(netaddr != NULL);

	switch (netaddr->family) {
	case AF_INET:
		type = &netaddr->type.in;
		break;
	case AF_INET6:
		type = &netaddr->type.in6;
		break;
#ifdef ISC_PLATFORM_HAVESYSUNH
	case AF_UNIX:
		alen = strlen(netaddr->type.un);
		if (alen > isc_buffer_availablelength(target))
			return (ISC_R_NOSPACE);
		isc_buffer_putmem(target,
				  (const unsigned char *)(netaddr->type.un),
				  alen);
		return (ISC_R_SUCCESS);
#endif
	default:
		return (ISC_R_FAILURE);
	}
	r = inet_ntop(netaddr->family, type, abuf, sizeof(abuf));
	if (r == NULL)
		return (ISC_R_FAILURE);

	alen = (unsigned int)strlen(abuf); /* no overflow possible */
	INSIST(alen < sizeof(abuf));

	zlen = 0;
	if (netaddr->family == AF_INET6 && netaddr->zone != 0) {
		zlen = snprintf(zbuf, sizeof(zbuf), "%%%u", netaddr->zone);
		if (zlen < 0)
			return (ISC_R_FAILURE);
		INSIST((unsigned int)zlen < sizeof(zbuf));
	}

	if (alen + zlen > isc_buffer_availablelength(target))
		return (ISC_R_NOSPACE);

	isc_buffer_putmem(target, (unsigned char *)abuf, alen);
	isc_buffer_putmem(target, (unsigned char *)zbuf, zlen);

	return (ISC_R_SUCCESS);
}

void
isc_netaddr_format(const isc_netaddr_t *na, char *array, unsigned int size) {
	isc_result_t result;
	isc_buffer_t buf;

	isc_buffer_init(&buf, array, size);
	result = isc_netaddr_totext(na, &buf);

	if (size == 0)
		return;

	/*
	 * Null terminate.
	 */
	if (result == ISC_R_SUCCESS) {
		if (isc_buffer_availablelength(&buf) >= 1)
			isc_buffer_putuint8(&buf, 0);
		else
			result = ISC_R_NOSPACE;
	}

	if (result != ISC_R_SUCCESS) {
		snprintf(array, size,
			 "<%s %u>",
			 isc_msgcat_get(isc_msgcat, ISC_MSGSET_NETADDR,
					ISC_MSG_UNKNOWNADDR,
					"unknown address, family"),
			 na->family);
		array[size - 1] = '\0';
	}
}


isc_result_t
isc_netaddr_prefixok(const isc_netaddr_t *na, unsigned int prefixlen) {
	static const unsigned char zeros[16] = { 0 };
	unsigned int nbits, nbytes, ipbytes = 0;
	const unsigned char *p;

	switch (na->family) {
	case AF_INET:
		p = (const unsigned char *) &na->type.in;
		ipbytes = 4;
		if (prefixlen > 32)
			return (ISC_R_RANGE);
		break;
	case AF_INET6:
		p = (const unsigned char *) &na->type.in6;
		ipbytes = 16;
		if (prefixlen > 128)
			return (ISC_R_RANGE);
		break;
	default:
		return (ISC_R_NOTIMPLEMENTED);
	}
	nbytes = prefixlen / 8;
	nbits = prefixlen % 8;
	if (nbits != 0) {
		if ((p[nbytes] & (0xff>>nbits)) != 0U)
			return (ISC_R_FAILURE);
		nbytes++;
	}
	if (memcmp(p + nbytes, zeros, ipbytes - nbytes) != 0)
		return (ISC_R_FAILURE);
	return (ISC_R_SUCCESS);
}

isc_result_t
isc_netaddr_masktoprefixlen(const isc_netaddr_t *s, unsigned int *lenp) {
	unsigned int nbits = 0, nbytes = 0, ipbytes = 0, i;
	const unsigned char *p;

	switch (s->family) {
	case AF_INET:
		p = (const unsigned char *) &s->type.in;
		ipbytes = 4;
		break;
	case AF_INET6:
		p = (const unsigned char *) &s->type.in6;
		ipbytes = 16;
		break;
	default:
		return (ISC_R_NOTIMPLEMENTED);
	}
	for (i = 0; i < ipbytes; i++) {
		if (p[i] != 0xFF)
			break;
	}
	nbytes = i;
	if (i < ipbytes) {
		unsigned int c = p[nbytes];
		while ((c & 0x80) != 0 && nbits < 8) {
			c <<= 1; nbits++;
		}
		if ((c & 0xFF) != 0)
			return (ISC_R_MASKNONCONTIG);
		i++;
	}
	for (; i < ipbytes; i++) {
		if (p[i] != 0)
			return (ISC_R_MASKNONCONTIG);
	}
	*lenp = nbytes * 8 + nbits;
	return (ISC_R_SUCCESS);
}

void
isc_netaddr_fromin(isc_netaddr_t *netaddr, const struct in_addr *ina) {
	memset(netaddr, 0, sizeof(*netaddr));
	netaddr->family = AF_INET;
	netaddr->type.in = *ina;
}

void
isc_netaddr_fromin6(isc_netaddr_t *netaddr, const struct in6_addr *ina6) {
	memset(netaddr, 0, sizeof(*netaddr));
	netaddr->family = AF_INET6;
	netaddr->type.in6 = *ina6;
}

isc_result_t
isc_netaddr_frompath(isc_netaddr_t *netaddr, const char *path) {
#ifdef ISC_PLATFORM_HAVESYSUNH
	if (strlen(path) > sizeof(netaddr->type.un) - 1)
		return (ISC_R_NOSPACE);

        memset(netaddr, 0, sizeof(*netaddr));
        netaddr->family = AF_UNIX;
	strlcpy(netaddr->type.un, path, sizeof(netaddr->type.un));
        netaddr->zone = 0;
        return (ISC_R_SUCCESS);
#else 
	UNUSED(netaddr);
	UNUSED(path);
	return (ISC_R_NOTIMPLEMENTED);
#endif
}


void
isc_netaddr_setzone(isc_netaddr_t *netaddr, isc_uint32_t zone) {
	/* we currently only support AF_INET6. */
	REQUIRE(netaddr->family == AF_INET6);

	netaddr->zone = zone;
}

isc_uint32_t
isc_netaddr_getzone(const isc_netaddr_t *netaddr) {
	return (netaddr->zone);
}

void
isc_netaddr_fromsockaddr(isc_netaddr_t *t, const isc_sockaddr_t *s) {
	int family = s->type.sa.sa_family;
	t->family = family;
	switch (family) {
	case AF_INET:
		t->type.in = s->type.sin.sin_addr;
		t->zone = 0;
		break;
	case AF_INET6:
		memcpy(&t->type.in6, &s->type.sin6.sin6_addr, 16);
#ifdef ISC_PLATFORM_HAVESCOPEID
		t->zone = s->type.sin6.sin6_scope_id;
#else
		t->zone = 0;
#endif
		break;
#ifdef ISC_PLATFORM_HAVESYSUNH
	case AF_UNIX:
		memcpy(t->type.un, s->type.sunix.sun_path, sizeof(t->type.un));
		t->zone = 0;
		break;
#endif
	default:
		INSIST(0);
	}
}

void
isc_netaddr_any(isc_netaddr_t *netaddr) {
	memset(netaddr, 0, sizeof(*netaddr));
	netaddr->family = AF_INET;
	netaddr->type.in.s_addr = INADDR_ANY;
}

void
isc_netaddr_any6(isc_netaddr_t *netaddr) {
	memset(netaddr, 0, sizeof(*netaddr));
	netaddr->family = AF_INET6;
	netaddr->type.in6 = in6addr_any;
}

isc_boolean_t
isc_netaddr_ismulticast(isc_netaddr_t *na) {
	switch (na->family) {
	case AF_INET:
		return (ISC_TF(ISC_IPADDR_ISMULTICAST(na->type.in.s_addr)));
	case AF_INET6:
		return (ISC_TF(IN6_IS_ADDR_MULTICAST(&na->type.in6)));
	default:
		return (ISC_FALSE);  /* XXXMLG ? */
	}
}

isc_boolean_t
isc_netaddr_isexperimental(isc_netaddr_t *na) {
	switch (na->family) {
	case AF_INET:
		return (ISC_TF(ISC_IPADDR_ISEXPERIMENTAL(na->type.in.s_addr)));
	default:
		return (ISC_FALSE);  /* XXXMLG ? */
	}
}

isc_boolean_t
isc_netaddr_islinklocal(isc_netaddr_t *na) {
	switch (na->family) {
	case AF_INET:
		return (ISC_FALSE);
	case AF_INET6:
		return (ISC_TF(IN6_IS_ADDR_LINKLOCAL(&na->type.in6)));
	default:
		return (ISC_FALSE);
	}
}

isc_boolean_t
isc_netaddr_issitelocal(isc_netaddr_t *na) {
	switch (na->family) {
	case AF_INET:
		return (ISC_FALSE);
	case AF_INET6:
		return (ISC_TF(IN6_IS_ADDR_SITELOCAL(&na->type.in6)));
	default:
		return (ISC_FALSE);
	}
}

void
isc_netaddr_fromv4mapped(isc_netaddr_t *t, const isc_netaddr_t *s) {
	isc_netaddr_t *src;

	DE_CONST(s, src);	/* Must come before IN6_IS_ADDR_V4MAPPED. */

	REQUIRE(s->family == AF_INET6);
	REQUIRE(IN6_IS_ADDR_V4MAPPED(&src->type.in6));

	memset(t, 0, sizeof(*t));
	t->family = AF_INET;
	memcpy(&t->type.in, (char *)&src->type.in6 + 12, 4);
	return;
}
