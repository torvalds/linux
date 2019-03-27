/*
 * Copyright (c) 1989, 1990, 1991, 1993, 1994, 1995, 1996, 1997
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

/* \summary: Compressed Serial Line Internet Protocol printer */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <netdissect-stdinc.h>

#include "netdissect.h"
#include "extract.h"

#include "ip.h"
#include "tcp.h"
#include "slcompress.h"

/*
 * definitions of the pseudo- link-level header attached to slip
 * packets grabbed by the packet filter (bpf) traffic monitor.
 */
#define SLIP_HDRLEN 16

#define SLX_DIR 0
#define SLX_CHDR 1
#define CHDR_LEN 15

#define SLIPDIR_IN 0
#define SLIPDIR_OUT 1

static const char tstr[] = "[|slip]";

static u_int lastlen[2][256];
static u_int lastconn = 255;

static void sliplink_print(netdissect_options *, const u_char *, const struct ip *, u_int);
static void compressed_sl_print(netdissect_options *, const u_char *, const struct ip *, u_int, int);

u_int
sl_if_print(netdissect_options *ndo,
            const struct pcap_pkthdr *h, const u_char *p)
{
	register u_int caplen = h->caplen;
	register u_int length = h->len;
	register const struct ip *ip;

	if (caplen < SLIP_HDRLEN || length < SLIP_HDRLEN) {
		ND_PRINT((ndo, "%s", tstr));
		return (caplen);
	}

	caplen -= SLIP_HDRLEN;
	length -= SLIP_HDRLEN;

	ip = (const struct ip *)(p + SLIP_HDRLEN);

	if (ndo->ndo_eflag)
		sliplink_print(ndo, p, ip, length);

	if (caplen < 1 || length < 1) {
		ND_PRINT((ndo, "%s", tstr));
		return (caplen + SLIP_HDRLEN);
	}

	switch (IP_V(ip)) {
	case 4:
	        ip_print(ndo, (const u_char *)ip, length);
		break;
	case 6:
		ip6_print(ndo, (const u_char *)ip, length);
		break;
	default:
		ND_PRINT((ndo, "ip v%d", IP_V(ip)));
	}

	return (SLIP_HDRLEN);
}

u_int
sl_bsdos_if_print(netdissect_options *ndo,
                  const struct pcap_pkthdr *h, const u_char *p)
{
	register u_int caplen = h->caplen;
	register u_int length = h->len;
	register const struct ip *ip;

	if (caplen < SLIP_HDRLEN) {
		ND_PRINT((ndo, "%s", tstr));
		return (caplen);
	}

	length -= SLIP_HDRLEN;

	ip = (const struct ip *)(p + SLIP_HDRLEN);

#ifdef notdef
	if (ndo->ndo_eflag)
		sliplink_print(ndo, p, ip, length);
#endif

	ip_print(ndo, (const u_char *)ip, length);

	return (SLIP_HDRLEN);
}

static void
sliplink_print(netdissect_options *ndo,
               register const u_char *p, register const struct ip *ip,
               register u_int length)
{
	int dir;
	u_int hlen;

	dir = p[SLX_DIR];
	switch (dir) {

	case SLIPDIR_IN:
		ND_PRINT((ndo, "I "));
		break;

	case SLIPDIR_OUT:
		ND_PRINT((ndo, "O "));
		break;

	default:
		ND_PRINT((ndo, "Invalid direction %d ", dir));
		dir = -1;
		break;
	}
	if (ndo->ndo_nflag) {
		/* XXX just dump the header */
		register int i;

		for (i = SLX_CHDR; i < SLX_CHDR + CHDR_LEN - 1; ++i)
			ND_PRINT((ndo, "%02x.", p[i]));
		ND_PRINT((ndo, "%02x: ", p[SLX_CHDR + CHDR_LEN - 1]));
		return;
	}
	switch (p[SLX_CHDR] & 0xf0) {

	case TYPE_IP:
		ND_PRINT((ndo, "ip %d: ", length + SLIP_HDRLEN));
		break;

	case TYPE_UNCOMPRESSED_TCP:
		/*
		 * The connection id is stored in the IP protocol field.
		 * Get it from the link layer since sl_uncompress_tcp()
		 * has restored the IP header copy to IPPROTO_TCP.
		 */
		lastconn = ((const struct ip *)&p[SLX_CHDR])->ip_p;
		ND_PRINT((ndo, "utcp %d: ", lastconn));
		if (dir == -1) {
			/* Direction is bogus, don't use it */
			return;
		}
		hlen = IP_HL(ip);
		hlen += TH_OFF((const struct tcphdr *)&((const int *)ip)[hlen]);
		lastlen[dir][lastconn] = length - (hlen << 2);
		break;

	default:
		if (dir == -1) {
			/* Direction is bogus, don't use it */
			return;
		}
		if (p[SLX_CHDR] & TYPE_COMPRESSED_TCP) {
			compressed_sl_print(ndo, &p[SLX_CHDR], ip,
			    length, dir);
			ND_PRINT((ndo, ": "));
		} else
			ND_PRINT((ndo, "slip-%d!: ", p[SLX_CHDR]));
	}
}

static const u_char *
print_sl_change(netdissect_options *ndo,
                const char *str, register const u_char *cp)
{
	register u_int i;

	if ((i = *cp++) == 0) {
		i = EXTRACT_16BITS(cp);
		cp += 2;
	}
	ND_PRINT((ndo, " %s%d", str, i));
	return (cp);
}

static const u_char *
print_sl_winchange(netdissect_options *ndo,
                   register const u_char *cp)
{
	register short i;

	if ((i = *cp++) == 0) {
		i = EXTRACT_16BITS(cp);
		cp += 2;
	}
	if (i >= 0)
		ND_PRINT((ndo, " W+%d", i));
	else
		ND_PRINT((ndo, " W%d", i));
	return (cp);
}

static void
compressed_sl_print(netdissect_options *ndo,
                    const u_char *chdr, const struct ip *ip,
                    u_int length, int dir)
{
	register const u_char *cp = chdr;
	register u_int flags, hlen;

	flags = *cp++;
	if (flags & NEW_C) {
		lastconn = *cp++;
		ND_PRINT((ndo, "ctcp %d", lastconn));
	} else
		ND_PRINT((ndo, "ctcp *"));

	/* skip tcp checksum */
	cp += 2;

	switch (flags & SPECIALS_MASK) {
	case SPECIAL_I:
		ND_PRINT((ndo, " *SA+%d", lastlen[dir][lastconn]));
		break;

	case SPECIAL_D:
		ND_PRINT((ndo, " *S+%d", lastlen[dir][lastconn]));
		break;

	default:
		if (flags & NEW_U)
			cp = print_sl_change(ndo, "U=", cp);
		if (flags & NEW_W)
			cp = print_sl_winchange(ndo, cp);
		if (flags & NEW_A)
			cp = print_sl_change(ndo, "A+", cp);
		if (flags & NEW_S)
			cp = print_sl_change(ndo, "S+", cp);
		break;
	}
	if (flags & NEW_I)
		cp = print_sl_change(ndo, "I+", cp);

	/*
	 * 'hlen' is the length of the uncompressed TCP/IP header (in words).
	 * 'cp - chdr' is the length of the compressed header.
	 * 'length - hlen' is the amount of data in the packet.
	 */
	hlen = IP_HL(ip);
	hlen += TH_OFF((const struct tcphdr *)&((const int32_t *)ip)[hlen]);
	lastlen[dir][lastconn] = length - (hlen << 2);
	ND_PRINT((ndo, " %d (%ld)", lastlen[dir][lastconn], (long)(cp - chdr)));
}
