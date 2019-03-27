/*
 * Copyright (C) 2012 by Darren Reed.
 *
 * See the IPFILTER.LICENCE file for details on licencing.
 *
 * $Id: printstatefields.c,v 1.4.2.2 2012/01/26 05:44:26 darren_r Exp $
 */

#include "ipf.h"

wordtab_t statefields[] = {
	{ "all",	-2 },
	{ "ifp0",	1 },
	{ "ifp1",	2 },
	{ "ifp2",	3 },
	{ "ifp3",	4 },
	{ "ifname0",	5 },
	{ "ifname1",	6 },
	{ "ifname2",	7 },
	{ "ifname3",	8 },
	{ "pkts0",	9 },
	{ "pkts1",	10 },
	{ "pkts2",	11 },
	{ "pkts3",	12 },
	{ "bytes0",	13 },
	{ "bytes1",	14 },
	{ "bytes2",	15 },
	{ "bytes3",	16 },
	{ "state0",	17 },
	{ "state1",	18 },
	{ "age0",	19 },
	{ "age1",	20 },
	{ "ref",	21 },
	{ "isn0",	22 },
	{ "isn1",	23 },
	{ "sumd0",	24 },
	{ "sumd1",	25 },
	{ "src",	26 },
	{ "dst",	27 },
	{ "sport",	28 },
	{ "dport",	29 },
	{ "icmptype",	30 },
	{ "-",		31 },
	{ "pass",	32 },
	{ "proto",	33 },
	{ "version",	34 },
	{ "hash",	35 },
	{ "tag",	36 },
	{ "flags",	37 },
	{ "rulen",	38 },
	{ "group",	39 },
	{ "flx0",	40 },
	{ "flx1",	41 },
	{ "flx2",	42 },
	{ "flx3",	43 },
	{ "opt0",	44 },
	{ "opt1",	45 },
	{ "optmsk0",	46 },
	{ "optmsk1",	47 },
	{ "sec",	48 },
	{ "secmsk",	49 },
	{ "auth",	50 },
	{ "authmsk",	51 },
	{ "icmppkts0",	52 },
	{ "icmppkts1",	53 },
	{ "icmppkts2",	54 },
	{ "icmppkts3",	55 },
	{ NULL, 0 }
};


