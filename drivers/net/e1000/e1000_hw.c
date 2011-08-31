/*******************************************************************************

  Intel PRO/1000 Linux driver
  Copyright(c) 1999 - 2006 Intel Corporation.

  This program is free software; you can redistribute it and/or modify it
  under the terms and conditions of the GNU General Public License,
  version 2, as published by the Free Software Foundation.

  This program is distributed in the hope it will be useful, but WITHOUT
  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
  more details.

  You should have received a copy of the GNU General Public License along with
  this program; if not, write to the Free Software Foundation, Inc.,
  51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.

  The full GNU General Public License is included in this distribution in
  the file called "COPYING".

  Contact Information:
  Linux NICS <linux.nics@intel.com>
  e1000-devel Mailing List <e1000-devel@lists.sourceforge.net>
  Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497

 */

/* e1000_hw.c
 * Shared functions for accessing and configuring the MAC
 */

#include "e1000.h"

static s32 e1000_check_downshift(struct e1000_hw *hw);
static s32 e1000_check_polarity(struct e1000_hw *hw,
				e1000_rev_polarity *polarity);
static void e1000_clear_hw_cntrs(struct e1000_hw *hw);
static void e1000_clear_vfta(struct e1000_hw *hw);
static s32 e1000_config_dsp_after_link_change(struct e1000_hw *hw,
					      bool link_up);
static s32 e1000_config_fc_after_link_up(struct e1000_hw *hw);
static s32 e1000_detect_gig_phy(struct e1000_hw *hw);
static s32 e1000_get_auto_rd_done(struct e1000_hw *hw);
static s32 e1000_get_cable_length(struct e1000_hw *hw, u16 *min_length,
				  u16 *max_length);
static s32 e1000_get_phy_cfg_done(struct e1000_hw *hw);
static s32 e1000_id_led_init(struct e1000_hw *hw);
static void e1000_init_rx_addrs(struct e1000_hw *hw);
static s32 e1000_phy_igp_get_info(struct e1000_hw *hw,
				  struct e1000_phy_info *phy_info);
static s32 e1000_phy_m88_get_info(struct e1000_hw *hw,
				  struct e1000_phy_info *phy_info);
static s32 e1000_set_d3_lplu_state(struct e1000_hw *hw, bool active);
static s32 e1000_wait_autoneg(struct e1000_hw *hw);
static void e1000_write_reg_io(struct e1000_hw *hw, u32 offset, u32 value);
static s32 e1000_set_phy_type(struct e1000_hw *hw);
static void e1000_phy_init_script(struct e1000_hw *hw);
static s32 e1000_setup_copper_link(struct e1000_hw *hw);
static s32 e1000_setup_fiber_serdes_link(struct e1000_hw *hw);
static s32 e1000_adjust_serdes_amplitude(struct e1000_hw *hw);
static s32 e1000_phy_force_speed_duplex(struct e1000_hw *hw);
static s32 e1000_config_mac_to_phy(struct e1000_hw *hw);
static void e1000_raise_mdi_clk(struct e1000_hw *hw, u32 *ctrl);
static void e1000_lower_mdi_clk(struct e1000_hw *hw, u32 *ctrl);
static void e1000_shift_out_mdi_bits(struct e1000_hw *hw, u32 data, u16 count);
static u16 e1000_shift_in_mdi_bits(struct e1000_hw *hw);
static s32 e1000_phy_reset_dsp(struct e1000_hw *hw);
static s32 e1000_write_eeprom_spi(struct e1000_hw *hw, u16 offset,
				  u16 words, u16 *data);
static s32 e1000_write_eeprom_microwire(struct e1000_hw *hw, u16 offset,
					u16 words, u16 *data);
static s32 e1000_spi_eeprom_ready(struct e1000_hw *hw);
static void e1000_raise_ee_clk(struct e1000_hw *hw, u32 *eecd);
static void e1000_lower_ee_clk(struct e1000_hw *hw, u32 *eecd);
static void e1000_shift_out_ee_bits(struct e1000_hw *hw, u16 data, u16 count);
static s32 e1000_write_phy_reg_ex(struct e1000_hw *hw, u32 reg_addr,
				  u16 phy_data);
static s32 e1000_read_phy_reg_ex(struct e1000_hw *hw, u32 reg_addr,
				 u16 *phy_data);
static u16 e1000_shift_in_ee_bits(struct e1000_hw *hw, u16 count);
static s32 e1000_acquire_eeprom(struct e1000_hw *hw);
static void e1000_release_eeprom(struct e1000_hw *hw);
static void e1000_standby_eeprom(struct e1000_hw *hw);
static s32 e1000_set_vco_speed(struct e1000_hw *hw);
static s32 e1000_polarity_reversal_workaround(struct e1000_hw *hw);
static s32 e1000_set_phy_mode(struct e1000_hw *hw);
static s32 e1000_do_read_eeprom(struct e1000_hw *hw, u16 offset, u16 words,
				u16 *data);
static s32 e1000_do_write_eeprom(struct e1000_hw *hw, u16 offset, u16 words,
				 u16 *data);

/* IGP cable length table */
static const
u16 e1000_igp_cable_length_table[IGP01E1000_AGC_LENGTH_TABLE_SIZE] = {
	5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5,
	5, 10, 10, 10, 10, 10, 10, 10, 20, 20, 20, 20, 20, 25, 25, 25,
	25, 25, 25, 25, 30, 30, 30, 30, 40, 40, 40, 40, 40, 40, 40, 40,
	40, 50, 50, 50, 50, 50, 50, 50, 60, 60, 60, 60, 60, 60, 60, 60,
	60, 70, 70, 70, 70, 70, 70, 80, 80, 80, 80, 80, 80, 90, 90, 90,
	90, 90, 90, 90, 90, 90, 100, 100, 100, 100, 100, 100, 100, 100, 100,
	    100,
	100, 100, 100, 100, 110, 110, 110, 110, 110, 110, 110, 110, 110, 110,
	    110, 110,
	110, 110, 110, 110, 110, 110, 120, 120, 120, 120, 120, 120, 120, 120,
	    120, 120
};

static DEFINE_SPINLOCK(e1000_eeprom_lock);

/**
 * e1000_set_phy_type - Set the phy type member in the hw struct.
 * @hw: Struct containing variables accessed by shared code
 */
static s32 e1000_set_phy_type(struct e1000_hw *hw)
{
	e_dbg("e1000_set_phy_type");

	if (hw->mac_type == e1000_undefined)
		return -E1000_ERR_PHY_TYPE;

	switch (hw->phy_id) {
	case M88E1000_E_PHY_ID:
	case M88E1000_I_PHY_ID:
	case M88E1011_I_PHY_ID:
	case M88E1111_I_PHY_ID:
	case M88E1118_E_PHY_ID:
		hw->phy_type = e1000_phy_m88;
		break;
	case IGP01E1000_I_PHY_ID:
		if (hw->mac_type == e1000_82541 ||
		    hw->mac_type == e1000_82541_rev_2 ||
		    hw->mac_type == e1000_82547 ||
		    hw->mac_type == e1000_82547_rev_2)
			hw->phy_type = e1000_phy_igp;
		break;
	case RTL8211B_PHY_ID:
		hw->phy_type = e1000_phy_8211;
		break;
	case RTL8201N_PHY_ID:
		hw->phy_type = e1000_phy_8201;
		break;
	default:
		/* Should never have loaded on this device */
		hw->phy_type = e1000_phy_undefined;
		return -E1000_ERR_PHY_TYPE;
	}

	return E1000_SUCCESS;
}

/**
 * e1000_phy_init_script - IGP phy init script - initializes the GbE PHY
 * @hw: Struct containing variables accessed by shared code
 */
static void e1000_phy_init_script(struct e1000_hw *hw)
{
	u32 ret_val;
	u16 phy_saved_data;

	e_dbg("e1000_phy_init_script");

	if (hw->phy_init_script) {
		msleep(20);

		/* Save off the current value of register 0x2F5B to be restored at
		 * the end of this routine. */
		ret_val = e1000_read_phy_reg(hw, 0x2F5B, &phy_saved_data);

		/* Disabled the PHY transmitter */
		e1000_write_phy_reg(hw, 0x2F5B, 0x0003);
		msleep(20);

		e1000_write_phy_reg(hw, 0x0000, 0x0140);
		msleep(5);

		switch (hw->mac_type) {
		case e1000_82541:
		case e1000_82547:
			e1000_write_phy_reg(hw, 0x1F95, 0x0001);
			e1000_write_phy_reg(hw, 0x1F71, 0xBD21);
			e1000_write_phy_reg(hw, 0x1F79, 0x0018);
			e1000_write_phy_reg(hw, 0x1F30, 0x1600);
			e1000_write_phy_reg(hw, 0x1F31, 0x0014);
			e1000_write_phy_reg(hw, 0x1F32, 0x161C);
			e1000_write_phy_reg(hw, 0x1F94, 0x0003);
			e1000_write_phy_reg(hw, 0x1F96, 0x003F);
			e1000_write_phy_reg(hw, 0x2010, 0x0008);
			break;

		case e1000_82541_rev_2:
		case e1000_82547_rev_2:
			e1000_write_phy_reg(hw, 0x1F73, 0x0099);
			break;
		default:
			break;
		}

		e1000_write_phy_reg(hw, 0x0000, 0x3300);
		msleep(20);

		/* Now enable the transmitter */
		e1000_write_phy_reg(hw, 0x2F5B, phy_saved_data);

		if (hw->mac_type == e1000_82547) {
			u16 fused, fine, coarse;

			/* Move to analog registers page */
			e1000_read_phy_reg(hw,
					   IGP01E1000_ANALOG_SPARE_FUSE_STATUS,
					   &fused);

			if (!(fused & IGP01E1000_ANALOG_SPARE_FUSE_ENABLED)) {
				e1000_read_phy_reg(hw,
						   IGP01E1000_ANALOG_FUSE_STATUS,
						   &fused);

				fine = fused & IGP01E1000_ANALOG_FUSE_FINE_MASK;
				coarse =
				    fused & IGP01E1000_ANALOG_FUSE_COARSE_MASK;

				if (coarse >
				    IGP01E1000_ANALOG_FUSE_COARSE_THRESH) {
					coarse -=
					    IGP01E1000_ANALOG_FUSE_COARSE_10;
					fine -= IGP01E1000_ANALOG_FUSE_FINE_1;
				} else if (coarse ==
					   IGP01E1000_ANALOG_FUSE_COARSE_THRESH)
					fine -= IGP01E1000_ANALOG_FUSE_FINE_10;

				fused =
				    (fused & IGP01E1000_ANALOG_FUSE_POLY_MASK) |
				    (fine & IGP01E1000_ANALOG_FUSE_FINE_MASK) |
				    (coarse &
				     IGP01E1000_ANALOG_FUSE_COARSE_MASK);

				e1000_write_phy_reg(hw,
						    IGP01E1000_ANALOG_FUSE_CONTROL,
						    fused);
				e1000_write_phy_reg(hw,
						    IGP01E1000_ANALOG_FUSE_BYPASS,
						    IGP01E1000_ANALOG_FUSE_ENABLE_SW_CONTROL);
			}
		}
	}
}

/**
 * e1000_set_mac_type - Set the mac type member in the hw struct.
 * @hw: Struct containing variables accessed by shared code
 */
s32 e1000_set_mac_type(struct e1000_hw *hw)
{
	e_dbg("e1000_set_mac_type");

	switch (hw->device_id) {
	case E1000_DEV_ID_82542:
		switch (hw->revision_id) {
		case E1000_82542_2_0_REV_ID:
			hw->mac_type = e1000_82542_rev2_0;
			break;
		case E1000_82542_2_1_REV_ID:
			hw->mac_type = e1000_82542_rev2_1;
			break;
		default:
			/* Invalid 82542 revision ID */
			return -E1000_ERR_MAC_TYPE;
		}
		break;
	case E1000_DEV_ID_82543GC_FIBER:
	case E1000_DEV_ID_82543GC_COPPER:
		hw->mac_type = e1000_82543;
		break;
	case E1000_DEV_ID_82544EI_COPPER:
	case E1000_DEV_ID_82544EI_FIBER:
	case E1000_DEV_ID_82544GC_COPPER:
	case E1000_DEV_ID_82544GC_LOM:
		hw->mac_type = e1000_82544;
		break;
	case E1000_DEV_ID_82540EM:
	case E1000_DEV_ID_82540EM_LOM:
	case E1000_DEV_ID_82540EP:
	case E1000_DEV_ID_82540EP_LOM:
	case E1000_DEV_ID_82540EP_LP:
		hw->mac_type = e1000_82540;
		break;
	case E1000_DEV_ID_82545EM_COPPER:
	case E1000_DEV_ID_82545EM_FIBER:
		hw->mac_type = e1000_82545;
		break;
	case E1000_DEV_ID_82545GM_COPPER:
	case E1000_DEV_ID_82545GM_FIBER:
	case E1000_DEV_ID_82545GM_SERDES:
		hw->mac_type = e1000_82545_rev_3;
		break;
	case E1000_DEV_ID_82546EB_COPPER:
	case E1000_DEV_ID_82546EB_FIBER:
	case E1000_DEV_ID_82546EB_QUAD_COPPER:
		hw->mac_type = e1000_82546;
		break;
	case E1000_DEV_ID_82546GB_COPPER:
	case E1000_DEV_ID_82546GB_FIBER:
	case E1000_DEV_ID_82546GB_SERDES:
	case E1000_DEV_ID_82546GB_PCIE:
	case E1000_DEV_ID_82546GB_QUAD_COPPER:
	case E1000_DEV_ID_82546GB_QUAD_COPPER_KSP3:
		hw->mac_type = e1000_82546_rev_3;
		break;
	case E1000_DEV_ID_82541EI:
	case E1000_DEV_ID_82541EI_MOBILE:
	case E1000_DEV_ID_82541ER_LOM:
		hw->mac_type = e1000_82541;
		break;
	case E1000_DEV_ID_82541ER:
	case E1000_DEV_ID_82541GI:
	case E1000_DEV_ID_82541GI_LF:
	case E1000_DEV_ID_82541GI_MOBILE:
		hw->mac_type = e1000_82541_rev_2;
		break;
	case E1000_DEV_ID_82547EI:
	case E1000_DEV_ID_82547EI_MOBILE:
		hw->mac_type = e1000_82547;
		break;
	case E1000_DEV_ID_82547GI:
		hw->mac_type = e1000_82547_rev_2;
		break;
	case E1000_DEV_ID_INTEL_CE4100_GBE:
		hw->mac_type = e1000_ce4100;
		break;
	default:
		/* Should never have loaded on this device */
		return -E1000_ERR_MAC_TYPE;
	}

	switch (hw->mac_type) {
	case e1000_82541:
	case e1000_82547:
	case e1000_82541_rev_2:
	case e1000_82547_rev_2:
		hw->asf_firmware_present = true;
		break;
	default:
		break;
	}

	/* The 82543 chip does not count tx_carrier_errors properly in
	 * FD mode
	 */
	if (hw->mac_type == e1000_82543)
		hw->bad_tx_carr_stats_fd = true;

	if (hw->mac_type > e1000_82544)
		hw->has_smbus = true;

	return E1000_SUCCESS;
}

/**
 * e1000_set_media_type - Set media type and TBI compatibility.
 * @hw: Struct containing variables accessed by shared code
 */
void e1000_set_media_type(struct e1000_hw *hw)
{
	u32 status;

	e_dbg("e1000_set_media_type");

	if (hw->mac_type != e1000_82543) {
		/* tbi_compatibility is only valid on 82543 */
		hw->tbi_compatibility_en = false;
	}

	switch (hw->device_id) {
	case E1000_DEV_ID_82545GM_SERDES:
	case E1000_DEV_ID_82546GB_SERDES:
		hw->media_type = e1000_media_type_internal_serdes;
		break;
	default:
		switch (hw->mac_type) {
		case e1000_82542_rev2_0:
		case e1000_82542_rev2_1:
			hw->media_type = e1000_media_type_fiber;
			break;
		case e1000_ce4100:
			hw->media_type = e1000_media_type_copper;
			break;
		default:
			status = er32(STATUS);
			if (status & E1000_STATUS_TBIMODE) {
				hw->media_type = e1000_media_type_fiber;
				/* tbi_compatibility not valid on fiber */
				hw->tbi_compatibility_en = false;
			} else {
				hw->media_type = e1000_media_type_copper;
			}
			break;
		}
	}
}

/**
 * e1000_reset_hw: reset the hardware completely
 * @hw: Struct containing variables accessed by shared code
 *
 * Reset the transmit and receive units; mask and clear all interrupts.
 */
s32 e1000_reset_hw(struct e1000_hw *hw)
{
	u32 ctrl;
	u32 ctrl_ext;
	u32 icr;
	u32 manc;
	u32 led_ctrl;
	s32 ret_val;

	e_dbg("e1000_reset_hw");

	/* For 82542 (rev 2.0), disable MWI before issuing a device reset */
	if (hw->mac_type == e1000_82542_rev2_0) {
		e_dbg("Disabling MWI on 82542 rev 2.0\n");
		e1000_pci_clear_mwi(hw);
	}

	/* Clear interrupt mask to stop board from generating interrupts */
	e_dbg("Masking off all interrupts\n");
	ew32(IMC, 0xffffffff);

	/* Disable the Transmit and Receive units.  Then delay to allow
	 * any pending transactions to complete before we hit the MAC with
	 * the global reset.
	 */
	ew32(RCTL, 0);
	ew32(TCTL, E1000_TCTL_PSP);
	E1000_WRITE_FLUSH();

	/* The tbi_compatibility_on Flag must be cleared when Rctl is cleared. */
	hw->tbi_compatibility_on = false;

	/* Delay to allow any outstanding PCI transactions to complete before
	 * resetting the device
	 */
	msleep(10);

	ctrl = er32(CTRL);

	/* Must reset the PHY before resetting the MAC */
	if ((hw->mac_type == e1000_82541) || (hw->mac_type == e1000_82547)) {
		ew32(CTRL, (ctrl | E1000_CTRL_PHY_RST));
		msleep(5);
	}

	/* Issue a global reset to the MAC.  This will reset the chip's
	 * transmit, receive, DMA, and link units.  It will not effect
	 * the current PCI configuration.  The global reset bit is self-
	 * clearing, and should clear within a microsecond.
	 */
	e_dbg("Issuing a global reset to MAC\n");

	switch (hw->mac_type) {
	case e1000_82544:
	case e1000_82540:
	case e1000_82545:
	case e1000_82546:
	case e1000_82541:
	case e1000_82541_rev_2:
		/* These controllers can't ack the 64-bit write when issuing the
		 * reset, so use IO-mapping as a workaround to issue the reset */
		E1000_WRITE_REG_IO(hw, CTRL, (ctrl | E1000_CTRL_RST));
		break;
	case e1000_82545_rev_3:
	case e1000_82546_rev_3:
		/* Reset is performed on a shadow of the control register */
		ew32(CTRL_DUP, (ctrl | E1000_CTRL_RST));
		break;
	case e1000_ce4100:
	default:
		ew32(CTRL, (ctrl | E1000_CTRL_RST));
		break;
	}

	/* After MAC reset, force reload of EEPROM to restore power-on settings to
	 * device.  Later controllers reload the EEPROM automatically, so just wait
	 * for reload to complete.
	 */
	switch (hw->mac_type) {
	case e1000_82542_rev2_0:
	case e1000_82542_rev2_1:
	case e1000_82543:
	case e1000_82544:
		/* Wait for reset to complete */
		udelay(10);
		ctrl_ext = er32(CTRL_EXT);
		ctrl_ext |= E1000_CTRL_EXT_EE_RST;
		ew32(CTRL_EXT, ctrl_ext);
		E1000_WRITE_FLUSH();
		/* Wait for EEPROM reload */
		msleep(2);
		break;
	case e1000_82541:
	case e1000_82541_rev_2:
	case e1000_82547:
	case e1000_82547_rev_2:
		/* Wait for EEPROM reload */
		msleep(20);
		break;
	default:
		/* Auto read done will delay 5ms or poll based on mac type */
		ret_val = e1000_get_auto_rd_done(hw);
		if (ret_val)
			return ret_val;
		break;
	}

	/* Disable HW ARPs on ASF enabled adapters */
	if (hw->mac_type >= e1000_82540) {
		manc = er32(MANC);
		manc &= ~(E1000_MANC_ARP_EN);
		ew32(MANC, manc);
	}

	if ((hw->mac_type == e1000_82541) || (hw->mac_type == e1000_82547)) {
		e1000_phy_init_script(hw);

		/* Configure activity LED after PHY reset */
		led_ctrl = er32(LEDCTL);
		led_ctrl &= IGP_ACTIVITY_LED_MASK;
		led_ctrl |= (IGP_ACTIVITY_LED_ENABLE | IGP_LED3_MODE);
		ew32(LEDCTL, led_ctrl);
	}

	/* Clear interrupt mask to stop board from generating interrupts */
	e_dbg("Masking off all interrupts\n");
	ew32(IMC, 0xffffffff);

	/* Clear any pending interrupt events. */
	icr = er32(ICR);

	/* If MWI was previously enabled, reenable it. */
	if (hw->mac_type == e1000_82542_rev2_0) {
		if (hw->pci_cmd_word & PCI_COMMAND_INVALIDATE)
			e1000_pci_set_mwi(hw);
	}

	return E1000_SUCCESS;
}

/**
 * e1000_init_hw: Performs basic configuration of the adapter.
 * @hw: Struct containing variables accessed by shared code
 *
 * Assumes that the controller has previously been reset and is in a
 * post-reset uninitialized state. Initializes the receive address registers,
 * multicast table, and VLAN filter table. Calls routines to setup link
 * configuration and flow control settings. Clears all on-chip counters. Leaves
 * the transmit and receive units disabled and uninitialized.
 */
s32 e1000_init_hw(struct e1000_hw *hw)
{
	u32 ctrl;
	u32 i;
	s32 ret_val;
	u32 mta_size;
	u32 ctrl_ext;

	e_dbg("e1000_init_hw");

	/* Initialize Identification LED */
	ret_val = e1000_id_led_init(hw);
	if (ret_val) {
		e_dbg("Error Initializing Identification LED\n");
		return ret_val;
	}

	/* Set the media type and TBI compatibility */
	e1000_set_media_type(hw);

	/* Disabling VLAN filtering. */
	e_dbg("Initializing the IEEE VLAN\n");
	if (hw->mac_type < e1000_82545_rev_3)
		ew32(VET, 0);
	e1000_clear_vfta(hw);

	/* For 82542 (rev 2.0), disable MWI and put the receiver into reset */
	if (hw->mac_type == e1000_82542_rev2_0) {
		e_dbg("Disabling MWI on 82542 rev 2.0\n");
		e1000_pci_clear_mwi(hw);
		ew32(RCTL, E1000_RCTL_RST);
		E1000_WRITE_FLUSH();
		msleep(5);
	}

	/* Setup the receive address. This involves initializing all of the Receive
	 * Address Registers (RARs 0 - 15).
	 */
	e1000_init_rx_addrs(hw);

	/* For 82542 (rev 2.0), take the receiver out of reset and enable MWI */
	if (hw->mac_type == e1000_82542_rev2_0) {
		ew32(RCTL, 0);
		E1000_WRITE_FLUSH();
		msleep(1);
		if (hw->pci_cmd_word & PCI_COMMAND_INVALIDATE)
			e1000_pci_set_mwi(hw);
	}

	/* Zero out the Multicast HASH table */
	e_dbg("Zeroing the MTA\n");
	mta_size = E1000_MC_TBL_SIZE;
	for (i = 0; i < mta_size; i++) {
		E1000_WRITE_REG_ARRAY(hw, MTA, i, 0);
		/* use write flush to prevent Memory Write Block (MWB) from
		 * occurring when accessing our register space */
		E1000_WRITE_FLUSH();
	}

	/* Set the PCI priority bit correctly in the CTRL register.  This
	 * determines if the adapter gives priority to receives, or if it
	 * gives equal priority to transmits and receives.  Valid only on
	 * 82542 and 82543 silicon.
	 */
	if (hw->dma_fairness && hw->mac_type <= e1000_82543) {
		ctrl = er32(CTRL);
		ew32(CTRL, ctrl | E1000_CTRL_PRIOR);
	}

	switch (hw->mac_type) {
	case e1000_82545_rev_3:
	case e1000_82546_rev_3:
		break;
	default:
		/* Workaround for PCI-X problem when BIOS sets MMRBC incorrectly. */
		if (hw->bus_type == e1000_bus_type_pcix
		    && e1000_pcix_get_mmrbc(hw) > 2048)
			e1000_pcix_set_mmrbc(hw, 2048);
		break;
	}

	/* Call a subroutine to configure the link and setup flow control. */
	ret_val = e1000_setup_link(hw);

	/* Set the transmit descriptor write-back policy */
	if (hw->mac_type > e1000_82544) {
		ctrl = er32(TXDCTL);
		ctrl =
		    (ctrl & ~E1000_TXDCTL_WTHRESH) |
		    E1000_TXDCTL_FULL_TX_DESC_WB;
		ew32(TXDCTL, ctrl);
	}

	/* Clear all of the statistics registers (clear on read).  It is
	 * important that we do this after we have tried to establish link
	 * because the symbol error count will increment wildly if there
	 * is no link.
	 */
	e1000_clear_hw_cntrs(hw);

	if (hw->device_id == E1000_DEV_ID_82546GB_QUAD_COPPER ||
	    hw->device_id == E1000_DEV_ID_82546GB_QUAD_COPPER_KSP3) {
		ctrl_ext = er32(CTRL_EXT);
		/* Relaxed ordering must be disabled to avoid a parity
		 * error crash in a PCI slot. */
		ctrl_ext |= E1000_CTRL_EXT_RO_DIS;
		ew32(CTRL_EXT, ctrl_ext);
	}

	return ret_val;
}

/**
 * e1000_adjust_serdes_amplitude - Adjust SERDES output amplitude based on EEPROM setting.
 * @hw: Struct containing variables accessed by shared code.
 */
