/*
 * Copyright (c) 2009
 * 	Siemens AG, All rights reserved.
 * 	Dmitry Eremin-Solenikov (dbaryshkov@gmail.com)
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

/* \summary: IEEE 802.15.4 printer */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <netdissect-stdinc.h>

#include "netdissect.h"
#include "addrtoname.h"

#include "extract.h"

static const char *ftypes[] = {
	"Beacon",			/* 0 */
	"Data",				/* 1 */
	"ACK",				/* 2 */
	"Command",			/* 3 */
	"Reserved (0x4)",		/* 4 */
	"Reserved (0x5)",		/* 5 */
	"Reserved (0x6)",		/* 6 */
	"Reserved (0x7)",		/* 7 */
};

/*
 * Frame Control subfields.
 */
#define FC_FRAME_TYPE(fc)		((fc) & 0x7)
#define FC_SECURITY_ENABLED		0x0008
#define FC_FRAME_PENDING		0x0010
#define FC_ACK_REQUEST			0x0020
#define FC_PAN_ID_COMPRESSION		0x0040
#define FC_DEST_ADDRESSING_MODE(fc)	(((fc) >> 10) & 0x3)
#define FC_FRAME_VERSION(fc)		(((fc) >> 12) & 0x3)
#define FC_SRC_ADDRESSING_MODE(fc)	(((fc) >> 14) & 0x3)

#define FC_ADDRESSING_MODE_NONE		0x00
#define FC_ADDRESSING_MODE_RESERVED	0x01
#define FC_ADDRESSING_MODE_SHORT	0x02
#define FC_ADDRESSING_MODE_LONG		0x03

u_int
ieee802_15_4_if_print(netdissect_options *ndo,
                      const struct pcap_pkthdr *h, const u_char *p)
{
	u_int caplen = h->caplen;
	u_int hdrlen;
	uint16_t fc;
	uint8_t seq;
	uint16_t panid = 0;

	if (caplen < 3) {
		ND_PRINT((ndo, "[|802.15.4]"));
		return caplen;
	}
	hdrlen = 3;

	fc = EXTRACT_LE_16BITS(p);
	seq = EXTRACT_LE_8BITS(p + 2);

	p += 3;
	caplen -= 3;

	ND_PRINT((ndo,"IEEE 802.15.4 %s packet ", ftypes[FC_FRAME_TYPE(fc)]));
	if (ndo->ndo_vflag)
		ND_PRINT((ndo,"seq %02x ", seq));

	/*
	 * Destination address and PAN ID, if present.
	 */
	switch (FC_DEST_ADDRESSING_MODE(fc)) {
	case FC_ADDRESSING_MODE_NONE:
		if (fc & FC_PAN_ID_COMPRESSION) {
			/*
			 * PAN ID compression; this requires that both
			 * the source and destination addresses be present,
			 * but the destination address is missing.
			 */
			ND_PRINT((ndo, "[|802.15.4]"));
			return hdrlen;
		}
		if (ndo->ndo_vflag)
			ND_PRINT((ndo,"none "));
		break;
	case FC_ADDRESSING_MODE_RESERVED:
		if (ndo->ndo_vflag)
			ND_PRINT((ndo,"reserved destination addressing mode"));
		return hdrlen;
	case FC_ADDRESSING_MODE_SHORT:
		if (caplen < 2) {
			ND_PRINT((ndo, "[|802.15.4]"));
			return hdrlen;
		}
		panid = EXTRACT_LE_16BITS(p);
		p += 2;
		caplen -= 2;
		hdrlen += 2;
		if (caplen < 2) {
			ND_PRINT((ndo, "[|802.15.4]"));
			return hdrlen;
		}
		if (ndo->ndo_vflag)
			ND_PRINT((ndo,"%04x:%04x ", panid, EXTRACT_LE_16BITS(p)));
		p += 2;
		caplen -= 2;
		hdrlen += 2;
		break;
	case FC_ADDRESSING_MODE_LONG:
		if (caplen < 2) {
			ND_PRINT((ndo, "[|802.15.4]"));
			return hdrlen;
		}
		panid = EXTRACT_LE_16BITS(p);
		p += 2;
		caplen -= 2;
		hdrlen += 2;
		if (caplen < 8) {
			ND_PRINT((ndo, "[|802.15.4]"));
			return hdrlen;
		}
		if (ndo->ndo_vflag)
			ND_PRINT((ndo,"%04x:%s ", panid, le64addr_string(ndo, p)));
		p += 8;
		caplen -= 8;
		hdrlen += 8;
		break;
	}
	if (ndo->ndo_vflag)
		ND_PRINT((ndo,"< "));

	/*
	 * Source address and PAN ID, if present.
	 */
	switch (FC_SRC_ADDRESSING_MODE(fc)) {
	case FC_ADDRESSING_MODE_NONE:
		if (ndo->ndo_vflag)
			ND_PRINT((ndo,"none "));
		break;
	case FC_ADDRESSING_MODE_RESERVED:
		if (ndo->ndo_vflag)
			ND_PRINT((ndo,"reserved source addressing mode"));
		return 0;
	case FC_ADDRESSING_MODE_SHORT:
		if (!(fc & FC_PAN_ID_COMPRESSION)) {
			/*
			 * The source PAN ID is not compressed out, so
			 * fetch it.  (Otherwise, we'll use the destination
			 * PAN ID, fetched above.)
			 */
			if (caplen < 2) {
				ND_PRINT((ndo, "[|802.15.4]"));
				return hdrlen;
			}
			panid = EXTRACT_LE_16BITS(p);
			p += 2;
			caplen -= 2;
			hdrlen += 2;
		}
		if (caplen < 2) {
			ND_PRINT((ndo, "[|802.15.4]"));
			return hdrlen;
		}
		if (ndo->ndo_vflag)
			ND_PRINT((ndo,"%04x:%04x ", panid, EXTRACT_LE_16BITS(p)));
		p += 2;
		caplen -= 2;
		hdrlen += 2;
		break;
	case FC_ADDRESSING_MODE_LONG:
		if (!(fc & FC_PAN_ID_COMPRESSION)) {
			/*
			 * The source PAN ID is not compressed out, so
			 * fetch it.  (Otherwise, we'll use the destination
			 * PAN ID, fetched above.)
			 */
			if (caplen < 2) {
				ND_PRINT((ndo, "[|802.15.4]"));
				return hdrlen;
			}
			panid = EXTRACT_LE_16BITS(p);
			p += 2;
			caplen -= 2;
			hdrlen += 2;
		}
		if (caplen < 8) {
			ND_PRINT((ndo, "[|802.15.4]"));
			return hdrlen;
		}
		if (ndo->ndo_vflag)
			ND_PRINT((ndo,"%04x:%s ", panid, le64addr_string(ndo, p)));
		p += 8;
		caplen -= 8;
		hdrlen += 8;
		break;
	}

	if (!ndo->ndo_suppress_default_print)
		ND_DEFAULTPRINT(p, caplen);

	return hdrlen;
}
