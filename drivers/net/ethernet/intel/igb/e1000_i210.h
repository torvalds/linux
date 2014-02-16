/*******************************************************************************

  Intel(R) Gigabit Ethernet Linux driver
  Copyright(c) 2007-2013 Intel Corporation.

  This program is free software; you can redistribute it and/or modify it
  under the terms and conditions of the GNU General Public License,
  version 2, as published by the Free Software Foundation.

  This program is distributed in the hope it will be useful, but WITHOUT
  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
  more details.

  You should have received a copy of the GNU General Public License along with
  this program; if not, write to the Free Software Foundation, Inc.,
  51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.

  The full GNU General Public License is included in this distribution in
  the file called "COPYING".

  Contact Information:
  e1000-devel Mailing List <e1000-devel@lists.sourceforge.net>
  Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497

*******************************************************************************/

#ifndef _E1000_I210_H_
#define _E1000_I210_H_

s32 igb_update_flash_i210(struct e1000_hw *hw);
s32 igb_update_nvm_checksum_i210(struct e1000_hw *hw);
s32 igb_validate_nvm_checksum_i210(struct e1000_hw *hw);
s32 igb_write_nvm_srwr_i210(struct e1000_hw *hw, u16 offset, u16 words,
			    u16 *data);
s32 igb_read_nvm_srrd_i210(struct e1000_hw *hw, u16 offset, u16 words,
			   u16 *data);
s32 igb_acquire_swfw_sync_i210(struct e1000_hw *hw, u16 mask);
void igb_release_swfw_sync_i210(struct e1000_hw *hw, u16 mask);
s32 igb_acquire_nvm_i210(struct e1000_hw *hw);
void igb_release_nvm_i210(struct e1000_hw *hw);
s32 igb_valid_led_default_i210(struct e1000_hw *hw, u16 *data);
s32 igb_read_invm_version(struct e1000_hw *hw,
			  struct e1000_fw_version *invm_ver);
s32 igb_read_xmdio_reg(struct e1000_hw *hw, u16 addr, u8 dev_addr, u16 *data);
s32 igb_write_xmdio_reg(struct e1000_hw *hw, u16 addr, u8 dev_addr, u16 data);
s32 igb_init_nvm_params_i210(struct e1000_hw *hw);
bool igb_get_flash_presence_i210(struct e1000_hw *hw);

#define E1000_STM_OPCODE		0xDB00
#define E1000_EEPROM_FLASH_SIZE_WORD	0x11

#define INVM_DWORD_TO_RECORD_TYPE(invm_dword) \
	(u8)((invm_dword) & 0x7)
#define INVM_DWORD_TO_WORD_ADDRESS(invm_dword) \
	(u8)(((invm_dword) & 0x0000FE00) >> 9)
#define INVM_DWORD_TO_WORD_DATA(invm_dword) \
	(u16)(((invm_dword) & 0xFFFF0000) >> 16)

enum E1000_INVM_STRUCTURE_TYPE {
	E1000_INVM_UNINITIALIZED_STRUCTURE		= 0x00,
	E1000_INVM_WORD_AUTOLOAD_STRUCTURE		= 0x01,
	E1000_INVM_CSR_AUTOLOAD_STRUCTURE		= 0x02,
	E1000_INVM_PHY_REGISTER_AUTOLOAD_STRUCTURE	= 0x03,
	E1000_INVM_RSA_KEY_SHA256_STRUCTURE		= 0x04,
	E1000_INVM_INVALIDATED_STRUCTURE		= 0x0F,
};

#define E1000_INVM_RSA_KEY_SHA256_DATA_SIZE_IN_DWORDS	8
#define E1000_INVM_CSR_AUTOLOAD_DATA_SIZE_IN_DWORDS	1
#define E1000_INVM_ULT_BYTES_SIZE			8
#define E1000_INVM_RECORD_SIZE_IN_BYTES			4
#define E1000_INVM_VER_FIELD_ONE			0x1FF8
#define E1000_INVM_VER_FIELD_TWO			0x7FE000
#define E1000_INVM_IMGTYPE_FIELD			0x1F800000

#define E1000_INVM_MAJOR_MASK		0x3F0
#define E1000_INVM_MINOR_MASK		0xF
#define E1000_INVM_MAJOR_SHIFT		4

#define ID_LED_DEFAULT_I210		((ID_LED_OFF1_ON2  << 8) | \
					 (ID_LED_DEF1_DEF2 <<  4) | \
					 (ID_LED_OFF1_OFF2))
#define ID_LED_DEFAULT_I210_SERDES	((ID_LED_DEF1_DEF2 << 8) | \
					 (ID_LED_DEF1_DEF2 <<  4) | \
					 (ID_LED_OFF1_ON2))

/* NVM offset defaults for i211 device */
#define NVM_INIT_CTRL_2_DEFAULT_I211	0X7243
#define NVM_INIT_CTRL_4_DEFAULT_I211	0x00C1
#define NVM_LED_1_CFG_DEFAULT_I211	0x0184
#define NVM_LED_0_2_CFG_DEFAULT_I211	0x200C

#endif
