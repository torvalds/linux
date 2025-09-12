// SPDX-License-Identifier: GPL-2.0
// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.

#include "hinic3_hw_comm.h"
#include "hinic3_hwdev.h"
#include "hinic3_hwif.h"
#include "hinic3_mbox.h"
#include "hinic3_mgmt.h"

#define HINIC3_HWDEV_WQ_NAME    "hinic3_hardware"
#define HINIC3_WQ_MAX_REQ       10

enum hinic3_hwdev_init_state {
	HINIC3_HWDEV_MBOX_INITED = 2,
	HINIC3_HWDEV_CMDQ_INITED = 3,
};

int hinic3_init_hwdev(struct pci_dev *pdev)
{
	struct hinic3_pcidev *pci_adapter = pci_get_drvdata(pdev);
	struct hinic3_hwdev *hwdev;
	int err;

	hwdev = kzalloc(sizeof(*hwdev), GFP_KERNEL);
	if (!hwdev)
		return -ENOMEM;

	pci_adapter->hwdev = hwdev;
	hwdev->adapter = pci_adapter;
	hwdev->pdev = pci_adapter->pdev;
	hwdev->dev = &pci_adapter->pdev->dev;
	hwdev->func_state = 0;
	spin_lock_init(&hwdev->channel_lock);

	err = hinic3_init_hwif(hwdev);
	if (err) {
		dev_err(hwdev->dev, "Failed to init hwif\n");
		goto err_free_hwdev;
	}

	hwdev->workq = alloc_workqueue(HINIC3_HWDEV_WQ_NAME, WQ_MEM_RECLAIM,
				       HINIC3_WQ_MAX_REQ);
	if (!hwdev->workq) {
		dev_err(hwdev->dev, "Failed to alloc hardware workq\n");
		err = -ENOMEM;
		goto err_free_hwif;
	}

	return 0;

err_free_hwif:
	hinic3_free_hwif(hwdev);
err_free_hwdev:
	pci_adapter->hwdev = NULL;
	kfree(hwdev);

	return err;
}

void hinic3_free_hwdev(struct hinic3_hwdev *hwdev)
{
	destroy_workqueue(hwdev->workq);
	hinic3_free_hwif(hwdev);
	kfree(hwdev);
}

void hinic3_set_api_stop(struct hinic3_hwdev *hwdev)
{
	/* Completed by later submission due to LoC limit. */
}
