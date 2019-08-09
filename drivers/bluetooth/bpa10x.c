// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *
 *  Digianswer Bluetooth USB driver
 *
 *  Copyright (C) 2004-2007  Marcel Holtmann <marcel@holtmann.org>
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

#include "h4_recv.h"

#define VERSION "0.11"

static const struct usb_device_id bpa10x_table[] = {
	/* Tektronix BPA 100/105 (Digianswer) */
	{ USB_DEVICE(0x08fd, 0x0002) },

	{ }	/* Terminating entry */
};

MODULE_DEVICE_TABLE(usb, bpa10x_table);

struct bpa10x_data {
	struct hci_dev    *hdev;
	struct usb_device *udev;

	struct usb_anchor tx_anchor;
	struct usb_anchor rx_anchor;

	struct sk_buff *rx_skb[2];
};

static void bpa10x_tx_complete(struct urb *urb)
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

#define HCI_VENDOR_HDR_SIZE 5

#define HCI_RECV_VENDOR \
	.type = HCI_VENDOR_PKT, \
	.hlen = HCI_VENDOR_HDR_SIZE, \
	.loff = 3, \
	.lsize = 2, \
	.maxlen = HCI_MAX_FRAME_SIZE

static const struct h4_recv_pkt bpa10x_recv_pkts[] = {
	{ H4_RECV_ACL,     .recv = hci_recv_frame },
	{ H4_RECV_SCO,     .recv = hci_recv_frame },
	{ H4_RECV_EVENT,   .recv = hci_recv_frame },
	{ HCI_RECV_VENDOR, .recv = hci_recv_diag  },
};

static void bpa10x_rx_complete(struct urb *urb)
{
	struct hci_dev *hdev = urb->context;
	struct bpa10x_data *data = hci_get_drvdata(hdev);
	int err;

	BT_DBG("%s urb %p status %d count %d", hdev->name,
					urb, urb->status, urb->actual_length);

	if (!test_bit(HCI_RUNNING, &hdev->flags))
		return;

	if (urb->status == 0) {
		bool idx = usb_pipebulk(urb->pipe);

		data->rx_skb[idx] = h4_recv_buf(hdev, data->rx_skb[idx],
						urb->transfer_buffer,
						urb->actual_length,
						bpa10x_recv_pkts,
						ARRAY_SIZE(bpa10x_recv_pkts));
		if (IS_ERR(data->rx_skb[idx])) {
			bt_dev_err(hdev, "corrupted event packet");
			hdev->stat.err_rx++;
			data->rx_skb[idx] = NULL;
		}
	}

	usb_anchor_urb(urb, &data->rx_anchor);

	err = usb_submit_urb(urb, GFP_ATOMIC);
	if (err < 0) {
		bt_dev_err(hdev, "urb %p failed to resubmit (%d)", urb, -err);
		usb_unanchor_urb(urb);
	}
}

static inline int bpa10x_submit_intr_urb(struct hci_dev *hdev)
{
	struct bpa10x_data *data = hci_get_drvdata(hdev);
	struct urb *urb;
	unsigned char *buf;
	unsigned int pipe;
	int err, size = 16;

	BT_DBG("%s", hdev->name);

	urb = usb_alloc_urb(0, GFP_KERNEL);
	if (!urb)
		return -ENOMEM;

	buf = kmalloc(size, GFP_KERNEL);
	if (!buf) {
		usb_free_urb(urb);
		return -ENOMEM;
	}

	pipe = usb_rcvintpipe(data->udev, 0x81);

	usb_fill_int_urb(urb, data->udev, pipe, buf, size,
						bpa10x_rx_complete, hdev, 1);

	urb->transfer_flags |= URB_FREE_BUFFER;

	usb_anchor_urb(urb, &data->rx_anchor);

	err = usb_submit_urb(urb, GFP_KERNEL);
	if (err < 0) {
		bt_dev_err(hdev, "urb %p submission failed (%d)", urb, -err);
		usb_unanchor_urb(urb);
	}

	usb_free_urb(urb);

	return err;
}

