// SPDX-License-Identifier: GPL-2.0-only
/*
 * Generic driver for NXP NCI NFC chips
 *
 * Copyright (C) 2014  NXP Semiconductors  All rights reserved.
 *
 * Author: Clément Perrochaud <clement.perrochaud@nxp.com>
 *
 * Derived from PN544 device driver:
 * Copyright (C) 2012  Intel Corporation. All rights reserved.
 */

#include <linux/completion.h>
#include <linux/firmware.h>
#include <linux/nfc.h>
#include <asm/unaligned.h>

#include "nxp-nci.h"

/* Crypto operations can take up to 30 seconds */
#define NXP_NCI_FW_ANSWER_TIMEOUT	msecs_to_jiffies(30000)

#define NXP_NCI_FW_CMD_RESET		0xF0
#define NXP_NCI_FW_CMD_GETVERSION	0xF1
#define NXP_NCI_FW_CMD_CHECKINTEGRITY	0xE0
#define NXP_NCI_FW_CMD_WRITE		0xC0
#define NXP_NCI_FW_CMD_READ		0xA2
#define NXP_NCI_FW_CMD_GETSESSIONSTATE	0xF2
#define NXP_NCI_FW_CMD_LOG		0xA7
#define NXP_NCI_FW_CMD_FORCE		0xD0
#define NXP_NCI_FW_CMD_GET_DIE_ID	0xF4

#define NXP_NCI_FW_CHUNK_FLAG	0x0400

#define NXP_NCI_FW_RESULT_OK				0x00
#define NXP_NCI_FW_RESULT_INVALID_ADDR			0x01
#define NXP_NCI_FW_RESULT_GENERIC_ERROR			0x02
#define NXP_NCI_FW_RESULT_UNKNOWN_CMD			0x0B
#define NXP_NCI_FW_RESULT_ABORTED_CMD			0x0C
#define NXP_NCI_FW_RESULT_PLL_ERROR			0x0D
#define NXP_NCI_FW_RESULT_ADDR_RANGE_OFL_ERROR		0x1E
#define NXP_NCI_FW_RESULT_BUFFER_OFL_ERROR		0x1F
#define NXP_NCI_FW_RESULT_MEM_BSY			0x20
#define NXP_NCI_FW_RESULT_SIGNATURE_ERROR		0x21
#define NXP_NCI_FW_RESULT_FIRMWARE_VERSION_ERROR	0x24
#define NXP_NCI_FW_RESULT_PROTOCOL_ERROR		0x28
#define NXP_NCI_FW_RESULT_SFWU_DEGRADED			0x2A
#define NXP_NCI_FW_RESULT_PH_STATUS_FIRST_CHUNK		0x2D
#define NXP_NCI_FW_RESULT_PH_STATUS_NEXT_CHUNK		0x2E
#define NXP_NCI_FW_RESULT_PH_STATUS_INTERNAL_ERROR_5	0xC5

void nxp_nci_fw_work_complete(struct nxp_nci_info *info, int result)
{
	struct nxp_nci_fw_info *fw_info = &info->fw_info;
	int r;

	if (info->phy_ops->set_mode) {
		r = info->phy_ops->set_mode(info->phy_id, NXP_NCI_MODE_COLD);
		if (r < 0 && result == 0)
			result = -r;
	}

	info->mode = NXP_NCI_MODE_COLD;

	if (fw_info->fw) {
		release_firmware(fw_info->fw);
		fw_info->fw = NULL;
	}

	nfc_fw_download_done(info->ndev->nfc_dev, fw_info->name, (u32) -result);
}

/* crc_ccitt cannot be used since it is computed MSB first and not LSB first */
static u16 nxp_nci_fw_crc(u8 const *buffer, size_t len)
{
	u16 crc = 0xffff;

	while (len--) {
		crc = ((crc >> 8) | (crc << 8)) ^ *buffer++;
		crc ^= (crc & 0xff) >> 4;
		crc ^= (crc & 0xff) << 12;
		crc ^= (crc & 0xff) << 5;
	}

	return crc;
}

