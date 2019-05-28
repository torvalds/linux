// SPDX-License-Identifier: GPL-2.0-only
/*
 *  Bluetooth supports for Qualcomm Atheros chips
 *
 *  Copyright (c) 2015 The Linux Foundation. All rights reserved.
 */
#include <linux/module.h>
#include <linux/firmware.h>

#include <net/bluetooth/bluetooth.h>
#include <net/bluetooth/hci_core.h>

#include "btqca.h"

#define VERSION "0.1"

int qca_read_soc_version(struct hci_dev *hdev, u32 *soc_version)
{
	struct sk_buff *skb;
	struct edl_event_hdr *edl;
	struct rome_version *ver;
	char cmd;
	int err = 0;

	bt_dev_dbg(hdev, "QCA Version Request");

	cmd = EDL_PATCH_VER_REQ_CMD;
	skb = __hci_cmd_sync_ev(hdev, EDL_PATCH_CMD_OPCODE, EDL_PATCH_CMD_LEN,
				&cmd, HCI_EV_VENDOR, HCI_INIT_TIMEOUT);
	if (IS_ERR(skb)) {
		err = PTR_ERR(skb);
		bt_dev_err(hdev, "Reading QCA version information failed (%d)",
			   err);
		return err;
	}

	if (skb->len != sizeof(*edl) + sizeof(*ver)) {
		bt_dev_err(hdev, "QCA Version size mismatch len %d", skb->len);
		err = -EILSEQ;
		goto out;
	}

	edl = (struct edl_event_hdr *)(skb->data);
	if (!edl) {
		bt_dev_err(hdev, "QCA TLV with no header");
		err = -EILSEQ;
		goto out;
	}

	if (edl->cresp != EDL_CMD_REQ_RES_EVT ||
	    edl->rtype != EDL_APP_VER_RES_EVT) {
		bt_dev_err(hdev, "QCA Wrong packet received %d %d", edl->cresp,
			   edl->rtype);
		err = -EIO;
		goto out;
	}

	ver = (struct rome_version *)(edl->data);

	BT_DBG("%s: Product:0x%08x", hdev->name, le32_to_cpu(ver->product_id));
	BT_DBG("%s: Patch  :0x%08x", hdev->name, le16_to_cpu(ver->patch_ver));
	BT_DBG("%s: ROM    :0x%08x", hdev->name, le16_to_cpu(ver->rome_ver));
	BT_DBG("%s: SOC    :0x%08x", hdev->name, le32_to_cpu(ver->soc_id));

	/* QCA chipset version can be decided by patch and SoC
	 * version, combination with upper 2 bytes from SoC
	 * and lower 2 bytes from patch will be used.
	 */
	*soc_version = (le32_to_cpu(ver->soc_id) << 16) |
			(le16_to_cpu(ver->rome_ver) & 0x0000ffff);
	if (*soc_version == 0)
		err = -EILSEQ;

out:
	kfree_skb(skb);
	if (err)
		bt_dev_err(hdev, "QCA Failed to get version (%d)", err);

	return err;
}
EXPORT_SYMBOL_GPL(qca_read_soc_version);

static int qca_send_reset(struct hci_dev *hdev)
{
	struct sk_buff *skb;
	int err;

	bt_dev_dbg(hdev, "QCA HCI_RESET");

	skb = __hci_cmd_sync(hdev, HCI_OP_RESET, 0, NULL, HCI_INIT_TIMEOUT);
	if (IS_ERR(skb)) {
		err = PTR_ERR(skb);
		bt_dev_err(hdev, "QCA Reset failed (%d)", err);
		return err;
	}

	kfree_skb(skb);

	return 0;
}

