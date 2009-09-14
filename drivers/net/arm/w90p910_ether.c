/*
 * Copyright (c) 2008-2009 Nuvoton technology corporation.
 *
 * Wan ZongShun <mcuos.com@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation;version 2 of the License.
 *
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/mii.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include <linux/ethtool.h>
#include <linux/platform_device.h>
#include <linux/clk.h>

#define DRV_MODULE_NAME		"w90p910-emc"
#define DRV_MODULE_VERSION	"0.1"

/* Ethernet MAC Registers */
#define REG_CAMCMR		0x00
#define REG_CAMEN		0x04
#define REG_CAMM_BASE		0x08
#define REG_CAML_BASE		0x0c
#define REG_TXDLSA		0x88
#define REG_RXDLSA		0x8C
#define REG_MCMDR		0x90
#define REG_MIID		0x94
#define REG_MIIDA		0x98
#define REG_FFTCR		0x9C
#define REG_TSDR		0xa0
#define REG_RSDR		0xa4
#define REG_DMARFC		0xa8
#define REG_MIEN		0xac
#define REG_MISTA		0xb0
#define REG_CTXDSA		0xcc
#define REG_CTXBSA		0xd0
#define REG_CRXDSA		0xd4
#define REG_CRXBSA		0xd8

/* mac controller bit */
#define MCMDR_RXON		0x01
#define MCMDR_ACP		(0x01 << 3)
#define MCMDR_SPCRC		(0x01 << 5)
#define MCMDR_TXON		(0x01 << 8)
#define MCMDR_FDUP		(0x01 << 18)
#define MCMDR_ENMDC		(0x01 << 19)
#define MCMDR_OPMOD		(0x01 << 20)
#define SWR			(0x01 << 24)

/* cam command regiser */
#define CAMCMR_AUP		0x01
#define CAMCMR_AMP		(0x01 << 1)
#define CAMCMR_ABP		(0x01 << 2)
#define CAMCMR_CCAM		(0x01 << 3)
#define CAMCMR_ECMP		(0x01 << 4)
#define CAM0EN			0x01

/* mac mii controller bit */
#define MDCCR			(0x0a << 20)
#define PHYAD			(0x01 << 8)
#define PHYWR			(0x01 << 16)
#define PHYBUSY			(0x01 << 17)
#define PHYPRESP		(0x01 << 18)
#define CAM_ENTRY_SIZE		0x08

/* rx and tx status */
#define TXDS_TXCP		(0x01 << 19)
#define RXDS_CRCE		(0x01 << 17)
#define RXDS_PTLE		(0x01 << 19)
#define RXDS_RXGD		(0x01 << 20)
#define RXDS_ALIE		(0x01 << 21)
#define RXDS_RP			(0x01 << 22)

/* mac interrupt status*/
#define MISTA_EXDEF		(0x01 << 19)
#define MISTA_TXBERR		(0x01 << 24)
#define MISTA_TDU		(0x01 << 23)
#define MISTA_RDU		(0x01 << 10)
#define MISTA_RXBERR		(0x01 << 11)

#define ENSTART			0x01
#define ENRXINTR		0x01
#define ENRXGD			(0x01 << 4)
#define ENRXBERR		(0x01 << 11)
#define ENTXINTR		(0x01 << 16)
#define ENTXCP			(0x01 << 18)
#define ENTXABT			(0x01 << 21)
#define ENTXBERR		(0x01 << 24)
#define ENMDC			(0x01 << 19)
#define PHYBUSY			(0x01 << 17)
#define MDCCR_VAL		0xa00000

/* rx and tx owner bit */
#define RX_OWEN_DMA		(0x01 << 31)
#define RX_OWEN_CPU		(~(0x03 << 30))
#define TX_OWEN_DMA		(0x01 << 31)
#define TX_OWEN_CPU		(~(0x01 << 31))

/* tx frame desc controller bit */
#define MACTXINTEN		0x04
#define CRCMODE			0x02
#define PADDINGMODE		0x01

/* fftcr controller bit */
#define TXTHD 			(0x03 << 8)
#define BLENGTH			(0x01 << 20)

/* global setting for driver */
#define RX_DESC_SIZE		50
#define TX_DESC_SIZE		10
#define MAX_RBUFF_SZ		0x600
#define MAX_TBUFF_SZ		0x600
#define TX_TIMEOUT		50
#define DELAY			1000
#define CAM0			0x0

static int w90p910_mdio_read(struct net_device *dev, int phy_id, int reg);

struct w90p910_rxbd {
	unsigned int sl;
	unsigned int buffer;
	unsigned int reserved;
	unsigned int next;
};

