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
load_poolnode(role, name, node, ttl, iocfunc)
	int role;
	char *name;
	ip_pool_node_t *node;
	int ttl;
	ioctlfunc_t iocfunc;
{
	ip_pool_node_t pn;
	iplookupop_t op;
	char *what;
	int err;

	if (pool_open() == -1)
		return -1;

	op.iplo_unit = role;
	op.iplo_type = IPLT_POOL;
	op.iplo_arg = 0;
	op.iplo_struct = &pn;
	op.iplo_size = sizeof(pn);
	strncpy(op.iplo_name, name, sizeof(op.iplo_name));

	bzero((char *)&pn, sizeof(pn));
	bcopy((char *)&node->ipn_addr, (char *)&pn.ipn_addr,
	      sizeof(pn.ipn_addr));
	bcopy((char *)&node->ipn_mask, (char *)&pn.ipn_mask,
	      sizeof(pn.ipn_mask));
	pn.ipn_info = node->ipn_info;
	pn.ipn_die = ttl;
	strncpy(pn.ipn_name, node->ipn_name, sizeof(pn.ipn_name));

	if ((opts & OPT_REMOVE) == 0) {
		what = "add";
		err = pool_ioctl(iocfunc, SIOCLOOKUPADDNODE, &op);
	} else {
		what = "delete";
		err = pool_ioctl(iocfunc, SIOCLOOKUPDELNODE, &op);
	}

	if (err != 0) {
		if ((opts & OPT_DONOTHING) == 0) {
			char msg[80];

			sprintf(msg, "%s pool node(%s/", what,
				inet_ntoa(pn.ipn_addr.adf_addr.in4));
			strcat(msg, inet_ntoa(pn.ipn_mask.adf_addr.in4));
			return ipf_perror_fd(pool_fd(), iocfunc, msg);
		}
	}

	return 0;
}
