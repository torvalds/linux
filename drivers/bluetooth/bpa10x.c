/*
 *
 *  Digianswer Bluetooth USB driver
 *
 *  Copyright (C) 2004-2007  Marcel Holtmann <marcel@holtmann.org>
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

#define VERSION "0.10"

static struct usb_device_id bpa10x_table[] = {
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

#define HCI_VENDOR_HDR_SIZE 5

struct hci_vendor_hdr {
	__u8    type;
	__le16  snum;
	__le16  dlen;
} __packed;

static int bpa10x_recv(struct hci_dev *hdev, int queue, void *buf, int count)
{
	struct bpa10x_data *data = hdev->driver_data;

	BT_DBG("%s queue %d buffer %p count %d", hdev->name,
							queue, buf, count);

	if (queue < 0 || queue > 1)
		return -EILSEQ;

	hdev->stat.byte_rx += count;

	while (count) {
		struct sk_buff *skb = data->rx_skb[queue];
		struct { __u8 type; int expect; } *scb;
		int type, len = 0;

		if (!skb) {
			/* Start of the frame */

			type = *((__u8 *) buf);
			count--; buf++;

			switch (type) {
			case HCI_EVENT_PKT:
				if (count >= HCI_EVENT_HDR_SIZE) {
					struct hci_event_hdr *h = buf;
					len = HCI_EVENT_HDR_SIZE + h->plen;
				} else
					return -EILSEQ;
				break;

			case HCI_ACLDATA_PKT:
				if (count >= HCI_ACL_HDR_SIZE) {
					struct hci_acl_hdr *h = buf;
					len = HCI_ACL_HDR_SIZE +
							__le16_to_cpu(h->dlen);
				} else
					return -EILSEQ;
				break;

			case HCI_SCODATA_PKT:
				if (count >= HCI_SCO_HDR_SIZE) {
					struct hci_sco_hdr *h = buf;
					len = HCI_SCO_HDR_SIZE + h->dlen;
				} else
					return -EILSEQ;
				break;

			case HCI_VENDOR_PKT:
				if (count >= HCI_VENDOR_HDR_SIZE) {
					struct hci_vendor_hdr *h = buf;
					len = HCI_VENDOR_HDR_SIZE +
							__le16_to_cpu(h->dlen);
				} else
					return -EILSEQ;
				break;
			}

			skb = bt_skb_alloc(len, GFP_ATOMIC);
			if (!skb) {
				BT_ERR("%s no memory for packet", hdev->name);
				return -ENOMEM;
			}

			skb->dev = (void *) hdev;

			data->rx_skb[queue] = skb;

			scb = (void *) skb->cb;
			scb->type = type;
			scb->expect = len;
		} else {
			/* Continuation */

			scb = (void *) skb->cb;
			len = scb->expect;
		}

		len = min(len, count);

		memcpy(skb_put(skb, len), buf, len);

		scb->expect -= len;

		if (scb->expect == 0) {
			/* Complete frame */

			data->rx_skb[queue] = NULL;

			bt_cb(skb)->pkt_type = scb->type;
			hci_recv_frame(skb);
		}

		count -= len; buf += len;
	}

	return 0;
}

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

static void bpa10x_rx_complete(struct urb *urb)
{
	struct hci_dev *hdev = urb->context;
	struct bpa10x_data *data = hdev->driver_data;
	int err;

	BT_DBG("%s urb %p status %d count %d", hdev->name,
					urb, urb->status, urb->actual_length);

	if (!test_bit(HCI_RUNNING, &hdev->flags))
		return;

	if (urb->status == 0) {
		if (bpa10x_recv(hdev, usb_pipebulk(urb->pipe),
						urb->transfer_buffer,
						urb->actual_length) < 0) {
			BT_ERR("%s corrupted event packet", hdev->name);
			hdev->stat.err_rx++;
		}
	}

	usb_anchor_urb(urb, &data->rx_anchor);

	err = usb_submit_urb(urb, GFP_ATOMIC);
	if (err < 0) {
		BT_ERR("%s urb %p failed to resubmit (%d)",
						hdev->name, urb, -err);
		usb_unanchor_urb(urb);
	}
}

static inline int bpa10x_submit_intr_urb(struct hci_dev *hdev)
{
	struct bpa10x_data *data = hdev->driver_data;
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
		BT_ERR("%s urb %p submission failed (%d)",
						hdev->name, urb, -err);
		usb_unanchor_urb(urb);
	}

	usb_free_urb(urb);

	return err;
}

static inline int bpa10x_submit_bulk_urb(struct hci_dev *hdev)
{
	struct bpa10x_data *data = hdev->driver_data;
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
		BT_ERR("%s urb %p submission failed (%d)",
						hdev->name, urb, -err);
		usb_unanchor_urb(urb);
	}

	usb_free_urb(urb);

	return err;
}

