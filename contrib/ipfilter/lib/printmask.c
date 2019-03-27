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
printmask(family, mask)
	int	family;
	u_32_t	*mask;
{
	struct in_addr ipa;
	int ones;

	if (family == AF_INET6) {
		PRINTF("/%d", count6bits(mask));
	} else if ((ones = count4bits(*mask)) == -1) {
		ipa.s_addr = *mask;
		PRINTF("/%s", inet_ntoa(ipa));
	} else {
		PRINTF("/%d", ones);
	}
}
