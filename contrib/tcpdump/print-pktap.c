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

/* \summary: Apple's DLT_PKTAP printer */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <netdissect-stdinc.h>

#include "netdissect.h"
#include "extract.h"

#ifdef DLT_PKTAP

/*
 * XXX - these are little-endian in the captures I've seen, but Apple
 * no longer make any big-endian machines (Macs use x86, iOS machines
 * use ARM and run it little-endian), so that might be by definition
 * or they might be host-endian.
 *
 * If a big-endian PKTAP file ever shows up, and it comes from a
 * big-endian machine, presumably these are host-endian, and we need
 * to just fetch the fields directly in tcpdump but byte-swap them
 * to host byte order in libpcap.
 */
typedef struct pktap_header {
	uint32_t	pkt_len;	/* length of pktap header */
	uint32_t	pkt_rectype;	/* type of record */
	uint32_t	pkt_dlt;	/* DLT type of this packet */
	char		pkt_ifname[24];	/* interface name */
	uint32_t	pkt_flags;
	uint32_t	pkt_pfamily;	/* "protocol family" */
	uint32_t	pkt_llhdrlen;	/* link-layer header length? */
	uint32_t	pkt_lltrlrlen;	/* link-layer trailer length? */
	uint32_t	pkt_pid;	/* process ID */
	char		pkt_cmdname[20]; /* command name */
	uint32_t	pkt_svc_class;	/* "service class" */
	uint16_t	pkt_iftype;	/* "interface type" */
	uint16_t	pkt_ifunit;	/* unit number of interface? */
	uint32_t	pkt_epid;	/* "effective process ID" */
	char		pkt_ecmdname[20]; /* "effective command name" */
} pktap_header_t;

/*
 * Record types.
 */
#define PKT_REC_NONE	0	/* nothing follows the header */
#define PKT_REC_PACKET	1	/* a packet follows the header */

static inline void
pktap_header_print(netdissect_options *ndo, const u_char *bp, u_int length)
{
	const pktap_header_t *hdr;
	uint32_t dlt, hdrlen;
	const char *dltname;

	hdr = (const pktap_header_t *)bp;

	dlt = EXTRACT_LE_32BITS(&hdr->pkt_dlt);
	hdrlen = EXTRACT_LE_32BITS(&hdr->pkt_len);
	dltname = pcap_datalink_val_to_name(dlt);
	if (!ndo->ndo_qflag) {
		ND_PRINT((ndo,"DLT %s (%d) len %d",
			  (dltname != NULL ? dltname : "UNKNOWN"), dlt, hdrlen));
        } else {
		ND_PRINT((ndo,"%s", (dltname != NULL ? dltname : "UNKNOWN")));
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
pktap_if_print(netdissect_options *ndo,
               const struct pcap_pkthdr *h, const u_char *p)
{
	uint32_t dlt, hdrlen, rectype;
	u_int caplen = h->caplen;
	u_int length = h->len;
	if_printer printer;
	const pktap_header_t *hdr;
	struct pcap_pkthdr nhdr;

	if (caplen < sizeof(pktap_header_t) || length < sizeof(pktap_header_t)) {
		ND_PRINT((ndo, "[|pktap]"));
		return (0);
	}
	hdr = (const pktap_header_t *)p;
	dlt = EXTRACT_LE_32BITS(&hdr->pkt_dlt);
	hdrlen = EXTRACT_LE_32BITS(&hdr->pkt_len);
	if (hdrlen < sizeof(pktap_header_t)) {
		/*
		 * Claimed header length < structure length.
		 * XXX - does this just mean some fields aren't
		 * being supplied, or is it truly an error (i.e.,
		 * is the length supplied so that the header can
		 * be expanded in the future)?
		 */
		ND_PRINT((ndo, "[|pktap]"));
		return (0);
	}
	if (caplen < hdrlen || length < hdrlen) {
		ND_PRINT((ndo, "[|pktap]"));
		return (hdrlen);
	}

	if (ndo->ndo_eflag)
		pktap_header_print(ndo, p, length);

	length -= hdrlen;
	caplen -= hdrlen;
	p += hdrlen;

	rectype = EXTRACT_LE_32BITS(&hdr->pkt_rectype);
	switch (rectype) {

	case PKT_REC_NONE:
		ND_PRINT((ndo, "no data"));
		break;

	case PKT_REC_PACKET:
		if ((printer = lookup_printer(dlt)) != NULL) {
			nhdr = *h;
			nhdr.caplen = caplen;
			nhdr.len = length;
			hdrlen += printer(ndo, &nhdr, p);
		} else {
			if (!ndo->ndo_eflag)
				pktap_header_print(ndo, (const u_char *)hdr,
						length + hdrlen);

			if (!ndo->ndo_suppress_default_print)
				ND_DEFAULTPRINT(p, caplen);
		}
		break;
	}

	return (hdrlen);
}

/*
 * Local Variables:
 * c-style: whitesmith
 * c-basic-offset: 8
 * End:
 */

#endif /* DLT_PKTAP */
