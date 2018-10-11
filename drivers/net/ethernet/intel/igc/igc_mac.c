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
