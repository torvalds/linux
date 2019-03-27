/*
 * Copyright (c) 1992, 1993, 1994, 1995, 1996, 1997
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
 * Code by Gert Doering, SpaceNet GmbH, gert@space.net
 *
 * Reference documentation:
 *    http://www.cisco.com/univercd/cc/td/doc/product/lan/trsrb/frames.htm
 */

/* \summary: Cisco Discovery Protocol (CDP) printer */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <netdissect-stdinc.h>

#include <string.h>

#include "netdissect.h"
#include "addrtoname.h"
#include "extract.h"
#include "nlpid.h"

static const char tstr[] = "[|cdp]";

#define CDP_HEADER_LEN             4
#define CDP_HEADER_VERSION_OFFSET  0
#define CDP_HEADER_TTL_OFFSET      1
#define CDP_HEADER_CHECKSUM_OFFSET 2

#define CDP_TLV_HEADER_LEN  4
#define CDP_TLV_TYPE_OFFSET 0
#define CDP_TLV_LEN_OFFSET  2

static const struct tok cdp_tlv_values[] = {
    { 0x01,             "Device-ID"},
    { 0x02,             "Address"},
    { 0x03,             "Port-ID"},
    { 0x04,             "Capability"},
    { 0x05,             "Version String"},
    { 0x06,             "Platform"},
    { 0x07,             "Prefixes"},
    { 0x08,             "Protocol-Hello option"},
    { 0x09,             "VTP Management Domain"},
    { 0x0a,             "Native VLAN ID"},
    { 0x0b,             "Duplex"},
    { 0x0e,             "ATA-186 VoIP VLAN request"},
    { 0x0f,             "ATA-186 VoIP VLAN assignment"},
    { 0x10,             "power consumption"},
    { 0x11,             "MTU"},
    { 0x12,             "AVVID trust bitmap"},
    { 0x13,             "AVVID untrusted ports CoS"},
    { 0x14,             "System Name"},
    { 0x15,             "System Object ID (not decoded)"},
    { 0x16,             "Management Addresses"},
    { 0x17,             "Physical Location"},
    { 0, NULL}
};

static const struct tok cdp_capability_values[] = {
    { 0x01,             "Router" },
    { 0x02,             "Transparent Bridge" },
    { 0x04,             "Source Route Bridge" },
    { 0x08,             "L2 Switch" },
    { 0x10,             "L3 capable" },
    { 0x20,             "IGMP snooping" },
    { 0x40,             "L1 capable" },
    { 0, NULL }
};

static int cdp_print_addr(netdissect_options *, const u_char *, int);
static int cdp_print_prefixes(netdissect_options *, const u_char *, int);
static unsigned long cdp_get_number(const u_char *, int);

