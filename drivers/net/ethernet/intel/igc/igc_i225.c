// SPDX-License-Identifier: GPL-2.0
/* Copyright (c)  2018 Intel Corporation */

#include <linux/delay.h>

#include "igc_hw.h"

/**
 * igc_get_hw_semaphore_i225 - Acquire hardware semaphore
 * @hw: pointer to the HW structure
 *
 * Acquire the necessary semaphores for exclusive access to the EEPROM.
 * Set the EEPROM access request bit and wait for EEPROM access grant bit.
 * Return successful if access grant bit set, else clear the request for
 * EEPROM access and return -IGC_ERR_NVM (-1).
 */
static s32 igc_acquire_nvm_i225(struct igc_hw *hw)
{
	return igc_acquire_swfw_sync_i225(hw, IGC_SWFW_EEP_SM);
}

/**
 * igc_release_nvm_i225 - Release exclusive access to EEPROM
 * @hw: pointer to the HW structure
 *
 * Stop any current commands to the EEPROM and clear the EEPROM request bit,
 * then release the semaphores acquired.
 */
static void igc_release_nvm_i225(struct igc_hw *hw)
{
	igc_release_swfw_sync_i225(hw, IGC_SWFW_EEP_SM);
}

/**
 * igc_get_hw_semaphore_i225 - Acquire hardware semaphore
 * @hw: pointer to the HW structure
 *
 * Acquire the HW semaphore to access the PHY or NVM
 */
static s32 igc_get_hw_semaphore_i225(struct igc_hw *hw)
{
	s32 timeout = hw->nvm.word_size + 1;
	s32 i = 0;
	u32 swsm;

	/* Get the SW semaphore */
	while (i < timeout) {
		swsm = rd32(IGC_SWSM);
		if (!(swsm & IGC_SWSM_SMBI))
			break;

		usleep_range(500, 600);
		i++;
	}

	if (i == timeout) {
		/* In rare circumstances, the SW semaphore may already be held
		 * unintentionally. Clear the semaphore once before giving up.
		 */
		if (hw->dev_spec._base.clear_semaphore_once) {
			hw->dev_spec._base.clear_semaphore_once = false;
			igc_put_hw_semaphore(hw);
			for (i = 0; i < timeout; i++) {
				swsm = rd32(IGC_SWSM);
				if (!(swsm & IGC_SWSM_SMBI))
					break;

				usleep_range(500, 600);
			}
		}

		/* If we do not have the semaphore here, we have to give up. */
		if (i == timeout) {
			hw_dbg("Driver can't access device - SMBI bit is set.\n");
			return -IGC_ERR_NVM;
		}
	}

	/* Get the FW semaphore. */
	for (i = 0; i < timeout; i++) {
		swsm = rd32(IGC_SWSM);
		wr32(IGC_SWSM, swsm | IGC_SWSM_SWESMBI);

		/* Semaphore acquired if bit latched */
		if (rd32(IGC_SWSM) & IGC_SWSM_SWESMBI)
			break;

		usleep_range(500, 600);
	}

	if (i == timeout) {
		/* Release semaphores */
		igc_put_hw_semaphore(hw);
		hw_dbg("Driver can't access the NVM\n");
		return -IGC_ERR_NVM;
	}

	return 0;
}

/**
 * igc_acquire_swfw_sync_i225 - Acquire SW/FW semaphore
 * @hw: pointer to the HW structure
 * @mask: specifies which semaphore to acquire
 *
 * Acquire the SW/FW semaphore to access the PHY or NVM.  The mask
 * will also specify which port we're acquiring the lock for.
 */
