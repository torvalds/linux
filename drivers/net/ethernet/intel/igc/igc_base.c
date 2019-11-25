// SPDX-License-Identifier: GPL-2.0
/* Copyright (c)  2018 Intel Corporation */

#include <linux/delay.h>

#include "igc_hw.h"
#include "igc_i225.h"
#include "igc_mac.h"
#include "igc_base.h"
#include "igc.h"

/**
 * igc_reset_hw_base - Reset hardware
 * @hw: pointer to the HW structure
 *
 * This resets the hardware into a known state.  This is a
 * function pointer entry point called by the api module.
 */
static s32 igc_reset_hw_base(struct igc_hw *hw)
{
	s32 ret_val;
	u32 ctrl;

	/* Prevent the PCI-E bus from sticking if there is no TLP connection
	 * on the last TLP read/write transaction when MAC is reset.
	 */
	ret_val = igc_disable_pcie_master(hw);
	if (ret_val)
		hw_dbg("PCI-E Master disable polling has failed.\n");

	hw_dbg("Masking off all interrupts\n");
	wr32(IGC_IMC, 0xffffffff);

	wr32(IGC_RCTL, 0);
	wr32(IGC_TCTL, IGC_TCTL_PSP);
	wrfl();

	usleep_range(10000, 20000);

	ctrl = rd32(IGC_CTRL);

	hw_dbg("Issuing a global reset to MAC\n");
	wr32(IGC_CTRL, ctrl | IGC_CTRL_DEV_RST);

	ret_val = igc_get_auto_rd_done(hw);
	if (ret_val) {
		/* When auto config read does not complete, do not
		 * return with an error. This can happen in situations
		 * where there is no eeprom and prevents getting link.
		 */
		hw_dbg("Auto Read Done did not complete\n");
	}

	/* Clear any pending interrupt events. */
	wr32(IGC_IMC, 0xffffffff);
	rd32(IGC_ICR);

	return ret_val;
}

/**
 * igc_init_nvm_params_base - Init NVM func ptrs.
 * @hw: pointer to the HW structure
 */
static s32 igc_init_nvm_params_base(struct igc_hw *hw)
{
	struct igc_nvm_info *nvm = &hw->nvm;
	u32 eecd = rd32(IGC_EECD);
	u16 size;

	size = (u16)((eecd & IGC_EECD_SIZE_EX_MASK) >>
		     IGC_EECD_SIZE_EX_SHIFT);

	/* Added to a constant, "size" becomes the left-shift value
	 * for setting word_size.
	 */
	size += NVM_WORD_SIZE_BASE_SHIFT;

	/* Just in case size is out of range, cap it to the largest
	 * EEPROM size supported
	 */
	if (size > 15)
		size = 15;

	nvm->type = igc_nvm_eeprom_spi;
	nvm->word_size = BIT(size);
	nvm->opcode_bits = 8;
	nvm->delay_usec = 1;

	nvm->page_size = eecd & IGC_EECD_ADDR_BITS ? 32 : 8;
	nvm->address_bits = eecd & IGC_EECD_ADDR_BITS ?
			    16 : 8;

	if (nvm->word_size == BIT(15))
		nvm->page_size = 128;

	return 0;
}

/**
 * igc_setup_copper_link_base - Configure copper link settings
 * @hw: pointer to the HW structure
 *
 * Configures the link for auto-neg or forced speed and duplex.  Then we check
 * for link, once link is established calls to configure collision distance
 * and flow control are called.
 */
static s32 igc_setup_copper_link_base(struct igc_hw *hw)
{
	s32  ret_val = 0;
	u32 ctrl;

	ctrl = rd32(IGC_CTRL);
	ctrl |= IGC_CTRL_SLU;
	ctrl &= ~(IGC_CTRL_FRCSPD | IGC_CTRL_FRCDPX);
	wr32(IGC_CTRL, ctrl);

	ret_val = igc_setup_copper_link(hw);

	return ret_val;
}

/**
 * igc_init_mac_params_base - Init MAC func ptrs.
 * @hw: pointer to the HW structure
 */
static s32 igc_init_mac_params_base(struct igc_hw *hw)
{
	struct igc_dev_spec_base *dev_spec = &hw->dev_spec._base;
	struct igc_mac_info *mac = &hw->mac;

	/* Set mta register count */
	mac->mta_reg_count = 128;
	mac->rar_entry_count = IGC_RAR_ENTRIES;

	/* reset */
	mac->ops.reset_hw = igc_reset_hw_base;

	mac->ops.acquire_swfw_sync = igc_acquire_swfw_sync_i225;
	mac->ops.release_swfw_sync = igc_release_swfw_sync_i225;

	/* Allow a single clear of the SW semaphore on I225 */
	if (mac->type == igc_i225)
		dev_spec->clear_semaphore_once = true;

	/* physical interface link setup */
	mac->ops.setup_physical_interface = igc_setup_copper_link_base;

	return 0;
}