static s32 e1000_adjust_serdes_amplitude(struct e1000_hw *hw)
{
	u16 eeprom_data;
	s32 ret_val;

	e_dbg("e1000_adjust_serdes_amplitude");

	if (hw->media_type != e1000_media_type_internal_serdes)
		return E1000_SUCCESS;

	switch (hw->mac_type) {
	case e1000_82545_rev_3:
	case e1000_82546_rev_3:
		break;
	default:
		return E1000_SUCCESS;
	}

	ret_val = e1000_read_eeprom(hw, EEPROM_SERDES_AMPLITUDE, 1,
	                            &eeprom_data);
	if (ret_val) {
		return ret_val;
	}

	if (eeprom_data != EEPROM_RESERVED_WORD) {
		/* Adjust SERDES output amplitude only. */
		eeprom_data &= EEPROM_SERDES_AMPLITUDE_MASK;
		ret_val =
		    e1000_write_phy_reg(hw, M88E1000_PHY_EXT_CTRL, eeprom_data);
		if (ret_val)
			return ret_val;
	}

	return E1000_SUCCESS;
}

/**
 * e1000_setup_link - Configures flow control and link settings.
 * @hw: Struct containing variables accessed by shared code
 *
 * Determines which flow control settings to use. Calls the appropriate media-
 * specific link configuration function. Configures the flow control settings.
 * Assuming the adapter has a valid link partner, a valid link should be
 * established. Assumes the hardware has previously been reset and the
 * transmitter and receiver are not enabled.
 */
s32 e1000_setup_link(struct e1000_hw *hw)
{
	u32 ctrl_ext;
	s32 ret_val;
	u16 eeprom_data;

	e_dbg("e1000_setup_link");

	/* Read and store word 0x0F of the EEPROM. This word contains bits
	 * that determine the hardware's default PAUSE (flow control) mode,
	 * a bit that determines whether the HW defaults to enabling or
	 * disabling auto-negotiation, and the direction of the
	 * SW defined pins. If there is no SW over-ride of the flow
	 * control setting, then the variable hw->fc will
	 * be initialized based on a value in the EEPROM.
	 */
	if (hw->fc == E1000_FC_DEFAULT) {
		ret_val = e1000_read_eeprom(hw, EEPROM_INIT_CONTROL2_REG,
					    1, &eeprom_data);
		if (ret_val) {
			e_dbg("EEPROM Read Error\n");
			return -E1000_ERR_EEPROM;
		}
		if ((eeprom_data & EEPROM_WORD0F_PAUSE_MASK) == 0)
			hw->fc = E1000_FC_NONE;
		else if ((eeprom_data & EEPROM_WORD0F_PAUSE_MASK) ==
			 EEPROM_WORD0F_ASM_DIR)
			hw->fc = E1000_FC_TX_PAUSE;
		else
			hw->fc = E1000_FC_FULL;
	}

	/* We want to save off the original Flow Control configuration just
	 * in case we get disconnected and then reconnected into a different
	 * hub or switch with different Flow Control capabilities.
	 */
	if (hw->mac_type == e1000_82542_rev2_0)
		hw->fc &= (~E1000_FC_TX_PAUSE);

	if ((hw->mac_type < e1000_82543) && (hw->report_tx_early == 1))
		hw->fc &= (~E1000_FC_RX_PAUSE);

	hw->original_fc = hw->fc;

	e_dbg("After fix-ups FlowControl is now = %x\n", hw->fc);

	/* Take the 4 bits from EEPROM word 0x0F that determine the initial
	 * polarity value for the SW controlled pins, and setup the
	 * Extended Device Control reg with that info.
	 * This is needed because one of the SW controlled pins is used for
	 * signal detection.  So this should be done before e1000_setup_pcs_link()
	 * or e1000_phy_setup() is called.
	 */
	if (hw->mac_type == e1000_82543) {
		ret_val = e1000_read_eeprom(hw, EEPROM_INIT_CONTROL2_REG,
					    1, &eeprom_data);
		if (ret_val) {
			e_dbg("EEPROM Read Error\n");
			return -E1000_ERR_EEPROM;
		}
		ctrl_ext = ((eeprom_data & EEPROM_WORD0F_SWPDIO_EXT) <<
			    SWDPIO__EXT_SHIFT);
		ew32(CTRL_EXT, ctrl_ext);
	}

	/* Call the necessary subroutine to configure the link. */
	ret_val = (hw->media_type == e1000_media_type_copper) ?
	    e1000_setup_copper_link(hw) : e1000_setup_fiber_serdes_link(hw);

	/* Initialize the flow control address, type, and PAUSE timer
	 * registers to their default values.  This is done even if flow
	 * control is disabled, because it does not hurt anything to
	 * initialize these registers.
	 */
	e_dbg("Initializing the Flow Control address, type and timer regs\n");

	ew32(FCT, FLOW_CONTROL_TYPE);
	ew32(FCAH, FLOW_CONTROL_ADDRESS_HIGH);
	ew32(FCAL, FLOW_CONTROL_ADDRESS_LOW);

	ew32(FCTTV, hw->fc_pause_time);

	/* Set the flow control receive threshold registers.  Normally,
	 * these registers will be set to a default threshold that may be
	 * adjusted later by the driver's runtime code.  However, if the
	 * ability to transmit pause frames in not enabled, then these
	 * registers will be set to 0.
	 */
	if (!(hw->fc & E1000_FC_TX_PAUSE)) {
		ew32(FCRTL, 0);
		ew32(FCRTH, 0);
	} else {
		/* We need to set up the Receive Threshold high and low water marks
		 * as well as (optionally) enabling the transmission of XON frames.
		 */
		if (hw->fc_send_xon) {
			ew32(FCRTL, (hw->fc_low_water | E1000_FCRTL_XONE));
			ew32(FCRTH, hw->fc_high_water);
		} else {
			ew32(FCRTL, hw->fc_low_water);
			ew32(FCRTH, hw->fc_high_water);
		}
	}
	return ret_val;
}

/**
 * e1000_setup_fiber_serdes_link - prepare fiber or serdes link
 * @hw: Struct containing variables accessed by shared code
 *
 * Manipulates Physical Coding Sublayer functions in order to configure
 * link. Assumes the hardware has been previously reset and the transmitter
 * and receiver are not enabled.
 */
static s32 e1000_setup_fiber_serdes_link(struct e1000_hw *hw)
{
	u32 ctrl;
	u32 status;
	u32 txcw = 0;
	u32 i;
	u32 signal = 0;
	s32 ret_val;

	e_dbg("e1000_setup_fiber_serdes_link");

	/* On adapters with a MAC newer than 82544, SWDP 1 will be
	 * set when the optics detect a signal. On older adapters, it will be
	 * cleared when there is a signal.  This applies to fiber media only.
	 * If we're on serdes media, adjust the output amplitude to value
	 * set in the EEPROM.
	 */
	ctrl = er32(CTRL);
	if (hw->media_type == e1000_media_type_fiber)
		signal = (hw->mac_type > e1000_82544) ? E1000_CTRL_SWDPIN1 : 0;

	ret_val = e1000_adjust_serdes_amplitude(hw);
	if (ret_val)
		return ret_val;

	/* Take the link out of reset */
	ctrl &= ~(E1000_CTRL_LRST);

	/* Adjust VCO speed to improve BER performance */
	ret_val = e1000_set_vco_speed(hw);
	if (ret_val)
		return ret_val;

	e1000_config_collision_dist(hw);

	/* Check for a software override of the flow control settings, and setup
	 * the device accordingly.  If auto-negotiation is enabled, then software
	 * will have to set the "PAUSE" bits to the correct value in the Tranmsit
	 * Config Word Register (TXCW) and re-start auto-negotiation.  However, if
	 * auto-negotiation is disabled, then software will have to manually
	 * configure the two flow control enable bits in the CTRL register.
	 *
	 * The possible values of the "fc" parameter are:
	 *      0:  Flow control is completely disabled
	 *      1:  Rx flow control is enabled (we can receive pause frames, but
	 *          not send pause frames).
	 *      2:  Tx flow control is enabled (we can send pause frames but we do
	 *          not support receiving pause frames).
	 *      3:  Both Rx and TX flow control (symmetric) are enabled.
	 */
	switch (hw->fc) {
	case E1000_FC_NONE:
		/* Flow control is completely disabled by a software over-ride. */
		txcw = (E1000_TXCW_ANE | E1000_TXCW_FD);
		break;
	case E1000_FC_RX_PAUSE:
		/* RX Flow control is enabled and TX Flow control is disabled by a
		 * software over-ride. Since there really isn't a way to advertise
		 * that we are capable of RX Pause ONLY, we will advertise that we
		 * support both symmetric and asymmetric RX PAUSE. Later, we will
		 *  disable the adapter's ability to send PAUSE frames.
		 */
		txcw = (E1000_TXCW_ANE | E1000_TXCW_FD | E1000_TXCW_PAUSE_MASK);
		break;
	case E1000_FC_TX_PAUSE:
		/* TX Flow control is enabled, and RX Flow control is disabled, by a
		 * software over-ride.
		 */
		txcw = (E1000_TXCW_ANE | E1000_TXCW_FD | E1000_TXCW_ASM_DIR);
		break;
	case E1000_FC_FULL:
		/* Flow control (both RX and TX) is enabled by a software over-ride. */
		txcw = (E1000_TXCW_ANE | E1000_TXCW_FD | E1000_TXCW_PAUSE_MASK);
		break;
	default:
		e_dbg("Flow control param set incorrectly\n");
		return -E1000_ERR_CONFIG;
		break;
	}

	/* Since auto-negotiation is enabled, take the link out of reset (the link
	 * will be in reset, because we previously reset the chip). This will
	 * restart auto-negotiation.  If auto-negotiation is successful then the
	 * link-up status bit will be set and the flow control enable bits (RFCE
	 * and TFCE) will be set according to their negotiated value.
	 */
	e_dbg("Auto-negotiation enabled\n");

	ew32(TXCW, txcw);
	ew32(CTRL, ctrl);
	E1000_WRITE_FLUSH();

	hw->txcw = txcw;
	msleep(1);

	/* If we have a signal (the cable is plugged in) then poll for a "Link-Up"
	 * indication in the Device Status Register.  Time-out if a link isn't
	 * seen in 500 milliseconds seconds (Auto-negotiation should complete in
	 * less than 500 milliseconds even if the other end is doing it in SW).
	 * For internal serdes, we just assume a signal is present, then poll.
	 */
	if (hw->media_type == e1000_media_type_internal_serdes ||
	    (er32(CTRL) & E1000_CTRL_SWDPIN1) == signal) {
		e_dbg("Looking for Link\n");
		for (i = 0; i < (LINK_UP_TIMEOUT / 10); i++) {
			msleep(10);
			status = er32(STATUS);
			if (status & E1000_STATUS_LU)
				break;
		}
		if (i == (LINK_UP_TIMEOUT / 10)) {
			e_dbg("Never got a valid link from auto-neg!!!\n");
			hw->autoneg_failed = 1;
			/* AutoNeg failed to achieve a link, so we'll call
			 * e1000_check_for_link. This routine will force the link up if
			 * we detect a signal. This will allow us to communicate with
			 * non-autonegotiating link partners.
			 */
			ret_val = e1000_check_for_link(hw);
			if (ret_val) {
				e_dbg("Error while checking for link\n");
				return ret_val;
			}
			hw->autoneg_failed = 0;
		} else {
			hw->autoneg_failed = 0;
			e_dbg("Valid Link Found\n");
		}
	} else {
		e_dbg("No Signal Detected\n");
	}
	return E1000_SUCCESS;
}

/**
 * e1000_copper_link_rtl_setup - Copper link setup for e1000_phy_rtl series.
 * @hw: Struct containing variables accessed by shared code
 *
 * Commits changes to PHY configuration by calling e1000_phy_reset().
 */
static s32 e1000_copper_link_rtl_setup(struct e1000_hw *hw)
{
	s32 ret_val;

	/* SW reset the PHY so all changes take effect */
	ret_val = e1000_phy_reset(hw);
	if (ret_val) {
		e_dbg("Error Resetting the PHY\n");
		return ret_val;
	}

	return E1000_SUCCESS;
}

static s32 gbe_dhg_phy_setup(struct e1000_hw *hw)
{
	s32 ret_val;
	u32 ctrl_aux;

	switch (hw->phy_type) {
	case e1000_phy_8211:
		ret_val = e1000_copper_link_rtl_setup(hw);
		if (ret_val) {
			e_dbg("e1000_copper_link_rtl_setup failed!\n");
			return ret_val;
		}
		break;
	case e1000_phy_8201:
		/* Set RMII mode */
		ctrl_aux = er32(CTL_AUX);
		ctrl_aux |= E1000_CTL_AUX_RMII;
		ew32(CTL_AUX, ctrl_aux);
		E1000_WRITE_FLUSH();

		/* Disable the J/K bits required for receive */
		ctrl_aux = er32(CTL_AUX);
		ctrl_aux |= 0x4;
		ctrl_aux &= ~0x2;
		ew32(CTL_AUX, ctrl_aux);
		E1000_WRITE_FLUSH();
		ret_val = e1000_copper_link_rtl_setup(hw);

		if (ret_val) {
			e_dbg("e1000_copper_link_rtl_setup failed!\n");
			return ret_val;
		}
		break;
	default:
		e_dbg("Error Resetting the PHY\n");
		return E1000_ERR_PHY_TYPE;
	}

	return E1000_SUCCESS;
}

/**
 * e1000_copper_link_preconfig - early configuration for copper
 * @hw: Struct containing variables accessed by shared code
 *
 * Make sure we have a valid PHY and change PHY mode before link setup.
 */
static s32 e1000_copper_link_preconfig(struct e1000_hw *hw)
{
	u32 ctrl;
	s32 ret_val;
	u16 phy_data;

	e_dbg("e1000_copper_link_preconfig");

	ctrl = er32(CTRL);
	/* With 82543, we need to force speed and duplex on the MAC equal to what
	 * the PHY speed and duplex configuration is. In addition, we need to
	 * perform a hardware reset on the PHY to take it out of reset.
	 */
	if (hw->mac_type > e1000_82543) {
		ctrl |= E1000_CTRL_SLU;
		ctrl &= ~(E1000_CTRL_FRCSPD | E1000_CTRL_FRCDPX);
		ew32(CTRL, ctrl);
	} else {
		ctrl |=
		    (E1000_CTRL_FRCSPD | E1000_CTRL_FRCDPX | E1000_CTRL_SLU);
		ew32(CTRL, ctrl);
		ret_val = e1000_phy_hw_reset(hw);
		if (ret_val)
			return ret_val;
	}

	/* Make sure we have a valid PHY */
	ret_val = e1000_detect_gig_phy(hw);
	if (ret_val) {
		e_dbg("Error, did not detect valid phy.\n");
		return ret_val;
	}
	e_dbg("Phy ID = %x\n", hw->phy_id);

	/* Set PHY to class A mode (if necessary) */
	ret_val = e1000_set_phy_mode(hw);
	if (ret_val)
		return ret_val;

	if ((hw->mac_type == e1000_82545_rev_3) ||
	    (hw->mac_type == e1000_82546_rev_3)) {
		ret_val =
		    e1000_read_phy_reg(hw, M88E1000_PHY_SPEC_CTRL, &phy_data);
		phy_data |= 0x00000008;
		ret_val =
		    e1000_write_phy_reg(hw, M88E1000_PHY_SPEC_CTRL, phy_data);
	}

	if (hw->mac_type <= e1000_82543 ||
	    hw->mac_type == e1000_82541 || hw->mac_type == e1000_82547 ||
	    hw->mac_type == e1000_82541_rev_2
	    || hw->mac_type == e1000_82547_rev_2)
		hw->phy_reset_disable = false;

	return E1000_SUCCESS;
}

/**
 * e1000_copper_link_igp_setup - Copper link setup for e1000_phy_igp series.
 * @hw: Struct containing variables accessed by shared code
 */
static s32 e1000_copper_link_igp_setup(struct e1000_hw *hw)
{
	u32 led_ctrl;
	s32 ret_val;
	u16 phy_data;

	e_dbg("e1000_copper_link_igp_setup");

	if (hw->phy_reset_disable)
		return E1000_SUCCESS;

	ret_val = e1000_phy_reset(hw);
	if (ret_val) {
		e_dbg("Error Resetting the PHY\n");
		return ret_val;
	}

	/* Wait 15ms for MAC to configure PHY from eeprom settings */
	msleep(15);
	/* Configure activity LED after PHY reset */
	led_ctrl = er32(LEDCTL);
	led_ctrl &= IGP_ACTIVITY_LED_MASK;
	led_ctrl |= (IGP_ACTIVITY_LED_ENABLE | IGP_LED3_MODE);
	ew32(LEDCTL, led_ctrl);

	/* The NVM settings will configure LPLU in D3 for IGP2 and IGP3 PHYs */
	if (hw->phy_type == e1000_phy_igp) {
		/* disable lplu d3 during driver init */
		ret_val = e1000_set_d3_lplu_state(hw, false);
		if (ret_val) {
			e_dbg("Error Disabling LPLU D3\n");
			return ret_val;
		}
	}

	/* Configure mdi-mdix settings */
	ret_val = e1000_read_phy_reg(hw, IGP01E1000_PHY_PORT_CTRL, &phy_data);
	if (ret_val)
		return ret_val;

	if ((hw->mac_type == e1000_82541) || (hw->mac_type == e1000_82547)) {
		hw->dsp_config_state = e1000_dsp_config_disabled;
		/* Force MDI for earlier revs of the IGP PHY */
		phy_data &=
		    ~(IGP01E1000_PSCR_AUTO_MDIX |
		      IGP01E1000_PSCR_FORCE_MDI_MDIX);
		hw->mdix = 1;

	} else {
		hw->dsp_config_state = e1000_dsp_config_enabled;
		phy_data &= ~IGP01E1000_PSCR_AUTO_MDIX;

		switch (hw->mdix) {
		case 1:
			phy_data &= ~IGP01E1000_PSCR_FORCE_MDI_MDIX;
			break;
		case 2:
			phy_data |= IGP01E1000_PSCR_FORCE_MDI_MDIX;
			break;
		case 0:
		default:
			phy_data |= IGP01E1000_PSCR_AUTO_MDIX;
			break;
		}
	}
	ret_val = e1000_write_phy_reg(hw, IGP01E1000_PHY_PORT_CTRL, phy_data);
	if (ret_val)
		return ret_val;

	/* set auto-master slave resolution settings */
	if (hw->autoneg) {
		e1000_ms_type phy_ms_setting = hw->master_slave;

		if (hw->ffe_config_state == e1000_ffe_config_active)
			hw->ffe_config_state = e1000_ffe_config_enabled;

		if (hw->dsp_config_state == e1000_dsp_config_activated)
			hw->dsp_config_state = e1000_dsp_config_enabled;

		/* when autonegotiation advertisement is only 1000Mbps then we
		 * should disable SmartSpeed and enable Auto MasterSlave
		 * resolution as hardware default. */
		if (hw->autoneg_advertised == ADVERTISE_1000_FULL) {
			/* Disable SmartSpeed */
			ret_val =
			    e1000_read_phy_reg(hw, IGP01E1000_PHY_PORT_CONFIG,
					       &phy_data);
			if (ret_val)
				return ret_val;
			phy_data &= ~IGP01E1000_PSCFR_SMART_SPEED;
			ret_val =
			    e1000_write_phy_reg(hw, IGP01E1000_PHY_PORT_CONFIG,
						phy_data);
			if (ret_val)
				return ret_val;
			/* Set auto Master/Slave resolution process */
			ret_val =
			    e1000_read_phy_reg(hw, PHY_1000T_CTRL, &phy_data);
			if (ret_val)
				return ret_val;
			phy_data &= ~CR_1000T_MS_ENABLE;
			ret_val =
			    e1000_write_phy_reg(hw, PHY_1000T_CTRL, phy_data);
			if (ret_val)
				return ret_val;
		}

		ret_val = e1000_read_phy_reg(hw, PHY_1000T_CTRL, &phy_data);
		if (ret_val)
			return ret_val;

		/* load defaults for future use */
		hw->original_master_slave = (phy_data & CR_1000T_MS_ENABLE) ?
		    ((phy_data & CR_1000T_MS_VALUE) ?
		     e1000_ms_force_master :
		     e1000_ms_force_slave) : e1000_ms_auto;

		switch (phy_ms_setting) {
		case e1000_ms_force_master:
			phy_data |= (CR_1000T_MS_ENABLE | CR_1000T_MS_VALUE);
			break;
		case e1000_ms_force_slave:
			phy_data |= CR_1000T_MS_ENABLE;
			phy_data &= ~(CR_1000T_MS_VALUE);
			break;
		case e1000_ms_auto:
			phy_data &= ~CR_1000T_MS_ENABLE;
		default:
			break;
		}
		ret_val = e1000_write_phy_reg(hw, PHY_1000T_CTRL, phy_data);
		if (ret_val)
			return ret_val;
	}

	return E1000_SUCCESS;
}

/**
 * e1000_copper_link_mgp_setup - Copper link setup for e1000_phy_m88 series.
 * @hw: Struct containing variables accessed by shared code
 */
static s32 e1000_copper_link_mgp_setup(struct e1000_hw *hw)
{
	s32 ret_val;
	u16 phy_data;

	e_dbg("e1000_copper_link_mgp_setup");

	if (hw->phy_reset_disable)
		return E1000_SUCCESS;

	/* Enable CRS on TX. This must be set for half-duplex operation. */
	ret_val = e1000_read_phy_reg(hw, M88E1000_PHY_SPEC_CTRL, &phy_data);
	if (ret_val)
		return ret_val;

	phy_data |= M88E1000_PSCR_ASSERT_CRS_ON_TX;

	/* Options:
	 *   MDI/MDI-X = 0 (default)
	 *   0 - Auto for all speeds
	 *   1 - MDI mode
	 *   2 - MDI-X mode
	 *   3 - Auto for 1000Base-T only (MDI-X for 10/100Base-T modes)
	 */
	phy_data &= ~M88E1000_PSCR_AUTO_X_MODE;

	switch (hw->mdix) {
	case 1:
		phy_data |= M88E1000_PSCR_MDI_MANUAL_MODE;
		break;
	case 2:
		phy_data |= M88E1000_PSCR_MDIX_MANUAL_MODE;
		break;
	case 3:
		phy_data |= M88E1000_PSCR_AUTO_X_1000T;
		break;
	case 0:
	default:
		phy_data |= M88E1000_PSCR_AUTO_X_MODE;
		break;
	}

	/* Options:
	 *   disable_polarity_correction = 0 (default)
	 *       Automatic Correction for Reversed Cable Polarity
	 *   0 - Disabled
	 *   1 - Enabled
	 */
	phy_data &= ~M88E1000_PSCR_POLARITY_REVERSAL;
	if (hw->disable_polarity_correction == 1)
		phy_data |= M88E1000_PSCR_POLARITY_REVERSAL;
	ret_val = e1000_write_phy_reg(hw, M88E1000_PHY_SPEC_CTRL, phy_data);
	if (ret_val)
		return ret_val;

	if (hw->phy_revision < M88E1011_I_REV_4) {
		/* Force TX_CLK in the Extended PHY Specific Control Register
		 * to 25MHz clock.
		 */
		ret_val =
		    e1000_read_phy_reg(hw, M88E1000_EXT_PHY_SPEC_CTRL,
				       &phy_data);
		if (ret_val)
			return ret_val;

		phy_data |= M88E1000_EPSCR_TX_CLK_25;

		if ((hw->phy_revision == E1000_REVISION_2) &&
		    (hw->phy_id == M88E1111_I_PHY_ID)) {
			/* Vidalia Phy, set the downshift counter to 5x */
			phy_data &= ~(M88EC018_EPSCR_DOWNSHIFT_COUNTER_MASK);
			phy_data |= M88EC018_EPSCR_DOWNSHIFT_COUNTER_5X;
			ret_val = e1000_write_phy_reg(hw,
						      M88E1000_EXT_PHY_SPEC_CTRL,
						      phy_data);
			if (ret_val)
				return ret_val;
		} else {
			/* Configure Master and Slave downshift values */
			phy_data &= ~(M88E1000_EPSCR_MASTER_DOWNSHIFT_MASK |
				      M88E1000_EPSCR_SLAVE_DOWNSHIFT_MASK);
			phy_data |= (M88E1000_EPSCR_MASTER_DOWNSHIFT_1X |
				     M88E1000_EPSCR_SLAVE_DOWNSHIFT_1X);
			ret_val = e1000_write_phy_reg(hw,
						      M88E1000_EXT_PHY_SPEC_CTRL,
						      phy_data);
			if (ret_val)
				return ret_val;
		}
	}

	/* SW Reset the PHY so all changes take effect */
	ret_val = e1000_phy_reset(hw);
	if (ret_val) {
		e_dbg("Error Resetting the PHY\n");
		return ret_val;
	}

	return E1000_SUCCESS;
}

/**
 * e1000_copper_link_autoneg - setup auto-neg
 * @hw: Struct containing variables accessed by shared code
 *
 * Setup auto-negotiation and flow control advertisements,
 * and then perform auto-negotiation.
 */
static s32 e1000_copper_link_autoneg(struct e1000_hw *hw)
{
	s32 ret_val;
	u16 phy_data;

	e_dbg("e1000_copper_link_autoneg");

	/* Perform some bounds checking on the hw->autoneg_advertised
	 * parameter.  If this variable is zero, then set it to the default.
	 */
	hw->autoneg_advertised &= AUTONEG_ADVERTISE_SPEED_DEFAULT;

	/* If autoneg_advertised is zero, we assume it was not defaulted
	 * by the calling code so we set to advertise full capability.
	 */
	if (hw->autoneg_advertised == 0)
		hw->autoneg_advertised = AUTONEG_ADVERTISE_SPEED_DEFAULT;

	/* IFE/RTL8201N PHY only supports 10/100 */
	if (hw->phy_type == e1000_phy_8201)
		hw->autoneg_advertised &= AUTONEG_ADVERTISE_10_100_ALL;

	e_dbg("Reconfiguring auto-neg advertisement params\n");
	ret_val = e1000_phy_setup_autoneg(hw);
	if (ret_val) {
		e_dbg("Error Setting up Auto-Negotiation\n");
		return ret_val;
	}
	e_dbg("Restarting Auto-Neg\n");

	/* Restart auto-negotiation by setting the Auto Neg Enable bit and
	 * the Auto Neg Restart bit in the PHY control register.
	 */
	ret_val = e1000_read_phy_reg(hw, PHY_CTRL, &phy_data);
	if (ret_val)
		return ret_val;

	phy_data |= (MII_CR_AUTO_NEG_EN | MII_CR_RESTART_AUTO_NEG);
	ret_val = e1000_write_phy_reg(hw, PHY_CTRL, phy_data);
	if (ret_val)
		return ret_val;

	/* Does the user want to wait for Auto-Neg to complete here, or
	 * check at a later time (for example, callback routine).
	 */
	if (hw->wait_autoneg_complete) {
		ret_val = e1000_wait_autoneg(hw);
		if (ret_val) {
			e_dbg
			    ("Error while waiting for autoneg to complete\n");
			return ret_val;
		}
	}

	hw->get_link_status = true;

	return E1000_SUCCESS;
}

