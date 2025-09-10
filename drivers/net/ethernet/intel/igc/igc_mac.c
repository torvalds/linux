// SPDX-License-Identifier: GPL-2.0
/* Copyright (c)  2018 Intel Corporation */

#include <linux/pci.h>
#include <linux/delay.h>

#include "igc_mac.h"
#include "igc_hw.h"

/**
 * igc_disable_pcie_master - Disables PCI-express master access
 * @hw: pointer to the HW structure
 *
 * Returns 0 (0) if successful, else returns -10
 * (-IGC_ERR_MASTER_REQUESTS_PENDING) if master disable bit has not caused
 * the master requests to be disabled.
 *
 * Disables PCI-Express master access and verifies there are no pending
 * requests.
 */
s32 igc_disable_pcie_master(struct igc_hw *hw)
{
	s32 timeout = MASTER_DISABLE_TIMEOUT;
	s32 ret_val = 0;
	u32 ctrl;

	ctrl = rd32(IGC_CTRL);
	ctrl |= IGC_CTRL_GIO_MASTER_DISABLE;
	wr32(IGC_CTRL, ctrl);

	while (timeout) {
		if (!(rd32(IGC_STATUS) &
		    IGC_STATUS_GIO_MASTER_ENABLE))
			break;
		usleep_range(2000, 3000);
		timeout--;
	}

	if (!timeout) {
		hw_dbg("Master requests are pending.\n");
		ret_val = -IGC_ERR_MASTER_REQUESTS_PENDING;
		goto out;
	}

out:
	return ret_val;
}

/**
 * igc_init_rx_addrs - Initialize receive addresses
 * @hw: pointer to the HW structure
 * @rar_count: receive address registers
 *
 * Setup the receive address registers by setting the base receive address
 * register to the devices MAC address and clearing all the other receive
 * address registers to 0.
 */
void igc_init_rx_addrs(struct igc_hw *hw, u16 rar_count)
{
	u8 mac_addr[ETH_ALEN] = {0};
	u32 i;

	/* Setup the receive address */
	hw_dbg("Programming MAC Address into RAR[0]\n");

	hw->mac.ops.rar_set(hw, hw->mac.addr, 0);

	/* Zero out the other (rar_entry_count - 1) receive addresses */
	hw_dbg("Clearing RAR[1-%u]\n", rar_count - 1);
	for (i = 1; i < rar_count; i++)
		hw->mac.ops.rar_set(hw, mac_addr, i);
}

/**
 * igc_set_fc_watermarks - Set flow control high/low watermarks
 * @hw: pointer to the HW structure
 *
 * Sets the flow control high/low threshold (watermark) registers.  If
 * flow control XON frame transmission is enabled, then set XON frame
 * transmission as well.
 */
static s32 igc_set_fc_watermarks(struct igc_hw *hw)
{
	u32 fcrtl = 0, fcrth = 0;

	/* Set the flow control receive threshold registers.  Normally,
	 * these registers will be set to a default threshold that may be
	 * adjusted later by the driver's runtime code.  However, if the
	 * ability to transmit pause frames is not enabled, then these
	 * registers will be set to 0.
	 */
	if (hw->fc.current_mode & igc_fc_tx_pause) {
		/* We need to set up the Receive Threshold high and low water
		 * marks as well as (optionally) enabling the transmission of
		 * XON frames.
		 */
		fcrtl = hw->fc.low_water;
		if (hw->fc.send_xon)
			fcrtl |= IGC_FCRTL_XONE;

		fcrth = hw->fc.high_water;
	}
	wr32(IGC_FCRTL, fcrtl);
	wr32(IGC_FCRTH, fcrth);

	return 0;
}

/**
 * igc_setup_link - Setup flow control and link settings
 * @hw: pointer to the HW structure
 *
 * Determines which flow control settings to use, then configures flow
 * control.  Calls the appropriate media-specific link configuration
 * function.  Assuming the adapter has a valid link partner, a valid link
 * should be established.  Assumes the hardware has previously been reset
 * and the transmitter and receiver are not enabled.
 */
