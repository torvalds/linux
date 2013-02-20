/*******************************************************************************

  Intel PRO/1000 Linux driver
  Copyright(c) 1999 - 2013 Intel Corporation.

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

/* 82571EB Gigabit Ethernet Controller
 * 82571EB Gigabit Ethernet Controller (Copper)
 * 82571EB Gigabit Ethernet Controller (Fiber)
 * 82571EB Dual Port Gigabit Mezzanine Adapter
 * 82571EB Quad Port Gigabit Mezzanine Adapter
 * 82571PT Gigabit PT Quad Port Server ExpressModule
 * 82572EI Gigabit Ethernet Controller (Copper)
 * 82572EI Gigabit Ethernet Controller (Fiber)
 * 82572EI Gigabit Ethernet Controller
 * 82573V Gigabit Ethernet Controller (Copper)
 * 82573E Gigabit Ethernet Controller (Copper)
 * 82573L Gigabit Ethernet Controller
 * 82574L Gigabit Network Connection
 * 82583V Gigabit Network Connection
 */

#include "e1000.h"

static s32 e1000_get_phy_id_82571(struct e1000_hw *hw);
static s32 e1000_setup_copper_link_82571(struct e1000_hw *hw);
static s32 e1000_setup_fiber_serdes_link_82571(struct e1000_hw *hw);
static s32 e1000_check_for_serdes_link_82571(struct e1000_hw *hw);
static s32 e1000_write_nvm_eewr_82571(struct e1000_hw *hw, u16 offset,
				      u16 words, u16 *data);
static s32 e1000_fix_nvm_checksum_82571(struct e1000_hw *hw);
static void e1000_initialize_hw_bits_82571(struct e1000_hw *hw);
static void e1000_clear_hw_cntrs_82571(struct e1000_hw *hw);
static bool e1000_check_mng_mode_82574(struct e1000_hw *hw);
static s32 e1000_led_on_82574(struct e1000_hw *hw);
static void e1000_put_hw_semaphore_82571(struct e1000_hw *hw);
static void e1000_power_down_phy_copper_82571(struct e1000_hw *hw);
static void e1000_put_hw_semaphore_82573(struct e1000_hw *hw);
static s32 e1000_get_hw_semaphore_82574(struct e1000_hw *hw);
static void e1000_put_hw_semaphore_82574(struct e1000_hw *hw);
static s32 e1000_set_d0_lplu_state_82574(struct e1000_hw *hw, bool active);
static s32 e1000_set_d3_lplu_state_82574(struct e1000_hw *hw, bool active);

/**
 *  e1000_init_phy_params_82571 - Init PHY func ptrs.
 *  @hw: pointer to the HW structure
 **/
static s32 e1000_init_phy_params_82571(struct e1000_hw *hw)
{
	struct e1000_phy_info *phy = &hw->phy;
	s32 ret_val;

	if (hw->phy.media_type != e1000_media_type_copper) {
		phy->type = e1000_phy_none;
		return 0;
	}

	phy->addr			 = 1;
	phy->autoneg_mask		 = AUTONEG_ADVERTISE_SPEED_DEFAULT;
	phy->reset_delay_us		 = 100;

	phy->ops.power_up		 = e1000_power_up_phy_copper;
	phy->ops.power_down		 = e1000_power_down_phy_copper_82571;

	switch (hw->mac.type) {
	case e1000_82571:
	case e1000_82572:
		phy->type		 = e1000_phy_igp_2;
		break;
	case e1000_82573:
		phy->type		 = e1000_phy_m88;
		break;
	case e1000_82574:
	case e1000_82583:
		phy->type		 = e1000_phy_bm;
		phy->ops.acquire = e1000_get_hw_semaphore_82574;
		phy->ops.release = e1000_put_hw_semaphore_82574;
		phy->ops.set_d0_lplu_state = e1000_set_d0_lplu_state_82574;
		phy->ops.set_d3_lplu_state = e1000_set_d3_lplu_state_82574;
		break;
	default:
		return -E1000_ERR_PHY;
		break;
	}

	/* This can only be done after all function pointers are setup. */
	ret_val = e1000_get_phy_id_82571(hw);
	if (ret_val) {
		e_dbg("Error getting PHY ID\n");
		return ret_val;
	}

	/* Verify phy id */
	switch (hw->mac.type) {
	case e1000_82571:
	case e1000_82572:
		if (phy->id != IGP01E1000_I_PHY_ID)
			ret_val = -E1000_ERR_PHY;
		break;
	case e1000_82573:
		if (phy->id != M88E1111_I_PHY_ID)
			ret_val = -E1000_ERR_PHY;
		break;
	case e1000_82574:
	case e1000_82583:
		if (phy->id != BME1000_E_PHY_ID_R2)
			ret_val = -E1000_ERR_PHY;
		break;
	default:
		ret_val = -E1000_ERR_PHY;
		break;
	}

	if (ret_val)
		e_dbg("PHY ID unknown: type = 0x%08x\n", phy->id);

	return ret_val;
}

/**
 *  e1000_init_nvm_params_82571 - Init NVM func ptrs.
 *  @hw: pointer to the HW structure
 **/
static s32 e1000_init_nvm_params_82571(struct e1000_hw *hw)
{
	struct e1000_nvm_info *nvm = &hw->nvm;
	u32 eecd = er32(EECD);
	u16 size;

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
		nvm->address_bits = eecd & E1000_EECD_ADDR_BITS ? 16 : 8;
		break;
	}

	switch (hw->mac.type) {
	case e1000_82573:
	case e1000_82574:
	case e1000_82583:
		if (((eecd >> 15) & 0x3) == 0x3) {
			nvm->type = e1000_nvm_flash_hw;
			nvm->word_size = 2048;
			/* Autonomous Flash update bit must be cleared due
			 * to Flash update issue.
			 */
			eecd &= ~E1000_EECD_AUPDEN;
			ew32(EECD, eecd);
			break;
		}
		/* Fall Through */
	default:
		nvm->type = e1000_nvm_eeprom_spi;
		size = (u16)((eecd & E1000_EECD_SIZE_EX_MASK) >>
			     E1000_EECD_SIZE_EX_SHIFT);
		/* Added to a constant, "size" becomes the left-shift value
		 * for setting word_size.
		 */
		size += NVM_WORD_SIZE_BASE_SHIFT;

		/* EEPROM access above 16k is unsupported */
		if (size > 14)
			size = 14;
		nvm->word_size	= 1 << size;
		break;
	}

	/* Function Pointers */
	switch (hw->mac.type) {
	case e1000_82574:
	case e1000_82583:
		nvm->ops.acquire = e1000_get_hw_semaphore_82574;
		nvm->ops.release = e1000_put_hw_semaphore_82574;
		break;
	default:
		break;
	}

	return 0;
}

/**
 *  e1000_init_mac_params_82571 - Init MAC func ptrs.
 *  @hw: pointer to the HW structure
 **/
