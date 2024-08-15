/* SPDX-License-Identifier: GPL-2.0 */
/*
 * RSS and Classifier definitions for Marvell PPv2 Network Controller
 *
 * Copyright (C) 2014 Marvell
 *
 * Marcin Wojtas <mw@semihalf.com>
 */

#ifndef _MVPP2_CLS_H_
#define _MVPP2_CLS_H_

#include "mvpp2.h"
#include "mvpp2_prs.h"

/* Classifier constants */
#define MVPP2_CLS_FLOWS_TBL_SIZE	512
#define MVPP2_CLS_FLOWS_TBL_DATA_WORDS	3
#define MVPP2_CLS_LKP_TBL_SIZE		64
#define MVPP2_CLS_RX_QUEUES		256

/* Classifier flow constants */

#define MVPP2_FLOW_N_FIELDS		4

enum mvpp2_cls_engine {
	MVPP22_CLS_ENGINE_C2 = 1,
	MVPP22_CLS_ENGINE_C3A,
	MVPP22_CLS_ENGINE_C3B,
	MVPP22_CLS_ENGINE_C4,
	MVPP22_CLS_ENGINE_C3HA = 6,
	MVPP22_CLS_ENGINE_C3HB = 7,
};

#define MVPP22_CLS_HEK_OPT_MAC_DA	BIT(0)
#define MVPP22_CLS_HEK_OPT_VLAN_PRI	BIT(1)
#define MVPP22_CLS_HEK_OPT_VLAN		BIT(2)
#define MVPP22_CLS_HEK_OPT_L3_PROTO	BIT(3)
#define MVPP22_CLS_HEK_OPT_IP4SA	BIT(4)
#define MVPP22_CLS_HEK_OPT_IP4DA	BIT(5)
#define MVPP22_CLS_HEK_OPT_IP6SA	BIT(6)
#define MVPP22_CLS_HEK_OPT_IP6DA	BIT(7)
#define MVPP22_CLS_HEK_OPT_L4SIP	BIT(8)
#define MVPP22_CLS_HEK_OPT_L4DIP	BIT(9)
#define MVPP22_CLS_HEK_N_FIELDS		10

#define MVPP22_CLS_HEK_L4_OPTS	(MVPP22_CLS_HEK_OPT_L4SIP | \
				 MVPP22_CLS_HEK_OPT_L4DIP)

#define MVPP22_CLS_HEK_IP4_2T	(MVPP22_CLS_HEK_OPT_IP4SA | \
				 MVPP22_CLS_HEK_OPT_IP4DA)

#define MVPP22_CLS_HEK_IP6_2T	(MVPP22_CLS_HEK_OPT_IP6SA | \
				 MVPP22_CLS_HEK_OPT_IP6DA)

/* The fifth tuple in "5T" is the L4_Info field */
#define MVPP22_CLS_HEK_IP4_5T	(MVPP22_CLS_HEK_IP4_2T | \
				 MVPP22_CLS_HEK_L4_OPTS)

#define MVPP22_CLS_HEK_IP6_5T	(MVPP22_CLS_HEK_IP6_2T | \
				 MVPP22_CLS_HEK_L4_OPTS)

#define MVPP22_CLS_HEK_TAGGED	(MVPP22_CLS_HEK_OPT_VLAN | \
				 MVPP22_CLS_HEK_OPT_VLAN_PRI)

enum mvpp2_cls_field_id {
	MVPP22_CLS_FIELD_MAC_DA = 0x03,
	MVPP22_CLS_FIELD_VLAN_PRI = 0x05,
	MVPP22_CLS_FIELD_VLAN = 0x06,
	MVPP22_CLS_FIELD_L3_PROTO = 0x0f,
	MVPP22_CLS_FIELD_IP4SA = 0x10,
	MVPP22_CLS_FIELD_IP4DA = 0x11,
	MVPP22_CLS_FIELD_IP6SA = 0x17,
	MVPP22_CLS_FIELD_IP6DA = 0x1a,
	MVPP22_CLS_FIELD_L4SIP = 0x1d,
	MVPP22_CLS_FIELD_L4DIP = 0x1e,
};

