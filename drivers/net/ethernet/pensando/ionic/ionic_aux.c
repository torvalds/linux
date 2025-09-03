// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) 2018-2025, Advanced Micro Devices, Inc. */

#include <linux/kernel.h>
#include "ionic.h"
#include "ionic_lif.h"
#include "ionic_aux.h"

static DEFINE_IDA(aux_ida);

static void ionic_auxbus_release(struct device *dev)
{
	struct ionic_aux_dev *ionic_adev;

	ionic_adev = container_of(dev, struct ionic_aux_dev, adev.dev);
	ida_free(&aux_ida, ionic_adev->adev.id);
	kfree(ionic_adev);
}

int ionic_auxbus_register(struct ionic_lif *lif)
{
	struct ionic_aux_dev *ionic_adev;
	struct auxiliary_device *aux_dev;
	int err, id;

	if (!(le64_to_cpu(lif->ionic->ident.lif.capabilities) & IONIC_LIF_CAP_RDMA))
		return 0;

	ionic_adev = kzalloc(sizeof(*ionic_adev), GFP_KERNEL);
	if (!ionic_adev)
		return -ENOMEM;

	aux_dev = &ionic_adev->adev;

	id = ida_alloc(&aux_ida, GFP_KERNEL);
	if (id < 0) {
		dev_err(lif->ionic->dev, "Failed to allocate aux id: %d\n", id);
		kfree(ionic_adev);
		return id;
	}

	aux_dev->id = id;
	aux_dev->name = "rdma";
	aux_dev->dev.parent = &lif->ionic->pdev->dev;
	aux_dev->dev.release = ionic_auxbus_release;
	ionic_adev->lif = lif;
	err = auxiliary_device_init(aux_dev);
	if (err) {
		dev_err(lif->ionic->dev, "Failed to initialize %s aux device: %d\n",
			aux_dev->name, err);
		ida_free(&aux_ida, id);
		kfree(ionic_adev);
		return err;
	}

	err = auxiliary_device_add(aux_dev);
	if (err) {
		dev_err(lif->ionic->dev, "Failed to add %s aux device: %d\n",
			aux_dev->name, err);
		auxiliary_device_uninit(aux_dev);
		return err;
	}

	lif->ionic_adev = ionic_adev;
	return 0;
}

void ionic_auxbus_unregister(struct ionic_lif *lif)
{
	mutex_lock(&lif->adev_lock);
	if (!lif->ionic_adev)
		goto out;

	auxiliary_device_delete(&lif->ionic_adev->adev);
	auxiliary_device_uninit(&lif->ionic_adev->adev);

	lif->ionic_adev = NULL;
out:
	mutex_unlock(&lif->adev_lock);
}

void ionic_request_rdma_reset(struct ionic_lif *lif)
{
	struct ionic *ionic = lif->ionic;
	int err;

	union ionic_dev_cmd cmd = {
		.cmd.opcode = IONIC_CMD_RDMA_RESET_LIF,
		.cmd.lif_index = cpu_to_le16(lif->index),
	};

	mutex_lock(&ionic->dev_cmd_lock);

	ionic_dev_cmd_go(&ionic->idev, &cmd);
	err = ionic_dev_cmd_wait(ionic, DEVCMD_TIMEOUT);

	mutex_unlock(&ionic->dev_cmd_lock);

	if (err)
		pr_warn("%s request_reset: error %d\n", __func__, err);
}
EXPORT_SYMBOL_NS(ionic_request_rdma_reset, "NET_IONIC");
