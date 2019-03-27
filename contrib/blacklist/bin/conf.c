/*	$NetBSD: conf.c,v 1.24 2016/04/04 15:52:56 christos Exp $	*/

/*-
 * Copyright (c) 2015 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Christos Zoulas.
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
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <sys/cdefs.h>
__RCSID("$NetBSD: conf.c,v 1.24 2016/04/04 15:52:56 christos Exp $");

#include <stdio.h>
#ifdef HAVE_LIBUTIL_H
#include <libutil.h>
#endif
#ifdef HAVE_UTIL_H
#include <util.h>
#endif
#include <string.h>
#include <ctype.h>
#include <inttypes.h>
#include <netdb.h>
#include <pwd.h>
#include <syslog.h>
#include <errno.h>
#include <stdlib.h>
#include <limits.h>
#include <ifaddrs.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <net/if.h>
#include <sys/socket.h>

#include "bl.h"
#include "internal.h"
#include "support.h"
#include "conf.h"


struct sockaddr_if {
	uint8_t		sif_len;
	sa_family_t	sif_family;
	in_port_t	sif_port;
	char		sif_name[16];
};

#define SIF_NAME(a) \
    ((const struct sockaddr_if *)(const void *)(a))->sif_name

static int conf_is_interface(const char *);

#define FSTAR	-1
#define FEQUAL	-2

static void
advance(char **p)
{
	char *ep = *p;
	while (*ep && !isspace((unsigned char)*ep))
		ep++;
	while (*ep && isspace((unsigned char)*ep))
		*ep++ = '\0';
	*p = ep;
}

static int
getnum(const char *f, size_t l, bool local, void *rp, const char *name,
    const char *p)
{
	int e;
	intmax_t im;
	int *r = rp;

	if (strcmp(p, "*") == 0) {
		*r = FSTAR;
		return 0;
	}
	if (strcmp(p, "=") == 0) {
		if (local)
			goto out;
		*r = FEQUAL;
		return 0;
	}

	im = strtoi(p, NULL, 0, 0, INT_MAX, &e);
	if (e == 0) {
		*r = (int)im;
		return 0;
	}

	if (f == NULL)
		return -1;
	(*lfun)(LOG_ERR, "%s: %s, %zu: Bad number for %s [%s]", __func__, f, l,
	   name,  p);
	return -1;
out:
	(*lfun)(LOG_ERR, "%s: %s, %zu: `=' for %s not allowed in local config",
	    __func__, f, l, name);
	return -1;

}

static int
getnfail(const char *f, size_t l, bool local, struct conf *c, const char *p)
{
	return getnum(f, l, local, &c->c_nfail, "nfail", p);
}

static int
getsecs(const char *f, size_t l, bool local, struct conf *c, const char *p)
{
	int e;
	char *ep;
	intmax_t tot, im;

	tot = 0;
	if (strcmp(p, "*") == 0) {
		c->c_duration = FSTAR;
		return 0;
	}
	if (strcmp(p, "=") == 0) {
		if (local)
			goto out;
		c->c_duration = FEQUAL;
		return 0;
	}
again:
	im = strtoi(p, &ep, 0, 0, INT_MAX, &e);

	if (e == ENOTSUP) {
		switch (*ep) {
		case 'd':
			im *= 24;
			/*FALLTHROUGH*/
		case 'h':
			im *= 60;
			/*FALLTHROUGH*/
		case 'm':
			im *= 60;
			/*FALLTHROUGH*/
		case 's':
			e = 0;
			tot += im;
			if (ep[1] != '\0') {
				p = ep + 2;
				goto again;
			}
			break;
		}
	} else	
		tot = im;
			
	if (e == 0) {
		c->c_duration = (int)tot;
		return 0;
	}

	if (f == NULL)
		return -1;
	(*lfun)(LOG_ERR, "%s: %s, %zu: Bad number [%s]", __func__, f, l, p);
	return -1;
