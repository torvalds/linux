/**
 * Microchip ENCX24J600 ethernet driver
 *
 * Copyright (C) 2015 Gridpoint
 * Author: Jon Ringle <jringle@gridpoint.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 */

#include <linux/device.h>
#include <linux/errno.h>
#include <linux/etherdevice.h>
#include <linux/ethtool.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/regmap.h>
#include <linux/skbuff.h>
#include <linux/spi/spi.h>

#include "encx24j600_hw.h"

#define DRV_NAME	"encx24j600"
#define DRV_VERSION	"1.0"

#define DEFAULT_MSG_ENABLE (NETIF_MSG_DRV | NETIF_MSG_PROBE | NETIF_MSG_LINK)
static int debug = -1;
module_param(debug, int, 0);
MODULE_PARM_DESC(debug, "Debug level (0=none,...,16=all)");

/* SRAM memory layout:
 *
 * 0x0000-0x05ff TX buffers  1.5KB  (1*1536) reside in the GP area in SRAM
 * 0x0600-0x5fff RX buffers 22.5KB (15*1536) reside in the RX area in SRAM
 */
#define ENC_TX_BUF_START 0x0000U
#define ENC_RX_BUF_START 0x0600U
#define ENC_RX_BUF_END   0x5fffU
#define ENC_SRAM_SIZE    0x6000U

enum {
	RXFILTER_NORMAL,
	RXFILTER_MULTI,
	RXFILTER_PROMISC
};

struct encx24j600_priv {
	struct net_device        *ndev;
	struct mutex              lock; /* device access lock */
	struct encx24j600_context ctx;
	struct sk_buff           *tx_skb;
	struct task_struct       *kworker_task;
	struct kthread_worker     kworker;
	struct kthread_work       tx_work;
	struct kthread_work       setrx_work;
	u16                       next_packet;
	bool                      hw_enabled;
	bool                      full_duplex;
	bool                      autoneg;
	u16                       speed;
	int                       rxfilter;
	u32                       msg_enable;
};

static void dump_packet(const char *msg, int len, const char *data)
{
	pr_debug(DRV_NAME ": %s - packet len:%d\n", msg, len);
	print_hex_dump_bytes("pk data: ", DUMP_PREFIX_OFFSET, data, len);
}

static void encx24j600_dump_rsv(struct encx24j600_priv *priv, const char *msg,
				struct rsv *rsv)
{
	struct net_device *dev = priv->ndev;

	netdev_info(dev, "RX packet Len:%d\n", rsv->len);
	netdev_dbg(dev, "%s - NextPk: 0x%04x\n", msg,
		   rsv->next_packet);
	netdev_dbg(dev, "RxOK: %d, DribbleNibble: %d\n",
		   RSV_GETBIT(rsv->rxstat, RSV_RXOK),
		   RSV_GETBIT(rsv->rxstat, RSV_DRIBBLENIBBLE));
	netdev_dbg(dev, "CRCErr:%d, LenChkErr: %d, LenOutOfRange: %d\n",
		   RSV_GETBIT(rsv->rxstat, RSV_CRCERROR),
		   RSV_GETBIT(rsv->rxstat, RSV_LENCHECKERR),
		   RSV_GETBIT(rsv->rxstat, RSV_LENOUTOFRANGE));
	netdev_dbg(dev, "Multicast: %d, Broadcast: %d, LongDropEvent: %d, CarrierEvent: %d\n",
		   RSV_GETBIT(rsv->rxstat, RSV_RXMULTICAST),
		   RSV_GETBIT(rsv->rxstat, RSV_RXBROADCAST),
		   RSV_GETBIT(rsv->rxstat, RSV_RXLONGEVDROPEV),
		   RSV_GETBIT(rsv->rxstat, RSV_CARRIEREV));
	netdev_dbg(dev, "ControlFrame: %d, PauseFrame: %d, UnknownOp: %d, VLanTagFrame: %d\n",
		   RSV_GETBIT(rsv->rxstat, RSV_RXCONTROLFRAME),
		   RSV_GETBIT(rsv->rxstat, RSV_RXPAUSEFRAME),
		   RSV_GETBIT(rsv->rxstat, RSV_RXUNKNOWNOPCODE),
		   RSV_GETBIT(rsv->rxstat, RSV_RXTYPEVLAN));
}

static u16 encx24j600_read_reg(struct encx24j600_priv *priv, u8 reg)
{
	struct net_device *dev = priv->ndev;
	unsigned int val = 0;
	int ret = regmap_read(priv->ctx.regmap, reg, &val);
	if (unlikely(ret))
		netif_err(priv, drv, dev, "%s: error %d reading reg %02x\n",
			  __func__, ret, reg);
	return val;
}

static void encx24j600_write_reg(struct encx24j600_priv *priv, u8 reg, u16 val)
{
	struct net_device *dev = priv->ndev;
	int ret = regmap_write(priv->ctx.regmap, reg, val);
	if (unlikely(ret))
		netif_err(priv, drv, dev, "%s: error %d writing reg %02x=%04x\n",
			  __func__, ret, reg, val);
}

