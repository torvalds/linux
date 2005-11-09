/* 
   HCI USB driver for Linux Bluetooth protocol stack (BlueZ)
   Copyright (C) 2000-2001 Qualcomm Incorporated
   Written 2000,2001 by Maxim Krasnyansky <maxk@qualcomm.com>

   Copyright (C) 2003 Maxim Krasnyansky <maxk@qualcomm.com>

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License version 2 as
   published by the Free Software Foundation;

   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
   OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
   FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT OF THIRD PARTY RIGHTS.
   IN NO EVENT SHALL THE COPYRIGHT HOLDER(S) AND AUTHOR(S) BE LIABLE FOR ANY
   CLAIM, OR ANY SPECIAL INDIRECT OR CONSEQUENTIAL DAMAGES, OR ANY DAMAGES 
   WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN 
   ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF 
   OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.

   ALL LIABILITY, INCLUDING LIABILITY FOR INFRINGEMENT OF ANY PATENTS, 
   COPYRIGHTS, TRADEMARKS OR OTHER RIGHTS, RELATING TO USE OF THIS 
   SOFTWARE IS DISCLAIMED.
*/

/*
 * Bluetooth HCI USB driver.
 * Based on original USB Bluetooth driver for Linux kernel
 *    Copyright (c) 2000 Greg Kroah-Hartman        <greg@kroah.com>
 *    Copyright (c) 2000 Mark Douglas Corner       <mcorner@umich.edu>
 *
 */

#include <linux/config.h>
#include <linux/module.h>

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/unistd.h>
#include <linux/types.h>
#include <linux/interrupt.h>
#include <linux/moduleparam.h>

#include <linux/slab.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/skbuff.h>

#include <linux/usb.h>

#include <net/bluetooth/bluetooth.h>
#include <net/bluetooth/hci_core.h>

#include "hci_usb.h"

#ifndef CONFIG_BT_HCIUSB_DEBUG
#undef  BT_DBG
#define BT_DBG(D...)
#endif

#ifndef CONFIG_BT_HCIUSB_ZERO_PACKET
#undef  URB_ZERO_PACKET
#define URB_ZERO_PACKET 0
#endif

static int ignore = 0;
static int ignore_dga = 0;
static int ignore_csr = 0;
static int ignore_sniffer = 0;
static int reset = 0;

#ifdef CONFIG_BT_HCIUSB_SCO
static int isoc = 2;
#endif

#define VERSION "2.9"

static struct usb_driver hci_usb_driver; 

static struct usb_device_id bluetooth_ids[] = {
	/* Generic Bluetooth USB device */
	{ USB_DEVICE_INFO(HCI_DEV_CLASS, HCI_DEV_SUBCLASS, HCI_DEV_PROTOCOL) },

	/* AVM BlueFRITZ! USB v2.0 */
	{ USB_DEVICE(0x057c, 0x3800) },

	/* Bluetooth Ultraport Module from IBM */
	{ USB_DEVICE(0x04bf, 0x030a) },

	/* ALPS Modules with non-standard id */
	{ USB_DEVICE(0x044e, 0x3001) },
	{ USB_DEVICE(0x044e, 0x3002) },

	/* Ericsson with non-standard id */
	{ USB_DEVICE(0x0bdb, 0x1002) },

	{ }	/* Terminating entry */
};

MODULE_DEVICE_TABLE (usb, bluetooth_ids);

static struct usb_device_id blacklist_ids[] = {
	/* CSR BlueCore devices */
	{ USB_DEVICE(0x0a12, 0x0001), .driver_info = HCI_CSR },

	/* Broadcom BCM2033 without firmware */
	{ USB_DEVICE(0x0a5c, 0x2033), .driver_info = HCI_IGNORE },

	/* Broadcom BCM2035 */
	{ USB_DEVICE(0x0a5c, 0x200a), .driver_info = HCI_RESET | HCI_BROKEN_ISOC },
	{ USB_DEVICE(0x0a5c, 0x2009), .driver_info = HCI_BCM92035 },

	/* Microsoft Wireless Transceiver for Bluetooth 2.0 */
	{ USB_DEVICE(0x045e, 0x009c), .driver_info = HCI_RESET },

	/* Kensington Bluetooth USB adapter */
	{ USB_DEVICE(0x047d, 0x105d), .driver_info = HCI_RESET },

	/* ISSC Bluetooth Adapter v3.1 */
	{ USB_DEVICE(0x1131, 0x1001), .driver_info = HCI_RESET },

	/* RTX Telecom based adapter with buggy SCO support */
	{ USB_DEVICE(0x0400, 0x0807), .driver_info = HCI_BROKEN_ISOC },

	/* Digianswer devices */
	{ USB_DEVICE(0x08fd, 0x0001), .driver_info = HCI_DIGIANSWER },
	{ USB_DEVICE(0x08fd, 0x0002), .driver_info = HCI_IGNORE },

