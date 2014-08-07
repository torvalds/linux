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

/* Sliding window size */
#define H5_TX_WIN_MAX		4

#define H5_ACK_TIMEOUT	msecs_to_jiffies(250)
#define H5_SYNC_TIMEOUT	msecs_to_jiffies(100)

/*
 * Maximum Three-wire packet:
 *     4 byte header + max value for 12-bit length + 2 bytes for CRC
 */
#define H5_MAX_LEN (4 + 0xfff + 2)

/* Convenience macros for reading Three-wire header values */
#define H5_HDR_SEQ(hdr)		((hdr)[0] & 0x07)
#define H5_HDR_ACK(hdr)		(((hdr)[0] >> 3) & 0x07)
#define H5_HDR_CRC(hdr)		(((hdr)[0] >> 6) & 0x01)
#define H5_HDR_RELIABLE(hdr)	(((hdr)[0] >> 7) & 0x01)
#define H5_HDR_PKT_TYPE(hdr)	((hdr)[1] & 0x0f)
#define H5_HDR_LEN(hdr)		((((hdr)[1] >> 4) & 0xff) + ((hdr)[2] << 4))

#define SLIP_DELIMITER	0xc0
#define SLIP_ESC	0xdb
#define SLIP_ESC_DELIM	0xdc
#define SLIP_ESC_ESC	0xdd

/* H5 state flags */
enum {
	H5_RX_ESC,	/* SLIP escape mode */
	H5_TX_ACK_REQ,	/* Pending ack to send */
};

struct h5 {
	struct sk_buff_head	unack;		/* Unack'ed packets queue */
	struct sk_buff_head	rel;		/* Reliable packets queue */
	struct sk_buff_head	unrel;		/* Unreliable packets queue */

	unsigned long		flags;

	struct sk_buff		*rx_skb;	/* Receive buffer */
	size_t			rx_pending;	/* Expecting more bytes */
	u8			rx_ack;		/* Last ack number received */

	int			(*rx_func) (struct hci_uart *hu, u8 c);

	struct timer_list	timer;		/* Retransmission timer */

	u8			tx_seq;		/* Next seq number to send */
	u8			tx_ack;		/* Next ack number to send */
	u8			tx_win;		/* Sliding window size */

	enum {
		H5_UNINITIALIZED,
		H5_INITIALIZED,
		H5_ACTIVE,
	} state;

	enum {
		H5_AWAKE,
		H5_SLEEPING,
		H5_WAKING_UP,
	} sleep;
};

static void h5_reset_rx(struct h5 *h5);

static void h5_link_control(struct hci_uart *hu, const void *data, size_t len)
{
	struct h5 *h5 = hu->priv;
	struct sk_buff *nskb;

	nskb = alloc_skb(3, GFP_ATOMIC);
	if (!nskb)
		return;

	bt_cb(nskb)->pkt_type = HCI_3WIRE_LINK_PKT;

	memcpy(skb_put(nskb, len), data, len);

	skb_queue_tail(&h5->unrel, nskb);
}

static u8 h5_cfg_field(struct h5 *h5)
{
	u8 field = 0;

	/* Sliding window size (first 3 bits) */
	field |= (h5->tx_win & 7);

	return field;
}

static void h5_timed_event(unsigned long arg)
{
	const unsigned char sync_req[] = { 0x01, 0x7e };
	unsigned char conf_req[] = { 0x03, 0xfc, 0x01 };
	struct hci_uart *hu = (struct hci_uart *) arg;
	struct h5 *h5 = hu->priv;
	struct sk_buff *skb;
	unsigned long flags;

	BT_DBG("%s", hu->hdev->name);

	if (h5->state == H5_UNINITIALIZED)
		h5_link_control(hu, sync_req, sizeof(sync_req));

	if (h5->state == H5_INITIALIZED) {
		conf_req[2] = h5_cfg_field(h5);
		h5_link_control(hu, conf_req, sizeof(conf_req));
	}

	if (h5->state != H5_ACTIVE) {
		mod_timer(&h5->timer, jiffies + H5_SYNC_TIMEOUT);
		goto wakeup;
	}

	if (h5->sleep != H5_AWAKE) {
		h5->sleep = H5_SLEEPING;
		goto wakeup;
	}

	BT_DBG("hu %p retransmitting %u pkts", hu, h5->unack.qlen);

	spin_lock_irqsave_nested(&h5->unack.lock, flags, SINGLE_DEPTH_NESTING);

	while ((skb = __skb_dequeue_tail(&h5->unack)) != NULL) {
		h5->tx_seq = (h5->tx_seq - 1) & 0x07;
		skb_queue_head(&h5->rel, skb);
	}

	spin_unlock_irqrestore(&h5->unack.lock, flags);

wakeup:
	hci_uart_tx_wakeup(hu);
}

