/*	$FreeBSD$	*/

/*
 * Copyright (C) 2012 by Darren Reed.
 *
 * See the IPFILTER.LICENCE file for details on licencing.
 */

#include "ipf.h"
#include "kmem.h"


ipstate_t *
printstate(ipstate_t *sp, int opts, u_long now)
{
	struct protoent *pr;
	synclist_t ipsync;

	if ((opts & OPT_NORESOLVE) == 0)
		pr = getprotobynumber(sp->is_p);
	else
		pr = NULL;

	PRINTF("%d:", sp->is_v);
	if (pr != NULL)
		PRINTF("%s", pr->p_name);
	else
		PRINTF("%d", sp->is_p);

	PRINTF(" src:%s", hostname(sp->is_family, &sp->is_src.in4));
	if (sp->is_p == IPPROTO_UDP || sp->is_p == IPPROTO_TCP) {
		if (sp->is_flags & IS_WSPORT)
			PRINTF(",*");
		else
			PRINTF(",%d", ntohs(sp->is_sport));
	}

	PRINTF(" dst:%s", hostname(sp->is_family, &sp->is_dst.in4));
	if (sp->is_p == IPPROTO_UDP || sp->is_p == IPPROTO_TCP) {
		if (sp->is_flags & IS_WDPORT)
			PRINTF(",*");
		else
			PRINTF(",%d", ntohs(sp->is_dport));
	}

	if (sp->is_p == IPPROTO_TCP) {
		PRINTF(" state:%d/%d", sp->is_state[0], sp->is_state[1]);
	}

	PRINTF(" %ld", sp->is_die - now);
	if (sp->is_phnext == NULL)
		PRINTF(" ORPHAN");
	if (sp->is_flags & IS_CLONE)
		PRINTF(" CLONE");
	putchar('\n');

	if (sp->is_p == IPPROTO_TCP) {
		PRINTF("\t%x:%x %hu<<%d:%hu<<%d\n",
			sp->is_send, sp->is_dend,
			sp->is_maxswin, sp->is_swinscale,
			sp->is_maxdwin, sp->is_dwinscale);
		if ((opts & OPT_VERBOSE) != 0) {
			PRINTF("\tcmsk %04x smsk %04x isc %p s0 %08x/%08x\n",
				sp->is_smsk[0], sp->is_smsk[1], sp->is_isc,
				sp->is_s0[0], sp->is_s0[1]);
			PRINTF("\tFWD: ISN inc %x sumd %x\n",
				sp->is_isninc[0], sp->is_sumd[0]);
			PRINTF("\tREV: ISN inc %x sumd %x\n",
				sp->is_isninc[1], sp->is_sumd[1]);
#ifdef	IPFILTER_SCAN
			PRINTF("\tsbuf[0] [");
			printsbuf(sp->is_sbuf[0]);
			PRINTF("] sbuf[1] [");
			printsbuf(sp->is_sbuf[1]);
			PRINTF("]\n");
#endif
		}
	} else if (sp->is_p == IPPROTO_GRE) {
		PRINTF("\tcall %hx/%hx\n", ntohs(sp->is_gre.gs_call[0]),
		       ntohs(sp->is_gre.gs_call[1]));
	} else if (sp->is_p == IPPROTO_ICMP
#ifdef	USE_INET6
		 || sp->is_p == IPPROTO_ICMPV6
#endif
		) {
		PRINTF("\tid %hu seq %hu type %d\n", sp->is_icmp.ici_id,
			sp->is_icmp.ici_seq, sp->is_icmp.ici_type);
	}

#ifdef        USE_QUAD_T
	PRINTF("\tFWD: IN pkts %"PRIu64" bytes %"PRIu64" OUT pkts %"PRIu64" bytes %"PRIu64"\n\tREV: IN pkts %"PRIu64" bytes %"PRIu64" OUT pkts %"PRIu64" bytes %"PRIu64"\n",
		sp->is_pkts[0], sp->is_bytes[0],
		sp->is_pkts[1], sp->is_bytes[1],
		sp->is_pkts[2], sp->is_bytes[2],
		sp->is_pkts[3], sp->is_bytes[3]);
#else
	PRINTF("\tFWD: IN pkts %lu bytes %lu OUT pkts %lu bytes %lu\n\tREV: IN pkts %lu bytes %lu OUT pkts %lu bytes %lu\n",
		sp->is_pkts[0], sp->is_bytes[0],
		sp->is_pkts[1], sp->is_bytes[1],
		sp->is_pkts[2], sp->is_bytes[2],
		sp->is_pkts[3], sp->is_bytes[3]);
#endif

	PRINTF("\ttag %u pass %#x = ", sp->is_tag, sp->is_pass);

	/*
	 * Print out bits set in the result code for the state being
	 * kept as they would for a rule.
	 */
	if (FR_ISPASS(sp->is_pass)) {
		PRINTF("pass");
	} else if (FR_ISBLOCK(sp->is_pass)) {
		PRINTF("block");
		switch (sp->is_pass & FR_RETMASK)
		{
		case FR_RETICMP :
			PRINTF(" return-icmp");
			break;
		case FR_FAKEICMP :
			PRINTF(" return-icmp-as-dest");
			break;
		case FR_RETRST :
			PRINTF(" return-rst");
			break;
		default :
			break;
		}
	} else if ((sp->is_pass & FR_LOGMASK) == FR_LOG) {
			PRINTF("log");
		if (sp->is_pass & FR_LOGBODY)
			PRINTF(" body");
		if (sp->is_pass & FR_LOGFIRST)
			PRINTF(" first");
	} else if (FR_ISACCOUNT(sp->is_pass)) {
		PRINTF("count");
	} else if (FR_ISPREAUTH(sp->is_pass)) {
		PRINTF("preauth");
	} else if (FR_ISAUTH(sp->is_pass))
		PRINTF("auth");

	if (sp->is_pass & FR_OUTQUE)
		PRINTF(" out");
	else
		PRINTF(" in");

	if ((sp->is_pass & FR_LOG) != 0) {
		PRINTF(" log");
		if (sp->is_pass & FR_LOGBODY)
			PRINTF(" body");
		if (sp->is_pass & FR_LOGFIRST)
			PRINTF(" first");
		if (sp->is_pass & FR_LOGORBLOCK)
			PRINTF(" or-block");
	}
	if (sp->is_pass & FR_QUICK)
		PRINTF(" quick");
	if (sp->is_pass & FR_KEEPFRAG)
		PRINTF(" keep frags");
	/* a given; no? */
	if (sp->is_pass & FR_KEEPSTATE) {
		PRINTF(" keep state");
		if (sp->is_pass & (FR_STATESYNC|FR_STSTRICT|FR_STLOOSE)) {
			PRINTF(" (");
			if (sp->is_pass & FR_STATESYNC)
				PRINTF(" sync");
			if (sp->is_pass & FR_STSTRICT)
				PRINTF(" strict");
			if (sp->is_pass & FR_STLOOSE)
				PRINTF(" loose");
			PRINTF(" )");
		}
	}
	PRINTF("\n");

	if ((opts & OPT_VERBOSE) != 0) {
		PRINTF("\tref %d", sp->is_ref);
		PRINTF(" pkt_flags & %x(%x) = %x\n",
			sp->is_flags & 0xf, sp->is_flags, sp->is_flags >> 4);
		PRINTF("\tpkt_options & %x = %x, %x = %x \n", sp->is_optmsk[0],
			sp->is_opt[0], sp->is_optmsk[1], sp->is_opt[1]);
		PRINTF("\tpkt_security & %x = %x, pkt_auth & %x = %x\n",
			sp->is_secmsk, sp->is_sec, sp->is_authmsk,
			sp->is_auth);
		PRINTF("\tis_flx %#x %#x %#x %#x\n", sp->is_flx[0][0],
			sp->is_flx[0][1], sp->is_flx[1][0], sp->is_flx[1][1]);
	}
	PRINTF("\tinterfaces: in %s[%s", getifname(sp->is_ifp[0]),
		sp->is_ifname[0]);
	if (opts & OPT_DEBUG)
		PRINTF("/%p", sp->is_ifp[0]);
	putchar(']');
	PRINTF(",%s[%s", getifname(sp->is_ifp[1]), sp->is_ifname[1]);
	if (opts & OPT_DEBUG)
		PRINTF("/%p", sp->is_ifp[1]);
	putchar(']');
	PRINTF(" out %s[%s", getifname(sp->is_ifp[2]), sp->is_ifname[2]);
	if (opts & OPT_DEBUG)
		PRINTF("/%p", sp->is_ifp[2]);
	putchar(']');
	PRINTF(",%s[%s", getifname(sp->is_ifp[3]), sp->is_ifname[3]);
	if (opts & OPT_DEBUG)
		PRINTF("/%p", sp->is_ifp[3]);
	PRINTF("]\n");

	PRINTF("\tSync status: ");
	if (sp->is_sync != NULL) {
		if (kmemcpy((char *)&ipsync, (u_long)sp->is_sync,
			    sizeof(ipsync))) {
			PRINTF("status could not be retrieved\n");
			return (NULL);
		}

		PRINTF("idx %d num %d v %d pr %d rev %d\n",
			ipsync.sl_idx, ipsync.sl_num, ipsync.sl_v,
			ipsync.sl_p, ipsync.sl_rev);
	} else {
		PRINTF("not synchronized\n");
	}

	return (sp->is_next);
}
