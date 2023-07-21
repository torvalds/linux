/*
 * sun4i_can.c - CAN bus controller driver for Allwinner SUN4I&SUN7I based SoCs
 *
 * Copyright (C) 2013 Peter Chen
 * Copyright (C) 2015 Gerhard Bertelsmann
 * All rights reserved.
 *
 * Parts of this software are based on (derived from) the SJA1000 code by:
 *   Copyright (C) 2014 Oliver Hartkopp <oliver.hartkopp@volkswagen.de>
 *   Copyright (C) 2007 Wolfgang Grandegger <wg@grandegger.com>
 *   Copyright (C) 2002-2007 Volkswagen Group Electronic Research
 *   Copyright (C) 2003 Matthias Brukner, Trajet Gmbh, Rebenring 33,
 *   38106 Braunschweig, GERMANY
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of Volkswagen nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * Alternatively, provided that this notice is retained in full, this
 * software may be distributed under the terms of the GNU General
 * Public License ("GPL") version 2, in which case the provisions of the
 * GPL apply INSTEAD OF those given above.
 *
 * The provided data structures and external interfaces from this code
 * are not restricted to be used by modules with a GPL compatible license.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH
 * DAMAGE.
 *
 */

#include <linux/netdevice.h>
#include <linux/can.h>
#include <linux/can/dev.h>
#include <linux/can/error.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/ethtool.h>
#include <linux/interrupt.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/reset.h>

#define DRV_NAME "sun4i_can"

/* Registers address (physical base address 0x01C2BC00) */
#define SUN4I_REG_MSEL_ADDR	0x0000	/* CAN Mode Select */
#define SUN4I_REG_CMD_ADDR	0x0004	/* CAN Command */
#define SUN4I_REG_STA_ADDR	0x0008	/* CAN Status */
#define SUN4I_REG_INT_ADDR	0x000c	/* CAN Interrupt Flag */
#define SUN4I_REG_INTEN_ADDR	0x0010	/* CAN Interrupt Enable */
#define SUN4I_REG_BTIME_ADDR	0x0014	/* CAN Bus Timing 0 */
#define SUN4I_REG_TEWL_ADDR	0x0018	/* CAN Tx Error Warning Limit */
#define SUN4I_REG_ERRC_ADDR	0x001c	/* CAN Error Counter */
#define SUN4I_REG_RMCNT_ADDR	0x0020	/* CAN Receive Message Counter */
#define SUN4I_REG_RBUFSA_ADDR	0x0024	/* CAN Receive Buffer Start Address */
#define SUN4I_REG_BUF0_ADDR	0x0040	/* CAN Tx/Rx Buffer 0 */
#define SUN4I_REG_BUF1_ADDR	0x0044	/* CAN Tx/Rx Buffer 1 */
#define SUN4I_REG_BUF2_ADDR	0x0048	/* CAN Tx/Rx Buffer 2 */
#define SUN4I_REG_BUF3_ADDR	0x004c	/* CAN Tx/Rx Buffer 3 */
#define SUN4I_REG_BUF4_ADDR	0x0050	/* CAN Tx/Rx Buffer 4 */
#define SUN4I_REG_BUF5_ADDR	0x0054	/* CAN Tx/Rx Buffer 5 */
#define SUN4I_REG_BUF6_ADDR	0x0058	/* CAN Tx/Rx Buffer 6 */
#define SUN4I_REG_BUF7_ADDR	0x005c	/* CAN Tx/Rx Buffer 7 */
#define SUN4I_REG_BUF8_ADDR	0x0060	/* CAN Tx/Rx Buffer 8 */
#define SUN4I_REG_BUF9_ADDR	0x0064	/* CAN Tx/Rx Buffer 9 */
#define SUN4I_REG_BUF10_ADDR	0x0068	/* CAN Tx/Rx Buffer 10 */
#define SUN4I_REG_BUF11_ADDR	0x006c	/* CAN Tx/Rx Buffer 11 */
#define SUN4I_REG_BUF12_ADDR	0x0070	/* CAN Tx/Rx Buffer 12 */
#define SUN4I_REG_ACPC_ADDR	0x0040	/* CAN Acceptance Code 0 */
#define SUN4I_REG_ACPM_ADDR	0x0044	/* CAN Acceptance Mask 0 */
#define SUN4I_REG_ACPC_ADDR_D1	0x0028	/* CAN Acceptance Code 0 on the D1 */
#define SUN4I_REG_ACPM_ADDR_D1	0x002C	/* CAN Acceptance Mask 0 on the D1 */
#define SUN4I_REG_RBUF_RBACK_START_ADDR	0x0180	/* CAN transmit buffer start */
#define SUN4I_REG_RBUF_RBACK_END_ADDR	0x01b0	/* CAN transmit buffer end */