out:
	(*lfun)(LOG_ERR, "%s: %s, %zu: `=' duration not allowed in local"
	    " config", __func__, f, l);
	return -1;

}

static int
getport(const char *f, size_t l, bool local, void *r, const char *p)
{
	struct servent *sv;

	// XXX: Pass in the proto instead
	if ((sv = getservbyname(p, "tcp")) != NULL) {
		*(int *)r = ntohs(sv->s_port);
		return 0;
	}
	if ((sv = getservbyname(p, "udp")) != NULL) {
		*(int *)r = ntohs(sv->s_port);
		return 0;
	}

	return getnum(f, l, local, r, "service", p);
}

static int
getmask(const char *f, size_t l, bool local, const char **p, int *mask)
{
	char *d;
	const char *s = *p; 

	if ((d = strchr(s, ':')) != NULL) {
		*d++ = '\0';
		*p = d;
	}
	if ((d = strchr(s, '/')) == NULL) {
		*mask = FSTAR;
		return 0;
	}

	*d++ = '\0';
	return getnum(f, l, local, mask, "mask", d);
}

static int
gethostport(const char *f, size_t l, bool local, struct conf *c, const char *p)
{
	char *d;	// XXX: Ok to write to string.
	in_port_t *port = NULL;
	const char *pstr;

	if (strcmp(p, "*") == 0) {
		c->c_port = FSTAR;
		c->c_lmask = FSTAR;
		return 0;
	}

	if ((d = strchr(p, ']')) != NULL) {
		*d++ = '\0';
		pstr = d;
		p++;
	} else
		pstr = p;

	if (getmask(f, l, local, &pstr, &c->c_lmask) == -1)
		goto out;

	if (d) {
		struct sockaddr_in6 *sin6 = (void *)&c->c_ss;
		if (debug)
			(*lfun)(LOG_DEBUG, "%s: host6 %s", __func__, p);
		if (strcmp(p, "*") != 0) {
			if (inet_pton(AF_INET6, p, &sin6->sin6_addr) == -1)
				goto out;
			sin6->sin6_family = AF_INET6;
#ifdef HAVE_STRUCT_SOCKADDR_SA_LEN
			sin6->sin6_len = sizeof(*sin6);
#endif
			port = &sin6->sin6_port;
		} 
	} else if (pstr != p || strchr(p, '.') || conf_is_interface(p)) {
		if (pstr == p)
			pstr = "*";
		struct sockaddr_in *sin = (void *)&c->c_ss;
		struct sockaddr_if *sif = (void *)&c->c_ss;
		if (debug)
			(*lfun)(LOG_DEBUG, "%s: host4 %s", __func__, p);
		if (strcmp(p, "*") != 0) {
			if (conf_is_interface(p)) {
				if (!local)
					goto out2;
				if (debug)
					(*lfun)(LOG_DEBUG, "%s: interface %s",
					    __func__, p);
				if (c->c_lmask != FSTAR)
					goto out1;
				sif->sif_family = AF_MAX;
				strlcpy(sif->sif_name, p,
				    sizeof(sif->sif_name));
#ifdef HAVE_STRUCT_SOCKADDR_SA_LEN
				sif->sif_len = sizeof(*sif);
#endif
				port = &sif->sif_port;
			} else if (inet_pton(AF_INET, p, &sin->sin_addr) != -1)
			{
				sin->sin_family = AF_INET;
#ifdef HAVE_STRUCT_SOCKADDR_SA_LEN
				sin->sin_len = sizeof(*sin);
#endif
				port = &sin->sin_port;
			} else
				goto out;
		}
	}

	if (getport(f, l, local, &c->c_port, pstr) == -1)
		return -1;

	if (port && c->c_port != FSTAR && c->c_port != FEQUAL)
		*port = htons((in_port_t)c->c_port);
	return 0;
out:
	(*lfun)(LOG_ERR, "%s: %s, %zu: Bad address [%s]", __func__, f, l, pstr);
	return -1;
out1:
	(*lfun)(LOG_ERR, "%s: %s, %zu: Can't specify mask %d with "
	    "interface [%s]", __func__, f, l, c->c_lmask, p);
	return -1;
out2:
	(*lfun)(LOG_ERR, "%s: %s, %zu: Interface spec does not make sense "
	    "with remote config [%s]", __func__, f, l, p);
	return -1;
}

