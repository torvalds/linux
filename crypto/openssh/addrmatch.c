/*	$OpenBSD: addrmatch.c,v 1.14 2018/07/31 03:07:24 djm Exp $ */

/*
 * Copyright (c) 2004-2008 Damien Miller <djm@mindrot.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include "includes.h"

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <netdb.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>

#include "match.h"
#include "log.h"

struct xaddr {
	sa_family_t	af;
	union {
		struct in_addr		v4;
		struct in6_addr		v6;
		u_int8_t		addr8[16];
		u_int32_t		addr32[4];
	} xa;		    /* 128-bit address */
	u_int32_t	scope_id;	/* iface scope id for v6 */
#define v4	xa.v4
#define v6	xa.v6
#define addr8	xa.addr8
#define addr32	xa.addr32
};

static int
addr_unicast_masklen(int af)
{
	switch (af) {
	case AF_INET:
		return 32;
	case AF_INET6:
		return 128;
	default:
		return -1;
	}
}

static inline int
masklen_valid(int af, u_int masklen)
{
	switch (af) {
	case AF_INET:
		return masklen <= 32 ? 0 : -1;
	case AF_INET6:
		return masklen <= 128 ? 0 : -1;
	default:
		return -1;
	}
}

/*
 * Convert struct sockaddr to struct xaddr
 * Returns 0 on success, -1 on failure.
 */
static int
addr_sa_to_xaddr(struct sockaddr *sa, socklen_t slen, struct xaddr *xa)
{
	struct sockaddr_in *in4 = (struct sockaddr_in *)sa;
	struct sockaddr_in6 *in6 = (struct sockaddr_in6 *)sa;

	memset(xa, '\0', sizeof(*xa));

	switch (sa->sa_family) {
	case AF_INET:
		if (slen < (socklen_t)sizeof(*in4))
			return -1;
		xa->af = AF_INET;
		memcpy(&xa->v4, &in4->sin_addr, sizeof(xa->v4));
		break;
	case AF_INET6:
		if (slen < (socklen_t)sizeof(*in6))
			return -1;
		xa->af = AF_INET6;
		memcpy(&xa->v6, &in6->sin6_addr, sizeof(xa->v6));
#ifdef HAVE_STRUCT_SOCKADDR_IN6_SIN6_SCOPE_ID
		xa->scope_id = in6->sin6_scope_id;
#endif
		break;
	default:
		return -1;
	}

	return 0;
}

/*
 * Calculate a netmask of length 'l' for address family 'af' and
 * store it in 'n'.
 * Returns 0 on success, -1 on failure.
 */
static int
addr_netmask(int af, u_int l, struct xaddr *n)
{
	int i;

	if (masklen_valid(af, l) != 0 || n == NULL)
		return -1;

	memset(n, '\0', sizeof(*n));
	switch (af) {
	case AF_INET:
		n->af = AF_INET;
		if (l == 0)
			return 0;
		n->v4.s_addr = htonl((0xffffffff << (32 - l)) & 0xffffffff);
		return 0;
	case AF_INET6:
		n->af = AF_INET6;
		for (i = 0; i < 4 && l >= 32; i++, l -= 32)
			n->addr32[i] = 0xffffffffU;
		if (i < 4 && l != 0)
			n->addr32[i] = htonl((0xffffffff << (32 - l)) &
			    0xffffffff);
		return 0;
	default:
		return -1;
	}
}

/*
 * Perform logical AND of addresses 'a' and 'b', storing result in 'dst'.
 * Returns 0 on success, -1 on failure.
 */
static int
addr_and(struct xaddr *dst, const struct xaddr *a, const struct xaddr *b)
{
	int i;

	if (dst == NULL || a == NULL || b == NULL || a->af != b->af)
		return -1;

	memcpy(dst, a, sizeof(*dst));
	switch (a->af) {
	case AF_INET:
		dst->v4.s_addr &= b->v4.s_addr;
		return 0;
	case AF_INET6:
		dst->scope_id = a->scope_id;
		for (i = 0; i < 4; i++)
			dst->addr32[i] &= b->addr32[i];
		return 0;
	default:
		return -1;
	}
}

/*
 * Compare addresses 'a' and 'b'
 * Return 0 if addresses are identical, -1 if (a < b) or 1 if (a > b)
 */