s32 igc_setup_link(struct igc_hw *hw)
{
	s32 ret_val = 0;

	/* In the case of the phy reset being blocked, we already have a link.
	 * We do not need to set it up again.
	 */
	if (igc_check_reset_block(hw))
		goto out;

	/* If requested flow control is set to default, set flow control
	 * to both 'rx' and 'tx' pause frames.
	 */
	if (hw->fc.requested_mode == igc_fc_default)
		hw->fc.requested_mode = igc_fc_full;

	/* We want to save off the original Flow Control configuration just
	 * in case we get disconnected and then reconnected into a different
	 * hub or switch with different Flow Control capabilities.
	 */
	hw->fc.current_mode = hw->fc.requested_mode;

	hw_dbg("After fix-ups FlowControl is now = %x\n", hw->fc.current_mode);

	/* Call the necessary media_type subroutine to configure the link. */
	ret_val = hw->mac.ops.setup_physical_interface(hw);
	if (ret_val)
		goto out;

	/* Initialize the flow control address, type, and PAUSE timer
	 * registers to their default values.  This is done even if flow
	 * control is disabled, because it does not hurt anything to
	 * initialize these registers.
	 */
	hw_dbg("Initializing the Flow Control address, type and timer regs\n");
	wr32(IGC_FCT, FLOW_CONTROL_TYPE);
	wr32(IGC_FCAH, FLOW_CONTROL_ADDRESS_HIGH);
	wr32(IGC_FCAL, FLOW_CONTROL_ADDRESS_LOW);

	wr32(IGC_FCTTV, hw->fc.pause_time);

	ret_val = igc_set_fc_watermarks(hw);

out:
	return ret_val;
}

/**
 * igc_force_mac_fc - Force the MAC's flow control settings
 * @hw: pointer to the HW structure
 *
 * Force the MAC's flow control settings.  Sets the TFCE and RFCE bits in the
 * device control register to reflect the adapter settings.  TFCE and RFCE
 * need to be explicitly set by software when a copper PHY is used because
 * autonegotiation is managed by the PHY rather than the MAC.  Software must
 * also configure these bits when link is forced on a fiber connection.
 */
s32 igc_force_mac_fc(struct igc_hw *hw)
{
	s32 ret_val = 0;
	u32 ctrl;

	ctrl = rd32(IGC_CTRL);

	/* Because we didn't get link via the internal auto-negotiation
	 * mechanism (we either forced link or we got link via PHY
	 * auto-neg), we have to manually enable/disable transmit an
	 * receive flow control.
	 *
	 * The "Case" statement below enables/disable flow control
	 * according to the "hw->fc.current_mode" parameter.
	 *
	 * The possible values of the "fc" parameter are:
	 *      0:  Flow control is completely disabled
	 *      1:  Rx flow control is enabled (we can receive pause
	 *          frames but not send pause frames).
	 *      2:  Tx flow control is enabled (we can send pause frames
	 *          but we do not receive pause frames).
	 *      3:  Both Rx and TX flow control (symmetric) is enabled.
	 *  other:  No other values should be possible at this point.
	 */
	hw_dbg("hw->fc.current_mode = %u\n", hw->fc.current_mode);

	switch (hw->fc.current_mode) {
	case igc_fc_none:
		ctrl &= (~(IGC_CTRL_TFCE | IGC_CTRL_RFCE));
		break;
	case igc_fc_rx_pause:
		ctrl &= (~IGC_CTRL_TFCE);
		ctrl |= IGC_CTRL_RFCE;
		break;
	case igc_fc_tx_pause:
		ctrl &= (~IGC_CTRL_RFCE);
		ctrl |= IGC_CTRL_TFCE;
		break;
	case igc_fc_full:
		ctrl |= (IGC_CTRL_TFCE | IGC_CTRL_RFCE);
		break;
	default:
		hw_dbg("Flow control param set incorrectly\n");
		ret_val = -IGC_ERR_CONFIG;
		goto out;
	}

	wr32(IGC_CTRL, ctrl);

out:
	return ret_val;
}

