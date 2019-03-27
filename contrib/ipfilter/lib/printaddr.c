/*
 * Copyright (C) 2012 by Darren Reed.
 *
 * See the IPFILTER.LICENCE file for details on licencing.
 *
 * $Id$
 */

#include "ipf.h"

void
printaddr(family, type, base, ifidx, addr, mask)
	int family, type, ifidx;
	char *base;
	u_32_t *addr, *mask;
{
	char *suffix;

	switch (type)
	{
	case FRI_BROADCAST :
		suffix = "bcast";
		break;

	case FRI_DYNAMIC :
		PRINTF("%s", base + ifidx);
		printmask(family, mask);
		suffix = NULL;
		break;

	case FRI_NETWORK :
		suffix = "net";
		break;

	case FRI_NETMASKED :
		suffix = "netmasked";
		break;

	case FRI_PEERADDR :
		suffix = "peer";
		break;

	case FRI_LOOKUP :
		suffix = NULL;
		printlookup(base, (i6addr_t *)addr, (i6addr_t *)mask);
		break;

	case FRI_NONE :
	case FRI_NORMAL :
		printhostmask(family, addr, mask);
		suffix = NULL;
		break;
	case FRI_RANGE :
		printhost(family, addr);
		putchar('-');
		printhost(family, mask);
		suffix = NULL;
		break;
	case FRI_SPLIT :
		printhost(family, addr);
		putchar(',');
		printhost(family, mask);
		suffix = NULL;
		break;
	default :
		PRINTF("<%d>", type);
		printmask(family, mask);
		suffix = NULL;
		break;
	}

	if (suffix != NULL) {
		PRINTF("%s/%s", base + ifidx, suffix);
	}
}
