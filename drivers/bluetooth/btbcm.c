/*
 *
 *  Bluetooth support for Broadcom devices
 *
 *  Copyright (C) 2015  Intel Corporation
 *
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

#include <linux/module.h>
#include <linux/firmware.h>
#include <asm/unaligned.h>

#include <net/bluetooth/bluetooth.h>
#include <net/bluetooth/hci_core.h>

#include "btbcm.h"

#define VERSION "0.1"

#define BDADDR_BCM20702A0 (&(bdaddr_t) {{0x00, 0xa0, 0x02, 0x70, 0x20, 0x00}})
#define BDADDR_BCM4324B3 (&(bdaddr_t) {{0x00, 0x00, 0x00, 0xb3, 0x24, 0x43}})

int btbcm_check_bdaddr(struct hci_dev *hdev)
{
	struct hci_rp_read_bd_addr *bda;
	struct sk_buff *skb;

	skb = __hci_cmd_sync(hdev, HCI_OP_READ_BD_ADDR, 0, NULL,
			     HCI_INIT_TIMEOUT);
	if (IS_ERR(skb)) {
		int err = PTR_ERR(skb);
		BT_ERR("%s: BCM: Reading device address failed (%d)",
		       hdev->name, err);
		return err;
	}

	if (skb->len != sizeof(*bda)) {
		BT_ERR("%s: BCM: Device address length mismatch", hdev->name);
		kfree_skb(skb);
		return -EIO;
	}

	bda = (struct hci_rp_read_bd_addr *)skb->data;

	/* Check if the address indicates a controller with either an
	 * invalid or default address. In both cases the device needs
	 * to be marked as not having a valid address.
	 *
	 * The address 00:20:70:02:A0:00 indicates a BCM20702A0 controller
	 * with no configured address.
	 *
	 * The address 43:24:B3:00:00:00 indicates a BCM4324B3 controller
	 * with waiting for configuration state.
	 */
	if (!bacmp(&bda->bdaddr, BDADDR_BCM20702A0) ||
	    !bacmp(&bda->bdaddr, BDADDR_BCM4324B3)) {
		BT_INFO("%s: BCM: Using default device address (%pMR)",
			hdev->name, &bda->bdaddr);
		set_bit(HCI_QUIRK_INVALID_BDADDR, &hdev->quirks);
	}

	kfree_skb(skb);

	return 0;
}
EXPORT_SYMBOL_GPL(btbcm_check_bdaddr);

int btbcm_set_bdaddr(struct hci_dev *hdev, const bdaddr_t *bdaddr)
{
	struct sk_buff *skb;
	int err;

	skb = __hci_cmd_sync(hdev, 0xfc01, 6, bdaddr, HCI_INIT_TIMEOUT);
	if (IS_ERR(skb)) {
		err = PTR_ERR(skb);
		BT_ERR("%s: BCM: Change address command failed (%d)",
		       hdev->name, err);
		return err;
	}
	kfree_skb(skb);

	return 0;
}
EXPORT_SYMBOL_GPL(btbcm_set_bdaddr);

int btbcm_patchram(struct hci_dev *hdev, const struct firmware *fw)
{
	const struct hci_command_hdr *cmd;
	const u8 *fw_ptr;
	size_t fw_size;
	struct sk_buff *skb;
	u16 opcode;
	int err = 0;

	/* Start Download */
	skb = __hci_cmd_sync(hdev, 0xfc2e, 0, NULL, HCI_INIT_TIMEOUT);
	if (IS_ERR(skb)) {
		err = PTR_ERR(skb);
		BT_ERR("%s: BCM: Download Minidrv command failed (%d)",
		       hdev->name, err);
		goto done;
	}
	kfree_skb(skb);

	/* 50 msec delay after Download Minidrv completes */
	msleep(50);

	fw_ptr = fw->data;
	fw_size = fw->size;

	while (fw_size >= sizeof(*cmd)) {
		const u8 *cmd_param;

		cmd = (struct hci_command_hdr *)fw_ptr;
		fw_ptr += sizeof(*cmd);
		fw_size -= sizeof(*cmd);

		if (fw_size < cmd->plen) {
			BT_ERR("%s: BCM: Patch is corrupted", hdev->name);
			err = -EINVAL;
			goto done;
		}

		cmd_param = fw_ptr;
		fw_ptr += cmd->plen;
		fw_size -= cmd->plen;

		opcode = le16_to_cpu(cmd->opcode);

		skb = __hci_cmd_sync(hdev, opcode, cmd->plen, cmd_param,
				     HCI_INIT_TIMEOUT);
		if (IS_ERR(skb)) {
			err = PTR_ERR(skb);
			BT_ERR("%s: BCM: Patch command %04x failed (%d)",
			       hdev->name, opcode, err);
			goto done;
		}
		kfree_skb(skb);
	}

	/* 250 msec delay after Launch Ram completes */
	msleep(250);

done:
	return err;
}
EXPORT_SYMBOL(btbcm_patchram);

