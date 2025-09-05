// SPDX-License-Identifier: GPL-2.0
/* Copyright(c) 1999 - 2024 Intel Corporation. */

#include <linux/pci.h>
#include <linux/delay.h>
#include <linux/sched.h>

#include "ixgbe.h"
#include "ixgbe_mbx.h"
#include "ixgbe_phy.h"
#include "ixgbe_x540.h"

#define IXGBE_X540_MAX_TX_QUEUES	128
#define IXGBE_X540_MAX_RX_QUEUES	128
#define IXGBE_X540_RAR_ENTRIES		128
#define IXGBE_X540_MC_TBL_SIZE		128
#define IXGBE_X540_VFT_TBL_SIZE		128
#define IXGBE_X540_RX_PB_SIZE		384

static int ixgbe_update_flash_X540(struct ixgbe_hw *hw);
static int ixgbe_poll_flash_update_done_X540(struct ixgbe_hw *hw);
static int ixgbe_get_swfw_sync_semaphore(struct ixgbe_hw *hw);
static void ixgbe_release_swfw_sync_semaphore(struct ixgbe_hw *hw);

enum ixgbe_media_type ixgbe_get_media_type_X540(struct ixgbe_hw *hw)
{
	return ixgbe_media_type_copper;
}

int ixgbe_get_invariants_X540(struct ixgbe_hw *hw)
{
	struct ixgbe_mac_info *mac = &hw->mac;
	struct ixgbe_phy_info *phy = &hw->phy;

	/* set_phy_power was set by default to NULL */
	phy->ops.set_phy_power = ixgbe_set_copper_phy_power;

	mac->mcft_size = IXGBE_X540_MC_TBL_SIZE;
	mac->vft_size = IXGBE_X540_VFT_TBL_SIZE;
	mac->num_rar_entries = IXGBE_X540_RAR_ENTRIES;
	mac->rx_pb_size = IXGBE_X540_RX_PB_SIZE;
	mac->max_rx_queues = IXGBE_X540_MAX_RX_QUEUES;
	mac->max_tx_queues = IXGBE_X540_MAX_TX_QUEUES;
	mac->max_msix_vectors = ixgbe_get_pcie_msix_count_generic(hw);

	return 0;
}

/**
 *  ixgbe_setup_mac_link_X540 - Set the auto advertised capabilities
 *  @hw: pointer to hardware structure
 *  @speed: new link speed
 *  @autoneg_wait_to_complete: true when waiting for completion is needed
 **/
int ixgbe_setup_mac_link_X540(struct ixgbe_hw *hw, ixgbe_link_speed speed,
			      bool autoneg_wait_to_complete)
{
	return hw->phy.ops.setup_link_speed(hw, speed,
					    autoneg_wait_to_complete);
}

/**
 *  ixgbe_reset_hw_X540 - Perform hardware reset
 *  @hw: pointer to hardware structure
 *
 *  Resets the hardware by resetting the transmit and receive units, masks
 *  and clears all interrupts, perform a PHY reset, and perform a link (MAC)
 *  reset.
 *
 *  Return: 0 on success or negative value on failure
 */