/**
 * igc_init_phy_params_base - Init PHY func ptrs.
 * @hw: pointer to the HW structure
 */
static s32 igc_init_phy_params_base(struct igc_hw *hw)
{
	struct igc_phy_info *phy = &hw->phy;
	s32 ret_val = 0;

	if (hw->phy.media_type != igc_media_type_copper) {
		phy->type = igc_phy_none;
		goto out;
	}

	phy->autoneg_mask	= AUTONEG_ADVERTISE_SPEED_DEFAULT_2500;
	phy->reset_delay_us	= 100;

	/* set lan id */
	hw->bus.func = (rd32(IGC_STATUS) & IGC_STATUS_FUNC_MASK) >>
			IGC_STATUS_FUNC_SHIFT;

	/* Make sure the PHY is in a good state. Several people have reported
	 * firmware leaving the PHY's page select register set to something
	 * other than the default of zero, which causes the PHY ID read to
	 * access something other than the intended register.
	 */
	ret_val = hw->phy.ops.reset(hw);
	if (ret_val) {
		hw_dbg("Error resetting the PHY.\n");
		goto out;
	}

	ret_val = igc_get_phy_id(hw);
	if (ret_val)
		return ret_val;

	igc_check_for_copper_link(hw);

	/* Verify phy id and set remaining function pointers */
	switch (phy->id) {
	case I225_I_PHY_ID:
		phy->type	= igc_phy_i225;
		break;
	default:
		ret_val = -IGC_ERR_PHY;
		goto out;
	}

out:
	return ret_val;
}

static s32 igc_get_invariants_base(struct igc_hw *hw)
{
	struct igc_mac_info *mac = &hw->mac;
	s32 ret_val = 0;

	switch (hw->device_id) {
	case IGC_DEV_ID_I225_LM:
	case IGC_DEV_ID_I225_V:
	case IGC_DEV_ID_I225_I:
	case IGC_DEV_ID_I220_V:
	case IGC_DEV_ID_I225_K:
		mac->type = igc_i225;
		break;
	default:
		return -IGC_ERR_MAC_INIT;
	}

	hw->phy.media_type = igc_media_type_copper;

	/* mac initialization and operations */
	ret_val = igc_init_mac_params_base(hw);
	if (ret_val)
		goto out;

	/* NVM initialization */
	ret_val = igc_init_nvm_params_base(hw);
	switch (hw->mac.type) {
	case igc_i225:
		ret_val = igc_init_nvm_params_i225(hw);
		break;
	default:
		break;
	}

	/* setup PHY parameters */
	ret_val = igc_init_phy_params_base(hw);
	if (ret_val)
		goto out;

out:
	return ret_val;
}

/**
 * igc_acquire_phy_base - Acquire rights to access PHY
 * @hw: pointer to the HW structure
 *
 * Acquire access rights to the correct PHY.  This is a
 * function pointer entry point called by the api module.
 */
static s32 igc_acquire_phy_base(struct igc_hw *hw)
{
	u16 mask = IGC_SWFW_PHY0_SM;

	return hw->mac.ops.acquire_swfw_sync(hw, mask);
}

/**
 * igc_release_phy_base - Release rights to access PHY
 * @hw: pointer to the HW structure
 *
 * A wrapper to release access rights to the correct PHY.  This is a
 * function pointer entry point called by the api module.
 */
static void igc_release_phy_base(struct igc_hw *hw)
{
	u16 mask = IGC_SWFW_PHY0_SM;

	hw->mac.ops.release_swfw_sync(hw, mask);
}

/**
 * igc_init_hw_base - Initialize hardware
 * @hw: pointer to the HW structure
 *
 * This inits the hardware readying it for operation.
 */
static s32 igc_init_hw_base(struct igc_hw *hw)
{
	struct igc_mac_info *mac = &hw->mac;
	u16 i, rar_count = mac->rar_entry_count;
	s32 ret_val = 0;

	/* Setup the receive address */
	igc_init_rx_addrs(hw, rar_count);

	/* Zero out the Multicast HASH table */
	hw_dbg("Zeroing the MTA\n");
	for (i = 0; i < mac->mta_reg_count; i++)
		array_wr32(IGC_MTA, i, 0);

	/* Zero out the Unicast HASH table */
	hw_dbg("Zeroing the UTA\n");
	for (i = 0; i < mac->uta_reg_count; i++)
		array_wr32(IGC_UTA, i, 0);

	/* Setup link and flow control */
	ret_val = igc_setup_link(hw);

	/* Clear all of the statistics registers (clear on read).  It is
	 * important that we do this after we have tried to establish link
	 * because the symbol error count will increment wildly if there
	 * is no link.
	 */
	igc_clear_hw_cntrs_base(hw);

	return ret_val;
}

