/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2018, Intel Corporation. */

#ifndef _ICE_SCHED_H_
#define _ICE_SCHED_H_

#include "ice_common.h"

#define ICE_QGRP_LAYER_OFFSET	2
#define ICE_VSI_LAYER_OFFSET	4
#define ICE_AGG_LAYER_OFFSET	6
#define ICE_SCHED_INVAL_LAYER_NUM	0xFF
/* Burst size is a 12 bits register that is configured while creating the RL
 * profile(s). MSB is a granularity bit and tells the granularity type
 * 0 - LSB bits are in 64 bytes granularity
 * 1 - LSB bits are in 1K bytes granularity
 */
#define ICE_64_BYTE_GRANULARITY			0
#define ICE_KBYTE_GRANULARITY			BIT(11)
#define ICE_MIN_BURST_SIZE_ALLOWED		64 /* In Bytes */
#define ICE_MAX_BURST_SIZE_ALLOWED \
	((BIT(11) - 1) * 1024) /* In Bytes */
#define ICE_MAX_BURST_SIZE_64_BYTE_GRANULARITY \
	((BIT(11) - 1) * 64) /* In Bytes */
#define ICE_MAX_BURST_SIZE_KBYTE_GRANULARITY	ICE_MAX_BURST_SIZE_ALLOWED

#define ICE_RL_PROF_ACCURACY_BYTES 128
#define ICE_RL_PROF_MULTIPLIER 10000
#define ICE_RL_PROF_TS_MULTIPLIER 32
#define ICE_RL_PROF_FRACTION 512

#define ICE_PSM_CLK_367MHZ_IN_HZ 367647059
#define ICE_PSM_CLK_416MHZ_IN_HZ 416666667
#define ICE_PSM_CLK_446MHZ_IN_HZ 446428571
#define ICE_PSM_CLK_390MHZ_IN_HZ 390625000

/* BW rate limit profile parameters list entry along
 * with bandwidth maintained per layer in port info
 */
struct ice_aqc_rl_profile_info {
	struct ice_aqc_rl_profile_elem profile;
	struct list_head list_entry;
	u32 bw;			/* requested */
	u16 prof_id_ref;	/* profile ID to node association ref count */
};

struct ice_sched_agg_vsi_info {
	struct list_head list_entry;
	DECLARE_BITMAP(tc_bitmap, ICE_MAX_TRAFFIC_CLASS);
	u16 vsi_handle;
	/* save aggregator VSI TC bitmap */
	DECLARE_BITMAP(replay_tc_bitmap, ICE_MAX_TRAFFIC_CLASS);
};

struct ice_sched_agg_info {
	struct list_head agg_vsi_list;
	struct list_head list_entry;
	DECLARE_BITMAP(tc_bitmap, ICE_MAX_TRAFFIC_CLASS);
	u32 agg_id;
	enum ice_agg_type agg_type;
	/* save aggregator TC bitmap */
	DECLARE_BITMAP(replay_tc_bitmap, ICE_MAX_TRAFFIC_CLASS);
};

/* FW AQ command calls */
enum ice_status
ice_aq_query_sched_elems(struct ice_hw *hw, u16 elems_req,
			 struct ice_aqc_txsched_elem_data *buf, u16 buf_size,
			 u16 *elems_ret, struct ice_sq_cd *cd);
enum ice_status ice_sched_init_port(struct ice_port_info *pi);
enum ice_status ice_sched_query_res_alloc(struct ice_hw *hw);
void ice_sched_get_psm_clk_freq(struct ice_hw *hw);

void ice_sched_clear_port(struct ice_port_info *pi);
void ice_sched_cleanup_all(struct ice_hw *hw);
void ice_sched_clear_agg(struct ice_hw *hw);

struct ice_sched_node *
ice_sched_find_node_by_teid(struct ice_sched_node *start_node, u32 teid);
enum ice_status
ice_sched_add_node(struct ice_port_info *pi, u8 layer,
		   struct ice_aqc_txsched_elem_data *info);
void ice_free_sched_node(struct ice_port_info *pi, struct ice_sched_node *node);
struct ice_sched_node *ice_sched_get_tc_node(struct ice_port_info *pi, u8 tc);
struct ice_sched_node *
ice_sched_get_free_qparent(struct ice_port_info *pi, u16 vsi_handle, u8 tc,
			   u8 owner);
enum ice_status
ice_sched_cfg_vsi(struct ice_port_info *pi, u16 vsi_handle, u8 tc, u16 maxqs,
		  u8 owner, bool enable);
enum ice_status ice_rm_vsi_lan_cfg(struct ice_port_info *pi, u16 vsi_handle);
enum ice_status ice_rm_vsi_rdma_cfg(struct ice_port_info *pi, u16 vsi_handle);

/* Tx scheduler rate limiter functions */
enum ice_status
ice_cfg_agg(struct ice_port_info *pi, u32 agg_id,
	    enum ice_agg_type agg_type, u8 tc_bitmap);
enum ice_status
ice_move_vsi_to_agg(struct ice_port_info *pi, u32 agg_id, u16 vsi_handle,
		    u8 tc_bitmap);
enum ice_status
ice_cfg_q_bw_lmt(struct ice_port_info *pi, u16 vsi_handle, u8 tc,
		 u16 q_handle, enum ice_rl_type rl_type, u32 bw);
enum ice_status
ice_cfg_q_bw_dflt_lmt(struct ice_port_info *pi, u16 vsi_handle, u8 tc,
		      u16 q_handle, enum ice_rl_type rl_type);
enum ice_status ice_cfg_rl_burst_size(struct ice_hw *hw, u32 bytes);
void ice_sched_replay_agg_vsi_preinit(struct ice_hw *hw);
void ice_sched_replay_agg(struct ice_hw *hw);
enum ice_status ice_replay_vsi_agg(struct ice_hw *hw, u16 vsi_handle);
enum ice_status
ice_sched_replay_q_bw(struct ice_port_info *pi, struct ice_q_ctx *q_ctx);
#endif /* _ICE_SCHED_H_ */
