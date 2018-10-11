// SPDX-License-Identifier: GPL-2.0
/* Copyright (c)  2018 Intel Corporation */

#include <linux/delay.h>

#include "igc_hw.h"
#include "igc_i225.h"
#include "igc_mac.h"
#include "igc_base.h"
#include "igc.h"

/**
 * igc_set_pcie_completion_timeout - set pci-e completion timeout
 * @hw: pointer to the HW structure
 */
static s32 igc_set_pcie_completion_timeout(struct igc_hw *hw)
{
	u32 gcr = rd32(IGC_GCR);
	u16 pcie_devctl2;
	s32 ret_val = 0;

	/* only take action if timeout value is defaulted to 0 */
	if (gcr & IGC_GCR_CMPL_TMOUT_MASK)
		goto out;

	/* if capabilities version is type 1 we can write the
	 * timeout of 10ms to 200ms through the GCR register
	 */
	if (!(gcr & IGC_GCR_CAP_VER2)) {
		gcr |= IGC_GCR_CMPL_TMOUT_10ms;
		goto out;
	}

	/* for version 2 capabilities we need to write the config space
	 * directly in order to set the completion timeout value for
	 * 16ms to 55ms
	 */
	ret_val = igc_read_pcie_cap_reg(hw, PCIE_DEVICE_CONTROL2,
					&pcie_devctl2);
	if (ret_val)
		goto out;

	pcie_devctl2 |= PCIE_DEVICE_CONTROL2_16ms;

	ret_val = igc_write_pcie_cap_reg(hw, PCIE_DEVICE_CONTROL2,
					 &pcie_devctl2);
out:
	/* disable completion timeout resend */
	gcr &= ~IGC_GCR_CMPL_TMOUT_RESEND;

	wr32(IGC_GCR, gcr);

	return ret_val;
}

/**
 * igc_check_for_link_base - Check for link
 * @hw: pointer to the HW structure
 *
 * If sgmii is enabled, then use the pcs register to determine link, otherwise
 * use the generic interface for determining link.
 */
static s32 igc_check_for_link_base(struct igc_hw *hw)
{
	s32 ret_val = 0;

	ret_val = igc_check_for_copper_link(hw);

	return ret_val;
}

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

	/* set the completion timeout for interface */
	ret_val = igc_set_pcie_completion_timeout(hw);
	if (ret_val)
		hw_dbg("PCI-E Set completion timeout has failed.\n");

	hw_dbg("Masking off all interrupts\n");
	wr32(IGC_IMC, 0xffffffff);

	wr32(IGC_RCTL, 0);
	wr32(IGC_TCTL, IGC_TCTL_PSP);
	wrfl();

	usleep_range(10000, 20000);

	ctrl = rd32(IGC_CTRL);

	hw_dbg("Issuing a global reset to MAC\n");
	wr32(IGC_CTRL, ctrl | IGC_CTRL_RST);

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

	return 0;
}

static s32 igc_get_invariants_base(struct igc_hw *hw)
{
	u32 link_mode = 0;
	u32 ctrl_ext = 0;
	s32 ret_val = 0;

	ctrl_ext = rd32(IGC_CTRL_EXT);
	link_mode = ctrl_ext & IGC_CTRL_EXT_LINK_MODE_MASK;

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

	if (ret_val)
		goto out;

out:
	return ret_val;
}

/**
 * igc_get_link_up_info_base - Get link speed/duplex info
 * @hw: pointer to the HW structure
 * @speed: stores the current speed
 * @duplex: stores the current duplex
 *
 * This is a wrapper function, if using the serial gigabit media independent
 * interface, use PCS to retrieve the link speed and duplex information.
 * Otherwise, use the generic function to get the link speed and duplex info.
 */
static s32 igc_get_link_up_info_base(struct igc_hw *hw, u16 *speed,
				     u16 *duplex)
{
	s32 ret_val;

	ret_val = igc_get_speed_and_duplex_copper(hw, speed, duplex);

	return ret_val;
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
 * igc_read_mac_addr_base - Read device MAC address
 * @hw: pointer to the HW structure
 */
static s32 igc_read_mac_addr_base(struct igc_hw *hw)
{
	s32 ret_val = 0;

	ret_val = igc_read_mac_addr(hw);

	return ret_val;
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
	.check_for_link		= igc_check_for_link_base,
	.rar_set		= igc_rar_set,
	.read_mac_addr		= igc_read_mac_addr_base,
	.get_speed_and_duplex	= igc_get_link_up_info_base,
};

const struct igc_info igc_base_info = {
	.get_invariants		= igc_get_invariants_base,
	.mac_ops		= &igc_mac_ops_base,
};
