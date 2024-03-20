// SPDX-License-Identifier: GPL-2.0-only
/*
 *  Bluetooth supports for Qualcomm Atheros chips
 *
 *  Copyright (c) 2015 The Linux Foundation. All rights reserved.
 */
#include <linux/module.h>
#include <linux/firmware.h>
#include <linux/vmalloc.h>

#include <net/bluetooth/bluetooth.h>
#include <net/bluetooth/hci_core.h>

#include "btqca.h"

#define VERSION "0.1"

int qca_read_soc_version(struct hci_dev *hdev, struct qca_btsoc_version *ver,
			 enum qca_btsoc_type soc_type)
{
	struct sk_buff *skb;
	struct edl_event_hdr *edl;
	char cmd;
	int err = 0;
	u8 event_type = HCI_EV_VENDOR;
	u8 rlen = sizeof(*edl) + sizeof(*ver);
	u8 rtype = EDL_APP_VER_RES_EVT;

	bt_dev_dbg(hdev, "QCA Version Request");

	/* Unlike other SoC's sending version command response as payload to
	 * VSE event. WCN3991 sends version command response as a payload to
	 * command complete event.
	 */
	if (soc_type >= QCA_WCN3991) {
		event_type = 0;
		rlen += 1;
		rtype = EDL_PATCH_VER_REQ_CMD;
	}

	cmd = EDL_PATCH_VER_REQ_CMD;
	skb = __hci_cmd_sync_ev(hdev, EDL_PATCH_CMD_OPCODE, EDL_PATCH_CMD_LEN,
				&cmd, event_type, HCI_INIT_TIMEOUT);
	if (IS_ERR(skb)) {
		err = PTR_ERR(skb);
		bt_dev_err(hdev, "Reading QCA version information failed (%d)",
			   err);
		return err;
	}

	if (skb->len != rlen) {
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
	    edl->rtype != rtype) {
		bt_dev_err(hdev, "QCA Wrong packet received %d %d", edl->cresp,
			   edl->rtype);
		err = -EIO;
		goto out;
	}

	if (soc_type >= QCA_WCN3991)
		memcpy(ver, edl->data + 1, sizeof(*ver));
	else
		memcpy(ver, &edl->data, sizeof(*ver));

	bt_dev_info(hdev, "QCA Product ID   :0x%08x",
		    le32_to_cpu(ver->product_id));
	bt_dev_info(hdev, "QCA SOC Version  :0x%08x",
		    le32_to_cpu(ver->soc_id));
	bt_dev_info(hdev, "QCA ROM Version  :0x%08x",
		    le16_to_cpu(ver->rom_ver));
	bt_dev_info(hdev, "QCA Patch Version:0x%08x",
		    le16_to_cpu(ver->patch_ver));

	if (ver->soc_id == 0 || ver->rom_ver == 0)
		err = -EILSEQ;

out:
	kfree_skb(skb);
	if (err)
		bt_dev_err(hdev, "QCA Failed to get version (%d)", err);

	return err;
}
EXPORT_SYMBOL_GPL(qca_read_soc_version);

static int qca_read_fw_build_info(struct hci_dev *hdev)
{
	struct sk_buff *skb;
	struct edl_event_hdr *edl;
	char cmd, build_label[QCA_FW_BUILD_VER_LEN];
	int build_lbl_len, err = 0;

	bt_dev_dbg(hdev, "QCA read fw build info");

	cmd = EDL_GET_BUILD_INFO_CMD;
	skb = __hci_cmd_sync_ev(hdev, EDL_PATCH_CMD_OPCODE, EDL_PATCH_CMD_LEN,
				&cmd, 0, HCI_INIT_TIMEOUT);
	if (IS_ERR(skb)) {
		err = PTR_ERR(skb);
		bt_dev_err(hdev, "Reading QCA fw build info failed (%d)",
			   err);
		return err;
	}

	edl = (struct edl_event_hdr *)(skb->data);
	if (!edl) {
		bt_dev_err(hdev, "QCA read fw build info with no header");
		err = -EILSEQ;
		goto out;
	}

	if (edl->cresp != EDL_CMD_REQ_RES_EVT ||
	    edl->rtype != EDL_GET_BUILD_INFO_CMD) {
		bt_dev_err(hdev, "QCA Wrong packet received %d %d", edl->cresp,
			   edl->rtype);
		err = -EIO;
		goto out;
	}

	build_lbl_len = edl->data[0];
	if (build_lbl_len <= QCA_FW_BUILD_VER_LEN - 1) {
		memcpy(build_label, edl->data + 1, build_lbl_len);
		*(build_label + build_lbl_len) = '\0';
	}

	hci_set_fw_info(hdev, "%s", build_label);

out:
	kfree_skb(skb);
	return err;
}

