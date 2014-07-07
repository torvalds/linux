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
 * Thanks to all the Nokia people that helped with this driver,
 * including Ville Tervo and Roger Quadros.
 *
 * Power saving functionality was removed from this driver to make
 * merging easier.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/errno.h>
#include <linux/delay.h>
#include <linux/spinlock.h>
#include <linux/serial_reg.h>
#include <linux/skbuff.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/clk.h>
#include <linux/interrupt.h>
#include <linux/gpio.h>
#include <linux/timer.h>
#include <linux/kthread.h>
#include <linux/io.h>
#include <linux/completion.h>
#include <linux/sizes.h>

#include <net/bluetooth/bluetooth.h>
#include <net/bluetooth/hci_core.h>
#include <net/bluetooth/hci.h>

#include <linux/platform_data/bt-nokia-h4p.h>

#include "hci_h4p.h"

/* This should be used in function that cannot release clocks */
static void hci_h4p_set_clk(struct hci_h4p_info *info, int *clock, int enable)
{
	unsigned long flags;

	spin_lock_irqsave(&info->clocks_lock, flags);
	if (enable && !*clock) {
		BT_DBG("Enabling %p", clock);
		clk_prepare_enable(info->uart_fclk);
		clk_prepare_enable(info->uart_iclk);
		if (atomic_read(&info->clk_users) == 0)
			hci_h4p_restore_regs(info);
		atomic_inc(&info->clk_users);
	}

	if (!enable && *clock) {
		BT_DBG("Disabling %p", clock);
		if (atomic_dec_and_test(&info->clk_users))
			hci_h4p_store_regs(info);
		clk_disable_unprepare(info->uart_fclk);
		clk_disable_unprepare(info->uart_iclk);
	}

	*clock = enable;
	spin_unlock_irqrestore(&info->clocks_lock, flags);
}

static void hci_h4p_lazy_clock_release(unsigned long data)
{
	struct hci_h4p_info *info = (struct hci_h4p_info *)data;
	unsigned long flags;

	spin_lock_irqsave(&info->lock, flags);
	if (!info->tx_enabled)
		hci_h4p_set_clk(info, &info->tx_clocks_en, 0);
	spin_unlock_irqrestore(&info->lock, flags);
}

/* Power management functions */
void hci_h4p_smart_idle(struct hci_h4p_info *info, bool enable)
{
	u8 v;

	v = hci_h4p_inb(info, UART_OMAP_SYSC);
	v &= ~(UART_OMAP_SYSC_IDLEMASK);

	if (enable)
		v |= UART_OMAP_SYSC_SMART_IDLE;
	else
		v |= UART_OMAP_SYSC_NO_IDLE;

	hci_h4p_outb(info, UART_OMAP_SYSC, v);
}

static inline void h4p_schedule_pm(struct hci_h4p_info *info)
{
}

static void hci_h4p_disable_tx(struct hci_h4p_info *info)
{
	if (!info->pm_enabled)
		return;

	/* Re-enable smart-idle */
	hci_h4p_smart_idle(info, 1);

	gpio_set_value(info->bt_wakeup_gpio, 0);
	mod_timer(&info->lazy_release, jiffies + msecs_to_jiffies(100));
	info->tx_enabled = 0;
}

void hci_h4p_enable_tx(struct hci_h4p_info *info)
{
	unsigned long flags;

	if (!info->pm_enabled)
		return;

	h4p_schedule_pm(info);

	spin_lock_irqsave(&info->lock, flags);
	del_timer(&info->lazy_release);
	hci_h4p_set_clk(info, &info->tx_clocks_en, 1);
	info->tx_enabled = 1;
	gpio_set_value(info->bt_wakeup_gpio, 1);
	hci_h4p_outb(info, UART_IER, hci_h4p_inb(info, UART_IER) |
		     UART_IER_THRI);
	/*
	 * Disable smart-idle as UART TX interrupts
	 * are not wake-up capable
	 */
	hci_h4p_smart_idle(info, 0);

	spin_unlock_irqrestore(&info->lock, flags);
}

static void hci_h4p_disable_rx(struct hci_h4p_info *info)
{
	if (!info->pm_enabled)
		return;

	info->rx_enabled = 0;

	if (hci_h4p_inb(info, UART_LSR) & UART_LSR_DR)
		return;

	if (!(hci_h4p_inb(info, UART_LSR) & UART_LSR_TEMT))
		return;

	__hci_h4p_set_auto_ctsrts(info, 0, UART_EFR_RTS);
	info->autorts = 0;
	hci_h4p_set_clk(info, &info->rx_clocks_en, 0);
}

static void hci_h4p_enable_rx(struct hci_h4p_info *info)
{
	if (!info->pm_enabled)
		return;

	h4p_schedule_pm(info);

	hci_h4p_set_clk(info, &info->rx_clocks_en, 1);
	info->rx_enabled = 1;

	if (!(hci_h4p_inb(info, UART_LSR) & UART_LSR_TEMT))
		return;

	__hci_h4p_set_auto_ctsrts(info, 1, UART_EFR_RTS);
	info->autorts = 1;
}

