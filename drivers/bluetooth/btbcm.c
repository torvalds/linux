// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *
 *  Bluetooth support for Broadcom devices
 *
 *  Copyright (C) 2015  Intel Corporation
 */

#include <linux/module.h>
#include <linux/firmware.h>
#include <linux/dmi.h>
#include <linux/of.h>
#include <asm/unaligned.h>

#include <net/bluetooth/bluetooth.h>
#include <net/bluetooth/hci_core.h>

#include "btbcm.h"

#define VERSION "0.1"

#define BDADDR_BCM20702A0 (&(bdaddr_t) {{0x00, 0xa0, 0x02, 0x70, 0x20, 0x00}})
#define BDADDR_BCM20702A1 (&(bdaddr_t) {{0x00, 0x00, 0xa0, 0x02, 0x70, 0x20}})
#define BDADDR_BCM2076B1 (&(bdaddr_t) {{0x79, 0x56, 0x00, 0xa0, 0x76, 0x20}})
#define BDADDR_BCM43430A0 (&(bdaddr_t) {{0xac, 0x1f, 0x12, 0xa0, 0x43, 0x43}})
#define BDADDR_BCM4324B3 (&(bdaddr_t) {{0x00, 0x00, 0x00, 0xb3, 0x24, 0x43}})
#define BDADDR_BCM4330B1 (&(bdaddr_t) {{0x00, 0x00, 0x00, 0xb1, 0x30, 0x43}})
#define BDADDR_BCM4334B0 (&(bdaddr_t) {{0x00, 0x00, 0x00, 0xb0, 0x34, 0x43}})
#define BDADDR_BCM4345C5 (&(bdaddr_t) {{0xac, 0x1f, 0x00, 0xc5, 0x45, 0x43}})
#define BDADDR_BCM43341B (&(bdaddr_t) {{0xac, 0x1f, 0x00, 0x1b, 0x34, 0x43}})

#define BCM_FW_NAME_LEN			64
#define BCM_FW_NAME_COUNT_MAX		4
/* For kmalloc-ing the fw-name array instead of putting it on the stack */
typedef char bcm_fw_name[BCM_FW_NAME_LEN];

