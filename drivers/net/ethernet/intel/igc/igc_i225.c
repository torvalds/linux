// SPDX-License-Identifier: GPL-2.0
/* Copyright (c)  2018 Intel Corporation */

#include <linux/delay.h>

#include "igc_hw.h"

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
