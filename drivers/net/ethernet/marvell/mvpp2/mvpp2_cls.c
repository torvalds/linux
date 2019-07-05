// SPDX-License-Identifier: GPL-2.0
/*
 * RSS and Classifier helpers for Marvell PPv2 Network Controller
 *
 * Copyright (C) 2014 Marvell
 *
 * Marcin Wojtas <mw@semihalf.com>
 */

#include "mvpp2.h"
#include "mvpp2_cls.h"
#include "mvpp2_prs.h"

#define MVPP2_DEF_FLOW(_type, _id, _opts, _ri, _ri_mask)	\
{								\
	.flow_type = _type,					\
	.flow_id = _id,						\
	.supported_hash_opts = _opts,				\
	.prs_ri = {						\
		.ri = _ri,					\
		.ri_mask = _ri_mask				\
	}							\
}

static const struct mvpp2_cls_flow cls_flows[MVPP2_N_PRS_FLOWS] = {
	/* TCP over IPv4 flows, Not fragmented, no vlan tag */
	MVPP2_DEF_FLOW(MVPP22_FLOW_TCP4, MVPP2_FL_IP4_TCP_NF_UNTAG,
		       MVPP22_CLS_HEK_IP4_5T,
		       MVPP2_PRS_RI_VLAN_NONE | MVPP2_PRS_RI_L3_IP4 |
		       MVPP2_PRS_RI_L4_TCP,
		       MVPP2_PRS_IP_MASK | MVPP2_PRS_RI_VLAN_MASK),

	MVPP2_DEF_FLOW(MVPP22_FLOW_TCP4, MVPP2_FL_IP4_TCP_NF_UNTAG,
		       MVPP22_CLS_HEK_IP4_5T,
		       MVPP2_PRS_RI_VLAN_NONE | MVPP2_PRS_RI_L3_IP4_OPT |
		       MVPP2_PRS_RI_L4_TCP,
		       MVPP2_PRS_IP_MASK | MVPP2_PRS_RI_VLAN_MASK),

	MVPP2_DEF_FLOW(MVPP22_FLOW_TCP4, MVPP2_FL_IP4_TCP_NF_UNTAG,
		       MVPP22_CLS_HEK_IP4_5T,
		       MVPP2_PRS_RI_VLAN_NONE | MVPP2_PRS_RI_L3_IP4_OTHER |
		       MVPP2_PRS_RI_L4_TCP,
		       MVPP2_PRS_IP_MASK | MVPP2_PRS_RI_VLAN_MASK),

	/* TCP over IPv4 flows, Not fragmented, with vlan tag */
	MVPP2_DEF_FLOW(MVPP22_FLOW_TCP4, MVPP2_FL_IP4_TCP_NF_TAG,
		       MVPP22_CLS_HEK_IP4_5T | MVPP22_CLS_HEK_TAGGED,
		       MVPP2_PRS_RI_L3_IP4 | MVPP2_PRS_RI_L4_TCP,
		       MVPP2_PRS_IP_MASK),

	MVPP2_DEF_FLOW(MVPP22_FLOW_TCP4, MVPP2_FL_IP4_TCP_NF_TAG,
		       MVPP22_CLS_HEK_IP4_5T | MVPP22_CLS_HEK_TAGGED,
		       MVPP2_PRS_RI_L3_IP4_OPT | MVPP2_PRS_RI_L4_TCP,
		       MVPP2_PRS_IP_MASK),

	MVPP2_DEF_FLOW(MVPP22_FLOW_TCP4, MVPP2_FL_IP4_TCP_NF_TAG,
		       MVPP22_CLS_HEK_IP4_5T | MVPP22_CLS_HEK_TAGGED,
		       MVPP2_PRS_RI_L3_IP4_OTHER | MVPP2_PRS_RI_L4_TCP,
		       MVPP2_PRS_IP_MASK),

	/* TCP over IPv4 flows, fragmented, no vlan tag */
	MVPP2_DEF_FLOW(MVPP22_FLOW_TCP4, MVPP2_FL_IP4_TCP_FRAG_UNTAG,
		       MVPP22_CLS_HEK_IP4_2T,
		       MVPP2_PRS_RI_VLAN_NONE | MVPP2_PRS_RI_L3_IP4 |
		       MVPP2_PRS_RI_L4_TCP,
		       MVPP2_PRS_IP_MASK | MVPP2_PRS_RI_VLAN_MASK),

	MVPP2_DEF_FLOW(MVPP22_FLOW_TCP4, MVPP2_FL_IP4_TCP_FRAG_UNTAG,
		       MVPP22_CLS_HEK_IP4_2T,
		       MVPP2_PRS_RI_VLAN_NONE | MVPP2_PRS_RI_L3_IP4_OPT |
		       MVPP2_PRS_RI_L4_TCP,
		       MVPP2_PRS_IP_MASK | MVPP2_PRS_RI_VLAN_MASK),

	MVPP2_DEF_FLOW(MVPP22_FLOW_TCP4, MVPP2_FL_IP4_TCP_FRAG_UNTAG,
		       MVPP22_CLS_HEK_IP4_2T,
		       MVPP2_PRS_RI_VLAN_NONE | MVPP2_PRS_RI_L3_IP4_OTHER |
		       MVPP2_PRS_RI_L4_TCP,
		       MVPP2_PRS_IP_MASK | MVPP2_PRS_RI_VLAN_MASK),

	/* TCP over IPv4 flows, fragmented, with vlan tag */
	MVPP2_DEF_FLOW(MVPP22_FLOW_TCP4, MVPP2_FL_IP4_TCP_FRAG_TAG,
		       MVPP22_CLS_HEK_IP4_2T | MVPP22_CLS_HEK_TAGGED,
		       MVPP2_PRS_RI_L3_IP4 | MVPP2_PRS_RI_L4_TCP,
		       MVPP2_PRS_IP_MASK),

	MVPP2_DEF_FLOW(MVPP22_FLOW_TCP4, MVPP2_FL_IP4_TCP_FRAG_TAG,
		       MVPP22_CLS_HEK_IP4_2T | MVPP22_CLS_HEK_TAGGED,
		       MVPP2_PRS_RI_L3_IP4_OPT | MVPP2_PRS_RI_L4_TCP,
		       MVPP2_PRS_IP_MASK),

	MVPP2_DEF_FLOW(MVPP22_FLOW_TCP4, MVPP2_FL_IP4_TCP_FRAG_TAG,
		       MVPP22_CLS_HEK_IP4_2T | MVPP22_CLS_HEK_TAGGED,
		       MVPP2_PRS_RI_L3_IP4_OTHER | MVPP2_PRS_RI_L4_TCP,
		       MVPP2_PRS_IP_MASK),

	/* UDP over IPv4 flows, Not fragmented, no vlan tag */
	MVPP2_DEF_FLOW(MVPP22_FLOW_UDP4, MVPP2_FL_IP4_UDP_NF_UNTAG,
		       MVPP22_CLS_HEK_IP4_5T,
		       MVPP2_PRS_RI_VLAN_NONE | MVPP2_PRS_RI_L3_IP4 |
		       MVPP2_PRS_RI_L4_UDP,
		       MVPP2_PRS_IP_MASK | MVPP2_PRS_RI_VLAN_MASK),

	MVPP2_DEF_FLOW(MVPP22_FLOW_UDP4, MVPP2_FL_IP4_UDP_NF_UNTAG,
		       MVPP22_CLS_HEK_IP4_5T,
		       MVPP2_PRS_RI_VLAN_NONE | MVPP2_PRS_RI_L3_IP4_OPT |
		       MVPP2_PRS_RI_L4_UDP,
		       MVPP2_PRS_IP_MASK | MVPP2_PRS_RI_VLAN_MASK),

	MVPP2_DEF_FLOW(MVPP22_FLOW_UDP4, MVPP2_FL_IP4_UDP_NF_UNTAG,
		       MVPP22_CLS_HEK_IP4_5T,
		       MVPP2_PRS_RI_VLAN_NONE | MVPP2_PRS_RI_L3_IP4_OTHER |
		       MVPP2_PRS_RI_L4_UDP,
		       MVPP2_PRS_IP_MASK | MVPP2_PRS_RI_VLAN_MASK),

	/* UDP over IPv4 flows, Not fragmented, with vlan tag */
	MVPP2_DEF_FLOW(MVPP22_FLOW_UDP4, MVPP2_FL_IP4_UDP_NF_TAG,
		       MVPP22_CLS_HEK_IP4_5T | MVPP22_CLS_HEK_TAGGED,
		       MVPP2_PRS_RI_L3_IP4 | MVPP2_PRS_RI_L4_UDP,
		       MVPP2_PRS_IP_MASK),

	MVPP2_DEF_FLOW(MVPP22_FLOW_UDP4, MVPP2_FL_IP4_UDP_NF_TAG,
		       MVPP22_CLS_HEK_IP4_5T | MVPP22_CLS_HEK_TAGGED,
		       MVPP2_PRS_RI_L3_IP4_OPT | MVPP2_PRS_RI_L4_UDP,
		       MVPP2_PRS_IP_MASK),

	MVPP2_DEF_FLOW(MVPP22_FLOW_UDP4, MVPP2_FL_IP4_UDP_NF_TAG,
		       MVPP22_CLS_HEK_IP4_5T | MVPP22_CLS_HEK_TAGGED,
		       MVPP2_PRS_RI_L3_IP4_OTHER | MVPP2_PRS_RI_L4_UDP,
		       MVPP2_PRS_IP_MASK),

	/* UDP over IPv4 flows, fragmented, no vlan tag */
	MVPP2_DEF_FLOW(MVPP22_FLOW_UDP4, MVPP2_FL_IP4_UDP_FRAG_UNTAG,
		       MVPP22_CLS_HEK_IP4_2T,
		       MVPP2_PRS_RI_VLAN_NONE | MVPP2_PRS_RI_L3_IP4 |
		       MVPP2_PRS_RI_L4_UDP,
		       MVPP2_PRS_IP_MASK | MVPP2_PRS_RI_VLAN_MASK),

	MVPP2_DEF_FLOW(MVPP22_FLOW_UDP4, MVPP2_FL_IP4_UDP_FRAG_UNTAG,
		       MVPP22_CLS_HEK_IP4_2T,
		       MVPP2_PRS_RI_VLAN_NONE | MVPP2_PRS_RI_L3_IP4_OPT |
		       MVPP2_PRS_RI_L4_UDP,
		       MVPP2_PRS_IP_MASK | MVPP2_PRS_RI_VLAN_MASK),

	MVPP2_DEF_FLOW(MVPP22_FLOW_UDP4, MVPP2_FL_IP4_UDP_FRAG_UNTAG,
		       MVPP22_CLS_HEK_IP4_2T,
		       MVPP2_PRS_RI_VLAN_NONE | MVPP2_PRS_RI_L3_IP4_OTHER |
		       MVPP2_PRS_RI_L4_UDP,
		       MVPP2_PRS_IP_MASK | MVPP2_PRS_RI_VLAN_MASK),

	/* UDP over IPv4 flows, fragmented, with vlan tag */
	MVPP2_DEF_FLOW(MVPP22_FLOW_UDP4, MVPP2_FL_IP4_UDP_FRAG_TAG,
		       MVPP22_CLS_HEK_IP4_2T | MVPP22_CLS_HEK_TAGGED,
		       MVPP2_PRS_RI_L3_IP4 | MVPP2_PRS_RI_L4_UDP,
		       MVPP2_PRS_IP_MASK),

	MVPP2_DEF_FLOW(MVPP22_FLOW_UDP4, MVPP2_FL_IP4_UDP_FRAG_TAG,
		       MVPP22_CLS_HEK_IP4_2T | MVPP22_CLS_HEK_TAGGED,
		       MVPP2_PRS_RI_L3_IP4_OPT | MVPP2_PRS_RI_L4_UDP,
		       MVPP2_PRS_IP_MASK),

	MVPP2_DEF_FLOW(MVPP22_FLOW_UDP4, MVPP2_FL_IP4_UDP_FRAG_TAG,
		       MVPP22_CLS_HEK_IP4_2T | MVPP22_CLS_HEK_TAGGED,
		       MVPP2_PRS_RI_L3_IP4_OTHER | MVPP2_PRS_RI_L4_UDP,
		       MVPP2_PRS_IP_MASK),

	/* TCP over IPv6 flows, not fragmented, no vlan tag */
	MVPP2_DEF_FLOW(MVPP22_FLOW_TCP6, MVPP2_FL_IP6_TCP_NF_UNTAG,
		       MVPP22_CLS_HEK_IP6_5T,
		       MVPP2_PRS_RI_VLAN_NONE | MVPP2_PRS_RI_L3_IP6 |
		       MVPP2_PRS_RI_L4_TCP,
		       MVPP2_PRS_IP_MASK | MVPP2_PRS_RI_VLAN_MASK),

	MVPP2_DEF_FLOW(MVPP22_FLOW_TCP6, MVPP2_FL_IP6_TCP_NF_UNTAG,
		       MVPP22_CLS_HEK_IP6_5T,
		       MVPP2_PRS_RI_VLAN_NONE | MVPP2_PRS_RI_L3_IP6_EXT |
		       MVPP2_PRS_RI_L4_TCP,
		       MVPP2_PRS_IP_MASK | MVPP2_PRS_RI_VLAN_MASK),

	/* TCP over IPv6 flows, not fragmented, with vlan tag */
	MVPP2_DEF_FLOW(MVPP22_FLOW_TCP6, MVPP2_FL_IP6_TCP_NF_TAG,
		       MVPP22_CLS_HEK_IP6_5T | MVPP22_CLS_HEK_TAGGED,
		       MVPP2_PRS_RI_L3_IP6 | MVPP2_PRS_RI_L4_TCP,
		       MVPP2_PRS_IP_MASK),

	MVPP2_DEF_FLOW(MVPP22_FLOW_TCP6, MVPP2_FL_IP6_TCP_NF_TAG,
		       MVPP22_CLS_HEK_IP6_5T | MVPP22_CLS_HEK_TAGGED,
		       MVPP2_PRS_RI_L3_IP6_EXT | MVPP2_PRS_RI_L4_TCP,
		       MVPP2_PRS_IP_MASK),

	/* TCP over IPv6 flows, fragmented, no vlan tag */
	MVPP2_DEF_FLOW(MVPP22_FLOW_TCP6, MVPP2_FL_IP6_TCP_FRAG_UNTAG,
		       MVPP22_CLS_HEK_IP6_2T,
		       MVPP2_PRS_RI_VLAN_NONE | MVPP2_PRS_RI_L3_IP6 |
		       MVPP2_PRS_RI_IP_FRAG_TRUE | MVPP2_PRS_RI_L4_TCP,
		       MVPP2_PRS_IP_MASK | MVPP2_PRS_RI_VLAN_MASK),

	MVPP2_DEF_FLOW(MVPP22_FLOW_TCP6, MVPP2_FL_IP6_TCP_FRAG_UNTAG,
		       MVPP22_CLS_HEK_IP6_2T,
		       MVPP2_PRS_RI_VLAN_NONE | MVPP2_PRS_RI_L3_IP6_EXT |
		       MVPP2_PRS_RI_IP_FRAG_TRUE | MVPP2_PRS_RI_L4_TCP,
		       MVPP2_PRS_IP_MASK | MVPP2_PRS_RI_VLAN_MASK),

	/* TCP over IPv6 flows, fragmented, with vlan tag */
	MVPP2_DEF_FLOW(MVPP22_FLOW_TCP6, MVPP2_FL_IP6_TCP_FRAG_TAG,
		       MVPP22_CLS_HEK_IP6_2T | MVPP22_CLS_HEK_TAGGED,
		       MVPP2_PRS_RI_L3_IP6 | MVPP2_PRS_RI_IP_FRAG_TRUE |
		       MVPP2_PRS_RI_L4_TCP,
		       MVPP2_PRS_IP_MASK),

	MVPP2_DEF_FLOW(MVPP22_FLOW_TCP6, MVPP2_FL_IP6_TCP_FRAG_TAG,
		       MVPP22_CLS_HEK_IP6_2T | MVPP22_CLS_HEK_TAGGED,
		       MVPP2_PRS_RI_L3_IP6_EXT | MVPP2_PRS_RI_IP_FRAG_TRUE |
		       MVPP2_PRS_RI_L4_TCP,
		       MVPP2_PRS_IP_MASK),

	/* UDP over IPv6 flows, not fragmented, no vlan tag */
	MVPP2_DEF_FLOW(MVPP22_FLOW_UDP6, MVPP2_FL_IP6_UDP_NF_UNTAG,
		       MVPP22_CLS_HEK_IP6_5T,
		       MVPP2_PRS_RI_VLAN_NONE | MVPP2_PRS_RI_L3_IP6 |
		       MVPP2_PRS_RI_L4_UDP,
		       MVPP2_PRS_IP_MASK | MVPP2_PRS_RI_VLAN_MASK),

	MVPP2_DEF_FLOW(MVPP22_FLOW_UDP6, MVPP2_FL_IP6_UDP_NF_UNTAG,
		       MVPP22_CLS_HEK_IP6_5T,
		       MVPP2_PRS_RI_VLAN_NONE | MVPP2_PRS_RI_L3_IP6_EXT |
		       MVPP2_PRS_RI_L4_UDP,
		       MVPP2_PRS_IP_MASK | MVPP2_PRS_RI_VLAN_MASK),

	/* UDP over IPv6 flows, not fragmented, with vlan tag */
	MVPP2_DEF_FLOW(MVPP22_FLOW_UDP6, MVPP2_FL_IP6_UDP_NF_TAG,
		       MVPP22_CLS_HEK_IP6_5T | MVPP22_CLS_HEK_TAGGED,
		       MVPP2_PRS_RI_L3_IP6 | MVPP2_PRS_RI_L4_UDP,
		       MVPP2_PRS_IP_MASK),

	MVPP2_DEF_FLOW(MVPP22_FLOW_UDP6, MVPP2_FL_IP6_UDP_NF_TAG,
		       MVPP22_CLS_HEK_IP6_5T | MVPP22_CLS_HEK_TAGGED,
		       MVPP2_PRS_RI_L3_IP6_EXT | MVPP2_PRS_RI_L4_UDP,
		       MVPP2_PRS_IP_MASK),

	/* UDP over IPv6 flows, fragmented, no vlan tag */
	MVPP2_DEF_FLOW(MVPP22_FLOW_UDP6, MVPP2_FL_IP6_UDP_FRAG_UNTAG,
		       MVPP22_CLS_HEK_IP6_2T,
		       MVPP2_PRS_RI_VLAN_NONE | MVPP2_PRS_RI_L3_IP6 |
		       MVPP2_PRS_RI_IP_FRAG_TRUE | MVPP2_PRS_RI_L4_UDP,
		       MVPP2_PRS_IP_MASK | MVPP2_PRS_RI_VLAN_MASK),

	MVPP2_DEF_FLOW(MVPP22_FLOW_UDP6, MVPP2_FL_IP6_UDP_FRAG_UNTAG,
		       MVPP22_CLS_HEK_IP6_2T,
		       MVPP2_PRS_RI_VLAN_NONE | MVPP2_PRS_RI_L3_IP6_EXT |
		       MVPP2_PRS_RI_IP_FRAG_TRUE | MVPP2_PRS_RI_L4_UDP,
		       MVPP2_PRS_IP_MASK | MVPP2_PRS_RI_VLAN_MASK),

	/* UDP over IPv6 flows, fragmented, with vlan tag */
	MVPP2_DEF_FLOW(MVPP22_FLOW_UDP6, MVPP2_FL_IP6_UDP_FRAG_TAG,
		       MVPP22_CLS_HEK_IP6_2T | MVPP22_CLS_HEK_TAGGED,
		       MVPP2_PRS_RI_L3_IP6 | MVPP2_PRS_RI_IP_FRAG_TRUE |
		       MVPP2_PRS_RI_L4_UDP,
		       MVPP2_PRS_IP_MASK),

	MVPP2_DEF_FLOW(MVPP22_FLOW_UDP6, MVPP2_FL_IP6_UDP_FRAG_TAG,
		       MVPP22_CLS_HEK_IP6_2T | MVPP22_CLS_HEK_TAGGED,
		       MVPP2_PRS_RI_L3_IP6_EXT | MVPP2_PRS_RI_IP_FRAG_TRUE |
		       MVPP2_PRS_RI_L4_UDP,
		       MVPP2_PRS_IP_MASK),

	/* IPv4 flows, no vlan tag */
	MVPP2_DEF_FLOW(MVPP22_FLOW_IP4, MVPP2_FL_IP4_UNTAG,
		       MVPP22_CLS_HEK_IP4_2T,
		       MVPP2_PRS_RI_VLAN_NONE | MVPP2_PRS_RI_L3_IP4,
		       MVPP2_PRS_RI_VLAN_MASK | MVPP2_PRS_RI_L3_PROTO_MASK),
	MVPP2_DEF_FLOW(MVPP22_FLOW_IP4, MVPP2_FL_IP4_UNTAG,
		       MVPP22_CLS_HEK_IP4_2T,
		       MVPP2_PRS_RI_VLAN_NONE | MVPP2_PRS_RI_L3_IP4_OPT,
		       MVPP2_PRS_RI_VLAN_MASK | MVPP2_PRS_RI_L3_PROTO_MASK),
	MVPP2_DEF_FLOW(MVPP22_FLOW_IP4, MVPP2_FL_IP4_UNTAG,
		       MVPP22_CLS_HEK_IP4_2T,
		       MVPP2_PRS_RI_VLAN_NONE | MVPP2_PRS_RI_L3_IP4_OTHER,
		       MVPP2_PRS_RI_VLAN_MASK | MVPP2_PRS_RI_L3_PROTO_MASK),

	/* IPv4 flows, with vlan tag */
	MVPP2_DEF_FLOW(MVPP22_FLOW_IP4, MVPP2_FL_IP4_TAG,
		       MVPP22_CLS_HEK_IP4_2T | MVPP22_CLS_HEK_TAGGED,
		       MVPP2_PRS_RI_L3_IP4,
		       MVPP2_PRS_RI_L3_PROTO_MASK),
	MVPP2_DEF_FLOW(MVPP22_FLOW_IP4, MVPP2_FL_IP4_TAG,
		       MVPP22_CLS_HEK_IP4_2T | MVPP22_CLS_HEK_TAGGED,
		       MVPP2_PRS_RI_L3_IP4_OPT,
		       MVPP2_PRS_RI_L3_PROTO_MASK),
	MVPP2_DEF_FLOW(MVPP22_FLOW_IP4, MVPP2_FL_IP4_TAG,
		       MVPP22_CLS_HEK_IP4_2T | MVPP22_CLS_HEK_TAGGED,
		       MVPP2_PRS_RI_L3_IP4_OTHER,
		       MVPP2_PRS_RI_L3_PROTO_MASK),

	/* IPv6 flows, no vlan tag */
	MVPP2_DEF_FLOW(MVPP22_FLOW_IP6, MVPP2_FL_IP6_UNTAG,
		       MVPP22_CLS_HEK_IP6_2T,
		       MVPP2_PRS_RI_VLAN_NONE | MVPP2_PRS_RI_L3_IP6,
		       MVPP2_PRS_RI_VLAN_MASK | MVPP2_PRS_RI_L3_PROTO_MASK),
	MVPP2_DEF_FLOW(MVPP22_FLOW_IP6, MVPP2_FL_IP6_UNTAG,
		       MVPP22_CLS_HEK_IP6_2T,
		       MVPP2_PRS_RI_VLAN_NONE | MVPP2_PRS_RI_L3_IP6,
		       MVPP2_PRS_RI_VLAN_MASK | MVPP2_PRS_RI_L3_PROTO_MASK),

	/* IPv6 flows, with vlan tag */
	MVPP2_DEF_FLOW(MVPP22_FLOW_IP6, MVPP2_FL_IP6_TAG,
		       MVPP22_CLS_HEK_IP6_2T | MVPP22_CLS_HEK_TAGGED,
		       MVPP2_PRS_RI_L3_IP6,
		       MVPP2_PRS_RI_L3_PROTO_MASK),
	MVPP2_DEF_FLOW(MVPP22_FLOW_IP6, MVPP2_FL_IP6_TAG,
		       MVPP22_CLS_HEK_IP6_2T | MVPP22_CLS_HEK_TAGGED,
		       MVPP2_PRS_RI_L3_IP6,
		       MVPP2_PRS_RI_L3_PROTO_MASK),

	/* Non IP flow, no vlan tag */
	MVPP2_DEF_FLOW(MVPP22_FLOW_ETHERNET, MVPP2_FL_NON_IP_UNTAG,
		       0,
		       MVPP2_PRS_RI_VLAN_NONE,
		       MVPP2_PRS_RI_VLAN_MASK),
	/* Non IP flow, with vlan tag */
	MVPP2_DEF_FLOW(MVPP22_FLOW_ETHERNET, MVPP2_FL_NON_IP_TAG,
		       MVPP22_CLS_HEK_OPT_VLAN,
		       0, 0),
};

