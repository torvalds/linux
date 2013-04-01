/*
 * HCI based Driver for Inside Secure microread NFC Chip
 *
 * Copyright (C) 2013  Intel Corporation. All rights reserved.
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
 * along with this program; if not, write to the
 * Free Software Foundation, Inc.,
 * 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#include <linux/module.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/crc-ccitt.h>

#include <linux/nfc.h>
#include <net/nfc/nfc.h>
#include <net/nfc/hci.h>
#include <net/nfc/llc.h>

#include "microread.h"

/* Proprietary gates, events, commands and registers */
/* Admin */
#define MICROREAD_GATE_ID_ADM NFC_HCI_ADMIN_GATE
#define MICROREAD_GATE_ID_MGT 0x01
#define MICROREAD_GATE_ID_OS 0x02
#define MICROREAD_GATE_ID_TESTRF 0x03
#define MICROREAD_GATE_ID_LOOPBACK NFC_HCI_LOOPBACK_GATE
#define MICROREAD_GATE_ID_IDT NFC_HCI_ID_MGMT_GATE
#define MICROREAD_GATE_ID_LMS NFC_HCI_LINK_MGMT_GATE

/* Reader */
#define MICROREAD_GATE_ID_MREAD_GEN 0x10
#define MICROREAD_GATE_ID_MREAD_ISO_B NFC_HCI_RF_READER_B_GATE
#define MICROREAD_GATE_ID_MREAD_NFC_T1 0x12
#define MICROREAD_GATE_ID_MREAD_ISO_A NFC_HCI_RF_READER_A_GATE
#define MICROREAD_GATE_ID_MREAD_NFC_T3 0x14
#define MICROREAD_GATE_ID_MREAD_ISO_15_3 0x15
#define MICROREAD_GATE_ID_MREAD_ISO_15_2 0x16
#define MICROREAD_GATE_ID_MREAD_ISO_B_3 0x17
#define MICROREAD_GATE_ID_MREAD_BPRIME 0x18
#define MICROREAD_GATE_ID_MREAD_ISO_A_3 0x19

/* Card */
#define MICROREAD_GATE_ID_MCARD_GEN 0x20
#define MICROREAD_GATE_ID_MCARD_ISO_B 0x21
#define MICROREAD_GATE_ID_MCARD_BPRIME 0x22
#define MICROREAD_GATE_ID_MCARD_ISO_A 0x23
#define MICROREAD_GATE_ID_MCARD_NFC_T3 0x24
#define MICROREAD_GATE_ID_MCARD_ISO_15_3 0x25
#define MICROREAD_GATE_ID_MCARD_ISO_15_2 0x26
#define MICROREAD_GATE_ID_MCARD_ISO_B_2 0x27
#define MICROREAD_GATE_ID_MCARD_ISO_CUSTOM 0x28
#define MICROREAD_GATE_ID_SECURE_ELEMENT 0x2F

/* P2P */
#define MICROREAD_GATE_ID_P2P_GEN 0x30
#define MICROREAD_GATE_ID_P2P_TARGET 0x31
#define MICROREAD_PAR_P2P_TARGET_MODE 0x01
#define MICROREAD_PAR_P2P_TARGET_GT 0x04
#define MICROREAD_GATE_ID_P2P_INITIATOR 0x32
#define MICROREAD_PAR_P2P_INITIATOR_GI 0x01
#define MICROREAD_PAR_P2P_INITIATOR_GT 0x03

