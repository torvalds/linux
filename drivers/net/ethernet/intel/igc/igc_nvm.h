/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c)  2018 Intel Corporation */

#ifndef _IGC_NVM_H_
#define _IGC_NVM_H_

s32 igc_acquire_nvm(struct igc_hw *hw);
void igc_release_nvm(struct igc_hw *hw);
s32 igc_read_mac_addr(struct igc_hw *hw);
s32 igc_read_nvm_eerd(struct igc_hw *hw, u16 offset, u16 words, u16 *data);
s32 igc_validate_nvm_checksum(struct igc_hw *hw);
s32 igc_update_nvm_checksum(struct igc_hw *hw);

#endif
