/* SPDX-License-Identifier: GPL-2.0-only
 *
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#ifndef __PPE_CONFIG_H__
#define __PPE_CONFIG_H__

#include <linux/types.h>

#include "ppe.h"

/* There are different table index ranges for configuring queue base ID of
 * the destination port, CPU code and service code.
 */
#define PPE_QUEUE_BASE_DEST_PORT		0
#define PPE_QUEUE_BASE_CPU_CODE			1024
#define PPE_QUEUE_BASE_SERVICE_CODE		2048

#define PPE_QUEUE_INTER_PRI_NUM			16
#define PPE_QUEUE_HASH_NUM			256

/* The service code is used by EDMA port to transmit packet to PPE. */
#define PPE_EDMA_SC_BYPASS_ID			1

/* The PPE RSS hash configured for IPv4 and IPv6 packet separately. */
#define PPE_RSS_HASH_MODE_IPV4			BIT(0)
#define PPE_RSS_HASH_MODE_IPV6			BIT(1)
#define PPE_RSS_HASH_IP_LENGTH			4
#define PPE_RSS_HASH_TUPLES			5

/* PPE supports 300 queues, each bit presents as one queue. */
#define PPE_RING_TO_QUEUE_BITMAP_WORD_CNT	10

/**
 * enum ppe_scheduler_frame_mode - PPE scheduler frame mode.
 * @PPE_SCH_WITH_IPG_PREAMBLE_FRAME_CRC: The scheduled frame includes IPG,
 * preamble, Ethernet packet and CRC.
 * @PPE_SCH_WITH_FRAME_CRC: The scheduled frame includes Ethernet frame and CRC
 * excluding IPG and preamble.
 * @PPE_SCH_WITH_L3_PAYLOAD: The scheduled frame includes layer 3 packet data.
 */
enum ppe_scheduler_frame_mode {
	PPE_SCH_WITH_IPG_PREAMBLE_FRAME_CRC = 0,
	PPE_SCH_WITH_FRAME_CRC = 1,
	PPE_SCH_WITH_L3_PAYLOAD = 2,
};

/**
 * struct ppe_scheduler_cfg - PPE scheduler configuration.
 * @flow_id: PPE flow ID.
 * @pri: Scheduler priority.
 * @drr_node_id: Node ID for scheduled traffic.
 * @drr_node_wt: Weight for scheduled traffic.
 * @unit_is_packet: Packet based or byte based unit for scheduled traffic.
 * @frame_mode: Packet mode to be scheduled.
 *
 * PPE scheduler supports commit rate and exceed rate configurations.
 */
struct ppe_scheduler_cfg {
	int flow_id;
	int pri;
	int drr_node_id;
	int drr_node_wt;
	bool unit_is_packet;
	enum ppe_scheduler_frame_mode frame_mode;
};

/**
 * enum ppe_resource_type - PPE resource type.
 * @PPE_RES_UCAST: Unicast queue resource.
 * @PPE_RES_MCAST: Multicast queue resource.
 * @PPE_RES_L0_NODE: Level 0 for queue based node resource.
 * @PPE_RES_L1_NODE: Level 1 for flow based node resource.
 * @PPE_RES_FLOW_ID: Flow based node resource.
 */
enum ppe_resource_type {
	PPE_RES_UCAST,
	PPE_RES_MCAST,
	PPE_RES_L0_NODE,
	PPE_RES_L1_NODE,
	PPE_RES_FLOW_ID,
};

/**
 * struct ppe_queue_ucast_dest - PPE unicast queue destination.
 * @src_profile: Source profile.
 * @service_code_en: Enable service code to map the queue base ID.
 * @service_code: Service code.
 * @cpu_code_en: Enable CPU code to map the queue base ID.
 * @cpu_code: CPU code.
 * @dest_port: destination port.
 *
 * PPE egress queue ID is decided by the service code if enabled, otherwise
 * by the CPU code if enabled, or by destination port if both service code
 * and CPU code are disabled.
 */
struct ppe_queue_ucast_dest {
	int src_profile;
	bool service_code_en;
	int service_code;
	bool cpu_code_en;
	int cpu_code;
	int dest_port;
};

