// SPDX-License-Identifier: GPL-2.0-only
/*
 * Proprietary commands extension for STMicroelectronics NFC NCI Chip
 *
 * Copyright (C) 2014-2015  STMicroelectronics SAS. All rights reserved.
 */

#include <net/genetlink.h>
#include <linux/module.h>
#include <linux/nfc.h>
#include <linux/delay.h>
#include <net/nfc/nci_core.h>

#include "st-nci.h"

#define ST_NCI_HCI_DM_GETDATA			0x10
#define ST_NCI_HCI_DM_PUTDATA			0x11
#define ST_NCI_HCI_DM_LOAD			0x12
#define ST_NCI_HCI_DM_GETINFO			0x13
#define ST_NCI_HCI_DM_FWUPD_START		0x14
#define ST_NCI_HCI_DM_FWUPD_STOP		0x15
#define ST_NCI_HCI_DM_UPDATE_AID		0x20
#define ST_NCI_HCI_DM_RESET			0x3e

#define ST_NCI_HCI_DM_FIELD_GENERATOR		0x32
#define ST_NCI_HCI_DM_VDC_MEASUREMENT_VALUE	0x33
#define ST_NCI_HCI_DM_VDC_VALUE_COMPARISON	0x34

#define ST_NCI_FACTORY_MODE_ON			1
#define ST_NCI_FACTORY_MODE_OFF			0

#define ST_NCI_EVT_POST_DATA			0x02

struct get_param_data {
	u8 gate;
	u8 data;
} __packed;

