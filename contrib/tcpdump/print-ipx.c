/*
 * Copyright (c) 1994, 1995, 1996
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
 * Contributed by Brad Parker (brad@fcr.com).
 */

/* \summary: Novell IPX printer */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <netdissect-stdinc.h>

#include <stdio.h>

#include "netdissect.h"
#include "addrtoname.h"
#include "extract.h"

/* well-known sockets */
#define	IPX_SKT_NCP		0x0451
#define	IPX_SKT_SAP		0x0452
#define	IPX_SKT_RIP		0x0453
#define	IPX_SKT_NETBIOS		0x0455
#define	IPX_SKT_DIAGNOSTICS	0x0456
#define	IPX_SKT_NWLINK_DGM	0x0553	/* NWLink datagram, may contain SMB */
#define	IPX_SKT_EIGRP		0x85be	/* Cisco EIGRP over IPX */

/* IPX transport header */
struct ipxHdr {
    uint16_t	cksum;		/* Checksum */
    uint16_t	length;		/* Length, in bytes, including header */
    uint8_t	tCtl;		/* Transport Control (i.e. hop count) */
    uint8_t	pType;		/* Packet Type (i.e. level 2 protocol) */
    uint16_t	dstNet[2];	/* destination net */
    uint8_t	dstNode[6];	/* destination node */
    uint16_t	dstSkt;		/* destination socket */
    uint16_t	srcNet[2];	/* source net */
    uint8_t	srcNode[6];	/* source node */
    uint16_t	srcSkt;		/* source socket */
};

#define ipxSize	30

static const char *ipxaddr_string(uint32_t, const u_char *);
static void ipx_decode(netdissect_options *, const struct ipxHdr *, const u_char *, u_int);
static void ipx_sap_print(netdissect_options *, const u_short *, u_int);
static void ipx_rip_print(netdissect_options *, const u_short *, u_int);

/*
 * Print IPX datagram packets.
 */
void
ipx_print(netdissect_options *ndo, const u_char *p, u_int length)
{
	const struct ipxHdr *ipx = (const struct ipxHdr *)p;

	if (!ndo->ndo_eflag)
		ND_PRINT((ndo, "IPX "));

	ND_TCHECK(ipx->srcSkt);
	ND_PRINT((ndo, "%s.%04x > ",
		     ipxaddr_string(EXTRACT_32BITS(ipx->srcNet), ipx->srcNode),
		     EXTRACT_16BITS(&ipx->srcSkt)));

	ND_PRINT((ndo, "%s.%04x: ",
		     ipxaddr_string(EXTRACT_32BITS(ipx->dstNet), ipx->dstNode),
		     EXTRACT_16BITS(&ipx->dstSkt)));

	/* take length from ipx header */
	ND_TCHECK(ipx->length);
	length = EXTRACT_16BITS(&ipx->length);

	ipx_decode(ndo, ipx, p + ipxSize, length - ipxSize);
	return;
trunc:
	ND_PRINT((ndo, "[|ipx %d]", length));
}

static const char *
ipxaddr_string(uint32_t net, const u_char *node)
{
    static char line[256];

    snprintf(line, sizeof(line), "%08x.%02x:%02x:%02x:%02x:%02x:%02x",
	    net, node[0], node[1], node[2], node[3], node[4], node[5]);

    return line;
}