void
cdp_print(netdissect_options *ndo,
          const u_char *pptr, u_int length, u_int caplen)
{
	int type, len, i, j;
	const u_char *tptr;

	if (caplen < CDP_HEADER_LEN) {
		ND_PRINT((ndo, "%s", tstr));
		return;
	}

	tptr = pptr; /* temporary pointer */

	ND_TCHECK2(*tptr, CDP_HEADER_LEN);
	ND_PRINT((ndo, "CDPv%u, ttl: %us", *(tptr + CDP_HEADER_VERSION_OFFSET),
					   *(tptr + CDP_HEADER_TTL_OFFSET)));
	if (ndo->ndo_vflag)
		ND_PRINT((ndo, ", checksum: 0x%04x (unverified), length %u", EXTRACT_16BITS(tptr+CDP_HEADER_CHECKSUM_OFFSET), length));
	tptr += CDP_HEADER_LEN;

	while (tptr < (pptr+length)) {
		ND_TCHECK2(*tptr, CDP_TLV_HEADER_LEN); /* read out Type and Length */
		type = EXTRACT_16BITS(tptr+CDP_TLV_TYPE_OFFSET);
		len  = EXTRACT_16BITS(tptr+CDP_TLV_LEN_OFFSET); /* object length includes the 4 bytes header length */
		if (len < CDP_TLV_HEADER_LEN) {
		    if (ndo->ndo_vflag)
			ND_PRINT((ndo, "\n\t%s (0x%02x), TLV length: %u byte%s (too short)",
			       tok2str(cdp_tlv_values,"unknown field type", type),
			       type,
			       len,
			       PLURAL_SUFFIX(len))); /* plural */
		    else
			ND_PRINT((ndo, ", %s TLV length %u too short",
			       tok2str(cdp_tlv_values,"unknown field type", type),
			       len));
		    break;
		}
		tptr += CDP_TLV_HEADER_LEN;
		len -= CDP_TLV_HEADER_LEN;

		ND_TCHECK2(*tptr, len);

		if (ndo->ndo_vflag || type == 1) { /* in non-verbose mode just print Device-ID */

		    if (ndo->ndo_vflag)
			ND_PRINT((ndo, "\n\t%s (0x%02x), value length: %u byte%s: ",
			       tok2str(cdp_tlv_values,"unknown field type", type),
			       type,
			       len,
			       PLURAL_SUFFIX(len))); /* plural */

		    switch (type) {

		    case 0x01: /* Device-ID */
			if (!ndo->ndo_vflag)
			    ND_PRINT((ndo, ", Device-ID "));
			ND_PRINT((ndo, "'"));
			(void)fn_printn(ndo, tptr, len, NULL);
			ND_PRINT((ndo, "'"));
			break;
		    case 0x02: /* Address */
			if (cdp_print_addr(ndo, tptr, len) < 0)
			    goto trunc;
			break;
		    case 0x03: /* Port-ID */
			ND_PRINT((ndo, "'"));
			(void)fn_printn(ndo, tptr, len, NULL);
			ND_PRINT((ndo, "'"));
			break;
		    case 0x04: /* Capabilities */
			if (len < 4)
			    goto trunc;
			ND_PRINT((ndo, "(0x%08x): %s",
			       EXTRACT_32BITS(tptr),
			       bittok2str(cdp_capability_values, "none", EXTRACT_32BITS(tptr))));
			break;
		    case 0x05: /* Version */
			ND_PRINT((ndo, "\n\t  "));
			for (i=0;i<len;i++) {
			    j = *(tptr+i);
			    if (j == '\n') /* lets rework the version string to
					      get a nice indentation */
				ND_PRINT((ndo, "\n\t  "));
			    else
				fn_print_char(ndo, j);
			}
			break;
		    case 0x06: /* Platform */
			ND_PRINT((ndo, "'"));
			(void)fn_printn(ndo, tptr, len, NULL);
			ND_PRINT((ndo, "'"));
			break;
		    case 0x07: /* Prefixes */
			if (cdp_print_prefixes(ndo, tptr, len) < 0)
			    goto trunc;
			break;
		    case 0x08: /* Protocol Hello Option - not documented */
			break;
		    case 0x09: /* VTP Mgmt Domain  - CDPv2 */
			ND_PRINT((ndo, "'"));
			(void)fn_printn(ndo, tptr, len, NULL);
			ND_PRINT((ndo, "'"));
			break;
		    case 0x0a: /* Native VLAN ID - CDPv2 */
			if (len < 2)
			    goto trunc;
			ND_PRINT((ndo, "%d", EXTRACT_16BITS(tptr)));
			break;
		    case 0x0b: /* Duplex - CDPv2 */
			if (len < 1)
			    goto trunc;
			ND_PRINT((ndo, "%s", *(tptr) ? "full": "half"));
			break;

		    /* http://www.cisco.com/c/en/us/td/docs/voice_ip_comm/cata/186/2_12_m/english/release/notes/186rn21m.html
		     * plus more details from other sources
		     */
		    case 0x0e: /* ATA-186 VoIP VLAN request - incomplete doc. */
			if (len < 3)
			    goto trunc;
			ND_PRINT((ndo, "app %d, vlan %d", *(tptr), EXTRACT_16BITS(tptr + 1)));
			break;
		    case 0x10: /* ATA-186 VoIP VLAN assignment - incomplete doc. */
			ND_PRINT((ndo, "%1.2fW", cdp_get_number(tptr, len) / 1000.0));
			break;
		    case 0x11: /* MTU - not documented */
			if (len < 4)
			    goto trunc;
			ND_PRINT((ndo, "%u bytes", EXTRACT_32BITS(tptr)));
			break;
		    case 0x12: /* AVVID trust bitmap - not documented */
			if (len < 1)
			    goto trunc;
			ND_PRINT((ndo, "0x%02x", *(tptr)));
			break;
		    case 0x13: /* AVVID untrusted port CoS - not documented */
			if (len < 1)
			    goto trunc;
			ND_PRINT((ndo, "0x%02x", *(tptr)));
			break;
		    case 0x14: /* System Name - not documented */
			ND_PRINT((ndo, "'"));
			(void)fn_printn(ndo, tptr, len, NULL);
			ND_PRINT((ndo, "'"));
			break;
		    case 0x16: /* System Object ID - not documented */
			if (cdp_print_addr(ndo, tptr, len) < 0)
				goto trunc;
			break;
		    case 0x17: /* Physical Location - not documented */
			if (len < 1)
			    goto trunc;
			ND_PRINT((ndo, "0x%02x", *(tptr)));
			if (len > 1) {
				ND_PRINT((ndo, "/"));
				(void)fn_printn(ndo, tptr + 1, len - 1, NULL);
			}
			break;
		    default:
			print_unknown_data(ndo, tptr, "\n\t  ", len);
			break;
		    }
		}
		tptr = tptr+len;
	}
	if (ndo->ndo_vflag < 1)
	    ND_PRINT((ndo, ", length %u", caplen));

	return;
trunc:
	ND_PRINT((ndo, "%s", tstr));
}

