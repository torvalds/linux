/*
 * CAN bus driver for the alone generic (as possible as) MSCAN controller.
 *
 * Copyright (C) 2005-2006 Andrey Volkov <avolkov@varma-el.com>,
 *                         Varma Electronics Oy
 * Copyright (C) 2008-2009 Wolfgang Grandegger <wg@grandegger.com>
 * Copyright (C) 2008-2009 Pengutronix <kernel@pengutronix.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the version 2 of the GNU General Public License
 * as published by the Free Software Foundation
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/netdevice.h>
#include <linux/if_arp.h>
#include <linux/if_ether.h>
#include <linux/list.h>
#include <linux/can/dev.h>
#include <linux/can/error.h>
#include <linux/io.h>

#include "mscan.h"

static struct can_bittiming_const mscan_bittiming_const = {
	.name = "mscan",
	.tseg1_min = 4,
	.tseg1_max = 16,
	.tseg2_min = 2,
	.tseg2_max = 8,
	.sjw_max = 4,
	.brp_min = 1,
	.brp_max = 64,
	.brp_inc = 1,
};

struct mscan_state {
	u8 mode;
	u8 canrier;
	u8 cantier;
};

static enum can_state state_map[] = {
	CAN_STATE_ERROR_ACTIVE,
	CAN_STATE_ERROR_WARNING,
	CAN_STATE_ERROR_PASSIVE,
	CAN_STATE_BUS_OFF
};

static int mscan_set_mode(struct net_device *dev, u8 mode)
{
	struct mscan_priv *priv = netdev_priv(dev);
	struct mscan_regs __iomem *regs = priv->reg_base;
	int ret = 0;
	int i;
	u8 canctl1;

	if (mode != MSCAN_NORMAL_MODE) {
		if (priv->tx_active) {
			/* Abort transfers before going to sleep */#
			out_8(&regs->cantarq, priv->tx_active);
			/* Suppress TX done interrupts */
			out_8(&regs->cantier, 0);
		}

		canctl1 = in_8(&regs->canctl1);
		if ((mode & MSCAN_SLPRQ) && !(canctl1 & MSCAN_SLPAK)) {
			setbits8(&regs->canctl0, MSCAN_SLPRQ);
			for (i = 0; i < MSCAN_SET_MODE_RETRIES; i++) {
				if (in_8(&regs->canctl1) & MSCAN_SLPAK)
					break;
				udelay(100);
			}
			/*
			 * The mscan controller will fail to enter sleep mode,
			 * while there are irregular activities on bus, like
			 * somebody keeps retransmitting. This behavior is
			 * undocumented and seems to differ between mscan built
			 * in mpc5200b and mpc5200. We proceed in that case,
			 * since otherwise the slprq will be kept set and the
			 * controller will get stuck. NOTE: INITRQ or CSWAI
			 * will abort all active transmit actions, if still
			 * any, at once.
			 */
			if (i >= MSCAN_SET_MODE_RETRIES)
				netdev_dbg(dev,
					   "device failed to enter sleep mode. "
					   "We proceed anyhow.\n");
			else
				priv->can.state = CAN_STATE_SLEEPING;
		}

		if ((mode & MSCAN_INITRQ) && !(canctl1 & MSCAN_INITAK)) {
			setbits8(&regs->canctl0, MSCAN_INITRQ);
			for (i = 0; i < MSCAN_SET_MODE_RETRIES; i++) {
				if (in_8(&regs->canctl1) & MSCAN_INITAK)
					break;
			}
			if (i >= MSCAN_SET_MODE_RETRIES)
				ret = -ENODEV;
		}
		if (!ret)
			priv->can.state = CAN_STATE_STOPPED;

		if (mode & MSCAN_CSWAI)
			setbits8(&regs->canctl0, MSCAN_CSWAI);

	} else {
		canctl1 = in_8(&regs->canctl1);
		if (canctl1 & (MSCAN_SLPAK | MSCAN_INITAK)) {
			clrbits8(&regs->canctl0, MSCAN_SLPRQ | MSCAN_INITRQ);
			for (i = 0; i < MSCAN_SET_MODE_RETRIES; i++) {
				canctl1 = in_8(&regs->canctl1);
				if (!(canctl1 & (MSCAN_INITAK | MSCAN_SLPAK)))
					break;
			}
			if (i >= MSCAN_SET_MODE_RETRIES)
				ret = -ENODEV;
			else
				priv->can.state = CAN_STATE_ERROR_ACTIVE;
		}
	}
	return ret;
}

