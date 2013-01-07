/*******************************************************************************

  Intel 10 Gigabit PCI Express Linux driver
  Copyright(c) 1999 - 2011 Intel Corporation.

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
  e1000-devel Mailing List <e1000-devel@lists.sourceforge.net>
  Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497

*******************************************************************************/

#include <linux/pci.h>
#include <linux/delay.h>
#include <linux/sched.h>

#include "ixgbe.h"
#include "ixgbe_phy.h"
#include "ixgbe_mbx.h"

#define IXGBE_82599_MAX_TX_QUEUES 128
#define IXGBE_82599_MAX_RX_QUEUES 128
#define IXGBE_82599_RAR_ENTRIES   128
#define IXGBE_82599_MC_TBL_SIZE   128
#define IXGBE_82599_VFT_TBL_SIZE  128
#define IXGBE_82599_RX_PB_SIZE	  512

static void ixgbe_disable_tx_laser_multispeed_fiber(struct ixgbe_hw *hw);
static void ixgbe_enable_tx_laser_multispeed_fiber(struct ixgbe_hw *hw);
static void ixgbe_flap_tx_laser_multispeed_fiber(struct ixgbe_hw *hw);
static s32 ixgbe_setup_mac_link_multispeed_fiber(struct ixgbe_hw *hw,
						 ixgbe_link_speed speed,
						 bool autoneg,
						 bool autoneg_wait_to_complete);
static s32 ixgbe_setup_mac_link_smartspeed(struct ixgbe_hw *hw,
                                           ixgbe_link_speed speed,
                                           bool autoneg,
                                           bool autoneg_wait_to_complete);
static s32 ixgbe_start_mac_link_82599(struct ixgbe_hw *hw,
				      bool autoneg_wait_to_complete);
static s32 ixgbe_setup_mac_link_82599(struct ixgbe_hw *hw,
                               ixgbe_link_speed speed,
                               bool autoneg,
                               bool autoneg_wait_to_complete);
static s32 ixgbe_setup_copper_link_82599(struct ixgbe_hw *hw,
                                         ixgbe_link_speed speed,
                                         bool autoneg,
                                         bool autoneg_wait_to_complete);
static s32 ixgbe_verify_fw_version_82599(struct ixgbe_hw *hw);
static bool ixgbe_verify_lesm_fw_enabled_82599(struct ixgbe_hw *hw);

static void ixgbe_init_mac_link_ops_82599(struct ixgbe_hw *hw)
{
	struct ixgbe_mac_info *mac = &hw->mac;

	/* enable the laser control functions for SFP+ fiber */
	if (mac->ops.get_media_type(hw) == ixgbe_media_type_fiber) {
		mac->ops.disable_tx_laser =
		                       &ixgbe_disable_tx_laser_multispeed_fiber;
		mac->ops.enable_tx_laser =
		                        &ixgbe_enable_tx_laser_multispeed_fiber;
		mac->ops.flap_tx_laser = &ixgbe_flap_tx_laser_multispeed_fiber;
	} else {
		mac->ops.disable_tx_laser = NULL;
		mac->ops.enable_tx_laser = NULL;
		mac->ops.flap_tx_laser = NULL;
	}

	if (hw->phy.multispeed_fiber) {
		/* Set up dual speed SFP+ support */
		mac->ops.setup_link = &ixgbe_setup_mac_link_multispeed_fiber;
	} else {
		if ((mac->ops.get_media_type(hw) ==
		     ixgbe_media_type_backplane) &&
		    (hw->phy.smart_speed == ixgbe_smart_speed_auto ||
		     hw->phy.smart_speed == ixgbe_smart_speed_on) &&
		     !ixgbe_verify_lesm_fw_enabled_82599(hw))
			mac->ops.setup_link = &ixgbe_setup_mac_link_smartspeed;
		else
			mac->ops.setup_link = &ixgbe_setup_mac_link_82599;
	}
}

static s32 ixgbe_setup_sfp_modules_82599(struct ixgbe_hw *hw)
{
	s32 ret_val = 0;
	u32 reg_anlp1 = 0;
	u32 i = 0;
	u16 list_offset, data_offset, data_value;

	if (hw->phy.sfp_type != ixgbe_sfp_type_unknown) {
		ixgbe_init_mac_link_ops_82599(hw);

		hw->phy.ops.reset = NULL;

		ret_val = ixgbe_get_sfp_init_sequence_offsets(hw, &list_offset,
		                                              &data_offset);
		if (ret_val != 0)
			goto setup_sfp_out;

		/* PHY config will finish before releasing the semaphore */
		ret_val = hw->mac.ops.acquire_swfw_sync(hw,
		                                        IXGBE_GSSR_MAC_CSR_SM);
		if (ret_val != 0) {
			ret_val = IXGBE_ERR_SWFW_SYNC;
			goto setup_sfp_out;
		}

		hw->eeprom.ops.read(hw, ++data_offset, &data_value);
		while (data_value != 0xffff) {
			IXGBE_WRITE_REG(hw, IXGBE_CORECTL, data_value);
			IXGBE_WRITE_FLUSH(hw);
			hw->eeprom.ops.read(hw, ++data_offset, &data_value);
		}

		/* Release the semaphore */
		hw->mac.ops.release_swfw_sync(hw, IXGBE_GSSR_MAC_CSR_SM);
		/*
		 * Delay obtaining semaphore again to allow FW access,
		 * semaphore_delay is in ms usleep_range needs us.
		 */
		usleep_range(hw->eeprom.semaphore_delay * 1000,
			     hw->eeprom.semaphore_delay * 2000);

		/* Now restart DSP by setting Restart_AN and clearing LMS */
		IXGBE_WRITE_REG(hw, IXGBE_AUTOC, ((IXGBE_READ_REG(hw,
		                IXGBE_AUTOC) & ~IXGBE_AUTOC_LMS_MASK) |
		                IXGBE_AUTOC_AN_RESTART));

		/* Wait for AN to leave state 0 */
		for (i = 0; i < 10; i++) {
			usleep_range(4000, 8000);
			reg_anlp1 = IXGBE_READ_REG(hw, IXGBE_ANLP1);
			if (reg_anlp1 & IXGBE_ANLP1_AN_STATE_MASK)
				break;
		}
		if (!(reg_anlp1 & IXGBE_ANLP1_AN_STATE_MASK)) {
			hw_dbg(hw, "sfp module setup not complete\n");
			ret_val = IXGBE_ERR_SFP_SETUP_NOT_COMPLETE;
			goto setup_sfp_out;
		}

		/* Restart DSP by setting Restart_AN and return to SFI mode */
		IXGBE_WRITE_REG(hw, IXGBE_AUTOC, (IXGBE_READ_REG(hw,
		                IXGBE_AUTOC) | IXGBE_AUTOC_LMS_10G_SERIAL |
		                IXGBE_AUTOC_AN_RESTART));
	}

setup_sfp_out:
	return ret_val;
}

static s32 ixgbe_get_invariants_82599(struct ixgbe_hw *hw)
{
	struct ixgbe_mac_info *mac = &hw->mac;

	ixgbe_init_mac_link_ops_82599(hw);

	mac->mcft_size = IXGBE_82599_MC_TBL_SIZE;
	mac->vft_size = IXGBE_82599_VFT_TBL_SIZE;
	mac->num_rar_entries = IXGBE_82599_RAR_ENTRIES;
	mac->max_rx_queues = IXGBE_82599_MAX_RX_QUEUES;
	mac->max_tx_queues = IXGBE_82599_MAX_TX_QUEUES;
	mac->max_msix_vectors = ixgbe_get_pcie_msix_count_generic(hw);

	return 0;
}

/**
 *  ixgbe_init_phy_ops_82599 - PHY/SFP specific init
 *  @hw: pointer to hardware structure
 *
 *  Initialize any function pointers that were not able to be
 *  set during get_invariants because the PHY/SFP type was
 *  not known.  Perform the SFP init if necessary.
 *
 **/
static s32 ixgbe_init_phy_ops_82599(struct ixgbe_hw *hw)
{
	struct ixgbe_mac_info *mac = &hw->mac;
	struct ixgbe_phy_info *phy = &hw->phy;
	s32 ret_val = 0;

	/* Identify the PHY or SFP module */
	ret_val = phy->ops.identify(hw);

	/* Setup function pointers based on detected SFP module and speeds */
	ixgbe_init_mac_link_ops_82599(hw);

	/* If copper media, overwrite with copper function pointers */
	if (mac->ops.get_media_type(hw) == ixgbe_media_type_copper) {
		mac->ops.setup_link = &ixgbe_setup_copper_link_82599;
		mac->ops.get_link_capabilities =
			&ixgbe_get_copper_link_capabilities_generic;
	}

	/* Set necessary function pointers based on phy type */
	switch (hw->phy.type) {
	case ixgbe_phy_tn:
		phy->ops.check_link = &ixgbe_check_phy_link_tnx;
		phy->ops.get_firmware_version =
		             &ixgbe_get_phy_firmware_version_tnx;
		break;
	case ixgbe_phy_aq:
		phy->ops.get_firmware_version =
			&ixgbe_get_phy_firmware_version_generic;
		break;
	default:
		break;
	}

	return ret_val;
}

/**
 *  ixgbe_get_link_capabilities_82599 - Determines link capabilities
 *  @hw: pointer to hardware structure
 *  @speed: pointer to link speed
 *  @negotiation: true when autoneg or autotry is enabled
 *
 *  Determines the link capabilities by reading the AUTOC register.
 **/
static s32 ixgbe_get_link_capabilities_82599(struct ixgbe_hw *hw,
                                             ixgbe_link_speed *speed,
                                             bool *negotiation)
{
	s32 status = 0;
	u32 autoc = 0;

	/* Determine 1G link capabilities off of SFP+ type */
	if (hw->phy.sfp_type == ixgbe_sfp_type_1g_cu_core0 ||
	    hw->phy.sfp_type == ixgbe_sfp_type_1g_cu_core1) {
		*speed = IXGBE_LINK_SPEED_1GB_FULL;
		*negotiation = true;
		goto out;
	}

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
		*negotiation = false;
		break;

	case IXGBE_AUTOC_LMS_10G_LINK_NO_AN:
		*speed = IXGBE_LINK_SPEED_10GB_FULL;
		*negotiation = false;
		break;

	case IXGBE_AUTOC_LMS_1G_AN:
		*speed = IXGBE_LINK_SPEED_1GB_FULL;
		*negotiation = true;
		break;

	case IXGBE_AUTOC_LMS_10G_SERIAL:
		*speed = IXGBE_LINK_SPEED_10GB_FULL;
		*negotiation = false;
		break;

	case IXGBE_AUTOC_LMS_KX4_KX_KR:
	case IXGBE_AUTOC_LMS_KX4_KX_KR_1G_AN:
		*speed = IXGBE_LINK_SPEED_UNKNOWN;
		if (autoc & IXGBE_AUTOC_KR_SUPP)
			*speed |= IXGBE_LINK_SPEED_10GB_FULL;
		if (autoc & IXGBE_AUTOC_KX4_SUPP)
			*speed |= IXGBE_LINK_SPEED_10GB_FULL;
		if (autoc & IXGBE_AUTOC_KX_SUPP)
			*speed |= IXGBE_LINK_SPEED_1GB_FULL;
		*negotiation = true;
		break;

	case IXGBE_AUTOC_LMS_KX4_KX_KR_SGMII:
		*speed = IXGBE_LINK_SPEED_100_FULL;
		if (autoc & IXGBE_AUTOC_KR_SUPP)
			*speed |= IXGBE_LINK_SPEED_10GB_FULL;
		if (autoc & IXGBE_AUTOC_KX4_SUPP)
			*speed |= IXGBE_LINK_SPEED_10GB_FULL;
		if (autoc & IXGBE_AUTOC_KX_SUPP)
			*speed |= IXGBE_LINK_SPEED_1GB_FULL;
		*negotiation = true;
		break;

	case IXGBE_AUTOC_LMS_SGMII_1G_100M:
		*speed = IXGBE_LINK_SPEED_1GB_FULL | IXGBE_LINK_SPEED_100_FULL;
		*negotiation = false;
		break;

	default:
		status = IXGBE_ERR_LINK_SETUP;
		goto out;
		break;
	}

	if (hw->phy.multispeed_fiber) {
		*speed |= IXGBE_LINK_SPEED_10GB_FULL |
		          IXGBE_LINK_SPEED_1GB_FULL;
		*negotiation = true;
	}