/* Negotiation functions */
int hci_h4p_send_alive_packet(struct hci_h4p_info *info)
{
	struct hci_h4p_alive_hdr *hdr;
	struct hci_h4p_alive_pkt *pkt;
	struct sk_buff *skb;
	unsigned long flags;
	int len;

	BT_DBG("Sending alive packet");

	len = H4_TYPE_SIZE + sizeof(*hdr) + sizeof(*pkt);
	skb = bt_skb_alloc(len, GFP_KERNEL);
	if (!skb)
		return -ENOMEM;

	memset(skb->data, 0x00, len);
	*skb_put(skb, 1) = H4_ALIVE_PKT;
	hdr = (struct hci_h4p_alive_hdr *)skb_put(skb, sizeof(*hdr));
	hdr->dlen = sizeof(*pkt);
	pkt = (struct hci_h4p_alive_pkt *)skb_put(skb, sizeof(*pkt));
	pkt->mid = H4P_ALIVE_REQ;

	skb_queue_tail(&info->txq, skb);
	spin_lock_irqsave(&info->lock, flags);
	hci_h4p_outb(info, UART_IER, hci_h4p_inb(info, UART_IER) |
		     UART_IER_THRI);
	spin_unlock_irqrestore(&info->lock, flags);

	BT_DBG("Alive packet sent");

	return 0;
}

static void hci_h4p_alive_packet(struct hci_h4p_info *info,
				 struct sk_buff *skb)
{
	struct hci_h4p_alive_hdr *hdr;
	struct hci_h4p_alive_pkt *pkt;

	BT_DBG("Received alive packet");
	hdr = (struct hci_h4p_alive_hdr *)skb->data;
	if (hdr->dlen != sizeof(*pkt)) {
		dev_err(info->dev, "Corrupted alive message\n");
		info->init_error = -EIO;
		goto finish_alive;
	}

	pkt = (struct hci_h4p_alive_pkt *)skb_pull(skb, sizeof(*hdr));
	if (pkt->mid != H4P_ALIVE_RESP) {
		dev_err(info->dev, "Could not negotiate hci_h4p settings\n");
		info->init_error = -EINVAL;
	}

finish_alive:
	complete(&info->init_completion);
	kfree_skb(skb);
}

static int hci_h4p_send_negotiation(struct hci_h4p_info *info)
{
	struct hci_h4p_neg_cmd *neg_cmd;
	struct hci_h4p_neg_hdr *neg_hdr;
	struct sk_buff *skb;
	unsigned long flags;
	int err, len;
	u16 sysclk;

	BT_DBG("Sending negotiation..");

	switch (info->bt_sysclk) {
	case 1:
		sysclk = 12000;
		break;
	case 2:
		sysclk = 38400;
		break;
	default:
		return -EINVAL;
	}

	len = sizeof(*neg_cmd) + sizeof(*neg_hdr) + H4_TYPE_SIZE;
	skb = bt_skb_alloc(len, GFP_KERNEL);
	if (!skb)
		return -ENOMEM;

	memset(skb->data, 0x00, len);
	*skb_put(skb, 1) = H4_NEG_PKT;
	neg_hdr = (struct hci_h4p_neg_hdr *)skb_put(skb, sizeof(*neg_hdr));
	neg_cmd = (struct hci_h4p_neg_cmd *)skb_put(skb, sizeof(*neg_cmd));

	neg_hdr->dlen = sizeof(*neg_cmd);
	neg_cmd->ack = H4P_NEG_REQ;
	neg_cmd->baud = cpu_to_le16(BT_BAUDRATE_DIVIDER/MAX_BAUD_RATE);
	neg_cmd->proto = H4P_PROTO_BYTE;
	neg_cmd->sys_clk = cpu_to_le16(sysclk);

	hci_h4p_change_speed(info, INIT_SPEED);

	hci_h4p_set_rts(info, 1);
	info->init_error = 0;
	init_completion(&info->init_completion);
	skb_queue_tail(&info->txq, skb);
	spin_lock_irqsave(&info->lock, flags);
	hci_h4p_outb(info, UART_IER, hci_h4p_inb(info, UART_IER) |
		     UART_IER_THRI);
	spin_unlock_irqrestore(&info->lock, flags);

	if (!wait_for_completion_interruptible_timeout(&info->init_completion,
				msecs_to_jiffies(1000)))
		return -ETIMEDOUT;

	if (info->init_error < 0)
		return info->init_error;

	/* Change to operational settings */
	hci_h4p_set_auto_ctsrts(info, 0, UART_EFR_RTS);
	hci_h4p_set_rts(info, 0);
	hci_h4p_change_speed(info, MAX_BAUD_RATE);

	err = hci_h4p_wait_for_cts(info, 1, 100);
	if (err < 0)
		return err;

	hci_h4p_set_auto_ctsrts(info, 1, UART_EFR_RTS);
	init_completion(&info->init_completion);
	err = hci_h4p_send_alive_packet(info);

	if (err < 0)
		return err;

	if (!wait_for_completion_interruptible_timeout(&info->init_completion,
				msecs_to_jiffies(1000)))
		return -ETIMEDOUT;

	if (info->init_error < 0)
		return info->init_error;

	BT_DBG("Negotiation successful");
	return 0;
}