static int mscan_start(struct net_device *dev)
{
	struct mscan_priv *priv = netdev_priv(dev);
	struct mscan_regs __iomem *regs = priv->reg_base;
	u8 canrflg;
	int err;

	out_8(&regs->canrier, 0);

	INIT_LIST_HEAD(&priv->tx_head);
	priv->prev_buf_id = 0;
	priv->cur_pri = 0;
	priv->tx_active = 0;
	priv->shadow_canrier = 0;
	priv->flags = 0;

	if (priv->type == MSCAN_TYPE_MPC5121) {
		/* Clear pending bus-off condition */
		if (in_8(&regs->canmisc) & MSCAN_BOHOLD)
			out_8(&regs->canmisc, MSCAN_BOHOLD);
	}

	err = mscan_set_mode(dev, MSCAN_NORMAL_MODE);
	if (err)
		return err;

	canrflg = in_8(&regs->canrflg);
	priv->shadow_statflg = canrflg & MSCAN_STAT_MSK;
	priv->can.state = state_map[max(MSCAN_STATE_RX(canrflg),
				    MSCAN_STATE_TX(canrflg))];
	out_8(&regs->cantier, 0);

	/* Enable receive interrupts. */
	out_8(&regs->canrier, MSCAN_RX_INTS_ENABLE);

	return 0;
}

static int mscan_restart(struct net_device *dev)
{
	struct mscan_priv *priv = netdev_priv(dev);

	if (priv->type == MSCAN_TYPE_MPC5121) {
		struct mscan_regs __iomem *regs = priv->reg_base;

		priv->can.state = CAN_STATE_ERROR_ACTIVE;
		WARN(!(in_8(&regs->canmisc) & MSCAN_BOHOLD),
		     "bus-off state expected\n");
		out_8(&regs->canmisc, MSCAN_BOHOLD);
		/* Re-enable receive interrupts. */
		out_8(&regs->canrier, MSCAN_RX_INTS_ENABLE);
	} else {
		if (priv->can.state <= CAN_STATE_BUS_OFF)
			mscan_set_mode(dev, MSCAN_INIT_MODE);
		return mscan_start(dev);
	}

	return 0;
}

static netdev_tx_t mscan_start_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct can_frame *frame = (struct can_frame *)skb->data;
	struct mscan_priv *priv = netdev_priv(dev);
	struct mscan_regs __iomem *regs = priv->reg_base;
	int i, rtr, buf_id;
	u32 can_id;

	if (can_dropped_invalid_skb(dev, skb))
		return NETDEV_TX_OK;

	out_8(&regs->cantier, 0);

	i = ~priv->tx_active & MSCAN_TXE;
	buf_id = ffs(i) - 1;
	switch (hweight8(i)) {
	case 0:
		netif_stop_queue(dev);
		netdev_err(dev, "Tx Ring full when queue awake!\n");
		return NETDEV_TX_BUSY;
	case 1:
		/*
		 * if buf_id < 3, then current frame will be send out of order,
		 * since buffer with lower id have higher priority (hell..)
		 */
		netif_stop_queue(dev);
	case 2:
		if (buf_id < priv->prev_buf_id) {
			priv->cur_pri++;
			if (priv->cur_pri == 0xff) {
				set_bit(F_TX_WAIT_ALL, &priv->flags);
				netif_stop_queue(dev);
			}
		}
		set_bit(F_TX_PROGRESS, &priv->flags);
		break;
	}
	priv->prev_buf_id = buf_id;
	out_8(&regs->cantbsel, i);

	rtr = frame->can_id & CAN_RTR_FLAG;

	/* RTR is always the lowest bit of interest, then IDs follow */
	if (frame->can_id & CAN_EFF_FLAG) {
		can_id = (frame->can_id & CAN_EFF_MASK)
			 << (MSCAN_EFF_RTR_SHIFT + 1);
		if (rtr)
			can_id |= 1 << MSCAN_EFF_RTR_SHIFT;
		out_be16(&regs->tx.idr3_2, can_id);

		can_id >>= 16;
		/* EFF_FLAGS are between the IDs :( */
		can_id = (can_id & 0x7) | ((can_id << 2) & 0xffe0)
			 | MSCAN_EFF_FLAGS;
	} else {
		can_id = (frame->can_id & CAN_SFF_MASK)
			 << (MSCAN_SFF_RTR_SHIFT + 1);
		if (rtr)
			can_id |= 1 << MSCAN_SFF_RTR_SHIFT;
	}
	out_be16(&regs->tx.idr1_0, can_id);

	if (!rtr) {
		void __iomem *data = &regs->tx.dsr1_0;
		u16 *payload = (u16 *)frame->data;

		for (i = 0; i < frame->can_dlc / 2; i++) {
			out_be16(data, *payload++);
			data += 2 + _MSCAN_RESERVED_DSR_SIZE;
		}
		/* write remaining byte if necessary */
		if (frame->can_dlc & 1)
			out_8(data, frame->data[frame->can_dlc - 1]);
	}

	out_8(&regs->tx.dlr, frame->can_dlc);
	out_8(&regs->tx.tbpr, priv->cur_pri);

	/* Start transmission. */
	out_8(&regs->cantflg, 1 << buf_id);

	if (!test_bit(F_TX_PROGRESS, &priv->flags))
		dev->trans_start = jiffies;

	list_add_tail(&priv->tx_queue[buf_id].list, &priv->tx_head);

	can_put_echo_skb(skb, dev, buf_id);

	/* Enable interrupt. */
	priv->tx_active |= 1 << buf_id;
	out_8(&regs->cantier, priv->tx_active);

	return NETDEV_TX_OK;
}

