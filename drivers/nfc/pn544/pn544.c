// SPDX-License-Identifier: GPL-2.0-only
/*
 * HCI based Driver for NXP PN544 NFC Chip
 *
 * Copyright (C) 2012  Intel Corporation. All rights reserved.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/module.h>

#include <linux/nfc.h>
#include <net/nfc/hci.h>
#include <net/nfc/llc.h>

#include "pn544.h"

/* Timing restrictions (ms) */
#define PN544_HCI_RESETVEN_TIME		30

enum pn544_state {
	PN544_ST_COLD,
	PN544_ST_FW_READY,
	PN544_ST_READY,
};

#define FULL_VERSION_LEN 11

/* Proprietary commands */
#define PN544_WRITE		0x3f
#define PN544_TEST_SWP		0x21

/* Proprietary gates, events, commands and registers */

/* NFC_HCI_RF_READER_A_GATE additional registers and commands */
#define PN544_RF_READER_A_AUTO_ACTIVATION			0x10
#define PN544_RF_READER_A_CMD_CONTINUE_ACTIVATION		0x12
#define PN544_MIFARE_CMD					0x21

/* Commands that apply to all RF readers */
#define PN544_RF_READER_CMD_PRESENCE_CHECK	0x30
#define PN544_RF_READER_CMD_ACTIVATE_NEXT	0x32

/* NFC_HCI_ID_MGMT_GATE additional registers */
#define PN544_ID_MGMT_FULL_VERSION_SW		0x10

#define PN544_RF_READER_ISO15693_GATE		0x12

#define PN544_RF_READER_F_GATE			0x14
#define PN544_FELICA_ID				0x04
#define PN544_FELICA_RAW			0x20

#define PN544_RF_READER_JEWEL_GATE		0x15
#define PN544_JEWEL_RAW_CMD			0x23

#define PN544_RF_READER_NFCIP1_INITIATOR_GATE	0x30
#define PN544_RF_READER_NFCIP1_TARGET_GATE	0x31

#define PN544_SYS_MGMT_GATE			0x90
#define PN544_SYS_MGMT_INFO_NOTIFICATION	0x02

#define PN544_POLLING_LOOP_MGMT_GATE		0x94
#define PN544_DEP_MODE				0x01
#define PN544_DEP_ATR_REQ			0x02
#define PN544_DEP_ATR_RES			0x03
#define PN544_DEP_MERGE				0x0D
#define PN544_PL_RDPHASES			0x06
#define PN544_PL_EMULATION			0x07
#define PN544_PL_NFCT_DEACTIVATED		0x09

#define PN544_SWP_MGMT_GATE			0xA0
#define PN544_SWP_DEFAULT_MODE			0x01

#define PN544_NFC_WI_MGMT_GATE			0xA1
#define PN544_NFC_ESE_DEFAULT_MODE		0x01

#define PN544_HCI_EVT_SND_DATA			0x01
#define PN544_HCI_EVT_ACTIVATED			0x02
#define PN544_HCI_EVT_DEACTIVATED		0x03
#define PN544_HCI_EVT_RCV_DATA			0x04
#define PN544_HCI_EVT_CONTINUE_MI		0x05
#define PN544_HCI_EVT_SWITCH_MODE		0x03

#define PN544_HCI_CMD_ATTREQUEST		0x12
#define PN544_HCI_CMD_CONTINUE_ACTIVATION	0x13

static struct nfc_hci_gate pn544_gates[] = {
	{NFC_HCI_ADMIN_GATE, NFC_HCI_INVALID_PIPE},
	{NFC_HCI_LOOPBACK_GATE, NFC_HCI_INVALID_PIPE},
	{NFC_HCI_ID_MGMT_GATE, NFC_HCI_INVALID_PIPE},
	{NFC_HCI_LINK_MGMT_GATE, NFC_HCI_INVALID_PIPE},
	{NFC_HCI_RF_READER_B_GATE, NFC_HCI_INVALID_PIPE},
	{NFC_HCI_RF_READER_A_GATE, NFC_HCI_INVALID_PIPE},
	{PN544_SYS_MGMT_GATE, NFC_HCI_INVALID_PIPE},
	{PN544_SWP_MGMT_GATE, NFC_HCI_INVALID_PIPE},
	{PN544_POLLING_LOOP_MGMT_GATE, NFC_HCI_INVALID_PIPE},
	{PN544_NFC_WI_MGMT_GATE, NFC_HCI_INVALID_PIPE},
	{PN544_RF_READER_F_GATE, NFC_HCI_INVALID_PIPE},
	{PN544_RF_READER_JEWEL_GATE, NFC_HCI_INVALID_PIPE},
	{PN544_RF_READER_ISO15693_GATE, NFC_HCI_INVALID_PIPE},
	{PN544_RF_READER_NFCIP1_INITIATOR_GATE, NFC_HCI_INVALID_PIPE},
	{PN544_RF_READER_NFCIP1_TARGET_GATE, NFC_HCI_INVALID_PIPE}
};