/* Hardware bitmaps for bypassing features of the ingress packet. */
enum ppe_sc_ingress_type {
	PPE_SC_BYPASS_INGRESS_VLAN_TAG_FMT_CHECK = 0,
	PPE_SC_BYPASS_INGRESS_VLAN_MEMBER_CHECK = 1,
	PPE_SC_BYPASS_INGRESS_VLAN_TRANSLATE = 2,
	PPE_SC_BYPASS_INGRESS_MY_MAC_CHECK = 3,
	PPE_SC_BYPASS_INGRESS_DIP_LOOKUP = 4,
	PPE_SC_BYPASS_INGRESS_FLOW_LOOKUP = 5,
	PPE_SC_BYPASS_INGRESS_FLOW_ACTION = 6,
	PPE_SC_BYPASS_INGRESS_ACL = 7,
	PPE_SC_BYPASS_INGRESS_FAKE_MAC_HEADER = 8,
	PPE_SC_BYPASS_INGRESS_SERVICE_CODE = 9,
	PPE_SC_BYPASS_INGRESS_WRONG_PKT_FMT_L2 = 10,
	PPE_SC_BYPASS_INGRESS_WRONG_PKT_FMT_L3_IPV4 = 11,
	PPE_SC_BYPASS_INGRESS_WRONG_PKT_FMT_L3_IPV6 = 12,
	PPE_SC_BYPASS_INGRESS_WRONG_PKT_FMT_L4 = 13,
	PPE_SC_BYPASS_INGRESS_FLOW_SERVICE_CODE = 14,
	PPE_SC_BYPASS_INGRESS_ACL_SERVICE_CODE = 15,
	PPE_SC_BYPASS_INGRESS_FAKE_L2_PROTO = 16,
	PPE_SC_BYPASS_INGRESS_PPPOE_TERMINATION = 17,
	PPE_SC_BYPASS_INGRESS_DEFAULT_VLAN = 18,
	PPE_SC_BYPASS_INGRESS_DEFAULT_PCP = 19,
	PPE_SC_BYPASS_INGRESS_VSI_ASSIGN = 20,
	/* Values 21-23 are not specified by hardware. */
	PPE_SC_BYPASS_INGRESS_VLAN_ASSIGN_FAIL = 24,
	PPE_SC_BYPASS_INGRESS_SOURCE_GUARD = 25,
	PPE_SC_BYPASS_INGRESS_MRU_MTU_CHECK = 26,
	PPE_SC_BYPASS_INGRESS_FLOW_SRC_CHECK = 27,
	PPE_SC_BYPASS_INGRESS_FLOW_QOS = 28,
	/* This must be last as it determines the size of the BITMAP. */
	PPE_SC_BYPASS_INGRESS_SIZE,
};

/* Hardware bitmaps for bypassing features of the egress packet. */
enum ppe_sc_egress_type {
	PPE_SC_BYPASS_EGRESS_VLAN_MEMBER_CHECK = 0,
	PPE_SC_BYPASS_EGRESS_VLAN_TRANSLATE = 1,
	PPE_SC_BYPASS_EGRESS_VLAN_TAG_FMT_CTRL = 2,
	PPE_SC_BYPASS_EGRESS_FDB_LEARN = 3,
	PPE_SC_BYPASS_EGRESS_FDB_REFRESH = 4,
	PPE_SC_BYPASS_EGRESS_L2_SOURCE_SECURITY = 5,
	PPE_SC_BYPASS_EGRESS_MANAGEMENT_FWD = 6,
	PPE_SC_BYPASS_EGRESS_BRIDGING_FWD = 7,
	PPE_SC_BYPASS_EGRESS_IN_STP_FLTR = 8,
	PPE_SC_BYPASS_EGRESS_EG_STP_FLTR = 9,
	PPE_SC_BYPASS_EGRESS_SOURCE_FLTR = 10,
	PPE_SC_BYPASS_EGRESS_POLICER = 11,
	PPE_SC_BYPASS_EGRESS_L2_PKT_EDIT = 12,
	PPE_SC_BYPASS_EGRESS_L3_PKT_EDIT = 13,
	PPE_SC_BYPASS_EGRESS_ACL_POST_ROUTING_CHECK = 14,
	PPE_SC_BYPASS_EGRESS_PORT_ISOLATION = 15,
	PPE_SC_BYPASS_EGRESS_PRE_ACL_QOS = 16,
	PPE_SC_BYPASS_EGRESS_POST_ACL_QOS = 17,
	PPE_SC_BYPASS_EGRESS_DSCP_QOS = 18,
	PPE_SC_BYPASS_EGRESS_PCP_QOS = 19,
	PPE_SC_BYPASS_EGRESS_PREHEADER_QOS = 20,
	PPE_SC_BYPASS_EGRESS_FAKE_MAC_DROP = 21,
	PPE_SC_BYPASS_EGRESS_TUNL_CONTEXT = 22,
	PPE_SC_BYPASS_EGRESS_FLOW_POLICER = 23,
	/* This must be last as it determines the size of the BITMAP. */
	PPE_SC_BYPASS_EGRESS_SIZE,
};