u32 mvpp2_cls_flow_hits(struct mvpp2 *priv, int index)
{
	mvpp2_write(priv, MVPP2_CTRS_IDX, index);

	return mvpp2_read(priv, MVPP2_CLS_FLOW_TBL_HIT_CTR);
}

void mvpp2_cls_flow_read(struct mvpp2 *priv, int index,
			 struct mvpp2_cls_flow_entry *fe)
{
	fe->index = index;
	mvpp2_write(priv, MVPP2_CLS_FLOW_INDEX_REG, index);
	fe->data[0] = mvpp2_read(priv, MVPP2_CLS_FLOW_TBL0_REG);
	fe->data[1] = mvpp2_read(priv, MVPP2_CLS_FLOW_TBL1_REG);
	fe->data[2] = mvpp2_read(priv, MVPP2_CLS_FLOW_TBL2_REG);
}

/* Update classification flow table registers */
static void mvpp2_cls_flow_write(struct mvpp2 *priv,
				 struct mvpp2_cls_flow_entry *fe)
{
	mvpp2_write(priv, MVPP2_CLS_FLOW_INDEX_REG, fe->index);
	mvpp2_write(priv, MVPP2_CLS_FLOW_TBL0_REG, fe->data[0]);
	mvpp2_write(priv, MVPP2_CLS_FLOW_TBL1_REG, fe->data[1]);
	mvpp2_write(priv, MVPP2_CLS_FLOW_TBL2_REG, fe->data[2]);
}