	/* CSR BlueCore Bluetooth Sniffer */
	{ USB_DEVICE(0x0a12, 0x0002), .driver_info = HCI_SNIFFER },

	{ }	/* Terminating entry */
};

static struct _urb *_urb_alloc(int isoc, gfp_t gfp)
{
	struct _urb *_urb = kmalloc(sizeof(struct _urb) +
				sizeof(struct usb_iso_packet_descriptor) * isoc, gfp);
	if (_urb) {
		memset(_urb, 0, sizeof(*_urb));
		usb_init_urb(&_urb->urb);
	}
	return _urb;
}

static struct _urb *_urb_dequeue(struct _urb_queue *q)
{
	struct _urb *_urb = NULL;
	unsigned long flags;
	spin_lock_irqsave(&q->lock, flags);
	{
		struct list_head *head = &q->head;
		struct list_head *next = head->next;
		if (next != head) {
			_urb = list_entry(next, struct _urb, list);
			list_del(next); _urb->queue = NULL;
		}
	}
	spin_unlock_irqrestore(&q->lock, flags);
	return _urb;
}

static void hci_usb_rx_complete(struct urb *urb, struct pt_regs *regs);
static void hci_usb_tx_complete(struct urb *urb, struct pt_regs *regs);

#define __pending_tx(husb, type)  (&husb->pending_tx[type-1])
#define __pending_q(husb, type)   (&husb->pending_q[type-1])
#define __completed_q(husb, type) (&husb->completed_q[type-1])
#define __transmit_q(husb, type)  (&husb->transmit_q[type-1])
#define __reassembly(husb, type)  (husb->reassembly[type-1])

static inline struct _urb *__get_completed(struct hci_usb *husb, int type)
{
	return _urb_dequeue(__completed_q(husb, type)); 
}

#ifdef CONFIG_BT_HCIUSB_SCO
static void __fill_isoc_desc(struct urb *urb, int len, int mtu)
{
	int offset = 0, i;

	BT_DBG("len %d mtu %d", len, mtu);

	for (i=0; i < HCI_MAX_ISOC_FRAMES && len >= mtu; i++, offset += mtu, len -= mtu) {
		urb->iso_frame_desc[i].offset = offset;
		urb->iso_frame_desc[i].length = mtu;
		BT_DBG("desc %d offset %d len %d", i, offset, mtu);
	}
	if (len && i < HCI_MAX_ISOC_FRAMES) {
		urb->iso_frame_desc[i].offset = offset;
		urb->iso_frame_desc[i].length = len;
		BT_DBG("desc %d offset %d len %d", i, offset, len);
		i++;
	}
	urb->number_of_packets = i;
}
#endif

static int hci_usb_intr_rx_submit(struct hci_usb *husb)
{
	struct _urb *_urb;
	struct urb *urb;
	int err, pipe, interval, size;
	void *buf;

	BT_DBG("%s", husb->hdev->name);

	size = le16_to_cpu(husb->intr_in_ep->desc.wMaxPacketSize);

	buf = kmalloc(size, GFP_ATOMIC);
	if (!buf)
		return -ENOMEM;

	_urb = _urb_alloc(0, GFP_ATOMIC);
	if (!_urb) {
		kfree(buf);
		return -ENOMEM;
	}
	_urb->type = HCI_EVENT_PKT;
	_urb_queue_tail(__pending_q(husb, _urb->type), _urb);

	urb = &_urb->urb;
	pipe     = usb_rcvintpipe(husb->udev, husb->intr_in_ep->desc.bEndpointAddress);
	interval = husb->intr_in_ep->desc.bInterval;
	usb_fill_int_urb(urb, husb->udev, pipe, buf, size, hci_usb_rx_complete, husb, interval);
	
	err = usb_submit_urb(urb, GFP_ATOMIC);
	if (err) {
		BT_ERR("%s intr rx submit failed urb %p err %d",
				husb->hdev->name, urb, err);
		_urb_unlink(_urb);
		_urb_free(_urb);
		kfree(buf);
	}
	return err;
}

static int hci_usb_bulk_rx_submit(struct hci_usb *husb)
{
	struct _urb *_urb;
	struct urb *urb;
	int err, pipe, size = HCI_MAX_FRAME_SIZE;
	void *buf;

	buf = kmalloc(size, GFP_ATOMIC);
	if (!buf)
		return -ENOMEM;

	_urb = _urb_alloc(0, GFP_ATOMIC);
	if (!_urb) {
		kfree(buf);
		return -ENOMEM;
	}
	_urb->type = HCI_ACLDATA_PKT;
	_urb_queue_tail(__pending_q(husb, _urb->type), _urb);

	urb  = &_urb->urb;
	pipe = usb_rcvbulkpipe(husb->udev, husb->bulk_in_ep->desc.bEndpointAddress);
	usb_fill_bulk_urb(urb, husb->udev, pipe, buf, size, hci_usb_rx_complete, husb);
	urb->transfer_flags = 0;

	BT_DBG("%s urb %p", husb->hdev->name, urb);

	err = usb_submit_urb(urb, GFP_ATOMIC);
	if (err) {
		BT_ERR("%s bulk rx submit failed urb %p err %d",
				husb->hdev->name, urb, err);
		_urb_unlink(_urb);
		_urb_free(_urb);
		kfree(buf);
	}
	return err;
}

