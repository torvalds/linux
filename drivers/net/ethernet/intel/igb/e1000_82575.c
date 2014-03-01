/*******************************************************************************

  Intel(R) Gigabit Ethernet Linux driver
  Copyright(c) 2007-2013 Intel Corporation.

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

/* e1000_82575
 * e1000_82576
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/types.h>
#include <linux/if_ether.h>
#include <linux/i2c.h>

#include "e1000_mac.h"
#include "e1000_82575.h"
#include "e1000_i210.h"

static s32  igb_get_invariants_82575(struct e1000_hw *);
static s32  igb_acquire_phy_82575(struct e1000_hw *);
static void igb_release_phy_82575(struct e1000_hw *);
static s32  igb_acquire_nvm_82575(struct e1000_hw *);
static void igb_release_nvm_82575(struct e1000_hw *);
static s32  igb_check_for_link_82575(struct e1000_hw *);
static s32  igb_get_cfg_done_82575(struct e1000_hw *);
static s32  igb_init_hw_82575(struct e1000_hw *);
static s32  igb_phy_hw_reset_sgmii_82575(struct e1000_hw *);
static s32  igb_read_phy_reg_sgmii_82575(struct e1000_hw *, u32, u16 *);
static s32  igb_read_phy_reg_82580(struct e1000_hw *, u32, u16 *);
static s32  igb_write_phy_reg_82580(struct e1000_hw *, u32, u16);
static s32  igb_reset_hw_82575(struct e1000_hw *);
static s32  igb_reset_hw_82580(struct e1000_hw *);
static s32  igb_set_d0_lplu_state_82575(struct e1000_hw *, bool);
static s32  igb_set_d0_lplu_state_82580(struct e1000_hw *, bool);
static s32  igb_set_d3_lplu_state_82580(struct e1000_hw *, bool);
static s32  igb_setup_copper_link_82575(struct e1000_hw *);
static s32  igb_setup_serdes_link_82575(struct e1000_hw *);
static s32  igb_write_phy_reg_sgmii_82575(struct e1000_hw *, u32, u16);
static void igb_clear_hw_cntrs_82575(struct e1000_hw *);
static s32  igb_acquire_swfw_sync_82575(struct e1000_hw *, u16);
static s32  igb_get_pcs_speed_and_duplex_82575(struct e1000_hw *, u16 *,
						 u16 *);
static s32  igb_get_phy_id_82575(struct e1000_hw *);
static void igb_release_swfw_sync_82575(struct e1000_hw *, u16);
static bool igb_sgmii_active_82575(struct e1000_hw *);
static s32  igb_reset_init_script_82575(struct e1000_hw *);
static s32  igb_read_mac_addr_82575(struct e1000_hw *);
static s32  igb_set_pcie_completion_timeout(struct e1000_hw *hw);
static s32  igb_reset_mdicnfg_82580(struct e1000_hw *hw);
static s32  igb_validate_nvm_checksum_82580(struct e1000_hw *hw);
static s32  igb_update_nvm_checksum_82580(struct e1000_hw *hw);
static s32 igb_validate_nvm_checksum_i350(struct e1000_hw *hw);
static s32 igb_update_nvm_checksum_i350(struct e1000_hw *hw);
static const u16 e1000_82580_rxpbs_table[] =
	{ 36, 72, 144, 1, 2, 4, 8, 16,
	  35, 70, 140 };
#define E1000_82580_RXPBS_TABLE_SIZE \
	(sizeof(e1000_82580_rxpbs_table)/sizeof(u16))

/**
 *  igb_sgmii_uses_mdio_82575 - Determine if I2C pins are for external MDIO
 *  @hw: pointer to the HW structure
 *
 *  Called to determine if the I2C pins are being used for I2C or as an
 *  external MDIO interface since the two options are mutually exclusive.
 **/
static bool igb_sgmii_uses_mdio_82575(struct e1000_hw *hw)
{
	u32 reg = 0;
	bool ext_mdio = false;

	switch (hw->mac.type) {
	case e1000_82575:
	case e1000_82576:
		reg = rd32(E1000_MDIC);
		ext_mdio = !!(reg & E1000_MDIC_DEST);
		break;
	case e1000_82580:
	case e1000_i350:
	case e1000_i354:
	case e1000_i210:
	case e1000_i211:
		reg = rd32(E1000_MDICNFG);
		ext_mdio = !!(reg & E1000_MDICNFG_EXT_MDIO);
		break;
	default:
		break;
	}
	return ext_mdio;
}

/**
 *  igb_check_for_link_media_swap - Check which M88E1112 interface linked
 *  @hw: pointer to the HW structure
 *
 *  Poll the M88E1112 interfaces to see which interface achieved link.
 */
static s32 igb_check_for_link_media_swap(struct e1000_hw *hw)
{
	struct e1000_phy_info *phy = &hw->phy;
	s32 ret_val;
	u16 data;
	u8 port = 0;

	/* Check the copper medium. */
	ret_val = phy->ops.write_reg(hw, E1000_M88E1112_PAGE_ADDR, 0);
	if (ret_val)
		return ret_val;

	ret_val = phy->ops.read_reg(hw, E1000_M88E1112_STATUS, &data);
	if (ret_val)
		return ret_val;

	if (data & E1000_M88E1112_STATUS_LINK)
		port = E1000_MEDIA_PORT_COPPER;

	/* Check the other medium. */
	ret_val = phy->ops.write_reg(hw, E1000_M88E1112_PAGE_ADDR, 1);
	if (ret_val)
		return ret_val;

	ret_val = phy->ops.read_reg(hw, E1000_M88E1112_STATUS, &data);
	if (ret_val)
		return ret_val;

	/* reset page to 0 */
	ret_val = phy->ops.write_reg(hw, E1000_M88E1112_PAGE_ADDR, 0);
	if (ret_val)
		return ret_val;

	if (data & E1000_M88E1112_STATUS_LINK)
		port = E1000_MEDIA_PORT_OTHER;

	/* Determine if a swap needs to happen. */
	if (port && (hw->dev_spec._82575.media_port != port)) {
		hw->dev_spec._82575.media_port = port;
		hw->dev_spec._82575.media_changed = true;
	} else {
		ret_val = igb_check_for_link_82575(hw);
	}

	return E1000_SUCCESS;
}

/**
 *  igb_init_phy_params_82575 - Init PHY func ptrs.
 *  @hw: pointer to the HW structure
 **/
static s32 igb_init_phy_params_82575(struct e1000_hw *hw)
{
	struct e1000_phy_info *phy = &hw->phy;
	s32 ret_val = 0;
	u32 ctrl_ext;

	if (hw->phy.media_type != e1000_media_type_copper) {
		phy->type = e1000_phy_none;
		goto out;
	}

	phy->autoneg_mask	= AUTONEG_ADVERTISE_SPEED_DEFAULT;
	phy->reset_delay_us	= 100;

	ctrl_ext = rd32(E1000_CTRL_EXT);

	if (igb_sgmii_active_82575(hw)) {
		phy->ops.reset = igb_phy_hw_reset_sgmii_82575;
		ctrl_ext |= E1000_CTRL_I2C_ENA;
	} else {
		phy->ops.reset = igb_phy_hw_reset;
		ctrl_ext &= ~E1000_CTRL_I2C_ENA;
	}

	wr32(E1000_CTRL_EXT, ctrl_ext);
	igb_reset_mdicnfg_82580(hw);

	if (igb_sgmii_active_82575(hw) && !igb_sgmii_uses_mdio_82575(hw)) {
		phy->ops.read_reg = igb_read_phy_reg_sgmii_82575;
		phy->ops.write_reg = igb_write_phy_reg_sgmii_82575;
	} else {
		switch (hw->mac.type) {
		case e1000_82580:
		case e1000_i350:
		case e1000_i354:
			phy->ops.read_reg = igb_read_phy_reg_82580;
			phy->ops.write_reg = igb_write_phy_reg_82580;
			break;
		case e1000_i210:
		case e1000_i211:
			phy->ops.read_reg = igb_read_phy_reg_gs40g;
			phy->ops.write_reg = igb_write_phy_reg_gs40g;
			break;
		default:
			phy->ops.read_reg = igb_read_phy_reg_igp;
			phy->ops.write_reg = igb_write_phy_reg_igp;
		}
	}

	/* set lan id */
	hw->bus.func = (rd32(E1000_STATUS) & E1000_STATUS_FUNC_MASK) >>
			E1000_STATUS_FUNC_SHIFT;

	/* Set phy->phy_addr and phy->id. */
	ret_val = igb_get_phy_id_82575(hw);
	if (ret_val)
		return ret_val;

	/* Verify phy id and set remaining function pointers */
	switch (phy->id) {
	case M88E1543_E_PHY_ID:
	case I347AT4_E_PHY_ID:
	case M88E1112_E_PHY_ID:
	case M88E1111_I_PHY_ID:
		phy->type		= e1000_phy_m88;
		phy->ops.check_polarity	= igb_check_polarity_m88;
		phy->ops.get_phy_info	= igb_get_phy_info_m88;
		if (phy->id != M88E1111_I_PHY_ID)
			phy->ops.get_cable_length =
					 igb_get_cable_length_m88_gen2;
		else
			phy->ops.get_cable_length = igb_get_cable_length_m88;
		phy->ops.force_speed_duplex = igb_phy_force_speed_duplex_m88;
		/* Check if this PHY is confgured for media swap. */
		if (phy->id == M88E1112_E_PHY_ID) {
			u16 data;

			ret_val = phy->ops.write_reg(hw,
						     E1000_M88E1112_PAGE_ADDR,
						     2);
			if (ret_val)
				goto out;

			ret_val = phy->ops.read_reg(hw,
						    E1000_M88E1112_MAC_CTRL_1,
						    &data);
			if (ret_val)
				goto out;

			data = (data & E1000_M88E1112_MAC_CTRL_1_MODE_MASK) >>
			       E1000_M88E1112_MAC_CTRL_1_MODE_SHIFT;
			if (data == E1000_M88E1112_AUTO_COPPER_SGMII ||
			    data == E1000_M88E1112_AUTO_COPPER_BASEX)
				hw->mac.ops.check_for_link =
						igb_check_for_link_media_swap;
		}
		break;
	case IGP03E1000_E_PHY_ID:
		phy->type = e1000_phy_igp_3;
		phy->ops.get_phy_info = igb_get_phy_info_igp;
		phy->ops.get_cable_length = igb_get_cable_length_igp_2;
		phy->ops.force_speed_duplex = igb_phy_force_speed_duplex_igp;
		phy->ops.set_d0_lplu_state = igb_set_d0_lplu_state_82575;
		phy->ops.set_d3_lplu_state = igb_set_d3_lplu_state;
		break;
	case I82580_I_PHY_ID:
	case I350_I_PHY_ID:
		phy->type = e1000_phy_82580;
		phy->ops.force_speed_duplex =
					 igb_phy_force_speed_duplex_82580;
		phy->ops.get_cable_length = igb_get_cable_length_82580;
		phy->ops.get_phy_info = igb_get_phy_info_82580;
		phy->ops.set_d0_lplu_state = igb_set_d0_lplu_state_82580;
		phy->ops.set_d3_lplu_state = igb_set_d3_lplu_state_82580;
		break;
	case I210_I_PHY_ID:
		phy->type		= e1000_phy_i210;
		phy->ops.check_polarity	= igb_check_polarity_m88;
		phy->ops.get_phy_info	= igb_get_phy_info_m88;
		phy->ops.get_cable_length = igb_get_cable_length_m88_gen2;
		phy->ops.set_d0_lplu_state = igb_set_d0_lplu_state_82580;
		phy->ops.set_d3_lplu_state = igb_set_d3_lplu_state_82580;
		phy->ops.force_speed_duplex = igb_phy_force_speed_duplex_m88;
		break;
	default:
		ret_val = -E1000_ERR_PHY;
		goto out;
	}

out:
	return ret_val;
}

/**
 *  igb_init_nvm_params_82575 - Init NVM func ptrs.
 *  @hw: pointer to the HW structure
 **/
