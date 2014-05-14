/*
 * This file is part of Nokia H4P bluetooth driver
 *
 * Copyright (C) 2005-2008 Nokia Corporation.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 */

#include <linux/skbuff.h>
#include <linux/delay.h>
#include <linux/serial_reg.h>

#include "hci_h4p.h"

void hci_h4p_bc4_parse_fw_event(struct hci_h4p_info *info, struct sk_buff *skb)
{
	/* Check if this is fw packet */
	if (skb->data[0] != 0xff) {
		hci_recv_frame(info->hdev, skb);
		return;
	}

	if (skb->data[11] || skb->data[12]) {
		dev_err(info->dev, "Firmware sending command failed\n");
		info->fw_error = -EPROTO;
	}

	kfree_skb(skb);
	complete(&info->fw_completion);
}

int hci_h4p_bc4_send_fw(struct hci_h4p_info *info,
			struct sk_buff_head *fw_queue)
{
	static const u8 nokia_oui[3] = {0x00, 0x19, 0x4F};
	struct sk_buff *skb;
	unsigned int offset;
	int retries, count, i, not_valid;
	unsigned long flags;

	info->fw_error = 0;

	BT_DBG("Sending firmware");
	skb = skb_dequeue(fw_queue);

	if (!skb)
		return -ENOMSG;

	/* Check if this is bd_address packet */
	if (skb->data[15] == 0x01 && skb->data[16] == 0x00) {
		offset = 21;
		skb->data[offset + 1] = 0x00;
		skb->data[offset + 5] = 0x00;

		not_valid = 1;
		for (i = 0; i < 6; i++) {
			if (info->bd_addr[i] != 0x00) {
				not_valid = 0;
				break;
			}
		}

		if (not_valid) {
			dev_info(info->dev, "Valid bluetooth address not found, setting some random\n");
			/* When address is not valid, use some random */
			memcpy(info->bd_addr, nokia_oui, 3);
			get_random_bytes(info->bd_addr + 3, 3);
		}

		skb->data[offset + 7] = info->bd_addr[0];
		skb->data[offset + 6] = info->bd_addr[1];
		skb->data[offset + 4] = info->bd_addr[2];
		skb->data[offset + 0] = info->bd_addr[3];
		skb->data[offset + 3] = info->bd_addr[4];
		skb->data[offset + 2] = info->bd_addr[5];
	}

	for (count = 1; ; count++) {
		BT_DBG("Sending firmware command %d", count);
		init_completion(&info->fw_completion);
		skb_queue_tail(&info->txq, skb);
		spin_lock_irqsave(&info->lock, flags);
		hci_h4p_outb(info, UART_IER, hci_h4p_inb(info, UART_IER) |
							 UART_IER_THRI);
		spin_unlock_irqrestore(&info->lock, flags);

		skb = skb_dequeue(fw_queue);
		if (!skb)
			break;

		if (!wait_for_completion_timeout(&info->fw_completion,
						 msecs_to_jiffies(1000))) {
			dev_err(info->dev, "No reply to fw command\n");
			return -ETIMEDOUT;
		}

		if (info->fw_error) {
			dev_err(info->dev, "FW error\n");
			return -EPROTO;
		}
	};

	/* Wait for chip warm reset */
	retries = 100;
	while ((!skb_queue_empty(&info->txq) ||
	       !(hci_h4p_inb(info, UART_LSR) & UART_LSR_TEMT)) &&
	       retries--) {
		msleep(10);
	}
	if (!retries) {
		dev_err(info->dev, "Transmitter not empty\n");
		return -ETIMEDOUT;
	}

	hci_h4p_change_speed(info, BC4_MAX_BAUD_RATE);

	if (hci_h4p_wait_for_cts(info, 1, 100)) {
		dev_err(info->dev, "cts didn't deassert after final speed\n");
		return -ETIMEDOUT;
	}

	retries = 100;
	do {
		init_completion(&info->init_completion);
		hci_h4p_send_alive_packet(info);
		retries--;
	} while (!wait_for_completion_timeout(&info->init_completion, 100) &&
		 retries > 0);

	if (!retries) {
		dev_err(info->dev, "No alive reply after speed change\n");
		return -ETIMEDOUT;
	}

	return 0;
}
