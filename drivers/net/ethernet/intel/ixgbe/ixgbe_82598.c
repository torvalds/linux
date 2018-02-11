/*******************************************************************************

  Intel 10 Gigabit PCI Express Linux driver
  Copyright(c) 1999 - 2016 Intel Corporation.

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

*******************************************************************************/

#include <linux/pci.h>
#include <linux/delay.h>
#include <linux/sched.h>

#include "ixgbe.h"
#include "ixgbe_phy.h"

#define IXGBE_82598_MAX_TX_QUEUES 32
#define IXGBE_82598_MAX_RX_QUEUES 64
#define IXGBE_82598_RAR_ENTRIES   16
#define IXGBE_82598_MC_TBL_SIZE  128
#define IXGBE_82598_VFT_TBL_SIZE 128
#define IXGBE_82598_RX_PB_SIZE	 512

static s32 ixgbe_setup_copper_link_82598(struct ixgbe_hw *hw,
					 ixgbe_link_speed speed,
					 bool autoneg_wait_to_complete);
static s32 ixgbe_read_i2c_eeprom_82598(struct ixgbe_hw *hw, u8 byte_offset,
				       u8 *eeprom_data);

/**
 *  ixgbe_set_pcie_completion_timeout - set pci-e completion timeout
 *  @hw: pointer to the HW structure
 *
 *  The defaults for 82598 should be in the range of 50us to 50ms,
 *  however the hardware default for these parts is 500us to 1ms which is less
 *  than the 10ms recommended by the pci-e spec.  To address this we need to
 *  increase the value to either 10ms to 250ms for capability version 1 config,
 *  or 16ms to 55ms for version 2.
 **/
static void ixgbe_set_pcie_completion_timeout(struct ixgbe_hw *hw)
{
	u32 gcr = IXGBE_READ_REG(hw, IXGBE_GCR);
	u16 pcie_devctl2;

	if (ixgbe_removed(hw->hw_addr))
		return;

	/* only take action if timeout value is defaulted to 0 */
	if (gcr & IXGBE_GCR_CMPL_TMOUT_MASK)
		goto out;

	/*
	 * if capababilities version is type 1 we can write the
	 * timeout of 10ms to 250ms through the GCR register
	 */
	if (!(gcr & IXGBE_GCR_CAP_VER2)) {
		gcr |= IXGBE_GCR_CMPL_TMOUT_10ms;
		goto out;
	}

	/*
	 * for version 2 capabilities we need to write the config space
	 * directly in order to set the completion timeout value for
	 * 16ms to 55ms
	 */
	pcie_devctl2 = ixgbe_read_pci_cfg_word(hw, IXGBE_PCI_DEVICE_CONTROL2);
	pcie_devctl2 |= IXGBE_PCI_DEVICE_CONTROL2_16ms;
	ixgbe_write_pci_cfg_word(hw, IXGBE_PCI_DEVICE_CONTROL2, pcie_devctl2);
out:
	/* disable completion timeout resend */
	gcr &= ~IXGBE_GCR_CMPL_TMOUT_RESEND;
	IXGBE_WRITE_REG(hw, IXGBE_GCR, gcr);
}

static s32 ixgbe_get_invariants_82598(struct ixgbe_hw *hw)
{
	struct ixgbe_mac_info *mac = &hw->mac;

	/* Call PHY identify routine to get the phy type */
	ixgbe_identify_phy_generic(hw);

	mac->mcft_size = IXGBE_82598_MC_TBL_SIZE;
	mac->vft_size = IXGBE_82598_VFT_TBL_SIZE;
	mac->num_rar_entries = IXGBE_82598_RAR_ENTRIES;
	mac->rx_pb_size = IXGBE_82598_RX_PB_SIZE;
	mac->max_rx_queues = IXGBE_82598_MAX_RX_QUEUES;
	mac->max_tx_queues = IXGBE_82598_MAX_TX_QUEUES;
	mac->max_msix_vectors = ixgbe_get_pcie_msix_count_generic(hw);

	return 0;
}

/**
 *  ixgbe_init_phy_ops_82598 - PHY/SFP specific init
 *  @hw: pointer to hardware structure
 *
 *  Initialize any function pointers that were not able to be
 *  set during get_invariants because the PHY/SFP type was
 *  not known.  Perform the SFP init if necessary.
 *
 **/
static s32 ixgbe_init_phy_ops_82598(struct ixgbe_hw *hw)
{
	struct ixgbe_mac_info *mac = &hw->mac;
	struct ixgbe_phy_info *phy = &hw->phy;
	s32 ret_val;
	u16 list_offset, data_offset;

	/* Identify the PHY */
	phy->ops.identify(hw);

	/* Overwrite the link function pointers if copper PHY */
	if (mac->ops.get_media_type(hw) == ixgbe_media_type_copper) {
		mac->ops.setup_link = &ixgbe_setup_copper_link_82598;
		mac->ops.get_link_capabilities =
			&ixgbe_get_copper_link_capabilities_generic;
	}

	switch (hw->phy.type) {
	case ixgbe_phy_tn:
		phy->ops.setup_link = &ixgbe_setup_phy_link_tnx;
		phy->ops.check_link = &ixgbe_check_phy_link_tnx;
		break;
	case ixgbe_phy_nl:
		phy->ops.reset = &ixgbe_reset_phy_nl;

		/* Call SFP+ identify routine to get the SFP+ module type */
		ret_val = phy->ops.identify_sfp(hw);
		if (ret_val)
			return ret_val;
		if (hw->phy.sfp_type == ixgbe_sfp_type_unknown)
			return IXGBE_ERR_SFP_NOT_SUPPORTED;

		/* Check to see if SFP+ module is supported */
		ret_val = ixgbe_get_sfp_init_sequence_offsets(hw,
							    &list_offset,
							    &data_offset);
		if (ret_val)
			return IXGBE_ERR_SFP_NOT_SUPPORTED;
		break;
	default:
		break;
	}

	return 0;
}

/**
 *  ixgbe_start_hw_82598 - Prepare hardware for Tx/Rx
 *  @hw: pointer to hardware structure
 *
 *  Starts the hardware using the generic start_hw function.
 *  Disables relaxed ordering for archs other than SPARC
 *  Then set pcie completion timeout
 *
 **/
