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
 */

/* \summary: AppleTalk printer */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <netdissect-stdinc.h>

#include <stdio.h>
#include <string.h>

#include "netdissect.h"
#include "addrtoname.h"
#include "ethertype.h"
#include "extract.h"
#include "appletalk.h"

static const char tstr[] = "[|atalk]";

static const struct tok type2str[] = {
	{ ddpRTMP,		"rtmp" },
	{ ddpRTMPrequest,	"rtmpReq" },
	{ ddpECHO,		"echo" },
	{ ddpIP,		"IP" },
	{ ddpARP,		"ARP" },
	{ ddpKLAP,		"KLAP" },
	{ 0,			NULL }
};

struct aarp {
	uint16_t	htype, ptype;
	uint8_t		halen, palen;
	uint16_t	op;
	uint8_t		hsaddr[6];
	uint8_t		psaddr[4];
	uint8_t		hdaddr[6];
	uint8_t		pdaddr[4];
};

static void atp_print(netdissect_options *, const struct atATP *, u_int);
static void atp_bitmap_print(netdissect_options *, u_char);
static void nbp_print(netdissect_options *, const struct atNBP *, u_int, u_short, u_char, u_char);
static const struct atNBPtuple *nbp_tuple_print(netdissect_options *ndo, const struct atNBPtuple *,
						const u_char *,
						u_short, u_char, u_char);
static const struct atNBPtuple *nbp_name_print(netdissect_options *, const struct atNBPtuple *,
					       const u_char *);
static const char *ataddr_string(netdissect_options *, u_short, u_char);
static void ddp_print(netdissect_options *, const u_char *, u_int, int, u_short, u_char, u_char);
static const char *ddpskt_string(netdissect_options *, int);

/*
 * Print LLAP packets received on a physical LocalTalk interface.
 */
u_int
ltalk_if_print(netdissect_options *ndo,
               const struct pcap_pkthdr *h, const u_char *p)
{
	u_int hdrlen;

	hdrlen = llap_print(ndo, p, h->len);
	if (hdrlen == 0) {
		/* Cut short by the snapshot length. */
		return (h->caplen);
	}
	return (hdrlen);
}

/*
 * Print AppleTalk LLAP packets.
 */
u_int
llap_print(netdissect_options *ndo,
           register const u_char *bp, u_int length)
{
	register const struct LAP *lp;
	register const struct atDDP *dp;
	register const struct atShortDDP *sdp;
	u_short snet;
	u_int hdrlen;

	if (length < sizeof(*lp)) {
		ND_PRINT((ndo, " [|llap %u]", length));
		return (length);
	}
	if (!ND_TTEST2(*bp, sizeof(*lp))) {
		ND_PRINT((ndo, " [|llap]"));
		return (0);	/* cut short by the snapshot length */
	}
	lp = (const struct LAP *)bp;
	bp += sizeof(*lp);
	length -= sizeof(*lp);
	hdrlen = sizeof(*lp);
	switch (lp->type) {

	case lapShortDDP:
		if (length < ddpSSize) {
			ND_PRINT((ndo, " [|sddp %u]", length));
			return (length);
		}
		if (!ND_TTEST2(*bp, ddpSSize)) {
			ND_PRINT((ndo, " [|sddp]"));
			return (0);	/* cut short by the snapshot length */
		}
		sdp = (const struct atShortDDP *)bp;
		ND_PRINT((ndo, "%s.%s",
		    ataddr_string(ndo, 0, lp->src), ddpskt_string(ndo, sdp->srcSkt)));
		ND_PRINT((ndo, " > %s.%s:",
		    ataddr_string(ndo, 0, lp->dst), ddpskt_string(ndo, sdp->dstSkt)));
		bp += ddpSSize;
		length -= ddpSSize;
		hdrlen += ddpSSize;
		ddp_print(ndo, bp, length, sdp->type, 0, lp->src, sdp->srcSkt);
		break;

	case lapDDP:
		if (length < ddpSize) {
			ND_PRINT((ndo, " [|ddp %u]", length));
			return (length);
		}
		if (!ND_TTEST2(*bp, ddpSize)) {
			ND_PRINT((ndo, " [|ddp]"));
			return (0);	/* cut short by the snapshot length */
		}
		dp = (const struct atDDP *)bp;
		snet = EXTRACT_16BITS(&dp->srcNet);
		ND_PRINT((ndo, "%s.%s", ataddr_string(ndo, snet, dp->srcNode),
		    ddpskt_string(ndo, dp->srcSkt)));
		ND_PRINT((ndo, " > %s.%s:",
		    ataddr_string(ndo, EXTRACT_16BITS(&dp->dstNet), dp->dstNode),
		    ddpskt_string(ndo, dp->dstSkt)));
		bp += ddpSize;
		length -= ddpSize;
		hdrlen += ddpSize;
		ddp_print(ndo, bp, length, dp->type, snet, dp->srcNode, dp->srcSkt);
		break;

#ifdef notdef
	case lapKLAP:
		klap_print(bp, length);
		break;
#endif

	default:
		ND_PRINT((ndo, "%d > %d at-lap#%d %u",
		    lp->src, lp->dst, lp->type, length));
		break;
	}
	return (hdrlen);
}

