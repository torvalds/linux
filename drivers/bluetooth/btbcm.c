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

#include <net/bluetooth/bluetooth.h>
#include <net/bluetooth/hci_core.h>

#include "btbcm.h"

#define VERSION "0.1"

#define BDADDR_BCM20702A0 (&(bdaddr_t) {{0x00, 0xa0, 0x02, 0x70, 0x20, 0x00}})

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
	if (bda->status) {
		BT_ERR("%s: BCM: Device address result failed (%02x)",
		       hdev->name, bda->status);
		kfree_skb(skb);
		return -bt_to_errno(bda->status);
	}

	/* The address 00:20:70:02:A0:00 indicates a BCM20702A0 controller
	 * with no configured address.
	 */
	if (!bacmp(&bda->bdaddr, BDADDR_BCM20702A0)) {
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

MODULE_AUTHOR("Marcel Holtmann <marcel@holtmann.org>");
MODULE_DESCRIPTION("Bluetooth support for Broadcom devices ver " VERSION);
MODULE_VERSION(VERSION);
MODULE_LICENSE("GPL");