struct w90p910_txbd {
	unsigned int mode;
	unsigned int buffer;
	unsigned int sl;
	unsigned int next;
};

struct recv_pdesc {
	struct w90p910_rxbd desclist[RX_DESC_SIZE];
	char recv_buf[RX_DESC_SIZE][MAX_RBUFF_SZ];
};

struct tran_pdesc {
	struct w90p910_txbd desclist[TX_DESC_SIZE];
	char tran_buf[RX_DESC_SIZE][MAX_TBUFF_SZ];
};

struct  w90p910_ether {
	struct recv_pdesc *rdesc;
	struct recv_pdesc *rdesc_phys;
	struct tran_pdesc *tdesc;
	struct tran_pdesc *tdesc_phys;
	struct net_device_stats stats;
	struct platform_device *pdev;
	struct sk_buff *skb;
	struct clk *clk;
	struct clk *rmiiclk;
	struct mii_if_info mii;
	struct timer_list check_timer;
	void __iomem *reg;
	unsigned int rxirq;
	unsigned int txirq;
	unsigned int cur_tx;
	unsigned int cur_rx;
	unsigned int finish_tx;
	unsigned int rx_packets;
	unsigned int rx_bytes;
	unsigned int start_tx_ptr;
	unsigned int start_rx_ptr;
	unsigned int linkflag;
	spinlock_t lock;
};

static void update_linkspeed_register(struct net_device *dev,
				unsigned int speed, unsigned int duplex)
{
	struct w90p910_ether *ether = netdev_priv(dev);
	unsigned int val;

	val = __raw_readl(ether->reg + REG_MCMDR);

	if (speed == SPEED_100) {
		/* 100 full/half duplex */
		if (duplex == DUPLEX_FULL) {
			val |= (MCMDR_OPMOD | MCMDR_FDUP);
		} else {
			val |= MCMDR_OPMOD;
			val &= ~MCMDR_FDUP;
		}
	} else {
		/* 10 full/half duplex */
		if (duplex == DUPLEX_FULL) {
			val |= MCMDR_FDUP;
			val &= ~MCMDR_OPMOD;
		} else {
			val &= ~(MCMDR_FDUP | MCMDR_OPMOD);
		}
	}

	__raw_writel(val, ether->reg + REG_MCMDR);
}

static void update_linkspeed(struct net_device *dev)
{
	struct w90p910_ether *ether = netdev_priv(dev);
	struct platform_device *pdev;
	unsigned int bmsr, bmcr, lpa, speed, duplex;

	pdev = ether->pdev;

	if (!mii_link_ok(&ether->mii)) {
		ether->linkflag = 0x0;
		netif_carrier_off(dev);
		dev_warn(&pdev->dev, "%s: Link down.\n", dev->name);
		return;
	}

	if (ether->linkflag == 1)
		return;

	bmsr = w90p910_mdio_read(dev, ether->mii.phy_id, MII_BMSR);
	bmcr = w90p910_mdio_read(dev, ether->mii.phy_id, MII_BMCR);

	if (bmcr & BMCR_ANENABLE) {
		if (!(bmsr & BMSR_ANEGCOMPLETE))
			return;

		lpa = w90p910_mdio_read(dev, ether->mii.phy_id, MII_LPA);

		if ((lpa & LPA_100FULL) || (lpa & LPA_100HALF))
			speed = SPEED_100;
		else
			speed = SPEED_10;

		if ((lpa & LPA_100FULL) || (lpa & LPA_10FULL))
			duplex = DUPLEX_FULL;
		else
			duplex = DUPLEX_HALF;

	} else {
		speed = (bmcr & BMCR_SPEED100) ? SPEED_100 : SPEED_10;
		duplex = (bmcr & BMCR_FULLDPLX) ? DUPLEX_FULL : DUPLEX_HALF;
	}

	update_linkspeed_register(dev, speed, duplex);

	dev_info(&pdev->dev, "%s: Link now %i-%s\n", dev->name, speed,
			(duplex == DUPLEX_FULL) ? "FullDuplex" : "HalfDuplex");
	ether->linkflag = 0x01;

	netif_carrier_on(dev);
}

static void w90p910_check_link(unsigned long dev_id)
{
	struct net_device *dev = (struct net_device *) dev_id;
	struct w90p910_ether *ether = netdev_priv(dev);

	update_linkspeed(dev);
	mod_timer(&ether->check_timer, jiffies + msecs_to_jiffies(1000));
}