static int qca_send_patch_config_cmd(struct hci_dev *hdev)
{
	const u8 cmd[] = { EDL_PATCH_CONFIG_CMD, 0x01, 0, 0, 0 };
	struct sk_buff *skb;
	struct edl_event_hdr *edl;
	int err;

	bt_dev_dbg(hdev, "QCA Patch config");

	skb = __hci_cmd_sync_ev(hdev, EDL_PATCH_CMD_OPCODE, sizeof(cmd),
				cmd, 0, HCI_INIT_TIMEOUT);
	if (IS_ERR(skb)) {
		err = PTR_ERR(skb);
		bt_dev_err(hdev, "Sending QCA Patch config failed (%d)", err);
		return err;
	}

	if (skb->len != 2) {
		bt_dev_err(hdev, "QCA Patch config cmd size mismatch len %d", skb->len);
		err = -EILSEQ;
		goto out;
	}

	edl = (struct edl_event_hdr *)(skb->data);
	if (!edl) {
		bt_dev_err(hdev, "QCA Patch config with no header");
		err = -EILSEQ;
		goto out;
	}

	if (edl->cresp != EDL_PATCH_CONFIG_RES_EVT || edl->rtype != EDL_PATCH_CONFIG_CMD) {
		bt_dev_err(hdev, "QCA Wrong packet received %d %d", edl->cresp,
			   edl->rtype);
		err = -EIO;
		goto out;
	}

	err = 0;

out:
	kfree_skb(skb);
	return err;
}

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

int qca_send_pre_shutdown_cmd(struct hci_dev *hdev)
{
	struct sk_buff *skb;
	int err;

	bt_dev_dbg(hdev, "QCA pre shutdown cmd");

	skb = __hci_cmd_sync_ev(hdev, QCA_PRE_SHUTDOWN_CMD, 0,
				NULL, HCI_EV_CMD_COMPLETE, HCI_INIT_TIMEOUT);

	if (IS_ERR(skb)) {
		err = PTR_ERR(skb);
		bt_dev_err(hdev, "QCA preshutdown_cmd failed (%d)", err);
		return err;
	}

	kfree_skb(skb);

	return 0;
}
EXPORT_SYMBOL_GPL(qca_send_pre_shutdown_cmd);

