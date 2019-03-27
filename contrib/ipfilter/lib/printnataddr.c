/*
 * Copyright (C) 2012 by Darren Reed.
 *
 * See the IPFILTER.LICENCE file for details on licencing.
 *
 * Added redirect stuff and a variety of bug fixes. (mcn@EnGarde.com)
 */

#include "ipf.h"
#include "kmem.h"


#if !defined(lint)
static const char rcsid[] = "@(#)$Id: printnataddr.c,v 1.4.2.2 2012/07/22 08:04:24 darren_r Exp $";
#endif


void
printnataddr(v, base, addr, ifidx)
	int v;
	char *base;
	nat_addr_t *addr;
	int ifidx;
{
	switch (v)
	{
	case 4 :
		if (addr->na_atype == FRI_NORMAL &&
		    addr->na_addr[0].in4.s_addr == 0) {
			PRINTF("0/%d", count4bits(addr->na_addr[1].in4.s_addr));
		} else {
			printaddr(AF_INET, addr->na_atype, base, ifidx,
				  (u_32_t *)&addr->na_addr[0].in4.s_addr,
				  (u_32_t *)&addr->na_addr[1].in4.s_addr);
		}
		break;
#ifdef USE_INET6
	case 6 :
		printaddr(AF_INET6, addr->na_atype, base, ifidx,
			  (u_32_t *)&addr->na_addr[0].in6,
			  (u_32_t *)&addr->na_addr[1].in6);
		break;
#endif
	default :
		printf("{v=%d}", v);
		break;
	}
}