/* This function returns the old state to see where we came from */
static enum can_state check_set_state(struct net_device *dev, u8 canrflg)
{
	struct mscan_priv *priv = netdev_priv(dev);
	enum can_state state, old_state = priv->can.state;

	if (canrflg & MSCAN_CSCIF && old_state <= CAN_STATE_BUS_OFF) {
		state = state_map[max(MSCAN_STATE_RX(canrflg),
				      MSCAN_STATE_TX(canrflg))];
		priv->can.state = state;
	}
	return old_state;
}

static void mscan_get_rx_frame(struct net_device *dev, struct can_frame *frame)
{
	struct mscan_priv *priv = netdev_priv(dev);
	struct mscan_regs __iomem *regs = priv->reg_base;
	u32 can_id;
	int i;

	can_id = in_be16(&regs->rx.idr1_0);
	if (can_id & (1 << 3)) {
		frame->can_id = CAN_EFF_FLAG;
		can_id = ((can_id << 16) | in_be16(&regs->rx.idr3_2));
		can_id = ((can_id & 0xffe00000) |
			  ((can_id & 0x7ffff) << 2)) >> 2;
	} else {
		can_id >>= 4;
		frame->can_id = 0;
	}

	frame->can_id |= can_id >> 1;
	if (can_id & 1)
		frame->can_id |= CAN_RTR_FLAG;

	frame->can_dlc = get_can_dlc(in_8(&regs->rx.dlr) & 0xf);

	if (!(frame->can_id & CAN_RTR_FLAG)) {
		void __iomem *data = &regs->rx.dsr1_0;
		u16 *payload = (u16 *)frame->data;

		for (i = 0; i < frame->can_dlc / 2; i++) {
			*payload++ = in_be16(data);
			data += 2 + _MSCAN_RESERVED_DSR_SIZE;
		}
		/* read remaining byte if necessary */
		if (frame->can_dlc & 1)
			frame->data[frame->can_dlc - 1] = in_8(data);
	}

	out_8(&regs->canrflg, MSCAN_RXF);
}

