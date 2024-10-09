// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * EP93xx ethernet network device driver
 * Copyright (C) 2006 Lennert Buytenhek <buytenh@wantstofly.org>
 * Dedicated to Marija Kulikova.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ":%s: " fmt, __func__

#include <linux/dma-mapping.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/netdevice.h>
#include <linux/mii.h>
#include <linux/etherdevice.h>
#include <linux/ethtool.h>
#include <linux/interrupt.h>
#include <linux/moduleparam.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/slab.h>

#define DRV_MODULE_NAME		"ep93xx-eth"

#define RX_QUEUE_ENTRIES	64
#define TX_QUEUE_ENTRIES	8

#define MAX_PKT_SIZE		2044
#define PKT_BUF_SIZE		2048

#define REG_RXCTL		0x0000
#define  REG_RXCTL_DEFAULT	0x00073800
#define REG_TXCTL		0x0004
#define  REG_TXCTL_ENABLE	0x00000001
#define REG_MIICMD		0x0010
#define  REG_MIICMD_READ	0x00008000
#define  REG_MIICMD_WRITE	0x00004000
#define REG_MIIDATA		0x0014
#define REG_MIISTS		0x0018
#define  REG_MIISTS_BUSY	0x00000001
#define REG_SELFCTL		0x0020
#define  REG_SELFCTL_RESET	0x00000001
#define REG_INTEN		0x0024
#define  REG_INTEN_TX		0x00000008
#define  REG_INTEN_RX		0x00000007
#define REG_INTSTSP		0x0028
#define  REG_INTSTS_TX		0x00000008
#define  REG_INTSTS_RX		0x00000004
#define REG_INTSTSC		0x002c
#define REG_AFP			0x004c
#define REG_INDAD0		0x0050
#define REG_INDAD1		0x0051
#define REG_INDAD2		0x0052
#define REG_INDAD3		0x0053
#define REG_INDAD4		0x0054
#define REG_INDAD5		0x0055
#define REG_GIINTMSK		0x0064
#define  REG_GIINTMSK_ENABLE	0x00008000
#define REG_BMCTL		0x0080
#define  REG_BMCTL_ENABLE_TX	0x00000100
#define  REG_BMCTL_ENABLE_RX	0x00000001
#define REG_BMSTS		0x0084
#define  REG_BMSTS_RX_ACTIVE	0x00000008
#define REG_RXDQBADD		0x0090
#define REG_RXDQBLEN		0x0094
#define REG_RXDCURADD		0x0098
#define REG_RXDENQ		0x009c
#define REG_RXSTSQBADD		0x00a0
#define REG_RXSTSQBLEN		0x00a4
#define REG_RXSTSQCURADD	0x00a8
#define REG_RXSTSENQ		0x00ac
#define REG_TXDQBADD		0x00b0
#define REG_TXDQBLEN		0x00b4
#define REG_TXDQCURADD		0x00b8
#define REG_TXDENQ		0x00bc
#define REG_TXSTSQBADD		0x00c0
#define REG_TXSTSQBLEN		0x00c4
#define REG_TXSTSQCURADD	0x00c8
#define REG_MAXFRMLEN		0x00e8

struct ep93xx_rdesc
{
	u32	buf_addr;
	u32	rdesc1;
};

#define RDESC1_NSOF		0x80000000
#define RDESC1_BUFFER_INDEX	0x7fff0000
#define RDESC1_BUFFER_LENGTH	0x0000ffff

struct ep93xx_rstat
{
	u32	rstat0;
	u32	rstat1;
};

