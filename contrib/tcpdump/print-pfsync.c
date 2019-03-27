/*
 * Copyright (c) 2012 Gleb Smirnoff <glebius@FreeBSD.org>
 * Copyright (c) 2002 Michael Shalayeff
 * Copyright (c) 2001 Daniel Hartmeier
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
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR OR HIS RELATIVES BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF MIND, USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $OpenBSD: print-pfsync.c,v 1.38 2012/09/19 13:50:36 mikeb Exp $
 * $OpenBSD: pf_print_state.c,v 1.11 2012/07/08 17:48:37 lteo Exp $
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifndef HAVE_NET_PFVAR_H
#error "No pf headers available"
#endif
#include <sys/endian.h>
#include <net/if.h>
#include <net/pfvar.h>
#include <net/if_pfsync.h>
#define	TCPSTATES
#include <netinet/tcp_fsm.h>

#include <netdissect-stdinc.h>
#include <string.h>

#include "netdissect.h"
#include "interface.h"
#include "addrtoname.h"

static void	pfsync_print(netdissect_options *, struct pfsync_header *,
		    const u_char *, u_int);
static void	print_src_dst(netdissect_options *,
		    const struct pfsync_state_peer *,
		    const struct pfsync_state_peer *, uint8_t);
static void	print_state(netdissect_options *, struct pfsync_state *);

#ifdef notyet
void
pfsync_if_print(u_char *user, const struct pcap_pkthdr *h,
    register const u_char *p)
{
	u_int caplen = h->caplen;

	ts_print(&h->ts);

	if (caplen < PFSYNC_HDRLEN) {
		ND_PRINT((ndo, "[|pfsync]"));
		goto out;
	}

	pfsync_print((struct pfsync_header *)p,
	    p + sizeof(struct pfsync_header),
	    caplen - sizeof(struct pfsync_header));
out:
	if (xflag) {
		default_print((const u_char *)p, caplen);
	}
	safeputchar(ndo, '\n');
}
#endif /* notyet */

void
pfsync_ip_print(netdissect_options *ndo , const u_char *bp, u_int len)
{
	struct pfsync_header *hdr = (struct pfsync_header *)bp;

	if (len < PFSYNC_HDRLEN)
		ND_PRINT((ndo, "[|pfsync]"));
	else
		pfsync_print(ndo, hdr, bp + sizeof(struct pfsync_header),
		    len - sizeof(struct pfsync_header));
}

struct pfsync_actions {
	const char *name;
	size_t len;
	void (*print)(netdissect_options *, const void *);
};

static void	pfsync_print_clr(netdissect_options *, const void *);
static void	pfsync_print_state(netdissect_options *, const void *);
static void	pfsync_print_ins_ack(netdissect_options *, const void *);
static void	pfsync_print_upd_c(netdissect_options *, const void *);
static void	pfsync_print_upd_req(netdissect_options *, const void *);
static void	pfsync_print_del_c(netdissect_options *, const void *);
static void	pfsync_print_bus(netdissect_options *, const void *);
static void	pfsync_print_tdb(netdissect_options *, const void *);

struct pfsync_actions actions[] = {
	{ "clear all", sizeof(struct pfsync_clr),	pfsync_print_clr },
	{ "insert", sizeof(struct pfsync_state),	pfsync_print_state },
	{ "insert ack", sizeof(struct pfsync_ins_ack),	pfsync_print_ins_ack },
	{ "update", sizeof(struct pfsync_ins_ack),	pfsync_print_state },
	{ "update compressed", sizeof(struct pfsync_upd_c),
							pfsync_print_upd_c },
	{ "request uncompressed", sizeof(struct pfsync_upd_req),
							pfsync_print_upd_req },
	{ "delete", sizeof(struct pfsync_state),	pfsync_print_state },
	{ "delete compressed", sizeof(struct pfsync_del_c),
							pfsync_print_del_c },
	{ "frag insert", 0,				NULL },
	{ "frag delete", 0,				NULL },
	{ "bulk update status", sizeof(struct pfsync_bus),
							pfsync_print_bus },
	{ "tdb", 0,					pfsync_print_tdb },
	{ "eof", 0,					NULL },
};

