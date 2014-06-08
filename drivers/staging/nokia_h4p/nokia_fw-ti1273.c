/*
 * This file is part of Nokia H4P bluetooth driver
 *
 * Copyright (C) 2009 Nokia Corporation.
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

static struct sk_buff_head *fw_q;

void hci_h4p_ti1273_parse_fw_event(struct hci_h4p_info *info,
			struct sk_buff *skb)
{
	struct sk_buff *fw_skb;
	unsigned long flags;

	if (skb->data[5] != 0x00) {
		dev_err(info->dev, "Firmware sending command failed 0x%.2x\n",
			skb->data[5]);
		info->fw_error = -EPROTO;
	}

	kfree_skb(skb);

	fw_skb = skb_dequeue(fw_q);
	if (fw_skb == NULL || info->fw_error) {
		complete(&info->fw_completion);
		return;
	}

	skb_queue_tail(&info->txq, fw_skb);
	spin_lock_irqsave(&info->lock, flags);
	hci_h4p_outb(info, UART_IER, hci_h4p_inb(info, UART_IER) |
			UART_IER_THRI);
	spin_unlock_irqrestore(&info->lock, flags);
}


int hci_h4p_ti1273_send_fw(struct hci_h4p_info *info,
			struct sk_buff_head *fw_queue)
{
	struct sk_buff *skb;
	unsigned long flags, time;

	info->fw_error = 0;

	BT_DBG("Sending firmware");

	time = jiffies;

	fw_q = fw_queue;
	skb = skb_dequeue(fw_queue);
	if (!skb)
		return -ENODATA;

	BT_DBG("Sending commands");
	/* Check if this is bd_address packet */
	init_completion(&info->fw_completion);
	hci_h4p_smart_idle(info, 0);
	skb_queue_tail(&info->txq, skb);
	spin_lock_irqsave(&info->lock, flags);
	hci_h4p_outb(info, UART_IER, hci_h4p_inb(info, UART_IER) |
			UART_IER_THRI);
	spin_unlock_irqrestore(&info->lock, flags);

	if (!wait_for_completion_timeout(&info->fw_completion,
				msecs_to_jiffies(2000))) {
		dev_err(info->dev, "No reply to fw command\n");
		return -ETIMEDOUT;
	}

	if (info->fw_error) {
		dev_err(info->dev, "FW error\n");
		return -EPROTO;
	}

	BT_DBG("Firmware sent in %d msecs",
		   jiffies_to_msecs(jiffies-time));

	hci_h4p_set_auto_ctsrts(info, 0, UART_EFR_RTS);
	hci_h4p_set_rts(info, 0);
	hci_h4p_change_speed(info, BC4_MAX_BAUD_RATE);
	if (hci_h4p_wait_for_cts(info, 1, 100)) {
		dev_err(info->dev,
			"cts didn't go down after final speed change\n");
		return -ETIMEDOUT;
	}
	hci_h4p_set_auto_ctsrts(info, 1, UART_EFR_RTS);

	return 0;
}
