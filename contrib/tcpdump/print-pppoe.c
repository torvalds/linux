/*
 * Copyright (c) 1988, 1989, 1990, 1991, 1992, 1993, 1994, 1995, 1996, 1997
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
 * Original code by Greg Stark <gsstark@mit.edu>
 */

/* \summary: PPP-over-Ethernet (PPPoE) printer */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <netdissect-stdinc.h>

#include "netdissect.h"
#include "extract.h"

/* Codes */
enum {
	PPPOE_PADI = 0x09,
	PPPOE_PADO = 0x07,
	PPPOE_PADR = 0x19,
	PPPOE_PADS = 0x65,
	PPPOE_PADT = 0xa7
};

static const struct tok pppoecode2str[] = {
	{ PPPOE_PADI, "PADI" },
	{ PPPOE_PADO, "PADO" },
	{ PPPOE_PADR, "PADR" },
	{ PPPOE_PADS, "PADS" },
	{ PPPOE_PADT, "PADT" },
	{ 0, "" }, /* PPP Data */
	{ 0, NULL }
};

/* Tags */
enum {
	PPPOE_EOL = 0,
	PPPOE_SERVICE_NAME = 0x0101,
	PPPOE_AC_NAME = 0x0102,
	PPPOE_HOST_UNIQ = 0x0103,
	PPPOE_AC_COOKIE = 0x0104,
	PPPOE_VENDOR = 0x0105,
	PPPOE_RELAY_SID = 0x0110,
	PPPOE_MAX_PAYLOAD = 0x0120,
	PPPOE_SERVICE_NAME_ERROR = 0x0201,
	PPPOE_AC_SYSTEM_ERROR = 0x0202,
	PPPOE_GENERIC_ERROR = 0x0203
};

static const struct tok pppoetag2str[] = {
	{ PPPOE_EOL, "EOL" },
	{ PPPOE_SERVICE_NAME, "Service-Name" },
	{ PPPOE_AC_NAME, "AC-Name" },
	{ PPPOE_HOST_UNIQ, "Host-Uniq" },
	{ PPPOE_AC_COOKIE, "AC-Cookie" },
	{ PPPOE_VENDOR, "Vendor-Specific" },
	{ PPPOE_RELAY_SID, "Relay-Session-ID" },
	{ PPPOE_MAX_PAYLOAD, "PPP-Max-Payload" },
	{ PPPOE_SERVICE_NAME_ERROR, "Service-Name-Error" },
	{ PPPOE_AC_SYSTEM_ERROR, "AC-System-Error" },
	{ PPPOE_GENERIC_ERROR, "Generic-Error" },
	{ 0, NULL }
};

#define PPPOE_HDRLEN 6
#define MAXTAGPRINT 80

u_int
pppoe_if_print(netdissect_options *ndo, const struct pcap_pkthdr *h, register const u_char *p)
{
	return (pppoe_print(ndo, p, h->len));
}

u_int
pppoe_print(netdissect_options *ndo, register const u_char *bp, u_int length)
{
	uint16_t pppoe_ver, pppoe_type, pppoe_code, pppoe_sessionid;
	u_int pppoe_length;
	const u_char *pppoe_packet, *pppoe_payload;

	if (length < PPPOE_HDRLEN) {
		ND_PRINT((ndo, "truncated-pppoe %u", length));
		return (length);
	}
	length -= PPPOE_HDRLEN;
	pppoe_packet = bp;
	ND_TCHECK2(*pppoe_packet, PPPOE_HDRLEN);
	pppoe_ver  = (pppoe_packet[0] & 0xF0) >> 4;
	pppoe_type  = (pppoe_packet[0] & 0x0F);
	pppoe_code = pppoe_packet[1];
	pppoe_sessionid = EXTRACT_16BITS(pppoe_packet + 2);
	pppoe_length    = EXTRACT_16BITS(pppoe_packet + 4);
	pppoe_payload = pppoe_packet + PPPOE_HDRLEN;

	if (pppoe_ver != 1) {
		ND_PRINT((ndo, " [ver %d]",pppoe_ver));
	}
	if (pppoe_type != 1) {
		ND_PRINT((ndo, " [type %d]",pppoe_type));
	}

	ND_PRINT((ndo, "PPPoE %s", tok2str(pppoecode2str, "PAD-%x", pppoe_code)));
	if (pppoe_code == PPPOE_PADI && pppoe_length > 1484 - PPPOE_HDRLEN) {
		ND_PRINT((ndo, " [len %u!]",pppoe_length));
	}
	if (pppoe_length > length) {
		ND_PRINT((ndo, " [len %u > %u!]", pppoe_length, length));
		pppoe_length = length;
	}
	if (pppoe_sessionid) {
		ND_PRINT((ndo, " [ses 0x%x]", pppoe_sessionid));
	}

	if (pppoe_code) {
		/* PPP session packets don't contain tags */
		u_short tag_type = 0xffff, tag_len;
		const u_char *p = pppoe_payload;

		/*
		 * loop invariant:
		 * p points to current tag,
		 * tag_type is previous tag or 0xffff for first iteration
		 */
		while (tag_type && p < pppoe_payload + pppoe_length) {
			ND_TCHECK2(*p, 4);
			tag_type = EXTRACT_16BITS(p);
			tag_len = EXTRACT_16BITS(p + 2);
			p += 4;
			/* p points to tag_value */

			if (tag_len) {
				unsigned ascii_count = 0, garbage_count = 0;
				const u_char *v;
				char tag_str[MAXTAGPRINT];
				unsigned tag_str_len = 0;

				/* TODO print UTF-8 decoded text */
				ND_TCHECK2(*p, tag_len);
				for (v = p; v < p + tag_len && tag_str_len < MAXTAGPRINT-1; v++)
					if (*v >= 32 && *v < 127) {
						tag_str[tag_str_len++] = *v;
						ascii_count++;
					} else {
						tag_str[tag_str_len++] = '.';
						garbage_count++;
					}
				tag_str[tag_str_len] = 0;

				if (ascii_count > garbage_count) {
					ND_PRINT((ndo, " [%s \"%*.*s\"]",
					       tok2str(pppoetag2str, "TAG-0x%x", tag_type),
					       (int)tag_str_len,
					       (int)tag_str_len,
					       tag_str));
				} else {
					/* Print hex, not fast to abuse printf but this doesn't get used much */
					ND_PRINT((ndo, " [%s 0x", tok2str(pppoetag2str, "TAG-0x%x", tag_type)));
					for (v=p; v<p+tag_len; v++) {
						ND_PRINT((ndo, "%02X", *v));
					}
					ND_PRINT((ndo, "]"));
				}


			} else
				ND_PRINT((ndo, " [%s]", tok2str(pppoetag2str,
				    "TAG-0x%x", tag_type)));

			p += tag_len;
			/* p points to next tag */
		}
		return (0);
	} else {
		/* PPPoE data */
		ND_PRINT((ndo, " "));
		return (PPPOE_HDRLEN + ppp_print(ndo, pppoe_payload, pppoe_length));
	}

trunc:
	ND_PRINT((ndo, "[|pppoe]"));
	return (PPPOE_HDRLEN);
}
