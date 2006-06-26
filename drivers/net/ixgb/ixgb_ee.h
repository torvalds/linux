/*******************************************************************************

  
  Copyright(c) 1999 - 2006 Intel Corporation. All rights reserved.
  
  This program is free software; you can redistribute it and/or modify it 
  under the terms of the GNU General Public License as published by the Free 
  Software Foundation; either version 2 of the License, or (at your option) 
  any later version.
  
  This program is distributed in the hope that it will be useful, but WITHOUT 
  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or 
  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for 
  more details.
  
  You should have received a copy of the GNU General Public License along with
  this program; if not, write to the Free Software Foundation, Inc., 59 
  Temple Place - Suite 330, Boston, MA  02111-1307, USA.
  
  The full GNU General Public License is included in this distribution in the
  file called LICENSE.
  
  Contact Information:
  Linux NICS <linux.nics@intel.com>
  Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497

*******************************************************************************/

#ifndef _IXGB_EE_H_
#define _IXGB_EE_H_

#define IXGB_EEPROM_SIZE    64	/* Size in words */

#define IXGB_ETH_LENGTH_OF_ADDRESS   6

/* EEPROM Commands */
#define EEPROM_READ_OPCODE  0x6	/* EERPOM read opcode */
#define EEPROM_WRITE_OPCODE 0x5	/* EERPOM write opcode */
#define EEPROM_ERASE_OPCODE 0x7	/* EERPOM erase opcode */
#define EEPROM_EWEN_OPCODE  0x13	/* EERPOM erase/write enable */
#define EEPROM_EWDS_OPCODE  0x10	/* EERPOM erast/write disable */

/* EEPROM MAP (Word Offsets) */
#define EEPROM_IA_1_2_REG        0x0000
#define EEPROM_IA_3_4_REG        0x0001
#define EEPROM_IA_5_6_REG        0x0002
#define EEPROM_COMPATIBILITY_REG 0x0003
#define EEPROM_PBA_1_2_REG       0x0008
#define EEPROM_PBA_3_4_REG       0x0009
#define EEPROM_INIT_CONTROL1_REG 0x000A
#define EEPROM_SUBSYS_ID_REG     0x000B
#define EEPROM_SUBVEND_ID_REG    0x000C
#define EEPROM_DEVICE_ID_REG     0x000D
#define EEPROM_VENDOR_ID_REG     0x000E
#define EEPROM_INIT_CONTROL2_REG 0x000F
#define EEPROM_SWDPINS_REG       0x0020
#define EEPROM_CIRCUIT_CTRL_REG  0x0021
#define EEPROM_D0_D3_POWER_REG   0x0022
#define EEPROM_FLASH_VERSION     0x0032
#define EEPROM_CHECKSUM_REG      0x003F

/* Mask bits for fields in Word 0x0a of the EEPROM */

#define EEPROM_ICW1_SIGNATURE_MASK  0xC000
#define EEPROM_ICW1_SIGNATURE_VALID 0x4000
#define EEPROM_ICW1_SIGNATURE_CLEAR 0x0000

/* For checksumming, the sum of all words in the EEPROM should equal 0xBABA. */
#define EEPROM_SUM 0xBABA

/* EEPROM Map Sizes (Byte Counts) */
#define PBA_SIZE 4

/* EEPROM Map defines (WORD OFFSETS)*/

/* EEPROM structure */
struct ixgb_ee_map_type {
	uint8_t mac_addr[IXGB_ETH_LENGTH_OF_ADDRESS];
	uint16_t compatibility;
	uint16_t reserved1[4];
	uint32_t pba_number;
	uint16_t init_ctrl_reg_1;
	uint16_t subsystem_id;
	uint16_t subvendor_id;
	uint16_t device_id;
	uint16_t vendor_id;
	uint16_t init_ctrl_reg_2;
	uint16_t oem_reserved[16];
	uint16_t swdpins_reg;
	uint16_t circuit_ctrl_reg;
	uint8_t d3_power;
	uint8_t d0_power;
	uint16_t reserved2[28];
	uint16_t checksum;
};

/* EEPROM Functions */
uint16_t ixgb_read_eeprom(struct ixgb_hw *hw, uint16_t reg);

boolean_t ixgb_validate_eeprom_checksum(struct ixgb_hw *hw);

void ixgb_update_eeprom_checksum(struct ixgb_hw *hw);

void ixgb_write_eeprom(struct ixgb_hw *hw, uint16_t reg, uint16_t data);

#endif				/* IXGB_EE_H */
