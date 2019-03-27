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
remove_hash(iphp, iocfunc)
	iphtable_t *iphp;
	ioctlfunc_t iocfunc;
{
	iplookupop_t op;
	iphtable_t iph;

	if (pool_open() == -1)
		return -1;

	op.iplo_type = IPLT_HASH;
	op.iplo_unit = iphp->iph_unit;
	strncpy(op.iplo_name, iphp->iph_name, sizeof(op.iplo_name));
	if (*op.iplo_name == '\0')
		op.iplo_arg = IPHASH_ANON;
	op.iplo_size = sizeof(iph);
	op.iplo_struct = &iph;

	bzero((char *)&iph, sizeof(iph));
	iph.iph_unit = iphp->iph_unit;
	iph.iph_type = iphp->iph_type;
	strncpy(iph.iph_name, iphp->iph_name, sizeof(iph.iph_name));
	iph.iph_flags = iphp->iph_flags;

	if (pool_ioctl(iocfunc, SIOCLOOKUPDELTABLE, &op)) {
		if ((opts & OPT_DONOTHING) == 0) {
			return ipf_perror_fd(pool_fd(), iocfunc,
					     "remove lookup hash table");
		}
	}
	return 0;
}
