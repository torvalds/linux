/*
 * Core driver for the CC770 and AN82527 CAN controllers
 *
 * Copyright (C) 2009, 2011 Wolfgang Grandegger <wg@grandegger.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the version 2 of the GNU General Public License
 * as published by the Free Software Foundation
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/types.h>
#include <linux/fcntl.h>
#include <linux/interrupt.h>
#include <linux/ptrace.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/netdevice.h>
#include <linux/if_arp.h>
#include <linux/if_ether.h>
#include <linux/skbuff.h>
#include <linux/delay.h>

#include <linux/can.h>
#include <linux/can/dev.h>
#include <linux/can/error.h>
#include <linux/can/dev.h>
#include <linux/can/platform/cc770.h>

#include "cc770.h"

MODULE_AUTHOR("Wolfgang Grandegger <wg@grandegger.com>");
MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION(KBUILD_MODNAME "CAN netdevice driver");

/*
 * The CC770 is a CAN controller from Bosch, which is 100% compatible
 * with the AN82527 from Intel, but with "bugs" being fixed and some
 * additional functionality, mainly:
 *
 * 1. RX and TX error counters are readable.
 * 2. Support of silent (listen-only) mode.
 * 3. Message object 15 can receive all types of frames, also RTR and EFF.
 *
 * Details are available from Bosch's "CC770_Product_Info_2007-01.pdf",
 * which explains in detail the compatibility between the CC770 and the
 * 82527. This driver use the additional functionality 3. on real CC770
 * devices. Unfortunately, the CC770 does still not store the message
 * identifier of received remote transmission request frames and
 * therefore it's set to 0.
 *
 * The message objects 1..14 can be used for TX and RX while the message
 * objects 15 is optimized for RX. It has a shadow register for reliable
 * data receiption under heavy bus load. Therefore it makes sense to use
 * this message object for the needed use case. The frame type (EFF/SFF)
 * for the message object 15 can be defined via kernel module parameter
 * "msgobj15_eff". If not equal 0, it will receive 29-bit EFF frames,
 * otherwise 11 bit SFF messages.
 */
static int msgobj15_eff;
module_param(msgobj15_eff, int, S_IRUGO);
MODULE_PARM_DESC(msgobj15_eff, "Extended 29-bit frames for message object 15 "
		 "(default: 11-bit standard frames)");

static int i82527_compat;
module_param(i82527_compat, int, S_IRUGO);
MODULE_PARM_DESC(i82527_compat, "Strict Intel 82527 comptibility mode "
		 "without using additional functions");

/*
 * This driver uses the last 5 message objects 11..15. The definitions
 * and structure below allows to configure and assign them to the real
 * message object.
 */
static unsigned char cc770_obj_flags[CC770_OBJ_MAX] = {
	[CC770_OBJ_RX0] = CC770_OBJ_FLAG_RX,
	[CC770_OBJ_RX1] = CC770_OBJ_FLAG_RX | CC770_OBJ_FLAG_EFF,
	[CC770_OBJ_RX_RTR0] = CC770_OBJ_FLAG_RX | CC770_OBJ_FLAG_RTR,
	[CC770_OBJ_RX_RTR1] = CC770_OBJ_FLAG_RX | CC770_OBJ_FLAG_RTR |
			      CC770_OBJ_FLAG_EFF,
	[CC770_OBJ_TX] = 0,
};

static struct can_bittiming_const cc770_bittiming_const = {
	.name = KBUILD_MODNAME,
	.tseg1_min = 1,
	.tseg1_max = 16,
	.tseg2_min = 1,
	.tseg2_max = 8,
	.sjw_max = 4,
	.brp_min = 1,
	.brp_max = 64,
	.brp_inc = 1,
};

static inline int intid2obj(unsigned int intid)
{
	if (intid == 2)
		return 0;
	else
		return MSGOBJ_LAST + 2 - intid;
}