int btbcm_check_bdaddr(struct hci_dev *hdev)
{
	struct hci_rp_read_bd_addr *bda;
	struct sk_buff *skb;

	skb = __hci_cmd_sync(hdev, HCI_OP_READ_BD_ADDR, 0, NULL,
			     HCI_INIT_TIMEOUT);
	if (IS_ERR(skb)) {
		int err = PTR_ERR(skb);

		bt_dev_err(hdev, "BCM: Reading device address failed (%d)", err);
		return err;
	}

	if (skb->len != sizeof(*bda)) {
		bt_dev_err(hdev, "BCM: Device address length mismatch");
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
	 * The address 20:70:02:A0:00:00 indicates a BCM20702A1 controller
	 * with no configured address.
	 *
	 * The address 20:76:A0:00:56:79 indicates a BCM2076B1 controller
	 * with no configured address.
	 *
	 * The address 43:24:B3:00:00:00 indicates a BCM4324B3 controller
	 * with waiting for configuration state.
	 *
	 * The address 43:30:B1:00:00:00 indicates a BCM4330B1 controller
	 * with waiting for configuration state.
	 *
	 * The address 43:43:A0:12:1F:AC indicates a BCM43430A0 controller
	 * with no configured address.
	 */
	if (!bacmp(&bda->bdaddr, BDADDR_BCM20702A0) ||
	    !bacmp(&bda->bdaddr, BDADDR_BCM20702A1) ||
	    !bacmp(&bda->bdaddr, BDADDR_BCM2076B1) ||
	    !bacmp(&bda->bdaddr, BDADDR_BCM4324B3) ||
	    !bacmp(&bda->bdaddr, BDADDR_BCM4330B1) ||
	    !bacmp(&bda->bdaddr, BDADDR_BCM4334B0) ||
	    !bacmp(&bda->bdaddr, BDADDR_BCM4345C5) ||
	    !bacmp(&bda->bdaddr, BDADDR_BCM43430A0) ||
	    !bacmp(&bda->bdaddr, BDADDR_BCM43341B)) {
		bt_dev_info(hdev, "BCM: Using default device address (%pMR)",
			    &bda->bdaddr);
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
		bt_dev_err(hdev, "BCM: Change address command failed (%d)", err);
		return err;
	}
	kfree_skb(skb);

	return 0;
}
EXPORT_SYMBOL_GPL(btbcm_set_bdaddr);

int btbcm_read_pcm_int_params(struct hci_dev *hdev,
			      struct bcm_set_pcm_int_params *params)
{
	struct sk_buff *skb;
	int err = 0;

	skb = __hci_cmd_sync(hdev, 0xfc1d, 0, NULL, HCI_INIT_TIMEOUT);
	if (IS_ERR(skb)) {
		err = PTR_ERR(skb);
		bt_dev_err(hdev, "BCM: Read PCM int params failed (%d)", err);
		return err;
	}

	if (skb->len != 6 || skb->data[0]) {
		bt_dev_err(hdev, "BCM: Read PCM int params length mismatch");
		kfree_skb(skb);
		return -EIO;
	}

	if (params)
		memcpy(params, skb->data + 1, 5);

	kfree_skb(skb);

	return 0;
}
EXPORT_SYMBOL_GPL(btbcm_read_pcm_int_params);

int btbcm_write_pcm_int_params(struct hci_dev *hdev,
			       const struct bcm_set_pcm_int_params *params)
{
	struct sk_buff *skb;
	int err;

	skb = __hci_cmd_sync(hdev, 0xfc1c, 5, params, HCI_INIT_TIMEOUT);
	if (IS_ERR(skb)) {
		err = PTR_ERR(skb);
		bt_dev_err(hdev, "BCM: Write PCM int params failed (%d)", err);
		return err;
	}
	kfree_skb(skb);

	return 0;
}
EXPORT_SYMBOL_GPL(btbcm_write_pcm_int_params);

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
		bt_dev_err(hdev, "BCM: Download Minidrv command failed (%d)",
			   err);
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
			bt_dev_err(hdev, "BCM: Patch is corrupted");
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
			bt_dev_err(hdev, "BCM: Patch command %04x failed (%d)",
				   opcode, err);
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

		bt_dev_err(hdev, "BCM: Reset failed (%d)", err);
		return err;
	}
	kfree_skb(skb);

	/* 100 msec delay for module to complete reset process */
	msleep(100);

	return 0;
}

static struct sk_buff *btbcm_read_local_name(struct hci_dev *hdev)
{
	struct sk_buff *skb;

	skb = __hci_cmd_sync(hdev, HCI_OP_READ_LOCAL_NAME, 0, NULL,
			     HCI_INIT_TIMEOUT);
	if (IS_ERR(skb)) {
		bt_dev_err(hdev, "BCM: Reading local name failed (%ld)",
			   PTR_ERR(skb));
		return skb;
	}

	if (skb->len != sizeof(struct hci_rp_read_local_name)) {
		bt_dev_err(hdev, "BCM: Local name length mismatch");
		kfree_skb(skb);
		return ERR_PTR(-EIO);
	}

	return skb;
}

static struct sk_buff *btbcm_read_local_version(struct hci_dev *hdev)
{
	struct sk_buff *skb;

	skb = __hci_cmd_sync(hdev, HCI_OP_READ_LOCAL_VERSION, 0, NULL,
			     HCI_INIT_TIMEOUT);
	if (IS_ERR(skb)) {
		bt_dev_err(hdev, "BCM: Reading local version info failed (%ld)",
			   PTR_ERR(skb));
		return skb;
	}

	if (skb->len != sizeof(struct hci_rp_read_local_version)) {
		bt_dev_err(hdev, "BCM: Local version length mismatch");
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
		bt_dev_err(hdev, "BCM: Read verbose config info failed (%ld)",
			   PTR_ERR(skb));
		return skb;
	}

	if (skb->len != 7) {
		bt_dev_err(hdev, "BCM: Verbose config length mismatch");
		kfree_skb(skb);
		return ERR_PTR(-EIO);
	}

	return skb;
}

static struct sk_buff *btbcm_read_controller_features(struct hci_dev *hdev)
{
	struct sk_buff *skb;

	skb = __hci_cmd_sync(hdev, 0xfc6e, 0, NULL, HCI_INIT_TIMEOUT);
	if (IS_ERR(skb)) {
		bt_dev_err(hdev, "BCM: Read controller features failed (%ld)",
			   PTR_ERR(skb));
		return skb;
	}

