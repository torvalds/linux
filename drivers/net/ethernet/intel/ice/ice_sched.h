/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2018, Intel Corporation. */

#ifndef _ICE_SCHED_H_
#define _ICE_SCHED_H_

#include "ice_common.h"

#define ICE_QGRP_LAYER_OFFSET	2

struct ice_sched_agg_vsi_info {
	struct list_head list_entry;
	DECLARE_BITMAP(tc_bitmap, ICE_MAX_TRAFFIC_CLASS);
	u16 vsi_id;
};

struct ice_sched_agg_info {
	struct list_head agg_vsi_list;
	struct list_head list_entry;
	DECLARE_BITMAP(tc_bitmap, ICE_MAX_TRAFFIC_CLASS);
	u32 agg_id;
	enum ice_agg_type agg_type;
};

/* FW AQ command calls */
enum ice_status ice_sched_init_port(struct ice_port_info *pi);
enum ice_status ice_sched_query_res_alloc(struct ice_hw *hw);
void ice_sched_cleanup_all(struct ice_hw *hw);
struct ice_sched_node *
ice_sched_find_node_by_teid(struct ice_sched_node *start_node, u32 teid);
enum ice_status
ice_sched_add_node(struct ice_port_info *pi, u8 layer,
		   struct ice_aqc_txsched_elem_data *info);
void ice_free_sched_node(struct ice_port_info *pi, struct ice_sched_node *node);
struct ice_sched_node *ice_sched_get_tc_node(struct ice_port_info *pi, u8 tc);
struct ice_sched_node *
ice_sched_get_free_qparent(struct ice_port_info *pi, u16 vsi_id, u8 tc,
			   u8 owner);
#endif /* _ICE_SCHED_H_ */