static s32 e1000_init_mac_params_82571(struct e1000_hw *hw)
{
	struct e1000_mac_info *mac = &hw->mac;
	u32 swsm = 0;
	u32 swsm2 = 0;
	bool force_clear_smbi = false;

	/* Set media type and media-dependent function pointers */
	switch (hw->adapter->pdev->device) {
	case E1000_DEV_ID_82571EB_FIBER:
	case E1000_DEV_ID_82572EI_FIBER:
	case E1000_DEV_ID_82571EB_QUAD_FIBER:
		hw->phy.media_type = e1000_media_type_fiber;
		mac->ops.setup_physical_interface =
		    e1000_setup_fiber_serdes_link_82571;
		mac->ops.check_for_link = e1000e_check_for_fiber_link;
		mac->ops.get_link_up_info =
		    e1000e_get_speed_and_duplex_fiber_serdes;
		break;
	case E1000_DEV_ID_82571EB_SERDES:
	case E1000_DEV_ID_82571EB_SERDES_DUAL:
	case E1000_DEV_ID_82571EB_SERDES_QUAD:
	case E1000_DEV_ID_82572EI_SERDES:
		hw->phy.media_type = e1000_media_type_internal_serdes;
		mac->ops.setup_physical_interface =
		    e1000_setup_fiber_serdes_link_82571;
		mac->ops.check_for_link = e1000_check_for_serdes_link_82571;
		mac->ops.get_link_up_info =
		    e1000e_get_speed_and_duplex_fiber_serdes;
		break;
	default:
		hw->phy.media_type = e1000_media_type_copper;
		mac->ops.setup_physical_interface =
		    e1000_setup_copper_link_82571;
		mac->ops.check_for_link = e1000e_check_for_copper_link;
		mac->ops.get_link_up_info = e1000e_get_speed_and_duplex_copper;
		break;
	}

	/* Set mta register count */
	mac->mta_reg_count = 128;
	/* Set rar entry count */
	mac->rar_entry_count = E1000_RAR_ENTRIES;
	/* Adaptive IFS supported */
	mac->adaptive_ifs = true;

	/* MAC-specific function pointers */
	switch (hw->mac.type) {
	case e1000_82573:
		mac->ops.set_lan_id = e1000_set_lan_id_single_port;
		mac->ops.check_mng_mode = e1000e_check_mng_mode_generic;
		mac->ops.led_on = e1000e_led_on_generic;
		mac->ops.blink_led = e1000e_blink_led_generic;

		/* FWSM register */
		mac->has_fwsm = true;
		/* ARC supported; valid only if manageability features are
		 * enabled.
		 */
		mac->arc_subsystem_valid = !!(er32(FWSM) &
					      E1000_FWSM_MODE_MASK);
		break;
	case e1000_82574:
	case e1000_82583:
		mac->ops.set_lan_id = e1000_set_lan_id_single_port;
		mac->ops.check_mng_mode = e1000_check_mng_mode_82574;
		mac->ops.led_on = e1000_led_on_82574;
		break;
	default:
		mac->ops.check_mng_mode = e1000e_check_mng_mode_generic;
		mac->ops.led_on = e1000e_led_on_generic;
		mac->ops.blink_led = e1000e_blink_led_generic;

		/* FWSM register */
		mac->has_fwsm = true;
		break;
	}

	/* Ensure that the inter-port SWSM.SMBI lock bit is clear before
	 * first NVM or PHY access. This should be done for single-port
	 * devices, and for one port only on dual-port devices so that
	 * for those devices we can still use the SMBI lock to synchronize
	 * inter-port accesses to the PHY & NVM.
	 */
	switch (hw->mac.type) {
	case e1000_82571:
	case e1000_82572:
		swsm2 = er32(SWSM2);

		if (!(swsm2 & E1000_SWSM2_LOCK)) {
			/* Only do this for the first interface on this card */
			ew32(SWSM2, swsm2 | E1000_SWSM2_LOCK);
			force_clear_smbi = true;
		} else {
			force_clear_smbi = false;
		}
		break;
	default:
		force_clear_smbi = true;
		break;
	}

	if (force_clear_smbi) {
		/* Make sure SWSM.SMBI is clear */
		swsm = er32(SWSM);
		if (swsm & E1000_SWSM_SMBI) {
			/* This bit should not be set on a first interface, and
			 * indicates that the bootagent or EFI code has
			 * improperly left this bit enabled
			 */
			e_dbg("Please update your 82571 Bootagent\n");
		}
		ew32(SWSM, swsm & ~E1000_SWSM_SMBI);
	}

	/* Initialize device specific counter of SMBI acquisition timeouts. */
	hw->dev_spec.e82571.smb_counter = 0;

	return 0;
}

static s32 e1000_get_variants_82571(struct e1000_adapter *adapter)
{
	struct e1000_hw *hw = &adapter->hw;
	static int global_quad_port_a; /* global port a indication */
	struct pci_dev *pdev = adapter->pdev;
	int is_port_b = er32(STATUS) & E1000_STATUS_FUNC_1;
	s32 rc;

	rc = e1000_init_mac_params_82571(hw);
	if (rc)
		return rc;

	rc = e1000_init_nvm_params_82571(hw);
	if (rc)
		return rc;

	rc = e1000_init_phy_params_82571(hw);
	if (rc)
		return rc;

	/* tag quad port adapters first, it's used below */
	switch (pdev->device) {
	case E1000_DEV_ID_82571EB_QUAD_COPPER:
	case E1000_DEV_ID_82571EB_QUAD_FIBER:
	case E1000_DEV_ID_82571EB_QUAD_COPPER_LP:
	case E1000_DEV_ID_82571PT_QUAD_COPPER:
		adapter->flags |= FLAG_IS_QUAD_PORT;
		/* mark the first port */
		if (global_quad_port_a == 0)
			adapter->flags |= FLAG_IS_QUAD_PORT_A;
		/* Reset for multiple quad port adapters */
		global_quad_port_a++;
		if (global_quad_port_a == 4)
			global_quad_port_a = 0;
		break;
	default:
		break;
	}

	switch (adapter->hw.mac.type) {
	case e1000_82571:
		/* these dual ports don't have WoL on port B at all */
		if (((pdev->device == E1000_DEV_ID_82571EB_FIBER) ||
		     (pdev->device == E1000_DEV_ID_82571EB_SERDES) ||
		     (pdev->device == E1000_DEV_ID_82571EB_COPPER)) &&
		    (is_port_b))
			adapter->flags &= ~FLAG_HAS_WOL;
		/* quad ports only support WoL on port A */
		if (adapter->flags & FLAG_IS_QUAD_PORT &&
		    (!(adapter->flags & FLAG_IS_QUAD_PORT_A)))
			adapter->flags &= ~FLAG_HAS_WOL;
		/* Does not support WoL on any port */
		if (pdev->device == E1000_DEV_ID_82571EB_SERDES_QUAD)
			adapter->flags &= ~FLAG_HAS_WOL;
		break;
	case e1000_82573:
		if (pdev->device == E1000_DEV_ID_82573L) {
			adapter->flags |= FLAG_HAS_JUMBO_FRAMES;
			adapter->max_hw_frame_size = DEFAULT_JUMBO;
		}
		break;
	default:
		break;
	}

	return 0;
}

/**
 *  e1000_get_phy_id_82571 - Retrieve the PHY ID and revision
 *  @hw: pointer to the HW structure
 *
 *  Reads the PHY registers and stores the PHY ID and possibly the PHY
 *  revision in the hardware structure.
 **/
static s32 e1000_get_phy_id_82571(struct e1000_hw *hw)
{
	struct e1000_phy_info *phy = &hw->phy;
	s32 ret_val;
	u16 phy_id = 0;

	switch (hw->mac.type) {
	case e1000_82571:
	case e1000_82572:
		/* The 82571 firmware may still be configuring the PHY.
		 * In this case, we cannot access the PHY until the
		 * configuration is done.  So we explicitly set the
		 * PHY ID.
		 */
		phy->id = IGP01E1000_I_PHY_ID;
		break;
	case e1000_82573:
		return e1000e_get_phy_id(hw);
		break;
	case e1000_82574:
	case e1000_82583:
		ret_val = e1e_rphy(hw, MII_PHYSID1, &phy_id);
		if (ret_val)
			return ret_val;

		phy->id = (u32)(phy_id << 16);
		udelay(20);
		ret_val = e1e_rphy(hw, MII_PHYSID2, &phy_id);
		if (ret_val)
			return ret_val;

		phy->id |= (u32)(phy_id);
		phy->revision = (u32)(phy_id & ~PHY_REVISION_MASK);
		break;
	default:
		return -E1000_ERR_PHY;
		break;
	}

	return 0;
}

/**
 *  e1000_get_hw_semaphore_82571 - Acquire hardware semaphore
 *  @hw: pointer to the HW structure
 *
 *  Acquire the HW semaphore to access the PHY or NVM
 **/
static s32 e1000_get_hw_semaphore_82571(struct e1000_hw *hw)
{
	u32 swsm;
	s32 sw_timeout = hw->nvm.word_size + 1;
	s32 fw_timeout = hw->nvm.word_size + 1;
	s32 i = 0;

	/* If we have timedout 3 times on trying to acquire
	 * the inter-port SMBI semaphore, there is old code
	 * operating on the other port, and it is not
	 * releasing SMBI. Modify the number of times that
	 * we try for the semaphore to interwork with this
	 * older code.
	 */
	if (hw->dev_spec.e82571.smb_counter > 2)
		sw_timeout = 1;

	/* Get the SW semaphore */
	while (i < sw_timeout) {
		swsm = er32(SWSM);
		if (!(swsm & E1000_SWSM_SMBI))
			break;

		udelay(50);
		i++;
	}

	if (i == sw_timeout) {
		e_dbg("Driver can't access device - SMBI bit is set.\n");
		hw->dev_spec.e82571.smb_counter++;
	}
	/* Get the FW semaphore. */
	for (i = 0; i < fw_timeout; i++) {
		swsm = er32(SWSM);
		ew32(SWSM, swsm | E1000_SWSM_SWESMBI);

		/* Semaphore acquired if bit latched */
		if (er32(SWSM) & E1000_SWSM_SWESMBI)
			break;

		udelay(50);
	}

	if (i == fw_timeout) {
		/* Release semaphores */
		e1000_put_hw_semaphore_82571(hw);
		e_dbg("Driver can't access the NVM\n");
		return -E1000_ERR_NVM;
	}

	return 0;
}

/**
 *  e1000_put_hw_semaphore_82571 - Release hardware semaphore
 *  @hw: pointer to the HW structure
 *
 *  Release hardware semaphore used to access the PHY or NVM
 **/
static void e1000_put_hw_semaphore_82571(struct e1000_hw *hw)
{
	u32 swsm;

	swsm = er32(SWSM);
	swsm &= ~(E1000_SWSM_SMBI | E1000_SWSM_SWESMBI);
	ew32(SWSM, swsm);
}
/**
 *  e1000_get_hw_semaphore_82573 - Acquire hardware semaphore
 *  @hw: pointer to the HW structure
 *
 *  Acquire the HW semaphore during reset.
 *
 **/