/* Those pipes are created/opened by default in the chip */
#define MICROREAD_PIPE_ID_LMS 0x00
#define MICROREAD_PIPE_ID_ADMIN 0x01
#define MICROREAD_PIPE_ID_MGT 0x02
#define MICROREAD_PIPE_ID_OS 0x03
#define MICROREAD_PIPE_ID_HDS_LOOPBACK 0x04
#define MICROREAD_PIPE_ID_HDS_IDT 0x05
#define MICROREAD_PIPE_ID_HDS_MCARD_ISO_B 0x08
#define MICROREAD_PIPE_ID_HDS_MCARD_ISO_BPRIME 0x09
#define MICROREAD_PIPE_ID_HDS_MCARD_ISO_A 0x0A
#define MICROREAD_PIPE_ID_HDS_MCARD_ISO_15_3 0x0B
#define MICROREAD_PIPE_ID_HDS_MCARD_ISO_15_2 0x0C
#define MICROREAD_PIPE_ID_HDS_MCARD_NFC_T3 0x0D
#define MICROREAD_PIPE_ID_HDS_MCARD_ISO_B_2 0x0E
#define MICROREAD_PIPE_ID_HDS_MCARD_CUSTOM 0x0F
#define MICROREAD_PIPE_ID_HDS_MREAD_ISO_B 0x10
#define MICROREAD_PIPE_ID_HDS_MREAD_NFC_T1 0x11
#define MICROREAD_PIPE_ID_HDS_MREAD_ISO_A 0x12
#define MICROREAD_PIPE_ID_HDS_MREAD_ISO_15_3 0x13
#define MICROREAD_PIPE_ID_HDS_MREAD_ISO_15_2 0x14
#define MICROREAD_PIPE_ID_HDS_MREAD_NFC_T3 0x15
#define MICROREAD_PIPE_ID_HDS_MREAD_ISO_B_3 0x16
#define MICROREAD_PIPE_ID_HDS_MREAD_BPRIME 0x17
#define MICROREAD_PIPE_ID_HDS_MREAD_ISO_A_3 0x18
#define MICROREAD_PIPE_ID_HDS_MREAD_GEN 0x1B
#define MICROREAD_PIPE_ID_HDS_STACKED_ELEMENT 0x1C
#define MICROREAD_PIPE_ID_HDS_INSTANCES 0x1D
#define MICROREAD_PIPE_ID_HDS_TESTRF 0x1E
#define MICROREAD_PIPE_ID_HDS_P2P_TARGET 0x1F
#define MICROREAD_PIPE_ID_HDS_P2P_INITIATOR 0x20

/* Events */
#define MICROREAD_EVT_MREAD_DISCOVERY_OCCURED NFC_HCI_EVT_TARGET_DISCOVERED
#define MICROREAD_EVT_MREAD_CARD_FOUND 0x3D
#define MICROREAD_EMCF_A_ATQA 0
#define MICROREAD_EMCF_A_SAK 2
#define MICROREAD_EMCF_A_LEN 3
#define MICROREAD_EMCF_A_UID 4
#define MICROREAD_EMCF_A3_ATQA 0
#define MICROREAD_EMCF_A3_SAK 2
#define MICROREAD_EMCF_A3_LEN 3
#define MICROREAD_EMCF_A3_UID 4
#define MICROREAD_EMCF_B_UID 0
#define MICROREAD_EMCF_T1_ATQA 0
#define MICROREAD_EMCF_T1_UID 4
#define MICROREAD_EMCF_T3_UID 0
#define MICROREAD_EVT_MREAD_DISCOVERY_START NFC_HCI_EVT_READER_REQUESTED
#define MICROREAD_EVT_MREAD_DISCOVERY_START_SOME 0x3E
#define MICROREAD_EVT_MREAD_DISCOVERY_STOP NFC_HCI_EVT_END_OPERATION
#define MICROREAD_EVT_MREAD_SIM_REQUESTS 0x3F
#define MICROREAD_EVT_MCARD_EXCHANGE NFC_HCI_EVT_TARGET_DISCOVERED
#define MICROREAD_EVT_P2P_INITIATOR_EXCHANGE_TO_RF 0x20
#define MICROREAD_EVT_P2P_INITIATOR_EXCHANGE_FROM_RF 0x21
#define MICROREAD_EVT_MCARD_FIELD_ON 0x11
#define MICROREAD_EVT_P2P_TARGET_ACTIVATED 0x13
#define MICROREAD_EVT_P2P_TARGET_DEACTIVATED 0x12
#define MICROREAD_EVT_MCARD_FIELD_OFF 0x14

/* Commands */
#define MICROREAD_CMD_MREAD_EXCHANGE 0x10
#define MICROREAD_CMD_MREAD_SUBSCRIBE 0x3F

/* Hosts IDs */
#define MICROREAD_ELT_ID_HDS NFC_HCI_TERMINAL_HOST_ID
#define MICROREAD_ELT_ID_SIM NFC_HCI_UICC_HOST_ID
#define MICROREAD_ELT_ID_SE1 0x03
#define MICROREAD_ELT_ID_SE2 0x04
#define MICROREAD_ELT_ID_SE3 0x05

