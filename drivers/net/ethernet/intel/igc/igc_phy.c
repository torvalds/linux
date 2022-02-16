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
	s32 ret_val;

	switch (phy->type) {
	case igc_phy_i225:
	default:
		/* speed downshift not supported */
		phy->speed_downgraded = false;
		ret_val = 0;
	}

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
	u32 phpm = 0, timeout = 10000;
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

	phpm = rd32(IGC_I225_PHPM);

	ctrl = rd32(IGC_CTRL);
	wr32(IGC_CTRL, ctrl | IGC_CTRL_PHY_RST);
	wrfl();

	udelay(phy->reset_delay_us);

	wr32(IGC_CTRL, ctrl);
	wrfl();

	/* SW should guarantee 100us for the completion of the PHY reset */
	usleep_range(100, 150);
	do {
		phpm = rd32(IGC_I225_PHPM);
		timeout--;
		udelay(1);
	} while (!(phpm & IGC_PHY_RST_COMP) && timeout);

	if (!timeout)
		hw_dbg("Timeout is expired after a phy reset\n");

	usleep_range(100, 150);

	phy->ops.release(hw);

out:
	return ret_val;
}

/**
 * igc_phy_setup_autoneg - Configure PHY for auto-negotiation
 * @hw: pointer to the HW structure
 *
 * Reads the MII auto-neg advertisement register and/or the 1000T control
 * register and if the PHY is already setup for auto-negotiation, then
 * return successful.  Otherwise, setup advertisement and flow control to
 * the appropriate values for the wanted auto-negotiation.
 */