#define RSTAT0_RFP		0x80000000
#define RSTAT0_RWE		0x40000000
#define RSTAT0_EOF		0x20000000
#define RSTAT0_EOB		0x10000000
#define RSTAT0_AM		0x00c00000
#define RSTAT0_RX_ERR		0x00200000
#define RSTAT0_OE		0x00100000
#define RSTAT0_FE		0x00080000
#define RSTAT0_RUNT		0x00040000
#define RSTAT0_EDATA		0x00020000
#define RSTAT0_CRCE		0x00010000
#define RSTAT0_CRCI		0x00008000
#define RSTAT0_HTI		0x00003f00
#define RSTAT1_RFP		0x80000000
#define RSTAT1_BUFFER_INDEX	0x7fff0000
#define RSTAT1_FRAME_LENGTH	0x0000ffff

struct ep93xx_tdesc
{
	u32	buf_addr;
	u32	tdesc1;
};

#define TDESC1_EOF		0x80000000
#define TDESC1_BUFFER_INDEX	0x7fff0000
#define TDESC1_BUFFER_ABORT	0x00008000
#define TDESC1_BUFFER_LENGTH	0x00000fff

struct ep93xx_tstat
{
	u32	tstat0;
};

#define TSTAT0_TXFP		0x80000000
#define TSTAT0_TXWE		0x40000000
#define TSTAT0_FA		0x20000000
#define TSTAT0_LCRS		0x10000000
#define TSTAT0_OW		0x04000000
#define TSTAT0_TXU		0x02000000
#define TSTAT0_ECOLL		0x01000000
#define TSTAT0_NCOLL		0x001f0000
#define TSTAT0_BUFFER_INDEX	0x00007fff

struct ep93xx_descs
{
	struct ep93xx_rdesc	rdesc[RX_QUEUE_ENTRIES];
	struct ep93xx_tdesc	tdesc[TX_QUEUE_ENTRIES];
	struct ep93xx_rstat	rstat[RX_QUEUE_ENTRIES];
	struct ep93xx_tstat	tstat[TX_QUEUE_ENTRIES];
};

struct ep93xx_priv
{
	struct resource		*res;
	void __iomem		*base_addr;
	int			irq;

	struct ep93xx_descs	*descs;
	dma_addr_t		descs_dma_addr;

	void			*rx_buf[RX_QUEUE_ENTRIES];
	void			*tx_buf[TX_QUEUE_ENTRIES];

	spinlock_t		rx_lock;
	unsigned int		rx_pointer;
	unsigned int		tx_clean_pointer;
	unsigned int		tx_pointer;
	spinlock_t		tx_pending_lock;
	unsigned int		tx_pending;

	struct net_device	*dev;
	struct napi_struct	napi;

	struct mii_if_info	mii;
	u8			mdc_divisor;
};

#define rdb(ep, off)		__raw_readb((ep)->base_addr + (off))
#define rdw(ep, off)		__raw_readw((ep)->base_addr + (off))
#define rdl(ep, off)		__raw_readl((ep)->base_addr + (off))
#define wrb(ep, off, val)	__raw_writeb((val), (ep)->base_addr + (off))
#define wrw(ep, off, val)	__raw_writew((val), (ep)->base_addr + (off))
#define wrl(ep, off, val)	__raw_writel((val), (ep)->base_addr + (off))

static int ep93xx_mdio_read(struct net_device *dev, int phy_id, int reg)
{
	struct ep93xx_priv *ep = netdev_priv(dev);
	int data;
	int i;

	wrl(ep, REG_MIICMD, REG_MIICMD_READ | (phy_id << 5) | reg);

	for (i = 0; i < 10; i++) {
		if ((rdl(ep, REG_MIISTS) & REG_MIISTS_BUSY) == 0)
			break;
		msleep(1);
	}

	if (i == 10) {
		pr_info("mdio read timed out\n");
		data = 0xffff;
	} else {
		data = rdl(ep, REG_MIIDATA);
	}

	return data;
}

