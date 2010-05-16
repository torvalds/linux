/*
 *
 *  AVM BlueFRITZ! USB driver
 *
 *  Copyright (C) 2003-2006  Marcel Holtmann <marcel@holtmann.org>
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

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/skbuff.h>

#include <linux/device.h>
#include <linux/firmware.h>

#include <linux/usb.h>

#include <net/bluetooth/bluetooth.h>
#include <net/bluetooth/hci_core.h>

#define VERSION "1.2"

static struct usb_driver bfusb_driver;

static struct usb_device_id bfusb_table[] = {
	/* AVM BlueFRITZ! USB */
	{ USB_DEVICE(0x057c, 0x2200) },

	{ }	/* Terminating entry */
};

MODULE_DEVICE_TABLE(usb, bfusb_table);

#define BFUSB_MAX_BLOCK_SIZE	256

#define BFUSB_BLOCK_TIMEOUT	3000

#define BFUSB_TX_PROCESS	1
#define BFUSB_TX_WAKEUP		2

#define BFUSB_MAX_BULK_TX	2
#define BFUSB_MAX_BULK_RX	2

struct bfusb_data {
	struct hci_dev		*hdev;

	unsigned long		state;

	struct usb_device	*udev;

	unsigned int		bulk_in_ep;
	unsigned int		bulk_out_ep;
	unsigned int		bulk_pkt_size;

	rwlock_t		lock;

	struct sk_buff_head	transmit_q;

	struct sk_buff		*reassembly;

	atomic_t		pending_tx;
	struct sk_buff_head	pending_q;
	struct sk_buff_head	completed_q;
};

struct bfusb_data_scb {
	struct urb *urb;
};

static void bfusb_tx_complete(struct urb *urb);
static void bfusb_rx_complete(struct urb *urb);

static struct urb *bfusb_get_completed(struct bfusb_data *data)
{
	struct sk_buff *skb;
	struct urb *urb = NULL;

	BT_DBG("bfusb %p", data);

	skb = skb_dequeue(&data->completed_q);
	if (skb) {
		urb = ((struct bfusb_data_scb *) skb->cb)->urb;
		kfree_skb(skb);
	}

	return urb;
}

static void bfusb_unlink_urbs(struct bfusb_data *data)
{
	struct sk_buff *skb;
	struct urb *urb;

	BT_DBG("bfusb %p", data);

	while ((skb = skb_dequeue(&data->pending_q))) {
		urb = ((struct bfusb_data_scb *) skb->cb)->urb;
		usb_kill_urb(urb);
		skb_queue_tail(&data->completed_q, skb);
	}

	while ((urb = bfusb_get_completed(data)))
		usb_free_urb(urb);
}

static int bfusb_send_bulk(struct bfusb_data *data, struct sk_buff *skb)
{
	struct bfusb_data_scb *scb = (void *) skb->cb;
	struct urb *urb = bfusb_get_completed(data);
	int err, pipe;

	BT_DBG("bfusb %p skb %p len %d", data, skb, skb->len);

	if (!urb && !(urb = usb_alloc_urb(0, GFP_ATOMIC)))
		return -ENOMEM;

	pipe = usb_sndbulkpipe(data->udev, data->bulk_out_ep);

	usb_fill_bulk_urb(urb, data->udev, pipe, skb->data, skb->len,
			bfusb_tx_complete, skb);

	scb->urb = urb;

	skb_queue_tail(&data->pending_q, skb);

	err = usb_submit_urb(urb, GFP_ATOMIC);
	if (err) {
		BT_ERR("%s bulk tx submit failed urb %p err %d", 
					data->hdev->name, urb, err);
		skb_unlink(skb, &data->pending_q);
		usb_free_urb(urb);
	} else
		atomic_inc(&data->pending_tx);

	return err;
}

static void bfusb_tx_wakeup(struct bfusb_data *data)
{
	struct sk_buff *skb;

	BT_DBG("bfusb %p", data);

	if (test_and_set_bit(BFUSB_TX_PROCESS, &data->state)) {
		set_bit(BFUSB_TX_WAKEUP, &data->state);
		return;
	}

	do {
		clear_bit(BFUSB_TX_WAKEUP, &data->state);

		while ((atomic_read(&data->pending_tx) < BFUSB_MAX_BULK_TX) &&
				(skb = skb_dequeue(&data->transmit_q))) {
			if (bfusb_send_bulk(data, skb) < 0) {
				skb_queue_head(&data->transmit_q, skb);
				break;
			}
		}

	} while (test_bit(BFUSB_TX_WAKEUP, &data->state));

	clear_bit(BFUSB_TX_PROCESS, &data->state);
}