static s32 e1000_get_hw_semaphore_82573(struct e1000_hw *hw)
{
	u32 extcnf_ctrl;
	s32 i = 0;

	extcnf_ctrl = er32(EXTCNF_CTRL);
	do {
		extcnf_ctrl |= E1000_EXTCNF_CTRL_MDIO_SW_OWNERSHIP;
		ew32(EXTCNF_CTRL, extcnf_ctrl);
		extcnf_ctrl = er32(EXTCNF_CTRL);

		if (extcnf_ctrl & E1000_EXTCNF_CTRL_MDIO_SW_OWNERSHIP)
			break;

		usleep_range(2000, 4000);
		i++;
	} while (i < MDIO_OWNERSHIP_TIMEOUT);

	if (i == MDIO_OWNERSHIP_TIMEOUT) {
		/* Release semaphores */
		e1000_put_hw_semaphore_82573(hw);
		e_dbg("Driver can't access the PHY\n");
		return -E1000_ERR_PHY;
	}

	return 0;
}

/**
 *  e1000_put_hw_semaphore_82573 - Release hardware semaphore
 *  @hw: pointer to the HW structure
 *
 *  Release hardware semaphore used during reset.
 *
 **/
static void e1000_put_hw_semaphore_82573(struct e1000_hw *hw)
{
	u32 extcnf_ctrl;

	extcnf_ctrl = er32(EXTCNF_CTRL);
	extcnf_ctrl &= ~E1000_EXTCNF_CTRL_MDIO_SW_OWNERSHIP;
	ew32(EXTCNF_CTRL, extcnf_ctrl);
}

static DEFINE_MUTEX(swflag_mutex);

/**
 *  e1000_get_hw_semaphore_82574 - Acquire hardware semaphore
 *  @hw: pointer to the HW structure
 *
 *  Acquire the HW semaphore to access the PHY or NVM.
 *
 **/
static s32 e1000_get_hw_semaphore_82574(struct e1000_hw *hw)
{
	s32 ret_val;

	mutex_lock(&swflag_mutex);
	ret_val = e1000_get_hw_semaphore_82573(hw);
	if (ret_val)
		mutex_unlock(&swflag_mutex);
	return ret_val;
}

/**
 *  e1000_put_hw_semaphore_82574 - Release hardware semaphore
 *  @hw: pointer to the HW structure
 *
 *  Release hardware semaphore used to access the PHY or NVM
 *
 **/
static void e1000_put_hw_semaphore_82574(struct e1000_hw *hw)
{
	e1000_put_hw_semaphore_82573(hw);
	mutex_unlock(&swflag_mutex);
}

/**
 *  e1000_set_d0_lplu_state_82574 - Set Low Power Linkup D0 state
 *  @hw: pointer to the HW structure
 *  @active: true to enable LPLU, false to disable
 *
 *  Sets the LPLU D0 state according to the active flag.
 *  LPLU will not be activated unless the
 *  device autonegotiation advertisement meets standards of
 *  either 10 or 10/100 or 10/100/1000 at all duplexes.
 *  This is a function pointer entry point only called by
 *  PHY setup routines.
 **/
static s32 e1000_set_d0_lplu_state_82574(struct e1000_hw *hw, bool active)
{
	u32 data = er32(POEMB);

	if (active)
		data |= E1000_PHY_CTRL_D0A_LPLU;
	else
		data &= ~E1000_PHY_CTRL_D0A_LPLU;

	ew32(POEMB, data);
	return 0;
}

/**
 *  e1000_set_d3_lplu_state_82574 - Sets low power link up state for D3
 *  @hw: pointer to the HW structure
 *  @active: boolean used to enable/disable lplu
 *
 *  The low power link up (lplu) state is set to the power management level D3
 *  when active is true, else clear lplu for D3. LPLU
 *  is used during Dx states where the power conservation is most important.
 *  During driver activity, SmartSpeed should be enabled so performance is
 *  maintained.
 **/
static s32 e1000_set_d3_lplu_state_82574(struct e1000_hw *hw, bool active)
{
	u32 data = er32(POEMB);

	if (!active) {
		data &= ~E1000_PHY_CTRL_NOND0A_LPLU;
	} else if ((hw->phy.autoneg_advertised == E1000_ALL_SPEED_DUPLEX) ||
		   (hw->phy.autoneg_advertised == E1000_ALL_NOT_GIG) ||
		   (hw->phy.autoneg_advertised == E1000_ALL_10_SPEED)) {
		data |= E1000_PHY_CTRL_NOND0A_LPLU;
	}

	ew32(POEMB, data);
	return 0;
}

/**
 *  e1000_acquire_nvm_82571 - Request for access to the EEPROM
 *  @hw: pointer to the HW structure
 *
 *  To gain access to the EEPROM, first we must obtain a hardware semaphore.
 *  Then for non-82573 hardware, set the EEPROM access request bit and wait
 *  for EEPROM access grant bit.  If the access grant bit is not set, release
 *  hardware semaphore.
 **/
static s32 e1000_acquire_nvm_82571(struct e1000_hw *hw)
{
	s32 ret_val;

	ret_val = e1000_get_hw_semaphore_82571(hw);
	if (ret_val)
		return ret_val;

	switch (hw->mac.type) {
	case e1000_82573:
		break;
	default:
		ret_val = e1000e_acquire_nvm(hw);
		break;
	}

	if (ret_val)
		e1000_put_hw_semaphore_82571(hw);

	return ret_val;
}

/**
 *  e1000_release_nvm_82571 - Release exclusive access to EEPROM
 *  @hw: pointer to the HW structure
 *
 *  Stop any current commands to the EEPROM and clear the EEPROM request bit.
 **/
static void e1000_release_nvm_82571(struct e1000_hw *hw)
{
	e1000e_release_nvm(hw);
	e1000_put_hw_semaphore_82571(hw);
}

/**
 *  e1000_write_nvm_82571 - Write to EEPROM using appropriate interface
 *  @hw: pointer to the HW structure
 *  @offset: offset within the EEPROM to be written to
 *  @words: number of words to write
 *  @data: 16 bit word(s) to be written to the EEPROM
 *
 *  For non-82573 silicon, write data to EEPROM at offset using SPI interface.
 *
 *  If e1000e_update_nvm_checksum is not called after this function, the
 *  EEPROM will most likely contain an invalid checksum.
 **/
static s32 e1000_write_nvm_82571(struct e1000_hw *hw, u16 offset, u16 words,
				 u16 *data)
{
	s32 ret_val;

	switch (hw->mac.type) {
	case e1000_82573:
	case e1000_82574:
	case e1000_82583:
		ret_val = e1000_write_nvm_eewr_82571(hw, offset, words, data);
		break;
	case e1000_82571:
	case e1000_82572:
		ret_val = e1000e_write_nvm_spi(hw, offset, words, data);
		break;
	default:
		ret_val = -E1000_ERR_NVM;
		break;
	}

	return ret_val;
}

/**
 *  e1000_update_nvm_checksum_82571 - Update EEPROM checksum
 *  @hw: pointer to the HW structure
 *
 *  Updates the EEPROM checksum by reading/adding each word of the EEPROM
 *  up to the checksum.  Then calculates the EEPROM checksum and writes the
 *  value to the EEPROM.
 **/
static s32 e1000_update_nvm_checksum_82571(struct e1000_hw *hw)
{
	u32 eecd;
	s32 ret_val;
	u16 i;

	ret_val = e1000e_update_nvm_checksum_generic(hw);
	if (ret_val)
		return ret_val;

	/* If our nvm is an EEPROM, then we're done
	 * otherwise, commit the checksum to the flash NVM.
	 */
	if (hw->nvm.type != e1000_nvm_flash_hw)
		return 0;

	/* Check for pending operations. */
	for (i = 0; i < E1000_FLASH_UPDATES; i++) {
		usleep_range(1000, 2000);
		if (!(er32(EECD) & E1000_EECD_FLUPD))
			break;
	}

	if (i == E1000_FLASH_UPDATES)
		return -E1000_ERR_NVM;

	/* Reset the firmware if using STM opcode. */
	if ((er32(FLOP) & 0xFF00) == E1000_STM_OPCODE) {
		/* The enabling of and the actual reset must be done
		 * in two write cycles.
		 */
		ew32(HICR, E1000_HICR_FW_RESET_ENABLE);
		e1e_flush();
		ew32(HICR, E1000_HICR_FW_RESET);
	}

	/* Commit the write to flash */
	eecd = er32(EECD) | E1000_EECD_FLUPD;
	ew32(EECD, eecd);

	for (i = 0; i < E1000_FLASH_UPDATES; i++) {
		usleep_range(1000, 2000);
		if (!(er32(EECD) & E1000_EECD_FLUPD))
			break;
	}

	if (i == E1000_FLASH_UPDATES)
		return -E1000_ERR_NVM;

	return 0;
}

/**
 *  e1000_validate_nvm_checksum_82571 - Validate EEPROM checksum
 *  @hw: pointer to the HW structure
 *
 *  Calculates the EEPROM checksum by reading/adding each word of the EEPROM
 *  and then verifies that the sum of the EEPROM is equal to 0xBABA.
 **/
