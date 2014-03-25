/*
 * HCI based Driver for STMicroelectronics NFC Chip
 *
 * Copyright (C) 2014  STMicroelectronics SAS. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/crc-ccitt.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/miscdevice.h>
#include <linux/interrupt.h>
#include <linux/gpio.h>
#include <linux/i2c.h>

#include <linux/nfc.h>
#include <net/nfc/hci.h>
#include <net/nfc/llc.h>

#include <uapi/linux/nfc.h>

#include "st21nfca.h"
#include <linux/platform_data/st21nfca.h>

#define DRIVER_DESC "HCI NFC driver for ST21NFCA"

#define FULL_VERSION_LEN 3

/* Proprietary gates, events, commands and registers */

/* Commands that apply to all RF readers */
#define ST21NFCA_RF_READER_CMD_PRESENCE_CHECK	0x30

#define ST21NFCA_RF_READER_ISO15693_GATE	0x12

/*
 * Reader gate for communication with contact-less cards using Type A
 * protocol ISO14443-3 but not compliant with ISO14443-4
 */
#define ST21NFCA_RF_READER_14443_3_A_GATE	0x15
#define ST21NFCA_RF_READER_14443_3_A_UID	0x02
#define ST21NFCA_RF_READER_14443_3_A_ATQA	0x03
#define ST21NFCA_RF_READER_14443_3_A_SAK	0x04

#define ST21NFCA_DEVICE_MGNT_GATE		0x01
#define ST21NFCA_DEVICE_MGNT_PIPE		0x02
#define ST21NFCA_NFC_MODE	0x03	/* NFC_MODE parameter*/


static DECLARE_BITMAP(dev_mask, ST21NFCA_NUM_DEVICES);

static struct nfc_hci_gate st21nfca_gates[] = {
	{NFC_HCI_ADMIN_GATE, NFC_HCI_INVALID_PIPE},
	{NFC_HCI_LOOPBACK_GATE, NFC_HCI_INVALID_PIPE},
	{NFC_HCI_ID_MGMT_GATE, NFC_HCI_INVALID_PIPE},
	{NFC_HCI_LINK_MGMT_GATE, NFC_HCI_INVALID_PIPE},
	{NFC_HCI_RF_READER_B_GATE, NFC_HCI_INVALID_PIPE},
	{NFC_HCI_RF_READER_A_GATE, NFC_HCI_INVALID_PIPE},
	{ST21NFCA_DEVICE_MGNT_GATE, ST21NFCA_DEVICE_MGNT_PIPE},
	{ST21NFCA_RF_READER_F_GATE, NFC_HCI_INVALID_PIPE},
	{ST21NFCA_RF_READER_14443_3_A_GATE, NFC_HCI_INVALID_PIPE},
};
/* Largest headroom needed for outgoing custom commands */
#define ST21NFCA_CMDS_HEADROOM  7

static int st21nfca_hci_open(struct nfc_hci_dev *hdev)
{
	struct st21nfca_hci_info *info = nfc_hci_get_clientdata(hdev);
	int r;

	mutex_lock(&info->info_lock);

	if (info->state != ST21NFCA_ST_COLD) {
		r = -EBUSY;
		goto out;
	}

	r = info->phy_ops->enable(info->phy_id);

	if (r == 0)
		info->state = ST21NFCA_ST_READY;

out:
	mutex_unlock(&info->info_lock);
	return r;
}

static void st21nfca_hci_close(struct nfc_hci_dev *hdev)
{
	struct st21nfca_hci_info *info = nfc_hci_get_clientdata(hdev);

	mutex_lock(&info->info_lock);

	if (info->state == ST21NFCA_ST_COLD)
		goto out;

	info->phy_ops->disable(info->phy_id);
	info->state = ST21NFCA_ST_COLD;

out:
	mutex_unlock(&info->info_lock);
}