static int st_nci_factory_mode(struct nfc_dev *dev, void *data,
			       size_t data_len)
{
	struct nci_dev *ndev = nfc_get_drvdata(dev);
	struct st_nci_info *info = nci_get_drvdata(ndev);

	if (data_len != 1)
		return -EINVAL;

	pr_debug("factory mode: %x\n", ((u8 *)data)[0]);

	switch (((u8 *)data)[0]) {
	case ST_NCI_FACTORY_MODE_ON:
		test_and_set_bit(ST_NCI_FACTORY_MODE, &info->flags);
	break;
	case ST_NCI_FACTORY_MODE_OFF:
		clear_bit(ST_NCI_FACTORY_MODE, &info->flags);
	break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int st_nci_hci_clear_all_pipes(struct nfc_dev *dev, void *data,
				      size_t data_len)
{
	struct nci_dev *ndev = nfc_get_drvdata(dev);

	return nci_hci_clear_all_pipes(ndev);
}

static int st_nci_hci_dm_put_data(struct nfc_dev *dev, void *data,
				  size_t data_len)
{
	struct nci_dev *ndev = nfc_get_drvdata(dev);

	return nci_hci_send_cmd(ndev, ST_NCI_DEVICE_MGNT_GATE,
				ST_NCI_HCI_DM_PUTDATA, data,
				data_len, NULL);
}

static int st_nci_hci_dm_update_aid(struct nfc_dev *dev, void *data,
				    size_t data_len)
{
	struct nci_dev *ndev = nfc_get_drvdata(dev);

	return nci_hci_send_cmd(ndev, ST_NCI_DEVICE_MGNT_GATE,
			ST_NCI_HCI_DM_UPDATE_AID, data, data_len, NULL);
}

static int st_nci_hci_dm_get_info(struct nfc_dev *dev, void *data,
				  size_t data_len)
{
	int r;
	struct sk_buff *msg, *skb;
	struct nci_dev *ndev = nfc_get_drvdata(dev);

	r = nci_hci_send_cmd(ndev, ST_NCI_DEVICE_MGNT_GATE, ST_NCI_HCI_DM_GETINFO,
			     data, data_len, &skb);
	if (r)
		return r;

	msg = nfc_vendor_cmd_alloc_reply_skb(dev, ST_NCI_VENDOR_OUI,
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
	return r;
}

static int st_nci_hci_dm_get_data(struct nfc_dev *dev, void *data,
				  size_t data_len)
{
	int r;
	struct sk_buff *msg, *skb;
	struct nci_dev *ndev = nfc_get_drvdata(dev);

	r = nci_hci_send_cmd(ndev, ST_NCI_DEVICE_MGNT_GATE, ST_NCI_HCI_DM_GETDATA,
			     data, data_len, &skb);
	if (r)
		return r;

	msg = nfc_vendor_cmd_alloc_reply_skb(dev, ST_NCI_VENDOR_OUI,
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
	return r;
}

static int st_nci_hci_dm_fwupd_start(struct nfc_dev *dev, void *data,
				     size_t data_len)
{
	int r;
	struct nci_dev *ndev = nfc_get_drvdata(dev);

	dev->fw_download_in_progress = true;
	r = nci_hci_send_cmd(ndev, ST_NCI_DEVICE_MGNT_GATE,
			ST_NCI_HCI_DM_FWUPD_START, data, data_len, NULL);
	if (r)
		dev->fw_download_in_progress = false;

	return r;
}

static int st_nci_hci_dm_fwupd_end(struct nfc_dev *dev, void *data,
				   size_t data_len)
{
	struct nci_dev *ndev = nfc_get_drvdata(dev);

	return nci_hci_send_cmd(ndev, ST_NCI_DEVICE_MGNT_GATE,
			ST_NCI_HCI_DM_FWUPD_STOP, data, data_len, NULL);
}

static int st_nci_hci_dm_direct_load(struct nfc_dev *dev, void *data,
				     size_t data_len)
{
	struct nci_dev *ndev = nfc_get_drvdata(dev);

	if (dev->fw_download_in_progress) {
		dev->fw_download_in_progress = false;
		return nci_hci_send_cmd(ndev, ST_NCI_DEVICE_MGNT_GATE,
				ST_NCI_HCI_DM_LOAD, data, data_len, NULL);
	}
	return -EPROTO;
}

static int st_nci_hci_dm_reset(struct nfc_dev *dev, void *data,
			       size_t data_len)
{
	struct nci_dev *ndev = nfc_get_drvdata(dev);

	nci_hci_send_cmd(ndev, ST_NCI_DEVICE_MGNT_GATE,
			ST_NCI_HCI_DM_RESET, data, data_len, NULL);
	msleep(200);

	return 0;
}

static int st_nci_hci_get_param(struct nfc_dev *dev, void *data,
				size_t data_len)
{
	int r;
	struct sk_buff *msg, *skb;
	struct nci_dev *ndev = nfc_get_drvdata(dev);
	struct get_param_data *param = (struct get_param_data *)data;

	if (data_len < sizeof(struct get_param_data))
		return -EPROTO;

	r = nci_hci_get_param(ndev, param->gate, param->data, &skb);
	if (r)
		return r;

	msg = nfc_vendor_cmd_alloc_reply_skb(dev, ST_NCI_VENDOR_OUI,
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
	return r;
}

static int st_nci_hci_dm_field_generator(struct nfc_dev *dev, void *data,
					 size_t data_len)
{
	struct nci_dev *ndev = nfc_get_drvdata(dev);

	return nci_hci_send_cmd(ndev, ST_NCI_DEVICE_MGNT_GATE,
				ST_NCI_HCI_DM_FIELD_GENERATOR, data, data_len, NULL);
}

static int st_nci_hci_dm_vdc_measurement_value(struct nfc_dev *dev, void *data,
					       size_t data_len)
{
	int r;
	struct sk_buff *msg, *skb;
	struct nci_dev *ndev = nfc_get_drvdata(dev);

	if (data_len != 4)
		return -EPROTO;

	r = nci_hci_send_cmd(ndev, ST_NCI_DEVICE_MGNT_GATE,
			     ST_NCI_HCI_DM_VDC_MEASUREMENT_VALUE,
			     data, data_len, &skb);
	if (r)
		return r;

	msg = nfc_vendor_cmd_alloc_reply_skb(dev, ST_NCI_VENDOR_OUI,
				HCI_DM_VDC_MEASUREMENT_VALUE, skb->len);
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
	return r;
}

static int st_nci_hci_dm_vdc_value_comparison(struct nfc_dev *dev, void *data,
					      size_t data_len)
{
	int r;
	struct sk_buff *msg, *skb;
	struct nci_dev *ndev = nfc_get_drvdata(dev);

	if (data_len != 2)
		return -EPROTO;

	r = nci_hci_send_cmd(ndev, ST_NCI_DEVICE_MGNT_GATE,
			     ST_NCI_HCI_DM_VDC_VALUE_COMPARISON,
			     data, data_len, &skb);
	if (r)
		return r;

	msg = nfc_vendor_cmd_alloc_reply_skb(dev, ST_NCI_VENDOR_OUI,
					HCI_DM_VDC_VALUE_COMPARISON, skb->len);
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
	return r;
}

static int st_nci_loopback(struct nfc_dev *dev, void *data,
			   size_t data_len)
{
	int r;
	struct sk_buff *msg, *skb;
	struct nci_dev *ndev = nfc_get_drvdata(dev);

	if (data_len <= 0)
		return -EPROTO;

	r = nci_nfcc_loopback(ndev, data, data_len, &skb);
	if (r < 0)
		return r;

	msg = nfc_vendor_cmd_alloc_reply_skb(dev, ST_NCI_VENDOR_OUI,
					     LOOPBACK, skb->len);
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
	return r;
}

static int st_nci_manufacturer_specific(struct nfc_dev *dev, void *data,
					size_t data_len)
{
	struct sk_buff *msg;
	struct nci_dev *ndev = nfc_get_drvdata(dev);

	msg = nfc_vendor_cmd_alloc_reply_skb(dev, ST_NCI_VENDOR_OUI,
					MANUFACTURER_SPECIFIC,
					sizeof(ndev->manufact_specific_info));
	if (!msg)
		return -ENOMEM;

	if (nla_put(msg, NFC_ATTR_VENDOR_DATA, sizeof(ndev->manufact_specific_info),
		    &ndev->manufact_specific_info)) {
		kfree_skb(msg);
		return -ENOBUFS;
	}

	return nfc_vendor_cmd_reply(msg);
}

static const struct nfc_vendor_cmd st_nci_vendor_cmds[] = {
	{
		.vendor_id = ST_NCI_VENDOR_OUI,
		.subcmd = FACTORY_MODE,
		.doit = st_nci_factory_mode,
	},
	{
		.vendor_id = ST_NCI_VENDOR_OUI,
		.subcmd = HCI_CLEAR_ALL_PIPES,
		.doit = st_nci_hci_clear_all_pipes,
	},
	{
		.vendor_id = ST_NCI_VENDOR_OUI,
		.subcmd = HCI_DM_PUT_DATA,
		.doit = st_nci_hci_dm_put_data,
	},
	{
		.vendor_id = ST_NCI_VENDOR_OUI,
		.subcmd = HCI_DM_UPDATE_AID,
		.doit = st_nci_hci_dm_update_aid,
	},
	{
		.vendor_id = ST_NCI_VENDOR_OUI,
		.subcmd = HCI_DM_GET_INFO,
		.doit = st_nci_hci_dm_get_info,
	},
	{
		.vendor_id = ST_NCI_VENDOR_OUI,
		.subcmd = HCI_DM_GET_DATA,
		.doit = st_nci_hci_dm_get_data,
	},
	{
		.vendor_id = ST_NCI_VENDOR_OUI,
		.subcmd = HCI_DM_DIRECT_LOAD,
		.doit = st_nci_hci_dm_direct_load,
	},
	{
		.vendor_id = ST_NCI_VENDOR_OUI,
		.subcmd = HCI_DM_RESET,
		.doit = st_nci_hci_dm_reset,
	},
	{
		.vendor_id = ST_NCI_VENDOR_OUI,
		.subcmd = HCI_GET_PARAM,
		.doit = st_nci_hci_get_param,
	},
	{
		.vendor_id = ST_NCI_VENDOR_OUI,
		.subcmd = HCI_DM_FIELD_GENERATOR,
		.doit = st_nci_hci_dm_field_generator,
	},
	{
		.vendor_id = ST_NCI_VENDOR_OUI,
		.subcmd = HCI_DM_FWUPD_START,
		.doit = st_nci_hci_dm_fwupd_start,
	},
	{
		.vendor_id = ST_NCI_VENDOR_OUI,
		.subcmd = HCI_DM_FWUPD_END,
		.doit = st_nci_hci_dm_fwupd_end,
	},
	{
		.vendor_id = ST_NCI_VENDOR_OUI,
		.subcmd = LOOPBACK,
		.doit = st_nci_loopback,
	},
	{
		.vendor_id = ST_NCI_VENDOR_OUI,
		.subcmd = HCI_DM_VDC_MEASUREMENT_VALUE,
		.doit = st_nci_hci_dm_vdc_measurement_value,
	},
	{
		.vendor_id = ST_NCI_VENDOR_OUI,
		.subcmd = HCI_DM_VDC_VALUE_COMPARISON,
		.doit = st_nci_hci_dm_vdc_value_comparison,
	},
	{
		.vendor_id = ST_NCI_VENDOR_OUI,
		.subcmd = MANUFACTURER_SPECIFIC,
		.doit = st_nci_manufacturer_specific,
	},
};

int st_nci_vendor_cmds_init(struct nci_dev *ndev)
{
	return nci_set_vendor_cmds(ndev, st_nci_vendor_cmds,
				   sizeof(st_nci_vendor_cmds));
}
EXPORT_SYMBOL(st_nci_vendor_cmds_init);
