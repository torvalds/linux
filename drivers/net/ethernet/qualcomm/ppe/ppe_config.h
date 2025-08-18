/* SPDX-License-Identifier: GPL-2.0-only
 *
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#ifndef __PPE_CONFIG_H__
#define __PPE_CONFIG_H__

#include "ppe.h"

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

int ppe_hw_config(struct ppe_device *ppe_dev);
int ppe_queue_scheduler_set(struct ppe_device *ppe_dev,
			    int node_id, bool flow_level, int port,
			    struct ppe_scheduler_cfg scheduler_cfg);
#endif