#ifdef CONFIG_BT_HCIUSB_SCO
static int hci_usb_isoc_rx_submit(struct hci_usb *husb)
{
	struct _urb *_urb;
	struct urb *urb;
	int err, mtu, size;
	void *buf;

	mtu  = le16_to_cpu(husb->isoc_in_ep->desc.wMaxPacketSize);
	size = mtu * HCI_MAX_ISOC_FRAMES;

	buf = kmalloc(size, GFP_ATOMIC);
	if (!buf)
		return -ENOMEM;

	_urb = _urb_alloc(HCI_MAX_ISOC_FRAMES, GFP_ATOMIC);
	if (!_urb) {
		kfree(buf);
		return -ENOMEM;
	}
	_urb->type = HCI_SCODATA_PKT;
	_urb_queue_tail(__pending_q(husb, _urb->type), _urb);

	urb = &_urb->urb;

	urb->context  = husb;
	urb->dev      = husb->udev;
	urb->pipe     = usb_rcvisocpipe(husb->udev, husb->isoc_in_ep->desc.bEndpointAddress);
	urb->complete = hci_usb_rx_complete;

	urb->interval = husb->isoc_in_ep->desc.bInterval;

	urb->transfer_buffer_length = size;
	urb->transfer_buffer = buf;
	urb->transfer_flags  = URB_ISO_ASAP;

	__fill_isoc_desc(urb, size, mtu);

	BT_DBG("%s urb %p", husb->hdev->name, urb);

	err = usb_submit_urb(urb, GFP_ATOMIC);
	if (err) {
		BT_ERR("%s isoc rx submit failed urb %p err %d",
				husb->hdev->name, urb, err);
		_urb_unlink(_urb);
		_urb_free(_urb);
		kfree(buf);
	}
	return err;
}
#endif

/* Initialize device */
static int hci_usb_open(struct hci_dev *hdev)
{
	struct hci_usb *husb = (struct hci_usb *) hdev->driver_data;
	int i, err;
	unsigned long flags;

	BT_DBG("%s", hdev->name);

	if (test_and_set_bit(HCI_RUNNING, &hdev->flags))
		return 0;

	write_lock_irqsave(&husb->completion_lock, flags);

	err = hci_usb_intr_rx_submit(husb);
	if (!err) {
		for (i = 0; i < HCI_MAX_BULK_RX; i++)
			hci_usb_bulk_rx_submit(husb);

#ifdef CONFIG_BT_HCIUSB_SCO
		if (husb->isoc_iface)
			for (i = 0; i < HCI_MAX_ISOC_RX; i++)
				hci_usb_isoc_rx_submit(husb);
#endif
	} else {
		clear_bit(HCI_RUNNING, &hdev->flags);
	}

	write_unlock_irqrestore(&husb->completion_lock, flags);
	return err;
}

/* Reset device */
static int hci_usb_flush(struct hci_dev *hdev)
{
	struct hci_usb *husb = (struct hci_usb *) hdev->driver_data;
	int i;

	BT_DBG("%s", hdev->name);

	for (i = 0; i < 4; i++)
		skb_queue_purge(&husb->transmit_q[i]);
	return 0;
}

static void hci_usb_unlink_urbs(struct hci_usb *husb)
{
	int i;

	BT_DBG("%s", husb->hdev->name);

	for (i = 0; i < 4; i++) {
		struct _urb *_urb;
		struct urb *urb;

		/* Kill pending requests */
		while ((_urb = _urb_dequeue(&husb->pending_q[i]))) {
			urb = &_urb->urb;
			BT_DBG("%s unlinking _urb %p type %d urb %p", 
					husb->hdev->name, _urb, _urb->type, urb);
			usb_kill_urb(urb);
			_urb_queue_tail(__completed_q(husb, _urb->type), _urb);
		}

		/* Release completed requests */
		while ((_urb = _urb_dequeue(&husb->completed_q[i]))) {
			urb = &_urb->urb;
			BT_DBG("%s freeing _urb %p type %d urb %p",
					husb->hdev->name, _urb, _urb->type, urb);
			kfree(urb->setup_packet);
			kfree(urb->transfer_buffer);
			_urb_free(_urb);
		}

		/* Release reassembly buffers */
		if (husb->reassembly[i]) {
			kfree_skb(husb->reassembly[i]);
			husb->reassembly[i] = NULL;
		}
	}
}