static s32 ixgbe_start_hw_82598(struct ixgbe_hw *hw)
{
	s32 ret_val;

	ret_val = ixgbe_start_hw_generic(hw);
	if (ret_val)
		return ret_val;

	/* set the completion timeout for interface */
	ixgbe_set_pcie_completion_timeout(hw);

	return 0;
}

/**
 *  ixgbe_get_link_capabilities_82598 - Determines link capabilities
 *  @hw: pointer to hardware structure
 *  @speed: pointer to link speed
 *  @autoneg: boolean auto-negotiation value
 *
 *  Determines the link capabilities by reading the AUTOC register.
 **/
static s32 ixgbe_get_link_capabilities_82598(struct ixgbe_hw *hw,
					     ixgbe_link_speed *speed,
					     bool *autoneg)
{
	u32 autoc = 0;

	/*
	 * Determine link capabilities based on the stored value of AUTOC,
	 * which represents EEPROM defaults.  If AUTOC value has not been
	 * stored, use the current register value.
	 */
	if (hw->mac.orig_link_settings_stored)
		autoc = hw->mac.orig_autoc;
	else
		autoc = IXGBE_READ_REG(hw, IXGBE_AUTOC);

	switch (autoc & IXGBE_AUTOC_LMS_MASK) {
	case IXGBE_AUTOC_LMS_1G_LINK_NO_AN:
		*speed = IXGBE_LINK_SPEED_1GB_FULL;
		*autoneg = false;
		break;

	case IXGBE_AUTOC_LMS_10G_LINK_NO_AN:
		*speed = IXGBE_LINK_SPEED_10GB_FULL;
		*autoneg = false;
		break;

	case IXGBE_AUTOC_LMS_1G_AN:
		*speed = IXGBE_LINK_SPEED_1GB_FULL;
		*autoneg = true;
		break;

	case IXGBE_AUTOC_LMS_KX4_AN:
	case IXGBE_AUTOC_LMS_KX4_AN_1G_AN:
		*speed = IXGBE_LINK_SPEED_UNKNOWN;
		if (autoc & IXGBE_AUTOC_KX4_SUPP)
			*speed |= IXGBE_LINK_SPEED_10GB_FULL;
		if (autoc & IXGBE_AUTOC_KX_SUPP)
			*speed |= IXGBE_LINK_SPEED_1GB_FULL;
		*autoneg = true;
		break;

	default:
		return IXGBE_ERR_LINK_SETUP;
	}

	return 0;
}

/**
 *  ixgbe_get_media_type_82598 - Determines media type
 *  @hw: pointer to hardware structure
 *
 *  Returns the media type (fiber, copper, backplane)
 **/
static enum ixgbe_media_type ixgbe_get_media_type_82598(struct ixgbe_hw *hw)
{
	/* Detect if there is a copper PHY attached. */
	switch (hw->phy.type) {
	case ixgbe_phy_cu_unknown:
	case ixgbe_phy_tn:
		return ixgbe_media_type_copper;

	default:
		break;
	}

	/* Media type for I82598 is based on device ID */
	switch (hw->device_id) {
	case IXGBE_DEV_ID_82598:
	case IXGBE_DEV_ID_82598_BX:
		/* Default device ID is mezzanine card KX/KX4 */
		return ixgbe_media_type_backplane;

	case IXGBE_DEV_ID_82598AF_DUAL_PORT:
	case IXGBE_DEV_ID_82598AF_SINGLE_PORT:
	case IXGBE_DEV_ID_82598_DA_DUAL_PORT:
	case IXGBE_DEV_ID_82598_SR_DUAL_PORT_EM:
	case IXGBE_DEV_ID_82598EB_XF_LR:
	case IXGBE_DEV_ID_82598EB_SFP_LOM:
		return ixgbe_media_type_fiber;

	case IXGBE_DEV_ID_82598EB_CX4:
	case IXGBE_DEV_ID_82598_CX4_DUAL_PORT:
		return ixgbe_media_type_cx4;

	case IXGBE_DEV_ID_82598AT:
	case IXGBE_DEV_ID_82598AT2:
		return ixgbe_media_type_copper;

	default:
		return ixgbe_media_type_unknown;
	}
}

/**
 *  ixgbe_fc_enable_82598 - Enable flow control
 *  @hw: pointer to hardware structure
 *
 *  Enable flow control according to the current settings.
 **/
