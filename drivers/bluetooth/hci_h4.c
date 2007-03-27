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

#ifndef CONFIG_BT_HCIUART_DEBUG
#undef  BT_DBG
#define BT_DBG( A... )
#endif

#define VERSION "1.2"

struct h4_struct {
	unsigned long rx_state;
	unsigned long rx_count;
	struct sk_buff *rx_skb;
	struct sk_buff_head txq;
};

/* H4 receiver States */
#define H4_W4_PACKET_TYPE	0
#define H4_W4_EVENT_HDR		1
#define H4_W4_ACL_HDR		2
#define H4_W4_SCO_HDR		3
#define H4_W4_DATA		4

/* Initialize protocol */
static int h4_open(struct hci_uart *hu)
{
	struct h4_struct *h4;

	BT_DBG("hu %p", hu);

	h4 = kzalloc(sizeof(*h4), GFP_ATOMIC);
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

	if (h4->rx_skb)
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

static inline int h4_check_data_len(struct h4_struct *h4, int len)
{
	register int room = skb_tailroom(h4->rx_skb);

	BT_DBG("len %d room %d", len, room);

	if (!len) {
		hci_recv_frame(h4->rx_skb);
	} else if (len > room) {
		BT_ERR("Data length is too large");
		kfree_skb(h4->rx_skb);
	} else {
		h4->rx_state = H4_W4_DATA;
		h4->rx_count = len;
		return len;
	}

	h4->rx_state = H4_W4_PACKET_TYPE;
	h4->rx_skb   = NULL;
	h4->rx_count = 0;

	return 0;
}

/* Recv data */
static int h4_recv(struct hci_uart *hu, void *data, int count)
{
	struct h4_struct *h4 = hu->priv;
	register char *ptr;
	struct hci_event_hdr *eh;
	struct hci_acl_hdr   *ah;
	struct hci_sco_hdr   *sh;
	register int len, type, dlen;

	BT_DBG("hu %p count %d rx_state %ld rx_count %ld", 
			hu, count, h4->rx_state, h4->rx_count);

	ptr = data;
	while (count) {
		if (h4->rx_count) {
			len = min_t(unsigned int, h4->rx_count, count);
			memcpy(skb_put(h4->rx_skb, len), ptr, len);
			h4->rx_count -= len; count -= len; ptr += len;

			if (h4->rx_count)
				continue;

			switch (h4->rx_state) {
			case H4_W4_DATA:
				BT_DBG("Complete data");

				hci_recv_frame(h4->rx_skb);

				h4->rx_state = H4_W4_PACKET_TYPE;
				h4->rx_skb = NULL;
				continue;

			case H4_W4_EVENT_HDR:
				eh = hci_event_hdr(h4->rx_skb);

				BT_DBG("Event header: evt 0x%2.2x plen %d", eh->evt, eh->plen);

				h4_check_data_len(h4, eh->plen);
				continue;

			case H4_W4_ACL_HDR:
				ah = hci_acl_hdr(h4->rx_skb);
				dlen = __le16_to_cpu(ah->dlen);

				BT_DBG("ACL header: dlen %d", dlen);

				h4_check_data_len(h4, dlen);
				continue;

			case H4_W4_SCO_HDR:
				sh = hci_sco_hdr(h4->rx_skb);

				BT_DBG("SCO header: dlen %d", sh->dlen);

				h4_check_data_len(h4, sh->dlen);
				continue;
			}
		}

		/* H4_W4_PACKET_TYPE */
		switch (*ptr) {
		case HCI_EVENT_PKT:
			BT_DBG("Event packet");
			h4->rx_state = H4_W4_EVENT_HDR;
			h4->rx_count = HCI_EVENT_HDR_SIZE;
			type = HCI_EVENT_PKT;
			break;

		case HCI_ACLDATA_PKT:
			BT_DBG("ACL packet");
			h4->rx_state = H4_W4_ACL_HDR;
			h4->rx_count = HCI_ACL_HDR_SIZE;
			type = HCI_ACLDATA_PKT;
			break;

		case HCI_SCODATA_PKT:
			BT_DBG("SCO packet");
			h4->rx_state = H4_W4_SCO_HDR;
			h4->rx_count = HCI_SCO_HDR_SIZE;
			type = HCI_SCODATA_PKT;
			break;

		default:
			BT_ERR("Unknown HCI packet type %2.2x", (__u8)*ptr);
			hu->hdev->stat.err_rx++;
			ptr++; count--;
			continue;
		};

		ptr++; count--;

		/* Allocate packet */
		h4->rx_skb = bt_skb_alloc(HCI_MAX_FRAME_SIZE, GFP_ATOMIC);
		if (!h4->rx_skb) {
			BT_ERR("Can't allocate mem for new packet");
			h4->rx_state = H4_W4_PACKET_TYPE;
			h4->rx_count = 0;
			return 0;
		}

		h4->rx_skb->dev = (void *) hu->hdev;
		bt_cb(h4->rx_skb)->pkt_type = type;
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

int h4_init(void)
{
	int err = hci_uart_register_proto(&h4p);

	if (!err)
		BT_INFO("HCI H4 protocol initialized");
	else
		BT_ERR("HCI H4 protocol registration failed");

	return err;
}

int h4_deinit(void)
{
	return hci_uart_unregister_proto(&h4p);
}
