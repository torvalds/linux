/* Intel PRO/1000 Linux driver
 * Copyright(c) 1999 - 2014 Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * The full GNU General Public License is included in this distribution in
 * the file called "COPYING".
 *
 * Contact Information:
 * Linux NICS <linux.nics@intel.com>
 * e1000-devel Mailing List <e1000-devel@lists.sourceforge.net>
 * Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497
 */

#ifndef _E1000E_NVM_H_
#define _E1000E_NVM_H_

s32 e1000e_acquire_nvm(struct e1000_hw *hw);

s32 e1000e_poll_eerd_eewr_done(struct e1000_hw *hw, int ee_reg);
s32 e1000_read_mac_addr_generic(struct e1000_hw *hw);
s32 e1000_read_pba_string_generic(struct e1000_hw *hw, u8 *pba_num,
				  u32 pba_num_size);
s32 e1000e_read_nvm_eerd(struct e1000_hw *hw, u16 offset, u16 words, u16 *data);
s32 e1000e_valid_led_default(struct e1000_hw *hw, u16 *data);
s32 e1000e_validate_nvm_checksum_generic(struct e1000_hw *hw);
s32 e1000e_write_nvm_spi(struct e1000_hw *hw, u16 offset, u16 words, u16 *data);
s32 e1000e_update_nvm_checksum_generic(struct e1000_hw *hw);
void e1000e_release_nvm(struct e1000_hw *hw);

#define E1000_STM_OPCODE	0xDB00

#endif