/**
 * igc_clear_hw_cntrs_base - Clear base hardware counters
 * @hw: pointer to the HW structure
 *
 * Clears the base hardware counters by reading the counter registers.
 */
void igc_clear_hw_cntrs_base(struct igc_hw *hw)
{
	rd32(IGC_CRCERRS);
	rd32(IGC_MPC);
	rd32(IGC_SCC);
	rd32(IGC_ECOL);
	rd32(IGC_MCC);
	rd32(IGC_LATECOL);
	rd32(IGC_COLC);
	rd32(IGC_RERC);
	rd32(IGC_DC);
	rd32(IGC_RLEC);
	rd32(IGC_XONRXC);
	rd32(IGC_XONTXC);
	rd32(IGC_XOFFRXC);
	rd32(IGC_XOFFTXC);
	rd32(IGC_FCRUC);
	rd32(IGC_GPRC);
	rd32(IGC_BPRC);
	rd32(IGC_MPRC);
	rd32(IGC_GPTC);
	rd32(IGC_GORCL);
	rd32(IGC_GORCH);
	rd32(IGC_GOTCL);
	rd32(IGC_GOTCH);
	rd32(IGC_RNBC);
	rd32(IGC_RUC);
	rd32(IGC_RFC);
	rd32(IGC_ROC);
	rd32(IGC_RJC);
	rd32(IGC_TORL);
	rd32(IGC_TORH);
	rd32(IGC_TOTL);
	rd32(IGC_TOTH);
	rd32(IGC_TPR);
	rd32(IGC_TPT);
	rd32(IGC_MPTC);
	rd32(IGC_BPTC);

	rd32(IGC_PRC64);
	rd32(IGC_PRC127);
	rd32(IGC_PRC255);
	rd32(IGC_PRC511);
	rd32(IGC_PRC1023);
	rd32(IGC_PRC1522);
	rd32(IGC_PTC64);
	rd32(IGC_PTC127);
	rd32(IGC_PTC255);
	rd32(IGC_PTC511);
	rd32(IGC_PTC1023);
	rd32(IGC_PTC1522);

	rd32(IGC_ALGNERRC);
	rd32(IGC_RXERRC);
	rd32(IGC_TNCRS);
	rd32(IGC_HTDPMC);
	rd32(IGC_TSCTC);

	rd32(IGC_MGTPRC);
	rd32(IGC_MGTPDC);
	rd32(IGC_MGTPTC);

	rd32(IGC_IAC);

	rd32(IGC_RPTHC);
	rd32(IGC_TLPIC);
	rd32(IGC_RLPIC);
	rd32(IGC_HGPTC);
	rd32(IGC_RXDMTC);
	rd32(IGC_HGORCL);
	rd32(IGC_HGORCH);
	rd32(IGC_HGOTCL);
	rd32(IGC_HGOTCH);
	rd32(IGC_LENERRS);
}

/**
 * igc_rar_set - Set receive address register
 * @hw: pointer to the HW structure
 * @addr: pointer to the receive address
 * @index: receive address array register
 *
 * Sets the receive address array register at index to the address passed
 * in by addr.
 */
void igc_rar_set(struct igc_hw *hw, u8 *addr, u32 index)
{
	u32 rar_low, rar_high;

	/* HW expects these in little endian so we reverse the byte order
	 * from network order (big endian) to little endian
	 */
	rar_low = ((u32)addr[0] |
		   ((u32)addr[1] << 8) |
		   ((u32)addr[2] << 16) | ((u32)addr[3] << 24));

	rar_high = ((u32)addr[4] | ((u32)addr[5] << 8));

	/* If MAC address zero, no need to set the AV bit */
	if (rar_low || rar_high)
		rar_high |= IGC_RAH_AV;

	/* Some bridges will combine consecutive 32-bit writes into
	 * a single burst write, which will malfunction on some parts.
	 * The flushes avoid this.
	 */
	wr32(IGC_RAL(index), rar_low);
	wrfl();
	wr32(IGC_RAH(index), rar_high);
	wrfl();
}

