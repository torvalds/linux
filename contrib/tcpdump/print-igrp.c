/*
 * Copyright (c) 1996, 1997
 *	The Regents of the University of California.  All rights reserved.
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
 *
 * Initial contribution from Francis Dupont (francis.dupont@inria.fr)
 */

/* \summary: Interior Gateway Routing Protocol (IGRP) printer */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <netdissect-stdinc.h>

#include "netdissect.h"
#include "extract.h"

/* Cisco IGRP definitions */

/* IGRP Header */

struct igrphdr {
	uint8_t ig_vop;	/* protocol version number / opcode */
#define IGRP_V(x)	(((x) & 0xf0) >> 4)
#define IGRP_OP(x)	((x) & 0x0f)
	uint8_t ig_ed;		/* edition number */
	uint16_t ig_as;	/* autonomous system number */
	uint16_t ig_ni;	/* number of subnet in local net */
	uint16_t ig_ns;	/* number of networks in AS */
	uint16_t ig_nx;	/* number of networks ouside AS */
	uint16_t ig_sum;	/* checksum of IGRP header & data */
};

#define IGRP_UPDATE	1
#define IGRP_REQUEST	2

/* IGRP routing entry */

struct igrprte {
	uint8_t igr_net[3];	/* 3 significant octets of IP address */
	uint8_t igr_dly[3];	/* delay in tens of microseconds */
	uint8_t igr_bw[3];	/* bandwidth in units of 1 kb/s */
	uint8_t igr_mtu[2];	/* MTU in octets */
	uint8_t igr_rel;	/* percent packets successfully tx/rx */
	uint8_t igr_ld;	/* percent of channel occupied */
	uint8_t igr_hct;	/* hop count */
};

#define IGRP_RTE_SIZE	14	/* don't believe sizeof ! */

static void
igrp_entry_print(netdissect_options *ndo, register const struct igrprte *igr,
    register int is_interior, register int is_exterior)
{
	register u_int delay, bandwidth;
	u_int metric, mtu;

	if (is_interior)
		ND_PRINT((ndo, " *.%d.%d.%d", igr->igr_net[0],
		    igr->igr_net[1], igr->igr_net[2]));
	else if (is_exterior)
		ND_PRINT((ndo, " X%d.%d.%d.0", igr->igr_net[0],
		    igr->igr_net[1], igr->igr_net[2]));
	else
		ND_PRINT((ndo, " %d.%d.%d.0", igr->igr_net[0],
		    igr->igr_net[1], igr->igr_net[2]));

	delay = EXTRACT_24BITS(igr->igr_dly);
	bandwidth = EXTRACT_24BITS(igr->igr_bw);
	metric = bandwidth + delay;
	if (metric > 0xffffff)
		metric = 0xffffff;
	mtu = EXTRACT_16BITS(igr->igr_mtu);

	ND_PRINT((ndo, " d=%d b=%d r=%d l=%d M=%d mtu=%d in %d hops",
	    10 * delay, bandwidth == 0 ? 0 : 10000000 / bandwidth,
	    igr->igr_rel, igr->igr_ld, metric,
	    mtu, igr->igr_hct));
}

static const struct tok op2str[] = {
	{ IGRP_UPDATE,		"update" },
	{ IGRP_REQUEST,		"request" },
	{ 0,			NULL }
};

void
igrp_print(netdissect_options *ndo, register const u_char *bp, u_int length)
{
	register const struct igrphdr *hdr;
	register const u_char *cp;
	u_int nint, nsys, next;

	hdr = (const struct igrphdr *)bp;
	cp = (const u_char *)(hdr + 1);
	ND_PRINT((ndo, "igrp:"));

	/* Header */
	ND_TCHECK(*hdr);
	nint = EXTRACT_16BITS(&hdr->ig_ni);
	nsys = EXTRACT_16BITS(&hdr->ig_ns);
	next = EXTRACT_16BITS(&hdr->ig_nx);

	ND_PRINT((ndo, " %s V%d edit=%d AS=%d (%d/%d/%d)",
	    tok2str(op2str, "op-#%d", IGRP_OP(hdr->ig_vop)),
	    IGRP_V(hdr->ig_vop),
	    hdr->ig_ed,
	    EXTRACT_16BITS(&hdr->ig_as),
	    nint,
	    nsys,
	    next));

	length -= sizeof(*hdr);
	while (length >= IGRP_RTE_SIZE) {
		if (nint > 0) {
			ND_TCHECK2(*cp, IGRP_RTE_SIZE);
			igrp_entry_print(ndo, (const struct igrprte *)cp, 1, 0);
			--nint;
		} else if (nsys > 0) {
			ND_TCHECK2(*cp, IGRP_RTE_SIZE);
			igrp_entry_print(ndo, (const struct igrprte *)cp, 0, 0);
			--nsys;
		} else if (next > 0) {
			ND_TCHECK2(*cp, IGRP_RTE_SIZE);
			igrp_entry_print(ndo, (const struct igrprte *)cp, 0, 1);
			--next;
		} else {
			ND_PRINT((ndo, " [extra bytes %d]", length));
			break;
		}
		cp += IGRP_RTE_SIZE;
		length -= IGRP_RTE_SIZE;
	}
	if (nint == 0 && nsys == 0 && next == 0)
		return;
trunc:
	ND_PRINT((ndo, " [|igrp]"));
}