static void enable_all_objs(const struct net_device *dev)
{
	struct cc770_priv *priv = netdev_priv(dev);
	u8 msgcfg;
	unsigned char obj_flags;
	unsigned int o, mo;

	for (o = 0; o < ARRAY_SIZE(priv->obj_flags); o++) {
		obj_flags = priv->obj_flags[o];
		mo = obj2msgobj(o);

		if (obj_flags & CC770_OBJ_FLAG_RX) {
			/*
			 * We don't need extra objects for RTR and EFF if
			 * the additional CC770 functions are enabled.
			 */
			if (priv->control_normal_mode & CTRL_EAF) {
				if (o > 0)
					continue;
				netdev_dbg(dev, "Message object %d for "
					   "RX data, RTR, SFF and EFF\n", mo);
			} else {
				netdev_dbg(dev,
					   "Message object %d for RX %s %s\n",
					   mo, obj_flags & CC770_OBJ_FLAG_RTR ?
					   "RTR" : "data",
					   obj_flags & CC770_OBJ_FLAG_EFF ?
					   "EFF" : "SFF");
			}

			if (obj_flags & CC770_OBJ_FLAG_EFF)
				msgcfg = MSGCFG_XTD;
			else
				msgcfg = 0;
			if (obj_flags & CC770_OBJ_FLAG_RTR)
				msgcfg |= MSGCFG_DIR;

			cc770_write_reg(priv, msgobj[mo].config, msgcfg);
			cc770_write_reg(priv, msgobj[mo].ctrl0,
					MSGVAL_SET | TXIE_RES |
					RXIE_SET | INTPND_RES);

			if (obj_flags & CC770_OBJ_FLAG_RTR)
				cc770_write_reg(priv, msgobj[mo].ctrl1,
						NEWDAT_RES | CPUUPD_SET |
						TXRQST_RES | RMTPND_RES);
			else
				cc770_write_reg(priv, msgobj[mo].ctrl1,
						NEWDAT_RES | MSGLST_RES |
						TXRQST_RES | RMTPND_RES);
		} else {
			netdev_dbg(dev, "Message object %d for "
				   "TX data, RTR, SFF and EFF\n", mo);

			cc770_write_reg(priv, msgobj[mo].ctrl1,
					RMTPND_RES | TXRQST_RES |
					CPUUPD_RES | NEWDAT_RES);
			cc770_write_reg(priv, msgobj[mo].ctrl0,
					MSGVAL_RES | TXIE_RES |
					RXIE_RES | INTPND_RES);
		}
	}
}

static void disable_all_objs(const struct cc770_priv *priv)
{
	int o, mo;

	for (o = 0; o <  ARRAY_SIZE(priv->obj_flags); o++) {
		mo = obj2msgobj(o);

		if (priv->obj_flags[o] & CC770_OBJ_FLAG_RX) {
			if (o > 0 && priv->control_normal_mode & CTRL_EAF)
				continue;

			cc770_write_reg(priv, msgobj[mo].ctrl1,
					NEWDAT_RES | MSGLST_RES |
					TXRQST_RES | RMTPND_RES);
			cc770_write_reg(priv, msgobj[mo].ctrl0,
					MSGVAL_RES | TXIE_RES |
					RXIE_RES | INTPND_RES);
		} else {
			/* Clear message object for send */
			cc770_write_reg(priv, msgobj[mo].ctrl1,
					RMTPND_RES | TXRQST_RES |
					CPUUPD_RES | NEWDAT_RES);
			cc770_write_reg(priv, msgobj[mo].ctrl0,
					MSGVAL_RES | TXIE_RES |
					RXIE_RES | INTPND_RES);
		}
	}
}

static void set_reset_mode(struct net_device *dev)
{
	struct cc770_priv *priv = netdev_priv(dev);

	/* Enable configuration and puts chip in bus-off, disable interrupts */
	cc770_write_reg(priv, control, CTRL_CCE | CTRL_INI);

	priv->can.state = CAN_STATE_STOPPED;

	/* Clear interrupts */
	cc770_read_reg(priv, interrupt);

	/* Clear status register */
	cc770_write_reg(priv, status, 0);

	/* Disable all used message objects */
	disable_all_objs(priv);
}