u32 mvpp2_cls_lookup_hits(struct mvpp2 *priv, int index)
{
	mvpp2_write(priv, MVPP2_CTRS_IDX, index);

	return mvpp2_read(priv, MVPP2_CLS_DEC_TBL_HIT_CTR);
}

void mvpp2_cls_lookup_read(struct mvpp2 *priv, int lkpid, int way,
			   struct mvpp2_cls_lookup_entry *le)
{
	u32 val;

	val = (way << MVPP2_CLS_LKP_INDEX_WAY_OFFS) | lkpid;
	mvpp2_write(priv, MVPP2_CLS_LKP_INDEX_REG, val);
	le->way = way;
	le->lkpid = lkpid;
	le->data = mvpp2_read(priv, MVPP2_CLS_LKP_TBL_REG);
}

/* Update classification lookup table register */
static void mvpp2_cls_lookup_write(struct mvpp2 *priv,
				   struct mvpp2_cls_lookup_entry *le)
{
	u32 val;

	val = (le->way << MVPP2_CLS_LKP_INDEX_WAY_OFFS) | le->lkpid;
	mvpp2_write(priv, MVPP2_CLS_LKP_INDEX_REG, val);
	mvpp2_write(priv, MVPP2_CLS_LKP_TBL_REG, le->data);
}

/* Operations on flow entry */
static int mvpp2_cls_flow_hek_num_get(struct mvpp2_cls_flow_entry *fe)
{
	return fe->data[1] & MVPP2_CLS_FLOW_TBL1_N_FIELDS_MASK;
}

static void mvpp2_cls_flow_hek_num_set(struct mvpp2_cls_flow_entry *fe,
				       int num_of_fields)
{
	fe->data[1] &= ~MVPP2_CLS_FLOW_TBL1_N_FIELDS_MASK;
	fe->data[1] |= MVPP2_CLS_FLOW_TBL1_N_FIELDS(num_of_fields);
}

static int mvpp2_cls_flow_hek_get(struct mvpp2_cls_flow_entry *fe,
				  int field_index)
{
	return (fe->data[2] >> MVPP2_CLS_FLOW_TBL2_FLD_OFFS(field_index)) &
		MVPP2_CLS_FLOW_TBL2_FLD_MASK;
}

static void mvpp2_cls_flow_hek_set(struct mvpp2_cls_flow_entry *fe,
				   int field_index, int field_id)
{
	fe->data[2] &= ~MVPP2_CLS_FLOW_TBL2_FLD(field_index,
						MVPP2_CLS_FLOW_TBL2_FLD_MASK);
	fe->data[2] |= MVPP2_CLS_FLOW_TBL2_FLD(field_index, field_id);
}

static void mvpp2_cls_flow_eng_set(struct mvpp2_cls_flow_entry *fe,
				   int engine)
{
	fe->data[0] &= ~MVPP2_CLS_FLOW_TBL0_ENG(MVPP2_CLS_FLOW_TBL0_ENG_MASK);
	fe->data[0] |= MVPP2_CLS_FLOW_TBL0_ENG(engine);
}

int mvpp2_cls_flow_eng_get(struct mvpp2_cls_flow_entry *fe)
{
	return (fe->data[0] >> MVPP2_CLS_FLOW_TBL0_OFFS) &
		MVPP2_CLS_FLOW_TBL0_ENG_MASK;
}

static void mvpp2_cls_flow_port_id_sel(struct mvpp2_cls_flow_entry *fe,
				       bool from_packet)
{
	if (from_packet)
		fe->data[0] |= MVPP2_CLS_FLOW_TBL0_PORT_ID_SEL;
	else
		fe->data[0] &= ~MVPP2_CLS_FLOW_TBL0_PORT_ID_SEL;
}

static void mvpp2_cls_flow_last_set(struct mvpp2_cls_flow_entry *fe,
				    bool is_last)
{
	fe->data[0] &= ~MVPP2_CLS_FLOW_TBL0_LAST;
	fe->data[0] |= !!is_last;
}