static s32 e1000_validate_nvm_checksum_82571(struct e1000_hw *hw)
{
	if (hw->nvm.type == e1000_nvm_flash_hw)
		e1000_fix_nvm_checksum_82571(hw);

	return e1000e_validate_nvm_checksum_generic(hw);
}

/**
 *  e1000_write_nvm_eewr_82571 - Write to EEPROM for 82573 silicon
 *  @hw: pointer to the HW structure
 *  @offset: offset within the EEPROM to be written to
 *  @words: number of words to write
 *  @data: 16 bit word(s) to be written to the EEPROM
 *
 *  After checking for invalid values, poll the EEPROM to ensure the previous
 *  command has completed before trying to write the next word.  After write
 *  poll for completion.
 *
 *  If e1000e_update_nvm_checksum is not called after this function, the
 *  EEPROM will most likely contain an invalid checksum.
 **/
static s32 e1000_write_nvm_eewr_82571(struct e1000_hw *hw, u16 offset,
				      u16 words, u16 *data)
{
	struct e1000_nvm_info *nvm = &hw->nvm;
	u32 i, eewr = 0;
	s32 ret_val = 0;

	/* A check for invalid values:  offset too large, too many words,
	 * and not enough words.
	 */
	if ((offset >= nvm->word_size) || (words > (nvm->word_size - offset)) ||
	    (words == 0)) {
		e_dbg("nvm parameter(s) out of bounds\n");
		return -E1000_ERR_NVM;
	}

	for (i = 0; i < words; i++) {
		eewr = ((data[i] << E1000_NVM_RW_REG_DATA) |
			((offset + i) << E1000_NVM_RW_ADDR_SHIFT) |
			E1000_NVM_RW_REG_START);

		ret_val = e1000e_poll_eerd_eewr_done(hw, E1000_NVM_POLL_WRITE);
		if (ret_val)
			break;

		ew32(EEWR, eewr);

		ret_val = e1000e_poll_eerd_eewr_done(hw, E1000_NVM_POLL_WRITE);
		if (ret_val)
			break;
	}

	return ret_val;
}

/**
 *  e1000_get_cfg_done_82571 - Poll for configuration done
 *  @hw: pointer to the HW structure
 *
 *  Reads the management control register for the config done bit to be set.
 **/
static s32 e1000_get_cfg_done_82571(struct e1000_hw *hw)
{
	s32 timeout = PHY_CFG_TIMEOUT;

	while (timeout) {
		if (er32(EEMNGCTL) &
		    E1000_NVM_CFG_DONE_PORT_0)
			break;
		usleep_range(1000, 2000);
		timeout--;
	}
	if (!timeout) {
		e_dbg("MNG configuration cycle has not completed.\n");
		return -E1000_ERR_RESET;
	}

	return 0;
}

/**
 *  e1000_set_d0_lplu_state_82571 - Set Low Power Linkup D0 state
 *  @hw: pointer to the HW structure
 *  @active: true to enable LPLU, false to disable
 *
 *  Sets the LPLU D0 state according to the active flag.  When activating LPLU
 *  this function also disables smart speed and vice versa.  LPLU will not be
 *  activated unless the device autonegotiation advertisement meets standards
 *  of either 10 or 10/100 or 10/100/1000 at all duplexes.  This is a function
 *  pointer entry point only called by PHY setup routines.
 **/
static s32 e1000_set_d0_lplu_state_82571(struct e1000_hw *hw, bool active)
{
	struct e1000_phy_info *phy = &hw->phy;
	s32 ret_val;
	u16 data;

	ret_val = e1e_rphy(hw, IGP02E1000_PHY_POWER_MGMT, &data);
	if (ret_val)
		return ret_val;

	if (active) {
		data |= IGP02E1000_PM_D0_LPLU;
		ret_val = e1e_wphy(hw, IGP02E1000_PHY_POWER_MGMT, data);
		if (ret_val)
			return ret_val;

		/* When LPLU is enabled, we should disable SmartSpeed */
		ret_val = e1e_rphy(hw, IGP01E1000_PHY_PORT_CONFIG, &data);
		if (ret_val)
			return ret_val;
		data &= ~IGP01E1000_PSCFR_SMART_SPEED;
		ret_val = e1e_wphy(hw, IGP01E1000_PHY_PORT_CONFIG, data);
		if (ret_val)
			return ret_val;
	} else {
		data &= ~IGP02E1000_PM_D0_LPLU;
		ret_val = e1e_wphy(hw, IGP02E1000_PHY_POWER_MGMT, data);
		/* LPLU and SmartSpeed are mutually exclusive.  LPLU is used
		 * during Dx states where the power conservation is most
		 * important.  During driver activity we should enable
		 * SmartSpeed, so performance is maintained.
		 */
		if (phy->smart_speed == e1000_smart_speed_on) {
			ret_val = e1e_rphy(hw, IGP01E1000_PHY_PORT_CONFIG,
					   &data);
			if (ret_val)
				return ret_val;

			data |= IGP01E1000_PSCFR_SMART_SPEED;
			ret_val = e1e_wphy(hw, IGP01E1000_PHY_PORT_CONFIG,
					   data);
			if (ret_val)
				return ret_val;
		} else if (phy->smart_speed == e1000_smart_speed_off) {
			ret_val = e1e_rphy(hw, IGP01E1000_PHY_PORT_CONFIG,
					   &data);
			if (ret_val)
				return ret_val;

			data &= ~IGP01E1000_PSCFR_SMART_SPEED;
			ret_val = e1e_wphy(hw, IGP01E1000_PHY_PORT_CONFIG,
					   data);
			if (ret_val)
				return ret_val;
		}
	}

	return 0;
}

/**
 *  e1000_reset_hw_82571 - Reset hardware
 *  @hw: pointer to the HW structure
 *
 *  This resets the hardware into a known state.
 **/
static s32 e1000_reset_hw_82571(struct e1000_hw *hw)
{
	u32 ctrl, ctrl_ext, eecd, tctl;
	s32 ret_val;

	/* Prevent the PCI-E bus from sticking if there is no TLP connection
	 * on the last TLP read/write transaction when MAC is reset.
	 */
	ret_val = e1000e_disable_pcie_master(hw);
	if (ret_val)
		e_dbg("PCI-E Master disable polling has failed.\n");

	e_dbg("Masking off all interrupts\n");
	ew32(IMC, 0xffffffff);

	ew32(RCTL, 0);
	tctl = er32(TCTL);
	tctl &= ~E1000_TCTL_EN;
	ew32(TCTL, tctl);
	e1e_flush();

	usleep_range(10000, 20000);

	/* Must acquire the MDIO ownership before MAC reset.
	 * Ownership defaults to firmware after a reset.
	 */
	switch (hw->mac.type) {
	case e1000_82573:
		ret_val = e1000_get_hw_semaphore_82573(hw);
		break;
	case e1000_82574:
	case e1000_82583:
		ret_val = e1000_get_hw_semaphore_82574(hw);
		break;
	default:
		break;
	}
	if (ret_val)
		e_dbg("Cannot acquire MDIO ownership\n");

	ctrl = er32(CTRL);

	e_dbg("Issuing a global reset to MAC\n");
	ew32(CTRL, ctrl | E1000_CTRL_RST);

	/* Must release MDIO ownership and mutex after MAC reset. */
	switch (hw->mac.type) {
	case e1000_82574:
	case e1000_82583:
		e1000_put_hw_semaphore_82574(hw);
		break;
	default:
		break;
	}

	if (hw->nvm.type == e1000_nvm_flash_hw) {
		udelay(10);
		ctrl_ext = er32(CTRL_EXT);
		ctrl_ext |= E1000_CTRL_EXT_EE_RST;
		ew32(CTRL_EXT, ctrl_ext);
		e1e_flush();
	}

	ret_val = e1000e_get_auto_rd_done(hw);
	if (ret_val)
		/* We don't want to continue accessing MAC registers. */
		return ret_val;

	/* Phy configuration from NVM just starts after EECD_AUTO_RD is set.
	 * Need to wait for Phy configuration completion before accessing
	 * NVM and Phy.
	 */

	switch (hw->mac.type) {
	case e1000_82571:
	case e1000_82572:
		/* REQ and GNT bits need to be cleared when using AUTO_RD
		 * to access the EEPROM.
		 */
		eecd = er32(EECD);
		eecd &= ~(E1000_EECD_REQ | E1000_EECD_GNT);
		ew32(EECD, eecd);
		break;
	case e1000_82573:
	case e1000_82574:
	case e1000_82583:
		msleep(25);
		break;
	default:
		break;
	}

	/* Clear any pending interrupt events. */
	ew32(IMC, 0xffffffff);
	er32(ICR);

	if (hw->mac.type == e1000_82571) {
		/* Install any alternate MAC address into RAR0 */
		ret_val = e1000_check_alt_mac_addr_generic(hw);
		if (ret_val)
			return ret_val;

		e1000e_set_laa_state_82571(hw, true);
	}

	/* Reinitialize the 82571 serdes link state machine */
	if (hw->phy.media_type == e1000_media_type_internal_serdes)
		hw->mac.serdes_link_state = e1000_serdes_link_down;

	return 0;
}