static int
getproto(const char *f, size_t l, bool local __unused, struct conf *c,
    const char *p)
{
	if (strcmp(p, "stream") == 0) {
		c->c_proto = IPPROTO_TCP;
		return 0;
	}
	if (strcmp(p, "dgram") == 0) {
		c->c_proto = IPPROTO_UDP;
		return 0;
	}
	return getnum(f, l, local, &c->c_proto, "protocol", p);
}

static int
getfamily(const char *f, size_t l, bool local __unused, struct conf *c,
    const char *p)
{
	if (strncmp(p, "tcp", 3) == 0 || strncmp(p, "udp", 3) == 0) {
		c->c_family = p[3] == '6' ? AF_INET6 : AF_INET;
		return 0;
	}
	return getnum(f, l, local, &c->c_family, "family", p);
}

static int
getuid(const char *f, size_t l, bool local __unused, struct conf *c,
    const char *p)
{
	struct passwd *pw;

	if ((pw = getpwnam(p)) != NULL) {
		c->c_uid = (int)pw->pw_uid;
		return 0;
	}

	return getnum(f, l, local, &c->c_uid, "user", p);
}


static int
getname(const char *f, size_t l, bool local, struct conf *c,
    const char *p)
{
	if (getmask(f, l, local, &p, &c->c_rmask) == -1)
		return -1;
		
	if (strcmp(p, "*") == 0) {
		strlcpy(c->c_name, rulename, CONFNAMESZ);
		return 0;
	}
	if (strcmp(p, "=") == 0) {
		if (local)
			goto out;
		c->c_name[0] = '\0';
		return 0;
	}

	snprintf(c->c_name, CONFNAMESZ, "%s%s", *p == '-' ? rulename : "", p);
	return 0;
out:
	(*lfun)(LOG_ERR, "%s: %s, %zu: `=' name not allowed in local"
	    " config", __func__, f, l);
	return -1;
}

static int
getvalue(const char *f, size_t l, bool local, void *r, char **p,
    int (*fun)(const char *, size_t, bool, struct conf *, const char *))
{
	char *ep = *p;

	advance(p);
	return (*fun)(f, l, local, r, ep);
}


static int
conf_parseline(const char *f, size_t l, char *p, struct conf *c, bool local)
{
	int e;

	while (*p && isspace((unsigned char)*p))
		p++;

	memset(c, 0, sizeof(*c));
	e = getvalue(f, l, local, c, &p, gethostport);
	if (e) return -1;
	e = getvalue(f, l, local, c, &p, getproto);
	if (e) return -1;
	e = getvalue(f, l, local, c, &p, getfamily);
	if (e) return -1;
	e = getvalue(f, l, local, c, &p, getuid);
	if (e) return -1;
	e = getvalue(f, l, local, c, &p, getname);
	if (e) return -1;
	e = getvalue(f, l, local, c, &p, getnfail);
	if (e) return -1;
	e = getvalue(f, l, local, c, &p, getsecs);
	if (e) return -1;

	return 0;
}

static int
conf_sort(const void *v1, const void *v2)
{
	const struct conf *c1 = v1;
	const struct conf *c2 = v2;

#define CMP(a, b, f) \
	if ((a)->f > (b)->f) return -1; \
	else if ((a)->f < (b)->f) return 1

	CMP(c1, c2, c_ss.ss_family);
	CMP(c1, c2, c_lmask);
	CMP(c1, c2, c_port);
	CMP(c1, c2, c_proto);
	CMP(c1, c2, c_family);
	CMP(c1, c2, c_rmask);
	CMP(c1, c2, c_uid);
#undef CMP
	return 0;
}

