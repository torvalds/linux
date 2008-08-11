/*
 *
 *  Generic Bluetooth USB driver
 *
 *  Copyright (C) 2005-2007  Marcel Holtmann <marcel@holtmann.org>
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

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/sched.h>
#include <linux/errno.h>
#include <linux/skbuff.h>

#include <linux/usb.h>

#include <net/bluetooth/bluetooth.h>
#include <net/bluetooth/hci_core.h>

//#define CONFIG_BT_HCIBTUSB_DEBUG
#ifndef CONFIG_BT_HCIBTUSB_DEBUG
#undef  BT_DBG
#define BT_DBG(D...)
#endif

#define VERSION "0.2"

static int ignore_dga;
static int ignore_csr;
static int ignore_sniffer;
static int disable_scofix;
static int force_scofix;
static int reset;

static struct usb_driver btusb_driver;

#define BTUSB_IGNORE		0x01
#define BTUSB_RESET		0x02
#define BTUSB_DIGIANSWER	0x04
#define BTUSB_CSR		0x08
#define BTUSB_SNIFFER		0x10
#define BTUSB_BCM92035		0x20
#define BTUSB_BROKEN_ISOC	0x40
#define BTUSB_WRONG_SCO_MTU	0x80

static struct usb_device_id btusb_table[] = {
	/* Generic Bluetooth USB device */
	{ USB_DEVICE_INFO(0xe0, 0x01, 0x01) },

	/* AVM BlueFRITZ! USB v2.0 */
	{ USB_DEVICE(0x057c, 0x3800) },

	/* Bluetooth Ultraport Module from IBM */
	{ USB_DEVICE(0x04bf, 0x030a) },

	/* ALPS Modules with non-standard id */
	{ USB_DEVICE(0x044e, 0x3001) },
	{ USB_DEVICE(0x044e, 0x3002) },

	/* Ericsson with non-standard id */
	{ USB_DEVICE(0x0bdb, 0x1002) },

	/* Canyon CN-BTU1 with HID interfaces */
	{ USB_DEVICE(0x0c10, 0x0000), .driver_info = BTUSB_RESET },

	{ }	/* Terminating entry */
};

MODULE_DEVICE_TABLE(usb, btusb_table);

static struct usb_device_id blacklist_table[] = {
	/* CSR BlueCore devices */
	{ USB_DEVICE(0x0a12, 0x0001), .driver_info = BTUSB_CSR },

	/* Broadcom BCM2033 without firmware */
	{ USB_DEVICE(0x0a5c, 0x2033), .driver_info = BTUSB_IGNORE },

	/* Broadcom BCM2035 */
	{ USB_DEVICE(0x0a5c, 0x2035), .driver_info = BTUSB_RESET | BTUSB_WRONG_SCO_MTU },
	{ USB_DEVICE(0x0a5c, 0x200a), .driver_info = BTUSB_RESET | BTUSB_WRONG_SCO_MTU },

	/* Broadcom BCM2045 */
	{ USB_DEVICE(0x0a5c, 0x2039), .driver_info = BTUSB_RESET | BTUSB_WRONG_SCO_MTU },
	{ USB_DEVICE(0x0a5c, 0x2101), .driver_info = BTUSB_RESET | BTUSB_WRONG_SCO_MTU },

	/* Broadcom BCM2046 */
	{ USB_DEVICE(0x0a5c, 0x2151), .driver_info = BTUSB_RESET },

	/* IBM/Lenovo ThinkPad with Broadcom chip */
	{ USB_DEVICE(0x0a5c, 0x201e), .driver_info = BTUSB_RESET | BTUSB_WRONG_SCO_MTU },
	{ USB_DEVICE(0x0a5c, 0x2110), .driver_info = BTUSB_RESET | BTUSB_WRONG_SCO_MTU },

	/* Targus ACB10US */
	{ USB_DEVICE(0x0a5c, 0x2100), .driver_info = BTUSB_RESET },

