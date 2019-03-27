/*
 * Copyright (C) 2012 by Darren Reed.
 *
 * See the IPFILTER.LICENCE file for details on licencing.
 *
 * $Id: printpoolfield.c,v 1.1.2.4 2012/01/26 05:44:26 darren_r Exp $
 */

#include "ipf.h"

wordtab_t poolfields[] = {
	{ "all",	-2 },
	{ "address",	1 },
	{ "mask",	2 },
	{ "ifname",	3 },
	{ "pkts",	4 },
	{ "bytes",	5 },
	{ "family",	6 },
	{ NULL, 0 }
};


void
printpoolfield(p, ptype, fieldnum)
	void *p;
	int ptype;
	int fieldnum;
{
	addrfamily_t *a;
	char abuf[80];
	int i;

	switch (fieldnum)
	{
	case -2 :
		for (i = 1; poolfields[i].w_word != NULL; i++) {
			if (poolfields[i].w_value > 0) {
				printpoolfield(p, ptype, i);
				if (poolfields[i + 1].w_value > 0)
					putchar('\t');
			}
		}
		break;

	case 1:
		if (ptype == IPLT_POOL) {
			ip_pool_node_t *node = (ip_pool_node_t *)p;

			if (node->ipn_info)
				PRINTF("!");
			a = &node->ipn_addr;
			PRINTF("%s", inet_ntop(a->adf_family, &a->adf_addr,
					       abuf, sizeof(abuf)));
		} else if (ptype == IPLT_HASH) {
			iphtent_t *node = (iphtent_t *)p;

			PRINTF("%s", inet_ntop(node->ipe_family,
					       &node->ipe_addr,
					       abuf, sizeof(abuf)));
		} else if (ptype == IPLT_DSTLIST) {
			ipf_dstnode_t *node = (ipf_dstnode_t *)p;

			a = &node->ipfd_dest.fd_addr;
			PRINTF("%s", inet_ntop(a->adf_family, &a->adf_addr,
					       abuf, sizeof(abuf)));
		}
		break;

	case 2:
		if (ptype == IPLT_POOL) {
			ip_pool_node_t *node = (ip_pool_node_t *)p;

			a = &node->ipn_mask;
			PRINTF("%s", inet_ntop(a->adf_family, &a->adf_addr,
					       abuf, sizeof(abuf)));
		} else if (ptype == IPLT_HASH) {
			iphtent_t *node = (iphtent_t *)p;

			PRINTF("%s", inet_ntop(node->ipe_family,
					       &node->ipe_mask,
					       abuf, sizeof(abuf)));
		} else if (ptype == IPLT_DSTLIST) {
			PRINTF("%s", "");
		}
		break;

	case 3:
		if (ptype == IPLT_POOL) {
			PRINTF("%s", "");
		} else if (ptype == IPLT_HASH) {
			PRINTF("%s", "");
		} else if (ptype == IPLT_DSTLIST) {
			ipf_dstnode_t *node = (ipf_dstnode_t *)p;

			if (node->ipfd_dest.fd_name == -1) {
				PRINTF("%s", "");
			} else {
				PRINTF("%s", node->ipfd_names +
				       node->ipfd_dest.fd_name);
			}
		}
		break;

	case 4:
		if (ptype == IPLT_POOL) {
			ip_pool_node_t *node = (ip_pool_node_t *)p;

#ifdef USE_QUAD_T
			PRINTF("%"PRIu64"", node->ipn_hits);
#else
			PRINTF("%lu", node->ipn_hits);
#endif
		} else if (ptype == IPLT_HASH) {
			iphtent_t *node = (iphtent_t *)p;

#ifdef USE_QUAD_T
			PRINTF("%"PRIu64"", node->ipe_hits);
#else
			PRINTF("%lu", node->ipe_hits);
#endif
		} else if (ptype == IPLT_DSTLIST) {
			printf("0");
		}
		break;

	case 5:
		if (ptype == IPLT_POOL) {
			ip_pool_node_t *node = (ip_pool_node_t *)p;

#ifdef USE_QUAD_T
			PRINTF("%"PRIu64"", node->ipn_bytes);
#else
			PRINTF("%lu", node->ipn_bytes);
#endif
		} else if (ptype == IPLT_HASH) {
			iphtent_t *node = (iphtent_t *)p;

#ifdef USE_QUAD_T
			PRINTF("%"PRIu64"", node->ipe_bytes);
#else
			PRINTF("%lu", node->ipe_bytes);
#endif
		} else if (ptype == IPLT_DSTLIST) {
			printf("0");
		}
		break;

	case 6:
		if (ptype == IPLT_POOL) {
			ip_pool_node_t *node = (ip_pool_node_t *)p;

			PRINTF("%s", familyname(node->ipn_addr.adf_family));
		} else if (ptype == IPLT_HASH) {
			iphtent_t *node = (iphtent_t *)p;

			PRINTF("%s", familyname(node->ipe_family));
		} else if (ptype == IPLT_DSTLIST) {
			ipf_dstnode_t *node = (ipf_dstnode_t *)p;

			a = &node->ipfd_dest.fd_addr;
			PRINTF("%s", familyname(a->adf_family));
		}
		break;

	default :
		break;
	}
}
