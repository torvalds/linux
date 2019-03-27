/*
 * Copyright (c) 1999 - 2001 Kungliga Tekniska Högskolan
 * (Royal Institute of Technology, Stockholm, Sweden).
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the Institute nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE INSTITUTE AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE INSTITUTE OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <config.h>

#include "roken.h"

/*
 * uses hints->ai_socktype and hints->ai_protocol
 */

static int
get_port_protocol_socktype (const char *servname,
			    const struct addrinfo *hints,
			    int *port,
			    int *protocol,
			    int *socktype)
{
    struct servent *se;
    const char *proto_str = NULL;

    *socktype = 0;

    if (hints != NULL && hints->ai_protocol != 0) {
	struct protoent *protoent = getprotobynumber (hints->ai_protocol);

	if (protoent == NULL)
	    return EAI_SOCKTYPE; /* XXX */

	proto_str = protoent->p_name;
	*protocol = protoent->p_proto;
    }

    if (hints != NULL)
	*socktype = hints->ai_socktype;

    if (*socktype == SOCK_STREAM) {
	se = getservbyname (servname, proto_str ? proto_str : "tcp");
	if (proto_str == NULL)
	    *protocol = IPPROTO_TCP;
    } else if (*socktype == SOCK_DGRAM) {
	se = getservbyname (servname, proto_str ? proto_str : "udp");
	if (proto_str == NULL)
	    *protocol = IPPROTO_UDP;
    } else if (*socktype == 0) {
	if (proto_str != NULL) {
	    se = getservbyname (servname, proto_str);
	} else {
	    se = getservbyname (servname, "tcp");
	    *protocol = IPPROTO_TCP;
	    *socktype = SOCK_STREAM;
	    if (se == NULL) {
		se = getservbyname (servname, "udp");
		*protocol = IPPROTO_UDP;
		*socktype = SOCK_DGRAM;
	    }
	}
    } else
	return EAI_SOCKTYPE;

    if (se == NULL) {
	char *endstr;

	*port = htons(strtol (servname, &endstr, 10));
	if (servname == endstr)
	    return EAI_NONAME;
    } else {
	*port = se->s_port;
    }
    return 0;
}

static int
add_one (int port, int protocol, int socktype,
	 struct addrinfo ***ptr,
	 int (*func)(struct addrinfo *, void *data, int port),
	 void *data,
	 char *canonname)
{
    struct addrinfo *a;
    int ret;

    a = malloc (sizeof (*a));
    if (a == NULL)
	return EAI_MEMORY;
    memset (a, 0, sizeof(*a));
    a->ai_flags     = 0;
    a->ai_next      = NULL;
    a->ai_protocol  = protocol;
    a->ai_socktype  = socktype;
    a->ai_canonname = canonname;
    ret = (*func)(a, data, port);
    if (ret) {
	free (a);
	return ret;
    }
    **ptr = a;
    *ptr = &a->ai_next;
    return 0;
}

static int
const_v4 (struct addrinfo *a, void *data, int port)
{
    struct sockaddr_in *sin4;
    struct in_addr *addr = (struct in_addr *)data;

    a->ai_family  = PF_INET;
    a->ai_addrlen = sizeof(*sin4);
    a->ai_addr    = malloc (sizeof(*sin4));
    if (a->ai_addr == NULL)
	return EAI_MEMORY;
    sin4 = (struct sockaddr_in *)a->ai_addr;
    memset (sin4, 0, sizeof(*sin4));
    sin4->sin_family = AF_INET;
    sin4->sin_port   = port;
    sin4->sin_addr   = *addr;
    return 0;
}

#ifdef HAVE_IPV6
static int
const_v6 (struct addrinfo *a, void *data, int port)
{
    struct sockaddr_in6 *sin6;
    struct in6_addr *addr = (struct in6_addr *)data;

    a->ai_family  = PF_INET6;
    a->ai_addrlen = sizeof(*sin6);
    a->ai_addr    = malloc (sizeof(*sin6));
    if (a->ai_addr == NULL)
	return EAI_MEMORY;
    sin6 = (struct sockaddr_in6 *)a->ai_addr;
    memset (sin6, 0, sizeof(*sin6));
    sin6->sin6_family = AF_INET6;
    sin6->sin6_port   = port;
    sin6->sin6_addr   = *addr;
    return 0;
}
#endif

/* this is mostly a hack for some versions of AIX that has a prototype
   for in6addr_loopback but no actual symbol in libc */
#if defined(HAVE_IPV6) && !defined(HAVE_IN6ADDR_LOOPBACK) && defined(IN6ADDR_LOOPBACK_INIT)
#define in6addr_loopback _roken_in6addr_loopback
struct in6_addr in6addr_loopback = IN6ADDR_LOOPBACK_INIT;
#endif

static int
get_null (const struct addrinfo *hints,
	  int port, int protocol, int socktype,
	  struct addrinfo **res)
{
    struct in_addr v4_addr;
#ifdef HAVE_IPV6
    struct in6_addr v6_addr;
#endif
    struct addrinfo *first = NULL;
    struct addrinfo **current = &first;
    int family = PF_UNSPEC;
    int ret;

    if (hints != NULL)
	family = hints->ai_family;