	/* ANYCOM Bluetooth USB-200 and USB-250 */
	{ USB_DEVICE(0x0a5c, 0x2111), .driver_info = BTUSB_RESET },

	/* HP laptop with Broadcom chip */
	{ USB_DEVICE(0x03f0, 0x171d), .driver_info = BTUSB_RESET | BTUSB_WRONG_SCO_MTU },

	/* Dell laptop with Broadcom chip */
	{ USB_DEVICE(0x413c, 0x8126), .driver_info = BTUSB_RESET | BTUSB_WRONG_SCO_MTU },

	/* Dell Wireless 370 */
	{ USB_DEVICE(0x413c, 0x8156), .driver_info = BTUSB_RESET | BTUSB_WRONG_SCO_MTU },

	/* Dell Wireless 410 */
	{ USB_DEVICE(0x413c, 0x8152), .driver_info = BTUSB_RESET | BTUSB_WRONG_SCO_MTU },

	/* Microsoft Wireless Transceiver for Bluetooth 2.0 */
	{ USB_DEVICE(0x045e, 0x009c), .driver_info = BTUSB_RESET },

	/* Kensington Bluetooth USB adapter */
	{ USB_DEVICE(0x047d, 0x105d), .driver_info = BTUSB_RESET },
	{ USB_DEVICE(0x047d, 0x105e), .driver_info = BTUSB_RESET | BTUSB_WRONG_SCO_MTU },

	/* ISSC Bluetooth Adapter v3.1 */
	{ USB_DEVICE(0x1131, 0x1001), .driver_info = BTUSB_RESET },

	/* RTX Telecom based adapters with buggy SCO support */
	{ USB_DEVICE(0x0400, 0x0807), .driver_info = BTUSB_BROKEN_ISOC },
	{ USB_DEVICE(0x0400, 0x080a), .driver_info = BTUSB_BROKEN_ISOC },

	/* CONWISE Technology based adapters with buggy SCO support */
	{ USB_DEVICE(0x0e5e, 0x6622), .driver_info = BTUSB_BROKEN_ISOC },

	/* Belkin F8T012 and F8T013 devices */
	{ USB_DEVICE(0x050d, 0x0012), .driver_info = BTUSB_RESET | BTUSB_WRONG_SCO_MTU },
	{ USB_DEVICE(0x050d, 0x0013), .driver_info = BTUSB_RESET | BTUSB_WRONG_SCO_MTU },

	/* Digianswer devices */
	{ USB_DEVICE(0x08fd, 0x0001), .driver_info = BTUSB_DIGIANSWER },
	{ USB_DEVICE(0x08fd, 0x0002), .driver_info = BTUSB_IGNORE },

	/* CSR BlueCore Bluetooth Sniffer */
	{ USB_DEVICE(0x0a12, 0x0002), .driver_info = BTUSB_SNIFFER },

	/* Frontline ComProbe Bluetooth Sniffer */
	{ USB_DEVICE(0x16d3, 0x0002), .driver_info = BTUSB_SNIFFER },

	{ }	/* Terminating entry */
};

#define BTUSB_INTR_RUNNING	0
#define BTUSB_BULK_RUNNING	1

struct btusb_data {
	struct hci_dev       *hdev;
	struct usb_device    *udev;

	spinlock_t lock;

	unsigned long flags;

	struct work_struct work;

	struct usb_anchor tx_anchor;
	struct usb_anchor intr_anchor;
	struct usb_anchor bulk_anchor;

	struct usb_endpoint_descriptor *intr_ep;
	struct usb_endpoint_descriptor *bulk_tx_ep;
	struct usb_endpoint_descriptor *bulk_rx_ep;
};