/*
 * Print EtherTalk/TokenTalk packets (or FDDITalk, or whatever it's called
 * when it runs over FDDI; yes, I've seen FDDI captures with AppleTalk
 * packets in them).
 */
void
atalk_print(netdissect_options *ndo,
            register const u_char *bp, u_int length)
{
	register const struct atDDP *dp;
	u_short snet;

        if(!ndo->ndo_eflag)
            ND_PRINT((ndo, "AT "));

	if (length < ddpSize) {
		ND_PRINT((ndo, " [|ddp %u]", length));
		return;
	}
	if (!ND_TTEST2(*bp, ddpSize)) {
		ND_PRINT((ndo, " [|ddp]"));
		return;
	}
	dp = (const struct atDDP *)bp;
	snet = EXTRACT_16BITS(&dp->srcNet);
	ND_PRINT((ndo, "%s.%s", ataddr_string(ndo, snet, dp->srcNode),
	       ddpskt_string(ndo, dp->srcSkt)));
	ND_PRINT((ndo, " > %s.%s: ",
	       ataddr_string(ndo, EXTRACT_16BITS(&dp->dstNet), dp->dstNode),
	       ddpskt_string(ndo, dp->dstSkt)));
	bp += ddpSize;
	length -= ddpSize;
	ddp_print(ndo, bp, length, dp->type, snet, dp->srcNode, dp->srcSkt);
}

/* XXX should probably pass in the snap header and do checks like arp_print() */
void
aarp_print(netdissect_options *ndo,
           register const u_char *bp, u_int length)
{
	register const struct aarp *ap;

#define AT(member) ataddr_string(ndo, (ap->member[1]<<8)|ap->member[2],ap->member[3])

	ND_PRINT((ndo, "aarp "));
	ap = (const struct aarp *)bp;
	if (!ND_TTEST(*ap)) {
		/* Just bail if we don't have the whole chunk. */
		ND_PRINT((ndo, " [|aarp]"));
		return;
	}
	if (length < sizeof(*ap)) {
		ND_PRINT((ndo, " [|aarp %u]", length));
		return;
	}
	if (EXTRACT_16BITS(&ap->htype) == 1 &&
	    EXTRACT_16BITS(&ap->ptype) == ETHERTYPE_ATALK &&
	    ap->halen == 6 && ap->palen == 4 )
		switch (EXTRACT_16BITS(&ap->op)) {

		case 1:				/* request */
			ND_PRINT((ndo, "who-has %s tell %s", AT(pdaddr), AT(psaddr)));
			return;

		case 2:				/* response */
			ND_PRINT((ndo, "reply %s is-at %s", AT(psaddr), etheraddr_string(ndo, ap->hsaddr)));
			return;

		case 3:				/* probe (oy!) */
			ND_PRINT((ndo, "probe %s tell %s", AT(pdaddr), AT(psaddr)));
			return;
		}
	ND_PRINT((ndo, "len %u op %u htype %u ptype %#x halen %u palen %u",
	    length, EXTRACT_16BITS(&ap->op), EXTRACT_16BITS(&ap->htype),
	    EXTRACT_16BITS(&ap->ptype), ap->halen, ap->palen));
}

/*
 * Print AppleTalk Datagram Delivery Protocol packets.
 */