/*
 * Protocol type values.
 *
 * PT_NLPID means that the protocol type field contains an OSI NLPID.
 *
 * PT_IEEE_802_2 means that the protocol type field contains an IEEE 802.2
 * LLC header that specifies that the payload is for that protocol.
 */
#define PT_NLPID		1	/* OSI NLPID */
#define PT_IEEE_802_2		2	/* IEEE 802.2 LLC header */

static int
cdp_print_addr(netdissect_options *ndo,
	       const u_char * p, int l)
{
	int pt, pl, al, num;
	const u_char *endp = p + l;
	static const u_char prot_ipv6[] = {
		0xaa, 0xaa, 0x03, 0x00, 0x00, 0x00, 0x86, 0xdd
	};

	ND_TCHECK2(*p, 4);
	if (p + 4 > endp)
		goto trunc;
	num = EXTRACT_32BITS(p);
	p += 4;

	while (p < endp && num >= 0) {
		ND_TCHECK2(*p, 2);
		if (p + 2 > endp)
			goto trunc;
		pt = p[0];		/* type of "protocol" field */
		pl = p[1];		/* length of "protocol" field */
		p += 2;

		ND_TCHECK2(p[pl], 2);
		if (p + pl + 2 > endp)
			goto trunc;
		al = EXTRACT_16BITS(&p[pl]);	/* address length */

		if (pt == PT_NLPID && pl == 1 && *p == NLPID_IP && al == 4) {
			/*
			 * IPv4: protocol type = NLPID, protocol length = 1
			 * (1-byte NLPID), protocol = 0xcc (NLPID for IPv4),
			 * address length = 4
			 */
			p += 3;

			ND_TCHECK2(*p, 4);
			if (p + 4 > endp)
				goto trunc;
			ND_PRINT((ndo, "IPv4 (%u) %s", num, ipaddr_string(ndo, p)));
			p += 4;
		}
		else if (pt == PT_IEEE_802_2 && pl == 8 &&
		    memcmp(p, prot_ipv6, 8) == 0 && al == 16) {
			/*
			 * IPv6: protocol type = IEEE 802.2 header,
			 * protocol length = 8 (size of LLC+SNAP header),
			 * protocol = LLC+SNAP header with the IPv6
			 * Ethertype, address length = 16
			 */
			p += 10;
			ND_TCHECK2(*p, al);
			if (p + al > endp)
				goto trunc;

			ND_PRINT((ndo, "IPv6 (%u) %s", num, ip6addr_string(ndo, p)));
			p += al;
		}
		else {
			/*
			 * Generic case: just print raw data
			 */
			ND_TCHECK2(*p, pl);
			if (p + pl > endp)
				goto trunc;
			ND_PRINT((ndo, "pt=0x%02x, pl=%d, pb=", *(p - 2), pl));
			while (pl-- > 0)
				ND_PRINT((ndo, " %02x", *p++));
			ND_TCHECK2(*p, 2);
			if (p + 2 > endp)
				goto trunc;
			al = (*p << 8) + *(p + 1);
			ND_PRINT((ndo, ", al=%d, a=", al));
			p += 2;
			ND_TCHECK2(*p, al);
			if (p + al > endp)
				goto trunc;
			while (al-- > 0)
				ND_PRINT((ndo, " %02x", *p++));
		}
		num--;
		if (num)
			ND_PRINT((ndo, " "));
	}

	return 0;

trunc:
	return -1;
}


static int
cdp_print_prefixes(netdissect_options *ndo,
		   const u_char * p, int l)
{
	if (l % 5)
		goto trunc;

	ND_PRINT((ndo, " IPv4 Prefixes (%d):", l / 5));

	while (l > 0) {
		ND_PRINT((ndo, " %u.%u.%u.%u/%u", p[0], p[1], p[2], p[3], p[4]));
		l -= 5;
		p += 5;
	}

	return 0;

trunc:
	return -1;
}

/* read in a <n>-byte number, MSB first
 * (of course this can handle max sizeof(long))
 */
static unsigned long cdp_get_number(const u_char * p, int l)
{
    unsigned long res=0;
    while( l>0 )
    {
	res = (res<<8) + *p;
	p++; l--;
    }
    return res;
}