s32 igc_acquire_swfw_sync_i225(struct igc_hw *hw, u16 mask)
{
	s32 i = 0, timeout = 200;
	u32 fwmask = mask << 16;
	u32 swmask = mask;
	s32 ret_val = 0;
	u32 swfw_sync;

	while (i < timeout) {
		if (igc_get_hw_semaphore_i225(hw)) {
			ret_val = -IGC_ERR_SWFW_SYNC;
			goto out;
		}

		swfw_sync = rd32(IGC_SW_FW_SYNC);
		if (!(swfw_sync & (fwmask | swmask)))
			break;

		/* Firmware currently using resource (fwmask) */
		igc_put_hw_semaphore(hw);
		mdelay(5);
		i++;
	}

	if (i == timeout) {
		hw_dbg("Driver can't access resource, SW_FW_SYNC timeout.\n");
		ret_val = -IGC_ERR_SWFW_SYNC;
		goto out;
	}

	swfw_sync |= swmask;
	wr32(IGC_SW_FW_SYNC, swfw_sync);

	igc_put_hw_semaphore(hw);
out:
	return ret_val;
}

/**
 * igc_release_swfw_sync_i225 - Release SW/FW semaphore
 * @hw: pointer to the HW structure
 * @mask: specifies which semaphore to acquire
 *
 * Release the SW/FW semaphore used to access the PHY or NVM.  The mask
 * will also specify which port we're releasing the lock for.
 */
void igc_release_swfw_sync_i225(struct igc_hw *hw, u16 mask)
{
	u32 swfw_sync;

	while (igc_get_hw_semaphore_i225(hw))
		; /* Empty */

	swfw_sync = rd32(IGC_SW_FW_SYNC);
	swfw_sync &= ~mask;
	wr32(IGC_SW_FW_SYNC, swfw_sync);

	igc_put_hw_semaphore(hw);
}

/**
 * igc_read_nvm_srrd_i225 - Reads Shadow Ram using EERD register
 * @hw: pointer to the HW structure
 * @offset: offset of word in the Shadow Ram to read
 * @words: number of words to read
 * @data: word read from the Shadow Ram
 *
 * Reads a 16 bit word from the Shadow Ram using the EERD register.
 * Uses necessary synchronization semaphores.
 */
static s32 igc_read_nvm_srrd_i225(struct igc_hw *hw, u16 offset, u16 words,
				  u16 *data)
{
	s32 status = 0;
	u16 i, count;

	/* We cannot hold synchronization semaphores for too long,
	 * because of forceful takeover procedure. However it is more efficient
	 * to read in bursts than synchronizing access for each word.
	 */
	for (i = 0; i < words; i += IGC_EERD_EEWR_MAX_COUNT) {
		count = (words - i) / IGC_EERD_EEWR_MAX_COUNT > 0 ?
			IGC_EERD_EEWR_MAX_COUNT : (words - i);

		status = hw->nvm.ops.acquire(hw);
		if (status)
			break;

		status = igc_read_nvm_eerd(hw, offset, count, data + i);
		hw->nvm.ops.release(hw);
		if (status)
			break;
	}

	return status;
}

/**
 * igc_write_nvm_srwr - Write to Shadow Ram using EEWR
 * @hw: pointer to the HW structure
 * @offset: offset within the Shadow Ram to be written to
 * @words: number of words to write
 * @data: 16 bit word(s) to be written to the Shadow Ram
 *
 * Writes data to Shadow Ram at offset using EEWR register.
 *
 * If igc_update_nvm_checksum is not called after this function , the
 * Shadow Ram will most likely contain an invalid checksum.
 */
static s32 igc_write_nvm_srwr(struct igc_hw *hw, u16 offset, u16 words,
			      u16 *data)
{
	struct igc_nvm_info *nvm = &hw->nvm;
	u32 attempts = 100000;
	u32 i, k, eewr = 0;
	s32 ret_val = 0;

	/* A check for invalid values:  offset too large, too many words,
	 * too many words for the offset, and not enough words.
	 */
	if (offset >= nvm->word_size || (words > (nvm->word_size - offset)) ||
	    words == 0) {
		hw_dbg("nvm parameter(s) out of bounds\n");
		ret_val = -IGC_ERR_NVM;
		goto out;
	}

	for (i = 0; i < words; i++) {
		eewr = ((offset + i) << IGC_NVM_RW_ADDR_SHIFT) |
			(data[i] << IGC_NVM_RW_REG_DATA) |
			IGC_NVM_RW_REG_START;

		wr32(IGC_SRWR, eewr);

		for (k = 0; k < attempts; k++) {
			if (IGC_NVM_RW_REG_DONE &
			    rd32(IGC_SRWR)) {
				ret_val = 0;
				break;
			}
			udelay(5);
		}

		if (ret_val) {
			hw_dbg("Shadow RAM write EEWR timed out\n");
			break;
		}
	}

out:
	return ret_val;
}

