/*
 * Copyright (C) 2012 by Darren Reed.
 *
 * See the IPFILTER.LICENCE file for details on licencing.
 */

#include "ipf.h"
#include <ctype.h>


void
printpooldata(pool, opts)
	ip_pool_t *pool;
	int opts;
{

	if ((opts & OPT_DEBUG) == 0) {
		if ((pool->ipo_flags & IPOOL_ANON) != 0)
			PRINTF("# 'anonymous' tree %s\n", pool->ipo_name);
		if ((pool->ipo_flags & IPOOL_DELETE) != 0)
			PRINTF("# ");
		PRINTF("table role=");
	} else {
		if ((pool->ipo_flags & IPOOL_DELETE) != 0)
			PRINTF("# ");
		PRINTF("%s: %s",
			ISDIGIT(*pool->ipo_name) ? "Number" : "Name",
			pool->ipo_name);
		if ((pool->ipo_flags & IPOOL_ANON) == IPOOL_ANON)
			PRINTF("(anon)");
		putchar(' ');
		PRINTF("Role: ");
	}

	printunit(pool->ipo_unit);

	if ((opts & OPT_DEBUG) == 0) {
		PRINTF(" type=tree %s=%s\n",
			(!*pool->ipo_name || ISDIGIT(*pool->ipo_name)) ? \
			"number" : "name", pool->ipo_name);
	} else {
		putchar(' ');

		PRINTF("\tReferences: %d\tHits: %lu\n", pool->ipo_ref,
			pool->ipo_hits);
		if ((pool->ipo_flags & IPOOL_DELETE) != 0)
			PRINTF("# ");
		PRINTF("\tNodes Starting at %p\n", pool->ipo_list);
	}
}