/* Controller Register Description */

/* mode select register (r/w)
 * offset:0x0000 default:0x0000_0001
 */
#define SUN4I_MSEL_SLEEP_MODE		(0x01 << 4) /* write in reset mode */
#define SUN4I_MSEL_WAKE_UP		(0x00 << 4)
#define SUN4I_MSEL_SINGLE_FILTER	(0x01 << 3) /* write in reset mode */
#define SUN4I_MSEL_DUAL_FILTERS		(0x00 << 3)
#define SUN4I_MSEL_LOOPBACK_MODE	BIT(2)
#define SUN4I_MSEL_LISTEN_ONLY_MODE	BIT(1)
#define SUN4I_MSEL_RESET_MODE		BIT(0)

/* command register (w)
 * offset:0x0004 default:0x0000_0000
 */
#define SUN4I_CMD_BUS_OFF_REQ	BIT(5)
#define SUN4I_CMD_SELF_RCV_REQ	BIT(4)
#define SUN4I_CMD_CLEAR_OR_FLAG	BIT(3)
#define SUN4I_CMD_RELEASE_RBUF	BIT(2)
#define SUN4I_CMD_ABORT_REQ	BIT(1)
#define SUN4I_CMD_TRANS_REQ	BIT(0)

/* status register (r)
 * offset:0x0008 default:0x0000_003c
 */
#define SUN4I_STA_BIT_ERR	(0x00 << 22)
#define SUN4I_STA_FORM_ERR	(0x01 << 22)
#define SUN4I_STA_STUFF_ERR	(0x02 << 22)
#define SUN4I_STA_OTHER_ERR	(0x03 << 22)
#define SUN4I_STA_MASK_ERR	(0x03 << 22)
#define SUN4I_STA_ERR_DIR	BIT(21)
#define SUN4I_STA_ERR_SEG_CODE	(0x1f << 16)
#define SUN4I_STA_START		(0x03 << 16)
#define SUN4I_STA_ID28_21	(0x02 << 16)
#define SUN4I_STA_ID20_18	(0x06 << 16)
#define SUN4I_STA_SRTR		(0x04 << 16)
#define SUN4I_STA_IDE		(0x05 << 16)
#define SUN4I_STA_ID17_13	(0x07 << 16)
#define SUN4I_STA_ID12_5	(0x0f << 16)
#define SUN4I_STA_ID4_0		(0x0e << 16)
#define SUN4I_STA_RTR		(0x0c << 16)
#define SUN4I_STA_RB1		(0x0d << 16)
#define SUN4I_STA_RB0		(0x09 << 16)
#define SUN4I_STA_DLEN		(0x0b << 16)
#define SUN4I_STA_DATA_FIELD	(0x0a << 16)
#define SUN4I_STA_CRC_SEQUENCE	(0x08 << 16)
#define SUN4I_STA_CRC_DELIMITER	(0x18 << 16)
#define SUN4I_STA_ACK		(0x19 << 16)
#define SUN4I_STA_ACK_DELIMITER	(0x1b << 16)
#define SUN4I_STA_END		(0x1a << 16)
#define SUN4I_STA_INTERMISSION	(0x12 << 16)
#define SUN4I_STA_ACTIVE_ERROR	(0x11 << 16)
#define SUN4I_STA_PASSIVE_ERROR	(0x16 << 16)
#define SUN4I_STA_TOLERATE_DOMINANT_BITS	(0x13 << 16)
#define SUN4I_STA_ERROR_DELIMITER	(0x17 << 16)
#define SUN4I_STA_OVERLOAD	(0x1c << 16)
#define SUN4I_STA_BUS_OFF	BIT(7)
#define SUN4I_STA_ERR_STA	BIT(6)
#define SUN4I_STA_TRANS_BUSY	BIT(5)
#define SUN4I_STA_RCV_BUSY	BIT(4)
#define SUN4I_STA_TRANS_OVER	BIT(3)
#define SUN4I_STA_TBUF_RDY	BIT(2)
#define SUN4I_STA_DATA_ORUN	BIT(1)
#define SUN4I_STA_RBUF_RDY	BIT(0)

/* interrupt register (r)
 * offset:0x000c default:0x0000_0000
 */