static void mscan_get_err_frame(struct net_device *dev, struct can_frame *frame,
				u8 canrflg)
{
	struct mscan_priv *priv = netdev_priv(dev);
	struct mscan_regs __iomem *regs = priv->reg_base;
	struct net_device_stats *stats = &dev->stats;
	enum can_state old_state;

	netdev_dbg(dev, "error interrupt (canrflg=%#x)\n", canrflg);
	frame->can_id = CAN_ERR_FLAG;

	if (canrflg & MSCAN_OVRIF) {
		frame->can_id |= CAN_ERR_CRTL;
		frame->data[1] = CAN_ERR_CRTL_RX_OVERFLOW;
		stats->rx_over_errors++;
		stats->rx_errors++;
	} else {
		frame->data[1] = 0;
	}

	old_state = check_set_state(dev, canrflg);
	/* State changed */
	if (old_state != priv->can.state) {
		switch (priv->can.state) {
		case CAN_STATE_ERROR_WARNING:
			frame->can_id |= CAN_ERR_CRTL;
			priv->can.can_stats.error_warning++;
			if ((priv->shadow_statflg & MSCAN_RSTAT_MSK) <
			    (canrflg & MSCAN_RSTAT_MSK))
				frame->data[1] |= CAN_ERR_CRTL_RX_WARNING;
			if ((priv->shadow_statflg & MSCAN_TSTAT_MSK) <
			    (canrflg & MSCAN_TSTAT_MSK))
				frame->data[1] |= CAN_ERR_CRTL_TX_WARNING;
			break;
		case CAN_STATE_ERROR_PASSIVE:
			frame->can_id |= CAN_ERR_CRTL;
			priv->can.can_stats.error_passive++;
			frame->data[1] |= CAN_ERR_CRTL_RX_PASSIVE;
			break;
		case CAN_STATE_BUS_OFF:
			frame->can_id |= CAN_ERR_BUSOFF;
			/*
			 * The MSCAN on the MPC5200 does recover from bus-off
			 * automatically. To avoid that we stop the chip doing
			 * a light-weight stop (we are in irq-context).
			 */
			if (priv->type != MSCAN_TYPE_MPC5121) {
				out_8(&regs->cantier, 0);
				out_8(&regs->canrier, 0);
				setbits8(&regs->canctl0,
					 MSCAN_SLPRQ | MSCAN_INITRQ);
			}
			can_bus_off(dev);
			break;
		default:
			break;
		}
	}
	priv->shadow_statflg = canrflg & MSCAN_STAT_MSK;
	frame->can_dlc = CAN_ERR_DLC;
	out_8(&regs->canrflg, MSCAN_ERR_IF);
}

static int mscan_rx_poll(struct napi_struct *napi, int quota)
{
	struct mscan_priv *priv = container_of(napi, struct mscan_priv, napi);
	struct net_device *dev = napi->dev;
	struct mscan_regs __iomem *regs = priv->reg_base;
	struct net_device_stats *stats = &dev->stats;
	int npackets = 0;
	int ret = 1;
	struct sk_buff *skb;
	struct can_frame *frame;
	u8 canrflg;

	while (npackets < quota) {
		canrflg = in_8(&regs->canrflg);
		if (!(canrflg & (MSCAN_RXF | MSCAN_ERR_IF)))
			break;

		skb = alloc_can_skb(dev, &frame);
		if (!skb) {
			if (printk_ratelimit())
				netdev_notice(dev, "packet dropped\n");
			stats->rx_dropped++;
			out_8(&regs->canrflg, canrflg);
			continue;
		}

		if (canrflg & MSCAN_RXF)
			mscan_get_rx_frame(dev, frame);
		else if (canrflg & MSCAN_ERR_IF)
			mscan_get_err_frame(dev, frame, canrflg);

		stats->rx_packets++;
		stats->rx_bytes += frame->can_dlc;
		npackets++;
		netif_receive_skb(skb);
	}

	if (!(in_8(&regs->canrflg) & (MSCAN_RXF | MSCAN_ERR_IF))) {
		napi_complete(&priv->napi);
		clear_bit(F_RX_PROGRESS, &priv->flags);
		if (priv->can.state < CAN_STATE_BUS_OFF)
			out_8(&regs->canrier, priv->shadow_canrier);
		ret = 0;
	}
	return ret;
}