static void encx24j600_update_reg(struct encx24j600_priv *priv, u8 reg,
				  u16 mask, u16 val)
{
	struct net_device *dev = priv->ndev;
	int ret = regmap_update_bits(priv->ctx.regmap, reg, mask, val);
	if (unlikely(ret))
		netif_err(priv, drv, dev, "%s: error %d updating reg %02x=%04x~%04x\n",
			  __func__, ret, reg, val, mask);
}

static u16 encx24j600_read_phy(struct encx24j600_priv *priv, u8 reg)
{
	struct net_device *dev = priv->ndev;
	unsigned int val = 0;
	int ret = regmap_read(priv->ctx.phymap, reg, &val);
	if (unlikely(ret))
		netif_err(priv, drv, dev, "%s: error %d reading %02x\n",
			  __func__, ret, reg);
	return val;
}

static void encx24j600_write_phy(struct encx24j600_priv *priv, u8 reg, u16 val)
{
	struct net_device *dev = priv->ndev;
	int ret = regmap_write(priv->ctx.phymap, reg, val);
	if (unlikely(ret))
		netif_err(priv, drv, dev, "%s: error %d writing reg %02x=%04x\n",
			  __func__, ret, reg, val);
}

static void encx24j600_clr_bits(struct encx24j600_priv *priv, u8 reg, u16 mask)
{
	encx24j600_update_reg(priv, reg, mask, 0);
}

static void encx24j600_set_bits(struct encx24j600_priv *priv, u8 reg, u16 mask)
{
	encx24j600_update_reg(priv, reg, mask, mask);
}

static void encx24j600_cmd(struct encx24j600_priv *priv, u8 cmd)
{
	struct net_device *dev = priv->ndev;
	int ret = regmap_write(priv->ctx.regmap, cmd, 0);
	if (unlikely(ret))
		netif_err(priv, drv, dev, "%s: error %d with cmd %02x\n",
			  __func__, ret, cmd);
}

static int encx24j600_raw_read(struct encx24j600_priv *priv, u8 reg, u8 *data,
			       size_t count)
{
	int ret;
	mutex_lock(&priv->ctx.mutex);
	ret = regmap_encx24j600_spi_read(&priv->ctx, reg, data, count);
	mutex_unlock(&priv->ctx.mutex);

	return ret;
}

static int encx24j600_raw_write(struct encx24j600_priv *priv, u8 reg,
				const u8 *data, size_t count)
{
	int ret;
	mutex_lock(&priv->ctx.mutex);
	ret = regmap_encx24j600_spi_write(&priv->ctx, reg, data, count);
	mutex_unlock(&priv->ctx.mutex);

	return ret;
}

static void encx24j600_update_phcon1(struct encx24j600_priv *priv)
{
	u16 phcon1 = encx24j600_read_phy(priv, PHCON1);
	if (priv->autoneg == AUTONEG_ENABLE) {
		phcon1 |= ANEN | RENEG;
	} else {
		phcon1 &= ~ANEN;
		if (priv->speed == SPEED_100)
			phcon1 |= SPD100;
		else
			phcon1 &= ~SPD100;

		if (priv->full_duplex)
			phcon1 |= PFULDPX;
		else
			phcon1 &= ~PFULDPX;
	}
	encx24j600_write_phy(priv, PHCON1, phcon1);
}

/* Waits for autonegotiation to complete. */
static int encx24j600_wait_for_autoneg(struct encx24j600_priv *priv)
{
	struct net_device *dev = priv->ndev;
	unsigned long timeout = jiffies + msecs_to_jiffies(2000);
	u16 phstat1;
	u16 estat;
	int ret = 0;

	phstat1 = encx24j600_read_phy(priv, PHSTAT1);
	while ((phstat1 & ANDONE) == 0) {
		if (time_after(jiffies, timeout)) {
			u16 phstat3;

			netif_notice(priv, drv, dev, "timeout waiting for autoneg done\n");

			priv->autoneg = AUTONEG_DISABLE;
			phstat3 = encx24j600_read_phy(priv, PHSTAT3);
			priv->speed = (phstat3 & PHY3SPD100)
				      ? SPEED_100 : SPEED_10;
			priv->full_duplex = (phstat3 & PHY3DPX) ? 1 : 0;
			encx24j600_update_phcon1(priv);
			netif_notice(priv, drv, dev, "Using parallel detection: %s/%s",
				     priv->speed == SPEED_100 ? "100" : "10",
				     priv->full_duplex ? "Full" : "Half");

			return -ETIMEDOUT;
		}
		cpu_relax();
		phstat1 = encx24j600_read_phy(priv, PHSTAT1);
	}

	estat = encx24j600_read_reg(priv, ESTAT);
	if (estat & PHYDPX) {
		encx24j600_set_bits(priv, MACON2, FULDPX);
		encx24j600_write_reg(priv, MABBIPG, 0x15);
	} else {
		encx24j600_clr_bits(priv, MACON2, FULDPX);
		encx24j600_write_reg(priv, MABBIPG, 0x12);
		/* Max retransmittions attempt  */
		encx24j600_write_reg(priv, MACLCON, 0x370f);
	}

	return ret;
}

