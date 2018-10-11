// SPDX-License-Identifier: GPL-2.0
/* Copyright (c)  2018 Intel Corporation */

#include "igc_phy.h"

/**
 * igc_check_reset_block - Check if PHY reset is blocked
 * @hw: pointer to the HW structure
 *
 * Read the PHY management control register and check whether a PHY reset
 * is blocked.  If a reset is not blocked return 0, otherwise
 * return IGC_ERR_BLK_PHY_RESET (12).
 */
s32 igc_check_reset_block(struct igc_hw *hw)
{
	u32 manc;

	manc = rd32(IGC_MANC);

	return (manc & IGC_MANC_BLK_PHY_RST_ON_IDE) ?
		IGC_ERR_BLK_PHY_RESET : 0;
}

/**
 * igc_get_phy_id - Retrieve the PHY ID and revision
 * @hw: pointer to the HW structure
 *
 * Reads the PHY registers and stores the PHY ID and possibly the PHY
 * revision in the hardware structure.
 */
s32 igc_get_phy_id(struct igc_hw *hw)
{
	struct igc_phy_info *phy = &hw->phy;
	s32 ret_val = 0;
	u16 phy_id;

	ret_val = phy->ops.read_reg(hw, PHY_ID1, &phy_id);
	if (ret_val)
		goto out;

	phy->id = (u32)(phy_id << 16);
	usleep_range(200, 500);
	ret_val = phy->ops.read_reg(hw, PHY_ID2, &phy_id);
	if (ret_val)
		goto out;

	phy->id |= (u32)(phy_id & PHY_REVISION_MASK);
	phy->revision = (u32)(phy_id & ~PHY_REVISION_MASK);

out:
	return ret_val;
}

/**
 * igc_phy_has_link - Polls PHY for link
 * @hw: pointer to the HW structure
 * @iterations: number of times to poll for link
 * @usec_interval: delay between polling attempts
 * @success: pointer to whether polling was successful or not
 *
 * Polls the PHY status register for link, 'iterations' number of times.
 */
s32 igc_phy_has_link(struct igc_hw *hw, u32 iterations,
		     u32 usec_interval, bool *success)
{
	u16 i, phy_status;
	s32 ret_val = 0;

	for (i = 0; i < iterations; i++) {
		/* Some PHYs require the PHY_STATUS register to be read
		 * twice due to the link bit being sticky.  No harm doing
		 * it across the board.
		 */
		ret_val = hw->phy.ops.read_reg(hw, PHY_STATUS, &phy_status);
		if (ret_val && usec_interval > 0) {
			/* If the first read fails, another entity may have
			 * ownership of the resources, wait and try again to
			 * see if they have relinquished the resources yet.
			 */
			if (usec_interval >= 1000)
				mdelay(usec_interval / 1000);
			else
				udelay(usec_interval);
		}
		ret_val = hw->phy.ops.read_reg(hw, PHY_STATUS, &phy_status);
		if (ret_val)
			break;
		if (phy_status & MII_SR_LINK_STATUS)
			break;
		if (usec_interval >= 1000)
			mdelay(usec_interval / 1000);
		else
			udelay(usec_interval);
	}

	*success = (i < iterations) ? true : false;

	return ret_val;
}

/**
 * igc_power_up_phy_copper - Restore copper link in case of PHY power down
 * @hw: pointer to the HW structure
 *
 * In the case of a PHY power down to save power, or to turn off link during a
 * driver unload, restore the link to previous settings.
 */
void igc_power_up_phy_copper(struct igc_hw *hw)
{
	u16 mii_reg = 0;

	/* The PHY will retain its settings across a power down/up cycle */
	hw->phy.ops.read_reg(hw, PHY_CONTROL, &mii_reg);
	mii_reg &= ~MII_CR_POWER_DOWN;
	hw->phy.ops.write_reg(hw, PHY_CONTROL, mii_reg);
}

/**
 * igc_power_down_phy_copper - Power down copper PHY
 * @hw: pointer to the HW structure
 *
 * Power down PHY to save power when interface is down and wake on lan
 * is not enabled.
 */
void igc_power_down_phy_copper(struct igc_hw *hw)
{
	u16 mii_reg = 0;

	/* The PHY will retain its settings across a power down/up cycle */
	hw->phy.ops.read_reg(hw, PHY_CONTROL, &mii_reg);
	mii_reg |= MII_CR_POWER_DOWN;

	/* Temporary workaround - should be removed when PHY will implement
	 * IEEE registers as properly
	 */
	/* hw->phy.ops.write_reg(hw, PHY_CONTROL, mii_reg);*/
	usleep_range(1000, 2000);
}

/**
 * igc_check_downshift - Checks whether a downshift in speed occurred
 * @hw: pointer to the HW structure
 *
 * Success returns 0, Failure returns 1
 *
 * A downshift is detected by querying the PHY link health.
 */
