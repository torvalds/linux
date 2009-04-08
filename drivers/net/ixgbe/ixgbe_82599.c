/*******************************************************************************

  Intel 10 Gigabit PCI Express Linux driver
  Copyright(c) 1999 - 2009 Intel Corporation.

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

#define IXGBE_82599_MAX_TX_QUEUES 128
#define IXGBE_82599_MAX_RX_QUEUES 128
#define IXGBE_82599_RAR_ENTRIES   128
#define IXGBE_82599_MC_TBL_SIZE   128
#define IXGBE_82599_VFT_TBL_SIZE  128

s32 ixgbe_get_link_capabilities_82599(struct ixgbe_hw *hw,
                                      ixgbe_link_speed *speed,
                                      bool *autoneg);
enum ixgbe_media_type ixgbe_get_media_type_82599(struct ixgbe_hw *hw);
s32 ixgbe_setup_mac_link_multispeed_fiber(struct ixgbe_hw *hw);
s32 ixgbe_setup_mac_link_speed_multispeed_fiber(struct ixgbe_hw *hw,
                                     ixgbe_link_speed speed, bool autoneg,
                                     bool autoneg_wait_to_complete);
s32 ixgbe_setup_mac_link_82599(struct ixgbe_hw *hw);
s32 ixgbe_check_mac_link_82599(struct ixgbe_hw *hw,
                               ixgbe_link_speed *speed,
                               bool *link_up, bool link_up_wait_to_complete);
s32 ixgbe_setup_mac_link_speed_82599(struct ixgbe_hw *hw,
                                     ixgbe_link_speed speed,
                                     bool autoneg,
                                     bool autoneg_wait_to_complete);
static s32 ixgbe_get_copper_link_capabilities_82599(struct ixgbe_hw *hw,
                                             ixgbe_link_speed *speed,
                                             bool *autoneg);
static s32 ixgbe_setup_copper_link_82599(struct ixgbe_hw *hw);
static s32 ixgbe_setup_copper_link_speed_82599(struct ixgbe_hw *hw,
                                               ixgbe_link_speed speed,
                                               bool autoneg,
                                               bool autoneg_wait_to_complete);
s32 ixgbe_reset_hw_82599(struct ixgbe_hw *hw);
s32 ixgbe_set_vmdq_82599(struct ixgbe_hw *hw, u32 rar, u32 vmdq);
s32 ixgbe_clear_vmdq_82599(struct ixgbe_hw *hw, u32 rar, u32 vmdq);
s32 ixgbe_set_vfta_82599(struct ixgbe_hw *hw, u32 vlan,
                         u32 vind, bool vlan_on);
s32 ixgbe_clear_vfta_82599(struct ixgbe_hw *hw);
s32 ixgbe_blink_led_stop_82599(struct ixgbe_hw *hw, u32 index);
s32 ixgbe_blink_led_start_82599(struct ixgbe_hw *hw, u32 index);
s32 ixgbe_init_uta_tables_82599(struct ixgbe_hw *hw);
s32 ixgbe_read_analog_reg8_82599(struct ixgbe_hw *hw, u32 reg, u8 *val);
s32 ixgbe_write_analog_reg8_82599(struct ixgbe_hw *hw, u32 reg, u8 val);
s32 ixgbe_start_hw_rev_0_82599(struct ixgbe_hw *hw);
s32 ixgbe_identify_phy_82599(struct ixgbe_hw *hw);
s32 ixgbe_start_hw_82599(struct ixgbe_hw *hw);
u32 ixgbe_get_supported_physical_layer_82599(struct ixgbe_hw *hw);

void ixgbe_init_mac_link_ops_82599(struct ixgbe_hw *hw)
{
	struct ixgbe_mac_info *mac = &hw->mac;
	if (hw->phy.multispeed_fiber) {
		/* Set up dual speed SFP+ support */
		mac->ops.setup_link =
		          &ixgbe_setup_mac_link_multispeed_fiber;
		mac->ops.setup_link_speed =
		          &ixgbe_setup_mac_link_speed_multispeed_fiber;
	} else {
		mac->ops.setup_link =
		          &ixgbe_setup_mac_link_82599;
		mac->ops.setup_link_speed =
		          &ixgbe_setup_mac_link_speed_82599;
	}
}

s32 ixgbe_setup_sfp_modules_82599(struct ixgbe_hw *hw)
{
	s32 ret_val = 0;
	u16 list_offset, data_offset, data_value;

	if (hw->phy.sfp_type != ixgbe_sfp_type_unknown) {
		ixgbe_init_mac_link_ops_82599(hw);
		ret_val = ixgbe_get_sfp_init_sequence_offsets(hw, &list_offset,
		                                              &data_offset);

		if (ret_val != 0)
			goto setup_sfp_out;

		hw->eeprom.ops.read(hw, ++data_offset, &data_value);
		while (data_value != 0xffff) {
			IXGBE_WRITE_REG(hw, IXGBE_CORECTL, data_value);
			IXGBE_WRITE_FLUSH(hw);
			hw->eeprom.ops.read(hw, ++data_offset, &data_value);
		}
		/* Now restart DSP */
		IXGBE_WRITE_REG(hw, IXGBE_CORECTL, 0x00000102);
		IXGBE_WRITE_REG(hw, IXGBE_CORECTL, 0x00000b1d);
		IXGBE_WRITE_FLUSH(hw);
	}

setup_sfp_out:
	return ret_val;
}