static int bpa10x_open(struct hci_dev *hdev)
{
	struct bpa10x_data *data = hdev->driver_data;
	int err;

	BT_DBG("%s", hdev->name);

	if (test_and_set_bit(HCI_RUNNING, &hdev->flags))
		return 0;

	err = bpa10x_submit_intr_urb(hdev);
	if (err < 0)
		goto error;

	err = bpa10x_submit_bulk_urb(hdev);
	if (err < 0)
		goto error;

	return 0;

error:
	usb_kill_anchored_urbs(&data->rx_anchor);

	clear_bit(HCI_RUNNING, &hdev->flags);

	return err;
}

static int bpa10x_close(struct hci_dev *hdev)
{
	struct bpa10x_data *data = hdev->driver_data;

	BT_DBG("%s", hdev->name);

	if (!test_and_clear_bit(HCI_RUNNING, &hdev->flags))
		return 0;

	usb_kill_anchored_urbs(&data->rx_anchor);

	return 0;
}

static int bpa10x_flush(struct hci_dev *hdev)
{
	struct bpa10x_data *data = hdev->driver_data;

	BT_DBG("%s", hdev->name);

	usb_kill_anchored_urbs(&data->tx_anchor);

	return 0;
}

static int bpa10x_send_frame(struct sk_buff *skb)
{
	struct hci_dev *hdev = (struct hci_dev *) skb->dev;
	struct bpa10x_data *data = hdev->driver_data;
	struct usb_ctrlrequest *dr;
	struct urb *urb;
	unsigned int pipe;
	int err;

	BT_DBG("%s", hdev->name);

	if (!test_bit(HCI_RUNNING, &hdev->flags))
		return -EBUSY;

	urb = usb_alloc_urb(0, GFP_ATOMIC);
	if (!urb)
		return -ENOMEM;

	/* Prepend skb with frame type */
	*skb_push(skb, 1) = bt_cb(skb)->pkt_type;

	switch (bt_cb(skb)->pkt_type) {
	case HCI_COMMAND_PKT:
		dr = kmalloc(sizeof(*dr), GFP_ATOMIC);
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

	err = usb_submit_urb(urb, GFP_ATOMIC);
	if (err < 0) {
		BT_ERR("%s urb %p submission failed", hdev->name, urb);
		kfree(urb->setup_packet);
		usb_unanchor_urb(urb);
	}

	usb_free_urb(urb);

	return 0;
}

static void bpa10x_destruct(struct hci_dev *hdev)
{
	struct bpa10x_data *data = hdev->driver_data;

	BT_DBG("%s", hdev->name);

	kfree_skb(data->rx_skb[0]);
	kfree_skb(data->rx_skb[1]);
	kfree(data);
}

static int bpa10x_probe(struct usb_interface *intf, const struct usb_device_id *id)
{
	struct bpa10x_data *data;
	struct hci_dev *hdev;
	int err;

	BT_DBG("intf %p id %p", intf, id);

	if (intf->cur_altsetting->desc.bInterfaceNumber != 0)
		return -ENODEV;

	data = kzalloc(sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->udev = interface_to_usbdev(intf);

	init_usb_anchor(&data->tx_anchor);
	init_usb_anchor(&data->rx_anchor);

	hdev = hci_alloc_dev();
	if (!hdev) {
		kfree(data);
		return -ENOMEM;
	}

	hdev->bus = HCI_USB;
	hdev->driver_data = data;

	data->hdev = hdev;

	SET_HCIDEV_DEV(hdev, &intf->dev);

	hdev->open     = bpa10x_open;
	hdev->close    = bpa10x_close;
	hdev->flush    = bpa10x_flush;
	hdev->send     = bpa10x_send_frame;
	hdev->destruct = bpa10x_destruct;

	hdev->owner = THIS_MODULE;

	set_bit(HCI_QUIRK_NO_RESET, &hdev->quirks);

	err = hci_register_dev(hdev);
	if (err < 0) {
		hci_free_dev(hdev);
		kfree(data);
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
}

static struct usb_driver bpa10x_driver = {
	.name		= "bpa10x",
	.probe		= bpa10x_probe,
	.disconnect	= bpa10x_disconnect,
	.id_table	= bpa10x_table,
};

module_usb_driver(bpa10x_driver);

MODULE_AUTHOR("Marcel Holtmann <marcel@holtmann.org>");
MODULE_DESCRIPTION("Digianswer Bluetooth USB driver ver " VERSION);
MODULE_VERSION(VERSION);
MODULE_LICENSE("GPL");