static irqreturn_t mscan_isr(int irq, void *dev_id)
{
	struct net_device *dev = (struct net_device *)dev_id;
	struct mscan_priv *priv = netdev_priv(dev);
	struct mscan_regs __iomem *regs = priv->reg_base;
	struct net_device_stats *stats = &dev->stats;
	u8 cantier, cantflg, canrflg;
	irqreturn_t ret = IRQ_NONE;

	cantier = in_8(&regs->cantier) & MSCAN_TXE;
	cantflg = in_8(&regs->cantflg) & cantier;

	if (cantier && cantflg) {
		struct list_head *tmp, *pos;

		list_for_each_safe(pos, tmp, &priv->tx_head) {
			struct tx_queue_entry *entry =
			    list_entry(pos, struct tx_queue_entry, list);
			u8 mask = entry->mask;

			if (!(cantflg & mask))
				continue;

			out_8(&regs->cantbsel, mask);
			stats->tx_bytes += in_8(&regs->tx.dlr);
			stats->tx_packets++;
			can_get_echo_skb(dev, entry->id);
			priv->tx_active &= ~mask;
			list_del(pos);
		}

		if (list_empty(&priv->tx_head)) {
			clear_bit(F_TX_WAIT_ALL, &priv->flags);
			clear_bit(F_TX_PROGRESS, &priv->flags);
			priv->cur_pri = 0;
		} else {
			dev->trans_start = jiffies;
		}

		if (!test_bit(F_TX_WAIT_ALL, &priv->flags))
			netif_wake_queue(dev);

		out_8(&regs->cantier, priv->tx_active);
		ret = IRQ_HANDLED;
	}

	canrflg = in_8(&regs->canrflg);
	if ((canrflg & ~MSCAN_STAT_MSK) &&
	    !test_and_set_bit(F_RX_PROGRESS, &priv->flags)) {
		if (canrflg & ~MSCAN_STAT_MSK) {
			priv->shadow_canrier = in_8(&regs->canrier);
			out_8(&regs->canrier, 0);
			napi_schedule(&priv->napi);
			ret = IRQ_HANDLED;
		} else {
			clear_bit(F_RX_PROGRESS, &priv->flags);
		}
	}
	return ret;
}

static int mscan_do_set_mode(struct net_device *dev, enum can_mode mode)
{
	struct mscan_priv *priv = netdev_priv(dev);
	int ret = 0;

	if (!priv->open_time)
		return -EINVAL;

	switch (mode) {
	case CAN_MODE_START:
		ret = mscan_restart(dev);
		if (ret)
			break;
		if (netif_queue_stopped(dev))
			netif_wake_queue(dev);
		break;

	default:
		ret = -EOPNOTSUPP;
		break;
	}
	return ret;
}

static int mscan_do_set_bittiming(struct net_device *dev)
{
	struct mscan_priv *priv = netdev_priv(dev);
	struct mscan_regs __iomem *regs = priv->reg_base;
	struct can_bittiming *bt = &priv->can.bittiming;
	u8 btr0, btr1;

	btr0 = BTR0_SET_BRP(bt->brp) | BTR0_SET_SJW(bt->sjw);
	btr1 = (BTR1_SET_TSEG1(bt->prop_seg + bt->phase_seg1) |
		BTR1_SET_TSEG2(bt->phase_seg2) |
		BTR1_SET_SAM(priv->can.ctrlmode & CAN_CTRLMODE_3_SAMPLES));

	netdev_info(dev, "setting BTR0=0x%02x BTR1=0x%02x\n", btr0, btr1);

	out_8(&regs->canbtr0, btr0);
	out_8(&regs->canbtr1, btr1);

	return 0;
}

static int mscan_get_berr_counter(const struct net_device *dev,
				  struct can_berr_counter *bec)
{
	struct mscan_priv *priv = netdev_priv(dev);
	struct mscan_regs __iomem *regs = priv->reg_base;

	bec->txerr = in_8(&regs->cantxerr);
	bec->rxerr = in_8(&regs->canrxerr);

	return 0;
}

static int mscan_open(struct net_device *dev)
{
	int ret;
	struct mscan_priv *priv = netdev_priv(dev);
	struct mscan_regs __iomem *regs = priv->reg_base;

	/* common open */
	ret = open_candev(dev);
	if (ret)
		return ret;

	napi_enable(&priv->napi);

	ret = request_irq(dev->irq, mscan_isr, 0, dev->name, dev);
	if (ret < 0) {
		netdev_err(dev, "failed to attach interrupt\n");
		goto exit_napi_disable;
	}

	priv->open_time = jiffies;

	if (priv->can.ctrlmode & CAN_CTRLMODE_LISTENONLY)
		setbits8(&regs->canctl1, MSCAN_LISTEN);
	else
		clrbits8(&regs->canctl1, MSCAN_LISTEN);

	ret = mscan_start(dev);
	if (ret)
		goto exit_free_irq;

	netif_start_queue(dev);

	return 0;

exit_free_irq:
	priv->open_time = 0;
	free_irq(dev->irq, dev);
exit_napi_disable:
	napi_disable(&priv->napi);
	close_candev(dev);
	return ret;
}

