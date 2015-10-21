/*
 *
 *  Bluetooth support for Intel devices
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

#include <net/bluetooth/bluetooth.h>
#include <net/bluetooth/hci_core.h>

#include "btintel.h"

#define VERSION "0.1"

#define BDADDR_INTEL (&(bdaddr_t) {{0x00, 0x8b, 0x9e, 0x19, 0x03, 0x00}})

int btintel_check_bdaddr(struct hci_dev *hdev)
{
	struct hci_rp_read_bd_addr *bda;
	struct sk_buff *skb;

	skb = __hci_cmd_sync(hdev, HCI_OP_READ_BD_ADDR, 0, NULL,
			     HCI_INIT_TIMEOUT);
	if (IS_ERR(skb)) {
		int err = PTR_ERR(skb);
		BT_ERR("%s: Reading Intel device address failed (%d)",
		       hdev->name, err);
		return err;
	}

	if (skb->len != sizeof(*bda)) {
		BT_ERR("%s: Intel device address length mismatch", hdev->name);
		kfree_skb(skb);
		return -EIO;
	}

	bda = (struct hci_rp_read_bd_addr *)skb->data;

	/* For some Intel based controllers, the default Bluetooth device
	 * address 00:03:19:9E:8B:00 can be found. These controllers are
	 * fully operational, but have the danger of duplicate addresses
	 * and that in turn can cause problems with Bluetooth operation.
	 */
	if (!bacmp(&bda->bdaddr, BDADDR_INTEL)) {
		BT_ERR("%s: Found Intel default device address (%pMR)",
		       hdev->name, &bda->bdaddr);
		set_bit(HCI_QUIRK_INVALID_BDADDR, &hdev->quirks);
	}

	kfree_skb(skb);

	return 0;
}
EXPORT_SYMBOL_GPL(btintel_check_bdaddr);

int btintel_set_bdaddr(struct hci_dev *hdev, const bdaddr_t *bdaddr)
{
	struct sk_buff *skb;
	int err;

	skb = __hci_cmd_sync(hdev, 0xfc31, 6, bdaddr, HCI_INIT_TIMEOUT);
	if (IS_ERR(skb)) {
		err = PTR_ERR(skb);
		BT_ERR("%s: Changing Intel device address failed (%d)",
		       hdev->name, err);
		return err;
	}
	kfree_skb(skb);

	return 0;
}
EXPORT_SYMBOL_GPL(btintel_set_bdaddr);

void btintel_hw_error(struct hci_dev *hdev, u8 code)
{
	struct sk_buff *skb;
	u8 type = 0x00;

	BT_ERR("%s: Hardware error 0x%2.2x", hdev->name, code);

	skb = __hci_cmd_sync(hdev, HCI_OP_RESET, 0, NULL, HCI_INIT_TIMEOUT);
	if (IS_ERR(skb)) {
		BT_ERR("%s: Reset after hardware error failed (%ld)",
		       hdev->name, PTR_ERR(skb));
		return;
	}
	kfree_skb(skb);

	skb = __hci_cmd_sync(hdev, 0xfc22, 1, &type, HCI_INIT_TIMEOUT);
	if (IS_ERR(skb)) {
		BT_ERR("%s: Retrieving Intel exception info failed (%ld)",
		       hdev->name, PTR_ERR(skb));
		return;
	}

	if (skb->len != 13) {
		BT_ERR("%s: Exception info size mismatch", hdev->name);
		kfree_skb(skb);
		return;
	}

	BT_ERR("%s: Exception info %s", hdev->name, (char *)(skb->data + 1));

	kfree_skb(skb);
}
EXPORT_SYMBOL_GPL(btintel_hw_error);

void btintel_version_info(struct hci_dev *hdev, struct intel_version *ver)
{
	const char *variant;

	switch (ver->fw_variant) {
	case 0x06:
		variant = "Bootloader";
		break;
	case 0x23:
		variant = "Firmware";
		break;
	default:
		return;
	}

	BT_INFO("%s: %s revision %u.%u build %u week %u %u", hdev->name,
		variant, ver->fw_revision >> 4, ver->fw_revision & 0x0f,
		ver->fw_build_num, ver->fw_build_ww, 2000 + ver->fw_build_yy);
}
EXPORT_SYMBOL_GPL(btintel_version_info);

int btintel_secure_send(struct hci_dev *hdev, u8 fragment_type, u32 plen,
			const void *param)
{
	while (plen > 0) {
		struct sk_buff *skb;
		u8 cmd_param[253], fragment_len = (plen > 252) ? 252 : plen;

		cmd_param[0] = fragment_type;
		memcpy(cmd_param + 1, param, fragment_len);

		skb = __hci_cmd_sync(hdev, 0xfc09, fragment_len + 1,
				     cmd_param, HCI_INIT_TIMEOUT);
		if (IS_ERR(skb))
			return PTR_ERR(skb);

		kfree_skb(skb);

		plen -= fragment_len;
		param += fragment_len;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(btintel_secure_send);

MODULE_AUTHOR("Marcel Holtmann <marcel@holtmann.org>");
MODULE_DESCRIPTION("Bluetooth support for Intel devices ver " VERSION);
MODULE_VERSION(VERSION);
MODULE_LICENSE("GPL");
MODULE_FIRMWARE("intel/ibt-11-5.sfi");
MODULE_FIRMWARE("intel/ibt-11-5.ddc");