static void bfusb_tx_complete(struct urb *urb)
{
	struct sk_buff *skb = (struct sk_buff *) urb->context;
	struct bfusb_data *data = (struct bfusb_data *) skb->dev;

	BT_DBG("bfusb %p urb %p skb %p len %d", data, urb, skb, skb->len);

	atomic_dec(&data->pending_tx);

	if (!test_bit(HCI_RUNNING, &data->hdev->flags))
		return;

	if (!urb->status)
		data->hdev->stat.byte_tx += skb->len;
	else
		data->hdev->stat.err_tx++;

	read_lock(&data->lock);

	skb_unlink(skb, &data->pending_q);
	skb_queue_tail(&data->completed_q, skb);

	bfusb_tx_wakeup(data);

	read_unlock(&data->lock);
}


static int bfusb_rx_submit(struct bfusb_data *data, struct urb *urb)
{
	struct bfusb_data_scb *scb;
	struct sk_buff *skb;
	int err, pipe, size = HCI_MAX_FRAME_SIZE + 32;

	BT_DBG("bfusb %p urb %p", data, urb);

	if (!urb && !(urb = usb_alloc_urb(0, GFP_ATOMIC)))
		return -ENOMEM;

	skb = bt_skb_alloc(size, GFP_ATOMIC);
	if (!skb) {
		usb_free_urb(urb);
		return -ENOMEM;
	}

	skb->dev = (void *) data;

	scb = (struct bfusb_data_scb *) skb->cb;
	scb->urb = urb;

	pipe = usb_rcvbulkpipe(data->udev, data->bulk_in_ep);

	usb_fill_bulk_urb(urb, data->udev, pipe, skb->data, size,
			bfusb_rx_complete, skb);

	skb_queue_tail(&data->pending_q, skb);

	err = usb_submit_urb(urb, GFP_ATOMIC);
	if (err) {
		BT_ERR("%s bulk rx submit failed urb %p err %d",
					data->hdev->name, urb, err);
		skb_unlink(skb, &data->pending_q);
		kfree_skb(skb);
		usb_free_urb(urb);
	}

	return err;
}

static inline int bfusb_recv_block(struct bfusb_data *data, int hdr, unsigned char *buf, int len)
{
	BT_DBG("bfusb %p hdr 0x%02x data %p len %d", data, hdr, buf, len);

	if (hdr & 0x10) {
		BT_ERR("%s error in block", data->hdev->name);
		kfree_skb(data->reassembly);
		data->reassembly = NULL;
		return -EIO;
	}

	if (hdr & 0x04) {
		struct sk_buff *skb;
		unsigned char pkt_type;
		int pkt_len = 0;

		if (data->reassembly) {
			BT_ERR("%s unexpected start block", data->hdev->name);
			kfree_skb(data->reassembly);
			data->reassembly = NULL;
		}

		if (len < 1) {
			BT_ERR("%s no packet type found", data->hdev->name);
			return -EPROTO;
		}

		pkt_type = *buf++; len--;

		switch (pkt_type) {
		case HCI_EVENT_PKT:
			if (len >= HCI_EVENT_HDR_SIZE) {
				struct hci_event_hdr *hdr = (struct hci_event_hdr *) buf;
				pkt_len = HCI_EVENT_HDR_SIZE + hdr->plen;
			} else {
				BT_ERR("%s event block is too short", data->hdev->name);
				return -EILSEQ;
			}
			break;

		case HCI_ACLDATA_PKT:
			if (len >= HCI_ACL_HDR_SIZE) {
				struct hci_acl_hdr *hdr = (struct hci_acl_hdr *) buf;
				pkt_len = HCI_ACL_HDR_SIZE + __le16_to_cpu(hdr->dlen);
			} else {
				BT_ERR("%s data block is too short", data->hdev->name);
				return -EILSEQ;
			}
			break;

		case HCI_SCODATA_PKT:
			if (len >= HCI_SCO_HDR_SIZE) {
				struct hci_sco_hdr *hdr = (struct hci_sco_hdr *) buf;
				pkt_len = HCI_SCO_HDR_SIZE + hdr->dlen;
			} else {
				BT_ERR("%s audio block is too short", data->hdev->name);
				return -EILSEQ;
			}
			break;
		}

		skb = bt_skb_alloc(pkt_len, GFP_ATOMIC);
		if (!skb) {
			BT_ERR("%s no memory for the packet", data->hdev->name);
			return -ENOMEM;
		}

		skb->dev = (void *) data->hdev;
		bt_cb(skb)->pkt_type = pkt_type;

		data->reassembly = skb;
	} else {
		if (!data->reassembly) {
			BT_ERR("%s unexpected continuation block", data->hdev->name);
			return -EIO;
		}
	}

	if (len > 0)
		memcpy(skb_put(data->reassembly, len), buf, len);

	if (hdr & 0x08) {
		hci_recv_frame(data->reassembly);
		data->reassembly = NULL;
	}

	return 0;
}

