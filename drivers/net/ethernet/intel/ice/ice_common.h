/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2018, Intel Corporation. */

#ifndef _ICE_COMMON_H_
#define _ICE_COMMON_H_

#include "ice.h"
#include "ice_type.h"

void ice_debug_cq(struct ice_hw *hw, u32 mask, void *desc, void *buf,
		  u16 buf_len);
enum ice_status ice_init_hw(struct ice_hw *hw);
void ice_deinit_hw(struct ice_hw *hw);
enum ice_status ice_check_reset(struct ice_hw *hw);
enum ice_status ice_reset(struct ice_hw *hw, enum ice_reset_req req);
enum ice_status ice_init_all_ctrlq(struct ice_hw *hw);
void ice_shutdown_all_ctrlq(struct ice_hw *hw);
enum ice_status
ice_acquire_res(struct ice_hw *hw, enum ice_aq_res_ids res,
		enum ice_aq_res_access_type access);
void ice_release_res(struct ice_hw *hw, enum ice_aq_res_ids res);
enum ice_status ice_init_nvm(struct ice_hw *hw);
enum ice_status
ice_sq_send_cmd(struct ice_hw *hw, struct ice_ctl_q_info *cq,
		struct ice_aq_desc *desc, void *buf, u16 buf_size,
		struct ice_sq_cd *cd);
void ice_clear_pxe_mode(struct ice_hw *hw);
bool ice_check_sq_alive(struct ice_hw *hw, struct ice_ctl_q_info *cq);
enum ice_status ice_aq_q_shutdown(struct ice_hw *hw, bool unloading);
void ice_fill_dflt_direct_cmd_desc(struct ice_aq_desc *desc, u16 opcode);
enum ice_status
ice_aq_send_cmd(struct ice_hw *hw, struct ice_aq_desc *desc,
		void *buf, u16 buf_size, struct ice_sq_cd *cd);
enum ice_status ice_aq_get_fw_ver(struct ice_hw *hw, struct ice_sq_cd *cd);
enum ice_status ice_clear_pf_cfg(struct ice_hw *hw);
#endif /* _ICE_COMMON_H_ */
