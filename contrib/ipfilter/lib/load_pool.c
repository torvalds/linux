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
#include "netinet/ip_pool.h"


int
load_pool(plp, iocfunc)
	ip_pool_t *plp;
	ioctlfunc_t iocfunc;
{
	iplookupop_t op;
	ip_pool_node_t *a;
	ip_pool_t pool;

	if (pool_open() == -1)
		return -1;

	op.iplo_unit = plp->ipo_unit;
	op.iplo_type = IPLT_POOL;
	op.iplo_arg = 0;
	strncpy(op.iplo_name, plp->ipo_name, sizeof(op.iplo_name));
	op.iplo_size = sizeof(pool);
	op.iplo_struct = &pool;
	bzero((char *)&pool, sizeof(pool));
	pool.ipo_unit = plp->ipo_unit;
	strncpy(pool.ipo_name, plp->ipo_name, sizeof(pool.ipo_name));
	if (plp->ipo_name[0] == '\0')
		op.iplo_arg |= IPOOL_ANON;

	if ((opts & OPT_REMOVE) == 0) {
		if (pool_ioctl(iocfunc, SIOCLOOKUPADDTABLE, &op)) {
			if ((opts & OPT_DONOTHING) == 0) {
				return ipf_perror_fd(pool_fd(), iocfunc,
						     "add lookup table");
			}
		}
	}

	if (op.iplo_arg & IPOOL_ANON)
		strncpy(pool.ipo_name, op.iplo_name, sizeof(pool.ipo_name));

	if ((opts & OPT_VERBOSE) != 0) {
		pool.ipo_list = plp->ipo_list;
		(void) printpool(&pool, bcopywrap, pool.ipo_name, opts, NULL);
		pool.ipo_list = NULL;
	}

	for (a = plp->ipo_list; a != NULL; a = a->ipn_next)
		load_poolnode(plp->ipo_unit, pool.ipo_name,
				     a, 0, iocfunc);

	if ((opts & OPT_REMOVE) != 0) {
		if (pool_ioctl(iocfunc, SIOCLOOKUPDELTABLE, &op))
			if ((opts & OPT_DONOTHING) == 0) {
				return ipf_perror_fd(pool_fd(), iocfunc,
						     "delete lookup table");
			}
	}
	return 0;
}