static s32 igb_init_nvm_params_82575(struct e1000_hw *hw)
{
	struct e1000_nvm_info *nvm = &hw->nvm;
	u32 eecd = rd32(E1000_EECD);
	u16 size;

	size = (u16)((eecd & E1000_EECD_SIZE_EX_MASK) >>
		     E1000_EECD_SIZE_EX_SHIFT);

	/* Added to a constant, "size" becomes the left-shift value
	 * for setting word_size.
	 */
	size += NVM_WORD_SIZE_BASE_SHIFT;

	/* Just in case size is out of range, cap it to the largest
	 * EEPROM size supported
	 */
	if (size > 15)
		size = 15;

	nvm->word_size = 1 << size;
	nvm->opcode_bits = 8;
	nvm->delay_usec = 1;

	switch (nvm->override) {
	case e1000_nvm_override_spi_large:
		nvm->page_size = 32;
		nvm->address_bits = 16;
		break;
	case e1000_nvm_override_spi_small:
		nvm->page_size = 8;
		nvm->address_bits = 8;
		break;
	default:
		nvm->page_size = eecd & E1000_EECD_ADDR_BITS ? 32 : 8;
		nvm->address_bits = eecd & E1000_EECD_ADDR_BITS ?
				    16 : 8;
		break;
	}
	if (nvm->word_size == (1 << 15))
		nvm->page_size = 128;

	nvm->type = e1000_nvm_eeprom_spi;

	/* NVM Function Pointers */
	nvm->ops.acquire = igb_acquire_nvm_82575;
	nvm->ops.release = igb_release_nvm_82575;
	nvm->ops.write = igb_write_nvm_spi;
	nvm->ops.validate = igb_validate_nvm_checksum;
	nvm->ops.update = igb_update_nvm_checksum;
	if (nvm->word_size < (1 << 15))
		nvm->ops.read = igb_read_nvm_eerd;
	else
		nvm->ops.read = igb_read_nvm_spi;

	/* override generic family function pointers for specific descendants */
	switch (hw->mac.type) {
	case e1000_82580:
		nvm->ops.validate = igb_validate_nvm_checksum_82580;
		nvm->ops.update = igb_update_nvm_checksum_82580;
		break;
	case e1000_i354:
	case e1000_i350:
		nvm->ops.validate = igb_validate_nvm_checksum_i350;
		nvm->ops.update = igb_update_nvm_checksum_i350;
		break;
	default:
		break;
	}

	return 0;
}

/**
 *  igb_init_mac_params_82575 - Init MAC func ptrs.
 *  @hw: pointer to the HW structure
 **/
static s32 igb_init_mac_params_82575(struct e1000_hw *hw)
{
	struct e1000_mac_info *mac = &hw->mac;
	struct e1000_dev_spec_82575 *dev_spec = &hw->dev_spec._82575;

	/* Set mta register count */
	mac->mta_reg_count = 128;
	/* Set rar entry count */
	switch (mac->type) {
	case e1000_82576:
		mac->rar_entry_count = E1000_RAR_ENTRIES_82576;
		break;
	case e1000_82580:
		mac->rar_entry_count = E1000_RAR_ENTRIES_82580;
		break;
	case e1000_i350:
	case e1000_i354:
		mac->rar_entry_count = E1000_RAR_ENTRIES_I350;
		break;
	default:
		mac->rar_entry_count = E1000_RAR_ENTRIES_82575;
		break;
	}
	/* reset */
	if (mac->type >= e1000_82580)
		mac->ops.reset_hw = igb_reset_hw_82580;
	else
		mac->ops.reset_hw = igb_reset_hw_82575;

	if (mac->type >= e1000_i210) {
		mac->ops.acquire_swfw_sync = igb_acquire_swfw_sync_i210;
		mac->ops.release_swfw_sync = igb_release_swfw_sync_i210;

	} else {
		mac->ops.acquire_swfw_sync = igb_acquire_swfw_sync_82575;
		mac->ops.release_swfw_sync = igb_release_swfw_sync_82575;
	}

	/* Set if part includes ASF firmware */
	mac->asf_firmware_present = true;
	/* Set if manageability features are enabled. */
	mac->arc_subsystem_valid =
		(rd32(E1000_FWSM) & E1000_FWSM_MODE_MASK)
			? true : false;
	/* enable EEE on i350 parts and later parts */
	if (mac->type >= e1000_i350)
		dev_spec->eee_disable = false;
	else
		dev_spec->eee_disable = true;
	/* Allow a single clear of the SW semaphore on I210 and newer */
	if (mac->type >= e1000_i210)
		dev_spec->clear_semaphore_once = true;
	/* physical interface link setup */
	mac->ops.setup_physical_interface =
		(hw->phy.media_type == e1000_media_type_copper)
			? igb_setup_copper_link_82575
			: igb_setup_serdes_link_82575;

	if (mac->type == e1000_82580) {
		switch (hw->device_id) {
		/* feature not supported on these id's */
		case E1000_DEV_ID_DH89XXCC_SGMII:
		case E1000_DEV_ID_DH89XXCC_SERDES:
		case E1000_DEV_ID_DH89XXCC_BACKPLANE:
		case E1000_DEV_ID_DH89XXCC_SFP:
			break;
		default:
			hw->dev_spec._82575.mas_capable = true;
			break;
		}
	}
	return 0;
}

/**
 *  igb_set_sfp_media_type_82575 - derives SFP module media type.
 *  @hw: pointer to the HW structure
 *
 *  The media type is chosen based on SFP module.
 *  compatibility flags retrieved from SFP ID EEPROM.
 **/
static s32 igb_set_sfp_media_type_82575(struct e1000_hw *hw)
{
	s32 ret_val = E1000_ERR_CONFIG;
	u32 ctrl_ext = 0;
	struct e1000_dev_spec_82575 *dev_spec = &hw->dev_spec._82575;
	struct e1000_sfp_flags *eth_flags = &dev_spec->eth_flags;
	u8 tranceiver_type = 0;
	s32 timeout = 3;

	/* Turn I2C interface ON and power on sfp cage */
	ctrl_ext = rd32(E1000_CTRL_EXT);
	ctrl_ext &= ~E1000_CTRL_EXT_SDP3_DATA;
	wr32(E1000_CTRL_EXT, ctrl_ext | E1000_CTRL_I2C_ENA);

	wrfl();

	/* Read SFP module data */
	while (timeout) {
		ret_val = igb_read_sfp_data_byte(hw,
			E1000_I2CCMD_SFP_DATA_ADDR(E1000_SFF_IDENTIFIER_OFFSET),
			&tranceiver_type);
		if (ret_val == 0)
			break;
		msleep(100);
		timeout--;
	}
	if (ret_val != 0)
		goto out;

	ret_val = igb_read_sfp_data_byte(hw,
			E1000_I2CCMD_SFP_DATA_ADDR(E1000_SFF_ETH_FLAGS_OFFSET),
			(u8 *)eth_flags);
	if (ret_val != 0)
		goto out;

	/* Check if there is some SFP module plugged and powered */
	if ((tranceiver_type == E1000_SFF_IDENTIFIER_SFP) ||
	    (tranceiver_type == E1000_SFF_IDENTIFIER_SFF)) {
		dev_spec->module_plugged = true;
		if (eth_flags->e1000_base_lx || eth_flags->e1000_base_sx) {
			hw->phy.media_type = e1000_media_type_internal_serdes;
		} else if (eth_flags->e100_base_fx) {
			dev_spec->sgmii_active = true;
			hw->phy.media_type = e1000_media_type_internal_serdes;
		} else if (eth_flags->e1000_base_t) {
			dev_spec->sgmii_active = true;
			hw->phy.media_type = e1000_media_type_copper;
		} else {
			hw->phy.media_type = e1000_media_type_unknown;
			hw_dbg("PHY module has not been recognized\n");
			goto out;
		}
	} else {
		hw->phy.media_type = e1000_media_type_unknown;
	}
	ret_val = 0;
out:
	/* Restore I2C interface setting */
	wr32(E1000_CTRL_EXT, ctrl_ext);
	return ret_val;
}

static s32 igb_get_invariants_82575(struct e1000_hw *hw)
{
	struct e1000_mac_info *mac = &hw->mac;
	struct e1000_dev_spec_82575 * dev_spec = &hw->dev_spec._82575;
	s32 ret_val;
	u32 ctrl_ext = 0;
	u32 link_mode = 0;

	switch (hw->device_id) {
	case E1000_DEV_ID_82575EB_COPPER:
	case E1000_DEV_ID_82575EB_FIBER_SERDES:
	case E1000_DEV_ID_82575GB_QUAD_COPPER:
		mac->type = e1000_82575;
		break;
	case E1000_DEV_ID_82576:
	case E1000_DEV_ID_82576_NS:
	case E1000_DEV_ID_82576_NS_SERDES:
	case E1000_DEV_ID_82576_FIBER:
	case E1000_DEV_ID_82576_SERDES:
	case E1000_DEV_ID_82576_QUAD_COPPER:
	case E1000_DEV_ID_82576_QUAD_COPPER_ET2:
	case E1000_DEV_ID_82576_SERDES_QUAD:
		mac->type = e1000_82576;
		break;
	case E1000_DEV_ID_82580_COPPER:
	case E1000_DEV_ID_82580_FIBER:
	case E1000_DEV_ID_82580_QUAD_FIBER:
	case E1000_DEV_ID_82580_SERDES:
	case E1000_DEV_ID_82580_SGMII:
	case E1000_DEV_ID_82580_COPPER_DUAL:
	case E1000_DEV_ID_DH89XXCC_SGMII:
	case E1000_DEV_ID_DH89XXCC_SERDES:
	case E1000_DEV_ID_DH89XXCC_BACKPLANE:
	case E1000_DEV_ID_DH89XXCC_SFP:
		mac->type = e1000_82580;
		break;
	case E1000_DEV_ID_I350_COPPER:
	case E1000_DEV_ID_I350_FIBER:
	case E1000_DEV_ID_I350_SERDES:
	case E1000_DEV_ID_I350_SGMII:
		mac->type = e1000_i350;
		break;
	case E1000_DEV_ID_I210_COPPER:
	case E1000_DEV_ID_I210_FIBER:
	case E1000_DEV_ID_I210_SERDES:
	case E1000_DEV_ID_I210_SGMII:
	case E1000_DEV_ID_I210_COPPER_FLASHLESS:
	case E1000_DEV_ID_I210_SERDES_FLASHLESS:
		mac->type = e1000_i210;
		break;
	case E1000_DEV_ID_I211_COPPER:
		mac->type = e1000_i211;
		break;
	case E1000_DEV_ID_I354_BACKPLANE_1GBPS:
	case E1000_DEV_ID_I354_SGMII:
	case E1000_DEV_ID_I354_BACKPLANE_2_5GBPS:
		mac->type = e1000_i354;
		break;
	default:
		return -E1000_ERR_MAC_INIT;
		break;
	}

	/* Set media type */
	/* The 82575 uses bits 22:23 for link mode. The mode can be changed
	 * based on the EEPROM. We cannot rely upon device ID. There
	 * is no distinguishable difference between fiber and internal
	 * SerDes mode on the 82575. There can be an external PHY attached
	 * on the SGMII interface. For this, we'll set sgmii_active to true.
	 */
	hw->phy.media_type = e1000_media_type_copper;
	dev_spec->sgmii_active = false;
	dev_spec->module_plugged = false;

	ctrl_ext = rd32(E1000_CTRL_EXT);

	link_mode = ctrl_ext & E1000_CTRL_EXT_LINK_MODE_MASK;
	switch (link_mode) {
	case E1000_CTRL_EXT_LINK_MODE_1000BASE_KX:
		hw->phy.media_type = e1000_media_type_internal_serdes;
		break;
	case E1000_CTRL_EXT_LINK_MODE_SGMII:
		/* Get phy control interface type set (MDIO vs. I2C)*/
		if (igb_sgmii_uses_mdio_82575(hw)) {
			hw->phy.media_type = e1000_media_type_copper;
			dev_spec->sgmii_active = true;
			break;
		}
		/* fall through for I2C based SGMII */
	case E1000_CTRL_EXT_LINK_MODE_PCIE_SERDES:
		/* read media type from SFP EEPROM */
		ret_val = igb_set_sfp_media_type_82575(hw);
		if ((ret_val != 0) ||
		    (hw->phy.media_type == e1000_media_type_unknown)) {
			/* If media type was not identified then return media
			 * type defined by the CTRL_EXT settings.
			 */
			hw->phy.media_type = e1000_media_type_internal_serdes;

			if (link_mode == E1000_CTRL_EXT_LINK_MODE_SGMII) {
				hw->phy.media_type = e1000_media_type_copper;
				dev_spec->sgmii_active = true;
			}

			break;
		}

		/* do not change link mode for 100BaseFX */
		if (dev_spec->eth_flags.e100_base_fx)
			break;

		/* change current link mode setting */
		ctrl_ext &= ~E1000_CTRL_EXT_LINK_MODE_MASK;

		if (hw->phy.media_type == e1000_media_type_copper)
			ctrl_ext |= E1000_CTRL_EXT_LINK_MODE_SGMII;
		else
			ctrl_ext |= E1000_CTRL_EXT_LINK_MODE_PCIE_SERDES;

		wr32(E1000_CTRL_EXT, ctrl_ext);

		break;
	default:
		break;
	}

	/* mac initialization and operations */
	ret_val = igb_init_mac_params_82575(hw);
	if (ret_val)
		goto out;

	/* NVM initialization */
	ret_val = igb_init_nvm_params_82575(hw);
	switch (hw->mac.type) {
	case e1000_i210:
	case e1000_i211:
		ret_val = igb_init_nvm_params_i210(hw);
		break;
	default:
		break;
	}

	if (ret_val)
		goto out;

	/* if part supports SR-IOV then initialize mailbox parameters */
	switch (mac->type) {
	case e1000_82576:
	case e1000_i350:
		igb_init_mbx_params_pf(hw);
		break;
	default:
		break;
	}

	/* setup PHY parameters */
	ret_val = igb_init_phy_params_82575(hw);

out:
	return ret_val;
}