/* Close device */
static int hci_usb_close(struct hci_dev *hdev)
{
	struct hci_usb *husb = (struct hci_usb *) hdev->driver_data;
	unsigned long flags;

	if (!test_and_clear_bit(HCI_RUNNING, &hdev->flags))
		return 0;

	BT_DBG("%s", hdev->name);

	/* Synchronize with completion handlers */
	write_lock_irqsave(&husb->completion_lock, flags);
	write_unlock_irqrestore(&husb->completion_lock, flags);

	hci_usb_unlink_urbs(husb);
	hci_usb_flush(hdev);
	return 0;
}

static int __tx_submit(struct hci_usb *husb, struct _urb *_urb)
{
	struct urb *urb = &_urb->urb;
	int err;

	BT_DBG("%s urb %p type %d", husb->hdev->name, urb, _urb->type);

	_urb_queue_tail(__pending_q(husb, _urb->type), _urb);
	err = usb_submit_urb(urb, GFP_ATOMIC);
	if (err) {
		BT_ERR("%s tx submit failed urb %p type %d err %d",
				husb->hdev->name, urb, _urb->type, err);
		_urb_unlink(_urb);
		_urb_queue_tail(__completed_q(husb, _urb->type), _urb);
	} else
		atomic_inc(__pending_tx(husb, _urb->type));

	return err;
}

static inline int hci_usb_send_ctrl(struct hci_usb *husb, struct sk_buff *skb)
{
	struct _urb *_urb = __get_completed(husb, bt_cb(skb)->pkt_type);
	struct usb_ctrlrequest *dr;
	struct urb *urb;

	if (!_urb) {
		_urb = _urb_alloc(0, GFP_ATOMIC);
		if (!_urb)
			return -ENOMEM;
		_urb->type = bt_cb(skb)->pkt_type;

		dr = kmalloc(sizeof(*dr), GFP_ATOMIC);
		if (!dr) {
			_urb_free(_urb);
			return -ENOMEM;
		}
	} else
		dr = (void *) _urb->urb.setup_packet;

	dr->bRequestType = husb->ctrl_req;
	dr->bRequest = 0;
	dr->wIndex   = 0;
	dr->wValue   = 0;
	dr->wLength  = __cpu_to_le16(skb->len);

	urb = &_urb->urb;
	usb_fill_control_urb(urb, husb->udev, usb_sndctrlpipe(husb->udev, 0),
		(void *) dr, skb->data, skb->len, hci_usb_tx_complete, husb);

	BT_DBG("%s skb %p len %d", husb->hdev->name, skb, skb->len);
	
	_urb->priv = skb;
	return __tx_submit(husb, _urb);
}

static inline int hci_usb_send_bulk(struct hci_usb *husb, struct sk_buff *skb)
{
	struct _urb *_urb = __get_completed(husb, bt_cb(skb)->pkt_type);
	struct urb *urb;
	int pipe;

	if (!_urb) {
		_urb = _urb_alloc(0, GFP_ATOMIC);
		if (!_urb)
			return -ENOMEM;
		_urb->type = bt_cb(skb)->pkt_type;
	}

	urb  = &_urb->urb;
	pipe = usb_sndbulkpipe(husb->udev, husb->bulk_out_ep->desc.bEndpointAddress);
	usb_fill_bulk_urb(urb, husb->udev, pipe, skb->data, skb->len, 
			hci_usb_tx_complete, husb);
	urb->transfer_flags = URB_ZERO_PACKET;

	BT_DBG("%s skb %p len %d", husb->hdev->name, skb, skb->len);

	_urb->priv = skb;
	return __tx_submit(husb, _urb);
}

#ifdef CONFIG_BT_HCIUSB_SCO
static inline int hci_usb_send_isoc(struct hci_usb *husb, struct sk_buff *skb)
{
	struct _urb *_urb = __get_completed(husb, bt_cb(skb)->pkt_type);
	struct urb *urb;

	if (!_urb) {
		_urb = _urb_alloc(HCI_MAX_ISOC_FRAMES, GFP_ATOMIC);
		if (!_urb)
			return -ENOMEM;
		_urb->type = bt_cb(skb)->pkt_type;
	}

	BT_DBG("%s skb %p len %d", husb->hdev->name, skb, skb->len);

	urb = &_urb->urb;

	urb->context  = husb;
	urb->dev      = husb->udev;
	urb->pipe     = usb_sndisocpipe(husb->udev, husb->isoc_out_ep->desc.bEndpointAddress);
	urb->complete = hci_usb_tx_complete;
	urb->transfer_flags = URB_ISO_ASAP;

	urb->interval = husb->isoc_out_ep->desc.bInterval;

	urb->transfer_buffer = skb->data;
	urb->transfer_buffer_length = skb->len;

	__fill_isoc_desc(urb, skb->len, le16_to_cpu(husb->isoc_out_ep->desc.wMaxPacketSize));

	_urb->priv = skb;
	return __tx_submit(husb, _urb);
}
#endif