static void qca_tlv_check_data(struct rome_config *config,
				const struct firmware *fw)
{
	const u8 *data;
	u32 type_len;
	u16 tag_id, tag_len;
	int idx, length;
	struct tlv_type_hdr *tlv;
	struct tlv_type_patch *tlv_patch;
	struct tlv_type_nvm *tlv_nvm;

	tlv = (struct tlv_type_hdr *)fw->data;

	type_len = le32_to_cpu(tlv->type_len);
	length = (type_len >> 8) & 0x00ffffff;

	BT_DBG("TLV Type\t\t : 0x%x", type_len & 0x000000ff);
	BT_DBG("Length\t\t : %d bytes", length);

	config->dnld_mode = ROME_SKIP_EVT_NONE;

	switch (config->type) {
	case TLV_TYPE_PATCH:
		tlv_patch = (struct tlv_type_patch *)tlv->data;

		/* For Rome version 1.1 to 3.1, all segment commands
		 * are acked by a vendor specific event (VSE).
		 * For Rome >= 3.2, the download mode field indicates
		 * if VSE is skipped by the controller.
		 * In case VSE is skipped, only the last segment is acked.
		 */
		config->dnld_mode = tlv_patch->download_mode;
		config->dnld_type = config->dnld_mode;

		BT_DBG("Total Length           : %d bytes",
		       le32_to_cpu(tlv_patch->total_size));
		BT_DBG("Patch Data Length      : %d bytes",
		       le32_to_cpu(tlv_patch->data_length));
		BT_DBG("Signing Format Version : 0x%x",
		       tlv_patch->format_version);
		BT_DBG("Signature Algorithm    : 0x%x",
		       tlv_patch->signature);
		BT_DBG("Download mode          : 0x%x",
		       tlv_patch->download_mode);
		BT_DBG("Reserved               : 0x%x",
		       tlv_patch->reserved1);
		BT_DBG("Product ID             : 0x%04x",
		       le16_to_cpu(tlv_patch->product_id));
		BT_DBG("Rom Build Version      : 0x%04x",
		       le16_to_cpu(tlv_patch->rom_build));
		BT_DBG("Patch Version          : 0x%04x",
		       le16_to_cpu(tlv_patch->patch_version));
		BT_DBG("Reserved               : 0x%x",
		       le16_to_cpu(tlv_patch->reserved2));
		BT_DBG("Patch Entry Address    : 0x%x",
		       le32_to_cpu(tlv_patch->entry));
		break;

	case TLV_TYPE_NVM:
		idx = 0;
		data = tlv->data;
		while (idx < length) {
			tlv_nvm = (struct tlv_type_nvm *)(data + idx);

			tag_id = le16_to_cpu(tlv_nvm->tag_id);
			tag_len = le16_to_cpu(tlv_nvm->tag_len);

			/* Update NVM tags as needed */
			switch (tag_id) {
			case EDL_TAG_ID_HCI:
				/* HCI transport layer parameters
				 * enabling software inband sleep
				 * onto controller side.
				 */
				tlv_nvm->data[0] |= 0x80;

				/* UART Baud Rate */
				tlv_nvm->data[2] = config->user_baud_rate;

				break;

			case EDL_TAG_ID_DEEP_SLEEP:
				/* Sleep enable mask
				 * enabling deep sleep feature on controller.
				 */
				tlv_nvm->data[0] |= 0x01;

				break;
			}

			idx += (sizeof(u16) + sizeof(u16) + 8 + tag_len);
		}
		break;

	default:
		BT_ERR("Unknown TLV type %d", config->type);
		break;
	}
}