static struct nfc_hci_gate microread_gates[] = {
	{MICROREAD_GATE_ID_ADM, MICROREAD_PIPE_ID_ADMIN},
	{MICROREAD_GATE_ID_LOOPBACK, MICROREAD_PIPE_ID_HDS_LOOPBACK},
	{MICROREAD_GATE_ID_IDT, MICROREAD_PIPE_ID_HDS_IDT},
	{MICROREAD_GATE_ID_LMS, MICROREAD_PIPE_ID_LMS},
	{MICROREAD_GATE_ID_MREAD_ISO_B, MICROREAD_PIPE_ID_HDS_MREAD_ISO_B},
	{MICROREAD_GATE_ID_MREAD_ISO_A, MICROREAD_PIPE_ID_HDS_MREAD_ISO_A},
	{MICROREAD_GATE_ID_MREAD_ISO_A_3, MICROREAD_PIPE_ID_HDS_MREAD_ISO_A_3},
	{MICROREAD_GATE_ID_MGT, MICROREAD_PIPE_ID_MGT},
	{MICROREAD_GATE_ID_OS, MICROREAD_PIPE_ID_OS},
	{MICROREAD_GATE_ID_MREAD_NFC_T1, MICROREAD_PIPE_ID_HDS_MREAD_NFC_T1},
	{MICROREAD_GATE_ID_MREAD_NFC_T3, MICROREAD_PIPE_ID_HDS_MREAD_NFC_T3},
	{MICROREAD_GATE_ID_P2P_TARGET, MICROREAD_PIPE_ID_HDS_P2P_TARGET},
	{MICROREAD_GATE_ID_P2P_INITIATOR, MICROREAD_PIPE_ID_HDS_P2P_INITIATOR}
};

/* Largest headroom needed for outgoing custom commands */
#define MICROREAD_CMDS_HEADROOM	2
#define MICROREAD_CMD_TAILROOM	2

struct microread_info {
	struct nfc_phy_ops *phy_ops;
	void *phy_id;

	struct nfc_hci_dev *hdev;

	int async_cb_type;
	data_exchange_cb_t async_cb;
	void *async_cb_context;
};

static int microread_open(struct nfc_hci_dev *hdev)
{
	struct microread_info *info = nfc_hci_get_clientdata(hdev);

	return info->phy_ops->enable(info->phy_id);
}

static void microread_close(struct nfc_hci_dev *hdev)
{
	struct microread_info *info = nfc_hci_get_clientdata(hdev);

	info->phy_ops->disable(info->phy_id);
}

static int microread_hci_ready(struct nfc_hci_dev *hdev)
{
	int r;
	u8 param[4];

	param[0] = 0x03;
	r = nfc_hci_send_cmd(hdev, MICROREAD_GATE_ID_MREAD_ISO_A,
			     MICROREAD_CMD_MREAD_SUBSCRIBE, param, 1, NULL);
	if (r)
		return r;

	r = nfc_hci_send_cmd(hdev, MICROREAD_GATE_ID_MREAD_ISO_A_3,
			     MICROREAD_CMD_MREAD_SUBSCRIBE, NULL, 0, NULL);
	if (r)
		return r;

	param[0] = 0x00;
	param[1] = 0x03;
	param[2] = 0x00;
	r = nfc_hci_send_cmd(hdev, MICROREAD_GATE_ID_MREAD_ISO_B,
			     MICROREAD_CMD_MREAD_SUBSCRIBE, param, 3, NULL);
	if (r)
		return r;

	r = nfc_hci_send_cmd(hdev, MICROREAD_GATE_ID_MREAD_NFC_T1,
			     MICROREAD_CMD_MREAD_SUBSCRIBE, NULL, 0, NULL);
	if (r)
		return r;

	param[0] = 0xFF;
	param[1] = 0xFF;
	param[2] = 0x00;
	param[3] = 0x00;
	r = nfc_hci_send_cmd(hdev, MICROREAD_GATE_ID_MREAD_NFC_T3,
			     MICROREAD_CMD_MREAD_SUBSCRIBE, param, 4, NULL);

	return r;
}