static void set_normal_mode(struct net_device *dev)
{
	struct cc770_priv *priv = netdev_priv(dev);

	/* Clear interrupts */
	cc770_read_reg(priv, interrupt);

	/* Clear status register and pre-set last error code */
	cc770_write_reg(priv, status, STAT_LEC_MASK);

	/* Enable all used message objects*/
	enable_all_objs(dev);

	/*
	 * Clear bus-off, interrupts only for errors,
	 * not for status change
	 */
	cc770_write_reg(priv, control, priv->control_normal_mode);

	priv->can.state = CAN_STATE_ERROR_ACTIVE;
}

static void chipset_init(struct cc770_priv *priv)
{
	int mo, id, data;

	/* Enable configuration and put chip in bus-off, disable interrupts */
	cc770_write_reg(priv, control, (CTRL_CCE | CTRL_INI));

	/* Set CLKOUT divider and slew rates */
	cc770_write_reg(priv, clkout, priv->clkout);

	/* Configure CPU interface / CLKOUT enable */
	cc770_write_reg(priv, cpu_interface, priv->cpu_interface);

	/* Set bus configuration  */
	cc770_write_reg(priv, bus_config, priv->bus_config);

	/* Clear interrupts */
	cc770_read_reg(priv, interrupt);

	/* Clear status register */
	cc770_write_reg(priv, status, 0);

	/* Clear and invalidate message objects */
	for (mo = MSGOBJ_FIRST; mo <= MSGOBJ_LAST; mo++) {
		cc770_write_reg(priv, msgobj[mo].ctrl0,
				INTPND_UNC | RXIE_RES |
				TXIE_RES | MSGVAL_RES);
		cc770_write_reg(priv, msgobj[mo].ctrl0,
				INTPND_RES | RXIE_RES |
				TXIE_RES | MSGVAL_RES);
		cc770_write_reg(priv, msgobj[mo].ctrl1,
				NEWDAT_RES | MSGLST_RES |
				TXRQST_RES | RMTPND_RES);
		for (data = 0; data < 8; data++)
			cc770_write_reg(priv, msgobj[mo].data[data], 0);
		for (id = 0; id < 4; id++)
			cc770_write_reg(priv, msgobj[mo].id[id], 0);
		cc770_write_reg(priv, msgobj[mo].config, 0);
	}

	/* Set all global ID masks to "don't care" */
	cc770_write_reg(priv, global_mask_std[0], 0);
	cc770_write_reg(priv, global_mask_std[1], 0);
	cc770_write_reg(priv, global_mask_ext[0], 0);
	cc770_write_reg(priv, global_mask_ext[1], 0);
	cc770_write_reg(priv, global_mask_ext[2], 0);
	cc770_write_reg(priv, global_mask_ext[3], 0);

}

static int cc770_probe_chip(struct net_device *dev)
{
	struct cc770_priv *priv = netdev_priv(dev);

	/* Enable configuration, put chip in bus-off, disable ints */
	cc770_write_reg(priv, control, CTRL_CCE | CTRL_EAF | CTRL_INI);
	/* Configure cpu interface / CLKOUT disable */
	cc770_write_reg(priv, cpu_interface, priv->cpu_interface);

	/*
	 * Check if hardware reset is still inactive or maybe there
	 * is no chip in this address space
	 */
	if (cc770_read_reg(priv, cpu_interface) & CPUIF_RST) {
		netdev_info(dev, "probing @0x%p failed (reset)\n",
			    priv->reg_base);
		return -ENODEV;
	}

	/* Write and read back test pattern (some arbitrary values) */
	cc770_write_reg(priv, msgobj[1].data[1], 0x25);
	cc770_write_reg(priv, msgobj[2].data[3], 0x52);
	cc770_write_reg(priv, msgobj[10].data[6], 0xc3);
	if ((cc770_read_reg(priv, msgobj[1].data[1]) != 0x25) ||
	    (cc770_read_reg(priv, msgobj[2].data[3]) != 0x52) ||
	    (cc770_read_reg(priv, msgobj[10].data[6]) != 0xc3)) {
		netdev_info(dev, "probing @0x%p failed (pattern)\n",
			    priv->reg_base);
		return -ENODEV;
	}

	/* Check if this chip is a CC770 supporting additional functions */
	if (cc770_read_reg(priv, control) & CTRL_EAF)
		priv->control_normal_mode |= CTRL_EAF;

	return 0;
}

