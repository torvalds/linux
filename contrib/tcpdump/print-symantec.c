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

/* \summary: Symantec Enterprise Firewall printer */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <netdissect-stdinc.h>

#include "netdissect.h"
#include "extract.h"
#include "ethertype.h"

#include "ether.h"

struct symantec_header {
	uint8_t  stuff1[6];
	uint16_t ether_type;
	uint8_t  stuff2[36];
};

static inline void
symantec_hdr_print(netdissect_options *ndo, register const u_char *bp, u_int length)
{
	register const struct symantec_header *sp;
	uint16_t etype;

	sp = (const struct symantec_header *)bp;

	etype = EXTRACT_16BITS(&sp->ether_type);
	if (!ndo->ndo_qflag) {
	        if (etype <= ETHERMTU)
		          ND_PRINT((ndo, "invalid ethertype %u", etype));
                else
		          ND_PRINT((ndo, "ethertype %s (0x%04x)",
				       tok2str(ethertype_values,"Unknown", etype),
                                       etype));
        } else {
                if (etype <= ETHERMTU)
                          ND_PRINT((ndo, "invalid ethertype %u", etype));
                else
                          ND_PRINT((ndo, "%s", tok2str(ethertype_values,"Unknown Ethertype (0x%04x)", etype)));
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
symantec_if_print(netdissect_options *ndo, const struct pcap_pkthdr *h, const u_char *p)
{
	u_int length = h->len;
	u_int caplen = h->caplen;
	const struct symantec_header *sp;
	u_short ether_type;

	if (caplen < sizeof (struct symantec_header)) {
		ND_PRINT((ndo, "[|symantec]"));
		return caplen;
	}

	if (ndo->ndo_eflag)
		symantec_hdr_print(ndo, p, length);

	length -= sizeof (struct symantec_header);
	caplen -= sizeof (struct symantec_header);
	sp = (const struct symantec_header *)p;
	p += sizeof (struct symantec_header);

	ether_type = EXTRACT_16BITS(&sp->ether_type);

	if (ether_type <= ETHERMTU) {
		/* ether_type not known, print raw packet */
		if (!ndo->ndo_eflag)
			symantec_hdr_print(ndo, (const u_char *)sp, length + sizeof (struct symantec_header));

		if (!ndo->ndo_suppress_default_print)
			ND_DEFAULTPRINT(p, caplen);
	} else if (ethertype_print(ndo, ether_type, p, length, caplen, NULL, NULL) == 0) {
		/* ether_type not known, print raw packet */
		if (!ndo->ndo_eflag)
			symantec_hdr_print(ndo, (const u_char *)sp, length + sizeof (struct symantec_header));

		if (!ndo->ndo_suppress_default_print)
			ND_DEFAULTPRINT(p, caplen);
	}

	return (sizeof (struct symantec_header));
}