static int
addr_cmp(const struct xaddr *a, const struct xaddr *b)
{
	int i;

	if (a->af != b->af)
		return a->af == AF_INET6 ? 1 : -1;

	switch (a->af) {
	case AF_INET:
		if (a->v4.s_addr == b->v4.s_addr)
			return 0;
		return ntohl(a->v4.s_addr) > ntohl(b->v4.s_addr) ? 1 : -1;
	case AF_INET6:
		for (i = 0; i < 16; i++)
			if (a->addr8[i] - b->addr8[i] != 0)
				return a->addr8[i] > b->addr8[i] ? 1 : -1;
		if (a->scope_id == b->scope_id)
			return 0;
		return a->scope_id > b->scope_id ? 1 : -1;
	default:
		return -1;
	}
}

/*
 * Parse string address 'p' into 'n'
 * Returns 0 on success, -1 on failure.
 */
static int
addr_pton(const char *p, struct xaddr *n)
{
	struct addrinfo hints, *ai = NULL;
	int ret = -1;

	memset(&hints, '\0', sizeof(hints));
	hints.ai_flags = AI_NUMERICHOST;

	if (p == NULL || getaddrinfo(p, NULL, &hints, &ai) != 0)
		goto out;
	if (ai == NULL || ai->ai_addr == NULL)
		goto out;
	if (n != NULL && addr_sa_to_xaddr(ai->ai_addr, ai->ai_addrlen, n) == -1)
		goto out;
	/* success */
	ret = 0;
 out:
	if (ai != NULL)
		freeaddrinfo(ai);
	return ret;
}

/*
 * Perform bitwise negation of address
 * Returns 0 on success, -1 on failure.
 */
static int
addr_invert(struct xaddr *n)
{
	int i;

	if (n == NULL)
		return (-1);

	switch (n->af) {
	case AF_INET:
		n->v4.s_addr = ~n->v4.s_addr;
		return (0);
	case AF_INET6:
		for (i = 0; i < 4; i++)
			n->addr32[i] = ~n->addr32[i];
		return (0);
	default:
		return (-1);
	}
}

/*
 * Calculate a netmask of length 'l' for address family 'af' and
 * store it in 'n'.
 * Returns 0 on success, -1 on failure.
 */
static int
addr_hostmask(int af, u_int l, struct xaddr *n)
{
	if (addr_netmask(af, l, n) == -1 || addr_invert(n) == -1)
		return (-1);
	return (0);
}

/*
 * Test whether address 'a' is all zeros (i.e. 0.0.0.0 or ::)
 * Returns 0 on if address is all-zeros, -1 if not all zeros or on failure.
 */
static int
addr_is_all0s(const struct xaddr *a)
{
	int i;

	switch (a->af) {
	case AF_INET:
		return (a->v4.s_addr == 0 ? 0 : -1);
	case AF_INET6:;
		for (i = 0; i < 4; i++)
			if (a->addr32[i] != 0)
				return (-1);
		return (0);
	default:
		return (-1);
	}
}

/*
 * Test whether host portion of address 'a', as determined by 'masklen'
 * is all zeros.
 * Returns 0 on if host portion of address is all-zeros,
 * -1 if not all zeros or on failure.
 */
static int
addr_host_is_all0s(const struct xaddr *a, u_int masklen)
{
	struct xaddr tmp_addr, tmp_mask, tmp_result;

	memcpy(&tmp_addr, a, sizeof(tmp_addr));
	if (addr_hostmask(a->af, masklen, &tmp_mask) == -1)
		return (-1);
	if (addr_and(&tmp_result, &tmp_addr, &tmp_mask) == -1)
		return (-1);
	return (addr_is_all0s(&tmp_result));
}

/*
 * Parse a CIDR address (x.x.x.x/y or xxxx:yyyy::/z).
 * Return -1 on parse error, -2 on inconsistency or 0 on success.
 */
static int
addr_pton_cidr(const char *p, struct xaddr *n, u_int *l)
{
	struct xaddr tmp;
	long unsigned int masklen = 999;
	char addrbuf[64], *mp, *cp;

	/* Don't modify argument */
	if (p == NULL || strlcpy(addrbuf, p, sizeof(addrbuf)) >= sizeof(addrbuf))
		return -1;

	if ((mp = strchr(addrbuf, '/')) != NULL) {
		*mp = '\0';
		mp++;
		masklen = strtoul(mp, &cp, 10);
		if (*mp == '\0' || *cp != '\0' || masklen > 128)
			return -1;
	}

	if (addr_pton(addrbuf, &tmp) == -1)
		return -1;

	if (mp == NULL)
		masklen = addr_unicast_masklen(tmp.af);
	if (masklen_valid(tmp.af, masklen) == -1)
		return -2;
	if (addr_host_is_all0s(&tmp, masklen) != 0)
		return -2;

	if (n != NULL)
		memcpy(n, &tmp, sizeof(*n));
	if (l != NULL)
		*l = masklen;

	return 0;
}