static int btbcm_reset(struct hci_dev *hdev)
{
	struct sk_buff *skb;

	skb = __hci_cmd_sync(hdev, HCI_OP_RESET, 0, NULL, HCI_INIT_TIMEOUT);
	if (IS_ERR(skb)) {
		int err = PTR_ERR(skb);
		BT_ERR("%s: BCM: Reset failed (%d)", hdev->name, err);
		return err;
	}
	kfree_skb(skb);

	return 0;
}

static struct sk_buff *btbcm_read_local_version(struct hci_dev *hdev)
{
	struct sk_buff *skb;

	skb = __hci_cmd_sync(hdev, HCI_OP_READ_LOCAL_VERSION, 0, NULL,
			     HCI_INIT_TIMEOUT);
	if (IS_ERR(skb)) {
		BT_ERR("%s: BCM: Reading local version info failed (%ld)",
		       hdev->name, PTR_ERR(skb));
		return skb;
	}

	if (skb->len != sizeof(struct hci_rp_read_local_version)) {
		BT_ERR("%s: BCM: Local version length mismatch", hdev->name);
		kfree_skb(skb);
		return ERR_PTR(-EIO);
	}

	return skb;
}

static struct sk_buff *btbcm_read_verbose_config(struct hci_dev *hdev)
{
	struct sk_buff *skb;

	skb = __hci_cmd_sync(hdev, 0xfc79, 0, NULL, HCI_INIT_TIMEOUT);
	if (IS_ERR(skb)) {
		BT_ERR("%s: BCM: Read verbose config info failed (%ld)",
		       hdev->name, PTR_ERR(skb));
		return skb;
	}

	if (skb->len != 7) {
		BT_ERR("%s: BCM: Verbose config length mismatch", hdev->name);
		kfree_skb(skb);
		return ERR_PTR(-EIO);
	}

	return skb;
}

static struct sk_buff *btbcm_read_usb_product(struct hci_dev *hdev)
{
	struct sk_buff *skb;

	skb = __hci_cmd_sync(hdev, 0xfc5a, 0, NULL, HCI_INIT_TIMEOUT);
	if (IS_ERR(skb)) {
		BT_ERR("%s: BCM: Read USB product info failed (%ld)",
		       hdev->name, PTR_ERR(skb));
		return skb;
	}

	if (skb->len != 5) {
		BT_ERR("%s: BCM: USB product length mismatch", hdev->name);
		kfree_skb(skb);
		return ERR_PTR(-EIO);
	}

	return skb;
}

static const struct {
	u16 subver;
	const char *name;
} bcm_uart_subver_table[] = {
	{ 0x410e, "BCM43341B0"	},	/* 002.001.014 */
	{ 0x4406, "BCM4324B3"	},	/* 002.004.006 */
	{ 0x610c, "BCM4354"	},	/* 003.001.012 */
	{ }
};