/* Access the PHY to determine link status */
static void encx24j600_check_link_status(struct encx24j600_priv *priv)
{
	struct net_device *dev = priv->ndev;
	u16 estat;

	estat = encx24j600_read_reg(priv, ESTAT);

	if (estat & PHYLNK) {
		if (priv->autoneg == AUTONEG_ENABLE)
			encx24j600_wait_for_autoneg(priv);

		netif_carrier_on(dev);
		netif_info(priv, ifup, dev, "link up\n");
	} else {
		netif_info(priv, ifdown, dev, "link down\n");

		/* Re-enable autoneg since we won't know what we might be
		 * connected to when the link is brought back up again.
		 */
		priv->autoneg  = AUTONEG_ENABLE;
		priv->full_duplex = true;
		priv->speed = SPEED_100;
		netif_carrier_off(dev);
	}
}

static void encx24j600_int_link_handler(struct encx24j600_priv *priv)
{
	struct net_device *dev = priv->ndev;

	netif_dbg(priv, intr, dev, "%s", __func__);
	encx24j600_check_link_status(priv);
	encx24j600_clr_bits(priv, EIR, LINKIF);
}

static void encx24j600_tx_complete(struct encx24j600_priv *priv, bool err)
{
	struct net_device *dev = priv->ndev;

	if (!priv->tx_skb) {
		BUG();
		return;
	}

	mutex_lock(&priv->lock);

	if (err)
		dev->stats.tx_errors++;
	else
		dev->stats.tx_packets++;

	dev->stats.tx_bytes += priv->tx_skb->len;

	encx24j600_clr_bits(priv, EIR, TXIF | TXABTIF);

	netif_dbg(priv, tx_done, dev, "TX Done%s\n", err ? ": Err" : "");

	dev_kfree_skb(priv->tx_skb);
	priv->tx_skb = NULL;

	netif_wake_queue(dev);

	mutex_unlock(&priv->lock);
}

static int encx24j600_receive_packet(struct encx24j600_priv *priv,
				     struct rsv *rsv)
{
	struct net_device *dev = priv->ndev;
	struct sk_buff *skb = netdev_alloc_skb(dev, rsv->len + NET_IP_ALIGN);
	if (!skb) {
		pr_err_ratelimited("RX: OOM: packet dropped\n");
		dev->stats.rx_dropped++;
		return -ENOMEM;
	}
	skb_reserve(skb, NET_IP_ALIGN);
	encx24j600_raw_read(priv, RRXDATA, skb_put(skb, rsv->len), rsv->len);

	if (netif_msg_pktdata(priv))
		dump_packet("RX", skb->len, skb->data);

	skb->dev = dev;
	skb->protocol = eth_type_trans(skb, dev);
	skb->ip_summed = CHECKSUM_COMPLETE;

	/* Maintain stats */
	dev->stats.rx_packets++;
	dev->stats.rx_bytes += rsv->len;
	priv->next_packet = rsv->next_packet;

	netif_rx(skb);

	return 0;
}

static void encx24j600_rx_packets(struct encx24j600_priv *priv, u8 packet_count)
{
	struct net_device *dev = priv->ndev;

	while (packet_count--) {
		struct rsv rsv;
		u16 newrxtail;

		encx24j600_write_reg(priv, ERXRDPT, priv->next_packet);
		encx24j600_raw_read(priv, RRXDATA, (u8 *)&rsv, sizeof(rsv));

		if (netif_msg_rx_status(priv))
			encx24j600_dump_rsv(priv, __func__, &rsv);

		if (!RSV_GETBIT(rsv.rxstat, RSV_RXOK) ||
		    (rsv.len > MAX_FRAMELEN)) {
			netif_err(priv, rx_err, dev, "RX Error %04x\n",
				  rsv.rxstat);
			dev->stats.rx_errors++;

			if (RSV_GETBIT(rsv.rxstat, RSV_CRCERROR))
				dev->stats.rx_crc_errors++;
			if (RSV_GETBIT(rsv.rxstat, RSV_LENCHECKERR))
				dev->stats.rx_frame_errors++;
			if (rsv.len > MAX_FRAMELEN)
				dev->stats.rx_over_errors++;
		} else {
			encx24j600_receive_packet(priv, &rsv);
		}

		newrxtail = priv->next_packet - 2;
		if (newrxtail == ENC_RX_BUF_START)
			newrxtail = SRAM_SIZE - 2;

		encx24j600_cmd(priv, SETPKTDEC);
		encx24j600_write_reg(priv, ERXTAIL, newrxtail);
	}
}