/**
 *  e1000_init_hw_82571 - Initialize hardware
 *  @hw: pointer to the HW structure
 *
 *  This inits the hardware readying it for operation.
 **/
static s32 e1000_init_hw_82571(struct e1000_hw *hw)
{
	struct e1000_mac_info *mac = &hw->mac;
	u32 reg_data;
	s32 ret_val;
	u16 i, rar_count = mac->rar_entry_count;

	e1000_initialize_hw_bits_82571(hw);

	/* Initialize identification LED */
	ret_val = mac->ops.id_led_init(hw);
	if (ret_val)
		e_dbg("Error initializing identification LED\n");
		/* This is not fatal and we should not stop init due to this */

	/* Disabling VLAN filtering */
	e_dbg("Initializing the IEEE VLAN\n");
	mac->ops.clear_vfta(hw);

	/* Setup the receive address.
	 * If, however, a locally administered address was assigned to the
	 * 82571, we must reserve a RAR for it to work around an issue where
	 * resetting one port will reload the MAC on the other port.
	 */
	if (e1000e_get_laa_state_82571(hw))
		rar_count--;
	e1000e_init_rx_addrs(hw, rar_count);

	/* Zero out the Multicast HASH table */
	e_dbg("Zeroing the MTA\n");
	for (i = 0; i < mac->mta_reg_count; i++)
		E1000_WRITE_REG_ARRAY(hw, E1000_MTA, i, 0);

	/* Setup link and flow control */
	ret_val = mac->ops.setup_link(hw);

	/* Set the transmit descriptor write-back policy */
	reg_data = er32(TXDCTL(0));
	reg_data = ((reg_data & ~E1000_TXDCTL_WTHRESH) |
		    E1000_TXDCTL_FULL_TX_DESC_WB |
		    E1000_TXDCTL_COUNT_DESC);
	ew32(TXDCTL(0), reg_data);

	/* ...for both queues. */
	switch (mac->type) {
	case e1000_82573:
		e1000e_enable_tx_pkt_filtering(hw);
		/* fall through */
	case e1000_82574:
	case e1000_82583:
		reg_data = er32(GCR);
		reg_data |= E1000_GCR_L1_ACT_WITHOUT_L0S_RX;
		ew32(GCR, reg_data);
		break;
	default:
		reg_data = er32(TXDCTL(1));
		reg_data = ((reg_data & ~E1000_TXDCTL_WTHRESH) |
			    E1000_TXDCTL_FULL_TX_DESC_WB |
			    E1000_TXDCTL_COUNT_DESC);
		ew32(TXDCTL(1), reg_data);
		break;
	}

	/* Clear all of the statistics registers (clear on read).  It is
	 * important that we do this after we have tried to establish link
	 * because the symbol error count will increment wildly if there
	 * is no link.
	 */
	e1000_clear_hw_cntrs_82571(hw);

	return ret_val;
}

/**
 *  e1000_initialize_hw_bits_82571 - Initialize hardware-dependent bits
 *  @hw: pointer to the HW structure
 *
 *  Initializes required hardware-dependent bits needed for normal operation.
 **/
static void e1000_initialize_hw_bits_82571(struct e1000_hw *hw)
{
	u32 reg;

	/* Transmit Descriptor Control 0 */
	reg = er32(TXDCTL(0));
	reg |= (1 << 22);
	ew32(TXDCTL(0), reg);

	/* Transmit Descriptor Control 1 */
	reg = er32(TXDCTL(1));
	reg |= (1 << 22);
	ew32(TXDCTL(1), reg);

	/* Transmit Arbitration Control 0 */
	reg = er32(TARC(0));
	reg &= ~(0xF << 27); /* 30:27 */
	switch (hw->mac.type) {
	case e1000_82571:
	case e1000_82572:
		reg |= (1 << 23) | (1 << 24) | (1 << 25) | (1 << 26);
		break;
	case e1000_82574:
	case e1000_82583:
		reg |= (1 << 26);
		break;
	default:
		break;
	}
	ew32(TARC(0), reg);

	/* Transmit Arbitration Control 1 */
	reg = er32(TARC(1));
	switch (hw->mac.type) {
	case e1000_82571:
	case e1000_82572:
		reg &= ~((1 << 29) | (1 << 30));
		reg |= (1 << 22) | (1 << 24) | (1 << 25) | (1 << 26);
		if (er32(TCTL) & E1000_TCTL_MULR)
			reg &= ~(1 << 28);
		else
			reg |= (1 << 28);
		ew32(TARC(1), reg);
		break;
	default:
		break;
	}

	/* Device Control */
	switch (hw->mac.type) {
	case e1000_82573:
	case e1000_82574:
	case e1000_82583:
		reg = er32(CTRL);
		reg &= ~(1 << 29);
		ew32(CTRL, reg);
		break;
	default:
		break;
	}

	/* Extended Device Control */
	switch (hw->mac.type) {
	case e1000_82573:
	case e1000_82574:
	case e1000_82583:
		reg = er32(CTRL_EXT);
		reg &= ~(1 << 23);
		reg |= (1 << 22);
		ew32(CTRL_EXT, reg);
		break;
	default:
		break;
	}

	if (hw->mac.type == e1000_82571) {
		reg = er32(PBA_ECC);
		reg |= E1000_PBA_ECC_CORR_EN;
		ew32(PBA_ECC, reg);
	}

	/* Workaround for hardware errata.
	 * Ensure that DMA Dynamic Clock gating is disabled on 82571 and 82572
	 */
	if ((hw->mac.type == e1000_82571) || (hw->mac.type == e1000_82572)) {
		reg = er32(CTRL_EXT);
		reg &= ~E1000_CTRL_EXT_DMA_DYN_CLK_EN;
		ew32(CTRL_EXT, reg);
	}

	/* Disable IPv6 extension header parsing because some malformed
	 * IPv6 headers can hang the Rx.
	 */
	if (hw->mac.type <= e1000_82573) {
		reg = er32(RFCTL);
		reg |= (E1000_RFCTL_IPV6_EX_DIS | E1000_RFCTL_NEW_IPV6_EXT_DIS);
		ew32(RFCTL, reg);
	}

	/* PCI-Ex Control Registers */
	switch (hw->mac.type) {
	case e1000_82574:
	case e1000_82583:
		reg = er32(GCR);
		reg |= (1 << 22);
		ew32(GCR, reg);

		/* Workaround for hardware errata.
		 * apply workaround for hardware errata documented in errata
		 * docs Fixes issue where some error prone or unreliable PCIe
		 * completions are occurring, particularly with ASPM enabled.
		 * Without fix, issue can cause Tx timeouts.
		 */
		reg = er32(GCR2);
		reg |= 1;
		ew32(GCR2, reg);
		break;
	default:
		break;
	}
}

/**
 *  e1000_clear_vfta_82571 - Clear VLAN filter table
 *  @hw: pointer to the HW structure
 *
 *  Clears the register array which contains the VLAN filter table by
 *  setting all the values to 0.
 **/
static void e1000_clear_vfta_82571(struct e1000_hw *hw)
{
	u32 offset;
	u32 vfta_value = 0;
	u32 vfta_offset = 0;
	u32 vfta_bit_in_reg = 0;

	switch (hw->mac.type) {
	case e1000_82573:
	case e1000_82574:
	case e1000_82583:
		if (hw->mng_cookie.vlan_id != 0) {
			/* The VFTA is a 4096b bit-field, each identifying
			 * a single VLAN ID.  The following operations
			 * determine which 32b entry (i.e. offset) into the
			 * array we want to set the VLAN ID (i.e. bit) of
			 * the manageability unit.
			 */
			vfta_offset = (hw->mng_cookie.vlan_id >>
				       E1000_VFTA_ENTRY_SHIFT) &
			    E1000_VFTA_ENTRY_MASK;
			vfta_bit_in_reg =
			    1 << (hw->mng_cookie.vlan_id &
				  E1000_VFTA_ENTRY_BIT_SHIFT_MASK);
		}
		break;
	default:
		break;
	}
	for (offset = 0; offset < E1000_VLAN_FILTER_TBL_SIZE; offset++) {
		/* If the offset we want to clear is the same offset of the
		 * manageability VLAN ID, then clear all bits except that of
		 * the manageability unit.
		 */
		vfta_value = (offset == vfta_offset) ? vfta_bit_in_reg : 0;
		E1000_WRITE_REG_ARRAY(hw, E1000_VFTA, offset, vfta_value);
		e1e_flush();
	}
}

/**
 *  e1000_check_mng_mode_82574 - Check manageability is enabled
 *  @hw: pointer to the HW structure
 *
 *  Reads the NVM Initialization Control Word 2 and returns true
 *  (>0) if any manageability is enabled, else false (0).
 **/
static bool e1000_check_mng_mode_82574(struct e1000_hw *hw)
{
	u16 data;

	e1000_read_nvm(hw, NVM_INIT_CONTROL2_REG, 1, &data);
	return (data & E1000_NVM_INIT_CTRL2_MNGM) != 0;
}