static void btusb_intr_complete(struct urb *urb)
{
	struct hci_dev *hdev = urb->context;
	struct btusb_data *data = hdev->driver_data;
	int err;

	BT_DBG("%s urb %p status %d count %d", hdev->name,
					urb, urb->status, urb->actual_length);

	if (!test_bit(HCI_RUNNING, &hdev->flags))
		return;

	if (urb->status == 0) {
		if (hci_recv_fragment(hdev, HCI_EVENT_PKT,
						urb->transfer_buffer,
						urb->actual_length) < 0) {
			BT_ERR("%s corrupted event packet", hdev->name);
			hdev->stat.err_rx++;
		}
	}

	if (!test_bit(BTUSB_INTR_RUNNING, &data->flags))
		return;

	usb_anchor_urb(urb, &data->intr_anchor);

	err = usb_submit_urb(urb, GFP_ATOMIC);
	if (err < 0) {
		BT_ERR("%s urb %p failed to resubmit (%d)",
						hdev->name, urb, -err);
		usb_unanchor_urb(urb);
	}
}

static inline int btusb_submit_intr_urb(struct hci_dev *hdev)
{
	struct btusb_data *data = hdev->driver_data;
	struct urb *urb;
	unsigned char *buf;
	unsigned int pipe;
	int err, size;

	BT_DBG("%s", hdev->name);

	urb = usb_alloc_urb(0, GFP_ATOMIC);
	if (!urb)
		return -ENOMEM;

	size = le16_to_cpu(data->intr_ep->wMaxPacketSize);

	buf = kmalloc(size, GFP_ATOMIC);
	if (!buf) {
		usb_free_urb(urb);
		return -ENOMEM;
	}

	pipe = usb_rcvintpipe(data->udev, data->intr_ep->bEndpointAddress);

	usb_fill_int_urb(urb, data->udev, pipe, buf, size,
						btusb_intr_complete, hdev,
						data->intr_ep->bInterval);

	urb->transfer_flags |= URB_FREE_BUFFER;

	usb_anchor_urb(urb, &data->intr_anchor);

	err = usb_submit_urb(urb, GFP_ATOMIC);
	if (err < 0) {
		BT_ERR("%s urb %p submission failed (%d)",
						hdev->name, urb, -err);
		usb_unanchor_urb(urb);
		kfree(buf);
	}

	usb_free_urb(urb);

	return err;
}

static void btusb_bulk_complete(struct urb *urb)
{
	struct hci_dev *hdev = urb->context;
	struct btusb_data *data = hdev->driver_data;
	int err;

	BT_DBG("%s urb %p status %d count %d", hdev->name,
					urb, urb->status, urb->actual_length);

	if (!test_bit(HCI_RUNNING, &hdev->flags))
		return;

	if (urb->status == 0) {
		if (hci_recv_fragment(hdev, HCI_ACLDATA_PKT,
						urb->transfer_buffer,
						urb->actual_length) < 0) {
			BT_ERR("%s corrupted ACL packet", hdev->name);
			hdev->stat.err_rx++;
		}
	}

	if (!test_bit(BTUSB_BULK_RUNNING, &data->flags))
		return;

	usb_anchor_urb(urb, &data->bulk_anchor);

	err = usb_submit_urb(urb, GFP_ATOMIC);
	if (err < 0) {
		BT_ERR("%s urb %p failed to resubmit (%d)",
						hdev->name, urb, -err);
		usb_unanchor_urb(urb);
	}
}

static inline int btusb_submit_bulk_urb(struct hci_dev *hdev)
{
	struct btusb_data *data = hdev->driver_data;
	struct urb *urb;
	unsigned char *buf;
	unsigned int pipe;
	int err, size;

	BT_DBG("%s", hdev->name);

	urb = usb_alloc_urb(0, GFP_KERNEL);
	if (!urb)
		return -ENOMEM;

	size = le16_to_cpu(data->bulk_rx_ep->wMaxPacketSize);

	buf = kmalloc(size, GFP_KERNEL);
	if (!buf) {
		usb_free_urb(urb);
		return -ENOMEM;
	}

	pipe = usb_rcvbulkpipe(data->udev, data->bulk_rx_ep->bEndpointAddress);

	usb_fill_bulk_urb(urb, data->udev, pipe,
					buf, size, btusb_bulk_complete, hdev);

	urb->transfer_flags |= URB_FREE_BUFFER;

	usb_anchor_urb(urb, &data->bulk_anchor);

	err = usb_submit_urb(urb, GFP_KERNEL);
	if (err < 0) {
		BT_ERR("%s urb %p submission failed (%d)",
						hdev->name, urb, -err);
		usb_unanchor_urb(urb);
		kfree(buf);
	}

	usb_free_urb(urb);

	return err;
}