static void
ddp_print(netdissect_options *ndo,
          register const u_char *bp, register u_int length, register int t,
          register u_short snet, register u_char snode, u_char skt)
{

	switch (t) {

	case ddpNBP:
		nbp_print(ndo, (const struct atNBP *)bp, length, snet, snode, skt);
		break;

	case ddpATP:
		atp_print(ndo, (const struct atATP *)bp, length);
		break;

	case ddpEIGRP:
		eigrp_print(ndo, bp, length);
		break;

	default:
		ND_PRINT((ndo, " at-%s %d", tok2str(type2str, NULL, t), length));
		break;
	}
}

static void
atp_print(netdissect_options *ndo,
          register const struct atATP *ap, u_int length)
{
	char c;
	uint32_t data;

	if ((const u_char *)(ap + 1) > ndo->ndo_snapend) {
		/* Just bail if we don't have the whole chunk. */
		ND_PRINT((ndo, "%s", tstr));
		return;
	}
	if (length < sizeof(*ap)) {
		ND_PRINT((ndo, " [|atp %u]", length));
		return;
	}
	length -= sizeof(*ap);
	switch (ap->control & 0xc0) {

	case atpReqCode:
		ND_PRINT((ndo, " atp-req%s %d",
			     ap->control & atpXO? " " : "*",
			     EXTRACT_16BITS(&ap->transID)));

		atp_bitmap_print(ndo, ap->bitmap);

		if (length != 0)
			ND_PRINT((ndo, " [len=%u]", length));

		switch (ap->control & (atpEOM|atpSTS)) {
		case atpEOM:
			ND_PRINT((ndo, " [EOM]"));
			break;
		case atpSTS:
			ND_PRINT((ndo, " [STS]"));
			break;
		case atpEOM|atpSTS:
			ND_PRINT((ndo, " [EOM,STS]"));
			break;
		}
		break;

	case atpRspCode:
		ND_PRINT((ndo, " atp-resp%s%d:%d (%u)",
			     ap->control & atpEOM? "*" : " ",
			     EXTRACT_16BITS(&ap->transID), ap->bitmap, length));
		switch (ap->control & (atpXO|atpSTS)) {
		case atpXO:
			ND_PRINT((ndo, " [XO]"));
			break;
		case atpSTS:
			ND_PRINT((ndo, " [STS]"));
			break;
		case atpXO|atpSTS:
			ND_PRINT((ndo, " [XO,STS]"));
			break;
		}
		break;

	case atpRelCode:
		ND_PRINT((ndo, " atp-rel  %d", EXTRACT_16BITS(&ap->transID)));

		atp_bitmap_print(ndo, ap->bitmap);

		/* length should be zero */
		if (length)
			ND_PRINT((ndo, " [len=%u]", length));

		/* there shouldn't be any control flags */
		if (ap->control & (atpXO|atpEOM|atpSTS)) {
			c = '[';
			if (ap->control & atpXO) {
				ND_PRINT((ndo, "%cXO", c));
				c = ',';
			}
			if (ap->control & atpEOM) {
				ND_PRINT((ndo, "%cEOM", c));
				c = ',';
			}
			if (ap->control & atpSTS) {
				ND_PRINT((ndo, "%cSTS", c));
				c = ',';
			}
			ND_PRINT((ndo, "]"));
		}
		break;

	default:
		ND_PRINT((ndo, " atp-0x%x  %d (%u)", ap->control,
			     EXTRACT_16BITS(&ap->transID), length));
		break;
	}
	data = EXTRACT_32BITS(&ap->userData);
	if (data != 0)
		ND_PRINT((ndo, " 0x%x", data));
}

static void
atp_bitmap_print(netdissect_options *ndo,
                 register u_char bm)
{
	register char c;
	register int i;

	/*
	 * The '& 0xff' below is needed for compilers that want to sign
	 * extend a u_char, which is the case with the Ultrix compiler.
	 * (gcc is smart enough to eliminate it, at least on the Sparc).
	 */
	if ((bm + 1) & (bm & 0xff)) {
		c = '<';
		for (i = 0; bm; ++i) {
			if (bm & 1) {
				ND_PRINT((ndo, "%c%d", c, i));
				c = ',';
			}
			bm >>= 1;
		}
		ND_PRINT((ndo, ">"));
	} else {
		for (i = 0; bm; ++i)
			bm >>= 1;
		if (i > 1)
			ND_PRINT((ndo, "<0-%d>", i - 1));
		else
			ND_PRINT((ndo, "<0>"));
	}
}