/**
 *  e1000_led_on_82574 - Turn LED on
 *  @hw: pointer to the HW structure
 *
 *  Turn LED on.
 **/
static s32 e1000_led_on_82574(struct e1000_hw *hw)
{
	u32 ctrl;
	u32 i;

	ctrl = hw->mac.ledctl_mode2;
	if (!(E1000_STATUS_LU & er32(STATUS))) {
		/* If no link, then turn LED on by setting the invert bit
		 * for each LED that's "on" (0x0E) in ledctl_mode2.
		 */
		for (i = 0; i < 4; i++)
			if (((hw->mac.ledctl_mode2 >> (i * 8)) & 0xFF) ==
			    E1000_LEDCTL_MODE_LED_ON)
				ctrl |= (E1000_LEDCTL_LED0_IVRT << (i * 8));
	}
	ew32(LEDCTL, ctrl);

	return 0;
}

/**
 *  e1000_check_phy_82574 - check 82574 phy hung state
 *  @hw: pointer to the HW structure
 *
 *  Returns whether phy is hung or not
 **/
bool e1000_check_phy_82574(struct e1000_hw *hw)
{
	u16 status_1kbt = 0;
	u16 receive_errors = 0;
	s32 ret_val;

	/* Read PHY Receive Error counter first, if its is max - all F's then
	 * read the Base1000T status register If both are max then PHY is hung.
	 */
	ret_val = e1e_rphy(hw, E1000_RECEIVE_ERROR_COUNTER, &receive_errors);
	if (ret_val)
		return false;
	if (receive_errors == E1000_RECEIVE_ERROR_MAX)  {
		ret_val = e1e_rphy(hw, E1000_BASE1000T_STATUS, &status_1kbt);
		if (ret_val)
			return false;
		if ((status_1kbt & E1000_IDLE_ERROR_COUNT_MASK) ==
		    E1000_IDLE_ERROR_COUNT_MASK)
			return true;
	}

	return false;
}

/**
 *  e1000_setup_link_82571 - Setup flow control and link settings
 *  @hw: pointer to the HW structure
 *
 *  Determines which flow control settings to use, then configures flow
 *  control.  Calls the appropriate media-specific link configuration
 *  function.  Assuming the adapter has a valid link partner, a valid link
 *  should be established.  Assumes the hardware has previously been reset
 *  and the transmitter and receiver are not enabled.
 **/
static s32 e1000_setup_link_82571(struct e1000_hw *hw)
{
	/* 82573 does not have a word in the NVM to determine
	 * the default flow control setting, so we explicitly
	 * set it to full.
	 */
	switch (hw->mac.type) {
	case e1000_82573:
	case e1000_82574:
	case e1000_82583:
		if (hw->fc.requested_mode == e1000_fc_default)
			hw->fc.requested_mode = e1000_fc_full;
		break;
	default:
		break;
	}

	return e1000e_setup_link_generic(hw);
}

/**
 *  e1000_setup_copper_link_82571 - Configure copper link settings
 *  @hw: pointer to the HW structure
 *
 *  Configures the link for auto-neg or forced speed and duplex.  Then we check
 *  for link, once link is established calls to configure collision distance
 *  and flow control are called.
 **/
static s32 e1000_setup_copper_link_82571(struct e1000_hw *hw)
{
	u32 ctrl;
	s32 ret_val;

	ctrl = er32(CTRL);
	ctrl |= E1000_CTRL_SLU;
	ctrl &= ~(E1000_CTRL_FRCSPD | E1000_CTRL_FRCDPX);
	ew32(CTRL, ctrl);

	switch (hw->phy.type) {
	case e1000_phy_m88:
	case e1000_phy_bm:
		ret_val = e1000e_copper_link_setup_m88(hw);
		break;
	case e1000_phy_igp_2:
		ret_val = e1000e_copper_link_setup_igp(hw);
		break;
	default:
		return -E1000_ERR_PHY;
		break;
	}

	if (ret_val)
		return ret_val;

	return e1000e_setup_copper_link(hw);
}

/**
 *  e1000_setup_fiber_serdes_link_82571 - Setup link for fiber/serdes
 *  @hw: pointer to the HW structure
 *
 *  Configures collision distance and flow control for fiber and serdes links.
 *  Upon successful setup, poll for link.
 **/
static s32 e1000_setup_fiber_serdes_link_82571(struct e1000_hw *hw)
{
	switch (hw->mac.type) {
	case e1000_82571:
	case e1000_82572:
		/* If SerDes loopback mode is entered, there is no form
		 * of reset to take the adapter out of that mode.  So we
		 * have to explicitly take the adapter out of loopback
		 * mode.  This prevents drivers from twiddling their thumbs
		 * if another tool failed to take it out of loopback mode.
		 */
		ew32(SCTL, E1000_SCTL_DISABLE_SERDES_LOOPBACK);
		break;
	default:
		break;
	}

	return e1000e_setup_fiber_serdes_link(hw);
}

/**
 *  e1000_check_for_serdes_link_82571 - Check for link (Serdes)
 *  @hw: pointer to the HW structure
 *
 *  Reports the link state as up or down.
 *
 *  If autonegotiation is supported by the link partner, the link state is
 *  determined by the result of autonegotiation. This is the most likely case.
 *  If autonegotiation is not supported by the link partner, and the link
 *  has a valid signal, force the link up.
 *
 *  The link state is represented internally here by 4 states:
 *
 *  1) down
 *  2) autoneg_progress
 *  3) autoneg_complete (the link successfully autonegotiated)
 *  4) forced_up (the link has been forced up, it did not autonegotiate)
 *
 **/
static s32 e1000_check_for_serdes_link_82571(struct e1000_hw *hw)
{
	struct e1000_mac_info *mac = &hw->mac;
	u32 rxcw;
	u32 ctrl;
	u32 status;
	u32 txcw;
	u32 i;
	s32 ret_val = 0;

	ctrl = er32(CTRL);
	status = er32(STATUS);
	er32(RXCW);
	/* SYNCH bit and IV bit are sticky */
	udelay(10);
	rxcw = er32(RXCW);

	if ((rxcw & E1000_RXCW_SYNCH) && !(rxcw & E1000_RXCW_IV)) {
		/* Receiver is synchronized with no invalid bits.  */
		switch (mac->serdes_link_state) {
		case e1000_serdes_link_autoneg_complete:
			if (!(status & E1000_STATUS_LU)) {
				/* We have lost link, retry autoneg before
				 * reporting link failure
				 */
				mac->serdes_link_state =
				    e1000_serdes_link_autoneg_progress;
				mac->serdes_has_link = false;
				e_dbg("AN_UP     -> AN_PROG\n");
			} else {
				mac->serdes_has_link = true;
			}
			break;

		case e1000_serdes_link_forced_up:
			/* If we are receiving /C/ ordered sets, re-enable
			 * auto-negotiation in the TXCW register and disable
			 * forced link in the Device Control register in an
			 * attempt to auto-negotiate with our link partner.
			 */
			if (rxcw & E1000_RXCW_C) {
				/* Enable autoneg, and unforce link up */
				ew32(TXCW, mac->txcw);
				ew32(CTRL, (ctrl & ~E1000_CTRL_SLU));
				mac->serdes_link_state =
				    e1000_serdes_link_autoneg_progress;
				mac->serdes_has_link = false;
				e_dbg("FORCED_UP -> AN_PROG\n");
			} else {
				mac->serdes_has_link = true;
			}
			break;

		case e1000_serdes_link_autoneg_progress:
			if (rxcw & E1000_RXCW_C) {
				/* We received /C/ ordered sets, meaning the
				 * link partner has autonegotiated, and we can
				 * trust the Link Up (LU) status bit.
				 */
				if (status & E1000_STATUS_LU) {
					mac->serdes_link_state =
					    e1000_serdes_link_autoneg_complete;
					e_dbg("AN_PROG   -> AN_UP\n");
					mac->serdes_has_link = true;
				} else {
					/* Autoneg completed, but failed. */
					mac->serdes_link_state =
					    e1000_serdes_link_down;
					e_dbg("AN_PROG   -> DOWN\n");
				}
			} else {
				/* The link partner did not autoneg.
				 * Force link up and full duplex, and change
				 * state to forced.
				 */
				ew32(TXCW, (mac->txcw & ~E1000_TXCW_ANE));
				ctrl |= (E1000_CTRL_SLU | E1000_CTRL_FD);
				ew32(CTRL, ctrl);

				/* Configure Flow Control after link up. */
				ret_val = e1000e_config_fc_after_link_up(hw);
				if (ret_val) {
					e_dbg("Error config flow control\n");
					break;
				}
				mac->serdes_link_state =
				    e1000_serdes_link_forced_up;
				mac->serdes_has_link = true;
				e_dbg("AN_PROG   -> FORCED_UP\n");
			}
			break;

		case e1000_serdes_link_down:
		default:
			/* The link was down but the receiver has now gained
			 * valid sync, so lets see if we can bring the link
			 * up.
			 */
			ew32(TXCW, mac->txcw);
			ew32(CTRL, (ctrl & ~E1000_CTRL_SLU));
			mac->serdes_link_state =
			    e1000_serdes_link_autoneg_progress;
			mac->serdes_has_link = false;
			e_dbg("DOWN      -> AN_PROG\n");
			break;
		}
	} else {
		if (!(rxcw & E1000_RXCW_SYNCH)) {
			mac->serdes_has_link = false;
			mac->serdes_link_state = e1000_serdes_link_down;
			e_dbg("ANYSTATE  -> DOWN\n");
		} else {
			/* Check several times, if SYNCH bit and CONFIG
			 * bit both are consistently 1 then simply ignore
			 * the IV bit and restart Autoneg
			 */
			for (i = 0; i < AN_RETRY_COUNT; i++) {
				udelay(10);
				rxcw = er32(RXCW);
				if ((rxcw & E1000_RXCW_SYNCH) &&
				    (rxcw & E1000_RXCW_C))
					continue;

				if (rxcw & E1000_RXCW_IV) {
					mac->serdes_has_link = false;
					mac->serdes_link_state =
					    e1000_serdes_link_down;
					e_dbg("ANYSTATE  -> DOWN\n");
					break;
				}
			}

			if (i == AN_RETRY_COUNT) {
				txcw = er32(TXCW);
				txcw |= E1000_TXCW_ANE;
				ew32(TXCW, txcw);
				mac->serdes_link_state =
				    e1000_serdes_link_autoneg_progress;
				mac->serdes_has_link = false;
				e_dbg("ANYSTATE  -> AN_PROG\n");
			}
		}
	}

	return ret_val;
}

