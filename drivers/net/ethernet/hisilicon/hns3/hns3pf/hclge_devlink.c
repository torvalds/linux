// SPDX-License-Identifier: GPL-2.0+
/* Copyright (c) 2021 Hisilicon Limited. */

#include <net/devlink.h>

#include "hclge_devlink.h"

static const struct devlink_ops hclge_devlink_ops = {
};

int hclge_devlink_init(struct hclge_dev *hdev)
{
	struct pci_dev *pdev = hdev->pdev;
	struct hclge_devlink_priv *priv;
	struct devlink *devlink;
	int ret;

	devlink = devlink_alloc(&hclge_devlink_ops,
				sizeof(struct hclge_devlink_priv));
	if (!devlink)
		return -ENOMEM;

	priv = devlink_priv(devlink);
	priv->hdev = hdev;

	ret = devlink_register(devlink, &pdev->dev);
	if (ret) {
		dev_err(&pdev->dev, "failed to register devlink, ret = %d\n",
			ret);
		goto out_reg_fail;
	}

	hdev->devlink = devlink;

	return 0;

out_reg_fail:
	devlink_free(devlink);
	return ret;
}

void hclge_devlink_uninit(struct hclge_dev *hdev)
{
	struct devlink *devlink = hdev->devlink;

	if (!devlink)
		return;

	devlink_unregister(devlink);

	devlink_free(devlink);

	hdev->devlink = NULL;
}