out:
	return status;
}

/**
 *  ixgbe_get_media_type_82599 - Get media type
 *  @hw: pointer to hardware structure
 *
 *  Returns the media type (fiber, copper, backplane)
 **/
static enum ixgbe_media_type ixgbe_get_media_type_82599(struct ixgbe_hw *hw)
{
	enum ixgbe_media_type media_type;

	/* Detect if there is a copper PHY attached. */
	switch (hw->phy.type) {
	case ixgbe_phy_cu_unknown:
	case ixgbe_phy_tn:
	case ixgbe_phy_aq:
		media_type = ixgbe_media_type_copper;
		goto out;
	default:
		break;
	}

	switch (hw->device_id) {
	case IXGBE_DEV_ID_82599_KX4:
	case IXGBE_DEV_ID_82599_KX4_MEZZ:
	case IXGBE_DEV_ID_82599_COMBO_BACKPLANE:
	case IXGBE_DEV_ID_82599_KR:
	case IXGBE_DEV_ID_82599_BACKPLANE_FCOE:
	case IXGBE_DEV_ID_82599_XAUI_LOM:
		/* Default device ID is mezzanine card KX/KX4 */
		media_type = ixgbe_media_type_backplane;
		break;
	case IXGBE_DEV_ID_82599_SFP:
	case IXGBE_DEV_ID_82599_SFP_FCOE:
	case IXGBE_DEV_ID_82599_SFP_EM:
	case IXGBE_DEV_ID_82599_SFP_SF2:
	case IXGBE_DEV_ID_82599EN_SFP:
	case IXGBE_DEV_ID_82599_SFP_SF_QP:
		media_type = ixgbe_media_type_fiber;
		break;
	case IXGBE_DEV_ID_82599_CX4:
		media_type = ixgbe_media_type_cx4;
		break;
	case IXGBE_DEV_ID_82599_T3_LOM:
		media_type = ixgbe_media_type_copper;
		break;
	case IXGBE_DEV_ID_82599_LS:
		media_type = ixgbe_media_type_fiber_lco;
		break;
	default:
		media_type = ixgbe_media_type_unknown;
		break;
	}
out:
	return media_type;
}

/**
 *  ixgbe_start_mac_link_82599 - Setup MAC link settings
 *  @hw: pointer to hardware structure
 *  @autoneg_wait_to_complete: true when waiting for completion is needed
 *
 *  Configures link settings based on values in the ixgbe_hw struct.
 *  Restarts the link.  Performs autonegotiation if needed.
 **/
static s32 ixgbe_start_mac_link_82599(struct ixgbe_hw *hw,
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
		     IXGBE_AUTOC_LMS_KX4_KX_KR ||
		    (autoc_reg & IXGBE_AUTOC_LMS_MASK) ==
		     IXGBE_AUTOC_LMS_KX4_KX_KR_1G_AN ||
		    (autoc_reg & IXGBE_AUTOC_LMS_MASK) ==
		     IXGBE_AUTOC_LMS_KX4_KX_KR_SGMII) {
			links_reg = 0; /* Just in case Autoneg time = 0 */
			for (i = 0; i < IXGBE_AUTO_NEG_TIME; i++) {
				links_reg = IXGBE_READ_REG(hw, IXGBE_LINKS);
				if (links_reg & IXGBE_LINKS_KX_AN_COMP)
					break;
				msleep(100);
			}
			if (!(links_reg & IXGBE_LINKS_KX_AN_COMP)) {
				status = IXGBE_ERR_AUTONEG_NOT_COMPLETE;
				hw_dbg(hw, "Autoneg did not complete.\n");
			}
		}
	}

	/* Add delay to filter out noises during initial link setup */
	msleep(50);

	return status;
}

/**
 *  ixgbe_disable_tx_laser_multispeed_fiber - Disable Tx laser
 *  @hw: pointer to hardware structure
 *
 *  The base drivers may require better control over SFP+ module
 *  PHY states.  This includes selectively shutting down the Tx
 *  laser on the PHY, effectively halting physical link.
 **/
static void ixgbe_disable_tx_laser_multispeed_fiber(struct ixgbe_hw *hw)
{
	u32 esdp_reg = IXGBE_READ_REG(hw, IXGBE_ESDP);

	/* Disable tx laser; allow 100us to go dark per spec */
	esdp_reg |= IXGBE_ESDP_SDP3;
	IXGBE_WRITE_REG(hw, IXGBE_ESDP, esdp_reg);
	IXGBE_WRITE_FLUSH(hw);
	udelay(100);
}

/**
 *  ixgbe_enable_tx_laser_multispeed_fiber - Enable Tx laser
 *  @hw: pointer to hardware structure
 *
 *  The base drivers may require better control over SFP+ module
 *  PHY states.  This includes selectively turning on the Tx
 *  laser on the PHY, effectively starting physical link.
 **/
static void ixgbe_enable_tx_laser_multispeed_fiber(struct ixgbe_hw *hw)
{
	u32 esdp_reg = IXGBE_READ_REG(hw, IXGBE_ESDP);

	/* Enable tx laser; allow 100ms to light up */
	esdp_reg &= ~IXGBE_ESDP_SDP3;
	IXGBE_WRITE_REG(hw, IXGBE_ESDP, esdp_reg);
	IXGBE_WRITE_FLUSH(hw);
	msleep(100);
}

/**
 *  ixgbe_flap_tx_laser_multispeed_fiber - Flap Tx laser
 *  @hw: pointer to hardware structure
 *
 *  When the driver changes the link speeds that it can support,
 *  it sets autotry_restart to true to indicate that we need to
 *  initiate a new autotry session with the link partner.  To do
 *  so, we set the speed then disable and re-enable the tx laser, to
 *  alert the link partner that it also needs to restart autotry on its
 *  end.  This is consistent with true clause 37 autoneg, which also
 *  involves a loss of signal.
 **/
static void ixgbe_flap_tx_laser_multispeed_fiber(struct ixgbe_hw *hw)
{
	if (hw->mac.autotry_restart) {
		ixgbe_disable_tx_laser_multispeed_fiber(hw);
		ixgbe_enable_tx_laser_multispeed_fiber(hw);
		hw->mac.autotry_restart = false;
	}
}

/**
 *  ixgbe_setup_mac_link_multispeed_fiber - Set MAC link speed
 *  @hw: pointer to hardware structure
 *  @speed: new link speed
 *  @autoneg: true if autonegotiation enabled
 *  @autoneg_wait_to_complete: true when waiting for completion is needed
 *
 *  Set the link speed in the AUTOC register and restarts link.
 **/
static s32 ixgbe_setup_mac_link_multispeed_fiber(struct ixgbe_hw *hw,
                                          ixgbe_link_speed speed,
                                          bool autoneg,
                                          bool autoneg_wait_to_complete)
{
	s32 status = 0;
	ixgbe_link_speed link_speed = IXGBE_LINK_SPEED_UNKNOWN;
	ixgbe_link_speed highest_link_speed = IXGBE_LINK_SPEED_UNKNOWN;
	u32 speedcnt = 0;
	u32 esdp_reg = IXGBE_READ_REG(hw, IXGBE_ESDP);
	u32 i = 0;
	bool link_up = false;
	bool negotiation;

	/* Mask off requested but non-supported speeds */
	status = hw->mac.ops.get_link_capabilities(hw, &link_speed,
						   &negotiation);
	if (status != 0)
		return status;

	speed &= link_speed;

	/*
	 * Try each speed one by one, highest priority first.  We do this in
	 * software because 10gb fiber doesn't support speed autonegotiation.
	 */
	if (speed & IXGBE_LINK_SPEED_10GB_FULL) {
		speedcnt++;
		highest_link_speed = IXGBE_LINK_SPEED_10GB_FULL;

		/* If we already have link at this speed, just jump out */
		status = hw->mac.ops.check_link(hw, &link_speed, &link_up,
						false);
		if (status != 0)
			return status;

		if ((link_speed == IXGBE_LINK_SPEED_10GB_FULL) && link_up)
			goto out;

		/* Set the module link speed */
		esdp_reg |= (IXGBE_ESDP_SDP5_DIR | IXGBE_ESDP_SDP5);
		IXGBE_WRITE_REG(hw, IXGBE_ESDP, esdp_reg);
		IXGBE_WRITE_FLUSH(hw);

		/* Allow module to change analog characteristics (1G->10G) */
		msleep(40);

		status = ixgbe_setup_mac_link_82599(hw,
						    IXGBE_LINK_SPEED_10GB_FULL,
						    autoneg,
						    autoneg_wait_to_complete);
		if (status != 0)
			return status;

		/* Flap the tx laser if it has not already been done */
		hw->mac.ops.flap_tx_laser(hw);

		/*
		 * Wait for the controller to acquire link.  Per IEEE 802.3ap,
		 * Section 73.10.2, we may have to wait up to 500ms if KR is
		 * attempted.  82599 uses the same timing for 10g SFI.
		 */
		for (i = 0; i < 5; i++) {
			/* Wait for the link partner to also set speed */
			msleep(100);

			/* If we have link, just jump out */
			status = hw->mac.ops.check_link(hw, &link_speed,
							&link_up, false);
			if (status != 0)
				return status;

			if (link_up)
				goto out;
		}
	}

	if (speed & IXGBE_LINK_SPEED_1GB_FULL) {
		speedcnt++;
		if (highest_link_speed == IXGBE_LINK_SPEED_UNKNOWN)
			highest_link_speed = IXGBE_LINK_SPEED_1GB_FULL;

		/* If we already have link at this speed, just jump out */
		status = hw->mac.ops.check_link(hw, &link_speed, &link_up,
						false);
		if (status != 0)
			return status;

		if ((link_speed == IXGBE_LINK_SPEED_1GB_FULL) && link_up)
			goto out;

		/* Set the module link speed */
		esdp_reg &= ~IXGBE_ESDP_SDP5;
		esdp_reg |= IXGBE_ESDP_SDP5_DIR;
		IXGBE_WRITE_REG(hw, IXGBE_ESDP, esdp_reg);
		IXGBE_WRITE_FLUSH(hw);

		/* Allow module to change analog characteristics (10G->1G) */
		msleep(40);

		status = ixgbe_setup_mac_link_82599(hw,
						    IXGBE_LINK_SPEED_1GB_FULL,
						    autoneg,
						    autoneg_wait_to_complete);
		if (status != 0)
			return status;

		/* Flap the tx laser if it has not already been done */
		hw->mac.ops.flap_tx_laser(hw);

		/* Wait for the link partner to also set speed */
		msleep(100);

		/* If we have link, just jump out */
		status = hw->mac.ops.check_link(hw, &link_speed, &link_up,
						false);
		if (status != 0)
			return status;

		if (link_up)
			goto out;
	}

	/*
	 * We didn't get link.  Configure back to the highest speed we tried,
	 * (if there was more than one).  We call ourselves back with just the
	 * single highest speed that the user requested.
	 */
	if (speedcnt > 1)
		status = ixgbe_setup_mac_link_multispeed_fiber(hw,
		                                               highest_link_speed,
		                                               autoneg,
		                                               autoneg_wait_to_complete);

out:
	/* Set autoneg_advertised value based on input link speed */
	hw->phy.autoneg_advertised = 0;

	if (speed & IXGBE_LINK_SPEED_10GB_FULL)
		hw->phy.autoneg_advertised |= IXGBE_LINK_SPEED_10GB_FULL;

	if (speed & IXGBE_LINK_SPEED_1GB_FULL)
		hw->phy.autoneg_advertised |= IXGBE_LINK_SPEED_1GB_FULL;

	return status;
}