/**
 *  e1000_valid_led_default_82571 - Verify a valid default LED config
 *  @hw: pointer to the HW structure
 *  @data: pointer to the NVM (EEPROM)
 *
 *  Read the EEPROM for the current default LED configuration.  If the
 *  LED configuration is not valid, set to a valid LED configuration.
 **/
static s32 e1000_valid_led_default_82571(struct e1000_hw *hw, u16 *data)
{
	s32 ret_val;

	ret_val = e1000_read_nvm(hw, NVM_ID_LED_SETTINGS, 1, data);
	if (ret_val) {
		e_dbg("NVM Read Error\n");
		return ret_val;
	}

	switch (hw->mac.type) {
	case e1000_82573:
	case e1000_82574:
	case e1000_82583:
		if (*data == ID_LED_RESERVED_F746)
			*data = ID_LED_DEFAULT_82573;
		break;
	default:
		if (*data == ID_LED_RESERVED_0000 ||
		    *data == ID_LED_RESERVED_FFFF)
			*data = ID_LED_DEFAULT;
		break;
	}

	return 0;
}

/**
 *  e1000e_get_laa_state_82571 - Get locally administered address state
 *  @hw: pointer to the HW structure
 *
 *  Retrieve and return the current locally administered address state.
 **/
bool e1000e_get_laa_state_82571(struct e1000_hw *hw)
{
	if (hw->mac.type != e1000_82571)
		return false;

	return hw->dev_spec.e82571.laa_is_present;
}

/**
 *  e1000e_set_laa_state_82571 - Set locally administered address state
 *  @hw: pointer to the HW structure
 *  @state: enable/disable locally administered address
 *
 *  Enable/Disable the current locally administered address state.
 **/
void e1000e_set_laa_state_82571(struct e1000_hw *hw, bool state)
{
	if (hw->mac.type != e1000_82571)
		return;

	hw->dev_spec.e82571.laa_is_present = state;

	/* If workaround is activated... */
	if (state)
		/* Hold a copy of the LAA in RAR[14] This is done so that
		 * between the time RAR[0] gets clobbered and the time it
		 * gets fixed, the actual LAA is in one of the RARs and no
		 * incoming packets directed to this port are dropped.
		 * Eventually the LAA will be in RAR[0] and RAR[14].
		 */
		hw->mac.ops.rar_set(hw, hw->mac.addr,
				    hw->mac.rar_entry_count - 1);
}

/**
 *  e1000_fix_nvm_checksum_82571 - Fix EEPROM checksum
 *  @hw: pointer to the HW structure
 *
 *  Verifies that the EEPROM has completed the update.  After updating the
 *  EEPROM, we need to check bit 15 in work 0x23 for the checksum fix.  If
 *  the checksum fix is not implemented, we need to set the bit and update
 *  the checksum.  Otherwise, if bit 15 is set and the checksum is incorrect,
 *  we need to return bad checksum.
 **/
static s32 e1000_fix_nvm_checksum_82571(struct e1000_hw *hw)
{
	struct e1000_nvm_info *nvm = &hw->nvm;
	s32 ret_val;
	u16 data;

	if (nvm->type != e1000_nvm_flash_hw)
		return 0;

	/* Check bit 4 of word 10h.  If it is 0, firmware is done updating
	 * 10h-12h.  Checksum may need to be fixed.
	 */
	ret_val = e1000_read_nvm(hw, 0x10, 1, &data);
	if (ret_val)
		return ret_val;

	if (!(data & 0x10)) {
		/* Read 0x23 and check bit 15.  This bit is a 1
		 * when the checksum has already been fixed.  If
		 * the checksum is still wrong and this bit is a
		 * 1, we need to return bad checksum.  Otherwise,
		 * we need to set this bit to a 1 and update the
		 * checksum.
		 */
		ret_val = e1000_read_nvm(hw, 0x23, 1, &data);
		if (ret_val)
			return ret_val;

		if (!(data & 0x8000)) {
			data |= 0x8000;
			ret_val = e1000_write_nvm(hw, 0x23, 1, &data);
			if (ret_val)
				return ret_val;
			ret_val = e1000e_update_nvm_checksum(hw);
			if (ret_val)
				return ret_val;
		}
	}

	return 0;
}

/**
 *  e1000_read_mac_addr_82571 - Read device MAC address
 *  @hw: pointer to the HW structure
 **/
static s32 e1000_read_mac_addr_82571(struct e1000_hw *hw)
{
	if (hw->mac.type == e1000_82571) {
		s32 ret_val;

		/* If there's an alternate MAC address place it in RAR0
		 * so that it will override the Si installed default perm
		 * address.
		 */
		ret_val = e1000_check_alt_mac_addr_generic(hw);
		if (ret_val)
			return ret_val;
	}

	return e1000_read_mac_addr_generic(hw);
}

/**
 * e1000_power_down_phy_copper_82571 - Remove link during PHY power down
 * @hw: pointer to the HW structure
 *
 * In the case of a PHY power down to save power, or to turn off link during a
 * driver unload, or wake on lan is not enabled, remove the link.
 **/
static void e1000_power_down_phy_copper_82571(struct e1000_hw *hw)
{
	struct e1000_phy_info *phy = &hw->phy;
	struct e1000_mac_info *mac = &hw->mac;

	if (!phy->ops.check_reset_block)
		return;

	/* If the management interface is not enabled, then power down */
	if (!(mac->ops.check_mng_mode(hw) || phy->ops.check_reset_block(hw)))
		e1000_power_down_phy_copper(hw);
}

/**
 *  e1000_clear_hw_cntrs_82571 - Clear device specific hardware counters
 *  @hw: pointer to the HW structure
 *
 *  Clears the hardware counters by reading the counter registers.
 **/
static void e1000_clear_hw_cntrs_82571(struct e1000_hw *hw)
{
	e1000e_clear_hw_cntrs_base(hw);

	er32(PRC64);
	er32(PRC127);
	er32(PRC255);
	er32(PRC511);
	er32(PRC1023);
	er32(PRC1522);
	er32(PTC64);
	er32(PTC127);
	er32(PTC255);
	er32(PTC511);
	er32(PTC1023);
	er32(PTC1522);

	er32(ALGNERRC);
	er32(RXERRC);
	er32(TNCRS);
	er32(CEXTERR);
	er32(TSCTC);
	er32(TSCTFC);

	er32(MGTPRC);
	er32(MGTPDC);
	er32(MGTPTC);

	er32(IAC);
	er32(ICRXOC);

	er32(ICRXPTC);
	er32(ICRXATC);
	er32(ICTXPTC);
	er32(ICTXATC);
	er32(ICTXQEC);
	er32(ICTXQMTC);
	er32(ICRXDMTC);
}

static const struct e1000_mac_operations e82571_mac_ops = {
	/* .check_mng_mode: mac type dependent */
	/* .check_for_link: media type dependent */
	.id_led_init		= e1000e_id_led_init_generic,
	.cleanup_led		= e1000e_cleanup_led_generic,
	.clear_hw_cntrs		= e1000_clear_hw_cntrs_82571,
	.get_bus_info		= e1000e_get_bus_info_pcie,
	.set_lan_id		= e1000_set_lan_id_multi_port_pcie,
	/* .get_link_up_info: media type dependent */
	/* .led_on: mac type dependent */
	.led_off		= e1000e_led_off_generic,
	.update_mc_addr_list	= e1000e_update_mc_addr_list_generic,
	.write_vfta		= e1000_write_vfta_generic,
	.clear_vfta		= e1000_clear_vfta_82571,
	.reset_hw		= e1000_reset_hw_82571,
	.init_hw		= e1000_init_hw_82571,
	.setup_link		= e1000_setup_link_82571,
	/* .setup_physical_interface: media type dependent */
	.setup_led		= e1000e_setup_led_generic,
	.config_collision_dist	= e1000e_config_collision_dist_generic,
	.read_mac_addr		= e1000_read_mac_addr_82571,
	.rar_set		= e1000e_rar_set_generic,
};