static void w90p910_write_cam(struct net_device *dev,
				unsigned int x, unsigned char *pval)
{
	struct w90p910_ether *ether = netdev_priv(dev);
	unsigned int msw, lsw;

	msw = (pval[0] << 24) | (pval[1] << 16) | (pval[2] << 8) | pval[3];

	lsw = (pval[4] << 24) | (pval[5] << 16);

	__raw_writel(lsw, ether->reg + REG_CAML_BASE + x * CAM_ENTRY_SIZE);
	__raw_writel(msw, ether->reg + REG_CAMM_BASE + x * CAM_ENTRY_SIZE);
}

static void w90p910_init_desc(struct net_device *dev)
{
	struct w90p910_ether *ether;
	struct w90p910_txbd  *tdesc, *tdesc_phys;
	struct w90p910_rxbd  *rdesc, *rdesc_phys;
	unsigned int i, j;

	ether = netdev_priv(dev);

	ether->tdesc = (struct tran_pdesc *)
			dma_alloc_coherent(NULL, sizeof(struct tran_pdesc),
				(dma_addr_t *) &ether->tdesc_phys, GFP_KERNEL);

	ether->rdesc = (struct recv_pdesc *)
			dma_alloc_coherent(NULL, sizeof(struct recv_pdesc),
				(dma_addr_t *) &ether->rdesc_phys, GFP_KERNEL);

	for (i = 0; i < TX_DESC_SIZE; i++) {
		tdesc = &(ether->tdesc->desclist[i]);

		j = ((i + 1) / TX_DESC_SIZE);

		if (j != 0) {
			tdesc_phys = &(ether->tdesc_phys->desclist[0]);
			ether->start_tx_ptr = (unsigned int)tdesc_phys;
			tdesc->next = (unsigned int)ether->start_tx_ptr;
		} else {
			tdesc_phys = &(ether->tdesc_phys->desclist[i+1]);
			tdesc->next = (unsigned int)tdesc_phys;
		}

		tdesc->buffer = (unsigned int)ether->tdesc_phys->tran_buf[i];
		tdesc->sl = 0;
		tdesc->mode = 0;
	}

	for (i = 0; i < RX_DESC_SIZE; i++) {
		rdesc = &(ether->rdesc->desclist[i]);

		j = ((i + 1) / RX_DESC_SIZE);

		if (j != 0) {
			rdesc_phys = &(ether->rdesc_phys->desclist[0]);
			ether->start_rx_ptr = (unsigned int)rdesc_phys;
			rdesc->next = (unsigned int)ether->start_rx_ptr;
		} else {
			rdesc_phys = &(ether->rdesc_phys->desclist[i+1]);
			rdesc->next = (unsigned int)rdesc_phys;
		}

		rdesc->sl = RX_OWEN_DMA;
		rdesc->buffer = (unsigned int)ether->rdesc_phys->recv_buf[i];
	  }
}

static void w90p910_set_fifo_threshold(struct net_device *dev)
{
	struct w90p910_ether *ether = netdev_priv(dev);
	unsigned int val;

	val = TXTHD | BLENGTH;
	__raw_writel(val, ether->reg + REG_FFTCR);
}

static void w90p910_return_default_idle(struct net_device *dev)
{
	struct w90p910_ether *ether = netdev_priv(dev);
	unsigned int val;

	val = __raw_readl(ether->reg + REG_MCMDR);
	val |= SWR;
	__raw_writel(val, ether->reg + REG_MCMDR);
}

static void w90p910_trigger_rx(struct net_device *dev)
{
	struct w90p910_ether *ether = netdev_priv(dev);

	__raw_writel(ENSTART, ether->reg + REG_RSDR);
}

static void w90p910_trigger_tx(struct net_device *dev)
{
	struct w90p910_ether *ether = netdev_priv(dev);

	__raw_writel(ENSTART, ether->reg + REG_TSDR);
}

static void w90p910_enable_mac_interrupt(struct net_device *dev)
{
	struct w90p910_ether *ether = netdev_priv(dev);
	unsigned int val;

	val = ENTXINTR | ENRXINTR | ENRXGD | ENTXCP;
	val |= ENTXBERR | ENRXBERR | ENTXABT;

	__raw_writel(val, ether->reg + REG_MIEN);
}

static void w90p910_get_and_clear_int(struct net_device *dev,
							unsigned int *val)
{
	struct w90p910_ether *ether = netdev_priv(dev);

	*val = __raw_readl(ether->reg + REG_MISTA);
	__raw_writel(*val, ether->reg + REG_MISTA);
}

