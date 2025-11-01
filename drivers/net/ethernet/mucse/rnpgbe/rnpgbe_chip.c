// SPDX-License-Identifier: GPL-2.0
/* Copyright(c) 2020 - 2025 Mucse Corporation. */

#include <linux/errno.h>

#include "rnpgbe.h"
#include "rnpgbe_hw.h"
#include "rnpgbe_mbx.h"

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