static const struct e1000_phy_operations e82_phy_ops_igp = {
	.acquire		= e1000_get_hw_semaphore_82571,
	.check_polarity		= e1000_check_polarity_igp,
	.check_reset_block	= e1000e_check_reset_block_generic,
	.commit			= NULL,
	.force_speed_duplex	= e1000e_phy_force_speed_duplex_igp,
	.get_cfg_done		= e1000_get_cfg_done_82571,
	.get_cable_length	= e1000e_get_cable_length_igp_2,
	.get_info		= e1000e_get_phy_info_igp,
	.read_reg		= e1000e_read_phy_reg_igp,
	.release		= e1000_put_hw_semaphore_82571,
	.reset			= e1000e_phy_hw_reset_generic,
	.set_d0_lplu_state	= e1000_set_d0_lplu_state_82571,
	.set_d3_lplu_state	= e1000e_set_d3_lplu_state,
	.write_reg		= e1000e_write_phy_reg_igp,
	.cfg_on_link_up		= NULL,
};

static const struct e1000_phy_operations e82_phy_ops_m88 = {
	.acquire		= e1000_get_hw_semaphore_82571,
	.check_polarity		= e1000_check_polarity_m88,
	.check_reset_block	= e1000e_check_reset_block_generic,
	.commit			= e1000e_phy_sw_reset,
	.force_speed_duplex	= e1000e_phy_force_speed_duplex_m88,
	.get_cfg_done		= e1000e_get_cfg_done_generic,
	.get_cable_length	= e1000e_get_cable_length_m88,
	.get_info		= e1000e_get_phy_info_m88,
	.read_reg		= e1000e_read_phy_reg_m88,
	.release		= e1000_put_hw_semaphore_82571,
	.reset			= e1000e_phy_hw_reset_generic,
	.set_d0_lplu_state	= e1000_set_d0_lplu_state_82571,
	.set_d3_lplu_state	= e1000e_set_d3_lplu_state,
	.write_reg		= e1000e_write_phy_reg_m88,
	.cfg_on_link_up		= NULL,
};

static const struct e1000_phy_operations e82_phy_ops_bm = {
	.acquire		= e1000_get_hw_semaphore_82571,
	.check_polarity		= e1000_check_polarity_m88,
	.check_reset_block	= e1000e_check_reset_block_generic,
	.commit			= e1000e_phy_sw_reset,
	.force_speed_duplex	= e1000e_phy_force_speed_duplex_m88,
	.get_cfg_done		= e1000e_get_cfg_done_generic,
	.get_cable_length	= e1000e_get_cable_length_m88,
	.get_info		= e1000e_get_phy_info_m88,
	.read_reg		= e1000e_read_phy_reg_bm2,
	.release		= e1000_put_hw_semaphore_82571,
	.reset			= e1000e_phy_hw_reset_generic,
	.set_d0_lplu_state	= e1000_set_d0_lplu_state_82571,
	.set_d3_lplu_state	= e1000e_set_d3_lplu_state,
	.write_reg		= e1000e_write_phy_reg_bm2,
	.cfg_on_link_up		= NULL,
};

static const struct e1000_nvm_operations e82571_nvm_ops = {
	.acquire		= e1000_acquire_nvm_82571,
	.read			= e1000e_read_nvm_eerd,
	.release		= e1000_release_nvm_82571,
	.reload			= e1000e_reload_nvm_generic,
	.update			= e1000_update_nvm_checksum_82571,
	.valid_led_default	= e1000_valid_led_default_82571,
	.validate		= e1000_validate_nvm_checksum_82571,
	.write			= e1000_write_nvm_82571,
};

const struct e1000_info e1000_82571_info = {
	.mac			= e1000_82571,
	.flags			= FLAG_HAS_HW_VLAN_FILTER
				  | FLAG_HAS_JUMBO_FRAMES
				  | FLAG_HAS_WOL
				  | FLAG_APME_IN_CTRL3
				  | FLAG_HAS_CTRLEXT_ON_LOAD
				  | FLAG_HAS_SMART_POWER_DOWN
				  | FLAG_RESET_OVERWRITES_LAA /* errata */
				  | FLAG_TARC_SPEED_MODE_BIT /* errata */
				  | FLAG_APME_CHECK_PORT_B,
	.flags2			= FLAG2_DISABLE_ASPM_L1 /* errata 13 */
				  | FLAG2_DMA_BURST,
	.pba			= 38,
	.max_hw_frame_size	= DEFAULT_JUMBO,
	.get_variants		= e1000_get_variants_82571,
	.mac_ops		= &e82571_mac_ops,
	.phy_ops		= &e82_phy_ops_igp,
	.nvm_ops		= &e82571_nvm_ops,
};

const struct e1000_info e1000_82572_info = {
	.mac			= e1000_82572,
	.flags			= FLAG_HAS_HW_VLAN_FILTER
				  | FLAG_HAS_JUMBO_FRAMES
				  | FLAG_HAS_WOL
				  | FLAG_APME_IN_CTRL3
				  | FLAG_HAS_CTRLEXT_ON_LOAD
				  | FLAG_TARC_SPEED_MODE_BIT, /* errata */
	.flags2			= FLAG2_DISABLE_ASPM_L1 /* errata 13 */
				  | FLAG2_DMA_BURST,
	.pba			= 38,
	.max_hw_frame_size	= DEFAULT_JUMBO,
	.get_variants		= e1000_get_variants_82571,
	.mac_ops		= &e82571_mac_ops,
	.phy_ops		= &e82_phy_ops_igp,
	.nvm_ops		= &e82571_nvm_ops,
};

const struct e1000_info e1000_82573_info = {
	.mac			= e1000_82573,
	.flags			= FLAG_HAS_HW_VLAN_FILTER
				  | FLAG_HAS_WOL
				  | FLAG_APME_IN_CTRL3
				  | FLAG_HAS_SMART_POWER_DOWN
				  | FLAG_HAS_AMT
				  | FLAG_HAS_SWSM_ON_LOAD,
	.flags2			= FLAG2_DISABLE_ASPM_L1
				  | FLAG2_DISABLE_ASPM_L0S,
	.pba			= 20,
	.max_hw_frame_size	= ETH_FRAME_LEN + ETH_FCS_LEN,
	.get_variants		= e1000_get_variants_82571,
	.mac_ops		= &e82571_mac_ops,
	.phy_ops		= &e82_phy_ops_m88,
	.nvm_ops		= &e82571_nvm_ops,
};

const struct e1000_info e1000_82574_info = {
	.mac			= e1000_82574,
	.flags			= FLAG_HAS_HW_VLAN_FILTER
				  | FLAG_HAS_MSIX
				  | FLAG_HAS_JUMBO_FRAMES
				  | FLAG_HAS_WOL
				  | FLAG_HAS_HW_TIMESTAMP
				  | FLAG_APME_IN_CTRL3
				  | FLAG_HAS_SMART_POWER_DOWN
				  | FLAG_HAS_AMT
				  | FLAG_HAS_CTRLEXT_ON_LOAD,
	.flags2			 = FLAG2_CHECK_PHY_HANG
				  | FLAG2_DISABLE_ASPM_L0S
				  | FLAG2_DISABLE_ASPM_L1
				  | FLAG2_NO_DISABLE_RX
				  | FLAG2_DMA_BURST,
	.pba			= 32,
	.max_hw_frame_size	= DEFAULT_JUMBO,
	.get_variants		= e1000_get_variants_82571,
	.mac_ops		= &e82571_mac_ops,
	.phy_ops		= &e82_phy_ops_bm,
	.nvm_ops		= &e82571_nvm_ops,
};

const struct e1000_info e1000_82583_info = {
	.mac			= e1000_82583,
	.flags			= FLAG_HAS_HW_VLAN_FILTER
				  | FLAG_HAS_WOL
				  | FLAG_HAS_HW_TIMESTAMP
				  | FLAG_APME_IN_CTRL3
				  | FLAG_HAS_SMART_POWER_DOWN
				  | FLAG_HAS_AMT
				  | FLAG_HAS_JUMBO_FRAMES
				  | FLAG_HAS_CTRLEXT_ON_LOAD,
	.flags2			= FLAG2_DISABLE_ASPM_L0S
				  | FLAG2_NO_DISABLE_RX,
	.pba			= 32,
	.max_hw_frame_size	= DEFAULT_JUMBO,
	.get_variants		= e1000_get_variants_82571,
	.mac_ops		= &e82571_mac_ops,
	.phy_ops		= &e82_phy_ops_bm,
	.nvm_ops		= &e82571_nvm_ops,
};

