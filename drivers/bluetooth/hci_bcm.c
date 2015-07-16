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
#include <linux/firmware.h>

#include <net/bluetooth/bluetooth.h>
#include <net/bluetooth/hci_core.h>

#include "btbcm.h"
#include "hci_uart.h"

struct bcm_data {
	struct sk_buff *rx_skb;
	struct sk_buff_head txq;
};

static int bcm_set_baudrate(struct hci_uart *hu, unsigned int speed)
{
	struct hci_dev *hdev = hu->hdev;
	struct sk_buff *skb;
	struct bcm_update_uart_baud_rate param;

	if (speed > 3000000) {
		struct bcm_write_uart_clock_setting clock;

		clock.type = BCM_UART_CLOCK_48MHZ;

		BT_DBG("%s: Set Controller clock (%d)", hdev->name, clock.type);

		/* This Broadcom specific command changes the UART's controller
		 * clock for baud rate > 3000000.
		 */
		skb = __hci_cmd_sync(hdev, 0xfc45, 1, &clock, HCI_INIT_TIMEOUT);
		if (IS_ERR(skb)) {
			int err = PTR_ERR(skb);
			BT_ERR("%s: BCM: failed to write clock command (%d)",
			       hdev->name, err);
			return err;
		}

		kfree_skb(skb);
	}

	BT_DBG("%s: Set Controller UART speed to %d bit/s", hdev->name, speed);

	param.zero = cpu_to_le16(0);
	param.baud_rate = cpu_to_le32(speed);

	/* This Broadcom specific command changes the UART's controller baud
	 * rate.
	 */
	skb = __hci_cmd_sync(hdev, 0xfc18, sizeof(param), &param,
			     HCI_INIT_TIMEOUT);
	if (IS_ERR(skb)) {
		int err = PTR_ERR(skb);
		BT_ERR("%s: BCM: failed to write update baudrate command (%d)",
		       hdev->name, err);
		return err;
	}

	kfree_skb(skb);

	return 0;
}

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
	char fw_name[64];
	const struct firmware *fw;
	unsigned int speed;
	int err;

	BT_DBG("hu %p", hu);

	hu->hdev->set_bdaddr = btbcm_set_bdaddr;

	err = btbcm_initialize(hu->hdev, fw_name, sizeof(fw_name));
	if (err)
		return err;

	err = request_firmware(&fw, fw_name, &hu->hdev->dev);
	if (err < 0) {
		BT_INFO("%s: BCM: Patch %s not found", hu->hdev->name, fw_name);
		return 0;
	}

	err = btbcm_patchram(hu->hdev, fw);
	if (err) {
		BT_INFO("%s: BCM: Patch failed (%d)", hu->hdev->name, err);
		goto finalize;
	}

	/* Init speed if any */
	if (hu->init_speed)
		speed = hu->init_speed;
	else if (hu->proto->init_speed)
		speed = hu->proto->init_speed;
	else
		speed = 0;

	if (speed)
		hci_uart_set_baudrate(hu, speed);

	/* Operational speed if any */
	if (hu->oper_speed)
		speed = hu->oper_speed;
	else if (hu->proto->oper_speed)
		speed = hu->proto->oper_speed;
	else
		speed = 0;

	if (speed) {
		err = bcm_set_baudrate(hu, speed);
		if (!err)
			hci_uart_set_baudrate(hu, speed);
	}

finalize:
	release_firmware(fw);

	err = btbcm_finalize(hu->hdev);

	return err;
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
		bcm->rx_skb = NULL;
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
	.init_speed	= 115200,
	.oper_speed	= 4000000,
	.open		= bcm_open,
	.close		= bcm_close,
	.flush		= bcm_flush,
	.setup		= bcm_setup,
	.set_baudrate	= bcm_set_baudrate,
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