static void hci_h4p_negotiation_packet(struct hci_h4p_info *info,
				       struct sk_buff *skb)
{
	struct hci_h4p_neg_hdr *hdr;
	struct hci_h4p_neg_evt *evt;

	hdr = (struct hci_h4p_neg_hdr *)skb->data;
	if (hdr->dlen != sizeof(*evt)) {
		info->init_error = -EIO;
		goto finish_neg;
	}

	evt = (struct hci_h4p_neg_evt *)skb_pull(skb, sizeof(*hdr));

	if (evt->ack != H4P_NEG_ACK) {
		dev_err(info->dev, "Could not negotiate hci_h4p settings\n");
		info->init_error = -EINVAL;
	}

	info->man_id = evt->man_id;
	info->ver_id = evt->ver_id;

finish_neg:

	complete(&info->init_completion);
	kfree_skb(skb);
}

/* H4 packet handling functions */
static int hci_h4p_get_hdr_len(struct hci_h4p_info *info, u8 pkt_type)
{
	long retval;

	switch (pkt_type) {
	case H4_EVT_PKT:
		retval = HCI_EVENT_HDR_SIZE;
		break;
	case H4_ACL_PKT:
		retval = HCI_ACL_HDR_SIZE;
		break;
	case H4_SCO_PKT:
		retval = HCI_SCO_HDR_SIZE;
		break;
	case H4_NEG_PKT:
		retval = H4P_NEG_HDR_SIZE;
		break;
	case H4_ALIVE_PKT:
		retval = H4P_ALIVE_HDR_SIZE;
		break;
	case H4_RADIO_PKT:
		retval = H4_RADIO_HDR_SIZE;
		break;
	default:
		dev_err(info->dev, "Unknown H4 packet type 0x%.2x\n", pkt_type);
		retval = -1;
		break;
	}

	return retval;
}

static unsigned int hci_h4p_get_data_len(struct hci_h4p_info *info,
					 struct sk_buff *skb)
{
	long retval = -1;
	struct hci_acl_hdr *acl_hdr;
	struct hci_sco_hdr *sco_hdr;
	struct hci_event_hdr *evt_hdr;
	struct hci_h4p_neg_hdr *neg_hdr;
	struct hci_h4p_alive_hdr *alive_hdr;
	struct hci_h4p_radio_hdr *radio_hdr;

	switch (bt_cb(skb)->pkt_type) {
	case H4_EVT_PKT:
		evt_hdr = (struct hci_event_hdr *)skb->data;
		retval = evt_hdr->plen;
		break;
	case H4_ACL_PKT:
		acl_hdr = (struct hci_acl_hdr *)skb->data;
		retval = le16_to_cpu(acl_hdr->dlen);
		break;
	case H4_SCO_PKT:
		sco_hdr = (struct hci_sco_hdr *)skb->data;
		retval = sco_hdr->dlen;
		break;
	case H4_RADIO_PKT:
		radio_hdr = (struct hci_h4p_radio_hdr *)skb->data;
		retval = radio_hdr->dlen;
		break;
	case H4_NEG_PKT:
		neg_hdr = (struct hci_h4p_neg_hdr *)skb->data;
		retval = neg_hdr->dlen;
		break;
	case H4_ALIVE_PKT:
		alive_hdr = (struct hci_h4p_alive_hdr *)skb->data;
		retval = alive_hdr->dlen;
		break;
	}

	return retval;
}

static inline void hci_h4p_recv_frame(struct hci_h4p_info *info,
				      struct sk_buff *skb)
{
	if (unlikely(!test_bit(HCI_RUNNING, &info->hdev->flags))) {
		switch (bt_cb(skb)->pkt_type) {
		case H4_NEG_PKT:
			hci_h4p_negotiation_packet(info, skb);
			info->rx_state = WAIT_FOR_PKT_TYPE;
			return;
		case H4_ALIVE_PKT:
			hci_h4p_alive_packet(info, skb);
			info->rx_state = WAIT_FOR_PKT_TYPE;
			return;
		}

		if (!test_bit(HCI_UP, &info->hdev->flags)) {
			BT_DBG("fw_event");
			hci_h4p_parse_fw_event(info, skb);
			return;
		}
	}

	hci_recv_frame(info->hdev, skb);
	BT_DBG("Frame sent to upper layer");
}