static s32 igc_phy_setup_autoneg(struct igc_hw *hw)
{
	struct igc_phy_info *phy = &hw->phy;
	u16 aneg_multigbt_an_ctrl = 0;
	u16 mii_1000t_ctrl_reg = 0;
	u16 mii_autoneg_adv_reg;
	s32 ret_val;

	phy->autoneg_advertised &= phy->autoneg_mask;

	/* Read the MII Auto-Neg Advertisement Register (Address 4). */
	ret_val = phy->ops.read_reg(hw, PHY_AUTONEG_ADV, &mii_autoneg_adv_reg);
	if (ret_val)
		return ret_val;

	if (phy->autoneg_mask & ADVERTISE_1000_FULL) {
		/* Read the MII 1000Base-T Control Register (Address 9). */
		ret_val = phy->ops.read_reg(hw, PHY_1000T_CTRL,
					    &mii_1000t_ctrl_reg);
		if (ret_val)
			return ret_val;
	}

	if (phy->autoneg_mask & ADVERTISE_2500_FULL) {
		/* Read the MULTI GBT AN Control Register - reg 7.32 */
		ret_val = phy->ops.read_reg(hw, (STANDARD_AN_REG_MASK <<
					    MMD_DEVADDR_SHIFT) |
					    ANEG_MULTIGBT_AN_CTRL,
					    &aneg_multigbt_an_ctrl);

		if (ret_val)
			return ret_val;
	}

	/* Need to parse both autoneg_advertised and fc and set up
	 * the appropriate PHY registers.  First we will parse for
	 * autoneg_advertised software override.  Since we can advertise
	 * a plethora of combinations, we need to check each bit
	 * individually.
	 */

	/* First we clear all the 10/100 mb speed bits in the Auto-Neg
	 * Advertisement Register (Address 4) and the 1000 mb speed bits in
	 * the  1000Base-T Control Register (Address 9).
	 */
	mii_autoneg_adv_reg &= ~(NWAY_AR_100TX_FD_CAPS |
				 NWAY_AR_100TX_HD_CAPS |
				 NWAY_AR_10T_FD_CAPS   |
				 NWAY_AR_10T_HD_CAPS);
	mii_1000t_ctrl_reg &= ~(CR_1000T_HD_CAPS | CR_1000T_FD_CAPS);

	hw_dbg("autoneg_advertised %x\n", phy->autoneg_advertised);

	/* Do we want to advertise 10 Mb Half Duplex? */
	if (phy->autoneg_advertised & ADVERTISE_10_HALF) {
		hw_dbg("Advertise 10mb Half duplex\n");
		mii_autoneg_adv_reg |= NWAY_AR_10T_HD_CAPS;
	}

	/* Do we want to advertise 10 Mb Full Duplex? */
	if (phy->autoneg_advertised & ADVERTISE_10_FULL) {
		hw_dbg("Advertise 10mb Full duplex\n");
		mii_autoneg_adv_reg |= NWAY_AR_10T_FD_CAPS;
	}

	/* Do we want to advertise 100 Mb Half Duplex? */
	if (phy->autoneg_advertised & ADVERTISE_100_HALF) {
		hw_dbg("Advertise 100mb Half duplex\n");
		mii_autoneg_adv_reg |= NWAY_AR_100TX_HD_CAPS;
	}

	/* Do we want to advertise 100 Mb Full Duplex? */
	if (phy->autoneg_advertised & ADVERTISE_100_FULL) {
		hw_dbg("Advertise 100mb Full duplex\n");
		mii_autoneg_adv_reg |= NWAY_AR_100TX_FD_CAPS;
	}

	/* We do not allow the Phy to advertise 1000 Mb Half Duplex */
	if (phy->autoneg_advertised & ADVERTISE_1000_HALF)
		hw_dbg("Advertise 1000mb Half duplex request denied!\n");

	/* Do we want to advertise 1000 Mb Full Duplex? */
	if (phy->autoneg_advertised & ADVERTISE_1000_FULL) {
		hw_dbg("Advertise 1000mb Full duplex\n");
		mii_1000t_ctrl_reg |= CR_1000T_FD_CAPS;
	}

	/* We do not allow the Phy to advertise 2500 Mb Half Duplex */
	if (phy->autoneg_advertised & ADVERTISE_2500_HALF)
		hw_dbg("Advertise 2500mb Half duplex request denied!\n");

	/* Do we want to advertise 2500 Mb Full Duplex? */
	if (phy->autoneg_advertised & ADVERTISE_2500_FULL) {
		hw_dbg("Advertise 2500mb Full duplex\n");
		aneg_multigbt_an_ctrl |= CR_2500T_FD_CAPS;
	} else {
		aneg_multigbt_an_ctrl &= ~CR_2500T_FD_CAPS;
	}

	/* Check for a software override of the flow control settings, and
	 * setup the PHY advertisement registers accordingly.  If
	 * auto-negotiation is enabled, then software will have to set the
	 * "PAUSE" bits to the correct value in the Auto-Negotiation
	 * Advertisement Register (PHY_AUTONEG_ADV) and re-start auto-
	 * negotiation.
	 *
	 * The possible values of the "fc" parameter are:
	 *      0:  Flow control is completely disabled
	 *      1:  Rx flow control is enabled (we can receive pause frames
	 *          but not send pause frames).
	 *      2:  Tx flow control is enabled (we can send pause frames
	 *          but we do not support receiving pause frames).
	 *      3:  Both Rx and Tx flow control (symmetric) are enabled.
	 *  other:  No software override.  The flow control configuration
	 *          in the EEPROM is used.
	 */
	switch (hw->fc.current_mode) {
	case igc_fc_none:
		/* Flow control (Rx & Tx) is completely disabled by a
		 * software over-ride.
		 */
		mii_autoneg_adv_reg &= ~(NWAY_AR_ASM_DIR | NWAY_AR_PAUSE);
		break;
	case igc_fc_rx_pause:
		/* Rx Flow control is enabled, and Tx Flow control is
		 * disabled, by a software over-ride.
		 *
		 * Since there really isn't a way to advertise that we are
		 * capable of Rx Pause ONLY, we will advertise that we
		 * support both symmetric and asymmetric Rx PAUSE.  Later
		 * (in igc_config_fc_after_link_up) we will disable the
		 * hw's ability to send PAUSE frames.
		 */
		mii_autoneg_adv_reg |= (NWAY_AR_ASM_DIR | NWAY_AR_PAUSE);
		break;
	case igc_fc_tx_pause:
		/* Tx Flow control is enabled, and Rx Flow control is
		 * disabled, by a software over-ride.
		 */
		mii_autoneg_adv_reg |= NWAY_AR_ASM_DIR;
		mii_autoneg_adv_reg &= ~NWAY_AR_PAUSE;
		break;
	case igc_fc_full:
		/* Flow control (both Rx and Tx) is enabled by a software
		 * over-ride.
		 */
		mii_autoneg_adv_reg |= (NWAY_AR_ASM_DIR | NWAY_AR_PAUSE);
		break;
	default:
		hw_dbg("Flow control param set incorrectly\n");
		return -IGC_ERR_CONFIG;
	}

	ret_val = phy->ops.write_reg(hw, PHY_AUTONEG_ADV, mii_autoneg_adv_reg);
	if (ret_val)
		return ret_val;

	hw_dbg("Auto-Neg Advertising %x\n", mii_autoneg_adv_reg);

	if (phy->autoneg_mask & ADVERTISE_1000_FULL)
		ret_val = phy->ops.write_reg(hw, PHY_1000T_CTRL,
					     mii_1000t_ctrl_reg);

	if (phy->autoneg_mask & ADVERTISE_2500_FULL)
		ret_val = phy->ops.write_reg(hw,
					     (STANDARD_AN_REG_MASK <<
					     MMD_DEVADDR_SHIFT) |
					     ANEG_MULTIGBT_AN_CTRL,
					     aneg_multigbt_an_ctrl);

	return ret_val;
}

/**
 * igc_wait_autoneg - Wait for auto-neg completion
 * @hw: pointer to the HW structure
 *
 * Waits for auto-negotiation to complete or for the auto-negotiation time
 * limit to expire, which ever happens first.
 */
