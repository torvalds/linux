/*
 *
 *  Digianswer Bluetooth USB driver
 *
 *  Copyright (C) 2004-2005  Marcel Holtmann <marcel@holtmann.org>
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

#include <linux/config.h>
#include <linux/module.h>

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/errno.h>

#include <linux/usb.h>

#include <net/bluetooth/bluetooth.h>
#include <net/bluetooth/hci_core.h>

#ifndef CONFIG_BT_HCIBPA10X_DEBUG
#undef  BT_DBG
#define BT_DBG(D...)
#endif

#define VERSION "0.8"

static int ignore = 0;

static struct usb_device_id bpa10x_table[] = {
	/* Tektronix BPA 100/105 (Digianswer) */
	{ USB_DEVICE(0x08fd, 0x0002) },

	{ }	/* Terminating entry */
};

MODULE_DEVICE_TABLE(usb, bpa10x_table);

#define BPA10X_CMD_EP		0x00
#define BPA10X_EVT_EP		0x81
#define BPA10X_TX_EP		0x02
#define BPA10X_RX_EP		0x82

#define BPA10X_CMD_BUF_SIZE	252
#define BPA10X_EVT_BUF_SIZE	16
#define BPA10X_TX_BUF_SIZE	384
#define BPA10X_RX_BUF_SIZE	384

struct bpa10x_data {
	struct hci_dev		*hdev;
	struct usb_device	*udev;

	rwlock_t		lock;

	struct sk_buff_head	cmd_queue;
	struct urb		*cmd_urb;
	struct urb		*evt_urb;
	struct sk_buff		*evt_skb;
	unsigned int		evt_len;

	struct sk_buff_head	tx_queue;
	struct urb		*tx_urb;
	struct urb		*rx_urb;
};

#define HCI_VENDOR_HDR_SIZE	5

struct hci_vendor_hdr {
	__u8	type;
	__u16	snum;
	__u16	dlen;
} __attribute__ ((packed));

static void bpa10x_recv_bulk(struct bpa10x_data *data, unsigned char *buf, int count)
{
	struct hci_acl_hdr *ah;
	struct hci_sco_hdr *sh;
	struct hci_vendor_hdr *vh;
	struct sk_buff *skb;
	int len;

	while (count) {
		switch (*buf++) {
		case HCI_ACLDATA_PKT:
			ah = (struct hci_acl_hdr *) buf;
			len = HCI_ACL_HDR_SIZE + __le16_to_cpu(ah->dlen);
			skb = bt_skb_alloc(len, GFP_ATOMIC);
			if (skb) {
				memcpy(skb_put(skb, len), buf, len);
				skb->dev = (void *) data->hdev;
				bt_cb(skb)->pkt_type = HCI_ACLDATA_PKT;
				hci_recv_frame(skb);
			}
			break;

		case HCI_SCODATA_PKT:
			sh = (struct hci_sco_hdr *) buf;
			len = HCI_SCO_HDR_SIZE + sh->dlen;
			skb = bt_skb_alloc(len, GFP_ATOMIC);
			if (skb) {
				memcpy(skb_put(skb, len), buf, len);
				skb->dev = (void *) data->hdev;
				bt_cb(skb)->pkt_type = HCI_SCODATA_PKT;
				hci_recv_frame(skb);
			}
			break;

		case HCI_VENDOR_PKT:
			vh = (struct hci_vendor_hdr *) buf;
			len = HCI_VENDOR_HDR_SIZE + __le16_to_cpu(vh->dlen);
			skb = bt_skb_alloc(len, GFP_ATOMIC);
			if (skb) {
				memcpy(skb_put(skb, len), buf, len);
				skb->dev = (void *) data->hdev;
				bt_cb(skb)->pkt_type = HCI_VENDOR_PKT;
				hci_recv_frame(skb);
			}
			break;

		default:
			len = count - 1;
			break;
		}

		buf   += len;
		count -= (len + 1);
	}
}

static int bpa10x_recv_event(struct bpa10x_data *data, unsigned char *buf, int size)
{
	BT_DBG("data %p buf %p size %d", data, buf, size);

	if (data->evt_skb) {
		struct sk_buff *skb = data->evt_skb;

		memcpy(skb_put(skb, size), buf, size);

		if (skb->len == data->evt_len) {
			data->evt_skb = NULL;
			data->evt_len = 0;
			hci_recv_frame(skb);
		}
	} else {
		struct sk_buff *skb;
		struct hci_event_hdr *hdr;
		unsigned char pkt_type;
		int pkt_len = 0;

		if (size < HCI_EVENT_HDR_SIZE + 1) {
			BT_ERR("%s event packet block with size %d is too short",
							data->hdev->name, size);
			return -EILSEQ;
		}

		pkt_type = *buf++;
		size--;

		if (pkt_type != HCI_EVENT_PKT) {
			BT_ERR("%s unexpected event packet start byte 0x%02x",
							data->hdev->name, pkt_type);
			return -EPROTO;
		}

		hdr = (struct hci_event_hdr *) buf;
		pkt_len = HCI_EVENT_HDR_SIZE + hdr->plen;

		skb = bt_skb_alloc(pkt_len, GFP_ATOMIC);
		if (!skb) {
			BT_ERR("%s no memory for new event packet",
							data->hdev->name);
			return -ENOMEM;
		}

		skb->dev = (void *) data->hdev;
		bt_cb(skb)->pkt_type = pkt_type;

		memcpy(skb_put(skb, size), buf, size);

		if (pkt_len == size) {
			hci_recv_frame(skb);
		} else {
			data->evt_skb = skb;
			data->evt_len = pkt_len;
		}
	}

	return 0;
}