/**
 * igc_check_for_copper_link - Check for link (Copper)
 * @hw: pointer to the HW structure
 *
 * Checks to see of the link status of the hardware has changed.  If a
 * change in link status has been detected, then we read the PHY registers
 * to get the current speed/duplex if link exists.
 */
s32 igc_check_for_copper_link(struct igc_hw *hw)
{
	struct igc_mac_info *mac = &hw->mac;
	bool link = false;
	s32 ret_val;

	/* We only want to go out to the PHY registers to see if Auto-Neg
	 * has completed and/or if our link status has changed.  The
	 * get_link_status flag is set upon receiving a Link Status
	 * Change or Rx Sequence Error interrupt.
	 */
	if (!mac->get_link_status) {
		ret_val = 0;
		goto out;
	}

	/* First we want to see if the MII Status Register reports
	 * link.  If so, then we want to get the current speed/duplex
	 * of the PHY.
	 */
	ret_val = igc_phy_has_link(hw, 1, 0, &link);
	if (ret_val)
		goto out;

	if (!link)
		goto out; /* No link detected */

	mac->get_link_status = false;

	/* Check if there was DownShift, must be checked
	 * immediately after link-up
	 */
	igc_check_downshift(hw);

	/* Auto-Neg is enabled.  Auto Speed Detection takes care
	 * of MAC speed/duplex configuration.  So we only need to
	 * configure Collision Distance in the MAC.
	 */
	igc_config_collision_dist(hw);

	/* Configure Flow Control now that Auto-Neg has completed.
	 * First, we need to restore the desired flow control
	 * settings because we may have had to re-autoneg with a
	 * different link partner.
	 */
	ret_val = igc_config_fc_after_link_up(hw);
	if (ret_val)
		hw_dbg("Error configuring flow control\n");

out:
	/* Now that we are aware of our link settings, we can set the LTR
	 * thresholds.
	 */
	ret_val = igc_set_ltr_i225(hw, link);

	return ret_val;
}

/**
 * igc_config_collision_dist - Configure collision distance
 * @hw: pointer to the HW structure
 *
 * Configures the collision distance to the default value and is used
 * during link setup. Currently no func pointer exists and all
 * implementations are handled in the generic version of this function.
 */
void igc_config_collision_dist(struct igc_hw *hw)
{
	u32 tctl;

	tctl = rd32(IGC_TCTL);

	tctl &= ~IGC_TCTL_COLD;
	tctl |= IGC_COLLISION_DISTANCE << IGC_COLD_SHIFT;

	wr32(IGC_TCTL, tctl);
	wrfl();
}

/**
 * igc_config_fc_after_link_up - Configures flow control after link
 * @hw: pointer to the HW structure
 *
 * Checks the status of auto-negotiation after link up to ensure that the
 * speed and duplex were not forced.  If the link needed to be forced, then
 * flow control needs to be forced also.  If auto-negotiation is enabled
 * and did not fail, then we configure flow control based on our link
 * partner.
 */