static int
conf_is_interface(const char *name)
{
	const struct ifaddrs *ifa;

	for (ifa = ifas; ifa; ifa = ifa->ifa_next)
		if (strcmp(ifa->ifa_name, name) == 0)
			return 1;
	return 0;
}

#define MASK(m)  ((uint32_t)~((1 << (32 - (m))) - 1))

static int
conf_amask_eq(const void *v1, const void *v2, size_t len, int mask)
{
	const uint32_t *a1 = v1;
	const uint32_t *a2 = v2;
	uint32_t m;
	int omask = mask;

	len >>= 2;
	switch (mask) {
	case FSTAR:
		if (memcmp(v1, v2, len) == 0)
			return 1;
		goto out;
	case FEQUAL:
		
		(*lfun)(LOG_CRIT, "%s: Internal error: bad mask %d", __func__,
		    mask);
		abort();
	default:
		break;
	}

	for (size_t i = 0; i < len; i++) {
		if (mask > 32) {
			m = htonl((uint32_t)~0);
			mask -= 32;
		} else if (mask) {
			m = htonl(MASK(mask));
			mask = 0;
		} else
			return 1;
		if ((a1[i] & m) != (a2[i] & m))
			goto out;
	}
	return 1;
out:
	if (debug > 1) {
		char b1[256], b2[256];
		len <<= 2;
		blhexdump(b1, sizeof(b1), "a1", v1, len);
		blhexdump(b2, sizeof(b2), "a2", v2, len);
		(*lfun)(LOG_DEBUG, "%s: %s != %s [0x%x]", __func__,
		    b1, b2, omask);
	}
	return 0;
}

/*
 * Apply the mask to the given address
 */
static void
conf_apply_mask(void *v, size_t len, int mask)
{
	uint32_t *a = v;
	uint32_t m;

	switch (mask) {
	case FSTAR:
		return;
	case FEQUAL:
		(*lfun)(LOG_CRIT, "%s: Internal error: bad mask %d", __func__,
		    mask);
		abort();
	default:
		break;
	}
	len >>= 2;

	for (size_t i = 0; i < len; i++) {
		if (mask > 32) {
			m = htonl((uint32_t)~0);
			mask -= 32;
		} else if (mask) {
			m = htonl(MASK(mask));
			mask = 0;
		} else
			m = 0;
		a[i] &= m;
	}
}

/*
 * apply the mask and the port to the address given
 */
static void
conf_addr_set(struct conf *c, const struct sockaddr_storage *ss)
{
	struct sockaddr_in *sin;
	struct sockaddr_in6 *sin6;
	in_port_t *port;
	void *addr;
	size_t alen;

	c->c_lmask = c->c_rmask;
	c->c_ss = *ss;

	if (c->c_ss.ss_family != c->c_family) {
		(*lfun)(LOG_CRIT, "%s: Internal error: mismatched family "
		    "%u != %u", __func__, c->c_ss.ss_family, c->c_family);
		abort();
	}

	switch (c->c_ss.ss_family) {
	case AF_INET:
		sin = (void *)&c->c_ss;
		port = &sin->sin_port;
		addr = &sin->sin_addr;
		alen = sizeof(sin->sin_addr);
		break;
	case AF_INET6:
		sin6 = (void *)&c->c_ss;
		port = &sin6->sin6_port;
		addr = &sin6->sin6_addr;
		alen = sizeof(sin6->sin6_addr);
		break;
	default:
		(*lfun)(LOG_CRIT, "%s: Internal error: bad family %u",
		    __func__, c->c_ss.ss_family);
		abort();
	}

	*port = htons((in_port_t)c->c_port);
	conf_apply_mask(addr, alen, c->c_lmask);
	if (c->c_lmask == FSTAR)
		c->c_lmask = (int)(alen * 8);
	if (debug) {
		char buf[128];
		sockaddr_snprintf(buf, sizeof(buf), "%a:%p", (void *)&c->c_ss);
		(*lfun)(LOG_DEBUG, "Applied address %s", buf);
	}
}