/* Classifier C2 engine constants */
#define MVPP22_CLS_C2_TCAM_EN(data)		((data) << 16)

enum mvpp22_cls_c2_action {
	MVPP22_C2_NO_UPD = 0,
	MVPP22_C2_NO_UPD_LOCK,
	MVPP22_C2_UPD,
	MVPP22_C2_UPD_LOCK,
};

enum mvpp22_cls_c2_fwd_action {
	MVPP22_C2_FWD_NO_UPD = 0,
	MVPP22_C2_FWD_NO_UPD_LOCK,
	MVPP22_C2_FWD_SW,
	MVPP22_C2_FWD_SW_LOCK,
	MVPP22_C2_FWD_HW,
	MVPP22_C2_FWD_HW_LOCK,
	MVPP22_C2_FWD_HW_LOW_LAT,
	MVPP22_C2_FWD_HW_LOW_LAT_LOCK,
};

enum mvpp22_cls_c2_color_action {
	MVPP22_C2_COL_NO_UPD = 0,
	MVPP22_C2_COL_NO_UPD_LOCK,
	MVPP22_C2_COL_GREEN,
	MVPP22_C2_COL_GREEN_LOCK,
	MVPP22_C2_COL_YELLOW,
	MVPP22_C2_COL_YELLOW_LOCK,
	MVPP22_C2_COL_RED,		/* Drop */
	MVPP22_C2_COL_RED_LOCK,		/* Drop */
};

#define MVPP2_CLS_C2_TCAM_WORDS			5
#define MVPP2_CLS_C2_ATTR_WORDS			5

struct mvpp2_cls_c2_entry {
	u32 index;
	/* TCAM lookup key */
	u32 tcam[MVPP2_CLS_C2_TCAM_WORDS];
	/* Actions to perform upon TCAM match */
	u32 act;
	/* Attributes relative to the actions to perform */
	u32 attr[MVPP2_CLS_C2_ATTR_WORDS];
	/* Entry validity */
	u8 valid;
};

#define MVPP22_FLOW_ETHER_BIT	BIT(0)
#define MVPP22_FLOW_IP4_BIT	BIT(1)
#define MVPP22_FLOW_IP6_BIT	BIT(2)
#define MVPP22_FLOW_TCP_BIT	BIT(3)
#define MVPP22_FLOW_UDP_BIT	BIT(4)

#define MVPP22_FLOW_TCP4	(MVPP22_FLOW_ETHER_BIT | MVPP22_FLOW_IP4_BIT | MVPP22_FLOW_TCP_BIT)
#define MVPP22_FLOW_TCP6	(MVPP22_FLOW_ETHER_BIT | MVPP22_FLOW_IP6_BIT | MVPP22_FLOW_TCP_BIT)
#define MVPP22_FLOW_UDP4	(MVPP22_FLOW_ETHER_BIT | MVPP22_FLOW_IP4_BIT | MVPP22_FLOW_UDP_BIT)
#define MVPP22_FLOW_UDP6	(MVPP22_FLOW_ETHER_BIT | MVPP22_FLOW_IP6_BIT | MVPP22_FLOW_UDP_BIT)
#define MVPP22_FLOW_IP4		(MVPP22_FLOW_ETHER_BIT | MVPP22_FLOW_IP4_BIT)
#define MVPP22_FLOW_IP6		(MVPP22_FLOW_ETHER_BIT | MVPP22_FLOW_IP6_BIT)
#define MVPP22_FLOW_ETHERNET	(MVPP22_FLOW_ETHER_BIT)

/* Classifier C2 engine entries */
#define MVPP22_CLS_C2_N_ENTRIES		256

/* Number of per-port dedicated entries in the C2 TCAM */
#define MVPP22_CLS_C2_PORT_N_FLOWS	MVPP2_N_RFS_ENTRIES_PER_FLOW

/* Each port has one range per flow type + one entry controlling the global RSS
 * setting and the default rx queue
 */