/**
 *  igb_acquire_phy_82575 - Acquire rights to access PHY
 *  @hw: pointer to the HW structure
 *
 *  Acquire access rights to the correct PHY.  This is a
 *  function pointer entry point called by the api module.
 **/
static s32 igb_acquire_phy_82575(struct e1000_hw *hw)
{
	u16 mask = E1000_SWFW_PHY0_SM;

	if (hw->bus.func == E1000_FUNC_1)
		mask = E1000_SWFW_PHY1_SM;
	else if (hw->bus.func == E1000_FUNC_2)
		mask = E1000_SWFW_PHY2_SM;
	else if (hw->bus.func == E1000_FUNC_3)
		mask = E1000_SWFW_PHY3_SM;

	return hw->mac.ops.acquire_swfw_sync(hw, mask);
}

/**
 *  igb_release_phy_82575 - Release rights to access PHY
 *  @hw: pointer to the HW structure
 *
 *  A wrapper to release access rights to the correct PHY.  This is a
 *  function pointer entry point called by the api module.
 **/
static void igb_release_phy_82575(struct e1000_hw *hw)
{
	u16 mask = E1000_SWFW_PHY0_SM;

	if (hw->bus.func == E1000_FUNC_1)
		mask = E1000_SWFW_PHY1_SM;
	else if (hw->bus.func == E1000_FUNC_2)
		mask = E1000_SWFW_PHY2_SM;
	else if (hw->bus.func == E1000_FUNC_3)
		mask = E1000_SWFW_PHY3_SM;

	hw->mac.ops.release_swfw_sync(hw, mask);
}

/**
 *  igb_read_phy_reg_sgmii_82575 - Read PHY register using sgmii
 *  @hw: pointer to the HW structure
 *  @offset: register offset to be read
 *  @data: pointer to the read data
 *
 *  Reads the PHY register at offset using the serial gigabit media independent
 *  interface and stores the retrieved information in data.
 **/
static s32 igb_read_phy_reg_sgmii_82575(struct e1000_hw *hw, u32 offset,
					  u16 *data)
{
	s32 ret_val = -E1000_ERR_PARAM;

	if (offset > E1000_MAX_SGMII_PHY_REG_ADDR) {
		hw_dbg("PHY Address %u is out of range\n", offset);
		goto out;
	}

	ret_val = hw->phy.ops.acquire(hw);
	if (ret_val)
		goto out;

	ret_val = igb_read_phy_reg_i2c(hw, offset, data);

	hw->phy.ops.release(hw);

out:
	return ret_val;
}

/**
 *  igb_write_phy_reg_sgmii_82575 - Write PHY register using sgmii
 *  @hw: pointer to the HW structure
 *  @offset: register offset to write to
 *  @data: data to write at register offset
 *
 *  Writes the data to PHY register at the offset using the serial gigabit
 *  media independent interface.
 **/
static s32 igb_write_phy_reg_sgmii_82575(struct e1000_hw *hw, u32 offset,
					   u16 data)
{
	s32 ret_val = -E1000_ERR_PARAM;


	if (offset > E1000_MAX_SGMII_PHY_REG_ADDR) {
		hw_dbg("PHY Address %d is out of range\n", offset);
		goto out;
	}

	ret_val = hw->phy.ops.acquire(hw);
	if (ret_val)
		goto out;

	ret_val = igb_write_phy_reg_i2c(hw, offset, data);

	hw->phy.ops.release(hw);

out:
	return ret_val;
}

/**
 *  igb_get_phy_id_82575 - Retrieve PHY addr and id
 *  @hw: pointer to the HW structure
 *
 *  Retrieves the PHY address and ID for both PHY's which do and do not use
 *  sgmi interface.
 **/
static s32 igb_get_phy_id_82575(struct e1000_hw *hw)
{
	struct e1000_phy_info *phy = &hw->phy;
	s32  ret_val = 0;
	u16 phy_id;
	u32 ctrl_ext;
	u32 mdic;

	/* Extra read required for some PHY's on i354 */
	if (hw->mac.type == e1000_i354)
		igb_get_phy_id(hw);

	/* For SGMII PHYs, we try the list of possible addresses until
	 * we find one that works.  For non-SGMII PHYs
	 * (e.g. integrated copper PHYs), an address of 1 should
	 * work.  The result of this function should mean phy->phy_addr
	 * and phy->id are set correctly.
	 */
	if (!(igb_sgmii_active_82575(hw))) {
		phy->addr = 1;
		ret_val = igb_get_phy_id(hw);
		goto out;
	}

	if (igb_sgmii_uses_mdio_82575(hw)) {
		switch (hw->mac.type) {
		case e1000_82575:
		case e1000_82576:
			mdic = rd32(E1000_MDIC);
			mdic &= E1000_MDIC_PHY_MASK;
			phy->addr = mdic >> E1000_MDIC_PHY_SHIFT;
			break;
		case e1000_82580:
		case e1000_i350:
		case e1000_i354:
		case e1000_i210:
		case e1000_i211:
			mdic = rd32(E1000_MDICNFG);
			mdic &= E1000_MDICNFG_PHY_MASK;
			phy->addr = mdic >> E1000_MDICNFG_PHY_SHIFT;
			break;
		default:
			ret_val = -E1000_ERR_PHY;
			goto out;
			break;
		}
		ret_val = igb_get_phy_id(hw);
		goto out;
	}

	/* Power on sgmii phy if it is disabled */
	ctrl_ext = rd32(E1000_CTRL_EXT);
	wr32(E1000_CTRL_EXT, ctrl_ext & ~E1000_CTRL_EXT_SDP3_DATA);
	wrfl();
	msleep(300);

	/* The address field in the I2CCMD register is 3 bits and 0 is invalid.
	 * Therefore, we need to test 1-7
	 */
	for (phy->addr = 1; phy->addr < 8; phy->addr++) {
		ret_val = igb_read_phy_reg_sgmii_82575(hw, PHY_ID1, &phy_id);
		if (ret_val == 0) {
			hw_dbg("Vendor ID 0x%08X read at address %u\n",
			       phy_id, phy->addr);
			/* At the time of this writing, The M88 part is
			 * the only supported SGMII PHY product.
			 */
			if (phy_id == M88_VENDOR)
				break;
		} else {
			hw_dbg("PHY address %u was unreadable\n", phy->addr);
		}
	}

	/* A valid PHY type couldn't be found. */
	if (phy->addr == 8) {
		phy->addr = 0;
		ret_val = -E1000_ERR_PHY;
		goto out;
	} else {
		ret_val = igb_get_phy_id(hw);
	}

	/* restore previous sfp cage power state */
	wr32(E1000_CTRL_EXT, ctrl_ext);

out:
	return ret_val;
}

/**
 *  igb_phy_hw_reset_sgmii_82575 - Performs a PHY reset
 *  @hw: pointer to the HW structure
 *
 *  Resets the PHY using the serial gigabit media independent interface.
 **/
static s32 igb_phy_hw_reset_sgmii_82575(struct e1000_hw *hw)
{
	s32 ret_val;

	/* This isn't a true "hard" reset, but is the only reset
	 * available to us at this time.
	 */

	hw_dbg("Soft resetting SGMII attached PHY...\n");

	/* SFP documentation requires the following to configure the SPF module
	 * to work on SGMII.  No further documentation is given.
	 */
	ret_val = hw->phy.ops.write_reg(hw, 0x1B, 0x8084);
	if (ret_val)
		goto out;

	ret_val = igb_phy_sw_reset(hw);

out:
	return ret_val;
}

/**
 *  igb_set_d0_lplu_state_82575 - Set Low Power Linkup D0 state
 *  @hw: pointer to the HW structure
 *  @active: true to enable LPLU, false to disable
 *
 *  Sets the LPLU D0 state according to the active flag.  When
 *  activating LPLU this function also disables smart speed
 *  and vice versa.  LPLU will not be activated unless the
 *  device autonegotiation advertisement meets standards of
 *  either 10 or 10/100 or 10/100/1000 at all duplexes.
 *  This is a function pointer entry point only called by
 *  PHY setup routines.
 **/
static s32 igb_set_d0_lplu_state_82575(struct e1000_hw *hw, bool active)
{
	struct e1000_phy_info *phy = &hw->phy;
	s32 ret_val;
	u16 data;

	ret_val = phy->ops.read_reg(hw, IGP02E1000_PHY_POWER_MGMT, &data);
	if (ret_val)
		goto out;

	if (active) {
		data |= IGP02E1000_PM_D0_LPLU;
		ret_val = phy->ops.write_reg(hw, IGP02E1000_PHY_POWER_MGMT,
						 data);
		if (ret_val)
			goto out;

		/* When LPLU is enabled, we should disable SmartSpeed */
		ret_val = phy->ops.read_reg(hw, IGP01E1000_PHY_PORT_CONFIG,
						&data);
		data &= ~IGP01E1000_PSCFR_SMART_SPEED;
		ret_val = phy->ops.write_reg(hw, IGP01E1000_PHY_PORT_CONFIG,
						 data);
		if (ret_val)
			goto out;
	} else {
		data &= ~IGP02E1000_PM_D0_LPLU;
		ret_val = phy->ops.write_reg(hw, IGP02E1000_PHY_POWER_MGMT,
						 data);
		/* LPLU and SmartSpeed are mutually exclusive.  LPLU is used
		 * during Dx states where the power conservation is most
		 * important.  During driver activity we should enable
		 * SmartSpeed, so performance is maintained.
		 */
		if (phy->smart_speed == e1000_smart_speed_on) {
			ret_val = phy->ops.read_reg(hw,
					IGP01E1000_PHY_PORT_CONFIG, &data);
			if (ret_val)
				goto out;

			data |= IGP01E1000_PSCFR_SMART_SPEED;
			ret_val = phy->ops.write_reg(hw,
					IGP01E1000_PHY_PORT_CONFIG, data);
			if (ret_val)
				goto out;
		} else if (phy->smart_speed == e1000_smart_speed_off) {
			ret_val = phy->ops.read_reg(hw,
					IGP01E1000_PHY_PORT_CONFIG, &data);
			if (ret_val)
				goto out;

			data &= ~IGP01E1000_PSCFR_SMART_SPEED;
			ret_val = phy->ops.write_reg(hw,
					IGP01E1000_PHY_PORT_CONFIG, data);
			if (ret_val)
				goto out;
		}
	}

out:
	return ret_val;
}

/**
 *  igb_set_d0_lplu_state_82580 - Set Low Power Linkup D0 state
 *  @hw: pointer to the HW structure
 *  @active: true to enable LPLU, false to disable
 *
 *  Sets the LPLU D0 state according to the active flag.  When
 *  activating LPLU this function also disables smart speed
 *  and vice versa.  LPLU will not be activated unless the
 *  device autonegotiation advertisement meets standards of
 *  either 10 or 10/100 or 10/100/1000 at all duplexes.
 *  This is a function pointer entry point only called by
 *  PHY setup routines.
 **/
static s32 igb_set_d0_lplu_state_82580(struct e1000_hw *hw, bool active)
{
	struct e1000_phy_info *phy = &hw->phy;
	s32 ret_val = 0;
	u16 data;

	data = rd32(E1000_82580_PHY_POWER_MGMT);

	if (active) {
		data |= E1000_82580_PM_D0_LPLU;

		/* When LPLU is enabled, we should disable SmartSpeed */
		data &= ~E1000_82580_PM_SPD;
	} else {
		data &= ~E1000_82580_PM_D0_LPLU;

		/* LPLU and SmartSpeed are mutually exclusive.  LPLU is used
		 * during Dx states where the power conservation is most
		 * important.  During driver activity we should enable
		 * SmartSpeed, so performance is maintained.
		 */
		if (phy->smart_speed == e1000_smart_speed_on)
			data |= E1000_82580_PM_SPD;
		else if (phy->smart_speed == e1000_smart_speed_off)
			data &= ~E1000_82580_PM_SPD; }

	wr32(E1000_82580_PHY_POWER_MGMT, data);
	return ret_val;
}

/**
 *  igb_set_d3_lplu_state_82580 - Sets low power link up state for D3
 *  @hw: pointer to the HW structure
 *  @active: boolean used to enable/disable lplu
 *
 *  Success returns 0, Failure returns 1
 *
 *  The low power link up (lplu) state is set to the power management level D3
 *  and SmartSpeed is disabled when active is true, else clear lplu for D3
 *  and enable Smartspeed.  LPLU and Smartspeed are mutually exclusive.  LPLU
 *  is used during Dx states where the power conservation is most important.
 *  During driver activity, SmartSpeed should be enabled so performance is
 *  maintained.
 **/