static void w90p910_set_global_maccmd(struct net_device *dev)
{
	struct w90p910_ether *ether = netdev_priv(dev);
	unsigned int val;

	val = __raw_readl(ether->reg + REG_MCMDR);
	val |= MCMDR_SPCRC | MCMDR_ENMDC | MCMDR_ACP | ENMDC;
	__raw_writel(val, ether->reg + REG_MCMDR);
}

static void w90p910_enable_cam(struct net_device *dev)
{
	struct w90p910_ether *ether = netdev_priv(dev);
	unsigned int val;

	w90p910_write_cam(dev, CAM0, dev->dev_addr);

	val = __raw_readl(ether->reg + REG_CAMEN);
	val |= CAM0EN;
	__raw_writel(val, ether->reg + REG_CAMEN);
}

static void w90p910_enable_cam_command(struct net_device *dev)
{
	struct w90p910_ether *ether = netdev_priv(dev);
	unsigned int val;

	val = CAMCMR_ECMP | CAMCMR_ABP | CAMCMR_AMP;
	__raw_writel(val, ether->reg + REG_CAMCMR);
}

static void w90p910_enable_tx(struct net_device *dev, unsigned int enable)
{
	struct w90p910_ether *ether = netdev_priv(dev);
	unsigned int val;

	val = __raw_readl(ether->reg + REG_MCMDR);

	if (enable)
		val |= MCMDR_TXON;
	else
		val &= ~MCMDR_TXON;

	__raw_writel(val, ether->reg + REG_MCMDR);
}

static void w90p910_enable_rx(struct net_device *dev, unsigned int enable)
{
	struct w90p910_ether *ether = netdev_priv(dev);
	unsigned int val;

	val = __raw_readl(ether->reg + REG_MCMDR);

	if (enable)
		val |= MCMDR_RXON;
	else
		val &= ~MCMDR_RXON;

	__raw_writel(val, ether->reg + REG_MCMDR);
}

static void w90p910_set_curdest(struct net_device *dev)
{
	struct w90p910_ether *ether = netdev_priv(dev);

	__raw_writel(ether->start_rx_ptr, ether->reg + REG_RXDLSA);
	__raw_writel(ether->start_tx_ptr, ether->reg + REG_TXDLSA);
}

static void w90p910_reset_mac(struct net_device *dev)
{
	struct w90p910_ether *ether = netdev_priv(dev);

	spin_lock(&ether->lock);

	w90p910_enable_tx(dev, 0);
	w90p910_enable_rx(dev, 0);
	w90p910_set_fifo_threshold(dev);
	w90p910_return_default_idle(dev);

	if (!netif_queue_stopped(dev))
		netif_stop_queue(dev);

	w90p910_init_desc(dev);

	dev->trans_start = jiffies;
	ether->cur_tx = 0x0;
	ether->finish_tx = 0x0;
	ether->cur_rx = 0x0;

	w90p910_set_curdest(dev);
	w90p910_enable_cam(dev);
	w90p910_enable_cam_command(dev);
	w90p910_enable_mac_interrupt(dev);
	w90p910_enable_tx(dev, 1);
	w90p910_enable_rx(dev, 1);
	w90p910_trigger_tx(dev);
	w90p910_trigger_rx(dev);

	dev->trans_start = jiffies;

	if (netif_queue_stopped(dev))
		netif_wake_queue(dev);

	spin_unlock(&ether->lock);
}

static void w90p910_mdio_write(struct net_device *dev,
					int phy_id, int reg, int data)
{
	struct w90p910_ether *ether = netdev_priv(dev);
	struct platform_device *pdev;
	unsigned int val, i;

	pdev = ether->pdev;

	__raw_writel(data, ether->reg + REG_MIID);

	val = (phy_id << 0x08) | reg;
	val |= PHYBUSY | PHYWR | MDCCR_VAL;
	__raw_writel(val, ether->reg + REG_MIIDA);

	for (i = 0; i < DELAY; i++) {
		if ((__raw_readl(ether->reg + REG_MIIDA) & PHYBUSY) == 0)
			break;
	}

	if (i == DELAY)
		dev_warn(&pdev->dev, "mdio write timed out\n");
}

static int w90p910_mdio_read(struct net_device *dev, int phy_id, int reg)
{
	struct w90p910_ether *ether = netdev_priv(dev);
	struct platform_device *pdev;
	unsigned int val, i, data;

	pdev = ether->pdev;

	val = (phy_id << 0x08) | reg;
	val |= PHYBUSY | MDCCR_VAL;
	__raw_writel(val, ether->reg + REG_MIIDA);

	for (i = 0; i < DELAY; i++) {
		if ((__raw_readl(ether->reg + REG_MIIDA) & PHYBUSY) == 0)
			break;
	}

	if (i == DELAY) {
		dev_warn(&pdev->dev, "mdio read timed out\n");
		data = 0xffff;
	} else {
		data = __raw_readl(ether->reg + REG_MIID);
	}

	return data;
}