static int h5_open(struct hci_uart *hu)
{
	struct h5 *h5;
	const unsigned char sync[] = { 0x01, 0x7e };

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

	h5->tx_win = H5_TX_WIN_MAX;

	set_bit(HCI_UART_INIT_PENDING, &hu->hdev_flags);

	/* Send initial sync request */
	h5_link_control(hu, sync, sizeof(sync));
	mod_timer(&h5->timer, jiffies + H5_SYNC_TIMEOUT);

	return 0;
}

static int h5_close(struct hci_uart *hu)
{
	struct h5 *h5 = hu->priv;

	del_timer_sync(&h5->timer);

	skb_queue_purge(&h5->unack);
	skb_queue_purge(&h5->rel);
	skb_queue_purge(&h5->unrel);

	kfree(h5);

	return 0;
}

static void h5_pkt_cull(struct h5 *h5)
{
	struct sk_buff *skb, *tmp;
	unsigned long flags;
	int i, to_remove;
	u8 seq;

	spin_lock_irqsave(&h5->unack.lock, flags);

	to_remove = skb_queue_len(&h5->unack);
	if (to_remove == 0)
		goto unlock;

	seq = h5->tx_seq;

	while (to_remove > 0) {
		if (h5->rx_ack == seq)
			break;

		to_remove--;
		seq = (seq - 1) % 8;
	}

	if (seq != h5->rx_ack)
		BT_ERR("Controller acked invalid packet");

	i = 0;
	skb_queue_walk_safe(&h5->unack, skb, tmp) {
		if (i++ >= to_remove)
			break;

		__skb_unlink(skb, &h5->unack);
		kfree_skb(skb);
	}

	if (skb_queue_empty(&h5->unack))
		del_timer(&h5->timer);

unlock:
	spin_unlock_irqrestore(&h5->unack.lock, flags);
}

static void h5_handle_internal_rx(struct hci_uart *hu)
{
	struct h5 *h5 = hu->priv;
	const unsigned char sync_req[] = { 0x01, 0x7e };
	const unsigned char sync_rsp[] = { 0x02, 0x7d };
	unsigned char conf_req[] = { 0x03, 0xfc, 0x01 };
	const unsigned char conf_rsp[] = { 0x04, 0x7b };
	const unsigned char wakeup_req[] = { 0x05, 0xfa };
	const unsigned char woken_req[] = { 0x06, 0xf9 };
	const unsigned char sleep_req[] = { 0x07, 0x78 };
	const unsigned char *hdr = h5->rx_skb->data;
	const unsigned char *data = &h5->rx_skb->data[4];

	BT_DBG("%s", hu->hdev->name);

	if (H5_HDR_PKT_TYPE(hdr) != HCI_3WIRE_LINK_PKT)
		return;

	if (H5_HDR_LEN(hdr) < 2)
		return;

	conf_req[2] = h5_cfg_field(h5);

	if (memcmp(data, sync_req, 2) == 0) {
		h5_link_control(hu, sync_rsp, 2);
	} else if (memcmp(data, sync_rsp, 2) == 0) {
		h5->state = H5_INITIALIZED;
		h5_link_control(hu, conf_req, 3);
	} else if (memcmp(data, conf_req, 2) == 0) {
		h5_link_control(hu, conf_rsp, 2);
		h5_link_control(hu, conf_req, 3);
	} else if (memcmp(data, conf_rsp, 2) == 0) {
		if (H5_HDR_LEN(hdr) > 2)
			h5->tx_win = (data[2] & 7);
		BT_DBG("Three-wire init complete. tx_win %u", h5->tx_win);
		h5->state = H5_ACTIVE;
		hci_uart_init_ready(hu);
		return;
	} else if (memcmp(data, sleep_req, 2) == 0) {
		BT_DBG("Peer went to sleep");
		h5->sleep = H5_SLEEPING;
		return;
	} else if (memcmp(data, woken_req, 2) == 0) {
		BT_DBG("Peer woke up");
		h5->sleep = H5_AWAKE;
	} else if (memcmp(data, wakeup_req, 2) == 0) {
		BT_DBG("Peer requested wakeup");
		h5_link_control(hu, woken_req, 2);
		h5->sleep = H5_AWAKE;
	} else {
		BT_DBG("Link Control: 0x%02hhx 0x%02hhx", data[0], data[1]);
		return;
	}

	hci_uart_tx_wakeup(hu);
}