static void
nbp_print(netdissect_options *ndo,
          register const struct atNBP *np, u_int length, register u_short snet,
          register u_char snode, register u_char skt)
{
	register const struct atNBPtuple *tp =
		(const struct atNBPtuple *)((const u_char *)np + nbpHeaderSize);
	int i;
	const u_char *ep;

	if (length < nbpHeaderSize) {
		ND_PRINT((ndo, " truncated-nbp %u", length));
		return;
	}

	length -= nbpHeaderSize;
	if (length < 8) {
		/* must be room for at least one tuple */
		ND_PRINT((ndo, " truncated-nbp %u", length + nbpHeaderSize));
		return;
	}
	/* ep points to end of available data */
	ep = ndo->ndo_snapend;
	if ((const u_char *)tp > ep) {
		ND_PRINT((ndo, "%s", tstr));
		return;
	}
	switch (i = np->control & 0xf0) {

	case nbpBrRq:
	case nbpLkUp:
		ND_PRINT((ndo, i == nbpLkUp? " nbp-lkup %d:":" nbp-brRq %d:", np->id));
		if ((const u_char *)(tp + 1) > ep) {
			ND_PRINT((ndo, "%s", tstr));
			return;
		}
		(void)nbp_name_print(ndo, tp, ep);
		/*
		 * look for anomalies: the spec says there can only
		 * be one tuple, the address must match the source
		 * address and the enumerator should be zero.
		 */
		if ((np->control & 0xf) != 1)
			ND_PRINT((ndo, " [ntup=%d]", np->control & 0xf));
		if (tp->enumerator)
			ND_PRINT((ndo, " [enum=%d]", tp->enumerator));
		if (EXTRACT_16BITS(&tp->net) != snet ||
		    tp->node != snode || tp->skt != skt)
			ND_PRINT((ndo, " [addr=%s.%d]",
			    ataddr_string(ndo, EXTRACT_16BITS(&tp->net),
			    tp->node), tp->skt));
		break;

	case nbpLkUpReply:
		ND_PRINT((ndo, " nbp-reply %d:", np->id));

		/* print each of the tuples in the reply */
		for (i = np->control & 0xf; --i >= 0 && tp; )
			tp = nbp_tuple_print(ndo, tp, ep, snet, snode, skt);
		break;

	default:
		ND_PRINT((ndo, " nbp-0x%x  %d (%u)", np->control, np->id, length));
		break;
	}
}

/* print a counted string */
static const char *
print_cstring(netdissect_options *ndo,
              register const char *cp, register const u_char *ep)
{
	register u_int length;

	if (cp >= (const char *)ep) {
		ND_PRINT((ndo, "%s", tstr));
		return (0);
	}
	length = *cp++;

	/* Spec says string can be at most 32 bytes long */
	if (length > 32) {
		ND_PRINT((ndo, "[len=%u]", length));
		return (0);
	}
	while ((int)--length >= 0) {
		if (cp >= (const char *)ep) {
			ND_PRINT((ndo, "%s", tstr));
			return (0);
		}
		ND_PRINT((ndo, "%c", *cp++));
	}
	return (cp);
}

static const struct atNBPtuple *
nbp_tuple_print(netdissect_options *ndo,
                register const struct atNBPtuple *tp, register const u_char *ep,
                register u_short snet, register u_char snode, register u_char skt)
{
	register const struct atNBPtuple *tpn;

	if ((const u_char *)(tp + 1) > ep) {
		ND_PRINT((ndo, "%s", tstr));
		return 0;
	}
	tpn = nbp_name_print(ndo, tp, ep);

	/* if the enumerator isn't 1, print it */
	if (tp->enumerator != 1)
		ND_PRINT((ndo, "(%d)", tp->enumerator));

	/* if the socket doesn't match the src socket, print it */
	if (tp->skt != skt)
		ND_PRINT((ndo, " %d", tp->skt));

	/* if the address doesn't match the src address, it's an anomaly */
	if (EXTRACT_16BITS(&tp->net) != snet || tp->node != snode)
		ND_PRINT((ndo, " [addr=%s]",
		    ataddr_string(ndo, EXTRACT_16BITS(&tp->net), tp->node)));

	return (tpn);
}