/* Hardware bitmaps for bypassing counter of packet. */
enum ppe_sc_counter_type {
	PPE_SC_BYPASS_COUNTER_RX_VLAN = 0,
	PPE_SC_BYPASS_COUNTER_RX = 1,
	PPE_SC_BYPASS_COUNTER_TX_VLAN = 2,
	PPE_SC_BYPASS_COUNTER_TX = 3,
	/* This must be last as it determines the size of the BITMAP. */
	PPE_SC_BYPASS_COUNTER_SIZE,
};

/* Hardware bitmaps for bypassing features of tunnel packet. */
enum ppe_sc_tunnel_type {
	PPE_SC_BYPASS_TUNNEL_SERVICE_CODE = 0,
	PPE_SC_BYPASS_TUNNEL_TUNNEL_HANDLE = 1,
	PPE_SC_BYPASS_TUNNEL_L3_IF_CHECK = 2,
	PPE_SC_BYPASS_TUNNEL_VLAN_CHECK = 3,
	PPE_SC_BYPASS_TUNNEL_DMAC_CHECK = 4,
	PPE_SC_BYPASS_TUNNEL_UDP_CSUM_0_CHECK = 5,
	PPE_SC_BYPASS_TUNNEL_TBL_DE_ACCE_CHECK = 6,
	PPE_SC_BYPASS_TUNNEL_PPPOE_MC_TERM_CHECK = 7,
	PPE_SC_BYPASS_TUNNEL_TTL_EXCEED_CHECK = 8,
	PPE_SC_BYPASS_TUNNEL_MAP_SRC_CHECK = 9,
	PPE_SC_BYPASS_TUNNEL_MAP_DST_CHECK = 10,
	PPE_SC_BYPASS_TUNNEL_LPM_DST_LOOKUP = 11,
	PPE_SC_BYPASS_TUNNEL_LPM_LOOKUP = 12,
	PPE_SC_BYPASS_TUNNEL_WRONG_PKT_FMT_L2 = 13,
	PPE_SC_BYPASS_TUNNEL_WRONG_PKT_FMT_L3_IPV4 = 14,
	PPE_SC_BYPASS_TUNNEL_WRONG_PKT_FMT_L3_IPV6 = 15,
	PPE_SC_BYPASS_TUNNEL_WRONG_PKT_FMT_L4 = 16,
	PPE_SC_BYPASS_TUNNEL_WRONG_PKT_FMT_TUNNEL = 17,
	/* Values 18-19 are not specified by hardware. */
	PPE_SC_BYPASS_TUNNEL_PRE_IPO = 20,
	/* This must be last as it determines the size of the BITMAP. */
	PPE_SC_BYPASS_TUNNEL_SIZE,
};

/**
 * struct ppe_sc_bypass - PPE service bypass bitmaps
 * @ingress: Bitmap of features that can be bypassed on the ingress packet.
 * @egress: Bitmap of features that can be bypassed on the egress packet.
 * @counter: Bitmap of features that can be bypassed on the counter type.
 * @tunnel: Bitmap of features that can be bypassed on the tunnel packet.
 */
struct ppe_sc_bypass {
	DECLARE_BITMAP(ingress, PPE_SC_BYPASS_INGRESS_SIZE);
	DECLARE_BITMAP(egress, PPE_SC_BYPASS_EGRESS_SIZE);
	DECLARE_BITMAP(counter, PPE_SC_BYPASS_COUNTER_SIZE);
	DECLARE_BITMAP(tunnel, PPE_SC_BYPASS_TUNNEL_SIZE);
};