static void cc770_start(struct net_device *dev)
{
	struct cc770_priv *priv = netdev_priv(dev);

	/* leave reset mode */
	if (priv->can.state != CAN_STATE_STOPPED)
		set_reset_mode(dev);

	/* leave reset mode */
	set_normal_mode(dev);
}

static int cc770_set_mode(struct net_device *dev, enum can_mode mode)
{
	switch (mode) {
	case CAN_MODE_START:
		cc770_start(dev);
		netif_wake_queue(dev);
		break;

	default:
		return -EOPNOTSUPP;
	}

	return 0;
}

static int cc770_set_bittiming(struct net_device *dev)
{
	struct cc770_priv *priv = netdev_priv(dev);
	struct can_bittiming *bt = &priv->can.bittiming;
	u8 btr0, btr1;

	btr0 = ((bt->brp - 1) & 0x3f) | (((bt->sjw - 1) & 0x3) << 6);
	btr1 = ((bt->prop_seg + bt->phase_seg1 - 1) & 0xf) |
		(((bt->phase_seg2 - 1) & 0x7) << 4);
	if (priv->can.ctrlmode & CAN_CTRLMODE_3_SAMPLES)
		btr1 |= 0x80;

	netdev_info(dev, "setting BTR0=0x%02x BTR1=0x%02x\n", btr0, btr1);

	cc770_write_reg(priv, bit_timing_0, btr0);
	cc770_write_reg(priv, bit_timing_1, btr1);

	return 0;
}

static int cc770_get_berr_counter(const struct net_device *dev,
				  struct can_berr_counter *bec)
{
	struct cc770_priv *priv = netdev_priv(dev);

	bec->txerr = cc770_read_reg(priv, tx_error_counter);
	bec->rxerr = cc770_read_reg(priv, rx_error_counter);

	return 0;
}

