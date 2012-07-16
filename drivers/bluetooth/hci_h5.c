/*
 *
 *  Bluetooth HCI Three-wire UART driver
 *
 *  Copyright (C) 2012  Intel Corporation
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
#include <linux/errno.h>
#include <linux/skbuff.h>

#include <net/bluetooth/bluetooth.h>
#include <net/bluetooth/hci_core.h>

#include "hci_uart.h"

#define HCI_3WIRE_ACK_PKT	0
#define HCI_3WIRE_LINK_PKT	15

#define H5_TXWINSIZE	4

#define H5_ACK_TIMEOUT	msecs_to_jiffies(250)

/*
 * Maximum Three-wire packet:
 *     4 byte header + max value for 12-bit length + 2 bytes for CRC
 */
#define H5_MAX_LEN (4 + 0xfff + 2)

#define SLIP_DELIMITER	0xc0
#define SLIP_ESC	0xdb
#define SLIP_ESC_DELIM	0xdc
#define SLIP_ESC_ESC	0xdd

struct h5 {
	struct sk_buff_head	unack;		/* Unack'ed packets queue */
	struct sk_buff_head	rel;		/* Reliable packets queue */
	struct sk_buff_head	unrel;		/* Unreliable packets queue */

	struct sk_buff		*rx_skb;	/* Receive buffer */
	size_t			rx_pending;	/* Expecting more bytes */
	bool			rx_esc;		/* SLIP escape mode */

	int			(*rx_func) (struct hci_uart *hu, u8 c);

	struct timer_list	timer;		/* Retransmission timer */

	bool			txack_req;

	u8			next_ack;
	u8			next_seq;
};

static void h5_reset_rx(struct h5 *h5);

static void h5_timed_event(unsigned long arg)
{
	struct hci_uart *hu = (struct hci_uart *) arg;
	struct h5 *h5 = hu->priv;
	struct sk_buff *skb;
	unsigned long flags;

	BT_DBG("hu %p retransmitting %u pkts", hu, h5->unack.qlen);

	spin_lock_irqsave_nested(&h5->unack.lock, flags, SINGLE_DEPTH_NESTING);

	while ((skb = __skb_dequeue_tail(&h5->unack)) != NULL) {
		h5->next_seq = (h5->next_seq - 1) & 0x07;
		skb_queue_head(&h5->rel, skb);
	}

	spin_unlock_irqrestore(&h5->unack.lock, flags);

	hci_uart_tx_wakeup(hu);
}

static int h5_open(struct hci_uart *hu)
{
	struct h5 *h5;

	BT_DBG("hu %p", hu);

	h5 = kzalloc(sizeof(*h5), GFP_KERNEL);
	if (!h5)
		return -ENOMEM;

	hu->priv = h5;

	skb_queue_head_init(&h5->unack);
	skb_queue_head_init(&h5->rel);
	skb_queue_head_init(&h5->unrel);

	h5_reset_rx(h5);

	init_timer(&h5->timer);
	h5->timer.function = h5_timed_event;
	h5->timer.data = (unsigned long) hu;

	return 0;
}

static int h5_close(struct hci_uart *hu)
{
	struct h5 *h5 = hu->priv;

	skb_queue_purge(&h5->unack);
	skb_queue_purge(&h5->rel);
	skb_queue_purge(&h5->unrel);

	del_timer(&h5->timer);

	kfree(h5);

	return 0;
}

static void h5_handle_internal_rx(struct hci_uart *hu)
{
	BT_DBG("%s", hu->hdev->name);
}

static void h5_complete_rx_pkt(struct hci_uart *hu)
{
	struct h5 *h5 = hu->priv;
	u8 pkt_type;

	BT_DBG("%s", hu->hdev->name);

	pkt_type = h5->rx_skb->data[1] & 0x0f;

	switch (pkt_type) {
	case HCI_EVENT_PKT:
	case HCI_ACLDATA_PKT:
	case HCI_SCODATA_PKT:
		bt_cb(h5->rx_skb)->pkt_type = pkt_type;

		/* Remove Three-wire header */
		skb_pull(h5->rx_skb, 4);

		hci_recv_frame(h5->rx_skb);
		h5->rx_skb = NULL;

		break;

	default:
		h5_handle_internal_rx(hu);
		break;
	}

	h5_reset_rx(h5);
}

static int h5_rx_crc(struct hci_uart *hu, unsigned char c)
{
	struct h5 *h5 = hu->priv;

	BT_DBG("%s 0x%02hhx", hu->hdev->name, c);

	h5_complete_rx_pkt(hu);
	h5_reset_rx(h5);

	return 0;
}

static int h5_rx_payload(struct hci_uart *hu, unsigned char c)
{
	struct h5 *h5 = hu->priv;
	const unsigned char *hdr = h5->rx_skb->data;

	BT_DBG("%s 0x%02hhx", hu->hdev->name, c);

	if ((hdr[0] >> 4) & 0x01) {
		h5->rx_func = h5_rx_crc;
		h5->rx_pending = 2;
	} else {
		h5_complete_rx_pkt(hu);
		h5_reset_rx(h5);
	}

	return 0;
}

