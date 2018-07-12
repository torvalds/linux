/*
 * RSS and Classifier definitions for Marvell PPv2 Network Controller
 *
 * Copyright (C) 2014 Marvell
 *
 * Marcin Wojtas <mw@semihalf.com>
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
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
#define MVPP22_CLS_HEK_OPT_VLAN		BIT(1)
#define MVPP22_CLS_HEK_OPT_L3_PROTO	BIT(2)
#define MVPP22_CLS_HEK_OPT_IP4SA	BIT(3)
#define MVPP22_CLS_HEK_OPT_IP4DA	BIT(4)
#define MVPP22_CLS_HEK_OPT_IP6SA	BIT(5)
#define MVPP22_CLS_HEK_OPT_IP6DA	BIT(6)
#define MVPP22_CLS_HEK_OPT_L4SIP	BIT(7)
#define MVPP22_CLS_HEK_OPT_L4DIP	BIT(8)
#define MVPP22_CLS_HEK_N_FIELDS		9

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

enum mvpp2_cls_flow_seq {
	MVPP2_CLS_FLOW_SEQ_NORMAL = 0,
	MVPP2_CLS_FLOW_SEQ_FIRST1,
	MVPP2_CLS_FLOW_SEQ_FIRST2,
	MVPP2_CLS_FLOW_SEQ_LAST,
	MVPP2_CLS_FLOW_SEQ_MIDDLE
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

#define MVPP2_CLS_C2_TCAM_WORDS			5
#define MVPP2_CLS_C2_ATTR_WORDS			5

struct mvpp2_cls_c2_entry {
	u32 index;
	u32 tcam[MVPP2_CLS_C2_TCAM_WORDS];
	u32 act;
	u32 attr[MVPP2_CLS_C2_ATTR_WORDS];
};

/* Classifier C2 engine entries */
#define MVPP22_CLS_C2_RSS_ENTRY(port)	(port)
#define MVPP22_CLS_C2_N_ENTRIES		MVPP2_MAX_PORTS

/* RSS flow entries in the flow table. We have 2 entries per port for RSS.
 *
 * The first performs a lookup using the C2 TCAM engine, to tag the
 * packet for software forwarding (needed for RSS), enable or disable RSS, and
 * assign the default rx queue.
 *
 * The second configures the hash generation, by specifying which fields of the
 * packet header are used to generate the hash, and specifies the relevant hash
 * engine to use.
 */
#define MVPP22_RSS_FLOW_C2_OFFS		0
#define MVPP22_RSS_FLOW_HASH_OFFS	1
#define MVPP22_RSS_FLOW_SIZE		(MVPP22_RSS_FLOW_HASH_OFFS + 1)

#define MVPP22_RSS_FLOW_C2(port)	((port) * MVPP22_RSS_FLOW_SIZE + \
					 MVPP22_RSS_FLOW_C2_OFFS)
#define MVPP22_RSS_FLOW_HASH(port)	((port) * MVPP22_RSS_FLOW_SIZE + \
					 MVPP22_RSS_FLOW_HASH_OFFS)
#define MVPP22_RSS_FLOW_FIRST(port)	MVPP22_RSS_FLOW_C2(port)

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

#define MVPP2_N_FLOWS	52

#define MVPP2_ENTRIES_PER_FLOW			(MVPP2_MAX_PORTS + 1)
#define MVPP2_FLOW_C2_ENTRY(id)			((id) * MVPP2_ENTRIES_PER_FLOW)
#define MVPP2_PORT_FLOW_HASH_ENTRY(port, id)	((id) * MVPP2_ENTRIES_PER_FLOW + \
						(port) + 1)
struct mvpp2_cls_flow_entry {
	u32 index;
	u32 data[MVPP2_CLS_FLOWS_TBL_DATA_WORDS];
};

struct mvpp2_cls_lookup_entry {
	u32 lkpid;
	u32 way;
	u32 data;
};

void mvpp22_rss_fill_table(struct mvpp2_port *port, u32 table);

void mvpp22_rss_port_init(struct mvpp2_port *port);

void mvpp2_cls_init(struct mvpp2 *priv);

void mvpp2_cls_port_config(struct mvpp2_port *port);

void mvpp2_cls_oversize_rxq_set(struct mvpp2_port *port);

#endif