static void ep93xx_mdio_write(struct net_device *dev, int phy_id, int reg, int data)
{
	struct ep93xx_priv *ep = netdev_priv(dev);
	int i;

	wrl(ep, REG_MIIDATA, data);
	wrl(ep, REG_MIICMD, REG_MIICMD_WRITE | (phy_id << 5) | reg);

	for (i = 0; i < 10; i++) {
		if ((rdl(ep, REG_MIISTS) & REG_MIISTS_BUSY) == 0)
			break;
		msleep(1);
	}

	if (i == 10)
		pr_info("mdio write timed out\n");
}

static int ep93xx_rx(struct net_device *dev, int budget)
{
	struct ep93xx_priv *ep = netdev_priv(dev);
	int processed = 0;

	while (processed < budget) {
		int entry;
		struct ep93xx_rstat *rstat;
		u32 rstat0;
		u32 rstat1;
		int length;
		struct sk_buff *skb;

		entry = ep->rx_pointer;
		rstat = ep->descs->rstat + entry;

		rstat0 = rstat->rstat0;
		rstat1 = rstat->rstat1;
		if (!(rstat0 & RSTAT0_RFP) || !(rstat1 & RSTAT1_RFP))
			break;

		rstat->rstat0 = 0;
		rstat->rstat1 = 0;

		if (!(rstat0 & RSTAT0_EOF))
			pr_crit("not end-of-frame %.8x %.8x\n", rstat0, rstat1);
		if (!(rstat0 & RSTAT0_EOB))
			pr_crit("not end-of-buffer %.8x %.8x\n", rstat0, rstat1);
		if ((rstat1 & RSTAT1_BUFFER_INDEX) >> 16 != entry)
			pr_crit("entry mismatch %.8x %.8x\n", rstat0, rstat1);

		if (!(rstat0 & RSTAT0_RWE)) {
			dev->stats.rx_errors++;
			if (rstat0 & RSTAT0_OE)
				dev->stats.rx_fifo_errors++;
			if (rstat0 & RSTAT0_FE)
				dev->stats.rx_frame_errors++;
			if (rstat0 & (RSTAT0_RUNT | RSTAT0_EDATA))
				dev->stats.rx_length_errors++;
			if (rstat0 & RSTAT0_CRCE)
				dev->stats.rx_crc_errors++;
			goto err;
		}

		length = rstat1 & RSTAT1_FRAME_LENGTH;
		if (length > MAX_PKT_SIZE) {
			pr_notice("invalid length %.8x %.8x\n", rstat0, rstat1);
			goto err;
		}

		/* Strip FCS.  */
		if (rstat0 & RSTAT0_CRCI)
			length -= 4;

		skb = netdev_alloc_skb(dev, length + 2);
		if (likely(skb != NULL)) {
			struct ep93xx_rdesc *rxd = &ep->descs->rdesc[entry];
			skb_reserve(skb, 2);
			dma_sync_single_for_cpu(dev->dev.parent, rxd->buf_addr,
						length, DMA_FROM_DEVICE);
			skb_copy_to_linear_data(skb, ep->rx_buf[entry], length);
			dma_sync_single_for_device(dev->dev.parent,
						   rxd->buf_addr, length,
						   DMA_FROM_DEVICE);
			skb_put(skb, length);
			skb->protocol = eth_type_trans(skb, dev);

			napi_gro_receive(&ep->napi, skb);

			dev->stats.rx_packets++;
			dev->stats.rx_bytes += length;
		} else {
			dev->stats.rx_dropped++;
		}

err:
		ep->rx_pointer = (entry + 1) & (RX_QUEUE_ENTRIES - 1);
		processed++;
	}

	return processed;
}

static int ep93xx_poll(struct napi_struct *napi, int budget)
{
	struct ep93xx_priv *ep = container_of(napi, struct ep93xx_priv, napi);
	struct net_device *dev = ep->dev;
	int rx;

	rx = ep93xx_rx(dev, budget);
	if (rx < budget && napi_complete_done(napi, rx)) {
		spin_lock_irq(&ep->rx_lock);
		wrl(ep, REG_INTEN, REG_INTEN_TX | REG_INTEN_RX);
		spin_unlock_irq(&ep->rx_lock);
	}

	if (rx) {
		wrw(ep, REG_RXDENQ, rx);
		wrw(ep, REG_RXSTSENQ, rx);
	}

	return rx;
}