static int h5_rx_3wire_hdr(struct hci_uart *hu, unsigned char c)
{
	struct h5 *h5 = hu->priv;
	const unsigned char *hdr = h5->rx_skb->data;

	BT_DBG("%s 0x%02hhx", hu->hdev->name, c);

	if (((hdr[0] + hdr[1] + hdr[2] + hdr[3]) & 0xff) != 0xff) {
		BT_ERR("Invalid header checksum");
		h5_reset_rx(h5);
		return 0;
	}

	h5->rx_func = h5_rx_payload;
	h5->rx_pending = ((hdr[1] >> 4) & 0xff) + (hdr[2] << 4);

	return 0;
}

static int h5_rx_pkt_start(struct hci_uart *hu, unsigned char c)
{
	struct h5 *h5 = hu->priv;

	BT_DBG("%s 0x%02hhx", hu->hdev->name, c);

	if (c == SLIP_DELIMITER)
		return 1;

	h5->rx_func = h5_rx_3wire_hdr;
	h5->rx_pending = 4;

	h5->rx_skb = bt_skb_alloc(H5_MAX_LEN, GFP_ATOMIC);
	if (!h5->rx_skb) {
		BT_ERR("Can't allocate mem for new packet");
		h5_reset_rx(h5);
		return -ENOMEM;
	}

	h5->rx_skb->dev = (void *) hu->hdev;

	return 0;
}

static int h5_rx_delimiter(struct hci_uart *hu, unsigned char c)
{
	struct h5 *h5 = hu->priv;

	BT_DBG("%s 0x%02hhx", hu->hdev->name, c);

	if (c == SLIP_DELIMITER)
		h5->rx_func = h5_rx_pkt_start;

	return 1;
}

static void h5_unslip_one_byte(struct h5 *h5, unsigned char c)
{
	const u8 delim = SLIP_DELIMITER, esc = SLIP_ESC;
	const u8 *byte = &c;

	if (!h5->rx_esc && c == SLIP_ESC) {
		h5->rx_esc = true;
		return;
	}

	if (h5->rx_esc) {
		switch (c) {
		case SLIP_ESC_DELIM:
			byte = &delim;
			break;
		case SLIP_ESC_ESC:
			byte = &esc;
			break;
		default:
			BT_ERR("Invalid esc byte 0x%02hhx", c);
			h5_reset_rx(h5);
			return;
		}

		h5->rx_esc = false;
	}

	memcpy(skb_put(h5->rx_skb, 1), byte, 1);
	h5->rx_pending--;

	BT_DBG("unsliped 0x%02hhx", *byte);
}

static void h5_reset_rx(struct h5 *h5)
{
	if (h5->rx_skb) {
		kfree_skb(h5->rx_skb);
		h5->rx_skb = NULL;
	}

	h5->rx_func = h5_rx_delimiter;
	h5->rx_pending = 0;
	h5->rx_esc = false;
}

static int h5_recv(struct hci_uart *hu, void *data, int count)
{
	struct h5 *h5 = hu->priv;
	unsigned char *ptr = data;

	BT_DBG("%s count %d", hu->hdev->name, count);

	while (count > 0) {
		int processed;

		if (h5->rx_pending > 0) {
			if (*ptr == SLIP_DELIMITER) {
				BT_ERR("Too short H5 packet");
				h5_reset_rx(h5);
				continue;
			}

			h5_unslip_one_byte(h5, *ptr);

			ptr++; count--;
			continue;
		}

		processed = h5->rx_func(hu, *ptr);
		if (processed < 0)
			return processed;

		ptr += processed;
		count -= processed;
	}

	return 0;
}

static int h5_enqueue(struct hci_uart *hu, struct sk_buff *skb)
{
	struct h5 *h5 = hu->priv;

	if (skb->len > 0xfff) {
		BT_ERR("Packet too long (%u bytes)", skb->len);
		kfree_skb(skb);
		return 0;
	}

	switch (bt_cb(skb)->pkt_type) {
	case HCI_ACLDATA_PKT:
	case HCI_COMMAND_PKT:
		skb_queue_tail(&h5->rel, skb);
		break;

	case HCI_SCODATA_PKT:
		skb_queue_tail(&h5->unrel, skb);
		break;

	default:
		BT_ERR("Unknown packet type %u", bt_cb(skb)->pkt_type);
		kfree_skb(skb);
		break;
	}

	return 0;
}

static void h5_slip_delim(struct sk_buff *skb)
{
	const char delim = SLIP_DELIMITER;

	memcpy(skb_put(skb, 1), &delim, 1);
}

