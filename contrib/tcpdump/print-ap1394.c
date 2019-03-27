/*
 * Copyright (c) 1988, 1989, 1990, 1991, 1992, 1993, 1994, 1995, 1996, 1997, 2000
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

/* \summary: Apple IP-over-IEEE 1394 printer */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <netdissect-stdinc.h>

#include "netdissect.h"
#include "extract.h"
#include "addrtoname.h"
#include "ethertype.h"

/*
 * Structure of a header for Apple's IP-over-IEEE 1384 BPF header.
 */
#define FIREWIRE_EUI64_LEN	8
struct firewire_header {
	u_char  firewire_dhost[FIREWIRE_EUI64_LEN];
	u_char  firewire_shost[FIREWIRE_EUI64_LEN];
	u_short firewire_type;
};

/*
 * Length of that header; note that some compilers may pad
 * "struct firewire_header" to a multiple of 4 bytes, for example, so
 * "sizeof (struct firewire_header)" may not give the right answer.
 */
#define FIREWIRE_HDRLEN		18

static const char *
fwaddr_string(netdissect_options *ndo, const u_char *addr)
{
	return (linkaddr_string(ndo, addr, LINKADDR_IEEE1394, FIREWIRE_EUI64_LEN));
}

static inline void
ap1394_hdr_print(netdissect_options *ndo, register const u_char *bp, u_int length)
{
	register const struct firewire_header *fp;
	uint16_t firewire_type;

	fp = (const struct firewire_header *)bp;

	ND_PRINT((ndo, "%s > %s",
		     fwaddr_string(ndo, fp->firewire_shost),
		     fwaddr_string(ndo, fp->firewire_dhost)));

	firewire_type = EXTRACT_16BITS(&fp->firewire_type);
	if (!ndo->ndo_qflag) {
		ND_PRINT((ndo, ", ethertype %s (0x%04x)",
			       tok2str(ethertype_values,"Unknown", firewire_type),
                               firewire_type));
        } else {
                ND_PRINT((ndo, ", %s", tok2str(ethertype_values,"Unknown Ethertype (0x%04x)", firewire_type)));
        }

	ND_PRINT((ndo, ", length %u: ", length));
}

/*
 * This is the top level routine of the printer.  'p' points
 * to the ether header of the packet, 'h->ts' is the timestamp,
 * 'h->len' is the length of the packet off the wire, and 'h->caplen'
 * is the number of bytes actually captured.
 */
u_int
ap1394_if_print(netdissect_options *ndo, const struct pcap_pkthdr *h, const u_char *p)
{
	u_int length = h->len;
	u_int caplen = h->caplen;
	const struct firewire_header *fp;
	u_short ether_type;
	struct lladdr_info src, dst;

	if (caplen < FIREWIRE_HDRLEN) {
		ND_PRINT((ndo, "[|ap1394]"));
		return FIREWIRE_HDRLEN;
	}

	if (ndo->ndo_eflag)
		ap1394_hdr_print(ndo, p, length);

	length -= FIREWIRE_HDRLEN;
	caplen -= FIREWIRE_HDRLEN;
	fp = (const struct firewire_header *)p;
	p += FIREWIRE_HDRLEN;

	ether_type = EXTRACT_16BITS(&fp->firewire_type);
	src.addr = fp->firewire_shost;
	src.addr_string = fwaddr_string;
	dst.addr = fp->firewire_dhost;
	dst.addr_string = fwaddr_string;
	if (ethertype_print(ndo, ether_type, p, length, caplen, &src, &dst) == 0) {
		/* ether_type not known, print raw packet */
		if (!ndo->ndo_eflag)
			ap1394_hdr_print(ndo, (const u_char *)fp, length + FIREWIRE_HDRLEN);

		if (!ndo->ndo_suppress_default_print)
			ND_DEFAULTPRINT(p, caplen);
	}

	return FIREWIRE_HDRLEN;
}
