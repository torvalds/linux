/*	$OpenBSD: print-gre.c,v 1.6 2002/10/30 03:04:04 fgsch Exp $	*/

/*
 * Copyright (c) 2002 Jason L. Wright (jason@thought.net)
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by Jason L. Wright
 * 4. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/* \summary: Generic Routing Encapsulation (GRE) printer */

/*
 * netdissect printer for GRE - Generic Routing Encapsulation
 * RFC1701 (GRE), RFC1702 (GRE IPv4), and RFC2637 (Enhanced GRE)
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <netdissect-stdinc.h>

#include <string.h>

#include "netdissect.h"
#include "addrtostr.h"
#include "extract.h"
#include "ethertype.h"

static const char tstr[] = "[|gre]";

#define	GRE_CP		0x8000		/* checksum present */
#define	GRE_RP		0x4000		/* routing present */
#define	GRE_KP		0x2000		/* key present */
#define	GRE_SP		0x1000		/* sequence# present */
#define	GRE_sP		0x0800		/* source routing */
#define	GRE_RECRS	0x0700		/* recursion count */
#define	GRE_AP		0x0080		/* acknowledgment# present */

static const struct tok gre_flag_values[] = {
    { GRE_CP, "checksum present"},
    { GRE_RP, "routing present"},
    { GRE_KP, "key present"},
    { GRE_SP, "sequence# present"},
    { GRE_sP, "source routing present"},
    { GRE_RECRS, "recursion count"},
    { GRE_AP, "ack present"},
    { 0, NULL }
};

#define	GRE_VERS_MASK	0x0007		/* protocol version */

/* source route entry types */
#define	GRESRE_IP	0x0800		/* IP */
#define	GRESRE_ASN	0xfffe		/* ASN */

static void gre_print_0(netdissect_options *, const u_char *, u_int);
static void gre_print_1(netdissect_options *, const u_char *, u_int);
static int gre_sre_print(netdissect_options *, uint16_t, uint8_t, uint8_t, const u_char *, u_int);
static int gre_sre_ip_print(netdissect_options *, uint8_t, uint8_t, const u_char *, u_int);
static int gre_sre_asn_print(netdissect_options *, uint8_t, uint8_t, const u_char *, u_int);

void
gre_print(netdissect_options *ndo, const u_char *bp, u_int length)
{
	u_int len = length, vers;

	ND_TCHECK2(*bp, 2);
	if (len < 2)
		goto trunc;
	vers = EXTRACT_16BITS(bp) & GRE_VERS_MASK;
        ND_PRINT((ndo, "GREv%u",vers));

        switch(vers) {
        case 0:
            gre_print_0(ndo, bp, len);
            break;
        case 1:
            gre_print_1(ndo, bp, len);
            break;
	default:
            ND_PRINT((ndo, " ERROR: unknown-version"));
            break;
        }
        return;

trunc:
	ND_PRINT((ndo, "%s", tstr));
	return;
}

