/*
 * Copyright (c) 1990, 1991, 1993, 1994, 1995, 1996
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

/* \summary: OpenBSD packet filter log file printer */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifndef HAVE_NET_PFVAR_H
#error "No pf headers available"
#endif
#include <sys/types.h>
#include <sys/socket.h>
#include <net/if.h>
#include <net/pfvar.h>
#include <net/if_pflog.h>

#include <netdissect-stdinc.h>

#include "netdissect.h"
#include "extract.h"

static const char tstr[] = "[|pflog]";

static const struct tok pf_reasons[] = {
	{ 0,	"0(match)" },
	{ 1,	"1(bad-offset)" },
	{ 2,	"2(fragment)" },
	{ 3,	"3(short)" },
	{ 4,	"4(normalize)" },
	{ 5,	"5(memory)" },
	{ 6,	"6(bad-timestamp)" },
	{ 7,	"7(congestion)" },
	{ 8,	"8(ip-option)" },
	{ 9,	"9(proto-cksum)" },
	{ 10,	"10(state-mismatch)" },
	{ 11,	"11(state-insert)" },
	{ 12,	"12(state-limit)" },
	{ 13,	"13(src-limit)" },
	{ 14,	"14(synproxy)" },
	{ 0,	NULL }
};

static const struct tok pf_actions[] = {
	{ PF_PASS,		"pass" },
	{ PF_DROP,		"block" },
	{ PF_SCRUB,		"scrub" },
	{ PF_NAT,		"nat" },
	{ PF_NONAT,		"nat" },
	{ PF_BINAT,		"binat" },
	{ PF_NOBINAT,		"binat" },
	{ PF_RDR,		"rdr" },
	{ PF_NORDR,		"rdr" },
	{ PF_SYNPROXY_DROP,	"synproxy-drop" },
	{ 0,			NULL }
};

static const struct tok pf_directions[] = {
	{ PF_INOUT,	"in/out" },
	{ PF_IN,	"in" },
	{ PF_OUT,	"out" },
	{ 0,		NULL }
};

/* For reading capture files on other systems */
#define	OPENBSD_AF_INET		2
#define	OPENBSD_AF_INET6	24

static void
pflog_print(netdissect_options *ndo, const struct pfloghdr *hdr)
{
	uint32_t rulenr, subrulenr;

	rulenr = EXTRACT_32BITS(&hdr->rulenr);
	subrulenr = EXTRACT_32BITS(&hdr->subrulenr);
	if (subrulenr == (uint32_t)-1)
		ND_PRINT((ndo, "rule %u/", rulenr));
	else
		ND_PRINT((ndo, "rule %u.%s.%u/", rulenr, hdr->ruleset, subrulenr));

	ND_PRINT((ndo, "%s", tok2str(pf_reasons, "unkn(%u)", hdr->reason)));

	if (hdr->uid != UID_MAX)
		ND_PRINT((ndo, " [uid %u]", (unsigned)hdr->uid));

	ND_PRINT((ndo, ": %s %s on %s: ",
	    tok2str(pf_actions, "unkn(%u)", hdr->action),
	    tok2str(pf_directions, "unkn(%u)", hdr->dir),
	    hdr->ifname));
}

u_int
pflog_if_print(netdissect_options *ndo, const struct pcap_pkthdr *h,
               register const u_char *p)
{
	u_int length = h->len;
	u_int hdrlen;
	u_int caplen = h->caplen;
	const struct pfloghdr *hdr;
	uint8_t af;

	/* check length */
	if (caplen < sizeof(uint8_t)) {
		ND_PRINT((ndo, "%s", tstr));
		return (caplen);
	}

#define MIN_PFLOG_HDRLEN	45
	hdr = (const struct pfloghdr *)p;
	if (hdr->length < MIN_PFLOG_HDRLEN) {
		ND_PRINT((ndo, "[pflog: invalid header length!]"));
		return (hdr->length);	/* XXX: not really */
	}
	hdrlen = BPF_WORDALIGN(hdr->length);

	if (caplen < hdrlen) {
		ND_PRINT((ndo, "%s", tstr));
		return (hdrlen);	/* XXX: true? */
	}

	/* print what we know */
	ND_TCHECK(*hdr);
	if (ndo->ndo_eflag)
		pflog_print(ndo, hdr);

	/* skip to the real packet */
	af = hdr->af;
	length -= hdrlen;
	caplen -= hdrlen;
	p += hdrlen;
	switch (af) {

		case AF_INET:
#if OPENBSD_AF_INET != AF_INET
		case OPENBSD_AF_INET:		/* XXX: read pcap files */
#endif
		        ip_print(ndo, p, length);
			break;

#if defined(AF_INET6) || defined(OPENBSD_AF_INET6)
#ifdef AF_INET6
		case AF_INET6:
#endif /* AF_INET6 */
#if !defined(AF_INET6) || OPENBSD_AF_INET6 != AF_INET6
		case OPENBSD_AF_INET6:		/* XXX: read pcap files */
#endif /* !defined(AF_INET6) || OPENBSD_AF_INET6 != AF_INET6 */
			ip6_print(ndo, p, length);
			break;
#endif /* defined(AF_INET6) || defined(OPENBSD_AF_INET6) */

	default:
		/* address family not handled, print raw packet */
		if (!ndo->ndo_eflag)
			pflog_print(ndo, hdr);
		if (!ndo->ndo_suppress_default_print)
			ND_DEFAULTPRINT(p, caplen);
	}

	return (hdrlen);
trunc:
	ND_PRINT((ndo, "%s", tstr));
	return (hdrlen);
}

/*
 * Local Variables:
 * c-style: whitesmith
 * c-basic-offset: 8
 * End:
 */
