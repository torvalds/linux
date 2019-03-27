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
remove_hashnode(unit, name, node, iocfunc)
	int unit;
	char *name;
	iphtent_t *node;
	ioctlfunc_t iocfunc;
{
	iplookupop_t op;
	iphtent_t ipe;

	if (pool_open() == -1)
		return -1;

	op.iplo_type = IPLT_HASH;
	op.iplo_unit = unit;
	op.iplo_size = sizeof(ipe);
	op.iplo_struct = &ipe;
	op.iplo_arg = 0;
	strncpy(op.iplo_name, name, sizeof(op.iplo_name));

	bzero((char *)&ipe, sizeof(ipe));
	bcopy((char *)&node->ipe_addr, (char *)&ipe.ipe_addr,
	      sizeof(ipe.ipe_addr));
	bcopy((char *)&node->ipe_mask, (char *)&ipe.ipe_mask,
	      sizeof(ipe.ipe_mask));

	if (opts & OPT_DEBUG) {
		printf("\t%s - ", inet_ntoa(ipe.ipe_addr.in4));
		printf("%s\n", inet_ntoa(ipe.ipe_mask.in4));
	}

	if (pool_ioctl(iocfunc, SIOCLOOKUPDELNODE, &op)) {
		if (!(opts & OPT_DONOTHING)) {
			return ipf_perror_fd(pool_fd(), iocfunc,
					     "remove lookup hash node");
		}
	}
	return 0;
}
