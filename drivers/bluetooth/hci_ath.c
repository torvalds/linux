// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  Atheros Communication Bluetooth HCIATH3K UART protocol
 *
 *  HCIATH3K (HCI Atheros AR300x Protocol) is a Atheros Communication's
 *  power management protocol extension to H4 to support AR300x Bluetooth Chip.
 *
 *  Copyright (c) 2009-2010 Atheros Communications Inc.
 *
 *  Acknowledgements:
 *  This file is based on hci_h4.c, which was written
 *  by Maxim Krasnyansky and Marcel Holtmann.
 */

#include <linux/module.h>
#include <linux/kernel.h>

#include <linux/init.h>
#include <linux/slab.h>
#include <linux/tty.h>
#include <linux/errno.h>
#include <linux/ioctl.h>
#include <linux/skbuff.h>

#include <net/bluetooth/bluetooth.h>
#include <net/bluetooth/hci_core.h>

#include "hci_uart.h"

struct ath_struct {
	struct hci_uart *hu;
	unsigned int cur_sleep;

	struct sk_buff *rx_skb;
	struct sk_buff_head txq;
	struct work_struct ctxtsw;
};

#define OP_WRITE_TAG	0x01

#define INDEX_BDADDR	0x01

struct ath_vendor_cmd {
	__u8 opcode;
	__le16 index;
	__u8 len;
	__u8 data[251];
} __packed;

static int ath_wakeup_ar3k(struct tty_struct *tty)
{
	int status = tty->driver->ops->tiocmget(tty);

	if (status & TIOCM_CTS)
		return status;

	/* Clear RTS first */
	tty->driver->ops->tiocmget(tty);
	tty->driver->ops->tiocmset(tty, 0x00, TIOCM_RTS);
	msleep(20);

	/* Set RTS, wake up board */
	tty->driver->ops->tiocmget(tty);
	tty->driver->ops->tiocmset(tty, TIOCM_RTS, 0x00);
	msleep(20);

	status = tty->driver->ops->tiocmget(tty);
	return status;
}

static void ath_hci_uart_work(struct work_struct *work)
{
	int status;
	struct ath_struct *ath;
	struct hci_uart *hu;
	struct tty_struct *tty;

	ath = container_of(work, struct ath_struct, ctxtsw);

	hu = ath->hu;
	tty = hu->tty;

	/* verify and wake up controller */
	if (ath->cur_sleep) {
		status = ath_wakeup_ar3k(tty);
		if (!(status & TIOCM_CTS))
			return;
	}

	/* Ready to send Data */
	clear_bit(HCI_UART_SENDING, &hu->tx_state);
	hci_uart_tx_wakeup(hu);
}

static int ath_open(struct hci_uart *hu)
{
	struct ath_struct *ath;

	BT_DBG("hu %p", hu);

	if (!hci_uart_has_flow_control(hu))
		return -EOPNOTSUPP;

	ath = kzalloc(sizeof(*ath), GFP_KERNEL);
	if (!ath)
		return -ENOMEM;

	skb_queue_head_init(&ath->txq);

	hu->priv = ath;
	ath->hu = hu;

	INIT_WORK(&ath->ctxtsw, ath_hci_uart_work);

	return 0;
}

static int ath_close(struct hci_uart *hu)
{
	struct ath_struct *ath = hu->priv;

	BT_DBG("hu %p", hu);

	skb_queue_purge(&ath->txq);

	kfree_skb(ath->rx_skb);

	cancel_work_sync(&ath->ctxtsw);

	hu->priv = NULL;
	kfree(ath);

	return 0;
}

static int ath_flush(struct hci_uart *hu)
{
	struct ath_struct *ath = hu->priv;

	BT_DBG("hu %p", hu);

	skb_queue_purge(&ath->txq);

	return 0;
}