static void hci_usb_tx_process(struct hci_usb *husb)
{
	struct sk_buff_head *q;
	struct sk_buff *skb;

	BT_DBG("%s", husb->hdev->name);

	do {
		clear_bit(HCI_USB_TX_WAKEUP, &husb->state);

		/* Process command queue */
		q = __transmit_q(husb, HCI_COMMAND_PKT);
		if (!atomic_read(__pending_tx(husb, HCI_COMMAND_PKT)) &&
				(skb = skb_dequeue(q))) {
			if (hci_usb_send_ctrl(husb, skb) < 0)
				skb_queue_head(q, skb);
		}

#ifdef CONFIG_BT_HCIUSB_SCO
		/* Process SCO queue */
		q = __transmit_q(husb, HCI_SCODATA_PKT);
		if (atomic_read(__pending_tx(husb, HCI_SCODATA_PKT)) < HCI_MAX_ISOC_TX &&
				(skb = skb_dequeue(q))) {
			if (hci_usb_send_isoc(husb, skb) < 0)
				skb_queue_head(q, skb);
		}
#endif

		/* Process ACL queue */
		q = __transmit_q(husb, HCI_ACLDATA_PKT);
		while (atomic_read(__pending_tx(husb, HCI_ACLDATA_PKT)) < HCI_MAX_BULK_TX &&
				(skb = skb_dequeue(q))) {
			if (hci_usb_send_bulk(husb, skb) < 0) {
				skb_queue_head(q, skb);
				break;
			}
		}
	} while(test_bit(HCI_USB_TX_WAKEUP, &husb->state));
}

static inline void hci_usb_tx_wakeup(struct hci_usb *husb)
{
	/* Serialize TX queue processing to avoid data reordering */
	if (!test_and_set_bit(HCI_USB_TX_PROCESS, &husb->state)) {
		hci_usb_tx_process(husb);
		clear_bit(HCI_USB_TX_PROCESS, &husb->state);
	} else
		set_bit(HCI_USB_TX_WAKEUP, &husb->state);
}

/* Send frames from HCI layer */
static int hci_usb_send_frame(struct sk_buff *skb)
{
	struct hci_dev *hdev = (struct hci_dev *) skb->dev;
	struct hci_usb *husb;

	if (!hdev) {
		BT_ERR("frame for uknown device (hdev=NULL)");
		return -ENODEV;
	}

	if (!test_bit(HCI_RUNNING, &hdev->flags))
		return -EBUSY;

	BT_DBG("%s type %d len %d", hdev->name, bt_cb(skb)->pkt_type, skb->len);

	husb = (struct hci_usb *) hdev->driver_data;

	switch (bt_cb(skb)->pkt_type) {
	case HCI_COMMAND_PKT:
		hdev->stat.cmd_tx++;
		break;

	case HCI_ACLDATA_PKT:
		hdev->stat.acl_tx++;
		break;

#ifdef CONFIG_BT_HCIUSB_SCO
	case HCI_SCODATA_PKT:
		hdev->stat.sco_tx++;
		break;
#endif

	default:
		kfree_skb(skb);
		return 0;
	}

	read_lock(&husb->completion_lock);

	skb_queue_tail(__transmit_q(husb, bt_cb(skb)->pkt_type), skb);
	hci_usb_tx_wakeup(husb);

	read_unlock(&husb->completion_lock);
	return 0;
}