static s32 ixgbe_fc_enable_82598(struct ixgbe_hw *hw)
{
	u32 fctrl_reg;
	u32 rmcs_reg;
	u32 reg;
	u32 fcrtl, fcrth;
	u32 link_speed = 0;
	int i;
	bool link_up;

	/* Validate the water mark configuration */
	if (!hw->fc.pause_time)
		return IXGBE_ERR_INVALID_LINK_SETTINGS;

	/* Low water mark of zero causes XOFF floods */
	for (i = 0; i < MAX_TRAFFIC_CLASS; i++) {
		if ((hw->fc.current_mode & ixgbe_fc_tx_pause) &&
		    hw->fc.high_water[i]) {
			if (!hw->fc.low_water[i] ||
			    hw->fc.low_water[i] >= hw->fc.high_water[i]) {
				hw_dbg(hw, "Invalid water mark configuration\n");
				return IXGBE_ERR_INVALID_LINK_SETTINGS;
			}
		}
	}

	/*
	 * On 82598 having Rx FC on causes resets while doing 1G
	 * so if it's on turn it off once we know link_speed. For
	 * more details see 82598 Specification update.
	 */
	hw->mac.ops.check_link(hw, &link_speed, &link_up, false);
	if (link_up && link_speed == IXGBE_LINK_SPEED_1GB_FULL) {
		switch (hw->fc.requested_mode) {
		case ixgbe_fc_full:
			hw->fc.requested_mode = ixgbe_fc_tx_pause;
			break;
		case ixgbe_fc_rx_pause:
			hw->fc.requested_mode = ixgbe_fc_none;
			break;
		default:
			/* no change */
			break;
		}
	}

	/* Negotiate the fc mode to use */
	hw->mac.ops.fc_autoneg(hw);

	/* Disable any previous flow control settings */
	fctrl_reg = IXGBE_READ_REG(hw, IXGBE_FCTRL);
	fctrl_reg &= ~(IXGBE_FCTRL_RFCE | IXGBE_FCTRL_RPFCE);

	rmcs_reg = IXGBE_READ_REG(hw, IXGBE_RMCS);
	rmcs_reg &= ~(IXGBE_RMCS_TFCE_PRIORITY | IXGBE_RMCS_TFCE_802_3X);

	/*
	 * The possible values of fc.current_mode are:
	 * 0: Flow control is completely disabled
	 * 1: Rx flow control is enabled (we can receive pause frames,
	 *    but not send pause frames).
	 * 2: Tx flow control is enabled (we can send pause frames but
	 *     we do not support receiving pause frames).
	 * 3: Both Rx and Tx flow control (symmetric) are enabled.
	 * other: Invalid.
	 */
	switch (hw->fc.current_mode) {
	case ixgbe_fc_none:
		/*
		 * Flow control is disabled by software override or autoneg.
		 * The code below will actually disable it in the HW.
		 */
		break;
	case ixgbe_fc_rx_pause:
		/*
		 * Rx Flow control is enabled and Tx Flow control is
		 * disabled by software override. Since there really
		 * isn't a way to advertise that we are capable of RX
		 * Pause ONLY, we will advertise that we support both
		 * symmetric and asymmetric Rx PAUSE.  Later, we will
		 * disable the adapter's ability to send PAUSE frames.
		 */
		fctrl_reg |= IXGBE_FCTRL_RFCE;
		break;
	case ixgbe_fc_tx_pause:
		/*
		 * Tx Flow control is enabled, and Rx Flow control is
		 * disabled by software override.
		 */
		rmcs_reg |= IXGBE_RMCS_TFCE_802_3X;
		break;
	case ixgbe_fc_full:
		/* Flow control (both Rx and Tx) is enabled by SW override. */
		fctrl_reg |= IXGBE_FCTRL_RFCE;
		rmcs_reg |= IXGBE_RMCS_TFCE_802_3X;
		break;
	default:
		hw_dbg(hw, "Flow control param set incorrectly\n");
		return IXGBE_ERR_CONFIG;
	}

	/* Set 802.3x based flow control settings. */
	fctrl_reg |= IXGBE_FCTRL_DPF;
	IXGBE_WRITE_REG(hw, IXGBE_FCTRL, fctrl_reg);
	IXGBE_WRITE_REG(hw, IXGBE_RMCS, rmcs_reg);

	/* Set up and enable Rx high/low water mark thresholds, enable XON. */
	for (i = 0; i < MAX_TRAFFIC_CLASS; i++) {
		if ((hw->fc.current_mode & ixgbe_fc_tx_pause) &&
		    hw->fc.high_water[i]) {
			fcrtl = (hw->fc.low_water[i] << 10) | IXGBE_FCRTL_XONE;
			fcrth = (hw->fc.high_water[i] << 10) | IXGBE_FCRTH_FCEN;
			IXGBE_WRITE_REG(hw, IXGBE_FCRTL(i), fcrtl);
			IXGBE_WRITE_REG(hw, IXGBE_FCRTH(i), fcrth);
		} else {
			IXGBE_WRITE_REG(hw, IXGBE_FCRTL(i), 0);
			IXGBE_WRITE_REG(hw, IXGBE_FCRTH(i), 0);
		}

	}

	/* Configure pause time (2 TCs per register) */
	reg = hw->fc.pause_time * 0x00010001;
	for (i = 0; i < (MAX_TRAFFIC_CLASS / 2); i++)
		IXGBE_WRITE_REG(hw, IXGBE_FCTTV(i), reg);

	/* Configure flow control refresh threshold value */
	IXGBE_WRITE_REG(hw, IXGBE_FCRTV, hw->fc.pause_time / 2);

	return 0;
}

/**
 *  ixgbe_start_mac_link_82598 - Configures MAC link settings
 *  @hw: pointer to hardware structure
 *  @autoneg_wait_to_complete: true when waiting for completion is needed
 *
 *  Configures link settings based on values in the ixgbe_hw struct.
 *  Restarts the link.  Performs autonegotiation if needed.
 **/
static s32 ixgbe_start_mac_link_82598(struct ixgbe_hw *hw,
				      bool autoneg_wait_to_complete)
{
	u32 autoc_reg;
	u32 links_reg;
	u32 i;
	s32 status = 0;

	/* Restart link */
	autoc_reg = IXGBE_READ_REG(hw, IXGBE_AUTOC);
	autoc_reg |= IXGBE_AUTOC_AN_RESTART;
	IXGBE_WRITE_REG(hw, IXGBE_AUTOC, autoc_reg);

	/* Only poll for autoneg to complete if specified to do so */
	if (autoneg_wait_to_complete) {
		if ((autoc_reg & IXGBE_AUTOC_LMS_MASK) ==
		     IXGBE_AUTOC_LMS_KX4_AN ||
		    (autoc_reg & IXGBE_AUTOC_LMS_MASK) ==
		     IXGBE_AUTOC_LMS_KX4_AN_1G_AN) {
			links_reg = 0; /* Just in case Autoneg time = 0 */
			for (i = 0; i < IXGBE_AUTO_NEG_TIME; i++) {
				links_reg = IXGBE_READ_REG(hw, IXGBE_LINKS);
				if (links_reg & IXGBE_LINKS_KX_AN_COMP)
					break;
				msleep(100);
			}
			if (!(links_reg & IXGBE_LINKS_KX_AN_COMP)) {
				status = IXGBE_ERR_AUTONEG_NOT_COMPLETE;
				hw_dbg(hw, "Autonegotiation did not complete.\n");
			}
		}
	}

	/* Add delay to filter out noises during initial link setup */
	msleep(50);

	return status;
}

/**
 *  ixgbe_validate_link_ready - Function looks for phy link
 *  @hw: pointer to hardware structure
 *
 *  Function indicates success when phy link is available. If phy is not ready
 *  within 5 seconds of MAC indicating link, the function returns error.
 **/