static int nxp_nci_fw_send_chunk(struct nxp_nci_info *info)
{
	struct nxp_nci_fw_info *fw_info = &info->fw_info;
	u16 header, crc;
	struct sk_buff *skb;
	size_t chunk_len;
	size_t remaining_len;
	int r;

	skb = nci_skb_alloc(info->ndev, info->max_payload, GFP_KERNEL);
	if (!skb)
		return -ENOMEM;

	chunk_len = info->max_payload - NXP_NCI_FW_HDR_LEN - NXP_NCI_FW_CRC_LEN;
	remaining_len = fw_info->frame_size - fw_info->written;

	if (remaining_len > chunk_len) {
		header = NXP_NCI_FW_CHUNK_FLAG;
	} else {
		chunk_len = remaining_len;
		header = 0x0000;
	}

	header |= chunk_len & NXP_NCI_FW_FRAME_LEN_MASK;
	put_unaligned_be16(header, skb_put(skb, NXP_NCI_FW_HDR_LEN));

	skb_put_data(skb, fw_info->data + fw_info->written, chunk_len);

	crc = nxp_nci_fw_crc(skb->data, chunk_len + NXP_NCI_FW_HDR_LEN);
	put_unaligned_be16(crc, skb_put(skb, NXP_NCI_FW_CRC_LEN));

	r = info->phy_ops->write(info->phy_id, skb);
	if (r >= 0)
		r = chunk_len;

	kfree_skb(skb);

	return r;
}

static int nxp_nci_fw_send(struct nxp_nci_info *info)
{
	struct nxp_nci_fw_info *fw_info = &info->fw_info;
	long completion_rc;
	int r;

	reinit_completion(&fw_info->cmd_completion);

	if (fw_info->written == 0) {
		fw_info->frame_size = get_unaligned_be16(fw_info->data) &
				      NXP_NCI_FW_FRAME_LEN_MASK;
		fw_info->data += NXP_NCI_FW_HDR_LEN;
		fw_info->size -= NXP_NCI_FW_HDR_LEN;
	}

	if (fw_info->frame_size > fw_info->size)
		return -EMSGSIZE;

	r = nxp_nci_fw_send_chunk(info);
	if (r < 0)
		return r;

	fw_info->written += r;

	if (*fw_info->data == NXP_NCI_FW_CMD_RESET) {
		fw_info->cmd_result = 0;
		if (fw_info->fw)
			schedule_work(&fw_info->work);
	} else {
		completion_rc = wait_for_completion_interruptible_timeout(
			&fw_info->cmd_completion, NXP_NCI_FW_ANSWER_TIMEOUT);
		if (completion_rc == 0)
			return -ETIMEDOUT;
	}

	return 0;
}

void nxp_nci_fw_work(struct work_struct *work)
{
	struct nxp_nci_info *info;
	struct nxp_nci_fw_info *fw_info;
	int r;

	fw_info = container_of(work, struct nxp_nci_fw_info, work);
	info = container_of(fw_info, struct nxp_nci_info, fw_info);

	mutex_lock(&info->info_lock);

	r = fw_info->cmd_result;
	if (r < 0)
		goto exit_work;

	if (fw_info->written == fw_info->frame_size) {
		fw_info->data += fw_info->frame_size;
		fw_info->size -= fw_info->frame_size;
		fw_info->written = 0;
	}

	if (fw_info->size > 0)
		r = nxp_nci_fw_send(info);

exit_work:
	if (r < 0 || fw_info->size == 0)
		nxp_nci_fw_work_complete(info, r);
	mutex_unlock(&info->info_lock);
}