static netdev_tx_t ep93xx_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct ep93xx_priv *ep = netdev_priv(dev);
	struct ep93xx_tdesc *txd;
	int entry;

	if (unlikely(skb->len > MAX_PKT_SIZE)) {
		dev->stats.tx_dropped++;
		dev_kfree_skb(skb);
		return NETDEV_TX_OK;
	}

	entry = ep->tx_pointer;
	ep->tx_pointer = (ep->tx_pointer + 1) & (TX_QUEUE_ENTRIES - 1);

	txd = &ep->descs->tdesc[entry];

	txd->tdesc1 = TDESC1_EOF | (entry << 16) | (skb->len & 0xfff);
	dma_sync_single_for_cpu(dev->dev.parent, txd->buf_addr, skb->len,
				DMA_TO_DEVICE);
	skb_copy_and_csum_dev(skb, ep->tx_buf[entry]);
	dma_sync_single_for_device(dev->dev.parent, txd->buf_addr, skb->len,
				   DMA_TO_DEVICE);
	dev_kfree_skb(skb);

	spin_lock_irq(&ep->tx_pending_lock);
	ep->tx_pending++;
	if (ep->tx_pending == TX_QUEUE_ENTRIES)
		netif_stop_queue(dev);
	spin_unlock_irq(&ep->tx_pending_lock);

	wrl(ep, REG_TXDENQ, 1);

	return NETDEV_TX_OK;
}

static void ep93xx_tx_complete(struct net_device *dev)
{
	struct ep93xx_priv *ep = netdev_priv(dev);
	int wake;

	wake = 0;

	spin_lock(&ep->tx_pending_lock);
	while (1) {
		int entry;
		struct ep93xx_tstat *tstat;
		u32 tstat0;

		entry = ep->tx_clean_pointer;
		tstat = ep->descs->tstat + entry;

		tstat0 = tstat->tstat0;
		if (!(tstat0 & TSTAT0_TXFP))
			break;

		tstat->tstat0 = 0;

		if (tstat0 & TSTAT0_FA)
			pr_crit("frame aborted %.8x\n", tstat0);
		if ((tstat0 & TSTAT0_BUFFER_INDEX) != entry)
			pr_crit("entry mismatch %.8x\n", tstat0);

		if (tstat0 & TSTAT0_TXWE) {
			int length = ep->descs->tdesc[entry].tdesc1 & 0xfff;

			dev->stats.tx_packets++;
			dev->stats.tx_bytes += length;
		} else {
			dev->stats.tx_errors++;
		}

		if (tstat0 & TSTAT0_OW)
			dev->stats.tx_window_errors++;
		if (tstat0 & TSTAT0_TXU)
			dev->stats.tx_fifo_errors++;
		dev->stats.collisions += (tstat0 >> 16) & 0x1f;

		ep->tx_clean_pointer = (entry + 1) & (TX_QUEUE_ENTRIES - 1);
		if (ep->tx_pending == TX_QUEUE_ENTRIES)
			wake = 1;
		ep->tx_pending--;
	}
	spin_unlock(&ep->tx_pending_lock);

	if (wake)
		netif_wake_queue(dev);
}

static irqreturn_t ep93xx_irq(int irq, void *dev_id)
{
	struct net_device *dev = dev_id;
	struct ep93xx_priv *ep = netdev_priv(dev);
	u32 status;

	status = rdl(ep, REG_INTSTSC);
	if (status == 0)
		return IRQ_NONE;

	if (status & REG_INTSTS_RX) {
		spin_lock(&ep->rx_lock);
		if (likely(napi_schedule_prep(&ep->napi))) {
			wrl(ep, REG_INTEN, REG_INTEN_TX);
			__napi_schedule(&ep->napi);
		}
		spin_unlock(&ep->rx_lock);
	}

	if (status & REG_INTSTS_TX)
		ep93xx_tx_complete(dev);

	return IRQ_HANDLED;
}