static int microread_xmit(struct nfc_hci_dev *hdev, struct sk_buff *skb)
{
	struct microread_info *info = nfc_hci_get_clientdata(hdev);

	return info->phy_ops->write(info->phy_id, skb);
}

static int microread_start_poll(struct nfc_hci_dev *hdev,
				u32 im_protocols, u32 tm_protocols)
{
	int r;

	u8 param[2];
	u8 mode;

	param[0] = 0x00;
	param[1] = 0x00;

	if (im_protocols & NFC_PROTO_ISO14443_MASK)
		param[0] |= (1 << 2);

	if (im_protocols & NFC_PROTO_ISO14443_B_MASK)
		param[0] |= 1;

	if (im_protocols & NFC_PROTO_MIFARE_MASK)
		param[1] |= 1;

	if (im_protocols & NFC_PROTO_JEWEL_MASK)
		param[0] |= (1 << 1);

	if (im_protocols & NFC_PROTO_FELICA_MASK)
		param[0] |= (1 << 5);

	if (im_protocols & NFC_PROTO_NFC_DEP_MASK)
		param[1] |= (1 << 1);

	if ((im_protocols | tm_protocols) & NFC_PROTO_NFC_DEP_MASK) {
		hdev->gb = nfc_get_local_general_bytes(hdev->ndev,
						       &hdev->gb_len);
		if (hdev->gb == NULL || hdev->gb_len == 0) {
			im_protocols &= ~NFC_PROTO_NFC_DEP_MASK;
			tm_protocols &= ~NFC_PROTO_NFC_DEP_MASK;
		}
	}

	r = nfc_hci_send_event(hdev, MICROREAD_GATE_ID_MREAD_ISO_A,
			       MICROREAD_EVT_MREAD_DISCOVERY_STOP, NULL, 0);
	if (r)
		return r;

	mode = 0xff;
	r = nfc_hci_set_param(hdev, MICROREAD_GATE_ID_P2P_TARGET,
			      MICROREAD_PAR_P2P_TARGET_MODE, &mode, 1);
	if (r)
		return r;

	if (im_protocols & NFC_PROTO_NFC_DEP_MASK) {
		r = nfc_hci_set_param(hdev, MICROREAD_GATE_ID_P2P_INITIATOR,
				      MICROREAD_PAR_P2P_INITIATOR_GI,
				      hdev->gb, hdev->gb_len);
		if (r)
			return r;
	}

	if (tm_protocols & NFC_PROTO_NFC_DEP_MASK) {
		r = nfc_hci_set_param(hdev, MICROREAD_GATE_ID_P2P_TARGET,
				      MICROREAD_PAR_P2P_TARGET_GT,
				      hdev->gb, hdev->gb_len);
		if (r)
			return r;

		mode = 0x02;
		r = nfc_hci_set_param(hdev, MICROREAD_GATE_ID_P2P_TARGET,
				      MICROREAD_PAR_P2P_TARGET_MODE, &mode, 1);
		if (r)
			return r;
	}

	return nfc_hci_send_event(hdev, MICROREAD_GATE_ID_MREAD_ISO_A,
				  MICROREAD_EVT_MREAD_DISCOVERY_START_SOME,
				  param, 2);
}

static int microread_dep_link_up(struct nfc_hci_dev *hdev,
				struct nfc_target *target, u8 comm_mode,
				u8 *gb, size_t gb_len)
{
	struct sk_buff *rgb_skb = NULL;
	int r;

	r = nfc_hci_get_param(hdev, target->hci_reader_gate,
			      MICROREAD_PAR_P2P_INITIATOR_GT, &rgb_skb);
	if (r < 0)
		return r;

	if (rgb_skb->len == 0 || rgb_skb->len > NFC_GB_MAXSIZE) {
		r = -EPROTO;
		goto exit;
	}

	r = nfc_set_remote_general_bytes(hdev->ndev, rgb_skb->data,
					 rgb_skb->len);
	if (r == 0)
		r = nfc_dep_link_is_up(hdev->ndev, target->idx, comm_mode,
				       NFC_RF_INITIATOR);
exit:
	kfree_skb(rgb_skb);

	return r;
}

