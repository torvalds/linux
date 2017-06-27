/*
 * Copyright (c) 2016, Linaro Ltd.
 * Copyright (c) 2015, Sony Mobile Communications Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/rpmsg.h>
#include <linux/soc/qcom/wcnss_ctrl.h>
#include <linux/platform_device.h>

#include <net/bluetooth/bluetooth.h>
#include <net/bluetooth/hci_core.h>

#include "btqca.h"

struct btqcomsmd {
	struct hci_dev *hdev;

	struct rpmsg_endpoint *acl_channel;
	struct rpmsg_endpoint *cmd_channel;
};

static int btqcomsmd_recv(struct hci_dev *hdev, unsigned int type,
			   const void *data, size_t count)
{
	struct sk_buff *skb;

	/* Use GFP_ATOMIC as we're in IRQ context */
	skb = bt_skb_alloc(count, GFP_ATOMIC);
	if (!skb) {
		hdev->stat.err_rx++;
		return -ENOMEM;
	}

	hci_skb_pkt_type(skb) = type;
	memcpy(skb_put(skb, count), data, count);

	return hci_recv_frame(hdev, skb);
}

static int btqcomsmd_acl_callback(struct rpmsg_device *rpdev, void *data,
				  int count, void *priv, u32 addr)
{
	struct btqcomsmd *btq = priv;

	btq->hdev->stat.byte_rx += count;
	return btqcomsmd_recv(btq->hdev, HCI_ACLDATA_PKT, data, count);
}

static int btqcomsmd_cmd_callback(struct rpmsg_device *rpdev, void *data,
				  int count, void *priv, u32 addr)
{
	struct btqcomsmd *btq = priv;

	return btqcomsmd_recv(btq->hdev, HCI_EVENT_PKT, data, count);
}

static int btqcomsmd_send(struct hci_dev *hdev, struct sk_buff *skb)
{
	struct btqcomsmd *btq = hci_get_drvdata(hdev);
	int ret;

	switch (hci_skb_pkt_type(skb)) {
	case HCI_ACLDATA_PKT:
		ret = rpmsg_send(btq->acl_channel, skb->data, skb->len);
		hdev->stat.acl_tx++;
		hdev->stat.byte_tx += skb->len;
		break;
	case HCI_COMMAND_PKT:
		ret = rpmsg_send(btq->cmd_channel, skb->data, skb->len);
		hdev->stat.cmd_tx++;
		break;
	default:
		ret = -EILSEQ;
		break;
	}

	kfree_skb(skb);

	return ret;
}

static int btqcomsmd_open(struct hci_dev *hdev)
{
	return 0;
}

static int btqcomsmd_close(struct hci_dev *hdev)
{
	return 0;
}

static int btqcomsmd_probe(struct platform_device *pdev)
{
	struct btqcomsmd *btq;
	struct hci_dev *hdev;
	void *wcnss;
	int ret;

	btq = devm_kzalloc(&pdev->dev, sizeof(*btq), GFP_KERNEL);
	if (!btq)
		return -ENOMEM;

	wcnss = dev_get_drvdata(pdev->dev.parent);

	btq->acl_channel = qcom_wcnss_open_channel(wcnss, "APPS_RIVA_BT_ACL",
						   btqcomsmd_acl_callback, btq);
	if (IS_ERR(btq->acl_channel))
		return PTR_ERR(btq->acl_channel);

	btq->cmd_channel = qcom_wcnss_open_channel(wcnss, "APPS_RIVA_BT_CMD",
						   btqcomsmd_cmd_callback, btq);
	if (IS_ERR(btq->cmd_channel))
		return PTR_ERR(btq->cmd_channel);

	hdev = hci_alloc_dev();
	if (!hdev)
		return -ENOMEM;

	hci_set_drvdata(hdev, btq);
	btq->hdev = hdev;
	SET_HCIDEV_DEV(hdev, &pdev->dev);

	hdev->bus = HCI_SMD;
	hdev->open = btqcomsmd_open;
	hdev->close = btqcomsmd_close;
	hdev->send = btqcomsmd_send;
	hdev->set_bdaddr = qca_set_bdaddr_rome;

	ret = hci_register_dev(hdev);
	if (ret < 0) {
		hci_free_dev(hdev);
		return ret;
	}

	platform_set_drvdata(pdev, btq);

	return 0;
}

static int btqcomsmd_remove(struct platform_device *pdev)
{
	struct btqcomsmd *btq = platform_get_drvdata(pdev);

	hci_unregister_dev(btq->hdev);
	hci_free_dev(btq->hdev);

	rpmsg_destroy_ept(btq->cmd_channel);
	rpmsg_destroy_ept(btq->acl_channel);

	return 0;
}

static const struct of_device_id btqcomsmd_of_match[] = {
	{ .compatible = "qcom,wcnss-bt", },
	{ },
};
MODULE_DEVICE_TABLE(of, btqcomsmd_of_match);

static struct platform_driver btqcomsmd_driver = {
	.probe = btqcomsmd_probe,
	.remove = btqcomsmd_remove,
	.driver  = {
		.name  = "btqcomsmd",
		.of_match_table = btqcomsmd_of_match,
	},
};

module_platform_driver(btqcomsmd_driver);

MODULE_AUTHOR("Bjorn Andersson <bjorn.andersson@sonymobile.com>");
MODULE_DESCRIPTION("Qualcomm SMD HCI driver");
MODULE_LICENSE("GPL v2");
