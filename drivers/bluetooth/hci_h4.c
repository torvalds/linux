/*
 *
 *  Bluetooth HCI UART driver
 *
 *  Copyright (C) 2000-2001  Qualcomm Incorporated
 *  Copyright (C) 2002-2003  Maxim Krasnyansky <maxk@qualcomm.com>
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

#include <linux/module.h>

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/fcntl.h>
#include <linux/interrupt.h>
#include <linux/ptrace.h>
#include <linux/poll.h>

#include <linux/slab.h>
#include <linux/tty.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/signal.h>
#include <linux/ioctl.h>
#include <linux/skbuff.h>

#include <net/bluetooth/bluetooth.h>
#include <net/bluetooth/hci_core.h>

#include "hci_uart.h"

#define VERSION "1.2"

struct h4_struct {
	struct sk_buff *rx_skb;
	struct sk_buff_head txq;
};

/* Initialize protocol */
static int h4_open(struct hci_uart *hu)
{
	struct h4_struct *h4;

	BT_DBG("hu %p", hu);

	h4 = kzalloc(sizeof(*h4), GFP_KERNEL);
	if (!h4)
		return -ENOMEM;

	skb_queue_head_init(&h4->txq);

	hu->priv = h4;
	return 0;
}

/* Flush protocol data */
static int h4_flush(struct hci_uart *hu)
{
	struct h4_struct *h4 = hu->priv;

	BT_DBG("hu %p", hu);

	skb_queue_purge(&h4->txq);

	return 0;
}

/* Close protocol */
static int h4_close(struct hci_uart *hu)
{
	struct h4_struct *h4 = hu->priv;

	hu->priv = NULL;

	BT_DBG("hu %p", hu);

	skb_queue_purge(&h4->txq);

	kfree_skb(h4->rx_skb);

	hu->priv = NULL;
	kfree(h4);

	return 0;
}

/* Enqueue frame for transmittion (padding, crc, etc) */
static int h4_enqueue(struct hci_uart *hu, struct sk_buff *skb)
{
	struct h4_struct *h4 = hu->priv;

	BT_DBG("hu %p skb %p", hu, skb);

	/* Prepend skb with frame type */
	memcpy(skb_push(skb, 1), &bt_cb(skb)->pkt_type, 1);
	skb_queue_tail(&h4->txq, skb);

	return 0;
}

/* Recv data */
static int h4_recv(struct hci_uart *hu, const void *data, int count)
{
	struct h4_struct *h4 = hu->priv;

	if (!test_bit(HCI_UART_REGISTERED, &hu->flags))
		return -EUNATCH;

	h4->rx_skb = h4_recv_buf(hu->hdev, h4->rx_skb, data, count);
	if (IS_ERR(h4->rx_skb)) {
		int err = PTR_ERR(h4->rx_skb);
		BT_ERR("%s: Frame reassembly failed (%d)", hu->hdev->name, err);
		return err;
	}

	return count;
}

static struct sk_buff *h4_dequeue(struct hci_uart *hu)
{
	struct h4_struct *h4 = hu->priv;
	return skb_dequeue(&h4->txq);
}

static struct hci_uart_proto h4p = {
	.id		= HCI_UART_H4,
	.open		= h4_open,
	.close		= h4_close,
	.recv		= h4_recv,
	.enqueue	= h4_enqueue,
	.dequeue	= h4_dequeue,
	.flush		= h4_flush,
};

int __init h4_init(void)
{
	int err = hci_uart_register_proto(&h4p);

	if (!err)
		BT_INFO("HCI H4 protocol initialized");
	else
		BT_ERR("HCI H4 protocol registration failed");

	return err;
}

int __exit h4_deinit(void)
{
	return hci_uart_unregister_proto(&h4p);
}

struct sk_buff *h4_recv_buf(struct hci_dev *hdev, struct sk_buff *skb,
			    const unsigned char *buffer, int count)
{
	while (count) {
		int len;

		if (!skb) {
			switch (buffer[0]) {
			case HCI_ACLDATA_PKT:
				skb = bt_skb_alloc(HCI_MAX_FRAME_SIZE,
						   GFP_ATOMIC);
				if (!skb)
					return ERR_PTR(-ENOMEM);

				bt_cb(skb)->pkt_type = HCI_ACLDATA_PKT;
				bt_cb(skb)->expect = HCI_ACL_HDR_SIZE;
				break;
			case HCI_SCODATA_PKT:
				skb = bt_skb_alloc(HCI_MAX_SCO_SIZE,
						   GFP_ATOMIC);
				if (!skb)
					return ERR_PTR(-ENOMEM);

				bt_cb(skb)->pkt_type = HCI_SCODATA_PKT;
				bt_cb(skb)->expect = HCI_SCO_HDR_SIZE;
				break;
			case HCI_EVENT_PKT:
				skb = bt_skb_alloc(HCI_MAX_EVENT_SIZE,
						   GFP_ATOMIC);
				if (!skb)
					return ERR_PTR(-ENOMEM);

				bt_cb(skb)->pkt_type = HCI_EVENT_PKT;
				bt_cb(skb)->expect = HCI_EVENT_HDR_SIZE;
				break;
			default:
				return ERR_PTR(-EILSEQ);
			}

			count -= 1;
			buffer += 1;
		}

		len = min_t(uint, bt_cb(skb)->expect, count);
		memcpy(skb_put(skb, len), buffer, len);

		count -= len;
		buffer += len;
		bt_cb(skb)->expect -= len;

		switch (bt_cb(skb)->pkt_type) {
		case HCI_ACLDATA_PKT:
			if (skb->len == HCI_ACL_HDR_SIZE) {
				__le16 dlen = hci_acl_hdr(skb)->dlen;

				/* Complete ACL header */
				bt_cb(skb)->expect = __le16_to_cpu(dlen);

				if (skb_tailroom(skb) < bt_cb(skb)->expect) {
					kfree_skb(skb);
					return ERR_PTR(-EMSGSIZE);
				}
			}
			break;
		case HCI_SCODATA_PKT:
			if (skb->len == HCI_SCO_HDR_SIZE) {
				/* Complete SCO header */
				bt_cb(skb)->expect = hci_sco_hdr(skb)->dlen;

				if (skb_tailroom(skb) < bt_cb(skb)->expect) {
					kfree_skb(skb);
					return ERR_PTR(-EMSGSIZE);
				}
			}
			break;
		case HCI_EVENT_PKT:
			if (skb->len == HCI_EVENT_HDR_SIZE) {
				/* Complete event header */
				bt_cb(skb)->expect = hci_event_hdr(skb)->plen;

				if (skb_tailroom(skb) < bt_cb(skb)->expect) {
					kfree_skb(skb);
					return ERR_PTR(-EMSGSIZE);
				}
			}
			break;
		}

		if (bt_cb(skb)->expect == 0) {
			/* Complete frame */
			hci_recv_frame(hdev, skb);
			skb = NULL;
		}
	}

	return skb;
}