/**
 *  ixgbe_setup_mac_link_smartspeed - Set MAC link speed using SmartSpeed
 *  @hw: pointer to hardware structure
 *  @speed: new link speed
 *  @autoneg: true if autonegotiation enabled
 *  @autoneg_wait_to_complete: true when waiting for completion is needed
 *
 *  Implements the Intel SmartSpeed algorithm.
 **/
static s32 ixgbe_setup_mac_link_smartspeed(struct ixgbe_hw *hw,
				     ixgbe_link_speed speed, bool autoneg,
				     bool autoneg_wait_to_complete)
{
	s32 status = 0;
	ixgbe_link_speed link_speed = IXGBE_LINK_SPEED_UNKNOWN;
	s32 i, j;
	bool link_up = false;
	u32 autoc_reg = IXGBE_READ_REG(hw, IXGBE_AUTOC);

	 /* Set autoneg_advertised value based on input link speed */
	hw->phy.autoneg_advertised = 0;

	if (speed & IXGBE_LINK_SPEED_10GB_FULL)
		hw->phy.autoneg_advertised |= IXGBE_LINK_SPEED_10GB_FULL;

	if (speed & IXGBE_LINK_SPEED_1GB_FULL)
		hw->phy.autoneg_advertised |= IXGBE_LINK_SPEED_1GB_FULL;

	if (speed & IXGBE_LINK_SPEED_100_FULL)
		hw->phy.autoneg_advertised |= IXGBE_LINK_SPEED_100_FULL;

	/*
	 * Implement Intel SmartSpeed algorithm.  SmartSpeed will reduce the
	 * autoneg advertisement if link is unable to be established at the
	 * highest negotiated rate.  This can sometimes happen due to integrity
	 * issues with the physical media connection.
	 */

	/* First, try to get link with full advertisement */
	hw->phy.smart_speed_active = false;
	for (j = 0; j < IXGBE_SMARTSPEED_MAX_RETRIES; j++) {
		status = ixgbe_setup_mac_link_82599(hw, speed, autoneg,
						    autoneg_wait_to_complete);
		if (status != 0)
			goto out;

		/*
		 * Wait for the controller to acquire link.  Per IEEE 802.3ap,
		 * Section 73.10.2, we may have to wait up to 500ms if KR is
		 * attempted, or 200ms if KX/KX4/BX/BX4 is attempted, per
		 * Table 9 in the AN MAS.
		 */
		for (i = 0; i < 5; i++) {
			mdelay(100);

			/* If we have link, just jump out */
			status = hw->mac.ops.check_link(hw, &link_speed,
							&link_up, false);
			if (status != 0)
				goto out;

			if (link_up)
				goto out;
		}
	}

	/*
	 * We didn't get link.  If we advertised KR plus one of KX4/KX
	 * (or BX4/BX), then disable KR and try again.
	 */
	if (((autoc_reg & IXGBE_AUTOC_KR_SUPP) == 0) ||
	    ((autoc_reg & IXGBE_AUTOC_KX4_KX_SUPP_MASK) == 0))
		goto out;

	/* Turn SmartSpeed on to disable KR support */
	hw->phy.smart_speed_active = true;
	status = ixgbe_setup_mac_link_82599(hw, speed, autoneg,
					    autoneg_wait_to_complete);
	if (status != 0)
		goto out;

	/*
	 * Wait for the controller to acquire link.  600ms will allow for
	 * the AN link_fail_inhibit_timer as well for multiple cycles of
	 * parallel detect, both 10g and 1g. This allows for the maximum
	 * connect attempts as defined in the AN MAS table 73-7.
	 */
	for (i = 0; i < 6; i++) {
		mdelay(100);

		/* If we have link, just jump out */
		status = hw->mac.ops.check_link(hw, &link_speed,
						&link_up, false);
		if (status != 0)
			goto out;

		if (link_up)
			goto out;
	}

	/* We didn't get link.  Turn SmartSpeed back off. */
	hw->phy.smart_speed_active = false;
	status = ixgbe_setup_mac_link_82599(hw, speed, autoneg,
					    autoneg_wait_to_complete);

out:
	if (link_up && (link_speed == IXGBE_LINK_SPEED_1GB_FULL))
		hw_dbg(hw, "Smartspeed has downgraded the link speed from "
		       "the maximum advertised\n");
	return status;
}

/**
 *  ixgbe_setup_mac_link_82599 - Set MAC link speed
 *  @hw: pointer to hardware structure
 *  @speed: new link speed
 *  @autoneg: true if autonegotiation enabled
 *  @autoneg_wait_to_complete: true when waiting for completion is needed
 *
 *  Set the link speed in the AUTOC register and restarts link.
 **/
static s32 ixgbe_setup_mac_link_82599(struct ixgbe_hw *hw,
                               ixgbe_link_speed speed, bool autoneg,
                               bool autoneg_wait_to_complete)
{
	s32 status = 0;
	u32 autoc = IXGBE_READ_REG(hw, IXGBE_AUTOC);
	u32 autoc2 = IXGBE_READ_REG(hw, IXGBE_AUTOC2);
	u32 start_autoc = autoc;
	u32 orig_autoc = 0;
	u32 link_mode = autoc & IXGBE_AUTOC_LMS_MASK;
	u32 pma_pmd_1g = autoc & IXGBE_AUTOC_1G_PMA_PMD_MASK;
	u32 pma_pmd_10g_serial = autoc2 & IXGBE_AUTOC2_10G_SERIAL_PMA_PMD_MASK;
	u32 links_reg;
	u32 i;
	ixgbe_link_speed link_capabilities = IXGBE_LINK_SPEED_UNKNOWN;

	/* Check to see if speed passed in is supported. */
	hw->mac.ops.get_link_capabilities(hw, &link_capabilities, &autoneg);
	if (status != 0)
		goto out;

	speed &= link_capabilities;

	if (speed == IXGBE_LINK_SPEED_UNKNOWN) {
		status = IXGBE_ERR_LINK_SETUP;
		goto out;
	}

	/* Use stored value (EEPROM defaults) of AUTOC to find KR/KX4 support*/
	if (hw->mac.orig_link_settings_stored)
		orig_autoc = hw->mac.orig_autoc;
	else
		orig_autoc = autoc;

	if (link_mode == IXGBE_AUTOC_LMS_KX4_KX_KR ||
	    link_mode == IXGBE_AUTOC_LMS_KX4_KX_KR_1G_AN ||
	    link_mode == IXGBE_AUTOC_LMS_KX4_KX_KR_SGMII) {
		/* Set KX4/KX/KR support according to speed requested */
		autoc &= ~(IXGBE_AUTOC_KX4_KX_SUPP_MASK | IXGBE_AUTOC_KR_SUPP);
		if (speed & IXGBE_LINK_SPEED_10GB_FULL)
			if (orig_autoc & IXGBE_AUTOC_KX4_SUPP)
				autoc |= IXGBE_AUTOC_KX4_SUPP;
			if ((orig_autoc & IXGBE_AUTOC_KR_SUPP) &&
			    (hw->phy.smart_speed_active == false))
				autoc |= IXGBE_AUTOC_KR_SUPP;
		if (speed & IXGBE_LINK_SPEED_1GB_FULL)
			autoc |= IXGBE_AUTOC_KX_SUPP;
	} else if ((pma_pmd_1g == IXGBE_AUTOC_1G_SFI) &&
	           (link_mode == IXGBE_AUTOC_LMS_1G_LINK_NO_AN ||
	            link_mode == IXGBE_AUTOC_LMS_1G_AN)) {
		/* Switch from 1G SFI to 10G SFI if requested */
		if ((speed == IXGBE_LINK_SPEED_10GB_FULL) &&
		    (pma_pmd_10g_serial == IXGBE_AUTOC2_10G_SFI)) {
			autoc &= ~IXGBE_AUTOC_LMS_MASK;
			autoc |= IXGBE_AUTOC_LMS_10G_SERIAL;
		}
	} else if ((pma_pmd_10g_serial == IXGBE_AUTOC2_10G_SFI) &&
	           (link_mode == IXGBE_AUTOC_LMS_10G_SERIAL)) {
		/* Switch from 10G SFI to 1G SFI if requested */
		if ((speed == IXGBE_LINK_SPEED_1GB_FULL) &&
		    (pma_pmd_1g == IXGBE_AUTOC_1G_SFI)) {
			autoc &= ~IXGBE_AUTOC_LMS_MASK;
			if (autoneg)
				autoc |= IXGBE_AUTOC_LMS_1G_AN;
			else
				autoc |= IXGBE_AUTOC_LMS_1G_LINK_NO_AN;
		}
	}

	if (autoc != start_autoc) {
		/* Restart link */
		autoc |= IXGBE_AUTOC_AN_RESTART;
		IXGBE_WRITE_REG(hw, IXGBE_AUTOC, autoc);

		/* Only poll for autoneg to complete if specified to do so */
		if (autoneg_wait_to_complete) {
			if (link_mode == IXGBE_AUTOC_LMS_KX4_KX_KR ||
			    link_mode == IXGBE_AUTOC_LMS_KX4_KX_KR_1G_AN ||
			    link_mode == IXGBE_AUTOC_LMS_KX4_KX_KR_SGMII) {
				links_reg = 0; /*Just in case Autoneg time=0*/
				for (i = 0; i < IXGBE_AUTO_NEG_TIME; i++) {
					links_reg =
					       IXGBE_READ_REG(hw, IXGBE_LINKS);
					if (links_reg & IXGBE_LINKS_KX_AN_COMP)
						break;
					msleep(100);
				}
				if (!(links_reg & IXGBE_LINKS_KX_AN_COMP)) {
					status =
					        IXGBE_ERR_AUTONEG_NOT_COMPLETE;
					hw_dbg(hw, "Autoneg did not "
					       "complete.\n");
				}
			}
		}

		/* Add delay to filter out noises during initial link setup */
		msleep(50);
	}

out:
	return status;
}

/**
 *  ixgbe_setup_copper_link_82599 - Set the PHY autoneg advertised field
 *  @hw: pointer to hardware structure
 *  @speed: new link speed
 *  @autoneg: true if autonegotiation enabled
 *  @autoneg_wait_to_complete: true if waiting is needed to complete
 *
 *  Restarts link on PHY and MAC based on settings passed in.
 **/
static s32 ixgbe_setup_copper_link_82599(struct ixgbe_hw *hw,
                                         ixgbe_link_speed speed,
                                         bool autoneg,
                                         bool autoneg_wait_to_complete)
{
	s32 status;

	/* Setup the PHY according to input speed */
	status = hw->phy.ops.setup_link_speed(hw, speed, autoneg,
	                                      autoneg_wait_to_complete);
	/* Set up MAC */
	ixgbe_start_mac_link_82599(hw, autoneg_wait_to_complete);

	return status;
}

/**
 *  ixgbe_reset_hw_82599 - Perform hardware reset
 *  @hw: pointer to hardware structure
 *
 *  Resets the hardware by resetting the transmit and receive units, masks
 *  and clears all interrupts, perform a PHY reset, and perform a link (MAC)
 *  reset.
 **/