static int microread_dep_link_down(struct nfc_hci_dev *hdev)
{
	return nfc_hci_send_event(hdev, MICROREAD_GATE_ID_P2P_INITIATOR,
				  MICROREAD_EVT_MREAD_DISCOVERY_STOP, NULL, 0);
}

static int microread_target_from_gate(struct nfc_hci_dev *hdev, u8 gate,
				      struct nfc_target *target)
{
	switch (gate) {
	case MICROREAD_GATE_ID_P2P_INITIATOR:
		target->supported_protocols = NFC_PROTO_NFC_DEP_MASK;
		break;
	default:
		return -EPROTO;
	}

	return 0;
}

static int microread_complete_target_discovered(struct nfc_hci_dev *hdev,
						u8 gate,
						struct nfc_target *target)
{
	return 0;
}

#define MICROREAD_CB_TYPE_READER_ALL 1

static void microread_im_transceive_cb(void *context, struct sk_buff *skb,
				       int err)
{
	struct microread_info *info = context;

	switch (info->async_cb_type) {
	case MICROREAD_CB_TYPE_READER_ALL:
		if (err == 0) {
			if (skb->len == 0) {
				err = -EPROTO;
				kfree_skb(skb);
				info->async_cb(info->async_cb_context, NULL,
					       -EPROTO);
				return;
			}

			if (skb->data[skb->len - 1] != 0) {
				err = nfc_hci_result_to_errno(
						       skb->data[skb->len - 1]);
				kfree_skb(skb);
				info->async_cb(info->async_cb_context, NULL,
					       err);
				return;
			}

			skb_trim(skb, skb->len - 1);	/* RF Error ind. */
		}
		info->async_cb(info->async_cb_context, skb, err);
		break;
	default:
		if (err == 0)
			kfree_skb(skb);
		break;
	}
}

/*
 * Returns:
 * <= 0: driver handled the data exchange
 *    1: driver doesn't especially handle, please do standard processing
 */
static int microread_im_transceive(struct nfc_hci_dev *hdev,
				   struct nfc_target *target,
				   struct sk_buff *skb, data_exchange_cb_t cb,
				   void *cb_context)
{
	struct microread_info *info = nfc_hci_get_clientdata(hdev);
	u8 control_bits;
	u16 crc;

	pr_info("data exchange to gate 0x%x\n", target->hci_reader_gate);

	if (target->hci_reader_gate == MICROREAD_GATE_ID_P2P_INITIATOR) {
		*skb_push(skb, 1) = 0;

		return nfc_hci_send_event(hdev, target->hci_reader_gate,
				     MICROREAD_EVT_P2P_INITIATOR_EXCHANGE_TO_RF,
				     skb->data, skb->len);
	}

	switch (target->hci_reader_gate) {
	case MICROREAD_GATE_ID_MREAD_ISO_A:
		control_bits = 0xCB;
		break;
	case MICROREAD_GATE_ID_MREAD_ISO_A_3:
		control_bits = 0xCB;
		break;
	case MICROREAD_GATE_ID_MREAD_ISO_B:
		control_bits = 0xCB;
		break;
	case MICROREAD_GATE_ID_MREAD_NFC_T1:
		control_bits = 0x1B;

		crc = crc_ccitt(0xffff, skb->data, skb->len);
		crc = ~crc;
		*skb_put(skb, 1) = crc & 0xff;
		*skb_put(skb, 1) = crc >> 8;
		break;
	case MICROREAD_GATE_ID_MREAD_NFC_T3:
		control_bits = 0xDB;
		break;
	default:
		pr_info("Abort im_transceive to invalid gate 0x%x\n",
			target->hci_reader_gate);
		return 1;
	}

	*skb_push(skb, 1) = control_bits;

	info->async_cb_type = MICROREAD_CB_TYPE_READER_ALL;
	info->async_cb = cb;
	info->async_cb_context = cb_context;

	return nfc_hci_send_cmd_async(hdev, target->hci_reader_gate,
				      MICROREAD_CMD_MREAD_EXCHANGE,
				      skb->data, skb->len,
				      microread_im_transceive_cb, info);
}

static int microread_tm_send(struct nfc_hci_dev *hdev, struct sk_buff *skb)
{
	int r;

	r = nfc_hci_send_event(hdev, MICROREAD_GATE_ID_P2P_TARGET,
			       MICROREAD_EVT_MCARD_EXCHANGE,
			       skb->data, skb->len);

	kfree_skb(skb);

	return r;
}

