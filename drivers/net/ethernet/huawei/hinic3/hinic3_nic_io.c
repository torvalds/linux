// SPDX-License-Identifier: GPL-2.0
// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.

#include "hinic3_hw_comm.h"
#include "hinic3_hw_intf.h"
#include "hinic3_hwdev.h"
#include "hinic3_hwif.h"
#include "hinic3_nic_cfg.h"
#include "hinic3_nic_dev.h"
#include "hinic3_nic_io.h"

int hinic3_init_nic_io(struct hinic3_nic_dev *nic_dev)
{
	struct hinic3_hwdev *hwdev = nic_dev->hwdev;
	struct hinic3_nic_io *nic_io;
	int err;

	nic_io = kzalloc(sizeof(*nic_io), GFP_KERNEL);
	if (!nic_io)
		return -ENOMEM;

	nic_dev->nic_io = nic_io;

	err = hinic3_set_func_svc_used_state(hwdev, COMM_FUNC_SVC_T_NIC, 1);
	if (err) {
		dev_err(hwdev->dev, "Failed to set function svc used state\n");
		goto err_free_nicio;
	}

	err = hinic3_init_function_table(nic_dev);
	if (err) {
		dev_err(hwdev->dev, "Failed to init function table\n");
		goto err_clear_func_svc_used_state;
	}

	nic_io->rx_buf_len = nic_dev->rx_buf_len;

	err = hinic3_get_nic_feature_from_hw(nic_dev);
	if (err) {
		dev_err(hwdev->dev, "Failed to get nic features\n");
		goto err_clear_func_svc_used_state;
	}

	nic_io->feature_cap &= HINIC3_NIC_F_ALL_MASK;
	nic_io->feature_cap &= HINIC3_NIC_DRV_DEFAULT_FEATURE;
	dev_dbg(hwdev->dev, "nic features: 0x%llx\n\n", nic_io->feature_cap);

	return 0;

err_clear_func_svc_used_state:
	hinic3_set_func_svc_used_state(hwdev, COMM_FUNC_SVC_T_NIC, 0);
err_free_nicio:
	nic_dev->nic_io = NULL;
	kfree(nic_io);

	return err;
}

void hinic3_free_nic_io(struct hinic3_nic_dev *nic_dev)
{
	struct hinic3_nic_io *nic_io = nic_dev->nic_io;

	hinic3_set_func_svc_used_state(nic_dev->hwdev, COMM_FUNC_SVC_T_NIC, 0);
	nic_dev->nic_io = NULL;
	kfree(nic_io);
}
