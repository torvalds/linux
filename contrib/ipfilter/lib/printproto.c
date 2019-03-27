/*
 * Copyright (C) 2012 by Darren Reed.
 *
 * See the IPFILTER.LICENCE file for details on licencing.
 */

#include "ipf.h"


#if !defined(lint)
static const char rcsid[] = "@(#)$Id$";
#endif


void
printproto(pr, p, np)
	struct protoent *pr;
	int p;
	ipnat_t *np;
{
	if (np != NULL) {
		if ((np->in_flags & IPN_TCPUDP) == IPN_TCPUDP)
			PRINTF("tcp/udp");
		else if (np->in_flags & IPN_TCP)
			PRINTF("tcp");
		else if (np->in_flags & IPN_UDP)
			PRINTF("udp");
		else if (np->in_flags & IPN_ICMPQUERY)
			PRINTF("icmp");
		else if (np->in_pr[0] == 0)
			PRINTF("ip");
		else if (pr != NULL)
			PRINTF("%s", pr->p_name);
		else
			PRINTF("%d", np->in_pr[0]);
	} else {
		if (pr != NULL)
			PRINTF("%s", pr->p_name);
		else
			PRINTF("%d", p);
	}
}