s32 igc_config_fc_after_link_up(struct igc_hw *hw)
{
	u16 mii_status_reg, mii_nway_adv_reg, mii_nway_lp_ability_reg;
	struct igc_mac_info *mac = &hw->mac;
	u16 speed, duplex;
	s32 ret_val = 0;

	/* Check for the case where we have fiber media and auto-neg failed
	 * so we had to force link.  In this case, we need to force the
	 * configuration of the MAC to match the "fc" parameter.
	 */
	if (mac->autoneg_failed)
		ret_val = igc_force_mac_fc(hw);

	if (ret_val) {
		hw_dbg("Error forcing flow control settings\n");
		goto out;
	}

	/* In auto-neg, we need to check and see if Auto-Neg has completed,
	 * and if so, how the PHY and link partner has flow control
	 * configured.
	 */

	/* Read the MII Status Register and check to see if AutoNeg
	 * has completed.  We read this twice because this reg has
	 * some "sticky" (latched) bits.
	 */
	ret_val = hw->phy.ops.read_reg(hw, PHY_STATUS,
				       &mii_status_reg);
	if (ret_val)
		goto out;
	ret_val = hw->phy.ops.read_reg(hw, PHY_STATUS,
				       &mii_status_reg);
	if (ret_val)
		goto out;

	if (!(mii_status_reg & MII_SR_AUTONEG_COMPLETE)) {
		hw_dbg("Copper PHY and Auto Neg has not completed.\n");
		goto out;
	}

	/* The AutoNeg process has completed, so we now need to
	 * read both the Auto Negotiation Advertisement
	 * Register (Address 4) and the Auto_Negotiation Base
	 * Page Ability Register (Address 5) to determine how
	 * flow control was negotiated.
	 */
	ret_val = hw->phy.ops.read_reg(hw, PHY_AUTONEG_ADV,
				       &mii_nway_adv_reg);
	if (ret_val)
		goto out;
	ret_val = hw->phy.ops.read_reg(hw, PHY_LP_ABILITY,
				       &mii_nway_lp_ability_reg);
	if (ret_val)
		goto out;
	/* Two bits in the Auto Negotiation Advertisement Register
	 * (Address 4) and two bits in the Auto Negotiation Base
	 * Page Ability Register (Address 5) determine flow control
	 * for both the PHY and the link partner.  The following
	 * table, taken out of the IEEE 802.3ab/D6.0 dated March 25,
	 * 1999, describes these PAUSE resolution bits and how flow
	 * control is determined based upon these settings.
	 * NOTE:  DC = Don't Care
	 *
	 *   LOCAL DEVICE  |   LINK PARTNER
	 * PAUSE | ASM_DIR | PAUSE | ASM_DIR | NIC Resolution
	 *-------|---------|-------|---------|--------------------
	 *   0   |    0    |  DC   |   DC    | igc_fc_none
	 *   0   |    1    |   0   |   DC    | igc_fc_none
	 *   0   |    1    |   1   |    0    | igc_fc_none
	 *   0   |    1    |   1   |    1    | igc_fc_tx_pause
	 *   1   |    0    |   0   |   DC    | igc_fc_none
	 *   1   |   DC    |   1   |   DC    | igc_fc_full
	 *   1   |    1    |   0   |    0    | igc_fc_none
	 *   1   |    1    |   0   |    1    | igc_fc_rx_pause
	 *
	 * Are both PAUSE bits set to 1?  If so, this implies
	 * Symmetric Flow Control is enabled at both ends.  The
	 * ASM_DIR bits are irrelevant per the spec.
	 *
	 * For Symmetric Flow Control:
	 *
	 *   LOCAL DEVICE  |   LINK PARTNER
	 * PAUSE | ASM_DIR | PAUSE | ASM_DIR | Result
	 *-------|---------|-------|---------|--------------------
	 *   1   |   DC    |   1   |   DC    | IGC_fc_full
	 *
	 */
	if ((mii_nway_adv_reg & NWAY_AR_PAUSE) &&
	    (mii_nway_lp_ability_reg & NWAY_LPAR_PAUSE)) {
		/* Now we need to check if the user selected RX ONLY
		 * of pause frames.  In this case, we had to advertise
		 * FULL flow control because we could not advertise RX
		 * ONLY. Hence, we must now check to see if we need to
		 * turn OFF  the TRANSMISSION of PAUSE frames.
		 */
		if (hw->fc.requested_mode == igc_fc_full) {
			hw->fc.current_mode = igc_fc_full;
			hw_dbg("Flow Control = FULL.\n");
		} else {
			hw->fc.current_mode = igc_fc_rx_pause;
			hw_dbg("Flow Control = RX PAUSE frames only.\n");
		}
	}

	/* For receiving PAUSE frames ONLY.
	 *
	 *   LOCAL DEVICE  |   LINK PARTNER
	 * PAUSE | ASM_DIR | PAUSE | ASM_DIR | Result
	 *-------|---------|-------|---------|--------------------
	 *   0   |    1    |   1   |    1    | igc_fc_tx_pause
	 */
	else if (!(mii_nway_adv_reg & NWAY_AR_PAUSE) &&
		 (mii_nway_adv_reg & NWAY_AR_ASM_DIR) &&
		 (mii_nway_lp_ability_reg & NWAY_LPAR_PAUSE) &&
		 (mii_nway_lp_ability_reg & NWAY_LPAR_ASM_DIR)) {
		hw->fc.current_mode = igc_fc_tx_pause;
		hw_dbg("Flow Control = TX PAUSE frames only.\n");
	}
	/* For transmitting PAUSE frames ONLY.
	 *
	 *   LOCAL DEVICE  |   LINK PARTNER
	 * PAUSE | ASM_DIR | PAUSE | ASM_DIR | Result
	 *-------|---------|-------|---------|--------------------
	 *   1   |    1    |   0   |    1    | igc_fc_rx_pause
	 */
	else if ((mii_nway_adv_reg & NWAY_AR_PAUSE) &&
		 (mii_nway_adv_reg & NWAY_AR_ASM_DIR) &&
		 !(mii_nway_lp_ability_reg & NWAY_LPAR_PAUSE) &&
		 (mii_nway_lp_ability_reg & NWAY_LPAR_ASM_DIR)) {
		hw->fc.current_mode = igc_fc_rx_pause;
		hw_dbg("Flow Control = RX PAUSE frames only.\n");
	}
	/* Per the IEEE spec, at this point flow control should be
	 * disabled.  However, we want to consider that we could
	 * be connected to a legacy switch that doesn't advertise
	 * desired flow control, but can be forced on the link
	 * partner.  So if we advertised no flow control, that is
	 * what we will resolve to.  If we advertised some kind of
	 * receive capability (Rx Pause Only or Full Flow Control)
	 * and the link partner advertised none, we will configure
	 * ourselves to enable Rx Flow Control only.  We can do
	 * this safely for two reasons:  If the link partner really
	 * didn't want flow control enabled, and we enable Rx, no
	 * harm done since we won't be receiving any PAUSE frames
	 * anyway.  If the intent on the link partner was to have
	 * flow control enabled, then by us enabling RX only, we
	 * can at least receive pause frames and process them.
	 * This is a good idea because in most cases, since we are
	 * predominantly a server NIC, more times than not we will
	 * be asked to delay transmission of packets than asking
	 * our link partner to pause transmission of frames.
	 */
	else if ((hw->fc.requested_mode == igc_fc_none) ||
		 (hw->fc.requested_mode == igc_fc_tx_pause) ||
		 (hw->fc.strict_ieee)) {
		hw->fc.current_mode = igc_fc_none;
		hw_dbg("Flow Control = NONE.\n");
	} else {
		hw->fc.current_mode = igc_fc_rx_pause;
		hw_dbg("Flow Control = RX PAUSE frames only.\n");
	}

	/* Now we need to do one last check...  If we auto-
	 * negotiated to HALF DUPLEX, flow control should not be
	 * enabled per IEEE 802.3 spec.
	 */
	ret_val = hw->mac.ops.get_speed_and_duplex(hw, &speed, &duplex);
	if (ret_val) {
		hw_dbg("Error getting link speed and duplex\n");
		goto out;
	}

	if (duplex == HALF_DUPLEX)
		hw->fc.current_mode = igc_fc_none;

	/* Now we call a subroutine to actually force the MAC
	 * controller to use the correct flow control settings.
	 */
	ret_val = igc_force_mac_fc(hw);
	if (ret_val) {
		hw_dbg("Error forcing flow control settings\n");
		goto out;
	}

out:
	return ret_val;
}

