/*	$FreeBSD$	*/

/*
 * Copyright (C) 2012 by Darren Reed.
 *
 * See the IPFILTER.LICENCE file for details on licencing.
 *
 * $Id$
 */

#include "ipf.h"

int ntomask(family, nbits, ap)
	int family, nbits;
	u_32_t *ap;
{
	u_32_t mask;

	if (nbits < 0)
		return -1;

	switch (family)
	{
	case AF_INET :
		if (nbits > 32 || use_inet6 == 1)
			return -1;
		if (nbits == 0) {
			mask = 0;
		} else {
			mask = 0xffffffff;
			mask <<= (32 - nbits);
		}
		*ap = htonl(mask);
		break;

	case 0 :
	case AF_INET6 :
		if ((nbits > 128) || (use_inet6 == -1))
			return -1;
		fill6bits(nbits, ap);
		break;

	default :
		return -1;
	}
	return 0;
}