static irqreturn_t encx24j600_isr(int irq, void *dev_id)
{
	struct encx24j600_priv *priv = dev_id;
	struct net_device *dev = priv->ndev;
	int eir;

	/* Clear interrupts */
	encx24j600_cmd(priv, CLREIE);

	eir = encx24j600_read_reg(priv, EIR);

	if (eir & LINKIF)
		encx24j600_int_link_handler(priv);

	if (eir & TXIF)
		encx24j600_tx_complete(priv, false);

	if (eir & TXABTIF)
		encx24j600_tx_complete(priv, true);

	if (eir & RXABTIF) {
		if (eir & PCFULIF) {
			/* Packet counter is full */
			netif_err(priv, rx_err, dev, "Packet counter full\n");
		}
		dev->stats.rx_dropped++;
		encx24j600_clr_bits(priv, EIR, RXABTIF);
	}

	if (eir & PKTIF) {
		u8 packet_count;

		mutex_lock(&priv->lock);

		packet_count = encx24j600_read_reg(priv, ESTAT) & 0xff;
		while (packet_count) {
			encx24j600_rx_packets(priv, packet_count);
			packet_count = encx24j600_read_reg(priv, ESTAT) & 0xff;
		}

		mutex_unlock(&priv->lock);
	}

	/* Enable interrupts */
	encx24j600_cmd(priv, SETEIE);

	return IRQ_HANDLED;
}

static int encx24j600_soft_reset(struct encx24j600_priv *priv)
{
	int ret = 0;
	int timeout;
	u16 eudast;

	/* Write and verify a test value to EUDAST */
	regcache_cache_bypass(priv->ctx.regmap, true);
	timeout = 10;
	do {
		encx24j600_write_reg(priv, EUDAST, EUDAST_TEST_VAL);
		eudast = encx24j600_read_reg(priv, EUDAST);
		usleep_range(25, 100);
	} while ((eudast != EUDAST_TEST_VAL) && --timeout);
	regcache_cache_bypass(priv->ctx.regmap, false);

	if (timeout == 0) {
		ret = -ETIMEDOUT;
		goto err_out;
	}

	/* Wait for CLKRDY to become set */
	timeout = 10;
	while (!(encx24j600_read_reg(priv, ESTAT) & CLKRDY) && --timeout)
		usleep_range(25, 100);

	if (timeout == 0) {
		ret = -ETIMEDOUT;
		goto err_out;
	}

	/* Issue a System Reset command */
	encx24j600_cmd(priv, SETETHRST);
	usleep_range(25, 100);

	/* Confirm that EUDAST has 0000h after system reset */
	if (encx24j600_read_reg(priv, EUDAST) != 0) {
		ret = -EINVAL;
		goto err_out;
	}

	/* Wait for PHY register and status bits to become available */
	usleep_range(256, 1000);

err_out:
	return ret;
}

static int encx24j600_hw_reset(struct encx24j600_priv *priv)
{
	int ret;

	mutex_lock(&priv->lock);
	ret = encx24j600_soft_reset(priv);
	mutex_unlock(&priv->lock);

	return ret;
}

static void encx24j600_reset_hw_tx(struct encx24j600_priv *priv)
{
	encx24j600_set_bits(priv, ECON2, TXRST);
	encx24j600_clr_bits(priv, ECON2, TXRST);
}

static void encx24j600_hw_init_tx(struct encx24j600_priv *priv)
{
	/* Reset TX */
	encx24j600_reset_hw_tx(priv);

	/* Clear the TXIF flag if were previously set */
	encx24j600_clr_bits(priv, EIR, TXIF | TXABTIF);

	/* Write the Tx Buffer pointer */
	encx24j600_write_reg(priv, EGPWRPT, ENC_TX_BUF_START);
}

static void encx24j600_hw_init_rx(struct encx24j600_priv *priv)
{
	encx24j600_cmd(priv, DISABLERX);

	/* Set up RX packet start address in the SRAM */
	encx24j600_write_reg(priv, ERXST, ENC_RX_BUF_START);

	/* Preload the RX Data pointer to the beginning of the RX area */
	encx24j600_write_reg(priv, ERXRDPT, ENC_RX_BUF_START);

	priv->next_packet = ENC_RX_BUF_START;

	/* Set up RX end address in the SRAM */
	encx24j600_write_reg(priv, ERXTAIL, ENC_SRAM_SIZE - 2);

	/* Reset the  user data pointers    */
	encx24j600_write_reg(priv, EUDAST, ENC_SRAM_SIZE);
	encx24j600_write_reg(priv, EUDAND, ENC_SRAM_SIZE + 1);

	/* Set Max Frame length */
	encx24j600_write_reg(priv, MAMXFL, MAX_FRAMELEN);
}