static void h5_complete_rx_pkt(struct hci_uart *hu)
{
	struct h5 *h5 = hu->priv;
	const unsigned char *hdr = h5->rx_skb->data;

	if (H5_HDR_RELIABLE(hdr)) {
		h5->tx_ack = (h5->tx_ack + 1) % 8;
		set_bit(H5_TX_ACK_REQ, &h5->flags);
		hci_uart_tx_wakeup(hu);
	}

	h5->rx_ack = H5_HDR_ACK(hdr);

	h5_pkt_cull(h5);

	switch (H5_HDR_PKT_TYPE(hdr)) {
	case HCI_EVENT_PKT:
	case HCI_ACLDATA_PKT:
	case HCI_SCODATA_PKT:
		bt_cb(h5->rx_skb)->pkt_type = H5_HDR_PKT_TYPE(hdr);

		/* Remove Three-wire header */
		skb_pull(h5->rx_skb, 4);

		hci_recv_frame(hu->hdev, h5->rx_skb);
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

	h5_complete_rx_pkt(hu);
	h5_reset_rx(h5);

	return 0;
}

static int h5_rx_payload(struct hci_uart *hu, unsigned char c)
{
	struct h5 *h5 = hu->priv;
	const unsigned char *hdr = h5->rx_skb->data;

	if (H5_HDR_CRC(hdr)) {
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

	BT_DBG("%s rx: seq %u ack %u crc %u rel %u type %u len %u",
	       hu->hdev->name, H5_HDR_SEQ(hdr), H5_HDR_ACK(hdr),
	       H5_HDR_CRC(hdr), H5_HDR_RELIABLE(hdr), H5_HDR_PKT_TYPE(hdr),
	       H5_HDR_LEN(hdr));

	if (((hdr[0] + hdr[1] + hdr[2] + hdr[3]) & 0xff) != 0xff) {
		BT_ERR("Invalid header checksum");
		h5_reset_rx(h5);
		return 0;
	}

	if (H5_HDR_RELIABLE(hdr) && H5_HDR_SEQ(hdr) != h5->tx_ack) {
		BT_ERR("Out-of-order packet arrived (%u != %u)",
		       H5_HDR_SEQ(hdr), h5->tx_ack);
		h5_reset_rx(h5);
		return 0;
	}

	if (h5->state != H5_ACTIVE &&
	    H5_HDR_PKT_TYPE(hdr) != HCI_3WIRE_LINK_PKT) {
		BT_ERR("Non-link packet received in non-active state");
		h5_reset_rx(h5);
		return 0;
	}

	h5->rx_func = h5_rx_payload;
	h5->rx_pending = H5_HDR_LEN(hdr);

	return 0;
}

static int h5_rx_pkt_start(struct hci_uart *hu, unsigned char c)
{
	struct h5 *h5 = hu->priv;

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

	if (c == SLIP_DELIMITER)
		h5->rx_func = h5_rx_pkt_start;

	return 1;
}

static void h5_unslip_one_byte(struct h5 *h5, unsigned char c)
{
	const u8 delim = SLIP_DELIMITER, esc = SLIP_ESC;
	const u8 *byte = &c;

	if (!test_bit(H5_RX_ESC, &h5->flags) && c == SLIP_ESC) {
		set_bit(H5_RX_ESC, &h5->flags);
		return;
	}

	if (test_and_clear_bit(H5_RX_ESC, &h5->flags)) {
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
	}

	memcpy(skb_put(h5->rx_skb, 1), byte, 1);
	h5->rx_pending--;

	BT_DBG("unsliped 0x%02hhx, rx_pending %zu", *byte, h5->rx_pending);
}

static void h5_reset_rx(struct h5 *h5)
{
	if (h5->rx_skb) {
		kfree_skb(h5->rx_skb);
		h5->rx_skb = NULL;
	}

	h5->rx_func = h5_rx_delimiter;
	h5->rx_pending = 0;
	clear_bit(H5_RX_ESC, &h5->flags);
}

static int h5_recv(struct hci_uart *hu, void *data, int count)
{
	struct h5 *h5 = hu->priv;
	unsigned char *ptr = data;

	BT_DBG("%s pending %zu count %d", hu->hdev->name, h5->rx_pending,
	       count);

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

	if (h5->state != H5_ACTIVE) {
		BT_ERR("Ignoring HCI data in non-active state");
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

static bool valid_packet_type(u8 type)
{
	switch (type) {
	case HCI_ACLDATA_PKT:
	case HCI_COMMAND_PKT:
	case HCI_SCODATA_PKT:
	case HCI_3WIRE_LINK_PKT:
	case HCI_3WIRE_ACK_PKT:
		return true;
	default:
		return false;
	}
}

static struct sk_buff *h5_prepare_pkt(struct hci_uart *hu, u8 pkt_type,
				      const u8 *data, size_t len)
{
	struct h5 *h5 = hu->priv;
	struct sk_buff *nskb;
	u8 hdr[4];
	int i;

	if (!valid_packet_type(pkt_type)) {
		BT_ERR("Unknown packet type %u", pkt_type);
		return NULL;
	}

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

	hdr[0] = h5->tx_ack << 3;
	clear_bit(H5_TX_ACK_REQ, &h5->flags);

	/* Reliable packet? */
	if (pkt_type == HCI_ACLDATA_PKT || pkt_type == HCI_COMMAND_PKT) {
		hdr[0] |= 1 << 7;
		hdr[0] |= h5->tx_seq;
		h5->tx_seq = (h5->tx_seq + 1) % 8;
	}

	hdr[1] = pkt_type | ((len & 0x0f) << 4);
	hdr[2] = len >> 4;
	hdr[3] = ~((hdr[0] + hdr[1] + hdr[2]) & 0xff);

	BT_DBG("%s tx: seq %u ack %u crc %u rel %u type %u len %u",
	       hu->hdev->name, H5_HDR_SEQ(hdr), H5_HDR_ACK(hdr),
	       H5_HDR_CRC(hdr), H5_HDR_RELIABLE(hdr), H5_HDR_PKT_TYPE(hdr),
	       H5_HDR_LEN(hdr));

	for (i = 0; i < 4; i++)
		h5_slip_one_byte(nskb, hdr[i]);

	for (i = 0; i < len; i++)
		h5_slip_one_byte(nskb, data[i]);

	h5_slip_delim(nskb);

	return nskb;
}

static struct sk_buff *h5_dequeue(struct hci_uart *hu)
{
	struct h5 *h5 = hu->priv;
	unsigned long flags;
	struct sk_buff *skb, *nskb;

	if (h5->sleep != H5_AWAKE) {
		const unsigned char wakeup_req[] = { 0x05, 0xfa };

		if (h5->sleep == H5_WAKING_UP)
			return NULL;

		h5->sleep = H5_WAKING_UP;
		BT_DBG("Sending wakeup request");

		mod_timer(&h5->timer, jiffies + HZ / 100);
		return h5_prepare_pkt(hu, HCI_3WIRE_LINK_PKT, wakeup_req, 2);
	}

	skb = skb_dequeue(&h5->unrel);
	if (skb != NULL) {
		nskb = h5_prepare_pkt(hu, bt_cb(skb)->pkt_type,
				      skb->data, skb->len);
		if (nskb) {
			kfree_skb(skb);
			return nskb;
		}

		skb_queue_head(&h5->unrel, skb);
		BT_ERR("Could not dequeue pkt because alloc_skb failed");
	}

	spin_lock_irqsave_nested(&h5->unack.lock, flags, SINGLE_DEPTH_NESTING);

	if (h5->unack.qlen >= h5->tx_win)
		goto unlock;

	skb = skb_dequeue(&h5->rel);
	if (skb != NULL) {
		nskb = h5_prepare_pkt(hu, bt_cb(skb)->pkt_type,
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

	if (test_bit(H5_TX_ACK_REQ, &h5->flags))
		return h5_prepare_pkt(hu, HCI_3WIRE_ACK_PKT, NULL, 0);

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