static void h5_slip_one_byte(struct sk_buff *skb, u8 c)
{
	const char esc_delim[2] = { SLIP_ESC, SLIP_ESC_DELIM };
	const char esc_esc[2] = { SLIP_ESC, SLIP_ESC_ESC };

	switch (c) {
	case SLIP_DELIMITER:
		memcpy(skb_put(skb, 2), &esc_delim, 2);
		break;
	case SLIP_ESC:
		memcpy(skb_put(skb, 2), &esc_esc, 2);
		break;
	default:
		memcpy(skb_put(skb, 1), &c, 1);
	}
}

static struct sk_buff *h5_build_pkt(struct h5 *h5, bool rel, u8 pkt_type,
				    const u8 *data, size_t len)
{
	struct sk_buff *nskb;
	u8 hdr[4];
	int i;

	/*
	 * Max len of packet: (original len + 4 (H5 hdr) + 2 (crc)) * 2
	 * (because bytes 0xc0 and 0xdb are escaped, worst case is when
	 * the packet is all made of 0xc0 and 0xdb) + 2 (0xc0
	 * delimiters at start and end).
	 */
	nskb = alloc_skb((len + 6) * 2 + 2, GFP_ATOMIC);
	if (!nskb)
		return NULL;

	bt_cb(nskb)->pkt_type = pkt_type;

	h5_slip_delim(nskb);

	hdr[0] = h5->next_ack << 3;
	h5->txack_req = false;

	if (rel) {
		hdr[0] |= 1 << 7;
		hdr[0] |= h5->next_seq;
		h5->next_seq = (h5->next_seq + 1) % 8;
	}

	hdr[1] = pkt_type | ((len & 0x0f) << 4);
	hdr[2] = len >> 4;
	hdr[3] = ~((hdr[0] + hdr[1] + hdr[2]) & 0xff);

	for (i = 0; i < 4; i++)
		h5_slip_one_byte(nskb, hdr[i]);

	for (i = 0; i < len; i++)
		h5_slip_one_byte(nskb, data[i]);

	h5_slip_delim(nskb);

	return nskb;
}

static struct sk_buff *h5_prepare_pkt(struct h5 *h5, u8 pkt_type,
				      const u8 *data, size_t len)
{
	bool rel;

	switch (pkt_type) {
	case HCI_ACLDATA_PKT:
	case HCI_COMMAND_PKT:
		rel = true;
		break;
	case HCI_SCODATA_PKT:
	case HCI_3WIRE_LINK_PKT:
	case HCI_3WIRE_ACK_PKT:
		rel = false;
		break;
	default:
		BT_ERR("Unknown packet type %u", pkt_type);
		return NULL;
	}

	return h5_build_pkt(h5, rel, pkt_type, data, len);
}

static struct sk_buff *h5_prepare_ack(struct h5 *h5)
{
	h5->txack_req = false;
	return NULL;
}

static struct sk_buff *h5_dequeue(struct hci_uart *hu)
{
	struct h5 *h5 = hu->priv;
	unsigned long flags;
	struct sk_buff *skb, *nskb;

	if ((skb = skb_dequeue(&h5->unrel)) != NULL) {
		nskb = h5_prepare_pkt(h5, bt_cb(skb)->pkt_type,
				      skb->data, skb->len);
		if (nskb) {
			kfree_skb(skb);
			return nskb;
		}

		skb_queue_head(&h5->unrel, skb);
		BT_ERR("Could not dequeue pkt because alloc_skb failed");
	}

	spin_lock_irqsave_nested(&h5->unack.lock, flags, SINGLE_DEPTH_NESTING);

	if (h5->unack.qlen >= H5_TXWINSIZE)
		goto unlock;

	if ((skb = skb_dequeue(&h5->rel)) != NULL) {
		nskb = h5_prepare_pkt(h5, bt_cb(skb)->pkt_type,
				      skb->data, skb->len);
		if (nskb) {
			__skb_queue_tail(&h5->unack, skb);
			mod_timer(&h5->timer, jiffies + H5_ACK_TIMEOUT);
			spin_unlock_irqrestore(&h5->unack.lock, flags);
			return nskb;
		}

		skb_queue_head(&h5->rel, skb);
		BT_ERR("Could not dequeue pkt because alloc_skb failed");
	}

unlock:
	spin_unlock_irqrestore(&h5->unack.lock, flags);

	if (h5->txack_req)
		return h5_prepare_ack(h5);

	return NULL;
}

static int h5_flush(struct hci_uart *hu)
{
	BT_DBG("hu %p", hu);
	return 0;
}

static struct hci_uart_proto h5p = {
	.id		= HCI_UART_3WIRE,
	.open		= h5_open,
	.close		= h5_close,
	.recv		= h5_recv,
	.enqueue	= h5_enqueue,
	.dequeue	= h5_dequeue,
	.flush		= h5_flush,
};

int __init h5_init(void)
{
	int err = hci_uart_register_proto(&h5p);

	if (!err)
		BT_INFO("HCI Three-wire UART (H5) protocol initialized");
	else
		BT_ERR("HCI Three-wire UART (H5) protocol init failed");

	return err;
}

int __exit h5_deinit(void)
{
	return hci_uart_unregister_proto(&h5p);
}