int ixgbe_reset_hw_X540(struct ixgbe_hw *hw)
{
	u32 swfw_mask = hw->phy.phy_semaphore_mask;
	u32 ctrl, i;
	int status;

	/* Call adapter stop to disable tx/rx and clear interrupts */
	status = hw->mac.ops.stop_adapter(hw);
	if (status)
		return status;

	/* flush pending Tx transactions */
	ixgbe_clear_tx_pending(hw);

mac_reset_top:
	status = hw->mac.ops.acquire_swfw_sync(hw, swfw_mask);
	if (status) {
		hw_dbg(hw, "semaphore failed with %d", status);
		return -EBUSY;
	}

	ctrl = IXGBE_CTRL_RST;
	ctrl |= IXGBE_READ_REG(hw, IXGBE_CTRL);
	IXGBE_WRITE_REG(hw, IXGBE_CTRL, ctrl);
	IXGBE_WRITE_FLUSH(hw);
	hw->mac.ops.release_swfw_sync(hw, swfw_mask);
	usleep_range(1000, 1200);

	/* Poll for reset bit to self-clear indicating reset is complete */
	for (i = 0; i < 10; i++) {
		ctrl = IXGBE_READ_REG(hw, IXGBE_CTRL);
		if (!(ctrl & IXGBE_CTRL_RST_MASK))
			break;
		udelay(1);
	}

	if (ctrl & IXGBE_CTRL_RST_MASK) {
		status = -EIO;
		hw_dbg(hw, "Reset polling failed to complete.\n");
	}
	msleep(100);

	/*
	 * Double resets are required for recovery from certain error
	 * conditions.  Between resets, it is necessary to stall to allow time
	 * for any pending HW events to complete.
	 */
	if (hw->mac.flags & IXGBE_FLAGS_DOUBLE_RESET_REQUIRED) {
		hw->mac.flags &= ~IXGBE_FLAGS_DOUBLE_RESET_REQUIRED;
		goto mac_reset_top;
	}

	/* Set the Rx packet buffer size. */
	IXGBE_WRITE_REG(hw, IXGBE_RXPBSIZE(0), 384 << IXGBE_RXPBSIZE_SHIFT);

	/* Store the permanent mac address */
	hw->mac.ops.get_mac_addr(hw, hw->mac.perm_addr);

	/*
	 * Store MAC address from RAR0, clear receive address registers, and
	 * clear the multicast table.  Also reset num_rar_entries to 128,
	 * since we modify this value when programming the SAN MAC address.
	 */
	hw->mac.num_rar_entries = IXGBE_X540_MAX_TX_QUEUES;
	hw->mac.ops.init_rx_addrs(hw);

	/* The following is not supported by E610. */
	if (hw->mac.type == ixgbe_mac_e610)
		return status;

	/* Store the permanent SAN mac address */
	hw->mac.ops.get_san_mac_addr(hw, hw->mac.san_addr);

	/* Add the SAN MAC address to RAR if it's a valid address */
	if (is_valid_ether_addr(hw->mac.san_addr)) {
		/* Save the SAN MAC RAR index */
		hw->mac.san_mac_rar_index = hw->mac.num_rar_entries - 1;

		hw->mac.ops.set_rar(hw, hw->mac.san_mac_rar_index,
				    hw->mac.san_addr, 0, IXGBE_RAH_AV);

		/* clear VMDq pool/queue selection for this RAR */
		hw->mac.ops.clear_vmdq(hw, hw->mac.san_mac_rar_index,
				       IXGBE_CLEAR_VMDQ_ALL);

		/* Reserve the last RAR for the SAN MAC address */
		hw->mac.num_rar_entries--;
	}

	/* Store the alternative WWNN/WWPN prefix */
	hw->mac.ops.get_wwn_prefix(hw, &hw->mac.wwnn_prefix,
				   &hw->mac.wwpn_prefix);

	return status;
}

/**
 *  ixgbe_start_hw_X540 - Prepare hardware for Tx/Rx
 *  @hw: pointer to hardware structure
 *
 *  Starts the hardware using the generic start_hw function
 *  and the generation start_hw function.
 *  Then performs revision-specific operations, if any.
 **/
int ixgbe_start_hw_X540(struct ixgbe_hw *hw)
{
	int ret_val;

	ret_val = ixgbe_start_hw_generic(hw);
	if (ret_val)
		return ret_val;

	return ixgbe_start_hw_gen2(hw);
}

/**
 *  ixgbe_init_eeprom_params_X540 - Initialize EEPROM params
 *  @hw: pointer to hardware structure
 *
 *  Initializes the EEPROM parameters ixgbe_eeprom_info within the
 *  ixgbe_hw struct in order to set up EEPROM access.
 **/
int ixgbe_init_eeprom_params_X540(struct ixgbe_hw *hw)
{
	struct ixgbe_eeprom_info *eeprom = &hw->eeprom;

	if (eeprom->type == ixgbe_eeprom_uninitialized) {
		u16 eeprom_size;
		u32 eec;

		eeprom->semaphore_delay = 10;
		eeprom->type = ixgbe_flash;

		eec = IXGBE_READ_REG(hw, IXGBE_EEC(hw));
		eeprom_size = FIELD_GET(IXGBE_EEC_SIZE, eec);
		eeprom->word_size = BIT(eeprom_size +
					IXGBE_EEPROM_WORD_SIZE_SHIFT);

		hw_dbg(hw, "Eeprom params: type = %d, size = %d\n",
		       eeprom->type, eeprom->word_size);
	}

	return 0;
}

/**
 *  ixgbe_read_eerd_X540- Read EEPROM word using EERD
 *  @hw: pointer to hardware structure
 *  @offset: offset of  word in the EEPROM to read
 *  @data: word read from the EEPROM
 *
 *  Reads a 16 bit word from the EEPROM using the EERD register.
 **/
static int ixgbe_read_eerd_X540(struct ixgbe_hw *hw, u16 offset, u16 *data)
{
	int status;

	if (hw->mac.ops.acquire_swfw_sync(hw, IXGBE_GSSR_EEP_SM))
		return -EBUSY;

	status = ixgbe_read_eerd_generic(hw, offset, data);

	hw->mac.ops.release_swfw_sync(hw, IXGBE_GSSR_EEP_SM);
	return status;
}