#define SUN4I_INT_BUS_ERR	BIT(7)
#define SUN4I_INT_ARB_LOST	BIT(6)
#define SUN4I_INT_ERR_PASSIVE	BIT(5)
#define SUN4I_INT_WAKEUP	BIT(4)
#define SUN4I_INT_DATA_OR	BIT(3)
#define SUN4I_INT_ERR_WRN	BIT(2)
#define SUN4I_INT_TBUF_VLD	BIT(1)
#define SUN4I_INT_RBUF_VLD	BIT(0)

/* interrupt enable register (r/w)
 * offset:0x0010 default:0x0000_0000
 */
#define SUN4I_INTEN_BERR	BIT(7)
#define SUN4I_INTEN_ARB_LOST	BIT(6)
#define SUN4I_INTEN_ERR_PASSIVE	BIT(5)
#define SUN4I_INTEN_WAKEUP	BIT(4)
#define SUN4I_INTEN_OR		BIT(3)
#define SUN4I_INTEN_ERR_WRN	BIT(2)
#define SUN4I_INTEN_TX		BIT(1)
#define SUN4I_INTEN_RX		BIT(0)

/* error code */
#define SUN4I_ERR_INRCV		(0x1 << 5)
#define SUN4I_ERR_INTRANS	(0x0 << 5)

/* filter mode */
#define SUN4I_FILTER_CLOSE	0
#define SUN4I_SINGLE_FLTER_MODE	1
#define SUN4I_DUAL_FILTER_MODE	2

/* message buffer flags */
#define SUN4I_MSG_EFF_FLAG	BIT(7)
#define SUN4I_MSG_RTR_FLAG	BIT(6)

/* max. number of interrupts handled in ISR */
#define SUN4I_CAN_MAX_IRQ	20
#define SUN4I_MODE_MAX_RETRIES	100

/**
 * struct sun4ican_quirks - Differences between SoC variants.
 *
 * @has_reset: SoC needs reset deasserted.
 * @acp_offset: Offset of ACPC and ACPM registers
 */
struct sun4ican_quirks {
	bool has_reset;
	int acp_offset;
};

struct sun4ican_priv {
	struct can_priv can;
	void __iomem *base;
	struct clk *clk;
	struct reset_control *reset;
	spinlock_t cmdreg_lock;	/* lock for concurrent cmd register writes */
	int acp_offset;
};

static const struct can_bittiming_const sun4ican_bittiming_const = {
	.name = DRV_NAME,
	.tseg1_min = 1,
	.tseg1_max = 16,
	.tseg2_min = 1,
	.tseg2_max = 8,
	.sjw_max = 4,
	.brp_min = 1,
	.brp_max = 64,
	.brp_inc = 1,
};

static void sun4i_can_write_cmdreg(struct sun4ican_priv *priv, u8 val)
{
	unsigned long flags;

	spin_lock_irqsave(&priv->cmdreg_lock, flags);
	writel(val, priv->base + SUN4I_REG_CMD_ADDR);
	spin_unlock_irqrestore(&priv->cmdreg_lock, flags);
}

static int set_normal_mode(struct net_device *dev)
{
	struct sun4ican_priv *priv = netdev_priv(dev);
	int retry = SUN4I_MODE_MAX_RETRIES;
	u32 mod_reg_val = 0;

	do {
		mod_reg_val = readl(priv->base + SUN4I_REG_MSEL_ADDR);
		mod_reg_val &= ~SUN4I_MSEL_RESET_MODE;
		writel(mod_reg_val, priv->base + SUN4I_REG_MSEL_ADDR);
	} while (retry-- && (mod_reg_val & SUN4I_MSEL_RESET_MODE));

	if (readl(priv->base + SUN4I_REG_MSEL_ADDR) & SUN4I_MSEL_RESET_MODE) {
		netdev_err(dev,
			   "setting controller into normal mode failed!\n");
		return -ETIMEDOUT;
	}

	return 0;
}

static int set_reset_mode(struct net_device *dev)
{
	struct sun4ican_priv *priv = netdev_priv(dev);
	int retry = SUN4I_MODE_MAX_RETRIES;
	u32 mod_reg_val = 0;

	do {
		mod_reg_val = readl(priv->base + SUN4I_REG_MSEL_ADDR);
		mod_reg_val |= SUN4I_MSEL_RESET_MODE;
		writel(mod_reg_val, priv->base + SUN4I_REG_MSEL_ADDR);
	} while (retry-- && !(mod_reg_val & SUN4I_MSEL_RESET_MODE));

	if (!(readl(priv->base + SUN4I_REG_MSEL_ADDR) &
	      SUN4I_MSEL_RESET_MODE)) {
		netdev_err(dev, "setting controller into reset mode failed!\n");
		return -ETIMEDOUT;
	}

	return 0;
}