static void mvpp2_cls_flow_pri_set(struct mvpp2_cls_flow_entry *fe, int prio)
{
	fe->data[1] &= ~MVPP2_CLS_FLOW_TBL1_PRIO(MVPP2_CLS_FLOW_TBL1_PRIO_MASK);
	fe->data[1] |= MVPP2_CLS_FLOW_TBL1_PRIO(prio);
}

static void mvpp2_cls_flow_port_add(struct mvpp2_cls_flow_entry *fe,
				    u32 port)
{
	fe->data[0] |= MVPP2_CLS_FLOW_TBL0_PORT_ID(port);
}

static void mvpp2_cls_flow_port_remove(struct mvpp2_cls_flow_entry *fe,
				       u32 port)
{
	fe->data[0] &= ~MVPP2_CLS_FLOW_TBL0_PORT_ID(port);
}

static void mvpp2_cls_flow_lu_type_set(struct mvpp2_cls_flow_entry *fe,
				       u8 lu_type)
{
	fe->data[1] &= ~MVPP2_CLS_FLOW_TBL1_LU_TYPE(MVPP2_CLS_LU_TYPE_MASK);
	fe->data[1] |= MVPP2_CLS_FLOW_TBL1_LU_TYPE(lu_type);
}

/* Initialize the parser entry for the given flow */
static void mvpp2_cls_flow_prs_init(struct mvpp2 *priv,
				    const struct mvpp2_cls_flow *flow)
{
	mvpp2_prs_add_flow(priv, flow->flow_id, flow->prs_ri.ri,
			   flow->prs_ri.ri_mask);
}

/* Initialize the Lookup Id table entry for the given flow */
static void mvpp2_cls_flow_lkp_init(struct mvpp2 *priv,
				    const struct mvpp2_cls_flow *flow)
{
	struct mvpp2_cls_lookup_entry le;

	le.way = 0;
	le.lkpid = flow->flow_id;

	/* The default RxQ for this port is set in the C2 lookup */
	le.data = 0;

	/* We point on the first lookup in the sequence for the flow, that is
	 * the C2 lookup.
	 */
	le.data |= MVPP2_CLS_LKP_FLOW_PTR(MVPP2_CLS_FLT_FIRST(flow->flow_id));

	/* CLS is always enabled, RSS is enabled/disabled in C2 lookup */
	le.data |= MVPP2_CLS_LKP_TBL_LOOKUP_EN_MASK;

	mvpp2_cls_lookup_write(priv, &le);
}

static void mvpp2_cls_c2_write(struct mvpp2 *priv,
			       struct mvpp2_cls_c2_entry *c2)
{
	u32 val;
	mvpp2_write(priv, MVPP22_CLS_C2_TCAM_IDX, c2->index);

	val = mvpp2_read(priv, MVPP22_CLS_C2_TCAM_INV);
	if (c2->valid)
		val &= ~MVPP22_CLS_C2_TCAM_INV_BIT;
	else
		val |= MVPP22_CLS_C2_TCAM_INV_BIT;
	mvpp2_write(priv, MVPP22_CLS_C2_TCAM_INV, val);

	mvpp2_write(priv, MVPP22_CLS_C2_ACT, c2->act);

	mvpp2_write(priv, MVPP22_CLS_C2_ATTR0, c2->attr[0]);
	mvpp2_write(priv, MVPP22_CLS_C2_ATTR1, c2->attr[1]);
	mvpp2_write(priv, MVPP22_CLS_C2_ATTR2, c2->attr[2]);
	mvpp2_write(priv, MVPP22_CLS_C2_ATTR3, c2->attr[3]);

	mvpp2_write(priv, MVPP22_CLS_C2_TCAM_DATA0, c2->tcam[0]);
	mvpp2_write(priv, MVPP22_CLS_C2_TCAM_DATA1, c2->tcam[1]);
	mvpp2_write(priv, MVPP22_CLS_C2_TCAM_DATA2, c2->tcam[2]);
	mvpp2_write(priv, MVPP22_CLS_C2_TCAM_DATA3, c2->tcam[3]);
	/* Writing TCAM_DATA4 flushes writes to TCAM_DATA0-4 and INV to HW */
	mvpp2_write(priv, MVPP22_CLS_C2_TCAM_DATA4, c2->tcam[4]);
}

void mvpp2_cls_c2_read(struct mvpp2 *priv, int index,
		       struct mvpp2_cls_c2_entry *c2)
{
	u32 val;
	mvpp2_write(priv, MVPP22_CLS_C2_TCAM_IDX, index);

	c2->index = index;

	c2->tcam[0] = mvpp2_read(priv, MVPP22_CLS_C2_TCAM_DATA0);
	c2->tcam[1] = mvpp2_read(priv, MVPP22_CLS_C2_TCAM_DATA1);
	c2->tcam[2] = mvpp2_read(priv, MVPP22_CLS_C2_TCAM_DATA2);
	c2->tcam[3] = mvpp2_read(priv, MVPP22_CLS_C2_TCAM_DATA3);
	c2->tcam[4] = mvpp2_read(priv, MVPP22_CLS_C2_TCAM_DATA4);

	c2->act = mvpp2_read(priv, MVPP22_CLS_C2_ACT);

	c2->attr[0] = mvpp2_read(priv, MVPP22_CLS_C2_ATTR0);
	c2->attr[1] = mvpp2_read(priv, MVPP22_CLS_C2_ATTR1);
	c2->attr[2] = mvpp2_read(priv, MVPP22_CLS_C2_ATTR2);
	c2->attr[3] = mvpp2_read(priv, MVPP22_CLS_C2_ATTR3);

	val = mvpp2_read(priv, MVPP22_CLS_C2_TCAM_INV);
	c2->valid = !(val & MVPP22_CLS_C2_TCAM_INV_BIT);
}

static int mvpp2_cls_ethtool_flow_to_type(int flow_type)
{
	switch (flow_type & ~(FLOW_EXT | FLOW_MAC_EXT | FLOW_RSS)) {
	case TCP_V4_FLOW:
		return MVPP22_FLOW_TCP4;
	case TCP_V6_FLOW:
		return MVPP22_FLOW_TCP6;
	case UDP_V4_FLOW:
		return MVPP22_FLOW_UDP4;
	case UDP_V6_FLOW:
		return MVPP22_FLOW_UDP6;
	case IPV4_FLOW:
		return MVPP22_FLOW_IP4;
	case IPV6_FLOW:
		return MVPP22_FLOW_IP6;
	default:
		return -EOPNOTSUPP;
	}
}

static int mvpp2_cls_c2_port_flow_index(struct mvpp2_port *port, int loc)
{
	return MVPP22_CLS_C2_RFS_LOC(port->id, loc);
}

/* Initialize the flow table entries for the given flow */
static void mvpp2_cls_flow_init(struct mvpp2 *priv,
				const struct mvpp2_cls_flow *flow)
{
	struct mvpp2_cls_flow_entry fe;
	int i, pri = 0;

	/* Assign default values to all entries in the flow */
	for (i = MVPP2_CLS_FLT_FIRST(flow->flow_id);
	     i <= MVPP2_CLS_FLT_LAST(flow->flow_id); i++) {
		memset(&fe, 0, sizeof(fe));
		fe.index = i;
		mvpp2_cls_flow_pri_set(&fe, pri++);

		if (i == MVPP2_CLS_FLT_LAST(flow->flow_id))
			mvpp2_cls_flow_last_set(&fe, 1);

		mvpp2_cls_flow_write(priv, &fe);
	}

	/* RSS config C2 lookup */
	mvpp2_cls_flow_read(priv, MVPP2_CLS_FLT_C2_RSS_ENTRY(flow->flow_id),
			    &fe);

	mvpp2_cls_flow_eng_set(&fe, MVPP22_CLS_ENGINE_C2);
	mvpp2_cls_flow_port_id_sel(&fe, true);
	mvpp2_cls_flow_lu_type_set(&fe, MVPP22_CLS_LU_TYPE_ALL);

	/* Add all ports */
	for (i = 0; i < MVPP2_MAX_PORTS; i++)
		mvpp2_cls_flow_port_add(&fe, BIT(i));

	mvpp2_cls_flow_write(priv, &fe);

	/* C3Hx lookups */
	for (i = 0; i < MVPP2_MAX_PORTS; i++) {
		mvpp2_cls_flow_read(priv,
				    MVPP2_CLS_FLT_HASH_ENTRY(i, flow->flow_id),
				    &fe);

		/* Set a default engine. Will be overwritten when setting the
		 * real HEK parameters
		 */
		mvpp2_cls_flow_eng_set(&fe, MVPP22_CLS_ENGINE_C3HA);
		mvpp2_cls_flow_port_id_sel(&fe, true);
		mvpp2_cls_flow_port_add(&fe, BIT(i));

		mvpp2_cls_flow_write(priv, &fe);
	}
}

/* Adds a field to the Header Extracted Key generation parameters*/
static int mvpp2_flow_add_hek_field(struct mvpp2_cls_flow_entry *fe,
				    u32 field_id)
{
	int nb_fields = mvpp2_cls_flow_hek_num_get(fe);

	if (nb_fields == MVPP2_FLOW_N_FIELDS)
		return -EINVAL;

	mvpp2_cls_flow_hek_set(fe, nb_fields, field_id);

	mvpp2_cls_flow_hek_num_set(fe, nb_fields + 1);

	return 0;
}

static int mvpp2_flow_set_hek_fields(struct mvpp2_cls_flow_entry *fe,
				     unsigned long hash_opts)
{
	u32 field_id;
	int i;

	/* Clear old fields */
	mvpp2_cls_flow_hek_num_set(fe, 0);
	fe->data[2] = 0;

	for_each_set_bit(i, &hash_opts, MVPP22_CLS_HEK_N_FIELDS) {
		switch (BIT(i)) {
		case MVPP22_CLS_HEK_OPT_MAC_DA:
			field_id = MVPP22_CLS_FIELD_MAC_DA;
			break;
		case MVPP22_CLS_HEK_OPT_VLAN:
			field_id = MVPP22_CLS_FIELD_VLAN;
			break;
		case MVPP22_CLS_HEK_OPT_VLAN_PRI:
			field_id = MVPP22_CLS_FIELD_VLAN_PRI;
			break;
		case MVPP22_CLS_HEK_OPT_IP4SA:
			field_id = MVPP22_CLS_FIELD_IP4SA;
			break;
		case MVPP22_CLS_HEK_OPT_IP4DA:
			field_id = MVPP22_CLS_FIELD_IP4DA;
			break;
		case MVPP22_CLS_HEK_OPT_IP6SA:
			field_id = MVPP22_CLS_FIELD_IP6SA;
			break;
		case MVPP22_CLS_HEK_OPT_IP6DA:
			field_id = MVPP22_CLS_FIELD_IP6DA;
			break;
		case MVPP22_CLS_HEK_OPT_L4SIP:
			field_id = MVPP22_CLS_FIELD_L4SIP;
			break;
		case MVPP22_CLS_HEK_OPT_L4DIP:
			field_id = MVPP22_CLS_FIELD_L4DIP;
			break;
		default:
			return -EINVAL;
		}
		if (mvpp2_flow_add_hek_field(fe, field_id))
			return -EINVAL;
	}

	return 0;
}