/**
 * igc_write_nvm_srwr_i225 - Write to Shadow RAM using EEWR
 * @hw: pointer to the HW structure
 * @offset: offset within the Shadow RAM to be written to
 * @words: number of words to write
 * @data: 16 bit word(s) to be written to the Shadow RAM
 *
 * Writes data to Shadow RAM at offset using EEWR register.
 *
 * If igc_update_nvm_checksum is not called after this function , the
 * data will not be committed to FLASH and also Shadow RAM will most likely
 * contain an invalid checksum.
 *
 * If error code is returned, data and Shadow RAM may be inconsistent - buffer
 * partially written.
 */
static s32 igc_write_nvm_srwr_i225(struct igc_hw *hw, u16 offset, u16 words,
				   u16 *data)
{
	s32 status = 0;
	u16 i, count;

	/* We cannot hold synchronization semaphores for too long,
	 * because of forceful takeover procedure. However it is more efficient
	 * to write in bursts than synchronizing access for each word.
	 */
	for (i = 0; i < words; i += IGC_EERD_EEWR_MAX_COUNT) {
		count = (words - i) / IGC_EERD_EEWR_MAX_COUNT > 0 ?
			IGC_EERD_EEWR_MAX_COUNT : (words - i);

		status = hw->nvm.ops.acquire(hw);
		if (status)
			break;

		status = igc_write_nvm_srwr(hw, offset, count, data + i);
		hw->nvm.ops.release(hw);
		if (status)
			break;
	}

	return status;
}

/**
 * igc_validate_nvm_checksum_i225 - Validate EEPROM checksum
 * @hw: pointer to the HW structure
 *
 * Calculates the EEPROM checksum by reading/adding each word of the EEPROM
 * and then verifies that the sum of the EEPROM is equal to 0xBABA.
 */
static s32 igc_validate_nvm_checksum_i225(struct igc_hw *hw)
{
	s32 (*read_op_ptr)(struct igc_hw *hw, u16 offset, u16 count,
			   u16 *data);
	s32 status = 0;

	status = hw->nvm.ops.acquire(hw);
	if (status)
		goto out;

	/* Replace the read function with semaphore grabbing with
	 * the one that skips this for a while.
	 * We have semaphore taken already here.
	 */
	read_op_ptr = hw->nvm.ops.read;
	hw->nvm.ops.read = igc_read_nvm_eerd;

	status = igc_validate_nvm_checksum(hw);

	/* Revert original read operation. */
	hw->nvm.ops.read = read_op_ptr;

	hw->nvm.ops.release(hw);

out:
	return status;
}

/**
 * igc_pool_flash_update_done_i225 - Pool FLUDONE status
 * @hw: pointer to the HW structure
 */
static s32 igc_pool_flash_update_done_i225(struct igc_hw *hw)
{
	s32 ret_val = -IGC_ERR_NVM;
	u32 i, reg;

	for (i = 0; i < IGC_FLUDONE_ATTEMPTS; i++) {
		reg = rd32(IGC_EECD);
		if (reg & IGC_EECD_FLUDONE_I225) {
			ret_val = 0;
			break;
		}
		udelay(5);
	}

	return ret_val;
}

/**
 * igc_update_flash_i225 - Commit EEPROM to the flash
 * @hw: pointer to the HW structure
 */