static s32 igb_set_d3_lplu_state_82580(struct e1000_hw *hw, bool active)
{
	struct e1000_phy_info *phy = &hw->phy;
	s32 ret_val = 0;
	u16 data;

	data = rd32(E1000_82580_PHY_POWER_MGMT);

	if (!active) {
		data &= ~E1000_82580_PM_D3_LPLU;
		/* LPLU and SmartSpeed are mutually exclusive.  LPLU is used
		 * during Dx states where the power conservation is most
		 * important.  During driver activity we should enable
		 * SmartSpeed, so performance is maintained.
		 */
		if (phy->smart_speed == e1000_smart_speed_on)
			data |= E1000_82580_PM_SPD;
		else if (phy->smart_speed == e1000_smart_speed_off)
			data &= ~E1000_82580_PM_SPD;
	} else if ((phy->autoneg_advertised == E1000_ALL_SPEED_DUPLEX) ||
		   (phy->autoneg_advertised == E1000_ALL_NOT_GIG) ||
		   (phy->autoneg_advertised == E1000_ALL_10_SPEED)) {
		data |= E1000_82580_PM_D3_LPLU;
		/* When LPLU is enabled, we should disable SmartSpeed */
		data &= ~E1000_82580_PM_SPD;
	}

	wr32(E1000_82580_PHY_POWER_MGMT, data);
	return ret_val;
}

/**
 *  igb_acquire_nvm_82575 - Request for access to EEPROM
 *  @hw: pointer to the HW structure
 *
 *  Acquire the necessary semaphores for exclusive access to the EEPROM.
 *  Set the EEPROM access request bit and wait for EEPROM access grant bit.
 *  Return successful if access grant bit set, else clear the request for
 *  EEPROM access and return -E1000_ERR_NVM (-1).
 **/
static s32 igb_acquire_nvm_82575(struct e1000_hw *hw)
{
	s32 ret_val;

	ret_val = hw->mac.ops.acquire_swfw_sync(hw, E1000_SWFW_EEP_SM);
	if (ret_val)
		goto out;

	ret_val = igb_acquire_nvm(hw);

	if (ret_val)
		hw->mac.ops.release_swfw_sync(hw, E1000_SWFW_EEP_SM);

out:
	return ret_val;
}

/**
 *  igb_release_nvm_82575 - Release exclusive access to EEPROM
 *  @hw: pointer to the HW structure
 *
 *  Stop any current commands to the EEPROM and clear the EEPROM request bit,
 *  then release the semaphores acquired.
 **/
static void igb_release_nvm_82575(struct e1000_hw *hw)
{
	igb_release_nvm(hw);
	hw->mac.ops.release_swfw_sync(hw, E1000_SWFW_EEP_SM);
}

/**
 *  igb_acquire_swfw_sync_82575 - Acquire SW/FW semaphore
 *  @hw: pointer to the HW structure
 *  @mask: specifies which semaphore to acquire
 *
 *  Acquire the SW/FW semaphore to access the PHY or NVM.  The mask
 *  will also specify which port we're acquiring the lock for.
 **/
static s32 igb_acquire_swfw_sync_82575(struct e1000_hw *hw, u16 mask)
{
	u32 swfw_sync;
	u32 swmask = mask;
	u32 fwmask = mask << 16;
	s32 ret_val = 0;
	s32 i = 0, timeout = 200; /* FIXME: find real value to use here */

	while (i < timeout) {
		if (igb_get_hw_semaphore(hw)) {
			ret_val = -E1000_ERR_SWFW_SYNC;
			goto out;
		}

		swfw_sync = rd32(E1000_SW_FW_SYNC);
		if (!(swfw_sync & (fwmask | swmask)))
			break;

		/* Firmware currently using resource (fwmask)
		 * or other software thread using resource (swmask)
		 */
		igb_put_hw_semaphore(hw);
		mdelay(5);
		i++;
	}

	if (i == timeout) {
		hw_dbg("Driver can't access resource, SW_FW_SYNC timeout.\n");
		ret_val = -E1000_ERR_SWFW_SYNC;
		goto out;
	}

	swfw_sync |= swmask;
	wr32(E1000_SW_FW_SYNC, swfw_sync);

	igb_put_hw_semaphore(hw);

out:
	return ret_val;
}

/**
 *  igb_release_swfw_sync_82575 - Release SW/FW semaphore
 *  @hw: pointer to the HW structure
 *  @mask: specifies which semaphore to acquire
 *
 *  Release the SW/FW semaphore used to access the PHY or NVM.  The mask
 *  will also specify which port we're releasing the lock for.
 **/
static void igb_release_swfw_sync_82575(struct e1000_hw *hw, u16 mask)
{
	u32 swfw_sync;

	while (igb_get_hw_semaphore(hw) != 0);
	/* Empty */

	swfw_sync = rd32(E1000_SW_FW_SYNC);
	swfw_sync &= ~mask;
	wr32(E1000_SW_FW_SYNC, swfw_sync);

	igb_put_hw_semaphore(hw);
}

/**
 *  igb_get_cfg_done_82575 - Read config done bit
 *  @hw: pointer to the HW structure
 *
 *  Read the management control register for the config done bit for
 *  completion status.  NOTE: silicon which is EEPROM-less will fail trying
 *  to read the config done bit, so an error is *ONLY* logged and returns
 *  0.  If we were to return with error, EEPROM-less silicon
 *  would not be able to be reset or change link.
 **/
static s32 igb_get_cfg_done_82575(struct e1000_hw *hw)
{
	s32 timeout = PHY_CFG_TIMEOUT;
	s32 ret_val = 0;
	u32 mask = E1000_NVM_CFG_DONE_PORT_0;

	if (hw->bus.func == 1)
		mask = E1000_NVM_CFG_DONE_PORT_1;
	else if (hw->bus.func == E1000_FUNC_2)
		mask = E1000_NVM_CFG_DONE_PORT_2;
	else if (hw->bus.func == E1000_FUNC_3)
		mask = E1000_NVM_CFG_DONE_PORT_3;

	while (timeout) {
		if (rd32(E1000_EEMNGCTL) & mask)
			break;
		msleep(1);
		timeout--;
	}
	if (!timeout)
		hw_dbg("MNG configuration cycle has not completed.\n");

	/* If EEPROM is not marked present, init the PHY manually */
	if (((rd32(E1000_EECD) & E1000_EECD_PRES) == 0) &&
	    (hw->phy.type == e1000_phy_igp_3))
		igb_phy_init_script_igp3(hw);

	return ret_val;
}

/**
 *  igb_get_link_up_info_82575 - Get link speed/duplex info
 *  @hw: pointer to the HW structure
 *  @speed: stores the current speed
 *  @duplex: stores the current duplex
 *
 *  This is a wrapper function, if using the serial gigabit media independent
 *  interface, use PCS to retrieve the link speed and duplex information.
 *  Otherwise, use the generic function to get the link speed and duplex info.
 **/
static s32 igb_get_link_up_info_82575(struct e1000_hw *hw, u16 *speed,
					u16 *duplex)
{
	s32 ret_val;

	if (hw->phy.media_type != e1000_media_type_copper)
		ret_val = igb_get_pcs_speed_and_duplex_82575(hw, speed,
							       duplex);
	else
		ret_val = igb_get_speed_and_duplex_copper(hw, speed,
								    duplex);

	return ret_val;
}

/**
 *  igb_check_for_link_82575 - Check for link
 *  @hw: pointer to the HW structure
 *
 *  If sgmii is enabled, then use the pcs register to determine link, otherwise
 *  use the generic interface for determining link.
 **/
static s32 igb_check_for_link_82575(struct e1000_hw *hw)
{
	s32 ret_val;
	u16 speed, duplex;

	if (hw->phy.media_type != e1000_media_type_copper) {
		ret_val = igb_get_pcs_speed_and_duplex_82575(hw, &speed,
		                                             &duplex);
		/* Use this flag to determine if link needs to be checked or
		 * not.  If  we have link clear the flag so that we do not
		 * continue to check for link.
		 */
		hw->mac.get_link_status = !hw->mac.serdes_has_link;

		/* Configure Flow Control now that Auto-Neg has completed.
		 * First, we need to restore the desired flow control
		 * settings because we may have had to re-autoneg with a
		 * different link partner.
		 */
		ret_val = igb_config_fc_after_link_up(hw);
		if (ret_val)
			hw_dbg("Error configuring flow control\n");
	} else {
		ret_val = igb_check_for_copper_link(hw);
	}

	return ret_val;
}

/**
 *  igb_power_up_serdes_link_82575 - Power up the serdes link after shutdown
 *  @hw: pointer to the HW structure
 **/
void igb_power_up_serdes_link_82575(struct e1000_hw *hw)
{
	u32 reg;


	if ((hw->phy.media_type != e1000_media_type_internal_serdes) &&
	    !igb_sgmii_active_82575(hw))
		return;

	/* Enable PCS to turn on link */
	reg = rd32(E1000_PCS_CFG0);
	reg |= E1000_PCS_CFG_PCS_EN;
	wr32(E1000_PCS_CFG0, reg);

	/* Power up the laser */
	reg = rd32(E1000_CTRL_EXT);
	reg &= ~E1000_CTRL_EXT_SDP3_DATA;
	wr32(E1000_CTRL_EXT, reg);

	/* flush the write to verify completion */
	wrfl();
	msleep(1);
}

/**
 *  igb_get_pcs_speed_and_duplex_82575 - Retrieve current speed/duplex
 *  @hw: pointer to the HW structure
 *  @speed: stores the current speed
 *  @duplex: stores the current duplex
 *
 *  Using the physical coding sub-layer (PCS), retrieve the current speed and
 *  duplex, then store the values in the pointers provided.
 **/
static s32 igb_get_pcs_speed_and_duplex_82575(struct e1000_hw *hw, u16 *speed,
						u16 *duplex)
{
	struct e1000_mac_info *mac = &hw->mac;
	u32 pcs, status;

	/* Set up defaults for the return values of this function */
	mac->serdes_has_link = false;
	*speed = 0;
	*duplex = 0;

	/* Read the PCS Status register for link state. For non-copper mode,
	 * the status register is not accurate. The PCS status register is
	 * used instead.
	 */
	pcs = rd32(E1000_PCS_LSTAT);

	/* The link up bit determines when link is up on autoneg. The sync ok
	 * gets set once both sides sync up and agree upon link. Stable link
	 * can be determined by checking for both link up and link sync ok
	 */
	if ((pcs & E1000_PCS_LSTS_LINK_OK) && (pcs & E1000_PCS_LSTS_SYNK_OK)) {
		mac->serdes_has_link = true;

		/* Detect and store PCS speed */
		if (pcs & E1000_PCS_LSTS_SPEED_1000)
			*speed = SPEED_1000;
		else if (pcs & E1000_PCS_LSTS_SPEED_100)
			*speed = SPEED_100;
		else
			*speed = SPEED_10;

		/* Detect and store PCS duplex */
		if (pcs & E1000_PCS_LSTS_DUPLEX_FULL)
			*duplex = FULL_DUPLEX;
		else
			*duplex = HALF_DUPLEX;

	/* Check if it is an I354 2.5Gb backplane connection. */
		if (mac->type == e1000_i354) {
			status = rd32(E1000_STATUS);
			if ((status & E1000_STATUS_2P5_SKU) &&
			    !(status & E1000_STATUS_2P5_SKU_OVER)) {
				*speed = SPEED_2500;
				*duplex = FULL_DUPLEX;
				hw_dbg("2500 Mbs, ");
				hw_dbg("Full Duplex\n");
			}
		}

	}

	return 0;
}

/**
 *  igb_shutdown_serdes_link_82575 - Remove link during power down
 *  @hw: pointer to the HW structure
 *
 *  In the case of fiber serdes, shut down optics and PCS on driver unload
 *  when management pass thru is not enabled.
 **/
void igb_shutdown_serdes_link_82575(struct e1000_hw *hw)
{
	u32 reg;

	if (hw->phy.media_type != e1000_media_type_internal_serdes &&
	    igb_sgmii_active_82575(hw))
		return;

	if (!igb_enable_mng_pass_thru(hw)) {
		/* Disable PCS to turn off link */
		reg = rd32(E1000_PCS_CFG0);
		reg &= ~E1000_PCS_CFG_PCS_EN;
		wr32(E1000_PCS_CFG0, reg);

		/* shutdown the laser */
		reg = rd32(E1000_CTRL_EXT);
		reg |= E1000_CTRL_EXT_SDP3_DATA;
		wr32(E1000_CTRL_EXT, reg);

		/* flush the write to verify completion */
		wrfl();
		msleep(1);
	}
}

/**
 *  igb_reset_hw_82575 - Reset hardware
 *  @hw: pointer to the HW structure
 *
 *  This resets the hardware into a known state.  This is a
 *  function pointer entry point called by the api module.
 **/