/* Largest headroom needed for outgoing custom commands */
#define PN544_CMDS_HEADROOM	2

struct pn544_hci_info {
	struct nfc_phy_ops *phy_ops;
	void *phy_id;

	struct nfc_hci_dev *hdev;

	enum pn544_state state;

	struct mutex info_lock;

	int async_cb_type;
	data_exchange_cb_t async_cb;
	void *async_cb_context;

	fw_download_t fw_download;
};

static int pn544_hci_open(struct nfc_hci_dev *hdev)
{
	struct pn544_hci_info *info = nfc_hci_get_clientdata(hdev);
	int r = 0;

	mutex_lock(&info->info_lock);

	if (info->state != PN544_ST_COLD) {
		r = -EBUSY;
		goto out;
	}

	r = info->phy_ops->enable(info->phy_id);

	if (r == 0)
		info->state = PN544_ST_READY;

out:
	mutex_unlock(&info->info_lock);
	return r;
}

static void pn544_hci_close(struct nfc_hci_dev *hdev)
{
	struct pn544_hci_info *info = nfc_hci_get_clientdata(hdev);

	mutex_lock(&info->info_lock);

	if (info->state == PN544_ST_COLD)
		goto out;

	info->phy_ops->disable(info->phy_id);

	info->state = PN544_ST_COLD;

out:
	mutex_unlock(&info->info_lock);
}