/* Returns the size, in bits, of the corresponding HEK field */
static int mvpp2_cls_hek_field_size(u32 field)
{
	switch (field) {
	case MVPP22_CLS_HEK_OPT_MAC_DA:
		return 48;
	case MVPP22_CLS_HEK_OPT_VLAN:
		return 12;
	case MVPP22_CLS_HEK_OPT_VLAN_PRI:
		return 3;
	case MVPP22_CLS_HEK_OPT_IP4SA:
	case MVPP22_CLS_HEK_OPT_IP4DA:
		return 32;
	case MVPP22_CLS_HEK_OPT_IP6SA:
	case MVPP22_CLS_HEK_OPT_IP6DA:
		return 128;
	case MVPP22_CLS_HEK_OPT_L4SIP:
	case MVPP22_CLS_HEK_OPT_L4DIP:
		return 16;
	default:
		return -1;
	}
}

const struct mvpp2_cls_flow *mvpp2_cls_flow_get(int flow)
{
	if (flow >= MVPP2_N_PRS_FLOWS)
		return NULL;

	return &cls_flows[flow];
}

/* Set the hash generation options for the given traffic flow.
 * One traffic flow (in the ethtool sense) has multiple classification flows,
 * to handle specific cases such as fragmentation, or the presence of a
 * VLAN / DSA Tag.
 *
 * Each of these individual flows has different constraints, for example we
 * can't hash fragmented packets on L4 data (else we would risk having packet
 * re-ordering), so each classification flows masks the options with their
 * supported ones.
 *
 */
static int mvpp2_port_rss_hash_opts_set(struct mvpp2_port *port, int flow_type,
					u16 requested_opts)
{
	const struct mvpp2_cls_flow *flow;
	struct mvpp2_cls_flow_entry fe;
	int i, engine, flow_index;
	u16 hash_opts;

	for_each_cls_flow_id_with_type(i, flow_type) {
		flow = mvpp2_cls_flow_get(i);
		if (!flow)
			return -EINVAL;

		flow_index = MVPP2_CLS_FLT_HASH_ENTRY(port->id, flow->flow_id);

		mvpp2_cls_flow_read(port->priv, flow_index, &fe);

		hash_opts = flow->supported_hash_opts & requested_opts;

		/* Use C3HB engine to access L4 infos. This adds L4 infos to the
		 * hash parameters
		 */
		if (hash_opts & MVPP22_CLS_HEK_L4_OPTS)
			engine = MVPP22_CLS_ENGINE_C3HB;
		else
			engine = MVPP22_CLS_ENGINE_C3HA;

		if (mvpp2_flow_set_hek_fields(&fe, hash_opts))
			return -EINVAL;

		mvpp2_cls_flow_eng_set(&fe, engine);

		mvpp2_cls_flow_write(port->priv, &fe);
	}

	return 0;
}

u16 mvpp2_flow_get_hek_fields(struct mvpp2_cls_flow_entry *fe)
{
	u16 hash_opts = 0;
	int n_fields, i, field;

	n_fields = mvpp2_cls_flow_hek_num_get(fe);

	for (i = 0; i < n_fields; i++) {
		field = mvpp2_cls_flow_hek_get(fe, i);

		switch (field) {
		case MVPP22_CLS_FIELD_MAC_DA:
			hash_opts |= MVPP22_CLS_HEK_OPT_MAC_DA;
			break;
		case MVPP22_CLS_FIELD_VLAN:
			hash_opts |= MVPP22_CLS_HEK_OPT_VLAN;
			break;
		case MVPP22_CLS_FIELD_VLAN_PRI:
			hash_opts |= MVPP22_CLS_HEK_OPT_VLAN_PRI;
			break;
		case MVPP22_CLS_FIELD_L3_PROTO:
			hash_opts |= MVPP22_CLS_HEK_OPT_L3_PROTO;
			break;
		case MVPP22_CLS_FIELD_IP4SA:
			hash_opts |= MVPP22_CLS_HEK_OPT_IP4SA;
			break;
		case MVPP22_CLS_FIELD_IP4DA:
			hash_opts |= MVPP22_CLS_HEK_OPT_IP4DA;
			break;
		case MVPP22_CLS_FIELD_IP6SA:
			hash_opts |= MVPP22_CLS_HEK_OPT_IP6SA;
			break;
		case MVPP22_CLS_FIELD_IP6DA:
			hash_opts |= MVPP22_CLS_HEK_OPT_IP6DA;
			break;
		case MVPP22_CLS_FIELD_L4SIP:
			hash_opts |= MVPP22_CLS_HEK_OPT_L4SIP;
			break;
		case MVPP22_CLS_FIELD_L4DIP:
			hash_opts |= MVPP22_CLS_HEK_OPT_L4DIP;
			break;
		default:
			break;
		}
	}
	return hash_opts;
}

/* Returns the hash opts for this flow. There are several classifier flows
 * for one traffic flow, this returns an aggregation of all configurations.
 */
static u16 mvpp2_port_rss_hash_opts_get(struct mvpp2_port *port, int flow_type)
{
	const struct mvpp2_cls_flow *flow;
	struct mvpp2_cls_flow_entry fe;
	int i, flow_index;
	u16 hash_opts = 0;

	for_each_cls_flow_id_with_type(i, flow_type) {
		flow = mvpp2_cls_flow_get(i);
		if (!flow)
			return 0;

		flow_index = MVPP2_CLS_FLT_HASH_ENTRY(port->id, flow->flow_id);

		mvpp2_cls_flow_read(port->priv, flow_index, &fe);

		hash_opts |= mvpp2_flow_get_hek_fields(&fe);
	}

	return hash_opts;
}

static void mvpp2_cls_port_init_flows(struct mvpp2 *priv)
{
	const struct mvpp2_cls_flow *flow;
	int i;

	for (i = 0; i < MVPP2_N_PRS_FLOWS; i++) {
		flow = mvpp2_cls_flow_get(i);
		if (!flow)
			break;

		mvpp2_cls_flow_prs_init(priv, flow);
		mvpp2_cls_flow_lkp_init(priv, flow);
		mvpp2_cls_flow_init(priv, flow);
	}
}

static void mvpp2_port_c2_cls_init(struct mvpp2_port *port)
{
	struct mvpp2_cls_c2_entry c2;
	u8 qh, ql, pmap;

	memset(&c2, 0, sizeof(c2));

	c2.index = MVPP22_CLS_C2_RSS_ENTRY(port->id);

	pmap = BIT(port->id);
	c2.tcam[4] = MVPP22_CLS_C2_PORT_ID(pmap);
	c2.tcam[4] |= MVPP22_CLS_C2_TCAM_EN(MVPP22_CLS_C2_PORT_ID(pmap));

	/* Match on Lookup Type */
	c2.tcam[4] |= MVPP22_CLS_C2_TCAM_EN(MVPP22_CLS_C2_LU_TYPE(MVPP2_CLS_LU_TYPE_MASK));
	c2.tcam[4] |= MVPP22_CLS_C2_LU_TYPE(MVPP22_CLS_LU_TYPE_ALL);

	/* Update RSS status after matching this entry */
	c2.act = MVPP22_CLS_C2_ACT_RSS_EN(MVPP22_C2_UPD_LOCK);

	/* Mark packet as "forwarded to software", needed for RSS */
	c2.act |= MVPP22_CLS_C2_ACT_FWD(MVPP22_C2_FWD_SW_LOCK);

	/* Configure the default rx queue : Update Queue Low and Queue High, but
	 * don't lock, since the rx queue selection might be overridden by RSS
	 */
	c2.act |= MVPP22_CLS_C2_ACT_QHIGH(MVPP22_C2_UPD) |
		   MVPP22_CLS_C2_ACT_QLOW(MVPP22_C2_UPD);

	qh = (port->first_rxq >> 3) & MVPP22_CLS_C2_ATTR0_QHIGH_MASK;
	ql = port->first_rxq & MVPP22_CLS_C2_ATTR0_QLOW_MASK;

	c2.attr[0] = MVPP22_CLS_C2_ATTR0_QHIGH(qh) |
		      MVPP22_CLS_C2_ATTR0_QLOW(ql);

	c2.valid = true;

	mvpp2_cls_c2_write(port->priv, &c2);
}

/* Classifier default initialization */
void mvpp2_cls_init(struct mvpp2 *priv)
{
	struct mvpp2_cls_lookup_entry le;
	struct mvpp2_cls_flow_entry fe;
	struct mvpp2_cls_c2_entry c2;
	int index;

	/* Enable classifier */
	mvpp2_write(priv, MVPP2_CLS_MODE_REG, MVPP2_CLS_MODE_ACTIVE_MASK);

	/* Clear classifier flow table */
	memset(&fe.data, 0, sizeof(fe.data));
	for (index = 0; index < MVPP2_CLS_FLOWS_TBL_SIZE; index++) {
		fe.index = index;
		mvpp2_cls_flow_write(priv, &fe);
	}

	/* Clear classifier lookup table */
	le.data = 0;
	for (index = 0; index < MVPP2_CLS_LKP_TBL_SIZE; index++) {
		le.lkpid = index;
		le.way = 0;
		mvpp2_cls_lookup_write(priv, &le);

		le.way = 1;
		mvpp2_cls_lookup_write(priv, &le);
	}

	/* Clear C2 TCAM engine table */
	memset(&c2, 0, sizeof(c2));
	c2.valid = false;
	for (index = 0; index < MVPP22_CLS_C2_N_ENTRIES; index++) {
		c2.index = index;
		mvpp2_cls_c2_write(priv, &c2);
	}

	/* Disable the FIFO stages in C2 engine, which are only used in BIST
	 * mode
	 */
	mvpp2_write(priv, MVPP22_CLS_C2_TCAM_CTRL,
		    MVPP22_CLS_C2_TCAM_BYPASS_FIFO);

	mvpp2_cls_port_init_flows(priv);
}