int btbcm_initialize(struct hci_dev *hdev, char *fw_name, size_t len)
{
	u16 subver, rev;
	const char *hw_name = NULL;
	struct sk_buff *skb;
	struct hci_rp_read_local_version *ver;
	int i, err;

	/* Reset */
	err = btbcm_reset(hdev);
	if (err)
		return err;

	/* Read Local Version Info */
	skb = btbcm_read_local_version(hdev);
	if (IS_ERR(skb))
		return PTR_ERR(skb);

	ver = (struct hci_rp_read_local_version *)skb->data;
	rev = le16_to_cpu(ver->hci_rev);
	subver = le16_to_cpu(ver->lmp_subver);
	kfree_skb(skb);

	/* Read Verbose Config Version Info */
	skb = btbcm_read_verbose_config(hdev);
	if (IS_ERR(skb))
		return PTR_ERR(skb);

	BT_INFO("%s: BCM: chip id %u", hdev->name, skb->data[1]);
	kfree_skb(skb);

	switch ((rev & 0xf000) >> 12) {
	case 0:
	case 1:
	case 3:
		for (i = 0; bcm_uart_subver_table[i].name; i++) {
			if (subver == bcm_uart_subver_table[i].subver) {
				hw_name = bcm_uart_subver_table[i].name;
				break;
			}
		}

		snprintf(fw_name, len, "brcm/%s.hcd", hw_name ? : "BCM");
		break;
	default:
		return 0;
	}

	BT_INFO("%s: %s (%3.3u.%3.3u.%3.3u) build %4.4u", hdev->name,
		hw_name ? : "BCM", (subver & 0x7000) >> 13,
		(subver & 0x1f00) >> 8, (subver & 0x00ff), rev & 0x0fff);

	return 0;
}
EXPORT_SYMBOL_GPL(btbcm_initialize);

int btbcm_finalize(struct hci_dev *hdev)
{
	struct sk_buff *skb;
	struct hci_rp_read_local_version *ver;
	u16 subver, rev;
	int err;

	/* Reset */
	err = btbcm_reset(hdev);
	if (err)
		return err;

	/* Read Local Version Info */
	skb = btbcm_read_local_version(hdev);
	if (IS_ERR(skb))
		return PTR_ERR(skb);

	ver = (struct hci_rp_read_local_version *)skb->data;
	rev = le16_to_cpu(ver->hci_rev);
	subver = le16_to_cpu(ver->lmp_subver);
	kfree_skb(skb);

	BT_INFO("%s: BCM (%3.3u.%3.3u.%3.3u) build %4.4u", hdev->name,
		(subver & 0x7000) >> 13, (subver & 0x1f00) >> 8,
		(subver & 0x00ff), rev & 0x0fff);

	btbcm_check_bdaddr(hdev);

	set_bit(HCI_QUIRK_STRICT_DUPLICATE_FILTER, &hdev->quirks);

	return 0;
}
EXPORT_SYMBOL_GPL(btbcm_finalize);

static const struct {
	u16 subver;
	const char *name;
} bcm_usb_subver_table[] = {
	{ 0x210b, "BCM43142A0"	},	/* 001.001.011 */
	{ 0x2112, "BCM4314A0"	},	/* 001.001.018 */
	{ 0x2118, "BCM20702A0"	},	/* 001.001.024 */
	{ 0x2126, "BCM4335A0"	},	/* 001.001.038 */
	{ 0x220e, "BCM20702A1"	},	/* 001.002.014 */
	{ 0x230f, "BCM4354A2"	},	/* 001.003.015 */
	{ 0x4106, "BCM4335B0"	},	/* 002.001.006 */
	{ 0x410e, "BCM20702B0"	},	/* 002.001.014 */
	{ 0x6109, "BCM4335C0"	},	/* 003.001.009 */
	{ 0x610c, "BCM4354"	},	/* 003.001.012 */
	{ }
};

