// SPDX-License-Identifier: GPL-2.0
/* Copyright(c) 2020 - 2025 Mucse Corporation. */

#include <linux/pci.h>
#include <linux/errno.h>
#include <linux/etherdevice.h>
#include <linux/ethtool.h>

#include "rnpgbe.h"
#include "rnpgbe_hw.h"
#include "rnpgbe_mbx.h"
#include "rnpgbe_mbx_fw.h"

#define RNPGBE_DEFAULT_ADV	(RNPGBE_FW_ADV_10_FULL | \
				 RNPGBE_FW_ADV_100_FULL | \
				 RNPGBE_FW_ADV_1000_FULL | \
				 RNPGBE_FW_ADV_10_HALF | \
				 RNPGBE_FW_ADV_100_HALF)

/**
 * rnpgbe_get_permanent_mac - Get permanent mac
 * @hw: hw information structure
 * @perm_addr: pointer to store perm_addr
 *
 * rnpgbe_get_permanent_mac tries to get mac from hw
 *
 * Return: 0 on success, negative errno on failure
 **/
int rnpgbe_get_permanent_mac(struct mucse_hw *hw, u8 *perm_addr)
{
	struct device *dev = &hw->pdev->dev;
	int err;

	err = mucse_mbx_get_macaddr(hw, hw->pfvfnum, perm_addr, hw->port);
	if (err) {
		dev_err(dev, "Failed to get MAC from FW %d\n", err);
		return err;
	}

	if (!is_valid_ether_addr(perm_addr)) {
		dev_err(dev, "Failed to get valid MAC from FW\n");
		return -EINVAL;
	}

	return 0;
}

/**
 * rnpgbe_reset_hw - Do a hardware reset
 * @hw: hw information structure
 *
 * rnpgbe_reset_hw calls fw to do a hardware
 * reset, and cleans some regs to default.
 *
 * Return: 0 on success, negative errno on failure
 **/
int rnpgbe_reset_hw(struct mucse_hw *hw)
{
	mucse_hw_wr32(hw, RNPGBE_DMA_AXI_EN, 0);
	return mucse_mbx_reset_hw(hw);
}

/**
 * rnpgbe_setup_default_link - Configure the default link settings
 * @hw: hardware information structure
 *
 * Return: 0 on success, negative errno on failure
 **/
int rnpgbe_setup_default_link(struct mucse_hw *hw)
{
	return mucse_mbx_set_link(hw, RNPGBE_DEFAULT_ADV, true, 0, 0,
				  ETH_TP_MDI_AUTO);
}

/**
 * rnpgbe_send_notify - Echo fw status
 * @hw: hw information structure
 * @enable: true or false status
 * @mode: status mode
 *
 * Return: 0 on success, negative errno on failure
 **/
int rnpgbe_send_notify(struct mucse_hw *hw,
		       bool enable,
		       int mode)
{
	int err;

	switch (mode) {
	case mucse_fw_powerup:
		err = mucse_mbx_powerup(hw, enable);
		break;
	case mucse_fw_portup:
		err = mucse_mbx_phyup(hw, enable);
		break;
	case mucse_fw_link_report_en:
		err = mucse_mbx_link_report(hw, enable);
		break;
	default:
		err = -EINVAL;
	}

	return err;
}

/**
 * rnpgbe_init_n500 - Setup n500 hw info
 * @hw: hw information structure
 *
 * rnpgbe_init_n500 initializes all private
 * structure for n500
 **/
static void rnpgbe_init_n500(struct mucse_hw *hw)
{
	struct mucse_mbx_info *mbx = &hw->mbx;

	hw->ring_msix_base = hw->hw_addr + MUCSE_N500_RING_MSIX_BASE;

	mbx->fwpf_ctrl_base = MUCSE_N500_FWPF_CTRL_BASE;
	mbx->fwpf_shm_base = MUCSE_N500_FWPF_SHM_BASE;

	hw->cycles_per_us = M_DEFAULT_N500_MHZ;
}