void mvpp2_cls_port_config(struct mvpp2_port *port)
{
	struct mvpp2_cls_lookup_entry le;
	u32 val;

	/* Set way for the port */
	val = mvpp2_read(port->priv, MVPP2_CLS_PORT_WAY_REG);
	val &= ~MVPP2_CLS_PORT_WAY_MASK(port->id);
	mvpp2_write(port->priv, MVPP2_CLS_PORT_WAY_REG, val);

	/* Pick the entry to be accessed in lookup ID decoding table
	 * according to the way and lkpid.
	 */
	le.lkpid = port->id;
	le.way = 0;
	le.data = 0;

	/* Set initial CPU queue for receiving packets */
	le.data &= ~MVPP2_CLS_LKP_TBL_RXQ_MASK;
	le.data |= port->first_rxq;

	/* Disable classification engines */
	le.data &= ~MVPP2_CLS_LKP_TBL_LOOKUP_EN_MASK;

	/* Update lookup ID table entry */
	mvpp2_cls_lookup_write(port->priv, &le);

	mvpp2_port_c2_cls_init(port);
}

u32 mvpp2_cls_c2_hit_count(struct mvpp2 *priv, int c2_index)
{
	mvpp2_write(priv, MVPP22_CLS_C2_TCAM_IDX, c2_index);

	return mvpp2_read(priv, MVPP22_CLS_C2_HIT_CTR);
}

static void mvpp2_rss_port_c2_enable(struct mvpp2_port *port, u32 ctx)
{
	struct mvpp2_cls_c2_entry c2;
	u8 qh, ql;

	mvpp2_cls_c2_read(port->priv, MVPP22_CLS_C2_RSS_ENTRY(port->id), &c2);

	/* The RxQ number is used to select the RSS table. It that case, we set
	 * it to be the ctx number.
	 */
	qh = (ctx >> 3) & MVPP22_CLS_C2_ATTR0_QHIGH_MASK;
	ql = ctx & MVPP22_CLS_C2_ATTR0_QLOW_MASK;

	c2.attr[0] = MVPP22_CLS_C2_ATTR0_QHIGH(qh) |
		     MVPP22_CLS_C2_ATTR0_QLOW(ql);

	c2.attr[2] |= MVPP22_CLS_C2_ATTR2_RSS_EN;

	mvpp2_cls_c2_write(port->priv, &c2);
}

static void mvpp2_rss_port_c2_disable(struct mvpp2_port *port)
{
	struct mvpp2_cls_c2_entry c2;
	u8 qh, ql;

	mvpp2_cls_c2_read(port->priv, MVPP22_CLS_C2_RSS_ENTRY(port->id), &c2);

	/* Reset the default destination RxQ to the port's first rx queue. */
	qh = (port->first_rxq >> 3) & MVPP22_CLS_C2_ATTR0_QHIGH_MASK;
	ql = port->first_rxq & MVPP22_CLS_C2_ATTR0_QLOW_MASK;

	c2.attr[0] = MVPP22_CLS_C2_ATTR0_QHIGH(qh) |
		      MVPP22_CLS_C2_ATTR0_QLOW(ql);

	c2.attr[2] &= ~MVPP22_CLS_C2_ATTR2_RSS_EN;

	mvpp2_cls_c2_write(port->priv, &c2);
}

static inline int mvpp22_rss_ctx(struct mvpp2_port *port, int port_rss_ctx)
{
	return port->rss_ctx[port_rss_ctx];
}

int mvpp22_port_rss_enable(struct mvpp2_port *port)
{
	if (mvpp22_rss_ctx(port, 0) < 0)
		return -EINVAL;

	mvpp2_rss_port_c2_enable(port, mvpp22_rss_ctx(port, 0));

	return 0;
}

int mvpp22_port_rss_disable(struct mvpp2_port *port)
{
	if (mvpp22_rss_ctx(port, 0) < 0)
		return -EINVAL;

	mvpp2_rss_port_c2_disable(port);

	return 0;
}

static void mvpp22_port_c2_lookup_disable(struct mvpp2_port *port, int entry)
{
	struct mvpp2_cls_c2_entry c2;

	mvpp2_cls_c2_read(port->priv, entry, &c2);

	/* Clear the port map so that the entry doesn't match anymore */
	c2.tcam[4] &= ~(MVPP22_CLS_C2_PORT_ID(BIT(port->id)));

	mvpp2_cls_c2_write(port->priv, &c2);
}

/* Set CPU queue number for oversize packets */
void mvpp2_cls_oversize_rxq_set(struct mvpp2_port *port)
{
	u32 val;

	mvpp2_write(port->priv, MVPP2_CLS_OVERSIZE_RXQ_LOW_REG(port->id),
		    port->first_rxq & MVPP2_CLS_OVERSIZE_RXQ_LOW_MASK);

	mvpp2_write(port->priv, MVPP2_CLS_SWFWD_P2HQ_REG(port->id),
		    (port->first_rxq >> MVPP2_CLS_OVERSIZE_RXQ_LOW_BITS));

	val = mvpp2_read(port->priv, MVPP2_CLS_SWFWD_PCTRL_REG);
	val |= MVPP2_CLS_SWFWD_PCTRL_MASK(port->id);
	mvpp2_write(port->priv, MVPP2_CLS_SWFWD_PCTRL_REG, val);
}

static int mvpp2_port_c2_tcam_rule_add(struct mvpp2_port *port,
				       struct mvpp2_rfs_rule *rule)
{
	struct flow_action_entry *act;
	struct mvpp2_cls_c2_entry c2;
	u8 qh, ql, pmap;
	int index, ctx;

	memset(&c2, 0, sizeof(c2));

	index = mvpp2_cls_c2_port_flow_index(port, rule->loc);
	if (index < 0)
		return -EINVAL;
	c2.index = index;

	act = &rule->flow->action.entries[0];

	rule->c2_index = c2.index;

	c2.tcam[3] = (rule->c2_tcam & 0xffff) |
		     ((rule->c2_tcam_mask & 0xffff) << 16);
	c2.tcam[2] = ((rule->c2_tcam >> 16) & 0xffff) |
		     (((rule->c2_tcam_mask >> 16) & 0xffff) << 16);
	c2.tcam[1] = ((rule->c2_tcam >> 32) & 0xffff) |
		     (((rule->c2_tcam_mask >> 32) & 0xffff) << 16);
	c2.tcam[0] = ((rule->c2_tcam >> 48) & 0xffff) |
		     (((rule->c2_tcam_mask >> 48) & 0xffff) << 16);

	pmap = BIT(port->id);
	c2.tcam[4] = MVPP22_CLS_C2_PORT_ID(pmap);
	c2.tcam[4] |= MVPP22_CLS_C2_TCAM_EN(MVPP22_CLS_C2_PORT_ID(pmap));

	/* Match on Lookup Type */
	c2.tcam[4] |= MVPP22_CLS_C2_TCAM_EN(MVPP22_CLS_C2_LU_TYPE(MVPP2_CLS_LU_TYPE_MASK));
	c2.tcam[4] |= MVPP22_CLS_C2_LU_TYPE(rule->loc);

	if (act->id == FLOW_ACTION_DROP) {
		c2.act = MVPP22_CLS_C2_ACT_COLOR(MVPP22_C2_COL_RED_LOCK);
	} else {
		/* We want to keep the default color derived from the Header
		 * Parser drop entries, for VLAN and MAC filtering. This will
		 * assign a default color of Green or Red, and we want matches
		 * with a non-drop action to keep that color.
		 */
		c2.act = MVPP22_CLS_C2_ACT_COLOR(MVPP22_C2_COL_NO_UPD_LOCK);

		/* Update RSS status after matching this entry */
		if (act->queue.ctx)
			c2.attr[2] |= MVPP22_CLS_C2_ATTR2_RSS_EN;

		/* Always lock the RSS_EN decision. We might have high prio
		 * rules steering to an RXQ, and a lower one steering to RSS,
		 * we don't want the low prio RSS rule overwriting this flag.
		 */
		c2.act = MVPP22_CLS_C2_ACT_RSS_EN(MVPP22_C2_UPD_LOCK);

		/* Mark packet as "forwarded to software", needed for RSS */
		c2.act |= MVPP22_CLS_C2_ACT_FWD(MVPP22_C2_FWD_SW_LOCK);

		c2.act |= MVPP22_CLS_C2_ACT_QHIGH(MVPP22_C2_UPD_LOCK) |
			   MVPP22_CLS_C2_ACT_QLOW(MVPP22_C2_UPD_LOCK);

		if (act->queue.ctx) {
			/* Get the global ctx number */
			ctx = mvpp22_rss_ctx(port, act->queue.ctx);
			if (ctx < 0)
				return -EINVAL;

			qh = (ctx >> 3) & MVPP22_CLS_C2_ATTR0_QHIGH_MASK;
			ql = ctx & MVPP22_CLS_C2_ATTR0_QLOW_MASK;
		} else {
			qh = ((act->queue.index + port->first_rxq) >> 3) &
			      MVPP22_CLS_C2_ATTR0_QHIGH_MASK;
			ql = (act->queue.index + port->first_rxq) &
			      MVPP22_CLS_C2_ATTR0_QLOW_MASK;
		}

		c2.attr[0] = MVPP22_CLS_C2_ATTR0_QHIGH(qh) |
			      MVPP22_CLS_C2_ATTR0_QLOW(ql);
	}

	c2.valid = true;

	mvpp2_cls_c2_write(port->priv, &c2);

	return 0;
}

static int mvpp2_port_c2_rfs_rule_insert(struct mvpp2_port *port,
					 struct mvpp2_rfs_rule *rule)
{
	return mvpp2_port_c2_tcam_rule_add(port, rule);
}