static void ep93xx_free_buffers(struct ep93xx_priv *ep)
{
	struct device *dev = ep->dev->dev.parent;
	int i;

	if (!ep->descs)
		return;

	for (i = 0; i < RX_QUEUE_ENTRIES; i++) {
		dma_addr_t d;

		d = ep->descs->rdesc[i].buf_addr;
		if (d)
			dma_unmap_single(dev, d, PKT_BUF_SIZE, DMA_FROM_DEVICE);

		kfree(ep->rx_buf[i]);
	}

	for (i = 0; i < TX_QUEUE_ENTRIES; i++) {
		dma_addr_t d;

		d = ep->descs->tdesc[i].buf_addr;
		if (d)
			dma_unmap_single(dev, d, PKT_BUF_SIZE, DMA_TO_DEVICE);

		kfree(ep->tx_buf[i]);
	}

	dma_free_coherent(dev, sizeof(struct ep93xx_descs), ep->descs,
							ep->descs_dma_addr);
	ep->descs = NULL;
}

static int ep93xx_alloc_buffers(struct ep93xx_priv *ep)
{
	struct device *dev = ep->dev->dev.parent;
	int i;

	ep->descs = dma_alloc_coherent(dev, sizeof(struct ep93xx_descs),
				&ep->descs_dma_addr, GFP_KERNEL);
	if (ep->descs == NULL)
		return 1;

	for (i = 0; i < RX_QUEUE_ENTRIES; i++) {
		void *buf;
		dma_addr_t d;

		buf = kmalloc(PKT_BUF_SIZE, GFP_KERNEL);
		if (buf == NULL)
			goto err;

		d = dma_map_single(dev, buf, PKT_BUF_SIZE, DMA_FROM_DEVICE);
		if (dma_mapping_error(dev, d)) {
			kfree(buf);
			goto err;
		}

		ep->rx_buf[i] = buf;
		ep->descs->rdesc[i].buf_addr = d;
		ep->descs->rdesc[i].rdesc1 = (i << 16) | PKT_BUF_SIZE;
	}

	for (i = 0; i < TX_QUEUE_ENTRIES; i++) {
		void *buf;
		dma_addr_t d;

		buf = kmalloc(PKT_BUF_SIZE, GFP_KERNEL);
		if (buf == NULL)
			goto err;

		d = dma_map_single(dev, buf, PKT_BUF_SIZE, DMA_TO_DEVICE);
		if (dma_mapping_error(dev, d)) {
			kfree(buf);
			goto err;
		}

		ep->tx_buf[i] = buf;
		ep->descs->tdesc[i].buf_addr = d;
	}

	return 0;

err:
	ep93xx_free_buffers(ep);
	return 1;
}