static netdev_tx_t cc770_start_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct cc770_priv *priv = netdev_priv(dev);
	struct net_device_stats *stats = &dev->stats;
	struct can_frame *cf = (struct can_frame *)skb->data;
	unsigned int mo = obj2msgobj(CC770_OBJ_TX);
	u8 dlc, rtr;
	u32 id;
	int i;

	if (can_dropped_invalid_skb(dev, skb))
		return NETDEV_TX_OK;

	if ((cc770_read_reg(priv,
			    msgobj[mo].ctrl1) & TXRQST_UNC) == TXRQST_SET) {
		netdev_err(dev, "TX register is still occupied!\n");
		return NETDEV_TX_BUSY;
	}

	netif_stop_queue(dev);

	dlc = cf->can_dlc;
	id = cf->can_id;
	if (cf->can_id & CAN_RTR_FLAG)
		rtr = 0;
	else
		rtr = MSGCFG_DIR;
	cc770_write_reg(priv, msgobj[mo].ctrl1,
			RMTPND_RES | TXRQST_RES | CPUUPD_SET | NEWDAT_RES);
	cc770_write_reg(priv, msgobj[mo].ctrl0,
			MSGVAL_SET | TXIE_SET | RXIE_RES | INTPND_RES);
	if (id & CAN_EFF_FLAG) {
		id &= CAN_EFF_MASK;
		cc770_write_reg(priv, msgobj[mo].config,
				(dlc << 4) | rtr | MSGCFG_XTD);
		cc770_write_reg(priv, msgobj[mo].id[3], id << 3);
		cc770_write_reg(priv, msgobj[mo].id[2], id >> 5);
		cc770_write_reg(priv, msgobj[mo].id[1], id >> 13);
		cc770_write_reg(priv, msgobj[mo].id[0], id >> 21);
	} else {
		id &= CAN_SFF_MASK;
		cc770_write_reg(priv, msgobj[mo].config, (dlc << 4) | rtr);
		cc770_write_reg(priv, msgobj[mo].id[0], id >> 3);
		cc770_write_reg(priv, msgobj[mo].id[1], id << 5);
	}

	for (i = 0; i < dlc; i++)
		cc770_write_reg(priv, msgobj[mo].data[i], cf->data[i]);

	cc770_write_reg(priv, msgobj[mo].ctrl1,
			RMTPND_RES | TXRQST_SET | CPUUPD_RES | NEWDAT_UNC);

	stats->tx_bytes += dlc;

	can_put_echo_skb(skb, dev, 0);

	/*
	 * HM: We had some cases of repeated IRQs so make sure the
	 * INT is acknowledged I know it's already further up, but
	 * doing again fixed the issue
	 */
	cc770_write_reg(priv, msgobj[mo].ctrl0,
			MSGVAL_UNC | TXIE_UNC | RXIE_UNC | INTPND_RES);

	return NETDEV_TX_OK;
}

static void cc770_rx(struct net_device *dev, unsigned int mo, u8 ctrl1)
{
	struct cc770_priv *priv = netdev_priv(dev);
	struct net_device_stats *stats = &dev->stats;
	struct can_frame *cf;
	struct sk_buff *skb;
	u8 config;
	u32 id;
	int i;

	skb = alloc_can_skb(dev, &cf);
	if (!skb)
		return;

	config = cc770_read_reg(priv, msgobj[mo].config);

	if (ctrl1 & RMTPND_SET) {
		/*
		 * Unfortunately, the chip does not store the real message
		 * identifier of the received remote transmission request
		 * frame. Therefore we set it to 0.
		 */
		cf->can_id = CAN_RTR_FLAG;
		if (config & MSGCFG_XTD)
			cf->can_id |= CAN_EFF_FLAG;
		cf->can_dlc = 0;
	} else {
		if (config & MSGCFG_XTD) {
			id = cc770_read_reg(priv, msgobj[mo].id[3]);
			id |= cc770_read_reg(priv, msgobj[mo].id[2]) << 8;
			id |= cc770_read_reg(priv, msgobj[mo].id[1]) << 16;
			id |= cc770_read_reg(priv, msgobj[mo].id[0]) << 24;
			id >>= 3;
			id |= CAN_EFF_FLAG;
		} else {
			id = cc770_read_reg(priv, msgobj[mo].id[1]);
			id |= cc770_read_reg(priv, msgobj[mo].id[0]) << 8;
			id >>= 5;
		}

		cf->can_id = id;
		cf->can_dlc = get_can_dlc((config & 0xf0) >> 4);
		for (i = 0; i < cf->can_dlc; i++)
			cf->data[i] = cc770_read_reg(priv, msgobj[mo].data[i]);
	}
	netif_rx(skb);

	stats->rx_packets++;
	stats->rx_bytes += cf->can_dlc;
}