static int mvpp2_port_cls_rfs_rule_remove(struct mvpp2_port *port,
					  struct mvpp2_rfs_rule *rule)
{
	const struct mvpp2_cls_flow *flow;
	struct mvpp2_cls_flow_entry fe;
	int index, i;

	for_each_cls_flow_id_containing_type(i, rule->flow_type) {
		flow = mvpp2_cls_flow_get(i);
		if (!flow)
			return 0;

		index = MVPP2_CLS_FLT_C2_RFS(port->id, flow->flow_id, rule->loc);

		mvpp2_cls_flow_read(port->priv, index, &fe);
		mvpp2_cls_flow_port_remove(&fe, BIT(port->id));
		mvpp2_cls_flow_write(port->priv, &fe);
	}

	if (rule->c2_index >= 0)
		mvpp22_port_c2_lookup_disable(port, rule->c2_index);

	return 0;
}

static int mvpp2_port_flt_rfs_rule_insert(struct mvpp2_port *port,
					  struct mvpp2_rfs_rule *rule)
{
	const struct mvpp2_cls_flow *flow;
	struct mvpp2 *priv = port->priv;
	struct mvpp2_cls_flow_entry fe;
	int index, ret, i;

	if (rule->engine != MVPP22_CLS_ENGINE_C2)
		return -EOPNOTSUPP;

	ret = mvpp2_port_c2_rfs_rule_insert(port, rule);
	if (ret)
		return ret;

	for_each_cls_flow_id_containing_type(i, rule->flow_type) {
		flow = mvpp2_cls_flow_get(i);
		if (!flow)
			return 0;

		if ((rule->hek_fields & flow->supported_hash_opts) != rule->hek_fields)
			continue;

		index = MVPP2_CLS_FLT_C2_RFS(port->id, flow->flow_id, rule->loc);

		mvpp2_cls_flow_read(priv, index, &fe);
		mvpp2_cls_flow_eng_set(&fe, rule->engine);
		mvpp2_cls_flow_port_id_sel(&fe, true);
		mvpp2_flow_set_hek_fields(&fe, rule->hek_fields);
		mvpp2_cls_flow_lu_type_set(&fe, rule->loc);
		mvpp2_cls_flow_port_add(&fe, 0xf);

		mvpp2_cls_flow_write(priv, &fe);
	}

	return 0;
}

static int mvpp2_cls_c2_build_match(struct mvpp2_rfs_rule *rule)
{
	struct flow_rule *flow = rule->flow;
	int offs = 0;

	/* The order of insertion in C2 tcam must match the order in which
	 * the fields are found in the header
	 */
	if (flow_rule_match_key(flow, FLOW_DISSECTOR_KEY_VLAN)) {
		struct flow_match_vlan match;

		flow_rule_match_vlan(flow, &match);
		if (match.mask->vlan_id) {
			rule->hek_fields |= MVPP22_CLS_HEK_OPT_VLAN;

			rule->c2_tcam |= ((u64)match.key->vlan_id) << offs;
			rule->c2_tcam_mask |= ((u64)match.mask->vlan_id) << offs;

			/* Don't update the offset yet */
		}

		if (match.mask->vlan_priority) {
			rule->hek_fields |= MVPP22_CLS_HEK_OPT_VLAN_PRI;

			/* VLAN pri is always at offset 13 relative to the
			 * current offset
			 */
			rule->c2_tcam |= ((u64)match.key->vlan_priority) <<
				(offs + 13);
			rule->c2_tcam_mask |= ((u64)match.mask->vlan_priority) <<
				(offs + 13);
		}

		if (match.mask->vlan_dei)
			return -EOPNOTSUPP;

		/* vlan id and prio always seem to take a full 16-bit slot in
		 * the Header Extracted Key.
		 */
		offs += 16;
	}

	if (flow_rule_match_key(flow, FLOW_DISSECTOR_KEY_PORTS)) {
		struct flow_match_ports match;

		flow_rule_match_ports(flow, &match);
		if (match.mask->src) {
			rule->hek_fields |= MVPP22_CLS_HEK_OPT_L4SIP;

			rule->c2_tcam |= ((u64)ntohs(match.key->src)) << offs;
			rule->c2_tcam_mask |= ((u64)ntohs(match.mask->src)) << offs;
			offs += mvpp2_cls_hek_field_size(MVPP22_CLS_HEK_OPT_L4SIP);
		}

		if (match.mask->dst) {
			rule->hek_fields |= MVPP22_CLS_HEK_OPT_L4DIP;

			rule->c2_tcam |= ((u64)ntohs(match.key->dst)) << offs;
			rule->c2_tcam_mask |= ((u64)ntohs(match.mask->dst)) << offs;
			offs += mvpp2_cls_hek_field_size(MVPP22_CLS_HEK_OPT_L4DIP);
		}
	}

	if (hweight16(rule->hek_fields) > MVPP2_FLOW_N_FIELDS)
		return -EOPNOTSUPP;

	return 0;
}

static int mvpp2_cls_rfs_parse_rule(struct mvpp2_rfs_rule *rule)
{
	struct flow_rule *flow = rule->flow;
	struct flow_action_entry *act;

	act = &flow->action.entries[0];
	if (act->id != FLOW_ACTION_QUEUE && act->id != FLOW_ACTION_DROP)
		return -EOPNOTSUPP;

	/* When both an RSS context and an queue index are set, the index
	 * is considered as an offset to be added to the indirection table
	 * entries. We don't support this, so reject this rule.
	 */
	if (act->queue.ctx && act->queue.index)
		return -EOPNOTSUPP;

	/* For now, only use the C2 engine which has a HEK size limited to 64
	 * bits for TCAM matching.
	 */
	rule->engine = MVPP22_CLS_ENGINE_C2;

	if (mvpp2_cls_c2_build_match(rule))
		return -EINVAL;

	return 0;
}

int mvpp2_ethtool_cls_rule_get(struct mvpp2_port *port,
			       struct ethtool_rxnfc *rxnfc)
{
	struct mvpp2_ethtool_fs *efs;

	if (rxnfc->fs.location >= MVPP2_N_RFS_ENTRIES_PER_FLOW)
		return -EINVAL;

	efs = port->rfs_rules[rxnfc->fs.location];
	if (!efs)
		return -ENOENT;

	memcpy(rxnfc, &efs->rxnfc, sizeof(efs->rxnfc));

	return 0;
}

int mvpp2_ethtool_cls_rule_ins(struct mvpp2_port *port,
			       struct ethtool_rxnfc *info)
{
	struct ethtool_rx_flow_spec_input input = {};
	struct ethtool_rx_flow_rule *ethtool_rule;
	struct mvpp2_ethtool_fs *efs, *old_efs;
	int ret = 0;

	if (info->fs.location >= MVPP2_N_RFS_ENTRIES_PER_FLOW)
		return -EINVAL;

	efs = kzalloc(sizeof(*efs), GFP_KERNEL);
	if (!efs)
		return -ENOMEM;

	input.fs = &info->fs;

	/* We need to manually set the rss_ctx, since this info isn't present
	 * in info->fs
	 */
	if (info->fs.flow_type & FLOW_RSS)
		input.rss_ctx = info->rss_context;

	ethtool_rule = ethtool_rx_flow_rule_create(&input);
	if (IS_ERR(ethtool_rule)) {
		ret = PTR_ERR(ethtool_rule);
		goto clean_rule;
	}

	efs->rule.flow = ethtool_rule->rule;
	efs->rule.flow_type = mvpp2_cls_ethtool_flow_to_type(info->fs.flow_type);
	if (efs->rule.flow_type < 0) {
		ret = efs->rule.flow_type;
		goto clean_rule;
	}

	ret = mvpp2_cls_rfs_parse_rule(&efs->rule);
	if (ret)
		goto clean_eth_rule;

	efs->rule.loc = info->fs.location;

	/* Replace an already existing rule */
	if (port->rfs_rules[efs->rule.loc]) {
		old_efs = port->rfs_rules[efs->rule.loc];
		ret = mvpp2_port_cls_rfs_rule_remove(port, &old_efs->rule);
		if (ret)
			goto clean_eth_rule;
		kfree(old_efs);
		port->n_rfs_rules--;
	}

	ret = mvpp2_port_flt_rfs_rule_insert(port, &efs->rule);
	if (ret)
		goto clean_eth_rule;

	ethtool_rx_flow_rule_destroy(ethtool_rule);
	efs->rule.flow = NULL;

	memcpy(&efs->rxnfc, info, sizeof(*info));
	port->rfs_rules[efs->rule.loc] = efs;
	port->n_rfs_rules++;

	return ret;

clean_eth_rule:
	ethtool_rx_flow_rule_destroy(ethtool_rule);
clean_rule:
	kfree(efs);
	return ret;
}

int mvpp2_ethtool_cls_rule_del(struct mvpp2_port *port,
			       struct ethtool_rxnfc *info)
{
	struct mvpp2_ethtool_fs *efs;
	int ret;

	efs = port->rfs_rules[info->fs.location];
	if (!efs)
		return -EINVAL;

	/* Remove the rule from the engines. */
	ret = mvpp2_port_cls_rfs_rule_remove(port, &efs->rule);
	if (ret)
		return ret;

	port->n_rfs_rules--;
	port->rfs_rules[info->fs.location] = NULL;
	kfree(efs);

	return 0;
}

static inline u32 mvpp22_rxfh_indir(struct mvpp2_port *port, u32 rxq)
{
	int nrxqs, cpu, cpus = num_possible_cpus();

	/* Number of RXQs per CPU */
	nrxqs = port->nrxqs / cpus;

	/* CPU that will handle this rx queue */
	cpu = rxq / nrxqs;

	if (!cpu_online(cpu))
		return port->first_rxq;

	/* Indirection to better distribute the paquets on the CPUs when
	 * configuring the RSS queues.
	 */
	return port->first_rxq + ((rxq * nrxqs + rxq / cpus) % port->nrxqs);
}

static void mvpp22_rss_fill_table(struct mvpp2_port *port,
				  struct mvpp2_rss_table *table,
				  u32 rss_ctx)
{
	struct mvpp2 *priv = port->priv;
	int i;

	for (i = 0; i < MVPP22_RSS_TABLE_ENTRIES; i++) {
		u32 sel = MVPP22_RSS_INDEX_TABLE(rss_ctx) |
			  MVPP22_RSS_INDEX_TABLE_ENTRY(i);
		mvpp2_write(priv, MVPP22_RSS_INDEX, sel);

		mvpp2_write(priv, MVPP22_RSS_TABLE_ENTRY,
			    mvpp22_rxfh_indir(port, table->indir[i]));
	}
}