/**
 *  ixgbe_read_eerd_buffer_X540 - Read EEPROM word(s) using EERD
 *  @hw: pointer to hardware structure
 *  @offset: offset of  word in the EEPROM to read
 *  @words: number of words
 *  @data: word(s) read from the EEPROM
 *
 *  Reads a 16 bit word(s) from the EEPROM using the EERD register.
 **/
static int ixgbe_read_eerd_buffer_X540(struct ixgbe_hw *hw,
				       u16 offset, u16 words, u16 *data)
{
	int status;

	if (hw->mac.ops.acquire_swfw_sync(hw, IXGBE_GSSR_EEP_SM))
		return -EBUSY;

	status = ixgbe_read_eerd_buffer_generic(hw, offset, words, data);

	hw->mac.ops.release_swfw_sync(hw, IXGBE_GSSR_EEP_SM);
	return status;
}

/**
 *  ixgbe_write_eewr_X540 - Write EEPROM word using EEWR
 *  @hw: pointer to hardware structure
 *  @offset: offset of  word in the EEPROM to write
 *  @data: word write to the EEPROM
 *
 *  Write a 16 bit word to the EEPROM using the EEWR register.
 **/
static int ixgbe_write_eewr_X540(struct ixgbe_hw *hw, u16 offset, u16 data)
{
	int status;

	if (hw->mac.ops.acquire_swfw_sync(hw, IXGBE_GSSR_EEP_SM))
		return -EBUSY;

	status = ixgbe_write_eewr_generic(hw, offset, data);

	hw->mac.ops.release_swfw_sync(hw, IXGBE_GSSR_EEP_SM);
	return status;
}

/**
 *  ixgbe_write_eewr_buffer_X540 - Write EEPROM word(s) using EEWR
 *  @hw: pointer to hardware structure
 *  @offset: offset of  word in the EEPROM to write
 *  @words: number of words
 *  @data: word(s) write to the EEPROM
 *
 *  Write a 16 bit word(s) to the EEPROM using the EEWR register.
 **/
static int ixgbe_write_eewr_buffer_X540(struct ixgbe_hw *hw,
					u16 offset, u16 words, u16 *data)
{
	int status;

	if (hw->mac.ops.acquire_swfw_sync(hw, IXGBE_GSSR_EEP_SM))
		return -EBUSY;

	status = ixgbe_write_eewr_buffer_generic(hw, offset, words, data);

	hw->mac.ops.release_swfw_sync(hw, IXGBE_GSSR_EEP_SM);
	return status;
}

/**
 *  ixgbe_calc_eeprom_checksum_X540 - Calculates and returns the checksum
 *
 *  This function does not use synchronization for EERD and EEWR. It can
 *  be used internally by function which utilize ixgbe_acquire_swfw_sync_X540.
 *
 *  @hw: pointer to hardware structure
 **/
static int ixgbe_calc_eeprom_checksum_X540(struct ixgbe_hw *hw)
{
	u16 i;
	u16 j;
	u16 checksum = 0;
	u16 length = 0;
	u16 pointer = 0;
	u16 word = 0;
	u16 checksum_last_word = IXGBE_EEPROM_CHECKSUM;
	u16 ptr_start = IXGBE_PCIE_ANALOG_PTR;

	/*
	 * Do not use hw->eeprom.ops.read because we do not want to take
	 * the synchronization semaphores here. Instead use
	 * ixgbe_read_eerd_generic
	 */

	/* Include 0x0-0x3F in the checksum */
	for (i = 0; i < checksum_last_word; i++) {
		if (ixgbe_read_eerd_generic(hw, i, &word)) {
			hw_dbg(hw, "EEPROM read failed\n");
			return -EIO;
		}
		checksum += word;
	}

	/*
	 * Include all data from pointers 0x3, 0x6-0xE.  This excludes the
	 * FW, PHY module, and PCIe Expansion/Option ROM pointers.
	 */
	for (i = ptr_start; i < IXGBE_FW_PTR; i++) {
		if (i == IXGBE_PHY_PTR || i == IXGBE_OPTION_ROM_PTR)
			continue;

		if (ixgbe_read_eerd_generic(hw, i, &pointer)) {
			hw_dbg(hw, "EEPROM read failed\n");
			break;
		}

		/* Skip pointer section if the pointer is invalid. */
		if (pointer == 0xFFFF || pointer == 0 ||
		    pointer >= hw->eeprom.word_size)
			continue;

		if (ixgbe_read_eerd_generic(hw, pointer, &length)) {
			hw_dbg(hw, "EEPROM read failed\n");
			return -EIO;
		}

		/* Skip pointer section if length is invalid. */
		if (length == 0xFFFF || length == 0 ||
		    (pointer + length) >= hw->eeprom.word_size)
			continue;

		for (j = pointer + 1; j <= pointer + length; j++) {
			if (ixgbe_read_eerd_generic(hw, j, &word)) {
				hw_dbg(hw, "EEPROM read failed\n");
				return -EIO;
			}
			checksum += word;
		}
	}

	checksum = IXGBE_EEPROM_SUM - checksum;

	return checksum;
}