static inline int bpa10x_submit_bulk_urb(struct hci_dev *hdev)
{
	struct bpa10x_data *data = hci_get_drvdata(hdev);
	struct urb *urb;
	unsigned char *buf;
	unsigned int pipe;
	int err, size = 64;

	BT_DBG("%s", hdev->name);

	urb = usb_alloc_urb(0, GFP_KERNEL);
	if (!urb)
		return -ENOMEM;

	buf = kmalloc(size, GFP_KERNEL);
	if (!buf) {
		usb_free_urb(urb);
		return -ENOMEM;
	}

	pipe = usb_rcvbulkpipe(data->udev, 0x82);

	usb_fill_bulk_urb(urb, data->udev, pipe,
					buf, size, bpa10x_rx_complete, hdev);

	urb->transfer_flags |= URB_FREE_BUFFER;

	usb_anchor_urb(urb, &data->rx_anchor);

	err = usb_submit_urb(urb, GFP_KERNEL);
	if (err < 0) {
		bt_dev_err(hdev, "urb %p submission failed (%d)", urb, -err);
		usb_unanchor_urb(urb);
	}

	usb_free_urb(urb);

	return err;
}

static int bpa10x_open(struct hci_dev *hdev)
{
	struct bpa10x_data *data = hci_get_drvdata(hdev);
	int err;

	BT_DBG("%s", hdev->name);

	err = bpa10x_submit_intr_urb(hdev);
	if (err < 0)
		goto error;

	err = bpa10x_submit_bulk_urb(hdev);
	if (err < 0)
		goto error;

	return 0;

error:
	usb_kill_anchored_urbs(&data->rx_anchor);

	return err;
}

static int bpa10x_close(struct hci_dev *hdev)
{
	struct bpa10x_data *data = hci_get_drvdata(hdev);

	BT_DBG("%s", hdev->name);

	usb_kill_anchored_urbs(&data->rx_anchor);

	return 0;
}

static int bpa10x_flush(struct hci_dev *hdev)
{
	struct bpa10x_data *data = hci_get_drvdata(hdev);

	BT_DBG("%s", hdev->name);

	usb_kill_anchored_urbs(&data->tx_anchor);

	return 0;
}

static int bpa10x_setup(struct hci_dev *hdev)
{
	static const u8 req[] = { 0x07 };
	struct sk_buff *skb;

	BT_DBG("%s", hdev->name);

	/* Read revision string */
	skb = __hci_cmd_sync(hdev, 0xfc0e, sizeof(req), req, HCI_INIT_TIMEOUT);
	if (IS_ERR(skb))
		return PTR_ERR(skb);

	bt_dev_info(hdev, "%s", (char *)(skb->data + 1));

	hci_set_fw_info(hdev, "%s", skb->data + 1);

	kfree_skb(skb);
	return 0;
}

static int bpa10x_send_frame(struct hci_dev *hdev, struct sk_buff *skb)
{
	struct bpa10x_data *data = hci_get_drvdata(hdev);
	struct usb_ctrlrequest *dr;
	struct urb *urb;
	unsigned int pipe;
	int err;

	BT_DBG("%s", hdev->name);

	skb->dev = (void *) hdev;

	urb = usb_alloc_urb(0, GFP_KERNEL);
	if (!urb)
		return -ENOMEM;

	/* Prepend skb with frame type */
	*(u8 *)skb_push(skb, 1) = hci_skb_pkt_type(skb);

	switch (hci_skb_pkt_type(skb)) {
	case HCI_COMMAND_PKT:
		dr = kmalloc(sizeof(*dr), GFP_KERNEL);
		if (!dr) {
			usb_free_urb(urb);
			return -ENOMEM;
		}

		dr->bRequestType = USB_TYPE_VENDOR;
		dr->bRequest     = 0;
		dr->wIndex       = 0;
		dr->wValue       = 0;
		dr->wLength      = __cpu_to_le16(skb->len);

		pipe = usb_sndctrlpipe(data->udev, 0x00);

		usb_fill_control_urb(urb, data->udev, pipe, (void *) dr,
				skb->data, skb->len, bpa10x_tx_complete, skb);

		hdev->stat.cmd_tx++;
		break;

	case HCI_ACLDATA_PKT:
		pipe = usb_sndbulkpipe(data->udev, 0x02);

		usb_fill_bulk_urb(urb, data->udev, pipe,
				skb->data, skb->len, bpa10x_tx_complete, skb);

		hdev->stat.acl_tx++;
		break;

	case HCI_SCODATA_PKT:
		pipe = usb_sndbulkpipe(data->udev, 0x02);

		usb_fill_bulk_urb(urb, data->udev, pipe,
				skb->data, skb->len, bpa10x_tx_complete, skb);

		hdev->stat.sco_tx++;
		break;

	default:
		usb_free_urb(urb);
		return -EILSEQ;
	}

	usb_anchor_urb(urb, &data->tx_anchor);

	err = usb_submit_urb(urb, GFP_KERNEL);
	if (err < 0) {
		bt_dev_err(hdev, "urb %p submission failed", urb);
		kfree(urb->setup_packet);
		usb_unanchor_urb(urb);
	}

	usb_free_urb(urb);

	return 0;
}

