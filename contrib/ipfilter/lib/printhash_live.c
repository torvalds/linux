/*
 * Copyright (C) 2012 by Darren Reed.
 *
 * See the IPFILTER.LICENCE file for details on licencing.
 */

#include <sys/ioctl.h>
#include "ipf.h"
#include "netinet/ipl.h"


iphtable_t *
printhash_live(hp, fd, name, opts, fields)
	iphtable_t *hp;
	int fd;
	char *name;
	int opts;
	wordtab_t *fields;
{
	iphtent_t entry, zero;
	ipflookupiter_t iter;
	int last, printed;
	ipfobj_t obj;

	if ((name != NULL) && strncmp(name, hp->iph_name, FR_GROUPLEN))
		return hp->iph_next;

	if (fields == NULL)
		printhashdata(hp, opts);

	if ((hp->iph_flags & IPHASH_DELETE) != 0)
		PRINTF("# ");

	if ((opts & OPT_DEBUG) == 0)
		PRINTF("\t{");

	obj.ipfo_rev = IPFILTER_VERSION;
	obj.ipfo_type = IPFOBJ_LOOKUPITER;
	obj.ipfo_ptr = &iter;
	obj.ipfo_size = sizeof(iter);

	iter.ili_data = &entry;
	iter.ili_type = IPLT_HASH;
	iter.ili_otype = IPFLOOKUPITER_NODE;
	iter.ili_ival = IPFGENITER_LOOKUP;
	iter.ili_unit = hp->iph_unit;
	strncpy(iter.ili_name, hp->iph_name, FR_GROUPLEN);

	last = 0;
	printed = 0;
	bzero((char *)&zero, sizeof(zero));

	while (!last && (ioctl(fd, SIOCLOOKUPITER, &obj) == 0)) {
		if (entry.ipe_next == NULL)
			last = 1;
		if (bcmp(&zero, &entry, sizeof(zero)) == 0)
			break;
		(void) printhashnode(hp, &entry, bcopywrap, opts, fields);
		printed++;
	}
	if (last == 0)
		ipferror(fd, "walking hash nodes");

	if (printed == 0)
		putchar(';');

	if ((opts & OPT_DEBUG) == 0)
		PRINTF(" };\n");
	return hp->iph_next;
}