static s32 ixgbe_validate_link_ready(struct ixgbe_hw *hw)
{
	u32 timeout;
	u16 an_reg;

	if (hw->device_id != IXGBE_DEV_ID_82598AT2)
		return 0;

	for (timeout = 0;
	     timeout < IXGBE_VALIDATE_LINK_READY_TIMEOUT; timeout++) {
		hw->phy.ops.read_reg(hw, MDIO_STAT1, MDIO_MMD_AN, &an_reg);

		if ((an_reg & MDIO_AN_STAT1_COMPLETE) &&
		    (an_reg & MDIO_STAT1_LSTATUS))
			break;

		msleep(100);
	}

	if (timeout == IXGBE_VALIDATE_LINK_READY_TIMEOUT) {
		hw_dbg(hw, "Link was indicated but link is down\n");
		return IXGBE_ERR_LINK_SETUP;
	}

	return 0;
}

/**
 *  ixgbe_check_mac_link_82598 - Get link/speed status
 *  @hw: pointer to hardware structure
 *  @speed: pointer to link speed
 *  @link_up: true is link is up, false otherwise
 *  @link_up_wait_to_complete: bool used to wait for link up or not
 *
 *  Reads the links register to determine if link is up and the current speed
 **/
static s32 ixgbe_check_mac_link_82598(struct ixgbe_hw *hw,
				      ixgbe_link_speed *speed, bool *link_up,
				      bool link_up_wait_to_complete)
{
	u32 links_reg;
	u32 i;
	u16 link_reg, adapt_comp_reg;

	/*
	 * SERDES PHY requires us to read link status from register 0xC79F.
	 * Bit 0 set indicates link is up/ready; clear indicates link down.
	 * 0xC00C is read to check that the XAUI lanes are active.  Bit 0
	 * clear indicates active; set indicates inactive.
	 */
	if (hw->phy.type == ixgbe_phy_nl) {
		hw->phy.ops.read_reg(hw, 0xC79F, MDIO_MMD_PMAPMD, &link_reg);
		hw->phy.ops.read_reg(hw, 0xC79F, MDIO_MMD_PMAPMD, &link_reg);
		hw->phy.ops.read_reg(hw, 0xC00C, MDIO_MMD_PMAPMD,
				     &adapt_comp_reg);
		if (link_up_wait_to_complete) {
			for (i = 0; i < IXGBE_LINK_UP_TIME; i++) {
				if ((link_reg & 1) &&
				    ((adapt_comp_reg & 1) == 0)) {
					*link_up = true;
					break;
				} else {
					*link_up = false;
				}
				msleep(100);
				hw->phy.ops.read_reg(hw, 0xC79F,
						     MDIO_MMD_PMAPMD,
						     &link_reg);
				hw->phy.ops.read_reg(hw, 0xC00C,
						     MDIO_MMD_PMAPMD,
						     &adapt_comp_reg);
			}
		} else {
			if ((link_reg & 1) && ((adapt_comp_reg & 1) == 0))
				*link_up = true;
			else
				*link_up = false;
		}

		if (!*link_up)
			return 0;
	}

	links_reg = IXGBE_READ_REG(hw, IXGBE_LINKS);
	if (link_up_wait_to_complete) {
		for (i = 0; i < IXGBE_LINK_UP_TIME; i++) {
			if (links_reg & IXGBE_LINKS_UP) {
				*link_up = true;
				break;
			} else {
				*link_up = false;
			}
			msleep(100);
			links_reg = IXGBE_READ_REG(hw, IXGBE_LINKS);
		}
	} else {
		if (links_reg & IXGBE_LINKS_UP)
			*link_up = true;
		else
			*link_up = false;
	}

	if (links_reg & IXGBE_LINKS_SPEED)
		*speed = IXGBE_LINK_SPEED_10GB_FULL;
	else
		*speed = IXGBE_LINK_SPEED_1GB_FULL;

	if ((hw->device_id == IXGBE_DEV_ID_82598AT2) && *link_up &&
	    (ixgbe_validate_link_ready(hw) != 0))
		*link_up = false;

	return 0;
}

/**
 *  ixgbe_setup_mac_link_82598 - Set MAC link speed
 *  @hw: pointer to hardware structure
 *  @speed: new link speed
 *  @autoneg_wait_to_complete: true when waiting for completion is needed
 *
 *  Set the link speed in the AUTOC register and restarts link.
 **/
static s32 ixgbe_setup_mac_link_82598(struct ixgbe_hw *hw,
				      ixgbe_link_speed speed,
				      bool autoneg_wait_to_complete)
{
	bool		 autoneg	   = false;
	ixgbe_link_speed link_capabilities = IXGBE_LINK_SPEED_UNKNOWN;
	u32              curr_autoc        = IXGBE_READ_REG(hw, IXGBE_AUTOC);
	u32              autoc             = curr_autoc;
	u32              link_mode         = autoc & IXGBE_AUTOC_LMS_MASK;

	/* Check to see if speed passed in is supported. */
	ixgbe_get_link_capabilities_82598(hw, &link_capabilities, &autoneg);
	speed &= link_capabilities;

	if (speed == IXGBE_LINK_SPEED_UNKNOWN)
		return IXGBE_ERR_LINK_SETUP;

	/* Set KX4/KX support according to speed requested */
	else if (link_mode == IXGBE_AUTOC_LMS_KX4_AN ||
		 link_mode == IXGBE_AUTOC_LMS_KX4_AN_1G_AN) {
		autoc &= ~IXGBE_AUTOC_KX4_KX_SUPP_MASK;
		if (speed & IXGBE_LINK_SPEED_10GB_FULL)
			autoc |= IXGBE_AUTOC_KX4_SUPP;
		if (speed & IXGBE_LINK_SPEED_1GB_FULL)
			autoc |= IXGBE_AUTOC_KX_SUPP;
		if (autoc != curr_autoc)
			IXGBE_WRITE_REG(hw, IXGBE_AUTOC, autoc);
	}

	/* Setup and restart the link based on the new values in
	 * ixgbe_hw This will write the AUTOC register based on the new
	 * stored values
	 */
	return ixgbe_start_mac_link_82598(hw, autoneg_wait_to_complete);
}


/**
 *  ixgbe_setup_copper_link_82598 - Set the PHY autoneg advertised field
 *  @hw: pointer to hardware structure
 *  @speed: new link speed
 *  @autoneg_wait_to_complete: true if waiting is needed to complete
 *
 *  Sets the link speed in the AUTOC register in the MAC and restarts link.
 **/