static s32 igb_reset_hw_82575(struct e1000_hw *hw)
{
	u32 ctrl;
	s32 ret_val;

	/* Prevent the PCI-E bus from sticking if there is no TLP connection
	 * on the last TLP read/write transaction when MAC is reset.
	 */
	ret_val = igb_disable_pcie_master(hw);
	if (ret_val)
		hw_dbg("PCI-E Master disable polling has failed.\n");

	/* set the completion timeout for interface */
	ret_val = igb_set_pcie_completion_timeout(hw);
	if (ret_val) {
		hw_dbg("PCI-E Set completion timeout has failed.\n");
	}

	hw_dbg("Masking off all interrupts\n");
	wr32(E1000_IMC, 0xffffffff);

	wr32(E1000_RCTL, 0);
	wr32(E1000_TCTL, E1000_TCTL_PSP);
	wrfl();

	msleep(10);

	ctrl = rd32(E1000_CTRL);

	hw_dbg("Issuing a global reset to MAC\n");
	wr32(E1000_CTRL, ctrl | E1000_CTRL_RST);

	ret_val = igb_get_auto_rd_done(hw);
	if (ret_val) {
		/* When auto config read does not complete, do not
		 * return with an error. This can happen in situations
		 * where there is no eeprom and prevents getting link.
		 */
		hw_dbg("Auto Read Done did not complete\n");
	}

	/* If EEPROM is not present, run manual init scripts */
	if ((rd32(E1000_EECD) & E1000_EECD_PRES) == 0)
		igb_reset_init_script_82575(hw);

	/* Clear any pending interrupt events. */
	wr32(E1000_IMC, 0xffffffff);
	rd32(E1000_ICR);

	/* Install any alternate MAC address into RAR0 */
	ret_val = igb_check_alt_mac_addr(hw);

	return ret_val;
}

/**
 *  igb_init_hw_82575 - Initialize hardware
 *  @hw: pointer to the HW structure
 *
 *  This inits the hardware readying it for operation.
 **/
static s32 igb_init_hw_82575(struct e1000_hw *hw)
{
	struct e1000_mac_info *mac = &hw->mac;
	s32 ret_val;
	u16 i, rar_count = mac->rar_entry_count;

	/* Initialize identification LED */
	ret_val = igb_id_led_init(hw);
	if (ret_val) {
		hw_dbg("Error initializing identification LED\n");
		/* This is not fatal and we should not stop init due to this */
	}

	/* Disabling VLAN filtering */
	hw_dbg("Initializing the IEEE VLAN\n");
	if ((hw->mac.type == e1000_i350) || (hw->mac.type == e1000_i354))
		igb_clear_vfta_i350(hw);
	else
		igb_clear_vfta(hw);

	/* Setup the receive address */
	igb_init_rx_addrs(hw, rar_count);

	/* Zero out the Multicast HASH table */
	hw_dbg("Zeroing the MTA\n");
	for (i = 0; i < mac->mta_reg_count; i++)
		array_wr32(E1000_MTA, i, 0);

	/* Zero out the Unicast HASH table */
	hw_dbg("Zeroing the UTA\n");
	for (i = 0; i < mac->uta_reg_count; i++)
		array_wr32(E1000_UTA, i, 0);

	/* Setup link and flow control */
	ret_val = igb_setup_link(hw);

	/* Clear all of the statistics registers (clear on read).  It is
	 * important that we do this after we have tried to establish link
	 * because the symbol error count will increment wildly if there
	 * is no link.
	 */
	igb_clear_hw_cntrs_82575(hw);
	return ret_val;
}

/**
 *  igb_setup_copper_link_82575 - Configure copper link settings
 *  @hw: pointer to the HW structure
 *
 *  Configures the link for auto-neg or forced speed and duplex.  Then we check
 *  for link, once link is established calls to configure collision distance
 *  and flow control are called.
 **/
static s32 igb_setup_copper_link_82575(struct e1000_hw *hw)
{
	u32 ctrl;
	s32  ret_val;
	u32 phpm_reg;

	ctrl = rd32(E1000_CTRL);
	ctrl |= E1000_CTRL_SLU;
	ctrl &= ~(E1000_CTRL_FRCSPD | E1000_CTRL_FRCDPX);
	wr32(E1000_CTRL, ctrl);

	/* Clear Go Link Disconnect bit on supported devices */
	switch (hw->mac.type) {
	case e1000_82580:
	case e1000_i350:
	case e1000_i210:
	case e1000_i211:
		phpm_reg = rd32(E1000_82580_PHY_POWER_MGMT);
		phpm_reg &= ~E1000_82580_PM_GO_LINKD;
		wr32(E1000_82580_PHY_POWER_MGMT, phpm_reg);
		break;
	default:
		break;
	}

	ret_val = igb_setup_serdes_link_82575(hw);
	if (ret_val)
		goto out;

	if (igb_sgmii_active_82575(hw) && !hw->phy.reset_disable) {
		/* allow time for SFP cage time to power up phy */
		msleep(300);

		ret_val = hw->phy.ops.reset(hw);
		if (ret_val) {
			hw_dbg("Error resetting the PHY.\n");
			goto out;
		}
	}
	switch (hw->phy.type) {
	case e1000_phy_i210:
	case e1000_phy_m88:
		switch (hw->phy.id) {
		case I347AT4_E_PHY_ID:
		case M88E1112_E_PHY_ID:
		case M88E1543_E_PHY_ID:
		case I210_I_PHY_ID:
			ret_val = igb_copper_link_setup_m88_gen2(hw);
			break;
		default:
			ret_val = igb_copper_link_setup_m88(hw);
			break;
		}
		break;
	case e1000_phy_igp_3:
		ret_val = igb_copper_link_setup_igp(hw);
		break;
	case e1000_phy_82580:
		ret_val = igb_copper_link_setup_82580(hw);
		break;
	default:
		ret_val = -E1000_ERR_PHY;
		break;
	}

	if (ret_val)
		goto out;

	ret_val = igb_setup_copper_link(hw);
out:
	return ret_val;
}

/**
 *  igb_setup_serdes_link_82575 - Setup link for serdes
 *  @hw: pointer to the HW structure
 *
 *  Configure the physical coding sub-layer (PCS) link.  The PCS link is
 *  used on copper connections where the serialized gigabit media independent
 *  interface (sgmii), or serdes fiber is being used.  Configures the link
 *  for auto-negotiation or forces speed/duplex.
 **/
static s32 igb_setup_serdes_link_82575(struct e1000_hw *hw)
{
	u32 ctrl_ext, ctrl_reg, reg, anadv_reg;
	bool pcs_autoneg;
	s32 ret_val = E1000_SUCCESS;
	u16 data;

	if ((hw->phy.media_type != e1000_media_type_internal_serdes) &&
	    !igb_sgmii_active_82575(hw))
		return ret_val;


	/* On the 82575, SerDes loopback mode persists until it is
	 * explicitly turned off or a power cycle is performed.  A read to
	 * the register does not indicate its status.  Therefore, we ensure
	 * loopback mode is disabled during initialization.
	 */
	wr32(E1000_SCTL, E1000_SCTL_DISABLE_SERDES_LOOPBACK);

	/* power on the sfp cage if present and turn on I2C */
	ctrl_ext = rd32(E1000_CTRL_EXT);
	ctrl_ext &= ~E1000_CTRL_EXT_SDP3_DATA;
	ctrl_ext |= E1000_CTRL_I2C_ENA;
	wr32(E1000_CTRL_EXT, ctrl_ext);

	ctrl_reg = rd32(E1000_CTRL);
	ctrl_reg |= E1000_CTRL_SLU;

	if (hw->mac.type == e1000_82575 || hw->mac.type == e1000_82576) {
		/* set both sw defined pins */
		ctrl_reg |= E1000_CTRL_SWDPIN0 | E1000_CTRL_SWDPIN1;

		/* Set switch control to serdes energy detect */
		reg = rd32(E1000_CONNSW);
		reg |= E1000_CONNSW_ENRGSRC;
		wr32(E1000_CONNSW, reg);
	}

	reg = rd32(E1000_PCS_LCTL);

	/* default pcs_autoneg to the same setting as mac autoneg */
	pcs_autoneg = hw->mac.autoneg;

	switch (ctrl_ext & E1000_CTRL_EXT_LINK_MODE_MASK) {
	case E1000_CTRL_EXT_LINK_MODE_SGMII:
		/* sgmii mode lets the phy handle forcing speed/duplex */
		pcs_autoneg = true;
		/* autoneg time out should be disabled for SGMII mode */
		reg &= ~(E1000_PCS_LCTL_AN_TIMEOUT);
		break;
	case E1000_CTRL_EXT_LINK_MODE_1000BASE_KX:
		/* disable PCS autoneg and support parallel detect only */
		pcs_autoneg = false;
	default:
		if (hw->mac.type == e1000_82575 ||
		    hw->mac.type == e1000_82576) {
			ret_val = hw->nvm.ops.read(hw, NVM_COMPAT, 1, &data);
			if (ret_val) {
				printk(KERN_DEBUG "NVM Read Error\n\n");
				return ret_val;
			}

			if (data & E1000_EEPROM_PCS_AUTONEG_DISABLE_BIT)
				pcs_autoneg = false;
		}

		/* non-SGMII modes only supports a speed of 1000/Full for the
		 * link so it is best to just force the MAC and let the pcs
		 * link either autoneg or be forced to 1000/Full
		 */
		ctrl_reg |= E1000_CTRL_SPD_1000 | E1000_CTRL_FRCSPD |
		            E1000_CTRL_FD | E1000_CTRL_FRCDPX;

		/* set speed of 1000/Full if speed/duplex is forced */
		reg |= E1000_PCS_LCTL_FSV_1000 | E1000_PCS_LCTL_FDV_FULL;
		break;
	}

	wr32(E1000_CTRL, ctrl_reg);

	/* New SerDes mode allows for forcing speed or autonegotiating speed
	 * at 1gb. Autoneg should be default set by most drivers. This is the
	 * mode that will be compatible with older link partners and switches.
	 * However, both are supported by the hardware and some drivers/tools.
	 */
	reg &= ~(E1000_PCS_LCTL_AN_ENABLE | E1000_PCS_LCTL_FLV_LINK_UP |
		E1000_PCS_LCTL_FSD | E1000_PCS_LCTL_FORCE_LINK);

	if (pcs_autoneg) {
		/* Set PCS register for autoneg */
		reg |= E1000_PCS_LCTL_AN_ENABLE | /* Enable Autoneg */
		       E1000_PCS_LCTL_AN_RESTART; /* Restart autoneg */

		/* Disable force flow control for autoneg */
		reg &= ~E1000_PCS_LCTL_FORCE_FCTRL;

		/* Configure flow control advertisement for autoneg */
		anadv_reg = rd32(E1000_PCS_ANADV);
		anadv_reg &= ~(E1000_TXCW_ASM_DIR | E1000_TXCW_PAUSE);
		switch (hw->fc.requested_mode) {
		case e1000_fc_full:
		case e1000_fc_rx_pause:
			anadv_reg |= E1000_TXCW_ASM_DIR;
			anadv_reg |= E1000_TXCW_PAUSE;
			break;
		case e1000_fc_tx_pause:
			anadv_reg |= E1000_TXCW_ASM_DIR;
			break;
		default:
			break;
		}
		wr32(E1000_PCS_ANADV, anadv_reg);

		hw_dbg("Configuring Autoneg:PCS_LCTL=0x%08X\n", reg);
	} else {
		/* Set PCS register for forced link */
		reg |= E1000_PCS_LCTL_FSD;        /* Force Speed */

		/* Force flow control for forced link */
		reg |= E1000_PCS_LCTL_FORCE_FCTRL;

		hw_dbg("Configuring Forced Link:PCS_LCTL=0x%08X\n", reg);
	}

	wr32(E1000_PCS_LCTL, reg);

	if (!pcs_autoneg && !igb_sgmii_active_82575(hw))
		igb_force_mac_fc(hw);

	return ret_val;
}

/**
 *  igb_sgmii_active_82575 - Return sgmii state
 *  @hw: pointer to the HW structure
 *
 *  82575 silicon has a serialized gigabit media independent interface (sgmii)
 *  which can be enabled for use in the embedded applications.  Simply
 *  return the current state of the sgmii interface.
 **/
static bool igb_sgmii_active_82575(struct e1000_hw *hw)
{
	struct e1000_dev_spec_82575 *dev_spec = &hw->dev_spec._82575;
	return dev_spec->sgmii_active;
}

/**
 *  igb_reset_init_script_82575 - Inits HW defaults after reset
 *  @hw: pointer to the HW structure
 *
 *  Inits recommended HW defaults after a reset when there is no EEPROM
 *  detected. This is only for the 82575.
 **/
static s32 igb_reset_init_script_82575(struct e1000_hw *hw)
{
	if (hw->mac.type == e1000_82575) {
		hw_dbg("Running reset init script for 82575\n");
		/* SerDes configuration via SERDESCTRL */
		igb_write_8bit_ctrl_reg(hw, E1000_SCTL, 0x00, 0x0C);
		igb_write_8bit_ctrl_reg(hw, E1000_SCTL, 0x01, 0x78);
		igb_write_8bit_ctrl_reg(hw, E1000_SCTL, 0x1B, 0x23);
		igb_write_8bit_ctrl_reg(hw, E1000_SCTL, 0x23, 0x15);

		/* CCM configuration via CCMCTL register */
		igb_write_8bit_ctrl_reg(hw, E1000_CCMCTL, 0x14, 0x00);
		igb_write_8bit_ctrl_reg(hw, E1000_CCMCTL, 0x10, 0x00);

		/* PCIe lanes configuration */
		igb_write_8bit_ctrl_reg(hw, E1000_GIOCTL, 0x00, 0xEC);
		igb_write_8bit_ctrl_reg(hw, E1000_GIOCTL, 0x61, 0xDF);
		igb_write_8bit_ctrl_reg(hw, E1000_GIOCTL, 0x34, 0x05);
		igb_write_8bit_ctrl_reg(hw, E1000_GIOCTL, 0x2F, 0x81);

		/* PCIe PLL Configuration */
		igb_write_8bit_ctrl_reg(hw, E1000_SCCTL, 0x02, 0x47);
		igb_write_8bit_ctrl_reg(hw, E1000_SCCTL, 0x14, 0x00);
		igb_write_8bit_ctrl_reg(hw, E1000_SCCTL, 0x10, 0x00);
	}

	return 0;
}