static void bfusb_rx_complete(struct urb *urb)
{
	struct sk_buff *skb = (struct sk_buff *) urb->context;
	struct bfusb_data *data = (struct bfusb_data *) skb->dev;
	unsigned char *buf = urb->transfer_buffer;
	int count = urb->actual_length;
	int err, hdr, len;

	BT_DBG("bfusb %p urb %p skb %p len %d", data, urb, skb, skb->len);

	read_lock(&data->lock);

	if (!test_bit(HCI_RUNNING, &data->hdev->flags))
		goto unlock;

	if (urb->status || !count)
		goto resubmit;

	data->hdev->stat.byte_rx += count;

	skb_put(skb, count);

	while (count) {
		hdr = buf[0] | (buf[1] << 8);

		if (hdr & 0x4000) {
			len = 0;
			count -= 2;
			buf   += 2;
		} else {
			len = (buf[2] == 0) ? 256 : buf[2];
			count -= 3;
			buf   += 3;
		}

		if (count < len) {
			BT_ERR("%s block extends over URB buffer ranges",
					data->hdev->name);
		}

		if ((hdr & 0xe1) == 0xc1)
			bfusb_recv_block(data, hdr, buf, len);

		count -= len;
		buf   += len;
	}

	skb_unlink(skb, &data->pending_q);
	kfree_skb(skb);

	bfusb_rx_submit(data, urb);

	read_unlock(&data->lock);

	return;

resubmit:
	urb->dev = data->udev;

	err = usb_submit_urb(urb, GFP_ATOMIC);
	if (err) {
		BT_ERR("%s bulk resubmit failed urb %p err %d",
					data->hdev->name, urb, err);
	}

unlock:
	read_unlock(&data->lock);
}

static int bfusb_open(struct hci_dev *hdev)
{
	struct bfusb_data *data = hdev->driver_data;
	unsigned long flags;
	int i, err;

	BT_DBG("hdev %p bfusb %p", hdev, data);

	if (test_and_set_bit(HCI_RUNNING, &hdev->flags))
		return 0;

	write_lock_irqsave(&data->lock, flags);

	err = bfusb_rx_submit(data, NULL);
	if (!err) {
		for (i = 1; i < BFUSB_MAX_BULK_RX; i++)
			bfusb_rx_submit(data, NULL);
	} else {
		clear_bit(HCI_RUNNING, &hdev->flags);
	}

	write_unlock_irqrestore(&data->lock, flags);

	return err;
}

static int bfusb_flush(struct hci_dev *hdev)
{
	struct bfusb_data *data = hdev->driver_data;

	BT_DBG("hdev %p bfusb %p", hdev, data);

	skb_queue_purge(&data->transmit_q);

	return 0;
}

static int bfusb_close(struct hci_dev *hdev)
{
	struct bfusb_data *data = hdev->driver_data;
	unsigned long flags;

	BT_DBG("hdev %p bfusb %p", hdev, data);

	if (!test_and_clear_bit(HCI_RUNNING, &hdev->flags))
		return 0;

	write_lock_irqsave(&data->lock, flags);
	write_unlock_irqrestore(&data->lock, flags);

	bfusb_unlink_urbs(data);
	bfusb_flush(hdev);

	return 0;
}

