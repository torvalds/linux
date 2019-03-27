/*	$FreeBSD$	*/

/*
 * Copyright (C) 2012 by Darren Reed.
 *
 * See the IPFILTER.LICENCE file for details on licencing.
 *
 * $Id$
 */

#include "ipf.h"

int gethost(family, name, hostp)
	int family;
	char *name;
	i6addr_t *hostp;
{
	struct hostent *h;
	struct netent *n;
	u_32_t addr;

	bzero(hostp, sizeof(*hostp));
	if (!strcmp(name, "test.host.dots")) {
		if (family == AF_INET) {
			hostp->in4.s_addr = htonl(0xfedcba98);
		}
#ifdef USE_INET6
		if (family == AF_INET6) {
			hostp->i6[0] = htonl(0xfe80aa55);
			hostp->i6[1] = htonl(0x12345678);
			hostp->i6[2] = htonl(0x5a5aa5a5);
			hostp->i6[3] = htonl(0xfedcba98);
		}
#endif
		return 0;
	}

	if (!strcmp(name, "<thishost>"))
		name = thishost;

	if (family == AF_INET) {
		h = gethostbyname(name);
		if (h != NULL) {
			if ((h->h_addr != NULL) &&
			    (h->h_length == sizeof(addr))) {
				bcopy(h->h_addr, (char *)&addr, sizeof(addr));
				hostp->in4.s_addr = addr;
				return 0;
			}
		}

		n = getnetbyname(name);
		if (n != NULL) {
			hostp->in4.s_addr = htonl(n->n_net & 0xffffffff);
			return 0;
		}
	}
#ifdef USE_INET6
	if (family == AF_INET6) {
		struct addrinfo hints, *res;
		struct sockaddr_in6 *sin6;

		bzero((char *)&hints, sizeof(hints));
		hints.ai_family = PF_INET6;

		getaddrinfo(name, NULL, &hints, &res);
		if (res != NULL) {
			sin6 = (struct sockaddr_in6 *)res->ai_addr;
			hostp->in6 = sin6->sin6_addr;
			freeaddrinfo(res);
			return 0;
		}
	}
#endif
	return -1;
}