/**
 *  ixgbe_get_pcie_msix_count_82599 - Gets MSI-X vector count
 *  @hw: pointer to hardware structure
 *
 *  Read PCIe configuration space, and get the MSI-X vector count from
 *  the capabilities table.
 **/
u32 ixgbe_get_pcie_msix_count_82599(struct ixgbe_hw *hw)
{
	struct ixgbe_adapter *adapter = hw->back;
	u16 msix_count;
	pci_read_config_word(adapter->pdev, IXGBE_PCIE_MSIX_82599_CAPS,
	                     &msix_count);
	msix_count &= IXGBE_PCIE_MSIX_TBL_SZ_MASK;

	/* MSI-X count is zero-based in HW, so increment to give proper value */
	msix_count++;

	return msix_count;
}

static s32 ixgbe_get_invariants_82599(struct ixgbe_hw *hw)
{
	struct ixgbe_mac_info *mac = &hw->mac;
	struct ixgbe_phy_info *phy = &hw->phy;
	s32 ret_val;

	/* Set the bus information prior to PHY identification */
	mac->ops.get_bus_info(hw);

	/* Call PHY identify routine to get the Cu or SFI phy type */
	ret_val = phy->ops.identify(hw);

	if (ret_val == IXGBE_ERR_SFP_NOT_SUPPORTED)
		goto get_invariants_out;

	ixgbe_init_mac_link_ops_82599(hw);

	/* Setup SFP module if there is one present. */
	ret_val = mac->ops.setup_sfp(hw);

	/* If copper media, overwrite with copper function pointers */
	if (mac->ops.get_media_type(hw) == ixgbe_media_type_copper) {
		mac->ops.setup_link = &ixgbe_setup_copper_link_82599;
		mac->ops.setup_link_speed =
		                  &ixgbe_setup_copper_link_speed_82599;
		mac->ops.get_link_capabilities =
		                  &ixgbe_get_copper_link_capabilities_82599;
	}

	/* PHY Init */
	switch (hw->phy.type) {
	case ixgbe_phy_tn:
		phy->ops.check_link = &ixgbe_check_phy_link_tnx;
		phy->ops.get_firmware_version =
		                  &ixgbe_get_phy_firmware_version_tnx;
		break;
	default:
		break;
	}

	mac->mcft_size = IXGBE_82599_MC_TBL_SIZE;
	mac->vft_size = IXGBE_82599_VFT_TBL_SIZE;
	mac->num_rar_entries = IXGBE_82599_RAR_ENTRIES;
	mac->max_rx_queues = IXGBE_82599_MAX_RX_QUEUES;
	mac->max_tx_queues = IXGBE_82599_MAX_TX_QUEUES;
	mac->max_msix_vectors = ixgbe_get_pcie_msix_count_82599(hw);

get_invariants_out:
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
s32 ixgbe_get_link_capabilities_82599(struct ixgbe_hw *hw,
                                      ixgbe_link_speed *speed,
                                      bool *negotiation)
{
	s32 status = 0;

	switch (hw->mac.orig_autoc & IXGBE_AUTOC_LMS_MASK) {
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
		if (hw->mac.orig_autoc & IXGBE_AUTOC_KR_SUPP)
			*speed |= IXGBE_LINK_SPEED_10GB_FULL;
		if (hw->mac.orig_autoc & IXGBE_AUTOC_KX4_SUPP)
			*speed |= IXGBE_LINK_SPEED_10GB_FULL;
		if (hw->mac.orig_autoc & IXGBE_AUTOC_KX_SUPP)
			*speed |= IXGBE_LINK_SPEED_1GB_FULL;
		*negotiation = true;
		break;

	case IXGBE_AUTOC_LMS_KX4_KX_KR_SGMII:
		*speed = IXGBE_LINK_SPEED_100_FULL;
		if (hw->mac.orig_autoc & IXGBE_AUTOC_KR_SUPP)
			*speed |= IXGBE_LINK_SPEED_10GB_FULL;
		if (hw->mac.orig_autoc & IXGBE_AUTOC_KX4_SUPP)
			*speed |= IXGBE_LINK_SPEED_10GB_FULL;
		if (hw->mac.orig_autoc & IXGBE_AUTOC_KX_SUPP)
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
 *  ixgbe_get_copper_link_capabilities_82599 - Determines link capabilities
 *  @hw: pointer to hardware structure
 *  @speed: pointer to link speed
 *  @autoneg: boolean auto-negotiation value
 *
 *  Determines the link capabilities by reading the AUTOC register.
 **/
static s32 ixgbe_get_copper_link_capabilities_82599(struct ixgbe_hw *hw,
                                                    ixgbe_link_speed *speed,
                                                    bool *autoneg)
{
	s32 status = IXGBE_ERR_LINK_SETUP;
	u16 speed_ability;

	*speed = 0;
	*autoneg = true;

	status = hw->phy.ops.read_reg(hw, IXGBE_MDIO_PHY_SPEED_ABILITY,
	                              IXGBE_MDIO_PMA_PMD_DEV_TYPE,
	                              &speed_ability);

	if (status == 0) {
		if (speed_ability & IXGBE_MDIO_PHY_SPEED_10G)
		    *speed |= IXGBE_LINK_SPEED_10GB_FULL;
		if (speed_ability & IXGBE_MDIO_PHY_SPEED_1G)
		    *speed |= IXGBE_LINK_SPEED_1GB_FULL;
	}

	return status;
}

/**
 *  ixgbe_get_media_type_82599 - Get media type
 *  @hw: pointer to hardware structure
 *
 *  Returns the media type (fiber, copper, backplane)
 **/
enum ixgbe_media_type ixgbe_get_media_type_82599(struct ixgbe_hw *hw)
{
	enum ixgbe_media_type media_type;

	/* Detect if there is a copper PHY attached. */
	if (hw->phy.type == ixgbe_phy_cu_unknown ||
	    hw->phy.type == ixgbe_phy_tn) {
		media_type = ixgbe_media_type_copper;
		goto out;
	}

	switch (hw->device_id) {
	case IXGBE_DEV_ID_82599:
	case IXGBE_DEV_ID_82599_KX4:
		/* Default device ID is mezzanine card KX/KX4 */
		media_type = ixgbe_media_type_backplane;
		break;
	case IXGBE_DEV_ID_82599_SFP:
		media_type = ixgbe_media_type_fiber;
		break;
	default:
		media_type = ixgbe_media_type_unknown;
		break;
	}
out:
	return media_type;
}

/**
 *  ixgbe_setup_mac_link_82599 - Setup MAC link settings
 *  @hw: pointer to hardware structure
 *
 *  Configures link settings based on values in the ixgbe_hw struct.
 *  Restarts the link.  Performs autonegotiation if needed.
 **/
s32 ixgbe_setup_mac_link_82599(struct ixgbe_hw *hw)
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
	if (hw->phy.autoneg_wait_to_complete) {
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

	/* Set up flow control */
	status = ixgbe_setup_fc_generic(hw, 0);

	/* Add delay to filter out noises during initial link setup */
	msleep(50);

	return status;
}

/**
 *  ixgbe_setup_mac_link_multispeed_fiber - Setup MAC link settings
 *  @hw: pointer to hardware structure
 *
 *  Configures link settings based on values in the ixgbe_hw struct.
 *  Restarts the link for multi-speed fiber at 1G speed, if link
 *  fails at 10G.
 *  Performs autonegotiation if needed.
 **/
s32 ixgbe_setup_mac_link_multispeed_fiber(struct ixgbe_hw *hw)
{
	s32 status = 0;
	ixgbe_link_speed link_speed = IXGBE_LINK_SPEED_82599_AUTONEG;
	status = ixgbe_setup_mac_link_speed_multispeed_fiber(hw, link_speed,
	                                                     true, true);
	return status;
}

/**
 *  ixgbe_setup_mac_link_speed_multispeed_fiber - Set MAC link speed
 *  @hw: pointer to hardware structure
 *  @speed: new link speed
 *  @autoneg: true if autonegotiation enabled
 *  @autoneg_wait_to_complete: true when waiting for completion is needed
 *
 *  Set the link speed in the AUTOC register and restarts link.
 **/
s32 ixgbe_setup_mac_link_speed_multispeed_fiber(struct ixgbe_hw *hw,
                                                ixgbe_link_speed speed,
                                                bool autoneg,
                                                bool autoneg_wait_to_complete)
{
	s32 status = 0;
	ixgbe_link_speed phy_link_speed;
	ixgbe_link_speed highest_link_speed = IXGBE_LINK_SPEED_UNKNOWN;
	u32 speedcnt = 0;
	u32 esdp_reg = IXGBE_READ_REG(hw, IXGBE_ESDP);
	bool link_up = false;
	bool negotiation;

	/* Mask off requested but non-supported speeds */
	hw->mac.ops.get_link_capabilities(hw, &phy_link_speed, &negotiation);
	speed &= phy_link_speed;

	/*
	 * Try each speed one by one, highest priority first.  We do this in
	 * software because 10gb fiber doesn't support speed autonegotiation.
	 */
	if (speed & IXGBE_LINK_SPEED_10GB_FULL) {
		speedcnt++;
		highest_link_speed = IXGBE_LINK_SPEED_10GB_FULL;

		/* Set hardware SDP's */
		esdp_reg |= (IXGBE_ESDP_SDP5_DIR | IXGBE_ESDP_SDP5);
		IXGBE_WRITE_REG(hw, IXGBE_ESDP, esdp_reg);

		ixgbe_setup_mac_link_speed_82599(hw,
		                                 IXGBE_LINK_SPEED_10GB_FULL,
		                                 autoneg,
		                                 autoneg_wait_to_complete);

		msleep(50);

		/* If we have link, just jump out */
		hw->mac.ops.check_link(hw, &phy_link_speed, &link_up, false);
		if (link_up)
			goto out;
	}

	if (speed & IXGBE_LINK_SPEED_1GB_FULL) {
		speedcnt++;
		if (highest_link_speed == IXGBE_LINK_SPEED_UNKNOWN)
			highest_link_speed = IXGBE_LINK_SPEED_1GB_FULL;

		/* Set hardware SDP's */
		esdp_reg &= ~IXGBE_ESDP_SDP5;
		esdp_reg |= IXGBE_ESDP_SDP5_DIR;
		IXGBE_WRITE_REG(hw, IXGBE_ESDP, esdp_reg);

		ixgbe_setup_mac_link_speed_82599(
			hw, IXGBE_LINK_SPEED_1GB_FULL, autoneg,
			autoneg_wait_to_complete);

		msleep(50);

		/* If we have link, just jump out */
		hw->mac.ops.check_link(hw, &phy_link_speed, &link_up, false);
		if (link_up)
			goto out;
	}

	/*
	 * We didn't get link.  Configure back to the highest speed we tried,
	 * (if there was more than one).  We call ourselves back with just the
	 * single highest speed that the user requested.
	 */
	if (speedcnt > 1)
		status = ixgbe_setup_mac_link_speed_multispeed_fiber(hw,
		                                     highest_link_speed,
		                                     autoneg,
		                                     autoneg_wait_to_complete);

out:
	return status;
}

/**
 *  ixgbe_check_mac_link_82599 - Determine link and speed status
 *  @hw: pointer to hardware structure
 *  @speed: pointer to link speed
 *  @link_up: true when link is up
 *  @link_up_wait_to_complete: bool used to wait for link up or not
 *
 *  Reads the links register to determine if link is up and the current speed
 **/
s32 ixgbe_check_mac_link_82599(struct ixgbe_hw *hw, ixgbe_link_speed *speed,
                               bool *link_up, bool link_up_wait_to_complete)
{
	u32 links_reg;
	u32 i;

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

	if ((links_reg & IXGBE_LINKS_SPEED_82599) ==
	    IXGBE_LINKS_SPEED_10G_82599)
		*speed = IXGBE_LINK_SPEED_10GB_FULL;
	else if ((links_reg & IXGBE_LINKS_SPEED_82599) ==
	         IXGBE_LINKS_SPEED_1G_82599)
		*speed = IXGBE_LINK_SPEED_1GB_FULL;
	else
		*speed = IXGBE_LINK_SPEED_100_FULL;


	return 0;
}

/**
 *  ixgbe_setup_mac_link_speed_82599 - Set MAC link speed
 *  @hw: pointer to hardware structure
 *  @speed: new link speed
 *  @autoneg: true if autonegotiation enabled
 *  @autoneg_wait_to_complete: true when waiting for completion is needed
 *
 *  Set the link speed in the AUTOC register and restarts link.
 **/
s32 ixgbe_setup_mac_link_speed_82599(struct ixgbe_hw *hw,
                                     ixgbe_link_speed speed, bool autoneg,
                                     bool autoneg_wait_to_complete)
{
	s32 status = 0;
	u32 autoc = IXGBE_READ_REG(hw, IXGBE_AUTOC);
	u32 autoc2 = IXGBE_READ_REG(hw, IXGBE_AUTOC2);
	u32 link_mode = autoc & IXGBE_AUTOC_LMS_MASK;
	u32 pma_pmd_1g = autoc & IXGBE_AUTOC_1G_PMA_PMD_MASK;
	u32 pma_pmd_10g_serial = autoc2 & IXGBE_AUTOC2_10G_SERIAL_PMA_PMD_MASK;
	u32 links_reg;
	u32 i;
	ixgbe_link_speed link_capabilities = IXGBE_LINK_SPEED_UNKNOWN;

	/* Check to see if speed passed in is supported. */
	hw->mac.ops.get_link_capabilities(hw, &link_capabilities, &autoneg);
	speed &= link_capabilities;

	if (speed == IXGBE_LINK_SPEED_UNKNOWN) {
		status = IXGBE_ERR_LINK_SETUP;
	} else if (link_mode == IXGBE_AUTOC_LMS_KX4_KX_KR ||
	           link_mode == IXGBE_AUTOC_LMS_KX4_KX_KR_1G_AN ||
	           link_mode == IXGBE_AUTOC_LMS_KX4_KX_KR_SGMII) {
		/* Set KX4/KX/KR support according to speed requested */
		autoc &= ~(IXGBE_AUTOC_KX4_KX_SUPP_MASK | IXGBE_AUTOC_KR_SUPP);
		if (speed & IXGBE_LINK_SPEED_10GB_FULL)
			if (hw->mac.orig_autoc & IXGBE_AUTOC_KX4_SUPP)
				autoc |= IXGBE_AUTOC_KX4_SUPP;
			if (hw->mac.orig_autoc & IXGBE_AUTOC_KR_SUPP)
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

	if (status == 0) {
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

		/* Set up flow control */
		status = ixgbe_setup_fc_generic(hw, 0);

		/* Add delay to filter out noises during initial link setup */
		msleep(50);
	}

	return status;
}

/**
 *  ixgbe_setup_copper_link_82599 - Setup copper link settings
 *  @hw: pointer to hardware structure
 *
 *  Restarts the link on PHY and then MAC. Performs autonegotiation if needed.
 **/
static s32 ixgbe_setup_copper_link_82599(struct ixgbe_hw *hw)
{
	s32 status;

	/* Restart autonegotiation on PHY */
	status = hw->phy.ops.setup_link(hw);

	/* Set up MAC */
	ixgbe_setup_mac_link_82599(hw);

	return status;
}

/**
 *  ixgbe_setup_copper_link_speed_82599 - Set the PHY autoneg advertised field
 *  @hw: pointer to hardware structure
 *  @speed: new link speed
 *  @autoneg: true if autonegotiation enabled
 *  @autoneg_wait_to_complete: true if waiting is needed to complete
 *
 *  Restarts link on PHY and MAC based on settings passed in.
 **/
static s32 ixgbe_setup_copper_link_speed_82599(struct ixgbe_hw *hw,
                                               ixgbe_link_speed speed,
                                               bool autoneg,
                                               bool autoneg_wait_to_complete)
{
	s32 status;

	/* Setup the PHY according to input speed */
	status = hw->phy.ops.setup_link_speed(hw, speed, autoneg,
	                                      autoneg_wait_to_complete);
	/* Set up MAC */
	ixgbe_setup_mac_link_82599(hw);

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
s32 ixgbe_reset_hw_82599(struct ixgbe_hw *hw)
{
	s32 status = 0;
	u32 ctrl, ctrl_ext;
	u32 i;
	u32 autoc;
	u32 autoc2;

	/* Call adapter stop to disable tx/rx and clear interrupts */
	hw->mac.ops.stop_adapter(hw);

	/* Reset PHY */
	hw->phy.ops.reset(hw);

	/*
	 * Prevent the PCI-E bus from from hanging by disabling PCI-E master
	 * access and verify no pending requests before reset
	 */
	if (ixgbe_disable_pcie_master(hw) != 0) {
		status = IXGBE_ERR_MASTER_REQUESTS_PENDING;
		hw_dbg(hw, "PCI-E Master disable polling has failed.\n");
	}

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
	/* Clear PF Reset Done bit so PF/VF Mail Ops can work */
	ctrl_ext = IXGBE_READ_REG(hw, IXGBE_CTRL_EXT);
	ctrl_ext |= IXGBE_CTRL_EXT_PFRSTD;
	IXGBE_WRITE_REG(hw, IXGBE_CTRL_EXT, ctrl_ext);

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

	return status;
}

/**
 *  ixgbe_clear_vmdq_82599 - Disassociate a VMDq pool index from a rx address
 *  @hw: pointer to hardware struct
 *  @rar: receive address register index to disassociate
 *  @vmdq: VMDq pool index to remove from the rar
 **/
s32 ixgbe_clear_vmdq_82599(struct ixgbe_hw *hw, u32 rar, u32 vmdq)
{
	u32 mpsar_lo, mpsar_hi;
	u32 rar_entries = hw->mac.num_rar_entries;

	if (rar < rar_entries) {
		mpsar_lo = IXGBE_READ_REG(hw, IXGBE_MPSAR_LO(rar));
		mpsar_hi = IXGBE_READ_REG(hw, IXGBE_MPSAR_HI(rar));

		if (!mpsar_lo && !mpsar_hi)
			goto done;

		if (vmdq == IXGBE_CLEAR_VMDQ_ALL) {
			if (mpsar_lo) {
				IXGBE_WRITE_REG(hw, IXGBE_MPSAR_LO(rar), 0);
				mpsar_lo = 0;
			}
			if (mpsar_hi) {
				IXGBE_WRITE_REG(hw, IXGBE_MPSAR_HI(rar), 0);
				mpsar_hi = 0;
			}
		} else if (vmdq < 32) {
			mpsar_lo &= ~(1 << vmdq);
			IXGBE_WRITE_REG(hw, IXGBE_MPSAR_LO(rar), mpsar_lo);
		} else {
			mpsar_hi &= ~(1 << (vmdq - 32));
			IXGBE_WRITE_REG(hw, IXGBE_MPSAR_HI(rar), mpsar_hi);
		}

		/* was that the last pool using this rar? */
		if (mpsar_lo == 0 && mpsar_hi == 0 && rar != 0)
			hw->mac.ops.clear_rar(hw, rar);
	} else {
		hw_dbg(hw, "RAR index %d is out of range.\n", rar);
	}

done:
	return 0;
}

/**
 *  ixgbe_set_vmdq_82599 - Associate a VMDq pool index with a rx address
 *  @hw: pointer to hardware struct
 *  @rar: receive address register index to associate with a VMDq index
 *  @vmdq: VMDq pool index
 **/
s32 ixgbe_set_vmdq_82599(struct ixgbe_hw *hw, u32 rar, u32 vmdq)
{
	u32 mpsar;
	u32 rar_entries = hw->mac.num_rar_entries;

	if (rar < rar_entries) {
		if (vmdq < 32) {
			mpsar = IXGBE_READ_REG(hw, IXGBE_MPSAR_LO(rar));
			mpsar |= 1 << vmdq;
			IXGBE_WRITE_REG(hw, IXGBE_MPSAR_LO(rar), mpsar);
		} else {
			mpsar = IXGBE_READ_REG(hw, IXGBE_MPSAR_HI(rar));
			mpsar |= 1 << (vmdq - 32);
			IXGBE_WRITE_REG(hw, IXGBE_MPSAR_HI(rar), mpsar);
		}
	} else {
		hw_dbg(hw, "RAR index %d is out of range.\n", rar);
	}
	return 0;
}

/**
 *  ixgbe_set_vfta_82599 - Set VLAN filter table
 *  @hw: pointer to hardware structure
 *  @vlan: VLAN id to write to VLAN filter
 *  @vind: VMDq output index that maps queue to VLAN id in VFVFB
 *  @vlan_on: boolean flag to turn on/off VLAN in VFVF
 *
 *  Turn on/off specified VLAN in the VLAN filter table.
 **/
s32 ixgbe_set_vfta_82599(struct ixgbe_hw *hw, u32 vlan, u32 vind,
                         bool vlan_on)
{
	u32 regindex;
	u32 bitindex;
	u32 bits;
	u32 first_empty_slot;

	if (vlan > 4095)
		return IXGBE_ERR_PARAM;

	/*
	 * this is a 2 part operation - first the VFTA, then the
	 * VLVF and VLVFB if vind is set
	 */

	/* Part 1
	 * The VFTA is a bitstring made up of 128 32-bit registers
	 * that enable the particular VLAN id, much like the MTA:
	 *    bits[11-5]: which register
	 *    bits[4-0]:  which bit in the register
	 */
	regindex = (vlan >> 5) & 0x7F;
	bitindex = vlan & 0x1F;
	bits = IXGBE_READ_REG(hw, IXGBE_VFTA(regindex));
	if (vlan_on)
		bits |= (1 << bitindex);
	else
		bits &= ~(1 << bitindex);
	IXGBE_WRITE_REG(hw, IXGBE_VFTA(regindex), bits);


	/* Part 2
	 * If the vind is set
	 *   Either vlan_on
	 *     make sure the vlan is in VLVF
	 *     set the vind bit in the matching VLVFB
	 *   Or !vlan_on
	 *     clear the pool bit and possibly the vind
	 */
	if (vind) {
		/* find the vlanid or the first empty slot */
		first_empty_slot = 0;

		for (regindex = 1; regindex < IXGBE_VLVF_ENTRIES; regindex++) {
			bits = IXGBE_READ_REG(hw, IXGBE_VLVF(regindex));
			if (!bits && !first_empty_slot)
				first_empty_slot = regindex;
			else if ((bits & 0x0FFF) == vlan)
				break;
		}

		if (regindex >= IXGBE_VLVF_ENTRIES) {
			if (first_empty_slot)
				regindex = first_empty_slot;
			else {
				hw_dbg(hw, "No space in VLVF.\n");
				goto out;
			}
		}

		if (vlan_on) {
			/* set the pool bit */
			if (vind < 32) {
				bits = IXGBE_READ_REG(hw,
				                    IXGBE_VLVFB(regindex * 2));
				bits |= (1 << vind);
				IXGBE_WRITE_REG(hw,
				              IXGBE_VLVFB(regindex * 2), bits);
			} else {
				bits = IXGBE_READ_REG(hw,
				              IXGBE_VLVFB((regindex * 2) + 1));
				bits |= (1 << vind);
				IXGBE_WRITE_REG(hw,
				        IXGBE_VLVFB((regindex * 2) + 1), bits);
			}
		} else {
			/* clear the pool bit */
			if (vind < 32) {
				bits = IXGBE_READ_REG(hw,
				     IXGBE_VLVFB(regindex * 2));
			bits &= ~(1 << vind);
				IXGBE_WRITE_REG(hw,
				              IXGBE_VLVFB(regindex * 2), bits);
				bits |= IXGBE_READ_REG(hw,
				              IXGBE_VLVFB((regindex * 2) + 1));
			} else {
				bits = IXGBE_READ_REG(hw,
				              IXGBE_VLVFB((regindex * 2) + 1));
				bits &= ~(1 << vind);
				IXGBE_WRITE_REG(hw,
				        IXGBE_VLVFB((regindex * 2) + 1), bits);
				bits |= IXGBE_READ_REG(hw,
				                    IXGBE_VLVFB(regindex * 2));
			}
		}

		if (bits)
			IXGBE_WRITE_REG(hw, IXGBE_VLVF(regindex),
			                (IXGBE_VLVF_VIEN | vlan));
		else
			IXGBE_WRITE_REG(hw, IXGBE_VLVF(regindex), 0);
	}

out:
	return 0;
}

/**
 *  ixgbe_clear_vfta_82599 - Clear VLAN filter table
 *  @hw: pointer to hardware structure
 *
 *  Clears the VLAN filer table, and the VMDq index associated with the filter
 **/
s32 ixgbe_clear_vfta_82599(struct ixgbe_hw *hw)
{
	u32 offset;

	for (offset = 0; offset < hw->mac.vft_size; offset++)
		IXGBE_WRITE_REG(hw, IXGBE_VFTA(offset), 0);

	for (offset = 0; offset < IXGBE_VLVF_ENTRIES; offset++) {
		IXGBE_WRITE_REG(hw, IXGBE_VLVF(offset), 0);
		IXGBE_WRITE_REG(hw, IXGBE_VLVFB(offset * 2), 0);
		IXGBE_WRITE_REG(hw, IXGBE_VLVFB((offset * 2) + 1), 0);
	}

	return 0;
}

/**
 *  ixgbe_blink_led_start_82599 - Blink LED based on index.
 *  @hw: pointer to hardware structure
 *  @index: led number to blink
 **/
s32 ixgbe_blink_led_start_82599(struct ixgbe_hw *hw, u32 index)
{
	u32 led_reg = IXGBE_READ_REG(hw, IXGBE_LEDCTL);

	led_reg &= ~IXGBE_LED_MODE_MASK(index);
	led_reg |= IXGBE_LED_BLINK(index);
	IXGBE_WRITE_REG(hw, IXGBE_LEDCTL, led_reg);
	IXGBE_WRITE_FLUSH(hw);

	return 0;
}

/**
 *  ixgbe_blink_led_stop_82599 - Stop blinking LED based on index.
 *  @hw: pointer to hardware structure
 *  @index: led number to stop blinking
 **/
s32 ixgbe_blink_led_stop_82599(struct ixgbe_hw *hw, u32 index)
{
	u32 led_reg = IXGBE_READ_REG(hw, IXGBE_LEDCTL);

	led_reg &= ~IXGBE_LED_MODE_MASK(index);
	led_reg &= ~IXGBE_LED_BLINK(index);
	IXGBE_WRITE_REG(hw, IXGBE_LEDCTL, led_reg);
	IXGBE_WRITE_FLUSH(hw);

	return 0;
}

/**
 *  ixgbe_init_uta_tables_82599 - Initialize the Unicast Table Array
 *  @hw: pointer to hardware structure
 **/
s32 ixgbe_init_uta_tables_82599(struct ixgbe_hw *hw)
{
	int i;
	hw_dbg(hw, " Clearing UTA\n");

	for (i = 0; i < 128; i++)
		IXGBE_WRITE_REG(hw, IXGBE_UTA(i), 0);

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
s32 ixgbe_read_analog_reg8_82599(struct ixgbe_hw *hw, u32 reg, u8 *val)
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
s32 ixgbe_write_analog_reg8_82599(struct ixgbe_hw *hw, u32 reg, u8 val)
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
 *  Starts the hardware using the generic start_hw function.
 *  Then performs device-specific:
 *  Clears the rate limiter registers.
 **/
s32 ixgbe_start_hw_82599(struct ixgbe_hw *hw)
{
	u32 q_num;

	ixgbe_start_hw_generic(hw);

	/* Clear the rate limiters */
	for (q_num = 0; q_num < hw->mac.max_tx_queues; q_num++) {
		IXGBE_WRITE_REG(hw, IXGBE_RTTDQSEL, q_num);
		IXGBE_WRITE_REG(hw, IXGBE_RTTBCNRC, 0);
	}
	IXGBE_WRITE_FLUSH(hw);

	return 0;
}

/**
 *  ixgbe_identify_phy_82599 - Get physical layer module
 *  @hw: pointer to hardware structure
 *
 *  Determines the physical layer module found on the current adapter.
 **/
s32 ixgbe_identify_phy_82599(struct ixgbe_hw *hw)
{
	s32 status = IXGBE_ERR_PHY_ADDR_INVALID;
	status = ixgbe_identify_phy_generic(hw);
	if (status != 0)
		status = ixgbe_identify_sfp_module_generic(hw);
	return status;
}

/**
 *  ixgbe_get_supported_physical_layer_82599 - Returns physical layer type
 *  @hw: pointer to hardware structure
 *
 *  Determines physical layer capabilities of the current configuration.
 **/
u32 ixgbe_get_supported_physical_layer_82599(struct ixgbe_hw *hw)
{
	u32 physical_layer = IXGBE_PHYSICAL_LAYER_UNKNOWN;
	u8 comp_codes_10g = 0;

	switch (hw->device_id) {
	case IXGBE_DEV_ID_82599:
	case IXGBE_DEV_ID_82599_KX4:
		/* Default device ID is mezzanine card KX/KX4 */
		physical_layer = (IXGBE_PHYSICAL_LAYER_10GBASE_KX4 |
		                  IXGBE_PHYSICAL_LAYER_1000BASE_KX);
		break;
	case IXGBE_DEV_ID_82599_SFP:
		hw->phy.ops.identify_sfp(hw);

		switch (hw->phy.sfp_type) {
		case ixgbe_sfp_type_da_cu:
		case ixgbe_sfp_type_da_cu_core0:
		case ixgbe_sfp_type_da_cu_core1:
			physical_layer = IXGBE_PHYSICAL_LAYER_SFP_PLUS_CU;
			break;
		case ixgbe_sfp_type_sr:
			physical_layer = IXGBE_PHYSICAL_LAYER_10GBASE_SR;
			break;
		case ixgbe_sfp_type_lr:
			physical_layer = IXGBE_PHYSICAL_LAYER_10GBASE_LR;
			break;
		case ixgbe_sfp_type_srlr_core0:
		case ixgbe_sfp_type_srlr_core1:
			hw->phy.ops.read_i2c_eeprom(hw,
			                            IXGBE_SFF_10GBE_COMP_CODES,
			                            &comp_codes_10g);
			if (comp_codes_10g & IXGBE_SFF_10GBASESR_CAPABLE)
				physical_layer =
				                IXGBE_PHYSICAL_LAYER_10GBASE_SR;
			else if (comp_codes_10g & IXGBE_SFF_10GBASELR_CAPABLE)
				physical_layer =
				                IXGBE_PHYSICAL_LAYER_10GBASE_LR;
			else
				physical_layer = IXGBE_PHYSICAL_LAYER_UNKNOWN;
		default:
			physical_layer = IXGBE_PHYSICAL_LAYER_UNKNOWN;
			break;
		}
		break;
	default:
		physical_layer = IXGBE_PHYSICAL_LAYER_UNKNOWN;
		break;
	}

	return physical_layer;
}

/**
 *  ixgbe_enable_rx_dma_82599 - Enable the Rx DMA unit on 82599
 *  @hw: pointer to hardware structure
 *  @regval: register value to write to RXCTRL
 *
 *  Enables the Rx DMA unit for 82599
 **/
s32 ixgbe_enable_rx_dma_82599(struct ixgbe_hw *hw, u32 regval)
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

static struct ixgbe_mac_operations mac_ops_82599 = {
	.init_hw                = &ixgbe_init_hw_generic,
	.reset_hw               = &ixgbe_reset_hw_82599,
	.start_hw               = &ixgbe_start_hw_82599,
	.clear_hw_cntrs         = &ixgbe_clear_hw_cntrs_generic,
	.get_media_type         = &ixgbe_get_media_type_82599,
	.get_supported_physical_layer = &ixgbe_get_supported_physical_layer_82599,
	.enable_rx_dma          = &ixgbe_enable_rx_dma_82599,
	.get_mac_addr           = &ixgbe_get_mac_addr_generic,
	.stop_adapter           = &ixgbe_stop_adapter_generic,
	.get_bus_info           = &ixgbe_get_bus_info_generic,
	.set_lan_id             = &ixgbe_set_lan_id_multi_port_pcie,
	.read_analog_reg8       = &ixgbe_read_analog_reg8_82599,
	.write_analog_reg8      = &ixgbe_write_analog_reg8_82599,
	.setup_link             = &ixgbe_setup_mac_link_82599,
	.setup_link_speed       = &ixgbe_setup_mac_link_speed_82599,
	.check_link             = &ixgbe_check_mac_link_82599,
	.get_link_capabilities  = &ixgbe_get_link_capabilities_82599,
	.led_on                 = &ixgbe_led_on_generic,
	.led_off                = &ixgbe_led_off_generic,
	.blink_led_start        = &ixgbe_blink_led_start_82599,
	.blink_led_stop         = &ixgbe_blink_led_stop_82599,
	.set_rar                = &ixgbe_set_rar_generic,
	.clear_rar              = &ixgbe_clear_rar_generic,
	.set_vmdq               = &ixgbe_set_vmdq_82599,
	.clear_vmdq             = &ixgbe_clear_vmdq_82599,
	.init_rx_addrs          = &ixgbe_init_rx_addrs_generic,
	.update_uc_addr_list    = &ixgbe_update_uc_addr_list_generic,
	.update_mc_addr_list    = &ixgbe_update_mc_addr_list_generic,
	.enable_mc              = &ixgbe_enable_mc_generic,
	.disable_mc             = &ixgbe_disable_mc_generic,
	.clear_vfta             = &ixgbe_clear_vfta_82599,
	.set_vfta               = &ixgbe_set_vfta_82599,
	.setup_fc               = &ixgbe_setup_fc_generic,
	.init_uta_tables        = &ixgbe_init_uta_tables_82599,
	.setup_sfp              = &ixgbe_setup_sfp_modules_82599,
};

static struct ixgbe_eeprom_operations eeprom_ops_82599 = {
	.init_params            = &ixgbe_init_eeprom_params_generic,
	.read                   = &ixgbe_read_eeprom_generic,
	.write                  = &ixgbe_write_eeprom_generic,
	.validate_checksum      = &ixgbe_validate_eeprom_checksum_generic,
	.update_checksum        = &ixgbe_update_eeprom_checksum_generic,
};

static struct ixgbe_phy_operations phy_ops_82599 = {
	.identify               = &ixgbe_identify_phy_82599,
	.identify_sfp           = &ixgbe_identify_sfp_module_generic,
	.reset                  = &ixgbe_reset_phy_generic,
	.read_reg               = &ixgbe_read_phy_reg_generic,
	.write_reg              = &ixgbe_write_phy_reg_generic,
	.setup_link             = &ixgbe_setup_phy_link_generic,
	.setup_link_speed       = &ixgbe_setup_phy_link_speed_generic,
	.read_i2c_byte          = &ixgbe_read_i2c_byte_generic,
	.write_i2c_byte         = &ixgbe_write_i2c_byte_generic,
	.read_i2c_eeprom        = &ixgbe_read_i2c_eeprom_generic,
	.write_i2c_eeprom       = &ixgbe_write_i2c_eeprom_generic,
};

struct ixgbe_info ixgbe_82599_info = {
	.mac                    = ixgbe_mac_82599EB,
	.get_invariants         = &ixgbe_get_invariants_82599,
	.mac_ops                = &mac_ops_82599,
	.eeprom_ops             = &eeprom_ops_82599,
	.phy_ops                = &phy_ops_82599,
};