/**
 *  ixgbe_validate_eeprom_checksum_X540 - Validate EEPROM checksum
 *  @hw: pointer to hardware structure
 *  @checksum_val: calculated checksum
 *
 *  Performs checksum calculation and validates the EEPROM checksum.  If the
 *  caller does not need checksum_val, the value can be NULL.
 **/
static int ixgbe_validate_eeprom_checksum_X540(struct ixgbe_hw *hw,
					       u16 *checksum_val)
{
	u16 read_checksum = 0;
	u16 checksum;
	int status;

	/* Read the first word from the EEPROM. If this times out or fails, do
	 * not continue or we could be in for a very long wait while every
	 * EEPROM read fails
	 */
	status = hw->eeprom.ops.read(hw, 0, &checksum);
	if (status) {
		hw_dbg(hw, "EEPROM read failed\n");
		return status;
	}

	if (hw->mac.ops.acquire_swfw_sync(hw, IXGBE_GSSR_EEP_SM))
		return -EBUSY;

	status = hw->eeprom.ops.calc_checksum(hw);
	if (status < 0)
		goto out;

	checksum = (u16)(status & 0xffff);

	/* Do not use hw->eeprom.ops.read because we do not want to take
	 * the synchronization semaphores twice here.
	 */
	status = ixgbe_read_eerd_generic(hw, IXGBE_EEPROM_CHECKSUM,
					 &read_checksum);
	if (status)
		goto out;

	/* Verify read checksum from EEPROM is the same as
	 * calculated checksum
	 */
	if (read_checksum != checksum) {
		hw_dbg(hw, "Invalid EEPROM checksum");
		status = -EIO;
	}

	/* If the user cares, return the calculated checksum */
	if (checksum_val)
		*checksum_val = checksum;

out:
	hw->mac.ops.release_swfw_sync(hw, IXGBE_GSSR_EEP_SM);

	return status;
}

/**
 * ixgbe_update_eeprom_checksum_X540 - Updates the EEPROM checksum and flash
 * @hw: pointer to hardware structure
 *
 * After writing EEPROM to shadow RAM using EEWR register, software calculates
 * checksum and updates the EEPROM and instructs the hardware to update
 * the flash.
 **/
static int ixgbe_update_eeprom_checksum_X540(struct ixgbe_hw *hw)
{
	u16 checksum;
	int status;

	/* Read the first word from the EEPROM. If this times out or fails, do
	 * not continue or we could be in for a very long wait while every
	 * EEPROM read fails
	 */
	status = hw->eeprom.ops.read(hw, 0, &checksum);
	if (status) {
		hw_dbg(hw, "EEPROM read failed\n");
		return status;
	}

	if (hw->mac.ops.acquire_swfw_sync(hw, IXGBE_GSSR_EEP_SM))
		return  -EBUSY;

	status = hw->eeprom.ops.calc_checksum(hw);
	if (status < 0)
		goto out;

	checksum = (u16)(status & 0xffff);

	/* Do not use hw->eeprom.ops.write because we do not want to
	 * take the synchronization semaphores twice here.
	 */
	status = ixgbe_write_eewr_generic(hw, IXGBE_EEPROM_CHECKSUM, checksum);
	if (status)
		goto out;

	status = ixgbe_update_flash_X540(hw);

out:
	hw->mac.ops.release_swfw_sync(hw, IXGBE_GSSR_EEP_SM);
	return status;
}

/**
 * ixgbe_update_flash_X540 - Instruct HW to copy EEPROM to Flash device
 * @hw: pointer to hardware structure
 *
 * Set FLUP (bit 23) of the EEC register to instruct Hardware to copy
 * EEPROM from shadow RAM to the flash device.
 **/
static int ixgbe_update_flash_X540(struct ixgbe_hw *hw)
{
	int status;
	u32 flup;

	status = ixgbe_poll_flash_update_done_X540(hw);
	if (status == -EIO) {
		hw_dbg(hw, "Flash update time out\n");
		return status;
	}

	flup = IXGBE_READ_REG(hw, IXGBE_EEC(hw)) | IXGBE_EEC_FLUP;
	IXGBE_WRITE_REG(hw, IXGBE_EEC(hw), flup);

	status = ixgbe_poll_flash_update_done_X540(hw);
	if (status == 0)
		hw_dbg(hw, "Flash update complete\n");
	else
		hw_dbg(hw, "Flash update time out\n");

	if (hw->revision_id == 0) {
		flup = IXGBE_READ_REG(hw, IXGBE_EEC(hw));

		if (flup & IXGBE_EEC_SEC1VAL) {
			flup |= IXGBE_EEC_FLUP;
			IXGBE_WRITE_REG(hw, IXGBE_EEC(hw), flup);
		}

		status = ixgbe_poll_flash_update_done_X540(hw);
		if (status == 0)
			hw_dbg(hw, "Flash update complete\n");
		else
			hw_dbg(hw, "Flash update time out\n");
	}

	return status;
}