static int cc770_err(struct net_device *dev, u8 status)
{
	struct cc770_priv *priv = netdev_priv(dev);
	struct net_device_stats *stats = &dev->stats;
	struct can_frame *cf;
	struct sk_buff *skb;
	u8 lec;

	netdev_dbg(dev, "status interrupt (%#x)\n", status);

	skb = alloc_can_err_skb(dev, &cf);
	if (!skb)
		return -ENOMEM;

	/* Use extended functions of the CC770 */
	if (priv->control_normal_mode & CTRL_EAF) {
		cf->data[6] = cc770_read_reg(priv, tx_error_counter);
		cf->data[7] = cc770_read_reg(priv, rx_error_counter);
	}

	if (status & STAT_BOFF) {
		/* Disable interrupts */
		cc770_write_reg(priv, control, CTRL_INI);
		cf->can_id |= CAN_ERR_BUSOFF;
		priv->can.state = CAN_STATE_BUS_OFF;
		can_bus_off(dev);
	} else if (status & STAT_WARN) {
		cf->can_id |= CAN_ERR_CRTL;
		/* Only the CC770 does show error passive */
		if (cf->data[7] > 127) {
			cf->data[1] = CAN_ERR_CRTL_RX_PASSIVE |
				CAN_ERR_CRTL_TX_PASSIVE;
			priv->can.state = CAN_STATE_ERROR_PASSIVE;
			priv->can.can_stats.error_passive++;
		} else {
			cf->data[1] = CAN_ERR_CRTL_RX_WARNING |
				CAN_ERR_CRTL_TX_WARNING;
			priv->can.state = CAN_STATE_ERROR_WARNING;
			priv->can.can_stats.error_warning++;
		}
	} else {
		/* Back to error avtive */
		cf->can_id |= CAN_ERR_PROT;
		cf->data[2] = CAN_ERR_PROT_ACTIVE;
		priv->can.state = CAN_STATE_ERROR_ACTIVE;
	}

	lec = status & STAT_LEC_MASK;
	if (lec < 7 && lec > 0) {
		if (lec == STAT_LEC_ACK) {
			cf->can_id |= CAN_ERR_ACK;
		} else {
			cf->can_id |= CAN_ERR_PROT;
			switch (lec) {
			case STAT_LEC_STUFF:
				cf->data[2] |= CAN_ERR_PROT_STUFF;
				break;
			case STAT_LEC_FORM:
				cf->data[2] |= CAN_ERR_PROT_FORM;
				break;
			case STAT_LEC_BIT1:
				cf->data[2] |= CAN_ERR_PROT_BIT1;
				break;
			case STAT_LEC_BIT0:
				cf->data[2] |= CAN_ERR_PROT_BIT0;
				break;
			case STAT_LEC_CRC:
				cf->data[3] |= CAN_ERR_PROT_LOC_CRC_SEQ;
				break;
			}
		}
	}

	netif_rx(skb);

	stats->rx_packets++;
	stats->rx_bytes += cf->can_dlc;

	return 0;
}

static int cc770_status_interrupt(struct net_device *dev)
{
	struct cc770_priv *priv = netdev_priv(dev);
	u8 status;

	status = cc770_read_reg(priv, status);
	/* Reset the status register including RXOK and TXOK */
	cc770_write_reg(priv, status, STAT_LEC_MASK);

	if (status & (STAT_WARN | STAT_BOFF) ||
	    (status & STAT_LEC_MASK) != STAT_LEC_MASK) {
		cc770_err(dev, status);
		return status & STAT_BOFF;
	}

	return 0;
}

static void cc770_rx_interrupt(struct net_device *dev, unsigned int o)
{
	struct cc770_priv *priv = netdev_priv(dev);
	struct net_device_stats *stats = &dev->stats;
	unsigned int mo = obj2msgobj(o);
	u8 ctrl1;
	int n = CC770_MAX_MSG;

	while (n--) {
		ctrl1 = cc770_read_reg(priv, msgobj[mo].ctrl1);

		if (!(ctrl1 & NEWDAT_SET))  {
			/* Check for RTR if additional functions are enabled */
			if (priv->control_normal_mode & CTRL_EAF) {
				if (!(cc770_read_reg(priv, msgobj[mo].ctrl0) &
				      INTPND_SET))
					break;
			} else {
				break;
			}
		}

		if (ctrl1 & MSGLST_SET) {
			stats->rx_over_errors++;
			stats->rx_errors++;
		}
		if (mo < MSGOBJ_LAST)
			cc770_write_reg(priv, msgobj[mo].ctrl1,
					NEWDAT_RES | MSGLST_RES |
					TXRQST_UNC | RMTPND_UNC);
		cc770_rx(dev, mo, ctrl1);

		cc770_write_reg(priv, msgobj[mo].ctrl0,
				MSGVAL_SET | TXIE_RES |
				RXIE_SET | INTPND_RES);
		cc770_write_reg(priv, msgobj[mo].ctrl1,
				NEWDAT_RES | MSGLST_RES |
				TXRQST_RES | RMTPND_RES);
	}
}

