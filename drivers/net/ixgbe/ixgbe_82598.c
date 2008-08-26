/*******************************************************************************

  Intel 10 Gigabit PCI Express Linux driver
  Copyright(c) 1999 - 2007 Intel Corporation.

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

static s32 ixgbe_get_invariants_82598(struct ixgbe_hw *hw);
static s32 ixgbe_get_link_settings_82598(struct ixgbe_hw *hw, u32 *speed,
					 bool *autoneg);
static s32 ixgbe_get_copper_link_settings_82598(struct ixgbe_hw *hw,
						u32 *speed, bool *autoneg);
static enum ixgbe_media_type ixgbe_get_media_type_82598(struct ixgbe_hw *hw);
static s32 ixgbe_setup_mac_link_82598(struct ixgbe_hw *hw);
static s32 ixgbe_check_mac_link_82598(struct ixgbe_hw *hw, u32 *speed,
				      bool *link_up);
static s32 ixgbe_setup_mac_link_speed_82598(struct ixgbe_hw *hw, u32 speed,
					    bool autoneg,
					    bool autoneg_wait_to_complete);
static s32 ixgbe_setup_copper_link_82598(struct ixgbe_hw *hw);
static s32 ixgbe_setup_copper_link_speed_82598(struct ixgbe_hw *hw, u32 speed,
					       bool autoneg,
					       bool autoneg_wait_to_complete);
static s32 ixgbe_reset_hw_82598(struct ixgbe_hw *hw);


static s32 ixgbe_get_invariants_82598(struct ixgbe_hw *hw)
{
	hw->mac.num_rx_queues = IXGBE_82598_MAX_RX_QUEUES;
	hw->mac.num_tx_queues = IXGBE_82598_MAX_TX_QUEUES;
	hw->mac.num_rx_addrs = IXGBE_82598_RAR_ENTRIES;

	/* PHY ops are filled in by default properly for Fiber only */
	if (hw->mac.ops.get_media_type(hw) == ixgbe_media_type_copper) {
		hw->mac.ops.setup_link = &ixgbe_setup_copper_link_82598;
		hw->mac.ops.setup_link_speed = &ixgbe_setup_copper_link_speed_82598;
		hw->mac.ops.get_link_settings =
				&ixgbe_get_copper_link_settings_82598;

		/* Call PHY identify routine to get the phy type */
		ixgbe_identify_phy(hw);

		switch (hw->phy.type) {
		case ixgbe_phy_tn:
			hw->phy.ops.setup_link = &ixgbe_setup_tnx_phy_link;
			hw->phy.ops.check_link = &ixgbe_check_tnx_phy_link;
			hw->phy.ops.setup_link_speed =
					&ixgbe_setup_tnx_phy_link_speed;
			break;
		default:
			break;
		}
	}

	return 0;
}

/**
 *  ixgbe_get_link_settings_82598 - Determines default link settings
 *  @hw: pointer to hardware structure
 *  @speed: pointer to link speed
 *  @autoneg: boolean auto-negotiation value
 *
 *  Determines the default link settings by reading the AUTOC register.
 **/
static s32 ixgbe_get_link_settings_82598(struct ixgbe_hw *hw, u32 *speed,
					 bool *autoneg)
{
	s32 status = 0;
	s32 autoc_reg;

	autoc_reg = IXGBE_READ_REG(hw, IXGBE_AUTOC);

	if (hw->mac.link_settings_loaded) {
		autoc_reg &= ~IXGBE_AUTOC_LMS_ATTACH_TYPE;
		autoc_reg &= ~IXGBE_AUTOC_LMS_MASK;
		autoc_reg |= hw->mac.link_attach_type;
		autoc_reg |= hw->mac.link_mode_select;
	}

	switch (autoc_reg & IXGBE_AUTOC_LMS_MASK) {
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
		if (autoc_reg & IXGBE_AUTOC_KX4_SUPP)
			*speed |= IXGBE_LINK_SPEED_10GB_FULL;
		if (autoc_reg & IXGBE_AUTOC_KX_SUPP)
			*speed |= IXGBE_LINK_SPEED_1GB_FULL;
		*autoneg = true;
		break;

	default:
		status = IXGBE_ERR_LINK_SETUP;
		break;
	}

	return status;
}

/**
 *  ixgbe_get_copper_link_settings_82598 - Determines default link settings
 *  @hw: pointer to hardware structure
 *  @speed: pointer to link speed
 *  @autoneg: boolean auto-negotiation value
 *
 *  Determines the default link settings by reading the AUTOC register.
 **/