/**
 * ixgbe_poll_flash_update_done_X540 - Poll flash update status
 * @hw: pointer to hardware structure
 *
 * Polls the FLUDONE (bit 26) of the EEC Register to determine when the
 * flash update is done.
 **/
static int ixgbe_poll_flash_update_done_X540(struct ixgbe_hw *hw)
{
	u32 i;
	u32 reg;

	for (i = 0; i < IXGBE_FLUDONE_ATTEMPTS; i++) {
		reg = IXGBE_READ_REG(hw, IXGBE_EEC(hw));
		if (reg & IXGBE_EEC_FLUDONE)
			return 0;
		udelay(5);
	}
	return -EIO;
}

/**
 * ixgbe_acquire_swfw_sync_X540 - Acquire SWFW semaphore
 * @hw: pointer to hardware structure
 * @mask: Mask to specify which semaphore to acquire
 *
 * Acquires the SWFW semaphore thought the SW_FW_SYNC register for
 * the specified function (CSR, PHY0, PHY1, NVM, Flash)
 **/
int ixgbe_acquire_swfw_sync_X540(struct ixgbe_hw *hw, u32 mask)
{
	u32 swmask = mask & IXGBE_GSSR_NVM_PHY_MASK;
	u32 swi2c_mask = mask & IXGBE_GSSR_I2C_MASK;
	u32 fwmask = swmask << 5;
	u32 timeout = 200;
	u32 hwmask = 0;
	u32 swfw_sync;
	u32 i;

	if (swmask & IXGBE_GSSR_EEP_SM)
		hwmask = IXGBE_GSSR_FLASH_SM;

	/* SW only mask does not have FW bit pair */
	if (mask & IXGBE_GSSR_SW_MNG_SM)
		swmask |= IXGBE_GSSR_SW_MNG_SM;

	swmask |= swi2c_mask;
	fwmask |= swi2c_mask << 2;
	for (i = 0; i < timeout; i++) {
		/* SW NVM semaphore bit is used for access to all
		 * SW_FW_SYNC bits (not just NVM)
		 */
		if (ixgbe_get_swfw_sync_semaphore(hw))
			return -EBUSY;

		swfw_sync = IXGBE_READ_REG(hw, IXGBE_SWFW_SYNC(hw));
		if (!(swfw_sync & (fwmask | swmask | hwmask))) {
			swfw_sync |= swmask;
			IXGBE_WRITE_REG(hw, IXGBE_SWFW_SYNC(hw), swfw_sync);
			ixgbe_release_swfw_sync_semaphore(hw);
			usleep_range(5000, 6000);
			return 0;
		}
		/* Firmware currently using resource (fwmask), hardware
		 * currently using resource (hwmask), or other software
		 * thread currently using resource (swmask)
		 */
		ixgbe_release_swfw_sync_semaphore(hw);
		usleep_range(5000, 10000);
	}

	/* If the resource is not released by the FW/HW the SW can assume that
	 * the FW/HW malfunctions. In that case the SW should set the SW bit(s)
	 * of the requested resource(s) while ignoring the corresponding FW/HW
	 * bits in the SW_FW_SYNC register.
	 */
	if (ixgbe_get_swfw_sync_semaphore(hw))
		return -EBUSY;
	swfw_sync = IXGBE_READ_REG(hw, IXGBE_SWFW_SYNC(hw));
	if (swfw_sync & (fwmask | hwmask)) {
		swfw_sync |= swmask;
		IXGBE_WRITE_REG(hw, IXGBE_SWFW_SYNC(hw), swfw_sync);
		ixgbe_release_swfw_sync_semaphore(hw);
		usleep_range(5000, 6000);
		return 0;
	}
	/* If the resource is not released by other SW the SW can assume that
	 * the other SW malfunctions. In that case the SW should clear all SW
	 * flags that it does not own and then repeat the whole process once
	 * again.
	 */
	if (swfw_sync & swmask) {
		u32 rmask = IXGBE_GSSR_EEP_SM | IXGBE_GSSR_PHY0_SM |
			    IXGBE_GSSR_PHY1_SM | IXGBE_GSSR_MAC_CSR_SM |
			    IXGBE_GSSR_SW_MNG_SM;

		if (swi2c_mask)
			rmask |= IXGBE_GSSR_I2C_MASK;
		ixgbe_release_swfw_sync_X540(hw, rmask);
		ixgbe_release_swfw_sync_semaphore(hw);
		return -EBUSY;
	}
	ixgbe_release_swfw_sync_semaphore(hw);

	return -EBUSY;
}