/**
 * e1000_copper_link_postconfig - post link setup
 * @hw: Struct containing variables accessed by shared code
 *
 * Config the MAC and the PHY after link is up.
 *   1) Set up the MAC to the current PHY speed/duplex
 *      if we are on 82543.  If we
 *      are on newer silicon, we only need to configure
 *      collision distance in the Transmit Control Register.
 *   2) Set up flow control on the MAC to that established with
 *      the link partner.
 *   3) Config DSP to improve Gigabit link quality for some PHY revisions.
 */
static s32 e1000_copper_link_postconfig(struct e1000_hw *hw)
{
	s32 ret_val;
	e_dbg("e1000_copper_link_postconfig");

	if ((hw->mac_type >= e1000_82544) && (hw->mac_type != e1000_ce4100)) {
		e1000_config_collision_dist(hw);
	} else {
		ret_val = e1000_config_mac_to_phy(hw);
		if (ret_val) {
			e_dbg("Error configuring MAC to PHY settings\n");
			return ret_val;
		}
	}
	ret_val = e1000_config_fc_after_link_up(hw);
	if (ret_val) {
		e_dbg("Error Configuring Flow Control\n");
		return ret_val;
	}

	/* Config DSP to improve Giga link quality */
	if (hw->phy_type == e1000_phy_igp) {
		ret_val = e1000_config_dsp_after_link_change(hw, true);
		if (ret_val) {
			e_dbg("Error Configuring DSP after link up\n");
			return ret_val;
		}
	}

	return E1000_SUCCESS;
}

/**
 * e1000_setup_copper_link - phy/speed/duplex setting
 * @hw: Struct containing variables accessed by shared code
 *
 * Detects which PHY is present and sets up the speed and duplex
 */
static s32 e1000_setup_copper_link(struct e1000_hw *hw)
{
	s32 ret_val;
	u16 i;
	u16 phy_data;

	e_dbg("e1000_setup_copper_link");

	/* Check if it is a valid PHY and set PHY mode if necessary. */
	ret_val = e1000_copper_link_preconfig(hw);
	if (ret_val)
		return ret_val;

	if (hw->phy_type == e1000_phy_igp) {
		ret_val = e1000_copper_link_igp_setup(hw);
		if (ret_val)
			return ret_val;
	} else if (hw->phy_type == e1000_phy_m88) {
		ret_val = e1000_copper_link_mgp_setup(hw);
		if (ret_val)
			return ret_val;
	} else {
		ret_val = gbe_dhg_phy_setup(hw);
		if (ret_val) {
			e_dbg("gbe_dhg_phy_setup failed!\n");
			return ret_val;
		}
	}

	if (hw->autoneg) {
		/* Setup autoneg and flow control advertisement
		 * and perform autonegotiation */
		ret_val = e1000_copper_link_autoneg(hw);
		if (ret_val)
			return ret_val;
	} else {
		/* PHY will be set to 10H, 10F, 100H,or 100F
		 * depending on value from forced_speed_duplex. */
		e_dbg("Forcing speed and duplex\n");
		ret_val = e1000_phy_force_speed_duplex(hw);
		if (ret_val) {
			e_dbg("Error Forcing Speed and Duplex\n");
			return ret_val;
		}
	}

	/* Check link status. Wait up to 100 microseconds for link to become
	 * valid.
	 */
	for (i = 0; i < 10; i++) {
		ret_val = e1000_read_phy_reg(hw, PHY_STATUS, &phy_data);
		if (ret_val)
			return ret_val;
		ret_val = e1000_read_phy_reg(hw, PHY_STATUS, &phy_data);
		if (ret_val)
			return ret_val;

		if (phy_data & MII_SR_LINK_STATUS) {
			/* Config the MAC and PHY after link is up */
			ret_val = e1000_copper_link_postconfig(hw);
			if (ret_val)
				return ret_val;

			e_dbg("Valid link established!!!\n");
			return E1000_SUCCESS;
		}
		udelay(10);
	}

	e_dbg("Unable to establish link!!!\n");
	return E1000_SUCCESS;
}

/**
 * e1000_phy_setup_autoneg - phy settings
 * @hw: Struct containing variables accessed by shared code
 *
 * Configures PHY autoneg and flow control advertisement settings
 */
s32 e1000_phy_setup_autoneg(struct e1000_hw *hw)
{
	s32 ret_val;
	u16 mii_autoneg_adv_reg;
	u16 mii_1000t_ctrl_reg;

	e_dbg("e1000_phy_setup_autoneg");

	/* Read the MII Auto-Neg Advertisement Register (Address 4). */
	ret_val = e1000_read_phy_reg(hw, PHY_AUTONEG_ADV, &mii_autoneg_adv_reg);
	if (ret_val)
		return ret_val;

	/* Read the MII 1000Base-T Control Register (Address 9). */
	ret_val = e1000_read_phy_reg(hw, PHY_1000T_CTRL, &mii_1000t_ctrl_reg);
	if (ret_val)
		return ret_val;
	else if (hw->phy_type == e1000_phy_8201)
		mii_1000t_ctrl_reg &= ~REG9_SPEED_MASK;

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
	mii_autoneg_adv_reg &= ~REG4_SPEED_MASK;
	mii_1000t_ctrl_reg &= ~REG9_SPEED_MASK;

	e_dbg("autoneg_advertised %x\n", hw->autoneg_advertised);

	/* Do we want to advertise 10 Mb Half Duplex? */
	if (hw->autoneg_advertised & ADVERTISE_10_HALF) {
		e_dbg("Advertise 10mb Half duplex\n");
		mii_autoneg_adv_reg |= NWAY_AR_10T_HD_CAPS;
	}

	/* Do we want to advertise 10 Mb Full Duplex? */
	if (hw->autoneg_advertised & ADVERTISE_10_FULL) {
		e_dbg("Advertise 10mb Full duplex\n");
		mii_autoneg_adv_reg |= NWAY_AR_10T_FD_CAPS;
	}

	/* Do we want to advertise 100 Mb Half Duplex? */
	if (hw->autoneg_advertised & ADVERTISE_100_HALF) {
		e_dbg("Advertise 100mb Half duplex\n");
		mii_autoneg_adv_reg |= NWAY_AR_100TX_HD_CAPS;
	}

	/* Do we want to advertise 100 Mb Full Duplex? */
	if (hw->autoneg_advertised & ADVERTISE_100_FULL) {
		e_dbg("Advertise 100mb Full duplex\n");
		mii_autoneg_adv_reg |= NWAY_AR_100TX_FD_CAPS;
	}

	/* We do not allow the Phy to advertise 1000 Mb Half Duplex */
	if (hw->autoneg_advertised & ADVERTISE_1000_HALF) {
		e_dbg
		    ("Advertise 1000mb Half duplex requested, request denied!\n");
	}

	/* Do we want to advertise 1000 Mb Full Duplex? */
	if (hw->autoneg_advertised & ADVERTISE_1000_FULL) {
		e_dbg("Advertise 1000mb Full duplex\n");
		mii_1000t_ctrl_reg |= CR_1000T_FD_CAPS;
	}

	/* Check for a software override of the flow control settings, and
	 * setup the PHY advertisement registers accordingly.  If
	 * auto-negotiation is enabled, then software will have to set the
	 * "PAUSE" bits to the correct value in the Auto-Negotiation
	 * Advertisement Register (PHY_AUTONEG_ADV) and re-start auto-negotiation.
	 *
	 * The possible values of the "fc" parameter are:
	 *      0:  Flow control is completely disabled
	 *      1:  Rx flow control is enabled (we can receive pause frames
	 *          but not send pause frames).
	 *      2:  Tx flow control is enabled (we can send pause frames
	 *          but we do not support receiving pause frames).
	 *      3:  Both Rx and TX flow control (symmetric) are enabled.
	 *  other:  No software override.  The flow control configuration
	 *          in the EEPROM is used.
	 */
	switch (hw->fc) {
	case E1000_FC_NONE:	/* 0 */
		/* Flow control (RX & TX) is completely disabled by a
		 * software over-ride.
		 */
		mii_autoneg_adv_reg &= ~(NWAY_AR_ASM_DIR | NWAY_AR_PAUSE);
		break;
	case E1000_FC_RX_PAUSE:	/* 1 */
		/* RX Flow control is enabled, and TX Flow control is
		 * disabled, by a software over-ride.
		 */
		/* Since there really isn't a way to advertise that we are
		 * capable of RX Pause ONLY, we will advertise that we
		 * support both symmetric and asymmetric RX PAUSE.  Later
		 * (in e1000_config_fc_after_link_up) we will disable the
		 *hw's ability to send PAUSE frames.
		 */
		mii_autoneg_adv_reg |= (NWAY_AR_ASM_DIR | NWAY_AR_PAUSE);
		break;
	case E1000_FC_TX_PAUSE:	/* 2 */
		/* TX Flow control is enabled, and RX Flow control is
		 * disabled, by a software over-ride.
		 */
		mii_autoneg_adv_reg |= NWAY_AR_ASM_DIR;
		mii_autoneg_adv_reg &= ~NWAY_AR_PAUSE;
		break;
	case E1000_FC_FULL:	/* 3 */
		/* Flow control (both RX and TX) is enabled by a software
		 * over-ride.
		 */
		mii_autoneg_adv_reg |= (NWAY_AR_ASM_DIR | NWAY_AR_PAUSE);
		break;
	default:
		e_dbg("Flow control param set incorrectly\n");
		return -E1000_ERR_CONFIG;
	}

	ret_val = e1000_write_phy_reg(hw, PHY_AUTONEG_ADV, mii_autoneg_adv_reg);
	if (ret_val)
		return ret_val;

	e_dbg("Auto-Neg Advertising %x\n", mii_autoneg_adv_reg);

	if (hw->phy_type == e1000_phy_8201) {
		mii_1000t_ctrl_reg = 0;
	} else {
		ret_val = e1000_write_phy_reg(hw, PHY_1000T_CTRL,
		                              mii_1000t_ctrl_reg);
		if (ret_val)
			return ret_val;
	}

	return E1000_SUCCESS;
}

/**
 * e1000_phy_force_speed_duplex - force link settings
 * @hw: Struct containing variables accessed by shared code
 *
 * Force PHY speed and duplex settings to hw->forced_speed_duplex
 */
static s32 e1000_phy_force_speed_duplex(struct e1000_hw *hw)
{
	u32 ctrl;
	s32 ret_val;
	u16 mii_ctrl_reg;
	u16 mii_status_reg;
	u16 phy_data;
	u16 i;

	e_dbg("e1000_phy_force_speed_duplex");

	/* Turn off Flow control if we are forcing speed and duplex. */
	hw->fc = E1000_FC_NONE;

	e_dbg("hw->fc = %d\n", hw->fc);

	/* Read the Device Control Register. */
	ctrl = er32(CTRL);

	/* Set the bits to Force Speed and Duplex in the Device Ctrl Reg. */
	ctrl |= (E1000_CTRL_FRCSPD | E1000_CTRL_FRCDPX);
	ctrl &= ~(DEVICE_SPEED_MASK);

	/* Clear the Auto Speed Detect Enable bit. */
	ctrl &= ~E1000_CTRL_ASDE;

	/* Read the MII Control Register. */
	ret_val = e1000_read_phy_reg(hw, PHY_CTRL, &mii_ctrl_reg);
	if (ret_val)
		return ret_val;

	/* We need to disable autoneg in order to force link and duplex. */

	mii_ctrl_reg &= ~MII_CR_AUTO_NEG_EN;

	/* Are we forcing Full or Half Duplex? */
	if (hw->forced_speed_duplex == e1000_100_full ||
	    hw->forced_speed_duplex == e1000_10_full) {
		/* We want to force full duplex so we SET the full duplex bits in the
		 * Device and MII Control Registers.
		 */
		ctrl |= E1000_CTRL_FD;
		mii_ctrl_reg |= MII_CR_FULL_DUPLEX;
		e_dbg("Full Duplex\n");
	} else {
		/* We want to force half duplex so we CLEAR the full duplex bits in
		 * the Device and MII Control Registers.
		 */
		ctrl &= ~E1000_CTRL_FD;
		mii_ctrl_reg &= ~MII_CR_FULL_DUPLEX;
		e_dbg("Half Duplex\n");
	}

	/* Are we forcing 100Mbps??? */
	if (hw->forced_speed_duplex == e1000_100_full ||
	    hw->forced_speed_duplex == e1000_100_half) {
		/* Set the 100Mb bit and turn off the 1000Mb and 10Mb bits. */
		ctrl |= E1000_CTRL_SPD_100;
		mii_ctrl_reg |= MII_CR_SPEED_100;
		mii_ctrl_reg &= ~(MII_CR_SPEED_1000 | MII_CR_SPEED_10);
		e_dbg("Forcing 100mb ");
	} else {
		/* Set the 10Mb bit and turn off the 1000Mb and 100Mb bits. */
		ctrl &= ~(E1000_CTRL_SPD_1000 | E1000_CTRL_SPD_100);
		mii_ctrl_reg |= MII_CR_SPEED_10;
		mii_ctrl_reg &= ~(MII_CR_SPEED_1000 | MII_CR_SPEED_100);
		e_dbg("Forcing 10mb ");
	}

	e1000_config_collision_dist(hw);

	/* Write the configured values back to the Device Control Reg. */
	ew32(CTRL, ctrl);

	if (hw->phy_type == e1000_phy_m88) {
		ret_val =
		    e1000_read_phy_reg(hw, M88E1000_PHY_SPEC_CTRL, &phy_data);
		if (ret_val)
			return ret_val;

		/* Clear Auto-Crossover to force MDI manually. M88E1000 requires MDI
		 * forced whenever speed are duplex are forced.
		 */
		phy_data &= ~M88E1000_PSCR_AUTO_X_MODE;
		ret_val =
		    e1000_write_phy_reg(hw, M88E1000_PHY_SPEC_CTRL, phy_data);
		if (ret_val)
			return ret_val;

		e_dbg("M88E1000 PSCR: %x\n", phy_data);

		/* Need to reset the PHY or these changes will be ignored */
		mii_ctrl_reg |= MII_CR_RESET;

		/* Disable MDI-X support for 10/100 */
	} else {
		/* Clear Auto-Crossover to force MDI manually.  IGP requires MDI
		 * forced whenever speed or duplex are forced.
		 */
		ret_val =
		    e1000_read_phy_reg(hw, IGP01E1000_PHY_PORT_CTRL, &phy_data);
		if (ret_val)
			return ret_val;

		phy_data &= ~IGP01E1000_PSCR_AUTO_MDIX;
		phy_data &= ~IGP01E1000_PSCR_FORCE_MDI_MDIX;

		ret_val =
		    e1000_write_phy_reg(hw, IGP01E1000_PHY_PORT_CTRL, phy_data);
		if (ret_val)
			return ret_val;
	}

	/* Write back the modified PHY MII control register. */
	ret_val = e1000_write_phy_reg(hw, PHY_CTRL, mii_ctrl_reg);
	if (ret_val)
		return ret_val;

	udelay(1);

	/* The wait_autoneg_complete flag may be a little misleading here.
	 * Since we are forcing speed and duplex, Auto-Neg is not enabled.
	 * But we do want to delay for a period while forcing only so we
	 * don't generate false No Link messages.  So we will wait here
	 * only if the user has set wait_autoneg_complete to 1, which is
	 * the default.
	 */
	if (hw->wait_autoneg_complete) {
		/* We will wait for autoneg to complete. */
		e_dbg("Waiting for forced speed/duplex link.\n");
		mii_status_reg = 0;

		/* We will wait for autoneg to complete or 4.5 seconds to expire. */
		for (i = PHY_FORCE_TIME; i > 0; i--) {
			/* Read the MII Status Register and wait for Auto-Neg Complete bit
			 * to be set.
			 */
			ret_val =
			    e1000_read_phy_reg(hw, PHY_STATUS, &mii_status_reg);
			if (ret_val)
				return ret_val;

			ret_val =
			    e1000_read_phy_reg(hw, PHY_STATUS, &mii_status_reg);
			if (ret_val)
				return ret_val;

			if (mii_status_reg & MII_SR_LINK_STATUS)
				break;
			msleep(100);
		}
		if ((i == 0) && (hw->phy_type == e1000_phy_m88)) {
			/* We didn't get link.  Reset the DSP and wait again for link. */
			ret_val = e1000_phy_reset_dsp(hw);
			if (ret_val) {
				e_dbg("Error Resetting PHY DSP\n");
				return ret_val;
			}
		}
		/* This loop will early-out if the link condition has been met.  */
		for (i = PHY_FORCE_TIME; i > 0; i--) {
			if (mii_status_reg & MII_SR_LINK_STATUS)
				break;
			msleep(100);
			/* Read the MII Status Register and wait for Auto-Neg Complete bit
			 * to be set.
			 */
			ret_val =
			    e1000_read_phy_reg(hw, PHY_STATUS, &mii_status_reg);
			if (ret_val)
				return ret_val;

			ret_val =
			    e1000_read_phy_reg(hw, PHY_STATUS, &mii_status_reg);
			if (ret_val)
				return ret_val;
		}
	}

	if (hw->phy_type == e1000_phy_m88) {
		/* Because we reset the PHY above, we need to re-force TX_CLK in the
		 * Extended PHY Specific Control Register to 25MHz clock.  This value
		 * defaults back to a 2.5MHz clock when the PHY is reset.
		 */
		ret_val =
		    e1000_read_phy_reg(hw, M88E1000_EXT_PHY_SPEC_CTRL,
				       &phy_data);
		if (ret_val)
			return ret_val;

		phy_data |= M88E1000_EPSCR_TX_CLK_25;
		ret_val =
		    e1000_write_phy_reg(hw, M88E1000_EXT_PHY_SPEC_CTRL,
					phy_data);
		if (ret_val)
			return ret_val;

		/* In addition, because of the s/w reset above, we need to enable CRS on
		 * TX.  This must be set for both full and half duplex operation.
		 */
		ret_val =
		    e1000_read_phy_reg(hw, M88E1000_PHY_SPEC_CTRL, &phy_data);
		if (ret_val)
			return ret_val;

		phy_data |= M88E1000_PSCR_ASSERT_CRS_ON_TX;
		ret_val =
		    e1000_write_phy_reg(hw, M88E1000_PHY_SPEC_CTRL, phy_data);
		if (ret_val)
			return ret_val;

		if ((hw->mac_type == e1000_82544 || hw->mac_type == e1000_82543)
		    && (!hw->autoneg)
		    && (hw->forced_speed_duplex == e1000_10_full
			|| hw->forced_speed_duplex == e1000_10_half)) {
			ret_val = e1000_polarity_reversal_workaround(hw);
			if (ret_val)
				return ret_val;
		}
	}
	return E1000_SUCCESS;
}

/**
 * e1000_config_collision_dist - set collision distance register
 * @hw: Struct containing variables accessed by shared code
 *
 * Sets the collision distance in the Transmit Control register.
 * Link should have been established previously. Reads the speed and duplex
 * information from the Device Status register.
 */
void e1000_config_collision_dist(struct e1000_hw *hw)
{
	u32 tctl, coll_dist;

	e_dbg("e1000_config_collision_dist");

	if (hw->mac_type < e1000_82543)
		coll_dist = E1000_COLLISION_DISTANCE_82542;
	else
		coll_dist = E1000_COLLISION_DISTANCE;

	tctl = er32(TCTL);

	tctl &= ~E1000_TCTL_COLD;
	tctl |= coll_dist << E1000_COLD_SHIFT;

	ew32(TCTL, tctl);
	E1000_WRITE_FLUSH();
}

/**
 * e1000_config_mac_to_phy - sync phy and mac settings
 * @hw: Struct containing variables accessed by shared code
 * @mii_reg: data to write to the MII control register
 *
 * Sets MAC speed and duplex settings to reflect the those in the PHY
 * The contents of the PHY register containing the needed information need to
 * be passed in.
 */
static s32 e1000_config_mac_to_phy(struct e1000_hw *hw)
{
	u32 ctrl;
	s32 ret_val;
	u16 phy_data;

	e_dbg("e1000_config_mac_to_phy");

	/* 82544 or newer MAC, Auto Speed Detection takes care of
	 * MAC speed/duplex configuration.*/
	if ((hw->mac_type >= e1000_82544) && (hw->mac_type != e1000_ce4100))
		return E1000_SUCCESS;

	/* Read the Device Control Register and set the bits to Force Speed
	 * and Duplex.
	 */
	ctrl = er32(CTRL);
	ctrl |= (E1000_CTRL_FRCSPD | E1000_CTRL_FRCDPX);
	ctrl &= ~(E1000_CTRL_SPD_SEL | E1000_CTRL_ILOS);

	switch (hw->phy_type) {
	case e1000_phy_8201:
		ret_val = e1000_read_phy_reg(hw, PHY_CTRL, &phy_data);
		if (ret_val)
			return ret_val;

		if (phy_data & RTL_PHY_CTRL_FD)
			ctrl |= E1000_CTRL_FD;
		else
			ctrl &= ~E1000_CTRL_FD;

		if (phy_data & RTL_PHY_CTRL_SPD_100)
			ctrl |= E1000_CTRL_SPD_100;
		else
			ctrl |= E1000_CTRL_SPD_10;

		e1000_config_collision_dist(hw);
		break;
	default:
		/* Set up duplex in the Device Control and Transmit Control
		 * registers depending on negotiated values.
		 */
		ret_val = e1000_read_phy_reg(hw, M88E1000_PHY_SPEC_STATUS,
		                             &phy_data);
		if (ret_val)
			return ret_val;

		if (phy_data & M88E1000_PSSR_DPLX)
			ctrl |= E1000_CTRL_FD;
		else
			ctrl &= ~E1000_CTRL_FD;

		e1000_config_collision_dist(hw);

		/* Set up speed in the Device Control register depending on
		 * negotiated values.
		 */
		if ((phy_data & M88E1000_PSSR_SPEED) == M88E1000_PSSR_1000MBS)
			ctrl |= E1000_CTRL_SPD_1000;
		else if ((phy_data & M88E1000_PSSR_SPEED) ==
		         M88E1000_PSSR_100MBS)
			ctrl |= E1000_CTRL_SPD_100;
	}

	/* Write the configured values back to the Device Control Reg. */
	ew32(CTRL, ctrl);
	return E1000_SUCCESS;
}

/**
 * e1000_force_mac_fc - force flow control settings
 * @hw: Struct containing variables accessed by shared code
 *
 * Forces the MAC's flow control settings.
 * Sets the TFCE and RFCE bits in the device control register to reflect
 * the adapter settings. TFCE and RFCE need to be explicitly set by
 * software when a Copper PHY is used because autonegotiation is managed
 * by the PHY rather than the MAC. Software must also configure these
 * bits when link is forced on a fiber connection.
 */
s32 e1000_force_mac_fc(struct e1000_hw *hw)
{
	u32 ctrl;

	e_dbg("e1000_force_mac_fc");

	/* Get the current configuration of the Device Control Register */
	ctrl = er32(CTRL);

	/* Because we didn't get link via the internal auto-negotiation
	 * mechanism (we either forced link or we got link via PHY
	 * auto-neg), we have to manually enable/disable transmit an
	 * receive flow control.
	 *
	 * The "Case" statement below enables/disable flow control
	 * according to the "hw->fc" parameter.
	 *
	 * The possible values of the "fc" parameter are:
	 *      0:  Flow control is completely disabled
	 *      1:  Rx flow control is enabled (we can receive pause
	 *          frames but not send pause frames).
	 *      2:  Tx flow control is enabled (we can send pause frames
	 *          frames but we do not receive pause frames).
	 *      3:  Both Rx and TX flow control (symmetric) is enabled.
	 *  other:  No other values should be possible at this point.
	 */

	switch (hw->fc) {
	case E1000_FC_NONE:
		ctrl &= (~(E1000_CTRL_TFCE | E1000_CTRL_RFCE));
		break;
	case E1000_FC_RX_PAUSE:
		ctrl &= (~E1000_CTRL_TFCE);
		ctrl |= E1000_CTRL_RFCE;
		break;
	case E1000_FC_TX_PAUSE:
		ctrl &= (~E1000_CTRL_RFCE);
		ctrl |= E1000_CTRL_TFCE;
		break;
	case E1000_FC_FULL:
		ctrl |= (E1000_CTRL_TFCE | E1000_CTRL_RFCE);
		break;
	default:
		e_dbg("Flow control param set incorrectly\n");
		return -E1000_ERR_CONFIG;
	}

	/* Disable TX Flow Control for 82542 (rev 2.0) */
	if (hw->mac_type == e1000_82542_rev2_0)
		ctrl &= (~E1000_CTRL_TFCE);

	ew32(CTRL, ctrl);
	return E1000_SUCCESS;
}

/**
 * e1000_config_fc_after_link_up - configure flow control after autoneg
 * @hw: Struct containing variables accessed by shared code
 *
 * Configures flow control settings after link is established
 * Should be called immediately after a valid link has been established.
 * Forces MAC flow control settings if link was forced. When in MII/GMII mode
 * and autonegotiation is enabled, the MAC flow control settings will be set
 * based on the flow control negotiated by the PHY. In TBI mode, the TFCE
 * and RFCE bits will be automatically set to the negotiated flow control mode.
 */