static void cc770_rtr_interrupt(struct net_device *dev, unsigned int o)
{
	struct cc770_priv *priv = netdev_priv(dev);
	unsigned int mo = obj2msgobj(o);
	u8 ctrl0, ctrl1;
	int n = CC770_MAX_MSG;

	while (n--) {
		ctrl0 = cc770_read_reg(priv, msgobj[mo].ctrl0);
		if (!(ctrl0 & INTPND_SET))
			break;

		ctrl1 = cc770_read_reg(priv, msgobj[mo].ctrl1);
		cc770_rx(dev, mo, ctrl1);

		cc770_write_reg(priv, msgobj[mo].ctrl0,
				MSGVAL_SET | TXIE_RES |
				RXIE_SET | INTPND_RES);
		cc770_write_reg(priv, msgobj[mo].ctrl1,
				NEWDAT_RES | CPUUPD_SET |
				TXRQST_RES | RMTPND_RES);
	}
}

static void cc770_tx_interrupt(struct net_device *dev, unsigned int o)
{
	struct cc770_priv *priv = netdev_priv(dev);
	struct net_device_stats *stats = &dev->stats;
	unsigned int mo = obj2msgobj(o);

	/* Nothing more to send, switch off interrupts */
	cc770_write_reg(priv, msgobj[mo].ctrl0,
			MSGVAL_RES | TXIE_RES | RXIE_RES | INTPND_RES);
	/*
	 * We had some cases of repeated IRQ so make sure the
	 * INT is acknowledged
	 */
	cc770_write_reg(priv, msgobj[mo].ctrl0,
			MSGVAL_UNC | TXIE_UNC | RXIE_UNC | INTPND_RES);

	stats->tx_packets++;
	can_get_echo_skb(dev, 0);
	netif_wake_queue(dev);
}

irqreturn_t cc770_interrupt(int irq, void *dev_id)
{
	struct net_device *dev = (struct net_device *)dev_id;
	struct cc770_priv *priv = netdev_priv(dev);
	u8 intid;
	int o, n = 0;

	/* Shared interrupts and IRQ off? */
	if (priv->can.state == CAN_STATE_STOPPED)
		return IRQ_NONE;

	if (priv->pre_irq)
		priv->pre_irq(priv);

	while (n < CC770_MAX_IRQ) {
		/* Read the highest pending interrupt request */
		intid = cc770_read_reg(priv, interrupt);
		if (!intid)
			break;
		n++;

		if (intid == 1) {
			/* Exit in case of bus-off */
			if (cc770_status_interrupt(dev))
				break;
		} else {
			o = intid2obj(intid);

			if (o >= CC770_OBJ_MAX) {
				netdev_err(dev, "Unexpected interrupt id %d\n",
					   intid);
				continue;
			}

			if (priv->obj_flags[o] & CC770_OBJ_FLAG_RTR)
				cc770_rtr_interrupt(dev, o);
			else if (priv->obj_flags[o] & CC770_OBJ_FLAG_RX)
				cc770_rx_interrupt(dev, o);
			else
				cc770_tx_interrupt(dev, o);
		}
	}

	if (priv->post_irq)
		priv->post_irq(priv);

	if (n >= CC770_MAX_IRQ)
		netdev_dbg(dev, "%d messages handled in ISR", n);

	return (n) ? IRQ_HANDLED : IRQ_NONE;
}