/**
 * ixgbe_release_swfw_sync_X540 - Release SWFW semaphore
 * @hw: pointer to hardware structure
 * @mask: Mask to specify which semaphore to release
 *
 * Releases the SWFW semaphore through the SW_FW_SYNC register
 * for the specified function (CSR, PHY0, PHY1, EVM, Flash)
 **/
void ixgbe_release_swfw_sync_X540(struct ixgbe_hw *hw, u32 mask)
{
	u32 swmask = mask & (IXGBE_GSSR_NVM_PHY_MASK | IXGBE_GSSR_SW_MNG_SM);
	u32 swfw_sync;

	if (mask & IXGBE_GSSR_I2C_MASK)
		swmask |= mask & IXGBE_GSSR_I2C_MASK;
	ixgbe_get_swfw_sync_semaphore(hw);

	swfw_sync = IXGBE_READ_REG(hw, IXGBE_SWFW_SYNC(hw));
	swfw_sync &= ~swmask;
	IXGBE_WRITE_REG(hw, IXGBE_SWFW_SYNC(hw), swfw_sync);

	ixgbe_release_swfw_sync_semaphore(hw);
	usleep_range(5000, 6000);
}

/**
 * ixgbe_get_swfw_sync_semaphore - Get hardware semaphore
 * @hw: pointer to hardware structure
 *
 * Sets the hardware semaphores so SW/FW can gain control of shared resources
 */
static int ixgbe_get_swfw_sync_semaphore(struct ixgbe_hw *hw)
{
	u32 timeout = 2000;
	u32 i;
	u32 swsm;

	/* Get SMBI software semaphore between device drivers first */
	for (i = 0; i < timeout; i++) {
		/* If the SMBI bit is 0 when we read it, then the bit will be
		 * set and we have the semaphore
		 */
		swsm = IXGBE_READ_REG(hw, IXGBE_SWSM(hw));
		if (!(swsm & IXGBE_SWSM_SMBI))
			break;
		usleep_range(50, 100);
	}

	if (i == timeout) {
		hw_dbg(hw,
		       "Software semaphore SMBI between device drivers not granted.\n");
		return -EIO;
	}

	/* Now get the semaphore between SW/FW through the REGSMP bit */
	for (i = 0; i < timeout; i++) {
		swsm = IXGBE_READ_REG(hw, IXGBE_SWFW_SYNC(hw));
		if (!(swsm & IXGBE_SWFW_REGSMP))
			return 0;

		usleep_range(50, 100);
	}

	/* Release semaphores and return error if SW NVM semaphore
	 * was not granted because we do not have access to the EEPROM
	 */
	hw_dbg(hw, "REGSMP Software NVM semaphore not granted\n");
	ixgbe_release_swfw_sync_semaphore(hw);
	return -EIO;
}

/**
 * ixgbe_release_swfw_sync_semaphore - Release hardware semaphore
 * @hw: pointer to hardware structure
 *
 * This function clears hardware semaphore bits.
 **/
static void ixgbe_release_swfw_sync_semaphore(struct ixgbe_hw *hw)
{
	 u32 swsm;

	/* Release both semaphores by writing 0 to the bits REGSMP and SMBI */

	swsm = IXGBE_READ_REG(hw, IXGBE_SWFW_SYNC(hw));
	swsm &= ~IXGBE_SWFW_REGSMP;
	IXGBE_WRITE_REG(hw, IXGBE_SWFW_SYNC(hw), swsm);

	swsm = IXGBE_READ_REG(hw, IXGBE_SWSM(hw));
	swsm &= ~IXGBE_SWSM_SMBI;
	IXGBE_WRITE_REG(hw, IXGBE_SWSM(hw), swsm);

	IXGBE_WRITE_FLUSH(hw);
}

/**
 *  ixgbe_init_swfw_sync_X540 - Release hardware semaphore
 *  @hw: pointer to hardware structure
 *
 *  This function reset hardware semaphore bits for a semaphore that may
 *  have be left locked due to a catastrophic failure.
 **/
void ixgbe_init_swfw_sync_X540(struct ixgbe_hw *hw)
{
	u32 rmask;

	/* First try to grab the semaphore but we don't need to bother
	 * looking to see whether we got the lock or not since we do
	 * the same thing regardless of whether we got the lock or not.
	 * We got the lock - we release it.
	 * We timeout trying to get the lock - we force its release.
	 */
	ixgbe_get_swfw_sync_semaphore(hw);
	ixgbe_release_swfw_sync_semaphore(hw);

	/* Acquire and release all software resources. */
	rmask = IXGBE_GSSR_EEP_SM | IXGBE_GSSR_PHY0_SM |
		IXGBE_GSSR_PHY1_SM | IXGBE_GSSR_MAC_CSR_SM |
		IXGBE_GSSR_SW_MNG_SM | IXGBE_GSSR_I2C_MASK;

	ixgbe_acquire_swfw_sync_X540(hw, rmask);
	ixgbe_release_swfw_sync_X540(hw, rmask);
}