static int set_mac_address(struct net_device *dev, void *addr)
{
	struct sockaddr *address = addr;

	if (!is_valid_ether_addr(address->sa_data))
		return -EADDRNOTAVAIL;

	memcpy(dev->dev_addr, address->sa_data, dev->addr_len);
	w90p910_write_cam(dev, CAM0, dev->dev_addr);

	return 0;
}

static int w90p910_ether_close(struct net_device *dev)
{
	struct w90p910_ether *ether = netdev_priv(dev);

	dma_free_writecombine(NULL, sizeof(struct w90p910_rxbd),
				ether->rdesc, (dma_addr_t)ether->rdesc_phys);
	dma_free_writecombine(NULL, sizeof(struct w90p910_txbd),
				ether->tdesc, (dma_addr_t)ether->tdesc_phys);

	netif_stop_queue(dev);

	del_timer_sync(&ether->check_timer);
	clk_disable(ether->rmiiclk);
	clk_disable(ether->clk);

	free_irq(ether->txirq, dev);
	free_irq(ether->rxirq, dev);

	return 0;
}

static struct net_device_stats *w90p910_ether_stats(struct net_device *dev)
{
	struct w90p910_ether *ether;

	ether = netdev_priv(dev);

	return &ether->stats;
}

static int w90p910_send_frame(struct net_device *dev,
					unsigned char *data, int length)
{
	struct w90p910_ether *ether;
	struct w90p910_txbd *txbd;
	struct platform_device *pdev;
	unsigned char *buffer;

	ether = netdev_priv(dev);
	pdev = ether->pdev;

	txbd = &ether->tdesc->desclist[ether->cur_tx];
	buffer = ether->tdesc->tran_buf[ether->cur_tx];
	if (length > 1514) {
		dev_err(&pdev->dev, "send data %d bytes, check it\n", length);
		length = 1514;
	}

	txbd->sl = length & 0xFFFF;

	memcpy(buffer, data, length);

	txbd->mode = TX_OWEN_DMA | PADDINGMODE | CRCMODE | MACTXINTEN;

	w90p910_enable_tx(dev, 1);

	w90p910_trigger_tx(dev);

	ether->cur_tx = (ether->cur_tx+1) % TX_DESC_SIZE;
	txbd = &ether->tdesc->desclist[ether->cur_tx];

	dev->trans_start = jiffies;

	if (txbd->mode & TX_OWEN_DMA)
		netif_stop_queue(dev);

	return 0;
}

static int w90p910_ether_start_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct w90p910_ether *ether = netdev_priv(dev);

	if (!(w90p910_send_frame(dev, skb->data, skb->len))) {
		ether->skb = skb;
		dev_kfree_skb_irq(skb);
		return 0;
	}
	return -1;
}

static irqreturn_t w90p910_tx_interrupt(int irq, void *dev_id)
{
	struct w90p910_ether *ether;
	struct w90p910_txbd  *txbd;
	struct platform_device *pdev;
	struct tran_pdesc *tran_pdesc;
	struct net_device *dev;
	unsigned int cur_entry, entry, status;

	dev = (struct net_device *)dev_id;
	ether = netdev_priv(dev);
	pdev = ether->pdev;

	spin_lock(&ether->lock);

	w90p910_get_and_clear_int(dev, &status);

	cur_entry = __raw_readl(ether->reg + REG_CTXDSA);

	tran_pdesc = ether->tdesc_phys;
	entry = (unsigned int)(&tran_pdesc->desclist[ether->finish_tx]);

	while (entry != cur_entry) {
		txbd = &ether->tdesc->desclist[ether->finish_tx];

		ether->finish_tx = (ether->finish_tx + 1) % TX_DESC_SIZE;

		if (txbd->sl & TXDS_TXCP) {
			ether->stats.tx_packets++;
			ether->stats.tx_bytes += txbd->sl & 0xFFFF;
		} else {
			ether->stats.tx_errors++;
		}

		txbd->sl = 0x0;
		txbd->mode = 0x0;

		if (netif_queue_stopped(dev))
			netif_wake_queue(dev);

		entry = (unsigned int)(&tran_pdesc->desclist[ether->finish_tx]);
	}

	if (status & MISTA_EXDEF) {
		dev_err(&pdev->dev, "emc defer exceed interrupt\n");
	} else if (status & MISTA_TXBERR) {
			dev_err(&pdev->dev, "emc bus error interrupt\n");
			w90p910_reset_mac(dev);
		} else if (status & MISTA_TDU) {
				if (netif_queue_stopped(dev))
					netif_wake_queue(dev);
			}

	spin_unlock(&ether->lock);

	return IRQ_HANDLED;
}