static inline void hci_h4p_handle_byte(struct hci_h4p_info *info, u8 byte)
{
	switch (info->rx_state) {
	case WAIT_FOR_PKT_TYPE:
		bt_cb(info->rx_skb)->pkt_type = byte;
		info->rx_count = hci_h4p_get_hdr_len(info, byte);
		if (info->rx_count < 0) {
			info->hdev->stat.err_rx++;
			kfree_skb(info->rx_skb);
			info->rx_skb = NULL;
		} else {
			info->rx_state = WAIT_FOR_HEADER;
		}
		break;
	case WAIT_FOR_HEADER:
		info->rx_count--;
		*skb_put(info->rx_skb, 1) = byte;
		if (info->rx_count != 0)
			break;
		info->rx_count = hci_h4p_get_data_len(info, info->rx_skb);
		if (info->rx_count > skb_tailroom(info->rx_skb)) {
			dev_err(info->dev, "frame too long\n");
			info->garbage_bytes = info->rx_count
				- skb_tailroom(info->rx_skb);
			kfree_skb(info->rx_skb);
			info->rx_skb = NULL;
			break;
		}
		info->rx_state = WAIT_FOR_DATA;
		break;
	case WAIT_FOR_DATA:
		info->rx_count--;
		*skb_put(info->rx_skb, 1) = byte;
		break;
	default:
		WARN_ON(1);
		break;
	}

	if (info->rx_count == 0) {
		/* H4+ devices should always send word aligned packets */
		if (!(info->rx_skb->len % 2))
			info->garbage_bytes++;
		hci_h4p_recv_frame(info, info->rx_skb);
		info->rx_skb = NULL;
	}
}

static void hci_h4p_rx_tasklet(unsigned long data)
{
	u8 byte;
	struct hci_h4p_info *info = (struct hci_h4p_info *)data;

	BT_DBG("tasklet woke up");
	BT_DBG("rx_tasklet woke up");

	while (hci_h4p_inb(info, UART_LSR) & UART_LSR_DR) {
		byte = hci_h4p_inb(info, UART_RX);
		if (info->garbage_bytes) {
			info->garbage_bytes--;
			continue;
		}
		if (info->rx_skb == NULL) {
			info->rx_skb = bt_skb_alloc(HCI_MAX_FRAME_SIZE,
						    GFP_ATOMIC | GFP_DMA);
			if (!info->rx_skb) {
				dev_err(info->dev,
					"No memory for new packet\n");
				goto finish_rx;
			}
			info->rx_state = WAIT_FOR_PKT_TYPE;
			info->rx_skb->dev = (void *)info->hdev;
		}
		info->hdev->stat.byte_rx++;
		hci_h4p_handle_byte(info, byte);
	}

	if (!info->rx_enabled) {
		if (hci_h4p_inb(info, UART_LSR) & UART_LSR_TEMT &&
						  info->autorts) {
			__hci_h4p_set_auto_ctsrts(info, 0 , UART_EFR_RTS);
			info->autorts = 0;
		}
		/* Flush posted write to avoid spurious interrupts */
		hci_h4p_inb(info, UART_OMAP_SCR);
		hci_h4p_set_clk(info, &info->rx_clocks_en, 0);
	}

finish_rx:
	BT_DBG("rx_ended");
}

static void hci_h4p_tx_tasklet(unsigned long data)
{
	unsigned int sent = 0;
	struct sk_buff *skb;
	struct hci_h4p_info *info = (struct hci_h4p_info *)data;

	BT_DBG("tasklet woke up");
	BT_DBG("tx_tasklet woke up");

	if (info->autorts != info->rx_enabled) {
		if (hci_h4p_inb(info, UART_LSR) & UART_LSR_TEMT) {
			if (info->autorts && !info->rx_enabled) {
				__hci_h4p_set_auto_ctsrts(info, 0,
							  UART_EFR_RTS);
				info->autorts = 0;
			}
			if (!info->autorts && info->rx_enabled) {
				__hci_h4p_set_auto_ctsrts(info, 1,
							  UART_EFR_RTS);
				info->autorts = 1;
			}
		} else {
			hci_h4p_outb(info, UART_OMAP_SCR,
				     hci_h4p_inb(info, UART_OMAP_SCR) |
				     UART_OMAP_SCR_EMPTY_THR);
			goto finish_tx;
		}
	}

	skb = skb_dequeue(&info->txq);
	if (!skb) {
		/* No data in buffer */
		BT_DBG("skb ready");
		if (hci_h4p_inb(info, UART_LSR) & UART_LSR_TEMT) {
			hci_h4p_outb(info, UART_IER,
				     hci_h4p_inb(info, UART_IER) &
				     ~UART_IER_THRI);
			hci_h4p_inb(info, UART_OMAP_SCR);
			hci_h4p_disable_tx(info);
			return;
		}
		hci_h4p_outb(info, UART_OMAP_SCR,
			     hci_h4p_inb(info, UART_OMAP_SCR) |
			     UART_OMAP_SCR_EMPTY_THR);
		goto finish_tx;
	}

	/* Copy data to tx fifo */
	while (!(hci_h4p_inb(info, UART_OMAP_SSR) & UART_OMAP_SSR_TXFULL) &&
	       (sent < skb->len)) {
		hci_h4p_outb(info, UART_TX, skb->data[sent]);
		sent++;
	}

	info->hdev->stat.byte_tx += sent;
	if (skb->len == sent) {
		kfree_skb(skb);
	} else {
		skb_pull(skb, sent);
		skb_queue_head(&info->txq, skb);
	}

	hci_h4p_outb(info, UART_OMAP_SCR, hci_h4p_inb(info, UART_OMAP_SCR) &
						     ~UART_OMAP_SCR_EMPTY_THR);
	hci_h4p_outb(info, UART_IER, hci_h4p_inb(info, UART_IER) |
						 UART_IER_THRI);

finish_tx:
	/* Flush posted write to avoid spurious interrupts */
	hci_h4p_inb(info, UART_OMAP_SCR);

}