static s32 igc_update_flash_i225(struct igc_hw *hw)
{
	s32 ret_val = 0;
	u32 flup;

	ret_val = igc_pool_flash_update_done_i225(hw);
	if (ret_val == -IGC_ERR_NVM) {
		hw_dbg("Flash update time out\n");
		goto out;
	}

	flup = rd32(IGC_EECD) | IGC_EECD_FLUPD_I225;
	wr32(IGC_EECD, flup);

	ret_val = igc_pool_flash_update_done_i225(hw);
	if (ret_val)
		hw_dbg("Flash update time out\n");
	else
		hw_dbg("Flash update complete\n");

out:
	return ret_val;
}

/**
 * igc_update_nvm_checksum_i225 - Update EEPROM checksum
 * @hw: pointer to the HW structure
 *
 * Updates the EEPROM checksum by reading/adding each word of the EEPROM
 * up to the checksum.  Then calculates the EEPROM checksum and writes the
 * value to the EEPROM. Next commit EEPROM data onto the Flash.
 */
static s32 igc_update_nvm_checksum_i225(struct igc_hw *hw)
{
	u16 checksum = 0;
	s32 ret_val = 0;
	u16 i, nvm_data;

	/* Read the first word from the EEPROM. If this times out or fails, do
	 * not continue or we could be in for a very long wait while every
	 * EEPROM read fails
	 */
	ret_val = igc_read_nvm_eerd(hw, 0, 1, &nvm_data);
	if (ret_val) {
		hw_dbg("EEPROM read failed\n");
		goto out;
	}

	ret_val = hw->nvm.ops.acquire(hw);
	if (ret_val)
		goto out;

	/* Do not use hw->nvm.ops.write, hw->nvm.ops.read
	 * because we do not want to take the synchronization
	 * semaphores twice here.
	 */

	for (i = 0; i < NVM_CHECKSUM_REG; i++) {
		ret_val = igc_read_nvm_eerd(hw, i, 1, &nvm_data);
		if (ret_val) {
			hw->nvm.ops.release(hw);
			hw_dbg("NVM Read Error while updating checksum.\n");
			goto out;
		}
		checksum += nvm_data;
	}
	checksum = (u16)NVM_SUM - checksum;
	ret_val = igc_write_nvm_srwr(hw, NVM_CHECKSUM_REG, 1,
				     &checksum);
	if (ret_val) {
		hw->nvm.ops.release(hw);
		hw_dbg("NVM Write Error while updating checksum.\n");
		goto out;
	}

	hw->nvm.ops.release(hw);

	ret_val = igc_update_flash_i225(hw);

out:
	return ret_val;
}

/**
 * igc_get_flash_presence_i225 - Check if flash device is detected
 * @hw: pointer to the HW structure
 */
bool igc_get_flash_presence_i225(struct igc_hw *hw)
{
	bool ret_val = false;
	u32 eec = 0;

	eec = rd32(IGC_EECD);
	if (eec & IGC_EECD_FLASH_DETECTED_I225)
		ret_val = true;

	return ret_val;
}

/**
 * igc_init_nvm_params_i225 - Init NVM func ptrs.
 * @hw: pointer to the HW structure
 */
s32 igc_init_nvm_params_i225(struct igc_hw *hw)
{
	struct igc_nvm_info *nvm = &hw->nvm;

	nvm->ops.acquire = igc_acquire_nvm_i225;
	nvm->ops.release = igc_release_nvm_i225;

	/* NVM Function Pointers */
	if (igc_get_flash_presence_i225(hw)) {
		hw->nvm.type = igc_nvm_flash_hw;
		nvm->ops.read = igc_read_nvm_srrd_i225;
		nvm->ops.write = igc_write_nvm_srwr_i225;
		nvm->ops.validate = igc_validate_nvm_checksum_i225;
		nvm->ops.update = igc_update_nvm_checksum_i225;
	} else {
		hw->nvm.type = igc_nvm_invm;
		nvm->ops.read = igc_read_nvm_eerd;
		nvm->ops.write = NULL;
		nvm->ops.validate = NULL;
		nvm->ops.update = NULL;
	}
	return 0;
}
