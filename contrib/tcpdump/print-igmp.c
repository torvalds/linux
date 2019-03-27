/*
 * Copyright (c) 1988, 1989, 1990, 1991, 1993, 1994, 1995, 1996
 *  The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that: (1) source code distributions
 * retain the above copyright notice and this paragraph in its entirety, (2)
 * distributions including binary code include the above copyright notice and
 * this paragraph in its entirety in the documentation or other materials
 * provided with the distribution, and (3) all advertising materials mentioning
 * features or use of this software display the following acknowledgement:
 * ``This product includes software developed by the University of California,
 * Lawrence Berkeley Laboratory and its contributors.'' Neither the name of
 * the University nor the names of its contributors may be used to endorse
 * or promote products derived from this software without specific prior
 * written permission.
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 */

/* \summary: Internet Group Management Protocol (IGMP) printer */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <netdissect-stdinc.h>

#include "netdissect.h"
#include "addrtoname.h"
#include "extract.h"

#ifndef IN_CLASSD
#define IN_CLASSD(i) (((int32_t)(i) & 0xf0000000) == 0xe0000000)
#endif

static const char tstr[] = "[|igmp]";

/* (following from ipmulti/mrouted/prune.h) */

/*
 * The packet format for a traceroute request.
 */
struct tr_query {
    uint32_t  tr_src;          /* traceroute source */
    uint32_t  tr_dst;          /* traceroute destination */
    uint32_t  tr_raddr;        /* traceroute response address */
    uint32_t  tr_rttlqid;      /* response ttl and qid */
};

#define TR_GETTTL(x)        (int)(((x) >> 24) & 0xff)
#define TR_GETQID(x)        ((x) & 0x00ffffff)

/*
 * Traceroute response format.  A traceroute response has a tr_query at the
 * beginning, followed by one tr_resp for each hop taken.
 */
struct tr_resp {
    uint32_t tr_qarr;          /* query arrival time */
    uint32_t tr_inaddr;        /* incoming interface address */
    uint32_t tr_outaddr;       /* outgoing interface address */
    uint32_t tr_rmtaddr;       /* parent address in source tree */
    uint32_t tr_vifin;         /* input packet count on interface */
    uint32_t tr_vifout;        /* output packet count on interface */
    uint32_t tr_pktcnt;        /* total incoming packets for src-grp */
    uint8_t  tr_rproto;      /* routing proto deployed on router */
    uint8_t  tr_fttl;        /* ttl required to forward on outvif */
    uint8_t  tr_smask;       /* subnet mask for src addr */
    uint8_t  tr_rflags;      /* forwarding error codes */
};

/* defs within mtrace */
#define TR_QUERY 1
#define TR_RESP 2

/* fields for tr_rflags (forwarding error codes) */
#define TR_NO_ERR   0
#define TR_WRONG_IF 1
#define TR_PRUNED   2
#define TR_OPRUNED  3
#define TR_SCOPED   4
#define TR_NO_RTE   5
#define TR_NO_FWD   7
#define TR_NO_SPACE 0x81
#define TR_OLD_ROUTER   0x82

/* fields for tr_rproto (routing protocol) */
#define TR_PROTO_DVMRP  1
#define TR_PROTO_MOSPF  2
#define TR_PROTO_PIM    3
#define TR_PROTO_CBT    4

/* igmpv3 report types */
static const struct tok igmpv3report2str[] = {
	{ 1,	"is_in" },
	{ 2,	"is_ex" },
	{ 3,	"to_in" },
	{ 4,	"to_ex" },
	{ 5,	"allow" },
	{ 6,	"block" },
	{ 0,	NULL }
};

static void
print_mtrace(netdissect_options *ndo,
             register const u_char *bp, register u_int len)
{
    register const struct tr_query *tr = (const struct tr_query *)(bp + 8);

    ND_TCHECK(*tr);
    if (len < 8 + sizeof (struct tr_query)) {
	ND_PRINT((ndo, " [invalid len %d]", len));
	return;
    }
    ND_PRINT((ndo, "mtrace %u: %s to %s reply-to %s",
        TR_GETQID(EXTRACT_32BITS(&tr->tr_rttlqid)),
        ipaddr_string(ndo, &tr->tr_src), ipaddr_string(ndo, &tr->tr_dst),
        ipaddr_string(ndo, &tr->tr_raddr)));
    if (IN_CLASSD(EXTRACT_32BITS(&tr->tr_raddr)))
        ND_PRINT((ndo, " with-ttl %d", TR_GETTTL(EXTRACT_32BITS(&tr->tr_rttlqid))));
    return;
trunc:
    ND_PRINT((ndo, "%s", tstr));
}