static int bpa10x_set_diag(struct hci_dev *hdev, bool enable)
{
	const u8 req[] = { 0x00, enable };
	struct sk_buff *skb;

	BT_DBG("%s", hdev->name);

	if (!test_bit(HCI_RUNNING, &hdev->flags))
		return -ENETDOWN;

	/* Enable sniffer operation */
	skb = __hci_cmd_sync(hdev, 0xfc0e, sizeof(req), req, HCI_INIT_TIMEOUT);
	if (IS_ERR(skb))
		return PTR_ERR(skb);

	kfree_skb(skb);
	return 0;
}

static int bpa10x_probe(struct usb_interface *intf,
			const struct usb_device_id *id)
{
	struct bpa10x_data *data;
	struct hci_dev *hdev;
	int err;

	BT_DBG("intf %p id %p", intf, id);

	if (intf->cur_altsetting->desc.bInterfaceNumber != 0)
		return -ENODEV;

	data = devm_kzalloc(&intf->dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->udev = interface_to_usbdev(intf);

	init_usb_anchor(&data->tx_anchor);
	init_usb_anchor(&data->rx_anchor);

	hdev = hci_alloc_dev();
	if (!hdev)
		return -ENOMEM;

	hdev->bus = HCI_USB;
	hci_set_drvdata(hdev, data);

	data->hdev = hdev;

	SET_HCIDEV_DEV(hdev, &intf->dev);

	hdev->open     = bpa10x_open;
	hdev->close    = bpa10x_close;
	hdev->flush    = bpa10x_flush;
	hdev->setup    = bpa10x_setup;
	hdev->send     = bpa10x_send_frame;
	hdev->set_diag = bpa10x_set_diag;

	set_bit(HCI_QUIRK_RESET_ON_CLOSE, &hdev->quirks);

	err = hci_register_dev(hdev);
	if (err < 0) {
		hci_free_dev(hdev);
		return err;
	}

	usb_set_intfdata(intf, data);

	return 0;
}

static void bpa10x_disconnect(struct usb_interface *intf)
{
	struct bpa10x_data *data = usb_get_intfdata(intf);

	BT_DBG("intf %p", intf);

	if (!data)
		return;

	usb_set_intfdata(intf, NULL);

	hci_unregister_dev(data->hdev);

	hci_free_dev(data->hdev);
	kfree_skb(data->rx_skb[0]);
	kfree_skb(data->rx_skb[1]);
}

static struct usb_driver bpa10x_driver = {
	.name		= "bpa10x",
	.probe		= bpa10x_probe,
	.disconnect	= bpa10x_disconnect,
	.id_table	= bpa10x_table,
	.disable_hub_initiated_lpm = 1,
};

module_usb_driver(bpa10x_driver);

MODULE_AUTHOR("Marcel Holtmann <marcel@holtmann.org>");
MODULE_DESCRIPTION("Digianswer Bluetooth USB driver ver " VERSION);
MODULE_VERSION(VERSION);
MODULE_LICENSE("GPL");