/**
 * ixgbe_blink_led_start_X540 - Blink LED based on index.
 * @hw: pointer to hardware structure
 * @index: led number to blink
 *
 * Devices that implement the version 2 interface:
 *   X540
 **/
int ixgbe_blink_led_start_X540(struct ixgbe_hw *hw, u32 index)
{
	u32 macc_reg;
	u32 ledctl_reg;
	ixgbe_link_speed speed;
	bool link_up;

	if (index > 3)
		return -EINVAL;

	/* Link should be up in order for the blink bit in the LED control
	 * register to work. Force link and speed in the MAC if link is down.
	 * This will be reversed when we stop the blinking.
	 */
	hw->mac.ops.check_link(hw, &speed, &link_up, false);
	if (!link_up) {
		macc_reg = IXGBE_READ_REG(hw, IXGBE_MACC);
		macc_reg |= IXGBE_MACC_FLU | IXGBE_MACC_FSV_10G | IXGBE_MACC_FS;
		IXGBE_WRITE_REG(hw, IXGBE_MACC, macc_reg);
	}
	/* Set the LED to LINK_UP + BLINK. */
	ledctl_reg = IXGBE_READ_REG(hw, IXGBE_LEDCTL);
	ledctl_reg &= ~IXGBE_LED_MODE_MASK(index);
	ledctl_reg |= IXGBE_LED_BLINK(index);
	IXGBE_WRITE_REG(hw, IXGBE_LEDCTL, ledctl_reg);
	IXGBE_WRITE_FLUSH(hw);

	return 0;
}

/**
 * ixgbe_blink_led_stop_X540 - Stop blinking LED based on index.
 * @hw: pointer to hardware structure
 * @index: led number to stop blinking
 *
 * Devices that implement the version 2 interface:
 *   X540
 **/
int ixgbe_blink_led_stop_X540(struct ixgbe_hw *hw, u32 index)
{
	u32 macc_reg;
	u32 ledctl_reg;

	if (index > 3)
		return -EINVAL;

	/* Restore the LED to its default value. */
	ledctl_reg = IXGBE_READ_REG(hw, IXGBE_LEDCTL);
	ledctl_reg &= ~IXGBE_LED_MODE_MASK(index);
	ledctl_reg |= IXGBE_LED_LINK_ACTIVE << IXGBE_LED_MODE_SHIFT(index);
	ledctl_reg &= ~IXGBE_LED_BLINK(index);
	IXGBE_WRITE_REG(hw, IXGBE_LEDCTL, ledctl_reg);

	/* Unforce link and speed in the MAC. */
	macc_reg = IXGBE_READ_REG(hw, IXGBE_MACC);
	macc_reg &= ~(IXGBE_MACC_FLU | IXGBE_MACC_FSV_10G | IXGBE_MACC_FS);
	IXGBE_WRITE_REG(hw, IXGBE_MACC, macc_reg);
	IXGBE_WRITE_FLUSH(hw);

	return 0;
}
static const struct ixgbe_mac_operations mac_ops_X540 = {
	.init_hw                = &ixgbe_init_hw_generic,
	.reset_hw               = &ixgbe_reset_hw_X540,
	.start_hw               = &ixgbe_start_hw_X540,
	.clear_hw_cntrs         = &ixgbe_clear_hw_cntrs_generic,
	.get_media_type         = &ixgbe_get_media_type_X540,
	.enable_rx_dma          = &ixgbe_enable_rx_dma_generic,
	.get_mac_addr           = &ixgbe_get_mac_addr_generic,
	.get_san_mac_addr       = &ixgbe_get_san_mac_addr_generic,
	.get_device_caps        = &ixgbe_get_device_caps_generic,
	.get_wwn_prefix         = &ixgbe_get_wwn_prefix_generic,
	.stop_adapter           = &ixgbe_stop_adapter_generic,
	.get_bus_info           = &ixgbe_get_bus_info_generic,
	.set_lan_id             = &ixgbe_set_lan_id_multi_port_pcie,
	.read_analog_reg8       = NULL,
	.write_analog_reg8      = NULL,
	.setup_link             = &ixgbe_setup_mac_link_X540,
	.set_rxpba		= &ixgbe_set_rxpba_generic,
	.check_link             = &ixgbe_check_mac_link_generic,
	.get_link_capabilities  = &ixgbe_get_copper_link_capabilities_generic,
	.led_on                 = &ixgbe_led_on_generic,
	.led_off                = &ixgbe_led_off_generic,
	.init_led_link_act	= ixgbe_init_led_link_act_generic,
	.blink_led_start        = &ixgbe_blink_led_start_X540,
	.blink_led_stop         = &ixgbe_blink_led_stop_X540,
	.set_rar                = &ixgbe_set_rar_generic,
	.clear_rar              = &ixgbe_clear_rar_generic,
	.set_vmdq               = &ixgbe_set_vmdq_generic,
	.set_vmdq_san_mac	= &ixgbe_set_vmdq_san_mac_generic,
	.clear_vmdq             = &ixgbe_clear_vmdq_generic,
	.init_rx_addrs          = &ixgbe_init_rx_addrs_generic,
	.update_mc_addr_list    = &ixgbe_update_mc_addr_list_generic,
	.enable_mc              = &ixgbe_enable_mc_generic,
	.disable_mc             = &ixgbe_disable_mc_generic,
	.clear_vfta             = &ixgbe_clear_vfta_generic,
	.set_vfta               = &ixgbe_set_vfta_generic,
	.fc_enable              = &ixgbe_fc_enable_generic,
	.setup_fc		= ixgbe_setup_fc_generic,
	.fc_autoneg		= ixgbe_fc_autoneg,
	.set_fw_drv_ver         = &ixgbe_set_fw_drv_ver_generic,
	.init_uta_tables        = &ixgbe_init_uta_tables_generic,
	.setup_sfp              = NULL,
	.set_mac_anti_spoofing  = &ixgbe_set_mac_anti_spoofing,
	.set_vlan_anti_spoofing = &ixgbe_set_vlan_anti_spoofing,
	.acquire_swfw_sync      = &ixgbe_acquire_swfw_sync_X540,
	.release_swfw_sync      = &ixgbe_release_swfw_sync_X540,
	.init_swfw_sync		= &ixgbe_init_swfw_sync_X540,
	.disable_rx_buff	= &ixgbe_disable_rx_buff_generic,
	.enable_rx_buff		= &ixgbe_enable_rx_buff_generic,
	.get_thermal_sensor_data = NULL,
	.init_thermal_sensor_thresh = NULL,
	.prot_autoc_read	= &prot_autoc_read_generic,
	.prot_autoc_write	= &prot_autoc_write_generic,
	.enable_rx		= &ixgbe_enable_rx_generic,
	.disable_rx		= &ixgbe_disable_rx_generic,
};