static int ath_vendor_cmd(struct hci_dev *hdev, uint8_t opcode, uint16_t index,
			  const void *data, size_t dlen)
{
	struct sk_buff *skb;
	struct ath_vendor_cmd cmd;

	if (dlen > sizeof(cmd.data))
		return -EINVAL;

	cmd.opcode = opcode;
	cmd.index = cpu_to_le16(index);
	cmd.len = dlen;
	memcpy(cmd.data, data, dlen);

	skb = __hci_cmd_sync(hdev, 0xfc0b, dlen + 4, &cmd, HCI_INIT_TIMEOUT);
	if (IS_ERR(skb))
		return PTR_ERR(skb);
	kfree_skb(skb);

	return 0;
}

static int ath_set_bdaddr(struct hci_dev *hdev, const bdaddr_t *bdaddr)
{
	return ath_vendor_cmd(hdev, OP_WRITE_TAG, INDEX_BDADDR, bdaddr,
			      sizeof(*bdaddr));
}

static int ath_setup(struct hci_uart *hu)
{
	BT_DBG("hu %p", hu);

	hu->hdev->set_bdaddr = ath_set_bdaddr;

	return 0;
}

static const struct h4_recv_pkt ath_recv_pkts[] = {
	{ H4_RECV_ACL,   .recv = hci_recv_frame },
	{ H4_RECV_SCO,   .recv = hci_recv_frame },
	{ H4_RECV_EVENT, .recv = hci_recv_frame },
};

static int ath_recv(struct hci_uart *hu, const void *data, int count)
{
	struct ath_struct *ath = hu->priv;

	ath->rx_skb = h4_recv_buf(hu, ath->rx_skb, data, count,
				  ath_recv_pkts, ARRAY_SIZE(ath_recv_pkts));
	if (IS_ERR(ath->rx_skb)) {
		int err = PTR_ERR(ath->rx_skb);
		bt_dev_err(hu->hdev, "Frame reassembly failed (%d)", err);
		ath->rx_skb = NULL;
		return err;
	}

	return count;
}

#define HCI_OP_ATH_SLEEP 0xFC04

static int ath_enqueue(struct hci_uart *hu, struct sk_buff *skb)
{
	struct ath_struct *ath = hu->priv;

	if (hci_skb_pkt_type(skb) == HCI_SCODATA_PKT) {
		kfree_skb(skb);
		return 0;
	}

	/* Update power management enable flag with parameters of
	 * HCI sleep enable vendor specific HCI command.
	 */
	if (hci_skb_pkt_type(skb) == HCI_COMMAND_PKT) {
		struct hci_command_hdr *hdr = (void *)skb->data;

		if (__le16_to_cpu(hdr->opcode) == HCI_OP_ATH_SLEEP)
			ath->cur_sleep = skb->data[HCI_COMMAND_HDR_SIZE];
	}

	BT_DBG("hu %p skb %p", hu, skb);

	/* Prepend skb with frame type */
	memcpy(skb_push(skb, 1), &hci_skb_pkt_type(skb), 1);

	skb_queue_tail(&ath->txq, skb);
	set_bit(HCI_UART_SENDING, &hu->tx_state);

	schedule_work(&ath->ctxtsw);

	return 0;
}

static struct sk_buff *ath_dequeue(struct hci_uart *hu)
{
	struct ath_struct *ath = hu->priv;

	return skb_dequeue(&ath->txq);
}

static const struct hci_uart_proto athp = {
	.id		= HCI_UART_ATH3K,
	.name		= "ATH3K",
	.manufacturer	= 69,
	.open		= ath_open,
	.close		= ath_close,
	.flush		= ath_flush,
	.setup		= ath_setup,
	.recv		= ath_recv,
	.enqueue	= ath_enqueue,
	.dequeue	= ath_dequeue,
};

int __init ath_init(void)
{
	return hci_uart_register_proto(&athp);
}

int __exit ath_deinit(void)
{
	return hci_uart_unregister_proto(&athp);
}