static void encx24j600_dump_config(struct encx24j600_priv *priv,
				   const char *msg)
{
	pr_info(DRV_NAME ": %s\n", msg);

	/* CHIP configuration */
	pr_info(DRV_NAME " ECON1:   %04X\n", encx24j600_read_reg(priv, ECON1));
	pr_info(DRV_NAME " ECON2:   %04X\n", encx24j600_read_reg(priv, ECON2));
	pr_info(DRV_NAME " ERXFCON: %04X\n", encx24j600_read_reg(priv,
								 ERXFCON));
	pr_info(DRV_NAME " ESTAT:   %04X\n", encx24j600_read_reg(priv, ESTAT));
	pr_info(DRV_NAME " EIR:     %04X\n", encx24j600_read_reg(priv, EIR));
	pr_info(DRV_NAME " EIDLED:  %04X\n", encx24j600_read_reg(priv, EIDLED));

	/* MAC layer configuration */
	pr_info(DRV_NAME " MACON1:  %04X\n", encx24j600_read_reg(priv, MACON1));
	pr_info(DRV_NAME " MACON2:  %04X\n", encx24j600_read_reg(priv, MACON2));
	pr_info(DRV_NAME " MAIPG:   %04X\n", encx24j600_read_reg(priv, MAIPG));
	pr_info(DRV_NAME " MACLCON: %04X\n", encx24j600_read_reg(priv,
								 MACLCON));
	pr_info(DRV_NAME " MABBIPG: %04X\n", encx24j600_read_reg(priv,
								 MABBIPG));

	/* PHY configuation */
	pr_info(DRV_NAME " PHCON1:  %04X\n", encx24j600_read_phy(priv, PHCON1));
	pr_info(DRV_NAME " PHCON2:  %04X\n", encx24j600_read_phy(priv, PHCON2));
	pr_info(DRV_NAME " PHANA:   %04X\n", encx24j600_read_phy(priv, PHANA));
	pr_info(DRV_NAME " PHANLPA: %04X\n", encx24j600_read_phy(priv,
								 PHANLPA));
	pr_info(DRV_NAME " PHANE:   %04X\n", encx24j600_read_phy(priv, PHANE));
	pr_info(DRV_NAME " PHSTAT1: %04X\n", encx24j600_read_phy(priv,
								 PHSTAT1));
	pr_info(DRV_NAME " PHSTAT2: %04X\n", encx24j600_read_phy(priv,
								 PHSTAT2));
	pr_info(DRV_NAME " PHSTAT3: %04X\n", encx24j600_read_phy(priv,
								 PHSTAT3));
}

static void encx24j600_set_rxfilter_mode(struct encx24j600_priv *priv)
{
	switch (priv->rxfilter) {
	case RXFILTER_PROMISC:
		encx24j600_set_bits(priv, MACON1, PASSALL);
		encx24j600_write_reg(priv, ERXFCON, UCEN | MCEN | NOTMEEN);
		break;
	case RXFILTER_MULTI:
		encx24j600_clr_bits(priv, MACON1, PASSALL);
		encx24j600_write_reg(priv, ERXFCON, UCEN | CRCEN | BCEN | MCEN);
		break;
	case RXFILTER_NORMAL:
	default:
		encx24j600_clr_bits(priv, MACON1, PASSALL);
		encx24j600_write_reg(priv, ERXFCON, UCEN | CRCEN | BCEN);
		break;
	}
}

static int encx24j600_hw_init(struct encx24j600_priv *priv)
{
	int ret = 0;
	u16 macon2;

	priv->hw_enabled = false;

	/* PHY Leds: link status,
	 * LEDA: Link State + collision events
	 * LEDB: Link State + transmit/receive events
	 */
	encx24j600_update_reg(priv, EIDLED, 0xff00, 0xcb00);

	/* Loopback disabled */
	encx24j600_write_reg(priv, MACON1, 0x9);

	/* interpacket gap value */
	encx24j600_write_reg(priv, MAIPG, 0x0c12);

	/* Write the auto negotiation pattern */
	encx24j600_write_phy(priv, PHANA, PHANA_DEFAULT);

	encx24j600_update_phcon1(priv);
	encx24j600_check_link_status(priv);

	macon2 = MACON2_RSV1 | TXCRCEN | PADCFG0 | PADCFG2 | MACON2_DEFER;
	if ((priv->autoneg == AUTONEG_DISABLE) && priv->full_duplex)
		macon2 |= FULDPX;

	encx24j600_set_bits(priv, MACON2, macon2);

	priv->rxfilter = RXFILTER_NORMAL;
	encx24j600_set_rxfilter_mode(priv);

	/* Program the Maximum frame length */
	encx24j600_write_reg(priv, MAMXFL, MAX_FRAMELEN);

	/* Init Tx pointers */
	encx24j600_hw_init_tx(priv);

	/* Init Rx pointers */
	encx24j600_hw_init_rx(priv);

	if (netif_msg_hw(priv))
		encx24j600_dump_config(priv, "Hw is initialized");

	return ret;
}

static void encx24j600_hw_enable(struct encx24j600_priv *priv)
{
	/* Clear the interrupt flags in case was set */
	encx24j600_clr_bits(priv, EIR, (PCFULIF | RXABTIF | TXABTIF | TXIF |
					PKTIF | LINKIF));

	/* Enable the interrupts */
	encx24j600_write_reg(priv, EIE, (PCFULIE | RXABTIE | TXABTIE | TXIE |
					 PKTIE | LINKIE | INTIE));

	/* Enable RX */
	encx24j600_cmd(priv, ENABLERX);

	priv->hw_enabled = true;
}

static void encx24j600_hw_disable(struct encx24j600_priv *priv)
{
	/* Disable all interrupts */
	encx24j600_write_reg(priv, EIE, 0);

	/* Disable RX */
	encx24j600_cmd(priv, DISABLERX);

	priv->hw_enabled = false;
}