	if (skb->len != 9) {
		bt_dev_err(hdev, "BCM: Controller features length mismatch");
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
		bt_dev_err(hdev, "BCM: Read USB product info failed (%ld)",
			   PTR_ERR(skb));
		return skb;
	}

	if (skb->len != 5) {
		bt_dev_err(hdev, "BCM: USB product length mismatch");
		kfree_skb(skb);
		return ERR_PTR(-EIO);
	}

	return skb;
}

static const struct dmi_system_id disable_broken_read_transmit_power[] = {
	{
		 .matches = {
			DMI_MATCH(DMI_BOARD_VENDOR, "Apple Inc."),
			DMI_MATCH(DMI_PRODUCT_NAME, "MacBookPro16,1"),
		},
	},
	{
		 .matches = {
			DMI_MATCH(DMI_BOARD_VENDOR, "Apple Inc."),
			DMI_MATCH(DMI_PRODUCT_NAME, "MacBookPro16,2"),
		},
	},
	{
		 .matches = {
			DMI_MATCH(DMI_BOARD_VENDOR, "Apple Inc."),
			DMI_MATCH(DMI_PRODUCT_NAME, "MacBookPro16,4"),
		},
	},
	{
		 .matches = {
			DMI_MATCH(DMI_BOARD_VENDOR, "Apple Inc."),
			DMI_MATCH(DMI_PRODUCT_NAME, "MacBookAir8,1"),
		},
	},
	{
		 .matches = {
			DMI_MATCH(DMI_BOARD_VENDOR, "Apple Inc."),
			DMI_MATCH(DMI_PRODUCT_NAME, "MacBookAir8,2"),
		},
	},
	{
		 .matches = {
			DMI_MATCH(DMI_BOARD_VENDOR, "Apple Inc."),
			DMI_MATCH(DMI_PRODUCT_NAME, "iMac20,1"),
		},
	},
	{
		 .matches = {
			DMI_MATCH(DMI_BOARD_VENDOR, "Apple Inc."),
			DMI_MATCH(DMI_PRODUCT_NAME, "iMac20,2"),
		},
	},
	{ }
};

static int btbcm_read_info(struct hci_dev *hdev)
{
	struct sk_buff *skb;

	/* Read Verbose Config Version Info */
	skb = btbcm_read_verbose_config(hdev);
	if (IS_ERR(skb))
		return PTR_ERR(skb);

	bt_dev_info(hdev, "BCM: chip id %u", skb->data[1]);
	kfree_skb(skb);

	/* Read Controller Features */
	skb = btbcm_read_controller_features(hdev);
	if (IS_ERR(skb))
		return PTR_ERR(skb);

	bt_dev_info(hdev, "BCM: features 0x%2.2x", skb->data[1]);
	kfree_skb(skb);

	/* Read DMI and disable broken Read LE Min/Max Tx Power */
	if (dmi_first_match(disable_broken_read_transmit_power))
		set_bit(HCI_QUIRK_BROKEN_READ_TRANSMIT_POWER, &hdev->quirks);

	return 0;
}

static int btbcm_print_local_name(struct hci_dev *hdev)
{
	struct sk_buff *skb;

	/* Read Local Name */
	skb = btbcm_read_local_name(hdev);
	if (IS_ERR(skb))
		return PTR_ERR(skb);

	bt_dev_info(hdev, "%s", (char *)(skb->data + 1));
	kfree_skb(skb);

	return 0;
}

struct bcm_subver_table {
	u16 subver;
	const char *name;
};

static const struct bcm_subver_table bcm_uart_subver_table[] = {
	{ 0x1111, "BCM4362A2"	},	/* 000.017.017 */
	{ 0x4103, "BCM4330B1"	},	/* 002.001.003 */
	{ 0x410d, "BCM4334B0"	},	/* 002.001.013 */
	{ 0x410e, "BCM43341B0"	},	/* 002.001.014 */
	{ 0x4204, "BCM2076B1"	},	/* 002.002.004 */
	{ 0x4406, "BCM4324B3"	},	/* 002.004.006 */
	{ 0x4606, "BCM4324B5"	},	/* 002.006.006 */
	{ 0x6109, "BCM4335C0"	},	/* 003.001.009 */
	{ 0x610c, "BCM4354"	},	/* 003.001.012 */
	{ 0x2122, "BCM4343A0"	},	/* 001.001.034 */
	{ 0x2209, "BCM43430A1"  },	/* 001.002.009 */
	{ 0x6119, "BCM4345C0"	},	/* 003.001.025 */
	{ 0x6606, "BCM4345C5"	},	/* 003.006.006 */
	{ 0x230f, "BCM4356A2"	},	/* 001.003.015 */
	{ 0x220e, "BCM20702A1"  },	/* 001.002.014 */
	{ 0x4217, "BCM4329B1"   },	/* 002.002.023 */
	{ 0x6106, "BCM4359C0"	},	/* 003.001.006 */
	{ 0x4106, "BCM4335A0"	},	/* 002.001.006 */
	{ 0x410c, "BCM43430B0"	},	/* 002.001.012 */
	{ }
};