/**
 * igc_get_auto_rd_done - Check for auto read completion
 * @hw: pointer to the HW structure
 *
 * Check EEPROM for Auto Read done bit.
 */
s32 igc_get_auto_rd_done(struct igc_hw *hw)
{
	s32 ret_val = 0;
	s32 i = 0;

	while (i < AUTO_READ_DONE_TIMEOUT) {
		if (rd32(IGC_EECD) & IGC_EECD_AUTO_RD)
			break;
		usleep_range(1000, 2000);
		i++;
	}

	if (i == AUTO_READ_DONE_TIMEOUT) {
		hw_dbg("Auto read by HW from NVM has not completed.\n");
		ret_val = -IGC_ERR_RESET;
		goto out;
	}

out:
	return ret_val;
}

/**
 * igc_get_speed_and_duplex_copper - Retrieve current speed/duplex
 * @hw: pointer to the HW structure
 * @speed: stores the current speed
 * @duplex: stores the current duplex
 *
 * Read the status register for the current speed/duplex and store the current
 * speed and duplex for copper connections.
 */
s32 igc_get_speed_and_duplex_copper(struct igc_hw *hw, u16 *speed,
				    u16 *duplex)
{
	u32 status;

	status = rd32(IGC_STATUS);
	if (status & IGC_STATUS_SPEED_1000) {
		/* For I225, STATUS will indicate 1G speed in both 1 Gbps
		 * and 2.5 Gbps link modes. An additional bit is used
		 * to differentiate between 1 Gbps and 2.5 Gbps.
		 */
		if (hw->mac.type == igc_i225 &&
		    (status & IGC_STATUS_SPEED_2500)) {
			*speed = SPEED_2500;
			hw_dbg("2500 Mbs, ");
		} else {
			*speed = SPEED_1000;
			hw_dbg("1000 Mbs, ");
		}
	} else if (status & IGC_STATUS_SPEED_100) {
		*speed = SPEED_100;
		hw_dbg("100 Mbs, ");
	} else {
		*speed = SPEED_10;
		hw_dbg("10 Mbs, ");
	}

	if (status & IGC_STATUS_FD) {
		*duplex = FULL_DUPLEX;
		hw_dbg("Full Duplex\n");
	} else {
		*duplex = HALF_DUPLEX;
		hw_dbg("Half Duplex\n");
	}

	return 0;
}