/**
 *  igb_read_mac_addr_82575 - Read device MAC address
 *  @hw: pointer to the HW structure
 **/
static s32 igb_read_mac_addr_82575(struct e1000_hw *hw)
{
	s32 ret_val = 0;

	/* If there's an alternate MAC address place it in RAR0
	 * so that it will override the Si installed default perm
	 * address.
	 */
	ret_val = igb_check_alt_mac_addr(hw);
	if (ret_val)
		goto out;

	ret_val = igb_read_mac_addr(hw);

out:
	return ret_val;
}

/**
 * igb_power_down_phy_copper_82575 - Remove link during PHY power down
 * @hw: pointer to the HW structure
 *
 * In the case of a PHY power down to save power, or to turn off link during a
 * driver unload, or wake on lan is not enabled, remove the link.
 **/
void igb_power_down_phy_copper_82575(struct e1000_hw *hw)
{
	/* If the management interface is not enabled, then power down */
	if (!(igb_enable_mng_pass_thru(hw) || igb_check_reset_block(hw)))
		igb_power_down_phy_copper(hw);
}

/**
 *  igb_clear_hw_cntrs_82575 - Clear device specific hardware counters
 *  @hw: pointer to the HW structure
 *
 *  Clears the hardware counters by reading the counter registers.
 **/
static void igb_clear_hw_cntrs_82575(struct e1000_hw *hw)
{
	igb_clear_hw_cntrs_base(hw);

	rd32(E1000_PRC64);
	rd32(E1000_PRC127);
	rd32(E1000_PRC255);
	rd32(E1000_PRC511);
	rd32(E1000_PRC1023);
	rd32(E1000_PRC1522);
	rd32(E1000_PTC64);
	rd32(E1000_PTC127);
	rd32(E1000_PTC255);
	rd32(E1000_PTC511);
	rd32(E1000_PTC1023);
	rd32(E1000_PTC1522);

	rd32(E1000_ALGNERRC);
	rd32(E1000_RXERRC);
	rd32(E1000_TNCRS);
	rd32(E1000_CEXTERR);
	rd32(E1000_TSCTC);
	rd32(E1000_TSCTFC);

	rd32(E1000_MGTPRC);
	rd32(E1000_MGTPDC);
	rd32(E1000_MGTPTC);

	rd32(E1000_IAC);
	rd32(E1000_ICRXOC);

	rd32(E1000_ICRXPTC);
	rd32(E1000_ICRXATC);
	rd32(E1000_ICTXPTC);
	rd32(E1000_ICTXATC);
	rd32(E1000_ICTXQEC);
	rd32(E1000_ICTXQMTC);
	rd32(E1000_ICRXDMTC);

	rd32(E1000_CBTMPC);
	rd32(E1000_HTDPMC);
	rd32(E1000_CBRMPC);
	rd32(E1000_RPTHC);
	rd32(E1000_HGPTC);
	rd32(E1000_HTCBDPC);
	rd32(E1000_HGORCL);
	rd32(E1000_HGORCH);
	rd32(E1000_HGOTCL);
	rd32(E1000_HGOTCH);
	rd32(E1000_LENERRS);

	/* This register should not be read in copper configurations */
	if (hw->phy.media_type == e1000_media_type_internal_serdes ||
	    igb_sgmii_active_82575(hw))
		rd32(E1000_SCVPC);
}

/**
 *  igb_rx_fifo_flush_82575 - Clean rx fifo after RX enable
 *  @hw: pointer to the HW structure
 *
 *  After rx enable if managability is enabled then there is likely some
 *  bad data at the start of the fifo and possibly in the DMA fifo.  This
 *  function clears the fifos and flushes any packets that came in as rx was
 *  being enabled.
 **/
void igb_rx_fifo_flush_82575(struct e1000_hw *hw)
{
	u32 rctl, rlpml, rxdctl[4], rfctl, temp_rctl, rx_enabled;
	int i, ms_wait;

	if (hw->mac.type != e1000_82575 ||
	    !(rd32(E1000_MANC) & E1000_MANC_RCV_TCO_EN))
		return;

	/* Disable all RX queues */
	for (i = 0; i < 4; i++) {
		rxdctl[i] = rd32(E1000_RXDCTL(i));
		wr32(E1000_RXDCTL(i),
		     rxdctl[i] & ~E1000_RXDCTL_QUEUE_ENABLE);
	}
	/* Poll all queues to verify they have shut down */
	for (ms_wait = 0; ms_wait < 10; ms_wait++) {
		msleep(1);
		rx_enabled = 0;
		for (i = 0; i < 4; i++)
			rx_enabled |= rd32(E1000_RXDCTL(i));
		if (!(rx_enabled & E1000_RXDCTL_QUEUE_ENABLE))
			break;
	}

	if (ms_wait == 10)
		hw_dbg("Queue disable timed out after 10ms\n");

	/* Clear RLPML, RCTL.SBP, RFCTL.LEF, and set RCTL.LPE so that all
	 * incoming packets are rejected.  Set enable and wait 2ms so that
	 * any packet that was coming in as RCTL.EN was set is flushed
	 */
	rfctl = rd32(E1000_RFCTL);
	wr32(E1000_RFCTL, rfctl & ~E1000_RFCTL_LEF);

	rlpml = rd32(E1000_RLPML);
	wr32(E1000_RLPML, 0);

	rctl = rd32(E1000_RCTL);
	temp_rctl = rctl & ~(E1000_RCTL_EN | E1000_RCTL_SBP);
	temp_rctl |= E1000_RCTL_LPE;

	wr32(E1000_RCTL, temp_rctl);
	wr32(E1000_RCTL, temp_rctl | E1000_RCTL_EN);
	wrfl();
	msleep(2);

	/* Enable RX queues that were previously enabled and restore our
	 * previous state
	 */
	for (i = 0; i < 4; i++)
		wr32(E1000_RXDCTL(i), rxdctl[i]);
	wr32(E1000_RCTL, rctl);
	wrfl();

	wr32(E1000_RLPML, rlpml);
	wr32(E1000_RFCTL, rfctl);

	/* Flush receive errors generated by workaround */
	rd32(E1000_ROC);
	rd32(E1000_RNBC);
	rd32(E1000_MPC);
}

/**
 *  igb_set_pcie_completion_timeout - set pci-e completion timeout
 *  @hw: pointer to the HW structure
 *
 *  The defaults for 82575 and 82576 should be in the range of 50us to 50ms,
 *  however the hardware default for these parts is 500us to 1ms which is less
 *  than the 10ms recommended by the pci-e spec.  To address this we need to
 *  increase the value to either 10ms to 200ms for capability version 1 config,
 *  or 16ms to 55ms for version 2.
 **/
static s32 igb_set_pcie_completion_timeout(struct e1000_hw *hw)
{
	u32 gcr = rd32(E1000_GCR);
	s32 ret_val = 0;
	u16 pcie_devctl2;

	/* only take action if timeout value is defaulted to 0 */
	if (gcr & E1000_GCR_CMPL_TMOUT_MASK)
		goto out;

	/* if capabilities version is type 1 we can write the
	 * timeout of 10ms to 200ms through the GCR register
	 */
	if (!(gcr & E1000_GCR_CAP_VER2)) {
		gcr |= E1000_GCR_CMPL_TMOUT_10ms;
		goto out;
	}

	/* for version 2 capabilities we need to write the config space
	 * directly in order to set the completion timeout value for
	 * 16ms to 55ms
	 */
	ret_val = igb_read_pcie_cap_reg(hw, PCIE_DEVICE_CONTROL2,
	                                &pcie_devctl2);
	if (ret_val)
		goto out;

	pcie_devctl2 |= PCIE_DEVICE_CONTROL2_16ms;

	ret_val = igb_write_pcie_cap_reg(hw, PCIE_DEVICE_CONTROL2,
	                                 &pcie_devctl2);
out:
	/* disable completion timeout resend */
	gcr &= ~E1000_GCR_CMPL_TMOUT_RESEND;

	wr32(E1000_GCR, gcr);
	return ret_val;
}

/**
 *  igb_vmdq_set_anti_spoofing_pf - enable or disable anti-spoofing
 *  @hw: pointer to the hardware struct
 *  @enable: state to enter, either enabled or disabled
 *  @pf: Physical Function pool - do not set anti-spoofing for the PF
 *
 *  enables/disables L2 switch anti-spoofing functionality.
 **/
void igb_vmdq_set_anti_spoofing_pf(struct e1000_hw *hw, bool enable, int pf)
{
	u32 reg_val, reg_offset;

	switch (hw->mac.type) {
	case e1000_82576:
		reg_offset = E1000_DTXSWC;
		break;
	case e1000_i350:
	case e1000_i354:
		reg_offset = E1000_TXSWC;
		break;
	default:
		return;
	}

	reg_val = rd32(reg_offset);
	if (enable) {
		reg_val |= (E1000_DTXSWC_MAC_SPOOF_MASK |
			     E1000_DTXSWC_VLAN_SPOOF_MASK);
		/* The PF can spoof - it has to in order to
		 * support emulation mode NICs
		 */
		reg_val ^= (1 << pf | 1 << (pf + MAX_NUM_VFS));
	} else {
		reg_val &= ~(E1000_DTXSWC_MAC_SPOOF_MASK |
			     E1000_DTXSWC_VLAN_SPOOF_MASK);
	}
	wr32(reg_offset, reg_val);
}

/**
 *  igb_vmdq_set_loopback_pf - enable or disable vmdq loopback
 *  @hw: pointer to the hardware struct
 *  @enable: state to enter, either enabled or disabled
 *
 *  enables/disables L2 switch loopback functionality.
 **/
void igb_vmdq_set_loopback_pf(struct e1000_hw *hw, bool enable)
{
	u32 dtxswc;

	switch (hw->mac.type) {
	case e1000_82576:
		dtxswc = rd32(E1000_DTXSWC);
		if (enable)
			dtxswc |= E1000_DTXSWC_VMDQ_LOOPBACK_EN;
		else
			dtxswc &= ~E1000_DTXSWC_VMDQ_LOOPBACK_EN;
		wr32(E1000_DTXSWC, dtxswc);
		break;
	case e1000_i354:
	case e1000_i350:
		dtxswc = rd32(E1000_TXSWC);
		if (enable)
			dtxswc |= E1000_DTXSWC_VMDQ_LOOPBACK_EN;
		else
			dtxswc &= ~E1000_DTXSWC_VMDQ_LOOPBACK_EN;
		wr32(E1000_TXSWC, dtxswc);
		break;
	default:
		/* Currently no other hardware supports loopback */
		break;
	}

}

/**
 *  igb_vmdq_set_replication_pf - enable or disable vmdq replication
 *  @hw: pointer to the hardware struct
 *  @enable: state to enter, either enabled or disabled
 *
 *  enables/disables replication of packets across multiple pools.
 **/
void igb_vmdq_set_replication_pf(struct e1000_hw *hw, bool enable)
{
	u32 vt_ctl = rd32(E1000_VT_CTL);

	if (enable)
		vt_ctl |= E1000_VT_CTL_VM_REPL_EN;
	else
		vt_ctl &= ~E1000_VT_CTL_VM_REPL_EN;

	wr32(E1000_VT_CTL, vt_ctl);
}

/**
 *  igb_read_phy_reg_82580 - Read 82580 MDI control register
 *  @hw: pointer to the HW structure
 *  @offset: register offset to be read
 *  @data: pointer to the read data
 *
 *  Reads the MDI control register in the PHY at offset and stores the
 *  information read to data.
 **/
static s32 igb_read_phy_reg_82580(struct e1000_hw *hw, u32 offset, u16 *data)
{
	s32 ret_val;

	ret_val = hw->phy.ops.acquire(hw);
	if (ret_val)
		goto out;

	ret_val = igb_read_phy_reg_mdic(hw, offset, data);

	hw->phy.ops.release(hw);

out:
	return ret_val;
}

/**
 *  igb_write_phy_reg_82580 - Write 82580 MDI control register
 *  @hw: pointer to the HW structure
 *  @offset: register offset to write to
 *  @data: data to write to register at offset
 *
 *  Writes data to MDI control register in the PHY at offset.
 **/