static void netdev_rx(struct net_device *dev)
{
	struct w90p910_ether *ether;
	struct w90p910_rxbd *rxbd;
	struct platform_device *pdev;
	struct recv_pdesc *rdesc_phys;
	struct sk_buff *skb;
	unsigned char *data;
	unsigned int length, status, val, entry;

	ether = netdev_priv(dev);
	pdev = ether->pdev;
	rdesc_phys = ether->rdesc_phys;

	rxbd = &ether->rdesc->desclist[ether->cur_rx];

	do {
		val = __raw_readl(ether->reg + REG_CRXDSA);
		entry = (unsigned int)&rdesc_phys->desclist[ether->cur_rx];

		if (val == entry)
			break;

		status = rxbd->sl;
		length = status & 0xFFFF;

		if (status & RXDS_RXGD) {
			data = ether->rdesc->recv_buf[ether->cur_rx];
			skb = dev_alloc_skb(length+2);
			if (!skb) {
				dev_err(&pdev->dev, "get skb buffer error\n");
				ether->stats.rx_dropped++;
				return;
			}

			skb->dev = dev;
			skb_reserve(skb, 2);
			skb_put(skb, length);
			skb_copy_to_linear_data(skb, data, length);
			skb->protocol = eth_type_trans(skb, dev);
			ether->stats.rx_packets++;
			ether->stats.rx_bytes += length;
			netif_rx(skb);
		} else {
			ether->stats.rx_errors++;

			if (status & RXDS_RP) {
				dev_err(&pdev->dev, "rx runt err\n");
				ether->stats.rx_length_errors++;
			} else if (status & RXDS_CRCE) {
					dev_err(&pdev->dev, "rx crc err\n");
					ether->stats.rx_crc_errors++;
				}

			if (status & RXDS_ALIE) {
				dev_err(&pdev->dev, "rx aligment err\n");
				ether->stats.rx_frame_errors++;
			} else if (status & RXDS_PTLE) {
					dev_err(&pdev->dev, "rx longer err\n");
					ether->stats.rx_over_errors++;
				}
			}

		rxbd->sl = RX_OWEN_DMA;
		rxbd->reserved = 0x0;
		ether->cur_rx = (ether->cur_rx+1) % RX_DESC_SIZE;
		rxbd = &ether->rdesc->desclist[ether->cur_rx];

		dev->last_rx = jiffies;
	} while (1);
}

static irqreturn_t w90p910_rx_interrupt(int irq, void *dev_id)
{
	struct net_device *dev;
	struct w90p910_ether  *ether;
	struct platform_device *pdev;
	unsigned int status;

	dev = (struct net_device *)dev_id;
	ether = netdev_priv(dev);
	pdev = ether->pdev;

	spin_lock(&ether->lock);

	w90p910_get_and_clear_int(dev, &status);

	if (status & MISTA_RDU) {
		netdev_rx(dev);

		w90p910_trigger_rx(dev);

		spin_unlock(&ether->lock);
		return IRQ_HANDLED;
	} else if (status & MISTA_RXBERR) {
			dev_err(&pdev->dev, "emc rx bus error\n");
			w90p910_reset_mac(dev);
		}

	netdev_rx(dev);
	spin_unlock(&ether->lock);
	return IRQ_HANDLED;
}

static int w90p910_ether_open(struct net_device *dev)
{
	struct w90p910_ether *ether;
	struct platform_device *pdev;

	ether = netdev_priv(dev);
	pdev = ether->pdev;

	w90p910_reset_mac(dev);
	w90p910_set_fifo_threshold(dev);
	w90p910_set_curdest(dev);
	w90p910_enable_cam(dev);
	w90p910_enable_cam_command(dev);
	w90p910_enable_mac_interrupt(dev);
	w90p910_set_global_maccmd(dev);
	w90p910_enable_rx(dev, 1);

	ether->rx_packets = 0x0;
	ether->rx_bytes = 0x0;

	if (request_irq(ether->txirq, w90p910_tx_interrupt,
						0x0, pdev->name, dev)) {
		dev_err(&pdev->dev, "register irq tx failed\n");
		return -EAGAIN;
	}

	if (request_irq(ether->rxirq, w90p910_rx_interrupt,
						0x0, pdev->name, dev)) {
		dev_err(&pdev->dev, "register irq rx failed\n");
		return -EAGAIN;
	}

	mod_timer(&ether->check_timer, jiffies + msecs_to_jiffies(1000));
	netif_start_queue(dev);
	w90p910_trigger_rx(dev);

	dev_info(&pdev->dev, "%s is OPENED\n", dev->name);

	return 0;
}

