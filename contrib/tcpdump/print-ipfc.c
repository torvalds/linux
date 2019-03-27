/*
 * Copyright (c) 1991, 1992, 1993, 1994, 1995, 1996, 1997
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
 */

/* \summary: IP over Fibre Channel printer */

/* specification: RFC 2625 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <netdissect-stdinc.h>

#include <string.h>

#include "netdissect.h"
#include "addrtoname.h"

#include "ether.h"

struct ipfc_header {
	u_char  ipfc_dhost[8];
	u_char  ipfc_shost[8];
};

#define IPFC_HDRLEN 16

/* Extract src, dst addresses */
static inline void
extract_ipfc_addrs(const struct ipfc_header *ipfcp, char *ipfcsrc,
    char *ipfcdst)
{
	/*
	 * We assume that, as per RFC 2625, the lower 48 bits of the
	 * source and destination addresses are MAC addresses.
	 */
	memcpy(ipfcdst, (const char *)&ipfcp->ipfc_dhost[2], 6);
	memcpy(ipfcsrc, (const char *)&ipfcp->ipfc_shost[2], 6);
}

/*
 * Print the Network_Header
 */
static inline void
ipfc_hdr_print(netdissect_options *ndo,
	   register const struct ipfc_header *ipfcp _U_,
	   register u_int length, register const u_char *ipfcsrc,
	   register const u_char *ipfcdst)
{
	const char *srcname, *dstname;

	srcname = etheraddr_string(ndo, ipfcsrc);
	dstname = etheraddr_string(ndo, ipfcdst);

	/*
	 * XXX - should we show the upper 16 bits of the addresses?
	 * Do so only if "vflag" is set?
	 * Section 3.3 "FC Port and Node Network Addresses" says that
	 *
	 *    In this specification, both the Source and Destination
	 *    4-bit NAA identifiers SHALL be set to binary '0001'
	 *    indicating that an IEEE 48-bit MAC address is contained
	 *    in the lower 48 bits of the network address fields. The
	 *    high order 12 bits in the network address fields SHALL
	 *    be set to 0x0000.
	 *
	 * so, for captures following this specification, the upper 16
	 * bits should be 0x1000, followed by a MAC address.
	 */
	ND_PRINT((ndo, "%s > %s, length %u: ", srcname, dstname, length));
}

static u_int
ipfc_print(netdissect_options *ndo, const u_char *p, u_int length, u_int caplen)
{
	const struct ipfc_header *ipfcp = (const struct ipfc_header *)p;
	struct ether_header ehdr;
	struct lladdr_info src, dst;
	int llc_hdrlen;

	if (caplen < IPFC_HDRLEN) {
		ND_PRINT((ndo, "[|ipfc]"));
		return (caplen);
	}
	/*
	 * Get the network addresses into a canonical form
	 */
	extract_ipfc_addrs(ipfcp, (char *)ESRC(&ehdr), (char *)EDST(&ehdr));

	if (ndo->ndo_eflag)
		ipfc_hdr_print(ndo, ipfcp, length, ESRC(&ehdr), EDST(&ehdr));

	src.addr = ESRC(&ehdr);
	src.addr_string = etheraddr_string;
	dst.addr = EDST(&ehdr);
	dst.addr_string = etheraddr_string;

	/* Skip over Network_Header */
	length -= IPFC_HDRLEN;
	p += IPFC_HDRLEN;
	caplen -= IPFC_HDRLEN;

	/* Try to print the LLC-layer header & higher layers */
	llc_hdrlen = llc_print(ndo, p, length, caplen, &src, &dst);
	if (llc_hdrlen < 0) {
		/*
		 * Some kinds of LLC packet we cannot
		 * handle intelligently
		 */
		if (!ndo->ndo_suppress_default_print)
			ND_DEFAULTPRINT(p, caplen);
		llc_hdrlen = -llc_hdrlen;
	}
	return (IPFC_HDRLEN + llc_hdrlen);
}

/*
 * This is the top level routine of the printer.  'p' points
 * to the Network_Header of the packet, 'h->ts' is the timestamp,
 * 'h->len' is the length of the packet off the wire, and 'h->caplen'
 * is the number of bytes actually captured.
 */
u_int
ipfc_if_print(netdissect_options *ndo, const struct pcap_pkthdr *h, register const u_char *p)
{
	return (ipfc_print(ndo, p, h->len, h->caplen));
}