static int mscan_close(struct net_device *dev)
{
	struct mscan_priv *priv = netdev_priv(dev);
	struct mscan_regs __iomem *regs = priv->reg_base;

	netif_stop_queue(dev);
	napi_disable(&priv->napi);

	out_8(&regs->cantier, 0);
	out_8(&regs->canrier, 0);
	mscan_set_mode(dev, MSCAN_INIT_MODE);
	close_candev(dev);
	free_irq(dev->irq, dev);
	priv->open_time = 0;

	return 0;
}

static const struct net_device_ops mscan_netdev_ops = {
       .ndo_open               = mscan_open,
       .ndo_stop               = mscan_close,
       .ndo_start_xmit         = mscan_start_xmit,
};

int register_mscandev(struct net_device *dev, int mscan_clksrc)
{
	struct mscan_priv *priv = netdev_priv(dev);
	struct mscan_regs __iomem *regs = priv->reg_base;
	u8 ctl1;

	ctl1 = in_8(&regs->canctl1);
	if (mscan_clksrc)
		ctl1 |= MSCAN_CLKSRC;
	else
		ctl1 &= ~MSCAN_CLKSRC;

	if (priv->type == MSCAN_TYPE_MPC5121) {
		priv->can.do_get_berr_counter = mscan_get_berr_counter;
		ctl1 |= MSCAN_BORM; /* bus-off recovery upon request */
	}

	ctl1 |= MSCAN_CANE;
	out_8(&regs->canctl1, ctl1);
	udelay(100);

	/* acceptance mask/acceptance code (accept everything) */
	out_be16(&regs->canidar1_0, 0);
	out_be16(&regs->canidar3_2, 0);
	out_be16(&regs->canidar5_4, 0);
	out_be16(&regs->canidar7_6, 0);

	out_be16(&regs->canidmr1_0, 0xffff);
	out_be16(&regs->canidmr3_2, 0xffff);
	out_be16(&regs->canidmr5_4, 0xffff);
	out_be16(&regs->canidmr7_6, 0xffff);
	/* Two 32 bit Acceptance Filters */
	out_8(&regs->canidac, MSCAN_AF_32BIT);

	mscan_set_mode(dev, MSCAN_INIT_MODE);

	return register_candev(dev);
}

void unregister_mscandev(struct net_device *dev)
{
	struct mscan_priv *priv = netdev_priv(dev);
	struct mscan_regs __iomem *regs = priv->reg_base;
	mscan_set_mode(dev, MSCAN_INIT_MODE);
	clrbits8(&regs->canctl1, MSCAN_CANE);
	unregister_candev(dev);
}

struct net_device *alloc_mscandev(void)
{
	struct net_device *dev;
	struct mscan_priv *priv;
	int i;

	dev = alloc_candev(sizeof(struct mscan_priv), MSCAN_ECHO_SKB_MAX);
	if (!dev)
		return NULL;
	priv = netdev_priv(dev);

	dev->netdev_ops = &mscan_netdev_ops;

	dev->flags |= IFF_ECHO;	/* we support local echo */

	netif_napi_add(dev, &priv->napi, mscan_rx_poll, 8);

	priv->can.bittiming_const = &mscan_bittiming_const;
	priv->can.do_set_bittiming = mscan_do_set_bittiming;
	priv->can.do_set_mode = mscan_do_set_mode;
	priv->can.ctrlmode_supported = CAN_CTRLMODE_3_SAMPLES |
		CAN_CTRLMODE_LISTENONLY;

	for (i = 0; i < TX_QUEUE_SIZE; i++) {
		priv->tx_queue[i].id = i;
		priv->tx_queue[i].mask = 1 << i;
	}

	return dev;
}

MODULE_AUTHOR("Andrey Volkov <avolkov@varma-el.com>");
MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("CAN port driver for a MSCAN based chips");