static void w90p910_ether_set_multicast_list(struct net_device *dev)
{
	struct w90p910_ether *ether;
	unsigned int rx_mode;

	ether = netdev_priv(dev);

	if (dev->flags & IFF_PROMISC)
		rx_mode = CAMCMR_AUP | CAMCMR_AMP | CAMCMR_ABP | CAMCMR_ECMP;
	else if ((dev->flags & IFF_ALLMULTI) || dev->mc_list)
			rx_mode = CAMCMR_AMP | CAMCMR_ABP | CAMCMR_ECMP;
		else
				rx_mode = CAMCMR_ECMP | CAMCMR_ABP;
	__raw_writel(rx_mode, ether->reg + REG_CAMCMR);
}

static int w90p910_ether_ioctl(struct net_device *dev,
						struct ifreq *ifr, int cmd)
{
	struct w90p910_ether *ether = netdev_priv(dev);
	struct mii_ioctl_data *data = if_mii(ifr);

	return generic_mii_ioctl(&ether->mii, data, cmd, NULL);
}

static void w90p910_get_drvinfo(struct net_device *dev,
					struct ethtool_drvinfo *info)
{
	strcpy(info->driver, DRV_MODULE_NAME);
	strcpy(info->version, DRV_MODULE_VERSION);
}

static int w90p910_get_settings(struct net_device *dev, struct ethtool_cmd *cmd)
{
	struct w90p910_ether *ether = netdev_priv(dev);
	return mii_ethtool_gset(&ether->mii, cmd);
}

static int w90p910_set_settings(struct net_device *dev, struct ethtool_cmd *cmd)
{
	struct w90p910_ether *ether = netdev_priv(dev);
	return mii_ethtool_sset(&ether->mii, cmd);
}

static int w90p910_nway_reset(struct net_device *dev)
{
	struct w90p910_ether *ether = netdev_priv(dev);
	return mii_nway_restart(&ether->mii);
}

static u32 w90p910_get_link(struct net_device *dev)
{
	struct w90p910_ether *ether = netdev_priv(dev);
	return mii_link_ok(&ether->mii);
}

static const struct ethtool_ops w90p910_ether_ethtool_ops = {
	.get_settings	= w90p910_get_settings,
	.set_settings	= w90p910_set_settings,
	.get_drvinfo	= w90p910_get_drvinfo,
	.nway_reset	= w90p910_nway_reset,
	.get_link	= w90p910_get_link,
};

static const struct net_device_ops w90p910_ether_netdev_ops = {
	.ndo_open		= w90p910_ether_open,
	.ndo_stop		= w90p910_ether_close,
	.ndo_start_xmit		= w90p910_ether_start_xmit,
	.ndo_get_stats		= w90p910_ether_stats,
	.ndo_set_multicast_list	= w90p910_ether_set_multicast_list,
	.ndo_set_mac_address	= set_mac_address,
	.ndo_do_ioctl		= w90p910_ether_ioctl,
	.ndo_validate_addr	= eth_validate_addr,
	.ndo_change_mtu		= eth_change_mtu,
};

static void __init get_mac_address(struct net_device *dev)
{
	struct w90p910_ether *ether = netdev_priv(dev);
	struct platform_device *pdev;
	char addr[6];

	pdev = ether->pdev;

	addr[0] = 0x00;
	addr[1] = 0x02;
	addr[2] = 0xac;
	addr[3] = 0x55;
	addr[4] = 0x88;
	addr[5] = 0xa8;

	if (is_valid_ether_addr(addr))
		memcpy(dev->dev_addr, &addr, 0x06);
	else
		dev_err(&pdev->dev, "invalid mac address\n");
}

static int w90p910_ether_setup(struct net_device *dev)
{
	struct w90p910_ether *ether = netdev_priv(dev);

	ether_setup(dev);
	dev->netdev_ops = &w90p910_ether_netdev_ops;
	dev->ethtool_ops = &w90p910_ether_ethtool_ops;

	dev->tx_queue_len = 16;
	dev->dma = 0x0;
	dev->watchdog_timeo = TX_TIMEOUT;

	get_mac_address(dev);

	spin_lock_init(&ether->lock);

	ether->cur_tx = 0x0;
	ether->cur_rx = 0x0;
	ether->finish_tx = 0x0;
	ether->linkflag = 0x0;
	ether->mii.phy_id = 0x01;
	ether->mii.phy_id_mask = 0x1f;
	ether->mii.reg_num_mask = 0x1f;
	ether->mii.dev = dev;
	ether->mii.mdio_read = w90p910_mdio_read;
	ether->mii.mdio_write = w90p910_mdio_write;

	setup_timer(&ether->check_timer, w90p910_check_link,
						(unsigned long)dev);

	return 0;
}