/* bittiming is called in reset_mode only */
static int sun4ican_set_bittiming(struct net_device *dev)
{
	struct sun4ican_priv *priv = netdev_priv(dev);
	struct can_bittiming *bt = &priv->can.bittiming;
	u32 cfg;

	cfg = ((bt->brp - 1) & 0x3FF) |
	     (((bt->sjw - 1) & 0x3) << 14) |
	     (((bt->prop_seg + bt->phase_seg1 - 1) & 0xf) << 16) |
	     (((bt->phase_seg2 - 1) & 0x7) << 20);
	if (priv->can.ctrlmode & CAN_CTRLMODE_3_SAMPLES)
		cfg |= 0x800000;

	netdev_dbg(dev, "setting BITTIMING=0x%08x\n", cfg);
	writel(cfg, priv->base + SUN4I_REG_BTIME_ADDR);

	return 0;
}

static int sun4ican_get_berr_counter(const struct net_device *dev,
				     struct can_berr_counter *bec)
{
	struct sun4ican_priv *priv = netdev_priv(dev);
	u32 errors;
	int err;

	err = clk_prepare_enable(priv->clk);
	if (err) {
		netdev_err(dev, "could not enable clock\n");
		return err;
	}

	errors = readl(priv->base + SUN4I_REG_ERRC_ADDR);

	bec->txerr = errors & 0xFF;
	bec->rxerr = (errors >> 16) & 0xFF;

	clk_disable_unprepare(priv->clk);

	return 0;
}

static int sun4i_can_start(struct net_device *dev)
{
	struct sun4ican_priv *priv = netdev_priv(dev);
	int err;
	u32 mod_reg_val;

	/* we need to enter the reset mode */
	err = set_reset_mode(dev);
	if (err) {
		netdev_err(dev, "could not enter reset mode\n");
		return err;
	}

	/* set filters - we accept all */
	writel(0x00000000, priv->base + SUN4I_REG_ACPC_ADDR + priv->acp_offset);
	writel(0xFFFFFFFF, priv->base + SUN4I_REG_ACPM_ADDR + priv->acp_offset);

	/* clear error counters and error code capture */
	writel(0, priv->base + SUN4I_REG_ERRC_ADDR);

	/* enable interrupts */
	if (priv->can.ctrlmode & CAN_CTRLMODE_BERR_REPORTING)
		writel(0xFF, priv->base + SUN4I_REG_INTEN_ADDR);
	else
		writel(0xFF & ~SUN4I_INTEN_BERR,
		       priv->base + SUN4I_REG_INTEN_ADDR);

	/* enter the selected mode */
	mod_reg_val = readl(priv->base + SUN4I_REG_MSEL_ADDR);
	if (priv->can.ctrlmode & CAN_CTRLMODE_LOOPBACK)
		mod_reg_val |= SUN4I_MSEL_LOOPBACK_MODE;
	else if (priv->can.ctrlmode & CAN_CTRLMODE_LISTENONLY)
		mod_reg_val |= SUN4I_MSEL_LISTEN_ONLY_MODE;
	writel(mod_reg_val, priv->base + SUN4I_REG_MSEL_ADDR);

	err = sun4ican_set_bittiming(dev);
	if (err)
		return err;

	/* we are ready to enter the normal mode */
	err = set_normal_mode(dev);
	if (err) {
		netdev_err(dev, "could not enter normal mode\n");
		return err;
	}

	priv->can.state = CAN_STATE_ERROR_ACTIVE;

	return 0;
}

static int sun4i_can_stop(struct net_device *dev)
{
	struct sun4ican_priv *priv = netdev_priv(dev);
	int err;

	priv->can.state = CAN_STATE_STOPPED;
	/* we need to enter reset mode */
	err = set_reset_mode(dev);
	if (err) {
		netdev_err(dev, "could not enter reset mode\n");
		return err;
	}

	/* disable all interrupts */
	writel(0, priv->base + SUN4I_REG_INTEN_ADDR);

	return 0;
}

static int sun4ican_set_mode(struct net_device *dev, enum can_mode mode)
{
	int err;

	switch (mode) {
	case CAN_MODE_START:
		err = sun4i_can_start(dev);
		if (err) {
			netdev_err(dev, "starting CAN controller failed!\n");
			return err;
		}
		if (netif_queue_stopped(dev))
			netif_wake_queue(dev);
		break;

	default:
		return -EOPNOTSUPP;
	}
	return 0;
}