int nxp_nci_fw_download(struct nci_dev *ndev, const char *firmware_name)
{
	struct nxp_nci_info *info = nci_get_drvdata(ndev);
	struct nxp_nci_fw_info *fw_info = &info->fw_info;
	int r;

	mutex_lock(&info->info_lock);

	if (!info->phy_ops->set_mode || !info->phy_ops->write) {
		r = -ENOTSUPP;
		goto fw_download_exit;
	}

	if (!firmware_name || firmware_name[0] == '\0') {
		r = -EINVAL;
		goto fw_download_exit;
	}

	strcpy(fw_info->name, firmware_name);

	r = request_firmware(&fw_info->fw, firmware_name,
			     ndev->nfc_dev->dev.parent);
	if (r < 0)
		goto fw_download_exit;

	r = info->phy_ops->set_mode(info->phy_id, NXP_NCI_MODE_FW);
	if (r < 0) {
		release_firmware(fw_info->fw);
		goto fw_download_exit;
	}

	info->mode = NXP_NCI_MODE_FW;

	fw_info->data = fw_info->fw->data;
	fw_info->size = fw_info->fw->size;
	fw_info->written = 0;
	fw_info->frame_size = 0;
	fw_info->cmd_result = 0;

	schedule_work(&fw_info->work);

fw_download_exit:
	mutex_unlock(&info->info_lock);
	return r;
}

static int nxp_nci_fw_read_status(u8 stat)
{
	switch (stat) {
	case NXP_NCI_FW_RESULT_OK:
		return 0;
	case NXP_NCI_FW_RESULT_INVALID_ADDR:
		return -EINVAL;
	case NXP_NCI_FW_RESULT_UNKNOWN_CMD:
		return -EINVAL;
	case NXP_NCI_FW_RESULT_ABORTED_CMD:
		return -EMSGSIZE;
	case NXP_NCI_FW_RESULT_ADDR_RANGE_OFL_ERROR:
		return -EADDRNOTAVAIL;
	case NXP_NCI_FW_RESULT_BUFFER_OFL_ERROR:
		return -ENOBUFS;
	case NXP_NCI_FW_RESULT_MEM_BSY:
		return -ENOKEY;
	case NXP_NCI_FW_RESULT_SIGNATURE_ERROR:
		return -EKEYREJECTED;
	case NXP_NCI_FW_RESULT_FIRMWARE_VERSION_ERROR:
		return -EALREADY;
	case NXP_NCI_FW_RESULT_PROTOCOL_ERROR:
		return -EPROTO;
	case NXP_NCI_FW_RESULT_SFWU_DEGRADED:
		return -EHWPOISON;
	case NXP_NCI_FW_RESULT_PH_STATUS_FIRST_CHUNK:
		return 0;
	case NXP_NCI_FW_RESULT_PH_STATUS_NEXT_CHUNK:
		return 0;
	case NXP_NCI_FW_RESULT_PH_STATUS_INTERNAL_ERROR_5:
		return -EINVAL;
	default:
		return -EIO;
	}
}

static u16 nxp_nci_fw_check_crc(struct sk_buff *skb)
{
	u16 crc, frame_crc;
	size_t len = skb->len - NXP_NCI_FW_CRC_LEN;

	crc = nxp_nci_fw_crc(skb->data, len);
	frame_crc = get_unaligned_be16(skb->data + len);

	return (crc ^ frame_crc);
}

void nxp_nci_fw_recv_frame(struct nci_dev *ndev, struct sk_buff *skb)
{
	struct nxp_nci_info *info = nci_get_drvdata(ndev);
	struct nxp_nci_fw_info *fw_info = &info->fw_info;

	complete(&fw_info->cmd_completion);

	if (skb) {
		if (nxp_nci_fw_check_crc(skb) != 0x00)
			fw_info->cmd_result = -EBADMSG;
		else
			fw_info->cmd_result = nxp_nci_fw_read_status(*(u8 *)skb_pull(skb, NXP_NCI_FW_HDR_LEN));
		kfree_skb(skb);
	} else {
		fw_info->cmd_result = -EIO;
	}

	if (fw_info->fw)
		schedule_work(&fw_info->work);
}
EXPORT_SYMBOL(nxp_nci_fw_recv_frame);