/*
 * Compared two addresses for equality applying the mask
 */
static int
conf_inet_eq(const void *v1, const void *v2, int mask)
{
	const struct sockaddr *sa1 = v1;
	const struct sockaddr *sa2 = v2;
	size_t size;

	if (sa1->sa_family != sa2->sa_family)
		return 0;

	switch (sa1->sa_family) {
	case AF_INET: {
		const struct sockaddr_in *s1 = v1;
		const struct sockaddr_in *s2 = v2;
		size = sizeof(s1->sin_addr);
		v1 = &s1->sin_addr;
		v2 = &s2->sin_addr;
		break;
	}

	case AF_INET6: {
		const struct sockaddr_in6 *s1 = v1;
		const struct sockaddr_in6 *s2 = v2;
		size = sizeof(s1->sin6_addr);
		v1 = &s1->sin6_addr;
		v2 = &s2->sin6_addr;
		break;
	}

	default:
		(*lfun)(LOG_CRIT, "%s: Internal error: bad family %u",
		    __func__, sa1->sa_family);
		abort();
	}

	return conf_amask_eq(v1, v2, size, mask);
}

static int
conf_addr_in_interface(const struct sockaddr_storage *s1,
    const struct sockaddr_storage *s2, int mask)
{
	const char *name = SIF_NAME(s2);
	const struct ifaddrs *ifa;

	for (ifa = ifas; ifa; ifa = ifa->ifa_next) {
		if ((ifa->ifa_flags & IFF_UP) == 0)
			continue;

		if (strcmp(ifa->ifa_name, name) != 0)
			continue;

		if (s1->ss_family != ifa->ifa_addr->sa_family)
			continue;

		bool eq;
		switch (s1->ss_family) {
		case AF_INET:
		case AF_INET6:
			eq = conf_inet_eq(ifa->ifa_addr, s1, mask);
			break;
		default:
			(*lfun)(LOG_ERR, "Bad family %u", s1->ss_family);
			continue;
		}
		if (eq)
			return 1;
	}
	return 0;
}

static int
conf_addr_eq(const struct sockaddr_storage *s1,
    const struct sockaddr_storage *s2, int mask)
{
	switch (s2->ss_family) {
	case 0:
		return 1;
	case AF_MAX:
		return conf_addr_in_interface(s1, s2, mask);
	case AF_INET:
	case AF_INET6:
		return conf_inet_eq(s1, s2, mask);
	default:
		(*lfun)(LOG_CRIT, "%s: Internal error: bad family %u",
		    __func__, s1->ss_family);
		abort();
	}
}

static int
conf_eq(const struct conf *c1, const struct conf *c2)
{
		
	if (!conf_addr_eq(&c1->c_ss, &c2->c_ss, c2->c_lmask))
		return 0;

#define CMP(a, b, f) \
	if ((a)->f != (b)->f && (b)->f != FSTAR && (b)->f != FEQUAL) { \
		if (debug > 1) \
			(*lfun)(LOG_DEBUG, "%s: %s fail %d != %d", __func__, \
			    __STRING(f), (a)->f, (b)->f); \
		return 0; \
	}
	CMP(c1, c2, c_port);
	CMP(c1, c2, c_proto);
	CMP(c1, c2, c_family);
	CMP(c1, c2, c_uid);
#undef CMP
	return 1;
}

static const char *
conf_num(char *b, size_t l, int n)
{
	switch (n) {
	case FSTAR:
		return "*";
	case FEQUAL:
		return "=";
	default:
		snprintf(b, l, "%d", n);
		return b;
	}
}

static const char *
fmtname(const char *n) {
	size_t l = strlen(rulename);
	if (l == 0)
		return "*";
	if (strncmp(n, rulename, l) == 0) {
		if (n[l] != '\0')
			return n + l;
		else
			return "*";
	} else if (!*n)
		return "=";
	else
		return n;
}