static s32 igb_write_phy_reg_82580(struct e1000_hw *hw, u32 offset, u16 data)
{
	s32 ret_val;


	ret_val = hw->phy.ops.acquire(hw);
	if (ret_val)
		goto out;

	ret_val = igb_write_phy_reg_mdic(hw, offset, data);

	hw->phy.ops.release(hw);

out:
	return ret_val;
}

/**
 *  igb_reset_mdicnfg_82580 - Reset MDICNFG destination and com_mdio bits
 *  @hw: pointer to the HW structure
 *
 *  This resets the the MDICNFG.Destination and MDICNFG.Com_MDIO bits based on
 *  the values found in the EEPROM.  This addresses an issue in which these
 *  bits are not restored from EEPROM after reset.
 **/
static s32 igb_reset_mdicnfg_82580(struct e1000_hw *hw)
{
	s32 ret_val = 0;
	u32 mdicnfg;
	u16 nvm_data = 0;

	if (hw->mac.type != e1000_82580)
		goto out;
	if (!igb_sgmii_active_82575(hw))
		goto out;

	ret_val = hw->nvm.ops.read(hw, NVM_INIT_CONTROL3_PORT_A +
				   NVM_82580_LAN_FUNC_OFFSET(hw->bus.func), 1,
				   &nvm_data);
	if (ret_val) {
		hw_dbg("NVM Read Error\n");
		goto out;
	}

	mdicnfg = rd32(E1000_MDICNFG);
	if (nvm_data & NVM_WORD24_EXT_MDIO)
		mdicnfg |= E1000_MDICNFG_EXT_MDIO;
	if (nvm_data & NVM_WORD24_COM_MDIO)
		mdicnfg |= E1000_MDICNFG_COM_MDIO;
	wr32(E1000_MDICNFG, mdicnfg);
out:
	return ret_val;
}

/**
 *  igb_reset_hw_82580 - Reset hardware
 *  @hw: pointer to the HW structure
 *
 *  This resets function or entire device (all ports, etc.)
 *  to a known state.
 **/
static s32 igb_reset_hw_82580(struct e1000_hw *hw)
{
	s32 ret_val = 0;
	/* BH SW mailbox bit in SW_FW_SYNC */
	u16 swmbsw_mask = E1000_SW_SYNCH_MB;
	u32 ctrl;
	bool global_device_reset = hw->dev_spec._82575.global_device_reset;

	hw->dev_spec._82575.global_device_reset = false;

	/* due to hw errata, global device reset doesn't always
	 * work on 82580
	 */
	if (hw->mac.type == e1000_82580)
		global_device_reset = false;

	/* Get current control state. */
	ctrl = rd32(E1000_CTRL);

	/* Prevent the PCI-E bus from sticking if there is no TLP connection
	 * on the last TLP read/write transaction when MAC is reset.
	 */
	ret_val = igb_disable_pcie_master(hw);
	if (ret_val)
		hw_dbg("PCI-E Master disable polling has failed.\n");

	hw_dbg("Masking off all interrupts\n");
	wr32(E1000_IMC, 0xffffffff);
	wr32(E1000_RCTL, 0);
	wr32(E1000_TCTL, E1000_TCTL_PSP);
	wrfl();

	msleep(10);

	/* Determine whether or not a global dev reset is requested */
	if (global_device_reset &&
		hw->mac.ops.acquire_swfw_sync(hw, swmbsw_mask))
			global_device_reset = false;

	if (global_device_reset &&
		!(rd32(E1000_STATUS) & E1000_STAT_DEV_RST_SET))
		ctrl |= E1000_CTRL_DEV_RST;
	else
		ctrl |= E1000_CTRL_RST;

	wr32(E1000_CTRL, ctrl);
	wrfl();

	/* Add delay to insure DEV_RST has time to complete */
	if (global_device_reset)
		msleep(5);

	ret_val = igb_get_auto_rd_done(hw);
	if (ret_val) {
		/* When auto config read does not complete, do not
		 * return with an error. This can happen in situations
		 * where there is no eeprom and prevents getting link.
		 */
		hw_dbg("Auto Read Done did not complete\n");
	}

	/* clear global device reset status bit */
	wr32(E1000_STATUS, E1000_STAT_DEV_RST_SET);

	/* Clear any pending interrupt events. */
	wr32(E1000_IMC, 0xffffffff);
	rd32(E1000_ICR);

	ret_val = igb_reset_mdicnfg_82580(hw);
	if (ret_val)
		hw_dbg("Could not reset MDICNFG based on EEPROM\n");

	/* Install any alternate MAC address into RAR0 */
	ret_val = igb_check_alt_mac_addr(hw);

	/* Release semaphore */
	if (global_device_reset)
		hw->mac.ops.release_swfw_sync(hw, swmbsw_mask);

	return ret_val;
}

/**
 *  igb_rxpbs_adjust_82580 - adjust RXPBS value to reflect actual RX PBA size
 *  @data: data received by reading RXPBS register
 *
 *  The 82580 uses a table based approach for packet buffer allocation sizes.
 *  This function converts the retrieved value into the correct table value
 *     0x0 0x1 0x2 0x3 0x4 0x5 0x6 0x7
 *  0x0 36  72 144   1   2   4   8  16
 *  0x8 35  70 140 rsv rsv rsv rsv rsv
 */
u16 igb_rxpbs_adjust_82580(u32 data)
{
	u16 ret_val = 0;

	if (data < E1000_82580_RXPBS_TABLE_SIZE)
		ret_val = e1000_82580_rxpbs_table[data];

	return ret_val;
}

/**
 *  igb_validate_nvm_checksum_with_offset - Validate EEPROM
 *  checksum
 *  @hw: pointer to the HW structure
 *  @offset: offset in words of the checksum protected region
 *
 *  Calculates the EEPROM checksum by reading/adding each word of the EEPROM
 *  and then verifies that the sum of the EEPROM is equal to 0xBABA.
 **/
static s32 igb_validate_nvm_checksum_with_offset(struct e1000_hw *hw,
						 u16 offset)
{
	s32 ret_val = 0;
	u16 checksum = 0;
	u16 i, nvm_data;

	for (i = offset; i < ((NVM_CHECKSUM_REG + offset) + 1); i++) {
		ret_val = hw->nvm.ops.read(hw, i, 1, &nvm_data);
		if (ret_val) {
			hw_dbg("NVM Read Error\n");
			goto out;
		}
		checksum += nvm_data;
	}

	if (checksum != (u16) NVM_SUM) {
		hw_dbg("NVM Checksum Invalid\n");
		ret_val = -E1000_ERR_NVM;
		goto out;
	}

out:
	return ret_val;
}

/**
 *  igb_update_nvm_checksum_with_offset - Update EEPROM
 *  checksum
 *  @hw: pointer to the HW structure
 *  @offset: offset in words of the checksum protected region
 *
 *  Updates the EEPROM checksum by reading/adding each word of the EEPROM
 *  up to the checksum.  Then calculates the EEPROM checksum and writes the
 *  value to the EEPROM.
 **/
static s32 igb_update_nvm_checksum_with_offset(struct e1000_hw *hw, u16 offset)
{
	s32 ret_val;
	u16 checksum = 0;
	u16 i, nvm_data;

	for (i = offset; i < (NVM_CHECKSUM_REG + offset); i++) {
		ret_val = hw->nvm.ops.read(hw, i, 1, &nvm_data);
		if (ret_val) {
			hw_dbg("NVM Read Error while updating checksum.\n");
			goto out;
		}
		checksum += nvm_data;
	}
	checksum = (u16) NVM_SUM - checksum;
	ret_val = hw->nvm.ops.write(hw, (NVM_CHECKSUM_REG + offset), 1,
				&checksum);
	if (ret_val)
		hw_dbg("NVM Write Error while updating checksum.\n");

out:
	return ret_val;
}

/**
 *  igb_validate_nvm_checksum_82580 - Validate EEPROM checksum
 *  @hw: pointer to the HW structure
 *
 *  Calculates the EEPROM section checksum by reading/adding each word of
 *  the EEPROM and then verifies that the sum of the EEPROM is
 *  equal to 0xBABA.
 **/
static s32 igb_validate_nvm_checksum_82580(struct e1000_hw *hw)
{
	s32 ret_val = 0;
	u16 eeprom_regions_count = 1;
	u16 j, nvm_data;
	u16 nvm_offset;

	ret_val = hw->nvm.ops.read(hw, NVM_COMPATIBILITY_REG_3, 1, &nvm_data);
	if (ret_val) {
		hw_dbg("NVM Read Error\n");
		goto out;
	}

	if (nvm_data & NVM_COMPATIBILITY_BIT_MASK) {
		/* if checksums compatibility bit is set validate checksums
		 * for all 4 ports.
		 */
		eeprom_regions_count = 4;
	}

	for (j = 0; j < eeprom_regions_count; j++) {
		nvm_offset = NVM_82580_LAN_FUNC_OFFSET(j);
		ret_val = igb_validate_nvm_checksum_with_offset(hw,
								nvm_offset);
		if (ret_val != 0)
			goto out;
	}

out:
	return ret_val;
}

/**
 *  igb_update_nvm_checksum_82580 - Update EEPROM checksum
 *  @hw: pointer to the HW structure
 *
 *  Updates the EEPROM section checksums for all 4 ports by reading/adding
 *  each word of the EEPROM up to the checksum.  Then calculates the EEPROM
 *  checksum and writes the value to the EEPROM.
 **/
static s32 igb_update_nvm_checksum_82580(struct e1000_hw *hw)
{
	s32 ret_val;
	u16 j, nvm_data;
	u16 nvm_offset;

	ret_val = hw->nvm.ops.read(hw, NVM_COMPATIBILITY_REG_3, 1, &nvm_data);
	if (ret_val) {
		hw_dbg("NVM Read Error while updating checksum"
			" compatibility bit.\n");
		goto out;
	}

	if ((nvm_data & NVM_COMPATIBILITY_BIT_MASK) == 0) {
		/* set compatibility bit to validate checksums appropriately */
		nvm_data = nvm_data | NVM_COMPATIBILITY_BIT_MASK;
		ret_val = hw->nvm.ops.write(hw, NVM_COMPATIBILITY_REG_3, 1,
					&nvm_data);
		if (ret_val) {
			hw_dbg("NVM Write Error while updating checksum"
				" compatibility bit.\n");
			goto out;
		}
	}

	for (j = 0; j < 4; j++) {
		nvm_offset = NVM_82580_LAN_FUNC_OFFSET(j);
		ret_val = igb_update_nvm_checksum_with_offset(hw, nvm_offset);
		if (ret_val)
			goto out;
	}

out:
	return ret_val;
}

/**
 *  igb_validate_nvm_checksum_i350 - Validate EEPROM checksum
 *  @hw: pointer to the HW structure
 *
 *  Calculates the EEPROM section checksum by reading/adding each word of
 *  the EEPROM and then verifies that the sum of the EEPROM is
 *  equal to 0xBABA.
 **/
static s32 igb_validate_nvm_checksum_i350(struct e1000_hw *hw)
{
	s32 ret_val = 0;
	u16 j;
	u16 nvm_offset;

	for (j = 0; j < 4; j++) {
		nvm_offset = NVM_82580_LAN_FUNC_OFFSET(j);
		ret_val = igb_validate_nvm_checksum_with_offset(hw,
								nvm_offset);
		if (ret_val != 0)
			goto out;
	}

out:
	return ret_val;
}

/**
 *  igb_update_nvm_checksum_i350 - Update EEPROM checksum
 *  @hw: pointer to the HW structure
 *
 *  Updates the EEPROM section checksums for all 4 ports by reading/adding
 *  each word of the EEPROM up to the checksum.  Then calculates the EEPROM
 *  checksum and writes the value to the EEPROM.
 **/
static s32 igb_update_nvm_checksum_i350(struct e1000_hw *hw)
{
	s32 ret_val = 0;
	u16 j;
	u16 nvm_offset;

	for (j = 0; j < 4; j++) {
		nvm_offset = NVM_82580_LAN_FUNC_OFFSET(j);
		ret_val = igb_update_nvm_checksum_with_offset(hw, nvm_offset);
		if (ret_val != 0)
			goto out;
	}

out:
	return ret_val;
}

/**
 *  __igb_access_emi_reg - Read/write EMI register
 *  @hw: pointer to the HW structure
 *  @addr: EMI address to program
 *  @data: pointer to value to read/write from/to the EMI address
 *  @read: boolean flag to indicate read or write
 **/
static s32 __igb_access_emi_reg(struct e1000_hw *hw, u16 address,
				  u16 *data, bool read)
{
	s32 ret_val = E1000_SUCCESS;

	ret_val = hw->phy.ops.write_reg(hw, E1000_EMIADD, address);
	if (ret_val)
		return ret_val;

	if (read)
		ret_val = hw->phy.ops.read_reg(hw, E1000_EMIDATA, data);
	else
		ret_val = hw->phy.ops.write_reg(hw, E1000_EMIDATA, *data);

	return ret_val;
}

/**
 *  igb_read_emi_reg - Read Extended Management Interface register
 *  @hw: pointer to the HW structure
 *  @addr: EMI address to program
 *  @data: value to be read from the EMI address
 **/
