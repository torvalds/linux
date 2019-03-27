/*
 * Copyright (C) 2012 by Darren Reed.
 *
 * See the IPFILTER.LICENCE file for details on licencing.
 */

#include "ipf.h"


ippool_dst_t *
printdstlist(pp, copyfunc, name, opts, nodes, fields)
	ippool_dst_t *pp;
	copyfunc_t copyfunc;
	char *name;
	int opts;
	ipf_dstnode_t *nodes;
	wordtab_t *fields;
{
	ipf_dstnode_t *node;
	ippool_dst_t dst;

	if ((*copyfunc)(pp, &dst, sizeof(dst)))
		return NULL;

	if ((name != NULL) && strncmp(name, dst.ipld_name, FR_GROUPLEN))
		return dst.ipld_next;

	if (fields == NULL)
		printdstlistdata(&dst, opts);

	if ((dst.ipld_flags & IPDST_DELETE) != 0)
		PRINTF("# ");
	if ((opts & OPT_DEBUG) == 0)
		PRINTF("\t{");

	if (nodes == NULL) {
		putchar(';');
	} else {
		for (node = nodes; node != NULL; ) {
			ipf_dstnode_t *n;

			n = calloc(1, node->ipfd_size);
			if (n == NULL)
				break;
			if ((*copyfunc)(node, n, node->ipfd_size)) {
				free(n);
				return NULL;
			}

			node = printdstlistnode(n, bcopywrap, opts, fields);

			free(n);
		}
	}

	if ((opts & OPT_DEBUG) == 0)
		PRINTF(" };\n");

	return dst.ipld_next;
}