/**
 * igc_power_down_phy_copper_base - Remove link during PHY power down
 * @hw: pointer to the HW structure
 *
 * In the case of a PHY power down to save power, or to turn off link during a
 * driver unload, or wake on lan is not enabled, remove the link.
 */
void igc_power_down_phy_copper_base(struct igc_hw *hw)
{
	/* If the management interface is not enabled, then power down */
	if (!(igc_enable_mng_pass_thru(hw) || igc_check_reset_block(hw)))
		igc_power_down_phy_copper(hw);
}

/**
 * igc_rx_fifo_flush_base - Clean rx fifo after Rx enable
 * @hw: pointer to the HW structure
 *
 * After Rx enable, if manageability is enabled then there is likely some
 * bad data at the start of the fifo and possibly in the DMA fifo.  This
 * function clears the fifos and flushes any packets that came in as rx was
 * being enabled.
 */
void igc_rx_fifo_flush_base(struct igc_hw *hw)
{
	u32 rctl, rlpml, rxdctl[4], rfctl, temp_rctl, rx_enabled;
	int i, ms_wait;

	/* disable IPv6 options as per hardware errata */
	rfctl = rd32(IGC_RFCTL);
	rfctl |= IGC_RFCTL_IPV6_EX_DIS;
	wr32(IGC_RFCTL, rfctl);

	if (!(rd32(IGC_MANC) & IGC_MANC_RCV_TCO_EN))
		return;

	/* Disable all Rx queues */
	for (i = 0; i < 4; i++) {
		rxdctl[i] = rd32(IGC_RXDCTL(i));
		wr32(IGC_RXDCTL(i),
		     rxdctl[i] & ~IGC_RXDCTL_QUEUE_ENABLE);
	}
	/* Poll all queues to verify they have shut down */
	for (ms_wait = 0; ms_wait < 10; ms_wait++) {
		usleep_range(1000, 2000);
		rx_enabled = 0;
		for (i = 0; i < 4; i++)
			rx_enabled |= rd32(IGC_RXDCTL(i));
		if (!(rx_enabled & IGC_RXDCTL_QUEUE_ENABLE))
			break;
	}

	if (ms_wait == 10)
		pr_debug("Queue disable timed out after 10ms\n");

	/* Clear RLPML, RCTL.SBP, RFCTL.LEF, and set RCTL.LPE so that all
	 * incoming packets are rejected.  Set enable and wait 2ms so that
	 * any packet that was coming in as RCTL.EN was set is flushed
	 */
	wr32(IGC_RFCTL, rfctl & ~IGC_RFCTL_LEF);

	rlpml = rd32(IGC_RLPML);
	wr32(IGC_RLPML, 0);

	rctl = rd32(IGC_RCTL);
	temp_rctl = rctl & ~(IGC_RCTL_EN | IGC_RCTL_SBP);
	temp_rctl |= IGC_RCTL_LPE;

	wr32(IGC_RCTL, temp_rctl);
	wr32(IGC_RCTL, temp_rctl | IGC_RCTL_EN);
	wrfl();
	usleep_range(2000, 3000);

	/* Enable Rx queues that were previously enabled and restore our
	 * previous state
	 */
	for (i = 0; i < 4; i++)
		wr32(IGC_RXDCTL(i), rxdctl[i]);
	wr32(IGC_RCTL, rctl);
	wrfl();

	wr32(IGC_RLPML, rlpml);
	wr32(IGC_RFCTL, rfctl);

	/* Flush receive errors generated by workaround */
	rd32(IGC_ROC);
	rd32(IGC_RNBC);
	rd32(IGC_MPC);
}

static struct igc_mac_operations igc_mac_ops_base = {
	.init_hw		= igc_init_hw_base,
	.check_for_link		= igc_check_for_copper_link,
	.rar_set		= igc_rar_set,
	.read_mac_addr		= igc_read_mac_addr,
	.get_speed_and_duplex	= igc_get_speed_and_duplex_copper,
};

static const struct igc_phy_operations igc_phy_ops_base = {
	.acquire		= igc_acquire_phy_base,
	.release		= igc_release_phy_base,
	.reset			= igc_phy_hw_reset,
	.read_reg		= igc_read_phy_reg_gpy,
	.write_reg		= igc_write_phy_reg_gpy,
};

const struct igc_info igc_base_info = {
	.get_invariants		= igc_get_invariants_base,
	.mac_ops		= &igc_mac_ops_base,
	.phy_ops		= &igc_phy_ops_base,
};