static irqreturn_t hci_h4p_interrupt(int irq, void *data)
{
	struct hci_h4p_info *info = (struct hci_h4p_info *)data;
	u8 iir, msr;
	int ret;

	ret = IRQ_NONE;

	iir = hci_h4p_inb(info, UART_IIR);
	if (iir & UART_IIR_NO_INT)
		return IRQ_HANDLED;

	BT_DBG("In interrupt handler iir 0x%.2x", iir);

	iir &= UART_IIR_ID;

	if (iir == UART_IIR_MSI) {
		msr = hci_h4p_inb(info, UART_MSR);
		ret = IRQ_HANDLED;
	}
	if (iir == UART_IIR_RLSI) {
		hci_h4p_inb(info, UART_RX);
		hci_h4p_inb(info, UART_LSR);
		ret = IRQ_HANDLED;
	}

	if (iir == UART_IIR_RDI) {
		hci_h4p_rx_tasklet((unsigned long)data);
		ret = IRQ_HANDLED;
	}

	if (iir == UART_IIR_THRI) {
		hci_h4p_tx_tasklet((unsigned long)data);
		ret = IRQ_HANDLED;
	}

	return ret;
}

static irqreturn_t hci_h4p_wakeup_interrupt(int irq, void *dev_inst)
{
	struct hci_h4p_info *info = dev_inst;
	int should_wakeup;
	struct hci_dev *hdev;

	if (!info->hdev)
		return IRQ_HANDLED;

	should_wakeup = gpio_get_value(info->host_wakeup_gpio);
	hdev = info->hdev;

	if (!test_bit(HCI_RUNNING, &hdev->flags)) {
		if (should_wakeup == 1)
			complete_all(&info->test_completion);

		return IRQ_HANDLED;
	}

	BT_DBG("gpio interrupt %d", should_wakeup);

	/* Check if wee have missed some interrupts */
	if (info->rx_enabled == should_wakeup)
		return IRQ_HANDLED;

	if (should_wakeup)
		hci_h4p_enable_rx(info);
	else
		hci_h4p_disable_rx(info);

	return IRQ_HANDLED;
}

static inline void hci_h4p_set_pm_limits(struct hci_h4p_info *info, bool set)
{
	struct hci_h4p_platform_data *bt_plat_data = info->dev->platform_data;
	const char *sset = set ? "set" : "clear";

	if (unlikely(!bt_plat_data || !bt_plat_data->set_pm_limits))
		return;

	if (set != !!test_bit(H4P_ACTIVE_MODE, &info->pm_flags)) {
		bt_plat_data->set_pm_limits(info->dev, set);
		if (set)
			set_bit(H4P_ACTIVE_MODE, &info->pm_flags);
		else
			clear_bit(H4P_ACTIVE_MODE, &info->pm_flags);
		BT_DBG("Change pm constraints to: %s", sset);
		return;
	}

	BT_DBG("pm constraints remains: %s", sset);
}

static int hci_h4p_reset(struct hci_h4p_info *info)
{
	int err;

	err = hci_h4p_reset_uart(info);
	if (err < 0) {
		dev_err(info->dev, "Uart reset failed\n");
		return err;
	}
	hci_h4p_init_uart(info);
	hci_h4p_set_rts(info, 0);

	gpio_set_value(info->reset_gpio, 0);
	gpio_set_value(info->bt_wakeup_gpio, 1);
	msleep(10);

	if (gpio_get_value(info->host_wakeup_gpio) == 1) {
		dev_err(info->dev, "host_wakeup_gpio not low\n");
		return -EPROTO;
	}

	init_completion(&info->test_completion);
	gpio_set_value(info->reset_gpio, 1);

	if (!wait_for_completion_interruptible_timeout(&info->test_completion,
						       msecs_to_jiffies(100))) {
		dev_err(info->dev, "wakeup test timed out\n");
		complete_all(&info->test_completion);
		return -EPROTO;
	}

	err = hci_h4p_wait_for_cts(info, 1, 100);
	if (err < 0) {
		dev_err(info->dev, "No cts from bt chip\n");
		return err;
	}

	hci_h4p_set_rts(info, 1);

	return 0;
}

/* hci callback functions */
static int hci_h4p_hci_flush(struct hci_dev *hdev)
{
	struct hci_h4p_info *info = hci_get_drvdata(hdev);
	skb_queue_purge(&info->txq);

	return 0;
}