static void btusb_tx_complete(struct urb *urb)
{
	struct sk_buff *skb = urb->context;
	struct hci_dev *hdev = (struct hci_dev *) skb->dev;

	BT_DBG("%s urb %p status %d count %d", hdev->name,
					urb, urb->status, urb->actual_length);

	if (!test_bit(HCI_RUNNING, &hdev->flags))
		goto done;

	if (!urb->status)
		hdev->stat.byte_tx += urb->transfer_buffer_length;
	else
		hdev->stat.err_tx++;

done:
	kfree(urb->setup_packet);

	kfree_skb(skb);
}

static int btusb_open(struct hci_dev *hdev)
{
	struct btusb_data *data = hdev->driver_data;
	int err;

	BT_DBG("%s", hdev->name);

	if (test_and_set_bit(HCI_RUNNING, &hdev->flags))
		return 0;

	if (test_and_set_bit(BTUSB_INTR_RUNNING, &data->flags))
		return 0;

	err = btusb_submit_intr_urb(hdev);
	if (err < 0) {
		clear_bit(BTUSB_INTR_RUNNING, &hdev->flags);
		clear_bit(HCI_RUNNING, &hdev->flags);
	}

	return err;
}

static int btusb_close(struct hci_dev *hdev)
{
	struct btusb_data *data = hdev->driver_data;

	BT_DBG("%s", hdev->name);

	if (!test_and_clear_bit(HCI_RUNNING, &hdev->flags))
		return 0;

	clear_bit(BTUSB_BULK_RUNNING, &data->flags);
	usb_kill_anchored_urbs(&data->bulk_anchor);

	clear_bit(BTUSB_INTR_RUNNING, &data->flags);
	usb_kill_anchored_urbs(&data->intr_anchor);

	return 0;
}

static int btusb_flush(struct hci_dev *hdev)
{
	struct btusb_data *data = hdev->driver_data;

	BT_DBG("%s", hdev->name);

	usb_kill_anchored_urbs(&data->tx_anchor);

	return 0;
}

static int btusb_send_frame(struct sk_buff *skb)
{
	struct hci_dev *hdev = (struct hci_dev *) skb->dev;
	struct btusb_data *data = hdev->driver_data;
	struct usb_ctrlrequest *dr;
	struct urb *urb;
	unsigned int pipe;
	int err;

	BT_DBG("%s", hdev->name);

	if (!test_bit(HCI_RUNNING, &hdev->flags))
		return -EBUSY;

	switch (bt_cb(skb)->pkt_type) {
	case HCI_COMMAND_PKT:
		urb = usb_alloc_urb(0, GFP_ATOMIC);
		if (!urb)
			return -ENOMEM;

		dr = kmalloc(sizeof(*dr), GFP_ATOMIC);
		if (!dr) {
			usb_free_urb(urb);
			return -ENOMEM;
		}

		dr->bRequestType = USB_TYPE_CLASS;
		dr->bRequest     = 0;
		dr->wIndex       = 0;
		dr->wValue       = 0;
		dr->wLength      = __cpu_to_le16(skb->len);

		pipe = usb_sndctrlpipe(data->udev, 0x00);

		usb_fill_control_urb(urb, data->udev, pipe, (void *) dr,
				skb->data, skb->len, btusb_tx_complete, skb);

		hdev->stat.cmd_tx++;
		break;

	case HCI_ACLDATA_PKT:
		urb = usb_alloc_urb(0, GFP_ATOMIC);
		if (!urb)
			return -ENOMEM;

		pipe = usb_sndbulkpipe(data->udev,
					data->bulk_tx_ep->bEndpointAddress);

		usb_fill_bulk_urb(urb, data->udev, pipe,
				skb->data, skb->len, btusb_tx_complete, skb);

		hdev->stat.acl_tx++;
		break;

	case HCI_SCODATA_PKT:
		hdev->stat.sco_tx++;
		kfree_skb(skb);
		return 0;

	default:
		return -EILSEQ;
	}

	usb_anchor_urb(urb, &data->tx_anchor);

	err = usb_submit_urb(urb, GFP_ATOMIC);
	if (err < 0) {
		BT_ERR("%s urb %p submission failed", hdev->name, urb);
		kfree(urb->setup_packet);
		usb_unanchor_urb(urb);
	}

	usb_free_urb(urb);

	return err;
}