static s32 ixgbe_reset_hw_82599(struct ixgbe_hw *hw)
{
	s32 status = 0;
	u32 ctrl;
	u32 i;
	u32 autoc;
	u32 autoc2;

	/* Call adapter stop to disable tx/rx and clear interrupts */
	hw->mac.ops.stop_adapter(hw);

	/* PHY ops must be identified and initialized prior to reset */

	/* Identify PHY and related function pointers */
	status = hw->phy.ops.init(hw);

	if (status == IXGBE_ERR_SFP_NOT_SUPPORTED)
		goto reset_hw_out;

	/* Setup SFP module if there is one present. */
	if (hw->phy.sfp_setup_needed) {
		status = hw->mac.ops.setup_sfp(hw);
		hw->phy.sfp_setup_needed = false;
	}

	if (status == IXGBE_ERR_SFP_NOT_SUPPORTED)
		goto reset_hw_out;

	/* Reset PHY */
	if (hw->phy.reset_disable == false && hw->phy.ops.reset != NULL)
		hw->phy.ops.reset(hw);

	/*
	 * Prevent the PCI-E bus from from hanging by disabling PCI-E master
	 * access and verify no pending requests before reset
	 */
	ixgbe_disable_pcie_master(hw);

mac_reset_top:
	/*
	 * Issue global reset to the MAC.  This needs to be a SW reset.
	 * If link reset is used, it might reset the MAC when mng is using it
	 */
	ctrl = IXGBE_READ_REG(hw, IXGBE_CTRL);
	IXGBE_WRITE_REG(hw, IXGBE_CTRL, (ctrl | IXGBE_CTRL_RST));
	IXGBE_WRITE_FLUSH(hw);

	/* Poll for reset bit to self-clear indicating reset is complete */
	for (i = 0; i < 10; i++) {
		udelay(1);
		ctrl = IXGBE_READ_REG(hw, IXGBE_CTRL);
		if (!(ctrl & IXGBE_CTRL_RST))
			break;
	}
	if (ctrl & IXGBE_CTRL_RST) {
		status = IXGBE_ERR_RESET_FAILED;
		hw_dbg(hw, "Reset polling failed to complete.\n");
	}

	/*
	 * Double resets are required for recovery from certain error
	 * conditions.  Between resets, it is necessary to stall to allow time
	 * for any pending HW events to complete.  We use 1usec since that is
	 * what is needed for ixgbe_disable_pcie_master().  The second reset
	 * then clears out any effects of those events.
	 */
	if (hw->mac.flags & IXGBE_FLAGS_DOUBLE_RESET_REQUIRED) {
		hw->mac.flags &= ~IXGBE_FLAGS_DOUBLE_RESET_REQUIRED;
		udelay(1);
		goto mac_reset_top;
	}

	msleep(50);

	/*
	 * Store the original AUTOC/AUTOC2 values if they have not been
	 * stored off yet.  Otherwise restore the stored original
	 * values since the reset operation sets back to defaults.
	 */
	autoc = IXGBE_READ_REG(hw, IXGBE_AUTOC);
	autoc2 = IXGBE_READ_REG(hw, IXGBE_AUTOC2);
	if (hw->mac.orig_link_settings_stored == false) {
		hw->mac.orig_autoc = autoc;
		hw->mac.orig_autoc2 = autoc2;
		hw->mac.orig_link_settings_stored = true;
	} else {
		if (autoc != hw->mac.orig_autoc)
			IXGBE_WRITE_REG(hw, IXGBE_AUTOC, (hw->mac.orig_autoc |
			                IXGBE_AUTOC_AN_RESTART));

		if ((autoc2 & IXGBE_AUTOC2_UPPER_MASK) !=
		    (hw->mac.orig_autoc2 & IXGBE_AUTOC2_UPPER_MASK)) {
			autoc2 &= ~IXGBE_AUTOC2_UPPER_MASK;
			autoc2 |= (hw->mac.orig_autoc2 &
			           IXGBE_AUTOC2_UPPER_MASK);
			IXGBE_WRITE_REG(hw, IXGBE_AUTOC2, autoc2);
		}
	}

	/* Store the permanent mac address */
	hw->mac.ops.get_mac_addr(hw, hw->mac.perm_addr);

	/*
	 * Store MAC address from RAR0, clear receive address registers, and
	 * clear the multicast table.  Also reset num_rar_entries to 128,
	 * since we modify this value when programming the SAN MAC address.
	 */
	hw->mac.num_rar_entries = 128;
	hw->mac.ops.init_rx_addrs(hw);

	/* Store the permanent SAN mac address */
	hw->mac.ops.get_san_mac_addr(hw, hw->mac.san_addr);

	/* Add the SAN MAC address to the RAR only if it's a valid address */
	if (ixgbe_validate_mac_addr(hw->mac.san_addr) == 0) {
		hw->mac.ops.set_rar(hw, hw->mac.num_rar_entries - 1,
		                    hw->mac.san_addr, 0, IXGBE_RAH_AV);

		/* Reserve the last RAR for the SAN MAC address */
		hw->mac.num_rar_entries--;
	}

	/* Store the alternative WWNN/WWPN prefix */
	hw->mac.ops.get_wwn_prefix(hw, &hw->mac.wwnn_prefix,
	                               &hw->mac.wwpn_prefix);

reset_hw_out:
	return status;
}

/**
 *  ixgbe_reinit_fdir_tables_82599 - Reinitialize Flow Director tables.
 *  @hw: pointer to hardware structure
 **/
s32 ixgbe_reinit_fdir_tables_82599(struct ixgbe_hw *hw)
{
	int i;
	u32 fdirctrl = IXGBE_READ_REG(hw, IXGBE_FDIRCTRL);
	fdirctrl &= ~IXGBE_FDIRCTRL_INIT_DONE;

	/*
	 * Before starting reinitialization process,
	 * FDIRCMD.CMD must be zero.
	 */
	for (i = 0; i < IXGBE_FDIRCMD_CMD_POLL; i++) {
		if (!(IXGBE_READ_REG(hw, IXGBE_FDIRCMD) &
		      IXGBE_FDIRCMD_CMD_MASK))
			break;
		udelay(10);
	}
	if (i >= IXGBE_FDIRCMD_CMD_POLL) {
		hw_dbg(hw, "Flow Director previous command isn't complete, "
		       "aborting table re-initialization.\n");
		return IXGBE_ERR_FDIR_REINIT_FAILED;
	}

	IXGBE_WRITE_REG(hw, IXGBE_FDIRFREE, 0);
	IXGBE_WRITE_FLUSH(hw);
	/*
	 * 82599 adapters flow director init flow cannot be restarted,
	 * Workaround 82599 silicon errata by performing the following steps
	 * before re-writing the FDIRCTRL control register with the same value.
	 * - write 1 to bit 8 of FDIRCMD register &
	 * - write 0 to bit 8 of FDIRCMD register
	 */
	IXGBE_WRITE_REG(hw, IXGBE_FDIRCMD,
	                (IXGBE_READ_REG(hw, IXGBE_FDIRCMD) |
	                 IXGBE_FDIRCMD_CLEARHT));
	IXGBE_WRITE_FLUSH(hw);
	IXGBE_WRITE_REG(hw, IXGBE_FDIRCMD,
	                (IXGBE_READ_REG(hw, IXGBE_FDIRCMD) &
	                 ~IXGBE_FDIRCMD_CLEARHT));
	IXGBE_WRITE_FLUSH(hw);
	/*
	 * Clear FDIR Hash register to clear any leftover hashes
	 * waiting to be programmed.
	 */
	IXGBE_WRITE_REG(hw, IXGBE_FDIRHASH, 0x00);
	IXGBE_WRITE_FLUSH(hw);

	IXGBE_WRITE_REG(hw, IXGBE_FDIRCTRL, fdirctrl);
	IXGBE_WRITE_FLUSH(hw);

	/* Poll init-done after we write FDIRCTRL register */
	for (i = 0; i < IXGBE_FDIR_INIT_DONE_POLL; i++) {
		if (IXGBE_READ_REG(hw, IXGBE_FDIRCTRL) &
		                   IXGBE_FDIRCTRL_INIT_DONE)
			break;
		udelay(10);
	}
	if (i >= IXGBE_FDIR_INIT_DONE_POLL) {
		hw_dbg(hw, "Flow Director Signature poll time exceeded!\n");
		return IXGBE_ERR_FDIR_REINIT_FAILED;
	}

	/* Clear FDIR statistics registers (read to clear) */
	IXGBE_READ_REG(hw, IXGBE_FDIRUSTAT);
	IXGBE_READ_REG(hw, IXGBE_FDIRFSTAT);
	IXGBE_READ_REG(hw, IXGBE_FDIRMATCH);
	IXGBE_READ_REG(hw, IXGBE_FDIRMISS);
	IXGBE_READ_REG(hw, IXGBE_FDIRLEN);

	return 0;
}

/**
 *  ixgbe_init_fdir_signature_82599 - Initialize Flow Director signature filters
 *  @hw: pointer to hardware structure
 *  @pballoc: which mode to allocate filters with
 **/
s32 ixgbe_init_fdir_signature_82599(struct ixgbe_hw *hw, u32 pballoc)
{
	u32 fdirctrl = 0;
	u32 pbsize;
	int i;

	/*
	 * Before enabling Flow Director, the Rx Packet Buffer size
	 * must be reduced.  The new value is the current size minus
	 * flow director memory usage size.
	 */
	pbsize = (1 << (IXGBE_FDIR_PBALLOC_SIZE_SHIFT + pballoc));
	IXGBE_WRITE_REG(hw, IXGBE_RXPBSIZE(0),
	    (IXGBE_READ_REG(hw, IXGBE_RXPBSIZE(0)) - pbsize));

	/*
	 * The defaults in the HW for RX PB 1-7 are not zero and so should be
	 * initialized to zero for non DCB mode otherwise actual total RX PB
	 * would be bigger than programmed and filter space would run into
	 * the PB 0 region.
	 */
	for (i = 1; i < 8; i++)
		IXGBE_WRITE_REG(hw, IXGBE_RXPBSIZE(i), 0);

	/* Send interrupt when 64 filters are left */
	fdirctrl |= 4 << IXGBE_FDIRCTRL_FULL_THRESH_SHIFT;

	/* Set the maximum length per hash bucket to 0xA filters */
	fdirctrl |= 0xA << IXGBE_FDIRCTRL_MAX_LENGTH_SHIFT;

	switch (pballoc) {
	case IXGBE_FDIR_PBALLOC_64K:
		/* 8k - 1 signature filters */
		fdirctrl |= IXGBE_FDIRCTRL_PBALLOC_64K;
		break;
	case IXGBE_FDIR_PBALLOC_128K:
		/* 16k - 1 signature filters */
		fdirctrl |= IXGBE_FDIRCTRL_PBALLOC_128K;
		break;
	case IXGBE_FDIR_PBALLOC_256K:
		/* 32k - 1 signature filters */
		fdirctrl |= IXGBE_FDIRCTRL_PBALLOC_256K;
		break;
	default:
		/* bad value */
		return IXGBE_ERR_CONFIG;
	};

	/* Move the flexible bytes to use the ethertype - shift 6 words */
	fdirctrl |= (0x6 << IXGBE_FDIRCTRL_FLEX_SHIFT);


	/* Prime the keys for hashing */
	IXGBE_WRITE_REG(hw, IXGBE_FDIRHKEY, IXGBE_ATR_BUCKET_HASH_KEY);
	IXGBE_WRITE_REG(hw, IXGBE_FDIRSKEY, IXGBE_ATR_SIGNATURE_HASH_KEY);

	/*
	 * Poll init-done after we write the register.  Estimated times:
	 *      10G: PBALLOC = 11b, timing is 60us
	 *       1G: PBALLOC = 11b, timing is 600us
	 *     100M: PBALLOC = 11b, timing is 6ms
	 *
	 *     Multiple these timings by 4 if under full Rx load
	 *
	 * So we'll poll for IXGBE_FDIR_INIT_DONE_POLL times, sleeping for
	 * 1 msec per poll time.  If we're at line rate and drop to 100M, then
	 * this might not finish in our poll time, but we can live with that
	 * for now.
	 */
	IXGBE_WRITE_REG(hw, IXGBE_FDIRCTRL, fdirctrl);
	IXGBE_WRITE_FLUSH(hw);
	for (i = 0; i < IXGBE_FDIR_INIT_DONE_POLL; i++) {
		if (IXGBE_READ_REG(hw, IXGBE_FDIRCTRL) &
		                   IXGBE_FDIRCTRL_INIT_DONE)
			break;
		usleep_range(1000, 2000);
	}
	if (i >= IXGBE_FDIR_INIT_DONE_POLL)
		hw_dbg(hw, "Flow Director Signature poll time exceeded!\n");

	return 0;
}

