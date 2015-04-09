/*
 *
 *  Bluetooth HCI UART driver for Broadcom devices
 *
 *  Copyright (C) 2015  Intel Corporation
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

#include "btbcm.h"
#include "hci_uart.h"

struct bcm_data {
	struct sk_buff *rx_skb;
	struct sk_buff_head txq;
};

static int bcm_open(struct hci_uart *hu)
{
	struct bcm_data *bcm;

	BT_DBG("hu %p", hu);

	bcm = kzalloc(sizeof(*bcm), GFP_KERNEL);
	if (!bcm)
		return -ENOMEM;

	skb_queue_head_init(&bcm->txq);

	hu->priv = bcm;
	return 0;
}

static int bcm_close(struct hci_uart *hu)
{
	struct bcm_data *bcm = hu->priv;

	BT_DBG("hu %p", hu);

	skb_queue_purge(&bcm->txq);
	kfree_skb(bcm->rx_skb);
	kfree(bcm);

	hu->priv = NULL;
	return 0;
}

static int bcm_flush(struct hci_uart *hu)
{
	struct bcm_data *bcm = hu->priv;

	BT_DBG("hu %p", hu);

	skb_queue_purge(&bcm->txq);

	return 0;
}

static int bcm_setup(struct hci_uart *hu)
{
	BT_DBG("hu %p", hu);

	hu->hdev->set_bdaddr = btbcm_set_bdaddr;

	return btbcm_setup_patchram(hu->hdev);
}

static const struct h4_recv_pkt bcm_recv_pkts[] = {
	{ H4_RECV_ACL,   .recv = hci_recv_frame },
	{ H4_RECV_SCO,   .recv = hci_recv_frame },
	{ H4_RECV_EVENT, .recv = hci_recv_frame },
};

static int bcm_recv(struct hci_uart *hu, const void *data, int count)
{
	struct bcm_data *bcm = hu->priv;

	if (!test_bit(HCI_UART_REGISTERED, &hu->flags))
		return -EUNATCH;

	bcm->rx_skb = h4_recv_buf(hu->hdev, bcm->rx_skb, data, count,
				  bcm_recv_pkts, ARRAY_SIZE(bcm_recv_pkts));
	if (IS_ERR(bcm->rx_skb)) {
		int err = PTR_ERR(bcm->rx_skb);
		BT_ERR("%s: Frame reassembly failed (%d)", hu->hdev->name, err);
		return err;
	}

	return count;
}

static int bcm_enqueue(struct hci_uart *hu, struct sk_buff *skb)
{
	struct bcm_data *bcm = hu->priv;

	BT_DBG("hu %p skb %p", hu, skb);

	/* Prepend skb with frame type */
	memcpy(skb_push(skb, 1), &bt_cb(skb)->pkt_type, 1);
	skb_queue_tail(&bcm->txq, skb);

	return 0;
}

static struct sk_buff *bcm_dequeue(struct hci_uart *hu)
{
	struct bcm_data *bcm = hu->priv;

	return skb_dequeue(&bcm->txq);
}

static const struct hci_uart_proto bcm_proto = {
	.id		= HCI_UART_BCM,
	.name		= "BCM",
	.open		= bcm_open,
	.close		= bcm_close,
	.flush		= bcm_flush,
	.setup		= bcm_setup,
	.recv		= bcm_recv,
	.enqueue	= bcm_enqueue,
	.dequeue	= bcm_dequeue,
};

int __init bcm_init(void)
{
	return hci_uart_register_proto(&bcm_proto);
}

int __exit bcm_deinit(void)
{
	return hci_uart_unregister_proto(&bcm_proto);
}