s32 igb_read_emi_reg(struct e1000_hw *hw, u16 addr, u16 *data)
{
	return __igb_access_emi_reg(hw, addr, data, true);
}

/**
 *  igb_set_eee_i350 - Enable/disable EEE support
 *  @hw: pointer to the HW structure
 *
 *  Enable/disable EEE based on setting in dev_spec structure.
 *
 **/
s32 igb_set_eee_i350(struct e1000_hw *hw)
{
	s32 ret_val = 0;
	u32 ipcnfg, eeer;

	if ((hw->mac.type < e1000_i350) ||
	    (hw->phy.media_type != e1000_media_type_copper))
		goto out;
	ipcnfg = rd32(E1000_IPCNFG);
	eeer = rd32(E1000_EEER);

	/* enable or disable per user setting */
	if (!(hw->dev_spec._82575.eee_disable)) {
		u32 eee_su = rd32(E1000_EEE_SU);

		ipcnfg |= (E1000_IPCNFG_EEE_1G_AN | E1000_IPCNFG_EEE_100M_AN);
		eeer |= (E1000_EEER_TX_LPI_EN | E1000_EEER_RX_LPI_EN |
			E1000_EEER_LPI_FC);

		/* This bit should not be set in normal operation. */
		if (eee_su & E1000_EEE_SU_LPI_CLK_STP)
			hw_dbg("LPI Clock Stop Bit should not be set!\n");

	} else {
		ipcnfg &= ~(E1000_IPCNFG_EEE_1G_AN |
			E1000_IPCNFG_EEE_100M_AN);
		eeer &= ~(E1000_EEER_TX_LPI_EN |
			E1000_EEER_RX_LPI_EN |
			E1000_EEER_LPI_FC);
	}
	wr32(E1000_IPCNFG, ipcnfg);
	wr32(E1000_EEER, eeer);
	rd32(E1000_IPCNFG);
	rd32(E1000_EEER);
out:

	return ret_val;
}

/**
 *  igb_set_eee_i354 - Enable/disable EEE support
 *  @hw: pointer to the HW structure
 *
 *  Enable/disable EEE legacy mode based on setting in dev_spec structure.
 *
 **/
s32 igb_set_eee_i354(struct e1000_hw *hw)
{
	struct e1000_phy_info *phy = &hw->phy;
	s32 ret_val = 0;
	u16 phy_data;

	if ((hw->phy.media_type != e1000_media_type_copper) ||
	    (phy->id != M88E1543_E_PHY_ID))
		goto out;

	if (!hw->dev_spec._82575.eee_disable) {
		/* Switch to PHY page 18. */
		ret_val = phy->ops.write_reg(hw, E1000_M88E1543_PAGE_ADDR, 18);
		if (ret_val)
			goto out;

		ret_val = phy->ops.read_reg(hw, E1000_M88E1543_EEE_CTRL_1,
					    &phy_data);
		if (ret_val)
			goto out;

		phy_data |= E1000_M88E1543_EEE_CTRL_1_MS;
		ret_val = phy->ops.write_reg(hw, E1000_M88E1543_EEE_CTRL_1,
					     phy_data);
		if (ret_val)
			goto out;

		/* Return the PHY to page 0. */
		ret_val = phy->ops.write_reg(hw, E1000_M88E1543_PAGE_ADDR, 0);
		if (ret_val)
			goto out;

		/* Turn on EEE advertisement. */
		ret_val = igb_read_xmdio_reg(hw, E1000_EEE_ADV_ADDR_I354,
					     E1000_EEE_ADV_DEV_I354,
					     &phy_data);
		if (ret_val)
			goto out;

		phy_data |= E1000_EEE_ADV_100_SUPPORTED |
			    E1000_EEE_ADV_1000_SUPPORTED;
		ret_val = igb_write_xmdio_reg(hw, E1000_EEE_ADV_ADDR_I354,
						E1000_EEE_ADV_DEV_I354,
						phy_data);
	} else {
		/* Turn off EEE advertisement. */
		ret_val = igb_read_xmdio_reg(hw, E1000_EEE_ADV_ADDR_I354,
					     E1000_EEE_ADV_DEV_I354,
					     &phy_data);
		if (ret_val)
			goto out;

		phy_data &= ~(E1000_EEE_ADV_100_SUPPORTED |
			      E1000_EEE_ADV_1000_SUPPORTED);
		ret_val = igb_write_xmdio_reg(hw, E1000_EEE_ADV_ADDR_I354,
					      E1000_EEE_ADV_DEV_I354,
					      phy_data);
	}

out:
	return ret_val;
}

/**
 *  igb_get_eee_status_i354 - Get EEE status
 *  @hw: pointer to the HW structure
 *  @status: EEE status
 *
 *  Get EEE status by guessing based on whether Tx or Rx LPI indications have
 *  been received.
 **/
s32 igb_get_eee_status_i354(struct e1000_hw *hw, bool *status)
{
	struct e1000_phy_info *phy = &hw->phy;
	s32 ret_val = 0;
	u16 phy_data;

	/* Check if EEE is supported on this device. */
	if ((hw->phy.media_type != e1000_media_type_copper) ||
	    (phy->id != M88E1543_E_PHY_ID))
		goto out;

	ret_val = igb_read_xmdio_reg(hw, E1000_PCS_STATUS_ADDR_I354,
				     E1000_PCS_STATUS_DEV_I354,
				     &phy_data);
	if (ret_val)
		goto out;

	*status = phy_data & (E1000_PCS_STATUS_TX_LPI_RCVD |
			      E1000_PCS_STATUS_RX_LPI_RCVD) ? true : false;

out:
	return ret_val;
}

static const u8 e1000_emc_temp_data[4] = {
	E1000_EMC_INTERNAL_DATA,
	E1000_EMC_DIODE1_DATA,
	E1000_EMC_DIODE2_DATA,
	E1000_EMC_DIODE3_DATA
};
static const u8 e1000_emc_therm_limit[4] = {
	E1000_EMC_INTERNAL_THERM_LIMIT,
	E1000_EMC_DIODE1_THERM_LIMIT,
	E1000_EMC_DIODE2_THERM_LIMIT,
	E1000_EMC_DIODE3_THERM_LIMIT
};

/**
 *  igb_get_thermal_sensor_data_generic - Gathers thermal sensor data
 *  @hw: pointer to hardware structure
 *
 *  Updates the temperatures in mac.thermal_sensor_data
 **/
s32 igb_get_thermal_sensor_data_generic(struct e1000_hw *hw)
{
	s32 status = E1000_SUCCESS;
	u16 ets_offset;
	u16 ets_cfg;
	u16 ets_sensor;
	u8  num_sensors;
	u8  sensor_index;
	u8  sensor_location;
	u8  i;
	struct e1000_thermal_sensor_data *data = &hw->mac.thermal_sensor_data;

	if ((hw->mac.type != e1000_i350) || (hw->bus.func != 0))
		return E1000_NOT_IMPLEMENTED;

	data->sensor[0].temp = (rd32(E1000_THMJT) & 0xFF);

	/* Return the internal sensor only if ETS is unsupported */
	hw->nvm.ops.read(hw, NVM_ETS_CFG, 1, &ets_offset);
	if ((ets_offset == 0x0000) || (ets_offset == 0xFFFF))
		return status;

	hw->nvm.ops.read(hw, ets_offset, 1, &ets_cfg);
	if (((ets_cfg & NVM_ETS_TYPE_MASK) >> NVM_ETS_TYPE_SHIFT)
	    != NVM_ETS_TYPE_EMC)
		return E1000_NOT_IMPLEMENTED;

	num_sensors = (ets_cfg & NVM_ETS_NUM_SENSORS_MASK);
	if (num_sensors > E1000_MAX_SENSORS)
		num_sensors = E1000_MAX_SENSORS;

	for (i = 1; i < num_sensors; i++) {
		hw->nvm.ops.read(hw, (ets_offset + i), 1, &ets_sensor);
		sensor_index = ((ets_sensor & NVM_ETS_DATA_INDEX_MASK) >>
				NVM_ETS_DATA_INDEX_SHIFT);
		sensor_location = ((ets_sensor & NVM_ETS_DATA_LOC_MASK) >>
				   NVM_ETS_DATA_LOC_SHIFT);

		if (sensor_location != 0)
			hw->phy.ops.read_i2c_byte(hw,
					e1000_emc_temp_data[sensor_index],
					E1000_I2C_THERMAL_SENSOR_ADDR,
					&data->sensor[i].temp);
	}
	return status;
}

/**
 *  igb_init_thermal_sensor_thresh_generic - Sets thermal sensor thresholds
 *  @hw: pointer to hardware structure
 *
 *  Sets the thermal sensor thresholds according to the NVM map
 *  and save off the threshold and location values into mac.thermal_sensor_data
 **/
s32 igb_init_thermal_sensor_thresh_generic(struct e1000_hw *hw)
{
	s32 status = E1000_SUCCESS;
	u16 ets_offset;
	u16 ets_cfg;
	u16 ets_sensor;
	u8  low_thresh_delta;
	u8  num_sensors;
	u8  sensor_index;
	u8  sensor_location;
	u8  therm_limit;
	u8  i;
	struct e1000_thermal_sensor_data *data = &hw->mac.thermal_sensor_data;

	if ((hw->mac.type != e1000_i350) || (hw->bus.func != 0))
		return E1000_NOT_IMPLEMENTED;

	memset(data, 0, sizeof(struct e1000_thermal_sensor_data));

	data->sensor[0].location = 0x1;
	data->sensor[0].caution_thresh =
		(rd32(E1000_THHIGHTC) & 0xFF);
	data->sensor[0].max_op_thresh =
		(rd32(E1000_THLOWTC) & 0xFF);

	/* Return the internal sensor only if ETS is unsupported */
	hw->nvm.ops.read(hw, NVM_ETS_CFG, 1, &ets_offset);
	if ((ets_offset == 0x0000) || (ets_offset == 0xFFFF))
		return status;

	hw->nvm.ops.read(hw, ets_offset, 1, &ets_cfg);
	if (((ets_cfg & NVM_ETS_TYPE_MASK) >> NVM_ETS_TYPE_SHIFT)
	    != NVM_ETS_TYPE_EMC)
		return E1000_NOT_IMPLEMENTED;

	low_thresh_delta = ((ets_cfg & NVM_ETS_LTHRES_DELTA_MASK) >>
			    NVM_ETS_LTHRES_DELTA_SHIFT);
	num_sensors = (ets_cfg & NVM_ETS_NUM_SENSORS_MASK);

	for (i = 1; i <= num_sensors; i++) {
		hw->nvm.ops.read(hw, (ets_offset + i), 1, &ets_sensor);
		sensor_index = ((ets_sensor & NVM_ETS_DATA_INDEX_MASK) >>
				NVM_ETS_DATA_INDEX_SHIFT);
		sensor_location = ((ets_sensor & NVM_ETS_DATA_LOC_MASK) >>
				   NVM_ETS_DATA_LOC_SHIFT);
		therm_limit = ets_sensor & NVM_ETS_DATA_HTHRESH_MASK;

		hw->phy.ops.write_i2c_byte(hw,
			e1000_emc_therm_limit[sensor_index],
			E1000_I2C_THERMAL_SENSOR_ADDR,
			therm_limit);

		if ((i < E1000_MAX_SENSORS) && (sensor_location != 0)) {
			data->sensor[i].location = sensor_location;
			data->sensor[i].caution_thresh = therm_limit;
			data->sensor[i].max_op_thresh = therm_limit -
							low_thresh_delta;
		}
	}
	return status;
}

static struct e1000_mac_operations e1000_mac_ops_82575 = {
	.init_hw              = igb_init_hw_82575,
	.check_for_link       = igb_check_for_link_82575,
	.rar_set              = igb_rar_set,
	.read_mac_addr        = igb_read_mac_addr_82575,
	.get_speed_and_duplex = igb_get_link_up_info_82575,
#ifdef CONFIG_IGB_HWMON
	.get_thermal_sensor_data = igb_get_thermal_sensor_data_generic,
	.init_thermal_sensor_thresh = igb_init_thermal_sensor_thresh_generic,
#endif
};

static struct e1000_phy_operations e1000_phy_ops_82575 = {
	.acquire              = igb_acquire_phy_82575,
	.get_cfg_done         = igb_get_cfg_done_82575,
	.release              = igb_release_phy_82575,
	.write_i2c_byte       = igb_write_i2c_byte,
	.read_i2c_byte        = igb_read_i2c_byte,
};

static struct e1000_nvm_operations e1000_nvm_ops_82575 = {
	.acquire              = igb_acquire_nvm_82575,
	.read                 = igb_read_nvm_eerd,
	.release              = igb_release_nvm_82575,
	.write                = igb_write_nvm_spi,
};

const struct e1000_info e1000_82575_info = {
	.get_invariants = igb_get_invariants_82575,
	.mac_ops = &e1000_mac_ops_82575,
	.phy_ops = &e1000_phy_ops_82575,
	.nvm_ops = &e1000_nvm_ops_82575,
};