static void
fmtport(char *b, size_t l, int port)
{
	char buf[128];

	if (port == FSTAR)
		return;

	if (b[0] == '\0' || strcmp(b, "*") == 0) 
		snprintf(b, l, "%d", port);
	else {
		snprintf(buf, sizeof(buf), ":%d", port);
		strlcat(b, buf, l);
	}
}

static const char *
fmtmask(char *b, size_t l, int fam, int mask)
{
	char buf[128];

	switch (mask) {
	case FSTAR:
		return "";
	case FEQUAL:
		if (strcmp(b, "=") == 0)
			return "";
		else {
			strlcat(b, "/=", l);
			return b;
		}
	default:
		break;
	}

	switch (fam) {
	case AF_INET:
		if (mask == 32)
			return "";
		break;
	case AF_INET6:
		if (mask == 128)
			return "";
		break;
	default:
		break;
	}

	snprintf(buf, sizeof(buf), "/%d", mask);
	strlcat(b, buf, l);
	return b;
}

static const char *
conf_namemask(char *b, size_t l, const struct conf *c)
{
	strlcpy(b, fmtname(c->c_name), l);
	fmtmask(b, l, c->c_family, c->c_rmask);
	return b;
}

const char *
conf_print(char *buf, size_t len, const char *pref, const char *delim,
    const struct conf *c)
{
	char ha[128], hb[32], b[5][64];
	int sp;

#define N(n, v) conf_num(b[n], sizeof(b[n]), (v))

	switch (c->c_ss.ss_family) {
	case 0:
		snprintf(ha, sizeof(ha), "*");
		break;
	case AF_MAX:
		snprintf(ha, sizeof(ha), "%s", SIF_NAME(&c->c_ss));
		break;
	default:
		sockaddr_snprintf(ha, sizeof(ha), "%a", (const void *)&c->c_ss);
		break;
	}

	fmtmask(ha, sizeof(ha), c->c_family, c->c_lmask);
	fmtport(ha, sizeof(ha), c->c_port);
	
	sp = *delim == '\t' ? 20 : -1;
	hb[0] = '\0';
	if (*delim)
		snprintf(buf, len, "%s%*.*s%s%s%s" "%s%s%s%s"
		    "%s%s" "%s%s%s",
		    pref, sp, sp, ha, delim, N(0, c->c_proto), delim,
		    N(1, c->c_family), delim, N(2, c->c_uid), delim,
		    conf_namemask(hb, sizeof(hb), c), delim,
		    N(3, c->c_nfail), delim, N(4, c->c_duration));
	else
		snprintf(buf, len, "%starget:%s, proto:%s, family:%s, "
		    "uid:%s, name:%s, nfail:%s, duration:%s", pref,
		    ha, N(0, c->c_proto), N(1, c->c_family), N(2, c->c_uid),
		    conf_namemask(hb, sizeof(hb), c),
		    N(3, c->c_nfail), N(4, c->c_duration));
	return buf;
}

/*
 * Apply the local config match to the result
 */
static void
conf_apply(struct conf *c, const struct conf *sc)
{
	char buf[BUFSIZ];

	if (debug) {
		(*lfun)(LOG_DEBUG, "%s: %s", __func__,
		    conf_print(buf, sizeof(buf), "merge:\t", "", sc));
		(*lfun)(LOG_DEBUG, "%s: %s", __func__,
		    conf_print(buf, sizeof(buf), "to:\t", "", c));
	}
	memcpy(c->c_name, sc->c_name, CONFNAMESZ);
	c->c_uid = sc->c_uid;
	c->c_rmask = sc->c_rmask;
	c->c_nfail = sc->c_nfail;
	c->c_duration = sc->c_duration;

	if (debug)
		(*lfun)(LOG_DEBUG, "%s: %s", __func__,
		    conf_print(buf, sizeof(buf), "result:\t", "", c));
}

/*
 * Merge a remote configuration to the result
 */