static s32 igc_wait_autoneg(struct igc_hw *hw)
{
	u16 i, phy_status;
	s32 ret_val = 0;

	/* Break after autoneg completes or PHY_AUTO_NEG_LIMIT expires. */
	for (i = PHY_AUTO_NEG_LIMIT; i > 0; i--) {
		ret_val = hw->phy.ops.read_reg(hw, PHY_STATUS, &phy_status);
		if (ret_val)
			break;
		ret_val = hw->phy.ops.read_reg(hw, PHY_STATUS, &phy_status);
		if (ret_val)
			break;
		if (phy_status & MII_SR_AUTONEG_COMPLETE)
			break;
		msleep(100);
	}

	/* PHY_AUTO_NEG_TIME expiration doesn't guarantee auto-negotiation
	 * has completed.
	 */
	return ret_val;
}

/**
 * igc_copper_link_autoneg - Setup/Enable autoneg for copper link
 * @hw: pointer to the HW structure
 *
 * Performs initial bounds checking on autoneg advertisement parameter, then
 * configure to advertise the full capability.  Setup the PHY to autoneg
 * and restart the negotiation process between the link partner.  If
 * autoneg_wait_to_complete, then wait for autoneg to complete before exiting.
 */
static s32 igc_copper_link_autoneg(struct igc_hw *hw)
{
	struct igc_phy_info *phy = &hw->phy;
	u16 phy_ctrl;
	s32 ret_val;

	/* Perform some bounds checking on the autoneg advertisement
	 * parameter.
	 */
	phy->autoneg_advertised &= phy->autoneg_mask;

	/* If autoneg_advertised is zero, we assume it was not defaulted
	 * by the calling code so we set to advertise full capability.
	 */
	if (phy->autoneg_advertised == 0)
		phy->autoneg_advertised = phy->autoneg_mask;

	hw_dbg("Reconfiguring auto-neg advertisement params\n");
	ret_val = igc_phy_setup_autoneg(hw);
	if (ret_val) {
		hw_dbg("Error Setting up Auto-Negotiation\n");
		goto out;
	}
	hw_dbg("Restarting Auto-Neg\n");

	/* Restart auto-negotiation by setting the Auto Neg Enable bit and
	 * the Auto Neg Restart bit in the PHY control register.
	 */
	ret_val = phy->ops.read_reg(hw, PHY_CONTROL, &phy_ctrl);
	if (ret_val)
		goto out;

	phy_ctrl |= (MII_CR_AUTO_NEG_EN | MII_CR_RESTART_AUTO_NEG);
	ret_val = phy->ops.write_reg(hw, PHY_CONTROL, phy_ctrl);
	if (ret_val)
		goto out;

	/* Does the user want to wait for Auto-Neg to complete here, or
	 * check at a later time (for example, callback routine).
	 */
	if (phy->autoneg_wait_to_complete) {
		ret_val = igc_wait_autoneg(hw);
		if (ret_val) {
			hw_dbg("Error while waiting for autoneg to complete\n");
			goto out;
		}
	}

	hw->mac.get_link_status = true;

out:
	return ret_val;
}

/**
 * igc_setup_copper_link - Configure copper link settings
 * @hw: pointer to the HW structure
 *
 * Calls the appropriate function to configure the link for auto-neg or forced
 * speed and duplex.  Then we check for link, once link is established calls
 * to configure collision distance and flow control are called.  If link is
 * not established, we return -IGC_ERR_PHY (-2).
 */
s32 igc_setup_copper_link(struct igc_hw *hw)
{
	s32 ret_val = 0;
	bool link;

	if (hw->mac.autoneg) {
		/* Setup autoneg and flow control advertisement and perform
		 * autonegotiation.
		 */
		ret_val = igc_copper_link_autoneg(hw);
		if (ret_val)
			goto out;
	} else {
		/* PHY will be set to 10H, 10F, 100H or 100F
		 * depending on user settings.
		 */
		hw_dbg("Forcing Speed and Duplex\n");
		ret_val = hw->phy.ops.force_speed_duplex(hw);
		if (ret_val) {
			hw_dbg("Error Forcing Speed and Duplex\n");
			goto out;
		}
	}

	/* Check link status. Wait up to 100 microseconds for link to become
	 * valid.
	 */
	ret_val = igc_phy_has_link(hw, COPPER_LINK_UP_LIMIT, 10, &link);
	if (ret_val)
		goto out;

	if (link) {
		hw_dbg("Valid link established!!!\n");
		igc_config_collision_dist(hw);
		ret_val = igc_config_fc_after_link_up(hw);
	} else {
		hw_dbg("Unable to establish link!!!\n");
	}

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
		hw->phy.ops.release(hw);
	} else {
		ret_val = igc_read_xmdio_reg(hw, (u16)offset, dev_addr,
					     data);
	}

	return ret_val;
}

/**
 * igc_read_phy_fw_version - Read gPHY firmware version
 * @hw: pointer to the HW structure
 */
u16 igc_read_phy_fw_version(struct igc_hw *hw)
{
	struct igc_phy_info *phy = &hw->phy;
	u16 gphy_version = 0;
	u16 ret_val;

	/* NVM image version is reported as firmware version for i225 device */
	ret_val = phy->ops.read_reg(hw, IGC_GPHY_VERSION, &gphy_version);
	if (ret_val)
		hw_dbg("igc_phy: read wrong gphy version\n");

	return gphy_version;
}