/**
 *  ixgbe_init_fdir_perfect_82599 - Initialize Flow Director perfect filters
 *  @hw: pointer to hardware structure
 *  @pballoc: which mode to allocate filters with
 **/
s32 ixgbe_init_fdir_perfect_82599(struct ixgbe_hw *hw, u32 pballoc)
{
	u32 fdirctrl = 0;
	u32 pbsize;
	int i;

	/*
	 * Before enabling Flow Director, the Rx Packet Buffer size
	 * must be reduced.  The new value is the current size minus
	 * flow director memory usage size.
	 */
	pbsize = (1 << (IXGBE_FDIR_PBALLOC_SIZE_SHIFT + pballoc));
	IXGBE_WRITE_REG(hw, IXGBE_RXPBSIZE(0),
	    (IXGBE_READ_REG(hw, IXGBE_RXPBSIZE(0)) - pbsize));

	/*
	 * The defaults in the HW for RX PB 1-7 are not zero and so should be
	 * initialized to zero for non DCB mode otherwise actual total RX PB
	 * would be bigger than programmed and filter space would run into
	 * the PB 0 region.
	 */
	for (i = 1; i < 8; i++)
		IXGBE_WRITE_REG(hw, IXGBE_RXPBSIZE(i), 0);

	/* Send interrupt when 64 filters are left */
	fdirctrl |= 4 << IXGBE_FDIRCTRL_FULL_THRESH_SHIFT;

	/* Initialize the drop queue to Rx queue 127 */
	fdirctrl |= (127 << IXGBE_FDIRCTRL_DROP_Q_SHIFT);

	switch (pballoc) {
	case IXGBE_FDIR_PBALLOC_64K:
		/* 2k - 1 perfect filters */
		fdirctrl |= IXGBE_FDIRCTRL_PBALLOC_64K;
		break;
	case IXGBE_FDIR_PBALLOC_128K:
		/* 4k - 1 perfect filters */
		fdirctrl |= IXGBE_FDIRCTRL_PBALLOC_128K;
		break;
	case IXGBE_FDIR_PBALLOC_256K:
		/* 8k - 1 perfect filters */
		fdirctrl |= IXGBE_FDIRCTRL_PBALLOC_256K;
		break;
	default:
		/* bad value */
		return IXGBE_ERR_CONFIG;
	};

	/* Turn perfect match filtering on */
	fdirctrl |= IXGBE_FDIRCTRL_PERFECT_MATCH;
	fdirctrl |= IXGBE_FDIRCTRL_REPORT_STATUS;

	/* Move the flexible bytes to use the ethertype - shift 6 words */
	fdirctrl |= (0x6 << IXGBE_FDIRCTRL_FLEX_SHIFT);

	/* Prime the keys for hashing */
	IXGBE_WRITE_REG(hw, IXGBE_FDIRHKEY, IXGBE_ATR_BUCKET_HASH_KEY);
	IXGBE_WRITE_REG(hw, IXGBE_FDIRSKEY, IXGBE_ATR_SIGNATURE_HASH_KEY);

	/*
	 * Poll init-done after we write the register.  Estimated times:
	 *      10G: PBALLOC = 11b, timing is 60us
	 *       1G: PBALLOC = 11b, timing is 600us
	 *     100M: PBALLOC = 11b, timing is 6ms
	 *
	 *     Multiple these timings by 4 if under full Rx load
	 *
	 * So we'll poll for IXGBE_FDIR_INIT_DONE_POLL times, sleeping for
	 * 1 msec per poll time.  If we're at line rate and drop to 100M, then
	 * this might not finish in our poll time, but we can live with that
	 * for now.
	 */

	/* Set the maximum length per hash bucket to 0xA filters */
	fdirctrl |= (0xA << IXGBE_FDIRCTRL_MAX_LENGTH_SHIFT);

	IXGBE_WRITE_REG(hw, IXGBE_FDIRCTRL, fdirctrl);
	IXGBE_WRITE_FLUSH(hw);
	for (i = 0; i < IXGBE_FDIR_INIT_DONE_POLL; i++) {
		if (IXGBE_READ_REG(hw, IXGBE_FDIRCTRL) &
		                   IXGBE_FDIRCTRL_INIT_DONE)
			break;
		usleep_range(1000, 2000);
	}
	if (i >= IXGBE_FDIR_INIT_DONE_POLL)
		hw_dbg(hw, "Flow Director Perfect poll time exceeded!\n");

	return 0;
}


/**
 *  ixgbe_atr_compute_hash_82599 - Compute the hashes for SW ATR
 *  @stream: input bitstream to compute the hash on
 *  @key: 32-bit hash key
 **/
static u32 ixgbe_atr_compute_hash_82599(union ixgbe_atr_input *atr_input,
					u32 key)
{
	/*
	 * The algorithm is as follows:
	 *    Hash[15:0] = Sum { S[n] x K[n+16] }, n = 0...350
	 *    where Sum {A[n]}, n = 0...n is bitwise XOR of A[0], A[1]...A[n]
	 *    and A[n] x B[n] is bitwise AND between same length strings
	 *
	 *    K[n] is 16 bits, defined as:
	 *       for n modulo 32 >= 15, K[n] = K[n % 32 : (n % 32) - 15]
	 *       for n modulo 32 < 15, K[n] =
	 *             K[(n % 32:0) | (31:31 - (14 - (n % 32)))]
	 *
	 *    S[n] is 16 bits, defined as:
	 *       for n >= 15, S[n] = S[n:n - 15]
	 *       for n < 15, S[n] = S[(n:0) | (350:350 - (14 - n))]
	 *
	 *    To simplify for programming, the algorithm is implemented
	 *    in software this way:
	 *
	 *    key[31:0], hi_hash_dword[31:0], lo_hash_dword[31:0], hash[15:0]
	 *
	 *    for (i = 0; i < 352; i+=32)
	 *        hi_hash_dword[31:0] ^= Stream[(i+31):i];
	 *
	 *    lo_hash_dword[15:0]  ^= Stream[15:0];
	 *    lo_hash_dword[15:0]  ^= hi_hash_dword[31:16];
	 *    lo_hash_dword[31:16] ^= hi_hash_dword[15:0];
	 *
	 *    hi_hash_dword[31:0]  ^= Stream[351:320];
	 *
	 *    if(key[0])
	 *        hash[15:0] ^= Stream[15:0];
	 *
	 *    for (i = 0; i < 16; i++) {
	 *        if (key[i])
	 *            hash[15:0] ^= lo_hash_dword[(i+15):i];
	 *        if (key[i + 16])
	 *            hash[15:0] ^= hi_hash_dword[(i+15):i];
	 *    }
	 *
	 */
	__be32 common_hash_dword = 0;
	u32 hi_hash_dword, lo_hash_dword, flow_vm_vlan;
	u32 hash_result = 0;
	u8 i;

	/* record the flow_vm_vlan bits as they are a key part to the hash */
	flow_vm_vlan = ntohl(atr_input->dword_stream[0]);

	/* generate common hash dword */
	for (i = 10; i; i -= 2)
		common_hash_dword ^= atr_input->dword_stream[i] ^
				     atr_input->dword_stream[i - 1];

	hi_hash_dword = ntohl(common_hash_dword);

	/* low dword is word swapped version of common */
	lo_hash_dword = (hi_hash_dword >> 16) | (hi_hash_dword << 16);

	/* apply flow ID/VM pool/VLAN ID bits to hash words */
	hi_hash_dword ^= flow_vm_vlan ^ (flow_vm_vlan >> 16);

	/* Process bits 0 and 16 */
	if (key & 0x0001) hash_result ^= lo_hash_dword;
	if (key & 0x00010000) hash_result ^= hi_hash_dword;

	/*
	 * apply flow ID/VM pool/VLAN ID bits to lo hash dword, we had to
	 * delay this because bit 0 of the stream should not be processed
	 * so we do not add the vlan until after bit 0 was processed
	 */
	lo_hash_dword ^= flow_vm_vlan ^ (flow_vm_vlan << 16);


	/* process the remaining 30 bits in the key 2 bits at a time */
	for (i = 15; i; i-- ) {
		if (key & (0x0001 << i)) hash_result ^= lo_hash_dword >> i;
		if (key & (0x00010000 << i)) hash_result ^= hi_hash_dword >> i;
	}

	return hash_result & IXGBE_ATR_HASH_MASK;
}

/*
 * These defines allow us to quickly generate all of the necessary instructions
 * in the function below by simply calling out IXGBE_COMPUTE_SIG_HASH_ITERATION
 * for values 0 through 15
 */
#define IXGBE_ATR_COMMON_HASH_KEY \
		(IXGBE_ATR_BUCKET_HASH_KEY & IXGBE_ATR_SIGNATURE_HASH_KEY)
#define IXGBE_COMPUTE_SIG_HASH_ITERATION(_n) \
do { \
	u32 n = (_n); \
	if (IXGBE_ATR_COMMON_HASH_KEY & (0x01 << n)) \
		common_hash ^= lo_hash_dword >> n; \
	else if (IXGBE_ATR_BUCKET_HASH_KEY & (0x01 << n)) \
		bucket_hash ^= lo_hash_dword >> n; \
	else if (IXGBE_ATR_SIGNATURE_HASH_KEY & (0x01 << n)) \
		sig_hash ^= lo_hash_dword << (16 - n); \
	if (IXGBE_ATR_COMMON_HASH_KEY & (0x01 << (n + 16))) \
		common_hash ^= hi_hash_dword >> n; \
	else if (IXGBE_ATR_BUCKET_HASH_KEY & (0x01 << (n + 16))) \
		bucket_hash ^= hi_hash_dword >> n; \
	else if (IXGBE_ATR_SIGNATURE_HASH_KEY & (0x01 << (n + 16))) \
		sig_hash ^= hi_hash_dword << (16 - n); \
} while (0);

/**
 *  ixgbe_atr_compute_sig_hash_82599 - Compute the signature hash
 *  @stream: input bitstream to compute the hash on
 *
 *  This function is almost identical to the function above but contains
 *  several optomizations such as unwinding all of the loops, letting the
 *  compiler work out all of the conditional ifs since the keys are static
 *  defines, and computing two keys at once since the hashed dword stream
 *  will be the same for both keys.
 **/
static u32 ixgbe_atr_compute_sig_hash_82599(union ixgbe_atr_hash_dword input,
					    union ixgbe_atr_hash_dword common)
{
	u32 hi_hash_dword, lo_hash_dword, flow_vm_vlan;
	u32 sig_hash = 0, bucket_hash = 0, common_hash = 0;

	/* record the flow_vm_vlan bits as they are a key part to the hash */
	flow_vm_vlan = ntohl(input.dword);

	/* generate common hash dword */
	hi_hash_dword = ntohl(common.dword);

	/* low dword is word swapped version of common */
	lo_hash_dword = (hi_hash_dword >> 16) | (hi_hash_dword << 16);

	/* apply flow ID/VM pool/VLAN ID bits to hash words */
	hi_hash_dword ^= flow_vm_vlan ^ (flow_vm_vlan >> 16);

	/* Process bits 0 and 16 */
	IXGBE_COMPUTE_SIG_HASH_ITERATION(0);

	/*
	 * apply flow ID/VM pool/VLAN ID bits to lo hash dword, we had to
	 * delay this because bit 0 of the stream should not be processed
	 * so we do not add the vlan until after bit 0 was processed
	 */
	lo_hash_dword ^= flow_vm_vlan ^ (flow_vm_vlan << 16);

