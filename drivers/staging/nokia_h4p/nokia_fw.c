/*
 * This file is part of hci_h4p bluetooth driver
 *
 * Copyright (C) 2005, 2006 Nokia Corporation.
 *
 * Contact: Ville Tervo <ville.tervo@nokia.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 */

#include <linux/module.h>
#include <linux/skbuff.h>
#include <linux/firmware.h>
#include <linux/clk.h>

#include <net/bluetooth/bluetooth.h>

#include "hci_h4p.h"

#define FW_NAME_TI1271_PRELE	"ti1273_prele.bin"
#define FW_NAME_TI1271_LE	"ti1273_le.bin"
#define FW_NAME_TI1271		"ti1273.bin"
#define FW_NAME_BCM2048		"bcmfw.bin"
#define FW_NAME_CSR		"bc4fw.bin"

static int fw_pos;

/* Firmware handling */
static int hci_h4p_open_firmware(struct hci_h4p_info *info,
				 const struct firmware **fw_entry)
{
	int err;

	fw_pos = 0;
	BT_DBG("Opening firmware man_id 0x%.2x ver_id 0x%.2x",
			info->man_id, info->ver_id);
	switch (info->man_id) {
	case H4P_ID_TI1271:
		switch (info->ver_id) {
		case 0xe1:
			err = request_firmware(fw_entry, FW_NAME_TI1271_PRELE,
						info->dev);
			break;
		case 0xd1:
		case 0xf1:
			err = request_firmware(fw_entry, FW_NAME_TI1271_LE,
						info->dev);
			break;
		default:
			err = request_firmware(fw_entry, FW_NAME_TI1271,
						info->dev);
		}
		break;
	case H4P_ID_CSR:
		err = request_firmware(fw_entry, FW_NAME_CSR, info->dev);
		break;
	case H4P_ID_BCM2048:
		err = request_firmware(fw_entry, FW_NAME_BCM2048, info->dev);
		break;
	default:
		dev_err(info->dev, "Invalid chip type\n");
		*fw_entry = NULL;
		err = -EINVAL;
	}

	return err;
}

static void hci_h4p_close_firmware(const struct firmware *fw_entry)
{
	release_firmware(fw_entry);
}

/* Read fw. Return length of the command. If no more commands in
 * fw 0 is returned. In error case return value is negative.
 */
static int hci_h4p_read_fw_cmd(struct hci_h4p_info *info, struct sk_buff **skb,
			       const struct firmware *fw_entry, gfp_t how)
{
	unsigned int cmd_len;

	if (fw_pos >= fw_entry->size)
		return 0;

	if (fw_pos + 2 > fw_entry->size) {
		dev_err(info->dev, "Corrupted firmware image 1\n");
		return -EMSGSIZE;
	}

	cmd_len = fw_entry->data[fw_pos++];
	cmd_len += fw_entry->data[fw_pos++] << 8;
	if (cmd_len == 0)
		return 0;

	if (fw_pos + cmd_len > fw_entry->size) {
		dev_err(info->dev, "Corrupted firmware image 2\n");
		return -EMSGSIZE;
	}

	*skb = bt_skb_alloc(cmd_len, how);
	if (!*skb) {
		dev_err(info->dev, "Cannot reserve memory for buffer\n");
		return -ENOMEM;
	}
	memcpy(skb_put(*skb, cmd_len), &fw_entry->data[fw_pos], cmd_len);

	fw_pos += cmd_len;

	return (*skb)->len;
}

int hci_h4p_read_fw(struct hci_h4p_info *info, struct sk_buff_head *fw_queue)
{
	const struct firmware *fw_entry = NULL;
	struct sk_buff *skb = NULL;
	int err;

	err = hci_h4p_open_firmware(info, &fw_entry);
	if (err < 0 || !fw_entry)
		goto err_clean;

	while ((err = hci_h4p_read_fw_cmd(info, &skb, fw_entry, GFP_KERNEL))) {
		if (err < 0 || !skb)
			goto err_clean;

		skb_queue_tail(fw_queue, skb);
	}

	/* Chip detection code does neg and alive stuff
	 * discard two first skbs */
	skb = skb_dequeue(fw_queue);
	if (!skb) {
		err = -EMSGSIZE;
		goto err_clean;
	}
	kfree_skb(skb);
	skb = skb_dequeue(fw_queue);
	if (!skb) {
		err = -EMSGSIZE;
		goto err_clean;
	}
	kfree_skb(skb);

err_clean:
	hci_h4p_close_firmware(fw_entry);
	return err;
}

int hci_h4p_send_fw(struct hci_h4p_info *info, struct sk_buff_head *fw_queue)
{
	int err;

	switch (info->man_id) {
	case H4P_ID_CSR:
		err = hci_h4p_bc4_send_fw(info, fw_queue);
		break;
	case H4P_ID_TI1271:
		err = hci_h4p_ti1273_send_fw(info, fw_queue);
		break;
	case H4P_ID_BCM2048:
		err = hci_h4p_bcm_send_fw(info, fw_queue);
		break;
	default:
		dev_err(info->dev, "Don't know how to send firmware\n");
		err = -EINVAL;
	}

	return err;
}

void hci_h4p_parse_fw_event(struct hci_h4p_info *info, struct sk_buff *skb)
{
	switch (info->man_id) {
	case H4P_ID_CSR:
		hci_h4p_bc4_parse_fw_event(info, skb);
		break;
	case H4P_ID_TI1271:
		hci_h4p_ti1273_parse_fw_event(info, skb);
		break;
	case H4P_ID_BCM2048:
		hci_h4p_bcm_parse_fw_event(info, skb);
		break;
	default:
		dev_err(info->dev, "Don't know how to parse fw event\n");
		info->fw_error = -EINVAL;
	}

	return;
}

MODULE_FIRMWARE(FW_NAME_TI1271_PRELE);
MODULE_FIRMWARE(FW_NAME_TI1271_LE);
MODULE_FIRMWARE(FW_NAME_TI1271);
MODULE_FIRMWARE(FW_NAME_BCM2048);
MODULE_FIRMWARE(FW_NAME_CSR);