/**
 * rnpgbe_init_n210 - Setup n210 hw info
 * @hw: hw information structure
 *
 * rnpgbe_init_n210 initializes all private
 * structure for n210
 **/
static void rnpgbe_init_n210(struct mucse_hw *hw)
{
	struct mucse_mbx_info *mbx = &hw->mbx;

	hw->ring_msix_base = hw->hw_addr + MUCSE_N210_RING_MSIX_BASE;

	mbx->fwpf_ctrl_base = MUCSE_N210_FWPF_CTRL_BASE;
	mbx->fwpf_shm_base = MUCSE_N210_FWPF_SHM_BASE;

	hw->cycles_per_us = M_DEFAULT_N210_MHZ;
}

/**
 * rnpgbe_init_hw - Setup hw info according to board_type
 * @hw: hw information structure
 * @board_type: board type
 *
 * rnpgbe_init_hw initializes all hw data
 *
 * Return: 0 on success, -EINVAL on failure
 **/
int rnpgbe_init_hw(struct mucse_hw *hw, int board_type)
{
	struct mucse_mbx_info *mbx = &hw->mbx;

	hw->port = 0;

	mbx->pf2fw_mbx_ctrl = MUCSE_GBE_PFFW_MBX_CTRL_OFFSET;
	mbx->fwpf_mbx_mask = MUCSE_GBE_FWPF_MBX_MASK_OFFSET;

	switch (board_type) {
	case board_n500:
		rnpgbe_init_n500(hw);
		break;
	case board_n210:
		rnpgbe_init_n210(hw);
		break;
	default:
		return -EINVAL;
	}
	/* init_params with mbx base */
	mucse_init_mbx_params_pf(hw);

	return 0;
}

static void rnpgbe_set_rar(struct mucse_hw *hw, u32 index, const u8 *addr)
{
	u32 old_low, old_high;
	u32 rar_low, rar_high;

	/* The RAR stores the Ethernet address in reverse byte order. */
	rar_low = (u32)addr[5] | ((u32)addr[4] << 8) |
		  ((u32)addr[3] << 16) | ((u32)addr[2] << 24);
	rar_high = (u32)addr[1] | ((u32)addr[0] << 8) |
		   RNPGBE_RX_RAR_VALID;
	old_low = mucse_hw_rd32(hw, RNPGBE_RX_RAR_LOW(index));
	old_high = mucse_hw_rd32(hw, RNPGBE_RX_RAR_HIGH(index));
	if (old_low == rar_low && old_high == rar_high)
		return;

	mucse_hw_wr32(hw, RNPGBE_RX_RAR_HIGH(index),
		      rar_high & ~RNPGBE_RX_RAR_VALID);
	mucse_hw_wr32(hw, RNPGBE_RX_RAR_LOW(index), rar_low);
	mucse_hw_wr32(hw, RNPGBE_RX_RAR_HIGH(index), rar_high);
}

static void rnpgbe_clear_rar(struct mucse_hw *hw, u32 index)
{
	u32 rar_high;

	rar_high = mucse_hw_rd32(hw, RNPGBE_RX_RAR_HIGH(index));
	if (!(rar_high & RNPGBE_RX_RAR_VALID))
		return;

	mucse_hw_wr32(hw, RNPGBE_RX_RAR_HIGH(index), 0);
	mucse_hw_wr32(hw, RNPGBE_RX_RAR_LOW(index), 0);
}

int rnpgbe_set_rx_mode(struct net_device *netdev,
		       struct netdev_hw_addr_list *uc,
		       struct netdev_hw_addr_list *mc)
{
	u32 mcast_hash[RNPGBE_RX_MCAST_HASH_ENTRIES] = {};
	u32 mcast_ctrl = RNPGBE_RX_UCAST_TABLE_EN;
	u32 filter_ctrl = RNPGBE_RX_FILTER_BCAST;
	struct netdev_hw_addr *ha;
	struct mucse_hw *hw;
	struct mucse *mucse;
	int rar = 1;
	u16 hash;
	int i;