static s32 ixgbe_setup_copper_link_82598(struct ixgbe_hw *hw,
					       ixgbe_link_speed speed,
					       bool autoneg_wait_to_complete)
{
	s32 status;

	/* Setup the PHY according to input speed */
	status = hw->phy.ops.setup_link_speed(hw, speed,
					      autoneg_wait_to_complete);
	/* Set up MAC */
	ixgbe_start_mac_link_82598(hw, autoneg_wait_to_complete);

	return status;
}

/**
 *  ixgbe_reset_hw_82598 - Performs hardware reset
 *  @hw: pointer to hardware structure
 *
 *  Resets the hardware by resetting the transmit and receive units, masks and
 *  clears all interrupts, performing a PHY reset, and performing a link (MAC)
 *  reset.
 **/
static s32 ixgbe_reset_hw_82598(struct ixgbe_hw *hw)
{
	s32 status;
	s32 phy_status = 0;
	u32 ctrl;
	u32 gheccr;
	u32 i;
	u32 autoc;
	u8  analog_val;

	/* Call adapter stop to disable tx/rx and clear interrupts */
	status = hw->mac.ops.stop_adapter(hw);
	if (status)
		return status;

	/*
	 * Power up the Atlas Tx lanes if they are currently powered down.
	 * Atlas Tx lanes are powered down for MAC loopback tests, but
	 * they are not automatically restored on reset.
	 */
	hw->mac.ops.read_analog_reg8(hw, IXGBE_ATLAS_PDN_LPBK, &analog_val);
	if (analog_val & IXGBE_ATLAS_PDN_TX_REG_EN) {
		/* Enable Tx Atlas so packets can be transmitted again */
		hw->mac.ops.read_analog_reg8(hw, IXGBE_ATLAS_PDN_LPBK,
					     &analog_val);
		analog_val &= ~IXGBE_ATLAS_PDN_TX_REG_EN;
		hw->mac.ops.write_analog_reg8(hw, IXGBE_ATLAS_PDN_LPBK,
					      analog_val);

		hw->mac.ops.read_analog_reg8(hw, IXGBE_ATLAS_PDN_10G,
					     &analog_val);
		analog_val &= ~IXGBE_ATLAS_PDN_TX_10G_QL_ALL;
		hw->mac.ops.write_analog_reg8(hw, IXGBE_ATLAS_PDN_10G,
					      analog_val);

		hw->mac.ops.read_analog_reg8(hw, IXGBE_ATLAS_PDN_1G,
					     &analog_val);
		analog_val &= ~IXGBE_ATLAS_PDN_TX_1G_QL_ALL;
		hw->mac.ops.write_analog_reg8(hw, IXGBE_ATLAS_PDN_1G,
					      analog_val);

		hw->mac.ops.read_analog_reg8(hw, IXGBE_ATLAS_PDN_AN,
					     &analog_val);
		analog_val &= ~IXGBE_ATLAS_PDN_TX_AN_QL_ALL;
		hw->mac.ops.write_analog_reg8(hw, IXGBE_ATLAS_PDN_AN,
					      analog_val);
	}

	/* Reset PHY */
	if (hw->phy.reset_disable == false) {
		/* PHY ops must be identified and initialized prior to reset */

		/* Init PHY and function pointers, perform SFP setup */
		phy_status = hw->phy.ops.init(hw);
		if (phy_status == IXGBE_ERR_SFP_NOT_SUPPORTED)
			return phy_status;
		if (phy_status == IXGBE_ERR_SFP_NOT_PRESENT)
			goto mac_reset_top;

		hw->phy.ops.reset(hw);
	}

mac_reset_top:
	/*
	 * Issue global reset to the MAC.  This needs to be a SW reset.
	 * If link reset is used, it might reset the MAC when mng is using it
	 */
	ctrl = IXGBE_READ_REG(hw, IXGBE_CTRL) | IXGBE_CTRL_RST;
	IXGBE_WRITE_REG(hw, IXGBE_CTRL, ctrl);
	IXGBE_WRITE_FLUSH(hw);
	usleep_range(1000, 1200);

	/* Poll for reset bit to self-clear indicating reset is complete */
	for (i = 0; i < 10; i++) {
		ctrl = IXGBE_READ_REG(hw, IXGBE_CTRL);
		if (!(ctrl & IXGBE_CTRL_RST))
			break;
		udelay(1);
	}
	if (ctrl & IXGBE_CTRL_RST) {
		status = IXGBE_ERR_RESET_FAILED;
		hw_dbg(hw, "Reset polling failed to complete.\n");
	}

	msleep(50);

	/*
	 * Double resets are required for recovery from certain error
	 * conditions.  Between resets, it is necessary to stall to allow time
	 * for any pending HW events to complete.
	 */
	if (hw->mac.flags & IXGBE_FLAGS_DOUBLE_RESET_REQUIRED) {
		hw->mac.flags &= ~IXGBE_FLAGS_DOUBLE_RESET_REQUIRED;
		goto mac_reset_top;
	}

	gheccr = IXGBE_READ_REG(hw, IXGBE_GHECCR);
	gheccr &= ~(BIT(21) | BIT(18) | BIT(9) | BIT(6));
	IXGBE_WRITE_REG(hw, IXGBE_GHECCR, gheccr);

	/*
	 * Store the original AUTOC value if it has not been
	 * stored off yet.  Otherwise restore the stored original
	 * AUTOC value since the reset operation sets back to deaults.
	 */
	autoc = IXGBE_READ_REG(hw, IXGBE_AUTOC);
	if (hw->mac.orig_link_settings_stored == false) {
		hw->mac.orig_autoc = autoc;
		hw->mac.orig_link_settings_stored = true;
	} else if (autoc != hw->mac.orig_autoc) {
		IXGBE_WRITE_REG(hw, IXGBE_AUTOC, hw->mac.orig_autoc);
	}

	/* Store the permanent mac address */
	hw->mac.ops.get_mac_addr(hw, hw->mac.perm_addr);

	/*
	 * Store MAC address from RAR0, clear receive address registers, and
	 * clear the multicast table
	 */
	hw->mac.ops.init_rx_addrs(hw);

	if (phy_status)
		status = phy_status;

	return status;
}

/**
 *  ixgbe_set_vmdq_82598 - Associate a VMDq set index with a rx address
 *  @hw: pointer to hardware struct
 *  @rar: receive address register index to associate with a VMDq index
 *  @vmdq: VMDq set index
 **/
