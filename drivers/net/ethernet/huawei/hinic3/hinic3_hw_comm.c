// SPDX-License-Identifier: GPL-2.0
// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.

#include <linux/delay.h>

#include "hinic3_hw_comm.h"
#include "hinic3_hwdev.h"
#include "hinic3_hwif.h"
#include "hinic3_mbox.h"

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
