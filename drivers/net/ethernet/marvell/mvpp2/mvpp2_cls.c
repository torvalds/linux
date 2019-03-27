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
	MVPP2_DEF_FLOW(TCP_V4_FLOW, MVPP2_FL_IP4_TCP_NF_UNTAG,
		       MVPP22_CLS_HEK_IP4_5T,
		       MVPP2_PRS_RI_VLAN_NONE | MVPP2_PRS_RI_L3_IP4 |
		       MVPP2_PRS_RI_L4_TCP,
		       MVPP2_PRS_IP_MASK | MVPP2_PRS_RI_VLAN_MASK),

	MVPP2_DEF_FLOW(TCP_V4_FLOW, MVPP2_FL_IP4_TCP_NF_UNTAG,
		       MVPP22_CLS_HEK_IP4_5T,
		       MVPP2_PRS_RI_VLAN_NONE | MVPP2_PRS_RI_L3_IP4_OPT |
		       MVPP2_PRS_RI_L4_TCP,
		       MVPP2_PRS_IP_MASK | MVPP2_PRS_RI_VLAN_MASK),

	MVPP2_DEF_FLOW(TCP_V4_FLOW, MVPP2_FL_IP4_TCP_NF_UNTAG,
		       MVPP22_CLS_HEK_IP4_5T,
		       MVPP2_PRS_RI_VLAN_NONE | MVPP2_PRS_RI_L3_IP4_OTHER |
		       MVPP2_PRS_RI_L4_TCP,
		       MVPP2_PRS_IP_MASK | MVPP2_PRS_RI_VLAN_MASK),

	/* TCP over IPv4 flows, Not fragmented, with vlan tag */
	MVPP2_DEF_FLOW(TCP_V4_FLOW, MVPP2_FL_IP4_TCP_NF_TAG,
		       MVPP22_CLS_HEK_IP4_5T | MVPP22_CLS_HEK_OPT_VLAN,
		       MVPP2_PRS_RI_L3_IP4 | MVPP2_PRS_RI_L4_TCP,
		       MVPP2_PRS_IP_MASK),

	MVPP2_DEF_FLOW(TCP_V4_FLOW, MVPP2_FL_IP4_TCP_NF_TAG,
		       MVPP22_CLS_HEK_IP4_5T | MVPP22_CLS_HEK_OPT_VLAN,
		       MVPP2_PRS_RI_L3_IP4_OPT | MVPP2_PRS_RI_L4_TCP,
		       MVPP2_PRS_IP_MASK),

	MVPP2_DEF_FLOW(TCP_V4_FLOW, MVPP2_FL_IP4_TCP_NF_TAG,
		       MVPP22_CLS_HEK_IP4_5T | MVPP22_CLS_HEK_OPT_VLAN,
		       MVPP2_PRS_RI_L3_IP4_OTHER | MVPP2_PRS_RI_L4_TCP,
		       MVPP2_PRS_IP_MASK),

	/* TCP over IPv4 flows, fragmented, no vlan tag */
	MVPP2_DEF_FLOW(TCP_V4_FLOW, MVPP2_FL_IP4_TCP_FRAG_UNTAG,
		       MVPP22_CLS_HEK_IP4_2T,
		       MVPP2_PRS_RI_VLAN_NONE | MVPP2_PRS_RI_L3_IP4 |
		       MVPP2_PRS_RI_L4_TCP,
		       MVPP2_PRS_IP_MASK | MVPP2_PRS_RI_VLAN_MASK),

	MVPP2_DEF_FLOW(TCP_V4_FLOW, MVPP2_FL_IP4_TCP_FRAG_UNTAG,
		       MVPP22_CLS_HEK_IP4_2T,
		       MVPP2_PRS_RI_VLAN_NONE | MVPP2_PRS_RI_L3_IP4_OPT |
		       MVPP2_PRS_RI_L4_TCP,
		       MVPP2_PRS_IP_MASK | MVPP2_PRS_RI_VLAN_MASK),

	MVPP2_DEF_FLOW(TCP_V4_FLOW, MVPP2_FL_IP4_TCP_FRAG_UNTAG,
		       MVPP22_CLS_HEK_IP4_2T,
		       MVPP2_PRS_RI_VLAN_NONE | MVPP2_PRS_RI_L3_IP4_OTHER |
		       MVPP2_PRS_RI_L4_TCP,
		       MVPP2_PRS_IP_MASK | MVPP2_PRS_RI_VLAN_MASK),

	/* TCP over IPv4 flows, fragmented, with vlan tag */
	MVPP2_DEF_FLOW(TCP_V4_FLOW, MVPP2_FL_IP4_TCP_FRAG_TAG,
		       MVPP22_CLS_HEK_IP4_2T | MVPP22_CLS_HEK_OPT_VLAN,
		       MVPP2_PRS_RI_L3_IP4 | MVPP2_PRS_RI_L4_TCP,
		       MVPP2_PRS_IP_MASK),

	MVPP2_DEF_FLOW(TCP_V4_FLOW, MVPP2_FL_IP4_TCP_FRAG_TAG,
		       MVPP22_CLS_HEK_IP4_2T | MVPP22_CLS_HEK_OPT_VLAN,
		       MVPP2_PRS_RI_L3_IP4_OPT | MVPP2_PRS_RI_L4_TCP,
		       MVPP2_PRS_IP_MASK),

	MVPP2_DEF_FLOW(TCP_V4_FLOW, MVPP2_FL_IP4_TCP_FRAG_TAG,
		       MVPP22_CLS_HEK_IP4_2T | MVPP22_CLS_HEK_OPT_VLAN,
		       MVPP2_PRS_RI_L3_IP4_OTHER | MVPP2_PRS_RI_L4_TCP,
		       MVPP2_PRS_IP_MASK),

	/* UDP over IPv4 flows, Not fragmented, no vlan tag */
	MVPP2_DEF_FLOW(UDP_V4_FLOW, MVPP2_FL_IP4_UDP_NF_UNTAG,
		       MVPP22_CLS_HEK_IP4_5T,
		       MVPP2_PRS_RI_VLAN_NONE | MVPP2_PRS_RI_L3_IP4 |
		       MVPP2_PRS_RI_L4_UDP,
		       MVPP2_PRS_IP_MASK | MVPP2_PRS_RI_VLAN_MASK),

	MVPP2_DEF_FLOW(UDP_V4_FLOW, MVPP2_FL_IP4_UDP_NF_UNTAG,
		       MVPP22_CLS_HEK_IP4_5T,
		       MVPP2_PRS_RI_VLAN_NONE | MVPP2_PRS_RI_L3_IP4_OPT |
		       MVPP2_PRS_RI_L4_UDP,
		       MVPP2_PRS_IP_MASK | MVPP2_PRS_RI_VLAN_MASK),

	MVPP2_DEF_FLOW(UDP_V4_FLOW, MVPP2_FL_IP4_UDP_NF_UNTAG,
		       MVPP22_CLS_HEK_IP4_5T,
		       MVPP2_PRS_RI_VLAN_NONE | MVPP2_PRS_RI_L3_IP4_OTHER |
		       MVPP2_PRS_RI_L4_UDP,
		       MVPP2_PRS_IP_MASK | MVPP2_PRS_RI_VLAN_MASK),

	/* UDP over IPv4 flows, Not fragmented, with vlan tag */
	MVPP2_DEF_FLOW(UDP_V4_FLOW, MVPP2_FL_IP4_UDP_NF_TAG,
		       MVPP22_CLS_HEK_IP4_5T | MVPP22_CLS_HEK_OPT_VLAN,
		       MVPP2_PRS_RI_L3_IP4 | MVPP2_PRS_RI_L4_UDP,
		       MVPP2_PRS_IP_MASK),

	MVPP2_DEF_FLOW(UDP_V4_FLOW, MVPP2_FL_IP4_UDP_NF_TAG,
		       MVPP22_CLS_HEK_IP4_5T | MVPP22_CLS_HEK_OPT_VLAN,
		       MVPP2_PRS_RI_L3_IP4_OPT | MVPP2_PRS_RI_L4_UDP,
		       MVPP2_PRS_IP_MASK),

	MVPP2_DEF_FLOW(UDP_V4_FLOW, MVPP2_FL_IP4_UDP_NF_TAG,
		       MVPP22_CLS_HEK_IP4_5T | MVPP22_CLS_HEK_OPT_VLAN,
		       MVPP2_PRS_RI_L3_IP4_OTHER | MVPP2_PRS_RI_L4_UDP,
		       MVPP2_PRS_IP_MASK),

	/* UDP over IPv4 flows, fragmented, no vlan tag */
	MVPP2_DEF_FLOW(UDP_V4_FLOW, MVPP2_FL_IP4_UDP_FRAG_UNTAG,
		       MVPP22_CLS_HEK_IP4_2T,
		       MVPP2_PRS_RI_VLAN_NONE | MVPP2_PRS_RI_L3_IP4 |
		       MVPP2_PRS_RI_L4_UDP,
		       MVPP2_PRS_IP_MASK | MVPP2_PRS_RI_VLAN_MASK),

	MVPP2_DEF_FLOW(UDP_V4_FLOW, MVPP2_FL_IP4_UDP_FRAG_UNTAG,
		       MVPP22_CLS_HEK_IP4_2T,
		       MVPP2_PRS_RI_VLAN_NONE | MVPP2_PRS_RI_L3_IP4_OPT |
		       MVPP2_PRS_RI_L4_UDP,
		       MVPP2_PRS_IP_MASK | MVPP2_PRS_RI_VLAN_MASK),

	MVPP2_DEF_FLOW(UDP_V4_FLOW, MVPP2_FL_IP4_UDP_FRAG_UNTAG,
		       MVPP22_CLS_HEK_IP4_2T,
		       MVPP2_PRS_RI_VLAN_NONE | MVPP2_PRS_RI_L3_IP4_OTHER |
		       MVPP2_PRS_RI_L4_UDP,
		       MVPP2_PRS_IP_MASK | MVPP2_PRS_RI_VLAN_MASK),

	/* UDP over IPv4 flows, fragmented, with vlan tag */
	MVPP2_DEF_FLOW(UDP_V4_FLOW, MVPP2_FL_IP4_UDP_FRAG_TAG,
		       MVPP22_CLS_HEK_IP4_2T | MVPP22_CLS_HEK_OPT_VLAN,
		       MVPP2_PRS_RI_L3_IP4 | MVPP2_PRS_RI_L4_UDP,
		       MVPP2_PRS_IP_MASK),

	MVPP2_DEF_FLOW(UDP_V4_FLOW, MVPP2_FL_IP4_UDP_FRAG_TAG,
		       MVPP22_CLS_HEK_IP4_2T | MVPP22_CLS_HEK_OPT_VLAN,
		       MVPP2_PRS_RI_L3_IP4_OPT | MVPP2_PRS_RI_L4_UDP,
		       MVPP2_PRS_IP_MASK),

	MVPP2_DEF_FLOW(UDP_V4_FLOW, MVPP2_FL_IP4_UDP_FRAG_TAG,
		       MVPP22_CLS_HEK_IP4_2T | MVPP22_CLS_HEK_OPT_VLAN,
		       MVPP2_PRS_RI_L3_IP4_OTHER | MVPP2_PRS_RI_L4_UDP,
		       MVPP2_PRS_IP_MASK),

	/* TCP over IPv6 flows, not fragmented, no vlan tag */
	MVPP2_DEF_FLOW(TCP_V6_FLOW, MVPP2_FL_IP6_TCP_NF_UNTAG,
		       MVPP22_CLS_HEK_IP6_5T,
		       MVPP2_PRS_RI_VLAN_NONE | MVPP2_PRS_RI_L3_IP6 |
		       MVPP2_PRS_RI_L4_TCP,
		       MVPP2_PRS_IP_MASK | MVPP2_PRS_RI_VLAN_MASK),

	MVPP2_DEF_FLOW(TCP_V6_FLOW, MVPP2_FL_IP6_TCP_NF_UNTAG,
		       MVPP22_CLS_HEK_IP6_5T,
		       MVPP2_PRS_RI_VLAN_NONE | MVPP2_PRS_RI_L3_IP6_EXT |
		       MVPP2_PRS_RI_L4_TCP,
		       MVPP2_PRS_IP_MASK | MVPP2_PRS_RI_VLAN_MASK),

	/* TCP over IPv6 flows, not fragmented, with vlan tag */
	MVPP2_DEF_FLOW(TCP_V6_FLOW, MVPP2_FL_IP6_TCP_NF_TAG,
		       MVPP22_CLS_HEK_IP6_5T | MVPP22_CLS_HEK_OPT_VLAN,
		       MVPP2_PRS_RI_L3_IP6 | MVPP2_PRS_RI_L4_TCP,
		       MVPP2_PRS_IP_MASK),

	MVPP2_DEF_FLOW(TCP_V6_FLOW, MVPP2_FL_IP6_TCP_NF_TAG,
		       MVPP22_CLS_HEK_IP6_5T | MVPP22_CLS_HEK_OPT_VLAN,
		       MVPP2_PRS_RI_L3_IP6_EXT | MVPP2_PRS_RI_L4_TCP,
		       MVPP2_PRS_IP_MASK),

	/* TCP over IPv6 flows, fragmented, no vlan tag */
	MVPP2_DEF_FLOW(TCP_V6_FLOW, MVPP2_FL_IP6_TCP_FRAG_UNTAG,
		       MVPP22_CLS_HEK_IP6_2T,
		       MVPP2_PRS_RI_VLAN_NONE | MVPP2_PRS_RI_L3_IP6 |
		       MVPP2_PRS_RI_IP_FRAG_TRUE | MVPP2_PRS_RI_L4_TCP,
		       MVPP2_PRS_IP_MASK | MVPP2_PRS_RI_VLAN_MASK),

	MVPP2_DEF_FLOW(TCP_V6_FLOW, MVPP2_FL_IP6_TCP_FRAG_UNTAG,
		       MVPP22_CLS_HEK_IP6_2T,
		       MVPP2_PRS_RI_VLAN_NONE | MVPP2_PRS_RI_L3_IP6_EXT |
		       MVPP2_PRS_RI_IP_FRAG_TRUE | MVPP2_PRS_RI_L4_TCP,
		       MVPP2_PRS_IP_MASK | MVPP2_PRS_RI_VLAN_MASK),

	/* TCP over IPv6 flows, fragmented, with vlan tag */
	MVPP2_DEF_FLOW(TCP_V6_FLOW, MVPP2_FL_IP6_TCP_FRAG_TAG,
		       MVPP22_CLS_HEK_IP6_2T | MVPP22_CLS_HEK_OPT_VLAN,
		       MVPP2_PRS_RI_L3_IP6 | MVPP2_PRS_RI_IP_FRAG_TRUE |
		       MVPP2_PRS_RI_L4_TCP,
		       MVPP2_PRS_IP_MASK),

	MVPP2_DEF_FLOW(TCP_V6_FLOW, MVPP2_FL_IP6_TCP_FRAG_TAG,
		       MVPP22_CLS_HEK_IP6_2T | MVPP22_CLS_HEK_OPT_VLAN,
		       MVPP2_PRS_RI_L3_IP6_EXT | MVPP2_PRS_RI_IP_FRAG_TRUE |
		       MVPP2_PRS_RI_L4_TCP,
		       MVPP2_PRS_IP_MASK),

	/* UDP over IPv6 flows, not fragmented, no vlan tag */
	MVPP2_DEF_FLOW(UDP_V6_FLOW, MVPP2_FL_IP6_UDP_NF_UNTAG,
		       MVPP22_CLS_HEK_IP6_5T,
		       MVPP2_PRS_RI_VLAN_NONE | MVPP2_PRS_RI_L3_IP6 |
		       MVPP2_PRS_RI_L4_UDP,
		       MVPP2_PRS_IP_MASK | MVPP2_PRS_RI_VLAN_MASK),

	MVPP2_DEF_FLOW(UDP_V6_FLOW, MVPP2_FL_IP6_UDP_NF_UNTAG,
		       MVPP22_CLS_HEK_IP6_5T,
		       MVPP2_PRS_RI_VLAN_NONE | MVPP2_PRS_RI_L3_IP6_EXT |
		       MVPP2_PRS_RI_L4_UDP,
		       MVPP2_PRS_IP_MASK | MVPP2_PRS_RI_VLAN_MASK),

	/* UDP over IPv6 flows, not fragmented, with vlan tag */
	MVPP2_DEF_FLOW(UDP_V6_FLOW, MVPP2_FL_IP6_UDP_NF_TAG,
		       MVPP22_CLS_HEK_IP6_5T | MVPP22_CLS_HEK_OPT_VLAN,
		       MVPP2_PRS_RI_L3_IP6 | MVPP2_PRS_RI_L4_UDP,
		       MVPP2_PRS_IP_MASK),

	MVPP2_DEF_FLOW(UDP_V6_FLOW, MVPP2_FL_IP6_UDP_NF_TAG,
		       MVPP22_CLS_HEK_IP6_5T | MVPP22_CLS_HEK_OPT_VLAN,
		       MVPP2_PRS_RI_L3_IP6_EXT | MVPP2_PRS_RI_L4_UDP,
		       MVPP2_PRS_IP_MASK),

	/* UDP over IPv6 flows, fragmented, no vlan tag */
	MVPP2_DEF_FLOW(UDP_V6_FLOW, MVPP2_FL_IP6_UDP_FRAG_UNTAG,
		       MVPP22_CLS_HEK_IP6_2T,
		       MVPP2_PRS_RI_VLAN_NONE | MVPP2_PRS_RI_L3_IP6 |
		       MVPP2_PRS_RI_IP_FRAG_TRUE | MVPP2_PRS_RI_L4_UDP,
		       MVPP2_PRS_IP_MASK | MVPP2_PRS_RI_VLAN_MASK),

	MVPP2_DEF_FLOW(UDP_V6_FLOW, MVPP2_FL_IP6_UDP_FRAG_UNTAG,
		       MVPP22_CLS_HEK_IP6_2T,
		       MVPP2_PRS_RI_VLAN_NONE | MVPP2_PRS_RI_L3_IP6_EXT |
		       MVPP2_PRS_RI_IP_FRAG_TRUE | MVPP2_PRS_RI_L4_UDP,
		       MVPP2_PRS_IP_MASK | MVPP2_PRS_RI_VLAN_MASK),

	/* UDP over IPv6 flows, fragmented, with vlan tag */
	MVPP2_DEF_FLOW(UDP_V6_FLOW, MVPP2_FL_IP6_UDP_FRAG_TAG,
		       MVPP22_CLS_HEK_IP6_2T | MVPP22_CLS_HEK_OPT_VLAN,
		       MVPP2_PRS_RI_L3_IP6 | MVPP2_PRS_RI_IP_FRAG_TRUE |
		       MVPP2_PRS_RI_L4_UDP,
		       MVPP2_PRS_IP_MASK),

	MVPP2_DEF_FLOW(UDP_V6_FLOW, MVPP2_FL_IP6_UDP_FRAG_TAG,
		       MVPP22_CLS_HEK_IP6_2T | MVPP22_CLS_HEK_OPT_VLAN,
		       MVPP2_PRS_RI_L3_IP6_EXT | MVPP2_PRS_RI_IP_FRAG_TRUE |
		       MVPP2_PRS_RI_L4_UDP,
		       MVPP2_PRS_IP_MASK),

	/* IPv4 flows, no vlan tag */
	MVPP2_DEF_FLOW(IPV4_FLOW, MVPP2_FL_IP4_UNTAG,
		       MVPP22_CLS_HEK_IP4_2T,
		       MVPP2_PRS_RI_VLAN_NONE | MVPP2_PRS_RI_L3_IP4,
		       MVPP2_PRS_RI_VLAN_MASK | MVPP2_PRS_RI_L3_PROTO_MASK),
	MVPP2_DEF_FLOW(IPV4_FLOW, MVPP2_FL_IP4_UNTAG,
		       MVPP22_CLS_HEK_IP4_2T,
		       MVPP2_PRS_RI_VLAN_NONE | MVPP2_PRS_RI_L3_IP4_OPT,
		       MVPP2_PRS_RI_VLAN_MASK | MVPP2_PRS_RI_L3_PROTO_MASK),
	MVPP2_DEF_FLOW(IPV4_FLOW, MVPP2_FL_IP4_UNTAG,
		       MVPP22_CLS_HEK_IP4_2T,
		       MVPP2_PRS_RI_VLAN_NONE | MVPP2_PRS_RI_L3_IP4_OTHER,
		       MVPP2_PRS_RI_VLAN_MASK | MVPP2_PRS_RI_L3_PROTO_MASK),

	/* IPv4 flows, with vlan tag */
	MVPP2_DEF_FLOW(IPV4_FLOW, MVPP2_FL_IP4_TAG,
		       MVPP22_CLS_HEK_IP4_2T | MVPP22_CLS_HEK_OPT_VLAN,
		       MVPP2_PRS_RI_L3_IP4,
		       MVPP2_PRS_RI_L3_PROTO_MASK),
	MVPP2_DEF_FLOW(IPV4_FLOW, MVPP2_FL_IP4_TAG,
		       MVPP22_CLS_HEK_IP4_2T | MVPP22_CLS_HEK_OPT_VLAN,
		       MVPP2_PRS_RI_L3_IP4_OPT,
		       MVPP2_PRS_RI_L3_PROTO_MASK),
	MVPP2_DEF_FLOW(IPV4_FLOW, MVPP2_FL_IP4_TAG,
		       MVPP22_CLS_HEK_IP4_2T | MVPP22_CLS_HEK_OPT_VLAN,
		       MVPP2_PRS_RI_L3_IP4_OTHER,
		       MVPP2_PRS_RI_L3_PROTO_MASK),

	/* IPv6 flows, no vlan tag */
	MVPP2_DEF_FLOW(IPV6_FLOW, MVPP2_FL_IP6_UNTAG,
		       MVPP22_CLS_HEK_IP6_2T,
		       MVPP2_PRS_RI_VLAN_NONE | MVPP2_PRS_RI_L3_IP6,
		       MVPP2_PRS_RI_VLAN_MASK | MVPP2_PRS_RI_L3_PROTO_MASK),
	MVPP2_DEF_FLOW(IPV6_FLOW, MVPP2_FL_IP6_UNTAG,
		       MVPP22_CLS_HEK_IP6_2T,
		       MVPP2_PRS_RI_VLAN_NONE | MVPP2_PRS_RI_L3_IP6,
		       MVPP2_PRS_RI_VLAN_MASK | MVPP2_PRS_RI_L3_PROTO_MASK),

	/* IPv6 flows, with vlan tag */
	MVPP2_DEF_FLOW(IPV6_FLOW, MVPP2_FL_IP6_TAG,
		       MVPP22_CLS_HEK_IP6_2T | MVPP22_CLS_HEK_OPT_VLAN,
		       MVPP2_PRS_RI_L3_IP6,
		       MVPP2_PRS_RI_L3_PROTO_MASK),
	MVPP2_DEF_FLOW(IPV6_FLOW, MVPP2_FL_IP6_TAG,
		       MVPP22_CLS_HEK_IP6_2T | MVPP22_CLS_HEK_OPT_VLAN,
		       MVPP2_PRS_RI_L3_IP6,
		       MVPP2_PRS_RI_L3_PROTO_MASK),

	/* Non IP flow, no vlan tag */
	MVPP2_DEF_FLOW(ETHER_FLOW, MVPP2_FL_NON_IP_UNTAG,
		       0,
		       MVPP2_PRS_RI_VLAN_NONE,
		       MVPP2_PRS_RI_VLAN_MASK),
	/* Non IP flow, with vlan tag */
	MVPP2_DEF_FLOW(ETHER_FLOW, MVPP2_FL_NON_IP_TAG,
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
	mvpp2_write(priv, MVPP2_CLS_FLOW_TBL0_REG,  fe->data[0]);
	mvpp2_write(priv, MVPP2_CLS_FLOW_TBL1_REG,  fe->data[1]);
	mvpp2_write(priv, MVPP2_CLS_FLOW_TBL2_REG,  fe->data[2]);
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
	mvpp2_cls_flow_lu_type_set(&fe, MVPP2_CLS_LU_ALL);

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
	c2.tcam[4] |= MVPP22_CLS_C2_LU_TYPE(MVPP2_CLS_LU_ALL);

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

static void mvpp2_rss_port_c2_enable(struct mvpp2_port *port)
{
	struct mvpp2_cls_c2_entry c2;

	mvpp2_cls_c2_read(port->priv, MVPP22_CLS_C2_RSS_ENTRY(port->id), &c2);

	c2.attr[2] |= MVPP22_CLS_C2_ATTR2_RSS_EN;

	mvpp2_cls_c2_write(port->priv, &c2);
}

static void mvpp2_rss_port_c2_disable(struct mvpp2_port *port)
{
	struct mvpp2_cls_c2_entry c2;

	mvpp2_cls_c2_read(port->priv, MVPP22_CLS_C2_RSS_ENTRY(port->id), &c2);

	c2.attr[2] &= ~MVPP22_CLS_C2_ATTR2_RSS_EN;

	mvpp2_cls_c2_write(port->priv, &c2);
}

void mvpp22_port_rss_enable(struct mvpp2_port *port)
{
	mvpp2_rss_port_c2_enable(port);
}

void mvpp22_port_rss_disable(struct mvpp2_port *port)
{
	mvpp2_rss_port_c2_disable(port);
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

void mvpp22_rss_fill_table(struct mvpp2_port *port, u32 table)
{
	struct mvpp2 *priv = port->priv;
	int i;

	for (i = 0; i < MVPP22_RSS_TABLE_ENTRIES; i++) {
		u32 sel = MVPP22_RSS_INDEX_TABLE(table) |
			  MVPP22_RSS_INDEX_TABLE_ENTRY(i);
		mvpp2_write(priv, MVPP22_RSS_INDEX, sel);

		mvpp2_write(priv, MVPP22_RSS_TABLE_ENTRY,
			    mvpp22_rxfh_indir(port, port->indir[i]));
	}
}

int mvpp2_ethtool_rxfh_set(struct mvpp2_port *port, struct ethtool_rxnfc *info)
{
	u16 hash_opts = 0;

	switch (info->flow_type) {
	case TCP_V4_FLOW:
	case UDP_V4_FLOW:
	case TCP_V6_FLOW:
	case UDP_V6_FLOW:
		if (info->data & RXH_L4_B_0_1)
			hash_opts |= MVPP22_CLS_HEK_OPT_L4SIP;
		if (info->data & RXH_L4_B_2_3)
			hash_opts |= MVPP22_CLS_HEK_OPT_L4DIP;
		/* Fallthrough */
	case IPV4_FLOW:
	case IPV6_FLOW:
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

	return mvpp2_port_rss_hash_opts_set(port, info->flow_type, hash_opts);
}

int mvpp2_ethtool_rxfh_get(struct mvpp2_port *port, struct ethtool_rxnfc *info)
{
	unsigned long hash_opts;
	int i;

	hash_opts = mvpp2_port_rss_hash_opts_get(port, info->flow_type);
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

void mvpp22_port_rss_init(struct mvpp2_port *port)
{
	struct mvpp2 *priv = port->priv;
	int i;

	/* Set the table width: replace the whole classifier Rx queue number
	 * with the ones configured in RSS table entries.
	 */
	mvpp2_write(priv, MVPP22_RSS_INDEX, MVPP22_RSS_INDEX_TABLE(port->id));
	mvpp2_write(priv, MVPP22_RSS_WIDTH, 8);

	/* The default RxQ is used as a key to select the RSS table to use.
	 * We use one RSS table per port.
	 */
	mvpp2_write(priv, MVPP22_RSS_INDEX,
		    MVPP22_RSS_INDEX_QUEUE(port->first_rxq));
	mvpp2_write(priv, MVPP22_RXQ2RSS_TABLE,
		    MVPP22_RSS_TABLE_POINTER(port->id));

	/* Configure the first table to evenly distribute the packets across
	 * real Rx Queues. The table entries map a hash to a port Rx Queue.
	 */
	for (i = 0; i < MVPP22_RSS_TABLE_ENTRIES; i++)
		port->indir[i] = ethtool_rxfh_indir_default(i, port->nrxqs);

	mvpp22_rss_fill_table(port, port->id);

	/* Configure default flows */
	mvpp2_port_rss_hash_opts_set(port, IPV4_FLOW, MVPP22_CLS_HEK_IP4_2T);
	mvpp2_port_rss_hash_opts_set(port, IPV6_FLOW, MVPP22_CLS_HEK_IP6_2T);
	mvpp2_port_rss_hash_opts_set(port, TCP_V4_FLOW, MVPP22_CLS_HEK_IP4_5T);
	mvpp2_port_rss_hash_opts_set(port, TCP_V6_FLOW, MVPP22_CLS_HEK_IP6_5T);
	mvpp2_port_rss_hash_opts_set(port, UDP_V4_FLOW, MVPP22_CLS_HEK_IP4_5T);
	mvpp2_port_rss_hash_opts_set(port, UDP_V6_FLOW, MVPP22_CLS_HEK_IP6_5T);
}