static int __devinit w90p910_ether_probe(struct platform_device *pdev)
{
	struct w90p910_ether *ether;
	struct net_device *dev;
	struct resource *res;
	int error;

	dev = alloc_etherdev(sizeof(struct w90p910_ether));
	if (!dev)
		return -ENOMEM;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (res == NULL) {
		dev_err(&pdev->dev, "failed to get I/O memory\n");
		error = -ENXIO;
		goto failed_free;
	}

	res = request_mem_region(res->start, resource_size(res), pdev->name);
	if (res == NULL) {
		dev_err(&pdev->dev, "failed to request I/O memory\n");
		error = -EBUSY;
		goto failed_free;
	}

	ether = netdev_priv(dev);

	ether->reg = ioremap(res->start, resource_size(res));
	if (ether->reg == NULL) {
		dev_err(&pdev->dev, "failed to remap I/O memory\n");
		error = -ENXIO;
		goto failed_free_mem;
	}

	ether->txirq = platform_get_irq(pdev, 0);
	if (ether->txirq < 0) {
		dev_err(&pdev->dev, "failed to get ether tx irq\n");
		error = -ENXIO;
		goto failed_free_io;
	}

	ether->rxirq = platform_get_irq(pdev, 1);
	if (ether->rxirq < 0) {
		dev_err(&pdev->dev, "failed to get ether rx irq\n");
		error = -ENXIO;
		goto failed_free_txirq;
	}

	platform_set_drvdata(pdev, dev);

	ether->clk = clk_get(&pdev->dev, NULL);
	if (IS_ERR(ether->clk)) {
		dev_err(&pdev->dev, "failed to get ether clock\n");
		error = PTR_ERR(ether->clk);
		goto failed_free_rxirq;
	}

	ether->rmiiclk = clk_get(&pdev->dev, "RMII");
	if (IS_ERR(ether->rmiiclk)) {
		dev_err(&pdev->dev, "failed to get ether clock\n");
		error = PTR_ERR(ether->rmiiclk);
		goto failed_put_clk;
	}

	ether->pdev = pdev;

	w90p910_ether_setup(dev);

	error = register_netdev(dev);
	if (error != 0) {
		dev_err(&pdev->dev, "Regiter EMC w90p910 FAILED\n");
		error = -ENODEV;
		goto failed_put_rmiiclk;
	}

	return 0;
failed_put_rmiiclk:
	clk_put(ether->rmiiclk);
failed_put_clk:
	clk_put(ether->clk);
failed_free_rxirq:
	free_irq(ether->rxirq, pdev);
	platform_set_drvdata(pdev, NULL);
failed_free_txirq:
	free_irq(ether->txirq, pdev);
failed_free_io:
	iounmap(ether->reg);
failed_free_mem:
	release_mem_region(res->start, resource_size(res));
failed_free:
	free_netdev(dev);
	return error;
}

static int __devexit w90p910_ether_remove(struct platform_device *pdev)
{
	struct net_device *dev = platform_get_drvdata(pdev);
	struct w90p910_ether *ether = netdev_priv(dev);

	unregister_netdev(dev);
	clk_put(ether->rmiiclk);
	clk_put(ether->clk);
	del_timer_sync(&ether->check_timer);
	platform_set_drvdata(pdev, NULL);
	free_netdev(dev);
	return 0;
}

static struct platform_driver w90p910_ether_driver = {
	.probe		= w90p910_ether_probe,
	.remove		= __devexit_p(w90p910_ether_remove),
	.driver		= {
		.name	= "nuc900-emc",
		.owner	= THIS_MODULE,
	},
};

static int __init w90p910_ether_init(void)
{
	return platform_driver_register(&w90p910_ether_driver);
}

static void __exit w90p910_ether_exit(void)
{
	platform_driver_unregister(&w90p910_ether_driver);
}

module_init(w90p910_ether_init);
module_exit(w90p910_ether_exit);

MODULE_AUTHOR("Wan ZongShun <mcuos.com@gmail.com>");
MODULE_DESCRIPTION("w90p910 MAC driver!");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:nuc900-emc");