static s32 ixgbe_set_vmdq_82598(struct ixgbe_hw *hw, u32 rar, u32 vmdq)
{
	u32 rar_high;
	u32 rar_entries = hw->mac.num_rar_entries;

	/* Make sure we are using a valid rar index range */
	if (rar >= rar_entries) {
		hw_dbg(hw, "RAR index %d is out of range.\n", rar);
		return IXGBE_ERR_INVALID_ARGUMENT;
	}

	rar_high = IXGBE_READ_REG(hw, IXGBE_RAH(rar));
	rar_high &= ~IXGBE_RAH_VIND_MASK;
	rar_high |= ((vmdq << IXGBE_RAH_VIND_SHIFT) & IXGBE_RAH_VIND_MASK);
	IXGBE_WRITE_REG(hw, IXGBE_RAH(rar), rar_high);
	return 0;
}

/**
 *  ixgbe_clear_vmdq_82598 - Disassociate a VMDq set index from an rx address
 *  @hw: pointer to hardware struct
 *  @rar: receive address register index to associate with a VMDq index
 *  @vmdq: VMDq clear index (not used in 82598, but elsewhere)
 **/
static s32 ixgbe_clear_vmdq_82598(struct ixgbe_hw *hw, u32 rar, u32 vmdq)
{
	u32 rar_high;
	u32 rar_entries = hw->mac.num_rar_entries;


	/* Make sure we are using a valid rar index range */
	if (rar >= rar_entries) {
		hw_dbg(hw, "RAR index %d is out of range.\n", rar);
		return IXGBE_ERR_INVALID_ARGUMENT;
	}

	rar_high = IXGBE_READ_REG(hw, IXGBE_RAH(rar));
	if (rar_high & IXGBE_RAH_VIND_MASK) {
		rar_high &= ~IXGBE_RAH_VIND_MASK;
		IXGBE_WRITE_REG(hw, IXGBE_RAH(rar), rar_high);
	}

	return 0;
}

/**
 *  ixgbe_set_vfta_82598 - Set VLAN filter table
 *  @hw: pointer to hardware structure
 *  @vlan: VLAN id to write to VLAN filter
 *  @vind: VMDq output index that maps queue to VLAN id in VFTA
 *  @vlan_on: boolean flag to turn on/off VLAN in VFTA
 *  @vlvf_bypass: boolean flag - unused
 *
 *  Turn on/off specified VLAN in the VLAN filter table.
 **/
static s32 ixgbe_set_vfta_82598(struct ixgbe_hw *hw, u32 vlan, u32 vind,
				bool vlan_on, bool vlvf_bypass)
{
	u32 regindex;
	u32 bitindex;
	u32 bits;
	u32 vftabyte;

	if (vlan > 4095)
		return IXGBE_ERR_PARAM;

	/* Determine 32-bit word position in array */
	regindex = (vlan >> 5) & 0x7F;   /* upper seven bits */

	/* Determine the location of the (VMD) queue index */
	vftabyte =  ((vlan >> 3) & 0x03); /* bits (4:3) indicating byte array */
	bitindex = (vlan & 0x7) << 2;    /* lower 3 bits indicate nibble */

	/* Set the nibble for VMD queue index */
	bits = IXGBE_READ_REG(hw, IXGBE_VFTAVIND(vftabyte, regindex));
	bits &= (~(0x0F << bitindex));
	bits |= (vind << bitindex);
	IXGBE_WRITE_REG(hw, IXGBE_VFTAVIND(vftabyte, regindex), bits);

	/* Determine the location of the bit for this VLAN id */
	bitindex = vlan & 0x1F;   /* lower five bits */

	bits = IXGBE_READ_REG(hw, IXGBE_VFTA(regindex));
	if (vlan_on)
		/* Turn on this VLAN id */
		bits |= BIT(bitindex);
	else
		/* Turn off this VLAN id */
		bits &= ~BIT(bitindex);
	IXGBE_WRITE_REG(hw, IXGBE_VFTA(regindex), bits);

	return 0;
}

/**
 *  ixgbe_clear_vfta_82598 - Clear VLAN filter table
 *  @hw: pointer to hardware structure
 *
 *  Clears the VLAN filer table, and the VMDq index associated with the filter
 **/
static s32 ixgbe_clear_vfta_82598(struct ixgbe_hw *hw)
{
	u32 offset;
	u32 vlanbyte;

	for (offset = 0; offset < hw->mac.vft_size; offset++)
		IXGBE_WRITE_REG(hw, IXGBE_VFTA(offset), 0);

	for (vlanbyte = 0; vlanbyte < 4; vlanbyte++)
		for (offset = 0; offset < hw->mac.vft_size; offset++)
			IXGBE_WRITE_REG(hw, IXGBE_VFTAVIND(vlanbyte, offset),
					0);

	return 0;
}

/**
 *  ixgbe_read_analog_reg8_82598 - Reads 8 bit Atlas analog register
 *  @hw: pointer to hardware structure
 *  @reg: analog register to read
 *  @val: read value
 *
 *  Performs read operation to Atlas analog register specified.
 **/
static s32 ixgbe_read_analog_reg8_82598(struct ixgbe_hw *hw, u32 reg, u8 *val)
{
	u32  atlas_ctl;

	IXGBE_WRITE_REG(hw, IXGBE_ATLASCTL,
			IXGBE_ATLASCTL_WRITE_CMD | (reg << 8));
	IXGBE_WRITE_FLUSH(hw);
	udelay(10);
	atlas_ctl = IXGBE_READ_REG(hw, IXGBE_ATLASCTL);
	*val = (u8)atlas_ctl;

	return 0;
}

/**
 *  ixgbe_write_analog_reg8_82598 - Writes 8 bit Atlas analog register
 *  @hw: pointer to hardware structure
 *  @reg: atlas register to write
 *  @val: value to write
 *
 *  Performs write operation to Atlas analog register specified.
 **/
static s32 ixgbe_write_analog_reg8_82598(struct ixgbe_hw *hw, u32 reg, u8 val)
{
	u32  atlas_ctl;

	atlas_ctl = (reg << 8) | val;
	IXGBE_WRITE_REG(hw, IXGBE_ATLASCTL, atlas_ctl);
	IXGBE_WRITE_FLUSH(hw);
	udelay(10);

	return 0;
}