static int bfusb_send_frame(struct sk_buff *skb)
{
	struct hci_dev *hdev = (struct hci_dev *) skb->dev;
	struct bfusb_data *data;
	struct sk_buff *nskb;
	unsigned char buf[3];
	int sent = 0, size, count;

	BT_DBG("hdev %p skb %p type %d len %d", hdev, skb, bt_cb(skb)->pkt_type, skb->len);

	if (!hdev) {
		BT_ERR("Frame for unknown HCI device (hdev=NULL)");
		return -ENODEV;
	}

	if (!test_bit(HCI_RUNNING, &hdev->flags))
		return -EBUSY;

	data = hdev->driver_data;

	switch (bt_cb(skb)->pkt_type) {
	case HCI_COMMAND_PKT:
		hdev->stat.cmd_tx++;
		break;
	case HCI_ACLDATA_PKT:
		hdev->stat.acl_tx++;
		break;
	case HCI_SCODATA_PKT:
		hdev->stat.sco_tx++;
		break;
	};

	/* Prepend skb with frame type */
	memcpy(skb_push(skb, 1), &bt_cb(skb)->pkt_type, 1);

	count = skb->len;

	/* Max HCI frame size seems to be 1511 + 1 */
	nskb = bt_skb_alloc(count + 32, GFP_ATOMIC);
	if (!nskb) {
		BT_ERR("Can't allocate memory for new packet");
		return -ENOMEM;
	}

	nskb->dev = (void *) data;

	while (count) {
		size = min_t(uint, count, BFUSB_MAX_BLOCK_SIZE);

		buf[0] = 0xc1 | ((sent == 0) ? 0x04 : 0) | ((count == size) ? 0x08 : 0);
		buf[1] = 0x00;
		buf[2] = (size == BFUSB_MAX_BLOCK_SIZE) ? 0 : size;

		memcpy(skb_put(nskb, 3), buf, 3);
		skb_copy_from_linear_data_offset(skb, sent, skb_put(nskb, size), size);

		sent  += size;
		count -= size;
	}

	/* Don't send frame with multiple size of bulk max packet */
	if ((nskb->len % data->bulk_pkt_size) == 0) {
		buf[0] = 0xdd;
		buf[1] = 0x00;
		memcpy(skb_put(nskb, 2), buf, 2);
	}

	read_lock(&data->lock);

	skb_queue_tail(&data->transmit_q, nskb);
	bfusb_tx_wakeup(data);

	read_unlock(&data->lock);

	kfree_skb(skb);

	return 0;
}

static void bfusb_destruct(struct hci_dev *hdev)
{
	struct bfusb_data *data = hdev->driver_data;

	BT_DBG("hdev %p bfusb %p", hdev, data);

	kfree(data);
}

static int bfusb_ioctl(struct hci_dev *hdev, unsigned int cmd, unsigned long arg)
{
	return -ENOIOCTLCMD;
}

static int bfusb_load_firmware(struct bfusb_data *data,
			       const unsigned char *firmware, int count)
{
	unsigned char *buf;
	int err, pipe, len, size, sent = 0;

	BT_DBG("bfusb %p udev %p", data, data->udev);

	BT_INFO("BlueFRITZ! USB loading firmware");

	pipe = usb_sndctrlpipe(data->udev, 0);

	if (usb_control_msg(data->udev, pipe, USB_REQ_SET_CONFIGURATION,
				0, 1, 0, NULL, 0, USB_CTRL_SET_TIMEOUT) < 0) {
		BT_ERR("Can't change to loading configuration");
		return -EBUSY;
	}

	data->udev->toggle[0] = data->udev->toggle[1] = 0;

	buf = kmalloc(BFUSB_MAX_BLOCK_SIZE + 3, GFP_ATOMIC);
	if (!buf) {
		BT_ERR("Can't allocate memory chunk for firmware");
		return -ENOMEM;
	}

	pipe = usb_sndbulkpipe(data->udev, data->bulk_out_ep);

	while (count) {
		size = min_t(uint, count, BFUSB_MAX_BLOCK_SIZE + 3);

		memcpy(buf, firmware + sent, size);

		err = usb_bulk_msg(data->udev, pipe, buf, size,
					&len, BFUSB_BLOCK_TIMEOUT);

		if (err || (len != size)) {
			BT_ERR("Error in firmware loading");
			goto error;
		}

		sent  += size;
		count -= size;
	}

	err = usb_bulk_msg(data->udev, pipe, NULL, 0,
					&len, BFUSB_BLOCK_TIMEOUT);
	if (err < 0) {
		BT_ERR("Error in null packet request");
		goto error;
	}

	pipe = usb_sndctrlpipe(data->udev, 0);

	err = usb_control_msg(data->udev, pipe, USB_REQ_SET_CONFIGURATION,
				0, 2, 0, NULL, 0, USB_CTRL_SET_TIMEOUT);
	if (err < 0) {
		BT_ERR("Can't change to running configuration");
		goto error;
	}

	data->udev->toggle[0] = data->udev->toggle[1] = 0;

	BT_INFO("BlueFRITZ! USB device ready");

	kfree(buf);
	return 0;

error:
	kfree(buf);

	pipe = usb_sndctrlpipe(data->udev, 0);

	usb_control_msg(data->udev, pipe, USB_REQ_SET_CONFIGURATION,
				0, 0, 0, NULL, 0, USB_CTRL_SET_TIMEOUT);

	return err;
}