	mucse = netdev_priv(netdev);
	hw = &mucse->hw;

	/* RAR 0 always holds the interface's primary unicast address. */
	rnpgbe_set_rar(hw, 0, netdev->dev_addr);

	netdev_hw_addr_list_for_each(ha, uc) {
		if (rar == RNPGBE_RX_RAR_ENTRIES) {
			filter_ctrl |= RNPGBE_RX_FILTER_UCAST_ALL;
			break;
		}

		rnpgbe_set_rar(hw, rar, ha->addr);
		rar++;
	}

	for (; rar < RNPGBE_RX_RAR_ENTRIES; rar++)
		rnpgbe_clear_rar(hw, rar);

	if (netdev->flags & IFF_PROMISC) {
		filter_ctrl |= RNPGBE_RX_FILTER_UCAST_ALL |
			       RNPGBE_RX_FILTER_MCAST_ALL;
	} else if (netdev->flags & IFF_ALLMULTI) {
		filter_ctrl |= RNPGBE_RX_FILTER_MCAST_ALL;
	} else {
		/* MTA type 0 uses ((addr[4] << 8) | addr[5]) & 0xfff. */
		netdev_hw_addr_list_for_each(ha, mc) {
			hash = ((ha->addr[4] & 0xf) << 8) | ha->addr[5];
			mcast_hash[hash >> 5] |= BIT(hash & 0x1f);
		}

		if (!netdev_hw_addr_list_empty(mc))
			mcast_ctrl |= RNPGBE_RX_MCAST_HASH_EN;
	}

	/* Widen multicast filtering before invalidating hash entries. */
	if (filter_ctrl & RNPGBE_RX_FILTER_MCAST_ALL)
		mucse_hw_wr32(hw, RNPGBE_RX_FILTER_CTRL, filter_ctrl);

	for (i = 0; i < RNPGBE_RX_MCAST_HASH_ENTRIES; i++)
		mucse_hw_wr32(hw, RNPGBE_RX_MCAST_HASH(i), mcast_hash[i]);

	mucse_hw_wr32(hw, RNPGBE_RX_MCAST_CTRL, mcast_ctrl);
	if (!(filter_ctrl & RNPGBE_RX_FILTER_MCAST_ALL))
		mucse_hw_wr32(hw, RNPGBE_RX_FILTER_CTRL, filter_ctrl);

	return 0;
}

/**
 * rnpgbe_set_link - Set the hardware link state
 * @hw: hw information structure
 * @linkup: link on or not
 *
 * Apply the firmware-reported link state to the GMAC. This helper is only
 * called while processing a firmware link event. The caller must hold the
 * containing struct mucse's link_lock.
 * Firmware updates the speed and duplex fields before posting a link event
 * and waits for the PF acknowledgment, so event handling completes this
 * read-modify-write before firmware accesses GMAC_CONTROL again.
 *
 **/
void rnpgbe_set_link(struct mucse_hw *hw, bool linkup)
{
	struct mucse *mucse = container_of(hw, struct mucse, hw);
	u32 value;

	lockdep_assert_held(&mucse->link_lock);
	value = mucse_hw_rd32(hw, GMAC_CONTROL);

	/* ndo_set_rx_mode_async() programs the chip-level filter. Keep the GMAC
	 * in receive-all mode so it does not discard frames accepted by that
	 * filter.
	 */
	if (linkup) {
		mucse_hw_wr32(hw, GMAC_FRAME_FILTER, GMAC_RX_ALL);
		value |= GMAC_CONTROL_RE;
		mucse_hw_wr32(hw, GMAC_CONTROL, value);
	} else {
		value &= ~GMAC_CONTROL_RE;
		mucse_hw_wr32(hw, GMAC_CONTROL, value);
		mucse_hw_wr32(hw, GMAC_FRAME_FILTER, 0);
	}
}