/* transmit a CAN message
 * message layout in the sk_buff should be like this:
 * xx xx xx xx         ff         ll 00 11 22 33 44 55 66 77
 * [ can_id ] [flags] [len] [can data (up to 8 bytes]
 */
static netdev_tx_t sun4ican_start_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct sun4ican_priv *priv = netdev_priv(dev);
	struct can_frame *cf = (struct can_frame *)skb->data;
	u8 dlc;
	u32 dreg, msg_flag_n;
	canid_t id;
	int i;

	if (can_dev_dropped_skb(dev, skb))
		return NETDEV_TX_OK;

	netif_stop_queue(dev);

	id = cf->can_id;
	dlc = cf->len;
	msg_flag_n = dlc;

	if (id & CAN_RTR_FLAG)
		msg_flag_n |= SUN4I_MSG_RTR_FLAG;

	if (id & CAN_EFF_FLAG) {
		msg_flag_n |= SUN4I_MSG_EFF_FLAG;
		dreg = SUN4I_REG_BUF5_ADDR;
		writel((id >> 21) & 0xFF, priv->base + SUN4I_REG_BUF1_ADDR);
		writel((id >> 13) & 0xFF, priv->base + SUN4I_REG_BUF2_ADDR);
		writel((id >> 5)  & 0xFF, priv->base + SUN4I_REG_BUF3_ADDR);
		writel((id << 3)  & 0xF8, priv->base + SUN4I_REG_BUF4_ADDR);
	} else {
		dreg = SUN4I_REG_BUF3_ADDR;
		writel((id >> 3) & 0xFF, priv->base + SUN4I_REG_BUF1_ADDR);
		writel((id << 5) & 0xE0, priv->base + SUN4I_REG_BUF2_ADDR);
	}

	for (i = 0; i < dlc; i++)
		writel(cf->data[i], priv->base + (dreg + i * 4));

	writel(msg_flag_n, priv->base + SUN4I_REG_BUF0_ADDR);

	can_put_echo_skb(skb, dev, 0, 0);

	if (priv->can.ctrlmode & CAN_CTRLMODE_LOOPBACK)
		sun4i_can_write_cmdreg(priv, SUN4I_CMD_SELF_RCV_REQ);
	else
		sun4i_can_write_cmdreg(priv, SUN4I_CMD_TRANS_REQ);

	return NETDEV_TX_OK;
}

static void sun4i_can_rx(struct net_device *dev)
{
	struct sun4ican_priv *priv = netdev_priv(dev);
	struct net_device_stats *stats = &dev->stats;
	struct can_frame *cf;
	struct sk_buff *skb;
	u8 fi;
	u32 dreg;
	canid_t id;
	int i;

	/* create zero'ed CAN frame buffer */
	skb = alloc_can_skb(dev, &cf);
	if (!skb)
		return;

	fi = readl(priv->base + SUN4I_REG_BUF0_ADDR);
	cf->len = can_cc_dlc2len(fi & 0x0F);
	if (fi & SUN4I_MSG_EFF_FLAG) {
		dreg = SUN4I_REG_BUF5_ADDR;
		id = (readl(priv->base + SUN4I_REG_BUF1_ADDR) << 21) |
		     (readl(priv->base + SUN4I_REG_BUF2_ADDR) << 13) |
		     (readl(priv->base + SUN4I_REG_BUF3_ADDR) << 5)  |
		    ((readl(priv->base + SUN4I_REG_BUF4_ADDR) >> 3)  & 0x1f);
		id |= CAN_EFF_FLAG;
	} else {
		dreg = SUN4I_REG_BUF3_ADDR;
		id = (readl(priv->base + SUN4I_REG_BUF1_ADDR) << 3) |
		    ((readl(priv->base + SUN4I_REG_BUF2_ADDR) >> 5) & 0x7);
	}

	/* remote frame ? */
	if (fi & SUN4I_MSG_RTR_FLAG) {
		id |= CAN_RTR_FLAG;
	} else {
		for (i = 0; i < cf->len; i++)
			cf->data[i] = readl(priv->base + dreg + i * 4);

		stats->rx_bytes += cf->len;
	}
	stats->rx_packets++;

	cf->can_id = id;

	sun4i_can_write_cmdreg(priv, SUN4I_CMD_RELEASE_RBUF);

	netif_rx(skb);
}