static int ep93xx_start_hw(struct net_device *dev)
{
	struct ep93xx_priv *ep = netdev_priv(dev);
	unsigned long addr;
	int i;

	wrl(ep, REG_SELFCTL, REG_SELFCTL_RESET);
	for (i = 0; i < 10; i++) {
		if ((rdl(ep, REG_SELFCTL) & REG_SELFCTL_RESET) == 0)
			break;
		msleep(1);
	}

	if (i == 10) {
		pr_crit("hw failed to reset\n");
		return 1;
	}

	wrl(ep, REG_SELFCTL, ((ep->mdc_divisor - 1) << 9));

	/* Does the PHY support preamble suppress?  */
	if ((ep93xx_mdio_read(dev, ep->mii.phy_id, MII_BMSR) & 0x0040) != 0)
		wrl(ep, REG_SELFCTL, ((ep->mdc_divisor - 1) << 9) | (1 << 8));

	/* Receive descriptor ring.  */
	addr = ep->descs_dma_addr + offsetof(struct ep93xx_descs, rdesc);
	wrl(ep, REG_RXDQBADD, addr);
	wrl(ep, REG_RXDCURADD, addr);
	wrw(ep, REG_RXDQBLEN, RX_QUEUE_ENTRIES * sizeof(struct ep93xx_rdesc));

	/* Receive status ring.  */
	addr = ep->descs_dma_addr + offsetof(struct ep93xx_descs, rstat);
	wrl(ep, REG_RXSTSQBADD, addr);
	wrl(ep, REG_RXSTSQCURADD, addr);
	wrw(ep, REG_RXSTSQBLEN, RX_QUEUE_ENTRIES * sizeof(struct ep93xx_rstat));

	/* Transmit descriptor ring.  */
	addr = ep->descs_dma_addr + offsetof(struct ep93xx_descs, tdesc);
	wrl(ep, REG_TXDQBADD, addr);
	wrl(ep, REG_TXDQCURADD, addr);
	wrw(ep, REG_TXDQBLEN, TX_QUEUE_ENTRIES * sizeof(struct ep93xx_tdesc));

	/* Transmit status ring.  */
	addr = ep->descs_dma_addr + offsetof(struct ep93xx_descs, tstat);
	wrl(ep, REG_TXSTSQBADD, addr);
	wrl(ep, REG_TXSTSQCURADD, addr);
	wrw(ep, REG_TXSTSQBLEN, TX_QUEUE_ENTRIES * sizeof(struct ep93xx_tstat));

	wrl(ep, REG_BMCTL, REG_BMCTL_ENABLE_TX | REG_BMCTL_ENABLE_RX);
	wrl(ep, REG_INTEN, REG_INTEN_TX | REG_INTEN_RX);
	wrl(ep, REG_GIINTMSK, 0);

	for (i = 0; i < 10; i++) {
		if ((rdl(ep, REG_BMSTS) & REG_BMSTS_RX_ACTIVE) != 0)
			break;
		msleep(1);
	}

	if (i == 10) {
		pr_crit("hw failed to start\n");
		return 1;
	}

	wrl(ep, REG_RXDENQ, RX_QUEUE_ENTRIES);
	wrl(ep, REG_RXSTSENQ, RX_QUEUE_ENTRIES);

	wrb(ep, REG_INDAD0, dev->dev_addr[0]);
	wrb(ep, REG_INDAD1, dev->dev_addr[1]);
	wrb(ep, REG_INDAD2, dev->dev_addr[2]);
	wrb(ep, REG_INDAD3, dev->dev_addr[3]);
	wrb(ep, REG_INDAD4, dev->dev_addr[4]);
	wrb(ep, REG_INDAD5, dev->dev_addr[5]);
	wrl(ep, REG_AFP, 0);

	wrl(ep, REG_MAXFRMLEN, (MAX_PKT_SIZE << 16) | MAX_PKT_SIZE);

	wrl(ep, REG_RXCTL, REG_RXCTL_DEFAULT);
	wrl(ep, REG_TXCTL, REG_TXCTL_ENABLE);

	return 0;
}

static void ep93xx_stop_hw(struct net_device *dev)
{
	struct ep93xx_priv *ep = netdev_priv(dev);
	int i;

	wrl(ep, REG_SELFCTL, REG_SELFCTL_RESET);
	for (i = 0; i < 10; i++) {
		if ((rdl(ep, REG_SELFCTL) & REG_SELFCTL_RESET) == 0)
			break;
		msleep(1);
	}

	if (i == 10)
		pr_crit("hw failed to reset\n");
}