#define MVPP22_CLS_C2_PORT_RANGE	(MVPP22_CLS_C2_PORT_N_FLOWS + 1)
#define MVPP22_CLS_C2_PORT_FIRST(p)	((p) * MVPP22_CLS_C2_PORT_RANGE)
#define MVPP22_CLS_C2_RSS_ENTRY(p)	(MVPP22_CLS_C2_PORT_FIRST((p) + 1) - 1)

#define MVPP22_CLS_C2_PORT_FLOW_FIRST(p)	(MVPP22_CLS_C2_PORT_FIRST(p))

#define MVPP22_CLS_C2_RFS_LOC(p, loc)	(MVPP22_CLS_C2_PORT_FLOW_FIRST(p) + (loc))

/* Packet flow ID */
enum mvpp2_prs_flow {
	MVPP2_FL_START = 8,
	MVPP2_FL_IP4_TCP_NF_UNTAG = MVPP2_FL_START,
	MVPP2_FL_IP4_UDP_NF_UNTAG,
	MVPP2_FL_IP4_TCP_NF_TAG,
	MVPP2_FL_IP4_UDP_NF_TAG,
	MVPP2_FL_IP6_TCP_NF_UNTAG,
	MVPP2_FL_IP6_UDP_NF_UNTAG,
	MVPP2_FL_IP6_TCP_NF_TAG,
	MVPP2_FL_IP6_UDP_NF_TAG,
	MVPP2_FL_IP4_TCP_FRAG_UNTAG,
	MVPP2_FL_IP4_UDP_FRAG_UNTAG,
	MVPP2_FL_IP4_TCP_FRAG_TAG,
	MVPP2_FL_IP4_UDP_FRAG_TAG,
	MVPP2_FL_IP6_TCP_FRAG_UNTAG,
	MVPP2_FL_IP6_UDP_FRAG_UNTAG,
	MVPP2_FL_IP6_TCP_FRAG_TAG,
	MVPP2_FL_IP6_UDP_FRAG_TAG,
	MVPP2_FL_IP4_UNTAG, /* non-TCP, non-UDP, same for below */
	MVPP2_FL_IP4_TAG,
	MVPP2_FL_IP6_UNTAG,
	MVPP2_FL_IP6_TAG,
	MVPP2_FL_NON_IP_UNTAG,
	MVPP2_FL_NON_IP_TAG,
	MVPP2_FL_LAST,
};

/* LU Type defined for all engines, and specified in the flow table */
#define MVPP2_CLS_LU_TYPE_MASK			0x3f

enum mvpp2_cls_lu_type {
	/* rule->loc is used as a lu-type for the entries 0 - 62. */
	MVPP22_CLS_LU_TYPE_ALL = 63,
};

#define MVPP2_N_FLOWS		(MVPP2_FL_LAST - MVPP2_FL_START)

struct mvpp2_cls_flow {
	/* The L2-L4 traffic flow type */
	int flow_type;

	/* The first id in the flow table for this flow */
	u16 flow_id;

	/* The supported HEK fields for this flow */
	u16 supported_hash_opts;

	/* The Header Parser result_info that matches this flow */
	struct mvpp2_prs_result_info prs_ri;
};

#define MVPP2_CLS_FLT_ENTRIES_PER_FLOW		(MVPP2_MAX_PORTS + 1 + 16)
#define MVPP2_CLS_FLT_FIRST(id)			(((id) - MVPP2_FL_START) * \
						 MVPP2_CLS_FLT_ENTRIES_PER_FLOW)

#define MVPP2_CLS_FLT_C2_RFS(port, id, rfs_n)	(MVPP2_CLS_FLT_FIRST(id) + \
						 ((port) * MVPP2_MAX_PORTS) + \
						 (rfs_n))

#define MVPP2_CLS_FLT_C2_RSS_ENTRY(id)		(MVPP2_CLS_FLT_C2_RFS(MVPP2_MAX_PORTS, id, 0))
#define MVPP2_CLS_FLT_HASH_ENTRY(port, id)	(MVPP2_CLS_FLT_C2_RSS_ENTRY(id) + 1 + (port))
#define MVPP2_CLS_FLT_LAST(id)			(MVPP2_CLS_FLT_FIRST(id) + \
						 MVPP2_CLS_FLT_ENTRIES_PER_FLOW - 1)