	/* Process remaining 30 bit of the key */
	IXGBE_COMPUTE_SIG_HASH_ITERATION(1);
	IXGBE_COMPUTE_SIG_HASH_ITERATION(2);
	IXGBE_COMPUTE_SIG_HASH_ITERATION(3);
	IXGBE_COMPUTE_SIG_HASH_ITERATION(4);
	IXGBE_COMPUTE_SIG_HASH_ITERATION(5);
	IXGBE_COMPUTE_SIG_HASH_ITERATION(6);
	IXGBE_COMPUTE_SIG_HASH_ITERATION(7);
	IXGBE_COMPUTE_SIG_HASH_ITERATION(8);
	IXGBE_COMPUTE_SIG_HASH_ITERATION(9);
	IXGBE_COMPUTE_SIG_HASH_ITERATION(10);
	IXGBE_COMPUTE_SIG_HASH_ITERATION(11);
	IXGBE_COMPUTE_SIG_HASH_ITERATION(12);
	IXGBE_COMPUTE_SIG_HASH_ITERATION(13);
	IXGBE_COMPUTE_SIG_HASH_ITERATION(14);
	IXGBE_COMPUTE_SIG_HASH_ITERATION(15);

	/* combine common_hash result with signature and bucket hashes */
	bucket_hash ^= common_hash;
	bucket_hash &= IXGBE_ATR_HASH_MASK;

	sig_hash ^= common_hash << 16;
	sig_hash &= IXGBE_ATR_HASH_MASK << 16;

	/* return completed signature hash */
	return sig_hash ^ bucket_hash;
}

/**
 *  ixgbe_atr_add_signature_filter_82599 - Adds a signature hash filter
 *  @hw: pointer to hardware structure
 *  @input: unique input dword
 *  @common: compressed common input dword
 *  @queue: queue index to direct traffic to
 **/
s32 ixgbe_fdir_add_signature_filter_82599(struct ixgbe_hw *hw,
                                          union ixgbe_atr_hash_dword input,
                                          union ixgbe_atr_hash_dword common,
                                          u8 queue)
{
	u64  fdirhashcmd;
	u32  fdircmd;

	/*
	 * Get the flow_type in order to program FDIRCMD properly
	 * lowest 2 bits are FDIRCMD.L4TYPE, third lowest bit is FDIRCMD.IPV6
	 */
	switch (input.formatted.flow_type) {
	case IXGBE_ATR_FLOW_TYPE_TCPV4:
	case IXGBE_ATR_FLOW_TYPE_UDPV4:
	case IXGBE_ATR_FLOW_TYPE_SCTPV4:
	case IXGBE_ATR_FLOW_TYPE_TCPV6:
	case IXGBE_ATR_FLOW_TYPE_UDPV6:
	case IXGBE_ATR_FLOW_TYPE_SCTPV6:
		break;
	default:
		hw_dbg(hw, " Error on flow type input\n");
		return IXGBE_ERR_CONFIG;
	}

	/* configure FDIRCMD register */
	fdircmd = IXGBE_FDIRCMD_CMD_ADD_FLOW | IXGBE_FDIRCMD_FILTER_UPDATE |
	          IXGBE_FDIRCMD_LAST | IXGBE_FDIRCMD_QUEUE_EN;
	fdircmd |= input.formatted.flow_type << IXGBE_FDIRCMD_FLOW_TYPE_SHIFT;
	fdircmd |= (u32)queue << IXGBE_FDIRCMD_RX_QUEUE_SHIFT;

	/*
	 * The lower 32-bits of fdirhashcmd is for FDIRHASH, the upper 32-bits
	 * is for FDIRCMD.  Then do a 64-bit register write from FDIRHASH.
	 */
	fdirhashcmd = (u64)fdircmd << 32;
	fdirhashcmd |= ixgbe_atr_compute_sig_hash_82599(input, common);

	IXGBE_WRITE_REG64(hw, IXGBE_FDIRHASH, fdirhashcmd);

	hw_dbg(hw, "Tx Queue=%x hash=%x\n", queue, (u32)fdirhashcmd);

	return 0;
}

/**
 *  ixgbe_get_fdirtcpm_82599 - generate a tcp port from atr_input_masks
 *  @input_mask: mask to be bit swapped
 *
 *  The source and destination port masks for flow director are bit swapped
 *  in that bit 15 effects bit 0, 14 effects 1, 13, 2 etc.  In order to
 *  generate a correctly swapped value we need to bit swap the mask and that
 *  is what is accomplished by this function.
 **/
static u32 ixgbe_get_fdirtcpm_82599(struct ixgbe_atr_input_masks *input_masks)
{
	u32 mask = ntohs(input_masks->dst_port_mask);
	mask <<= IXGBE_FDIRTCPM_DPORTM_SHIFT;
	mask |= ntohs(input_masks->src_port_mask);
	mask = ((mask & 0x55555555) << 1) | ((mask & 0xAAAAAAAA) >> 1);
	mask = ((mask & 0x33333333) << 2) | ((mask & 0xCCCCCCCC) >> 2);
	mask = ((mask & 0x0F0F0F0F) << 4) | ((mask & 0xF0F0F0F0) >> 4);
	return ((mask & 0x00FF00FF) << 8) | ((mask & 0xFF00FF00) >> 8);
}

/*
 * These two macros are meant to address the fact that we have registers
 * that are either all or in part big-endian.  As a result on big-endian
 * systems we will end up byte swapping the value to little-endian before
 * it is byte swapped again and written to the hardware in the original
 * big-endian format.
 */
#define IXGBE_STORE_AS_BE32(_value) \
	(((u32)(_value) >> 24) | (((u32)(_value) & 0x00FF0000) >> 8) | \
	 (((u32)(_value) & 0x0000FF00) << 8) | ((u32)(_value) << 24))

#define IXGBE_WRITE_REG_BE32(a, reg, value) \
	IXGBE_WRITE_REG((a), (reg), IXGBE_STORE_AS_BE32(ntohl(value)))

#define IXGBE_STORE_AS_BE16(_value) \
	(((u16)(_value) >> 8) | ((u16)(_value) << 8))

/**
 *  ixgbe_fdir_add_perfect_filter_82599 - Adds a perfect filter
 *  @hw: pointer to hardware structure
 *  @input: input bitstream
 *  @input_masks: bitwise masks for relevant fields
 *  @soft_id: software index into the silicon hash tables for filter storage
 *  @queue: queue index to direct traffic to
 *
 *  Note that the caller to this function must lock before calling, since the
 *  hardware writes must be protected from one another.
 **/
s32 ixgbe_fdir_add_perfect_filter_82599(struct ixgbe_hw *hw,
                                      union ixgbe_atr_input *input,
                                      struct ixgbe_atr_input_masks *input_masks,
                                      u16 soft_id, u8 queue)
{
	u32 fdirhash;
	u32 fdircmd;
	u32 fdirport, fdirtcpm;
	u32 fdirvlan;
	/* start with VLAN, flex bytes, VM pool, and IPv6 destination masked */
	u32 fdirm = IXGBE_FDIRM_VLANID | IXGBE_FDIRM_VLANP | IXGBE_FDIRM_FLEX |
		    IXGBE_FDIRM_POOL | IXGBE_FDIRM_DIPv6;

	/*
	 * Check flow_type formatting, and bail out before we touch the hardware
	 * if there's a configuration issue
	 */
	switch (input->formatted.flow_type) {
	case IXGBE_ATR_FLOW_TYPE_IPV4:
		/* use the L4 protocol mask for raw IPv4/IPv6 traffic */
		fdirm |= IXGBE_FDIRM_L4P;
	case IXGBE_ATR_FLOW_TYPE_SCTPV4:
		if (input_masks->dst_port_mask || input_masks->src_port_mask) {
			hw_dbg(hw, " Error on src/dst port mask\n");
			return IXGBE_ERR_CONFIG;
		}
	case IXGBE_ATR_FLOW_TYPE_TCPV4:
	case IXGBE_ATR_FLOW_TYPE_UDPV4:
		break;
	default:
		hw_dbg(hw, " Error on flow type input\n");
		return IXGBE_ERR_CONFIG;
	}

	/*
	 * Program the relevant mask registers.  If src/dst_port or src/dst_addr
	 * are zero, then assume a full mask for that field.  Also assume that
	 * a VLAN of 0 is unspecified, so mask that out as well.  L4type
	 * cannot be masked out in this implementation.
	 *
	 * This also assumes IPv4 only.  IPv6 masking isn't supported at this
	 * point in time.
	 */

	/* Program FDIRM */
	switch (ntohs(input_masks->vlan_id_mask) & 0xEFFF) {
	case 0xEFFF:
		/* Unmask VLAN ID - bit 0 and fall through to unmask prio */
		fdirm &= ~IXGBE_FDIRM_VLANID;
	case 0xE000:
		/* Unmask VLAN prio - bit 1 */
		fdirm &= ~IXGBE_FDIRM_VLANP;
		break;
	case 0x0FFF:
		/* Unmask VLAN ID - bit 0 */
		fdirm &= ~IXGBE_FDIRM_VLANID;
		break;
	case 0x0000:
		/* do nothing, vlans already masked */
		break;
	default:
		hw_dbg(hw, " Error on VLAN mask\n");
		return IXGBE_ERR_CONFIG;
	}

	if (input_masks->flex_mask & 0xFFFF) {
		if ((input_masks->flex_mask & 0xFFFF) != 0xFFFF) {
			hw_dbg(hw, " Error on flexible byte mask\n");
			return IXGBE_ERR_CONFIG;
		}
		/* Unmask Flex Bytes - bit 4 */
		fdirm &= ~IXGBE_FDIRM_FLEX;
	}

	/* Now mask VM pool and destination IPv6 - bits 5 and 2 */
	IXGBE_WRITE_REG(hw, IXGBE_FDIRM, fdirm);

	/* store the TCP/UDP port masks, bit reversed from port layout */
	fdirtcpm = ixgbe_get_fdirtcpm_82599(input_masks);

	/* write both the same so that UDP and TCP use the same mask */
	IXGBE_WRITE_REG(hw, IXGBE_FDIRTCPM, ~fdirtcpm);
	IXGBE_WRITE_REG(hw, IXGBE_FDIRUDPM, ~fdirtcpm);

	/* store source and destination IP masks (big-enian) */
	IXGBE_WRITE_REG_BE32(hw, IXGBE_FDIRSIP4M,
			     ~input_masks->src_ip_mask[0]);
	IXGBE_WRITE_REG_BE32(hw, IXGBE_FDIRDIP4M,
			     ~input_masks->dst_ip_mask[0]);

	/* Apply masks to input data */
	input->formatted.vlan_id &= input_masks->vlan_id_mask;
	input->formatted.flex_bytes &= input_masks->flex_mask;
	input->formatted.src_port &= input_masks->src_port_mask;
	input->formatted.dst_port &= input_masks->dst_port_mask;
	input->formatted.src_ip[0] &= input_masks->src_ip_mask[0];
	input->formatted.dst_ip[0] &= input_masks->dst_ip_mask[0];

	/* record vlan (little-endian) and flex_bytes(big-endian) */
	fdirvlan =
		IXGBE_STORE_AS_BE16(ntohs(input->formatted.flex_bytes));
	fdirvlan <<= IXGBE_FDIRVLAN_FLEX_SHIFT;
	fdirvlan |= ntohs(input->formatted.vlan_id);
	IXGBE_WRITE_REG(hw, IXGBE_FDIRVLAN, fdirvlan);

	/* record source and destination port (little-endian)*/
	fdirport = ntohs(input->formatted.dst_port);
	fdirport <<= IXGBE_FDIRPORT_DESTINATION_SHIFT;
	fdirport |= ntohs(input->formatted.src_port);
	IXGBE_WRITE_REG(hw, IXGBE_FDIRPORT, fdirport);

	/* record the first 32 bits of the destination address (big-endian) */
	IXGBE_WRITE_REG_BE32(hw, IXGBE_FDIRIPDA, input->formatted.dst_ip[0]);

	/* record the source address (big-endian) */
	IXGBE_WRITE_REG_BE32(hw, IXGBE_FDIRIPSA, input->formatted.src_ip[0]);

	/* configure FDIRCMD register */
	fdircmd = IXGBE_FDIRCMD_CMD_ADD_FLOW | IXGBE_FDIRCMD_FILTER_UPDATE |
		  IXGBE_FDIRCMD_LAST | IXGBE_FDIRCMD_QUEUE_EN;
	fdircmd |= input->formatted.flow_type << IXGBE_FDIRCMD_FLOW_TYPE_SHIFT;
	fdircmd |= (u32)queue << IXGBE_FDIRCMD_RX_QUEUE_SHIFT;

	/* we only want the bucket hash so drop the upper 16 bits */
	fdirhash = ixgbe_atr_compute_hash_82599(input,
						IXGBE_ATR_BUCKET_HASH_KEY);
	fdirhash |= soft_id << IXGBE_FDIRHASH_SIG_SW_INDEX_SHIFT;

	IXGBE_WRITE_REG(hw, IXGBE_FDIRHASH, fdirhash);
	IXGBE_WRITE_REG(hw, IXGBE_FDIRCMD, fdircmd);

	return 0;
}

