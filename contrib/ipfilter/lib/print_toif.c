/*	$FreeBSD$	*/

/*
 * Copyright (C) 2012 by Darren Reed.
 *
 * See the IPFILTER.LICENCE file for details on licencing.
 *
 * $Id$
 */

#include "ipf.h"


void
print_toif(family, tag, base, fdp)
	int family;
	char *tag;
	char *base;
	frdest_t *fdp;
{
	switch (fdp->fd_type)
	{
	case FRD_NORMAL :
		PRINTF("%s %s%s", tag, base + fdp->fd_name,
		       (fdp->fd_ptr || (long)fdp->fd_ptr == -1) ? "" : "(!)");
#ifdef	USE_INET6
		if (family == AF_INET6) {
			if (IP6_NOTZERO(&fdp->fd_ip6)) {
				char ipv6addr[80];

				inet_ntop(AF_INET6, &fdp->fd_ip6, ipv6addr,
					  sizeof(fdp->fd_ip6));
				PRINTF(":%s", ipv6addr);
			}
		} else
#endif
			if (fdp->fd_ip.s_addr)
				PRINTF(":%s", inet_ntoa(fdp->fd_ip));
		putchar(' ');
		break;

	case FRD_DSTLIST :
		PRINTF("%s dstlist/%s ", tag, base + fdp->fd_name);
		break;

	default :
		PRINTF("%s <%d>", tag, fdp->fd_type);
		break;
	}
}
