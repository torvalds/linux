/*	$FreeBSD$	*/

/*
 * Copyright (C) 2012 by Darren Reed.
 *
 * See the IPFILTER.LICENCE file for details on licencing.
 */

#include "ipf.h"


iphtable_t *
printhash(hp, copyfunc, name, opts, fields)
	iphtable_t *hp;
	copyfunc_t copyfunc;
	char *name;
	int opts;
	wordtab_t *fields;
{
	iphtent_t *ipep, **table;
	iphtable_t iph;
	int printed;
	size_t sz;

	if ((*copyfunc)((char *)hp, (char *)&iph, sizeof(iph)))
		return NULL;

	if ((name != NULL) && strncmp(name, iph.iph_name, FR_GROUPLEN))
		return iph.iph_next;

	if (fields == NULL)
		printhashdata(hp, opts);

	if ((hp->iph_flags & IPHASH_DELETE) != 0)
		PRINTF("# ");

	if ((opts & OPT_DEBUG) == 0)
		PRINTF("\t{");

	sz = iph.iph_size * sizeof(*table);
	table = malloc(sz);
	if ((*copyfunc)((char *)iph.iph_table, (char *)table, sz))
		return NULL;

	for (printed = 0, ipep = iph.iph_list; ipep != NULL; ) {
		ipep = printhashnode(&iph, ipep, copyfunc, opts, fields);
		printed++;
	}
	if (printed == 0)
		putchar(';');

	free(table);

	if ((opts & OPT_DEBUG) == 0)
		PRINTF(" };\n");

	return iph.iph_next;
}