static int sun4i_can_err(struct net_device *dev, u8 isrc, u8 status)
{
	struct sun4ican_priv *priv = netdev_priv(dev);
	struct net_device_stats *stats = &dev->stats;
	struct can_frame *cf;
	struct sk_buff *skb;
	enum can_state state = priv->can.state;
	enum can_state rx_state, tx_state;
	unsigned int rxerr, txerr, errc;
	u32 ecc, alc;

	/* we don't skip if alloc fails because we want the stats anyhow */
	skb = alloc_can_err_skb(dev, &cf);

	errc = readl(priv->base + SUN4I_REG_ERRC_ADDR);
	rxerr = (errc >> 16) & 0xFF;
	txerr = errc & 0xFF;

	if (isrc & SUN4I_INT_DATA_OR) {
		/* data overrun interrupt */
		netdev_dbg(dev, "data overrun interrupt\n");
		if (likely(skb)) {
			cf->can_id |= CAN_ERR_CRTL;
			cf->data[1] = CAN_ERR_CRTL_RX_OVERFLOW;
		}
		stats->rx_over_errors++;
		stats->rx_errors++;

		/* reset the CAN IP by entering reset mode
		 * ignoring timeout error
		 */
		set_reset_mode(dev);
		set_normal_mode(dev);

		/* clear bit */
		sun4i_can_write_cmdreg(priv, SUN4I_CMD_CLEAR_OR_FLAG);
	}
	if (isrc & SUN4I_INT_ERR_WRN) {
		/* error warning interrupt */
		netdev_dbg(dev, "error warning interrupt\n");

		if (status & SUN4I_STA_BUS_OFF)
			state = CAN_STATE_BUS_OFF;
		else if (status & SUN4I_STA_ERR_STA)
			state = CAN_STATE_ERROR_WARNING;
		else
			state = CAN_STATE_ERROR_ACTIVE;
	}
	if (skb && state != CAN_STATE_BUS_OFF) {
		cf->can_id |= CAN_ERR_CNT;
		cf->data[6] = txerr;
		cf->data[7] = rxerr;
	}
	if (isrc & SUN4I_INT_BUS_ERR) {
		/* bus error interrupt */
		netdev_dbg(dev, "bus error interrupt\n");
		priv->can.can_stats.bus_error++;
		stats->rx_errors++;

		if (likely(skb)) {
			ecc = readl(priv->base + SUN4I_REG_STA_ADDR);

			cf->can_id |= CAN_ERR_PROT | CAN_ERR_BUSERROR;

			switch (ecc & SUN4I_STA_MASK_ERR) {
			case SUN4I_STA_BIT_ERR:
				cf->data[2] |= CAN_ERR_PROT_BIT;
				break;
			case SUN4I_STA_FORM_ERR:
				cf->data[2] |= CAN_ERR_PROT_FORM;
				break;
			case SUN4I_STA_STUFF_ERR:
				cf->data[2] |= CAN_ERR_PROT_STUFF;
				break;
			default:
				cf->data[3] = (ecc & SUN4I_STA_ERR_SEG_CODE)
					       >> 16;
				break;
			}
			/* error occurred during transmission? */
			if ((ecc & SUN4I_STA_ERR_DIR) == 0)
				cf->data[2] |= CAN_ERR_PROT_TX;
		}
	}
	if (isrc & SUN4I_INT_ERR_PASSIVE) {
		/* error passive interrupt */
		netdev_dbg(dev, "error passive interrupt\n");
		if (state == CAN_STATE_ERROR_PASSIVE)
			state = CAN_STATE_ERROR_WARNING;
		else
			state = CAN_STATE_ERROR_PASSIVE;
	}
	if (isrc & SUN4I_INT_ARB_LOST) {
		/* arbitration lost interrupt */
		netdev_dbg(dev, "arbitration lost interrupt\n");
		alc = readl(priv->base + SUN4I_REG_STA_ADDR);
		priv->can.can_stats.arbitration_lost++;
		if (likely(skb)) {
			cf->can_id |= CAN_ERR_LOSTARB;
			cf->data[0] = (alc >> 8) & 0x1f;
		}
	}

	if (state != priv->can.state) {
		tx_state = txerr >= rxerr ? state : 0;
		rx_state = txerr <= rxerr ? state : 0;

		if (likely(skb))
			can_change_state(dev, cf, tx_state, rx_state);
		else
			priv->can.state = state;
		if (state == CAN_STATE_BUS_OFF)
			can_bus_off(dev);
	}

	if (likely(skb))
		netif_rx(skb);
	else
		return -ENOMEM;

	return 0;
}