static int ep93xx_open(struct net_device *dev)
{
	struct ep93xx_priv *ep = netdev_priv(dev);
	int err;

	if (ep93xx_alloc_buffers(ep))
		return -ENOMEM;

	napi_enable(&ep->napi);

	if (ep93xx_start_hw(dev)) {
		napi_disable(&ep->napi);
		ep93xx_free_buffers(ep);
		return -EIO;
	}

	spin_lock_init(&ep->rx_lock);
	ep->rx_pointer = 0;
	ep->tx_clean_pointer = 0;
	ep->tx_pointer = 0;
	spin_lock_init(&ep->tx_pending_lock);
	ep->tx_pending = 0;

	err = request_irq(ep->irq, ep93xx_irq, IRQF_SHARED, dev->name, dev);
	if (err) {
		napi_disable(&ep->napi);
		ep93xx_stop_hw(dev);
		ep93xx_free_buffers(ep);
		return err;
	}

	wrl(ep, REG_GIINTMSK, REG_GIINTMSK_ENABLE);

	netif_start_queue(dev);

	return 0;
}

static int ep93xx_close(struct net_device *dev)
{
	struct ep93xx_priv *ep = netdev_priv(dev);

	napi_disable(&ep->napi);
	netif_stop_queue(dev);

	wrl(ep, REG_GIINTMSK, 0);
	free_irq(ep->irq, dev);
	ep93xx_stop_hw(dev);
	ep93xx_free_buffers(ep);

	return 0;
}

static int ep93xx_ioctl(struct net_device *dev, struct ifreq *ifr, int cmd)
{
	struct ep93xx_priv *ep = netdev_priv(dev);
	struct mii_ioctl_data *data = if_mii(ifr);

	return generic_mii_ioctl(&ep->mii, data, cmd, NULL);
}

static void ep93xx_get_drvinfo(struct net_device *dev, struct ethtool_drvinfo *info)
{
	strscpy(info->driver, DRV_MODULE_NAME, sizeof(info->driver));
}

static int ep93xx_get_link_ksettings(struct net_device *dev,
				     struct ethtool_link_ksettings *cmd)
{
	struct ep93xx_priv *ep = netdev_priv(dev);

	mii_ethtool_get_link_ksettings(&ep->mii, cmd);

	return 0;
}

static int ep93xx_set_link_ksettings(struct net_device *dev,
				     const struct ethtool_link_ksettings *cmd)
{
	struct ep93xx_priv *ep = netdev_priv(dev);
	return mii_ethtool_set_link_ksettings(&ep->mii, cmd);
}

static int ep93xx_nway_reset(struct net_device *dev)
{
	struct ep93xx_priv *ep = netdev_priv(dev);
	return mii_nway_restart(&ep->mii);
}

static u32 ep93xx_get_link(struct net_device *dev)
{
	struct ep93xx_priv *ep = netdev_priv(dev);
	return mii_link_ok(&ep->mii);
}

static const struct ethtool_ops ep93xx_ethtool_ops = {
	.get_drvinfo		= ep93xx_get_drvinfo,
	.nway_reset		= ep93xx_nway_reset,
	.get_link		= ep93xx_get_link,
	.get_link_ksettings	= ep93xx_get_link_ksettings,
	.set_link_ksettings	= ep93xx_set_link_ksettings,
};

static const struct net_device_ops ep93xx_netdev_ops = {
	.ndo_open		= ep93xx_open,
	.ndo_stop		= ep93xx_close,
	.ndo_start_xmit		= ep93xx_xmit,
	.ndo_eth_ioctl		= ep93xx_ioctl,
	.ndo_validate_addr	= eth_validate_addr,
	.ndo_set_mac_address	= eth_mac_addr,
};

static void ep93xx_eth_remove(struct platform_device *pdev)
{
	struct net_device *dev;
	struct ep93xx_priv *ep;
	struct resource *mem;

	dev = platform_get_drvdata(pdev);
	if (dev == NULL)
		return;

	ep = netdev_priv(dev);

	/* @@@ Force down.  */
	unregister_netdev(dev);
	ep93xx_free_buffers(ep);

	if (ep->base_addr != NULL)
		iounmap(ep->base_addr);

	if (ep->res != NULL) {
		mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
		release_mem_region(mem->start, resource_size(mem));
	}

	free_netdev(dev);
}