void
printstatefield(sp, fieldnum)
	ipstate_t *sp;
	int fieldnum;
{
	int i;

	switch (fieldnum)
	{
	case -2 :
		for (i = 1; statefields[i].w_word != NULL; i++) {
			if (statefields[i].w_value > 0) {
				printstatefield(sp, i);
				if (statefields[i + 1].w_value > 0)
					putchar('\t');
			}
		}
		break;

	case 1:
		PRINTF("%#lx", (u_long)sp->is_ifp[0]);
		break;

	case 2:
		PRINTF("%#lx", (u_long)sp->is_ifp[1]);
		break;

	case 3:
		PRINTF("%#lx", (u_long)sp->is_ifp[2]);
		break;

	case 4:
		PRINTF("%#lx", (u_long)sp->is_ifp[3]);
		break;

	case 5:
		PRINTF("%s", sp->is_ifname[0]);
		break;

	case 6:
		PRINTF("%s", sp->is_ifname[1]);
		break;

	case 7:
		PRINTF("%s", sp->is_ifname[2]);
		break;

	case 8:
		PRINTF("%s", sp->is_ifname[3]);
		break;

	case 9:
#ifdef USE_QUAD_T
		PRINTF("%"PRIu64"", sp->is_pkts[0]);
#else
		PRINTF("%lu", sp->is_pkts[0]);
#endif
		break;

	case 10:
#ifdef USE_QUAD_T
		PRINTF("%"PRIu64"", sp->is_pkts[1]);
#else
		PRINTF("%lu", sp->is_pkts[1]);
#endif
		break;

	case 11:
#ifdef USE_QUAD_T
		PRINTF("%"PRIu64"", sp->is_pkts[2]);
#else
		PRINTF("%lu", sp->is_pkts[2]);
#endif
		break;

	case 12:
#ifdef USE_QUAD_T
		PRINTF("%"PRIu64"", sp->is_pkts[3]);
#else
		PRINTF("%lu", sp->is_pkts[3]);
#endif
		break;

	case 13:
#ifdef USE_QUAD_T
		PRINTF("%"PRIu64"", sp->is_bytes[0]);
#else
		PRINTF("%lu", sp->is_bytes[0]);
#endif
		break;

	case 14:
#ifdef USE_QUAD_T
		PRINTF("%"PRIu64"", sp->is_bytes[1]);
#else
		PRINTF("%lu", sp->is_bytes[1]);
#endif
		break;

	case 15:
#ifdef USE_QUAD_T
		PRINTF("%"PRIu64"", sp->is_bytes[2]);
#else
		PRINTF("%lu", sp->is_bytes[2]);
#endif
		break;

	case 16:
#ifdef USE_QUAD_T
		PRINTF("%"PRIu64"", sp->is_bytes[3]);
#else
		PRINTF("%lu", sp->is_bytes[3]);
#endif
		break;

	case 17:
		PRINTF("%d", sp->is_state[0]);
		break;

	case 18:
		PRINTF("%d", sp->is_state[1]);
		break;

	case 19:
		PRINTF("%d", sp->is_frage[0]);
		break;

	case 20:
		PRINTF("%d", sp->is_frage[1]);
		break;

	case 21:
		PRINTF("%d", sp->is_ref);
		break;

	case 22:
		PRINTF("%d", sp->is_isninc[0]);
		break;

	case 23:
		PRINTF("%d", sp->is_isninc[1]);
		break;

	case 24:
		PRINTF("%hd", sp->is_sumd[0]);
		break;

	case 25:
		PRINTF("%hd", sp->is_sumd[1]);
		break;

	case 26:
		PRINTF("%s", hostname(sp->is_v, &sp->is_src.in4));
		break;

	case 27:
		PRINTF("%s", hostname(sp->is_v, &sp->is_dst.in4));
		break;

	case 28:
		PRINTF("%hu", ntohs(sp->is_sport));
		break;

	case 29:
		PRINTF("%hu", ntohs(sp->is_dport));
		break;

	case 30:
		PRINTF("%d", sp->is_type);
		break;

	case 32:
		PRINTF("%#x", sp->is_pass);
		break;

	case 33:
		PRINTF("%d", sp->is_p);
		break;

	case 34:
		PRINTF("%d", sp->is_v);
		break;

	case 35:
		PRINTF("%d", sp->is_hv);
		break;

	case 36:
		PRINTF("%d", sp->is_tag);
		break;

	case 37:
		PRINTF("%#x", sp->is_flags);
		break;

	case 38:
		PRINTF("%d", sp->is_rulen);
		break;

	case 39:
		PRINTF("%s", sp->is_group);
		break;

	case 40:
		PRINTF("%#x", sp->is_flx[0][0]);
		break;

	case 41:
		PRINTF("%#x", sp->is_flx[0][1]);
		break;

	case 42:
		PRINTF("%#x", sp->is_flx[1][0]);
		break;

	case 43:
		PRINTF("%#x", sp->is_flx[1][1]);
		break;

	case 44:
		PRINTF("%#x", sp->is_opt[0]);
		break;

	case 45:
		PRINTF("%#x", sp->is_opt[1]);
		break;

	case 46:
		PRINTF("%#x", sp->is_optmsk[0]);
		break;

	case 47:
		PRINTF("%#x", sp->is_optmsk[1]);
		break;

	case 48:
		PRINTF("%#x", sp->is_sec);
		break;

	case 49:
		PRINTF("%#x", sp->is_secmsk);
		break;

	case 50:
		PRINTF("%#x", sp->is_auth);
		break;

	case 51:
		PRINTF("%#x", sp->is_authmsk);
		break;

	case 52:
#ifdef USE_QUAD_T
		PRINTF("%"PRIu64"", sp->is_icmppkts[0]);
#else
		PRINTF("%lu", sp->is_icmppkts[0]);
#endif
		break;

	case 53:
#ifdef USE_QUAD_T
		PRINTF("%"PRIu64"", sp->is_icmppkts[1]);
#else
		PRINTF("%lu", sp->is_icmppkts[1]);
#endif
		break;

	case 54:
#ifdef USE_QUAD_T
		PRINTF("%"PRIu64"", sp->is_icmppkts[2]);
#else
		PRINTF("%lu", sp->is_icmppkts[2]);
#endif
		break;

	case 55:
#ifdef USE_QUAD_T
		PRINTF("%"PRIu64"", sp->is_icmppkts[3]);
#else
		PRINTF("%lu", sp->is_icmppkts[3]);
#endif
		break;

	default:
		break;
	}
}