static int encx24j600_setlink(struct net_device *dev, u8 autoneg, u16 speed,
			      u8 duplex)
{
	struct encx24j600_priv *priv = netdev_priv(dev);
	int ret = 0;

	if (!priv->hw_enabled) {
		/* link is in low power mode now; duplex setting
		 * will take effect on next encx24j600_hw_init()
		 */
		if (speed == SPEED_10 || speed == SPEED_100) {
			priv->autoneg = (autoneg == AUTONEG_ENABLE);
			priv->full_duplex = (duplex == DUPLEX_FULL);
			priv->speed = (speed == SPEED_100);
		} else {
			netif_warn(priv, link, dev, "unsupported link speed setting\n");
			/*speeds other than SPEED_10 and SPEED_100 */
			/*are not supported by chip */
			ret = -EOPNOTSUPP;
		}
	} else {
		netif_warn(priv, link, dev, "Warning: hw must be disabled to set link mode\n");
		ret = -EBUSY;
	}
	return ret;
}

static void encx24j600_hw_get_macaddr(struct encx24j600_priv *priv,
				      unsigned char *ethaddr)
{
	unsigned short val;

	val = encx24j600_read_reg(priv, MAADR1);

	ethaddr[0] = val & 0x00ff;
	ethaddr[1] = (val & 0xff00) >> 8;

	val = encx24j600_read_reg(priv, MAADR2);

	ethaddr[2] = val & 0x00ffU;
	ethaddr[3] = (val & 0xff00U) >> 8;

	val = encx24j600_read_reg(priv, MAADR3);

	ethaddr[4] = val & 0x00ffU;
	ethaddr[5] = (val & 0xff00U) >> 8;
}

/* Program the hardware MAC address from dev->dev_addr.*/
static int encx24j600_set_hw_macaddr(struct net_device *dev)
{
	struct encx24j600_priv *priv = netdev_priv(dev);

	if (priv->hw_enabled) {
		netif_info(priv, drv, dev, "Hardware must be disabled to set Mac address\n");
		return -EBUSY;
	}

	mutex_lock(&priv->lock);

	netif_info(priv, drv, dev, "%s: Setting MAC address to %pM\n",
		   dev->name, dev->dev_addr);

	encx24j600_write_reg(priv, MAADR3, (dev->dev_addr[4] |
			     dev->dev_addr[5] << 8));
	encx24j600_write_reg(priv, MAADR2, (dev->dev_addr[2] |
			     dev->dev_addr[3] << 8));
	encx24j600_write_reg(priv, MAADR1, (dev->dev_addr[0] |
			     dev->dev_addr[1] << 8));

	mutex_unlock(&priv->lock);

	return 0;
}

/* Store the new hardware address in dev->dev_addr, and update the MAC.*/
static int encx24j600_set_mac_address(struct net_device *dev, void *addr)
{
	struct sockaddr *address = addr;

	if (netif_running(dev))
		return -EBUSY;
	if (!is_valid_ether_addr(address->sa_data))
		return -EADDRNOTAVAIL;

	memcpy(dev->dev_addr, address->sa_data, dev->addr_len);
	return encx24j600_set_hw_macaddr(dev);
}

static int encx24j600_open(struct net_device *dev)
{
	struct encx24j600_priv *priv = netdev_priv(dev);

	int ret = request_threaded_irq(priv->ctx.spi->irq, NULL, encx24j600_isr,
				       IRQF_TRIGGER_FALLING | IRQF_ONESHOT,
				       DRV_NAME, priv);
	if (unlikely(ret < 0)) {
		netdev_err(dev, "request irq %d failed (ret = %d)\n",
			   priv->ctx.spi->irq, ret);
		return ret;
	}

	encx24j600_hw_disable(priv);
	encx24j600_hw_init(priv);
	encx24j600_hw_enable(priv);
	netif_start_queue(dev);

	return 0;
}

static int encx24j600_stop(struct net_device *dev)
{
	struct encx24j600_priv *priv = netdev_priv(dev);

	netif_stop_queue(dev);
	free_irq(priv->ctx.spi->irq, priv);
	return 0;
}

static void encx24j600_setrx_proc(struct kthread_work *ws)
{
	struct encx24j600_priv *priv =
			container_of(ws, struct encx24j600_priv, setrx_work);

	mutex_lock(&priv->lock);
	encx24j600_set_rxfilter_mode(priv);
	mutex_unlock(&priv->lock);
}

static void encx24j600_set_multicast_list(struct net_device *dev)
{
	struct encx24j600_priv *priv = netdev_priv(dev);
	int oldfilter = priv->rxfilter;

	if (dev->flags & IFF_PROMISC) {
		netif_dbg(priv, link, dev, "promiscuous mode\n");
		priv->rxfilter = RXFILTER_PROMISC;
	} else if ((dev->flags & IFF_ALLMULTI) || !netdev_mc_empty(dev)) {
		netif_dbg(priv, link, dev, "%smulticast mode\n",
			  (dev->flags & IFF_ALLMULTI) ? "all-" : "");
		priv->rxfilter = RXFILTER_MULTI;
	} else {
		netif_dbg(priv, link, dev, "normal mode\n");
		priv->rxfilter = RXFILTER_NORMAL;
	}

	if (oldfilter != priv->rxfilter)
		kthread_queue_work(&priv->kworker, &priv->setrx_work);
}