static irqreturn_t sun4i_can_interrupt(int irq, void *dev_id)
{
	struct net_device *dev = (struct net_device *)dev_id;
	struct sun4ican_priv *priv = netdev_priv(dev);
	struct net_device_stats *stats = &dev->stats;
	u8 isrc, status;
	int n = 0;

	while ((isrc = readl(priv->base + SUN4I_REG_INT_ADDR)) &&
	       (n < SUN4I_CAN_MAX_IRQ)) {
		n++;
		status = readl(priv->base + SUN4I_REG_STA_ADDR);

		if (isrc & SUN4I_INT_WAKEUP)
			netdev_warn(dev, "wakeup interrupt\n");

		if (isrc & SUN4I_INT_TBUF_VLD) {
			/* transmission complete interrupt */
			stats->tx_bytes += can_get_echo_skb(dev, 0, NULL);
			stats->tx_packets++;
			netif_wake_queue(dev);
		}
		if ((isrc & SUN4I_INT_RBUF_VLD) &&
		    !(isrc & SUN4I_INT_DATA_OR)) {
			/* receive interrupt - don't read if overrun occurred */
			while (status & SUN4I_STA_RBUF_RDY) {
				/* RX buffer is not empty */
				sun4i_can_rx(dev);
				status = readl(priv->base + SUN4I_REG_STA_ADDR);
			}
		}
		if (isrc &
		    (SUN4I_INT_DATA_OR | SUN4I_INT_ERR_WRN | SUN4I_INT_BUS_ERR |
		     SUN4I_INT_ERR_PASSIVE | SUN4I_INT_ARB_LOST)) {
			/* error interrupt */
			if (sun4i_can_err(dev, isrc, status))
				netdev_err(dev, "can't allocate buffer - clearing pending interrupts\n");
		}
		/* clear interrupts */
		writel(isrc, priv->base + SUN4I_REG_INT_ADDR);
		readl(priv->base + SUN4I_REG_INT_ADDR);
	}
	if (n >= SUN4I_CAN_MAX_IRQ)
		netdev_dbg(dev, "%d messages handled in ISR", n);

	return (n) ? IRQ_HANDLED : IRQ_NONE;
}

static int sun4ican_open(struct net_device *dev)
{
	struct sun4ican_priv *priv = netdev_priv(dev);
	int err;

	/* common open */
	err = open_candev(dev);
	if (err)
		return err;

	/* register interrupt handler */
	err = request_irq(dev->irq, sun4i_can_interrupt, 0, dev->name, dev);
	if (err) {
		netdev_err(dev, "request_irq err: %d\n", err);
		goto exit_irq;
	}

	/* software reset deassert */
	err = reset_control_deassert(priv->reset);
	if (err) {
		netdev_err(dev, "could not deassert CAN reset\n");
		goto exit_soft_reset;
	}

	/* turn on clocking for CAN peripheral block */
	err = clk_prepare_enable(priv->clk);
	if (err) {
		netdev_err(dev, "could not enable CAN peripheral clock\n");
		goto exit_clock;
	}

	err = sun4i_can_start(dev);
	if (err) {
		netdev_err(dev, "could not start CAN peripheral\n");
		goto exit_can_start;
	}

	netif_start_queue(dev);

	return 0;

exit_can_start:
	clk_disable_unprepare(priv->clk);
exit_clock:
	reset_control_assert(priv->reset);
exit_soft_reset:
	free_irq(dev->irq, dev);
exit_irq:
	close_candev(dev);
	return err;
}

static int sun4ican_close(struct net_device *dev)
{
	struct sun4ican_priv *priv = netdev_priv(dev);

	netif_stop_queue(dev);
	sun4i_can_stop(dev);
	clk_disable_unprepare(priv->clk);
	reset_control_assert(priv->reset);

	free_irq(dev->irq, dev);
	close_candev(dev);

	return 0;
}

static const struct net_device_ops sun4ican_netdev_ops = {
	.ndo_open = sun4ican_open,
	.ndo_stop = sun4ican_close,
	.ndo_start_xmit = sun4ican_start_xmit,
};

static const struct ethtool_ops sun4ican_ethtool_ops = {
	.get_ts_info = ethtool_op_get_ts_info,
};

static const struct sun4ican_quirks sun4ican_quirks_a10 = {
	.has_reset = false,
	.acp_offset = 0,
};

static const struct sun4ican_quirks sun4ican_quirks_r40 = {
	.has_reset = true,
	.acp_offset = 0,
};