/**
 *  ixgbe_read_i2c_phy_82598 - Reads 8 bit word over I2C interface.
 *  @hw: pointer to hardware structure
 *  @dev_addr: address to read from
 *  @byte_offset: byte offset to read from dev_addr
 *  @eeprom_data: value read
 *
 *  Performs 8 byte read operation to SFP module's data over I2C interface.
 **/
static s32 ixgbe_read_i2c_phy_82598(struct ixgbe_hw *hw, u8 dev_addr,
				    u8 byte_offset, u8 *eeprom_data)
{
	s32 status = 0;
	u16 sfp_addr = 0;
	u16 sfp_data = 0;
	u16 sfp_stat = 0;
	u16 gssr;
	u32 i;

	if (IXGBE_READ_REG(hw, IXGBE_STATUS) & IXGBE_STATUS_LAN_ID_1)
		gssr = IXGBE_GSSR_PHY1_SM;
	else
		gssr = IXGBE_GSSR_PHY0_SM;

	if (hw->mac.ops.acquire_swfw_sync(hw, gssr) != 0)
		return IXGBE_ERR_SWFW_SYNC;

	if (hw->phy.type == ixgbe_phy_nl) {
		/*
		 * phy SDA/SCL registers are at addresses 0xC30A to
		 * 0xC30D.  These registers are used to talk to the SFP+
		 * module's EEPROM through the SDA/SCL (I2C) interface.
		 */
		sfp_addr = (dev_addr << 8) + byte_offset;
		sfp_addr = (sfp_addr | IXGBE_I2C_EEPROM_READ_MASK);
		hw->phy.ops.write_reg_mdi(hw,
					  IXGBE_MDIO_PMA_PMD_SDA_SCL_ADDR,
					  MDIO_MMD_PMAPMD,
					  sfp_addr);

		/* Poll status */
		for (i = 0; i < 100; i++) {
			hw->phy.ops.read_reg_mdi(hw,
						IXGBE_MDIO_PMA_PMD_SDA_SCL_STAT,
						MDIO_MMD_PMAPMD,
						&sfp_stat);
			sfp_stat = sfp_stat & IXGBE_I2C_EEPROM_STATUS_MASK;
			if (sfp_stat != IXGBE_I2C_EEPROM_STATUS_IN_PROGRESS)
				break;
			usleep_range(10000, 20000);
		}

		if (sfp_stat != IXGBE_I2C_EEPROM_STATUS_PASS) {
			hw_dbg(hw, "EEPROM read did not pass.\n");
			status = IXGBE_ERR_SFP_NOT_PRESENT;
			goto out;
		}

		/* Read data */
		hw->phy.ops.read_reg_mdi(hw, IXGBE_MDIO_PMA_PMD_SDA_SCL_DATA,
					MDIO_MMD_PMAPMD, &sfp_data);

		*eeprom_data = (u8)(sfp_data >> 8);
	} else {
		status = IXGBE_ERR_PHY;
	}

out:
	hw->mac.ops.release_swfw_sync(hw, gssr);
	return status;
}

/**
 *  ixgbe_read_i2c_eeprom_82598 - Reads 8 bit word over I2C interface.
 *  @hw: pointer to hardware structure
 *  @byte_offset: EEPROM byte offset to read
 *  @eeprom_data: value read
 *
 *  Performs 8 byte read operation to SFP module's EEPROM over I2C interface.
 **/
static s32 ixgbe_read_i2c_eeprom_82598(struct ixgbe_hw *hw, u8 byte_offset,
				       u8 *eeprom_data)
{
	return ixgbe_read_i2c_phy_82598(hw, IXGBE_I2C_EEPROM_DEV_ADDR,
					byte_offset, eeprom_data);
}

/**
 *  ixgbe_read_i2c_sff8472_82598 - Reads 8 bit word over I2C interface.
 *  @hw: pointer to hardware structure
 *  @byte_offset: byte offset at address 0xA2
 *  @sff8472_data: value read
 *
 *  Performs 8 byte read operation to SFP module's SFF-8472 data over I2C
 **/
static s32 ixgbe_read_i2c_sff8472_82598(struct ixgbe_hw *hw, u8 byte_offset,
				       u8 *sff8472_data)
{
	return ixgbe_read_i2c_phy_82598(hw, IXGBE_I2C_EEPROM_DEV_ADDR2,
					byte_offset, sff8472_data);
}

/**
 *  ixgbe_set_lan_id_multi_port_pcie_82598 - Set LAN id for PCIe multiple
 *  port devices.
 *  @hw: pointer to the HW structure
 *
 *  Calls common function and corrects issue with some single port devices
 *  that enable LAN1 but not LAN0.
 **/
static void ixgbe_set_lan_id_multi_port_pcie_82598(struct ixgbe_hw *hw)
{
	struct ixgbe_bus_info *bus = &hw->bus;
	u16 pci_gen = 0;
	u16 pci_ctrl2 = 0;

	ixgbe_set_lan_id_multi_port_pcie(hw);

	/* check if LAN0 is disabled */
	hw->eeprom.ops.read(hw, IXGBE_PCIE_GENERAL_PTR, &pci_gen);
	if ((pci_gen != 0) && (pci_gen != 0xFFFF)) {

		hw->eeprom.ops.read(hw, pci_gen + IXGBE_PCIE_CTRL2, &pci_ctrl2);

		/* if LAN0 is completely disabled force function to 0 */
		if ((pci_ctrl2 & IXGBE_PCIE_CTRL2_LAN_DISABLE) &&
		    !(pci_ctrl2 & IXGBE_PCIE_CTRL2_DISABLE_SELECT) &&
		    !(pci_ctrl2 & IXGBE_PCIE_CTRL2_DUMMY_ENABLE)) {

			bus->func = 0;
		}
	}
}

/**
 * ixgbe_set_rxpba_82598 - Initialize RX packet buffer
 * @hw: pointer to hardware structure
 * @num_pb: number of packet buffers to allocate
 * @headroom: reserve n KB of headroom
 * @strategy: packet buffer allocation strategy
 **/
