// SPDX-License-Identifier: GPL-2.0
// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.

#include <linux/delay.h>

#include "hinic3_hw_comm.h"
#include "hinic3_hwdev.h"
#include "hinic3_hwif.h"
#include "hinic3_mbox.h"

int hinic3_set_interrupt_cfg_direct(struct hinic3_hwdev *hwdev,
				    const struct hinic3_interrupt_info *info)
{
	struct comm_cmd_cfg_msix_ctrl_reg msix_cfg = {};
	struct mgmt_msg_params msg_params = {};
	int err;

	msix_cfg.func_id = hinic3_global_func_id(hwdev);
	msix_cfg.msix_index = info->msix_index;
	msix_cfg.opcode = MGMT_MSG_CMD_OP_SET;

	msix_cfg.lli_credit_cnt = info->lli_credit_limit;
	msix_cfg.lli_timer_cnt = info->lli_timer_cfg;
	msix_cfg.pending_cnt = info->pending_limit;
	msix_cfg.coalesce_timer_cnt = info->coalesc_timer_cfg;
	msix_cfg.resend_timer_cnt = info->resend_timer_cfg;

	mgmt_msg_params_init_default(&msg_params, &msix_cfg, sizeof(msix_cfg));

	err = hinic3_send_mbox_to_mgmt(hwdev, MGMT_MOD_COMM,
				       COMM_CMD_CFG_MSIX_CTRL_REG, &msg_params);
	if (err || msix_cfg.head.status) {
		dev_err(hwdev->dev,
			"Failed to set interrupt config, err: %d, status: 0x%x\n",
			err, msix_cfg.head.status);
		return -EINVAL;
	}

	return 0;
}

int hinic3_func_reset(struct hinic3_hwdev *hwdev, u16 func_id, u64 reset_flag)
{
	struct comm_cmd_func_reset func_reset = {};
	struct mgmt_msg_params msg_params = {};
	int err;

	func_reset.func_id = func_id;
	func_reset.reset_flag = reset_flag;

	mgmt_msg_params_init_default(&msg_params, &func_reset,
				     sizeof(func_reset));

	err = hinic3_send_mbox_to_mgmt(hwdev, MGMT_MOD_COMM,
				       COMM_CMD_FUNC_RESET, &msg_params);
	if (err || func_reset.head.status) {
		dev_err(hwdev->dev, "Failed to reset func resources, reset_flag 0x%llx, err: %d, status: 0x%x\n",
			reset_flag, err, func_reset.head.status);
		return -EIO;
	}

	return 0;
}
