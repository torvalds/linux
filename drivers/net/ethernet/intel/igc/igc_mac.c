// SPDX-License-Identifier: GPL-2.0
/* Copyright (c)  2018 Intel Corporation */

#include <linux/pci.h>
#include <linux/delay.h>

#include "igc_mac.h"
#include "igc_hw.h"

/* forward declaration */
static s32 igc_set_default_fc(struct igc_hw *hw);
static s32 igc_set_fc_watermarks(struct igc_hw *hw);

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

	/* If requested flow control is set to default, set flow control
	 * based on the EEPROM flow control settings.
	 */
	if (hw->fc.requested_mode == igc_fc_default) {
		ret_val = igc_set_default_fc(hw);
		if (ret_val)
			goto out;
	}

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
 * igc_set_default_fc - Set flow control default values
 * @hw: pointer to the HW structure
 *
 * Read the EEPROM for the default values for flow control and store the
 * values.
 */
static s32 igc_set_default_fc(struct igc_hw *hw)
{
	return 0;
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
 * igc_clear_hw_cntrs_base - Clear base hardware counters
 * @hw: pointer to the HW structure
 *
 * Clears the base hardware counters by reading the counter registers.
 */
void igc_clear_hw_cntrs_base(struct igc_hw *hw)
{
	rd32(IGC_CRCERRS);
	rd32(IGC_SYMERRS);
	rd32(IGC_MPC);
	rd32(IGC_SCC);
	rd32(IGC_ECOL);
	rd32(IGC_MCC);
	rd32(IGC_LATECOL);
	rd32(IGC_COLC);
	rd32(IGC_DC);
	rd32(IGC_SEC);
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
	rd32(IGC_CEXTERR);
	rd32(IGC_TSCTC);
	rd32(IGC_TSCTFC);

	rd32(IGC_MGTPRC);
	rd32(IGC_MGTPDC);
	rd32(IGC_MGTPTC);

	rd32(IGC_IAC);
	rd32(IGC_ICRXOC);

	rd32(IGC_ICRXPTC);
	rd32(IGC_ICRXATC);
	rd32(IGC_ICTXPTC);
	rd32(IGC_ICTXATC);
	rd32(IGC_ICTXQEC);
	rd32(IGC_ICTXQMTC);
	rd32(IGC_ICRXDMTC);

	rd32(IGC_CBTMPC);
	rd32(IGC_HTDPMC);
	rd32(IGC_CBRMPC);
	rd32(IGC_RPTHC);
	rd32(IGC_HGPTC);
	rd32(IGC_HTCBDPC);
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
	s32 ret_val;
	bool link;

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

	/* If we are forcing speed/duplex, then we simply return since
	 * we have already determined whether we have link or not.
	 */
	if (!mac->autoneg) {
		ret_val = -IGC_ERR_CONFIG;
		goto out;
	}

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
	if (ret_val)
		hw_dbg("Error configuring flow control\n");

out:
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