static void qca_tlv_check_data(struct hci_dev *hdev,
			       struct qca_fw_config *config,
		u8 *fw_data, enum qca_btsoc_type soc_type)
{
	const u8 *data;
	u32 type_len;
	u16 tag_id, tag_len;
	int idx, length;
	struct tlv_type_hdr *tlv;
	struct tlv_type_patch *tlv_patch;
	struct tlv_type_nvm *tlv_nvm;
	uint8_t nvm_baud_rate = config->user_baud_rate;

	config->dnld_mode = QCA_SKIP_EVT_NONE;
	config->dnld_type = QCA_SKIP_EVT_NONE;

	switch (config->type) {
	case ELF_TYPE_PATCH:
		config->dnld_mode = QCA_SKIP_EVT_VSE_CC;
		config->dnld_type = QCA_SKIP_EVT_VSE_CC;

		bt_dev_dbg(hdev, "File Class        : 0x%x", fw_data[4]);
		bt_dev_dbg(hdev, "Data Encoding     : 0x%x", fw_data[5]);
		bt_dev_dbg(hdev, "File version      : 0x%x", fw_data[6]);
		break;
	case TLV_TYPE_PATCH:
		tlv = (struct tlv_type_hdr *)fw_data;
		type_len = le32_to_cpu(tlv->type_len);
		tlv_patch = (struct tlv_type_patch *)tlv->data;

		/* For Rome version 1.1 to 3.1, all segment commands
		 * are acked by a vendor specific event (VSE).
		 * For Rome >= 3.2, the download mode field indicates
		 * if VSE is skipped by the controller.
		 * In case VSE is skipped, only the last segment is acked.
		 */
		config->dnld_mode = tlv_patch->download_mode;
		config->dnld_type = config->dnld_mode;

		BT_DBG("TLV Type\t\t : 0x%x", type_len & 0x000000ff);
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
		tlv = (struct tlv_type_hdr *)fw_data;

		type_len = le32_to_cpu(tlv->type_len);
		length = (type_len >> 8) & 0x00ffffff;

		BT_DBG("TLV Type\t\t : 0x%x", type_len & 0x000000ff);
		BT_DBG("Length\t\t : %d bytes", length);

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
				if (soc_type >= QCA_WCN3991)
					tlv_nvm->data[1] = nvm_baud_rate;
				else
					tlv_nvm->data[2] = nvm_baud_rate;

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
				const u8 *data, enum qca_tlv_dnld_mode mode,
				enum qca_btsoc_type soc_type)
{
	struct sk_buff *skb;
	struct edl_event_hdr *edl;
	struct tlv_seg_resp *tlv_resp;
	u8 cmd[MAX_SIZE_PER_TLV_SEGMENT + 2];
	int err = 0;
	u8 event_type = HCI_EV_VENDOR;
	u8 rlen = (sizeof(*edl) + sizeof(*tlv_resp));
	u8 rtype = EDL_TVL_DNLD_RES_EVT;

	cmd[0] = EDL_PATCH_TLV_REQ_CMD;
	cmd[1] = seg_size;
	memcpy(cmd + 2, data, seg_size);

	if (mode == QCA_SKIP_EVT_VSE_CC || mode == QCA_SKIP_EVT_VSE)
		return __hci_cmd_send(hdev, EDL_PATCH_CMD_OPCODE, seg_size + 2,
				      cmd);

	/* Unlike other SoC's sending version command response as payload to
	 * VSE event. WCN3991 sends version command response as a payload to
	 * command complete event.
	 */
	if (soc_type >= QCA_WCN3991) {
		event_type = 0;
		rlen = sizeof(*edl);
		rtype = EDL_PATCH_TLV_REQ_CMD;
	}

	skb = __hci_cmd_sync_ev(hdev, EDL_PATCH_CMD_OPCODE, seg_size + 2, cmd,
				event_type, HCI_INIT_TIMEOUT);
	if (IS_ERR(skb)) {
		err = PTR_ERR(skb);
		bt_dev_err(hdev, "QCA Failed to send TLV segment (%d)", err);
		return err;
	}

	if (skb->len != rlen) {
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

	if (edl->cresp != EDL_CMD_REQ_RES_EVT || edl->rtype != rtype) {
		bt_dev_err(hdev, "QCA TLV with error stat 0x%x rtype 0x%x",
			   edl->cresp, edl->rtype);
		err = -EIO;
	}

	if (soc_type >= QCA_WCN3991)
		goto out;

	tlv_resp = (struct tlv_seg_resp *)(edl->data);
	if (tlv_resp->result) {
		bt_dev_err(hdev, "QCA TLV with error stat 0x%x rtype 0x%x (0x%x)",
			   edl->cresp, edl->rtype, tlv_resp->result);
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
	evt->opcode = cpu_to_le16(QCA_HCI_CC_OPCODE);

	skb_put_u8(skb, QCA_HCI_CC_SUCCESS);

	hci_skb_pkt_type(skb) = HCI_EVENT_PKT;

	return hci_recv_frame(hdev, skb);
}

static int qca_download_firmware(struct hci_dev *hdev,
				 struct qca_fw_config *config,
				 enum qca_btsoc_type soc_type,
				 u8 rom_ver)
{
	const struct firmware *fw;
	u8 *data;
	const u8 *segment;
	int ret, size, remain, i = 0;

	bt_dev_info(hdev, "QCA Downloading %s", config->fwname);

	ret = request_firmware(&fw, config->fwname, &hdev->dev);
	if (ret) {
		/* For WCN6750, if mbn file is not present then check for
		 * tlv file.
		 */
		if (soc_type == QCA_WCN6750 && config->type == ELF_TYPE_PATCH) {
			bt_dev_dbg(hdev, "QCA Failed to request file: %s (%d)",
				   config->fwname, ret);
			config->type = TLV_TYPE_PATCH;
			snprintf(config->fwname, sizeof(config->fwname),
				 "qca/msbtfw%02x.tlv", rom_ver);
			bt_dev_info(hdev, "QCA Downloading %s", config->fwname);
			ret = request_firmware(&fw, config->fwname, &hdev->dev);
			if (ret) {
				bt_dev_err(hdev, "QCA Failed to request file: %s (%d)",
					   config->fwname, ret);
				return ret;
			}
		} else {
			bt_dev_err(hdev, "QCA Failed to request file: %s (%d)",
				   config->fwname, ret);
			return ret;
		}
	}

	size = fw->size;
	data = vmalloc(fw->size);
	if (!data) {
		bt_dev_err(hdev, "QCA Failed to allocate memory for file: %s",
			   config->fwname);
		release_firmware(fw);
		return -ENOMEM;
	}

	memcpy(data, fw->data, size);
	release_firmware(fw);

	qca_tlv_check_data(hdev, config, data, soc_type);

	segment = data;
	remain = size;
	while (remain > 0) {
		int segsize = min(MAX_SIZE_PER_TLV_SEGMENT, remain);

		bt_dev_dbg(hdev, "Send segment %d, size %d", i++, segsize);

		remain -= segsize;
		/* The last segment is always acked regardless download mode */
		if (!remain || segsize < MAX_SIZE_PER_TLV_SEGMENT)
			config->dnld_mode = QCA_SKIP_EVT_NONE;

		ret = qca_tlv_send_segment(hdev, segsize, segment,
					   config->dnld_mode, soc_type);
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
	if (config->dnld_type == QCA_SKIP_EVT_VSE_CC ||
	    config->dnld_type == QCA_SKIP_EVT_VSE)
		ret = qca_inject_cmd_complete_event(hdev);

out:
	vfree(data);

	return ret;
}

static int qca_disable_soc_logging(struct hci_dev *hdev)
{
	struct sk_buff *skb;
	u8 cmd[2];
	int err;

	cmd[0] = QCA_DISABLE_LOGGING_SUB_OP;
	cmd[1] = 0x00;
	skb = __hci_cmd_sync_ev(hdev, QCA_DISABLE_LOGGING, sizeof(cmd), cmd,
				HCI_EV_CMD_COMPLETE, HCI_INIT_TIMEOUT);
	if (IS_ERR(skb)) {
		err = PTR_ERR(skb);
		bt_dev_err(hdev, "QCA Failed to disable soc logging(%d)", err);
		return err;
	}

	kfree_skb(skb);

	return 0;
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
		   enum qca_btsoc_type soc_type, struct qca_btsoc_version ver,
		   const char *firmware_name)
{
	struct qca_fw_config config;
	int err;
	u8 rom_ver = 0;
	u32 soc_ver;

	bt_dev_dbg(hdev, "QCA setup on UART");

	soc_ver = get_soc_ver(ver.soc_id, ver.rom_ver);

	bt_dev_info(hdev, "QCA controller version 0x%08x", soc_ver);

	config.user_baud_rate = baudrate;

	/* Firmware files to download are based on ROM version.
	 * ROM version is derived from last two bytes of soc_ver.
	 */
	if (soc_type == QCA_WCN3988)
		rom_ver = ((soc_ver & 0x00000f00) >> 0x05) | (soc_ver & 0x0000000f);
	else
		rom_ver = ((soc_ver & 0x00000f00) >> 0x04) | (soc_ver & 0x0000000f);

	if (soc_type == QCA_WCN6750)
		qca_send_patch_config_cmd(hdev);

	/* Download rampatch file */
	config.type = TLV_TYPE_PATCH;
	switch (soc_type) {
	case QCA_WCN3990:
	case QCA_WCN3991:
	case QCA_WCN3998:
		snprintf(config.fwname, sizeof(config.fwname),
			 "qca/crbtfw%02x.tlv", rom_ver);
		break;
	case QCA_WCN3988:
		snprintf(config.fwname, sizeof(config.fwname),
			 "qca/apbtfw%02x.tlv", rom_ver);
		break;
	case QCA_QCA6390:
		snprintf(config.fwname, sizeof(config.fwname),
			 "qca/htbtfw%02x.tlv", rom_ver);
		break;
	case QCA_WCN6750:
		/* Choose mbn file by default.If mbn file is not found
		 * then choose tlv file
		 */
		config.type = ELF_TYPE_PATCH;
		snprintf(config.fwname, sizeof(config.fwname),
			 "qca/msbtfw%02x.mbn", rom_ver);
		break;
	case QCA_WCN6855:
		snprintf(config.fwname, sizeof(config.fwname),
			 "qca/hpbtfw%02x.tlv", rom_ver);
		break;
	case QCA_WCN7850:
		snprintf(config.fwname, sizeof(config.fwname),
			 "qca/hmtbtfw%02x.tlv", rom_ver);
		break;
	default:
		snprintf(config.fwname, sizeof(config.fwname),
			 "qca/rampatch_%08x.bin", soc_ver);
	}

	err = qca_download_firmware(hdev, &config, soc_type, rom_ver);
	if (err < 0) {
		bt_dev_err(hdev, "QCA Failed to download patch (%d)", err);
		return err;
	}

	/* Give the controller some time to get ready to receive the NVM */
	msleep(10);

	/* Download NVM configuration */
	config.type = TLV_TYPE_NVM;
	if (firmware_name) {
		snprintf(config.fwname, sizeof(config.fwname),
			 "qca/%s", firmware_name);
	} else {
		switch (soc_type) {
		case QCA_WCN3990:
		case QCA_WCN3991:
		case QCA_WCN3998:
			if (le32_to_cpu(ver.soc_id) == QCA_WCN3991_SOC_ID) {
				snprintf(config.fwname, sizeof(config.fwname),
					 "qca/crnv%02xu.bin", rom_ver);
			} else {
				snprintf(config.fwname, sizeof(config.fwname),
					 "qca/crnv%02x.bin", rom_ver);
			}
			break;
		case QCA_WCN3988:
			snprintf(config.fwname, sizeof(config.fwname),
				 "qca/apnv%02x.bin", rom_ver);
			break;
		case QCA_QCA6390:
			snprintf(config.fwname, sizeof(config.fwname),
				 "qca/htnv%02x.bin", rom_ver);
			break;
		case QCA_WCN6750:
			snprintf(config.fwname, sizeof(config.fwname),
				 "qca/msnv%02x.bin", rom_ver);
			break;
		case QCA_WCN6855:
			snprintf(config.fwname, sizeof(config.fwname),
				 "qca/hpnv%02x.bin", rom_ver);
			break;
		case QCA_WCN7850:
			snprintf(config.fwname, sizeof(config.fwname),
				 "qca/hmtnv%02x.bin", rom_ver);
			break;

		default:
			snprintf(config.fwname, sizeof(config.fwname),
				 "qca/nvm_%08x.bin", soc_ver);
		}
	}

	err = qca_download_firmware(hdev, &config, soc_type, rom_ver);
	if (err < 0) {
		bt_dev_err(hdev, "QCA Failed to download NVM (%d)", err);
		return err;
	}

	switch (soc_type) {
	case QCA_WCN3991:
	case QCA_QCA6390:
	case QCA_WCN6750:
	case QCA_WCN6855:
	case QCA_WCN7850:
		err = qca_disable_soc_logging(hdev);
		if (err < 0)
			return err;
		break;
	default:
		break;
	}

	/* WCN399x and WCN6750 supports the Microsoft vendor extension with 0xFD70 as the
	 * VsMsftOpCode.
	 */
	switch (soc_type) {
	case QCA_WCN3988:
	case QCA_WCN3990:
	case QCA_WCN3991:
	case QCA_WCN3998:
	case QCA_WCN6750:
		hci_set_msft_opcode(hdev, 0xFD70);
		break;
	default:
		break;
	}

	/* Perform HCI reset */
	err = qca_send_reset(hdev);
	if (err < 0) {
		bt_dev_err(hdev, "QCA Failed to run HCI_RESET (%d)", err);
		return err;
	}

	switch (soc_type) {
	case QCA_WCN3991:
	case QCA_WCN6750:
	case QCA_WCN6855:
	case QCA_WCN7850:
		/* get fw build info */
		err = qca_read_fw_build_info(hdev);
		if (err < 0)
			return err;
		break;
	default:
		break;
	}

	bt_dev_info(hdev, "QCA setup on UART is completed");

	return 0;
}
EXPORT_SYMBOL_GPL(qca_uart_setup);

int qca_set_bdaddr(struct hci_dev *hdev, const bdaddr_t *bdaddr)
{
	bdaddr_t bdaddr_swapped;
	struct sk_buff *skb;
	int err;

	baswap(&bdaddr_swapped, bdaddr);

	skb = __hci_cmd_sync_ev(hdev, EDL_WRITE_BD_ADDR_OPCODE, 6,
				&bdaddr_swapped, HCI_EV_VENDOR,
				HCI_INIT_TIMEOUT);
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
