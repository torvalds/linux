// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *
 *  Bluetooth HCI Three-wire UART driver
 *
 *  Copyright (C) 2012  Intel Corporation
 */

#include <linux/acpi.h>
#include <linux/errno.h>
#include <linux/gpio/consumer.h>
#include <linux/kernel.h>
#include <linux/mod_devicetable.h>
#include <linux/of_device.h>
#include <linux/serdev.h>
#include <linux/skbuff.h>

#include <net/bluetooth/bluetooth.h>
#include <net/bluetooth/hci_core.h>

#include "btrtl.h"
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
#define H5_HDR_LEN(hdr)		((((hdr)[1] >> 4) & 0x0f) + ((hdr)[2] << 4))

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
	/* Must be the first member, hci_serdev.c expects this. */
	struct hci_uart		serdev_hu;

	struct sk_buff_head	unack;		/* Unack'ed packets queue */
	struct sk_buff_head	rel;		/* Reliable packets queue */
	struct sk_buff_head	unrel;		/* Unreliable packets queue */

	unsigned long		flags;

	struct sk_buff		*rx_skb;	/* Receive buffer */
	size_t			rx_pending;	/* Expecting more bytes */
	u8			rx_ack;		/* Last ack number received */

	int			(*rx_func)(struct hci_uart *hu, u8 c);

	struct timer_list	timer;		/* Retransmission timer */
	struct hci_uart		*hu;		/* Parent HCI UART */

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

	const struct h5_vnd *vnd;
	const char *id;

	struct gpio_desc *enable_gpio;
	struct gpio_desc *device_wake_gpio;
};

struct h5_vnd {
	int (*setup)(struct h5 *h5);
	void (*open)(struct h5 *h5);
	void (*close)(struct h5 *h5);
	int (*suspend)(struct h5 *h5);
	int (*resume)(struct h5 *h5);
	const struct acpi_gpio_mapping *acpi_gpio_map;
};

static void h5_reset_rx(struct h5 *h5);

static void h5_link_control(struct hci_uart *hu, const void *data, size_t len)
{
	struct h5 *h5 = hu->priv;
	struct sk_buff *nskb;

	nskb = alloc_skb(3, GFP_ATOMIC);
	if (!nskb)
		return;

	hci_skb_pkt_type(nskb) = HCI_3WIRE_LINK_PKT;

	skb_put_data(nskb, data, len);

	skb_queue_tail(&h5->unrel, nskb);
}

static u8 h5_cfg_field(struct h5 *h5)
{
	/* Sliding window size (first 3 bits) */
	return h5->tx_win & 0x07;
}