static int
addr_netmatch(const struct xaddr *host, const struct xaddr *net, u_int masklen)
{
	struct xaddr tmp_mask, tmp_result;

	if (host->af != net->af)
		return -1;

	if (addr_netmask(host->af, masklen, &tmp_mask) == -1)
		return -1;
	if (addr_and(&tmp_result, host, &tmp_mask) == -1)
		return -1;
	return addr_cmp(&tmp_result, net);
}

/*
 * Match "addr" against list pattern list "_list", which may contain a
 * mix of CIDR addresses and old-school wildcards.
 *
 * If addr is NULL, then no matching is performed, but _list is parsed
 * and checked for well-formedness.
 *
 * Returns 1 on match found (never returned when addr == NULL).
 * Returns 0 on if no match found, or no errors found when addr == NULL.
 * Returns -1 on negated match found (never returned when addr == NULL).
 * Returns -2 on invalid list entry.
 */
int
addr_match_list(const char *addr, const char *_list)
{
	char *list, *cp, *o;
	struct xaddr try_addr, match_addr;
	u_int masklen, neg;
	int ret = 0, r;

	if (addr != NULL && addr_pton(addr, &try_addr) != 0) {
		debug2("%s: couldn't parse address %.100s", __func__, addr);
		return 0;
	}
	if ((o = list = strdup(_list)) == NULL)
		return -1;
	while ((cp = strsep(&list, ",")) != NULL) {
		neg = *cp == '!';
		if (neg)
			cp++;
		if (*cp == '\0') {
			ret = -2;
			break;
		}
		/* Prefer CIDR address matching */
		r = addr_pton_cidr(cp, &match_addr, &masklen);
		if (r == -2) {
			debug2("%s: inconsistent mask length for "
			    "match network \"%.100s\"", __func__, cp);
			ret = -2;
			break;
		} else if (r == 0) {
			if (addr != NULL && addr_netmatch(&try_addr,
                           &match_addr, masklen) == 0) {
 foundit:
				if (neg) {
					ret = -1;
					break;
				}
				ret = 1;
			}
			continue;
		} else {
			/* If CIDR parse failed, try wildcard string match */
			if (addr != NULL && match_pattern(addr, cp) == 1)
				goto foundit;
		}
	}
	free(o);

	return ret;
}

/*
 * Match "addr" against list CIDR list "_list". Lexical wildcards and
 * negation are not supported. If "addr" == NULL, will verify structure
 * of "_list".
 *
 * Returns 1 on match found (never returned when addr == NULL).
 * Returns 0 on if no match found, or no errors found when addr == NULL.
 * Returns -1 on error
 */
int
addr_match_cidr_list(const char *addr, const char *_list)
{
	char *list, *cp, *o;
	struct xaddr try_addr, match_addr;
	u_int masklen;
	int ret = 0, r;

	if (addr != NULL && addr_pton(addr, &try_addr) != 0) {
		debug2("%s: couldn't parse address %.100s", __func__, addr);
		return 0;
	}
	if ((o = list = strdup(_list)) == NULL)
		return -1;
	while ((cp = strsep(&list, ",")) != NULL) {
		if (*cp == '\0') {
			error("%s: empty entry in list \"%.100s\"",
			    __func__, o);
			ret = -1;
			break;
		}

		/*
		 * NB. This function is called in pre-auth with untrusted data,
		 * so be extra paranoid about junk reaching getaddrino (via
		 * addr_pton_cidr).
		 */

		/* Stop junk from reaching getaddrinfo. +3 is for masklen */
		if (strlen(cp) > INET6_ADDRSTRLEN + 3) {
			error("%s: list entry \"%.100s\" too long",
			    __func__, cp);
			ret = -1;
			break;
		}
#define VALID_CIDR_CHARS "0123456789abcdefABCDEF.:/"
		if (strspn(cp, VALID_CIDR_CHARS) != strlen(cp)) {
			error("%s: list entry \"%.100s\" contains invalid "
			    "characters", __func__, cp);
			ret = -1;
		}

		/* Prefer CIDR address matching */
		r = addr_pton_cidr(cp, &match_addr, &masklen);
		if (r == -1) {
			error("Invalid network entry \"%.100s\"", cp);
			ret = -1;
			break;
		} else if (r == -2) {
			error("Inconsistent mask length for "
			    "network \"%.100s\"", cp);
			ret = -1;
			break;
		} else if (r == 0 && addr != NULL) {
			if (addr_netmatch(&try_addr, &match_addr,
			    masklen) == 0)
				ret = 1;
			continue;
		}
	}
	free(o);

	return ret;
}