int btbcm_setup_patchram(struct hci_dev *hdev)
{
	char fw_name[64];
	const struct firmware *fw;
	u16 subver, rev, pid, vid;
	const char *hw_name = NULL;
	struct sk_buff *skb;
	struct hci_rp_read_local_version *ver;
	int i, err;

	/* Reset */
	err = btbcm_reset(hdev);
	if (err)
		return err;

	/* Read Local Version Info */
	skb = btbcm_read_local_version(hdev);
	if (IS_ERR(skb))
		return PTR_ERR(skb);

	ver = (struct hci_rp_read_local_version *)skb->data;
	rev = le16_to_cpu(ver->hci_rev);
	subver = le16_to_cpu(ver->lmp_subver);
	kfree_skb(skb);

	/* Read Verbose Config Version Info */
	skb = btbcm_read_verbose_config(hdev);
	if (IS_ERR(skb))
		return PTR_ERR(skb);

	BT_INFO("%s: BCM: chip id %u", hdev->name, skb->data[1]);
	kfree_skb(skb);

	switch ((rev & 0xf000) >> 12) {
	case 0:
	case 3:
		for (i = 0; bcm_uart_subver_table[i].name; i++) {
			if (subver == bcm_uart_subver_table[i].subver) {
				hw_name = bcm_uart_subver_table[i].name;
				break;
			}
		}

		snprintf(fw_name, sizeof(fw_name), "brcm/%s.hcd",
			 hw_name ? : "BCM");
		break;
	case 1:
	case 2:
		/* Read USB Product Info */
		skb = btbcm_read_usb_product(hdev);
		if (IS_ERR(skb))
			return PTR_ERR(skb);

		vid = get_unaligned_le16(skb->data + 1);
		pid = get_unaligned_le16(skb->data + 3);
		kfree_skb(skb);

		for (i = 0; bcm_usb_subver_table[i].name; i++) {
			if (subver == bcm_usb_subver_table[i].subver) {
				hw_name = bcm_usb_subver_table[i].name;
				break;
			}
		}

		snprintf(fw_name, sizeof(fw_name), "brcm/%s-%4.4x-%4.4x.hcd",
			 hw_name ? : "BCM", vid, pid);
		break;
	default:
		return 0;
	}

	BT_INFO("%s: %s (%3.3u.%3.3u.%3.3u) build %4.4u", hdev->name,
		hw_name ? : "BCM", (subver & 0x7000) >> 13,
		(subver & 0x1f00) >> 8, (subver & 0x00ff), rev & 0x0fff);

	err = request_firmware(&fw, fw_name, &hdev->dev);
	if (err < 0) {
		BT_INFO("%s: BCM: Patch %s not found", hdev->name, fw_name);
		return 0;
	}

	btbcm_patchram(hdev, fw);

	release_firmware(fw);

	/* Reset */
	err = btbcm_reset(hdev);
	if (err)
		return err;

	/* Read Local Version Info */
	skb = btbcm_read_local_version(hdev);
	if (IS_ERR(skb))
		return PTR_ERR(skb);

	ver = (struct hci_rp_read_local_version *)skb->data;
	rev = le16_to_cpu(ver->hci_rev);
	subver = le16_to_cpu(ver->lmp_subver);
	kfree_skb(skb);

	BT_INFO("%s: %s (%3.3u.%3.3u.%3.3u) build %4.4u", hdev->name,
		hw_name ? : "BCM", (subver & 0x7000) >> 13,
		(subver & 0x1f00) >> 8, (subver & 0x00ff), rev & 0x0fff);

	btbcm_check_bdaddr(hdev);

	set_bit(HCI_QUIRK_STRICT_DUPLICATE_FILTER, &hdev->quirks);

	return 0;
}
EXPORT_SYMBOL_GPL(btbcm_setup_patchram);

int btbcm_setup_apple(struct hci_dev *hdev)
{
	struct sk_buff *skb;

	/* Read Verbose Config Version Info */
	skb = btbcm_read_verbose_config(hdev);
	if (!IS_ERR(skb)) {
		BT_INFO("%s: BCM: chip id %u build %4.4u", hdev->name, skb->data[1],
			get_unaligned_le16(skb->data + 5));
		kfree_skb(skb);
	}

	set_bit(HCI_QUIRK_STRICT_DUPLICATE_FILTER, &hdev->quirks);

	return 0;
}
EXPORT_SYMBOL_GPL(btbcm_setup_apple);

MODULE_AUTHOR("Marcel Holtmann <marcel@holtmann.org>");
MODULE_DESCRIPTION("Bluetooth support for Broadcom devices ver " VERSION);
MODULE_VERSION(VERSION);
MODULE_LICENSE("GPL");