static void h5_timed_event(struct timer_list *t)
{
	const unsigned char sync_req[] = { 0x01, 0x7e };
	unsigned char conf_req[3] = { 0x03, 0xfc };
	struct h5 *h5 = from_timer(h5, t, timer);
	struct hci_uart *hu = h5->hu;
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

static void h5_peer_reset(struct hci_uart *hu)
{
	struct h5 *h5 = hu->priv;

	bt_dev_err(hu->hdev, "Peer device has reset");

	h5->state = H5_UNINITIALIZED;

	del_timer(&h5->timer);

	skb_queue_purge(&h5->rel);
	skb_queue_purge(&h5->unrel);
	skb_queue_purge(&h5->unack);

	h5->tx_seq = 0;
	h5->tx_ack = 0;

	/* Send reset request to upper stack */
	hci_reset_dev(hu->hdev);
}

static int h5_open(struct hci_uart *hu)
{
	struct h5 *h5;
	const unsigned char sync[] = { 0x01, 0x7e };

	BT_DBG("hu %p", hu);

	if (hu->serdev) {
		h5 = serdev_device_get_drvdata(hu->serdev);
	} else {
		h5 = kzalloc(sizeof(*h5), GFP_KERNEL);
		if (!h5)
			return -ENOMEM;
	}

	hu->priv = h5;
	h5->hu = hu;

	skb_queue_head_init(&h5->unack);
	skb_queue_head_init(&h5->rel);
	skb_queue_head_init(&h5->unrel);

	h5_reset_rx(h5);

	timer_setup(&h5->timer, h5_timed_event, 0);

	h5->tx_win = H5_TX_WIN_MAX;

	if (h5->vnd && h5->vnd->open)
		h5->vnd->open(h5);

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

	if (h5->vnd && h5->vnd->close)
		h5->vnd->close(h5);

	if (!hu->serdev)
		kfree(h5);

	return 0;
}

static int h5_setup(struct hci_uart *hu)
{
	struct h5 *h5 = hu->priv;

	if (h5->vnd && h5->vnd->setup)
		return h5->vnd->setup(h5);

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
		seq = (seq - 1) & 0x07;
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
	unsigned char conf_req[3] = { 0x03, 0xfc };
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
		if (h5->state == H5_ACTIVE)
			h5_peer_reset(hu);
		h5_link_control(hu, sync_rsp, 2);
	} else if (memcmp(data, sync_rsp, 2) == 0) {
		if (h5->state == H5_ACTIVE)
			h5_peer_reset(hu);
		h5->state = H5_INITIALIZED;
		h5_link_control(hu, conf_req, 3);
	} else if (memcmp(data, conf_req, 2) == 0) {
		h5_link_control(hu, conf_rsp, 2);
		h5_link_control(hu, conf_req, 3);
	} else if (memcmp(data, conf_rsp, 2) == 0) {
		if (H5_HDR_LEN(hdr) > 2)
			h5->tx_win = (data[2] & 0x07);
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
	case HCI_ISODATA_PKT:
		hci_skb_pkt_type(h5->rx_skb) = H5_HDR_PKT_TYPE(hdr);

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
	h5_complete_rx_pkt(hu);

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
		bt_dev_err(hu->hdev, "Invalid header checksum");
		h5_reset_rx(h5);
		return 0;
	}

	if (H5_HDR_RELIABLE(hdr) && H5_HDR_SEQ(hdr) != h5->tx_ack) {
		bt_dev_err(hu->hdev, "Out-of-order packet arrived (%u != %u)",
			   H5_HDR_SEQ(hdr), h5->tx_ack);
		h5_reset_rx(h5);
		return 0;
	}

	if (h5->state != H5_ACTIVE &&
	    H5_HDR_PKT_TYPE(hdr) != HCI_3WIRE_LINK_PKT) {
		bt_dev_err(hu->hdev, "Non-link packet received in non-active state");
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
		bt_dev_err(hu->hdev, "Can't allocate mem for new packet");
		h5_reset_rx(h5);
		return -ENOMEM;
	}

	h5->rx_skb->dev = (void *)hu->hdev;

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

	skb_put_data(h5->rx_skb, byte, 1);
	h5->rx_pending--;

	BT_DBG("unslipped 0x%02hhx, rx_pending %zu", *byte, h5->rx_pending);
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

static int h5_recv(struct hci_uart *hu, const void *data, int count)
{
	struct h5 *h5 = hu->priv;
	const unsigned char *ptr = data;

	BT_DBG("%s pending %zu count %d", hu->hdev->name, h5->rx_pending,
	       count);

	while (count > 0) {
		int processed;

		if (h5->rx_pending > 0) {
			if (*ptr == SLIP_DELIMITER) {
				bt_dev_err(hu->hdev, "Too short H5 packet");
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
		bt_dev_err(hu->hdev, "Packet too long (%u bytes)", skb->len);
		kfree_skb(skb);
		return 0;
	}

	if (h5->state != H5_ACTIVE) {
		bt_dev_err(hu->hdev, "Ignoring HCI data in non-active state");
		kfree_skb(skb);
		return 0;
	}

	switch (hci_skb_pkt_type(skb)) {
	case HCI_ACLDATA_PKT:
	case HCI_COMMAND_PKT:
		skb_queue_tail(&h5->rel, skb);
		break;

	case HCI_SCODATA_PKT:
	case HCI_ISODATA_PKT:
		skb_queue_tail(&h5->unrel, skb);
		break;

	default:
		bt_dev_err(hu->hdev, "Unknown packet type %u", hci_skb_pkt_type(skb));
		kfree_skb(skb);
		break;
	}

	return 0;
}

static void h5_slip_delim(struct sk_buff *skb)
{
	const char delim = SLIP_DELIMITER;

	skb_put_data(skb, &delim, 1);
}

static void h5_slip_one_byte(struct sk_buff *skb, u8 c)
{
	const char esc_delim[2] = { SLIP_ESC, SLIP_ESC_DELIM };
	const char esc_esc[2] = { SLIP_ESC, SLIP_ESC_ESC };

	switch (c) {
	case SLIP_DELIMITER:
		skb_put_data(skb, &esc_delim, 2);
		break;
	case SLIP_ESC:
		skb_put_data(skb, &esc_esc, 2);
		break;
	default:
		skb_put_data(skb, &c, 1);
	}
}

static bool valid_packet_type(u8 type)
{
	switch (type) {
	case HCI_ACLDATA_PKT:
	case HCI_COMMAND_PKT:
	case HCI_SCODATA_PKT:
	case HCI_ISODATA_PKT:
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
		bt_dev_err(hu->hdev, "Unknown packet type %u", pkt_type);
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

	hci_skb_pkt_type(nskb) = pkt_type;

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
	if (skb) {
		nskb = h5_prepare_pkt(hu, hci_skb_pkt_type(skb),
				      skb->data, skb->len);
		if (nskb) {
			kfree_skb(skb);
			return nskb;
		}

		skb_queue_head(&h5->unrel, skb);
		bt_dev_err(hu->hdev, "Could not dequeue pkt because alloc_skb failed");
	}

	spin_lock_irqsave_nested(&h5->unack.lock, flags, SINGLE_DEPTH_NESTING);

	if (h5->unack.qlen >= h5->tx_win)
		goto unlock;

	skb = skb_dequeue(&h5->rel);
	if (skb) {
		nskb = h5_prepare_pkt(hu, hci_skb_pkt_type(skb),
				      skb->data, skb->len);
		if (nskb) {
			__skb_queue_tail(&h5->unack, skb);
			mod_timer(&h5->timer, jiffies + H5_ACK_TIMEOUT);
			spin_unlock_irqrestore(&h5->unack.lock, flags);
			return nskb;
		}

		skb_queue_head(&h5->rel, skb);
		bt_dev_err(hu->hdev, "Could not dequeue pkt because alloc_skb failed");
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

static const struct hci_uart_proto h5p = {
	.id		= HCI_UART_3WIRE,
	.name		= "Three-wire (H5)",
	.open		= h5_open,
	.close		= h5_close,
	.setup		= h5_setup,
	.recv		= h5_recv,
	.enqueue	= h5_enqueue,
	.dequeue	= h5_dequeue,
	.flush		= h5_flush,
};

static int h5_serdev_probe(struct serdev_device *serdev)
{
	struct device *dev = &serdev->dev;
	struct h5 *h5;

	h5 = devm_kzalloc(dev, sizeof(*h5), GFP_KERNEL);
	if (!h5)
		return -ENOMEM;

	set_bit(HCI_UART_RESET_ON_INIT, &h5->serdev_hu.hdev_flags);

	h5->hu = &h5->serdev_hu;
	h5->serdev_hu.serdev = serdev;
	serdev_device_set_drvdata(serdev, h5);

	if (has_acpi_companion(dev)) {
		const struct acpi_device_id *match;

		match = acpi_match_device(dev->driver->acpi_match_table, dev);
		if (!match)
			return -ENODEV;

		h5->vnd = (const struct h5_vnd *)match->driver_data;
		h5->id  = (char *)match->id;

		if (h5->vnd->acpi_gpio_map)
			devm_acpi_dev_add_driver_gpios(dev,
						       h5->vnd->acpi_gpio_map);
	} else {
		const void *data;

		data = of_device_get_match_data(dev);
		if (!data)
			return -ENODEV;

		h5->vnd = (const struct h5_vnd *)data;
	}


	h5->enable_gpio = devm_gpiod_get_optional(dev, "enable", GPIOD_OUT_LOW);
	if (IS_ERR(h5->enable_gpio))
		return PTR_ERR(h5->enable_gpio);

	h5->device_wake_gpio = devm_gpiod_get_optional(dev, "device-wake",
						       GPIOD_OUT_LOW);
	if (IS_ERR(h5->device_wake_gpio))
		return PTR_ERR(h5->device_wake_gpio);

	return hci_uart_register_device(&h5->serdev_hu, &h5p);
}

static void h5_serdev_remove(struct serdev_device *serdev)
{
	struct h5 *h5 = serdev_device_get_drvdata(serdev);

	hci_uart_unregister_device(&h5->serdev_hu);
}

static int __maybe_unused h5_serdev_suspend(struct device *dev)
{
	struct h5 *h5 = dev_get_drvdata(dev);
	int ret = 0;

	if (h5->vnd && h5->vnd->suspend)
		ret = h5->vnd->suspend(h5);

	return ret;
}

static int __maybe_unused h5_serdev_resume(struct device *dev)
{
	struct h5 *h5 = dev_get_drvdata(dev);
	int ret = 0;

	if (h5->vnd && h5->vnd->resume)
		ret = h5->vnd->resume(h5);

	return ret;
}

#ifdef CONFIG_BT_HCIUART_RTL
static int h5_btrtl_setup(struct h5 *h5)
{
	struct btrtl_device_info *btrtl_dev;
	struct sk_buff *skb;
	__le32 baudrate_data;
	u32 device_baudrate;
	unsigned int controller_baudrate;
	bool flow_control;
	int err;

	btrtl_dev = btrtl_initialize(h5->hu->hdev, h5->id);
	if (IS_ERR(btrtl_dev))
		return PTR_ERR(btrtl_dev);

	err = btrtl_get_uart_settings(h5->hu->hdev, btrtl_dev,
				      &controller_baudrate, &device_baudrate,
				      &flow_control);
	if (err)
		goto out_free;

	baudrate_data = cpu_to_le32(device_baudrate);
	skb = __hci_cmd_sync(h5->hu->hdev, 0xfc17, sizeof(baudrate_data),
			     &baudrate_data, HCI_INIT_TIMEOUT);
	if (IS_ERR(skb)) {
		rtl_dev_err(h5->hu->hdev, "set baud rate command failed\n");
		err = PTR_ERR(skb);
		goto out_free;
	} else {
		kfree_skb(skb);
	}
	/* Give the device some time to set up the new baudrate. */
	usleep_range(10000, 20000);

	serdev_device_set_baudrate(h5->hu->serdev, controller_baudrate);
	serdev_device_set_flow_control(h5->hu->serdev, flow_control);

	err = btrtl_download_firmware(h5->hu->hdev, btrtl_dev);
	/* Give the device some time before the hci-core sends it a reset */
	usleep_range(10000, 20000);

out_free:
	btrtl_free(btrtl_dev);

	return err;
}

static void h5_btrtl_open(struct h5 *h5)
{
	/* Devices always start with these fixed parameters */
	serdev_device_set_flow_control(h5->hu->serdev, false);
	serdev_device_set_parity(h5->hu->serdev, SERDEV_PARITY_EVEN);
	serdev_device_set_baudrate(h5->hu->serdev, 115200);

	/* The controller needs up to 500ms to wakeup */
	gpiod_set_value_cansleep(h5->enable_gpio, 1);
	gpiod_set_value_cansleep(h5->device_wake_gpio, 1);
	msleep(500);
}

static void h5_btrtl_close(struct h5 *h5)
{
	gpiod_set_value_cansleep(h5->device_wake_gpio, 0);
	gpiod_set_value_cansleep(h5->enable_gpio, 0);
}

/* Suspend/resume support. On many devices the RTL BT device loses power during
 * suspend/resume, causing it to lose its firmware and all state. So we simply
 * turn it off on suspend and reprobe on resume.  This mirrors how RTL devices
 * are handled in the USB driver, where the USB_QUIRK_RESET_RESUME is used which
 * also causes a reprobe on resume.
 */
static int h5_btrtl_suspend(struct h5 *h5)
{
	serdev_device_set_flow_control(h5->hu->serdev, false);
	gpiod_set_value_cansleep(h5->device_wake_gpio, 0);
	gpiod_set_value_cansleep(h5->enable_gpio, 0);
	return 0;
}

struct h5_btrtl_reprobe {
	struct device *dev;
	struct work_struct work;
};

static void h5_btrtl_reprobe_worker(struct work_struct *work)
{
	struct h5_btrtl_reprobe *reprobe =
		container_of(work, struct h5_btrtl_reprobe, work);
	int ret;

	ret = device_reprobe(reprobe->dev);
	if (ret && ret != -EPROBE_DEFER)
		dev_err(reprobe->dev, "Reprobe error %d\n", ret);

	put_device(reprobe->dev);
	kfree(reprobe);
	module_put(THIS_MODULE);
}

static int h5_btrtl_resume(struct h5 *h5)
{
	struct h5_btrtl_reprobe *reprobe;

	reprobe = kzalloc(sizeof(*reprobe), GFP_KERNEL);
	if (!reprobe)
		return -ENOMEM;

	__module_get(THIS_MODULE);

	INIT_WORK(&reprobe->work, h5_btrtl_reprobe_worker);
	reprobe->dev = get_device(&h5->hu->serdev->dev);
	queue_work(system_long_wq, &reprobe->work);
	return 0;
}

static const struct acpi_gpio_params btrtl_device_wake_gpios = { 0, 0, false };
static const struct acpi_gpio_params btrtl_enable_gpios = { 1, 0, false };
static const struct acpi_gpio_params btrtl_host_wake_gpios = { 2, 0, false };
static const struct acpi_gpio_mapping acpi_btrtl_gpios[] = {
	{ "device-wake-gpios", &btrtl_device_wake_gpios, 1 },
	{ "enable-gpios", &btrtl_enable_gpios, 1 },
	{ "host-wake-gpios", &btrtl_host_wake_gpios, 1 },
	{},
};

static struct h5_vnd rtl_vnd = {
	.setup		= h5_btrtl_setup,
	.open		= h5_btrtl_open,
	.close		= h5_btrtl_close,
	.suspend	= h5_btrtl_suspend,
	.resume		= h5_btrtl_resume,
	.acpi_gpio_map	= acpi_btrtl_gpios,
};
#endif

#ifdef CONFIG_ACPI
static const struct acpi_device_id h5_acpi_match[] = {
#ifdef CONFIG_BT_HCIUART_RTL
	{ "OBDA8723", (kernel_ulong_t)&rtl_vnd },
#endif
	{ },
};
MODULE_DEVICE_TABLE(acpi, h5_acpi_match);
#endif

static const struct dev_pm_ops h5_serdev_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(h5_serdev_suspend, h5_serdev_resume)
};

static const struct of_device_id rtl_bluetooth_of_match[] = {
#ifdef CONFIG_BT_HCIUART_RTL
	{ .compatible = "realtek,rtl8822cs-bt",
	  .data = (const void *)&rtl_vnd },
	{ .compatible = "realtek,rtl8723bs-bt",
	  .data = (const void *)&rtl_vnd },
#endif
	{ },
};
MODULE_DEVICE_TABLE(of, rtl_bluetooth_of_match);

static struct serdev_device_driver h5_serdev_driver = {
	.probe = h5_serdev_probe,
	.remove = h5_serdev_remove,
	.driver = {
		.name = "hci_uart_h5",
		.acpi_match_table = ACPI_PTR(h5_acpi_match),
		.pm = &h5_serdev_pm_ops,
		.of_match_table = rtl_bluetooth_of_match,
	},
};

int __init h5_init(void)
{
	serdev_device_driver_register(&h5_serdev_driver);
	return hci_uart_register_proto(&h5p);
}

int __exit h5_deinit(void)
{
	serdev_device_driver_unregister(&h5_serdev_driver);
	return hci_uart_unregister_proto(&h5p);
}