static void
pfsync_print(netdissect_options *ndo, struct pfsync_header *hdr,
    const u_char *bp, u_int len)
{
	struct pfsync_subheader *subh;
	int count, plen, i;
	u_int alen;

	plen = ntohs(hdr->len);

	ND_PRINT((ndo, "PFSYNCv%d len %d", hdr->version, plen));

	if (hdr->version != PFSYNC_VERSION)
		return;

	plen -= sizeof(*hdr);

	while (plen > 0) {
		if (len < sizeof(*subh))
			break;

		subh = (struct pfsync_subheader *)bp;
		bp += sizeof(*subh);
		len -= sizeof(*subh);
		plen -= sizeof(*subh);

		if (subh->action >= PFSYNC_ACT_MAX) {
			ND_PRINT((ndo, "\n    act UNKNOWN id %d",
			    subh->action));
			return;
		}

		count = ntohs(subh->count);
		ND_PRINT((ndo, "\n    %s count %d", actions[subh->action].name,
		    count));
		alen = actions[subh->action].len;

		if (subh->action == PFSYNC_ACT_EOF)
			return;

		if (actions[subh->action].print == NULL) {
			ND_PRINT((ndo, "\n    unimplemented action %hhu",
			    subh->action));
			return;
		}

		for (i = 0; i < count; i++) {
			if (len < alen) {
				len = 0;
				break;
			}

			if (ndo->ndo_vflag)
				actions[subh->action].print(ndo, bp);

			bp += alen;
			len -= alen;
			plen -= alen;
		}
	}

	if (plen > 0) {
		ND_PRINT((ndo, "\n    ..."));
		return;
	}
	if (plen < 0) {
		ND_PRINT((ndo, "\n    invalid header length"));
		return;
	}
	if (len > 0)
		ND_PRINT((ndo, "\n    invalid packet length"));
}

static void
pfsync_print_clr(netdissect_options *ndo, const void *bp)
{
	const struct pfsync_clr *clr = bp;

	ND_PRINT((ndo, "\n\tcreatorid: %08x", htonl(clr->creatorid)));
	if (clr->ifname[0] != '\0')
		ND_PRINT((ndo, " interface: %s", clr->ifname));
}

static void
pfsync_print_state(netdissect_options *ndo, const void *bp)
{
	struct pfsync_state *st = (struct pfsync_state *)bp;

	safeputchar(ndo, '\n');
	print_state(ndo, st);
}

static void
pfsync_print_ins_ack(netdissect_options *ndo, const void *bp)
{
	const struct pfsync_ins_ack *iack = bp;

	ND_PRINT((ndo, "\n\tid: %016jx creatorid: %08x",
	    (uintmax_t)be64toh(iack->id), ntohl(iack->creatorid)));
}

static void
pfsync_print_upd_c(netdissect_options *ndo, const void *bp)
{
	const struct pfsync_upd_c *u = bp;

	ND_PRINT((ndo, "\n\tid: %016jx creatorid: %08x",
	    (uintmax_t)be64toh(u->id), ntohl(u->creatorid)));
	if (ndo->ndo_vflag > 2) {
		ND_PRINT((ndo, "\n\tTCP? :"));
		print_src_dst(ndo, &u->src, &u->dst, IPPROTO_TCP);
	}
}

static void
pfsync_print_upd_req(netdissect_options *ndo, const void *bp)
{
	const struct pfsync_upd_req *ur = bp;

	ND_PRINT((ndo, "\n\tid: %016jx creatorid: %08x",
	    (uintmax_t)be64toh(ur->id), ntohl(ur->creatorid)));
}

static void
pfsync_print_del_c(netdissect_options *ndo, const void *bp)
{
	const struct pfsync_del_c *d = bp;

	ND_PRINT((ndo, "\n\tid: %016jx creatorid: %08x",
	    (uintmax_t)be64toh(d->id), ntohl(d->creatorid)));
}

