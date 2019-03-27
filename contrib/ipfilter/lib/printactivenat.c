/*	$FreeBSD$	*/

/*
 * Copyright (C) 2012 by Darren Reed.
 *
 * See the IPFILTER.LICENCE file for details on licencing.
 *
 * Added redirect stuff and a variety of bug fixes. (mcn@EnGarde.com)
 */

#include "ipf.h"


#if !defined(lint)
static const char rcsid[] = "@(#)$Id$";
#endif


void
printactivenat(nat, opts, ticks)
	nat_t *nat;
	int opts;
	u_long ticks;
{

	PRINTF("%s", getnattype(nat));

	if (nat->nat_flags & SI_CLONE)
		PRINTF(" CLONE");
	if (nat->nat_phnext[0] == NULL && nat->nat_phnext[1] == NULL)
		PRINTF(" ORPHAN");

	putchar(' ');
	if (nat->nat_redir & NAT_REWRITE) {
		printactiveaddress(nat->nat_v[0], "%-15s", &nat->nat_osrc6,
				   nat->nat_ifnames[0]);

		if ((nat->nat_flags & IPN_TCPUDP) != 0)
			PRINTF(" %-5hu", ntohs(nat->nat_osport));

		putchar(' ');
		printactiveaddress(nat->nat_v[0], "%-15s", &nat->nat_odst6,
				   nat->nat_ifnames[0]);

		if ((nat->nat_flags & IPN_TCPUDP) != 0)
			PRINTF(" %-5hu", ntohs(nat->nat_odport));

		PRINTF("<- -> ");
		printactiveaddress(nat->nat_v[1], "%-15s", &nat->nat_nsrc6,
				   nat->nat_ifnames[0]);

		if ((nat->nat_flags & IPN_TCPUDP) != 0)
			PRINTF(" %-5hu", ntohs(nat->nat_nsport));

		putchar(' ');
		printactiveaddress(nat->nat_v[1], "%-15s", &nat->nat_ndst6,
				   nat->nat_ifnames[0]);
		if ((nat->nat_flags & IPN_TCPUDP) != 0)
			PRINTF(" %-5hu", ntohs(nat->nat_ndport));

	} else if (nat->nat_dir == NAT_OUTBOUND) {
		printactiveaddress(nat->nat_v[0], "%-15s", &nat->nat_osrc6,
				   nat->nat_ifnames[0]);

		if ((nat->nat_flags & IPN_TCPUDP) != 0)
			PRINTF(" %-5hu", ntohs(nat->nat_osport));

		PRINTF(" <- -> ");
		printactiveaddress(nat->nat_v[1], "%-15s", &nat->nat_nsrc6,
				   nat->nat_ifnames[0]);

		if ((nat->nat_flags & IPN_TCPUDP) != 0)
			PRINTF(" %-5hu", ntohs(nat->nat_nsport));

		PRINTF(" [");
		printactiveaddress(nat->nat_v[0], "%s", &nat->nat_odst6,
				   nat->nat_ifnames[0]);

		if ((nat->nat_flags & IPN_TCPUDP) != 0)
			PRINTF(" %hu", ntohs(nat->nat_odport));
		PRINTF("]");
	} else {
		printactiveaddress(nat->nat_v[1], "%-15s", &nat->nat_ndst6,
				   nat->nat_ifnames[0]);

		if ((nat->nat_flags & IPN_TCPUDP) != 0)
			PRINTF(" %-5hu", ntohs(nat->nat_ndport));

		PRINTF(" <- -> ");
		printactiveaddress(nat->nat_v[0], "%-15s", &nat->nat_odst6,
				   nat->nat_ifnames[0]);

		if ((nat->nat_flags & IPN_TCPUDP) != 0)
			PRINTF(" %-5hu", ntohs(nat->nat_odport));

		PRINTF(" [");
		printactiveaddress(nat->nat_v[0], "%s", &nat->nat_osrc6,
				   nat->nat_ifnames[0]);

		if ((nat->nat_flags & IPN_TCPUDP) != 0)
			PRINTF(" %hu", ntohs(nat->nat_osport));
		PRINTF("]");
	}

	if (opts & OPT_VERBOSE) {
		PRINTF("\n\tttl %lu use %hu sumd %s/",
			nat->nat_age - ticks, nat->nat_use,
			getsumd(nat->nat_sumd[0]));
		PRINTF("%s pr %u/%u hash %u/%u flags %x\n",
			getsumd(nat->nat_sumd[1]),
			nat->nat_pr[0], nat->nat_pr[1],
			nat->nat_hv[0], nat->nat_hv[1], nat->nat_flags);
		PRINTF("\tifp %s", getifname(nat->nat_ifps[0]));
		PRINTF(",%s ", getifname(nat->nat_ifps[1]));
#ifdef	USE_QUAD_T
		PRINTF("bytes %"PRIu64"/%"PRIu64" pkts %"PRIu64"/%"PRIu64"",
			(unsigned long long)nat->nat_bytes[0],
			(unsigned long long)nat->nat_bytes[1],
			(unsigned long long)nat->nat_pkts[0],
			(unsigned long long)nat->nat_pkts[1]);
#else
		PRINTF("bytes %lu/%lu pkts %lu/%lu", nat->nat_bytes[0],
			nat->nat_bytes[1], nat->nat_pkts[0], nat->nat_pkts[1]);
#endif
		PRINTF(" ipsumd %x", nat->nat_ipsumd);
	}

	if (opts & OPT_DEBUG) {
		PRINTF("\n\tnat_next %p _pnext %p _hm %p\n",
			nat->nat_next, nat->nat_pnext, nat->nat_hm);
		PRINTF("\t_hnext %p/%p _phnext %p/%p\n",
			nat->nat_hnext[0], nat->nat_hnext[1],
			nat->nat_phnext[0], nat->nat_phnext[1]);
		PRINTF("\t_data %p _me %p _state %p _aps %p\n",
			nat->nat_data, nat->nat_me, nat->nat_state,
			nat->nat_aps);
		PRINTF("\tfr %p ptr %p ifps %p/%p sync %p\n",
			nat->nat_fr, nat->nat_ptr, nat->nat_ifps[0],
			nat->nat_ifps[1], nat->nat_sync);
		PRINTF("\ttqe:pnext %p next %p ifq %p parent %p/%p\n",
			nat->nat_tqe.tqe_pnext, nat->nat_tqe.tqe_next,
			nat->nat_tqe.tqe_ifq, nat->nat_tqe.tqe_parent, nat);
		PRINTF("\ttqe:die %d touched %d flags %x state %d/%d\n",
			nat->nat_tqe.tqe_die, nat->nat_tqe.tqe_touched,
			nat->nat_tqe.tqe_flags, nat->nat_tqe.tqe_state[0],
			nat->nat_tqe.tqe_state[1]);
	}
	putchar('\n');
}