static void
ipx_decode(netdissect_options *ndo, const struct ipxHdr *ipx, const u_char *datap, u_int length)
{
    register u_short dstSkt;

    dstSkt = EXTRACT_16BITS(&ipx->dstSkt);
    switch (dstSkt) {
      case IPX_SKT_NCP:
	ND_PRINT((ndo, "ipx-ncp %d", length));
	break;
      case IPX_SKT_SAP:
	ipx_sap_print(ndo, (const u_short *)datap, length);
	break;
      case IPX_SKT_RIP:
	ipx_rip_print(ndo, (const u_short *)datap, length);
	break;
      case IPX_SKT_NETBIOS:
	ND_PRINT((ndo, "ipx-netbios %d", length));
#ifdef ENABLE_SMB
	ipx_netbios_print(ndo, datap, length);
#endif
	break;
      case IPX_SKT_DIAGNOSTICS:
	ND_PRINT((ndo, "ipx-diags %d", length));
	break;
      case IPX_SKT_NWLINK_DGM:
	ND_PRINT((ndo, "ipx-nwlink-dgm %d", length));
#ifdef ENABLE_SMB
	ipx_netbios_print(ndo, datap, length);
#endif
	break;
      case IPX_SKT_EIGRP:
	eigrp_print(ndo, datap, length);
	break;
      default:
	ND_PRINT((ndo, "ipx-#%x %d", dstSkt, length));
	break;
    }
}

static void
ipx_sap_print(netdissect_options *ndo, const u_short *ipx, u_int length)
{
    int command, i;

    ND_TCHECK(ipx[0]);
    command = EXTRACT_16BITS(ipx);
    ipx++;
    length -= 2;

    switch (command) {
      case 1:
      case 3:
	if (command == 1)
	    ND_PRINT((ndo, "ipx-sap-req"));
	else
	    ND_PRINT((ndo, "ipx-sap-nearest-req"));

	ND_TCHECK(ipx[0]);
	ND_PRINT((ndo, " %s", ipxsap_string(ndo, htons(EXTRACT_16BITS(&ipx[0])))));
	break;

      case 2:
      case 4:
	if (command == 2)
	    ND_PRINT((ndo, "ipx-sap-resp"));
	else
	    ND_PRINT((ndo, "ipx-sap-nearest-resp"));

	for (i = 0; i < 8 && length > 0; i++) {
	    ND_TCHECK(ipx[0]);
	    ND_PRINT((ndo, " %s '", ipxsap_string(ndo, htons(EXTRACT_16BITS(&ipx[0])))));
	    if (fn_printzp(ndo, (const u_char *)&ipx[1], 48, ndo->ndo_snapend)) {
		ND_PRINT((ndo, "'"));
		goto trunc;
	    }
	    ND_TCHECK2(ipx[25], 10);
	    ND_PRINT((ndo, "' addr %s",
		ipxaddr_string(EXTRACT_32BITS(&ipx[25]), (const u_char *)&ipx[27])));
	    ipx += 32;
	    length -= 64;
	}
	break;
      default:
	ND_PRINT((ndo, "ipx-sap-?%x", command));
	break;
    }
    return;
trunc:
    ND_PRINT((ndo, "[|ipx %d]", length));
}

static void
ipx_rip_print(netdissect_options *ndo, const u_short *ipx, u_int length)
{
    int command, i;

    ND_TCHECK(ipx[0]);
    command = EXTRACT_16BITS(ipx);
    ipx++;
    length -= 2;

    switch (command) {
      case 1:
	ND_PRINT((ndo, "ipx-rip-req"));
	if (length > 0) {
	    ND_TCHECK(ipx[3]);
	    ND_PRINT((ndo, " %08x/%d.%d", EXTRACT_32BITS(&ipx[0]),
			 EXTRACT_16BITS(&ipx[2]), EXTRACT_16BITS(&ipx[3])));
	}
	break;
      case 2:
	ND_PRINT((ndo, "ipx-rip-resp"));
	for (i = 0; i < 50 && length > 0; i++) {
	    ND_TCHECK(ipx[3]);
	    ND_PRINT((ndo, " %08x/%d.%d", EXTRACT_32BITS(&ipx[0]),
			 EXTRACT_16BITS(&ipx[2]), EXTRACT_16BITS(&ipx[3])));

	    ipx += 4;
	    length -= 8;
	}
	break;
      default:
	ND_PRINT((ndo, "ipx-rip-?%x", command));
	break;
    }
    return;
trunc:
    ND_PRINT((ndo, "[|ipx %d]", length));
}