static int hci_h4p_bt_wakeup_test(struct hci_h4p_info *info)
{
	/*
	 * Test Sequence:
	 * Host de-asserts the BT_WAKE_UP line.
	 * Host polls the UART_CTS line, waiting for it to be de-asserted.
	 * Host asserts the BT_WAKE_UP line.
	 * Host polls the UART_CTS line, waiting for it to be asserted.
	 * Host de-asserts the BT_WAKE_UP line (allow the Bluetooth device to
	 * sleep).
	 * Host polls the UART_CTS line, waiting for it to be de-asserted.
	 */
	int err;
	int ret = -ECOMM;

	if (!info)
		return -EINVAL;

	/* Disable wakeup interrupts */
	disable_irq(gpio_to_irq(info->host_wakeup_gpio));

	gpio_set_value(info->bt_wakeup_gpio, 0);
	err = hci_h4p_wait_for_cts(info, 0, 100);
	if (err) {
		dev_warn(info->dev,
				"bt_wakeup_test: fail: CTS low timed out: %d\n",
				err);
		goto out;
	}

	gpio_set_value(info->bt_wakeup_gpio, 1);
	err = hci_h4p_wait_for_cts(info, 1, 100);
	if (err) {
		dev_warn(info->dev,
				"bt_wakeup_test: fail: CTS high timed out: %d\n",
				err);
		goto out;
	}

	gpio_set_value(info->bt_wakeup_gpio, 0);
	err = hci_h4p_wait_for_cts(info, 0, 100);
	if (err) {
		dev_warn(info->dev,
				"bt_wakeup_test: fail: CTS re-low timed out: %d\n",
				err);
		goto out;
	}

	ret = 0;

out:

	/* Re-enable wakeup interrupts */
	enable_irq(gpio_to_irq(info->host_wakeup_gpio));

	return ret;
}

static int hci_h4p_hci_open(struct hci_dev *hdev)
{
	struct hci_h4p_info *info;
	int err, retries = 0;
	struct sk_buff_head fw_queue;
	unsigned long flags;

	info = hci_get_drvdata(hdev);

	if (test_bit(HCI_RUNNING, &hdev->flags))
		return 0;

	/* TI1271 has HW bug and boot up might fail. Retry up to three times */
again:

	info->rx_enabled = 1;
	info->rx_state = WAIT_FOR_PKT_TYPE;
	info->rx_count = 0;
	info->garbage_bytes = 0;
	info->rx_skb = NULL;
	info->pm_enabled = 0;
	init_completion(&info->fw_completion);
	hci_h4p_set_clk(info, &info->tx_clocks_en, 1);
	hci_h4p_set_clk(info, &info->rx_clocks_en, 1);
	skb_queue_head_init(&fw_queue);

	err = hci_h4p_reset(info);
	if (err < 0)
		goto err_clean;

	hci_h4p_set_auto_ctsrts(info, 1, UART_EFR_CTS | UART_EFR_RTS);
	info->autorts = 1;

	err = hci_h4p_send_negotiation(info);

	err = hci_h4p_read_fw(info, &fw_queue);
	if (err < 0) {
		dev_err(info->dev, "Cannot read firmware\n");
		goto err_clean;
	}

	err = hci_h4p_send_fw(info, &fw_queue);
	if (err < 0) {
		dev_err(info->dev, "Sending firmware failed.\n");
		goto err_clean;
	}

	info->pm_enabled = 1;

	err = hci_h4p_bt_wakeup_test(info);
	if (err < 0) {
		dev_err(info->dev, "BT wakeup test failed.\n");
		goto err_clean;
	}

	spin_lock_irqsave(&info->lock, flags);
	info->rx_enabled = gpio_get_value(info->host_wakeup_gpio);
	hci_h4p_set_clk(info, &info->rx_clocks_en, info->rx_enabled);
	spin_unlock_irqrestore(&info->lock, flags);

	hci_h4p_set_clk(info, &info->tx_clocks_en, 0);

	kfree_skb(info->alive_cmd_skb);
	info->alive_cmd_skb = NULL;
	set_bit(HCI_RUNNING, &hdev->flags);

	BT_DBG("hci up and running");
	return 0;

err_clean:
	hci_h4p_hci_flush(hdev);
	hci_h4p_reset_uart(info);
	del_timer_sync(&info->lazy_release);
	hci_h4p_set_clk(info, &info->tx_clocks_en, 0);
	hci_h4p_set_clk(info, &info->rx_clocks_en, 0);
	gpio_set_value(info->reset_gpio, 0);
	gpio_set_value(info->bt_wakeup_gpio, 0);
	skb_queue_purge(&fw_queue);
	kfree_skb(info->alive_cmd_skb);
	info->alive_cmd_skb = NULL;
	kfree_skb(info->rx_skb);
	info->rx_skb = NULL;

	if (retries++ < 3) {
		dev_err(info->dev, "FW loading try %d fail. Retry.\n", retries);
		goto again;
	}

	return err;
}