static void
print_mresp(netdissect_options *ndo,
            register const u_char *bp, register u_int len)
{
    register const struct tr_query *tr = (const struct tr_query *)(bp + 8);

    ND_TCHECK(*tr);
    if (len < 8 + sizeof (struct tr_query)) {
	ND_PRINT((ndo, " [invalid len %d]", len));
	return;
    }
    ND_PRINT((ndo, "mresp %lu: %s to %s reply-to %s",
        (u_long)TR_GETQID(EXTRACT_32BITS(&tr->tr_rttlqid)),
        ipaddr_string(ndo, &tr->tr_src), ipaddr_string(ndo, &tr->tr_dst),
        ipaddr_string(ndo, &tr->tr_raddr)));
    if (IN_CLASSD(EXTRACT_32BITS(&tr->tr_raddr)))
        ND_PRINT((ndo, " with-ttl %d", TR_GETTTL(EXTRACT_32BITS(&tr->tr_rttlqid))));
    return;
trunc:
    ND_PRINT((ndo, "%s", tstr));
}

static void
print_igmpv3_report(netdissect_options *ndo,
                    register const u_char *bp, register u_int len)
{
    u_int group, nsrcs, ngroups;
    register u_int i, j;

    /* Minimum len is 16, and should be a multiple of 4 */
    if (len < 16 || len & 0x03) {
	ND_PRINT((ndo, " [invalid len %d]", len));
	return;
    }
    ND_TCHECK2(bp[6], 2);
    ngroups = EXTRACT_16BITS(&bp[6]);
    ND_PRINT((ndo, ", %d group record(s)", ngroups));
    if (ndo->ndo_vflag > 0) {
	/* Print the group records */
	group = 8;
        for (i=0; i<ngroups; i++) {
	    if (len < group+8) {
		ND_PRINT((ndo, " [invalid number of groups]"));
		return;
	    }
	    ND_TCHECK2(bp[group+4], 4);
            ND_PRINT((ndo, " [gaddr %s", ipaddr_string(ndo, &bp[group+4])));
	    ND_PRINT((ndo, " %s", tok2str(igmpv3report2str, " [v3-report-#%d]",
								bp[group])));
            nsrcs = EXTRACT_16BITS(&bp[group+2]);
	    /* Check the number of sources and print them */
	    if (len < group+8+(nsrcs<<2)) {
		ND_PRINT((ndo, " [invalid number of sources %d]", nsrcs));
		return;
	    }
            if (ndo->ndo_vflag == 1)
                ND_PRINT((ndo, ", %d source(s)", nsrcs));
            else {
		/* Print the sources */
                ND_PRINT((ndo, " {"));
                for (j=0; j<nsrcs; j++) {
		    ND_TCHECK2(bp[group+8+(j<<2)], 4);
		    ND_PRINT((ndo, " %s", ipaddr_string(ndo, &bp[group+8+(j<<2)])));
		}
                ND_PRINT((ndo, " }"));
            }
	    /* Next group record */
            group += 8 + (nsrcs << 2);
	    ND_PRINT((ndo, "]"));
        }
    }
    return;
trunc:
    ND_PRINT((ndo, "%s", tstr));
}