static void bpa10x_wakeup(struct bpa10x_data *data)
{
	struct urb *urb;
	struct sk_buff *skb;
	int err;

	BT_DBG("data %p", data);

	urb = data->cmd_urb;
	if (urb->status == -EINPROGRESS)
		skb = NULL;
	else
		skb = skb_dequeue(&data->cmd_queue);

	if (skb) {
		struct usb_ctrlrequest *cr;

		if (skb->len > BPA10X_CMD_BUF_SIZE) {
			BT_ERR("%s command packet with size %d is too big",
							data->hdev->name, skb->len);
			kfree_skb(skb);
			return;
		}

		cr = (struct usb_ctrlrequest *) urb->setup_packet;
		cr->wLength = __cpu_to_le16(skb->len);

		memcpy(urb->transfer_buffer, skb->data, skb->len);
		urb->transfer_buffer_length = skb->len;

		err = usb_submit_urb(urb, GFP_ATOMIC);
		if (err < 0 && err != -ENODEV) {
			BT_ERR("%s submit failed for command urb %p with error %d",
							data->hdev->name, urb, err);
			skb_queue_head(&data->cmd_queue, skb);
		} else
			kfree_skb(skb);
	}

	urb = data->tx_urb;
	if (urb->status == -EINPROGRESS)
		skb = NULL;
	else
		skb = skb_dequeue(&data->tx_queue);

	if (skb) {
		memcpy(urb->transfer_buffer, skb->data, skb->len);
		urb->transfer_buffer_length = skb->len;

		err = usb_submit_urb(urb, GFP_ATOMIC);
		if (err < 0 && err != -ENODEV) {
			BT_ERR("%s submit failed for command urb %p with error %d",
							data->hdev->name, urb, err);
			skb_queue_head(&data->tx_queue, skb);
		} else
			kfree_skb(skb);
	}
}

static void bpa10x_complete(struct urb *urb, struct pt_regs *regs)
{
	struct bpa10x_data *data = urb->context;
	unsigned char *buf = urb->transfer_buffer;
	int err, count = urb->actual_length;

	BT_DBG("data %p urb %p buf %p count %d", data, urb, buf, count);

	read_lock(&data->lock);

	if (!test_bit(HCI_RUNNING, &data->hdev->flags))
		goto unlock;

	if (urb->status < 0 || !count)
		goto resubmit;

	if (usb_pipein(urb->pipe)) {
		data->hdev->stat.byte_rx += count;

		if (usb_pipetype(urb->pipe) == PIPE_INTERRUPT)
			bpa10x_recv_event(data, buf, count);

		if (usb_pipetype(urb->pipe) == PIPE_BULK)
			bpa10x_recv_bulk(data, buf, count);
	} else {
		data->hdev->stat.byte_tx += count;

		bpa10x_wakeup(data);
	}

resubmit:
	if (usb_pipein(urb->pipe)) {
		err = usb_submit_urb(urb, GFP_ATOMIC);
		if (err < 0 && err != -ENODEV) {
			BT_ERR("%s urb %p type %d resubmit status %d",
				data->hdev->name, urb, usb_pipetype(urb->pipe), err);
		}
	}

unlock:
	read_unlock(&data->lock);
}