/* Iterate on each classifier flow id. Sets 'i' to be the index of the first
 * entry in the cls_flows table for each different flow_id.
 * This relies on entries having the same flow_id in the cls_flows table being
 * contiguous.
 */
#define for_each_cls_flow_id(i)						      \
	for ((i) = 0; (i) < MVPP2_N_PRS_FLOWS; (i)++)			      \
		if ((i) > 0 &&						      \
		    cls_flows[(i)].flow_id == cls_flows[(i) - 1].flow_id)       \
			continue;					      \
		else

/* Iterate on each classifier flow that has a given flow_type. Sets 'i' to be
 * the index of the first entry in the cls_flow table for each different flow_id
 * that has the given flow_type. This allows to operate on all flows that
 * matches a given ethtool flow type.
 */
#define for_each_cls_flow_id_with_type(i, type)				      \
	for_each_cls_flow_id((i))					      \
		if (cls_flows[(i)].flow_type != (type))			      \
			continue;					      \
		else

#define for_each_cls_flow_id_containing_type(i, type)			      \
	for_each_cls_flow_id((i))					      \
		if ((cls_flows[(i)].flow_type & (type)) != (type))	      \
			continue;					      \
		else

struct mvpp2_cls_flow_entry {
	u32 index;
	u32 data[MVPP2_CLS_FLOWS_TBL_DATA_WORDS];
};

struct mvpp2_cls_lookup_entry {
	u32 lkpid;
	u32 way;
	u32 data;
};

int mvpp22_port_rss_init(struct mvpp2_port *port);

int mvpp22_port_rss_enable(struct mvpp2_port *port);
int mvpp22_port_rss_disable(struct mvpp2_port *port);

int mvpp22_port_rss_ctx_create(struct mvpp2_port *port, u32 *rss_ctx);
int mvpp22_port_rss_ctx_delete(struct mvpp2_port *port, u32 rss_ctx);

int mvpp22_port_rss_ctx_indir_set(struct mvpp2_port *port, u32 rss_ctx,
				  const u32 *indir);
int mvpp22_port_rss_ctx_indir_get(struct mvpp2_port *port, u32 rss_ctx,
				  u32 *indir);

int mvpp2_ethtool_rxfh_get(struct mvpp2_port *port, struct ethtool_rxnfc *info);
int mvpp2_ethtool_rxfh_set(struct mvpp2_port *port, struct ethtool_rxnfc *info);

void mvpp2_cls_init(struct mvpp2 *priv);

void mvpp2_cls_port_config(struct mvpp2_port *port);

void mvpp2_cls_oversize_rxq_set(struct mvpp2_port *port);

int mvpp2_cls_flow_eng_get(struct mvpp2_cls_flow_entry *fe);

u16 mvpp2_flow_get_hek_fields(struct mvpp2_cls_flow_entry *fe);

const struct mvpp2_cls_flow *mvpp2_cls_flow_get(int flow);

u32 mvpp2_cls_flow_hits(struct mvpp2 *priv, int index);

void mvpp2_cls_flow_read(struct mvpp2 *priv, int index,
			 struct mvpp2_cls_flow_entry *fe);

u32 mvpp2_cls_lookup_hits(struct mvpp2 *priv, int index);

void mvpp2_cls_lookup_read(struct mvpp2 *priv, int lkpid, int way,
			   struct mvpp2_cls_lookup_entry *le);

u32 mvpp2_cls_c2_hit_count(struct mvpp2 *priv, int c2_index);

void mvpp2_cls_c2_read(struct mvpp2 *priv, int index,
		       struct mvpp2_cls_c2_entry *c2);

int mvpp2_ethtool_cls_rule_get(struct mvpp2_port *port,
			       struct ethtool_rxnfc *rxnfc);

int mvpp2_ethtool_cls_rule_ins(struct mvpp2_port *port,
			       struct ethtool_rxnfc *info);

int mvpp2_ethtool_cls_rule_del(struct mvpp2_port *port,
			       struct ethtool_rxnfc *info);

#endif