static int st21nfca_hci_ready(struct nfc_hci_dev *hdev)
{
	struct sk_buff *skb;

	u8 param;
	int r;

	param = NFC_HCI_UICC_HOST_ID;
	r = nfc_hci_set_param(hdev, NFC_HCI_ADMIN_GATE,
			      NFC_HCI_ADMIN_WHITELIST, &param, 1);
	if (r < 0)
		return r;

	/* Set NFC_MODE in device management gate to enable */
	r = nfc_hci_get_param(hdev, ST21NFCA_DEVICE_MGNT_GATE,
			      ST21NFCA_NFC_MODE, &skb);
	if (r < 0)
		return r;

	if (skb->data[0] == 0) {
		kfree_skb(skb);
		param = 1;

		r = nfc_hci_set_param(hdev, ST21NFCA_DEVICE_MGNT_GATE,
					ST21NFCA_NFC_MODE, &param, 1);
		if (r < 0)
			return r;
	}

	r = nfc_hci_send_event(hdev, NFC_HCI_RF_READER_A_GATE,
			       NFC_HCI_EVT_END_OPERATION, NULL, 0);
	if (r < 0)
		return r;

	r = nfc_hci_get_param(hdev, NFC_HCI_ID_MGMT_GATE,
			      NFC_HCI_ID_MGMT_VERSION_SW, &skb);
	if (r < 0)
		return r;

	if (skb->len != FULL_VERSION_LEN) {
		kfree_skb(skb);
		return -EINVAL;
	}

	print_hex_dump(KERN_DEBUG, "FULL VERSION SOFTWARE INFO: ",
		       DUMP_PREFIX_NONE, 16, 1,
		       skb->data, FULL_VERSION_LEN, false);

	kfree_skb(skb);

	return 0;
}

static int st21nfca_hci_xmit(struct nfc_hci_dev *hdev, struct sk_buff *skb)
{
	struct st21nfca_hci_info *info = nfc_hci_get_clientdata(hdev);

	return info->phy_ops->write(info->phy_id, skb);
}

static int st21nfca_hci_start_poll(struct nfc_hci_dev *hdev,
				   u32 im_protocols, u32 tm_protocols)
{
	int r;

	pr_info(DRIVER_DESC ": %s protocols 0x%x 0x%x\n",
		__func__, im_protocols, tm_protocols);

	r = nfc_hci_send_event(hdev, NFC_HCI_RF_READER_A_GATE,
			       NFC_HCI_EVT_END_OPERATION, NULL, 0);
	if (r < 0)
		return r;
	if (im_protocols) {
		/*
		 * enable polling according to im_protocols & tm_protocols
		 * - CLOSE pipe according to im_protocols & tm_protocols
		 */
		if ((NFC_HCI_RF_READER_B_GATE & im_protocols) == 0) {
			r = nfc_hci_disconnect_gate(hdev,
					NFC_HCI_RF_READER_B_GATE);
			if (r < 0)
				return r;
		}

		if ((NFC_HCI_RF_READER_A_GATE & im_protocols) == 0) {
			r = nfc_hci_disconnect_gate(hdev,
					NFC_HCI_RF_READER_A_GATE);
			if (r < 0)
				return r;
		}

		if ((ST21NFCA_RF_READER_F_GATE & im_protocols) == 0) {
			r = nfc_hci_disconnect_gate(hdev,
					ST21NFCA_RF_READER_F_GATE);
			if (r < 0)
				return r;
		}

		if ((ST21NFCA_RF_READER_14443_3_A_GATE & im_protocols) == 0) {
			r = nfc_hci_disconnect_gate(hdev,
					ST21NFCA_RF_READER_14443_3_A_GATE);
			if (r < 0)
				return r;
		}

		if ((ST21NFCA_RF_READER_ISO15693_GATE & im_protocols) == 0) {
			r = nfc_hci_disconnect_gate(hdev,
					ST21NFCA_RF_READER_ISO15693_GATE);
			if (r < 0)
				return r;
		}

		r = nfc_hci_send_event(hdev, NFC_HCI_RF_READER_A_GATE,
				       NFC_HCI_EVT_READER_REQUESTED, NULL, 0);
		if (r < 0)
			nfc_hci_send_event(hdev, NFC_HCI_RF_READER_A_GATE,
					   NFC_HCI_EVT_END_OPERATION, NULL, 0);
	}
	return r;
}

static int st21nfca_get_iso14443_3_atqa(struct nfc_hci_dev *hdev, u16 *atqa)
{
	int r;
	struct sk_buff *atqa_skb = NULL;

	r = nfc_hci_get_param(hdev, ST21NFCA_RF_READER_14443_3_A_GATE,
			      ST21NFCA_RF_READER_14443_3_A_ATQA, &atqa_skb);
	if (r < 0)
		goto exit;

	if (atqa_skb->len != 2) {
		r = -EPROTO;
		goto exit;
	}

	*atqa = be16_to_cpu(*(u16 *) atqa_skb->data);

exit:
	kfree_skb(atqa_skb);
	return r;
}