s32 igc_check_downshift(struct igc_hw *hw)
{
	struct igc_phy_info *phy = &hw->phy;
	u16 phy_data, offset, mask;
	s32 ret_val;

	switch (phy->type) {
	case igc_phy_i225:
	default:
		/* speed downshift not supported */
		phy->speed_downgraded = false;
		ret_val = 0;
		goto out;
	}

	ret_val = phy->ops.read_reg(hw, offset, &phy_data);

	if (!ret_val)
		phy->speed_downgraded = (phy_data & mask) ? true : false;

out:
	return ret_val;
}

/**
 * igc_phy_hw_reset - PHY hardware reset
 * @hw: pointer to the HW structure
 *
 * Verify the reset block is not blocking us from resetting.  Acquire
 * semaphore (if necessary) and read/set/write the device control reset
 * bit in the PHY.  Wait the appropriate delay time for the device to
 * reset and release the semaphore (if necessary).
 */
s32 igc_phy_hw_reset(struct igc_hw *hw)
{
	struct igc_phy_info *phy = &hw->phy;
	s32  ret_val;
	u32 ctrl;

	ret_val = igc_check_reset_block(hw);
	if (ret_val) {
		ret_val = 0;
		goto out;
	}

	ret_val = phy->ops.acquire(hw);
	if (ret_val)
		goto out;

	ctrl = rd32(IGC_CTRL);
	wr32(IGC_CTRL, ctrl | IGC_CTRL_PHY_RST);
	wrfl();

	udelay(phy->reset_delay_us);

	wr32(IGC_CTRL, ctrl);
	wrfl();

	usleep_range(1500, 2000);

	phy->ops.release(hw);

out:
	return ret_val;
}

/**
 * igc_read_phy_reg_mdic - Read MDI control register
 * @hw: pointer to the HW structure
 * @offset: register offset to be read
 * @data: pointer to the read data
 *
 * Reads the MDI control register in the PHY at offset and stores the
 * information read to data.
 */
static s32 igc_read_phy_reg_mdic(struct igc_hw *hw, u32 offset, u16 *data)
{
	struct igc_phy_info *phy = &hw->phy;
	u32 i, mdic = 0;
	s32 ret_val = 0;

	if (offset > MAX_PHY_REG_ADDRESS) {
		hw_dbg("PHY Address %d is out of range\n", offset);
		ret_val = -IGC_ERR_PARAM;
		goto out;
	}

	/* Set up Op-code, Phy Address, and register offset in the MDI
	 * Control register.  The MAC will take care of interfacing with the
	 * PHY to retrieve the desired data.
	 */
	mdic = ((offset << IGC_MDIC_REG_SHIFT) |
		(phy->addr << IGC_MDIC_PHY_SHIFT) |
		(IGC_MDIC_OP_READ));

	wr32(IGC_MDIC, mdic);

	/* Poll the ready bit to see if the MDI read completed
	 * Increasing the time out as testing showed failures with
	 * the lower time out
	 */
	for (i = 0; i < IGC_GEN_POLL_TIMEOUT; i++) {
		usleep_range(500, 1000);
		mdic = rd32(IGC_MDIC);
		if (mdic & IGC_MDIC_READY)
			break;
	}
	if (!(mdic & IGC_MDIC_READY)) {
		hw_dbg("MDI Read did not complete\n");
		ret_val = -IGC_ERR_PHY;
		goto out;
	}
	if (mdic & IGC_MDIC_ERROR) {
		hw_dbg("MDI Error\n");
		ret_val = -IGC_ERR_PHY;
		goto out;
	}
	*data = (u16)mdic;

out:
	return ret_val;
}

/**
 * igc_write_phy_reg_mdic - Write MDI control register
 * @hw: pointer to the HW structure
 * @offset: register offset to write to
 * @data: data to write to register at offset
 *
 * Writes data to MDI control register in the PHY at offset.
 */
static s32 igc_write_phy_reg_mdic(struct igc_hw *hw, u32 offset, u16 data)
{
	struct igc_phy_info *phy = &hw->phy;
	u32 i, mdic = 0;
	s32 ret_val = 0;

	if (offset > MAX_PHY_REG_ADDRESS) {
		hw_dbg("PHY Address %d is out of range\n", offset);
		ret_val = -IGC_ERR_PARAM;
		goto out;
	}

	/* Set up Op-code, Phy Address, and register offset in the MDI
	 * Control register.  The MAC will take care of interfacing with the
	 * PHY to write the desired data.
	 */
	mdic = (((u32)data) |
		(offset << IGC_MDIC_REG_SHIFT) |
		(phy->addr << IGC_MDIC_PHY_SHIFT) |
		(IGC_MDIC_OP_WRITE));

	wr32(IGC_MDIC, mdic);

	/* Poll the ready bit to see if the MDI read completed
	 * Increasing the time out as testing showed failures with
	 * the lower time out
	 */
	for (i = 0; i < IGC_GEN_POLL_TIMEOUT; i++) {
		usleep_range(500, 1000);
		mdic = rd32(IGC_MDIC);
		if (mdic & IGC_MDIC_READY)
			break;
	}
	if (!(mdic & IGC_MDIC_READY)) {
		hw_dbg("MDI Write did not complete\n");
		ret_val = -IGC_ERR_PHY;
		goto out;
	}
	if (mdic & IGC_MDIC_ERROR) {
		hw_dbg("MDI Error\n");
		ret_val = -IGC_ERR_PHY;
		goto out;
	}

out:
	return ret_val;
}