static void
conf_merge(struct conf *c, const struct conf *sc)
{
	char buf[BUFSIZ];

	if (debug) {
		(*lfun)(LOG_DEBUG, "%s: %s", __func__,
		    conf_print(buf, sizeof(buf), "merge:\t", "", sc));
		(*lfun)(LOG_DEBUG, "%s: %s", __func__,
		    conf_print(buf, sizeof(buf), "to:\t", "", c));
	}
	
	if (sc->c_name[0])
		memcpy(c->c_name, sc->c_name, CONFNAMESZ);
	if (sc->c_uid != FEQUAL)
		c->c_uid = sc->c_uid;
	if (sc->c_rmask != FEQUAL)
		c->c_lmask = c->c_rmask = sc->c_rmask;
	if (sc->c_nfail != FEQUAL)
		c->c_nfail = sc->c_nfail;
	if (sc->c_duration != FEQUAL)
		c->c_duration = sc->c_duration;
	if (debug)
		(*lfun)(LOG_DEBUG, "%s: %s", __func__,
		    conf_print(buf, sizeof(buf), "result:\t", "", c));
}

static void
confset_init(struct confset *cs)
{
	cs->cs_c = NULL;
	cs->cs_n = 0;
	cs->cs_m = 0;
}

static int
confset_grow(struct confset *cs)
{
	void *tc;

	cs->cs_m += 10;
	tc = realloc(cs->cs_c, cs->cs_m * sizeof(*cs->cs_c));
	if (tc == NULL) {
		(*lfun)(LOG_ERR, "%s: Can't grow confset (%m)", __func__);
		return -1;
	}
	cs->cs_c = tc;
	return 0;
}

static struct conf *
confset_get(struct confset *cs)
{
	return &cs->cs_c[cs->cs_n];
}

static bool
confset_full(const struct confset *cs)
{
	return cs->cs_n == cs->cs_m;
}

static void
confset_sort(struct confset *cs)
{
	qsort(cs->cs_c, cs->cs_n, sizeof(*cs->cs_c), conf_sort);
}

static void
confset_add(struct confset *cs)
{
	cs->cs_n++;
}

static void
confset_free(struct confset *cs)
{
	free(cs->cs_c);
	confset_init(cs);
}

static void
confset_replace(struct confset *dc, struct confset *sc)
{
	struct confset tc;
	tc = *dc;
	*dc = *sc;
	confset_init(sc);
	confset_free(&tc);
}

static void
confset_list(const struct confset *cs, const char *msg, const char *where)
{
	char buf[BUFSIZ];

	(*lfun)(LOG_DEBUG, "[%s]", msg);
	(*lfun)(LOG_DEBUG, "%20.20s\ttype\tproto\towner\tname\tnfail\tduration",
	    where);
	for (size_t i = 0; i < cs->cs_n; i++)
		(*lfun)(LOG_DEBUG, "%s", conf_print(buf, sizeof(buf), "", "\t",
		    &cs->cs_c[i]));
}

/*
 * Match a configuration against the given list and apply the function
 * to it, returning the matched entry number.
 */
static size_t
confset_match(const struct confset *cs, struct conf *c,
    void (*fun)(struct conf *, const struct conf *))
{
	char buf[BUFSIZ];
	size_t i;

	for (i = 0; i < cs->cs_n; i++) {
		if (debug)
			(*lfun)(LOG_DEBUG, "%s", conf_print(buf, sizeof(buf),
			    "check:\t", "", &cs->cs_c[i]));
		if (conf_eq(c, &cs->cs_c[i])) {
			if (debug)
				(*lfun)(LOG_DEBUG, "%s",
				    conf_print(buf, sizeof(buf),
				    "found:\t", "", &cs->cs_c[i]));
			(*fun)(c, &cs->cs_c[i]);
			break;
		}
	}
	return i;
}

const struct conf *
conf_find(int fd, uid_t uid, const struct sockaddr_storage *rss,
    struct conf *cr)
{
	int proto;
	socklen_t slen;
	struct sockaddr_storage lss;
	size_t i;
	char buf[BUFSIZ];