static const struct ixgbe_eeprom_operations eeprom_ops_X540 = {
	.init_params            = &ixgbe_init_eeprom_params_X540,
	.read                   = &ixgbe_read_eerd_X540,
	.read_buffer		= &ixgbe_read_eerd_buffer_X540,
	.write                  = &ixgbe_write_eewr_X540,
	.write_buffer		= &ixgbe_write_eewr_buffer_X540,
	.calc_checksum		= &ixgbe_calc_eeprom_checksum_X540,
	.validate_checksum      = &ixgbe_validate_eeprom_checksum_X540,
	.update_checksum        = &ixgbe_update_eeprom_checksum_X540,
	.read_pba_string        = &ixgbe_read_pba_string_generic,
};

static const struct ixgbe_phy_operations phy_ops_X540 = {
	.identify               = &ixgbe_identify_phy_generic,
	.identify_sfp           = &ixgbe_identify_sfp_module_generic,
	.init			= NULL,
	.reset                  = NULL,
	.read_reg               = &ixgbe_read_phy_reg_generic,
	.write_reg              = &ixgbe_write_phy_reg_generic,
	.setup_link             = &ixgbe_setup_phy_link_generic,
	.setup_link_speed       = &ixgbe_setup_phy_link_speed_generic,
	.read_i2c_byte          = &ixgbe_read_i2c_byte_generic,
	.write_i2c_byte         = &ixgbe_write_i2c_byte_generic,
	.read_i2c_sff8472	= &ixgbe_read_i2c_sff8472_generic,
	.read_i2c_eeprom        = &ixgbe_read_i2c_eeprom_generic,
	.write_i2c_eeprom       = &ixgbe_write_i2c_eeprom_generic,
	.check_overtemp         = &ixgbe_tn_check_overtemp,
	.set_phy_power          = &ixgbe_set_copper_phy_power,
};

static const u32 ixgbe_mvals_X540[IXGBE_MVALS_IDX_LIMIT] = {
	IXGBE_MVALS_INIT(X540)
};

const struct ixgbe_info ixgbe_X540_info = {
	.mac                    = ixgbe_mac_X540,
	.get_invariants         = &ixgbe_get_invariants_X540,
	.mac_ops                = &mac_ops_X540,
	.eeprom_ops             = &eeprom_ops_X540,
	.phy_ops                = &phy_ops_X540,
	.mbx_ops                = &mbx_ops_generic,
	.mvals			= ixgbe_mvals_X540,
};