static void btusb_destruct(struct hci_dev *hdev)
{
	struct btusb_data *data = hdev->driver_data;

	BT_DBG("%s", hdev->name);

	kfree(data);
}

static void btusb_notify(struct hci_dev *hdev, unsigned int evt)
{
	struct btusb_data *data = hdev->driver_data;

	BT_DBG("%s evt %d", hdev->name, evt);

	if (evt == HCI_NOTIFY_CONN_ADD || evt == HCI_NOTIFY_CONN_DEL)
		schedule_work(&data->work);
}

static void btusb_work(struct work_struct *work)
{
	struct btusb_data *data = container_of(work, struct btusb_data, work);
	struct hci_dev *hdev = data->hdev;

	if (hdev->conn_hash.acl_num == 0) {
		clear_bit(BTUSB_BULK_RUNNING, &data->flags);
		usb_kill_anchored_urbs(&data->bulk_anchor);
		return;
	}

	if (!test_and_set_bit(BTUSB_BULK_RUNNING, &data->flags)) {
		if (btusb_submit_bulk_urb(hdev) < 0)
			clear_bit(BTUSB_BULK_RUNNING, &data->flags);
		else
			btusb_submit_bulk_urb(hdev);
	}
}

static int btusb_probe(struct usb_interface *intf,
				const struct usb_device_id *id)
{
	struct usb_endpoint_descriptor *ep_desc;
	struct btusb_data *data;
	struct hci_dev *hdev;
	int i, err;

	BT_DBG("intf %p id %p", intf, id);

	/* interface numbers are hardcoded in the spec */
	if (intf->cur_altsetting->desc.bInterfaceNumber != 0)
		return -ENODEV;

	if (!id->driver_info) {
		const struct usb_device_id *match;
		match = usb_match_id(intf, blacklist_table);
		if (match)
			id = match;
	}

	if (id->driver_info == BTUSB_IGNORE)
		return -ENODEV;

	if (ignore_dga && id->driver_info & BTUSB_DIGIANSWER)
		return -ENODEV;

	if (ignore_csr && id->driver_info & BTUSB_CSR)
		return -ENODEV;

	if (ignore_sniffer && id->driver_info & BTUSB_SNIFFER)
		return -ENODEV;

	data = kzalloc(sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	for (i = 0; i < intf->cur_altsetting->desc.bNumEndpoints; i++) {
		ep_desc = &intf->cur_altsetting->endpoint[i].desc;

		if (!data->intr_ep && usb_endpoint_is_int_in(ep_desc)) {
			data->intr_ep = ep_desc;
			continue;
		}

		if (!data->bulk_tx_ep && usb_endpoint_is_bulk_out(ep_desc)) {
			data->bulk_tx_ep = ep_desc;
			continue;
		}

		if (!data->bulk_rx_ep && usb_endpoint_is_bulk_in(ep_desc)) {
			data->bulk_rx_ep = ep_desc;
			continue;
		}
	}

	if (!data->intr_ep || !data->bulk_tx_ep || !data->bulk_rx_ep) {
		kfree(data);
		return -ENODEV;
	}

	data->udev = interface_to_usbdev(intf);

	spin_lock_init(&data->lock);

	INIT_WORK(&data->work, btusb_work);

	init_usb_anchor(&data->tx_anchor);
	init_usb_anchor(&data->intr_anchor);
	init_usb_anchor(&data->bulk_anchor);

	hdev = hci_alloc_dev();
	if (!hdev) {
		kfree(data);
		return -ENOMEM;
	}

	hdev->type = HCI_USB;
	hdev->driver_data = data;

	data->hdev = hdev;

	SET_HCIDEV_DEV(hdev, &intf->dev);

	hdev->open     = btusb_open;
	hdev->close    = btusb_close;
	hdev->flush    = btusb_flush;
	hdev->send     = btusb_send_frame;
	hdev->destruct = btusb_destruct;
	hdev->notify   = btusb_notify;

	hdev->owner = THIS_MODULE;

	if (reset || id->driver_info & BTUSB_RESET)
		set_bit(HCI_QUIRK_RESET_ON_INIT, &hdev->quirks);

	if (force_scofix || id->driver_info & BTUSB_WRONG_SCO_MTU) {
		if (!disable_scofix)
			set_bit(HCI_QUIRK_FIXUP_BUFFER_SIZE, &hdev->quirks);
	}

	if (id->driver_info & BTUSB_SNIFFER) {
		struct usb_device *udev = interface_to_usbdev(intf);

		if (le16_to_cpu(udev->descriptor.bcdDevice) > 0x997)
			set_bit(HCI_QUIRK_RAW_DEVICE, &hdev->quirks);
	}

	if (id->driver_info & BTUSB_BCM92035) {
		unsigned char cmd[] = { 0x3b, 0xfc, 0x01, 0x00 };
		struct sk_buff *skb;

		skb = bt_skb_alloc(sizeof(cmd), GFP_KERNEL);
		if (skb) {
			memcpy(skb_put(skb, sizeof(cmd)), cmd, sizeof(cmd));
			skb_queue_tail(&hdev->driver_init, skb);
		}
	}

	err = hci_register_dev(hdev);
	if (err < 0) {
		hci_free_dev(hdev);
		kfree(data);
		return err;
	}

	usb_set_intfdata(intf, data);

	return 0;
}

static void btusb_disconnect(struct usb_interface *intf)
{
	struct btusb_data *data = usb_get_intfdata(intf);
	struct hci_dev *hdev;

	BT_DBG("intf %p", intf);

	if (!data)
		return;

	hdev = data->hdev;

	usb_set_intfdata(intf, NULL);

	hci_unregister_dev(hdev);

	hci_free_dev(hdev);
}

static struct usb_driver btusb_driver = {
	.name		= "btusb",
	.probe		= btusb_probe,
	.disconnect	= btusb_disconnect,
	.id_table	= btusb_table,
};

static int __init btusb_init(void)
{
	BT_INFO("Generic Bluetooth USB driver ver %s", VERSION);

	return usb_register(&btusb_driver);
}

static void __exit btusb_exit(void)
{
	usb_deregister(&btusb_driver);
}

module_init(btusb_init);
module_exit(btusb_exit);

module_param(ignore_dga, bool, 0644);
MODULE_PARM_DESC(ignore_dga, "Ignore devices with id 08fd:0001");

module_param(ignore_csr, bool, 0644);
MODULE_PARM_DESC(ignore_csr, "Ignore devices with id 0a12:0001");

module_param(ignore_sniffer, bool, 0644);
MODULE_PARM_DESC(ignore_sniffer, "Ignore devices with id 0a12:0002");

module_param(disable_scofix, bool, 0644);
MODULE_PARM_DESC(disable_scofix, "Disable fixup of wrong SCO buffer size");

module_param(force_scofix, bool, 0644);
MODULE_PARM_DESC(force_scofix, "Force fixup of wrong SCO buffers size");

module_param(reset, bool, 0644);
MODULE_PARM_DESC(reset, "Send HCI reset command on initialization");

MODULE_AUTHOR("Marcel Holtmann <marcel@holtmann.org>");
MODULE_DESCRIPTION("Generic Bluetooth USB driver ver " VERSION);
MODULE_VERSION(VERSION);
MODULE_LICENSE("GPL");