/**
 *  ixgbe_read_analog_reg8_82599 - Reads 8 bit Omer analog register
 *  @hw: pointer to hardware structure
 *  @reg: analog register to read
 *  @val: read value
 *
 *  Performs read operation to Omer analog register specified.
 **/
static s32 ixgbe_read_analog_reg8_82599(struct ixgbe_hw *hw, u32 reg, u8 *val)
{
	u32  core_ctl;

	IXGBE_WRITE_REG(hw, IXGBE_CORECTL, IXGBE_CORECTL_WRITE_CMD |
	                (reg << 8));
	IXGBE_WRITE_FLUSH(hw);
	udelay(10);
	core_ctl = IXGBE_READ_REG(hw, IXGBE_CORECTL);
	*val = (u8)core_ctl;

	return 0;
}

/**
 *  ixgbe_write_analog_reg8_82599 - Writes 8 bit Omer analog register
 *  @hw: pointer to hardware structure
 *  @reg: atlas register to write
 *  @val: value to write
 *
 *  Performs write operation to Omer analog register specified.
 **/
static s32 ixgbe_write_analog_reg8_82599(struct ixgbe_hw *hw, u32 reg, u8 val)
{
	u32  core_ctl;

	core_ctl = (reg << 8) | val;
	IXGBE_WRITE_REG(hw, IXGBE_CORECTL, core_ctl);
	IXGBE_WRITE_FLUSH(hw);
	udelay(10);

	return 0;
}

/**
 *  ixgbe_start_hw_82599 - Prepare hardware for Tx/Rx
 *  @hw: pointer to hardware structure
 *
 *  Starts the hardware using the generic start_hw function
 *  and the generation start_hw function.
 *  Then performs revision-specific operations, if any.
 **/
static s32 ixgbe_start_hw_82599(struct ixgbe_hw *hw)
{
	s32 ret_val = 0;

	ret_val = ixgbe_start_hw_generic(hw);
	if (ret_val != 0)
		goto out;

	ret_val = ixgbe_start_hw_gen2(hw);
	if (ret_val != 0)
		goto out;

	/* We need to run link autotry after the driver loads */
	hw->mac.autotry_restart = true;
	hw->mac.rx_pb_size = IXGBE_82599_RX_PB_SIZE;

	if (ret_val == 0)
		ret_val = ixgbe_verify_fw_version_82599(hw);
out:
	return ret_val;
}

/**
 *  ixgbe_identify_phy_82599 - Get physical layer module
 *  @hw: pointer to hardware structure
 *
 *  Determines the physical layer module found on the current adapter.
 *  If PHY already detected, maintains current PHY type in hw struct,
 *  otherwise executes the PHY detection routine.
 **/
static s32 ixgbe_identify_phy_82599(struct ixgbe_hw *hw)
{
	s32 status = IXGBE_ERR_PHY_ADDR_INVALID;

	/* Detect PHY if not unknown - returns success if already detected. */
	status = ixgbe_identify_phy_generic(hw);
	if (status != 0) {
		/* 82599 10GBASE-T requires an external PHY */
		if (hw->mac.ops.get_media_type(hw) == ixgbe_media_type_copper)
			goto out;
		else
			status = ixgbe_identify_sfp_module_generic(hw);
	}

	/* Set PHY type none if no PHY detected */
	if (hw->phy.type == ixgbe_phy_unknown) {
		hw->phy.type = ixgbe_phy_none;
		status = 0;
	}

	/* Return error if SFP module has been detected but is not supported */
	if (hw->phy.type == ixgbe_phy_sfp_unsupported)
		status = IXGBE_ERR_SFP_NOT_SUPPORTED;

out:
	return status;
}

/**
 *  ixgbe_get_supported_physical_layer_82599 - Returns physical layer type
 *  @hw: pointer to hardware structure
 *
 *  Determines physical layer capabilities of the current configuration.
 **/
static u32 ixgbe_get_supported_physical_layer_82599(struct ixgbe_hw *hw)
{
	u32 physical_layer = IXGBE_PHYSICAL_LAYER_UNKNOWN;
	u32 autoc = IXGBE_READ_REG(hw, IXGBE_AUTOC);
	u32 autoc2 = IXGBE_READ_REG(hw, IXGBE_AUTOC2);
	u32 pma_pmd_10g_serial = autoc2 & IXGBE_AUTOC2_10G_SERIAL_PMA_PMD_MASK;
	u32 pma_pmd_10g_parallel = autoc & IXGBE_AUTOC_10G_PMA_PMD_MASK;
	u32 pma_pmd_1g = autoc & IXGBE_AUTOC_1G_PMA_PMD_MASK;
	u16 ext_ability = 0;
	u8 comp_codes_10g = 0;
	u8 comp_codes_1g = 0;

	hw->phy.ops.identify(hw);

	switch (hw->phy.type) {
	case ixgbe_phy_tn:
	case ixgbe_phy_aq:
	case ixgbe_phy_cu_unknown:
		hw->phy.ops.read_reg(hw, MDIO_PMA_EXTABLE, MDIO_MMD_PMAPMD,
							 &ext_ability);
		if (ext_ability & MDIO_PMA_EXTABLE_10GBT)
			physical_layer |= IXGBE_PHYSICAL_LAYER_10GBASE_T;
		if (ext_ability & MDIO_PMA_EXTABLE_1000BT)
			physical_layer |= IXGBE_PHYSICAL_LAYER_1000BASE_T;
		if (ext_ability & MDIO_PMA_EXTABLE_100BTX)
			physical_layer |= IXGBE_PHYSICAL_LAYER_100BASE_TX;
		goto out;
	default:
		break;
	}

	switch (autoc & IXGBE_AUTOC_LMS_MASK) {
	case IXGBE_AUTOC_LMS_1G_AN:
	case IXGBE_AUTOC_LMS_1G_LINK_NO_AN:
		if (pma_pmd_1g == IXGBE_AUTOC_1G_KX_BX) {
			physical_layer = IXGBE_PHYSICAL_LAYER_1000BASE_KX |
			    IXGBE_PHYSICAL_LAYER_1000BASE_BX;
			goto out;
		} else
			/* SFI mode so read SFP module */
			goto sfp_check;
		break;
	case IXGBE_AUTOC_LMS_10G_LINK_NO_AN:
		if (pma_pmd_10g_parallel == IXGBE_AUTOC_10G_CX4)
			physical_layer = IXGBE_PHYSICAL_LAYER_10GBASE_CX4;
		else if (pma_pmd_10g_parallel == IXGBE_AUTOC_10G_KX4)
			physical_layer = IXGBE_PHYSICAL_LAYER_10GBASE_KX4;
		else if (pma_pmd_10g_parallel == IXGBE_AUTOC_10G_XAUI)
			physical_layer = IXGBE_PHYSICAL_LAYER_10GBASE_XAUI;
		goto out;
		break;
	case IXGBE_AUTOC_LMS_10G_SERIAL:
		if (pma_pmd_10g_serial == IXGBE_AUTOC2_10G_KR) {
			physical_layer = IXGBE_PHYSICAL_LAYER_10GBASE_KR;
			goto out;
		} else if (pma_pmd_10g_serial == IXGBE_AUTOC2_10G_SFI)
			goto sfp_check;
		break;
	case IXGBE_AUTOC_LMS_KX4_KX_KR:
	case IXGBE_AUTOC_LMS_KX4_KX_KR_1G_AN:
		if (autoc & IXGBE_AUTOC_KX_SUPP)
			physical_layer |= IXGBE_PHYSICAL_LAYER_1000BASE_KX;
		if (autoc & IXGBE_AUTOC_KX4_SUPP)
			physical_layer |= IXGBE_PHYSICAL_LAYER_10GBASE_KX4;
		if (autoc & IXGBE_AUTOC_KR_SUPP)
			physical_layer |= IXGBE_PHYSICAL_LAYER_10GBASE_KR;
		goto out;
		break;
	default:
		goto out;
		break;
	}

sfp_check:
	/* SFP check must be done last since DA modules are sometimes used to
	 * test KR mode -  we need to id KR mode correctly before SFP module.
	 * Call identify_sfp because the pluggable module may have changed */
	hw->phy.ops.identify_sfp(hw);
	if (hw->phy.sfp_type == ixgbe_sfp_type_not_present)
		goto out;

	switch (hw->phy.type) {
	case ixgbe_phy_sfp_passive_tyco:
	case ixgbe_phy_sfp_passive_unknown:
		physical_layer = IXGBE_PHYSICAL_LAYER_SFP_PLUS_CU;
		break;
	case ixgbe_phy_sfp_ftl_active:
	case ixgbe_phy_sfp_active_unknown:
		physical_layer = IXGBE_PHYSICAL_LAYER_SFP_ACTIVE_DA;
		break;
	case ixgbe_phy_sfp_avago:
	case ixgbe_phy_sfp_ftl:
	case ixgbe_phy_sfp_intel:
	case ixgbe_phy_sfp_unknown:
		hw->phy.ops.read_i2c_eeprom(hw,
		      IXGBE_SFF_1GBE_COMP_CODES, &comp_codes_1g);
		hw->phy.ops.read_i2c_eeprom(hw,
		      IXGBE_SFF_10GBE_COMP_CODES, &comp_codes_10g);
		if (comp_codes_10g & IXGBE_SFF_10GBASESR_CAPABLE)
			physical_layer = IXGBE_PHYSICAL_LAYER_10GBASE_SR;
		else if (comp_codes_10g & IXGBE_SFF_10GBASELR_CAPABLE)
			physical_layer = IXGBE_PHYSICAL_LAYER_10GBASE_LR;
		else if (comp_codes_1g & IXGBE_SFF_1GBASET_CAPABLE)
			physical_layer = IXGBE_PHYSICAL_LAYER_1000BASE_T;
		break;
	default:
		break;
	}

out:
	return physical_layer;
}

/**
 *  ixgbe_enable_rx_dma_82599 - Enable the Rx DMA unit on 82599
 *  @hw: pointer to hardware structure
 *  @regval: register value to write to RXCTRL
 *
 *  Enables the Rx DMA unit for 82599
 **/
