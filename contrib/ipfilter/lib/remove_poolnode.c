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
remove_poolnode(unit, name, node, iocfunc)
	int unit;
	char *name;
	ip_pool_node_t *node;
	ioctlfunc_t iocfunc;
{
	ip_pool_node_t pn;
	iplookupop_t op;

	if (pool_open() == -1)
		return -1;

	op.iplo_unit = unit;
	op.iplo_type = IPLT_POOL;
	op.iplo_arg = 0;
	strncpy(op.iplo_name, name, sizeof(op.iplo_name));
	op.iplo_struct = &pn;
	op.iplo_size = sizeof(pn);

	bzero((char *)&pn, sizeof(pn));
	bcopy((char *)&node->ipn_addr, (char *)&pn.ipn_addr,
	      sizeof(pn.ipn_addr));
	bcopy((char *)&node->ipn_mask, (char *)&pn.ipn_mask,
	      sizeof(pn.ipn_mask));
	pn.ipn_info = node->ipn_info;
	strncpy(pn.ipn_name, node->ipn_name, sizeof(pn.ipn_name));

	if (pool_ioctl(iocfunc, SIOCLOOKUPDELNODE, &op)) {
		if ((opts & OPT_DONOTHING) == 0) {
			return ipf_perror_fd(pool_fd(), iocfunc,
					     "remove lookup pool node");
		}
	}

	return 0;
}