static int st21nfca_get_iso14443_3_sak(struct nfc_hci_dev *hdev, u8 *sak)
{
	int r;
	struct sk_buff *sak_skb = NULL;

	r = nfc_hci_get_param(hdev, ST21NFCA_RF_READER_14443_3_A_GATE,
			      ST21NFCA_RF_READER_14443_3_A_SAK, &sak_skb);
	if (r < 0)
		goto exit;

	if (sak_skb->len != 1) {
		r = -EPROTO;
		goto exit;
	}

	*sak = sak_skb->data[0];

exit:
	kfree_skb(sak_skb);
	return r;
}

static int st21nfca_get_iso14443_3_uid(struct nfc_hci_dev *hdev, u8 *gate,
				       int *len)
{
	int r;
	struct sk_buff *uid_skb = NULL;

	r = nfc_hci_get_param(hdev, ST21NFCA_RF_READER_14443_3_A_GATE,
			      ST21NFCA_RF_READER_14443_3_A_UID, &uid_skb);
	if (r < 0)
		goto exit;

	if (uid_skb->len == 0 || uid_skb->len > NFC_NFCID1_MAXSIZE) {
		r = -EPROTO;
		goto exit;
	}

	gate = uid_skb->data;
	*len = uid_skb->len;
exit:
	kfree_skb(uid_skb);
	return r;
}

static int st21nfca_hci_target_from_gate(struct nfc_hci_dev *hdev, u8 gate,
					 struct nfc_target *target)
{
	int r, len;
	u16 atqa;
	u8 sak;
	u8 uid[NFC_NFCID1_MAXSIZE];

	switch (gate) {
	case ST21NFCA_RF_READER_F_GATE:
		target->supported_protocols = NFC_PROTO_FELICA_MASK;
		break;
	case ST21NFCA_RF_READER_14443_3_A_GATE:
		/* ISO14443-3 type 1 or 2 tags */
		r = st21nfca_get_iso14443_3_atqa(hdev, &atqa);
		if (r < 0)
			return r;
		if (atqa == 0x000c) {
			target->supported_protocols = NFC_PROTO_JEWEL_MASK;
			target->sens_res = 0x0c00;
		} else {
			r = st21nfca_get_iso14443_3_sak(hdev, &sak);
			if (r < 0)
				return r;

			r = st21nfca_get_iso14443_3_uid(hdev, uid, &len);
			if (r < 0)
				return r;

			target->supported_protocols =
			    nfc_hci_sak_to_protocol(sak);
			if (target->supported_protocols == 0xffffffff)
				return -EPROTO;

			target->sens_res = atqa;
			target->sel_res = sak;
			memcpy(target->nfcid1, uid, len);
			target->nfcid1_len = len;
		}

		break;
	default:
		return -EPROTO;
	}

	return 0;
}

/*
 * Returns:
 * <= 0: driver handled the data exchange
 *    1: driver doesn't especially handle, please do standard processing
 */
static int st21nfca_hci_im_transceive(struct nfc_hci_dev *hdev,
				      struct nfc_target *target,
				      struct sk_buff *skb,
				      data_exchange_cb_t cb, void *cb_context)
{
	pr_info(DRIVER_DESC ": %s for gate=%d len=%d\n", __func__,
		target->hci_reader_gate, skb->len);

	switch (target->hci_reader_gate) {
	case ST21NFCA_RF_READER_F_GATE:
		*skb_push(skb, 1) = 0x1a;
		return nfc_hci_send_cmd_async(hdev, target->hci_reader_gate,
					      ST21NFCA_WR_XCHG_DATA, skb->data,
					      skb->len, cb, cb_context);
	case ST21NFCA_RF_READER_14443_3_A_GATE:
		*skb_push(skb, 1) = 0x1a;	/* CTR, see spec:10.2.2.1 */

		return nfc_hci_send_cmd_async(hdev, target->hci_reader_gate,
					      ST21NFCA_WR_XCHG_DATA, skb->data,
					      skb->len, cb, cb_context);
	default:
		return 1;
	}
}