static s32 ixgbe_get_copper_link_settings_82598(struct ixgbe_hw *hw,
						u32 *speed, bool *autoneg)
{
	s32 status = IXGBE_ERR_LINK_SETUP;
	u16 speed_ability;

	*speed = 0;
	*autoneg = true;

	status = ixgbe_read_phy_reg(hw, IXGBE_MDIO_PHY_SPEED_ABILITY,
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
 *  ixgbe_get_media_type_82598 - Determines media type
 *  @hw: pointer to hardware structure
 *
 *  Returns the media type (fiber, copper, backplane)
 **/
static enum ixgbe_media_type ixgbe_get_media_type_82598(struct ixgbe_hw *hw)
{
	enum ixgbe_media_type media_type;

	/* Media type for I82598 is based on device ID */
	switch (hw->device_id) {
	case IXGBE_DEV_ID_82598AF_DUAL_PORT:
	case IXGBE_DEV_ID_82598AF_SINGLE_PORT:
	case IXGBE_DEV_ID_82598EB_CX4:
	case IXGBE_DEV_ID_82598_CX4_DUAL_PORT:
		media_type = ixgbe_media_type_fiber;
		break;
	case IXGBE_DEV_ID_82598AT_DUAL_PORT:
		media_type = ixgbe_media_type_copper;
		break;
	default:
		media_type = ixgbe_media_type_unknown;
		break;
	}

	return media_type;
}

/**
 *  ixgbe_setup_mac_link_82598 - Configures MAC link settings
 *  @hw: pointer to hardware structure
 *
 *  Configures link settings based on values in the ixgbe_hw struct.
 *  Restarts the link.  Performs autonegotiation if needed.
 **/
static s32 ixgbe_setup_mac_link_82598(struct ixgbe_hw *hw)
{
	u32 autoc_reg;
	u32 links_reg;
	u32 i;
	s32 status = 0;

	autoc_reg = IXGBE_READ_REG(hw, IXGBE_AUTOC);

	if (hw->mac.link_settings_loaded) {
		autoc_reg &= ~IXGBE_AUTOC_LMS_ATTACH_TYPE;
		autoc_reg &= ~IXGBE_AUTOC_LMS_MASK;
		autoc_reg |= hw->mac.link_attach_type;
		autoc_reg |= hw->mac.link_mode_select;

		IXGBE_WRITE_REG(hw, IXGBE_AUTOC, autoc_reg);
		IXGBE_WRITE_FLUSH(hw);
		msleep(50);
	}

	/* Restart link */
	autoc_reg |= IXGBE_AUTOC_AN_RESTART;
	IXGBE_WRITE_REG(hw, IXGBE_AUTOC, autoc_reg);

	/* Only poll for autoneg to complete if specified to do so */
	if (hw->phy.autoneg_wait_to_complete) {
		if (hw->mac.link_mode_select == IXGBE_AUTOC_LMS_KX4_AN ||
		    hw->mac.link_mode_select == IXGBE_AUTOC_LMS_KX4_AN_1G_AN) {
			links_reg = 0; /* Just in case Autoneg time = 0 */
			for (i = 0; i < IXGBE_AUTO_NEG_TIME; i++) {
				links_reg = IXGBE_READ_REG(hw, IXGBE_LINKS);
				if (links_reg & IXGBE_LINKS_KX_AN_COMP)
					break;
				msleep(100);
			}
			if (!(links_reg & IXGBE_LINKS_KX_AN_COMP)) {
				status = IXGBE_ERR_AUTONEG_NOT_COMPLETE;
				hw_dbg(hw,
				       "Autonegotiation did not complete.\n");
			}
		}
	}

	/*
	 * We want to save off the original Flow Control configuration just in
	 * case we get disconnected and then reconnected into a different hub
	 * or switch with different Flow Control capabilities.
	 */
	hw->fc.type = hw->fc.original_type;
	ixgbe_setup_fc(hw, 0);

	/* Add delay to filter out noises during initial link setup */
	msleep(50);

	return status;
}

/**
 *  ixgbe_check_mac_link_82598 - Get link/speed status
 *  @hw: pointer to hardware structure
 *  @speed: pointer to link speed
 *  @link_up: true is link is up, false otherwise
 *
 *  Reads the links register to determine if link is up and the current speed
 **/
static s32 ixgbe_check_mac_link_82598(struct ixgbe_hw *hw, u32 *speed,
				      bool *link_up)
{
	u32 links_reg;

	links_reg = IXGBE_READ_REG(hw, IXGBE_LINKS);

	if (links_reg & IXGBE_LINKS_UP)
		*link_up = true;
	else
		*link_up = false;

	if (links_reg & IXGBE_LINKS_SPEED)
		*speed = IXGBE_LINK_SPEED_10GB_FULL;
	else
		*speed = IXGBE_LINK_SPEED_1GB_FULL;

	return 0;
}

/**
 *  ixgbe_setup_mac_link_speed_82598 - Set MAC link speed
 *  @hw: pointer to hardware structure
 *  @speed: new link speed
 *  @autoneg: true if auto-negotiation enabled
 *  @autoneg_wait_to_complete: true if waiting is needed to complete
 *
 *  Set the link speed in the AUTOC register and restarts link.
 **/
static s32 ixgbe_setup_mac_link_speed_82598(struct ixgbe_hw *hw,
					    u32 speed, bool autoneg,
					    bool autoneg_wait_to_complete)
{
	s32 status = 0;

	/* If speed is 10G, then check for CX4 or XAUI. */
	if ((speed == IXGBE_LINK_SPEED_10GB_FULL) &&
	    (!(hw->mac.link_attach_type & IXGBE_AUTOC_10G_KX4)))
		hw->mac.link_mode_select = IXGBE_AUTOC_LMS_10G_LINK_NO_AN;
	else if ((speed == IXGBE_LINK_SPEED_1GB_FULL) && (!autoneg))
		hw->mac.link_mode_select = IXGBE_AUTOC_LMS_1G_LINK_NO_AN;
	else if (autoneg) {
		/* BX mode - Autonegotiate 1G */
		if (!(hw->mac.link_attach_type & IXGBE_AUTOC_1G_PMA_PMD))
			hw->mac.link_mode_select = IXGBE_AUTOC_LMS_1G_AN;
		else /* KX/KX4 mode */
			hw->mac.link_mode_select = IXGBE_AUTOC_LMS_KX4_AN_1G_AN;
	} else {
		status = IXGBE_ERR_LINK_SETUP;
	}

	if (status == 0) {
		hw->phy.autoneg_wait_to_complete = autoneg_wait_to_complete;

		hw->mac.link_settings_loaded = true;
		/*
		 * Setup and restart the link based on the new values in
		 * ixgbe_hw This will write the AUTOC register based on the new
		 * stored values
		 */
		hw->mac.ops.setup_link(hw);
	}

	return status;
}


/**
 *  ixgbe_setup_copper_link_82598 - Setup copper link settings
 *  @hw: pointer to hardware structure
 *
 *  Configures link settings based on values in the ixgbe_hw struct.
 *  Restarts the link.  Performs autonegotiation if needed.  Restart
 *  phy and wait for autonegotiate to finish.  Then synchronize the
 *  MAC and PHY.
 **/
static s32 ixgbe_setup_copper_link_82598(struct ixgbe_hw *hw)
{
	s32 status = 0;

	/* Restart autonegotiation on PHY */
	if (hw->phy.ops.setup_link)
		status = hw->phy.ops.setup_link(hw);

	/* Set MAC to KX/KX4 autoneg, which defaultis to Parallel detection */
	hw->mac.link_attach_type = (IXGBE_AUTOC_10G_KX4 | IXGBE_AUTOC_1G_KX);
	hw->mac.link_mode_select = IXGBE_AUTOC_LMS_KX4_AN;

	/* Set up MAC */
	hw->mac.ops.setup_link(hw);

	return status;
}

/**
 *  ixgbe_setup_copper_link_speed_82598 - Set the PHY autoneg advertised field
 *  @hw: pointer to hardware structure
 *  @speed: new link speed
 *  @autoneg: true if autonegotiation enabled
 *  @autoneg_wait_to_complete: true if waiting is needed to complete
 *
 *  Sets the link speed in the AUTOC register in the MAC and restarts link.
 **/
static s32 ixgbe_setup_copper_link_speed_82598(struct ixgbe_hw *hw, u32 speed,
					       bool autoneg,
					       bool autoneg_wait_to_complete)
{
	s32 status = 0;

	/* Setup the PHY according to input speed */
	if (hw->phy.ops.setup_link_speed)
		status = hw->phy.ops.setup_link_speed(hw, speed, autoneg,
						autoneg_wait_to_complete);

	/* Set MAC to KX/KX4 autoneg, which defaults to Parallel detection */
	hw->mac.link_attach_type = (IXGBE_AUTOC_10G_KX4 | IXGBE_AUTOC_1G_KX);
	hw->mac.link_mode_select = IXGBE_AUTOC_LMS_KX4_AN;

	/* Set up MAC */
	hw->mac.ops.setup_link(hw);

	return status;
}

/**
 *  ixgbe_reset_hw_82598 - Performs hardware reset
 *  @hw: pointer to hardware structure
 *
 *  Resets the hardware by reseting the transmit and receive units, masks and
 *  clears all interrupts, performing a PHY reset, and performing a link (MAC)
 *  reset.
 **/
static s32 ixgbe_reset_hw_82598(struct ixgbe_hw *hw)
{
	s32 status = 0;
	u32 ctrl;
	u32 gheccr;
	u32 i;
	u32 autoc;
	u8  analog_val;

	/* Call adapter stop to disable tx/rx and clear interrupts */
	ixgbe_stop_adapter(hw);

	/*
	 * Power up the Atlas TX lanes if they are currently powered down.
	 * Atlas TX lanes are powered down for MAC loopback tests, but
	 * they are not automatically restored on reset.
	 */
	ixgbe_read_analog_reg8(hw, IXGBE_ATLAS_PDN_LPBK, &analog_val);
	if (analog_val & IXGBE_ATLAS_PDN_TX_REG_EN) {
		/* Enable TX Atlas so packets can be transmitted again */
		ixgbe_read_analog_reg8(hw, IXGBE_ATLAS_PDN_LPBK, &analog_val);
		analog_val &= ~IXGBE_ATLAS_PDN_TX_REG_EN;
		ixgbe_write_analog_reg8(hw, IXGBE_ATLAS_PDN_LPBK, analog_val);

		ixgbe_read_analog_reg8(hw, IXGBE_ATLAS_PDN_10G, &analog_val);
		analog_val &= ~IXGBE_ATLAS_PDN_TX_10G_QL_ALL;
		ixgbe_write_analog_reg8(hw, IXGBE_ATLAS_PDN_10G, analog_val);

		ixgbe_read_analog_reg8(hw, IXGBE_ATLAS_PDN_1G, &analog_val);
		analog_val &= ~IXGBE_ATLAS_PDN_TX_1G_QL_ALL;
		ixgbe_write_analog_reg8(hw, IXGBE_ATLAS_PDN_1G, analog_val);

		ixgbe_read_analog_reg8(hw, IXGBE_ATLAS_PDN_AN, &analog_val);
		analog_val &= ~IXGBE_ATLAS_PDN_TX_AN_QL_ALL;
		ixgbe_write_analog_reg8(hw, IXGBE_ATLAS_PDN_AN, analog_val);
	}

	/* Reset PHY */
	ixgbe_reset_phy(hw);

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

	msleep(50);

	gheccr = IXGBE_READ_REG(hw, IXGBE_GHECCR);
	gheccr &= ~((1 << 21) | (1 << 18) | (1 << 9) | (1 << 6));
	IXGBE_WRITE_REG(hw, IXGBE_GHECCR, gheccr);

	/*
	 * AUTOC register which stores link settings gets cleared
	 * and reloaded from EEPROM after reset. We need to restore
	 * our stored value from init in case SW changed the attach
	 * type or speed.  If this is the first time and link settings
	 * have not been stored, store default settings from AUTOC.
	 */
	autoc = IXGBE_READ_REG(hw, IXGBE_AUTOC);
	if (hw->mac.link_settings_loaded) {
		autoc &= ~(IXGBE_AUTOC_LMS_ATTACH_TYPE);
		autoc &= ~(IXGBE_AUTOC_LMS_MASK);
		autoc |= hw->mac.link_attach_type;
		autoc |= hw->mac.link_mode_select;
		IXGBE_WRITE_REG(hw, IXGBE_AUTOC, autoc);
	} else {
		hw->mac.link_attach_type =
					 (autoc & IXGBE_AUTOC_LMS_ATTACH_TYPE);
		hw->mac.link_mode_select = (autoc & IXGBE_AUTOC_LMS_MASK);
		hw->mac.link_settings_loaded = true;
	}

	/* Store the permanent mac address */
	ixgbe_get_mac_addr(hw, hw->mac.perm_addr);

	return status;
}

static struct ixgbe_mac_operations mac_ops_82598 = {
	.reset			= &ixgbe_reset_hw_82598,
	.get_media_type		= &ixgbe_get_media_type_82598,
	.setup_link		= &ixgbe_setup_mac_link_82598,
	.check_link		= &ixgbe_check_mac_link_82598,
	.setup_link_speed	= &ixgbe_setup_mac_link_speed_82598,
	.get_link_settings	= &ixgbe_get_link_settings_82598,
};

struct ixgbe_info ixgbe_82598_info = {
	.mac			= ixgbe_mac_82598EB,
	.get_invariants		= &ixgbe_get_invariants_82598,
	.mac_ops		= &mac_ops_82598,
};