static int mvpp22_rss_context_create(struct mvpp2_port *port, u32 *rss_ctx)
{
	struct mvpp2 *priv = port->priv;
	u32 ctx;

	/* Find the first free RSS table */
	for (ctx = 0; ctx < MVPP22_N_RSS_TABLES; ctx++) {
		if (!priv->rss_tables[ctx])
			break;
	}

	if (ctx == MVPP22_N_RSS_TABLES)
		return -EINVAL;

	priv->rss_tables[ctx] = kzalloc(sizeof(*priv->rss_tables[ctx]),
					GFP_KERNEL);
	if (!priv->rss_tables[ctx])
		return -ENOMEM;

	*rss_ctx = ctx;

	/* Set the table width: replace the whole classifier Rx queue number
	 * with the ones configured in RSS table entries.
	 */
	mvpp2_write(priv, MVPP22_RSS_INDEX, MVPP22_RSS_INDEX_TABLE(ctx));
	mvpp2_write(priv, MVPP22_RSS_WIDTH, 8);

	mvpp2_write(priv, MVPP22_RSS_INDEX, MVPP22_RSS_INDEX_QUEUE(ctx));
	mvpp2_write(priv, MVPP22_RXQ2RSS_TABLE, MVPP22_RSS_TABLE_POINTER(ctx));

	return 0;
}

int mvpp22_port_rss_ctx_create(struct mvpp2_port *port, u32 *port_ctx)
{
	u32 rss_ctx;
	int ret, i;

	ret = mvpp22_rss_context_create(port, &rss_ctx);
	if (ret)
		return ret;

	/* Find the first available context number in the port, starting from 1.
	 * Context 0 on each port is reserved for the default context.
	 */
	for (i = 1; i < MVPP22_N_RSS_TABLES; i++) {
		if (port->rss_ctx[i] < 0)
			break;
	}

	if (i == MVPP22_N_RSS_TABLES)
		return -EINVAL;

	port->rss_ctx[i] = rss_ctx;
	*port_ctx = i;

	return 0;
}

static struct mvpp2_rss_table *mvpp22_rss_table_get(struct mvpp2 *priv,
						    int rss_ctx)
{
	if (rss_ctx < 0 || rss_ctx >= MVPP22_N_RSS_TABLES)
		return NULL;

	return priv->rss_tables[rss_ctx];
}

int mvpp22_port_rss_ctx_delete(struct mvpp2_port *port, u32 port_ctx)
{
	struct mvpp2 *priv = port->priv;
	struct ethtool_rxnfc *rxnfc;
	int i, rss_ctx, ret;

	rss_ctx = mvpp22_rss_ctx(port, port_ctx);

	if (rss_ctx < 0 || rss_ctx >= MVPP22_N_RSS_TABLES)
		return -EINVAL;

	/* Invalidate any active classification rule that use this context */
	for (i = 0; i < MVPP2_N_RFS_ENTRIES_PER_FLOW; i++) {
		if (!port->rfs_rules[i])
			continue;

		rxnfc = &port->rfs_rules[i]->rxnfc;
		if (!(rxnfc->fs.flow_type & FLOW_RSS) ||
		    rxnfc->rss_context != port_ctx)
			continue;

		ret = mvpp2_ethtool_cls_rule_del(port, rxnfc);
		if (ret) {
			netdev_warn(port->dev,
				    "couldn't remove classification rule %d associated to this context",
				    rxnfc->fs.location);
		}
	}

	kfree(priv->rss_tables[rss_ctx]);

	priv->rss_tables[rss_ctx] = NULL;
	port->rss_ctx[port_ctx] = -1;

	return 0;
}

int mvpp22_port_rss_ctx_indir_set(struct mvpp2_port *port, u32 port_ctx,
				  const u32 *indir)
{
	int rss_ctx = mvpp22_rss_ctx(port, port_ctx);
	struct mvpp2_rss_table *rss_table = mvpp22_rss_table_get(port->priv,
								 rss_ctx);

	if (!rss_table)
		return -EINVAL;

	memcpy(rss_table->indir, indir,
	       MVPP22_RSS_TABLE_ENTRIES * sizeof(rss_table->indir[0]));

	mvpp22_rss_fill_table(port, rss_table, rss_ctx);

	return 0;
}

int mvpp22_port_rss_ctx_indir_get(struct mvpp2_port *port, u32 port_ctx,
				  u32 *indir)
{
	int rss_ctx =  mvpp22_rss_ctx(port, port_ctx);
	struct mvpp2_rss_table *rss_table = mvpp22_rss_table_get(port->priv,
								 rss_ctx);

	if (!rss_table)
		return -EINVAL;

	memcpy(indir, rss_table->indir,
	       MVPP22_RSS_TABLE_ENTRIES * sizeof(rss_table->indir[0]));

	return 0;
}

int mvpp2_ethtool_rxfh_set(struct mvpp2_port *port, struct ethtool_rxnfc *info)
{
	u16 hash_opts = 0;
	u32 flow_type;

	flow_type = mvpp2_cls_ethtool_flow_to_type(info->flow_type);

	switch (flow_type) {
	case MVPP22_FLOW_TCP4:
	case MVPP22_FLOW_UDP4:
	case MVPP22_FLOW_TCP6:
	case MVPP22_FLOW_UDP6:
		if (info->data & RXH_L4_B_0_1)
			hash_opts |= MVPP22_CLS_HEK_OPT_L4SIP;
		if (info->data & RXH_L4_B_2_3)
			hash_opts |= MVPP22_CLS_HEK_OPT_L4DIP;
		/* Fallthrough */
	case MVPP22_FLOW_IP4:
	case MVPP22_FLOW_IP6:
		if (info->data & RXH_L2DA)
			hash_opts |= MVPP22_CLS_HEK_OPT_MAC_DA;
		if (info->data & RXH_VLAN)
			hash_opts |= MVPP22_CLS_HEK_OPT_VLAN;
		if (info->data & RXH_L3_PROTO)
			hash_opts |= MVPP22_CLS_HEK_OPT_L3_PROTO;
		if (info->data & RXH_IP_SRC)
			hash_opts |= (MVPP22_CLS_HEK_OPT_IP4SA |
				     MVPP22_CLS_HEK_OPT_IP6SA);
		if (info->data & RXH_IP_DST)
			hash_opts |= (MVPP22_CLS_HEK_OPT_IP4DA |
				     MVPP22_CLS_HEK_OPT_IP6DA);
		break;
	default: return -EOPNOTSUPP;
	}

	return mvpp2_port_rss_hash_opts_set(port, flow_type, hash_opts);
}

int mvpp2_ethtool_rxfh_get(struct mvpp2_port *port, struct ethtool_rxnfc *info)
{
	unsigned long hash_opts;
	u32 flow_type;
	int i;

	flow_type = mvpp2_cls_ethtool_flow_to_type(info->flow_type);

	hash_opts = mvpp2_port_rss_hash_opts_get(port, flow_type);
	info->data = 0;

	for_each_set_bit(i, &hash_opts, MVPP22_CLS_HEK_N_FIELDS) {
		switch (BIT(i)) {
		case MVPP22_CLS_HEK_OPT_MAC_DA:
			info->data |= RXH_L2DA;
			break;
		case MVPP22_CLS_HEK_OPT_VLAN:
			info->data |= RXH_VLAN;
			break;
		case MVPP22_CLS_HEK_OPT_L3_PROTO:
			info->data |= RXH_L3_PROTO;
			break;
		case MVPP22_CLS_HEK_OPT_IP4SA:
		case MVPP22_CLS_HEK_OPT_IP6SA:
			info->data |= RXH_IP_SRC;
			break;
		case MVPP22_CLS_HEK_OPT_IP4DA:
		case MVPP22_CLS_HEK_OPT_IP6DA:
			info->data |= RXH_IP_DST;
			break;
		case MVPP22_CLS_HEK_OPT_L4SIP:
			info->data |= RXH_L4_B_0_1;
			break;
		case MVPP22_CLS_HEK_OPT_L4DIP:
			info->data |= RXH_L4_B_2_3;
			break;
		default:
			return -EINVAL;
		}
	}
	return 0;
}

int mvpp22_port_rss_init(struct mvpp2_port *port)
{
	struct mvpp2_rss_table *table;
	u32 context = 0;
	int i, ret;

	for (i = 0; i < MVPP22_N_RSS_TABLES; i++)
		port->rss_ctx[i] = -1;

	ret = mvpp22_rss_context_create(port, &context);
	if (ret)
		return ret;

	table = mvpp22_rss_table_get(port->priv, context);
	if (!table)
		return -EINVAL;

	port->rss_ctx[0] = context;

	/* Configure the first table to evenly distribute the packets across
	 * real Rx Queues. The table entries map a hash to a port Rx Queue.
	 */
	for (i = 0; i < MVPP22_RSS_TABLE_ENTRIES; i++)
		table->indir[i] = ethtool_rxfh_indir_default(i, port->nrxqs);

	mvpp22_rss_fill_table(port, table, mvpp22_rss_ctx(port, 0));

	/* Configure default flows */
	mvpp2_port_rss_hash_opts_set(port, MVPP22_FLOW_IP4, MVPP22_CLS_HEK_IP4_2T);
	mvpp2_port_rss_hash_opts_set(port, MVPP22_FLOW_IP6, MVPP22_CLS_HEK_IP6_2T);
	mvpp2_port_rss_hash_opts_set(port, MVPP22_FLOW_TCP4, MVPP22_CLS_HEK_IP4_5T);
	mvpp2_port_rss_hash_opts_set(port, MVPP22_FLOW_TCP6, MVPP22_CLS_HEK_IP6_5T);
	mvpp2_port_rss_hash_opts_set(port, MVPP22_FLOW_UDP4, MVPP22_CLS_HEK_IP4_5T);
	mvpp2_port_rss_hash_opts_set(port, MVPP22_FLOW_UDP6, MVPP22_CLS_HEK_IP6_5T);

	return 0;
}
