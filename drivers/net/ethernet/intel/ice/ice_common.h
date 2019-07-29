/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2018, Intel Corporation. */

#ifndef _ICE_COMMON_H_
#define _ICE_COMMON_H_

#include "ice.h"
#include "ice_type.h"
#include "ice_switch.h"
#include <linux/avf/virtchnl.h>

void ice_debug_cq(struct ice_hw *hw, u32 mask, void *desc, void *buf,
		  u16 buf_len);
enum ice_status ice_init_hw(struct ice_hw *hw);
void ice_deinit_hw(struct ice_hw *hw);
enum ice_status ice_check_reset(struct ice_hw *hw);
enum ice_status ice_reset(struct ice_hw *hw, enum ice_reset_req req);
enum ice_status ice_init_all_ctrlq(struct ice_hw *hw);
void ice_shutdown_all_ctrlq(struct ice_hw *hw);
enum ice_status
ice_clean_rq_elem(struct ice_hw *hw, struct ice_ctl_q_info *cq,
		  struct ice_rq_event_info *e, u16 *pending);
enum ice_status
ice_get_link_status(struct ice_port_info *pi, bool *link_up);
enum ice_status ice_update_link_info(struct ice_port_info *pi);
enum ice_status
ice_acquire_res(struct ice_hw *hw, enum ice_aq_res_ids res,
		enum ice_aq_res_access_type access, u32 timeout);
void ice_release_res(struct ice_hw *hw, enum ice_aq_res_ids res);
enum ice_status ice_init_nvm(struct ice_hw *hw);
enum ice_status
ice_sq_send_cmd(struct ice_hw *hw, struct ice_ctl_q_info *cq,
		struct ice_aq_desc *desc, void *buf, u16 buf_size,
		struct ice_sq_cd *cd);
void ice_clear_pxe_mode(struct ice_hw *hw);
enum ice_status ice_get_caps(struct ice_hw *hw);

void ice_dev_onetime_setup(struct ice_hw *hw);

enum ice_status
ice_write_rxq_ctx(struct ice_hw *hw, struct ice_rlan_ctx *rlan_ctx,
		  u32 rxq_index);

enum ice_status
ice_aq_get_rss_lut(struct ice_hw *hw, u16 vsi_handle, u8 lut_type, u8 *lut,
		   u16 lut_size);
enum ice_status
ice_aq_set_rss_lut(struct ice_hw *hw, u16 vsi_handle, u8 lut_type, u8 *lut,
		   u16 lut_size);
enum ice_status
ice_aq_get_rss_key(struct ice_hw *hw, u16 vsi_handle,
		   struct ice_aqc_get_set_rss_keys *keys);
enum ice_status
ice_aq_set_rss_key(struct ice_hw *hw, u16 vsi_handle,
		   struct ice_aqc_get_set_rss_keys *keys);

bool ice_check_sq_alive(struct ice_hw *hw, struct ice_ctl_q_info *cq);
enum ice_status ice_aq_q_shutdown(struct ice_hw *hw, bool unloading);
void ice_fill_dflt_direct_cmd_desc(struct ice_aq_desc *desc, u16 opcode);
extern const struct ice_ctx_ele ice_tlan_ctx_info[];
enum ice_status
ice_set_ctx(u8 *src_ctx, u8 *dest_ctx, const struct ice_ctx_ele *ce_info);
enum ice_status
ice_aq_send_cmd(struct ice_hw *hw, struct ice_aq_desc *desc,
		void *buf, u16 buf_size, struct ice_sq_cd *cd);
enum ice_status ice_aq_get_fw_ver(struct ice_hw *hw, struct ice_sq_cd *cd);

enum ice_status
ice_aq_get_phy_caps(struct ice_port_info *pi, bool qual_mods, u8 report_mode,
		    struct ice_aqc_get_phy_caps_data *caps,
		    struct ice_sq_cd *cd);
void
ice_update_phy_type(u64 *phy_type_low, u16 link_speeds_bitmap);
enum ice_status
ice_aq_manage_mac_write(struct ice_hw *hw, u8 *mac_addr, u8 flags,
			struct ice_sq_cd *cd);
enum ice_status ice_clear_pf_cfg(struct ice_hw *hw);
enum ice_status
ice_aq_set_phy_cfg(struct ice_hw *hw, u8 lport,
		   struct ice_aqc_set_phy_cfg_data *cfg, struct ice_sq_cd *cd);
enum ice_status
ice_set_fc(struct ice_port_info *pi, u8 *aq_failures,
	   bool ena_auto_link_update);

enum ice_status
ice_aq_set_link_restart_an(struct ice_port_info *pi, bool ena_link,
			   struct ice_sq_cd *cd);
enum ice_status
ice_dis_vsi_txq(struct ice_port_info *pi, u8 num_queues, u16 *q_ids,
		u32 *q_teids, enum ice_disq_rst_src rst_src, u16 vmvf_num,
		struct ice_sq_cd *cmd_details);
enum ice_status
ice_cfg_vsi_lan(struct ice_port_info *pi, u16 vsi_handle, u8 tc_bitmap,
		u16 *max_lanqs);
enum ice_status
ice_ena_vsi_txq(struct ice_port_info *pi, u16 vsi_handle, u8 tc, u8 num_qgrps,
		struct ice_aqc_add_tx_qgrp *buf, u16 buf_size,
		struct ice_sq_cd *cd);
enum ice_status ice_replay_vsi(struct ice_hw *hw, u16 vsi_handle);
void ice_replay_post(struct ice_hw *hw);
void ice_output_fw_log(struct ice_hw *hw, struct ice_aq_desc *desc, void *buf);
void ice_stat_update40(struct ice_hw *hw, u32 hireg, u32 loreg,
		       bool prev_stat_loaded, u64 *prev_stat, u64 *cur_stat);
void ice_stat_update32(struct ice_hw *hw, u32 reg, bool prev_stat_loaded,
		       u64 *prev_stat, u64 *cur_stat);
#endif /* _ICE_COMMON_H_ */