static s32 e1000_config_fc_after_link_up(struct e1000_hw *hw)
{
	s32 ret_val;
	u16 mii_status_reg;
	u16 mii_nway_adv_reg;
	u16 mii_nway_lp_ability_reg;
	u16 speed;
	u16 duplex;

	e_dbg("e1000_config_fc_after_link_up");

	/* Check for the case where we have fiber media and auto-neg failed
	 * so we had to force link.  In this case, we need to force the
	 * configuration of the MAC to match the "fc" parameter.
	 */
	if (((hw->media_type == e1000_media_type_fiber) && (hw->autoneg_failed))
	    || ((hw->media_type == e1000_media_type_internal_serdes)
		&& (hw->autoneg_failed))
	    || ((hw->media_type == e1000_media_type_copper)
		&& (!hw->autoneg))) {
		ret_val = e1000_force_mac_fc(hw);
		if (ret_val) {
			e_dbg("Error forcing flow control settings\n");
			return ret_val;
		}
	}

	/* Check for the case where we have copper media and auto-neg is
	 * enabled.  In this case, we need to check and see if Auto-Neg
	 * has completed, and if so, how the PHY and link partner has
	 * flow control configured.
	 */
	if ((hw->media_type == e1000_media_type_copper) && hw->autoneg) {
		/* Read the MII Status Register and check to see if AutoNeg
		 * has completed.  We read this twice because this reg has
		 * some "sticky" (latched) bits.
		 */
		ret_val = e1000_read_phy_reg(hw, PHY_STATUS, &mii_status_reg);
		if (ret_val)
			return ret_val;
		ret_val = e1000_read_phy_reg(hw, PHY_STATUS, &mii_status_reg);
		if (ret_val)
			return ret_val;

		if (mii_status_reg & MII_SR_AUTONEG_COMPLETE) {
			/* The AutoNeg process has completed, so we now need to
			 * read both the Auto Negotiation Advertisement Register
			 * (Address 4) and the Auto_Negotiation Base Page Ability
			 * Register (Address 5) to determine how flow control was
			 * negotiated.
			 */
			ret_val = e1000_read_phy_reg(hw, PHY_AUTONEG_ADV,
						     &mii_nway_adv_reg);
			if (ret_val)
				return ret_val;
			ret_val = e1000_read_phy_reg(hw, PHY_LP_ABILITY,
						     &mii_nway_lp_ability_reg);
			if (ret_val)
				return ret_val;

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
			 *   0   |    0    |  DC   |   DC    | E1000_FC_NONE
			 *   0   |    1    |   0   |   DC    | E1000_FC_NONE
			 *   0   |    1    |   1   |    0    | E1000_FC_NONE
			 *   0   |    1    |   1   |    1    | E1000_FC_TX_PAUSE
			 *   1   |    0    |   0   |   DC    | E1000_FC_NONE
			 *   1   |   DC    |   1   |   DC    | E1000_FC_FULL
			 *   1   |    1    |   0   |    0    | E1000_FC_NONE
			 *   1   |    1    |   0   |    1    | E1000_FC_RX_PAUSE
			 *
			 */
			/* Are both PAUSE bits set to 1?  If so, this implies
			 * Symmetric Flow Control is enabled at both ends.  The
			 * ASM_DIR bits are irrelevant per the spec.
			 *
			 * For Symmetric Flow Control:
			 *
			 *   LOCAL DEVICE  |   LINK PARTNER
			 * PAUSE | ASM_DIR | PAUSE | ASM_DIR | Result
			 *-------|---------|-------|---------|--------------------
			 *   1   |   DC    |   1   |   DC    | E1000_FC_FULL
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
				if (hw->original_fc == E1000_FC_FULL) {
					hw->fc = E1000_FC_FULL;
					e_dbg("Flow Control = FULL.\n");
				} else {
					hw->fc = E1000_FC_RX_PAUSE;
					e_dbg
					    ("Flow Control = RX PAUSE frames only.\n");
				}
			}
			/* For receiving PAUSE frames ONLY.
			 *
			 *   LOCAL DEVICE  |   LINK PARTNER
			 * PAUSE | ASM_DIR | PAUSE | ASM_DIR | Result
			 *-------|---------|-------|---------|--------------------
			 *   0   |    1    |   1   |    1    | E1000_FC_TX_PAUSE
			 *
			 */
			else if (!(mii_nway_adv_reg & NWAY_AR_PAUSE) &&
				 (mii_nway_adv_reg & NWAY_AR_ASM_DIR) &&
				 (mii_nway_lp_ability_reg & NWAY_LPAR_PAUSE) &&
				 (mii_nway_lp_ability_reg & NWAY_LPAR_ASM_DIR))
			{
				hw->fc = E1000_FC_TX_PAUSE;
				e_dbg
				    ("Flow Control = TX PAUSE frames only.\n");
			}
			/* For transmitting PAUSE frames ONLY.
			 *
			 *   LOCAL DEVICE  |   LINK PARTNER
			 * PAUSE | ASM_DIR | PAUSE | ASM_DIR | Result
			 *-------|---------|-------|---------|--------------------
			 *   1   |    1    |   0   |    1    | E1000_FC_RX_PAUSE
			 *
			 */
			else if ((mii_nway_adv_reg & NWAY_AR_PAUSE) &&
				 (mii_nway_adv_reg & NWAY_AR_ASM_DIR) &&
				 !(mii_nway_lp_ability_reg & NWAY_LPAR_PAUSE) &&
				 (mii_nway_lp_ability_reg & NWAY_LPAR_ASM_DIR))
			{
				hw->fc = E1000_FC_RX_PAUSE;
				e_dbg
				    ("Flow Control = RX PAUSE frames only.\n");
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
			else if ((hw->original_fc == E1000_FC_NONE ||
				  hw->original_fc == E1000_FC_TX_PAUSE) ||
				 hw->fc_strict_ieee) {
				hw->fc = E1000_FC_NONE;
				e_dbg("Flow Control = NONE.\n");
			} else {
				hw->fc = E1000_FC_RX_PAUSE;
				e_dbg
				    ("Flow Control = RX PAUSE frames only.\n");
			}

			/* Now we need to do one last check...  If we auto-
			 * negotiated to HALF DUPLEX, flow control should not be
			 * enabled per IEEE 802.3 spec.
			 */
			ret_val =
			    e1000_get_speed_and_duplex(hw, &speed, &duplex);
			if (ret_val) {
				e_dbg
				    ("Error getting link speed and duplex\n");
				return ret_val;
			}

			if (duplex == HALF_DUPLEX)
				hw->fc = E1000_FC_NONE;

			/* Now we call a subroutine to actually force the MAC
			 * controller to use the correct flow control settings.
			 */
			ret_val = e1000_force_mac_fc(hw);
			if (ret_val) {
				e_dbg
				    ("Error forcing flow control settings\n");
				return ret_val;
			}
		} else {
			e_dbg
			    ("Copper PHY and Auto Neg has not completed.\n");
		}
	}
	return E1000_SUCCESS;
}

/**
 * e1000_check_for_serdes_link_generic - Check for link (Serdes)
 * @hw: pointer to the HW structure
 *
 * Checks for link up on the hardware.  If link is not up and we have
 * a signal, then we need to force link up.
 */
static s32 e1000_check_for_serdes_link_generic(struct e1000_hw *hw)
{
	u32 rxcw;
	u32 ctrl;
	u32 status;
	s32 ret_val = E1000_SUCCESS;

	e_dbg("e1000_check_for_serdes_link_generic");

	ctrl = er32(CTRL);
	status = er32(STATUS);
	rxcw = er32(RXCW);

	/*
	 * If we don't have link (auto-negotiation failed or link partner
	 * cannot auto-negotiate), and our link partner is not trying to
	 * auto-negotiate with us (we are receiving idles or data),
	 * we need to force link up. We also need to give auto-negotiation
	 * time to complete.
	 */
	/* (ctrl & E1000_CTRL_SWDPIN1) == 1 == have signal */
	if ((!(status & E1000_STATUS_LU)) && (!(rxcw & E1000_RXCW_C))) {
		if (hw->autoneg_failed == 0) {
			hw->autoneg_failed = 1;
			goto out;
		}
		e_dbg("NOT RXing /C/, disable AutoNeg and force link.\n");

		/* Disable auto-negotiation in the TXCW register */
		ew32(TXCW, (hw->txcw & ~E1000_TXCW_ANE));

		/* Force link-up and also force full-duplex. */
		ctrl = er32(CTRL);
		ctrl |= (E1000_CTRL_SLU | E1000_CTRL_FD);
		ew32(CTRL, ctrl);

		/* Configure Flow Control after forcing link up. */
		ret_val = e1000_config_fc_after_link_up(hw);
		if (ret_val) {
			e_dbg("Error configuring flow control\n");
			goto out;
		}
	} else if ((ctrl & E1000_CTRL_SLU) && (rxcw & E1000_RXCW_C)) {
		/*
		 * If we are forcing link and we are receiving /C/ ordered
		 * sets, re-enable auto-negotiation in the TXCW register
		 * and disable forced link in the Device Control register
		 * in an attempt to auto-negotiate with our link partner.
		 */
		e_dbg("RXing /C/, enable AutoNeg and stop forcing link.\n");
		ew32(TXCW, hw->txcw);
		ew32(CTRL, (ctrl & ~E1000_CTRL_SLU));

		hw->serdes_has_link = true;
	} else if (!(E1000_TXCW_ANE & er32(TXCW))) {
		/*
		 * If we force link for non-auto-negotiation switch, check
		 * link status based on MAC synchronization for internal
		 * serdes media type.
		 */
		/* SYNCH bit and IV bit are sticky. */
		udelay(10);
		rxcw = er32(RXCW);
		if (rxcw & E1000_RXCW_SYNCH) {
			if (!(rxcw & E1000_RXCW_IV)) {
				hw->serdes_has_link = true;
				e_dbg("SERDES: Link up - forced.\n");
			}
		} else {
			hw->serdes_has_link = false;
			e_dbg("SERDES: Link down - force failed.\n");
		}
	}

	if (E1000_TXCW_ANE & er32(TXCW)) {
		status = er32(STATUS);
		if (status & E1000_STATUS_LU) {
			/* SYNCH bit and IV bit are sticky, so reread rxcw. */
			udelay(10);
			rxcw = er32(RXCW);
			if (rxcw & E1000_RXCW_SYNCH) {
				if (!(rxcw & E1000_RXCW_IV)) {
					hw->serdes_has_link = true;
					e_dbg("SERDES: Link up - autoneg "
						 "completed successfully.\n");
				} else {
					hw->serdes_has_link = false;
					e_dbg("SERDES: Link down - invalid"
						 "codewords detected in autoneg.\n");
				}
			} else {
				hw->serdes_has_link = false;
				e_dbg("SERDES: Link down - no sync.\n");
			}
		} else {
			hw->serdes_has_link = false;
			e_dbg("SERDES: Link down - autoneg failed\n");
		}
	}

      out:
	return ret_val;
}

/**
 * e1000_check_for_link
 * @hw: Struct containing variables accessed by shared code
 *
 * Checks to see if the link status of the hardware has changed.
 * Called by any function that needs to check the link status of the adapter.
 */
s32 e1000_check_for_link(struct e1000_hw *hw)
{
	u32 rxcw = 0;
	u32 ctrl;
	u32 status;
	u32 rctl;
	u32 icr;
	u32 signal = 0;
	s32 ret_val;
	u16 phy_data;

	e_dbg("e1000_check_for_link");

	ctrl = er32(CTRL);
	status = er32(STATUS);

	/* On adapters with a MAC newer than 82544, SW Definable pin 1 will be
	 * set when the optics detect a signal. On older adapters, it will be
	 * cleared when there is a signal.  This applies to fiber media only.
	 */
	if ((hw->media_type == e1000_media_type_fiber) ||
	    (hw->media_type == e1000_media_type_internal_serdes)) {
		rxcw = er32(RXCW);

		if (hw->media_type == e1000_media_type_fiber) {
			signal =
			    (hw->mac_type >
			     e1000_82544) ? E1000_CTRL_SWDPIN1 : 0;
			if (status & E1000_STATUS_LU)
				hw->get_link_status = false;
		}
	}

	/* If we have a copper PHY then we only want to go out to the PHY
	 * registers to see if Auto-Neg has completed and/or if our link
	 * status has changed.  The get_link_status flag will be set if we
	 * receive a Link Status Change interrupt or we have Rx Sequence
	 * Errors.
	 */
	if ((hw->media_type == e1000_media_type_copper) && hw->get_link_status) {
		/* First we want to see if the MII Status Register reports
		 * link.  If so, then we want to get the current speed/duplex
		 * of the PHY.
		 * Read the register twice since the link bit is sticky.
		 */
		ret_val = e1000_read_phy_reg(hw, PHY_STATUS, &phy_data);
		if (ret_val)
			return ret_val;
		ret_val = e1000_read_phy_reg(hw, PHY_STATUS, &phy_data);
		if (ret_val)
			return ret_val;

		if (phy_data & MII_SR_LINK_STATUS) {
			hw->get_link_status = false;
			/* Check if there was DownShift, must be checked immediately after
			 * link-up */
			e1000_check_downshift(hw);

			/* If we are on 82544 or 82543 silicon and speed/duplex
			 * are forced to 10H or 10F, then we will implement the polarity
			 * reversal workaround.  We disable interrupts first, and upon
			 * returning, place the devices interrupt state to its previous
			 * value except for the link status change interrupt which will
			 * happen due to the execution of this workaround.
			 */

			if ((hw->mac_type == e1000_82544
			     || hw->mac_type == e1000_82543) && (!hw->autoneg)
			    && (hw->forced_speed_duplex == e1000_10_full
				|| hw->forced_speed_duplex == e1000_10_half)) {
				ew32(IMC, 0xffffffff);
				ret_val =
				    e1000_polarity_reversal_workaround(hw);
				icr = er32(ICR);
				ew32(ICS, (icr & ~E1000_ICS_LSC));
				ew32(IMS, IMS_ENABLE_MASK);
			}

		} else {
			/* No link detected */
			e1000_config_dsp_after_link_change(hw, false);
			return 0;
		}

		/* If we are forcing speed/duplex, then we simply return since
		 * we have already determined whether we have link or not.
		 */
		if (!hw->autoneg)
			return -E1000_ERR_CONFIG;

		/* optimize the dsp settings for the igp phy */
		e1000_config_dsp_after_link_change(hw, true);

		/* We have a M88E1000 PHY and Auto-Neg is enabled.  If we
		 * have Si on board that is 82544 or newer, Auto
		 * Speed Detection takes care of MAC speed/duplex
		 * configuration.  So we only need to configure Collision
		 * Distance in the MAC.  Otherwise, we need to force
		 * speed/duplex on the MAC to the current PHY speed/duplex
		 * settings.
		 */
		if ((hw->mac_type >= e1000_82544) &&
		    (hw->mac_type != e1000_ce4100))
			e1000_config_collision_dist(hw);
		else {
			ret_val = e1000_config_mac_to_phy(hw);
			if (ret_val) {
				e_dbg
				    ("Error configuring MAC to PHY settings\n");
				return ret_val;
			}
		}

		/* Configure Flow Control now that Auto-Neg has completed. First, we
		 * need to restore the desired flow control settings because we may
		 * have had to re-autoneg with a different link partner.
		 */
		ret_val = e1000_config_fc_after_link_up(hw);
		if (ret_val) {
			e_dbg("Error configuring flow control\n");
			return ret_val;
		}

		/* At this point we know that we are on copper and we have
		 * auto-negotiated link.  These are conditions for checking the link
		 * partner capability register.  We use the link speed to determine if
		 * TBI compatibility needs to be turned on or off.  If the link is not
		 * at gigabit speed, then TBI compatibility is not needed.  If we are
		 * at gigabit speed, we turn on TBI compatibility.
		 */
		if (hw->tbi_compatibility_en) {
			u16 speed, duplex;
			ret_val =
			    e1000_get_speed_and_duplex(hw, &speed, &duplex);
			if (ret_val) {
				e_dbg
				    ("Error getting link speed and duplex\n");
				return ret_val;
			}
			if (speed != SPEED_1000) {
				/* If link speed is not set to gigabit speed, we do not need
				 * to enable TBI compatibility.
				 */
				if (hw->tbi_compatibility_on) {
					/* If we previously were in the mode, turn it off. */
					rctl = er32(RCTL);
					rctl &= ~E1000_RCTL_SBP;
					ew32(RCTL, rctl);
					hw->tbi_compatibility_on = false;
				}
			} else {
				/* If TBI compatibility is was previously off, turn it on. For
				 * compatibility with a TBI link partner, we will store bad
				 * packets. Some frames have an additional byte on the end and
				 * will look like CRC errors to to the hardware.
				 */
				if (!hw->tbi_compatibility_on) {
					hw->tbi_compatibility_on = true;
					rctl = er32(RCTL);
					rctl |= E1000_RCTL_SBP;
					ew32(RCTL, rctl);
				}
			}
		}
	}

	if ((hw->media_type == e1000_media_type_fiber) ||
	    (hw->media_type == e1000_media_type_internal_serdes))
		e1000_check_for_serdes_link_generic(hw);

	return E1000_SUCCESS;
}

/**
 * e1000_get_speed_and_duplex
 * @hw: Struct containing variables accessed by shared code
 * @speed: Speed of the connection
 * @duplex: Duplex setting of the connection

 * Detects the current speed and duplex settings of the hardware.
 */
s32 e1000_get_speed_and_duplex(struct e1000_hw *hw, u16 *speed, u16 *duplex)
{
	u32 status;
	s32 ret_val;
	u16 phy_data;

	e_dbg("e1000_get_speed_and_duplex");

	if (hw->mac_type >= e1000_82543) {
		status = er32(STATUS);
		if (status & E1000_STATUS_SPEED_1000) {
			*speed = SPEED_1000;
			e_dbg("1000 Mbs, ");
		} else if (status & E1000_STATUS_SPEED_100) {
			*speed = SPEED_100;
			e_dbg("100 Mbs, ");
		} else {
			*speed = SPEED_10;
			e_dbg("10 Mbs, ");
		}

		if (status & E1000_STATUS_FD) {
			*duplex = FULL_DUPLEX;
			e_dbg("Full Duplex\n");
		} else {
			*duplex = HALF_DUPLEX;
			e_dbg(" Half Duplex\n");
		}
	} else {
		e_dbg("1000 Mbs, Full Duplex\n");
		*speed = SPEED_1000;
		*duplex = FULL_DUPLEX;
	}

	/* IGP01 PHY may advertise full duplex operation after speed downgrade even
	 * if it is operating at half duplex.  Here we set the duplex settings to
	 * match the duplex in the link partner's capabilities.
	 */
	if (hw->phy_type == e1000_phy_igp && hw->speed_downgraded) {
		ret_val = e1000_read_phy_reg(hw, PHY_AUTONEG_EXP, &phy_data);
		if (ret_val)
			return ret_val;

		if (!(phy_data & NWAY_ER_LP_NWAY_CAPS))
			*duplex = HALF_DUPLEX;
		else {
			ret_val =
			    e1000_read_phy_reg(hw, PHY_LP_ABILITY, &phy_data);
			if (ret_val)
				return ret_val;
			if ((*speed == SPEED_100
			     && !(phy_data & NWAY_LPAR_100TX_FD_CAPS))
			    || (*speed == SPEED_10
				&& !(phy_data & NWAY_LPAR_10T_FD_CAPS)))
				*duplex = HALF_DUPLEX;
		}
	}

	return E1000_SUCCESS;
}

/**
 * e1000_wait_autoneg
 * @hw: Struct containing variables accessed by shared code
 *
 * Blocks until autoneg completes or times out (~4.5 seconds)
 */
static s32 e1000_wait_autoneg(struct e1000_hw *hw)
{
	s32 ret_val;
	u16 i;
	u16 phy_data;

	e_dbg("e1000_wait_autoneg");
	e_dbg("Waiting for Auto-Neg to complete.\n");

	/* We will wait for autoneg to complete or 4.5 seconds to expire. */
	for (i = PHY_AUTO_NEG_TIME; i > 0; i--) {
		/* Read the MII Status Register and wait for Auto-Neg
		 * Complete bit to be set.
		 */
		ret_val = e1000_read_phy_reg(hw, PHY_STATUS, &phy_data);
		if (ret_val)
			return ret_val;
		ret_val = e1000_read_phy_reg(hw, PHY_STATUS, &phy_data);
		if (ret_val)
			return ret_val;
		if (phy_data & MII_SR_AUTONEG_COMPLETE) {
			return E1000_SUCCESS;
		}
		msleep(100);
	}
	return E1000_SUCCESS;
}

/**
 * e1000_raise_mdi_clk - Raises the Management Data Clock
 * @hw: Struct containing variables accessed by shared code
 * @ctrl: Device control register's current value
 */
static void e1000_raise_mdi_clk(struct e1000_hw *hw, u32 *ctrl)
{
	/* Raise the clock input to the Management Data Clock (by setting the MDC
	 * bit), and then delay 10 microseconds.
	 */
	ew32(CTRL, (*ctrl | E1000_CTRL_MDC));
	E1000_WRITE_FLUSH();
	udelay(10);
}

/**
 * e1000_lower_mdi_clk - Lowers the Management Data Clock
 * @hw: Struct containing variables accessed by shared code
 * @ctrl: Device control register's current value
 */
static void e1000_lower_mdi_clk(struct e1000_hw *hw, u32 *ctrl)
{
	/* Lower the clock input to the Management Data Clock (by clearing the MDC
	 * bit), and then delay 10 microseconds.
	 */
	ew32(CTRL, (*ctrl & ~E1000_CTRL_MDC));
	E1000_WRITE_FLUSH();
	udelay(10);
}

/**
 * e1000_shift_out_mdi_bits - Shifts data bits out to the PHY
 * @hw: Struct containing variables accessed by shared code
 * @data: Data to send out to the PHY
 * @count: Number of bits to shift out
 *
 * Bits are shifted out in MSB to LSB order.
 */
static void e1000_shift_out_mdi_bits(struct e1000_hw *hw, u32 data, u16 count)
{
	u32 ctrl;
	u32 mask;

	/* We need to shift "count" number of bits out to the PHY. So, the value
	 * in the "data" parameter will be shifted out to the PHY one bit at a
	 * time. In order to do this, "data" must be broken down into bits.
	 */
	mask = 0x01;
	mask <<= (count - 1);

	ctrl = er32(CTRL);

	/* Set MDIO_DIR and MDC_DIR direction bits to be used as output pins. */
	ctrl |= (E1000_CTRL_MDIO_DIR | E1000_CTRL_MDC_DIR);

	while (mask) {
		/* A "1" is shifted out to the PHY by setting the MDIO bit to "1" and
		 * then raising and lowering the Management Data Clock. A "0" is
		 * shifted out to the PHY by setting the MDIO bit to "0" and then
		 * raising and lowering the clock.
		 */
		if (data & mask)
			ctrl |= E1000_CTRL_MDIO;
		else
			ctrl &= ~E1000_CTRL_MDIO;

		ew32(CTRL, ctrl);
		E1000_WRITE_FLUSH();

		udelay(10);

		e1000_raise_mdi_clk(hw, &ctrl);
		e1000_lower_mdi_clk(hw, &ctrl);

		mask = mask >> 1;
	}
}

/**
 * e1000_shift_in_mdi_bits - Shifts data bits in from the PHY
 * @hw: Struct containing variables accessed by shared code
 *
 * Bits are shifted in in MSB to LSB order.
 */
static u16 e1000_shift_in_mdi_bits(struct e1000_hw *hw)
{
	u32 ctrl;
	u16 data = 0;
	u8 i;

	/* In order to read a register from the PHY, we need to shift in a total
	 * of 18 bits from the PHY. The first two bit (turnaround) times are used
	 * to avoid contention on the MDIO pin when a read operation is performed.
	 * These two bits are ignored by us and thrown away. Bits are "shifted in"
	 * by raising the input to the Management Data Clock (setting the MDC bit),
	 * and then reading the value of the MDIO bit.
	 */
	ctrl = er32(CTRL);

	/* Clear MDIO_DIR (SWDPIO1) to indicate this bit is to be used as input. */
	ctrl &= ~E1000_CTRL_MDIO_DIR;
	ctrl &= ~E1000_CTRL_MDIO;

	ew32(CTRL, ctrl);
	E1000_WRITE_FLUSH();

	/* Raise and Lower the clock before reading in the data. This accounts for
	 * the turnaround bits. The first clock occurred when we clocked out the
	 * last bit of the Register Address.
	 */
	e1000_raise_mdi_clk(hw, &ctrl);
	e1000_lower_mdi_clk(hw, &ctrl);

	for (data = 0, i = 0; i < 16; i++) {
		data = data << 1;
		e1000_raise_mdi_clk(hw, &ctrl);
		ctrl = er32(CTRL);
		/* Check to see if we shifted in a "1". */
		if (ctrl & E1000_CTRL_MDIO)
			data |= 1;
		e1000_lower_mdi_clk(hw, &ctrl);
	}

	e1000_raise_mdi_clk(hw, &ctrl);
	e1000_lower_mdi_clk(hw, &ctrl);

	return data;
}


/**
 * e1000_read_phy_reg - read a phy register
 * @hw: Struct containing variables accessed by shared code
 * @reg_addr: address of the PHY register to read
 *
 * Reads the value from a PHY register, if the value is on a specific non zero
 * page, sets the page first.
 */
s32 e1000_read_phy_reg(struct e1000_hw *hw, u32 reg_addr, u16 *phy_data)
{
	u32 ret_val;

	e_dbg("e1000_read_phy_reg");

	if ((hw->phy_type == e1000_phy_igp) &&
	    (reg_addr > MAX_PHY_MULTI_PAGE_REG)) {
		ret_val = e1000_write_phy_reg_ex(hw, IGP01E1000_PHY_PAGE_SELECT,
						 (u16) reg_addr);
		if (ret_val)
			return ret_val;
	}

	ret_val = e1000_read_phy_reg_ex(hw, MAX_PHY_REG_ADDRESS & reg_addr,
					phy_data);

	return ret_val;
}

static s32 e1000_read_phy_reg_ex(struct e1000_hw *hw, u32 reg_addr,
				 u16 *phy_data)
{
	u32 i;
	u32 mdic = 0;
	const u32 phy_addr = (hw->mac_type == e1000_ce4100) ? hw->phy_addr : 1;

	e_dbg("e1000_read_phy_reg_ex");

	if (reg_addr > MAX_PHY_REG_ADDRESS) {
		e_dbg("PHY Address %d is out of range\n", reg_addr);
		return -E1000_ERR_PARAM;
	}

	if (hw->mac_type > e1000_82543) {
		/* Set up Op-code, Phy Address, and register address in the MDI
		 * Control register.  The MAC will take care of interfacing with the
		 * PHY to retrieve the desired data.
		 */
		if (hw->mac_type == e1000_ce4100) {
			mdic = ((reg_addr << E1000_MDIC_REG_SHIFT) |
				(phy_addr << E1000_MDIC_PHY_SHIFT) |
				(INTEL_CE_GBE_MDIC_OP_READ) |
				(INTEL_CE_GBE_MDIC_GO));

			writel(mdic, E1000_MDIO_CMD);

			/* Poll the ready bit to see if the MDI read
			 * completed
			 */
			for (i = 0; i < 64; i++) {
				udelay(50);
				mdic = readl(E1000_MDIO_CMD);
				if (!(mdic & INTEL_CE_GBE_MDIC_GO))
					break;
			}

			if (mdic & INTEL_CE_GBE_MDIC_GO) {
				e_dbg("MDI Read did not complete\n");
				return -E1000_ERR_PHY;
			}

			mdic = readl(E1000_MDIO_STS);
			if (mdic & INTEL_CE_GBE_MDIC_READ_ERROR) {
				e_dbg("MDI Read Error\n");
				return -E1000_ERR_PHY;
			}
			*phy_data = (u16) mdic;
		} else {
			mdic = ((reg_addr << E1000_MDIC_REG_SHIFT) |
				(phy_addr << E1000_MDIC_PHY_SHIFT) |
				(E1000_MDIC_OP_READ));

			ew32(MDIC, mdic);

			/* Poll the ready bit to see if the MDI read
			 * completed
			 */
			for (i = 0; i < 64; i++) {
				udelay(50);
				mdic = er32(MDIC);
				if (mdic & E1000_MDIC_READY)
					break;
			}
			if (!(mdic & E1000_MDIC_READY)) {
				e_dbg("MDI Read did not complete\n");
				return -E1000_ERR_PHY;
			}
			if (mdic & E1000_MDIC_ERROR) {
				e_dbg("MDI Error\n");
				return -E1000_ERR_PHY;
			}
			*phy_data = (u16) mdic;
		}
	} else {
		/* We must first send a preamble through the MDIO pin to signal the
		 * beginning of an MII instruction.  This is done by sending 32
		 * consecutive "1" bits.
		 */
		e1000_shift_out_mdi_bits(hw, PHY_PREAMBLE, PHY_PREAMBLE_SIZE);

		/* Now combine the next few fields that are required for a read
		 * operation.  We use this method instead of calling the
		 * e1000_shift_out_mdi_bits routine five different times. The format of
		 * a MII read instruction consists of a shift out of 14 bits and is
		 * defined as follows:
		 *    <Preamble><SOF><Op Code><Phy Addr><Reg Addr>
		 * followed by a shift in of 18 bits.  This first two bits shifted in
		 * are TurnAround bits used to avoid contention on the MDIO pin when a
		 * READ operation is performed.  These two bits are thrown away
		 * followed by a shift in of 16 bits which contains the desired data.
		 */
		mdic = ((reg_addr) | (phy_addr << 5) |
			(PHY_OP_READ << 10) | (PHY_SOF << 12));

		e1000_shift_out_mdi_bits(hw, mdic, 14);

		/* Now that we've shifted out the read command to the MII, we need to
		 * "shift in" the 16-bit value (18 total bits) of the requested PHY
		 * register address.
		 */
		*phy_data = e1000_shift_in_mdi_bits(hw);
	}
	return E1000_SUCCESS;
}

/**
 * e1000_write_phy_reg - write a phy register
 *
 * @hw: Struct containing variables accessed by shared code
 * @reg_addr: address of the PHY register to write
 * @data: data to write to the PHY

 * Writes a value to a PHY register
 */
s32 e1000_write_phy_reg(struct e1000_hw *hw, u32 reg_addr, u16 phy_data)
{
	u32 ret_val;

	e_dbg("e1000_write_phy_reg");

	if ((hw->phy_type == e1000_phy_igp) &&
	    (reg_addr > MAX_PHY_MULTI_PAGE_REG)) {
		ret_val = e1000_write_phy_reg_ex(hw, IGP01E1000_PHY_PAGE_SELECT,
						 (u16) reg_addr);
		if (ret_val)
			return ret_val;
	}

	ret_val = e1000_write_phy_reg_ex(hw, MAX_PHY_REG_ADDRESS & reg_addr,
					 phy_data);

	return ret_val;
}

static s32 e1000_write_phy_reg_ex(struct e1000_hw *hw, u32 reg_addr,
				  u16 phy_data)
{
	u32 i;
	u32 mdic = 0;
	const u32 phy_addr = (hw->mac_type == e1000_ce4100) ? hw->phy_addr : 1;

	e_dbg("e1000_write_phy_reg_ex");

	if (reg_addr > MAX_PHY_REG_ADDRESS) {
		e_dbg("PHY Address %d is out of range\n", reg_addr);
		return -E1000_ERR_PARAM;
	}

	if (hw->mac_type > e1000_82543) {
		/* Set up Op-code, Phy Address, register address, and data
		 * intended for the PHY register in the MDI Control register.
		 * The MAC will take care of interfacing with the PHY to send
		 * the desired data.
		 */
		if (hw->mac_type == e1000_ce4100) {
			mdic = (((u32) phy_data) |
				(reg_addr << E1000_MDIC_REG_SHIFT) |
				(phy_addr << E1000_MDIC_PHY_SHIFT) |
				(INTEL_CE_GBE_MDIC_OP_WRITE) |
				(INTEL_CE_GBE_MDIC_GO));

			writel(mdic, E1000_MDIO_CMD);

			/* Poll the ready bit to see if the MDI read
			 * completed
			 */
			for (i = 0; i < 640; i++) {
				udelay(5);
				mdic = readl(E1000_MDIO_CMD);
				if (!(mdic & INTEL_CE_GBE_MDIC_GO))
					break;
			}
			if (mdic & INTEL_CE_GBE_MDIC_GO) {
				e_dbg("MDI Write did not complete\n");
				return -E1000_ERR_PHY;
			}
		} else {
			mdic = (((u32) phy_data) |
				(reg_addr << E1000_MDIC_REG_SHIFT) |
				(phy_addr << E1000_MDIC_PHY_SHIFT) |
				(E1000_MDIC_OP_WRITE));

			ew32(MDIC, mdic);

			/* Poll the ready bit to see if the MDI read
			 * completed
			 */
			for (i = 0; i < 641; i++) {
				udelay(5);
				mdic = er32(MDIC);
				if (mdic & E1000_MDIC_READY)
					break;
			}
			if (!(mdic & E1000_MDIC_READY)) {
				e_dbg("MDI Write did not complete\n");
				return -E1000_ERR_PHY;
			}
		}
	} else {
		/* We'll need to use the SW defined pins to shift the write command
		 * out to the PHY. We first send a preamble to the PHY to signal the
		 * beginning of the MII instruction.  This is done by sending 32
		 * consecutive "1" bits.
		 */
		e1000_shift_out_mdi_bits(hw, PHY_PREAMBLE, PHY_PREAMBLE_SIZE);

		/* Now combine the remaining required fields that will indicate a
		 * write operation. We use this method instead of calling the
		 * e1000_shift_out_mdi_bits routine for each field in the command. The
		 * format of a MII write instruction is as follows:
		 * <Preamble><SOF><Op Code><Phy Addr><Reg Addr><Turnaround><Data>.
		 */
		mdic = ((PHY_TURNAROUND) | (reg_addr << 2) | (phy_addr << 7) |
			(PHY_OP_WRITE << 12) | (PHY_SOF << 14));
		mdic <<= 16;
		mdic |= (u32) phy_data;

		e1000_shift_out_mdi_bits(hw, mdic, 32);
	}

	return E1000_SUCCESS;
}

/**
 * e1000_phy_hw_reset - reset the phy, hardware style
 * @hw: Struct containing variables accessed by shared code
 *
 * Returns the PHY to the power-on reset state
 */
s32 e1000_phy_hw_reset(struct e1000_hw *hw)
{
	u32 ctrl, ctrl_ext;
	u32 led_ctrl;
	s32 ret_val;

	e_dbg("e1000_phy_hw_reset");

	e_dbg("Resetting Phy...\n");

	if (hw->mac_type > e1000_82543) {
		/* Read the device control register and assert the E1000_CTRL_PHY_RST
		 * bit. Then, take it out of reset.
		 * For e1000 hardware, we delay for 10ms between the assert
		 * and deassert.
		 */
		ctrl = er32(CTRL);
		ew32(CTRL, ctrl | E1000_CTRL_PHY_RST);
		E1000_WRITE_FLUSH();

		msleep(10);

		ew32(CTRL, ctrl);
		E1000_WRITE_FLUSH();

	} else {
		/* Read the Extended Device Control Register, assert the PHY_RESET_DIR
		 * bit to put the PHY into reset. Then, take it out of reset.
		 */
		ctrl_ext = er32(CTRL_EXT);
		ctrl_ext |= E1000_CTRL_EXT_SDP4_DIR;
		ctrl_ext &= ~E1000_CTRL_EXT_SDP4_DATA;
		ew32(CTRL_EXT, ctrl_ext);
		E1000_WRITE_FLUSH();
		msleep(10);
		ctrl_ext |= E1000_CTRL_EXT_SDP4_DATA;
		ew32(CTRL_EXT, ctrl_ext);
		E1000_WRITE_FLUSH();
	}
	udelay(150);

	if ((hw->mac_type == e1000_82541) || (hw->mac_type == e1000_82547)) {
		/* Configure activity LED after PHY reset */
		led_ctrl = er32(LEDCTL);
		led_ctrl &= IGP_ACTIVITY_LED_MASK;
		led_ctrl |= (IGP_ACTIVITY_LED_ENABLE | IGP_LED3_MODE);
		ew32(LEDCTL, led_ctrl);
	}

	/* Wait for FW to finish PHY configuration. */
	ret_val = e1000_get_phy_cfg_done(hw);
	if (ret_val != E1000_SUCCESS)
		return ret_val;

	return ret_val;
}

/**
 * e1000_phy_reset - reset the phy to commit settings
 * @hw: Struct containing variables accessed by shared code
 *
 * Resets the PHY
 * Sets bit 15 of the MII Control register
 */
s32 e1000_phy_reset(struct e1000_hw *hw)
{
	s32 ret_val;
	u16 phy_data;

	e_dbg("e1000_phy_reset");

	switch (hw->phy_type) {
	case e1000_phy_igp:
		ret_val = e1000_phy_hw_reset(hw);
		if (ret_val)
			return ret_val;
		break;
	default:
		ret_val = e1000_read_phy_reg(hw, PHY_CTRL, &phy_data);
		if (ret_val)
			return ret_val;

		phy_data |= MII_CR_RESET;
		ret_val = e1000_write_phy_reg(hw, PHY_CTRL, phy_data);
		if (ret_val)
			return ret_val;

		udelay(1);
		break;
	}

	if (hw->phy_type == e1000_phy_igp)
		e1000_phy_init_script(hw);

	return E1000_SUCCESS;
}

/**
 * e1000_detect_gig_phy - check the phy type
 * @hw: Struct containing variables accessed by shared code
 *
 * Probes the expected PHY address for known PHY IDs
 */
static s32 e1000_detect_gig_phy(struct e1000_hw *hw)
{
	s32 phy_init_status, ret_val;
	u16 phy_id_high, phy_id_low;
	bool match = false;

	e_dbg("e1000_detect_gig_phy");

	if (hw->phy_id != 0)
		return E1000_SUCCESS;

	/* Read the PHY ID Registers to identify which PHY is onboard. */
	ret_val = e1000_read_phy_reg(hw, PHY_ID1, &phy_id_high);
	if (ret_val)
		return ret_val;

	hw->phy_id = (u32) (phy_id_high << 16);
	udelay(20);
	ret_val = e1000_read_phy_reg(hw, PHY_ID2, &phy_id_low);
	if (ret_val)
		return ret_val;

	hw->phy_id |= (u32) (phy_id_low & PHY_REVISION_MASK);
	hw->phy_revision = (u32) phy_id_low & ~PHY_REVISION_MASK;

	switch (hw->mac_type) {
	case e1000_82543:
		if (hw->phy_id == M88E1000_E_PHY_ID)
			match = true;
		break;
	case e1000_82544:
		if (hw->phy_id == M88E1000_I_PHY_ID)
			match = true;
		break;
	case e1000_82540:
	case e1000_82545:
	case e1000_82545_rev_3:
	case e1000_82546:
	case e1000_82546_rev_3:
		if (hw->phy_id == M88E1011_I_PHY_ID)
			match = true;
		break;
	case e1000_ce4100:
		if ((hw->phy_id == RTL8211B_PHY_ID) ||
		    (hw->phy_id == RTL8201N_PHY_ID) ||
		    (hw->phy_id == M88E1118_E_PHY_ID))
			match = true;
		break;
	case e1000_82541:
	case e1000_82541_rev_2:
	case e1000_82547:
	case e1000_82547_rev_2:
		if (hw->phy_id == IGP01E1000_I_PHY_ID)
			match = true;
		break;
	default:
		e_dbg("Invalid MAC type %d\n", hw->mac_type);
		return -E1000_ERR_CONFIG;
	}
	phy_init_status = e1000_set_phy_type(hw);

	if ((match) && (phy_init_status == E1000_SUCCESS)) {
		e_dbg("PHY ID 0x%X detected\n", hw->phy_id);
		return E1000_SUCCESS;
	}
	e_dbg("Invalid PHY ID 0x%X\n", hw->phy_id);
	return -E1000_ERR_PHY;
}

/**
 * e1000_phy_reset_dsp - reset DSP
 * @hw: Struct containing variables accessed by shared code
 *
 * Resets the PHY's DSP
 */
static s32 e1000_phy_reset_dsp(struct e1000_hw *hw)
{
	s32 ret_val;
	e_dbg("e1000_phy_reset_dsp");

	do {
		ret_val = e1000_write_phy_reg(hw, 29, 0x001d);
		if (ret_val)
			break;
		ret_val = e1000_write_phy_reg(hw, 30, 0x00c1);
		if (ret_val)
			break;
		ret_val = e1000_write_phy_reg(hw, 30, 0x0000);
		if (ret_val)
			break;
		ret_val = E1000_SUCCESS;
	} while (0);

	return ret_val;
}

/**
 * e1000_phy_igp_get_info - get igp specific registers
 * @hw: Struct containing variables accessed by shared code
 * @phy_info: PHY information structure
 *
 * Get PHY information from various PHY registers for igp PHY only.
 */
static s32 e1000_phy_igp_get_info(struct e1000_hw *hw,
				  struct e1000_phy_info *phy_info)
{
	s32 ret_val;
	u16 phy_data, min_length, max_length, average;
	e1000_rev_polarity polarity;

	e_dbg("e1000_phy_igp_get_info");

	/* The downshift status is checked only once, after link is established,
	 * and it stored in the hw->speed_downgraded parameter. */
	phy_info->downshift = (e1000_downshift) hw->speed_downgraded;

	/* IGP01E1000 does not need to support it. */
	phy_info->extended_10bt_distance = e1000_10bt_ext_dist_enable_normal;

	/* IGP01E1000 always correct polarity reversal */
	phy_info->polarity_correction = e1000_polarity_reversal_enabled;

	/* Check polarity status */
	ret_val = e1000_check_polarity(hw, &polarity);
	if (ret_val)
		return ret_val;

	phy_info->cable_polarity = polarity;

	ret_val = e1000_read_phy_reg(hw, IGP01E1000_PHY_PORT_STATUS, &phy_data);
	if (ret_val)
		return ret_val;

	phy_info->mdix_mode =
	    (e1000_auto_x_mode) ((phy_data & IGP01E1000_PSSR_MDIX) >>
				 IGP01E1000_PSSR_MDIX_SHIFT);

	if ((phy_data & IGP01E1000_PSSR_SPEED_MASK) ==
	    IGP01E1000_PSSR_SPEED_1000MBPS) {
		/* Local/Remote Receiver Information are only valid at 1000 Mbps */
		ret_val = e1000_read_phy_reg(hw, PHY_1000T_STATUS, &phy_data);
		if (ret_val)
			return ret_val;

		phy_info->local_rx = ((phy_data & SR_1000T_LOCAL_RX_STATUS) >>
				      SR_1000T_LOCAL_RX_STATUS_SHIFT) ?
		    e1000_1000t_rx_status_ok : e1000_1000t_rx_status_not_ok;
		phy_info->remote_rx = ((phy_data & SR_1000T_REMOTE_RX_STATUS) >>
				       SR_1000T_REMOTE_RX_STATUS_SHIFT) ?
		    e1000_1000t_rx_status_ok : e1000_1000t_rx_status_not_ok;

		/* Get cable length */
		ret_val = e1000_get_cable_length(hw, &min_length, &max_length);
		if (ret_val)
			return ret_val;

		/* Translate to old method */
		average = (max_length + min_length) / 2;

		if (average <= e1000_igp_cable_length_50)
			phy_info->cable_length = e1000_cable_length_50;
		else if (average <= e1000_igp_cable_length_80)
			phy_info->cable_length = e1000_cable_length_50_80;
		else if (average <= e1000_igp_cable_length_110)
			phy_info->cable_length = e1000_cable_length_80_110;
		else if (average <= e1000_igp_cable_length_140)
			phy_info->cable_length = e1000_cable_length_110_140;
		else
			phy_info->cable_length = e1000_cable_length_140;
	}

	return E1000_SUCCESS;
}

/**
 * e1000_phy_m88_get_info - get m88 specific registers
 * @hw: Struct containing variables accessed by shared code
 * @phy_info: PHY information structure
 *
 * Get PHY information from various PHY registers for m88 PHY only.
 */
static s32 e1000_phy_m88_get_info(struct e1000_hw *hw,
				  struct e1000_phy_info *phy_info)
{
	s32 ret_val;
	u16 phy_data;
	e1000_rev_polarity polarity;

	e_dbg("e1000_phy_m88_get_info");

	/* The downshift status is checked only once, after link is established,
	 * and it stored in the hw->speed_downgraded parameter. */
	phy_info->downshift = (e1000_downshift) hw->speed_downgraded;

	ret_val = e1000_read_phy_reg(hw, M88E1000_PHY_SPEC_CTRL, &phy_data);
	if (ret_val)
		return ret_val;

	phy_info->extended_10bt_distance =
	    ((phy_data & M88E1000_PSCR_10BT_EXT_DIST_ENABLE) >>
	     M88E1000_PSCR_10BT_EXT_DIST_ENABLE_SHIFT) ?
	    e1000_10bt_ext_dist_enable_lower :
	    e1000_10bt_ext_dist_enable_normal;

	phy_info->polarity_correction =
	    ((phy_data & M88E1000_PSCR_POLARITY_REVERSAL) >>
	     M88E1000_PSCR_POLARITY_REVERSAL_SHIFT) ?
	    e1000_polarity_reversal_disabled : e1000_polarity_reversal_enabled;

	/* Check polarity status */
	ret_val = e1000_check_polarity(hw, &polarity);
	if (ret_val)
		return ret_val;
	phy_info->cable_polarity = polarity;

	ret_val = e1000_read_phy_reg(hw, M88E1000_PHY_SPEC_STATUS, &phy_data);
	if (ret_val)
		return ret_val;

	phy_info->mdix_mode =
	    (e1000_auto_x_mode) ((phy_data & M88E1000_PSSR_MDIX) >>
				 M88E1000_PSSR_MDIX_SHIFT);

	if ((phy_data & M88E1000_PSSR_SPEED) == M88E1000_PSSR_1000MBS) {
		/* Cable Length Estimation and Local/Remote Receiver Information
		 * are only valid at 1000 Mbps.
		 */
		phy_info->cable_length =
		    (e1000_cable_length) ((phy_data &
					   M88E1000_PSSR_CABLE_LENGTH) >>
					  M88E1000_PSSR_CABLE_LENGTH_SHIFT);

		ret_val = e1000_read_phy_reg(hw, PHY_1000T_STATUS, &phy_data);
		if (ret_val)
			return ret_val;

		phy_info->local_rx = ((phy_data & SR_1000T_LOCAL_RX_STATUS) >>
				      SR_1000T_LOCAL_RX_STATUS_SHIFT) ?
		    e1000_1000t_rx_status_ok : e1000_1000t_rx_status_not_ok;
		phy_info->remote_rx = ((phy_data & SR_1000T_REMOTE_RX_STATUS) >>
				       SR_1000T_REMOTE_RX_STATUS_SHIFT) ?
		    e1000_1000t_rx_status_ok : e1000_1000t_rx_status_not_ok;

	}

	return E1000_SUCCESS;
}

/**
 * e1000_phy_get_info - request phy info
 * @hw: Struct containing variables accessed by shared code
 * @phy_info: PHY information structure
 *
 * Get PHY information from various PHY registers
 */
s32 e1000_phy_get_info(struct e1000_hw *hw, struct e1000_phy_info *phy_info)
{
	s32 ret_val;
	u16 phy_data;

	e_dbg("e1000_phy_get_info");

	phy_info->cable_length = e1000_cable_length_undefined;
	phy_info->extended_10bt_distance = e1000_10bt_ext_dist_enable_undefined;
	phy_info->cable_polarity = e1000_rev_polarity_undefined;
	phy_info->downshift = e1000_downshift_undefined;
	phy_info->polarity_correction = e1000_polarity_reversal_undefined;
	phy_info->mdix_mode = e1000_auto_x_mode_undefined;
	phy_info->local_rx = e1000_1000t_rx_status_undefined;
	phy_info->remote_rx = e1000_1000t_rx_status_undefined;

	if (hw->media_type != e1000_media_type_copper) {
		e_dbg("PHY info is only valid for copper media\n");
		return -E1000_ERR_CONFIG;
	}

	ret_val = e1000_read_phy_reg(hw, PHY_STATUS, &phy_data);
	if (ret_val)
		return ret_val;

	ret_val = e1000_read_phy_reg(hw, PHY_STATUS, &phy_data);
	if (ret_val)
		return ret_val;

	if ((phy_data & MII_SR_LINK_STATUS) != MII_SR_LINK_STATUS) {
		e_dbg("PHY info is only valid if link is up\n");
		return -E1000_ERR_CONFIG;
	}

	if (hw->phy_type == e1000_phy_igp)
		return e1000_phy_igp_get_info(hw, phy_info);
	else if ((hw->phy_type == e1000_phy_8211) ||
	         (hw->phy_type == e1000_phy_8201))
		return E1000_SUCCESS;
	else
		return e1000_phy_m88_get_info(hw, phy_info);
}

s32 e1000_validate_mdi_setting(struct e1000_hw *hw)
{
	e_dbg("e1000_validate_mdi_settings");

	if (!hw->autoneg && (hw->mdix == 0 || hw->mdix == 3)) {
		e_dbg("Invalid MDI setting detected\n");
		hw->mdix = 1;
		return -E1000_ERR_CONFIG;
	}
	return E1000_SUCCESS;
}

/**
 * e1000_init_eeprom_params - initialize sw eeprom vars
 * @hw: Struct containing variables accessed by shared code
 *
 * Sets up eeprom variables in the hw struct.  Must be called after mac_type
 * is configured.
 */
s32 e1000_init_eeprom_params(struct e1000_hw *hw)
{
	struct e1000_eeprom_info *eeprom = &hw->eeprom;
	u32 eecd = er32(EECD);
	s32 ret_val = E1000_SUCCESS;
	u16 eeprom_size;

	e_dbg("e1000_init_eeprom_params");

	switch (hw->mac_type) {
	case e1000_82542_rev2_0:
	case e1000_82542_rev2_1:
	case e1000_82543:
	case e1000_82544:
		eeprom->type = e1000_eeprom_microwire;
		eeprom->word_size = 64;
		eeprom->opcode_bits = 3;
		eeprom->address_bits = 6;
		eeprom->delay_usec = 50;
		break;
	case e1000_82540:
	case e1000_82545:
	case e1000_82545_rev_3:
	case e1000_82546:
	case e1000_82546_rev_3:
		eeprom->type = e1000_eeprom_microwire;
		eeprom->opcode_bits = 3;
		eeprom->delay_usec = 50;
		if (eecd & E1000_EECD_SIZE) {
			eeprom->word_size = 256;
			eeprom->address_bits = 8;
		} else {
			eeprom->word_size = 64;
			eeprom->address_bits = 6;
		}
		break;
	case e1000_82541:
	case e1000_82541_rev_2:
	case e1000_82547:
	case e1000_82547_rev_2:
		if (eecd & E1000_EECD_TYPE) {
			eeprom->type = e1000_eeprom_spi;
			eeprom->opcode_bits = 8;
			eeprom->delay_usec = 1;
			if (eecd & E1000_EECD_ADDR_BITS) {
				eeprom->page_size = 32;
				eeprom->address_bits = 16;
			} else {
				eeprom->page_size = 8;
				eeprom->address_bits = 8;
			}
		} else {
			eeprom->type = e1000_eeprom_microwire;
			eeprom->opcode_bits = 3;
			eeprom->delay_usec = 50;
			if (eecd & E1000_EECD_ADDR_BITS) {
				eeprom->word_size = 256;
				eeprom->address_bits = 8;
			} else {
				eeprom->word_size = 64;
				eeprom->address_bits = 6;
			}
		}
		break;
	default:
		break;
	}

	if (eeprom->type == e1000_eeprom_spi) {
		/* eeprom_size will be an enum [0..8] that maps to eeprom sizes 128B to
		 * 32KB (incremented by powers of 2).
		 */
		/* Set to default value for initial eeprom read. */
		eeprom->word_size = 64;
		ret_val = e1000_read_eeprom(hw, EEPROM_CFG, 1, &eeprom_size);
		if (ret_val)
			return ret_val;
		eeprom_size =
		    (eeprom_size & EEPROM_SIZE_MASK) >> EEPROM_SIZE_SHIFT;
		/* 256B eeprom size was not supported in earlier hardware, so we
		 * bump eeprom_size up one to ensure that "1" (which maps to 256B)
		 * is never the result used in the shifting logic below. */
		if (eeprom_size)
			eeprom_size++;

		eeprom->word_size = 1 << (eeprom_size + EEPROM_WORD_SIZE_SHIFT);
	}
	return ret_val;
}

/**
 * e1000_raise_ee_clk - Raises the EEPROM's clock input.
 * @hw: Struct containing variables accessed by shared code
 * @eecd: EECD's current value
 */
static void e1000_raise_ee_clk(struct e1000_hw *hw, u32 *eecd)
{
	/* Raise the clock input to the EEPROM (by setting the SK bit), and then
	 * wait <delay> microseconds.
	 */
	*eecd = *eecd | E1000_EECD_SK;
	ew32(EECD, *eecd);
	E1000_WRITE_FLUSH();
	udelay(hw->eeprom.delay_usec);
}

/**
 * e1000_lower_ee_clk - Lowers the EEPROM's clock input.
 * @hw: Struct containing variables accessed by shared code
 * @eecd: EECD's current value
 */
static void e1000_lower_ee_clk(struct e1000_hw *hw, u32 *eecd)
{
	/* Lower the clock input to the EEPROM (by clearing the SK bit), and then
	 * wait 50 microseconds.
	 */
	*eecd = *eecd & ~E1000_EECD_SK;
	ew32(EECD, *eecd);
	E1000_WRITE_FLUSH();
	udelay(hw->eeprom.delay_usec);
}

/**
 * e1000_shift_out_ee_bits - Shift data bits out to the EEPROM.
 * @hw: Struct containing variables accessed by shared code
 * @data: data to send to the EEPROM
 * @count: number of bits to shift out
 */
static void e1000_shift_out_ee_bits(struct e1000_hw *hw, u16 data, u16 count)
{
	struct e1000_eeprom_info *eeprom = &hw->eeprom;
	u32 eecd;
	u32 mask;

	/* We need to shift "count" bits out to the EEPROM. So, value in the
	 * "data" parameter will be shifted out to the EEPROM one bit at a time.
	 * In order to do this, "data" must be broken down into bits.
	 */
	mask = 0x01 << (count - 1);
	eecd = er32(EECD);
	if (eeprom->type == e1000_eeprom_microwire) {
		eecd &= ~E1000_EECD_DO;
	} else if (eeprom->type == e1000_eeprom_spi) {
		eecd |= E1000_EECD_DO;
	}
	do {
		/* A "1" is shifted out to the EEPROM by setting bit "DI" to a "1",
		 * and then raising and then lowering the clock (the SK bit controls
		 * the clock input to the EEPROM).  A "0" is shifted out to the EEPROM
		 * by setting "DI" to "0" and then raising and then lowering the clock.
		 */
		eecd &= ~E1000_EECD_DI;

		if (data & mask)
			eecd |= E1000_EECD_DI;

		ew32(EECD, eecd);
		E1000_WRITE_FLUSH();

		udelay(eeprom->delay_usec);

		e1000_raise_ee_clk(hw, &eecd);
		e1000_lower_ee_clk(hw, &eecd);

		mask = mask >> 1;

	} while (mask);

	/* We leave the "DI" bit set to "0" when we leave this routine. */
	eecd &= ~E1000_EECD_DI;
	ew32(EECD, eecd);
}

/**
 * e1000_shift_in_ee_bits - Shift data bits in from the EEPROM
 * @hw: Struct containing variables accessed by shared code
 * @count: number of bits to shift in
 */
static u16 e1000_shift_in_ee_bits(struct e1000_hw *hw, u16 count)
{
	u32 eecd;
	u32 i;
	u16 data;

	/* In order to read a register from the EEPROM, we need to shift 'count'
	 * bits in from the EEPROM. Bits are "shifted in" by raising the clock
	 * input to the EEPROM (setting the SK bit), and then reading the value of
	 * the "DO" bit.  During this "shifting in" process the "DI" bit should
	 * always be clear.
	 */

	eecd = er32(EECD);

	eecd &= ~(E1000_EECD_DO | E1000_EECD_DI);
	data = 0;

	for (i = 0; i < count; i++) {
		data = data << 1;
		e1000_raise_ee_clk(hw, &eecd);

		eecd = er32(EECD);

		eecd &= ~(E1000_EECD_DI);
		if (eecd & E1000_EECD_DO)
			data |= 1;

		e1000_lower_ee_clk(hw, &eecd);
	}

	return data;
}

/**
 * e1000_acquire_eeprom - Prepares EEPROM for access
 * @hw: Struct containing variables accessed by shared code
 *
 * Lowers EEPROM clock. Clears input pin. Sets the chip select pin. This
 * function should be called before issuing a command to the EEPROM.
 */
static s32 e1000_acquire_eeprom(struct e1000_hw *hw)
{
	struct e1000_eeprom_info *eeprom = &hw->eeprom;
	u32 eecd, i = 0;

	e_dbg("e1000_acquire_eeprom");

	eecd = er32(EECD);

	/* Request EEPROM Access */
	if (hw->mac_type > e1000_82544) {
		eecd |= E1000_EECD_REQ;
		ew32(EECD, eecd);
		eecd = er32(EECD);
		while ((!(eecd & E1000_EECD_GNT)) &&
		       (i < E1000_EEPROM_GRANT_ATTEMPTS)) {
			i++;
			udelay(5);
			eecd = er32(EECD);
		}
		if (!(eecd & E1000_EECD_GNT)) {
			eecd &= ~E1000_EECD_REQ;
			ew32(EECD, eecd);
			e_dbg("Could not acquire EEPROM grant\n");
			return -E1000_ERR_EEPROM;
		}
	}

	/* Setup EEPROM for Read/Write */

	if (eeprom->type == e1000_eeprom_microwire) {
		/* Clear SK and DI */
		eecd &= ~(E1000_EECD_DI | E1000_EECD_SK);
		ew32(EECD, eecd);

		/* Set CS */
		eecd |= E1000_EECD_CS;
		ew32(EECD, eecd);
	} else if (eeprom->type == e1000_eeprom_spi) {
		/* Clear SK and CS */
		eecd &= ~(E1000_EECD_CS | E1000_EECD_SK);
		ew32(EECD, eecd);
		udelay(1);
	}

	return E1000_SUCCESS;
}

/**
 * e1000_standby_eeprom - Returns EEPROM to a "standby" state
 * @hw: Struct containing variables accessed by shared code
 */
static void e1000_standby_eeprom(struct e1000_hw *hw)
{
	struct e1000_eeprom_info *eeprom = &hw->eeprom;
	u32 eecd;

	eecd = er32(EECD);

	if (eeprom->type == e1000_eeprom_microwire) {
		eecd &= ~(E1000_EECD_CS | E1000_EECD_SK);
		ew32(EECD, eecd);
		E1000_WRITE_FLUSH();
		udelay(eeprom->delay_usec);

		/* Clock high */
		eecd |= E1000_EECD_SK;
		ew32(EECD, eecd);
		E1000_WRITE_FLUSH();
		udelay(eeprom->delay_usec);

		/* Select EEPROM */
		eecd |= E1000_EECD_CS;
		ew32(EECD, eecd);
		E1000_WRITE_FLUSH();
		udelay(eeprom->delay_usec);

		/* Clock low */
		eecd &= ~E1000_EECD_SK;
		ew32(EECD, eecd);
		E1000_WRITE_FLUSH();
		udelay(eeprom->delay_usec);
	} else if (eeprom->type == e1000_eeprom_spi) {
		/* Toggle CS to flush commands */
		eecd |= E1000_EECD_CS;
		ew32(EECD, eecd);
		E1000_WRITE_FLUSH();
		udelay(eeprom->delay_usec);
		eecd &= ~E1000_EECD_CS;
		ew32(EECD, eecd);
		E1000_WRITE_FLUSH();
		udelay(eeprom->delay_usec);
	}
}

/**
 * e1000_release_eeprom - drop chip select
 * @hw: Struct containing variables accessed by shared code
 *
 * Terminates a command by inverting the EEPROM's chip select pin
 */
static void e1000_release_eeprom(struct e1000_hw *hw)
{
	u32 eecd;

	e_dbg("e1000_release_eeprom");

	eecd = er32(EECD);

	if (hw->eeprom.type == e1000_eeprom_spi) {
		eecd |= E1000_EECD_CS;	/* Pull CS high */
		eecd &= ~E1000_EECD_SK;	/* Lower SCK */

		ew32(EECD, eecd);

		udelay(hw->eeprom.delay_usec);
	} else if (hw->eeprom.type == e1000_eeprom_microwire) {
		/* cleanup eeprom */

		/* CS on Microwire is active-high */
		eecd &= ~(E1000_EECD_CS | E1000_EECD_DI);

		ew32(EECD, eecd);

		/* Rising edge of clock */
		eecd |= E1000_EECD_SK;
		ew32(EECD, eecd);
		E1000_WRITE_FLUSH();
		udelay(hw->eeprom.delay_usec);

		/* Falling edge of clock */
		eecd &= ~E1000_EECD_SK;
		ew32(EECD, eecd);
		E1000_WRITE_FLUSH();
		udelay(hw->eeprom.delay_usec);
	}

	/* Stop requesting EEPROM access */
	if (hw->mac_type > e1000_82544) {
		eecd &= ~E1000_EECD_REQ;
		ew32(EECD, eecd);
	}
}

/**
 * e1000_spi_eeprom_ready - Reads a 16 bit word from the EEPROM.
 * @hw: Struct containing variables accessed by shared code
 */
static s32 e1000_spi_eeprom_ready(struct e1000_hw *hw)
{
	u16 retry_count = 0;
	u8 spi_stat_reg;

	e_dbg("e1000_spi_eeprom_ready");

	/* Read "Status Register" repeatedly until the LSB is cleared.  The
	 * EEPROM will signal that the command has been completed by clearing
	 * bit 0 of the internal status register.  If it's not cleared within
	 * 5 milliseconds, then error out.
	 */
	retry_count = 0;
	do {
		e1000_shift_out_ee_bits(hw, EEPROM_RDSR_OPCODE_SPI,
					hw->eeprom.opcode_bits);
		spi_stat_reg = (u8) e1000_shift_in_ee_bits(hw, 8);
		if (!(spi_stat_reg & EEPROM_STATUS_RDY_SPI))
			break;

		udelay(5);
		retry_count += 5;

		e1000_standby_eeprom(hw);
	} while (retry_count < EEPROM_MAX_RETRY_SPI);

	/* ATMEL SPI write time could vary from 0-20mSec on 3.3V devices (and
	 * only 0-5mSec on 5V devices)
	 */
	if (retry_count >= EEPROM_MAX_RETRY_SPI) {
		e_dbg("SPI EEPROM Status error\n");
		return -E1000_ERR_EEPROM;
	}

	return E1000_SUCCESS;
}

/**
 * e1000_read_eeprom - Reads a 16 bit word from the EEPROM.
 * @hw: Struct containing variables accessed by shared code
 * @offset: offset of  word in the EEPROM to read
 * @data: word read from the EEPROM
 * @words: number of words to read
 */
s32 e1000_read_eeprom(struct e1000_hw *hw, u16 offset, u16 words, u16 *data)
{
	s32 ret;
	spin_lock(&e1000_eeprom_lock);
	ret = e1000_do_read_eeprom(hw, offset, words, data);
	spin_unlock(&e1000_eeprom_lock);
	return ret;
}

static s32 e1000_do_read_eeprom(struct e1000_hw *hw, u16 offset, u16 words,
				u16 *data)
{
	struct e1000_eeprom_info *eeprom = &hw->eeprom;
	u32 i = 0;

	e_dbg("e1000_read_eeprom");

	if (hw->mac_type == e1000_ce4100) {
		GBE_CONFIG_FLASH_READ(GBE_CONFIG_BASE_VIRT, offset, words,
		                      data);
		return E1000_SUCCESS;
	}

	/* If eeprom is not yet detected, do so now */
	if (eeprom->word_size == 0)
		e1000_init_eeprom_params(hw);

	/* A check for invalid values:  offset too large, too many words, and not
	 * enough words.
	 */
	if ((offset >= eeprom->word_size)
	    || (words > eeprom->word_size - offset) || (words == 0)) {
		e_dbg("\"words\" parameter out of bounds. Words = %d,"
		      "size = %d\n", offset, eeprom->word_size);
		return -E1000_ERR_EEPROM;
	}

	/* EEPROM's that don't use EERD to read require us to bit-bang the SPI
	 * directly. In this case, we need to acquire the EEPROM so that
	 * FW or other port software does not interrupt.
	 */
	/* Prepare the EEPROM for bit-bang reading */
	if (e1000_acquire_eeprom(hw) != E1000_SUCCESS)
		return -E1000_ERR_EEPROM;

	/* Set up the SPI or Microwire EEPROM for bit-bang reading.  We have
	 * acquired the EEPROM at this point, so any returns should release it */
	if (eeprom->type == e1000_eeprom_spi) {
		u16 word_in;
		u8 read_opcode = EEPROM_READ_OPCODE_SPI;

		if (e1000_spi_eeprom_ready(hw)) {
			e1000_release_eeprom(hw);
			return -E1000_ERR_EEPROM;
		}

		e1000_standby_eeprom(hw);

		/* Some SPI eeproms use the 8th address bit embedded in the opcode */
		if ((eeprom->address_bits == 8) && (offset >= 128))
			read_opcode |= EEPROM_A8_OPCODE_SPI;

		/* Send the READ command (opcode + addr)  */
		e1000_shift_out_ee_bits(hw, read_opcode, eeprom->opcode_bits);
		e1000_shift_out_ee_bits(hw, (u16) (offset * 2),
					eeprom->address_bits);

		/* Read the data.  The address of the eeprom internally increments with
		 * each byte (spi) being read, saving on the overhead of eeprom setup
		 * and tear-down.  The address counter will roll over if reading beyond
		 * the size of the eeprom, thus allowing the entire memory to be read
		 * starting from any offset. */
		for (i = 0; i < words; i++) {
			word_in = e1000_shift_in_ee_bits(hw, 16);
			data[i] = (word_in >> 8) | (word_in << 8);
		}
	} else if (eeprom->type == e1000_eeprom_microwire) {
		for (i = 0; i < words; i++) {
			/* Send the READ command (opcode + addr)  */
			e1000_shift_out_ee_bits(hw,
						EEPROM_READ_OPCODE_MICROWIRE,
						eeprom->opcode_bits);
			e1000_shift_out_ee_bits(hw, (u16) (offset + i),
						eeprom->address_bits);

			/* Read the data.  For microwire, each word requires the overhead
			 * of eeprom setup and tear-down. */
			data[i] = e1000_shift_in_ee_bits(hw, 16);
			e1000_standby_eeprom(hw);
		}
	}

	/* End this read operation */
	e1000_release_eeprom(hw);

	return E1000_SUCCESS;
}

/**
 * e1000_validate_eeprom_checksum - Verifies that the EEPROM has a valid checksum
 * @hw: Struct containing variables accessed by shared code
 *
 * Reads the first 64 16 bit words of the EEPROM and sums the values read.
 * If the the sum of the 64 16 bit words is 0xBABA, the EEPROM's checksum is
 * valid.
 */
s32 e1000_validate_eeprom_checksum(struct e1000_hw *hw)
{
	u16 checksum = 0;
	u16 i, eeprom_data;

	e_dbg("e1000_validate_eeprom_checksum");

	for (i = 0; i < (EEPROM_CHECKSUM_REG + 1); i++) {
		if (e1000_read_eeprom(hw, i, 1, &eeprom_data) < 0) {
			e_dbg("EEPROM Read Error\n");
			return -E1000_ERR_EEPROM;
		}
		checksum += eeprom_data;
	}

#ifdef CONFIG_PARISC
	/* This is a signature and not a checksum on HP c8000 */
	if ((hw->subsystem_vendor_id == 0x103C) && (eeprom_data == 0x16d6))
		return E1000_SUCCESS;

#endif
	if (checksum == (u16) EEPROM_SUM)
		return E1000_SUCCESS;
	else {
		e_dbg("EEPROM Checksum Invalid\n");
		return -E1000_ERR_EEPROM;
	}
}

/**
 * e1000_update_eeprom_checksum - Calculates/writes the EEPROM checksum
 * @hw: Struct containing variables accessed by shared code
 *
 * Sums the first 63 16 bit words of the EEPROM. Subtracts the sum from 0xBABA.
 * Writes the difference to word offset 63 of the EEPROM.
 */
s32 e1000_update_eeprom_checksum(struct e1000_hw *hw)
{
	u16 checksum = 0;
	u16 i, eeprom_data;

	e_dbg("e1000_update_eeprom_checksum");

	for (i = 0; i < EEPROM_CHECKSUM_REG; i++) {
		if (e1000_read_eeprom(hw, i, 1, &eeprom_data) < 0) {
			e_dbg("EEPROM Read Error\n");
			return -E1000_ERR_EEPROM;
		}
		checksum += eeprom_data;
	}
	checksum = (u16) EEPROM_SUM - checksum;
	if (e1000_write_eeprom(hw, EEPROM_CHECKSUM_REG, 1, &checksum) < 0) {
		e_dbg("EEPROM Write Error\n");
		return -E1000_ERR_EEPROM;
	}
	return E1000_SUCCESS;
}

/**
 * e1000_write_eeprom - write words to the different EEPROM types.
 * @hw: Struct containing variables accessed by shared code
 * @offset: offset within the EEPROM to be written to
 * @words: number of words to write
 * @data: 16 bit word to be written to the EEPROM
 *
 * If e1000_update_eeprom_checksum is not called after this function, the
 * EEPROM will most likely contain an invalid checksum.
 */
s32 e1000_write_eeprom(struct e1000_hw *hw, u16 offset, u16 words, u16 *data)
{
	s32 ret;
	spin_lock(&e1000_eeprom_lock);
	ret = e1000_do_write_eeprom(hw, offset, words, data);
	spin_unlock(&e1000_eeprom_lock);
	return ret;
}

static s32 e1000_do_write_eeprom(struct e1000_hw *hw, u16 offset, u16 words,
				 u16 *data)
{
	struct e1000_eeprom_info *eeprom = &hw->eeprom;
	s32 status = 0;

	e_dbg("e1000_write_eeprom");

	if (hw->mac_type == e1000_ce4100) {
		GBE_CONFIG_FLASH_WRITE(GBE_CONFIG_BASE_VIRT, offset, words,
		                       data);
		return E1000_SUCCESS;
	}

	/* If eeprom is not yet detected, do so now */
	if (eeprom->word_size == 0)
		e1000_init_eeprom_params(hw);

	/* A check for invalid values:  offset too large, too many words, and not
	 * enough words.
	 */
	if ((offset >= eeprom->word_size)
	    || (words > eeprom->word_size - offset) || (words == 0)) {
		e_dbg("\"words\" parameter out of bounds\n");
		return -E1000_ERR_EEPROM;
	}

	/* Prepare the EEPROM for writing  */
	if (e1000_acquire_eeprom(hw) != E1000_SUCCESS)
		return -E1000_ERR_EEPROM;

	if (eeprom->type == e1000_eeprom_microwire) {
		status = e1000_write_eeprom_microwire(hw, offset, words, data);
	} else {
		status = e1000_write_eeprom_spi(hw, offset, words, data);
		msleep(10);
	}

	/* Done with writing */
	e1000_release_eeprom(hw);

	return status;
}

/**
 * e1000_write_eeprom_spi - Writes a 16 bit word to a given offset in an SPI EEPROM.
 * @hw: Struct containing variables accessed by shared code
 * @offset: offset within the EEPROM to be written to
 * @words: number of words to write
 * @data: pointer to array of 8 bit words to be written to the EEPROM
 */
static s32 e1000_write_eeprom_spi(struct e1000_hw *hw, u16 offset, u16 words,
				  u16 *data)
{
	struct e1000_eeprom_info *eeprom = &hw->eeprom;
	u16 widx = 0;

	e_dbg("e1000_write_eeprom_spi");

	while (widx < words) {
		u8 write_opcode = EEPROM_WRITE_OPCODE_SPI;

		if (e1000_spi_eeprom_ready(hw))
			return -E1000_ERR_EEPROM;

		e1000_standby_eeprom(hw);

		/*  Send the WRITE ENABLE command (8 bit opcode )  */
		e1000_shift_out_ee_bits(hw, EEPROM_WREN_OPCODE_SPI,
					eeprom->opcode_bits);

		e1000_standby_eeprom(hw);

		/* Some SPI eeproms use the 8th address bit embedded in the opcode */
		if ((eeprom->address_bits == 8) && (offset >= 128))
			write_opcode |= EEPROM_A8_OPCODE_SPI;

		/* Send the Write command (8-bit opcode + addr) */
		e1000_shift_out_ee_bits(hw, write_opcode, eeprom->opcode_bits);

		e1000_shift_out_ee_bits(hw, (u16) ((offset + widx) * 2),
					eeprom->address_bits);

		/* Send the data */

		/* Loop to allow for up to whole page write (32 bytes) of eeprom */
		while (widx < words) {
			u16 word_out = data[widx];
			word_out = (word_out >> 8) | (word_out << 8);
			e1000_shift_out_ee_bits(hw, word_out, 16);
			widx++;

			/* Some larger eeprom sizes are capable of a 32-byte PAGE WRITE
			 * operation, while the smaller eeproms are capable of an 8-byte
			 * PAGE WRITE operation.  Break the inner loop to pass new address
			 */
			if ((((offset + widx) * 2) % eeprom->page_size) == 0) {
				e1000_standby_eeprom(hw);
				break;
			}
		}
	}

	return E1000_SUCCESS;
}

/**
 * e1000_write_eeprom_microwire - Writes a 16 bit word to a given offset in a Microwire EEPROM.
 * @hw: Struct containing variables accessed by shared code
 * @offset: offset within the EEPROM to be written to
 * @words: number of words to write
 * @data: pointer to array of 8 bit words to be written to the EEPROM
 */
static s32 e1000_write_eeprom_microwire(struct e1000_hw *hw, u16 offset,
					u16 words, u16 *data)
{
	struct e1000_eeprom_info *eeprom = &hw->eeprom;
	u32 eecd;
	u16 words_written = 0;
	u16 i = 0;

	e_dbg("e1000_write_eeprom_microwire");

	/* Send the write enable command to the EEPROM (3-bit opcode plus
	 * 6/8-bit dummy address beginning with 11).  It's less work to include
	 * the 11 of the dummy address as part of the opcode than it is to shift
	 * it over the correct number of bits for the address.  This puts the
	 * EEPROM into write/erase mode.
	 */
	e1000_shift_out_ee_bits(hw, EEPROM_EWEN_OPCODE_MICROWIRE,
				(u16) (eeprom->opcode_bits + 2));

	e1000_shift_out_ee_bits(hw, 0, (u16) (eeprom->address_bits - 2));

	/* Prepare the EEPROM */
	e1000_standby_eeprom(hw);

	while (words_written < words) {
		/* Send the Write command (3-bit opcode + addr) */
		e1000_shift_out_ee_bits(hw, EEPROM_WRITE_OPCODE_MICROWIRE,
					eeprom->opcode_bits);

		e1000_shift_out_ee_bits(hw, (u16) (offset + words_written),
					eeprom->address_bits);

		/* Send the data */
		e1000_shift_out_ee_bits(hw, data[words_written], 16);

		/* Toggle the CS line.  This in effect tells the EEPROM to execute
		 * the previous command.
		 */
		e1000_standby_eeprom(hw);

		/* Read DO repeatedly until it is high (equal to '1').  The EEPROM will
		 * signal that the command has been completed by raising the DO signal.
		 * If DO does not go high in 10 milliseconds, then error out.
		 */
		for (i = 0; i < 200; i++) {
			eecd = er32(EECD);
			if (eecd & E1000_EECD_DO)
				break;
			udelay(50);
		}
		if (i == 200) {
			e_dbg("EEPROM Write did not complete\n");
			return -E1000_ERR_EEPROM;
		}

		/* Recover from write */
		e1000_standby_eeprom(hw);

		words_written++;
	}

	/* Send the write disable command to the EEPROM (3-bit opcode plus
	 * 6/8-bit dummy address beginning with 10).  It's less work to include
	 * the 10 of the dummy address as part of the opcode than it is to shift
	 * it over the correct number of bits for the address.  This takes the
	 * EEPROM out of write/erase mode.
	 */
	e1000_shift_out_ee_bits(hw, EEPROM_EWDS_OPCODE_MICROWIRE,
				(u16) (eeprom->opcode_bits + 2));

	e1000_shift_out_ee_bits(hw, 0, (u16) (eeprom->address_bits - 2));

	return E1000_SUCCESS;
}

/**
 * e1000_read_mac_addr - read the adapters MAC from eeprom
 * @hw: Struct containing variables accessed by shared code
 *
 * Reads the adapter's MAC address from the EEPROM and inverts the LSB for the
 * second function of dual function devices
 */
s32 e1000_read_mac_addr(struct e1000_hw *hw)
{
	u16 offset;
	u16 eeprom_data, i;

	e_dbg("e1000_read_mac_addr");

	for (i = 0; i < NODE_ADDRESS_SIZE; i += 2) {
		offset = i >> 1;
		if (e1000_read_eeprom(hw, offset, 1, &eeprom_data) < 0) {
			e_dbg("EEPROM Read Error\n");
			return -E1000_ERR_EEPROM;
		}
		hw->perm_mac_addr[i] = (u8) (eeprom_data & 0x00FF);
		hw->perm_mac_addr[i + 1] = (u8) (eeprom_data >> 8);
	}

	switch (hw->mac_type) {
	default:
		break;
	case e1000_82546:
	case e1000_82546_rev_3:
		if (er32(STATUS) & E1000_STATUS_FUNC_1)
			hw->perm_mac_addr[5] ^= 0x01;
		break;
	}

	for (i = 0; i < NODE_ADDRESS_SIZE; i++)
		hw->mac_addr[i] = hw->perm_mac_addr[i];
	return E1000_SUCCESS;
}

/**
 * e1000_init_rx_addrs - Initializes receive address filters.
 * @hw: Struct containing variables accessed by shared code
 *
 * Places the MAC address in receive address register 0 and clears the rest
 * of the receive address registers. Clears the multicast table. Assumes
 * the receiver is in reset when the routine is called.
 */
static void e1000_init_rx_addrs(struct e1000_hw *hw)
{
	u32 i;
	u32 rar_num;

	e_dbg("e1000_init_rx_addrs");

	/* Setup the receive address. */
	e_dbg("Programming MAC Address into RAR[0]\n");

	e1000_rar_set(hw, hw->mac_addr, 0);

	rar_num = E1000_RAR_ENTRIES;

	/* Zero out the other 15 receive addresses. */
	e_dbg("Clearing RAR[1-15]\n");
	for (i = 1; i < rar_num; i++) {
		E1000_WRITE_REG_ARRAY(hw, RA, (i << 1), 0);
		E1000_WRITE_FLUSH();
		E1000_WRITE_REG_ARRAY(hw, RA, ((i << 1) + 1), 0);
		E1000_WRITE_FLUSH();
	}
}

/**
 * e1000_hash_mc_addr - Hashes an address to determine its location in the multicast table
 * @hw: Struct containing variables accessed by shared code
 * @mc_addr: the multicast address to hash
 */
u32 e1000_hash_mc_addr(struct e1000_hw *hw, u8 *mc_addr)
{
	u32 hash_value = 0;

	/* The portion of the address that is used for the hash table is
	 * determined by the mc_filter_type setting.
	 */
	switch (hw->mc_filter_type) {
		/* [0] [1] [2] [3] [4] [5]
		 * 01  AA  00  12  34  56
		 * LSB                 MSB
		 */
	case 0:
		/* [47:36] i.e. 0x563 for above example address */
		hash_value = ((mc_addr[4] >> 4) | (((u16) mc_addr[5]) << 4));
		break;
	case 1:
		/* [46:35] i.e. 0xAC6 for above example address */
		hash_value = ((mc_addr[4] >> 3) | (((u16) mc_addr[5]) << 5));
		break;
	case 2:
		/* [45:34] i.e. 0x5D8 for above example address */
		hash_value = ((mc_addr[4] >> 2) | (((u16) mc_addr[5]) << 6));
		break;
	case 3:
		/* [43:32] i.e. 0x634 for above example address */
		hash_value = ((mc_addr[4]) | (((u16) mc_addr[5]) << 8));
		break;
	}

	hash_value &= 0xFFF;
	return hash_value;
}

/**
 * e1000_rar_set - Puts an ethernet address into a receive address register.
 * @hw: Struct containing variables accessed by shared code
 * @addr: Address to put into receive address register
 * @index: Receive address register to write
 */
void e1000_rar_set(struct e1000_hw *hw, u8 *addr, u32 index)
{
	u32 rar_low, rar_high;

	/* HW expects these in little endian so we reverse the byte order
	 * from network order (big endian) to little endian
	 */
	rar_low = ((u32) addr[0] | ((u32) addr[1] << 8) |
		   ((u32) addr[2] << 16) | ((u32) addr[3] << 24));
	rar_high = ((u32) addr[4] | ((u32) addr[5] << 8));

	/* Disable Rx and flush all Rx frames before enabling RSS to avoid Rx
	 * unit hang.
	 *
	 * Description:
	 * If there are any Rx frames queued up or otherwise present in the HW
	 * before RSS is enabled, and then we enable RSS, the HW Rx unit will
	 * hang.  To work around this issue, we have to disable receives and
	 * flush out all Rx frames before we enable RSS. To do so, we modify we
	 * redirect all Rx traffic to manageability and then reset the HW.
	 * This flushes away Rx frames, and (since the redirections to
	 * manageability persists across resets) keeps new ones from coming in
	 * while we work.  Then, we clear the Address Valid AV bit for all MAC
	 * addresses and undo the re-direction to manageability.
	 * Now, frames are coming in again, but the MAC won't accept them, so
	 * far so good.  We now proceed to initialize RSS (if necessary) and
	 * configure the Rx unit.  Last, we re-enable the AV bits and continue
	 * on our merry way.
	 */
	switch (hw->mac_type) {
	default:
		/* Indicate to hardware the Address is Valid. */
		rar_high |= E1000_RAH_AV;
		break;
	}

	E1000_WRITE_REG_ARRAY(hw, RA, (index << 1), rar_low);
	E1000_WRITE_FLUSH();
	E1000_WRITE_REG_ARRAY(hw, RA, ((index << 1) + 1), rar_high);
	E1000_WRITE_FLUSH();
}

/**
 * e1000_write_vfta - Writes a value to the specified offset in the VLAN filter table.
 * @hw: Struct containing variables accessed by shared code
 * @offset: Offset in VLAN filer table to write
 * @value: Value to write into VLAN filter table
 */
void e1000_write_vfta(struct e1000_hw *hw, u32 offset, u32 value)
{
	u32 temp;

	if ((hw->mac_type == e1000_82544) && ((offset & 0x1) == 1)) {
		temp = E1000_READ_REG_ARRAY(hw, VFTA, (offset - 1));
		E1000_WRITE_REG_ARRAY(hw, VFTA, offset, value);
		E1000_WRITE_FLUSH();
		E1000_WRITE_REG_ARRAY(hw, VFTA, (offset - 1), temp);
		E1000_WRITE_FLUSH();
	} else {
		E1000_WRITE_REG_ARRAY(hw, VFTA, offset, value);
		E1000_WRITE_FLUSH();
	}
}

/**
 * e1000_clear_vfta - Clears the VLAN filer table
 * @hw: Struct containing variables accessed by shared code
 */
static void e1000_clear_vfta(struct e1000_hw *hw)
{
	u32 offset;
	u32 vfta_value = 0;
	u32 vfta_offset = 0;
	u32 vfta_bit_in_reg = 0;

	for (offset = 0; offset < E1000_VLAN_FILTER_TBL_SIZE; offset++) {
		/* If the offset we want to clear is the same offset of the
		 * manageability VLAN ID, then clear all bits except that of the
		 * manageability unit */
		vfta_value = (offset == vfta_offset) ? vfta_bit_in_reg : 0;
		E1000_WRITE_REG_ARRAY(hw, VFTA, offset, vfta_value);
		E1000_WRITE_FLUSH();
	}
}

static s32 e1000_id_led_init(struct e1000_hw *hw)
{
	u32 ledctl;
	const u32 ledctl_mask = 0x000000FF;
	const u32 ledctl_on = E1000_LEDCTL_MODE_LED_ON;
	const u32 ledctl_off = E1000_LEDCTL_MODE_LED_OFF;
	u16 eeprom_data, i, temp;
	const u16 led_mask = 0x0F;

	e_dbg("e1000_id_led_init");

	if (hw->mac_type < e1000_82540) {
		/* Nothing to do */
		return E1000_SUCCESS;
	}

	ledctl = er32(LEDCTL);
	hw->ledctl_default = ledctl;
	hw->ledctl_mode1 = hw->ledctl_default;
	hw->ledctl_mode2 = hw->ledctl_default;

	if (e1000_read_eeprom(hw, EEPROM_ID_LED_SETTINGS, 1, &eeprom_data) < 0) {
		e_dbg("EEPROM Read Error\n");
		return -E1000_ERR_EEPROM;
	}

	if ((eeprom_data == ID_LED_RESERVED_0000) ||
	    (eeprom_data == ID_LED_RESERVED_FFFF)) {
		eeprom_data = ID_LED_DEFAULT;
	}

	for (i = 0; i < 4; i++) {
		temp = (eeprom_data >> (i << 2)) & led_mask;
		switch (temp) {
		case ID_LED_ON1_DEF2:
		case ID_LED_ON1_ON2:
		case ID_LED_ON1_OFF2:
			hw->ledctl_mode1 &= ~(ledctl_mask << (i << 3));
			hw->ledctl_mode1 |= ledctl_on << (i << 3);
			break;
		case ID_LED_OFF1_DEF2:
		case ID_LED_OFF1_ON2:
		case ID_LED_OFF1_OFF2:
			hw->ledctl_mode1 &= ~(ledctl_mask << (i << 3));
			hw->ledctl_mode1 |= ledctl_off << (i << 3);
			break;
		default:
			/* Do nothing */
			break;
		}
		switch (temp) {
		case ID_LED_DEF1_ON2:
		case ID_LED_ON1_ON2:
		case ID_LED_OFF1_ON2:
			hw->ledctl_mode2 &= ~(ledctl_mask << (i << 3));
			hw->ledctl_mode2 |= ledctl_on << (i << 3);
			break;
		case ID_LED_DEF1_OFF2:
		case ID_LED_ON1_OFF2:
		case ID_LED_OFF1_OFF2:
			hw->ledctl_mode2 &= ~(ledctl_mask << (i << 3));
			hw->ledctl_mode2 |= ledctl_off << (i << 3);
			break;
		default:
			/* Do nothing */
			break;
		}
	}
	return E1000_SUCCESS;
}

/**
 * e1000_setup_led
 * @hw: Struct containing variables accessed by shared code
 *
 * Prepares SW controlable LED for use and saves the current state of the LED.
 */
s32 e1000_setup_led(struct e1000_hw *hw)
{
	u32 ledctl;
	s32 ret_val = E1000_SUCCESS;

	e_dbg("e1000_setup_led");

	switch (hw->mac_type) {
	case e1000_82542_rev2_0:
	case e1000_82542_rev2_1:
	case e1000_82543:
	case e1000_82544:
		/* No setup necessary */
		break;
	case e1000_82541:
	case e1000_82547:
	case e1000_82541_rev_2:
	case e1000_82547_rev_2:
		/* Turn off PHY Smart Power Down (if enabled) */
		ret_val = e1000_read_phy_reg(hw, IGP01E1000_GMII_FIFO,
					     &hw->phy_spd_default);
		if (ret_val)
			return ret_val;
		ret_val = e1000_write_phy_reg(hw, IGP01E1000_GMII_FIFO,
					      (u16) (hw->phy_spd_default &
						     ~IGP01E1000_GMII_SPD));
		if (ret_val)
			return ret_val;
		/* Fall Through */
	default:
		if (hw->media_type == e1000_media_type_fiber) {
			ledctl = er32(LEDCTL);
			/* Save current LEDCTL settings */
			hw->ledctl_default = ledctl;
			/* Turn off LED0 */
			ledctl &= ~(E1000_LEDCTL_LED0_IVRT |
				    E1000_LEDCTL_LED0_BLINK |
				    E1000_LEDCTL_LED0_MODE_MASK);
			ledctl |= (E1000_LEDCTL_MODE_LED_OFF <<
				   E1000_LEDCTL_LED0_MODE_SHIFT);
			ew32(LEDCTL, ledctl);
		} else if (hw->media_type == e1000_media_type_copper)
			ew32(LEDCTL, hw->ledctl_mode1);
		break;
	}

	return E1000_SUCCESS;
}

/**
 * e1000_cleanup_led - Restores the saved state of the SW controlable LED.
 * @hw: Struct containing variables accessed by shared code
 */
s32 e1000_cleanup_led(struct e1000_hw *hw)
{
	s32 ret_val = E1000_SUCCESS;

	e_dbg("e1000_cleanup_led");

	switch (hw->mac_type) {
	case e1000_82542_rev2_0:
	case e1000_82542_rev2_1:
	case e1000_82543:
	case e1000_82544:
		/* No cleanup necessary */
		break;
	case e1000_82541:
	case e1000_82547:
	case e1000_82541_rev_2:
	case e1000_82547_rev_2:
		/* Turn on PHY Smart Power Down (if previously enabled) */
		ret_val = e1000_write_phy_reg(hw, IGP01E1000_GMII_FIFO,
					      hw->phy_spd_default);
		if (ret_val)
			return ret_val;
		/* Fall Through */
	default:
		/* Restore LEDCTL settings */
		ew32(LEDCTL, hw->ledctl_default);
		break;
	}

	return E1000_SUCCESS;
}

/**
 * e1000_led_on - Turns on the software controllable LED
 * @hw: Struct containing variables accessed by shared code
 */
s32 e1000_led_on(struct e1000_hw *hw)
{
	u32 ctrl = er32(CTRL);

	e_dbg("e1000_led_on");

	switch (hw->mac_type) {
	case e1000_82542_rev2_0:
	case e1000_82542_rev2_1:
	case e1000_82543:
		/* Set SW Defineable Pin 0 to turn on the LED */
		ctrl |= E1000_CTRL_SWDPIN0;
		ctrl |= E1000_CTRL_SWDPIO0;
		break;
	case e1000_82544:
		if (hw->media_type == e1000_media_type_fiber) {
			/* Set SW Defineable Pin 0 to turn on the LED */
			ctrl |= E1000_CTRL_SWDPIN0;
			ctrl |= E1000_CTRL_SWDPIO0;
		} else {
			/* Clear SW Defineable Pin 0 to turn on the LED */
			ctrl &= ~E1000_CTRL_SWDPIN0;
			ctrl |= E1000_CTRL_SWDPIO0;
		}
		break;
	default:
		if (hw->media_type == e1000_media_type_fiber) {
			/* Clear SW Defineable Pin 0 to turn on the LED */
			ctrl &= ~E1000_CTRL_SWDPIN0;
			ctrl |= E1000_CTRL_SWDPIO0;
		} else if (hw->media_type == e1000_media_type_copper) {
			ew32(LEDCTL, hw->ledctl_mode2);
			return E1000_SUCCESS;
		}
		break;
	}

	ew32(CTRL, ctrl);

	return E1000_SUCCESS;
}

/**
 * e1000_led_off - Turns off the software controllable LED
 * @hw: Struct containing variables accessed by shared code
 */
s32 e1000_led_off(struct e1000_hw *hw)
{
	u32 ctrl = er32(CTRL);

	e_dbg("e1000_led_off");

	switch (hw->mac_type) {
	case e1000_82542_rev2_0:
	case e1000_82542_rev2_1:
	case e1000_82543:
		/* Clear SW Defineable Pin 0 to turn off the LED */
		ctrl &= ~E1000_CTRL_SWDPIN0;
		ctrl |= E1000_CTRL_SWDPIO0;
		break;
	case e1000_82544:
		if (hw->media_type == e1000_media_type_fiber) {
			/* Clear SW Defineable Pin 0 to turn off the LED */
			ctrl &= ~E1000_CTRL_SWDPIN0;
			ctrl |= E1000_CTRL_SWDPIO0;
		} else {
			/* Set SW Defineable Pin 0 to turn off the LED */
			ctrl |= E1000_CTRL_SWDPIN0;
			ctrl |= E1000_CTRL_SWDPIO0;
		}
		break;
	default:
		if (hw->media_type == e1000_media_type_fiber) {
			/* Set SW Defineable Pin 0 to turn off the LED */
			ctrl |= E1000_CTRL_SWDPIN0;
			ctrl |= E1000_CTRL_SWDPIO0;
		} else if (hw->media_type == e1000_media_type_copper) {
			ew32(LEDCTL, hw->ledctl_mode1);
			return E1000_SUCCESS;
		}
		break;
	}

	ew32(CTRL, ctrl);

	return E1000_SUCCESS;
}

/**
 * e1000_clear_hw_cntrs - Clears all hardware statistics counters.
 * @hw: Struct containing variables accessed by shared code
 */
static void e1000_clear_hw_cntrs(struct e1000_hw *hw)
{
	volatile u32 temp;

	temp = er32(CRCERRS);
	temp = er32(SYMERRS);
	temp = er32(MPC);
	temp = er32(SCC);
	temp = er32(ECOL);
	temp = er32(MCC);
	temp = er32(LATECOL);
	temp = er32(COLC);
	temp = er32(DC);
	temp = er32(SEC);
	temp = er32(RLEC);
	temp = er32(XONRXC);
	temp = er32(XONTXC);
	temp = er32(XOFFRXC);
	temp = er32(XOFFTXC);
	temp = er32(FCRUC);

	temp = er32(PRC64);
	temp = er32(PRC127);
	temp = er32(PRC255);
	temp = er32(PRC511);
	temp = er32(PRC1023);
	temp = er32(PRC1522);

	temp = er32(GPRC);
	temp = er32(BPRC);
	temp = er32(MPRC);
	temp = er32(GPTC);
	temp = er32(GORCL);
	temp = er32(GORCH);
	temp = er32(GOTCL);
	temp = er32(GOTCH);
	temp = er32(RNBC);
	temp = er32(RUC);
	temp = er32(RFC);
	temp = er32(ROC);
	temp = er32(RJC);
	temp = er32(TORL);
	temp = er32(TORH);
	temp = er32(TOTL);
	temp = er32(TOTH);
	temp = er32(TPR);
	temp = er32(TPT);

	temp = er32(PTC64);
	temp = er32(PTC127);
	temp = er32(PTC255);
	temp = er32(PTC511);
	temp = er32(PTC1023);
	temp = er32(PTC1522);

	temp = er32(MPTC);
	temp = er32(BPTC);

	if (hw->mac_type < e1000_82543)
		return;

	temp = er32(ALGNERRC);
	temp = er32(RXERRC);
	temp = er32(TNCRS);
	temp = er32(CEXTERR);
	temp = er32(TSCTC);
	temp = er32(TSCTFC);

	if (hw->mac_type <= e1000_82544)
		return;

	temp = er32(MGTPRC);
	temp = er32(MGTPDC);
	temp = er32(MGTPTC);
}

/**
 * e1000_reset_adaptive - Resets Adaptive IFS to its default state.
 * @hw: Struct containing variables accessed by shared code
 *
 * Call this after e1000_init_hw. You may override the IFS defaults by setting
 * hw->ifs_params_forced to true. However, you must initialize hw->
 * current_ifs_val, ifs_min_val, ifs_max_val, ifs_step_size, and ifs_ratio
 * before calling this function.
 */
void e1000_reset_adaptive(struct e1000_hw *hw)
{
	e_dbg("e1000_reset_adaptive");

	if (hw->adaptive_ifs) {
		if (!hw->ifs_params_forced) {
			hw->current_ifs_val = 0;
			hw->ifs_min_val = IFS_MIN;
			hw->ifs_max_val = IFS_MAX;
			hw->ifs_step_size = IFS_STEP;
			hw->ifs_ratio = IFS_RATIO;
		}
		hw->in_ifs_mode = false;
		ew32(AIT, 0);
	} else {
		e_dbg("Not in Adaptive IFS mode!\n");
	}
}

/**
 * e1000_update_adaptive - update adaptive IFS
 * @hw: Struct containing variables accessed by shared code
 * @tx_packets: Number of transmits since last callback
 * @total_collisions: Number of collisions since last callback
 *
 * Called during the callback/watchdog routine to update IFS value based on
 * the ratio of transmits to collisions.
 */
void e1000_update_adaptive(struct e1000_hw *hw)
{
	e_dbg("e1000_update_adaptive");

	if (hw->adaptive_ifs) {
		if ((hw->collision_delta *hw->ifs_ratio) > hw->tx_packet_delta) {
			if (hw->tx_packet_delta > MIN_NUM_XMITS) {
				hw->in_ifs_mode = true;
				if (hw->current_ifs_val < hw->ifs_max_val) {
					if (hw->current_ifs_val == 0)
						hw->current_ifs_val =
						    hw->ifs_min_val;
					else
						hw->current_ifs_val +=
						    hw->ifs_step_size;
					ew32(AIT, hw->current_ifs_val);
				}
			}
		} else {
			if (hw->in_ifs_mode
			    && (hw->tx_packet_delta <= MIN_NUM_XMITS)) {
				hw->current_ifs_val = 0;
				hw->in_ifs_mode = false;
				ew32(AIT, 0);
			}
		}
	} else {
		e_dbg("Not in Adaptive IFS mode!\n");
	}
}

/**
 * e1000_tbi_adjust_stats
 * @hw: Struct containing variables accessed by shared code
 * @frame_len: The length of the frame in question
 * @mac_addr: The Ethernet destination address of the frame in question
 *
 * Adjusts the statistic counters when a frame is accepted by TBI_ACCEPT
 */
void e1000_tbi_adjust_stats(struct e1000_hw *hw, struct e1000_hw_stats *stats,
			    u32 frame_len, u8 *mac_addr)
{
	u64 carry_bit;

	/* First adjust the frame length. */
	frame_len--;
	/* We need to adjust the statistics counters, since the hardware
	 * counters overcount this packet as a CRC error and undercount
	 * the packet as a good packet
	 */
	/* This packet should not be counted as a CRC error.    */
	stats->crcerrs--;
	/* This packet does count as a Good Packet Received.    */
	stats->gprc++;

	/* Adjust the Good Octets received counters             */
	carry_bit = 0x80000000 & stats->gorcl;
	stats->gorcl += frame_len;
	/* If the high bit of Gorcl (the low 32 bits of the Good Octets
	 * Received Count) was one before the addition,
	 * AND it is zero after, then we lost the carry out,
	 * need to add one to Gorch (Good Octets Received Count High).
	 * This could be simplified if all environments supported
	 * 64-bit integers.
	 */
	if (carry_bit && ((stats->gorcl & 0x80000000) == 0))
		stats->gorch++;
	/* Is this a broadcast or multicast?  Check broadcast first,
	 * since the test for a multicast frame will test positive on
	 * a broadcast frame.
	 */
	if ((mac_addr[0] == (u8) 0xff) && (mac_addr[1] == (u8) 0xff))
		/* Broadcast packet */
		stats->bprc++;
	else if (*mac_addr & 0x01)
		/* Multicast packet */
		stats->mprc++;

	if (frame_len == hw->max_frame_size) {
		/* In this case, the hardware has overcounted the number of
		 * oversize frames.
		 */
		if (stats->roc > 0)
			stats->roc--;
	}

	/* Adjust the bin counters when the extra byte put the frame in the
	 * wrong bin. Remember that the frame_len was adjusted above.
	 */
	if (frame_len == 64) {
		stats->prc64++;
		stats->prc127--;
	} else if (frame_len == 127) {
		stats->prc127++;
		stats->prc255--;
	} else if (frame_len == 255) {
		stats->prc255++;
		stats->prc511--;
	} else if (frame_len == 511) {
		stats->prc511++;
		stats->prc1023--;
	} else if (frame_len == 1023) {
		stats->prc1023++;
		stats->prc1522--;
	} else if (frame_len == 1522) {
		stats->prc1522++;
	}
}

/**
 * e1000_get_bus_info
 * @hw: Struct containing variables accessed by shared code
 *
 * Gets the current PCI bus type, speed, and width of the hardware
 */
void e1000_get_bus_info(struct e1000_hw *hw)
{
	u32 status;

	switch (hw->mac_type) {
	case e1000_82542_rev2_0:
	case e1000_82542_rev2_1:
		hw->bus_type = e1000_bus_type_pci;
		hw->bus_speed = e1000_bus_speed_unknown;
		hw->bus_width = e1000_bus_width_unknown;
		break;
	default:
		status = er32(STATUS);
		hw->bus_type = (status & E1000_STATUS_PCIX_MODE) ?
		    e1000_bus_type_pcix : e1000_bus_type_pci;

		if (hw->device_id == E1000_DEV_ID_82546EB_QUAD_COPPER) {
			hw->bus_speed = (hw->bus_type == e1000_bus_type_pci) ?
			    e1000_bus_speed_66 : e1000_bus_speed_120;
		} else if (hw->bus_type == e1000_bus_type_pci) {
			hw->bus_speed = (status & E1000_STATUS_PCI66) ?
			    e1000_bus_speed_66 : e1000_bus_speed_33;
		} else {
			switch (status & E1000_STATUS_PCIX_SPEED) {
			case E1000_STATUS_PCIX_SPEED_66:
				hw->bus_speed = e1000_bus_speed_66;
				break;
			case E1000_STATUS_PCIX_SPEED_100:
				hw->bus_speed = e1000_bus_speed_100;
				break;
			case E1000_STATUS_PCIX_SPEED_133:
				hw->bus_speed = e1000_bus_speed_133;
				break;
			default:
				hw->bus_speed = e1000_bus_speed_reserved;
				break;
			}
		}
		hw->bus_width = (status & E1000_STATUS_BUS64) ?
		    e1000_bus_width_64 : e1000_bus_width_32;
		break;
	}
}

/**
 * e1000_write_reg_io
 * @hw: Struct containing variables accessed by shared code
 * @offset: offset to write to
 * @value: value to write
 *
 * Writes a value to one of the devices registers using port I/O (as opposed to
 * memory mapped I/O). Only 82544 and newer devices support port I/O.
 */
static void e1000_write_reg_io(struct e1000_hw *hw, u32 offset, u32 value)
{
	unsigned long io_addr = hw->io_base;
	unsigned long io_data = hw->io_base + 4;

	e1000_io_write(hw, io_addr, offset);
	e1000_io_write(hw, io_data, value);
}

/**
 * e1000_get_cable_length - Estimates the cable length.
 * @hw: Struct containing variables accessed by shared code
 * @min_length: The estimated minimum length
 * @max_length: The estimated maximum length
 *
 * returns: - E1000_ERR_XXX
 *            E1000_SUCCESS
 *
 * This function always returns a ranged length (minimum & maximum).
 * So for M88 phy's, this function interprets the one value returned from the
 * register to the minimum and maximum range.
 * For IGP phy's, the function calculates the range by the AGC registers.
 */
static s32 e1000_get_cable_length(struct e1000_hw *hw, u16 *min_length,
				  u16 *max_length)
{
	s32 ret_val;
	u16 agc_value = 0;
	u16 i, phy_data;
	u16 cable_length;

	e_dbg("e1000_get_cable_length");

	*min_length = *max_length = 0;

	/* Use old method for Phy older than IGP */
	if (hw->phy_type == e1000_phy_m88) {

		ret_val = e1000_read_phy_reg(hw, M88E1000_PHY_SPEC_STATUS,
					     &phy_data);
		if (ret_val)
			return ret_val;
		cable_length = (phy_data & M88E1000_PSSR_CABLE_LENGTH) >>
		    M88E1000_PSSR_CABLE_LENGTH_SHIFT;

		/* Convert the enum value to ranged values */
		switch (cable_length) {
		case e1000_cable_length_50:
			*min_length = 0;
			*max_length = e1000_igp_cable_length_50;
			break;
		case e1000_cable_length_50_80:
			*min_length = e1000_igp_cable_length_50;
			*max_length = e1000_igp_cable_length_80;
			break;
		case e1000_cable_length_80_110:
			*min_length = e1000_igp_cable_length_80;
			*max_length = e1000_igp_cable_length_110;
			break;
		case e1000_cable_length_110_140:
			*min_length = e1000_igp_cable_length_110;
			*max_length = e1000_igp_cable_length_140;
			break;
		case e1000_cable_length_140:
			*min_length = e1000_igp_cable_length_140;
			*max_length = e1000_igp_cable_length_170;
			break;
		default:
			return -E1000_ERR_PHY;
			break;
		}
	} else if (hw->phy_type == e1000_phy_igp) {	/* For IGP PHY */
		u16 cur_agc_value;
		u16 min_agc_value = IGP01E1000_AGC_LENGTH_TABLE_SIZE;
		static const u16 agc_reg_array[IGP01E1000_PHY_CHANNEL_NUM] = {
		       IGP01E1000_PHY_AGC_A,
		       IGP01E1000_PHY_AGC_B,
		       IGP01E1000_PHY_AGC_C,
		       IGP01E1000_PHY_AGC_D
		};
		/* Read the AGC registers for all channels */
		for (i = 0; i < IGP01E1000_PHY_CHANNEL_NUM; i++) {

			ret_val =
			    e1000_read_phy_reg(hw, agc_reg_array[i], &phy_data);
			if (ret_val)
				return ret_val;

			cur_agc_value = phy_data >> IGP01E1000_AGC_LENGTH_SHIFT;

			/* Value bound check. */
			if ((cur_agc_value >=
			     IGP01E1000_AGC_LENGTH_TABLE_SIZE - 1)
			    || (cur_agc_value == 0))
				return -E1000_ERR_PHY;

			agc_value += cur_agc_value;

			/* Update minimal AGC value. */
			if (min_agc_value > cur_agc_value)
				min_agc_value = cur_agc_value;
		}

		/* Remove the minimal AGC result for length < 50m */
		if (agc_value <
		    IGP01E1000_PHY_CHANNEL_NUM * e1000_igp_cable_length_50) {
			agc_value -= min_agc_value;

			/* Get the average length of the remaining 3 channels */
			agc_value /= (IGP01E1000_PHY_CHANNEL_NUM - 1);
		} else {
			/* Get the average length of all the 4 channels. */
			agc_value /= IGP01E1000_PHY_CHANNEL_NUM;
		}

		/* Set the range of the calculated length. */
		*min_length = ((e1000_igp_cable_length_table[agc_value] -
				IGP01E1000_AGC_RANGE) > 0) ?
		    (e1000_igp_cable_length_table[agc_value] -
		     IGP01E1000_AGC_RANGE) : 0;
		*max_length = e1000_igp_cable_length_table[agc_value] +
		    IGP01E1000_AGC_RANGE;
	}

	return E1000_SUCCESS;
}

/**
 * e1000_check_polarity - Check the cable polarity
 * @hw: Struct containing variables accessed by shared code
 * @polarity: output parameter : 0 - Polarity is not reversed
 *                               1 - Polarity is reversed.
 *
 * returns: - E1000_ERR_XXX
 *            E1000_SUCCESS
 *
 * For phy's older than IGP, this function simply reads the polarity bit in the
 * Phy Status register.  For IGP phy's, this bit is valid only if link speed is
 * 10 Mbps.  If the link speed is 100 Mbps there is no polarity so this bit will
 * return 0.  If the link speed is 1000 Mbps the polarity status is in the
 * IGP01E1000_PHY_PCS_INIT_REG.
 */
static s32 e1000_check_polarity(struct e1000_hw *hw,
				e1000_rev_polarity *polarity)
{
	s32 ret_val;
	u16 phy_data;

	e_dbg("e1000_check_polarity");

	if (hw->phy_type == e1000_phy_m88) {
		/* return the Polarity bit in the Status register. */
		ret_val = e1000_read_phy_reg(hw, M88E1000_PHY_SPEC_STATUS,
					     &phy_data);
		if (ret_val)
			return ret_val;
		*polarity = ((phy_data & M88E1000_PSSR_REV_POLARITY) >>
			     M88E1000_PSSR_REV_POLARITY_SHIFT) ?
		    e1000_rev_polarity_reversed : e1000_rev_polarity_normal;

	} else if (hw->phy_type == e1000_phy_igp) {
		/* Read the Status register to check the speed */
		ret_val = e1000_read_phy_reg(hw, IGP01E1000_PHY_PORT_STATUS,
					     &phy_data);
		if (ret_val)
			return ret_val;

		/* If speed is 1000 Mbps, must read the IGP01E1000_PHY_PCS_INIT_REG to
		 * find the polarity status */
		if ((phy_data & IGP01E1000_PSSR_SPEED_MASK) ==
		    IGP01E1000_PSSR_SPEED_1000MBPS) {

			/* Read the GIG initialization PCS register (0x00B4) */
			ret_val =
			    e1000_read_phy_reg(hw, IGP01E1000_PHY_PCS_INIT_REG,
					       &phy_data);
			if (ret_val)
				return ret_val;

			/* Check the polarity bits */
			*polarity = (phy_data & IGP01E1000_PHY_POLARITY_MASK) ?
			    e1000_rev_polarity_reversed :
			    e1000_rev_polarity_normal;
		} else {
			/* For 10 Mbps, read the polarity bit in the status register. (for
			 * 100 Mbps this bit is always 0) */
			*polarity =
			    (phy_data & IGP01E1000_PSSR_POLARITY_REVERSED) ?
			    e1000_rev_polarity_reversed :
			    e1000_rev_polarity_normal;
		}
	}
	return E1000_SUCCESS;
}

/**
 * e1000_check_downshift - Check if Downshift occurred
 * @hw: Struct containing variables accessed by shared code
 * @downshift: output parameter : 0 - No Downshift occurred.
 *                                1 - Downshift occurred.
 *
 * returns: - E1000_ERR_XXX
 *            E1000_SUCCESS
 *
 * For phy's older than IGP, this function reads the Downshift bit in the Phy
 * Specific Status register.  For IGP phy's, it reads the Downgrade bit in the
 * Link Health register.  In IGP this bit is latched high, so the driver must
 * read it immediately after link is established.
 */
static s32 e1000_check_downshift(struct e1000_hw *hw)
{
	s32 ret_val;
	u16 phy_data;

	e_dbg("e1000_check_downshift");

	if (hw->phy_type == e1000_phy_igp) {
		ret_val = e1000_read_phy_reg(hw, IGP01E1000_PHY_LINK_HEALTH,
					     &phy_data);
		if (ret_val)
			return ret_val;

		hw->speed_downgraded =
		    (phy_data & IGP01E1000_PLHR_SS_DOWNGRADE) ? 1 : 0;
	} else if (hw->phy_type == e1000_phy_m88) {
		ret_val = e1000_read_phy_reg(hw, M88E1000_PHY_SPEC_STATUS,
					     &phy_data);
		if (ret_val)
			return ret_val;

		hw->speed_downgraded = (phy_data & M88E1000_PSSR_DOWNSHIFT) >>
		    M88E1000_PSSR_DOWNSHIFT_SHIFT;
	}

	return E1000_SUCCESS;
}

/**
 * e1000_config_dsp_after_link_change
 * @hw: Struct containing variables accessed by shared code
 * @link_up: was link up at the time this was called
 *
 * returns: - E1000_ERR_PHY if fail to read/write the PHY
 *            E1000_SUCCESS at any other case.
 *
 * 82541_rev_2 & 82547_rev_2 have the capability to configure the DSP when a
 * gigabit link is achieved to improve link quality.
 */

static s32 e1000_config_dsp_after_link_change(struct e1000_hw *hw, bool link_up)
{
	s32 ret_val;
	u16 phy_data, phy_saved_data, speed, duplex, i;
	static const u16 dsp_reg_array[IGP01E1000_PHY_CHANNEL_NUM] = {
	       IGP01E1000_PHY_AGC_PARAM_A,
	       IGP01E1000_PHY_AGC_PARAM_B,
	       IGP01E1000_PHY_AGC_PARAM_C,
	       IGP01E1000_PHY_AGC_PARAM_D
	};
	u16 min_length, max_length;

	e_dbg("e1000_config_dsp_after_link_change");

	if (hw->phy_type != e1000_phy_igp)
		return E1000_SUCCESS;

	if (link_up) {
		ret_val = e1000_get_speed_and_duplex(hw, &speed, &duplex);
		if (ret_val) {
			e_dbg("Error getting link speed and duplex\n");
			return ret_val;
		}

		if (speed == SPEED_1000) {

			ret_val =
			    e1000_get_cable_length(hw, &min_length,
						   &max_length);
			if (ret_val)
				return ret_val;

			if ((hw->dsp_config_state == e1000_dsp_config_enabled)
			    && min_length >= e1000_igp_cable_length_50) {

				for (i = 0; i < IGP01E1000_PHY_CHANNEL_NUM; i++) {
					ret_val =
					    e1000_read_phy_reg(hw,
							       dsp_reg_array[i],
							       &phy_data);
					if (ret_val)
						return ret_val;

					phy_data &=
					    ~IGP01E1000_PHY_EDAC_MU_INDEX;

					ret_val =
					    e1000_write_phy_reg(hw,
								dsp_reg_array
								[i], phy_data);
					if (ret_val)
						return ret_val;
				}
				hw->dsp_config_state =
				    e1000_dsp_config_activated;
			}

			if ((hw->ffe_config_state == e1000_ffe_config_enabled)
			    && (min_length < e1000_igp_cable_length_50)) {

				u16 ffe_idle_err_timeout =
				    FFE_IDLE_ERR_COUNT_TIMEOUT_20;
				u32 idle_errs = 0;

				/* clear previous idle error counts */
				ret_val =
				    e1000_read_phy_reg(hw, PHY_1000T_STATUS,
						       &phy_data);
				if (ret_val)
					return ret_val;

				for (i = 0; i < ffe_idle_err_timeout; i++) {
					udelay(1000);
					ret_val =
					    e1000_read_phy_reg(hw,
							       PHY_1000T_STATUS,
							       &phy_data);
					if (ret_val)
						return ret_val;

					idle_errs +=
					    (phy_data &
					     SR_1000T_IDLE_ERROR_CNT);
					if (idle_errs >
					    SR_1000T_PHY_EXCESSIVE_IDLE_ERR_COUNT)
					{
						hw->ffe_config_state =
						    e1000_ffe_config_active;

						ret_val =
						    e1000_write_phy_reg(hw,
									IGP01E1000_PHY_DSP_FFE,
									IGP01E1000_PHY_DSP_FFE_CM_CP);
						if (ret_val)
							return ret_val;
						break;
					}

					if (idle_errs)
						ffe_idle_err_timeout =
						    FFE_IDLE_ERR_COUNT_TIMEOUT_100;
				}
			}
		}
	} else {
		if (hw->dsp_config_state == e1000_dsp_config_activated) {
			/* Save off the current value of register 0x2F5B to be restored at
			 * the end of the routines. */
			ret_val =
			    e1000_read_phy_reg(hw, 0x2F5B, &phy_saved_data);

			if (ret_val)
				return ret_val;

			/* Disable the PHY transmitter */
			ret_val = e1000_write_phy_reg(hw, 0x2F5B, 0x0003);

			if (ret_val)
				return ret_val;

			mdelay(20);

			ret_val = e1000_write_phy_reg(hw, 0x0000,
						      IGP01E1000_IEEE_FORCE_GIGA);
			if (ret_val)
				return ret_val;
			for (i = 0; i < IGP01E1000_PHY_CHANNEL_NUM; i++) {
				ret_val =
				    e1000_read_phy_reg(hw, dsp_reg_array[i],
						       &phy_data);
				if (ret_val)
					return ret_val;

				phy_data &= ~IGP01E1000_PHY_EDAC_MU_INDEX;
				phy_data |= IGP01E1000_PHY_EDAC_SIGN_EXT_9_BITS;

				ret_val =
				    e1000_write_phy_reg(hw, dsp_reg_array[i],
							phy_data);
				if (ret_val)
					return ret_val;
			}

			ret_val = e1000_write_phy_reg(hw, 0x0000,
						      IGP01E1000_IEEE_RESTART_AUTONEG);
			if (ret_val)
				return ret_val;

			mdelay(20);

			/* Now enable the transmitter */
			ret_val =
			    e1000_write_phy_reg(hw, 0x2F5B, phy_saved_data);

			if (ret_val)
				return ret_val;

			hw->dsp_config_state = e1000_dsp_config_enabled;
		}

		if (hw->ffe_config_state == e1000_ffe_config_active) {
			/* Save off the current value of register 0x2F5B to be restored at
			 * the end of the routines. */
			ret_val =
			    e1000_read_phy_reg(hw, 0x2F5B, &phy_saved_data);

			if (ret_val)
				return ret_val;

			/* Disable the PHY transmitter */
			ret_val = e1000_write_phy_reg(hw, 0x2F5B, 0x0003);

			if (ret_val)
				return ret_val;

			mdelay(20);

			ret_val = e1000_write_phy_reg(hw, 0x0000,
						      IGP01E1000_IEEE_FORCE_GIGA);
			if (ret_val)
				return ret_val;
			ret_val =
			    e1000_write_phy_reg(hw, IGP01E1000_PHY_DSP_FFE,
						IGP01E1000_PHY_DSP_FFE_DEFAULT);
			if (ret_val)
				return ret_val;

			ret_val = e1000_write_phy_reg(hw, 0x0000,
						      IGP01E1000_IEEE_RESTART_AUTONEG);
			if (ret_val)
				return ret_val;

			mdelay(20);

			/* Now enable the transmitter */
			ret_val =
			    e1000_write_phy_reg(hw, 0x2F5B, phy_saved_data);

			if (ret_val)
				return ret_val;

			hw->ffe_config_state = e1000_ffe_config_enabled;
		}
	}
	return E1000_SUCCESS;
}

/**
 * e1000_set_phy_mode - Set PHY to class A mode
 * @hw: Struct containing variables accessed by shared code
 *
 * Assumes the following operations will follow to enable the new class mode.
 *  1. Do a PHY soft reset
 *  2. Restart auto-negotiation or force link.
 */
static s32 e1000_set_phy_mode(struct e1000_hw *hw)
{
	s32 ret_val;
	u16 eeprom_data;

	e_dbg("e1000_set_phy_mode");

	if ((hw->mac_type == e1000_82545_rev_3) &&
	    (hw->media_type == e1000_media_type_copper)) {
		ret_val =
		    e1000_read_eeprom(hw, EEPROM_PHY_CLASS_WORD, 1,
				      &eeprom_data);
		if (ret_val) {
			return ret_val;
		}

		if ((eeprom_data != EEPROM_RESERVED_WORD) &&
		    (eeprom_data & EEPROM_PHY_CLASS_A)) {
			ret_val =
			    e1000_write_phy_reg(hw, M88E1000_PHY_PAGE_SELECT,
						0x000B);
			if (ret_val)
				return ret_val;
			ret_val =
			    e1000_write_phy_reg(hw, M88E1000_PHY_GEN_CONTROL,
						0x8104);
			if (ret_val)
				return ret_val;

			hw->phy_reset_disable = false;
		}
	}

	return E1000_SUCCESS;
}

/**
 * e1000_set_d3_lplu_state - set d3 link power state
 * @hw: Struct containing variables accessed by shared code
 * @active: true to enable lplu false to disable lplu.
 *
 * This function sets the lplu state according to the active flag.  When
 * activating lplu this function also disables smart speed and vise versa.
 * lplu will not be activated unless the device autonegotiation advertisement
 * meets standards of either 10 or 10/100 or 10/100/1000 at all duplexes.
 *
 * returns: - E1000_ERR_PHY if fail to read/write the PHY
 *            E1000_SUCCESS at any other case.
 */
static s32 e1000_set_d3_lplu_state(struct e1000_hw *hw, bool active)
{
	s32 ret_val;
	u16 phy_data;
	e_dbg("e1000_set_d3_lplu_state");

	if (hw->phy_type != e1000_phy_igp)
		return E1000_SUCCESS;

	/* During driver activity LPLU should not be used or it will attain link
	 * from the lowest speeds starting from 10Mbps. The capability is used for
	 * Dx transitions and states */
	if (hw->mac_type == e1000_82541_rev_2
	    || hw->mac_type == e1000_82547_rev_2) {
		ret_val =
		    e1000_read_phy_reg(hw, IGP01E1000_GMII_FIFO, &phy_data);
		if (ret_val)
			return ret_val;
	}

	if (!active) {
		if (hw->mac_type == e1000_82541_rev_2 ||
		    hw->mac_type == e1000_82547_rev_2) {
			phy_data &= ~IGP01E1000_GMII_FLEX_SPD;
			ret_val =
			    e1000_write_phy_reg(hw, IGP01E1000_GMII_FIFO,
						phy_data);
			if (ret_val)
				return ret_val;
		}

		/* LPLU and SmartSpeed are mutually exclusive.  LPLU is used during
		 * Dx states where the power conservation is most important.  During
		 * driver activity we should enable SmartSpeed, so performance is
		 * maintained. */
		if (hw->smart_speed == e1000_smart_speed_on) {
			ret_val =
			    e1000_read_phy_reg(hw, IGP01E1000_PHY_PORT_CONFIG,
					       &phy_data);
			if (ret_val)
				return ret_val;

			phy_data |= IGP01E1000_PSCFR_SMART_SPEED;
			ret_val =
			    e1000_write_phy_reg(hw, IGP01E1000_PHY_PORT_CONFIG,
						phy_data);
			if (ret_val)
				return ret_val;
		} else if (hw->smart_speed == e1000_smart_speed_off) {
			ret_val =
			    e1000_read_phy_reg(hw, IGP01E1000_PHY_PORT_CONFIG,
					       &phy_data);
			if (ret_val)
				return ret_val;

			phy_data &= ~IGP01E1000_PSCFR_SMART_SPEED;
			ret_val =
			    e1000_write_phy_reg(hw, IGP01E1000_PHY_PORT_CONFIG,
						phy_data);
			if (ret_val)
				return ret_val;
		}
	} else if ((hw->autoneg_advertised == AUTONEG_ADVERTISE_SPEED_DEFAULT)
		   || (hw->autoneg_advertised == AUTONEG_ADVERTISE_10_ALL)
		   || (hw->autoneg_advertised ==
		       AUTONEG_ADVERTISE_10_100_ALL)) {

		if (hw->mac_type == e1000_82541_rev_2 ||
		    hw->mac_type == e1000_82547_rev_2) {
			phy_data |= IGP01E1000_GMII_FLEX_SPD;
			ret_val =
			    e1000_write_phy_reg(hw, IGP01E1000_GMII_FIFO,
						phy_data);
			if (ret_val)
				return ret_val;
		}

		/* When LPLU is enabled we should disable SmartSpeed */
		ret_val =
		    e1000_read_phy_reg(hw, IGP01E1000_PHY_PORT_CONFIG,
				       &phy_data);
		if (ret_val)
			return ret_val;

		phy_data &= ~IGP01E1000_PSCFR_SMART_SPEED;
		ret_val =
		    e1000_write_phy_reg(hw, IGP01E1000_PHY_PORT_CONFIG,
					phy_data);
		if (ret_val)
			return ret_val;

	}
	return E1000_SUCCESS;
}

/**
 * e1000_set_vco_speed
 * @hw: Struct containing variables accessed by shared code
 *
 * Change VCO speed register to improve Bit Error Rate performance of SERDES.
 */
static s32 e1000_set_vco_speed(struct e1000_hw *hw)
{
	s32 ret_val;
	u16 default_page = 0;
	u16 phy_data;

	e_dbg("e1000_set_vco_speed");

	switch (hw->mac_type) {
	case e1000_82545_rev_3:
	case e1000_82546_rev_3:
		break;
	default:
		return E1000_SUCCESS;
	}

	/* Set PHY register 30, page 5, bit 8 to 0 */

	ret_val =
	    e1000_read_phy_reg(hw, M88E1000_PHY_PAGE_SELECT, &default_page);
	if (ret_val)
		return ret_val;

	ret_val = e1000_write_phy_reg(hw, M88E1000_PHY_PAGE_SELECT, 0x0005);
	if (ret_val)
		return ret_val;

	ret_val = e1000_read_phy_reg(hw, M88E1000_PHY_GEN_CONTROL, &phy_data);
	if (ret_val)
		return ret_val;

	phy_data &= ~M88E1000_PHY_VCO_REG_BIT8;
	ret_val = e1000_write_phy_reg(hw, M88E1000_PHY_GEN_CONTROL, phy_data);
	if (ret_val)
		return ret_val;

	/* Set PHY register 30, page 4, bit 11 to 1 */

	ret_val = e1000_write_phy_reg(hw, M88E1000_PHY_PAGE_SELECT, 0x0004);
	if (ret_val)
		return ret_val;

	ret_val = e1000_read_phy_reg(hw, M88E1000_PHY_GEN_CONTROL, &phy_data);
	if (ret_val)
		return ret_val;

	phy_data |= M88E1000_PHY_VCO_REG_BIT11;
	ret_val = e1000_write_phy_reg(hw, M88E1000_PHY_GEN_CONTROL, phy_data);
	if (ret_val)
		return ret_val;

	ret_val =
	    e1000_write_phy_reg(hw, M88E1000_PHY_PAGE_SELECT, default_page);
	if (ret_val)
		return ret_val;

	return E1000_SUCCESS;
}


/**
 * e1000_enable_mng_pass_thru - check for bmc pass through
 * @hw: Struct containing variables accessed by shared code
 *
 * Verifies the hardware needs to allow ARPs to be processed by the host
 * returns: - true/false
 */
u32 e1000_enable_mng_pass_thru(struct e1000_hw *hw)
{
	u32 manc;

	if (hw->asf_firmware_present) {
		manc = er32(MANC);

		if (!(manc & E1000_MANC_RCV_TCO_EN) ||
		    !(manc & E1000_MANC_EN_MAC_ADDR_FILTER))
			return false;
		if ((manc & E1000_MANC_SMBUS_EN) && !(manc & E1000_MANC_ASF_EN))
			return true;
	}
	return false;
}

static s32 e1000_polarity_reversal_workaround(struct e1000_hw *hw)
{
	s32 ret_val;
	u16 mii_status_reg;
	u16 i;

	/* Polarity reversal workaround for forced 10F/10H links. */

	/* Disable the transmitter on the PHY */

	ret_val = e1000_write_phy_reg(hw, M88E1000_PHY_PAGE_SELECT, 0x0019);
	if (ret_val)
		return ret_val;
	ret_val = e1000_write_phy_reg(hw, M88E1000_PHY_GEN_CONTROL, 0xFFFF);
	if (ret_val)
		return ret_val;

	ret_val = e1000_write_phy_reg(hw, M88E1000_PHY_PAGE_SELECT, 0x0000);
	if (ret_val)
		return ret_val;

	/* This loop will early-out if the NO link condition has been met. */
	for (i = PHY_FORCE_TIME; i > 0; i--) {
		/* Read the MII Status Register and wait for Link Status bit
		 * to be clear.
		 */

		ret_val = e1000_read_phy_reg(hw, PHY_STATUS, &mii_status_reg);
		if (ret_val)
			return ret_val;

		ret_val = e1000_read_phy_reg(hw, PHY_STATUS, &mii_status_reg);
		if (ret_val)
			return ret_val;

		if ((mii_status_reg & ~MII_SR_LINK_STATUS) == 0)
			break;
		mdelay(100);
	}

	/* Recommended delay time after link has been lost */
	mdelay(1000);

	/* Now we will re-enable th transmitter on the PHY */

	ret_val = e1000_write_phy_reg(hw, M88E1000_PHY_PAGE_SELECT, 0x0019);
	if (ret_val)
		return ret_val;
	mdelay(50);
	ret_val = e1000_write_phy_reg(hw, M88E1000_PHY_GEN_CONTROL, 0xFFF0);
	if (ret_val)
		return ret_val;
	mdelay(50);
	ret_val = e1000_write_phy_reg(hw, M88E1000_PHY_GEN_CONTROL, 0xFF00);
	if (ret_val)
		return ret_val;
	mdelay(50);
	ret_val = e1000_write_phy_reg(hw, M88E1000_PHY_GEN_CONTROL, 0x0000);
	if (ret_val)
		return ret_val;

	ret_val = e1000_write_phy_reg(hw, M88E1000_PHY_PAGE_SELECT, 0x0000);
	if (ret_val)
		return ret_val;

	/* This loop will early-out if the link condition has been met. */
	for (i = PHY_FORCE_TIME; i > 0; i--) {
		/* Read the MII Status Register and wait for Link Status bit
		 * to be set.
		 */

		ret_val = e1000_read_phy_reg(hw, PHY_STATUS, &mii_status_reg);
		if (ret_val)
			return ret_val;

		ret_val = e1000_read_phy_reg(hw, PHY_STATUS, &mii_status_reg);
		if (ret_val)
			return ret_val;

		if (mii_status_reg & MII_SR_LINK_STATUS)
			break;
		mdelay(100);
	}
	return E1000_SUCCESS;
}

/**
 * e1000_get_auto_rd_done
 * @hw: Struct containing variables accessed by shared code
 *
 * Check for EEPROM Auto Read bit done.
 * returns: - E1000_ERR_RESET if fail to reset MAC
 *            E1000_SUCCESS at any other case.
 */
static s32 e1000_get_auto_rd_done(struct e1000_hw *hw)
{
	e_dbg("e1000_get_auto_rd_done");
	msleep(5);
	return E1000_SUCCESS;
}

/**
 * e1000_get_phy_cfg_done
 * @hw: Struct containing variables accessed by shared code
 *
 * Checks if the PHY configuration is done
 * returns: - E1000_ERR_RESET if fail to reset MAC
 *            E1000_SUCCESS at any other case.
 */
static s32 e1000_get_phy_cfg_done(struct e1000_hw *hw)
{
	e_dbg("e1000_get_phy_cfg_done");
	mdelay(10);
	return E1000_SUCCESS;
}
