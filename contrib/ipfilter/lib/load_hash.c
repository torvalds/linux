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
load_hash(iphp, list, iocfunc)
	iphtable_t *iphp;
	iphtent_t *list;
	ioctlfunc_t iocfunc;
{
	iplookupop_t op;
	iphtable_t iph;
	iphtent_t *a;
	size_t size;
	int n;

	if (pool_open() == -1)
		return -1;

	for (n = 0, a = list; a != NULL; a = a->ipe_next)
		n++;

	bzero((char *)&iph, sizeof(iph));
	op.iplo_arg = 0;
	op.iplo_type = IPLT_HASH;
	op.iplo_unit = iphp->iph_unit;
	strncpy(op.iplo_name, iphp->iph_name, sizeof(op.iplo_name));
	if (*op.iplo_name == '\0')
		op.iplo_arg = IPHASH_ANON;
	op.iplo_size = sizeof(iph);
	op.iplo_struct = &iph;
	iph = *iphp;
	if (n <= 0)
		n = 1;
	if (iphp->iph_size == 0)
		size = n * 2 - 1;
	else
		size = iphp->iph_size;
	if ((list == NULL) && (size == 1)) {
		fprintf(stderr,
			"WARNING: empty hash table %s, recommend setting %s\n",
			iphp->iph_name, "size to match expected use");
	}
	iph.iph_size = size;
	iph.iph_table = NULL;
	iph.iph_list = NULL;
	iph.iph_ref = 0;

	if ((opts & OPT_REMOVE) == 0) {
		if (pool_ioctl(iocfunc, SIOCLOOKUPADDTABLE, &op))
			if ((opts & OPT_DONOTHING) == 0) {
				return ipf_perror_fd(pool_fd(), iocfunc,
					"add lookup hash table");
			}
	}

	strncpy(iph.iph_name, op.iplo_name, sizeof(op.iplo_name));
	strncpy(iphp->iph_name, op.iplo_name, sizeof(op.iplo_name));

	if (opts & OPT_VERBOSE) {
		iph.iph_table = calloc(size, sizeof(*iph.iph_table));
		if (iph.iph_table == NULL) {
			perror("calloc(size, sizeof(*iph.iph_table))");
			return -1;
		}
		iph.iph_list = list;
		printhash(&iph, bcopywrap, iph.iph_name, opts, NULL);
		free(iph.iph_table);

		for (a = list; a != NULL; a = a->ipe_next) {
			a->ipe_addr.in4_addr = htonl(a->ipe_addr.in4_addr);
			a->ipe_mask.in4_addr = htonl(a->ipe_mask.in4_addr);
		}
	}

	if (opts & OPT_DEBUG)
		printf("Hash %s:\n", iph.iph_name);

	for (a = list; a != NULL; a = a->ipe_next)
		load_hashnode(iphp->iph_unit, iph.iph_name, a, 0, iocfunc);

	if ((opts & OPT_REMOVE) != 0) {
		if (pool_ioctl(iocfunc, SIOCLOOKUPDELTABLE, &op))
			if ((opts & OPT_DONOTHING) == 0) {
				return ipf_perror_fd(pool_fd(), iocfunc,
					"delete lookup hash table");
			}
	}
	return 0;
}