static const struct sun4ican_quirks sun4ican_quirks_d1 = {
	.has_reset = true,
	.acp_offset = (SUN4I_REG_ACPC_ADDR_D1 - SUN4I_REG_ACPC_ADDR),
};

static const struct of_device_id sun4ican_of_match[] = {
	{
		.compatible = "allwinner,sun4i-a10-can",
		.data = &sun4ican_quirks_a10
	}, {
		.compatible = "allwinner,sun7i-a20-can",
		.data = &sun4ican_quirks_a10
	}, {
		.compatible = "allwinner,sun8i-r40-can",
		.data = &sun4ican_quirks_r40
	}, {
		.compatible = "allwinner,sun20i-d1-can",
		.data = &sun4ican_quirks_d1
	}, {
		/* sentinel */
	},
};

MODULE_DEVICE_TABLE(of, sun4ican_of_match);

static void sun4ican_remove(struct platform_device *pdev)
{
	struct net_device *dev = platform_get_drvdata(pdev);

	unregister_netdev(dev);
	free_candev(dev);
}

static int sun4ican_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct clk *clk;
	struct reset_control *reset = NULL;
	void __iomem *addr;
	int err, irq;
	struct net_device *dev;
	struct sun4ican_priv *priv;
	const struct sun4ican_quirks *quirks;

	quirks = of_device_get_match_data(&pdev->dev);
	if (!quirks) {
		dev_err(&pdev->dev, "failed to determine the quirks to use\n");
		err = -ENODEV;
		goto exit;
	}

	if (quirks->has_reset) {
		reset = devm_reset_control_get_exclusive(&pdev->dev, NULL);
		if (IS_ERR(reset)) {
			dev_err(&pdev->dev, "unable to request reset\n");
			err = PTR_ERR(reset);
			goto exit;
		}
	}

	clk = of_clk_get(np, 0);
	if (IS_ERR(clk)) {
		dev_err(&pdev->dev, "unable to request clock\n");
		err = -ENODEV;
		goto exit;
	}

	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		err = -ENODEV;
		goto exit;
	}

	addr = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(addr)) {
		err = PTR_ERR(addr);
		goto exit;
	}

	dev = alloc_candev(sizeof(struct sun4ican_priv), 1);
	if (!dev) {
		dev_err(&pdev->dev,
			"could not allocate memory for CAN device\n");
		err = -ENOMEM;
		goto exit;
	}

	dev->netdev_ops = &sun4ican_netdev_ops;
	dev->ethtool_ops = &sun4ican_ethtool_ops;
	dev->irq = irq;
	dev->flags |= IFF_ECHO;

	priv = netdev_priv(dev);
	priv->can.clock.freq = clk_get_rate(clk);
	priv->can.bittiming_const = &sun4ican_bittiming_const;
	priv->can.do_set_mode = sun4ican_set_mode;
	priv->can.do_get_berr_counter = sun4ican_get_berr_counter;
	priv->can.ctrlmode_supported = CAN_CTRLMODE_BERR_REPORTING |
				       CAN_CTRLMODE_LISTENONLY |
				       CAN_CTRLMODE_LOOPBACK |
				       CAN_CTRLMODE_3_SAMPLES;
	priv->base = addr;
	priv->clk = clk;
	priv->reset = reset;
	priv->acp_offset = quirks->acp_offset;
	spin_lock_init(&priv->cmdreg_lock);

	platform_set_drvdata(pdev, dev);
	SET_NETDEV_DEV(dev, &pdev->dev);

	err = register_candev(dev);
	if (err) {
		dev_err(&pdev->dev, "registering %s failed (err=%d)\n",
			DRV_NAME, err);
		goto exit_free;
	}

	dev_info(&pdev->dev, "device registered (base=%p, irq=%d)\n",
		 priv->base, dev->irq);

	return 0;

exit_free:
	free_candev(dev);
exit:
	return err;
}

static struct platform_driver sun4i_can_driver = {
	.driver = {
		.name = DRV_NAME,
		.of_match_table = sun4ican_of_match,
	},
	.probe = sun4ican_probe,
	.remove_new = sun4ican_remove,
};

module_platform_driver(sun4i_can_driver);

MODULE_AUTHOR("Peter Chen <xingkongcp@gmail.com>");
MODULE_AUTHOR("Gerhard Bertelsmann <info@gerhard-bertelsmann.de>");
MODULE_LICENSE("Dual BSD/GPL");
MODULE_DESCRIPTION("CAN driver for Allwinner SoCs (A10/A20/D1)");