static int bfusb_probe(struct usb_interface *intf, const struct usb_device_id *id)
{
	const struct firmware *firmware;
	struct usb_device *udev = interface_to_usbdev(intf);
	struct usb_host_endpoint *bulk_out_ep;
	struct usb_host_endpoint *bulk_in_ep;
	struct hci_dev *hdev;
	struct bfusb_data *data;

	BT_DBG("intf %p id %p", intf, id);

	/* Check number of endpoints */
	if (intf->cur_altsetting->desc.bNumEndpoints < 2)
		return -EIO;

	bulk_out_ep = &intf->cur_altsetting->endpoint[0];
	bulk_in_ep  = &intf->cur_altsetting->endpoint[1];

	if (!bulk_out_ep || !bulk_in_ep) {
		BT_ERR("Bulk endpoints not found");
		goto done;
	}

	/* Initialize control structure and load firmware */
	data = kzalloc(sizeof(struct bfusb_data), GFP_KERNEL);
	if (!data) {
		BT_ERR("Can't allocate memory for control structure");
		goto done;
	}

	data->udev = udev;
	data->bulk_in_ep    = bulk_in_ep->desc.bEndpointAddress;
	data->bulk_out_ep   = bulk_out_ep->desc.bEndpointAddress;
	data->bulk_pkt_size = le16_to_cpu(bulk_out_ep->desc.wMaxPacketSize);

	rwlock_init(&data->lock);

	data->reassembly = NULL;

	skb_queue_head_init(&data->transmit_q);
	skb_queue_head_init(&data->pending_q);
	skb_queue_head_init(&data->completed_q);

	if (request_firmware(&firmware, "bfubase.frm", &udev->dev) < 0) {
		BT_ERR("Firmware request failed");
		goto error;
	}

	BT_DBG("firmware data %p size %zu", firmware->data, firmware->size);

	if (bfusb_load_firmware(data, firmware->data, firmware->size) < 0) {
		BT_ERR("Firmware loading failed");
		goto release;
	}

	release_firmware(firmware);

	/* Initialize and register HCI device */
	hdev = hci_alloc_dev();
	if (!hdev) {
		BT_ERR("Can't allocate HCI device");
		goto error;
	}

	data->hdev = hdev;

	hdev->bus = HCI_USB;
	hdev->driver_data = data;
	SET_HCIDEV_DEV(hdev, &intf->dev);

	hdev->open     = bfusb_open;
	hdev->close    = bfusb_close;
	hdev->flush    = bfusb_flush;
	hdev->send     = bfusb_send_frame;
	hdev->destruct = bfusb_destruct;
	hdev->ioctl    = bfusb_ioctl;

	hdev->owner = THIS_MODULE;

	if (hci_register_dev(hdev) < 0) {
		BT_ERR("Can't register HCI device");
		hci_free_dev(hdev);
		goto error;
	}

	usb_set_intfdata(intf, data);

	return 0;

release:
	release_firmware(firmware);

error:
	kfree(data);

done:
	return -EIO;
}

static void bfusb_disconnect(struct usb_interface *intf)
{
	struct bfusb_data *data = usb_get_intfdata(intf);
	struct hci_dev *hdev = data->hdev;

	BT_DBG("intf %p", intf);

	if (!hdev)
		return;

	usb_set_intfdata(intf, NULL);

	bfusb_close(hdev);

	if (hci_unregister_dev(hdev) < 0)
		BT_ERR("Can't unregister HCI device %s", hdev->name);

	hci_free_dev(hdev);
}

static struct usb_driver bfusb_driver = {
	.name		= "bfusb",
	.probe		= bfusb_probe,
	.disconnect	= bfusb_disconnect,
	.id_table	= bfusb_table,
};

static int __init bfusb_init(void)
{
	int err;

	BT_INFO("BlueFRITZ! USB driver ver %s", VERSION);

	err = usb_register(&bfusb_driver);
	if (err < 0)
		BT_ERR("Failed to register BlueFRITZ! USB driver");

	return err;
}

static void __exit bfusb_exit(void)
{
	usb_deregister(&bfusb_driver);
}

module_init(bfusb_init);
module_exit(bfusb_exit);

MODULE_AUTHOR("Marcel Holtmann <marcel@holtmann.org>");
MODULE_DESCRIPTION("BlueFRITZ! USB driver ver " VERSION);
MODULE_VERSION(VERSION);
MODULE_LICENSE("GPL");
MODULE_FIRMWARE("bfubase.frm");