static int hci_h4p_hci_close(struct hci_dev *hdev)
{
	struct hci_h4p_info *info = hci_get_drvdata(hdev);

	if (!test_and_clear_bit(HCI_RUNNING, &hdev->flags))
		return 0;

	hci_h4p_hci_flush(hdev);
	hci_h4p_set_clk(info, &info->tx_clocks_en, 1);
	hci_h4p_set_clk(info, &info->rx_clocks_en, 1);
	hci_h4p_reset_uart(info);
	del_timer_sync(&info->lazy_release);
	hci_h4p_set_clk(info, &info->tx_clocks_en, 0);
	hci_h4p_set_clk(info, &info->rx_clocks_en, 0);
	gpio_set_value(info->reset_gpio, 0);
	gpio_set_value(info->bt_wakeup_gpio, 0);
	kfree_skb(info->rx_skb);

	return 0;
}

static int hci_h4p_hci_send_frame(struct hci_dev *hdev, struct sk_buff *skb)
{
	struct hci_h4p_info *info;
	int err = 0;

	BT_DBG("dev %p, skb %p", hdev, skb);

	info = hci_get_drvdata(hdev);

	if (!test_bit(HCI_RUNNING, &hdev->flags)) {
		dev_warn(info->dev, "Frame for non-running device\n");
		return -EIO;
	}

	switch (bt_cb(skb)->pkt_type) {
	case HCI_COMMAND_PKT:
		hdev->stat.cmd_tx++;
		break;
	case HCI_ACLDATA_PKT:
		hdev->stat.acl_tx++;
		break;
	case HCI_SCODATA_PKT:
		hdev->stat.sco_tx++;
		break;
	}

	/* Push frame type to skb */
	*skb_push(skb, 1) = (bt_cb(skb)->pkt_type);
	/* We should allways send word aligned data to h4+ devices */
	if (skb->len % 2) {
		err = skb_pad(skb, 1);
		if (!err)
			*skb_put(skb, 1) = 0x00;
	}
	if (err)
		return err;

	skb_queue_tail(&info->txq, skb);
	hci_h4p_enable_tx(info);

	return 0;
}

static ssize_t hci_h4p_store_bdaddr(struct device *dev,
				    struct device_attribute *attr,
				    const char *buf, size_t count)
{
	struct hci_h4p_info *info = dev_get_drvdata(dev);
	unsigned int bdaddr[6];
	int ret, i;

	ret = sscanf(buf, "%2x:%2x:%2x:%2x:%2x:%2x\n",
			&bdaddr[0], &bdaddr[1], &bdaddr[2],
			&bdaddr[3], &bdaddr[4], &bdaddr[5]);

	if (ret != 6)
		return -EINVAL;

	for (i = 0; i < 6; i++) {
		if (bdaddr[i] > 0xff)
			return -EINVAL;
		info->bd_addr[i] = bdaddr[i] & 0xff;
	}

	return count;
}

static ssize_t hci_h4p_show_bdaddr(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	struct hci_h4p_info *info = dev_get_drvdata(dev);

	return sprintf(buf, "%pMR\n", info->bd_addr);
}

static DEVICE_ATTR(bdaddr, S_IRUGO | S_IWUSR, hci_h4p_show_bdaddr,
		   hci_h4p_store_bdaddr);

static int hci_h4p_sysfs_create_files(struct device *dev)
{
	return device_create_file(dev, &dev_attr_bdaddr);
}

static void hci_h4p_sysfs_remove_files(struct device *dev)
{
	device_remove_file(dev, &dev_attr_bdaddr);
}

static int hci_h4p_register_hdev(struct hci_h4p_info *info)
{
	struct hci_dev *hdev;

	/* Initialize and register HCI device */

	hdev = hci_alloc_dev();
	if (!hdev) {
		dev_err(info->dev, "Can't allocate memory for device\n");
		return -ENOMEM;
	}
	info->hdev = hdev;

	hdev->bus = HCI_UART;
	hci_set_drvdata(hdev, info);

	hdev->open = hci_h4p_hci_open;
	hdev->close = hci_h4p_hci_close;
	hdev->flush = hci_h4p_hci_flush;
	hdev->send = hci_h4p_hci_send_frame;
	set_bit(HCI_QUIRK_RESET_ON_CLOSE, &hdev->quirks);

	SET_HCIDEV_DEV(hdev, info->dev);

	if (hci_h4p_sysfs_create_files(info->dev) < 0) {
		dev_err(info->dev, "failed to create sysfs files\n");
		goto free;
	}

	if (hci_register_dev(hdev) >= 0)
		return 0;

	dev_err(info->dev, "hci_register failed %s.\n", hdev->name);
	hci_h4p_sysfs_remove_files(info->dev);
free:
	hci_free_dev(info->hdev);
	return -ENODEV;
}