static void microread_target_discovered(struct nfc_hci_dev *hdev, u8 gate,
					struct sk_buff *skb)
{
	struct nfc_target *targets;
	int r = 0;

	pr_info("target discovered to gate 0x%x\n", gate);

	targets = kzalloc(sizeof(struct nfc_target), GFP_KERNEL);
	if (targets == NULL) {
		r = -ENOMEM;
		goto exit;
	}

	targets->hci_reader_gate = gate;

	switch (gate) {
	case MICROREAD_GATE_ID_MREAD_ISO_A:
		targets->supported_protocols =
		      nfc_hci_sak_to_protocol(skb->data[MICROREAD_EMCF_A_SAK]);
		targets->sens_res =
			 be16_to_cpu(*(u16 *)&skb->data[MICROREAD_EMCF_A_ATQA]);
		targets->sel_res = skb->data[MICROREAD_EMCF_A_SAK];
		memcpy(targets->nfcid1, &skb->data[MICROREAD_EMCF_A_UID],
		       skb->data[MICROREAD_EMCF_A_LEN]);
		targets->nfcid1_len = skb->data[MICROREAD_EMCF_A_LEN];
		break;
	case MICROREAD_GATE_ID_MREAD_ISO_A_3:
		targets->supported_protocols =
		      nfc_hci_sak_to_protocol(skb->data[MICROREAD_EMCF_A3_SAK]);
		targets->sens_res =
			 be16_to_cpu(*(u16 *)&skb->data[MICROREAD_EMCF_A3_ATQA]);
		targets->sel_res = skb->data[MICROREAD_EMCF_A3_SAK];
		memcpy(targets->nfcid1, &skb->data[MICROREAD_EMCF_A3_UID],
		       skb->data[MICROREAD_EMCF_A3_LEN]);
		targets->nfcid1_len = skb->data[MICROREAD_EMCF_A3_LEN];
		break;
	case MICROREAD_GATE_ID_MREAD_ISO_B:
		targets->supported_protocols = NFC_PROTO_ISO14443_B_MASK;
		memcpy(targets->nfcid1, &skb->data[MICROREAD_EMCF_B_UID], 4);
		targets->nfcid1_len = 4;
		break;
	case MICROREAD_GATE_ID_MREAD_NFC_T1:
		targets->supported_protocols = NFC_PROTO_JEWEL_MASK;
		targets->sens_res =
			le16_to_cpu(*(u16 *)&skb->data[MICROREAD_EMCF_T1_ATQA]);
		memcpy(targets->nfcid1, &skb->data[MICROREAD_EMCF_T1_UID], 4);
		targets->nfcid1_len = 4;
		break;
	case MICROREAD_GATE_ID_MREAD_NFC_T3:
		targets->supported_protocols = NFC_PROTO_FELICA_MASK;
		memcpy(targets->nfcid1, &skb->data[MICROREAD_EMCF_T3_UID], 8);
		targets->nfcid1_len = 8;
		break;
	default:
		pr_info("discard target discovered to gate 0x%x\n", gate);
		goto exit_free;
	}

	r = nfc_targets_found(hdev->ndev, targets, 1);

exit_free:
	kfree(targets);

exit:
	kfree_skb(skb);

	if (r)
		pr_err("Failed to handle discovered target err=%d", r);
}

