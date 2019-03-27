/*
 * Copyright (c) 2013 The TCPDUMP project
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that: (1) source code
 * distributions retain the above copyright notice and this paragraph
 * in its entirety, and (2) distributions including binary code include
 * the above copyright notice and this paragraph in its entirety in
 * the documentation or other materials provided with the distribution.
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND
 * WITHOUT ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, WITHOUT
 * LIMITATION, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE.
 *
 * Original code by Ola Martin Lykkja (ola.lykkja@q-free.com)
 */

/* \summary: ISO CALM FAST and ETSI GeoNetworking printer */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <netdissect-stdinc.h>

#include "netdissect.h"
#include "extract.h"
#include "addrtoname.h"


/*
   ETSI TS 102 636-5-1 V1.1.1 (2011-02)
   Intelligent Transport Systems (ITS); Vehicular Communications; GeoNetworking;
   Part 5: Transport Protocols; Sub-part 1: Basic Transport Protocol

   ETSI TS 102 636-4-1 V1.1.1 (2011-06)
   Intelligent Transport Systems (ITS); Vehicular communications; GeoNetworking;
   Part 4: Geographical addressing and forwarding for point-to-point and point-to-multipoint communications;
   Sub-part 1: Media-Independent Functionality
*/

#define GEONET_ADDR_LEN 8

static const struct tok msg_type_values[] = {
	{   0, "CAM" },
	{   1, "DENM" },
	{ 101, "TPEGM" },
	{ 102, "TSPDM" },
	{ 103, "VPM" },
	{ 104, "SRM" },
	{ 105, "SLAM" },
	{ 106, "ecoCAM" },
	{ 107, "ITM" },
	{ 150, "SA" },
	{   0, NULL }
};

static void
print_btp_body(netdissect_options *ndo,
               const u_char *bp)
{
	int version;
	int msg_type;
	const char *msg_type_str;

	/* Assuming ItsDpuHeader */
	version = bp[0];
	msg_type = bp[1];
	msg_type_str = tok2str(msg_type_values, "unknown (%u)", msg_type);

	ND_PRINT((ndo, "; ItsPduHeader v:%d t:%d-%s", version, msg_type, msg_type_str));
}

static void
print_btp(netdissect_options *ndo,
          const u_char *bp)
{
	uint16_t dest = EXTRACT_16BITS(bp+0);
	uint16_t src = EXTRACT_16BITS(bp+2);
	ND_PRINT((ndo, "; BTP Dst:%u Src:%u", dest, src));
}

static int
print_long_pos_vector(netdissect_options *ndo,
                      const u_char *bp)
{
	uint32_t lat, lon;

	if (!ND_TTEST2(*bp, GEONET_ADDR_LEN))
		return (-1);
	ND_PRINT((ndo, "GN_ADDR:%s ", linkaddr_string (ndo, bp, 0, GEONET_ADDR_LEN)));

	if (!ND_TTEST2(*(bp+12), 8))
		return (-1);
	lat = EXTRACT_32BITS(bp+12);
	ND_PRINT((ndo, "lat:%d ", lat));
	lon = EXTRACT_32BITS(bp+16);
	ND_PRINT((ndo, "lon:%d", lon));
	return (0);
}


/*
 * This is the top level routine of the printer.  'p' points
 * to the geonet header of the packet.
 */