static int pn544_hci_ready(struct nfc_hci_dev *hdev)
{
	struct sk_buff *skb;
	static struct hw_config {
		u8 adr[2];
		u8 value;
	} hw_config[] = {
		{{0x9f, 0x9a}, 0x00},

		{{0x98, 0x10}, 0xbc},

		{{0x9e, 0x71}, 0x00},

		{{0x98, 0x09}, 0x00},

		{{0x9e, 0xb4}, 0x00},

		{{0x9c, 0x01}, 0x08},

		{{0x9e, 0xaa}, 0x01},

		{{0x9b, 0xd1}, 0x17},
		{{0x9b, 0xd2}, 0x58},
		{{0x9b, 0xd3}, 0x10},
		{{0x9b, 0xd4}, 0x47},
		{{0x9b, 0xd5}, 0x0c},
		{{0x9b, 0xd6}, 0x37},
		{{0x9b, 0xdd}, 0x33},

		{{0x9b, 0x84}, 0x00},
		{{0x99, 0x81}, 0x79},
		{{0x99, 0x31}, 0x79},

		{{0x98, 0x00}, 0x3f},

		{{0x9f, 0x09}, 0x02},

		{{0x9f, 0x0a}, 0x05},

		{{0x9e, 0xd1}, 0xa1},
		{{0x99, 0x23}, 0x01},

		{{0x9e, 0x74}, 0x00},
		{{0x9e, 0x90}, 0x00},
		{{0x9f, 0x28}, 0x10},

		{{0x9f, 0x35}, 0x04},

		{{0x9f, 0x36}, 0x11},

		{{0x9c, 0x31}, 0x00},

		{{0x9c, 0x32}, 0x00},

		{{0x9c, 0x19}, 0x0a},

		{{0x9c, 0x1a}, 0x0a},

		{{0x9c, 0x0c}, 0x00},

		{{0x9c, 0x0d}, 0x00},

		{{0x9c, 0x12}, 0x00},

		{{0x9c, 0x13}, 0x00},

		{{0x98, 0xa2}, 0x09},

		{{0x98, 0x93}, 0x00},

		{{0x98, 0x7d}, 0x08},
		{{0x98, 0x7e}, 0x00},
		{{0x9f, 0xc8}, 0x00},
	};
	struct hw_config *p = hw_config;
	int count = ARRAY_SIZE(hw_config);
	struct sk_buff *res_skb;
	u8 param[4];
	int r;

	param[0] = 0;
	while (count--) {
		param[1] = p->adr[0];
		param[2] = p->adr[1];
		param[3] = p->value;

		r = nfc_hci_send_cmd(hdev, PN544_SYS_MGMT_GATE, PN544_WRITE,
				     param, 4, &res_skb);
		if (r < 0)
			return r;

		if (res_skb->len != 1) {
			kfree_skb(res_skb);
			return -EPROTO;
		}

		if (res_skb->data[0] != p->value) {
			kfree_skb(res_skb);
			return -EIO;
		}

		kfree_skb(res_skb);

		p++;
	}

	param[0] = NFC_HCI_UICC_HOST_ID;
	r = nfc_hci_set_param(hdev, NFC_HCI_ADMIN_GATE,
			      NFC_HCI_ADMIN_WHITELIST, param, 1);
	if (r < 0)
		return r;

	param[0] = 0x3d;
	r = nfc_hci_set_param(hdev, PN544_SYS_MGMT_GATE,
			      PN544_SYS_MGMT_INFO_NOTIFICATION, param, 1);
	if (r < 0)
		return r;

	param[0] = 0x0;
	r = nfc_hci_set_param(hdev, NFC_HCI_RF_READER_A_GATE,
			      PN544_RF_READER_A_AUTO_ACTIVATION, param, 1);
	if (r < 0)
		return r;

	r = nfc_hci_send_event(hdev, NFC_HCI_RF_READER_A_GATE,
			       NFC_HCI_EVT_END_OPERATION, NULL, 0);
	if (r < 0)
		return r;

	param[0] = 0x1;
	r = nfc_hci_set_param(hdev, PN544_POLLING_LOOP_MGMT_GATE,
			      PN544_PL_NFCT_DEACTIVATED, param, 1);
	if (r < 0)
		return r;

	param[0] = 0x0;
	r = nfc_hci_set_param(hdev, PN544_POLLING_LOOP_MGMT_GATE,
			      PN544_PL_RDPHASES, param, 1);
	if (r < 0)
		return r;

	r = nfc_hci_get_param(hdev, NFC_HCI_ID_MGMT_GATE,
			      PN544_ID_MGMT_FULL_VERSION_SW, &skb);
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

static int pn544_hci_xmit(struct nfc_hci_dev *hdev, struct sk_buff *skb)
{
	struct pn544_hci_info *info = nfc_hci_get_clientdata(hdev);

	return info->phy_ops->write(info->phy_id, skb);
}

static int pn544_hci_start_poll(struct nfc_hci_dev *hdev,
				u32 im_protocols, u32 tm_protocols)
{
	u8 phases = 0;
	int r;
	u8 duration[2];
	u8 activated;
	u8 i_mode = 0x3f; /* Enable all supported modes */
	u8 t_mode = 0x0f;
	u8 t_merge = 0x01; /* Enable merge by default */

	pr_info(DRIVER_DESC ": %s protocols 0x%x 0x%x\n",
		__func__, im_protocols, tm_protocols);

	r = nfc_hci_send_event(hdev, NFC_HCI_RF_READER_A_GATE,
			       NFC_HCI_EVT_END_OPERATION, NULL, 0);
	if (r < 0)
		return r;

	duration[0] = 0x18;
	duration[1] = 0x6a;
	r = nfc_hci_set_param(hdev, PN544_POLLING_LOOP_MGMT_GATE,
			      PN544_PL_EMULATION, duration, 2);
	if (r < 0)
		return r;

	activated = 0;
	r = nfc_hci_set_param(hdev, PN544_POLLING_LOOP_MGMT_GATE,
			      PN544_PL_NFCT_DEACTIVATED, &activated, 1);
	if (r < 0)
		return r;

	if (im_protocols & (NFC_PROTO_ISO14443_MASK | NFC_PROTO_MIFARE_MASK |
			 NFC_PROTO_JEWEL_MASK))
		phases |= 1;		/* Type A */
	if (im_protocols & NFC_PROTO_FELICA_MASK) {
		phases |= (1 << 2);	/* Type F 212 */
		phases |= (1 << 3);	/* Type F 424 */
	}

	phases |= (1 << 5);		/* NFC active */

	r = nfc_hci_set_param(hdev, PN544_POLLING_LOOP_MGMT_GATE,
			      PN544_PL_RDPHASES, &phases, 1);
	if (r < 0)
		return r;

	if ((im_protocols | tm_protocols) & NFC_PROTO_NFC_DEP_MASK) {
		hdev->gb = nfc_get_local_general_bytes(hdev->ndev,
							&hdev->gb_len);
		pr_debug("generate local bytes %p\n", hdev->gb);
		if (hdev->gb == NULL || hdev->gb_len == 0) {
			im_protocols &= ~NFC_PROTO_NFC_DEP_MASK;
			tm_protocols &= ~NFC_PROTO_NFC_DEP_MASK;
		}
	}

	if (im_protocols & NFC_PROTO_NFC_DEP_MASK) {
		r = nfc_hci_send_event(hdev,
				PN544_RF_READER_NFCIP1_INITIATOR_GATE,
				NFC_HCI_EVT_END_OPERATION, NULL, 0);
		if (r < 0)
			return r;

		r = nfc_hci_set_param(hdev,
				PN544_RF_READER_NFCIP1_INITIATOR_GATE,
				PN544_DEP_MODE, &i_mode, 1);
		if (r < 0)
			return r;

		r = nfc_hci_set_param(hdev,
				PN544_RF_READER_NFCIP1_INITIATOR_GATE,
				PN544_DEP_ATR_REQ, hdev->gb, hdev->gb_len);
		if (r < 0)
			return r;

		r = nfc_hci_send_event(hdev,
				PN544_RF_READER_NFCIP1_INITIATOR_GATE,
				NFC_HCI_EVT_READER_REQUESTED, NULL, 0);
		if (r < 0)
			nfc_hci_send_event(hdev,
					PN544_RF_READER_NFCIP1_INITIATOR_GATE,
					NFC_HCI_EVT_END_OPERATION, NULL, 0);
	}

	if (tm_protocols & NFC_PROTO_NFC_DEP_MASK) {
		r = nfc_hci_set_param(hdev, PN544_RF_READER_NFCIP1_TARGET_GATE,
				PN544_DEP_MODE, &t_mode, 1);
		if (r < 0)
			return r;

		r = nfc_hci_set_param(hdev, PN544_RF_READER_NFCIP1_TARGET_GATE,
				PN544_DEP_ATR_RES, hdev->gb, hdev->gb_len);
		if (r < 0)
			return r;

		r = nfc_hci_set_param(hdev, PN544_RF_READER_NFCIP1_TARGET_GATE,
				PN544_DEP_MERGE, &t_merge, 1);
		if (r < 0)
			return r;
	}

	r = nfc_hci_send_event(hdev, NFC_HCI_RF_READER_A_GATE,
			       NFC_HCI_EVT_READER_REQUESTED, NULL, 0);
	if (r < 0)
		nfc_hci_send_event(hdev, NFC_HCI_RF_READER_A_GATE,
				   NFC_HCI_EVT_END_OPERATION, NULL, 0);

	return r;
}

static int pn544_hci_dep_link_up(struct nfc_hci_dev *hdev,
				struct nfc_target *target, u8 comm_mode,
				u8 *gb, size_t gb_len)
{
	struct sk_buff *rgb_skb = NULL;
	int r;

	r = nfc_hci_get_param(hdev, target->hci_reader_gate,
				PN544_DEP_ATR_RES, &rgb_skb);
	if (r < 0)
		return r;

	if (rgb_skb->len == 0 || rgb_skb->len > NFC_GB_MAXSIZE) {
		r = -EPROTO;
		goto exit;
	}
	print_hex_dump(KERN_DEBUG, "remote gb: ", DUMP_PREFIX_OFFSET,
			16, 1, rgb_skb->data, rgb_skb->len, true);

	r = nfc_set_remote_general_bytes(hdev->ndev, rgb_skb->data,
						rgb_skb->len);

	if (r == 0)
		r = nfc_dep_link_is_up(hdev->ndev, target->idx, comm_mode,
					NFC_RF_INITIATOR);
exit:
	kfree_skb(rgb_skb);
	return r;
}

static int pn544_hci_dep_link_down(struct nfc_hci_dev *hdev)
{

	return nfc_hci_send_event(hdev, PN544_RF_READER_NFCIP1_INITIATOR_GATE,
					NFC_HCI_EVT_END_OPERATION, NULL, 0);
}

static int pn544_hci_target_from_gate(struct nfc_hci_dev *hdev, u8 gate,
				      struct nfc_target *target)
{
	switch (gate) {
	case PN544_RF_READER_F_GATE:
		target->supported_protocols = NFC_PROTO_FELICA_MASK;
		break;
	case PN544_RF_READER_JEWEL_GATE:
		target->supported_protocols = NFC_PROTO_JEWEL_MASK;
		target->sens_res = 0x0c00;
		break;
	case PN544_RF_READER_NFCIP1_INITIATOR_GATE:
		target->supported_protocols = NFC_PROTO_NFC_DEP_MASK;
		break;
	default:
		return -EPROTO;
	}

	return 0;
}

static int pn544_hci_complete_target_discovered(struct nfc_hci_dev *hdev,
						u8 gate,
						struct nfc_target *target)
{
	struct sk_buff *uid_skb;
	int r = 0;

	if (gate == PN544_RF_READER_NFCIP1_INITIATOR_GATE)
		return r;

	if (target->supported_protocols & NFC_PROTO_NFC_DEP_MASK) {
		r = nfc_hci_send_cmd(hdev,
			PN544_RF_READER_NFCIP1_INITIATOR_GATE,
			PN544_HCI_CMD_CONTINUE_ACTIVATION, NULL, 0, NULL);
		if (r < 0)
			return r;

		target->hci_reader_gate = PN544_RF_READER_NFCIP1_INITIATOR_GATE;
	} else if (target->supported_protocols & NFC_PROTO_MIFARE_MASK) {
		if (target->nfcid1_len != 4 && target->nfcid1_len != 7 &&
		    target->nfcid1_len != 10)
			return -EPROTO;

		r = nfc_hci_send_cmd(hdev, NFC_HCI_RF_READER_A_GATE,
				     PN544_RF_READER_CMD_ACTIVATE_NEXT,
				     target->nfcid1, target->nfcid1_len, NULL);
	} else if (target->supported_protocols & NFC_PROTO_FELICA_MASK) {
		r = nfc_hci_get_param(hdev, PN544_RF_READER_F_GATE,
				      PN544_FELICA_ID, &uid_skb);
		if (r < 0)
			return r;

		if (uid_skb->len != 8) {
			kfree_skb(uid_skb);
			return -EPROTO;
		}

		/* Type F NFC-DEP IDm has prefix 0x01FE */
		if ((uid_skb->data[0] == 0x01) && (uid_skb->data[1] == 0xfe)) {
			kfree_skb(uid_skb);
			r = nfc_hci_send_cmd(hdev,
					PN544_RF_READER_NFCIP1_INITIATOR_GATE,
					PN544_HCI_CMD_CONTINUE_ACTIVATION,
					NULL, 0, NULL);
			if (r < 0)
				return r;

			target->supported_protocols = NFC_PROTO_NFC_DEP_MASK;
			target->hci_reader_gate =
				PN544_RF_READER_NFCIP1_INITIATOR_GATE;
		} else {
			r = nfc_hci_send_cmd(hdev, PN544_RF_READER_F_GATE,
					     PN544_RF_READER_CMD_ACTIVATE_NEXT,
					     uid_skb->data, uid_skb->len, NULL);
			kfree_skb(uid_skb);
		}
	} else if (target->supported_protocols & NFC_PROTO_ISO14443_MASK) {
		/*
		 * TODO: maybe other ISO 14443 require some kind of continue
		 * activation, but for now we've seen only this one below.
		 */
		if (target->sens_res == 0x4403)	/* Type 4 Mifare DESFire */
			r = nfc_hci_send_cmd(hdev, NFC_HCI_RF_READER_A_GATE,
			      PN544_RF_READER_A_CMD_CONTINUE_ACTIVATION,
			      NULL, 0, NULL);
	}

	return r;
}

#define PN544_CB_TYPE_READER_F 1

static void pn544_hci_data_exchange_cb(void *context, struct sk_buff *skb,
				       int err)
{
	struct pn544_hci_info *info = context;

	switch (info->async_cb_type) {
	case PN544_CB_TYPE_READER_F:
		if (err == 0)
			skb_pull(skb, 1);
		info->async_cb(info->async_cb_context, skb, err);
		break;
	default:
		if (err == 0)
			kfree_skb(skb);
		break;
	}
}

#define MIFARE_CMD_AUTH_KEY_A	0x60
#define MIFARE_CMD_AUTH_KEY_B	0x61
#define MIFARE_CMD_HEADER	2
#define MIFARE_UID_LEN		4
#define MIFARE_KEY_LEN		6
#define MIFARE_CMD_LEN		12
/*
 * Returns:
 * <= 0: driver handled the data exchange
 *    1: driver doesn't especially handle, please do standard processing
 */
static int pn544_hci_im_transceive(struct nfc_hci_dev *hdev,
				   struct nfc_target *target,
				   struct sk_buff *skb, data_exchange_cb_t cb,
				   void *cb_context)
{
	struct pn544_hci_info *info = nfc_hci_get_clientdata(hdev);

	pr_info(DRIVER_DESC ": %s for gate=%d\n", __func__,
		target->hci_reader_gate);

	switch (target->hci_reader_gate) {
	case NFC_HCI_RF_READER_A_GATE:
		if (target->supported_protocols & NFC_PROTO_MIFARE_MASK) {
			/*
			 * It seems that pn544 is inverting key and UID for
			 * MIFARE authentication commands.
			 */
			if (skb->len == MIFARE_CMD_LEN &&
			    (skb->data[0] == MIFARE_CMD_AUTH_KEY_A ||
			     skb->data[0] == MIFARE_CMD_AUTH_KEY_B)) {
				u8 uid[MIFARE_UID_LEN];
				u8 *data = skb->data + MIFARE_CMD_HEADER;

				memcpy(uid, data + MIFARE_KEY_LEN,
				       MIFARE_UID_LEN);
				memmove(data + MIFARE_UID_LEN, data,
					MIFARE_KEY_LEN);
				memcpy(data, uid, MIFARE_UID_LEN);
			}

			return nfc_hci_send_cmd_async(hdev,
						      target->hci_reader_gate,
						      PN544_MIFARE_CMD,
						      skb->data, skb->len,
						      cb, cb_context);
		} else
			return 1;
	case PN544_RF_READER_F_GATE:
		*(u8 *)skb_push(skb, 1) = 0;
		*(u8 *)skb_push(skb, 1) = 0;

		info->async_cb_type = PN544_CB_TYPE_READER_F;
		info->async_cb = cb;
		info->async_cb_context = cb_context;

		return nfc_hci_send_cmd_async(hdev, target->hci_reader_gate,
					      PN544_FELICA_RAW, skb->data,
					      skb->len,
					      pn544_hci_data_exchange_cb, info);
	case PN544_RF_READER_JEWEL_GATE:
		return nfc_hci_send_cmd_async(hdev, target->hci_reader_gate,
					      PN544_JEWEL_RAW_CMD, skb->data,
					      skb->len, cb, cb_context);
	case PN544_RF_READER_NFCIP1_INITIATOR_GATE:
		*(u8 *)skb_push(skb, 1) = 0;

		return nfc_hci_send_event(hdev, target->hci_reader_gate,
					PN544_HCI_EVT_SND_DATA, skb->data,
					skb->len);
	default:
		return 1;
	}
}

static int pn544_hci_tm_send(struct nfc_hci_dev *hdev, struct sk_buff *skb)
{
	int r;

	/* Set default false for multiple information chaining */
	*(u8 *)skb_push(skb, 1) = 0;

	r = nfc_hci_send_event(hdev, PN544_RF_READER_NFCIP1_TARGET_GATE,
			       PN544_HCI_EVT_SND_DATA, skb->data, skb->len);

	kfree_skb(skb);

	return r;
}

static int pn544_hci_check_presence(struct nfc_hci_dev *hdev,
				   struct nfc_target *target)
{
	pr_debug("supported protocol %d\n", target->supported_protocols);
	if (target->supported_protocols & (NFC_PROTO_ISO14443_MASK |
					NFC_PROTO_ISO14443_B_MASK)) {
		return nfc_hci_send_cmd(hdev, target->hci_reader_gate,
					PN544_RF_READER_CMD_PRESENCE_CHECK,
					NULL, 0, NULL);
	} else if (target->supported_protocols & NFC_PROTO_MIFARE_MASK) {
		if (target->nfcid1_len != 4 && target->nfcid1_len != 7 &&
		    target->nfcid1_len != 10)
			return -EOPNOTSUPP;

		return nfc_hci_send_cmd(hdev, NFC_HCI_RF_READER_A_GATE,
				     PN544_RF_READER_CMD_ACTIVATE_NEXT,
				     target->nfcid1, target->nfcid1_len, NULL);
	} else if (target->supported_protocols & (NFC_PROTO_JEWEL_MASK |
						NFC_PROTO_FELICA_MASK)) {
		return -EOPNOTSUPP;
	} else if (target->supported_protocols & NFC_PROTO_NFC_DEP_MASK) {
		return nfc_hci_send_cmd(hdev, target->hci_reader_gate,
					PN544_HCI_CMD_ATTREQUEST,
					NULL, 0, NULL);
	}

	return 0;
}

/*
 * Returns:
 * <= 0: driver handled the event, skb consumed
 *    1: driver does not handle the event, please do standard processing
 */
static int pn544_hci_event_received(struct nfc_hci_dev *hdev, u8 pipe, u8 event,
				    struct sk_buff *skb)
{
	struct sk_buff *rgb_skb = NULL;
	u8 gate = hdev->pipes[pipe].gate;
	int r;

	pr_debug("hci event %d\n", event);
	switch (event) {
	case PN544_HCI_EVT_ACTIVATED:
		if (gate == PN544_RF_READER_NFCIP1_INITIATOR_GATE) {
			r = nfc_hci_target_discovered(hdev, gate);
		} else if (gate == PN544_RF_READER_NFCIP1_TARGET_GATE) {
			r = nfc_hci_get_param(hdev, gate, PN544_DEP_ATR_REQ,
					      &rgb_skb);
			if (r < 0)
				goto exit;

			r = nfc_tm_activated(hdev->ndev, NFC_PROTO_NFC_DEP_MASK,
					     NFC_COMM_PASSIVE, rgb_skb->data,
					     rgb_skb->len);

			kfree_skb(rgb_skb);
		} else {
			r = -EINVAL;
		}
		break;
	case PN544_HCI_EVT_DEACTIVATED:
		r = nfc_hci_send_event(hdev, gate, NFC_HCI_EVT_END_OPERATION,
				       NULL, 0);
		break;
	case PN544_HCI_EVT_RCV_DATA:
		if (skb->len < 2) {
			r = -EPROTO;
			goto exit;
		}

		if (skb->data[0] != 0) {
			pr_debug("data0 %d\n", skb->data[0]);
			r = -EPROTO;
			goto exit;
		}

		skb_pull(skb, 2);
		return nfc_tm_data_received(hdev->ndev, skb);
	default:
		return 1;
	}

exit:
	kfree_skb(skb);

	return r;
}

static int pn544_hci_fw_download(struct nfc_hci_dev *hdev,
				 const char *firmware_name)
{
	struct pn544_hci_info *info = nfc_hci_get_clientdata(hdev);

	if (info->fw_download == NULL)
		return -ENOTSUPP;

	return info->fw_download(info->phy_id, firmware_name, hdev->sw_romlib);
}

static int pn544_hci_discover_se(struct nfc_hci_dev *hdev)
{
	u32 se_idx = 0;
	u8 ese_mode = 0x01; /* Default mode */
	struct sk_buff *res_skb;
	int r;

	r = nfc_hci_send_cmd(hdev, PN544_SYS_MGMT_GATE, PN544_TEST_SWP,
			     NULL, 0, &res_skb);

	if (r == 0) {
		if (res_skb->len == 2 && res_skb->data[0] == 0x00)
			nfc_add_se(hdev->ndev, se_idx++, NFC_SE_UICC);

		kfree_skb(res_skb);
	}

	r = nfc_hci_send_event(hdev, PN544_NFC_WI_MGMT_GATE,
				PN544_HCI_EVT_SWITCH_MODE,
				&ese_mode, 1);
	if (r == 0)
		nfc_add_se(hdev->ndev, se_idx++, NFC_SE_EMBEDDED);

	return !se_idx;
}

#define PN544_SE_MODE_OFF	0x00
#define PN544_SE_MODE_ON	0x01
static int pn544_hci_enable_se(struct nfc_hci_dev *hdev, u32 se_idx)
{
	struct nfc_se *se;
	u8 enable = PN544_SE_MODE_ON;
	static struct uicc_gatelist {
		u8 head;
		u8 adr[2];
		u8 value;
	} uicc_gatelist[] = {
		{0x00, {0x9e, 0xd9}, 0x23},
		{0x00, {0x9e, 0xda}, 0x21},
		{0x00, {0x9e, 0xdb}, 0x22},
		{0x00, {0x9e, 0xdc}, 0x24},
	};
	struct uicc_gatelist *p = uicc_gatelist;
	int count = ARRAY_SIZE(uicc_gatelist);
	struct sk_buff *res_skb;
	int r;

	se = nfc_find_se(hdev->ndev, se_idx);

	switch (se->type) {
	case NFC_SE_UICC:
		while (count--) {
			r = nfc_hci_send_cmd(hdev, PN544_SYS_MGMT_GATE,
					PN544_WRITE, (u8 *)p, 4, &res_skb);
			if (r < 0)
				return r;

			if (res_skb->len != 1) {
				kfree_skb(res_skb);
				return -EPROTO;
			}

			if (res_skb->data[0] != p->value) {
				kfree_skb(res_skb);
				return -EIO;
			}

			kfree_skb(res_skb);

			p++;
		}

		return nfc_hci_set_param(hdev, PN544_SWP_MGMT_GATE,
			      PN544_SWP_DEFAULT_MODE, &enable, 1);
	case NFC_SE_EMBEDDED:
		return nfc_hci_set_param(hdev, PN544_NFC_WI_MGMT_GATE,
			      PN544_NFC_ESE_DEFAULT_MODE, &enable, 1);

	default:
		return -EINVAL;
	}
}

static int pn544_hci_disable_se(struct nfc_hci_dev *hdev, u32 se_idx)
{
	struct nfc_se *se;
	u8 disable = PN544_SE_MODE_OFF;

	se = nfc_find_se(hdev->ndev, se_idx);

	switch (se->type) {
	case NFC_SE_UICC:
		return nfc_hci_set_param(hdev, PN544_SWP_MGMT_GATE,
			      PN544_SWP_DEFAULT_MODE, &disable, 1);
	case NFC_SE_EMBEDDED:
		return nfc_hci_set_param(hdev, PN544_NFC_WI_MGMT_GATE,
			      PN544_NFC_ESE_DEFAULT_MODE, &disable, 1);
	default:
		return -EINVAL;
	}
}

static struct nfc_hci_ops pn544_hci_ops = {
	.open = pn544_hci_open,
	.close = pn544_hci_close,
	.hci_ready = pn544_hci_ready,
	.xmit = pn544_hci_xmit,
	.start_poll = pn544_hci_start_poll,
	.dep_link_up = pn544_hci_dep_link_up,
	.dep_link_down = pn544_hci_dep_link_down,
	.target_from_gate = pn544_hci_target_from_gate,
	.complete_target_discovered = pn544_hci_complete_target_discovered,
	.im_transceive = pn544_hci_im_transceive,
	.tm_send = pn544_hci_tm_send,
	.check_presence = pn544_hci_check_presence,
	.event_received = pn544_hci_event_received,
	.fw_download = pn544_hci_fw_download,
	.discover_se = pn544_hci_discover_se,
	.enable_se = pn544_hci_enable_se,
	.disable_se = pn544_hci_disable_se,
};

int pn544_hci_probe(void *phy_id, struct nfc_phy_ops *phy_ops, char *llc_name,
		    int phy_headroom, int phy_tailroom, int phy_payload,
		    fw_download_t fw_download, struct nfc_hci_dev **hdev)
{
	struct pn544_hci_info *info;
	u32 protocols;
	struct nfc_hci_init_data init_data;
	int r;

	info = kzalloc(sizeof(struct pn544_hci_info), GFP_KERNEL);
	if (!info) {
		r = -ENOMEM;
		goto err_info_alloc;
	}

	info->phy_ops = phy_ops;
	info->phy_id = phy_id;
	info->fw_download = fw_download;
	info->state = PN544_ST_COLD;
	mutex_init(&info->info_lock);

	init_data.gate_count = ARRAY_SIZE(pn544_gates);

	memcpy(init_data.gates, pn544_gates, sizeof(pn544_gates));

	/*
	 * TODO: Session id must include the driver name + some bus addr
	 * persistent info to discriminate 2 identical chips
	 */
	strcpy(init_data.session_id, "ID544HCI");

	protocols = NFC_PROTO_JEWEL_MASK |
		    NFC_PROTO_MIFARE_MASK |
		    NFC_PROTO_FELICA_MASK |
		    NFC_PROTO_ISO14443_MASK |
		    NFC_PROTO_ISO14443_B_MASK |
		    NFC_PROTO_NFC_DEP_MASK;

	info->hdev = nfc_hci_allocate_device(&pn544_hci_ops, &init_data, 0,
					     protocols, llc_name,
					     phy_headroom + PN544_CMDS_HEADROOM,
					     phy_tailroom, phy_payload);
	if (!info->hdev) {
		pr_err("Cannot allocate nfc hdev\n");
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

err_info_alloc:
	return r;
}
EXPORT_SYMBOL(pn544_hci_probe);

void pn544_hci_remove(struct nfc_hci_dev *hdev)
{
	struct pn544_hci_info *info = nfc_hci_get_clientdata(hdev);

	nfc_hci_unregister_device(hdev);
	nfc_hci_free_device(hdev);
	kfree(info);
}
EXPORT_SYMBOL(pn544_hci_remove);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION(DRIVER_DESC);
