// SPDX-License-Identifier: GPL-2.0-only
/*
 * Proprietary commands extension for STMicroelectronics NFC Chip
 *
 * Copyright (C) 2014-2015  STMicroelectronics SAS. All rights reserved.
 */

#include <net/genetlink.h>
#include <linux/module.h>
#include <linux/nfc.h>
#include <net/nfc/hci.h>
#include <net/nfc/llc.h>

#include "st21nfca.h"

#define ST21NFCA_HCI_DM_GETDATA			0x10
#define ST21NFCA_HCI_DM_PUTDATA			0x11
#define ST21NFCA_HCI_DM_LOAD			0x12
#define ST21NFCA_HCI_DM_GETINFO			0x13
#define ST21NFCA_HCI_DM_UPDATE_AID		0x20
#define ST21NFCA_HCI_DM_RESET			0x3e

#define ST21NFCA_HCI_DM_FIELD_GENERATOR		0x32

#define ST21NFCA_FACTORY_MODE_ON		1
#define ST21NFCA_FACTORY_MODE_OFF		0

#define ST21NFCA_EVT_POST_DATA			0x02

struct get_param_data {
	u8 gate;
	u8 data;
} __packed;

static int st21nfca_factory_mode(struct nfc_dev *dev, void *data,
			       size_t data_len)
{
	struct nfc_hci_dev *hdev = nfc_get_drvdata(dev);

	if (data_len != 1)
		return -EINVAL;

	pr_debug("factory mode: %x\n", ((u8 *)data)[0]);

	switch (((u8 *)data)[0]) {
	case ST21NFCA_FACTORY_MODE_ON:
		test_and_set_bit(ST21NFCA_FACTORY_MODE, &hdev->quirks);
	break;
	case ST21NFCA_FACTORY_MODE_OFF:
		clear_bit(ST21NFCA_FACTORY_MODE, &hdev->quirks);
	break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int st21nfca_hci_clear_all_pipes(struct nfc_dev *dev, void *data,
				      size_t data_len)
{
	struct nfc_hci_dev *hdev = nfc_get_drvdata(dev);

	return nfc_hci_disconnect_all_gates(hdev);
}

static int st21nfca_hci_dm_put_data(struct nfc_dev *dev, void *data,
				  size_t data_len)
{
	struct nfc_hci_dev *hdev = nfc_get_drvdata(dev);

	return nfc_hci_send_cmd(hdev, ST21NFCA_DEVICE_MGNT_GATE,
				ST21NFCA_HCI_DM_PUTDATA, data,
				data_len, NULL);
}

static int st21nfca_hci_dm_update_aid(struct nfc_dev *dev, void *data,
				    size_t data_len)
{
	struct nfc_hci_dev *hdev = nfc_get_drvdata(dev);

	return nfc_hci_send_cmd(hdev, ST21NFCA_DEVICE_MGNT_GATE,
			ST21NFCA_HCI_DM_UPDATE_AID, data, data_len, NULL);
}

static int st21nfca_hci_dm_get_info(struct nfc_dev *dev, void *data,
				    size_t data_len)
{
	int r;
	struct sk_buff *msg, *skb;
	struct nfc_hci_dev *hdev = nfc_get_drvdata(dev);

	r = nfc_hci_send_cmd(hdev,
			     ST21NFCA_DEVICE_MGNT_GATE,
			     ST21NFCA_HCI_DM_GETINFO,
			     data, data_len, &skb);
	if (r)
		goto exit;

	msg = nfc_vendor_cmd_alloc_reply_skb(dev, ST21NFCA_VENDOR_OUI,
					     HCI_DM_GET_INFO, skb->len);
	if (!msg) {
		r = -ENOMEM;
		goto free_skb;
	}

	if (nla_put(msg, NFC_ATTR_VENDOR_DATA, skb->len, skb->data)) {
		kfree_skb(msg);
		r = -ENOBUFS;
		goto free_skb;
	}

	r = nfc_vendor_cmd_reply(msg);

free_skb:
	kfree_skb(skb);
exit:
	return r;
}

static int st21nfca_hci_dm_get_data(struct nfc_dev *dev, void *data,
				    size_t data_len)
{
	int r;
	struct sk_buff *msg, *skb;
	struct nfc_hci_dev *hdev = nfc_get_drvdata(dev);

	r = nfc_hci_send_cmd(hdev,
			     ST21NFCA_DEVICE_MGNT_GATE,
			     ST21NFCA_HCI_DM_GETDATA,
			     data, data_len, &skb);
	if (r)
		goto exit;

	msg = nfc_vendor_cmd_alloc_reply_skb(dev, ST21NFCA_VENDOR_OUI,
					     HCI_DM_GET_DATA, skb->len);
	if (!msg) {
		r = -ENOMEM;
		goto free_skb;
	}

	if (nla_put(msg, NFC_ATTR_VENDOR_DATA, skb->len, skb->data)) {
		kfree_skb(msg);
		r = -ENOBUFS;
		goto free_skb;
	}

	r = nfc_vendor_cmd_reply(msg);

free_skb:
	kfree_skb(skb);
exit:
	return r;
}

static int st21nfca_hci_dm_load(struct nfc_dev *dev, void *data,
				size_t data_len)
{
	struct nfc_hci_dev *hdev = nfc_get_drvdata(dev);

	return nfc_hci_send_cmd(hdev, ST21NFCA_DEVICE_MGNT_GATE,
				ST21NFCA_HCI_DM_LOAD, data, data_len, NULL);
}

static int st21nfca_hci_dm_reset(struct nfc_dev *dev, void *data,
				 size_t data_len)
{
	int r;
	struct nfc_hci_dev *hdev = nfc_get_drvdata(dev);

	r = nfc_hci_send_cmd_async(hdev, ST21NFCA_DEVICE_MGNT_GATE,
			ST21NFCA_HCI_DM_RESET, data, data_len, NULL, NULL);
	if (r < 0)
		return r;

	r = nfc_llc_stop(hdev->llc);
	if (r < 0)
		return r;

	return nfc_llc_start(hdev->llc);
}

static int st21nfca_hci_get_param(struct nfc_dev *dev, void *data,
				  size_t data_len)
{
	int r;
	struct sk_buff *msg, *skb;
	struct nfc_hci_dev *hdev = nfc_get_drvdata(dev);
	struct get_param_data *param = (struct get_param_data *)data;

	if (data_len < sizeof(struct get_param_data))
		return -EPROTO;

	r = nfc_hci_get_param(hdev, param->gate, param->data, &skb);
	if (r)
		goto exit;

	msg = nfc_vendor_cmd_alloc_reply_skb(dev, ST21NFCA_VENDOR_OUI,
					     HCI_GET_PARAM, skb->len);
	if (!msg) {
		r = -ENOMEM;
		goto free_skb;
	}

	if (nla_put(msg, NFC_ATTR_VENDOR_DATA, skb->len, skb->data)) {
		kfree_skb(msg);
		r = -ENOBUFS;
		goto free_skb;
	}

	r = nfc_vendor_cmd_reply(msg);

free_skb:
	kfree_skb(skb);
exit:
	return r;
}

static int st21nfca_hci_dm_field_generator(struct nfc_dev *dev, void *data,
					   size_t data_len)
{
	struct nfc_hci_dev *hdev = nfc_get_drvdata(dev);

	return nfc_hci_send_cmd(hdev,
				ST21NFCA_DEVICE_MGNT_GATE,
				ST21NFCA_HCI_DM_FIELD_GENERATOR,
				data, data_len, NULL);
}

int st21nfca_hci_loopback_event_received(struct nfc_hci_dev *hdev, u8 event,
					 struct sk_buff *skb)
{
	struct st21nfca_hci_info *info = nfc_hci_get_clientdata(hdev);

	switch (event) {
	case ST21NFCA_EVT_POST_DATA:
		info->vendor_info.rx_skb = skb;
	break;
	default:
		nfc_err(&hdev->ndev->dev, "Unexpected event on loopback gate\n");
	}
	complete(&info->vendor_info.req_completion);
	return 0;
}
EXPORT_SYMBOL(st21nfca_hci_loopback_event_received);

static int st21nfca_hci_loopback(struct nfc_dev *dev, void *data,
				 size_t data_len)
{
	int r;
	struct sk_buff *msg;
	struct nfc_hci_dev *hdev = nfc_get_drvdata(dev);
	struct st21nfca_hci_info *info = nfc_hci_get_clientdata(hdev);

	if (data_len <= 0)
		return -EPROTO;

	reinit_completion(&info->vendor_info.req_completion);
	info->vendor_info.rx_skb = NULL;

	r = nfc_hci_send_event(hdev, NFC_HCI_LOOPBACK_GATE,
			       ST21NFCA_EVT_POST_DATA, data, data_len);
	if (r < 0) {
		r = -EPROTO;
		goto exit;
	}

	wait_for_completion_interruptible(&info->vendor_info.req_completion);
	if (!info->vendor_info.rx_skb ||
	    info->vendor_info.rx_skb->len != data_len) {
		r = -EPROTO;
		goto exit;
	}

	msg = nfc_vendor_cmd_alloc_reply_skb(hdev->ndev,
					ST21NFCA_VENDOR_OUI,
					HCI_LOOPBACK,
					info->vendor_info.rx_skb->len);
	if (!msg) {
		r = -ENOMEM;
		goto free_skb;
	}

	if (nla_put(msg, NFC_ATTR_VENDOR_DATA, info->vendor_info.rx_skb->len,
		    info->vendor_info.rx_skb->data)) {
		kfree_skb(msg);
		r = -ENOBUFS;
		goto free_skb;
	}

	r = nfc_vendor_cmd_reply(msg);
free_skb:
	kfree_skb(info->vendor_info.rx_skb);
exit:
	return r;
}

static const struct nfc_vendor_cmd st21nfca_vendor_cmds[] = {
	{
		.vendor_id = ST21NFCA_VENDOR_OUI,
		.subcmd = FACTORY_MODE,
		.doit = st21nfca_factory_mode,
	},
	{
		.vendor_id = ST21NFCA_VENDOR_OUI,
		.subcmd = HCI_CLEAR_ALL_PIPES,
		.doit = st21nfca_hci_clear_all_pipes,
	},
	{
		.vendor_id = ST21NFCA_VENDOR_OUI,
		.subcmd = HCI_DM_PUT_DATA,
		.doit = st21nfca_hci_dm_put_data,
	},
	{
		.vendor_id = ST21NFCA_VENDOR_OUI,
		.subcmd = HCI_DM_UPDATE_AID,
		.doit = st21nfca_hci_dm_update_aid,
	},
	{
		.vendor_id = ST21NFCA_VENDOR_OUI,
		.subcmd = HCI_DM_GET_INFO,
		.doit = st21nfca_hci_dm_get_info,
	},
	{
		.vendor_id = ST21NFCA_VENDOR_OUI,
		.subcmd = HCI_DM_GET_DATA,
		.doit = st21nfca_hci_dm_get_data,
	},
	{
		.vendor_id = ST21NFCA_VENDOR_OUI,
		.subcmd = HCI_DM_LOAD,
		.doit = st21nfca_hci_dm_load,
	},
	{
		.vendor_id = ST21NFCA_VENDOR_OUI,
		.subcmd = HCI_DM_RESET,
		.doit = st21nfca_hci_dm_reset,
	},
	{
		.vendor_id = ST21NFCA_VENDOR_OUI,
		.subcmd = HCI_GET_PARAM,
		.doit = st21nfca_hci_get_param,
	},
	{
		.vendor_id = ST21NFCA_VENDOR_OUI,
		.subcmd = HCI_DM_FIELD_GENERATOR,
		.doit = st21nfca_hci_dm_field_generator,
	},
	{
		.vendor_id = ST21NFCA_VENDOR_OUI,
		.subcmd = HCI_LOOPBACK,
		.doit = st21nfca_hci_loopback,
	},
};

int st21nfca_vendor_cmds_init(struct nfc_hci_dev *hdev)
{
	struct st21nfca_hci_info *info = nfc_hci_get_clientdata(hdev);

	init_completion(&info->vendor_info.req_completion);
	return nfc_set_vendor_cmds(hdev->ndev, st21nfca_vendor_cmds,
				   sizeof(st21nfca_vendor_cmds));
}
EXPORT_SYMBOL(st21nfca_vendor_cmds_init);