static inline struct urb *bpa10x_alloc_urb(struct usb_device *udev, unsigned int pipe,
					size_t size, gfp_t flags, void *data)
{
	struct urb *urb;
	struct usb_ctrlrequest *cr;
	unsigned char *buf;

	BT_DBG("udev %p data %p", udev, data);

	urb = usb_alloc_urb(0, flags);
	if (!urb)
		return NULL;

	buf = kmalloc(size, flags);
	if (!buf) {
		usb_free_urb(urb);
		return NULL;
	}

	switch (usb_pipetype(pipe)) {
	case PIPE_CONTROL:
		cr = kmalloc(sizeof(*cr), flags);
		if (!cr) {
			kfree(buf);
			usb_free_urb(urb);
			return NULL;
		}

		cr->bRequestType = USB_TYPE_VENDOR;
		cr->bRequest     = 0;
		cr->wIndex       = 0;
		cr->wValue       = 0;
		cr->wLength      = __cpu_to_le16(0);

		usb_fill_control_urb(urb, udev, pipe, (void *) cr, buf, 0, bpa10x_complete, data);
		break;

	case PIPE_INTERRUPT:
		usb_fill_int_urb(urb, udev, pipe, buf, size, bpa10x_complete, data, 1);
		break;

	case PIPE_BULK:
		usb_fill_bulk_urb(urb, udev, pipe, buf, size, bpa10x_complete, data);
		break;

	default:
		kfree(buf);
		usb_free_urb(urb);
		return NULL;
	}

	return urb;
}

static inline void bpa10x_free_urb(struct urb *urb)
{
	BT_DBG("urb %p", urb);

	if (!urb)
		return;

	kfree(urb->setup_packet);
	kfree(urb->transfer_buffer);

	usb_free_urb(urb);
}

static int bpa10x_open(struct hci_dev *hdev)
{
	struct bpa10x_data *data = hdev->driver_data;
	struct usb_device *udev = data->udev;
	unsigned long flags;
	int err;

	BT_DBG("hdev %p data %p", hdev, data);

	if (test_and_set_bit(HCI_RUNNING, &hdev->flags))
		return 0;

	data->cmd_urb = bpa10x_alloc_urb(udev, usb_sndctrlpipe(udev, BPA10X_CMD_EP),
					BPA10X_CMD_BUF_SIZE, GFP_KERNEL, data);
	if (!data->cmd_urb) {
		err = -ENOMEM;
		goto done;
	}

	data->evt_urb = bpa10x_alloc_urb(udev, usb_rcvintpipe(udev, BPA10X_EVT_EP),
					BPA10X_EVT_BUF_SIZE, GFP_KERNEL, data);
	if (!data->evt_urb) {
		bpa10x_free_urb(data->cmd_urb);
		err = -ENOMEM;
		goto done;
	}

	data->rx_urb = bpa10x_alloc_urb(udev, usb_rcvbulkpipe(udev, BPA10X_RX_EP),
					BPA10X_RX_BUF_SIZE, GFP_KERNEL, data);
	if (!data->rx_urb) {
		bpa10x_free_urb(data->evt_urb);
		bpa10x_free_urb(data->cmd_urb);
		err = -ENOMEM;
		goto done;
	}

	data->tx_urb = bpa10x_alloc_urb(udev, usb_sndbulkpipe(udev, BPA10X_TX_EP),
					BPA10X_TX_BUF_SIZE, GFP_KERNEL, data);
	if (!data->rx_urb) {
		bpa10x_free_urb(data->rx_urb);
		bpa10x_free_urb(data->evt_urb);
		bpa10x_free_urb(data->cmd_urb);
		err = -ENOMEM;
		goto done;
	}

	write_lock_irqsave(&data->lock, flags);

	err = usb_submit_urb(data->evt_urb, GFP_ATOMIC);
	if (err < 0) {
		BT_ERR("%s submit failed for event urb %p with error %d",
					data->hdev->name, data->evt_urb, err);
	} else {
		err = usb_submit_urb(data->rx_urb, GFP_ATOMIC);
		if (err < 0) {
			BT_ERR("%s submit failed for rx urb %p with error %d",
					data->hdev->name, data->evt_urb, err);
			usb_kill_urb(data->evt_urb);
		}
	}

	write_unlock_irqrestore(&data->lock, flags);

done:
	if (err < 0)
		clear_bit(HCI_RUNNING, &hdev->flags);

	return err;
}

static int bpa10x_close(struct hci_dev *hdev)
{
	struct bpa10x_data *data = hdev->driver_data;
	unsigned long flags;

	BT_DBG("hdev %p data %p", hdev, data);

	if (!test_and_clear_bit(HCI_RUNNING, &hdev->flags))
		return 0;

	write_lock_irqsave(&data->lock, flags);

	skb_queue_purge(&data->cmd_queue);
	usb_kill_urb(data->cmd_urb);
	usb_kill_urb(data->evt_urb);
	usb_kill_urb(data->rx_urb);
	usb_kill_urb(data->tx_urb);

	write_unlock_irqrestore(&data->lock, flags);

	bpa10x_free_urb(data->cmd_urb);
	bpa10x_free_urb(data->evt_urb);
	bpa10x_free_urb(data->rx_urb);
	bpa10x_free_urb(data->tx_urb);

	return 0;
}

static int bpa10x_flush(struct hci_dev *hdev)
{
	struct bpa10x_data *data = hdev->driver_data;

	BT_DBG("hdev %p data %p", hdev, data);

	skb_queue_purge(&data->cmd_queue);

	return 0;
}