static void
print_igmpv3_query(netdissect_options *ndo,
                   register const u_char *bp, register u_int len)
{
    u_int mrc;
    u_int mrt;
    u_int nsrcs;
    register u_int i;

    ND_PRINT((ndo, " v3"));
    /* Minimum len is 12, and should be a multiple of 4 */
    if (len < 12 || len & 0x03) {
	ND_PRINT((ndo, " [invalid len %d]", len));
	return;
    }
    ND_TCHECK(bp[1]);
    mrc = bp[1];
    if (mrc < 128) {
	mrt = mrc;
    } else {
        mrt = ((mrc & 0x0f) | 0x10) << (((mrc & 0x70) >> 4) + 3);
    }
    if (mrc != 100) {
	ND_PRINT((ndo, " [max resp time "));
        if (mrt < 600) {
            ND_PRINT((ndo, "%.1fs", mrt * 0.1));
        } else {
            unsigned_relts_print(ndo, mrt / 10);
        }
	ND_PRINT((ndo, "]"));
    }
    ND_TCHECK2(bp[4], 4);
    if (EXTRACT_32BITS(&bp[4]) == 0)
	return;
    ND_PRINT((ndo, " [gaddr %s", ipaddr_string(ndo, &bp[4])));
    ND_TCHECK2(bp[10], 2);
    nsrcs = EXTRACT_16BITS(&bp[10]);
    if (nsrcs > 0) {
	if (len < 12 + (nsrcs << 2))
	    ND_PRINT((ndo, " [invalid number of sources]"));
	else if (ndo->ndo_vflag > 1) {
	    ND_PRINT((ndo, " {"));
	    for (i=0; i<nsrcs; i++) {
		ND_TCHECK2(bp[12+(i<<2)], 4);
		ND_PRINT((ndo, " %s", ipaddr_string(ndo, &bp[12+(i<<2)])));
	    }
	    ND_PRINT((ndo, " }"));
	} else
	    ND_PRINT((ndo, ", %d source(s)", nsrcs));
    }
    ND_PRINT((ndo, "]"));
    return;
trunc:
    ND_PRINT((ndo, "%s", tstr));
}

void
igmp_print(netdissect_options *ndo,
           register const u_char *bp, register u_int len)
{
    struct cksum_vec vec[1];

    if (ndo->ndo_qflag) {
        ND_PRINT((ndo, "igmp"));
        return;
    }

    ND_TCHECK(bp[0]);
    switch (bp[0]) {
    case 0x11:
        ND_PRINT((ndo, "igmp query"));
	if (len >= 12)
	    print_igmpv3_query(ndo, bp, len);
	else {
            ND_TCHECK(bp[1]);
	    if (bp[1]) {
		ND_PRINT((ndo, " v2"));
		if (bp[1] != 100)
		    ND_PRINT((ndo, " [max resp time %d]", bp[1]));
	    } else
		ND_PRINT((ndo, " v1"));
            ND_TCHECK2(bp[4], 4);
	    if (EXTRACT_32BITS(&bp[4]))
                ND_PRINT((ndo, " [gaddr %s]", ipaddr_string(ndo, &bp[4])));
            if (len != 8)
                ND_PRINT((ndo, " [len %d]", len));
	}
        break;
    case 0x12:
        ND_TCHECK2(bp[4], 4);
        ND_PRINT((ndo, "igmp v1 report %s", ipaddr_string(ndo, &bp[4])));
        if (len != 8)
            ND_PRINT((ndo, " [len %d]", len));
        break;
    case 0x16:
        ND_TCHECK2(bp[4], 4);
        ND_PRINT((ndo, "igmp v2 report %s", ipaddr_string(ndo, &bp[4])));
        break;
    case 0x22:
        ND_PRINT((ndo, "igmp v3 report"));
	print_igmpv3_report(ndo, bp, len);
        break;
    case 0x17:
        ND_TCHECK2(bp[4], 4);
        ND_PRINT((ndo, "igmp leave %s", ipaddr_string(ndo, &bp[4])));
        break;
    case 0x13:
        ND_PRINT((ndo, "igmp dvmrp"));
        if (len < 8)
            ND_PRINT((ndo, " [len %d]", len));
        else
            dvmrp_print(ndo, bp, len);
        break;
    case 0x14:
        ND_PRINT((ndo, "igmp pimv1"));
        pimv1_print(ndo, bp, len);
        break;
    case 0x1e:
        print_mresp(ndo, bp, len);
        break;
    case 0x1f:
        print_mtrace(ndo, bp, len);
        break;
    default:
        ND_PRINT((ndo, "igmp-%d", bp[0]));
        break;
    }

    if (ndo->ndo_vflag && len >= 4 && ND_TTEST2(bp[0], len)) {
        /* Check the IGMP checksum */
        vec[0].ptr = bp;
        vec[0].len = len;
        if (in_cksum(vec, 1))
            ND_PRINT((ndo, " bad igmp cksum %x!", EXTRACT_16BITS(&bp[2])));
    }
    return;
trunc:
    ND_PRINT((ndo, "%s", tstr));
}