static const struct bcm_subver_table bcm_usb_subver_table[] = {
	{ 0x2105, "BCM20703A1"	},	/* 001.001.005 */
	{ 0x210b, "BCM43142A0"	},	/* 001.001.011 */
	{ 0x2112, "BCM4314A0"	},	/* 001.001.018 */
	{ 0x2118, "BCM20702A0"	},	/* 001.001.024 */
	{ 0x2126, "BCM4335A0"	},	/* 001.001.038 */
	{ 0x220e, "BCM20702A1"	},	/* 001.002.014 */
	{ 0x230f, "BCM4356A2"	},	/* 001.003.015 */
	{ 0x4106, "BCM4335B0"	},	/* 002.001.006 */
	{ 0x410e, "BCM20702B0"	},	/* 002.001.014 */
	{ 0x6109, "BCM4335C0"	},	/* 003.001.009 */
	{ 0x610c, "BCM4354"	},	/* 003.001.012 */
	{ 0x6607, "BCM4350C5"	},	/* 003.006.007 */
	{ }
};

/*
 * This currently only looks up the device tree board appendix,
 * but can be expanded to other mechanisms.
 */
static const char *btbcm_get_board_name(struct device *dev)
{
#ifdef CONFIG_OF
	struct device_node *root;
	char *board_type;
	const char *tmp;
	int len;
	int i;

	root = of_find_node_by_path("/");
	if (!root)
		return NULL;

	if (of_property_read_string_index(root, "compatible", 0, &tmp))
		return NULL;

	/* get rid of any '/' in the compatible string */
	len = strlen(tmp) + 1;
	board_type = devm_kzalloc(dev, len, GFP_KERNEL);
	strscpy(board_type, tmp, len);
	for (i = 0; i < board_type[i]; i++) {
		if (board_type[i] == '/')
			board_type[i] = '-';
	}
	of_node_put(root);

	return board_type;
#else
	return NULL;
#endif
}