static int hci_h4p_probe(struct platform_device *pdev)
{
	struct hci_h4p_platform_data *bt_plat_data;
	struct hci_h4p_info *info;
	int err;

	dev_info(&pdev->dev, "Registering HCI H4P device\n");
	info = devm_kzalloc(&pdev->dev, sizeof(struct hci_h4p_info),
			GFP_KERNEL);
	if (!info)
		return -ENOMEM;

	info->dev = &pdev->dev;
	info->tx_enabled = 1;
	info->rx_enabled = 1;
	spin_lock_init(&info->lock);
	spin_lock_init(&info->clocks_lock);
	skb_queue_head_init(&info->txq);

	if (pdev->dev.platform_data == NULL) {
		dev_err(&pdev->dev, "Could not get Bluetooth config data\n");
		return -ENODATA;
	}

	bt_plat_data = pdev->dev.platform_data;
	info->chip_type = bt_plat_data->chip_type;
	info->bt_wakeup_gpio = bt_plat_data->bt_wakeup_gpio;
	info->host_wakeup_gpio = bt_plat_data->host_wakeup_gpio;
	info->reset_gpio = bt_plat_data->reset_gpio;
	info->reset_gpio_shared = bt_plat_data->reset_gpio_shared;
	info->bt_sysclk = bt_plat_data->bt_sysclk;

	BT_DBG("RESET gpio: %d", info->reset_gpio);
	BT_DBG("BTWU gpio: %d", info->bt_wakeup_gpio);
	BT_DBG("HOSTWU gpio: %d", info->host_wakeup_gpio);
	BT_DBG("sysclk: %d", info->bt_sysclk);

	init_completion(&info->test_completion);
	complete_all(&info->test_completion);

	if (!info->reset_gpio_shared) {
		err = devm_gpio_request_one(&pdev->dev, info->reset_gpio,
					    GPIOF_OUT_INIT_LOW, "bt_reset");
		if (err < 0) {
			dev_err(&pdev->dev, "Cannot get GPIO line %d\n",
				info->reset_gpio);
			return err;
		}
	}

	err = devm_gpio_request_one(&pdev->dev, info->bt_wakeup_gpio,
				    GPIOF_OUT_INIT_LOW, "bt_wakeup");

	if (err < 0) {
		dev_err(info->dev, "Cannot get GPIO line 0x%d",
			info->bt_wakeup_gpio);
		return err;
	}

	err = devm_gpio_request_one(&pdev->dev, info->host_wakeup_gpio,
				    GPIOF_DIR_IN, "host_wakeup");
	if (err < 0) {
		dev_err(info->dev, "Cannot get GPIO line %d",
		       info->host_wakeup_gpio);
		return err;
	}

	info->irq = bt_plat_data->uart_irq;
	info->uart_base = devm_ioremap(&pdev->dev, bt_plat_data->uart_base,
					SZ_2K);
	info->uart_iclk = devm_clk_get(&pdev->dev, bt_plat_data->uart_iclk);
	info->uart_fclk = devm_clk_get(&pdev->dev, bt_plat_data->uart_fclk);

	err = devm_request_irq(&pdev->dev, info->irq, hci_h4p_interrupt,
				IRQF_DISABLED, "hci_h4p", info);
	if (err < 0) {
		dev_err(info->dev, "hci_h4p: unable to get IRQ %d\n",
			info->irq);
		return err;
	}

	err = devm_request_irq(&pdev->dev, gpio_to_irq(info->host_wakeup_gpio),
			  hci_h4p_wakeup_interrupt,  IRQF_TRIGGER_FALLING |
			  IRQF_TRIGGER_RISING | IRQF_DISABLED,
			  "hci_h4p_wkup", info);
	if (err < 0) {
		dev_err(info->dev, "hci_h4p: unable to get wakeup IRQ %d\n",
			  gpio_to_irq(info->host_wakeup_gpio));
		return err;
	}

	err = irq_set_irq_wake(gpio_to_irq(info->host_wakeup_gpio), 1);
	if (err < 0) {
		dev_err(info->dev, "hci_h4p: unable to set wakeup for IRQ %d\n",
				gpio_to_irq(info->host_wakeup_gpio));
		return err;
	}

	init_timer_deferrable(&info->lazy_release);
	info->lazy_release.function = hci_h4p_lazy_clock_release;
	info->lazy_release.data = (unsigned long)info;
	hci_h4p_set_clk(info, &info->tx_clocks_en, 1);
	err = hci_h4p_reset_uart(info);
	if (err < 0)
		return err;
	gpio_set_value(info->reset_gpio, 0);
	hci_h4p_set_clk(info, &info->tx_clocks_en, 0);

	platform_set_drvdata(pdev, info);

	if (hci_h4p_register_hdev(info) < 0) {
		dev_err(info->dev, "failed to register hci_h4p hci device\n");
		return -EINVAL;
	}

	return 0;
}

static int hci_h4p_remove(struct platform_device *pdev)
{
	struct hci_h4p_info *info;

	info = platform_get_drvdata(pdev);

	hci_h4p_sysfs_remove_files(info->dev);
	hci_h4p_hci_close(info->hdev);
	hci_unregister_dev(info->hdev);
	hci_free_dev(info->hdev);

	return 0;
}

static struct platform_driver hci_h4p_driver = {
	.probe		= hci_h4p_probe,
	.remove		= hci_h4p_remove,
	.driver		= {
		.name	= "hci_h4p",
	},
};

module_platform_driver(hci_h4p_driver);

MODULE_ALIAS("platform:hci_h4p");
MODULE_DESCRIPTION("Bluetooth h4 driver with nokia extensions");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Ville Tervo");