static void encx24j600_hw_tx(struct encx24j600_priv *priv)
{
	struct net_device *dev = priv->ndev;
	netif_info(priv, tx_queued, dev, "TX Packet Len:%d\n",
		   priv->tx_skb->len);

	if (netif_msg_pktdata(priv))
		dump_packet("TX", priv->tx_skb->len, priv->tx_skb->data);

	if (encx24j600_read_reg(priv, EIR) & TXABTIF)
		/* Last transmition aborted due to error. Reset TX interface */
		encx24j600_reset_hw_tx(priv);

	/* Clear the TXIF flag if were previously set */
	encx24j600_clr_bits(priv, EIR, TXIF);

	/* Set the data pointer to the TX buffer address in the SRAM */
	encx24j600_write_reg(priv, EGPWRPT, ENC_TX_BUF_START);

	/* Copy the packet into the SRAM */
	encx24j600_raw_write(priv, WGPDATA, (u8 *)priv->tx_skb->data,
			     priv->tx_skb->len);

	/* Program the Tx buffer start pointer */
	encx24j600_write_reg(priv, ETXST, ENC_TX_BUF_START);

	/* Program the packet length */
	encx24j600_write_reg(priv, ETXLEN, priv->tx_skb->len);

	/* Start the transmission */
	encx24j600_cmd(priv, SETTXRTS);
}

static void encx24j600_tx_proc(struct kthread_work *ws)
{
	struct encx24j600_priv *priv =
			container_of(ws, struct encx24j600_priv, tx_work);

	mutex_lock(&priv->lock);
	encx24j600_hw_tx(priv);
	mutex_unlock(&priv->lock);
}

static netdev_tx_t encx24j600_tx(struct sk_buff *skb, struct net_device *dev)
{
	struct encx24j600_priv *priv = netdev_priv(dev);

	netif_stop_queue(dev);

	/* save the timestamp */
	netif_trans_update(dev);

	/* Remember the skb for deferred processing */
	priv->tx_skb = skb;

	kthread_queue_work(&priv->kworker, &priv->tx_work);

	return NETDEV_TX_OK;
}

/* Deal with a transmit timeout */
static void encx24j600_tx_timeout(struct net_device *dev)
{
	struct encx24j600_priv *priv = netdev_priv(dev);

	netif_err(priv, tx_err, dev, "TX timeout at %ld, latency %ld\n",
		  jiffies, jiffies - dev_trans_start(dev));

	dev->stats.tx_errors++;
	netif_wake_queue(dev);
	return;
}

static int encx24j600_get_regs_len(struct net_device *dev)
{
	return SFR_REG_COUNT;
}

static void encx24j600_get_regs(struct net_device *dev,
				struct ethtool_regs *regs, void *p)
{
	struct encx24j600_priv *priv = netdev_priv(dev);
	u16 *buff = p;
	u8 reg;

	regs->version = 1;
	mutex_lock(&priv->lock);
	for (reg = 0; reg < SFR_REG_COUNT; reg += 2) {
		unsigned int val = 0;
		/* ignore errors for unreadable registers */
		regmap_read(priv->ctx.regmap, reg, &val);
		buff[reg] = val & 0xffff;
	}
	mutex_unlock(&priv->lock);
}

static void encx24j600_get_drvinfo(struct net_device *dev,
				   struct ethtool_drvinfo *info)
{
	strlcpy(info->driver, DRV_NAME, sizeof(info->driver));
	strlcpy(info->version, DRV_VERSION, sizeof(info->version));
	strlcpy(info->bus_info, dev_name(dev->dev.parent),
		sizeof(info->bus_info));
}

static int encx24j600_get_settings(struct net_device *dev,
				   struct ethtool_cmd *cmd)
{
	struct encx24j600_priv *priv = netdev_priv(dev);

	cmd->transceiver = XCVR_INTERNAL;
	cmd->supported = SUPPORTED_10baseT_Half | SUPPORTED_10baseT_Full |
			 SUPPORTED_100baseT_Half | SUPPORTED_100baseT_Full |
			 SUPPORTED_Autoneg | SUPPORTED_TP;

	ethtool_cmd_speed_set(cmd, priv->speed);
	cmd->duplex = priv->full_duplex ? DUPLEX_FULL : DUPLEX_HALF;
	cmd->port = PORT_TP;
	cmd->autoneg = priv->autoneg ? AUTONEG_ENABLE : AUTONEG_DISABLE;

	return 0;
}

static int encx24j600_set_settings(struct net_device *dev,
				   struct ethtool_cmd *cmd)
{
	return encx24j600_setlink(dev, cmd->autoneg,
				  ethtool_cmd_speed(cmd), cmd->duplex);
}

static u32 encx24j600_get_msglevel(struct net_device *dev)
{
	struct encx24j600_priv *priv = netdev_priv(dev);
	return priv->msg_enable;
}

static void encx24j600_set_msglevel(struct net_device *dev, u32 val)
{
	struct encx24j600_priv *priv = netdev_priv(dev);
	priv->msg_enable = val;
}

