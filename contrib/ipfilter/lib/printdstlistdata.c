/*
 * Copyright (C) 2012 by Darren Reed.
 *
 * See the IPFILTER.LICENCE file for details on licencing.
 */

#include "ipf.h"
#include <ctype.h>


void
printdstlistdata(pool, opts)
	ippool_dst_t *pool;
	int opts;
{

	if ((opts & OPT_DEBUG) == 0) {
		if ((pool->ipld_flags & IPDST_DELETE) != 0)
			PRINTF("# ");
		PRINTF("pool ");
	} else {
		if ((pool->ipld_flags & IPDST_DELETE) != 0)
			PRINTF("# ");
		PRINTF("Name: %s\tRole: ", pool->ipld_name);
	}

	printunit(pool->ipld_unit);

	if ((opts & OPT_DEBUG) == 0) {
		PRINTF("/dstlist (name %s;", pool->ipld_name);
		if (pool->ipld_policy != IPLDP_NONE) {
			PRINTF(" policy ");
			printdstlistpolicy(pool->ipld_policy);
			putchar(';');
		}
		PRINTF(")\n");
	} else {
		putchar(' ');

		PRINTF("\tReferences: %d\n", pool->ipld_ref);
		if ((pool->ipld_flags & IPDST_DELETE) != 0)
			PRINTF("# ");
		PRINTF("Policy: \n");
		printdstlistpolicy(pool->ipld_policy);
		PRINTF("\n\tNodes Starting at %p\n", pool->ipld_dests);
	}
}