static inline int __recv_frame(struct hci_usb *husb, int type, void *data, int count)
{
	BT_DBG("%s type %d data %p count %d", husb->hdev->name, type, data, count);

	husb->hdev->stat.byte_rx += count;

	while (count) {
		struct sk_buff *skb = __reassembly(husb, type);
		struct { int expect; } *scb;
		int len = 0;
	
		if (!skb) {
			/* Start of the frame */

			switch (type) {
			case HCI_EVENT_PKT:
				if (count >= HCI_EVENT_HDR_SIZE) {
					struct hci_event_hdr *h = data;
					len = HCI_EVENT_HDR_SIZE + h->plen;
				} else
					return -EILSEQ;
				break;

			case HCI_ACLDATA_PKT:
				if (count >= HCI_ACL_HDR_SIZE) {
					struct hci_acl_hdr *h = data;
					len = HCI_ACL_HDR_SIZE + __le16_to_cpu(h->dlen);
				} else
					return -EILSEQ;
				break;
#ifdef CONFIG_BT_HCIUSB_SCO
			case HCI_SCODATA_PKT:
				if (count >= HCI_SCO_HDR_SIZE) {
					struct hci_sco_hdr *h = data;
					len = HCI_SCO_HDR_SIZE + h->dlen;
				} else
					return -EILSEQ;
				break;
#endif
			}
			BT_DBG("new packet len %d", len);

			skb = bt_skb_alloc(len, GFP_ATOMIC);
			if (!skb) {
				BT_ERR("%s no memory for the packet", husb->hdev->name);
				return -ENOMEM;
			}
			skb->dev = (void *) husb->hdev;
			bt_cb(skb)->pkt_type = type;
	
			__reassembly(husb, type) = skb;

			scb = (void *) skb->cb;
			scb->expect = len;
		} else {
			/* Continuation */
			scb = (void *) skb->cb;
			len = scb->expect;
		}

		len = min(len, count);
		
		memcpy(skb_put(skb, len), data, len);

		scb->expect -= len;
		if (!scb->expect) {
			/* Complete frame */
			__reassembly(husb, type) = NULL;
			bt_cb(skb)->pkt_type = type;
			hci_recv_frame(skb);
		}

		count -= len; data += len;
	}
	return 0;
}

static void hci_usb_rx_complete(struct urb *urb, struct pt_regs *regs)
{
	struct _urb *_urb = container_of(urb, struct _urb, urb);
	struct hci_usb *husb = (void *) urb->context;
	struct hci_dev *hdev = husb->hdev;
	int err, count = urb->actual_length;

	BT_DBG("%s urb %p type %d status %d count %d flags %x", hdev->name, urb,
			_urb->type, urb->status, count, urb->transfer_flags);

	read_lock(&husb->completion_lock);

	if (!test_bit(HCI_RUNNING, &hdev->flags))
		goto unlock;

	if (urb->status || !count)
		goto resubmit;

	if (_urb->type == HCI_SCODATA_PKT) {
#ifdef CONFIG_BT_HCIUSB_SCO
		int i;
		for (i=0; i < urb->number_of_packets; i++) {
			BT_DBG("desc %d status %d offset %d len %d", i,
					urb->iso_frame_desc[i].status,
					urb->iso_frame_desc[i].offset,
					urb->iso_frame_desc[i].actual_length);
	
			if (!urb->iso_frame_desc[i].status)
				__recv_frame(husb, _urb->type, 
					urb->transfer_buffer + urb->iso_frame_desc[i].offset,
					urb->iso_frame_desc[i].actual_length);
		}
#else
		;
#endif
	} else {
		err = __recv_frame(husb, _urb->type, urb->transfer_buffer, count);
		if (err < 0) { 
			BT_ERR("%s corrupted packet: type %d count %d",
					husb->hdev->name, _urb->type, count);
			hdev->stat.err_rx++;
		}
	}

resubmit:
	urb->dev = husb->udev;
	err = usb_submit_urb(urb, GFP_ATOMIC);
	BT_DBG("%s urb %p type %d resubmit status %d", hdev->name, urb,
			_urb->type, err);

unlock:
	read_unlock(&husb->completion_lock);
}

static void hci_usb_tx_complete(struct urb *urb, struct pt_regs *regs)
{
	struct _urb *_urb = container_of(urb, struct _urb, urb);
	struct hci_usb *husb = (void *) urb->context;
	struct hci_dev *hdev = husb->hdev;

	BT_DBG("%s urb %p status %d flags %x", hdev->name, urb,
			urb->status, urb->transfer_flags);

	atomic_dec(__pending_tx(husb, _urb->type));

	urb->transfer_buffer = NULL;
	kfree_skb((struct sk_buff *) _urb->priv);

	if (!test_bit(HCI_RUNNING, &hdev->flags))
		return;

	if (!urb->status)
		hdev->stat.byte_tx += urb->transfer_buffer_length;
	else
		hdev->stat.err_tx++;

	read_lock(&husb->completion_lock);

	_urb_unlink(_urb);
	_urb_queue_tail(__completed_q(husb, _urb->type), _urb);

	hci_usb_tx_wakeup(husb);

	read_unlock(&husb->completion_lock);
}

static void hci_usb_destruct(struct hci_dev *hdev)
{
	struct hci_usb *husb = (struct hci_usb *) hdev->driver_data;

	BT_DBG("%s", hdev->name);

	kfree(husb);
}

static void hci_usb_notify(struct hci_dev *hdev, unsigned int evt)
{
	BT_DBG("%s evt %d", hdev->name, evt);
}

