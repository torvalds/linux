// SPDX-License-Identifier: GPL-2.0
/* Copyright (c)  2018 Intel Corporation */

#include "igc_mac.h"
#include "igc_nvm.h"

/**
 * igc_poll_eerd_eewr_done - Poll for EEPROM read/write completion
 * @hw: pointer to the HW structure
 * @ee_reg: EEPROM flag for polling
 *
 * Polls the EEPROM status bit for either read or write completion based
 * upon the value of 'ee_reg'.
 */
static s32 igc_poll_eerd_eewr_done(struct igc_hw *hw, int ee_reg)
{
	s32 ret_val = -IGC_ERR_NVM;
	u32 attempts = 100000;
	u32 i, reg = 0;

	for (i = 0; i < attempts; i++) {
		if (ee_reg == IGC_NVM_POLL_READ)
			reg = rd32(IGC_EERD);
		else
			reg = rd32(IGC_EEWR);

		if (reg & IGC_NVM_RW_REG_DONE) {
			ret_val = 0;
			break;
		}

		udelay(5);
	}

	return ret_val;
}

/**
 * igc_read_nvm_eerd - Reads EEPROM using EERD register
 * @hw: pointer to the HW structure
 * @offset: offset of word in the EEPROM to read
 * @words: number of words to read
 * @data: word read from the EEPROM
 *
 * Reads a 16 bit word from the EEPROM using the EERD register.
 */
s32 igc_read_nvm_eerd(struct igc_hw *hw, u16 offset, u16 words, u16 *data)
{
	struct igc_nvm_info *nvm = &hw->nvm;
	u32 i, eerd = 0;
	s32 ret_val = 0;

	/* A check for invalid values:  offset too large, too many words,
	 * and not enough words.
	 */
	if (offset >= nvm->word_size || (words > (nvm->word_size - offset)) ||
	    words == 0) {
		hw_dbg("nvm parameter(s) out of bounds\n");
		ret_val = -IGC_ERR_NVM;
		goto out;
	}

	for (i = 0; i < words; i++) {
		eerd = ((offset + i) << IGC_NVM_RW_ADDR_SHIFT) +
			IGC_NVM_RW_REG_START;

		wr32(IGC_EERD, eerd);
		ret_val = igc_poll_eerd_eewr_done(hw, IGC_NVM_POLL_READ);
		if (ret_val)
			break;

		data[i] = (rd32(IGC_EERD) >> IGC_NVM_RW_REG_DATA);
	}

out:
	return ret_val;
}

/**
 * igc_read_mac_addr - Read device MAC address
 * @hw: pointer to the HW structure
 */
s32 igc_read_mac_addr(struct igc_hw *hw)
{
	u32 rar_high;
	u32 rar_low;
	u16 i;

	rar_high = rd32(IGC_RAH(0));
	rar_low = rd32(IGC_RAL(0));

	for (i = 0; i < IGC_RAL_MAC_ADDR_LEN; i++)
		hw->mac.perm_addr[i] = (u8)(rar_low >> (i * 8));

	for (i = 0; i < IGC_RAH_MAC_ADDR_LEN; i++)
		hw->mac.perm_addr[i + 4] = (u8)(rar_high >> (i * 8));

	for (i = 0; i < ETH_ALEN; i++)
		hw->mac.addr[i] = hw->mac.perm_addr[i];

	return 0;
}

/**
 * igc_validate_nvm_checksum - Validate EEPROM checksum
 * @hw: pointer to the HW structure
 *
 * Calculates the EEPROM checksum by reading/adding each word of the EEPROM
 * and then verifies that the sum of the EEPROM is equal to 0xBABA.
 */
s32 igc_validate_nvm_checksum(struct igc_hw *hw)
{
	u16 checksum = 0;
	u16 i, nvm_data;
	s32 ret_val = 0;

	for (i = 0; i < (NVM_CHECKSUM_REG + 1); i++) {
		ret_val = hw->nvm.ops.read(hw, i, 1, &nvm_data);
		if (ret_val) {
			hw_dbg("NVM Read Error\n");
			goto out;
		}
		checksum += nvm_data;
	}

	if (checksum != (u16)NVM_SUM) {
		hw_dbg("NVM Checksum Invalid\n");
		ret_val = -IGC_ERR_NVM;
		goto out;
	}

out:
	return ret_val;
}

/**
 * igc_update_nvm_checksum - Update EEPROM checksum
 * @hw: pointer to the HW structure
 *
 * Updates the EEPROM checksum by reading/adding each word of the EEPROM
 * up to the checksum.  Then calculates the EEPROM checksum and writes the
 * value to the EEPROM.
 */
s32 igc_update_nvm_checksum(struct igc_hw *hw)
{
	u16 checksum = 0;
	u16 i, nvm_data;
	s32  ret_val;

	for (i = 0; i < NVM_CHECKSUM_REG; i++) {
		ret_val = hw->nvm.ops.read(hw, i, 1, &nvm_data);
		if (ret_val) {
			hw_dbg("NVM Read Error while updating checksum.\n");
			goto out;
		}
		checksum += nvm_data;
	}
	checksum = (u16)NVM_SUM - checksum;
	ret_val = hw->nvm.ops.write(hw, NVM_CHECKSUM_REG, 1, &checksum);
	if (ret_val)
		hw_dbg("NVM Write Error while updating checksum.\n");

out:
	return ret_val;
}