static int st21nfca_hci_check_presence(struct nfc_hci_dev *hdev,
				       struct nfc_target *target)
{
	u8 fwi = 0x11;
	switch (target->hci_reader_gate) {
	case NFC_HCI_RF_READER_A_GATE:
	case NFC_HCI_RF_READER_B_GATE:
		/*
		 * PRESENCE_CHECK on those gates is available
		 * However, the answer to this command is taking 3 * fwi
		 * if the card is no present.
		 * Instead, we send an empty I-Frame with a very short
		 * configurable fwi ~604µs.
		 */
		return nfc_hci_send_cmd(hdev, target->hci_reader_gate,
					ST21NFCA_WR_XCHG_DATA, &fwi, 1, NULL);
	case ST21NFCA_RF_READER_14443_3_A_GATE:
		return nfc_hci_send_cmd(hdev, target->hci_reader_gate,
					ST21NFCA_RF_READER_CMD_PRESENCE_CHECK,
					NULL, 0, NULL);
	default:
		return -EOPNOTSUPP;
	}
}

static struct nfc_hci_ops st21nfca_hci_ops = {
	.open = st21nfca_hci_open,
	.close = st21nfca_hci_close,
	.hci_ready = st21nfca_hci_ready,
	.xmit = st21nfca_hci_xmit,
	.start_poll = st21nfca_hci_start_poll,
	.target_from_gate = st21nfca_hci_target_from_gate,
	.im_transceive = st21nfca_hci_im_transceive,
	.check_presence = st21nfca_hci_check_presence,
};

int st21nfca_hci_probe(void *phy_id, struct nfc_phy_ops *phy_ops,
		       char *llc_name, int phy_headroom, int phy_tailroom,
		       int phy_payload, struct nfc_hci_dev **hdev)
{
	struct st21nfca_hci_info *info;
	int r = 0;
	int dev_num;
	u32 protocols;
	struct nfc_hci_init_data init_data;
	unsigned long quirks = 0;

	info = kzalloc(sizeof(struct st21nfca_hci_info), GFP_KERNEL);
	if (!info) {
		r = -ENOMEM;
		goto err_alloc_hdev;
	}

	info->phy_ops = phy_ops;
	info->phy_id = phy_id;
	info->state = ST21NFCA_ST_COLD;
	mutex_init(&info->info_lock);

	init_data.gate_count = ARRAY_SIZE(st21nfca_gates);

	memcpy(init_data.gates, st21nfca_gates, sizeof(st21nfca_gates));

	/*
	 * Session id must include the driver name + i2c bus addr
	 * persistent info to discriminate 2 identical chips
	 */
	dev_num = find_first_zero_bit(dev_mask, ST21NFCA_NUM_DEVICES);
	if (dev_num >= ST21NFCA_NUM_DEVICES)
		goto err_alloc_hdev;

	scnprintf(init_data.session_id, sizeof(init_data.session_id), "%s%2x",
		  "ST21AH", dev_num);

	protocols = NFC_PROTO_JEWEL_MASK |
	    NFC_PROTO_MIFARE_MASK |
	    NFC_PROTO_FELICA_MASK |
	    NFC_PROTO_ISO14443_MASK |
	    NFC_PROTO_ISO14443_B_MASK;

	set_bit(NFC_HCI_QUIRK_SHORT_CLEAR, &quirks);

	info->hdev =
	    nfc_hci_allocate_device(&st21nfca_hci_ops, &init_data, quirks,
				    protocols, llc_name,
				    phy_headroom + ST21NFCA_CMDS_HEADROOM,
				    phy_tailroom, phy_payload);

	if (!info->hdev) {
		pr_err("Cannot allocate nfc hdev.\n");
		r = -ENOMEM;
		goto err_alloc_hdev;
	}

	nfc_hci_set_clientdata(info->hdev, info);

	r = nfc_hci_register_device(info->hdev);
	if (r)
		goto err_regdev;

	*hdev = info->hdev;

	return 0;

err_regdev:
	nfc_hci_free_device(info->hdev);

err_alloc_hdev:
	kfree(info);

	return r;
}
EXPORT_SYMBOL(st21nfca_hci_probe);

void st21nfca_hci_remove(struct nfc_hci_dev *hdev)
{
	struct st21nfca_hci_info *info = nfc_hci_get_clientdata(hdev);

	nfc_hci_unregister_device(hdev);
	nfc_hci_free_device(hdev);
	kfree(info);
}
EXPORT_SYMBOL(st21nfca_hci_remove);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION(DRIVER_DESC);