static int hci_usb_probe(struct usb_interface *intf, const struct usb_device_id *id)
{
	struct usb_device *udev = interface_to_usbdev(intf);
	struct usb_host_endpoint *bulk_out_ep = NULL;
	struct usb_host_endpoint *bulk_in_ep = NULL;
	struct usb_host_endpoint *intr_in_ep = NULL;
	struct usb_host_endpoint  *ep;
	struct usb_host_interface *uif;
	struct usb_interface *isoc_iface;
	struct hci_usb *husb;
	struct hci_dev *hdev;
	int i, e, size, isoc_ifnum, isoc_alts;

	BT_DBG("udev %p intf %p", udev, intf);

	if (!id->driver_info) {
		const struct usb_device_id *match;
		match = usb_match_id(intf, blacklist_ids);
		if (match)
			id = match;
	}

	if (ignore || id->driver_info & HCI_IGNORE)
		return -ENODEV;

	if (ignore_dga && id->driver_info & HCI_DIGIANSWER)
		return -ENODEV;

	if (ignore_csr && id->driver_info & HCI_CSR)
		return -ENODEV;

	if (ignore_sniffer && id->driver_info & HCI_SNIFFER)
		return -ENODEV;

	if (intf->cur_altsetting->desc.bInterfaceNumber > 0)
		return -ENODEV;

	/* Find endpoints that we need */
	uif = intf->cur_altsetting;
	for (e = 0; e < uif->desc.bNumEndpoints; e++) {
		ep = &uif->endpoint[e];

		switch (ep->desc.bmAttributes & USB_ENDPOINT_XFERTYPE_MASK) {
		case USB_ENDPOINT_XFER_INT:
			if (ep->desc.bEndpointAddress & USB_DIR_IN)
				intr_in_ep = ep;
			break;

		case USB_ENDPOINT_XFER_BULK:
			if (ep->desc.bEndpointAddress & USB_DIR_IN)
				bulk_in_ep  = ep;
			else
				bulk_out_ep = ep;
			break;
		}
	}

	if (!bulk_in_ep || !bulk_out_ep || !intr_in_ep) {
		BT_DBG("Bulk endpoints not found");
		goto done;
	}

	if (!(husb = kzalloc(sizeof(struct hci_usb), GFP_KERNEL))) {
		BT_ERR("Can't allocate: control structure");
		goto done;
	}

	husb->udev = udev;
	husb->bulk_out_ep = bulk_out_ep;
	husb->bulk_in_ep  = bulk_in_ep;
	husb->intr_in_ep  = intr_in_ep;

	if (id->driver_info & HCI_DIGIANSWER)
		husb->ctrl_req = USB_TYPE_VENDOR;
	else
		husb->ctrl_req = USB_TYPE_CLASS;

	/* Find isochronous endpoints that we can use */
	size = 0; 
	isoc_iface = NULL;
	isoc_alts  = 0;
	isoc_ifnum = 1;

#ifdef CONFIG_BT_HCIUSB_SCO
	if (isoc && !(id->driver_info & (HCI_BROKEN_ISOC | HCI_SNIFFER)))
		isoc_iface = usb_ifnum_to_if(udev, isoc_ifnum);

	if (isoc_iface) {
		int a;
		struct usb_host_endpoint *isoc_out_ep = NULL;
		struct usb_host_endpoint *isoc_in_ep = NULL;

		for (a = 0; a < isoc_iface->num_altsetting; a++) {
			uif = &isoc_iface->altsetting[a];
			for (e = 0; e < uif->desc.bNumEndpoints; e++) {
				ep = &uif->endpoint[e];

				switch (ep->desc.bmAttributes & USB_ENDPOINT_XFERTYPE_MASK) {
				case USB_ENDPOINT_XFER_ISOC:
					if (le16_to_cpu(ep->desc.wMaxPacketSize) < size ||
							uif->desc.bAlternateSetting != isoc)
						break;
					size = le16_to_cpu(ep->desc.wMaxPacketSize);

					isoc_alts = uif->desc.bAlternateSetting;

					if (ep->desc.bEndpointAddress & USB_DIR_IN)
						isoc_in_ep  = ep;
					else
						isoc_out_ep = ep;
					break;
				}
			}
		}

		if (!isoc_in_ep || !isoc_out_ep)
			BT_DBG("Isoc endpoints not found");
		else {
			BT_DBG("isoc ifnum %d alts %d", isoc_ifnum, isoc_alts);
			if (usb_driver_claim_interface(&hci_usb_driver, isoc_iface, husb) != 0)
				BT_ERR("Can't claim isoc interface");
			else if (usb_set_interface(udev, isoc_ifnum, isoc_alts)) {
				BT_ERR("Can't set isoc interface settings");
				husb->isoc_iface = isoc_iface;
				usb_driver_release_interface(&hci_usb_driver, isoc_iface);
				husb->isoc_iface = NULL;
			} else {
				husb->isoc_iface  = isoc_iface;
				husb->isoc_in_ep  = isoc_in_ep;
				husb->isoc_out_ep = isoc_out_ep;
			}
		}
	}
#endif

	rwlock_init(&husb->completion_lock);

	for (i = 0; i < 4; i++) {
		skb_queue_head_init(&husb->transmit_q[i]);
		_urb_queue_init(&husb->pending_q[i]);
		_urb_queue_init(&husb->completed_q[i]);
	}

	/* Initialize and register HCI device */
	hdev = hci_alloc_dev();
	if (!hdev) {
		BT_ERR("Can't allocate HCI device");
		goto probe_error;
	}

	husb->hdev = hdev;

	hdev->type = HCI_USB;
	hdev->driver_data = husb;
	SET_HCIDEV_DEV(hdev, &intf->dev);

	hdev->open     = hci_usb_open;
	hdev->close    = hci_usb_close;
	hdev->flush    = hci_usb_flush;
	hdev->send     = hci_usb_send_frame;
	hdev->destruct = hci_usb_destruct;
	hdev->notify   = hci_usb_notify;

	hdev->owner = THIS_MODULE;

	if (reset || id->driver_info & HCI_RESET)
		set_bit(HCI_QUIRK_RESET_ON_INIT, &hdev->quirks);

	if (id->driver_info & HCI_SNIFFER) {
		if (le16_to_cpu(udev->descriptor.bcdDevice) > 0x997)
			set_bit(HCI_QUIRK_RAW_DEVICE, &hdev->quirks);
	}

	if (id->driver_info & HCI_BCM92035) {
		unsigned char cmd[] = { 0x3b, 0xfc, 0x01, 0x00 };
		struct sk_buff *skb;

		skb = bt_skb_alloc(sizeof(cmd), GFP_KERNEL);
		if (skb) {
			memcpy(skb_put(skb, sizeof(cmd)), cmd, sizeof(cmd));
			skb_queue_tail(&hdev->driver_init, skb);
		}
	}

	if (hci_register_dev(hdev) < 0) {
		BT_ERR("Can't register HCI device");
		hci_free_dev(hdev);
		goto probe_error;
	}

	usb_set_intfdata(intf, husb);
	return 0;

probe_error:
	if (husb->isoc_iface)
		usb_driver_release_interface(&hci_usb_driver, husb->isoc_iface);
	kfree(husb);

done:
	return -EIO;
}

