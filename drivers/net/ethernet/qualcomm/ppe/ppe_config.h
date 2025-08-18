/* SPDX-License-Identifier: GPL-2.0-only
 *
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#ifndef __PPE_CONFIG_H__
#define __PPE_CONFIG_H__

#include "ppe.h"

/* There are different table index ranges for configuring queue base ID of
 * the destination port, CPU code and service code.
 */
#define PPE_QUEUE_BASE_DEST_PORT		0
#define PPE_QUEUE_BASE_CPU_CODE			1024
#define PPE_QUEUE_BASE_SERVICE_CODE		2048

#define PPE_QUEUE_INTER_PRI_NUM			16
#define PPE_QUEUE_HASH_NUM			256

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
#endif