static void
gre_print_0(netdissect_options *ndo, const u_char *bp, u_int length)
{
	u_int len = length;
	uint16_t flags, prot;

	flags = EXTRACT_16BITS(bp);
        if (ndo->ndo_vflag)
            ND_PRINT((ndo, ", Flags [%s]",
                   bittok2str(gre_flag_values,"none",flags)));

	len -= 2;
	bp += 2;

	ND_TCHECK2(*bp, 2);
	if (len < 2)
		goto trunc;
	prot = EXTRACT_16BITS(bp);
	len -= 2;
	bp += 2;

	if ((flags & GRE_CP) | (flags & GRE_RP)) {
		ND_TCHECK2(*bp, 2);
		if (len < 2)
			goto trunc;
		if (ndo->ndo_vflag)
			ND_PRINT((ndo, ", sum 0x%x", EXTRACT_16BITS(bp)));
		bp += 2;
		len -= 2;

		ND_TCHECK2(*bp, 2);
		if (len < 2)
			goto trunc;
		ND_PRINT((ndo, ", off 0x%x", EXTRACT_16BITS(bp)));
		bp += 2;
		len -= 2;
	}

	if (flags & GRE_KP) {
		ND_TCHECK2(*bp, 4);
		if (len < 4)
			goto trunc;
		ND_PRINT((ndo, ", key=0x%x", EXTRACT_32BITS(bp)));
		bp += 4;
		len -= 4;
	}

	if (flags & GRE_SP) {
		ND_TCHECK2(*bp, 4);
		if (len < 4)
			goto trunc;
		ND_PRINT((ndo, ", seq %u", EXTRACT_32BITS(bp)));
		bp += 4;
		len -= 4;
	}

	if (flags & GRE_RP) {
		for (;;) {
			uint16_t af;
			uint8_t sreoff;
			uint8_t srelen;

			ND_TCHECK2(*bp, 4);
			if (len < 4)
				goto trunc;
			af = EXTRACT_16BITS(bp);
			sreoff = *(bp + 2);
			srelen = *(bp + 3);
			bp += 4;
			len -= 4;

			if (af == 0 && srelen == 0)
				break;

			if (!gre_sre_print(ndo, af, sreoff, srelen, bp, len))
				goto trunc;

			if (len < srelen)
				goto trunc;
			bp += srelen;
			len -= srelen;
		}
	}

        if (ndo->ndo_eflag)
            ND_PRINT((ndo, ", proto %s (0x%04x)",
                   tok2str(ethertype_values,"unknown",prot),
                   prot));

        ND_PRINT((ndo, ", length %u",length));

        if (ndo->ndo_vflag < 1)
            ND_PRINT((ndo, ": ")); /* put in a colon as protocol demarc */
        else
            ND_PRINT((ndo, "\n\t")); /* if verbose go multiline */

	switch (prot) {
	case ETHERTYPE_IP:
	        ip_print(ndo, bp, len);
		break;
	case ETHERTYPE_IPV6:
		ip6_print(ndo, bp, len);
		break;
	case ETHERTYPE_MPLS:
		mpls_print(ndo, bp, len);
		break;
	case ETHERTYPE_IPX:
		ipx_print(ndo, bp, len);
		break;
	case ETHERTYPE_ATALK:
		atalk_print(ndo, bp, len);
		break;
	case ETHERTYPE_GRE_ISO:
		isoclns_print(ndo, bp, len);
		break;
	case ETHERTYPE_TEB:
		ether_print(ndo, bp, len, ndo->ndo_snapend - bp, NULL, NULL);
		break;
	default:
		ND_PRINT((ndo, "gre-proto-0x%x", prot));
	}
	return;

trunc:
	ND_PRINT((ndo, "%s", tstr));
}

static void
gre_print_1(netdissect_options *ndo, const u_char *bp, u_int length)
{
	u_int len = length;
	uint16_t flags, prot;

	flags = EXTRACT_16BITS(bp);
	len -= 2;
	bp += 2;

	if (ndo->ndo_vflag)
            ND_PRINT((ndo, ", Flags [%s]",
                   bittok2str(gre_flag_values,"none",flags)));

	ND_TCHECK2(*bp, 2);
	if (len < 2)
		goto trunc;
	prot = EXTRACT_16BITS(bp);
	len -= 2;
	bp += 2;


	if (flags & GRE_KP) {
		uint32_t k;

		ND_TCHECK2(*bp, 4);
		if (len < 4)
			goto trunc;
		k = EXTRACT_32BITS(bp);
		ND_PRINT((ndo, ", call %d", k & 0xffff));
		len -= 4;
		bp += 4;
	}

	if (flags & GRE_SP) {
		ND_TCHECK2(*bp, 4);
		if (len < 4)
			goto trunc;
		ND_PRINT((ndo, ", seq %u", EXTRACT_32BITS(bp)));
		bp += 4;
		len -= 4;
	}

	if (flags & GRE_AP) {
		ND_TCHECK2(*bp, 4);
		if (len < 4)
			goto trunc;
		ND_PRINT((ndo, ", ack %u", EXTRACT_32BITS(bp)));
		bp += 4;
		len -= 4;
	}

	if ((flags & GRE_SP) == 0)
		ND_PRINT((ndo, ", no-payload"));

        if (ndo->ndo_eflag)
            ND_PRINT((ndo, ", proto %s (0x%04x)",
                   tok2str(ethertype_values,"unknown",prot),
                   prot));

        ND_PRINT((ndo, ", length %u",length));

        if ((flags & GRE_SP) == 0)
            return;

        if (ndo->ndo_vflag < 1)
            ND_PRINT((ndo, ": ")); /* put in a colon as protocol demarc */
        else
            ND_PRINT((ndo, "\n\t")); /* if verbose go multiline */

	switch (prot) {
	case ETHERTYPE_PPP:
		ppp_print(ndo, bp, len);
		break;
	default:
		ND_PRINT((ndo, "gre-proto-0x%x", prot));
		break;
	}
	return;

trunc:
	ND_PRINT((ndo, "%s", tstr));
}