static void
pfsync_print_bus(netdissect_options *ndo, const void *bp)
{
	const struct pfsync_bus *b = bp;
	uint32_t endtime;
	int min, sec;
	const char *status;

	endtime = ntohl(b->endtime);
	sec = endtime % 60;
	endtime /= 60;
	min = endtime % 60;
	endtime /= 60;

	switch (b->status) {
	case PFSYNC_BUS_START:
		status = "start";
		break;
	case PFSYNC_BUS_END:
		status = "end";
		break;
	default:
		status = "UNKNOWN";
		break;
	}

	ND_PRINT((ndo, "\n\tcreatorid: %08x age: %.2u:%.2u:%.2u status: %s",
	    htonl(b->creatorid), endtime, min, sec, status));
}

static void
pfsync_print_tdb(netdissect_options *ndo, const void *bp)
{
	const struct pfsync_tdb *t = bp;

	ND_PRINT((ndo, "\n\tspi: 0x%08x rpl: %ju cur_bytes: %ju",
	    ntohl(t->spi), (uintmax_t )be64toh(t->rpl),
	    (uintmax_t )be64toh(t->cur_bytes)));
}

static void
print_host(netdissect_options *ndo, struct pf_addr *addr, uint16_t port,
    sa_family_t af, const char *proto)
{
	char buf[48];

	if (inet_ntop(af, addr, buf, sizeof(buf)) == NULL)
		ND_PRINT((ndo, "?"));
	else
		ND_PRINT((ndo, "%s", buf));

	if (port)
		ND_PRINT((ndo, ".%hu", ntohs(port)));
}

static void
print_seq(netdissect_options *ndo, const struct pfsync_state_peer *p)
{
	if (p->seqdiff)
		ND_PRINT((ndo, "[%u + %u](+%u)", ntohl(p->seqlo),
		    ntohl(p->seqhi) - ntohl(p->seqlo), ntohl(p->seqdiff)));
	else
		ND_PRINT((ndo, "[%u + %u]", ntohl(p->seqlo),
		    ntohl(p->seqhi) - ntohl(p->seqlo)));
}

static void
print_src_dst(netdissect_options *ndo, const struct pfsync_state_peer *src,
    const struct pfsync_state_peer *dst, uint8_t proto)
{

	if (proto == IPPROTO_TCP) {
		if (src->state <= TCPS_TIME_WAIT &&
		    dst->state <= TCPS_TIME_WAIT)
			ND_PRINT((ndo, "   %s:%s", tcpstates[src->state],
			    tcpstates[dst->state]));
		else if (src->state == PF_TCPS_PROXY_SRC ||
		    dst->state == PF_TCPS_PROXY_SRC)
			ND_PRINT((ndo, "   PROXY:SRC"));
		else if (src->state == PF_TCPS_PROXY_DST ||
		    dst->state == PF_TCPS_PROXY_DST)
			ND_PRINT((ndo, "   PROXY:DST"));
		else
			ND_PRINT((ndo, "   <BAD STATE LEVELS %u:%u>",
			    src->state, dst->state));
		if (ndo->ndo_vflag > 1) {
			ND_PRINT((ndo, "\n\t"));
			print_seq(ndo, src);
			if (src->wscale && dst->wscale)
				ND_PRINT((ndo, " wscale %u",
				    src->wscale & PF_WSCALE_MASK));
			ND_PRINT((ndo, "  "));
			print_seq(ndo, dst);
			if (src->wscale && dst->wscale)
				ND_PRINT((ndo, " wscale %u",
				    dst->wscale & PF_WSCALE_MASK));
		}
	} else if (proto == IPPROTO_UDP && src->state < PFUDPS_NSTATES &&
	    dst->state < PFUDPS_NSTATES) {
		const char *states[] = PFUDPS_NAMES;

		ND_PRINT((ndo, "   %s:%s", states[src->state], states[dst->state]));
	} else if (proto != IPPROTO_ICMP && src->state < PFOTHERS_NSTATES &&
	    dst->state < PFOTHERS_NSTATES) {
		/* XXX ICMP doesn't really have state levels */
		const char *states[] = PFOTHERS_NAMES;

		ND_PRINT((ndo, "   %s:%s", states[src->state], states[dst->state]));
	} else {
		ND_PRINT((ndo, "   %u:%u", src->state, dst->state));
	}
}