static int bpa10x_send_frame(struct sk_buff *skb)
{
	struct hci_dev *hdev = (struct hci_dev *) skb->dev;
	struct bpa10x_data *data;

	BT_DBG("hdev %p skb %p type %d len %d", hdev, skb, bt_cb(skb)->pkt_type, skb->len);

	if (!hdev) {
		BT_ERR("Frame for unknown HCI device");
		return -ENODEV;
	}

	if (!test_bit(HCI_RUNNING, &hdev->flags))
		return -EBUSY;

	data = hdev->driver_data;

	/* Prepend skb with frame type */
	memcpy(skb_push(skb, 1), &bt_cb(skb)->pkt_type, 1);

	switch (bt_cb(skb)->pkt_type) {
	case HCI_COMMAND_PKT:
		hdev->stat.cmd_tx++;
		skb_queue_tail(&data->cmd_queue, skb);
		break;

	case HCI_ACLDATA_PKT:
		hdev->stat.acl_tx++;
		skb_queue_tail(&data->tx_queue, skb);
		break;

	case HCI_SCODATA_PKT:
		hdev->stat.sco_tx++;
		skb_queue_tail(&data->tx_queue, skb);
		break;
	};

	read_lock(&data->lock);

	bpa10x_wakeup(data);

	read_unlock(&data->lock);

	return 0;
}

static void bpa10x_destruct(struct hci_dev *hdev)
{
	struct bpa10x_data *data = hdev->driver_data;

	BT_DBG("hdev %p data %p", hdev, data);

	kfree(data);
}

static int bpa10x_probe(struct usb_interface *intf, const struct usb_device_id *id)
{
	struct usb_device *udev = interface_to_usbdev(intf);
	struct hci_dev *hdev;
	struct bpa10x_data *data;
	int err;

	BT_DBG("intf %p id %p", intf, id);

	if (ignore)
		return -ENODEV;

	if (intf->cur_altsetting->desc.bInterfaceNumber > 0)
		return -ENODEV;

	data = kmalloc(sizeof(*data), GFP_KERNEL);
	if (!data) {
		BT_ERR("Can't allocate data structure");
		return -ENOMEM;
	}

	memset(data, 0, sizeof(*data));

	data->udev = udev;

	rwlock_init(&data->lock);

	skb_queue_head_init(&data->cmd_queue);
	skb_queue_head_init(&data->tx_queue);

	hdev = hci_alloc_dev();
	if (!hdev) {
		BT_ERR("Can't allocate HCI device");
		kfree(data);
		return -ENOMEM;
	}

	data->hdev = hdev;

	hdev->type = HCI_USB;
	hdev->driver_data = data;
	SET_HCIDEV_DEV(hdev, &intf->dev);

	hdev->open	= bpa10x_open;
	hdev->close	= bpa10x_close;
	hdev->flush	= bpa10x_flush;
	hdev->send	= bpa10x_send_frame;
	hdev->destruct	= bpa10x_destruct;

	hdev->owner = THIS_MODULE;

	err = hci_register_dev(hdev);
	if (err < 0) {
		BT_ERR("Can't register HCI device");
		kfree(data);
		hci_free_dev(hdev);
		return err;
	}

	usb_set_intfdata(intf, data);

	return 0;
}

static void bpa10x_disconnect(struct usb_interface *intf)
{
	struct bpa10x_data *data = usb_get_intfdata(intf);
	struct hci_dev *hdev = data->hdev;

	BT_DBG("intf %p", intf);

	if (!hdev)
		return;

	usb_set_intfdata(intf, NULL);

	if (hci_unregister_dev(hdev) < 0)
		BT_ERR("Can't unregister HCI device %s", hdev->name);

	hci_free_dev(hdev);
}

static struct usb_driver bpa10x_driver = {
	.owner		= THIS_MODULE,
	.name		= "bpa10x",
	.probe		= bpa10x_probe,
	.disconnect	= bpa10x_disconnect,
	.id_table	= bpa10x_table,
};

static int __init bpa10x_init(void)
{
	int err;

	BT_INFO("Digianswer Bluetooth USB driver ver %s", VERSION);

	err = usb_register(&bpa10x_driver);
	if (err < 0)
		BT_ERR("Failed to register USB driver");

	return err;
}

static void __exit bpa10x_exit(void)
{
	usb_deregister(&bpa10x_driver);
}

module_init(bpa10x_init);
module_exit(bpa10x_exit);

module_param(ignore, bool, 0644);
MODULE_PARM_DESC(ignore, "Ignore devices from the matching table");

MODULE_AUTHOR("Marcel Holtmann <marcel@holtmann.org>");
MODULE_DESCRIPTION("Digianswer Bluetooth USB driver ver " VERSION);
MODULE_VERSION(VERSION);
MODULE_LICENSE("GPL");
