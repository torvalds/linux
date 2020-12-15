/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2019, Intel Corporation. */

#ifndef _ICE_NVM_H_
#define _ICE_NVM_H_

enum ice_status
ice_acquire_nvm(struct ice_hw *hw, enum ice_aq_res_access_type access);
void ice_release_nvm(struct ice_hw *hw);
enum ice_status
ice_read_flat_nvm(struct ice_hw *hw, u32 offset, u32 *length, u8 *data,
		  bool read_shadow_ram);
enum ice_status
ice_get_pfa_module_tlv(struct ice_hw *hw, u16 *module_tlv, u16 *module_tlv_len,
		       u16 module_type);
enum ice_status
ice_read_pba_string(struct ice_hw *hw, u8 *pba_num, u32 pba_num_size);
enum ice_status ice_init_nvm(struct ice_hw *hw);
enum ice_status ice_read_sr_word(struct ice_hw *hw, u16 offset, u16 *data);
enum ice_status
ice_aq_update_nvm(struct ice_hw *hw, u16 module_typeid, u32 offset,
		  u16 length, void *data, bool last_command, u8 command_flags,
		  struct ice_sq_cd *cd);
enum ice_status
ice_aq_erase_nvm(struct ice_hw *hw, u16 module_typeid, struct ice_sq_cd *cd);
enum ice_status ice_nvm_validate_checksum(struct ice_hw *hw);
enum ice_status ice_nvm_write_activate(struct ice_hw *hw, u8 cmd_flags);
enum ice_status ice_aq_nvm_update_empr(struct ice_hw *hw);
enum ice_status
ice_nvm_set_pkg_data(struct ice_hw *hw, bool del_pkg_data_flag, u8 *data,
		     u16 length, struct ice_sq_cd *cd);
enum ice_status
ice_nvm_pass_component_tbl(struct ice_hw *hw, u8 *data, u16 length,
			   u8 transfer_flag, u8 *comp_response,
			   u8 *comp_response_code, struct ice_sq_cd *cd);
#endif /* _ICE_NVM_H_ */
