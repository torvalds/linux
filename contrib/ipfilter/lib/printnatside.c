/*
 * Copyright (C) 2012 by Darren Reed.
 *
 * See the IPFILTER.LICENCE file for details on licencing.
 *
 * $Id: printnatside.c,v 1.2.2.6 2012/07/22 08:04:24 darren_r Exp $
 */
#include "ipf.h"

void
printnatside(side, ns)
	char *side;
	nat_stat_side_t *ns;
{
	PRINTF("%lu\tproxy create fail %s\n", ns->ns_appr_fail, side);
	PRINTF("%lu\tproxy fail %s\n", ns->ns_ipf_proxy_fail, side);
	PRINTF("%lu\tbad nat %s\n", ns->ns_badnat, side);
	PRINTF("%lu\tbad nat new %s\n", ns->ns_badnatnew, side);
	PRINTF("%lu\tbad next addr %s\n", ns->ns_badnextaddr, side);
	PRINTF("%lu\tbucket max %s\n", ns->ns_bucket_max, side);
	PRINTF("%lu\tclone nomem %s\n", ns->ns_clone_nomem, side);
	PRINTF("%lu\tdecap bad %s\n", ns->ns_decap_bad, side);
	PRINTF("%lu\tdecap fail %s\n", ns->ns_decap_fail, side);
	PRINTF("%lu\tdecap pullup %s\n", ns->ns_decap_pullup, side);
	PRINTF("%lu\tdivert dup %s\n", ns->ns_divert_dup, side);
	PRINTF("%lu\tdivert exist %s\n", ns->ns_divert_exist, side);
	PRINTF("%lu\tdrop %s\n", ns->ns_drop, side);
	PRINTF("%lu\texhausted %s\n", ns->ns_exhausted, side);
	PRINTF("%lu\ticmp address %s\n", ns->ns_icmp_address, side);
	PRINTF("%lu\ticmp basic %s\n", ns->ns_icmp_basic, side);
	PRINTF("%lu\tinuse %s\n", ns->ns_inuse, side);
	PRINTF("%lu\ticmp mbuf wrong size %s\n", ns->ns_icmp_mbuf, side);
	PRINTF("%lu\ticmp header unmatched %s\n", ns->ns_icmp_notfound, side);
	PRINTF("%lu\ticmp rebuild failures %s\n", ns->ns_icmp_rebuild, side);
	PRINTF("%lu\ticmp short %s\n", ns->ns_icmp_short, side);
	PRINTF("%lu\ticmp packet size wrong %s\n", ns->ns_icmp_size, side);
	PRINTF("%lu\tIFP address fetch failures %s\n",
		ns->ns_ifpaddrfail, side);
	PRINTF("%lu\tpackets untranslated %s\n", ns->ns_ignored, side);
	PRINTF("%lu\tNAT insert failures %s\n", ns->ns_insert_fail, side);
	PRINTF("%lu\tNAT lookup misses %s\n", ns->ns_lookup_miss, side);
	PRINTF("%lu\tNAT lookup nowild %s\n", ns->ns_lookup_nowild, side);
	PRINTF("%lu\tnew ifpaddr failed %s\n", ns->ns_new_ifpaddr, side);
	PRINTF("%lu\tmemory requests failed %s\n", ns->ns_memfail, side);
	PRINTF("%lu\ttable max reached %s\n", ns->ns_table_max, side);
	PRINTF("%lu\tpackets translated %s\n", ns->ns_translated, side);
	PRINTF("%lu\tfinalised failed %s\n", ns->ns_unfinalised, side);
	PRINTF("%lu\tsearch wraps %s\n", ns->ns_wrap, side);
	PRINTF("%lu\tnull translations %s\n", ns->ns_xlate_null, side);
	PRINTF("%lu\ttranslation exists %s\n", ns->ns_xlate_exists, side);
	PRINTF("%lu\tno memory %s\n", ns->ns_memfail, side);

	if (opts & OPT_VERBOSE)
		PRINTF("%p table %s\n", ns->ns_table, side);
}