	memset(cr, 0, sizeof(*cr));
	slen = sizeof(lss);
	memset(&lss, 0, slen);
	if (getsockname(fd, (void *)&lss, &slen) == -1) {
		(*lfun)(LOG_ERR, "getsockname failed (%m)"); 
		return NULL;
	}

	slen = sizeof(proto);
	if (getsockopt(fd, SOL_SOCKET, SO_TYPE, &proto, &slen) == -1) {
		(*lfun)(LOG_ERR, "getsockopt failed (%m)"); 
		return NULL;
	}

	if (debug) {
		sockaddr_snprintf(buf, sizeof(buf), "%a:%p", (void *)&lss);
		(*lfun)(LOG_DEBUG, "listening socket: %s", buf);
	}

	switch (proto) {
	case SOCK_STREAM:
		cr->c_proto = IPPROTO_TCP;
		break;
	case SOCK_DGRAM:
		cr->c_proto = IPPROTO_UDP;
		break;
	default:
		(*lfun)(LOG_ERR, "unsupported protocol %d", proto); 
		return NULL;
	}

	switch (lss.ss_family) {
	case AF_INET:
		cr->c_port = ntohs(((struct sockaddr_in *)&lss)->sin_port);
		break;
	case AF_INET6:
		cr->c_port = ntohs(((struct sockaddr_in6 *)&lss)->sin6_port);
		break;
	default:
		(*lfun)(LOG_ERR, "unsupported family %d", lss.ss_family); 
		return NULL;
	}

	cr->c_ss = lss;
	cr->c_lmask = FSTAR;
	cr->c_uid = (int)uid;
	cr->c_family = lss.ss_family;
	cr->c_name[0] = '\0';
	cr->c_rmask = FSTAR;
	cr->c_nfail = FSTAR;
	cr->c_duration = FSTAR;

	if (debug)
		(*lfun)(LOG_DEBUG, "%s", conf_print(buf, sizeof(buf),
		    "look:\t", "", cr));

	/* match the local config */
	i = confset_match(&lconf, cr, conf_apply);
	if (i == lconf.cs_n) {
		if (debug)
			(*lfun)(LOG_DEBUG, "not found");
		return NULL;
	}

	conf_addr_set(cr, rss);
	/* match the remote config */
	confset_match(&rconf, cr, conf_merge);
	/* to apply the mask */
	conf_addr_set(cr, &cr->c_ss);

	return cr;
}


void
conf_parse(const char *f)
{
	FILE *fp;
	char *line;
	size_t lineno, len;
	struct confset lc, rc, *cs;

	if ((fp = fopen(f, "r")) == NULL) {
		(*lfun)(LOG_ERR, "%s: Cannot open `%s' (%m)", __func__, f);
		return;
	}

	lineno = 1;

	confset_init(&rc);
	confset_init(&lc);
	cs = &lc;
	for (; (line = fparseln(fp, &len, &lineno, NULL, 0)) != NULL;
	    free(line))
	{
		if (!*line)
			continue;
		if (strcmp(line, "[local]") == 0) {
			cs = &lc;
			continue;
		}
		if (strcmp(line, "[remote]") == 0) {
			cs = &rc;
			continue;
		}

		if (confset_full(cs)) {
			if (confset_grow(cs) == -1) {
				confset_free(&lc);
				confset_free(&rc);
				fclose(fp);
				free(line);
				return;
			}
		}
		if (conf_parseline(f, lineno, line, confset_get(cs),
		    cs == &lc) == -1)
			continue;
		confset_add(cs);
	}

	fclose(fp);
	confset_sort(&lc);
	confset_sort(&rc);
	
	confset_replace(&rconf, &rc);
	confset_replace(&lconf, &lc);

	if (debug) {
		confset_list(&lconf, "local", "target");
		confset_list(&rconf, "remote", "source");
	}
}
