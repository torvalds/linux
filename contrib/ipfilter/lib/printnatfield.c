/*
 * Copyright (C) 2012 by Darren Reed.
 *
 * See the IPFILTER.LICENCE file for details on licencing.
 *
 * $Id: printnatfield.c,v 1.6.2.2 2012/01/26 05:44:26 darren_r Exp $
 */

#include "ipf.h"

wordtab_t natfields[] = {
	{ "all",	-2 },
	{ "ifp0",	1 },
	{ "ifp1",	2 },
	{ "mtu0",	3 },
	{ "mtu1",	4 },
	{ "ifname0",	5 },
	{ "ifname1",	6 },
	{ "sumd0",	7 },
	{ "sumd1",	8 },
	{ "pkts0",	9 },
	{ "pkts1",	10 },
	{ "bytes0",	11 },
	{ "bytes1",	12 },
	{ "proto0",	13 },
	{ "proto1",	14 },
	{ "hash0",	15 },
	{ "hash1",	16 },
	{ "ref",	17 },
	{ "rev",	18 },
	{ "v0",		19 },
	{ "redir",	20 },
	{ "use",	21 },
	{ "ipsumd",	22 },
	{ "dir",	23 },
	{ "olddstip",	24 },
	{ "oldsrcip",	25 },
	{ "newdstip",	26 },
	{ "newsrcip",	27 },
	{ "olddport",	28 },
	{ "oldsport",	29 },
	{ "newdport",	30 },
	{ "newsport",	31 },
	{ "age",	32 },
	{ "v1",		33 },
	{ NULL, 0 }
};


void
printnatfield(n, fieldnum)
	nat_t *n;
	int fieldnum;
{
	int i;

	switch (fieldnum)
	{
	case -2 :
		for (i = 1; natfields[i].w_word != NULL; i++) {
			if (natfields[i].w_value > 0) {
				printnatfield(n, i);
				if (natfields[i + 1].w_value > 0)
					putchar('\t');
			}
		}
		break;

	case 1:
		PRINTF("%#lx", (u_long)n->nat_ifps[0]);
		break;

	case 2:
		PRINTF("%#lx", (u_long)n->nat_ifps[1]);
		break;

	case 3:
		PRINTF("%d", n->nat_mtu[0]);
		break;

	case 4:
		PRINTF("%d", n->nat_mtu[1]);
		break;

	case 5:
		PRINTF("%s", n->nat_ifnames[0]);
		break;

	case 6:
		PRINTF("%s", n->nat_ifnames[1]);
		break;

	case 7:
		PRINTF("%d", n->nat_sumd[0]);
		break;

	case 8:
		PRINTF("%d", n->nat_sumd[1]);
		break;

	case 9:
#ifdef USE_QUAD_T
		PRINTF("%"PRIu64"", n->nat_pkts[0]);
#else
		PRINTF("%lu", n->nat_pkts[0]);
#endif
		break;

	case 10:
#ifdef USE_QUAD_T
		PRINTF("%"PRIu64"", n->nat_pkts[1]);
#else
		PRINTF("%lu", n->nat_pkts[1]);
#endif
		break;

	case 11:
#ifdef USE_QUAD_T
		PRINTF("%"PRIu64"", n->nat_bytes[0]);
#else
		PRINTF("%lu", n->nat_bytes[0]);
#endif
		break;

	case 12:
#ifdef USE_QUAD_T
		PRINTF("%"PRIu64"", n->nat_bytes[1]);
#else
		PRINTF("%lu", n->nat_bytes[1]);
#endif
		break;

	case 13:
		PRINTF("%d", n->nat_pr[0]);
		break;

	case 14:
		PRINTF("%d", n->nat_pr[1]);
		break;

	case 15:
		PRINTF("%u", n->nat_hv[0]);
		break;

	case 16:
		PRINTF("%u", n->nat_hv[1]);
		break;

	case 17:
		PRINTF("%d", n->nat_ref);
		break;

	case 18:
		PRINTF("%d", n->nat_rev);
		break;

	case 19:
		PRINTF("%d", n->nat_v[0]);
		break;

	case 33:
		PRINTF("%d", n->nat_v[0]);
		break;

	case 20:
		PRINTF("%d", n->nat_redir);
		break;

	case 21:
		PRINTF("%d", n->nat_use);
		break;

	case 22:
		PRINTF("%u", n->nat_ipsumd);
		break;

	case 23:
		PRINTF("%d", n->nat_dir);
		break;

	case 24:
		PRINTF("%s", hostname(n->nat_v[0], &n->nat_odstip));
		break;

	case 25:
		PRINTF("%s", hostname(n->nat_v[0], &n->nat_osrcip));
		break;

	case 26:
		PRINTF("%s", hostname(n->nat_v[1], &n->nat_ndstip));
		break;

	case 27:
		PRINTF("%s", hostname(n->nat_v[1], &n->nat_nsrcip));
		break;

	case 28:
		PRINTF("%hu", ntohs(n->nat_odport));
		break;

	case 29:
		PRINTF("%hu", ntohs(n->nat_osport));
		break;

	case 30:
		PRINTF("%hu", ntohs(n->nat_ndport));
		break;

	case 31:
		PRINTF("%hu", ntohs(n->nat_nsport));
		break;

	case 32:
		PRINTF("%u", n->nat_age);
		break;

	default:
		break;
	}
}