static const struct ethtool_ops encx24j600_ethtool_ops = {
	.get_settings = encx24j600_get_settings,
	.set_settings = encx24j600_set_settings,
	.get_drvinfo = encx24j600_get_drvinfo,
	.get_msglevel = encx24j600_get_msglevel,
	.set_msglevel = encx24j600_set_msglevel,
	.get_regs_len = encx24j600_get_regs_len,
	.get_regs = encx24j600_get_regs,
};

static const struct net_device_ops encx24j600_netdev_ops = {
	.ndo_open = encx24j600_open,
	.ndo_stop = encx24j600_stop,
	.ndo_start_xmit = encx24j600_tx,
	.ndo_set_rx_mode = encx24j600_set_multicast_list,
	.ndo_set_mac_address = encx24j600_set_mac_address,
	.ndo_tx_timeout = encx24j600_tx_timeout,
	.ndo_validate_addr = eth_validate_addr,
};

static int encx24j600_spi_probe(struct spi_device *spi)
{
	int ret;

	struct net_device *ndev;
	struct encx24j600_priv *priv;
	u16 eidled;

	ndev = alloc_etherdev(sizeof(struct encx24j600_priv));

	if (!ndev) {
		ret = -ENOMEM;
		goto error_out;
	}

	priv = netdev_priv(ndev);
	spi_set_drvdata(spi, priv);
	dev_set_drvdata(&spi->dev, priv);
	SET_NETDEV_DEV(ndev, &spi->dev);

	priv->msg_enable = netif_msg_init(debug, DEFAULT_MSG_ENABLE);
	priv->ndev = ndev;

	/* Default configuration PHY configuration */
	priv->full_duplex = true;
	priv->autoneg = AUTONEG_ENABLE;
	priv->speed = SPEED_100;

	priv->ctx.spi = spi;
	devm_regmap_init_encx24j600(&spi->dev, &priv->ctx);
	ndev->irq = spi->irq;
	ndev->netdev_ops = &encx24j600_netdev_ops;

	mutex_init(&priv->lock);

	/* Reset device and check if it is connected */
	if (encx24j600_hw_reset(priv)) {
		netif_err(priv, probe, ndev,
			  DRV_NAME ": Chip is not detected\n");
		ret = -EIO;
		goto out_free;
	}

	/* Initialize the device HW to the consistent state */
	if (encx24j600_hw_init(priv)) {
		netif_err(priv, probe, ndev,
			  DRV_NAME ": HW initialization error\n");
		ret = -EIO;
		goto out_free;
	}

	kthread_init_worker(&priv->kworker);
	kthread_init_work(&priv->tx_work, encx24j600_tx_proc);
	kthread_init_work(&priv->setrx_work, encx24j600_setrx_proc);

	priv->kworker_task = kthread_run(kthread_worker_fn, &priv->kworker,
					 "encx24j600");

	if (IS_ERR(priv->kworker_task)) {
		ret = PTR_ERR(priv->kworker_task);
		goto out_free;
	}

	/* Get the MAC address from the chip */
	encx24j600_hw_get_macaddr(priv, ndev->dev_addr);

	ndev->ethtool_ops = &encx24j600_ethtool_ops;

	ret = register_netdev(ndev);
	if (unlikely(ret)) {
		netif_err(priv, probe, ndev, "Error %d initializing card encx24j600 card\n",
			  ret);
		goto out_free;
	}

	eidled = encx24j600_read_reg(priv, EIDLED);
	if (((eidled & DEVID_MASK) >> DEVID_SHIFT) != ENCX24J600_DEV_ID) {
		ret = -EINVAL;
		goto out_unregister;
	}

	netif_info(priv, probe, ndev, "Silicon rev ID: 0x%02x\n",
		   (eidled & REVID_MASK) >> REVID_SHIFT);

	netif_info(priv, drv, priv->ndev, "MAC address %pM\n", ndev->dev_addr);

	return ret;

out_unregister:
	unregister_netdev(priv->ndev);
out_free:
	free_netdev(ndev);

error_out:
	return ret;
}

static int encx24j600_spi_remove(struct spi_device *spi)
{
	struct encx24j600_priv *priv = dev_get_drvdata(&spi->dev);

	unregister_netdev(priv->ndev);

	free_netdev(priv->ndev);

	return 0;
}

static const struct spi_device_id encx24j600_spi_id_table[] = {
	{ .name = "encx24j600" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(spi, encx24j600_spi_id_table);

static struct spi_driver encx24j600_spi_net_driver = {
	.driver = {
		.name	= DRV_NAME,
		.owner	= THIS_MODULE,
		.bus	= &spi_bus_type,
	},
	.probe		= encx24j600_spi_probe,
	.remove		= encx24j600_spi_remove,
	.id_table	= encx24j600_spi_id_table,
};

static int __init encx24j600_init(void)
{
	return spi_register_driver(&encx24j600_spi_net_driver);
}
module_init(encx24j600_init);

static void encx24j600_exit(void)
{
	spi_unregister_driver(&encx24j600_spi_net_driver);
}
module_exit(encx24j600_exit);

MODULE_DESCRIPTION(DRV_NAME " ethernet driver");
MODULE_AUTHOR("Jon Ringle <jringle@gridpoint.com>");
MODULE_LICENSE("GPL");
MODULE_ALIAS("spi:" DRV_NAME);
