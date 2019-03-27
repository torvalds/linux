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

/* \summary: Transparent Inter-Process Communication (TIPC) protocol printer */

/*
 * specification:
 *	http://tipc.sourceforge.net/doc/draft-spec-tipc-07.html
 *	http://tipc.sourceforge.net/doc/tipc_message_formats.html
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <netdissect-stdinc.h>

#include "netdissect.h"
#include "ether.h"
#include "ethertype.h"
#include "extract.h"

static const char tstr[] = "[|TIPC]";

#define TIPC_USER_LOW_IMPORTANCE	0
#define TIPC_USER_MEDIUM_IMPORTANCE	1
#define TIPC_USER_HIGH_IMPORTANCE	2
#define TIPC_USER_CRITICAL_IMPORTANCE	3
#define TIPC_USER_BCAST_PROTOCOL	5
#define TIPC_USER_MSG_BUNDLER		6
#define TIPC_USER_LINK_PROTOCOL		7
#define TIPC_USER_CONN_MANAGER		8
#define TIPC_USER_CHANGEOVER_PROTOCOL	10
#define TIPC_USER_NAME_DISTRIBUTOR	11
#define TIPC_USER_MSG_FRAGMENTER	12
#define TIPC_USER_LINK_CONFIG		13

#define TIPC_CONN_MSG			0
#define TIPC_DIRECT_MSG			1
#define TIPC_NAMED_MSG			2
#define TIPC_MCAST_MSG			3

#define TIPC_ZONE(addr)		(((addr) >> 24) & 0xFF)
#define TIPC_CLUSTER(addr)	(((addr) >> 12) & 0xFFF)
#define TIPC_NODE(addr)		(((addr) >> 0) & 0xFFF)

struct tipc_pkthdr {
	uint32_t w0;
	uint32_t w1;
};

#define TIPC_VER(w0)		(((w0) >> 29) & 0x07)
#define TIPC_USER(w0)		(((w0) >> 25) & 0x0F)
#define TIPC_HSIZE(w0)		(((w0) >> 21) & 0x0F)
#define TIPC_MSIZE(w0)		(((w0) >> 0) & 0xFFFF)
#define TIPC_MTYPE(w1)		(((w1) >> 29) & 0x07)
#define TIPC_BROADCAST_ACK(w1)	(((w1) >> 0) & 0xFFFF)
#define TIPC_LINK_ACK(w2)	(((w2) >> 16) & 0xFFFF)
#define TIPC_LINK_SEQ(w2)	(((w2) >> 0) & 0xFFFF)

static const struct tok tipcuser_values[] = {
    { TIPC_USER_LOW_IMPORTANCE,      "Low Importance Data payload" },
    { TIPC_USER_MEDIUM_IMPORTANCE,   "Medium Importance Data payload" },
    { TIPC_USER_HIGH_IMPORTANCE,     "High Importance Data payload" },
    { TIPC_USER_CRITICAL_IMPORTANCE, "Critical Importance Data payload" },
    { TIPC_USER_BCAST_PROTOCOL,      "Broadcast Link Protocol internal" },
    { TIPC_USER_MSG_BUNDLER,         "Message Bundler Protocol internal" },
    { TIPC_USER_LINK_PROTOCOL,       "Link State Protocol internal" },
    { TIPC_USER_CONN_MANAGER,        "Connection Manager internal" },
    { TIPC_USER_CHANGEOVER_PROTOCOL, "Link Changeover Protocol internal" },
    { TIPC_USER_NAME_DISTRIBUTOR,    "Name Table Update Protocol internal" },
    { TIPC_USER_MSG_FRAGMENTER,      "Message Fragmentation Protocol internal" },
    { TIPC_USER_LINK_CONFIG,         "Neighbor Detection Protocol internal" },
    { 0, NULL }
};

static const struct tok tipcmtype_values[] = {
    { TIPC_CONN_MSG,   "CONN_MSG" },
    { TIPC_DIRECT_MSG, "MCAST_MSG" },
    { TIPC_NAMED_MSG,  "NAMED_MSG" },
    { TIPC_MCAST_MSG,  "DIRECT_MSG" },
    { 0, NULL }
};

static const struct tok tipc_linkconf_mtype_values[] = {
    { 0,   "Link request" },
    { 1,   "Link response" },
    { 0, NULL }
};

struct payload_tipc_pkthdr {
	uint32_t w0;
	uint32_t w1;
	uint32_t w2;
	uint32_t prev_node;
	uint32_t orig_port;
	uint32_t dest_port;
	uint32_t orig_node;
	uint32_t dest_node;
	uint32_t name_type;
	uint32_t w9;
	uint32_t wA;
};

struct  internal_tipc_pkthdr {
	uint32_t w0;
	uint32_t w1;
	uint32_t w2;
	uint32_t prev_node;
	uint32_t w4;
	uint32_t w5;
	uint32_t orig_node;
	uint32_t dest_node;
	uint32_t trans_seq;
	uint32_t w9;
};

#define TIPC_SEQ_GAP(w1)	(((w1) >> 16) & 0x1FFF)
#define TIPC_BC_GAP_AFTER(w2)	(((w2) >> 16) & 0xFFFF)
#define TIPC_BC_GAP_TO(w2)	(((w2) >> 0) & 0xFFFF)
#define TIPC_LAST_SENT_FRAG(w4)	(((w4) >> 16) & 0xFFFF)
#define TIPC_NEXT_SENT_FRAG(w4)	(((w4) >> 0) & 0xFFFF)
#define TIPC_SESS_NO(w5)	(((w5) >> 16) & 0xFFFF)
#define TIPC_MSG_CNT(w9)	(((w9) >> 16) & 0xFFFF)
#define TIPC_LINK_TOL(w9)	(((w9) >> 0) & 0xFFFF)

struct link_conf_tipc_pkthdr {
	uint32_t w0;
	uint32_t w1;
	uint32_t dest_domain;
	uint32_t prev_node;
	uint32_t ntwrk_id;
	uint32_t w5;
	uint8_t media_address[16];
};

#define TIPC_NODE_SIG(w1)	(((w1) >> 0) & 0xFFFF)
#define TIPC_MEDIA_ID(w5)	(((w5) >> 0) & 0xFF)

static void
print_payload(netdissect_options *ndo, const struct payload_tipc_pkthdr *ap)
{
	uint32_t w0, w1, w2;
	u_int user;
	u_int hsize;
	u_int msize;
	u_int mtype;
	u_int broadcast_ack;
	u_int link_ack;
	u_int link_seq;
	u_int prev_node;
	u_int orig_port;
	u_int dest_port;
	u_int orig_node;
	u_int dest_node;

	ND_TCHECK(ap->dest_port);
	w0 = EXTRACT_32BITS(&ap->w0);
	user = TIPC_USER(w0);
	hsize = TIPC_HSIZE(w0);
	msize = TIPC_MSIZE(w0);
	w1 = EXTRACT_32BITS(&ap->w1);
	mtype = TIPC_MTYPE(w1);
	prev_node = EXTRACT_32BITS(&ap->prev_node);
	orig_port = EXTRACT_32BITS(&ap->orig_port);
	dest_port = EXTRACT_32BITS(&ap->dest_port);
	if (hsize <= 6) {
		ND_PRINT((ndo, "TIPC v%u.0 %u.%u.%u:%u > %u, headerlength %u bytes, MessageSize %u bytes, %s, messageType %s",
		    TIPC_VER(w0),
		    TIPC_ZONE(prev_node), TIPC_CLUSTER(prev_node), TIPC_NODE(prev_node),
		    orig_port, dest_port,
		    hsize*4, msize,
		    tok2str(tipcuser_values, "unknown", user),
		    tok2str(tipcmtype_values, "Unknown", mtype)));
	} else {
		ND_TCHECK(ap->dest_node);
		orig_node = EXTRACT_32BITS(&ap->orig_node);
		dest_node = EXTRACT_32BITS(&ap->dest_node);
		ND_PRINT((ndo, "TIPC v%u.0 %u.%u.%u:%u > %u.%u.%u:%u, headerlength %u bytes, MessageSize %u bytes, %s, messageType %s",
		    TIPC_VER(w0),
		    TIPC_ZONE(orig_node), TIPC_CLUSTER(orig_node), TIPC_NODE(orig_node),
		    orig_port,
		    TIPC_ZONE(dest_node), TIPC_CLUSTER(dest_node), TIPC_NODE(dest_node),
		    dest_port,
		    hsize*4, msize,
		    tok2str(tipcuser_values, "unknown", user),
		    tok2str(tipcmtype_values, "Unknown", mtype)));

		if (ndo->ndo_vflag) {
			broadcast_ack = TIPC_BROADCAST_ACK(w1);
			w2 = EXTRACT_32BITS(&ap->w2);
			link_ack = TIPC_LINK_ACK(w2);
			link_seq = TIPC_LINK_SEQ(w2);
			ND_PRINT((ndo, "\n\tPrevious Node %u.%u.%u, Broadcast Ack %u, Link Ack %u, Link Sequence %u",
			    TIPC_ZONE(prev_node), TIPC_CLUSTER(prev_node), TIPC_NODE(prev_node),
			    broadcast_ack, link_ack, link_seq));
		}
	}
	return;

trunc:
	ND_PRINT((ndo, "%s", tstr));
}

static void
print_internal(netdissect_options *ndo, const struct internal_tipc_pkthdr *ap)
{
	uint32_t w0, w1, w2, w4, w5, w9;
	u_int user;
	u_int hsize;
	u_int msize;
	u_int mtype;
	u_int seq_gap;
	u_int broadcast_ack;
	u_int bc_gap_after;
	u_int bc_gap_to;
	u_int prev_node;
	u_int last_sent_frag;
	u_int next_sent_frag;
	u_int sess_no;
	u_int orig_node;
	u_int dest_node;
	u_int trans_seq;
	u_int msg_cnt;
	u_int link_tol;

	ND_TCHECK(ap->dest_node);
	w0 = EXTRACT_32BITS(&ap->w0);
	user = TIPC_USER(w0);
	hsize = TIPC_HSIZE(w0);
	msize = TIPC_MSIZE(w0);
	w1 = EXTRACT_32BITS(&ap->w1);
	mtype = TIPC_MTYPE(w1);
	orig_node = EXTRACT_32BITS(&ap->orig_node);
	dest_node = EXTRACT_32BITS(&ap->dest_node);
	ND_PRINT((ndo, "TIPC v%u.0 %u.%u.%u > %u.%u.%u, headerlength %u bytes, MessageSize %u bytes, %s, messageType %s (0x%08x)",
	    TIPC_VER(w0),
	    TIPC_ZONE(orig_node), TIPC_CLUSTER(orig_node), TIPC_NODE(orig_node),
	    TIPC_ZONE(dest_node), TIPC_CLUSTER(dest_node), TIPC_NODE(dest_node),
	    hsize*4, msize,
	    tok2str(tipcuser_values, "unknown", user),
	    tok2str(tipcmtype_values, "Unknown", mtype), w1));

	if (ndo->ndo_vflag) {
		ND_TCHECK(*ap);
		seq_gap = TIPC_SEQ_GAP(w1);
		broadcast_ack = TIPC_BROADCAST_ACK(w1);
		w2 = EXTRACT_32BITS(&ap->w2);
		bc_gap_after = TIPC_BC_GAP_AFTER(w2);
		bc_gap_to = TIPC_BC_GAP_TO(w2);
		prev_node = EXTRACT_32BITS(&ap->prev_node);
		w4 = EXTRACT_32BITS(&ap->w4);
		last_sent_frag = TIPC_LAST_SENT_FRAG(w4);
		next_sent_frag = TIPC_NEXT_SENT_FRAG(w4);
		w5 = EXTRACT_32BITS(&ap->w5);
		sess_no = TIPC_SESS_NO(w5);
		trans_seq = EXTRACT_32BITS(&ap->trans_seq);
		w9 = EXTRACT_32BITS(&ap->w9);
		msg_cnt = TIPC_MSG_CNT(w9);
		link_tol = TIPC_LINK_TOL(w9);
		ND_PRINT((ndo, "\n\tPrevious Node %u.%u.%u, Session No. %u, Broadcast Ack %u, Sequence Gap %u,  Broadcast Gap After %u, Broadcast Gap To %u, Last Sent Packet No. %u, Next sent Packet No. %u, Transport Sequence %u, msg_count %u, Link Tolerance %u",
		    TIPC_ZONE(prev_node), TIPC_CLUSTER(prev_node), TIPC_NODE(prev_node),
		    sess_no, broadcast_ack, seq_gap, bc_gap_after, bc_gap_to,
		    last_sent_frag, next_sent_frag, trans_seq, msg_cnt,
		    link_tol));
	}
	return;

trunc:
	ND_PRINT((ndo, "%s", tstr));
}

static void
print_link_conf(netdissect_options *ndo, const struct link_conf_tipc_pkthdr *ap)
{
	uint32_t w0, w1, w5;
	u_int user;
	u_int hsize;
	u_int msize;
	u_int mtype;
	u_int node_sig;
	u_int prev_node;
	u_int dest_domain;
	u_int ntwrk_id;
	u_int media_id;

	ND_TCHECK(ap->prev_node);
	w0 = EXTRACT_32BITS(&ap->w0);
	user = TIPC_USER(w0);
	hsize = TIPC_HSIZE(w0);
	msize = TIPC_MSIZE(w0);
	w1 = EXTRACT_32BITS(&ap->w1);
	mtype = TIPC_MTYPE(w1);
	dest_domain = EXTRACT_32BITS(&ap->dest_domain);
	prev_node = EXTRACT_32BITS(&ap->prev_node);

	ND_PRINT((ndo, "TIPC v%u.0 %u.%u.%u > %u.%u.%u, headerlength %u bytes, MessageSize %u bytes, %s, messageType %s",
	    TIPC_VER(w0),
	    TIPC_ZONE(prev_node), TIPC_CLUSTER(prev_node), TIPC_NODE(prev_node),
	    TIPC_ZONE(dest_domain), TIPC_CLUSTER(dest_domain), TIPC_NODE(dest_domain),
	    hsize*4, msize,
	    tok2str(tipcuser_values, "unknown", user),
	    tok2str(tipc_linkconf_mtype_values, "Unknown", mtype)));
	if (ndo->ndo_vflag) {
		ND_TCHECK(ap->w5);
		node_sig = TIPC_NODE_SIG(w1);
		ntwrk_id = EXTRACT_32BITS(&ap->ntwrk_id);
		w5 = EXTRACT_32BITS(&ap->w5);
		media_id = TIPC_MEDIA_ID(w5);
		ND_PRINT((ndo, "\n\tNodeSignature %u, network_id %u, media_id %u",
		    node_sig, ntwrk_id, media_id));
	}
	return;

trunc:
	ND_PRINT((ndo, "%s", tstr));
}

void
tipc_print(netdissect_options *ndo, const u_char *bp, u_int length _U_,
    u_int caplen _U_)
{
	const struct tipc_pkthdr *ap;
	uint32_t w0;
	u_int user;

	ap = (const struct tipc_pkthdr *)bp;
	ND_TCHECK(ap->w0);
	w0 = EXTRACT_32BITS(&ap->w0);
	user = TIPC_USER(w0);

	switch (user)
	{
		case TIPC_USER_LOW_IMPORTANCE:
		case TIPC_USER_MEDIUM_IMPORTANCE:
		case TIPC_USER_HIGH_IMPORTANCE:
		case TIPC_USER_CRITICAL_IMPORTANCE:
		case TIPC_USER_NAME_DISTRIBUTOR:
		case TIPC_USER_CONN_MANAGER:
			print_payload(ndo, (const struct payload_tipc_pkthdr *)bp);
			break;

		case TIPC_USER_LINK_CONFIG:
			print_link_conf(ndo, (const struct link_conf_tipc_pkthdr *)bp);
			break;

		case TIPC_USER_BCAST_PROTOCOL:
		case TIPC_USER_MSG_BUNDLER:
		case TIPC_USER_LINK_PROTOCOL:
		case TIPC_USER_CHANGEOVER_PROTOCOL:
		case TIPC_USER_MSG_FRAGMENTER:
			print_internal(ndo, (const struct internal_tipc_pkthdr *)bp);
			break;

	}
	return;

trunc:
	ND_PRINT((ndo, "%s", tstr));
}

/*
 * Local Variables:
 * c-style: bsd
 * End:
 */

