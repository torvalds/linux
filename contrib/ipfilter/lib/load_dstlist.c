/*
 * Copyright (C) 2012 by Darren Reed.
 *
 * See the IPFILTER.LICENCE file for details on licencing.
 *
 * $Id: load_dstlist.c,v 1.1.2.5 2012/07/22 08:04:24 darren_r Exp $
 */

#include <fcntl.h>
#include <sys/ioctl.h>
#include "ipf.h"
#include "netinet/ip_lookup.h"
#include "netinet/ip_dstlist.h"


int
load_dstlist(dst, iocfunc, nodes)
	ippool_dst_t *dst;
	ioctlfunc_t iocfunc;
	ipf_dstnode_t *nodes;
{
	iplookupop_t op;
	ipf_dstnode_t *a;
	ippool_dst_t dest;

	if (dst->ipld_name[0] == '\0')
		return -1;

	if (pool_open() == -1)
		return -1;

	op.iplo_unit = dst->ipld_unit;
	op.iplo_type = IPLT_DSTLIST;
	op.iplo_arg = 0;
	strncpy(op.iplo_name, dst->ipld_name, sizeof(op.iplo_name));
	op.iplo_size = sizeof(dest);
	op.iplo_struct = &dest;
	bzero((char *)&dest, sizeof(dest));
	dest.ipld_unit = dst->ipld_unit;
	dest.ipld_policy = dst->ipld_policy;
	dest.ipld_flags = dst->ipld_flags;
	strncpy(dest.ipld_name, dst->ipld_name, sizeof(dest.ipld_name));

	if ((opts & OPT_REMOVE) == 0) {
		if (pool_ioctl(iocfunc, SIOCLOOKUPADDTABLE, &op))
			if ((opts & OPT_DONOTHING) == 0) {
				return ipf_perror_fd(pool_fd(), iocfunc,
						  "add destination list table");
			}
	}

	if ((opts & OPT_VERBOSE) != 0) {
		dest.ipld_dests = dst->ipld_dests;
		printdstlist(&dest, bcopywrap, dest.ipld_name, opts, nodes, NULL);
		dest.ipld_dests = NULL;
	}

	for (a = nodes; a != NULL; a = a->ipfd_next)
		load_dstlistnode(dst->ipld_unit, dest.ipld_name, a, iocfunc);

	if ((opts & OPT_REMOVE) != 0) {
		if (pool_ioctl(iocfunc, SIOCLOOKUPDELTABLE, &op))
			if ((opts & OPT_DONOTHING) == 0) {
				return ipf_perror_fd(pool_fd(), iocfunc,
					      "delete destination list table");
			}
	}
	return 0;
}
