// SPDX-License-Identifier: GPL-2.0
/* Copyright(c) 2020 - 2025 Mucse Corporation. */

#include <linux/pci.h>
#include <linux/errno.h>
#include <linux/etherdevice.h>

#include "rnpgbe.h"
#include "rnpgbe_hw.h"
#include "rnpgbe_mbx.h"
#include "rnpgbe_mbx_fw.h"

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
	/* Keep switch struct to support more modes in the future */
	switch (mode) {
	case mucse_fw_powerup:
		err = mucse_mbx_powerup(hw, enable);
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

	mbx->fwpf_ctrl_base = MUCSE_N500_FWPF_CTRL_BASE;
	mbx->fwpf_shm_base = MUCSE_N500_FWPF_SHM_BASE;
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

	mbx->fwpf_ctrl_base = MUCSE_N210_FWPF_CTRL_BASE;
	mbx->fwpf_shm_base = MUCSE_N210_FWPF_SHM_BASE;
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
