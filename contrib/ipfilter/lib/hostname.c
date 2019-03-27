/*	$FreeBSD$	*/

/*
 * Copyright (C) 2012 by Darren Reed.
 *
 * See the IPFILTER.LICENCE file for details on licencing.
 *
 * $Id$
 */

#include "ipf.h"

char *
hostname(int family, void *ip)
{
	static char hostbuf[MAXHOSTNAMELEN+1];
	struct hostent *hp;
	struct in_addr ipa;
	struct netent *np;

	memset(&ipa, 0, sizeof(ipa));	/* XXX gcc */

	if (family == AF_INET) {
		ipa.s_addr = *(u_32_t *)ip;
		if (ipa.s_addr == htonl(0xfedcba98))
			return ("test.host.dots");
	}

	if ((opts & OPT_NORESOLVE) == 0) {
		if (family == AF_INET) {
			hp = gethostbyaddr(ip, 4, AF_INET);
			if (hp != NULL && hp->h_name != NULL &&
			    *hp->h_name != '\0') {
				strncpy(hostbuf, hp->h_name, sizeof(hostbuf));
				hostbuf[sizeof(hostbuf) - 1] = '\0';
				return (hostbuf);
			}

			np = getnetbyaddr(ipa.s_addr, AF_INET);
			if (np != NULL && np->n_name != NULL &&
			    *np->n_name != '\0') {
				strncpy(hostbuf, np->n_name, sizeof(hostbuf));
				hostbuf[sizeof(hostbuf) - 1] = '\0';
				return (hostbuf);
			}
		}
	}

	if (family == AF_INET) {
		return (inet_ntoa(ipa));
	}
#ifdef  USE_INET6
	(void) inet_ntop(AF_INET6, ip, hostbuf, sizeof(hostbuf) - 1);
	hostbuf[MAXHOSTNAMELEN] = '\0';
	return (hostbuf);
#else
	return ("IPv6");
#endif
}