static int ep93xx_eth_probe(struct platform_device *pdev)
{
	struct net_device *dev;
	struct ep93xx_priv *ep;
	struct resource *mem;
	void __iomem *base_addr;
	struct device_node *np;
	u8 addr[ETH_ALEN];
	u32 phy_id;
	int irq;
	int err;

	if (pdev == NULL)
		return -ENODEV;

	mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	irq = platform_get_irq(pdev, 0);
	if (!mem || irq < 0)
		return -ENXIO;

	base_addr = ioremap(mem->start, resource_size(mem));
	if (!base_addr)
		return dev_err_probe(&pdev->dev, -EIO, "Failed to ioremap ethernet registers\n");

	np = of_parse_phandle(pdev->dev.of_node, "phy-handle", 0);
	if (!np)
		return dev_err_probe(&pdev->dev, -ENODEV, "Please provide \"phy-handle\"\n");

	err = of_property_read_u32(np, "reg", &phy_id);
	of_node_put(np);
	if (err)
		return dev_err_probe(&pdev->dev, -ENOENT, "Failed to locate \"phy_id\"\n");

	dev = alloc_etherdev(sizeof(struct ep93xx_priv));
	if (dev == NULL) {
		err = -ENOMEM;
		goto err_out;
	}

	memcpy_fromio(addr, base_addr + 0x50, ETH_ALEN);
	eth_hw_addr_set(dev, addr);
	dev->ethtool_ops = &ep93xx_ethtool_ops;
	dev->netdev_ops = &ep93xx_netdev_ops;
	dev->features |= NETIF_F_SG | NETIF_F_HW_CSUM;

	ep = netdev_priv(dev);
	ep->dev = dev;
	SET_NETDEV_DEV(dev, &pdev->dev);
	netif_napi_add(dev, &ep->napi, ep93xx_poll);

	platform_set_drvdata(pdev, dev);

	ep->res = request_mem_region(mem->start, resource_size(mem),
				     dev_name(&pdev->dev));
	if (ep->res == NULL) {
		dev_err(&pdev->dev, "Could not reserve memory region\n");
		err = -ENOMEM;
		goto err_out;
	}

	ep->base_addr = base_addr;
	ep->irq = irq;

	ep->mii.phy_id = phy_id;
	ep->mii.phy_id_mask = 0x1f;
	ep->mii.reg_num_mask = 0x1f;
	ep->mii.dev = dev;
	ep->mii.mdio_read = ep93xx_mdio_read;
	ep->mii.mdio_write = ep93xx_mdio_write;
	ep->mdc_divisor = 40;	/* Max HCLK 100 MHz, min MDIO clk 2.5 MHz.  */

	if (is_zero_ether_addr(dev->dev_addr))
		eth_hw_addr_random(dev);

	err = register_netdev(dev);
	if (err) {
		dev_err(&pdev->dev, "Failed to register netdev\n");
		goto err_out;
	}

	printk(KERN_INFO "%s: ep93xx on-chip ethernet, IRQ %d, %pM\n",
			dev->name, ep->irq, dev->dev_addr);

	return 0;

err_out:
	ep93xx_eth_remove(pdev);
	return err;
}

static const struct of_device_id ep93xx_eth_of_ids[] = {
	{ .compatible = "cirrus,ep9301-eth" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, ep93xx_eth_of_ids);

static struct platform_driver ep93xx_eth_driver = {
	.probe		= ep93xx_eth_probe,
	.remove		= ep93xx_eth_remove,
	.driver		= {
		.name	= "ep93xx-eth",
		.of_match_table = ep93xx_eth_of_ids,
	},
};

module_platform_driver(ep93xx_eth_driver);

MODULE_DESCRIPTION("Cirrus EP93xx Ethernet driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:ep93xx-eth");