static void ixgbe_set_rxpba_82598(struct ixgbe_hw *hw, int num_pb,
				  u32 headroom, int strategy)
{
	u32 rxpktsize = IXGBE_RXPBSIZE_64KB;
	u8  i = 0;

	if (!num_pb)
		return;

	/* Setup Rx packet buffer sizes */
	switch (strategy) {
	case PBA_STRATEGY_WEIGHTED:
		/* Setup the first four at 80KB */
		rxpktsize = IXGBE_RXPBSIZE_80KB;
		for (; i < 4; i++)
			IXGBE_WRITE_REG(hw, IXGBE_RXPBSIZE(i), rxpktsize);
		/* Setup the last four at 48KB...don't re-init i */
		rxpktsize = IXGBE_RXPBSIZE_48KB;
		/* Fall Through */
	case PBA_STRATEGY_EQUAL:
	default:
		/* Divide the remaining Rx packet buffer evenly among the TCs */
		for (; i < IXGBE_MAX_PACKET_BUFFERS; i++)
			IXGBE_WRITE_REG(hw, IXGBE_RXPBSIZE(i), rxpktsize);
		break;
	}

	/* Setup Tx packet buffer sizes */
	for (i = 0; i < IXGBE_MAX_PACKET_BUFFERS; i++)
		IXGBE_WRITE_REG(hw, IXGBE_TXPBSIZE(i), IXGBE_TXPBSIZE_40KB);
}

static const struct ixgbe_mac_operations mac_ops_82598 = {
	.init_hw		= &ixgbe_init_hw_generic,
	.reset_hw		= &ixgbe_reset_hw_82598,
	.start_hw		= &ixgbe_start_hw_82598,
	.clear_hw_cntrs		= &ixgbe_clear_hw_cntrs_generic,
	.get_media_type		= &ixgbe_get_media_type_82598,
	.enable_rx_dma          = &ixgbe_enable_rx_dma_generic,
	.get_mac_addr		= &ixgbe_get_mac_addr_generic,
	.stop_adapter		= &ixgbe_stop_adapter_generic,
	.get_bus_info           = &ixgbe_get_bus_info_generic,
	.set_lan_id             = &ixgbe_set_lan_id_multi_port_pcie_82598,
	.read_analog_reg8	= &ixgbe_read_analog_reg8_82598,
	.write_analog_reg8	= &ixgbe_write_analog_reg8_82598,
	.setup_link		= &ixgbe_setup_mac_link_82598,
	.set_rxpba		= &ixgbe_set_rxpba_82598,
	.check_link		= &ixgbe_check_mac_link_82598,
	.get_link_capabilities	= &ixgbe_get_link_capabilities_82598,
	.led_on			= &ixgbe_led_on_generic,
	.led_off		= &ixgbe_led_off_generic,
	.init_led_link_act	= ixgbe_init_led_link_act_generic,
	.blink_led_start	= &ixgbe_blink_led_start_generic,
	.blink_led_stop		= &ixgbe_blink_led_stop_generic,
	.set_rar		= &ixgbe_set_rar_generic,
	.clear_rar		= &ixgbe_clear_rar_generic,
	.set_vmdq		= &ixgbe_set_vmdq_82598,
	.clear_vmdq		= &ixgbe_clear_vmdq_82598,
	.init_rx_addrs		= &ixgbe_init_rx_addrs_generic,
	.update_mc_addr_list	= &ixgbe_update_mc_addr_list_generic,
	.enable_mc		= &ixgbe_enable_mc_generic,
	.disable_mc		= &ixgbe_disable_mc_generic,
	.clear_vfta		= &ixgbe_clear_vfta_82598,
	.set_vfta		= &ixgbe_set_vfta_82598,
	.fc_enable		= &ixgbe_fc_enable_82598,
	.setup_fc		= ixgbe_setup_fc_generic,
	.fc_autoneg		= ixgbe_fc_autoneg,
	.set_fw_drv_ver         = NULL,
	.acquire_swfw_sync      = &ixgbe_acquire_swfw_sync,
	.release_swfw_sync      = &ixgbe_release_swfw_sync,
	.init_swfw_sync		= NULL,
	.get_thermal_sensor_data = NULL,
	.init_thermal_sensor_thresh = NULL,
	.prot_autoc_read	= &prot_autoc_read_generic,
	.prot_autoc_write	= &prot_autoc_write_generic,
	.enable_rx		= &ixgbe_enable_rx_generic,
	.disable_rx		= &ixgbe_disable_rx_generic,
};

static const struct ixgbe_eeprom_operations eeprom_ops_82598 = {
	.init_params		= &ixgbe_init_eeprom_params_generic,
	.read			= &ixgbe_read_eerd_generic,
	.write			= &ixgbe_write_eeprom_generic,
	.write_buffer		= &ixgbe_write_eeprom_buffer_bit_bang_generic,
	.read_buffer		= &ixgbe_read_eerd_buffer_generic,
	.calc_checksum          = &ixgbe_calc_eeprom_checksum_generic,
	.validate_checksum	= &ixgbe_validate_eeprom_checksum_generic,
	.update_checksum	= &ixgbe_update_eeprom_checksum_generic,
};

static const struct ixgbe_phy_operations phy_ops_82598 = {
	.identify		= &ixgbe_identify_phy_generic,
	.identify_sfp		= &ixgbe_identify_module_generic,
	.init			= &ixgbe_init_phy_ops_82598,
	.reset			= &ixgbe_reset_phy_generic,
	.read_reg		= &ixgbe_read_phy_reg_generic,
	.write_reg		= &ixgbe_write_phy_reg_generic,
	.read_reg_mdi		= &ixgbe_read_phy_reg_mdi,
	.write_reg_mdi		= &ixgbe_write_phy_reg_mdi,
	.setup_link		= &ixgbe_setup_phy_link_generic,
	.setup_link_speed	= &ixgbe_setup_phy_link_speed_generic,
	.read_i2c_sff8472	= &ixgbe_read_i2c_sff8472_82598,
	.read_i2c_eeprom	= &ixgbe_read_i2c_eeprom_82598,
	.check_overtemp		= &ixgbe_tn_check_overtemp,
};

const struct ixgbe_info ixgbe_82598_info = {
	.mac			= ixgbe_mac_82598EB,
	.get_invariants		= &ixgbe_get_invariants_82598,
	.mac_ops		= &mac_ops_82598,
	.eeprom_ops		= &eeprom_ops_82598,
	.phy_ops		= &phy_ops_82598,
	.mvals			= ixgbe_mvals_8259X,
};
