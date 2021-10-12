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

static int hclge_devlink_reload_down(struct devlink *devlink, bool netns_change,
				     enum devlink_reload_action action,
				     enum devlink_reload_limit limit,
				     struct netlink_ext_ack *extack)
{
	struct hclge_devlink_priv *priv = devlink_priv(devlink);
	struct hclge_dev *hdev = priv->hdev;
	struct hnae3_handle *h = &hdev->vport->nic;
	struct pci_dev *pdev = hdev->pdev;
	int ret;

	if (test_bit(HCLGE_STATE_RST_HANDLING, &hdev->state)) {
		dev_err(&pdev->dev, "reset is handling\n");
		return -EBUSY;
	}

	switch (action) {
	case DEVLINK_RELOAD_ACTION_DRIVER_REINIT:
		rtnl_lock();
		ret = hdev->nic_client->ops->reset_notify(h, HNAE3_DOWN_CLIENT);
		if (ret) {
			rtnl_unlock();
			return ret;
		}

		ret = hdev->nic_client->ops->reset_notify(h,
							  HNAE3_UNINIT_CLIENT);
		rtnl_unlock();
		return ret;
	default:
		return -EOPNOTSUPP;
	}
}

static int hclge_devlink_reload_up(struct devlink *devlink,
				   enum devlink_reload_action action,
				   enum devlink_reload_limit limit,
				   u32 *actions_performed,
				   struct netlink_ext_ack *extack)
{
	struct hclge_devlink_priv *priv = devlink_priv(devlink);
	struct hclge_dev *hdev = priv->hdev;
	struct hnae3_handle *h = &hdev->vport->nic;
	int ret;

	*actions_performed = BIT(action);
	switch (action) {
	case DEVLINK_RELOAD_ACTION_DRIVER_REINIT:
		rtnl_lock();
		ret = hdev->nic_client->ops->reset_notify(h, HNAE3_INIT_CLIENT);
		if (ret) {
			rtnl_unlock();
			return ret;
		}

		ret = hdev->nic_client->ops->reset_notify(h, HNAE3_UP_CLIENT);
		rtnl_unlock();
		return ret;
	default:
		return -EOPNOTSUPP;
	}
}

static const struct devlink_ops hclge_devlink_ops = {
	.info_get = hclge_devlink_info_get,
	.reload_actions = BIT(DEVLINK_RELOAD_ACTION_DRIVER_REINIT),
	.reload_down = hclge_devlink_reload_down,
	.reload_up = hclge_devlink_reload_up,
};

int hclge_devlink_init(struct hclge_dev *hdev)
{
	struct pci_dev *pdev = hdev->pdev;
	struct hclge_devlink_priv *priv;
	struct devlink *devlink;

	devlink = devlink_alloc(&hclge_devlink_ops,
				sizeof(struct hclge_devlink_priv), &pdev->dev);
	if (!devlink)
		return -ENOMEM;

	priv = devlink_priv(devlink);
	priv->hdev = hdev;
	hdev->devlink = devlink;

	devlink_set_features(devlink, DEVLINK_F_RELOAD);
	devlink_register(devlink);
	devlink_reload_enable(devlink);
	return 0;
}

void hclge_devlink_uninit(struct hclge_dev *hdev)
{
	struct devlink *devlink = hdev->devlink;

	devlink_reload_disable(devlink);

	devlink_unregister(devlink);

	devlink_free(devlink);
}