    if (hints && hints->ai_flags & AI_PASSIVE) {
	v4_addr.s_addr = INADDR_ANY;
#ifdef HAVE_IPV6
	v6_addr        = in6addr_any;
#endif
    } else {
	v4_addr.s_addr = htonl(INADDR_LOOPBACK);
#ifdef HAVE_IPV6
	v6_addr        = in6addr_loopback;
#endif
    }

#ifdef HAVE_IPV6
    if (family == PF_INET6 || family == PF_UNSPEC) {
	ret = add_one (port, protocol, socktype,
		       &current, const_v6, &v6_addr, NULL);
    }
#endif
    if (family == PF_INET || family == PF_UNSPEC) {
	ret = add_one (port, protocol, socktype,
		       &current, const_v4, &v4_addr, NULL);
    }
    *res = first;
    return 0;
}

static int
add_hostent (int port, int protocol, int socktype,
	     struct addrinfo ***current,
	     int (*func)(struct addrinfo *, void *data, int port),
	     struct hostent *he, int *flags)
{
    int ret;
    char *canonname = NULL;
    char **h;

    if (*flags & AI_CANONNAME) {
	struct hostent *he2 = NULL;
	const char *tmp_canon;

	tmp_canon = hostent_find_fqdn (he);
	if (strchr (tmp_canon, '.') == NULL) {
	    int error;

	    he2 = getipnodebyaddr (he->h_addr_list[0], he->h_length,
				   he->h_addrtype, &error);
	    if (he2 != NULL) {
		const char *tmp = hostent_find_fqdn (he2);

		if (strchr (tmp, '.') != NULL)
		    tmp_canon = tmp;
	    }
	}

	canonname = strdup (tmp_canon);
	if (he2 != NULL)
	    freehostent (he2);
	if (canonname == NULL)
	    return EAI_MEMORY;
    }

    for (h = he->h_addr_list; *h != NULL; ++h) {
	ret = add_one (port, protocol, socktype,
		       current, func, *h, canonname);
	if (ret)
	    return ret;
	if (*flags & AI_CANONNAME) {
	    *flags &= ~AI_CANONNAME;
	    canonname = NULL;
	}
    }
    return 0;
}

static int
get_number (const char *nodename,
	    const struct addrinfo *hints,
	    int port, int protocol, int socktype,
	    struct addrinfo **res)
{
    struct addrinfo *first = NULL;
    struct addrinfo **current = &first;
    int family = PF_UNSPEC;
    int ret;

    if (hints != NULL) {
	family = hints->ai_family;
    }

#ifdef HAVE_IPV6
    if (family == PF_INET6 || family == PF_UNSPEC) {
	struct in6_addr v6_addr;

	if (inet_pton (PF_INET6, nodename, &v6_addr) == 1) {
	    ret = add_one (port, protocol, socktype,
			   &current, const_v6, &v6_addr, NULL);
	    *res = first;
	    return ret;
	}
    }
#endif
    if (family == PF_INET || family == PF_UNSPEC) {
	struct in_addr v4_addr;

	if (inet_pton (PF_INET, nodename, &v4_addr) == 1) {
	    ret = add_one (port, protocol, socktype,
			   &current, const_v4, &v4_addr, NULL);
	    *res = first;
	    return ret;
	}
    }
    return EAI_NONAME;
}

static int
get_nodes (const char *nodename,
	   const struct addrinfo *hints,
	   int port, int protocol, int socktype,
	   struct addrinfo **res)
{
    struct addrinfo *first = NULL;
    struct addrinfo **current = &first;
    int family = PF_UNSPEC;
    int flags  = 0;
    int ret = EAI_NONAME;
    int error;

    if (hints != NULL) {
	family = hints->ai_family;
	flags  = hints->ai_flags;
    }

#ifdef HAVE_IPV6
    if (family == PF_INET6 || family == PF_UNSPEC) {
	struct hostent *he;

	he = getipnodebyname (nodename, PF_INET6, 0, &error);

	if (he != NULL) {
	    ret = add_hostent (port, protocol, socktype,
			       &current, const_v6, he, &flags);
	    freehostent (he);
	}
    }
#endif
    if (family == PF_INET || family == PF_UNSPEC) {
	struct hostent *he;

	he = getipnodebyname (nodename, PF_INET, 0, &error);

	if (he != NULL) {
	    ret = add_hostent (port, protocol, socktype,
			       &current, const_v4, he, &flags);
	    freehostent (he);
	}
    }
    *res = first;
    return ret;
}

/*
 * hints:
 *
 * struct addrinfo {
 *     int    ai_flags;
 *     int    ai_family;
 *     int    ai_socktype;
 *     int    ai_protocol;
 * ...
 * };
 */

ROKEN_LIB_FUNCTION int ROKEN_LIB_CALL
getaddrinfo(const char *nodename,
	    const char *servname,
	    const struct addrinfo *hints,
	    struct addrinfo **res)
{
    int ret;
    int port     = 0;
    int protocol = 0;
    int socktype = 0;

    *res = NULL;

    if (servname == NULL && nodename == NULL)
	return EAI_NONAME;

    if (hints != NULL
	&& hints->ai_family != PF_UNSPEC
	&& hints->ai_family != PF_INET
#ifdef HAVE_IPV6
	&& hints->ai_family != PF_INET6
#endif
	)
	return EAI_FAMILY;

    if (servname != NULL) {
	ret = get_port_protocol_socktype (servname, hints,
					  &port, &protocol, &socktype);
	if (ret)
	    return ret;
    }
    if (nodename != NULL) {
	ret = get_number (nodename, hints, port, protocol, socktype, res);
	if (ret) {
	    if(hints && hints->ai_flags & AI_NUMERICHOST)
		ret = EAI_NONAME;
	    else
		ret = get_nodes (nodename, hints, port, protocol, socktype,
				 res);
	}
    } else {
	ret = get_null (hints, port, protocol, socktype, res);
    }
    if (ret)
	freeaddrinfo (*res);
    return ret;
}