/**
 * igc_put_hw_semaphore - Release hardware semaphore
 * @hw: pointer to the HW structure
 *
 * Release hardware semaphore used to access the PHY or NVM
 */
void igc_put_hw_semaphore(struct igc_hw *hw)
{
	u32 swsm;

	swsm = rd32(IGC_SWSM);

	swsm &= ~(IGC_SWSM_SMBI | IGC_SWSM_SWESMBI);

	wr32(IGC_SWSM, swsm);
}

/**
 * igc_enable_mng_pass_thru - Enable processing of ARP's
 * @hw: pointer to the HW structure
 *
 * Verifies the hardware needs to leave interface enabled so that frames can
 * be directed to and from the management interface.
 */
bool igc_enable_mng_pass_thru(struct igc_hw *hw)
{
	bool ret_val = false;
	u32 fwsm, factps;
	u32 manc;

	if (!hw->mac.asf_firmware_present)
		goto out;

	manc = rd32(IGC_MANC);

	if (!(manc & IGC_MANC_RCV_TCO_EN))
		goto out;

	if (hw->mac.arc_subsystem_valid) {
		fwsm = rd32(IGC_FWSM);
		factps = rd32(IGC_FACTPS);

		if (!(factps & IGC_FACTPS_MNGCG) &&
		    ((fwsm & IGC_FWSM_MODE_MASK) ==
		    (igc_mng_mode_pt << IGC_FWSM_MODE_SHIFT))) {
			ret_val = true;
			goto out;
		}
	} else {
		if ((manc & IGC_MANC_SMBUS_EN) &&
		    !(manc & IGC_MANC_ASF_EN)) {
			ret_val = true;
			goto out;
		}
	}

out:
	return ret_val;
}

/**
 *  igc_hash_mc_addr - Generate a multicast hash value
 *  @hw: pointer to the HW structure
 *  @mc_addr: pointer to a multicast address
 *
 *  Generates a multicast address hash value which is used to determine
 *  the multicast filter table array address and new table value.  See
 *  igc_mta_set()
 **/