static int qca_tlv_send_segment(struct hci_dev *hdev, int seg_size,
				 const u8 *data, enum rome_tlv_dnld_mode mode)
{
	struct sk_buff *skb;
	struct edl_event_hdr *edl;
	struct tlv_seg_resp *tlv_resp;
	u8 cmd[MAX_SIZE_PER_TLV_SEGMENT + 2];
	int err = 0;

	cmd[0] = EDL_PATCH_TLV_REQ_CMD;
	cmd[1] = seg_size;
	memcpy(cmd + 2, data, seg_size);

	if (mode == ROME_SKIP_EVT_VSE_CC || mode == ROME_SKIP_EVT_VSE)
		return __hci_cmd_send(hdev, EDL_PATCH_CMD_OPCODE, seg_size + 2,
				      cmd);

	skb = __hci_cmd_sync_ev(hdev, EDL_PATCH_CMD_OPCODE, seg_size + 2, cmd,
				HCI_EV_VENDOR, HCI_INIT_TIMEOUT);
	if (IS_ERR(skb)) {
		err = PTR_ERR(skb);
		bt_dev_err(hdev, "QCA Failed to send TLV segment (%d)", err);
		return err;
	}

	if (skb->len != sizeof(*edl) + sizeof(*tlv_resp)) {
		bt_dev_err(hdev, "QCA TLV response size mismatch");
		err = -EILSEQ;
		goto out;
	}

	edl = (struct edl_event_hdr *)(skb->data);
	if (!edl) {
		bt_dev_err(hdev, "TLV with no header");
		err = -EILSEQ;
		goto out;
	}

	tlv_resp = (struct tlv_seg_resp *)(edl->data);

	if (edl->cresp != EDL_CMD_REQ_RES_EVT ||
	    edl->rtype != EDL_TVL_DNLD_RES_EVT || tlv_resp->result != 0x00) {
		bt_dev_err(hdev, "QCA TLV with error stat 0x%x rtype 0x%x (0x%x)",
			   edl->cresp, edl->rtype, tlv_resp->result);
		err = -EIO;
	}

out:
	kfree_skb(skb);

	return err;
}

static int qca_inject_cmd_complete_event(struct hci_dev *hdev)
{
	struct hci_event_hdr *hdr;
	struct hci_ev_cmd_complete *evt;
	struct sk_buff *skb;

	skb = bt_skb_alloc(sizeof(*hdr) + sizeof(*evt) + 1, GFP_KERNEL);
	if (!skb)
		return -ENOMEM;

	hdr = skb_put(skb, sizeof(*hdr));
	hdr->evt = HCI_EV_CMD_COMPLETE;
	hdr->plen = sizeof(*evt) + 1;

	evt = skb_put(skb, sizeof(*evt));
	evt->ncmd = 1;
	evt->opcode = QCA_HCI_CC_OPCODE;

	skb_put_u8(skb, QCA_HCI_CC_SUCCESS);

	hci_skb_pkt_type(skb) = HCI_EVENT_PKT;

	return hci_recv_frame(hdev, skb);
}

static int qca_download_firmware(struct hci_dev *hdev,
				  struct rome_config *config)
{
	const struct firmware *fw;
	const u8 *segment;
	int ret, remain, i = 0;

	bt_dev_info(hdev, "QCA Downloading %s", config->fwname);

	ret = request_firmware(&fw, config->fwname, &hdev->dev);
	if (ret) {
		bt_dev_err(hdev, "QCA Failed to request file: %s (%d)",
			   config->fwname, ret);
		return ret;
	}

	qca_tlv_check_data(config, fw);

	segment = fw->data;
	remain = fw->size;
	while (remain > 0) {
		int segsize = min(MAX_SIZE_PER_TLV_SEGMENT, remain);

		bt_dev_dbg(hdev, "Send segment %d, size %d", i++, segsize);

		remain -= segsize;
		/* The last segment is always acked regardless download mode */
		if (!remain || segsize < MAX_SIZE_PER_TLV_SEGMENT)
			config->dnld_mode = ROME_SKIP_EVT_NONE;

		ret = qca_tlv_send_segment(hdev, segsize, segment,
					    config->dnld_mode);
		if (ret)
			goto out;

		segment += segsize;
	}

	/* Latest qualcomm chipsets are not sending a command complete event
	 * for every fw packet sent. They only respond with a vendor specific
	 * event for the last packet. This optimization in the chip will
	 * decrease the BT in initialization time. Here we will inject a command
	 * complete event to avoid a command timeout error message.
	 */
	if (config->dnld_type == ROME_SKIP_EVT_VSE_CC ||
	    config->dnld_type == ROME_SKIP_EVT_VSE)
		return qca_inject_cmd_complete_event(hdev);

out:
	release_firmware(fw);

	return ret;
}