static s32 ixgbe_enable_rx_dma_82599(struct ixgbe_hw *hw, u32 regval)
{
#define IXGBE_MAX_SECRX_POLL 30
	int i;
	int secrxreg;

	/*
	 * Workaround for 82599 silicon errata when enabling the Rx datapath.
	 * If traffic is incoming before we enable the Rx unit, it could hang
	 * the Rx DMA unit.  Therefore, make sure the security engine is
	 * completely disabled prior to enabling the Rx unit.
	 */
	secrxreg = IXGBE_READ_REG(hw, IXGBE_SECRXCTRL);
	secrxreg |= IXGBE_SECRXCTRL_RX_DIS;
	IXGBE_WRITE_REG(hw, IXGBE_SECRXCTRL, secrxreg);
	for (i = 0; i < IXGBE_MAX_SECRX_POLL; i++) {
		secrxreg = IXGBE_READ_REG(hw, IXGBE_SECRXSTAT);
		if (secrxreg & IXGBE_SECRXSTAT_SECRX_RDY)
			break;
		else
			/* Use interrupt-safe sleep just in case */
			udelay(10);
	}

	/* For informational purposes only */
	if (i >= IXGBE_MAX_SECRX_POLL)
		hw_dbg(hw, "Rx unit being enabled before security "
		       "path fully disabled.  Continuing with init.\n");

	IXGBE_WRITE_REG(hw, IXGBE_RXCTRL, regval);
	secrxreg = IXGBE_READ_REG(hw, IXGBE_SECRXCTRL);
	secrxreg &= ~IXGBE_SECRXCTRL_RX_DIS;
	IXGBE_WRITE_REG(hw, IXGBE_SECRXCTRL, secrxreg);
	IXGBE_WRITE_FLUSH(hw);

	return 0;
}

/**
 *  ixgbe_verify_fw_version_82599 - verify fw version for 82599
 *  @hw: pointer to hardware structure
 *
 *  Verifies that installed the firmware version is 0.6 or higher
 *  for SFI devices. All 82599 SFI devices should have version 0.6 or higher.
 *
 *  Returns IXGBE_ERR_EEPROM_VERSION if the FW is not present or
 *  if the FW version is not supported.
 **/
static s32 ixgbe_verify_fw_version_82599(struct ixgbe_hw *hw)
{
	s32 status = IXGBE_ERR_EEPROM_VERSION;
	u16 fw_offset, fw_ptp_cfg_offset;
	u16 fw_version = 0;

	/* firmware check is only necessary for SFI devices */
	if (hw->phy.media_type != ixgbe_media_type_fiber) {
		status = 0;
		goto fw_version_out;
	}

	/* get the offset to the Firmware Module block */
	hw->eeprom.ops.read(hw, IXGBE_FW_PTR, &fw_offset);

	if ((fw_offset == 0) || (fw_offset == 0xFFFF))
		goto fw_version_out;

	/* get the offset to the Pass Through Patch Configuration block */
	hw->eeprom.ops.read(hw, (fw_offset +
	                         IXGBE_FW_PASSTHROUGH_PATCH_CONFIG_PTR),
	                         &fw_ptp_cfg_offset);

	if ((fw_ptp_cfg_offset == 0) || (fw_ptp_cfg_offset == 0xFFFF))
		goto fw_version_out;

	/* get the firmware version */
	hw->eeprom.ops.read(hw, (fw_ptp_cfg_offset +
	                         IXGBE_FW_PATCH_VERSION_4),
	                         &fw_version);

	if (fw_version > 0x5)
		status = 0;

fw_version_out:
	return status;
}

/**
 *  ixgbe_verify_lesm_fw_enabled_82599 - Checks LESM FW module state.
 *  @hw: pointer to hardware structure
 *
 *  Returns true if the LESM FW module is present and enabled. Otherwise
 *  returns false. Smart Speed must be disabled if LESM FW module is enabled.
 **/
static bool ixgbe_verify_lesm_fw_enabled_82599(struct ixgbe_hw *hw)
{
	bool lesm_enabled = false;
	u16 fw_offset, fw_lesm_param_offset, fw_lesm_state;
	s32 status;

	/* get the offset to the Firmware Module block */
	status = hw->eeprom.ops.read(hw, IXGBE_FW_PTR, &fw_offset);

	if ((status != 0) ||
	    (fw_offset == 0) || (fw_offset == 0xFFFF))
		goto out;

	/* get the offset to the LESM Parameters block */
	status = hw->eeprom.ops.read(hw, (fw_offset +
				     IXGBE_FW_LESM_PARAMETERS_PTR),
				     &fw_lesm_param_offset);

	if ((status != 0) ||
	    (fw_lesm_param_offset == 0) || (fw_lesm_param_offset == 0xFFFF))
		goto out;

	/* get the lesm state word */
	status = hw->eeprom.ops.read(hw, (fw_lesm_param_offset +
				     IXGBE_FW_LESM_STATE_1),
				     &fw_lesm_state);

	if ((status == 0) &&
	    (fw_lesm_state & IXGBE_FW_LESM_STATE_ENABLED))
		lesm_enabled = true;

out:
	return lesm_enabled;
}

/**
 *  ixgbe_read_eeprom_buffer_82599 - Read EEPROM word(s) using
 *  fastest available method
 *
 *  @hw: pointer to hardware structure
 *  @offset: offset of  word in EEPROM to read
 *  @words: number of words
 *  @data: word(s) read from the EEPROM
 *
 *  Retrieves 16 bit word(s) read from EEPROM
 **/
static s32 ixgbe_read_eeprom_buffer_82599(struct ixgbe_hw *hw, u16 offset,
					  u16 words, u16 *data)
{
	struct ixgbe_eeprom_info *eeprom = &hw->eeprom;
	s32 ret_val = IXGBE_ERR_CONFIG;

	/*
	 * If EEPROM is detected and can be addressed using 14 bits,
	 * use EERD otherwise use bit bang
	 */
	if ((eeprom->type == ixgbe_eeprom_spi) &&
	    (offset + (words - 1) <= IXGBE_EERD_MAX_ADDR))
		ret_val = ixgbe_read_eerd_buffer_generic(hw, offset, words,
							 data);
	else
		ret_val = ixgbe_read_eeprom_buffer_bit_bang_generic(hw, offset,
								    words,
								    data);

	return ret_val;
}

/**
 *  ixgbe_read_eeprom_82599 - Read EEPROM word using
 *  fastest available method
 *
 *  @hw: pointer to hardware structure
 *  @offset: offset of  word in the EEPROM to read
 *  @data: word read from the EEPROM
 *
 *  Reads a 16 bit word from the EEPROM
 **/
static s32 ixgbe_read_eeprom_82599(struct ixgbe_hw *hw,
				   u16 offset, u16 *data)
{
	struct ixgbe_eeprom_info *eeprom = &hw->eeprom;
	s32 ret_val = IXGBE_ERR_CONFIG;

	/*
	 * If EEPROM is detected and can be addressed using 14 bits,
	 * use EERD otherwise use bit bang
	 */
	if ((eeprom->type == ixgbe_eeprom_spi) &&
	    (offset <= IXGBE_EERD_MAX_ADDR))
		ret_val = ixgbe_read_eerd_generic(hw, offset, data);
	else
		ret_val = ixgbe_read_eeprom_bit_bang_generic(hw, offset, data);

	return ret_val;
}

static struct ixgbe_mac_operations mac_ops_82599 = {
	.init_hw                = &ixgbe_init_hw_generic,
	.reset_hw               = &ixgbe_reset_hw_82599,
	.start_hw               = &ixgbe_start_hw_82599,
	.clear_hw_cntrs         = &ixgbe_clear_hw_cntrs_generic,
	.get_media_type         = &ixgbe_get_media_type_82599,
	.get_supported_physical_layer = &ixgbe_get_supported_physical_layer_82599,
	.enable_rx_dma          = &ixgbe_enable_rx_dma_82599,
	.get_mac_addr           = &ixgbe_get_mac_addr_generic,
	.get_san_mac_addr       = &ixgbe_get_san_mac_addr_generic,
	.get_device_caps        = &ixgbe_get_device_caps_generic,
	.get_wwn_prefix         = &ixgbe_get_wwn_prefix_generic,
	.stop_adapter           = &ixgbe_stop_adapter_generic,
	.get_bus_info           = &ixgbe_get_bus_info_generic,
	.set_lan_id             = &ixgbe_set_lan_id_multi_port_pcie,
	.read_analog_reg8       = &ixgbe_read_analog_reg8_82599,
	.write_analog_reg8      = &ixgbe_write_analog_reg8_82599,
	.setup_link             = &ixgbe_setup_mac_link_82599,
	.check_link             = &ixgbe_check_mac_link_generic,
	.get_link_capabilities  = &ixgbe_get_link_capabilities_82599,
	.led_on                 = &ixgbe_led_on_generic,
	.led_off                = &ixgbe_led_off_generic,
	.blink_led_start        = &ixgbe_blink_led_start_generic,
	.blink_led_stop         = &ixgbe_blink_led_stop_generic,
	.set_rar                = &ixgbe_set_rar_generic,
	.clear_rar              = &ixgbe_clear_rar_generic,
	.set_vmdq               = &ixgbe_set_vmdq_generic,
	.clear_vmdq             = &ixgbe_clear_vmdq_generic,
	.init_rx_addrs          = &ixgbe_init_rx_addrs_generic,
	.update_mc_addr_list    = &ixgbe_update_mc_addr_list_generic,
	.enable_mc              = &ixgbe_enable_mc_generic,
	.disable_mc             = &ixgbe_disable_mc_generic,
	.clear_vfta             = &ixgbe_clear_vfta_generic,
	.set_vfta               = &ixgbe_set_vfta_generic,
	.fc_enable              = &ixgbe_fc_enable_generic,
	.init_uta_tables        = &ixgbe_init_uta_tables_generic,
	.setup_sfp              = &ixgbe_setup_sfp_modules_82599,
	.set_mac_anti_spoofing  = &ixgbe_set_mac_anti_spoofing,
	.set_vlan_anti_spoofing = &ixgbe_set_vlan_anti_spoofing,
	.acquire_swfw_sync      = &ixgbe_acquire_swfw_sync,
	.release_swfw_sync      = &ixgbe_release_swfw_sync,

};

static struct ixgbe_eeprom_operations eeprom_ops_82599 = {
	.init_params		= &ixgbe_init_eeprom_params_generic,
	.read			= &ixgbe_read_eeprom_82599,
	.read_buffer		= &ixgbe_read_eeprom_buffer_82599,
	.write			= &ixgbe_write_eeprom_generic,
	.write_buffer		= &ixgbe_write_eeprom_buffer_bit_bang_generic,
	.calc_checksum		= &ixgbe_calc_eeprom_checksum_generic,
	.validate_checksum	= &ixgbe_validate_eeprom_checksum_generic,
	.update_checksum	= &ixgbe_update_eeprom_checksum_generic,
};

static struct ixgbe_phy_operations phy_ops_82599 = {
	.identify		= &ixgbe_identify_phy_82599,
	.identify_sfp		= &ixgbe_identify_sfp_module_generic,
	.init			= &ixgbe_init_phy_ops_82599,
	.reset			= &ixgbe_reset_phy_generic,
	.read_reg		= &ixgbe_read_phy_reg_generic,
	.write_reg		= &ixgbe_write_phy_reg_generic,
	.setup_link		= &ixgbe_setup_phy_link_generic,
	.setup_link_speed	= &ixgbe_setup_phy_link_speed_generic,
	.read_i2c_byte		= &ixgbe_read_i2c_byte_generic,
	.write_i2c_byte		= &ixgbe_write_i2c_byte_generic,
	.read_i2c_eeprom	= &ixgbe_read_i2c_eeprom_generic,
	.write_i2c_eeprom	= &ixgbe_write_i2c_eeprom_generic,
	.check_overtemp		= &ixgbe_tn_check_overtemp,
};

struct ixgbe_info ixgbe_82599_info = {
	.mac                    = ixgbe_mac_82599EB,
	.get_invariants         = &ixgbe_get_invariants_82599,
	.mac_ops                = &mac_ops_82599,
	.eeprom_ops             = &eeprom_ops_82599,
	.phy_ops                = &phy_ops_82599,
	.mbx_ops                = &mbx_ops_generic,
};