static int microread_event_received(struct nfc_hci_dev *hdev, u8 gate,
				     u8 event, struct sk_buff *skb)
{
	int r;
	u8 mode;

	pr_info("Microread received event 0x%x to gate 0x%x\n", event, gate);

	switch (event) {
	case MICROREAD_EVT_MREAD_CARD_FOUND:
		microread_target_discovered(hdev, gate, skb);
		return 0;

	case MICROREAD_EVT_P2P_INITIATOR_EXCHANGE_FROM_RF:
		if (skb->len < 1) {
			kfree_skb(skb);
			return -EPROTO;
		}

		if (skb->data[skb->len - 1]) {
			kfree_skb(skb);
			return -EIO;
		}

		skb_trim(skb, skb->len - 1);

		r = nfc_tm_data_received(hdev->ndev, skb);
		break;

	case MICROREAD_EVT_MCARD_FIELD_ON:
	case MICROREAD_EVT_MCARD_FIELD_OFF:
		kfree_skb(skb);
		return 0;

	case MICROREAD_EVT_P2P_TARGET_ACTIVATED:
		r = nfc_tm_activated(hdev->ndev, NFC_PROTO_NFC_DEP_MASK,
				     NFC_COMM_PASSIVE, skb->data,
				     skb->len);

		kfree_skb(skb);
		break;

	case MICROREAD_EVT_MCARD_EXCHANGE:
		if (skb->len < 1) {
			kfree_skb(skb);
			return -EPROTO;
		}

		if (skb->data[skb->len-1]) {
			kfree_skb(skb);
			return -EIO;
		}

		skb_trim(skb, skb->len - 1);

		r = nfc_tm_data_received(hdev->ndev, skb);
		break;

	case MICROREAD_EVT_P2P_TARGET_DEACTIVATED:
		kfree_skb(skb);

		mode = 0xff;
		r = nfc_hci_set_param(hdev, MICROREAD_GATE_ID_P2P_TARGET,
				      MICROREAD_PAR_P2P_TARGET_MODE, &mode, 1);
		if (r)
			break;

		r = nfc_hci_send_event(hdev, gate,
				       MICROREAD_EVT_MREAD_DISCOVERY_STOP, NULL,
				       0);
		break;

	default:
		return 1;
	}

	return r;
}

static struct nfc_hci_ops microread_hci_ops = {
	.open = microread_open,
	.close = microread_close,
	.hci_ready = microread_hci_ready,
	.xmit = microread_xmit,
	.start_poll = microread_start_poll,
	.dep_link_up = microread_dep_link_up,
	.dep_link_down = microread_dep_link_down,
	.target_from_gate = microread_target_from_gate,
	.complete_target_discovered = microread_complete_target_discovered,
	.im_transceive = microread_im_transceive,
	.tm_send = microread_tm_send,
	.check_presence = NULL,
	.event_received = microread_event_received,
};

int microread_probe(void *phy_id, struct nfc_phy_ops *phy_ops, char *llc_name,
		    int phy_headroom, int phy_tailroom, int phy_payload,
		    struct nfc_hci_dev **hdev)
{
	struct microread_info *info;
	unsigned long quirks = 0;
	u32 protocols, se;
	struct nfc_hci_init_data init_data;
	int r;

	info = kzalloc(sizeof(struct microread_info), GFP_KERNEL);
	if (!info) {
		pr_err("Cannot allocate memory for microread_info.\n");
		r = -ENOMEM;
		goto err_info_alloc;
	}

	info->phy_ops = phy_ops;
	info->phy_id = phy_id;

	init_data.gate_count = ARRAY_SIZE(microread_gates);
	memcpy(init_data.gates, microread_gates, sizeof(microread_gates));

	strcpy(init_data.session_id, "MICROREA");

	set_bit(NFC_HCI_QUIRK_SHORT_CLEAR, &quirks);

	protocols = NFC_PROTO_JEWEL_MASK |
		    NFC_PROTO_MIFARE_MASK |
		    NFC_PROTO_FELICA_MASK |
		    NFC_PROTO_ISO14443_MASK |
		    NFC_PROTO_ISO14443_B_MASK |
		    NFC_PROTO_NFC_DEP_MASK;

	se = NFC_SE_UICC | NFC_SE_EMBEDDED;

	info->hdev = nfc_hci_allocate_device(&microread_hci_ops, &init_data,
					     quirks, protocols, se, llc_name,
					     phy_headroom +
					     MICROREAD_CMDS_HEADROOM,
					     phy_tailroom +
					     MICROREAD_CMD_TAILROOM,
					     phy_payload);
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

err_info_alloc:
	return r;
}
EXPORT_SYMBOL(microread_probe);

void microread_remove(struct nfc_hci_dev *hdev)
{
	struct microread_info *info = nfc_hci_get_clientdata(hdev);

	nfc_hci_unregister_device(hdev);
	nfc_hci_free_device(hdev);
	kfree(info);
}
EXPORT_SYMBOL(microread_remove);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION(DRIVER_DESC);