void
geonet_print(netdissect_options *ndo, const u_char *bp, u_int length,
	     const struct lladdr_info *src)
{
	int version;
	int next_hdr;
	int hdr_type;
	int hdr_subtype;
	uint16_t payload_length;
	int hop_limit;
	const char *next_hdr_txt = "Unknown";
	const char *hdr_type_txt = "Unknown";
	int hdr_size = -1;

	ND_PRINT((ndo, "GeoNet "));
	if (src != NULL)
		ND_PRINT((ndo, "src:%s", (src->addr_string)(ndo, src->addr)));
	ND_PRINT((ndo, "; "));

	/* Process Common Header */
	if (length < 36)
		goto invalid;

	ND_TCHECK2(*bp, 8);
	version = bp[0] >> 4;
	next_hdr = bp[0] & 0x0f;
	hdr_type = bp[1] >> 4;
	hdr_subtype = bp[1] & 0x0f;
	payload_length = EXTRACT_16BITS(bp+4);
	hop_limit = bp[7];

	switch (next_hdr) {
		case 0: next_hdr_txt = "Any"; break;
		case 1: next_hdr_txt = "BTP-A"; break;
		case 2: next_hdr_txt = "BTP-B"; break;
		case 3: next_hdr_txt = "IPv6"; break;
	}

	switch (hdr_type) {
		case 0: hdr_type_txt = "Any"; break;
		case 1: hdr_type_txt = "Beacon"; break;
		case 2: hdr_type_txt = "GeoUnicast"; break;
		case 3: switch (hdr_subtype) {
				case 0: hdr_type_txt = "GeoAnycastCircle"; break;
				case 1: hdr_type_txt = "GeoAnycastRect"; break;
				case 2: hdr_type_txt = "GeoAnycastElipse"; break;
			}
			break;
		case 4: switch (hdr_subtype) {
				case 0: hdr_type_txt = "GeoBroadcastCircle"; break;
				case 1: hdr_type_txt = "GeoBroadcastRect"; break;
				case 2: hdr_type_txt = "GeoBroadcastElipse"; break;
			}
			break;
		case 5: switch (hdr_subtype) {
				case 0: hdr_type_txt = "TopoScopeBcast-SH"; break;
				case 1: hdr_type_txt = "TopoScopeBcast-MH"; break;
			}
			break;
		case 6: switch (hdr_subtype) {
				case 0: hdr_type_txt = "LocService-Request"; break;
				case 1: hdr_type_txt = "LocService-Reply"; break;
			}
			break;
	}

	ND_PRINT((ndo, "v:%d ", version));
	ND_PRINT((ndo, "NH:%d-%s ", next_hdr, next_hdr_txt));
	ND_PRINT((ndo, "HT:%d-%d-%s ", hdr_type, hdr_subtype, hdr_type_txt));
	ND_PRINT((ndo, "HopLim:%d ", hop_limit));
	ND_PRINT((ndo, "Payload:%d ", payload_length));
	if (print_long_pos_vector(ndo, bp + 8) == -1)
		goto trunc;

	/* Skip Common Header */
	length -= 36;
	bp += 36;

	/* Process Extended Headers */
	switch (hdr_type) {
		case 0: /* Any */
			hdr_size = 0;
			break;
		case 1: /* Beacon */
			hdr_size = 0;
			break;
		case 2: /* GeoUnicast */
			break;
		case 3: switch (hdr_subtype) {
				case 0: /* GeoAnycastCircle */
					break;
				case 1: /* GeoAnycastRect */
					break;
				case 2: /* GeoAnycastElipse */
					break;
			}
			break;
		case 4: switch (hdr_subtype) {
				case 0: /* GeoBroadcastCircle */
					break;
				case 1: /* GeoBroadcastRect */
					break;
				case 2: /* GeoBroadcastElipse */
					break;
			}
			break;
		case 5: switch (hdr_subtype) {
				case 0: /* TopoScopeBcast-SH */
					hdr_size = 0;
					break;
				case 1: /* TopoScopeBcast-MH */
					hdr_size = 68 - 36;
					break;
			}
			break;
		case 6: switch (hdr_subtype) {
				case 0: /* LocService-Request */
					break;
				case 1: /* LocService-Reply */
					break;
			}
			break;
	}

	/* Skip Extended headers */
	if (hdr_size >= 0) {
		if (length < (u_int)hdr_size)
			goto invalid;
		ND_TCHECK2(*bp, hdr_size);
		length -= hdr_size;
		bp += hdr_size;
		switch (next_hdr) {
			case 0: /* Any */
				break;
			case 1:
			case 2: /* BTP A/B */
				if (length < 4)
					goto invalid;
				ND_TCHECK2(*bp, 4);
				print_btp(ndo, bp);
				length -= 4;
				bp += 4;
				if (length >= 2) {
					/*
					 * XXX - did print_btp_body()
					 * return if length < 2
					 * because this is optional,
					 * or was that just not
					 * reporting genuine errors?
					 */
					ND_TCHECK2(*bp, 2);
					print_btp_body(ndo, bp);
				}
				break;
			case 3: /* IPv6 */
				break;
		}
	}

	/* Print user data part */
	if (ndo->ndo_vflag)
		ND_DEFAULTPRINT(bp, length);
	return;

invalid:
	ND_PRINT((ndo, " Malformed (small) "));
	/* XXX - print the remaining data as hex? */
	return;

trunc:
	ND_PRINT((ndo, "[|geonet]"));
}


/*
 * Local Variables:
 * c-style: whitesmith
 * c-basic-offset: 8
 * End:
 */
