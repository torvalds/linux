/*
 * Copyright (C) 2012 by Darren Reed.
 *
 * See the IPFILTER.LICENCE file for details on licencing.
 */

#include "ipf.h"


ipf_dstnode_t *
printdstlistnode(inp, copyfunc, opts, fields)
	ipf_dstnode_t *inp;
	copyfunc_t copyfunc;
	int opts;
	wordtab_t *fields;
{
	ipf_dstnode_t node, *np;
	int i;
#ifdef USE_INET6
	char buf[INET6_ADDRSTRLEN+1];
	const char *str;
#endif

	if ((*copyfunc)(inp, &node, sizeof(node)))
		return NULL;

	np = calloc(1, node.ipfd_size);
	if (np == NULL)
		return node.ipfd_next;
	if ((*copyfunc)(inp, np, node.ipfd_size))
		return NULL;

	if (fields != NULL) {
		for (i = 0; fields[i].w_value != 0; i++) {
			printpoolfield(np, IPLT_DSTLIST, i);
			if (fields[i + 1].w_value != 0)
				printf("\t");
		}
		printf("\n");
	} else if ((opts & OPT_DEBUG) == 0) {
		putchar(' ');
		if (np->ipfd_dest.fd_name >= 0)
			PRINTF("%s:", np->ipfd_names);
		if (np->ipfd_dest.fd_addr.adf_family == AF_INET) {
			printip(AF_INET, (u_32_t *)&np->ipfd_dest.fd_ip);
		} else {
#ifdef USE_INET6
			str = inet_ntop(AF_INET6, &np->ipfd_dest.fd_ip6,
					buf, sizeof(buf) - 1);
			if (str != NULL)
				PRINTF("%s", str);
#endif
		}
		putchar(';');
	} else {
		PRINTF("Interface: [%s]/%d\n", np->ipfd_names,
		       np->ipfd_dest.fd_name);
#ifdef USE_INET6
		str = inet_ntop(np->ipfd_dest.fd_addr.adf_family,
				&np->ipfd_dest.fd_ip6, buf, sizeof(buf) - 1);
		if (str != NULL) {
			PRINTF("\tAddress: %s\n", str);
		}
#else
		PRINTF("\tAddress: %s\n", inet_ntoa(np->ipfd_dest.fd_ip));
#endif
		PRINTF(
#ifdef USE_QUAD_T
		       "\t\tStates %d\tRef %d\tName [%s]\tUid %d\n",
#else
		       "\t\tStates %d\tRef %d\tName [%s]\tUid %d\n",
#endif
		       np->ipfd_states, np->ipfd_ref,
		       np->ipfd_names, np->ipfd_uid);
	}
	free(np);
	return node.ipfd_next;
}