static void hci_usb_disconnect(struct usb_interface *intf)
{
	struct hci_usb *husb = usb_get_intfdata(intf);
	struct hci_dev *hdev;

	if (!husb || intf == husb->isoc_iface)
		return;

	usb_set_intfdata(intf, NULL);
	hdev = husb->hdev;

	BT_DBG("%s", hdev->name);

	hci_usb_close(hdev);

	if (husb->isoc_iface)
		usb_driver_release_interface(&hci_usb_driver, husb->isoc_iface);

	if (hci_unregister_dev(hdev) < 0)
		BT_ERR("Can't unregister HCI device %s", hdev->name);

	hci_free_dev(hdev);
}

static struct usb_driver hci_usb_driver = {
	.owner		= THIS_MODULE,
	.name		= "hci_usb",
	.probe		= hci_usb_probe,
	.disconnect	= hci_usb_disconnect,
	.id_table	= bluetooth_ids,
};

static int __init hci_usb_init(void)
{
	int err;

	BT_INFO("HCI USB driver ver %s", VERSION);

	if ((err = usb_register(&hci_usb_driver)) < 0)
		BT_ERR("Failed to register HCI USB driver");

	return err;
}

static void __exit hci_usb_exit(void)
{
	usb_deregister(&hci_usb_driver);
}

module_init(hci_usb_init);
module_exit(hci_usb_exit);

module_param(ignore, bool, 0644);
MODULE_PARM_DESC(ignore, "Ignore devices from the matching table");

module_param(ignore_dga, bool, 0644);
MODULE_PARM_DESC(ignore_dga, "Ignore devices with id 08fd:0001");

module_param(ignore_csr, bool, 0644);
MODULE_PARM_DESC(ignore_csr, "Ignore devices with id 0a12:0001");

module_param(ignore_sniffer, bool, 0644);
MODULE_PARM_DESC(ignore_sniffer, "Ignore devices with id 0a12:0002");

module_param(reset, bool, 0644);
MODULE_PARM_DESC(reset, "Send HCI reset command on initialization");

#ifdef CONFIG_BT_HCIUSB_SCO
module_param(isoc, int, 0644);
MODULE_PARM_DESC(isoc, "Set isochronous transfers for SCO over HCI support");
#endif

MODULE_AUTHOR("Maxim Krasnyansky <maxk@qualcomm.com>, Marcel Holtmann <marcel@holtmann.org>");
MODULE_DESCRIPTION("Bluetooth HCI USB driver ver " VERSION);
MODULE_VERSION(VERSION);
MODULE_LICENSE("GPL");
