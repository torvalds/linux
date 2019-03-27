/*
 * Copyright (C) 2012 by Darren Reed.
 *
 * See the IPFILTER.LICENCE file for details on licencing.
 *
 * $Id$
 */

#include "ipf.h"


int genmask(family, msk, mskp)
	int family;
	char *msk;
	i6addr_t *mskp;
{
	char *endptr = 0L;
	u_32_t addr;
	int bits;

	if (strchr(msk, '.') || strchr(msk, 'x') || strchr(msk, ':')) {
		/* possibly of the form xxx.xxx.xxx.xxx
		 * or 0xYYYYYYYY */
		switch (family)
		{
#ifdef USE_INET6
		case AF_INET6 :
			if (inet_pton(AF_INET6, msk, &mskp->in4) != 1)
				return -1;
			break;
#endif
		case AF_INET :
			if (inet_aton(msk, &mskp->in4) == 0)
				return -1;
			break;
		default :
			return -1;
			/*NOTREACHED*/
		}
	} else {
		/*
		 * set x most significant bits
		 */
		bits = (int)strtol(msk, &endptr, 0);

		switch (family)
		{
		case AF_INET6 :
			if ((*endptr != '\0') || (bits < 0) || (bits > 128))
				return -1;
			fill6bits(bits, mskp->i6);
			break;
		case AF_INET :
			if (*endptr != '\0' || bits > 32 || bits < 0)
				return -1;
			if (bits == 0)
				addr = 0;
			else
				addr = htonl(0xffffffff << (32 - bits));
			mskp->in4.s_addr = addr;
			break;
		default :
			return -1;
			/*NOTREACHED*/
		}
	}
	return 0;
}
