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
printtunable(tup)
	ipftune_t *tup;
{
	PRINTF("%s\tmin %lu\tmax %lu\tcurrent ",
		tup->ipft_name, tup->ipft_min, tup->ipft_max);
	if (tup->ipft_sz == sizeof(u_long))
		PRINTF("%lu\n", tup->ipft_vlong);
	else if (tup->ipft_sz == sizeof(u_int))
		PRINTF("%u\n", tup->ipft_vint);
	else if (tup->ipft_sz == sizeof(u_short))
		PRINTF("%hu\n", tup->ipft_vshort);
	else if (tup->ipft_sz == sizeof(u_char))
		PRINTF("%u\n", (u_int)tup->ipft_vchar);
	else {
		PRINTF("sz = %d\n", tup->ipft_sz);
	}
}