/**
 * __igc_access_xmdio_reg - Read/write XMDIO register
 * @hw: pointer to the HW structure
 * @address: XMDIO address to program
 * @dev_addr: device address to program
 * @data: pointer to value to read/write from/to the XMDIO address
 * @read: boolean flag to indicate read or write
 */
static s32 __igc_access_xmdio_reg(struct igc_hw *hw, u16 address,
				  u8 dev_addr, u16 *data, bool read)
{
	s32 ret_val;

	ret_val = hw->phy.ops.write_reg(hw, IGC_MMDAC, dev_addr);
	if (ret_val)
		return ret_val;

	ret_val = hw->phy.ops.write_reg(hw, IGC_MMDAAD, address);
	if (ret_val)
		return ret_val;

	ret_val = hw->phy.ops.write_reg(hw, IGC_MMDAC, IGC_MMDAC_FUNC_DATA |
					dev_addr);
	if (ret_val)
		return ret_val;

	if (read)
		ret_val = hw->phy.ops.read_reg(hw, IGC_MMDAAD, data);
	else
		ret_val = hw->phy.ops.write_reg(hw, IGC_MMDAAD, *data);
	if (ret_val)
		return ret_val;

	/* Recalibrate the device back to 0 */
	ret_val = hw->phy.ops.write_reg(hw, IGC_MMDAC, 0);
	if (ret_val)
		return ret_val;

	return ret_val;
}

/**
 * igc_read_xmdio_reg - Read XMDIO register
 * @hw: pointer to the HW structure
 * @addr: XMDIO address to program
 * @dev_addr: device address to program
 * @data: value to be read from the EMI address
 */
static s32 igc_read_xmdio_reg(struct igc_hw *hw, u16 addr,
			      u8 dev_addr, u16 *data)
{
	return __igc_access_xmdio_reg(hw, addr, dev_addr, data, true);
}

/**
 * igc_write_xmdio_reg - Write XMDIO register
 * @hw: pointer to the HW structure
 * @addr: XMDIO address to program
 * @dev_addr: device address to program
 * @data: value to be written to the XMDIO address
 */
static s32 igc_write_xmdio_reg(struct igc_hw *hw, u16 addr,
			       u8 dev_addr, u16 data)
{
	return __igc_access_xmdio_reg(hw, addr, dev_addr, &data, false);
}

/**
 * igc_write_phy_reg_gpy - Write GPY PHY register
 * @hw: pointer to the HW structure
 * @offset: register offset to write to
 * @data: data to write at register offset
 *
 * Acquires semaphore, if necessary, then writes the data to PHY register
 * at the offset. Release any acquired semaphores before exiting.
 */
s32 igc_write_phy_reg_gpy(struct igc_hw *hw, u32 offset, u16 data)
{
	u8 dev_addr = (offset & GPY_MMD_MASK) >> GPY_MMD_SHIFT;
	s32 ret_val;

	offset = offset & GPY_REG_MASK;

	if (!dev_addr) {
		ret_val = hw->phy.ops.acquire(hw);
		if (ret_val)
			return ret_val;
		ret_val = igc_write_phy_reg_mdic(hw, offset, data);
		if (ret_val)
			return ret_val;
		hw->phy.ops.release(hw);
	} else {
		ret_val = igc_write_xmdio_reg(hw, (u16)offset, dev_addr,
					      data);
	}

	return ret_val;
}

/**
 * igc_read_phy_reg_gpy - Read GPY PHY register
 * @hw: pointer to the HW structure
 * @offset: lower half is register offset to read to
 * upper half is MMD to use.
 * @data: data to read at register offset
 *
 * Acquires semaphore, if necessary, then reads the data in the PHY register
 * at the offset. Release any acquired semaphores before exiting.
 */
s32 igc_read_phy_reg_gpy(struct igc_hw *hw, u32 offset, u16 *data)
{
	u8 dev_addr = (offset & GPY_MMD_MASK) >> GPY_MMD_SHIFT;
	s32 ret_val;

	offset = offset & GPY_REG_MASK;

	if (!dev_addr) {
		ret_val = hw->phy.ops.acquire(hw);
		if (ret_val)
			return ret_val;
		ret_val = igc_read_phy_reg_mdic(hw, offset, data);
		if (ret_val)
			return ret_val;
		hw->phy.ops.release(hw);
	} else {
		ret_val = igc_read_xmdio_reg(hw, (u16)offset, dev_addr,
					     data);
	}

	return ret_val;
}