int btbcm_initialize(struct hci_dev *hdev, bool *fw_load_done)
{
	u16 subver, rev, pid, vid;
	struct sk_buff *skb;
	struct hci_rp_read_local_version *ver;
	const struct bcm_subver_table *bcm_subver_table;
	const char *hw_name = NULL;
	const char *board_name;
	char postfix[16] = "";
	int fw_name_count = 0;
	bcm_fw_name *fw_name;
	const struct firmware *fw;
	int i, err;

	board_name = btbcm_get_board_name(&hdev->dev);

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

	/* Read controller information */
	if (!(*fw_load_done)) {
		err = btbcm_read_info(hdev);
		if (err)
			return err;
	}
	err = btbcm_print_local_name(hdev);
	if (err)
		return err;

	bcm_subver_table = (hdev->bus == HCI_USB) ? bcm_usb_subver_table :
						    bcm_uart_subver_table;

	for (i = 0; bcm_subver_table[i].name; i++) {
		if (subver == bcm_subver_table[i].subver) {
			hw_name = bcm_subver_table[i].name;
			break;
		}
	}

	bt_dev_info(hdev, "%s (%3.3u.%3.3u.%3.3u) build %4.4u",
		    hw_name ? hw_name : "BCM", (subver & 0xe000) >> 13,
		    (subver & 0x1f00) >> 8, (subver & 0x00ff), rev & 0x0fff);

	if (*fw_load_done)
		return 0;

	if (hdev->bus == HCI_USB) {
		/* Read USB Product Info */
		skb = btbcm_read_usb_product(hdev);
		if (IS_ERR(skb))
			return PTR_ERR(skb);

		vid = get_unaligned_le16(skb->data + 1);
		pid = get_unaligned_le16(skb->data + 3);
		kfree_skb(skb);

		snprintf(postfix, sizeof(postfix), "-%4.4x-%4.4x", vid, pid);
	}

	fw_name = kmalloc(BCM_FW_NAME_COUNT_MAX * BCM_FW_NAME_LEN, GFP_KERNEL);
	if (!fw_name)
		return -ENOMEM;

	if (hw_name) {
		if (board_name) {
			snprintf(fw_name[fw_name_count], BCM_FW_NAME_LEN,
				 "brcm/%s%s.%s.hcd", hw_name, postfix, board_name);
			fw_name_count++;
		}
		snprintf(fw_name[fw_name_count], BCM_FW_NAME_LEN,
			 "brcm/%s%s.hcd", hw_name, postfix);
		fw_name_count++;
	}

	if (board_name) {
		snprintf(fw_name[fw_name_count], BCM_FW_NAME_LEN,
			 "brcm/BCM%s.%s.hcd", postfix, board_name);
		fw_name_count++;
	}
	snprintf(fw_name[fw_name_count], BCM_FW_NAME_LEN,
		 "brcm/BCM%s.hcd", postfix);
	fw_name_count++;

	for (i = 0; i < fw_name_count; i++) {
		err = firmware_request_nowarn(&fw, fw_name[i], &hdev->dev);
		if (err == 0) {
			bt_dev_info(hdev, "%s '%s' Patch",
				    hw_name ? hw_name : "BCM", fw_name[i]);
			*fw_load_done = true;
			break;
		}
	}

	if (*fw_load_done) {
		err = btbcm_patchram(hdev, fw);
		if (err)
			bt_dev_info(hdev, "BCM: Patch failed (%d)", err);

		release_firmware(fw);
	} else {
		bt_dev_err(hdev, "BCM: firmware Patch file not found, tried:");
		for (i = 0; i < fw_name_count; i++)
			bt_dev_err(hdev, "BCM: '%s'", fw_name[i]);
	}

	kfree(fw_name);
	return 0;
}
EXPORT_SYMBOL_GPL(btbcm_initialize);

int btbcm_finalize(struct hci_dev *hdev, bool *fw_load_done)
{
	int err;

	/* Re-initialize if necessary */
	if (*fw_load_done) {
		err = btbcm_initialize(hdev, fw_load_done);
		if (err)
			return err;
	}

	btbcm_check_bdaddr(hdev);

	set_bit(HCI_QUIRK_STRICT_DUPLICATE_FILTER, &hdev->quirks);

	return 0;
}
EXPORT_SYMBOL_GPL(btbcm_finalize);

int btbcm_setup_patchram(struct hci_dev *hdev)
{
	bool fw_load_done = false;
	int err;

	/* Initialize */
	err = btbcm_initialize(hdev, &fw_load_done);
	if (err)
		return err;

	/* Re-initialize after loading Patch */
	return btbcm_finalize(hdev, &fw_load_done);
}
EXPORT_SYMBOL_GPL(btbcm_setup_patchram);

int btbcm_setup_apple(struct hci_dev *hdev)
{
	struct sk_buff *skb;
	int err;

	/* Reset */
	err = btbcm_reset(hdev);
	if (err)
		return err;

	/* Read Verbose Config Version Info */
	skb = btbcm_read_verbose_config(hdev);
	if (!IS_ERR(skb)) {
		bt_dev_info(hdev, "BCM: chip id %u build %4.4u",
			    skb->data[1], get_unaligned_le16(skb->data + 5));
		kfree_skb(skb);
	}

	/* Read USB Product Info */
	skb = btbcm_read_usb_product(hdev);
	if (!IS_ERR(skb)) {
		bt_dev_info(hdev, "BCM: product %4.4x:%4.4x",
			    get_unaligned_le16(skb->data + 1),
			    get_unaligned_le16(skb->data + 3));
		kfree_skb(skb);
	}

	/* Read Controller Features */
	skb = btbcm_read_controller_features(hdev);
	if (!IS_ERR(skb)) {
		bt_dev_info(hdev, "BCM: features 0x%2.2x", skb->data[1]);
		kfree_skb(skb);
	}

	/* Read Local Name */
	skb = btbcm_read_local_name(hdev);
	if (!IS_ERR(skb)) {
		bt_dev_info(hdev, "%s", (char *)(skb->data + 1));
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