static void
print_state(netdissect_options *ndo, struct pfsync_state *s)
{
	struct pfsync_state_peer *src, *dst;
	struct pfsync_state_key *sk, *nk;
	int min, sec;

	if (s->direction == PF_OUT) {
		src = &s->src;
		dst = &s->dst;
		sk = &s->key[PF_SK_STACK];
		nk = &s->key[PF_SK_WIRE];
		if (s->proto == IPPROTO_ICMP || s->proto == IPPROTO_ICMPV6)
			sk->port[0] = nk->port[0];
	} else {
		src = &s->dst;
		dst = &s->src;
		sk = &s->key[PF_SK_WIRE];
		nk = &s->key[PF_SK_STACK];
		if (s->proto == IPPROTO_ICMP || s->proto == IPPROTO_ICMPV6)
			sk->port[1] = nk->port[1];
	}
	ND_PRINT((ndo, "\t%s ", s->ifname));
	ND_PRINT((ndo, "proto %u ", s->proto));

	print_host(ndo, &nk->addr[1], nk->port[1], s->af, NULL);
	if (PF_ANEQ(&nk->addr[1], &sk->addr[1], s->af) ||
	    nk->port[1] != sk->port[1]) {
		ND_PRINT((ndo, " ("));
		print_host(ndo, &sk->addr[1], sk->port[1], s->af, NULL);
		ND_PRINT((ndo, ")"));
	}
	if (s->direction == PF_OUT)
		ND_PRINT((ndo, " -> "));
	else
		ND_PRINT((ndo, " <- "));
	print_host(ndo, &nk->addr[0], nk->port[0], s->af, NULL);
	if (PF_ANEQ(&nk->addr[0], &sk->addr[0], s->af) ||
	    nk->port[0] != sk->port[0]) {
		ND_PRINT((ndo, " ("));
		print_host(ndo, &sk->addr[0], sk->port[0], s->af, NULL);
		ND_PRINT((ndo, ")"));
	}

	print_src_dst(ndo, src, dst, s->proto);

	if (ndo->ndo_vflag > 1) {
		uint64_t packets[2];
		uint64_t bytes[2];
		uint32_t creation = ntohl(s->creation);
		uint32_t expire = ntohl(s->expire);

		sec = creation % 60;
		creation /= 60;
		min = creation % 60;
		creation /= 60;
		ND_PRINT((ndo, "\n\tage %.2u:%.2u:%.2u", creation, min, sec));
		sec = expire % 60;
		expire /= 60;
		min = expire % 60;
		expire /= 60;
		ND_PRINT((ndo, ", expires in %.2u:%.2u:%.2u", expire, min, sec));

		bcopy(s->packets[0], &packets[0], sizeof(uint64_t));
		bcopy(s->packets[1], &packets[1], sizeof(uint64_t));
		bcopy(s->bytes[0], &bytes[0], sizeof(uint64_t));
		bcopy(s->bytes[1], &bytes[1], sizeof(uint64_t));
		ND_PRINT((ndo, ", %ju:%ju pkts, %ju:%ju bytes",
		    be64toh(packets[0]), be64toh(packets[1]),
		    be64toh(bytes[0]), be64toh(bytes[1])));
		if (s->anchor != ntohl(-1))
			ND_PRINT((ndo, ", anchor %u", ntohl(s->anchor)));
		if (s->rule != ntohl(-1))
			ND_PRINT((ndo, ", rule %u", ntohl(s->rule)));
	}
	if (ndo->ndo_vflag > 1) {
		uint64_t id;

		bcopy(&s->id, &id, sizeof(uint64_t));
		ND_PRINT((ndo, "\n\tid: %016jx creatorid: %08x",
		    (uintmax_t )be64toh(id), ntohl(s->creatorid)));
	}
}
