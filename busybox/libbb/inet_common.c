/* vi: set sw=4 ts=4: */
/*
 * stolen from net-tools-1.59 and stripped down for busybox by
 *                      Erik Andersen <andersen@codepoet.org>
 *
 * Heavily modified by Manuel Novoa III       Mar 12, 2001
 *
 * Licensed under GPLv2, see file LICENSE in this source tree.
 */
#include "libbb.h"
#include "inet_common.h"

#if 0
# define dbg(...) bb_error_msg(__VA_ARGS__)
#else
# define dbg(...) ((void)0)
#endif

int FAST_FUNC INET_resolve(const char *name, struct sockaddr_in *s_in, int hostfirst)
{
	struct hostent *hp;
#if ENABLE_FEATURE_ETC_NETWORKS
	struct netent *np;
#endif

	/* Grmpf. -FvK */
	s_in->sin_family = AF_INET;
	s_in->sin_port = 0;

	/* Default is special, meaning 0.0.0.0. */
	if (strcmp(name, "default") == 0) {
		s_in->sin_addr.s_addr = INADDR_ANY;
		return 1;
	}
	/* Look to see if it's a dotted quad. */
	if (inet_aton(name, &s_in->sin_addr)) {
		return 0;
	}
	/* If we expect this to be a hostname, try hostname database first */
	if (hostfirst) {
		dbg("gethostbyname(%s)", name);
		hp = gethostbyname(name);
		if (hp) {
			memcpy(&s_in->sin_addr, hp->h_addr_list[0],
				sizeof(struct in_addr));
			return 0;
		}
	}
#if ENABLE_FEATURE_ETC_NETWORKS
	/* Try the NETWORKS database to see if this is a known network. */
	dbg("getnetbyname(%s)", name);
	np = getnetbyname(name);
	if (np) {
		s_in->sin_addr.s_addr = htonl(np->n_net);
		return 1;
	}
#endif
	if (hostfirst) {
		/* Don't try again */
		return -1;
	}
#ifdef DEBUG
	res_init();
	_res.options |= RES_DEBUG;
#endif
	dbg("gethostbyname(%s)", name);
	hp = gethostbyname(name);
	if (!hp) {
		return -1;
	}
	memcpy(&s_in->sin_addr, hp->h_addr_list[0], sizeof(struct in_addr));
	return 0;
}


/* numeric: & 0x8000: "default" instead of "*",
 *          & 0x4000: host instead of net,
 *          & 0x0fff: don't resolve
 */
char* FAST_FUNC INET_rresolve(struct sockaddr_in *s_in, int numeric, uint32_t netmask)
{
	/* addr-to-name cache */
	struct addr {
		struct addr *next;
		uint32_t nip;
		smallint is_host;
		char name[1];
	};
	static struct addr *cache = NULL;

	struct addr *pn;
	char *name;
	uint32_t nip;
	smallint is_host;

	if (s_in->sin_family != AF_INET) {
		dbg("rresolve: unsupported address family %d!",	s_in->sin_family);
		errno = EAFNOSUPPORT;
		return NULL;
	}
	nip = s_in->sin_addr.s_addr;
	dbg("rresolve: %08x mask:%08x num:%08x", (unsigned)nip, netmask, numeric);
	if (numeric & 0x0FFF)
		return xmalloc_sockaddr2dotted_noport((void*)s_in);
	if (nip == INADDR_ANY) {
		if (numeric & 0x8000)
			return xstrdup("default");
		return xstrdup("*");
	}

	is_host = ((nip & (~netmask)) != 0 || (numeric & 0x4000));

	pn = cache;
	while (pn) {
		if (pn->nip == nip && pn->is_host == is_host) {
			dbg("rresolve: found %s %08x in cache",
				(is_host ? "host" : "net"), (unsigned)nip);
			return xstrdup(pn->name);
		}
		pn = pn->next;
	}

	name = NULL;
	if (is_host) {
		dbg("sockaddr2host_noport(%08x)", (unsigned)nip);
		name = xmalloc_sockaddr2host_noport((void*)s_in);
	}
#if ENABLE_FEATURE_ETC_NETWORKS
	else {
		struct netent *np;
		dbg("getnetbyaddr(%08x)", (unsigned)ntohl(nip));
		np = getnetbyaddr(ntohl(nip), AF_INET);
		if (np)
			name = xstrdup(np->n_name);
	}
#endif
	if (!name)
		name = xmalloc_sockaddr2dotted_noport((void*)s_in);

	pn = xmalloc(sizeof(*pn) + strlen(name)); /* no '+ 1', it's already accounted for */
	pn->next = cache;
	pn->nip = nip;
	pn->is_host = is_host;
	strcpy(pn->name, name);
	cache = pn;

	return name;
}

#if ENABLE_FEATURE_IPV6

int FAST_FUNC INET6_resolve(const char *name, struct sockaddr_in6 *sin6)
{
	struct addrinfo req, *ai = NULL;
	int s;

	memset(&req, 0, sizeof(req));
	req.ai_family = AF_INET6;
	s = getaddrinfo(name, NULL, &req, &ai);
	if (s != 0) {
		bb_error_msg("getaddrinfo: %s: %d", name, s);
		return -1;
	}
	memcpy(sin6, ai->ai_addr, sizeof(*sin6));
	freeaddrinfo(ai);
	return 0;
}

#ifndef IN6_IS_ADDR_UNSPECIFIED
# define IN6_IS_ADDR_UNSPECIFIED(a) \
	(((uint32_t *) (a))[0] == 0 && ((uint32_t *) (a))[1] == 0 && \
	 ((uint32_t *) (a))[2] == 0 && ((uint32_t *) (a))[3] == 0)
#endif


char* FAST_FUNC INET6_rresolve(struct sockaddr_in6 *sin6, int numeric)
{
	if (sin6->sin6_family != AF_INET6) {
		dbg("rresolve: unsupported address family %d!",
				sin6->sin6_family);
		errno = EAFNOSUPPORT;
		return NULL;
	}
	if (numeric & 0x7FFF) {
		return xmalloc_sockaddr2dotted_noport((void*)sin6);
	}
	if (IN6_IS_ADDR_UNSPECIFIED(&sin6->sin6_addr)) {
		if (numeric & 0x8000)
			return xstrdup("default");
		return xstrdup("*");
	}

	return xmalloc_sockaddr2host_noport((void*)sin6);
}

#endif  /* CONFIG_FEATURE_IPV6 */