int qca_set_bdaddr_rome(struct hci_dev *hdev, const bdaddr_t *bdaddr)
{
	struct sk_buff *skb;
	u8 cmd[9];
	int err;

	cmd[0] = EDL_NVM_ACCESS_SET_REQ_CMD;
	cmd[1] = 0x02; 			/* TAG ID */
	cmd[2] = sizeof(bdaddr_t);	/* size */
	memcpy(cmd + 3, bdaddr, sizeof(bdaddr_t));
	skb = __hci_cmd_sync_ev(hdev, EDL_NVM_ACCESS_OPCODE, sizeof(cmd), cmd,
				HCI_EV_VENDOR, HCI_INIT_TIMEOUT);
	if (IS_ERR(skb)) {
		err = PTR_ERR(skb);
		bt_dev_err(hdev, "QCA Change address command failed (%d)", err);
		return err;
	}

	kfree_skb(skb);

	return 0;
}
EXPORT_SYMBOL_GPL(qca_set_bdaddr_rome);

int qca_uart_setup(struct hci_dev *hdev, uint8_t baudrate,
		   enum qca_btsoc_type soc_type, u32 soc_ver)
{
	struct rome_config config;
	int err;
	u8 rom_ver = 0;

	bt_dev_dbg(hdev, "QCA setup on UART");

	config.user_baud_rate = baudrate;

	/* Download rampatch file */
	config.type = TLV_TYPE_PATCH;
	if (qca_is_wcn399x(soc_type)) {
		/* Firmware files to download are based on ROM version.
		 * ROM version is derived from last two bytes of soc_ver.
		 */
		rom_ver = ((soc_ver & 0x00000f00) >> 0x04) |
			    (soc_ver & 0x0000000f);
		snprintf(config.fwname, sizeof(config.fwname),
			 "qca/crbtfw%02x.tlv", rom_ver);
	} else {
		snprintf(config.fwname, sizeof(config.fwname),
			 "qca/rampatch_%08x.bin", soc_ver);
	}

	err = qca_download_firmware(hdev, &config);
	if (err < 0) {
		bt_dev_err(hdev, "QCA Failed to download patch (%d)", err);
		return err;
	}

	/* Download NVM configuration */
	config.type = TLV_TYPE_NVM;
	if (qca_is_wcn399x(soc_type))
		snprintf(config.fwname, sizeof(config.fwname),
			 "qca/crnv%02x.bin", rom_ver);
	else
		snprintf(config.fwname, sizeof(config.fwname),
			 "qca/nvm_%08x.bin", soc_ver);

	err = qca_download_firmware(hdev, &config);
	if (err < 0) {
		bt_dev_err(hdev, "QCA Failed to download NVM (%d)", err);
		return err;
	}

	/* Perform HCI reset */
	err = qca_send_reset(hdev);
	if (err < 0) {
		bt_dev_err(hdev, "QCA Failed to run HCI_RESET (%d)", err);
		return err;
	}

	bt_dev_info(hdev, "QCA setup on UART is completed");

	return 0;
}
EXPORT_SYMBOL_GPL(qca_uart_setup);

int qca_set_bdaddr(struct hci_dev *hdev, const bdaddr_t *bdaddr)
{
	struct sk_buff *skb;
	int err;

	skb = __hci_cmd_sync_ev(hdev, EDL_WRITE_BD_ADDR_OPCODE, 6, bdaddr,
				HCI_EV_VENDOR, HCI_INIT_TIMEOUT);
	if (IS_ERR(skb)) {
		err = PTR_ERR(skb);
		bt_dev_err(hdev, "QCA Change address cmd failed (%d)", err);
		return err;
	}

	kfree_skb(skb);

	return 0;
}
EXPORT_SYMBOL_GPL(qca_set_bdaddr);


MODULE_AUTHOR("Ben Young Tae Kim <ytkim@qca.qualcomm.com>");
MODULE_DESCRIPTION("Bluetooth support for Qualcomm Atheros family ver " VERSION);
MODULE_VERSION(VERSION);
MODULE_LICENSE("GPL");