static int
gre_sre_print(netdissect_options *ndo, uint16_t af, uint8_t sreoff,
    uint8_t srelen, const u_char *bp, u_int len)
{
	int ret;

	switch (af) {
	case GRESRE_IP:
		ND_PRINT((ndo, ", (rtaf=ip"));
		ret = gre_sre_ip_print(ndo, sreoff, srelen, bp, len);
		ND_PRINT((ndo, ")"));
		break;
	case GRESRE_ASN:
		ND_PRINT((ndo, ", (rtaf=asn"));
		ret = gre_sre_asn_print(ndo, sreoff, srelen, bp, len);
		ND_PRINT((ndo, ")"));
		break;
	default:
		ND_PRINT((ndo, ", (rtaf=0x%x)", af));
		ret = 1;
	}
	return (ret);
}

static int
gre_sre_ip_print(netdissect_options *ndo, uint8_t sreoff, uint8_t srelen,
                 const u_char *bp, u_int len)
{
	const u_char *up = bp;
	char buf[INET_ADDRSTRLEN];

	if (sreoff & 3) {
		ND_PRINT((ndo, ", badoffset=%u", sreoff));
		return (1);
	}
	if (srelen & 3) {
		ND_PRINT((ndo, ", badlength=%u", srelen));
		return (1);
	}
	if (sreoff >= srelen) {
		ND_PRINT((ndo, ", badoff/len=%u/%u", sreoff, srelen));
		return (1);
	}

	while (srelen != 0) {
		if (!ND_TTEST2(*bp, 4))
			return (0);
		if (len < 4)
			return (0);

		addrtostr(bp, buf, sizeof(buf));
		ND_PRINT((ndo, " %s%s",
		    ((bp - up) == sreoff) ? "*" : "", buf));

		bp += 4;
		len -= 4;
		srelen -= 4;
	}
	return (1);
}

static int
gre_sre_asn_print(netdissect_options *ndo, uint8_t sreoff, uint8_t srelen,
                  const u_char *bp, u_int len)
{
	const u_char *up = bp;

	if (sreoff & 1) {
		ND_PRINT((ndo, ", badoffset=%u", sreoff));
		return (1);
	}
	if (srelen & 1) {
		ND_PRINT((ndo, ", badlength=%u", srelen));
		return (1);
	}
	if (sreoff >= srelen) {
		ND_PRINT((ndo, ", badoff/len=%u/%u", sreoff, srelen));
		return (1);
	}

	while (srelen != 0) {
		if (!ND_TTEST2(*bp, 2))
			return (0);
		if (len < 2)
			return (0);

		ND_PRINT((ndo, " %s%x",
		    ((bp - up) == sreoff) ? "*" : "",
		    EXTRACT_16BITS(bp)));

		bp += 2;
		len -= 2;
		srelen -= 2;
	}
	return (1);
}