static const struct atNBPtuple *
nbp_name_print(netdissect_options *ndo,
               const struct atNBPtuple *tp, register const u_char *ep)
{
	register const char *cp = (const char *)tp + nbpTupleSize;

	ND_PRINT((ndo, " "));

	/* Object */
	ND_PRINT((ndo, "\""));
	if ((cp = print_cstring(ndo, cp, ep)) != NULL) {
		/* Type */
		ND_PRINT((ndo, ":"));
		if ((cp = print_cstring(ndo, cp, ep)) != NULL) {
			/* Zone */
			ND_PRINT((ndo, "@"));
			if ((cp = print_cstring(ndo, cp, ep)) != NULL)
				ND_PRINT((ndo, "\""));
		}
	}
	return ((const struct atNBPtuple *)cp);
}


#define HASHNAMESIZE 4096

struct hnamemem {
	int addr;
	char *name;
	struct hnamemem *nxt;
};

static struct hnamemem hnametable[HASHNAMESIZE];

static const char *
ataddr_string(netdissect_options *ndo,
              u_short atnet, u_char athost)
{
	register struct hnamemem *tp, *tp2;
	register int i = (atnet << 8) | athost;
	char nambuf[256+1];
	static int first = 1;
	FILE *fp;

	/*
	 * if this is the first call, see if there's an AppleTalk
	 * number to name map file.
	 */
	if (first && (first = 0, !ndo->ndo_nflag)
	    && (fp = fopen("/etc/atalk.names", "r"))) {
		char line[256];
		int i1, i2;

		while (fgets(line, sizeof(line), fp)) {
			if (line[0] == '\n' || line[0] == 0 || line[0] == '#')
				continue;
			if (sscanf(line, "%d.%d %256s", &i1, &i2, nambuf) == 3)
				/* got a hostname. */
				i2 |= (i1 << 8);
			else if (sscanf(line, "%d %256s", &i1, nambuf) == 2)
				/* got a net name */
				i2 = (i1 << 8) | 255;
			else
				continue;

			for (tp = &hnametable[i2 & (HASHNAMESIZE-1)];
			     tp->nxt; tp = tp->nxt)
				;
			tp->addr = i2;
			tp->nxt = newhnamemem(ndo);
			tp->name = strdup(nambuf);
			if (tp->name == NULL)
				(*ndo->ndo_error)(ndo,
						  "ataddr_string: strdup(nambuf)");
		}
		fclose(fp);
	}

	for (tp = &hnametable[i & (HASHNAMESIZE-1)]; tp->nxt; tp = tp->nxt)
		if (tp->addr == i)
			return (tp->name);

	/* didn't have the node name -- see if we've got the net name */
	i |= 255;
	for (tp2 = &hnametable[i & (HASHNAMESIZE-1)]; tp2->nxt; tp2 = tp2->nxt)
		if (tp2->addr == i) {
			tp->addr = (atnet << 8) | athost;
			tp->nxt = newhnamemem(ndo);
			(void)snprintf(nambuf, sizeof(nambuf), "%s.%d",
			    tp2->name, athost);
			tp->name = strdup(nambuf);
			if (tp->name == NULL)
				(*ndo->ndo_error)(ndo,
						  "ataddr_string: strdup(nambuf)");
			return (tp->name);
		}

	tp->addr = (atnet << 8) | athost;
	tp->nxt = newhnamemem(ndo);
	if (athost != 255)
		(void)snprintf(nambuf, sizeof(nambuf), "%d.%d", atnet, athost);
	else
		(void)snprintf(nambuf, sizeof(nambuf), "%d", atnet);
	tp->name = strdup(nambuf);
	if (tp->name == NULL)
		(*ndo->ndo_error)(ndo, "ataddr_string: strdup(nambuf)");

	return (tp->name);
}

static const struct tok skt2str[] = {
	{ rtmpSkt,	"rtmp" },	/* routing table maintenance */
	{ nbpSkt,	"nis" },	/* name info socket */
	{ echoSkt,	"echo" },	/* AppleTalk echo protocol */
	{ zipSkt,	"zip" },	/* zone info protocol */
	{ 0,		NULL }
};

static const char *
ddpskt_string(netdissect_options *ndo,
              register int skt)
{
	static char buf[8];

	if (ndo->ndo_nflag) {
		(void)snprintf(buf, sizeof(buf), "%d", skt);
		return (buf);
	}
	return (tok2str(skt2str, "%d", skt));
}