static u32 igc_hash_mc_addr(struct igc_hw *hw, u8 *mc_addr)
{
	u32 hash_value, hash_mask;
	u8 bit_shift = 0;

	/* Register count multiplied by bits per register */
	hash_mask = (hw->mac.mta_reg_count * 32) - 1;

	/* For a mc_filter_type of 0, bit_shift is the number of left-shifts
	 * where 0xFF would still fall within the hash mask.
	 */
	while (hash_mask >> bit_shift != 0xFF)
		bit_shift++;

	/* The portion of the address that is used for the hash table
	 * is determined by the mc_filter_type setting.
	 * The algorithm is such that there is a total of 8 bits of shifting.
	 * The bit_shift for a mc_filter_type of 0 represents the number of
	 * left-shifts where the MSB of mc_addr[5] would still fall within
	 * the hash_mask.  Case 0 does this exactly.  Since there are a total
	 * of 8 bits of shifting, then mc_addr[4] will shift right the
	 * remaining number of bits. Thus 8 - bit_shift.  The rest of the
	 * cases are a variation of this algorithm...essentially raising the
	 * number of bits to shift mc_addr[5] left, while still keeping the
	 * 8-bit shifting total.
	 *
	 * For example, given the following Destination MAC Address and an
	 * MTA register count of 128 (thus a 4096-bit vector and 0xFFF mask),
	 * we can see that the bit_shift for case 0 is 4.  These are the hash
	 * values resulting from each mc_filter_type...
	 * [0] [1] [2] [3] [4] [5]
	 * 01  AA  00  12  34  56
	 * LSB                 MSB
	 *
	 * case 0: hash_value = ((0x34 >> 4) | (0x56 << 4)) & 0xFFF = 0x563
	 * case 1: hash_value = ((0x34 >> 3) | (0x56 << 5)) & 0xFFF = 0xAC6
	 * case 2: hash_value = ((0x34 >> 2) | (0x56 << 6)) & 0xFFF = 0x163
	 * case 3: hash_value = ((0x34 >> 0) | (0x56 << 8)) & 0xFFF = 0x634
	 */
	switch (hw->mac.mc_filter_type) {
	default:
	case 0:
		break;
	case 1:
		bit_shift += 1;
		break;
	case 2:
		bit_shift += 2;
		break;
	case 3:
		bit_shift += 4;
		break;
	}

	hash_value = hash_mask & (((mc_addr[4] >> (8 - bit_shift)) |
				  (((u16)mc_addr[5]) << bit_shift)));

	return hash_value;
}

/**
 *  igc_update_mc_addr_list - Update Multicast addresses
 *  @hw: pointer to the HW structure
 *  @mc_addr_list: array of multicast addresses to program
 *  @mc_addr_count: number of multicast addresses to program
 *
 *  Updates entire Multicast Table Array.
 *  The caller must have a packed mc_addr_list of multicast addresses.
 **/
void igc_update_mc_addr_list(struct igc_hw *hw,
			     u8 *mc_addr_list, u32 mc_addr_count)
{
	u32 hash_value, hash_bit, hash_reg;
	int i;

	/* clear mta_shadow */
	memset(&hw->mac.mta_shadow, 0, sizeof(hw->mac.mta_shadow));

	/* update mta_shadow from mc_addr_list */
	for (i = 0; (u32)i < mc_addr_count; i++) {
		hash_value = igc_hash_mc_addr(hw, mc_addr_list);

		hash_reg = (hash_value >> 5) & (hw->mac.mta_reg_count - 1);
		hash_bit = hash_value & 0x1F;

		hw->mac.mta_shadow[hash_reg] |= BIT(hash_bit);
		mc_addr_list += ETH_ALEN;
	}

	/* replace the entire MTA table */
	for (i = hw->mac.mta_reg_count - 1; i >= 0; i--)
		array_wr32(IGC_MTA, i, hw->mac.mta_shadow[i]);
	wrfl();
}
