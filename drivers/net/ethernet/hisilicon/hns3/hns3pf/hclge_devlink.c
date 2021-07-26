// SPDX-License-Identifier: GPL-2.0+
/* Copyright (c) 2021 Hisilicon Limited. */

#include <net/devlink.h>

#include "hclge_devlink.h"

static int hclge_devlink_info_get(struct devlink *devlink,
				  struct devlink_info_req *req,
				  struct netlink_ext_ack *extack)
{
#define	HCLGE_DEVLINK_FW_STRING_LEN	32
	struct hclge_devlink_priv *priv = devlink_priv(devlink);
	char version_str[HCLGE_DEVLINK_FW_STRING_LEN];
	struct hclge_dev *hdev = priv->hdev;
	int ret;

	ret = devlink_info_driver_name_put(req, KBUILD_MODNAME);
	if (ret)
		return ret;

	snprintf(version_str, sizeof(version_str), "%lu.%lu.%lu.%lu",
		 hnae3_get_field(hdev->fw_version, HNAE3_FW_VERSION_BYTE3_MASK,
				 HNAE3_FW_VERSION_BYTE3_SHIFT),
		 hnae3_get_field(hdev->fw_version, HNAE3_FW_VERSION_BYTE2_MASK,
				 HNAE3_FW_VERSION_BYTE2_SHIFT),
		 hnae3_get_field(hdev->fw_version, HNAE3_FW_VERSION_BYTE1_MASK,
				 HNAE3_FW_VERSION_BYTE1_SHIFT),
		 hnae3_get_field(hdev->fw_version, HNAE3_FW_VERSION_BYTE0_MASK,
				 HNAE3_FW_VERSION_BYTE0_SHIFT));

	return devlink_info_version_running_put(req,
						DEVLINK_INFO_VERSION_GENERIC_FW,
						version_str);
}

static const struct devlink_ops hclge_devlink_ops = {
	.info_get = hclge_devlink_info_get,
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