static int cc770_open(struct net_device *dev)
{
	struct cc770_priv *priv = netdev_priv(dev);
	int err;

	/* set chip into reset mode */
	set_reset_mode(dev);

	/* common open */
	err = open_candev(dev);
	if (err)
		return err;

	err = request_irq(dev->irq, &cc770_interrupt, priv->irq_flags,
			  dev->name, dev);
	if (err) {
		close_candev(dev);
		return -EAGAIN;
	}

	/* init and start chip */
	cc770_start(dev);

	netif_start_queue(dev);

	return 0;
}

static int cc770_close(struct net_device *dev)
{
	netif_stop_queue(dev);
	set_reset_mode(dev);

	free_irq(dev->irq, dev);
	close_candev(dev);

	return 0;
}

struct net_device *alloc_cc770dev(int sizeof_priv)
{
	struct net_device *dev;
	struct cc770_priv *priv;

	dev = alloc_candev(sizeof(struct cc770_priv) + sizeof_priv,
			   CC770_ECHO_SKB_MAX);
	if (!dev)
		return NULL;

	priv = netdev_priv(dev);

	priv->dev = dev;
	priv->can.bittiming_const = &cc770_bittiming_const;
	priv->can.do_set_bittiming = cc770_set_bittiming;
	priv->can.do_set_mode = cc770_set_mode;
	priv->can.ctrlmode_supported = CAN_CTRLMODE_3_SAMPLES;

	memcpy(priv->obj_flags, cc770_obj_flags, sizeof(cc770_obj_flags));

	if (sizeof_priv)
		priv->priv = (void *)priv + sizeof(struct cc770_priv);

	return dev;
}
EXPORT_SYMBOL_GPL(alloc_cc770dev);

void free_cc770dev(struct net_device *dev)
{
	free_candev(dev);
}
EXPORT_SYMBOL_GPL(free_cc770dev);

static const struct net_device_ops cc770_netdev_ops = {
	.ndo_open = cc770_open,
	.ndo_stop = cc770_close,
	.ndo_start_xmit = cc770_start_xmit,
};

int register_cc770dev(struct net_device *dev)
{
	struct cc770_priv *priv = netdev_priv(dev);
	int err;

	err = cc770_probe_chip(dev);
	if (err)
		return err;

	dev->netdev_ops = &cc770_netdev_ops;

	dev->flags |= IFF_ECHO;	/* we support local echo */

	/* Should we use additional functions? */
	if (!i82527_compat && priv->control_normal_mode & CTRL_EAF) {
		priv->can.do_get_berr_counter = cc770_get_berr_counter;
		priv->control_normal_mode = CTRL_IE | CTRL_EAF | CTRL_EIE;
		netdev_dbg(dev, "i82527 mode with additional functions\n");
	} else {
		priv->control_normal_mode = CTRL_IE | CTRL_EIE;
		netdev_dbg(dev, "strict i82527 compatibility mode\n");
	}

	chipset_init(priv);
	set_reset_mode(dev);

	return register_candev(dev);
}
EXPORT_SYMBOL_GPL(register_cc770dev);

void unregister_cc770dev(struct net_device *dev)
{
	set_reset_mode(dev);
	unregister_candev(dev);
}
EXPORT_SYMBOL_GPL(unregister_cc770dev);

static __init int cc770_init(void)
{
	if (msgobj15_eff) {
		cc770_obj_flags[CC770_OBJ_RX0] |= CC770_OBJ_FLAG_EFF;
		cc770_obj_flags[CC770_OBJ_RX1] &= ~CC770_OBJ_FLAG_EFF;
	}

	pr_info("CAN netdevice driver\n");

	return 0;
}
module_init(cc770_init);

static __exit void cc770_exit(void)
{
	pr_info("driver removed\n");
}
module_exit(cc770_exit);
