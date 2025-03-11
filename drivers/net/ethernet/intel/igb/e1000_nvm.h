/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright(c) 2007 - 2018 Intel Corporation. */

#ifndef _E1000_NVM_H_
#define _E1000_NVM_H_

s32  igb_acquire_nvm(struct e1000_hw *hw);
void igb_release_nvm(struct e1000_hw *hw);
s32  igb_read_mac_addr(struct e1000_hw *hw);
s32  igb_read_part_string(struct e1000_hw *hw, u8 *part_num,
			  u32 part_num_size);
s32  igb_read_nvm_eerd(struct e1000_hw *hw, u16 offset, u16 words, u16 *data);
s32  igb_read_nvm_spi(struct e1000_hw *hw, u16 offset, u16 words, u16 *data);
s32  igb_write_nvm_spi(struct e1000_hw *hw, u16 offset, u16 words, u16 *data);
s32  igb_validate_nvm_checksum(struct e1000_hw *hw);
s32  igb_update_nvm_checksum(struct e1000_hw *hw);

struct e1000_fw_version {
	u32 etrack_id;
	u16 eep_major;
	u16 eep_minor;
	u16 eep_build;

	u8 invm_major;
	u8 invm_minor;
	u8 invm_img_type;

	bool or_valid;
	u16 or_major;
	u16 or_build;
	u16 or_patch;
};
void igb_get_fw_version(struct e1000_hw *hw, struct e1000_fw_version *fw_vers);

#endif
