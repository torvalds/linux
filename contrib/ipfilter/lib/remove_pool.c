/*	$FreeBSD$	*/

/*
 * Copyright (C) 2012 by Darren Reed.
 *
 * See the IPFILTER.LICENCE file for details on licencing.
 *
 * $Id$
 */

#include <fcntl.h>
#include <sys/ioctl.h>
#include "ipf.h"
#include "netinet/ip_lookup.h"
#include "netinet/ip_htable.h"


int
remove_pool(poolp, iocfunc)
	ip_pool_t *poolp;
	ioctlfunc_t iocfunc;
{
	iplookupop_t op;
	ip_pool_t pool;

	if (pool_open() == -1)
		return -1;

	op.iplo_type = IPLT_POOL;
	op.iplo_unit = poolp->ipo_unit;
	strncpy(op.iplo_name, poolp->ipo_name, sizeof(op.iplo_name));
	op.iplo_size = sizeof(pool);
	op.iplo_struct = &pool;

	bzero((char *)&pool, sizeof(pool));
	pool.ipo_unit = poolp->ipo_unit;
	strncpy(pool.ipo_name, poolp->ipo_name, sizeof(pool.ipo_name));
	pool.ipo_flags = poolp->ipo_flags;

	if (pool_ioctl(iocfunc, SIOCLOOKUPDELTABLE, &op)) {
		if ((opts & OPT_DONOTHING) == 0) {
			return ipf_perror_fd(pool_fd(), iocfunc,
					     "delete lookup pool");
		}
	}
	return 0;
}