/**
 * struct ppe_sc_cfg - PPE service code configuration.
 * @dest_port_valid: Generate destination port or not.
 * @dest_port: Destination port ID.
 * @bitmaps: Bitmap of bypass features.
 * @is_src: Destination port acts as source port, packet sent to CPU.
 * @next_service_code: New service code generated.
 * @eip_field_update_bitmap: Fields updated as actions taken for EIP.
 * @eip_hw_service: Selected hardware functions for EIP.
 * @eip_offset_sel: Packet offset selection, using packet's layer 4 offset
 * or using packet's layer 3 offset for EIP.
 *
 * Service code is generated during the packet passing through PPE.
 */
struct ppe_sc_cfg {
	bool dest_port_valid;
	int dest_port;
	struct ppe_sc_bypass bitmaps;
	bool is_src;
	int next_service_code;
	int eip_field_update_bitmap;
	int eip_hw_service;
	int eip_offset_sel;
};

/**
 * enum ppe_action_type - PPE action of the received packet.
 * @PPE_ACTION_FORWARD: Packet forwarded per L2/L3 process.
 * @PPE_ACTION_DROP: Packet dropped by PPE.
 * @PPE_ACTION_COPY_TO_CPU: Packet copied to CPU port per multicast queue.
 * @PPE_ACTION_REDIRECT_TO_CPU: Packet redirected to CPU port per unicast queue.
 */
enum ppe_action_type {
	PPE_ACTION_FORWARD = 0,
	PPE_ACTION_DROP = 1,
	PPE_ACTION_COPY_TO_CPU = 2,
	PPE_ACTION_REDIRECT_TO_CPU = 3,
};

/**
 * struct ppe_rss_hash_cfg - PPE RSS hash configuration.
 * @hash_mask: Mask of the generated hash value.
 * @hash_fragment_mode: Hash generation mode for the first fragment of TCP,
 * UDP and UDP-Lite packets, to use either 3 tuple or 5 tuple for RSS hash
 * key computation.
 * @hash_seed: Seed to generate RSS hash.
 * @hash_sip_mix: Source IP selection.
 * @hash_dip_mix: Destination IP selection.
 * @hash_protocol_mix: Protocol selection.
 * @hash_sport_mix: Source L4 port selection.
 * @hash_dport_mix: Destination L4 port selection.
 * @hash_fin_inner: RSS hash value first selection.
 * @hash_fin_outer: RSS hash value second selection.
 *
 * PPE RSS hash value is generated for the packet based on the RSS hash
 * configured.
 */
struct ppe_rss_hash_cfg {
	u32 hash_mask;
	bool hash_fragment_mode;
	u32 hash_seed;
	u8 hash_sip_mix[PPE_RSS_HASH_IP_LENGTH];
	u8 hash_dip_mix[PPE_RSS_HASH_IP_LENGTH];
	u8 hash_protocol_mix;
	u8 hash_sport_mix;
	u8 hash_dport_mix;
	u8 hash_fin_inner[PPE_RSS_HASH_TUPLES];
	u8 hash_fin_outer[PPE_RSS_HASH_TUPLES];
};

int ppe_hw_config(struct ppe_device *ppe_dev);
int ppe_queue_scheduler_set(struct ppe_device *ppe_dev,
			    int node_id, bool flow_level, int port,
			    struct ppe_scheduler_cfg scheduler_cfg);
int ppe_queue_ucast_base_set(struct ppe_device *ppe_dev,
			     struct ppe_queue_ucast_dest queue_dst,
			     int queue_base,
			     int profile_id);
int ppe_queue_ucast_offset_pri_set(struct ppe_device *ppe_dev,
				   int profile_id,
				   int priority,
				   int queue_offset);
int ppe_queue_ucast_offset_hash_set(struct ppe_device *ppe_dev,
				    int profile_id,
				    int rss_hash,
				    int queue_offset);
int ppe_port_resource_get(struct ppe_device *ppe_dev, int port,
			  enum ppe_resource_type type,
			  int *res_start, int *res_end);
int ppe_sc_config_set(struct ppe_device *ppe_dev, int sc,
		      struct ppe_sc_cfg cfg);
int ppe_counter_enable_set(struct ppe_device *ppe_dev, int port);
int ppe_rss_hash_config_set(struct ppe_device *ppe_dev, int mode,
			    struct ppe_rss_hash_cfg hash_cfg);
int ppe_ring_queue_map_set(struct ppe_device *ppe_dev,
			   int ring_id,
			   u32 *queue_map);
#endif
