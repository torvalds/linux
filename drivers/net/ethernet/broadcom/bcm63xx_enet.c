/*
 * Driver for BCM963xx builtin Ethernet mac
 *
 * Copyright (C) 2008 Maxime Bizon <mbizon@freebox.fr>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/clk.h>
#include <linux/etherdevice.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/ethtool.h>
#include <linux/crc32.h>
#include <linux/err.h>
#include <linux/dma-mapping.h>
#include <linux/platform_device.h>
#include <linux/if_vlan.h>

#include <bcm63xx_dev_enet.h>
#include "bcm63xx_enet.h"

static char bcm_enet_driver_name[] = "bcm63xx_enet";
static char bcm_enet_driver_version[] = "1.0";

static int copybreak __read_mostly = 128;
module_param(copybreak, int, 0);
MODULE_PARM_DESC(copybreak, "Receive copy threshold");

/* io registers memory shared between all devices */
static void __iomem *bcm_enet_shared_base[3];

/*
 * io helpers to access mac registers
 */
static inline u32 enet_readl(struct bcm_enet_priv *priv, u32 off)
{
	return bcm_readl(priv->base + off);
}

static inline void enet_writel(struct bcm_enet_priv *priv,
			       u32 val, u32 off)
{
	bcm_writel(val, priv->base + off);
}

/*
 * io helpers to access switch registers
 */
static inline u32 enetsw_readl(struct bcm_enet_priv *priv, u32 off)
{
	return bcm_readl(priv->base + off);
}

static inline void enetsw_writel(struct bcm_enet_priv *priv,
				 u32 val, u32 off)
{
	bcm_writel(val, priv->base + off);
}

static inline u16 enetsw_readw(struct bcm_enet_priv *priv, u32 off)
{
	return bcm_readw(priv->base + off);
}

static inline void enetsw_writew(struct bcm_enet_priv *priv,
				 u16 val, u32 off)
{
	bcm_writew(val, priv->base + off);
}

static inline u8 enetsw_readb(struct bcm_enet_priv *priv, u32 off)
{
	return bcm_readb(priv->base + off);
}

static inline void enetsw_writeb(struct bcm_enet_priv *priv,
				 u8 val, u32 off)
{
	bcm_writeb(val, priv->base + off);
}


/* io helpers to access shared registers */
static inline u32 enet_dma_readl(struct bcm_enet_priv *priv, u32 off)
{
	return bcm_readl(bcm_enet_shared_base[0] + off);
}

static inline void enet_dma_writel(struct bcm_enet_priv *priv,
				       u32 val, u32 off)
{
	bcm_writel(val, bcm_enet_shared_base[0] + off);
}

static inline u32 enet_dmac_readl(struct bcm_enet_priv *priv, u32 off, int chan)
{
	return bcm_readl(bcm_enet_shared_base[1] +
		bcm63xx_enetdmacreg(off) + chan * priv->dma_chan_width);
}

static inline void enet_dmac_writel(struct bcm_enet_priv *priv,
				       u32 val, u32 off, int chan)
{
	bcm_writel(val, bcm_enet_shared_base[1] +
		bcm63xx_enetdmacreg(off) + chan * priv->dma_chan_width);
}

static inline u32 enet_dmas_readl(struct bcm_enet_priv *priv, u32 off, int chan)
{
	return bcm_readl(bcm_enet_shared_base[2] + off + chan * priv->dma_chan_width);
}

static inline void enet_dmas_writel(struct bcm_enet_priv *priv,
				       u32 val, u32 off, int chan)
{
	bcm_writel(val, bcm_enet_shared_base[2] + off + chan * priv->dma_chan_width);
}

/*
 * write given data into mii register and wait for transfer to end
 * with timeout (average measured transfer time is 25us)
 */
static int do_mdio_op(struct bcm_enet_priv *priv, unsigned int data)
{
	int limit;

	/* make sure mii interrupt status is cleared */
	enet_writel(priv, ENET_IR_MII, ENET_IR_REG);

	enet_writel(priv, data, ENET_MIIDATA_REG);
	wmb();

	/* busy wait on mii interrupt bit, with timeout */
	limit = 1000;
	do {
		if (enet_readl(priv, ENET_IR_REG) & ENET_IR_MII)
			break;
		udelay(1);
	} while (limit-- > 0);

	return (limit < 0) ? 1 : 0;
}

/*
 * MII internal read callback
 */
static int bcm_enet_mdio_read(struct bcm_enet_priv *priv, int mii_id,
			      int regnum)
{
	u32 tmp, val;

	tmp = regnum << ENET_MIIDATA_REG_SHIFT;
	tmp |= 0x2 << ENET_MIIDATA_TA_SHIFT;
	tmp |= mii_id << ENET_MIIDATA_PHYID_SHIFT;
	tmp |= ENET_MIIDATA_OP_READ_MASK;

	if (do_mdio_op(priv, tmp))
		return -1;

	val = enet_readl(priv, ENET_MIIDATA_REG);
	val &= 0xffff;
	return val;
}

/*
 * MII internal write callback
 */
static int bcm_enet_mdio_write(struct bcm_enet_priv *priv, int mii_id,
			       int regnum, u16 value)
{
	u32 tmp;

	tmp = (value & 0xffff) << ENET_MIIDATA_DATA_SHIFT;
	tmp |= 0x2 << ENET_MIIDATA_TA_SHIFT;
	tmp |= regnum << ENET_MIIDATA_REG_SHIFT;
	tmp |= mii_id << ENET_MIIDATA_PHYID_SHIFT;
	tmp |= ENET_MIIDATA_OP_WRITE_MASK;

	(void)do_mdio_op(priv, tmp);
	return 0;
}

/*
 * MII read callback from phylib
 */
static int bcm_enet_mdio_read_phylib(struct mii_bus *bus, int mii_id,
				     int regnum)
{
	return bcm_enet_mdio_read(bus->priv, mii_id, regnum);
}

/*
 * MII write callback from phylib
 */
static int bcm_enet_mdio_write_phylib(struct mii_bus *bus, int mii_id,
				      int regnum, u16 value)
{
	return bcm_enet_mdio_write(bus->priv, mii_id, regnum, value);
}

/*
 * MII read callback from mii core
 */
static int bcm_enet_mdio_read_mii(struct net_device *dev, int mii_id,
				  int regnum)
{
	return bcm_enet_mdio_read(netdev_priv(dev), mii_id, regnum);
}

/*
 * MII write callback from mii core
 */
static void bcm_enet_mdio_write_mii(struct net_device *dev, int mii_id,
				    int regnum, int value)
{
	bcm_enet_mdio_write(netdev_priv(dev), mii_id, regnum, value);
}

/*
 * refill rx queue
 */
static int bcm_enet_refill_rx(struct net_device *dev)
{
	struct bcm_enet_priv *priv;

	priv = netdev_priv(dev);

	while (priv->rx_desc_count < priv->rx_ring_size) {
		struct bcm_enet_desc *desc;
		struct sk_buff *skb;
		dma_addr_t p;
		int desc_idx;
		u32 len_stat;

		desc_idx = priv->rx_dirty_desc;
		desc = &priv->rx_desc_cpu[desc_idx];

		if (!priv->rx_skb[desc_idx]) {
			skb = netdev_alloc_skb(dev, priv->rx_skb_size);
			if (!skb)
				break;
			priv->rx_skb[desc_idx] = skb;
			p = dma_map_single(&priv->pdev->dev, skb->data,
					   priv->rx_skb_size,
					   DMA_FROM_DEVICE);
			desc->address = p;
		}

		len_stat = priv->rx_skb_size << DMADESC_LENGTH_SHIFT;
		len_stat |= DMADESC_OWNER_MASK;
		if (priv->rx_dirty_desc == priv->rx_ring_size - 1) {
			len_stat |= (DMADESC_WRAP_MASK >> priv->dma_desc_shift);
			priv->rx_dirty_desc = 0;
		} else {
			priv->rx_dirty_desc++;
		}
		wmb();
		desc->len_stat = len_stat;

		priv->rx_desc_count++;

		/* tell dma engine we allocated one buffer */
		if (priv->dma_has_sram)
			enet_dma_writel(priv, 1, ENETDMA_BUFALLOC_REG(priv->rx_chan));
		else
			enet_dmac_writel(priv, 1, ENETDMAC_BUFALLOC, priv->rx_chan);
	}

	/* If rx ring is still empty, set a timer to try allocating
	 * again at a later time. */
	if (priv->rx_desc_count == 0 && netif_running(dev)) {
		dev_warn(&priv->pdev->dev, "unable to refill rx ring\n");
		priv->rx_timeout.expires = jiffies + HZ;
		add_timer(&priv->rx_timeout);
	}

	return 0;
}

/*
 * timer callback to defer refill rx queue in case we're OOM
 */
static void bcm_enet_refill_rx_timer(unsigned long data)
{
	struct net_device *dev;
	struct bcm_enet_priv *priv;

	dev = (struct net_device *)data;
	priv = netdev_priv(dev);

	spin_lock(&priv->rx_lock);
	bcm_enet_refill_rx((struct net_device *)data);
	spin_unlock(&priv->rx_lock);
}

/*
 * extract packet from rx queue
 */
static int bcm_enet_receive_queue(struct net_device *dev, int budget)
{
	struct bcm_enet_priv *priv;
	struct device *kdev;
	int processed;

	priv = netdev_priv(dev);
	kdev = &priv->pdev->dev;
	processed = 0;

	/* don't scan ring further than number of refilled
	 * descriptor */
	if (budget > priv->rx_desc_count)
		budget = priv->rx_desc_count;

	do {
		struct bcm_enet_desc *desc;
		struct sk_buff *skb;
		int desc_idx;
		u32 len_stat;
		unsigned int len;

		desc_idx = priv->rx_curr_desc;
		desc = &priv->rx_desc_cpu[desc_idx];

		/* make sure we actually read the descriptor status at
		 * each loop */
		rmb();

		len_stat = desc->len_stat;

		/* break if dma ownership belongs to hw */
		if (len_stat & DMADESC_OWNER_MASK)
			break;

		processed++;
		priv->rx_curr_desc++;
		if (priv->rx_curr_desc == priv->rx_ring_size)
			priv->rx_curr_desc = 0;
		priv->rx_desc_count--;

		/* if the packet does not have start of packet _and_
		 * end of packet flag set, then just recycle it */
		if ((len_stat & (DMADESC_ESOP_MASK >> priv->dma_desc_shift)) !=
			(DMADESC_ESOP_MASK >> priv->dma_desc_shift)) {
			dev->stats.rx_dropped++;
			continue;
		}

		/* recycle packet if it's marked as bad */
		if (!priv->enet_is_sw &&
		    unlikely(len_stat & DMADESC_ERR_MASK)) {
			dev->stats.rx_errors++;

			if (len_stat & DMADESC_OVSIZE_MASK)
				dev->stats.rx_length_errors++;
			if (len_stat & DMADESC_CRC_MASK)
				dev->stats.rx_crc_errors++;
			if (len_stat & DMADESC_UNDER_MASK)
				dev->stats.rx_frame_errors++;
			if (len_stat & DMADESC_OV_MASK)
				dev->stats.rx_fifo_errors++;
			continue;
		}

		/* valid packet */
		skb = priv->rx_skb[desc_idx];
		len = (len_stat & DMADESC_LENGTH_MASK) >> DMADESC_LENGTH_SHIFT;
		/* don't include FCS */
		len -= 4;

		if (len < copybreak) {
			struct sk_buff *nskb;

			nskb = napi_alloc_skb(&priv->napi, len);
			if (!nskb) {
				/* forget packet, just rearm desc */
				dev->stats.rx_dropped++;
				continue;
			}

			dma_sync_single_for_cpu(kdev, desc->address,
						len, DMA_FROM_DEVICE);
			memcpy(nskb->data, skb->data, len);
			dma_sync_single_for_device(kdev, desc->address,
						   len, DMA_FROM_DEVICE);
			skb = nskb;
		} else {
			dma_unmap_single(&priv->pdev->dev, desc->address,
					 priv->rx_skb_size, DMA_FROM_DEVICE);
			priv->rx_skb[desc_idx] = NULL;
		}

		skb_put(skb, len);
		skb->protocol = eth_type_trans(skb, dev);
		dev->stats.rx_packets++;
		dev->stats.rx_bytes += len;
		netif_receive_skb(skb);

	} while (--budget > 0);

	if (processed || !priv->rx_desc_count) {
		bcm_enet_refill_rx(dev);

		/* kick rx dma */
		enet_dmac_writel(priv, priv->dma_chan_en_mask,
					 ENETDMAC_CHANCFG, priv->rx_chan);
	}

	return processed;
}


/*
 * try to or force reclaim of transmitted buffers
 */
static int bcm_enet_tx_reclaim(struct net_device *dev, int force)
{
	struct bcm_enet_priv *priv;
	int released;

	priv = netdev_priv(dev);
	released = 0;

	while (priv->tx_desc_count < priv->tx_ring_size) {
		struct bcm_enet_desc *desc;
		struct sk_buff *skb;

		/* We run in a bh and fight against start_xmit, which
		 * is called with bh disabled  */
		spin_lock(&priv->tx_lock);

		desc = &priv->tx_desc_cpu[priv->tx_dirty_desc];

		if (!force && (desc->len_stat & DMADESC_OWNER_MASK)) {
			spin_unlock(&priv->tx_lock);
			break;
		}

		/* ensure other field of the descriptor were not read
		 * before we checked ownership */
		rmb();

		skb = priv->tx_skb[priv->tx_dirty_desc];
		priv->tx_skb[priv->tx_dirty_desc] = NULL;
		dma_unmap_single(&priv->pdev->dev, desc->address, skb->len,
				 DMA_TO_DEVICE);

		priv->tx_dirty_desc++;
		if (priv->tx_dirty_desc == priv->tx_ring_size)
			priv->tx_dirty_desc = 0;
		priv->tx_desc_count++;

		spin_unlock(&priv->tx_lock);

		if (desc->len_stat & DMADESC_UNDER_MASK)
			dev->stats.tx_errors++;

		dev_kfree_skb(skb);
		released++;
	}

	if (netif_queue_stopped(dev) && released)
		netif_wake_queue(dev);

	return released;
}

/*
 * poll func, called by network core
 */
static int bcm_enet_poll(struct napi_struct *napi, int budget)
{
	struct bcm_enet_priv *priv;
	struct net_device *dev;
	int rx_work_done;

	priv = container_of(napi, struct bcm_enet_priv, napi);
	dev = priv->net_dev;

	/* ack interrupts */
	enet_dmac_writel(priv, priv->dma_chan_int_mask,
			 ENETDMAC_IR, priv->rx_chan);
	enet_dmac_writel(priv, priv->dma_chan_int_mask,
			 ENETDMAC_IR, priv->tx_chan);

	/* reclaim sent skb */
	bcm_enet_tx_reclaim(dev, 0);

	spin_lock(&priv->rx_lock);
	rx_work_done = bcm_enet_receive_queue(dev, budget);
	spin_unlock(&priv->rx_lock);

	if (rx_work_done >= budget) {
		/* rx queue is not yet empty/clean */
		return rx_work_done;
	}

	/* no more packet in rx/tx queue, remove device from poll
	 * queue */
	napi_complete(napi);

	/* restore rx/tx interrupt */
	enet_dmac_writel(priv, priv->dma_chan_int_mask,
			 ENETDMAC_IRMASK, priv->rx_chan);
	enet_dmac_writel(priv, priv->dma_chan_int_mask,
			 ENETDMAC_IRMASK, priv->tx_chan);

	return rx_work_done;
}

/*
 * mac interrupt handler
 */
static irqreturn_t bcm_enet_isr_mac(int irq, void *dev_id)
{
	struct net_device *dev;
	struct bcm_enet_priv *priv;
	u32 stat;

	dev = dev_id;
	priv = netdev_priv(dev);

	stat = enet_readl(priv, ENET_IR_REG);
	if (!(stat & ENET_IR_MIB))
		return IRQ_NONE;

	/* clear & mask interrupt */
	enet_writel(priv, ENET_IR_MIB, ENET_IR_REG);
	enet_writel(priv, 0, ENET_IRMASK_REG);

	/* read mib registers in workqueue */
	schedule_work(&priv->mib_update_task);

	return IRQ_HANDLED;
}

/*
 * rx/tx dma interrupt handler
 */
static irqreturn_t bcm_enet_isr_dma(int irq, void *dev_id)
{
	struct net_device *dev;
	struct bcm_enet_priv *priv;

	dev = dev_id;
	priv = netdev_priv(dev);

	/* mask rx/tx interrupts */
	enet_dmac_writel(priv, 0, ENETDMAC_IRMASK, priv->rx_chan);
	enet_dmac_writel(priv, 0, ENETDMAC_IRMASK, priv->tx_chan);

	napi_schedule(&priv->napi);

	return IRQ_HANDLED;
}

/*
 * tx request callback
 */
static int bcm_enet_start_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct bcm_enet_priv *priv;
	struct bcm_enet_desc *desc;
	u32 len_stat;
	int ret;

	priv = netdev_priv(dev);

	/* lock against tx reclaim */
	spin_lock(&priv->tx_lock);

	/* make sure  the tx hw queue  is not full,  should not happen
	 * since we stop queue before it's the case */
	if (unlikely(!priv->tx_desc_count)) {
		netif_stop_queue(dev);
		dev_err(&priv->pdev->dev, "xmit called with no tx desc "
			"available?\n");
		ret = NETDEV_TX_BUSY;
		goto out_unlock;
	}

	/* pad small packets sent on a switch device */
	if (priv->enet_is_sw && skb->len < 64) {
		int needed = 64 - skb->len;
		char *data;

		if (unlikely(skb_tailroom(skb) < needed)) {
			struct sk_buff *nskb;

			nskb = skb_copy_expand(skb, 0, needed, GFP_ATOMIC);
			if (!nskb) {
				ret = NETDEV_TX_BUSY;
				goto out_unlock;
			}
			dev_kfree_skb(skb);
			skb = nskb;
		}
		data = skb_put(skb, needed);
		memset(data, 0, needed);
	}

	/* point to the next available desc */
	desc = &priv->tx_desc_cpu[priv->tx_curr_desc];
	priv->tx_skb[priv->tx_curr_desc] = skb;

	/* fill descriptor */
	desc->address = dma_map_single(&priv->pdev->dev, skb->data, skb->len,
				       DMA_TO_DEVICE);

	len_stat = (skb->len << DMADESC_LENGTH_SHIFT) & DMADESC_LENGTH_MASK;
	len_stat |= (DMADESC_ESOP_MASK >> priv->dma_desc_shift) |
		DMADESC_APPEND_CRC |
		DMADESC_OWNER_MASK;

	priv->tx_curr_desc++;
	if (priv->tx_curr_desc == priv->tx_ring_size) {
		priv->tx_curr_desc = 0;
		len_stat |= (DMADESC_WRAP_MASK >> priv->dma_desc_shift);
	}
	priv->tx_desc_count--;

	/* dma might be already polling, make sure we update desc
	 * fields in correct order */
	wmb();
	desc->len_stat = len_stat;
	wmb();

	/* kick tx dma */
	enet_dmac_writel(priv, priv->dma_chan_en_mask,
				 ENETDMAC_CHANCFG, priv->tx_chan);

	/* stop queue if no more desc available */
	if (!priv->tx_desc_count)
		netif_stop_queue(dev);

	dev->stats.tx_bytes += skb->len;
	dev->stats.tx_packets++;
	ret = NETDEV_TX_OK;

out_unlock:
	spin_unlock(&priv->tx_lock);
	return ret;
}

/*
 * Change the interface's mac address.
 */
static int bcm_enet_set_mac_address(struct net_device *dev, void *p)
{
	struct bcm_enet_priv *priv;
	struct sockaddr *addr = p;
	u32 val;

	priv = netdev_priv(dev);
	memcpy(dev->dev_addr, addr->sa_data, ETH_ALEN);

	/* use perfect match register 0 to store my mac address */
	val = (dev->dev_addr[2] << 24) | (dev->dev_addr[3] << 16) |
		(dev->dev_addr[4] << 8) | dev->dev_addr[5];
	enet_writel(priv, val, ENET_PML_REG(0));

	val = (dev->dev_addr[0] << 8 | dev->dev_addr[1]);
	val |= ENET_PMH_DATAVALID_MASK;
	enet_writel(priv, val, ENET_PMH_REG(0));

	return 0;
}

/*
 * Change rx mode (promiscuous/allmulti) and update multicast list
 */
static void bcm_enet_set_multicast_list(struct net_device *dev)
{
	struct bcm_enet_priv *priv;
	struct netdev_hw_addr *ha;
	u32 val;
	int i;

	priv = netdev_priv(dev);

	val = enet_readl(priv, ENET_RXCFG_REG);

	if (dev->flags & IFF_PROMISC)
		val |= ENET_RXCFG_PROMISC_MASK;
	else
		val &= ~ENET_RXCFG_PROMISC_MASK;

	/* only 3 perfect match registers left, first one is used for
	 * own mac address */
	if ((dev->flags & IFF_ALLMULTI) || netdev_mc_count(dev) > 3)
		val |= ENET_RXCFG_ALLMCAST_MASK;
	else
		val &= ~ENET_RXCFG_ALLMCAST_MASK;

	/* no need to set perfect match registers if we catch all
	 * multicast */
	if (val & ENET_RXCFG_ALLMCAST_MASK) {
		enet_writel(priv, val, ENET_RXCFG_REG);
		return;
	}

	i = 0;
	netdev_for_each_mc_addr(ha, dev) {
		u8 *dmi_addr;
		u32 tmp;

		if (i == 3)
			break;
		/* update perfect match registers */
		dmi_addr = ha->addr;
		tmp = (dmi_addr[2] << 24) | (dmi_addr[3] << 16) |
			(dmi_addr[4] << 8) | dmi_addr[5];
		enet_writel(priv, tmp, ENET_PML_REG(i + 1));

		tmp = (dmi_addr[0] << 8 | dmi_addr[1]);
		tmp |= ENET_PMH_DATAVALID_MASK;
		enet_writel(priv, tmp, ENET_PMH_REG(i++ + 1));
	}

	for (; i < 3; i++) {
		enet_writel(priv, 0, ENET_PML_REG(i + 1));
		enet_writel(priv, 0, ENET_PMH_REG(i + 1));
	}

	enet_writel(priv, val, ENET_RXCFG_REG);
}

/*
 * set mac duplex parameters
 */
static void bcm_enet_set_duplex(struct bcm_enet_priv *priv, int fullduplex)
{
	u32 val;

	val = enet_readl(priv, ENET_TXCTL_REG);
	if (fullduplex)
		val |= ENET_TXCTL_FD_MASK;
	else
		val &= ~ENET_TXCTL_FD_MASK;
	enet_writel(priv, val, ENET_TXCTL_REG);
}

/*
 * set mac flow control parameters
 */
static void bcm_enet_set_flow(struct bcm_enet_priv *priv, int rx_en, int tx_en)
{
	u32 val;

	/* rx flow control (pause frame handling) */
	val = enet_readl(priv, ENET_RXCFG_REG);
	if (rx_en)
		val |= ENET_RXCFG_ENFLOW_MASK;
	else
		val &= ~ENET_RXCFG_ENFLOW_MASK;
	enet_writel(priv, val, ENET_RXCFG_REG);

	if (!priv->dma_has_sram)
		return;

	/* tx flow control (pause frame generation) */
	val = enet_dma_readl(priv, ENETDMA_CFG_REG);
	if (tx_en)
		val |= ENETDMA_CFG_FLOWCH_MASK(priv->rx_chan);
	else
		val &= ~ENETDMA_CFG_FLOWCH_MASK(priv->rx_chan);
	enet_dma_writel(priv, val, ENETDMA_CFG_REG);
}

/*
 * link changed callback (from phylib)
 */
static void bcm_enet_adjust_phy_link(struct net_device *dev)
{
	struct bcm_enet_priv *priv;
	struct phy_device *phydev;
	int status_changed;

	priv = netdev_priv(dev);
	phydev = dev->phydev;
	status_changed = 0;

	if (priv->old_link != phydev->link) {
		status_changed = 1;
		priv->old_link = phydev->link;
	}

	/* reflect duplex change in mac configuration */
	if (phydev->link && phydev->duplex != priv->old_duplex) {
		bcm_enet_set_duplex(priv,
				    (phydev->duplex == DUPLEX_FULL) ? 1 : 0);
		status_changed = 1;
		priv->old_duplex = phydev->duplex;
	}

	/* enable flow control if remote advertise it (trust phylib to
	 * check that duplex is full */
	if (phydev->link && phydev->pause != priv->old_pause) {
		int rx_pause_en, tx_pause_en;

		if (phydev->pause) {
			/* pause was advertised by lpa and us */
			rx_pause_en = 1;
			tx_pause_en = 1;
		} else if (!priv->pause_auto) {
			/* pause setting overrided by user */
			rx_pause_en = priv->pause_rx;
			tx_pause_en = priv->pause_tx;
		} else {
			rx_pause_en = 0;
			tx_pause_en = 0;
		}

		bcm_enet_set_flow(priv, rx_pause_en, tx_pause_en);
		status_changed = 1;
		priv->old_pause = phydev->pause;
	}

	if (status_changed) {
		pr_info("%s: link %s", dev->name, phydev->link ?
			"UP" : "DOWN");
		if (phydev->link)
			pr_cont(" - %d/%s - flow control %s", phydev->speed,
			       DUPLEX_FULL == phydev->duplex ? "full" : "half",
			       phydev->pause == 1 ? "rx&tx" : "off");

		pr_cont("\n");
	}
}

/*
 * link changed callback (if phylib is not used)
 */
static void bcm_enet_adjust_link(struct net_device *dev)
{
	struct bcm_enet_priv *priv;

	priv = netdev_priv(dev);
	bcm_enet_set_duplex(priv, priv->force_duplex_full);
	bcm_enet_set_flow(priv, priv->pause_rx, priv->pause_tx);
	netif_carrier_on(dev);

	pr_info("%s: link forced UP - %d/%s - flow control %s/%s\n",
		dev->name,
		priv->force_speed_100 ? 100 : 10,
		priv->force_duplex_full ? "full" : "half",
		priv->pause_rx ? "rx" : "off",
		priv->pause_tx ? "tx" : "off");
}

/*
 * open callback, allocate dma rings & buffers and start rx operation
 */
static int bcm_enet_open(struct net_device *dev)
{
	struct bcm_enet_priv *priv;
	struct sockaddr addr;
	struct device *kdev;
	struct phy_device *phydev;
	int i, ret;
	unsigned int size;
	char phy_id[MII_BUS_ID_SIZE + 3];
	void *p;
	u32 val;

	priv = netdev_priv(dev);
	kdev = &priv->pdev->dev;

	if (priv->has_phy) {
		/* connect to PHY */
		snprintf(phy_id, sizeof(phy_id), PHY_ID_FMT,
			 priv->mii_bus->id, priv->phy_id);

		phydev = phy_connect(dev, phy_id, bcm_enet_adjust_phy_link,
				     PHY_INTERFACE_MODE_MII);

		if (IS_ERR(phydev)) {
			dev_err(kdev, "could not attach to PHY\n");
			return PTR_ERR(phydev);
		}

		/* mask with MAC supported features */
		phydev->supported &= (SUPPORTED_10baseT_Half |
				      SUPPORTED_10baseT_Full |
				      SUPPORTED_100baseT_Half |
				      SUPPORTED_100baseT_Full |
				      SUPPORTED_Autoneg |
				      SUPPORTED_Pause |
				      SUPPORTED_MII);
		phydev->advertising = phydev->supported;

		if (priv->pause_auto && priv->pause_rx && priv->pause_tx)
			phydev->advertising |= SUPPORTED_Pause;
		else
			phydev->advertising &= ~SUPPORTED_Pause;

		phy_attached_info(phydev);

		priv->old_link = 0;
		priv->old_duplex = -1;
		priv->old_pause = -1;
	} else {
		phydev = NULL;
	}

	/* mask all interrupts and request them */
	enet_writel(priv, 0, ENET_IRMASK_REG);
	enet_dmac_writel(priv, 0, ENETDMAC_IRMASK, priv->rx_chan);
	enet_dmac_writel(priv, 0, ENETDMAC_IRMASK, priv->tx_chan);

	ret = request_irq(dev->irq, bcm_enet_isr_mac, 0, dev->name, dev);
	if (ret)
		goto out_phy_disconnect;

	ret = request_irq(priv->irq_rx, bcm_enet_isr_dma, 0,
			  dev->name, dev);
	if (ret)
		goto out_freeirq;

	ret = request_irq(priv->irq_tx, bcm_enet_isr_dma,
			  0, dev->name, dev);
	if (ret)
		goto out_freeirq_rx;

	/* initialize perfect match registers */
	for (i = 0; i < 4; i++) {
		enet_writel(priv, 0, ENET_PML_REG(i));
		enet_writel(priv, 0, ENET_PMH_REG(i));
	}

	/* write device mac address */
	memcpy(addr.sa_data, dev->dev_addr, ETH_ALEN);
	bcm_enet_set_mac_address(dev, &addr);

	/* allocate rx dma ring */
	size = priv->rx_ring_size * sizeof(struct bcm_enet_desc);
	p = dma_zalloc_coherent(kdev, size, &priv->rx_desc_dma, GFP_KERNEL);
	if (!p) {
		ret = -ENOMEM;
		goto out_freeirq_tx;
	}

	priv->rx_desc_alloc_size = size;
	priv->rx_desc_cpu = p;

	/* allocate tx dma ring */
	size = priv->tx_ring_size * sizeof(struct bcm_enet_desc);
	p = dma_zalloc_coherent(kdev, size, &priv->tx_desc_dma, GFP_KERNEL);
	if (!p) {
		ret = -ENOMEM;
		goto out_free_rx_ring;
	}

	priv->tx_desc_alloc_size = size;
	priv->tx_desc_cpu = p;

	priv->tx_skb = kcalloc(priv->tx_ring_size, sizeof(struct sk_buff *),
			       GFP_KERNEL);
	if (!priv->tx_skb) {
		ret = -ENOMEM;
		goto out_free_tx_ring;
	}

	priv->tx_desc_count = priv->tx_ring_size;
	priv->tx_dirty_desc = 0;
	priv->tx_curr_desc = 0;
	spin_lock_init(&priv->tx_lock);

	/* init & fill rx ring with skbs */
	priv->rx_skb = kcalloc(priv->rx_ring_size, sizeof(struct sk_buff *),
			       GFP_KERNEL);
	if (!priv->rx_skb) {
		ret = -ENOMEM;
		goto out_free_tx_skb;
	}

	priv->rx_desc_count = 0;
	priv->rx_dirty_desc = 0;
	priv->rx_curr_desc = 0;

	/* initialize flow control buffer allocation */
	if (priv->dma_has_sram)
		enet_dma_writel(priv, ENETDMA_BUFALLOC_FORCE_MASK | 0,
				ENETDMA_BUFALLOC_REG(priv->rx_chan));
	else
		enet_dmac_writel(priv, ENETDMA_BUFALLOC_FORCE_MASK | 0,
				ENETDMAC_BUFALLOC, priv->rx_chan);

	if (bcm_enet_refill_rx(dev)) {
		dev_err(kdev, "cannot allocate rx skb queue\n");
		ret = -ENOMEM;
		goto out;
	}

	/* write rx & tx ring addresses */
	if (priv->dma_has_sram) {
		enet_dmas_writel(priv, priv->rx_desc_dma,
				 ENETDMAS_RSTART_REG, priv->rx_chan);
		enet_dmas_writel(priv, priv->tx_desc_dma,
			 ENETDMAS_RSTART_REG, priv->tx_chan);
	} else {
		enet_dmac_writel(priv, priv->rx_desc_dma,
				ENETDMAC_RSTART, priv->rx_chan);
		enet_dmac_writel(priv, priv->tx_desc_dma,
				ENETDMAC_RSTART, priv->tx_chan);
	}

	/* clear remaining state ram for rx & tx channel */
	if (priv->dma_has_sram) {
		enet_dmas_writel(priv, 0, ENETDMAS_SRAM2_REG, priv->rx_chan);
		enet_dmas_writel(priv, 0, ENETDMAS_SRAM2_REG, priv->tx_chan);
		enet_dmas_writel(priv, 0, ENETDMAS_SRAM3_REG, priv->rx_chan);
		enet_dmas_writel(priv, 0, ENETDMAS_SRAM3_REG, priv->tx_chan);
		enet_dmas_writel(priv, 0, ENETDMAS_SRAM4_REG, priv->rx_chan);
		enet_dmas_writel(priv, 0, ENETDMAS_SRAM4_REG, priv->tx_chan);
	} else {
		enet_dmac_writel(priv, 0, ENETDMAC_FC, priv->rx_chan);
		enet_dmac_writel(priv, 0, ENETDMAC_FC, priv->tx_chan);
	}

	/* set max rx/tx length */
	enet_writel(priv, priv->hw_mtu, ENET_RXMAXLEN_REG);
	enet_writel(priv, priv->hw_mtu, ENET_TXMAXLEN_REG);

	/* set dma maximum burst len */
	enet_dmac_writel(priv, priv->dma_maxburst,
			 ENETDMAC_MAXBURST, priv->rx_chan);
	enet_dmac_writel(priv, priv->dma_maxburst,
			 ENETDMAC_MAXBURST, priv->tx_chan);

	/* set correct transmit fifo watermark */
	enet_writel(priv, BCMENET_TX_FIFO_TRESH, ENET_TXWMARK_REG);

	/* set flow control low/high threshold to 1/3 / 2/3 */
	if (priv->dma_has_sram) {
		val = priv->rx_ring_size / 3;
		enet_dma_writel(priv, val, ENETDMA_FLOWCL_REG(priv->rx_chan));
		val = (priv->rx_ring_size * 2) / 3;
		enet_dma_writel(priv, val, ENETDMA_FLOWCH_REG(priv->rx_chan));
	} else {
		enet_dmac_writel(priv, 5, ENETDMAC_FC, priv->rx_chan);
		enet_dmac_writel(priv, priv->rx_ring_size, ENETDMAC_LEN, priv->rx_chan);
		enet_dmac_writel(priv, priv->tx_ring_size, ENETDMAC_LEN, priv->tx_chan);
	}

	/* all set, enable mac and interrupts, start dma engine and
	 * kick rx dma channel */
	wmb();
	val = enet_readl(priv, ENET_CTL_REG);
	val |= ENET_CTL_ENABLE_MASK;
	enet_writel(priv, val, ENET_CTL_REG);
	enet_dma_writel(priv, ENETDMA_CFG_EN_MASK, ENETDMA_CFG_REG);
	enet_dmac_writel(priv, priv->dma_chan_en_mask,
			 ENETDMAC_CHANCFG, priv->rx_chan);

	/* watch "mib counters about to overflow" interrupt */
	enet_writel(priv, ENET_IR_MIB, ENET_IR_REG);
	enet_writel(priv, ENET_IR_MIB, ENET_IRMASK_REG);

	/* watch "packet transferred" interrupt in rx and tx */
	enet_dmac_writel(priv, priv->dma_chan_int_mask,
			 ENETDMAC_IR, priv->rx_chan);
	enet_dmac_writel(priv, priv->dma_chan_int_mask,
			 ENETDMAC_IR, priv->tx_chan);

	/* make sure we enable napi before rx interrupt  */
	napi_enable(&priv->napi);

	enet_dmac_writel(priv, priv->dma_chan_int_mask,
			 ENETDMAC_IRMASK, priv->rx_chan);
	enet_dmac_writel(priv, priv->dma_chan_int_mask,
			 ENETDMAC_IRMASK, priv->tx_chan);

	if (phydev)
		phy_start(phydev);
	else
		bcm_enet_adjust_link(dev);

	netif_start_queue(dev);
	return 0;

out:
	for (i = 0; i < priv->rx_ring_size; i++) {
		struct bcm_enet_desc *desc;

		if (!priv->rx_skb[i])
			continue;

		desc = &priv->rx_desc_cpu[i];
		dma_unmap_single(kdev, desc->address, priv->rx_skb_size,
				 DMA_FROM_DEVICE);
		kfree_skb(priv->rx_skb[i]);
	}
	kfree(priv->rx_skb);

out_free_tx_skb:
	kfree(priv->tx_skb);

out_free_tx_ring:
	dma_free_coherent(kdev, priv->tx_desc_alloc_size,
			  priv->tx_desc_cpu, priv->tx_desc_dma);

out_free_rx_ring:
	dma_free_coherent(kdev, priv->rx_desc_alloc_size,
			  priv->rx_desc_cpu, priv->rx_desc_dma);

out_freeirq_tx:
	free_irq(priv->irq_tx, dev);

out_freeirq_rx:
	free_irq(priv->irq_rx, dev);

out_freeirq:
	free_irq(dev->irq, dev);

out_phy_disconnect:
	if (phydev)
		phy_disconnect(phydev);

	return ret;
}

/*
 * disable mac
 */
static void bcm_enet_disable_mac(struct bcm_enet_priv *priv)
{
	int limit;
	u32 val;

	val = enet_readl(priv, ENET_CTL_REG);
	val |= ENET_CTL_DISABLE_MASK;
	enet_writel(priv, val, ENET_CTL_REG);

	limit = 1000;
	do {
		u32 val;

		val = enet_readl(priv, ENET_CTL_REG);
		if (!(val & ENET_CTL_DISABLE_MASK))
			break;
		udelay(1);
	} while (limit--);
}

/*
 * disable dma in given channel
 */
static void bcm_enet_disable_dma(struct bcm_enet_priv *priv, int chan)
{
	int limit;

	enet_dmac_writel(priv, 0, ENETDMAC_CHANCFG, chan);

	limit = 1000;
	do {
		u32 val;

		val = enet_dmac_readl(priv, ENETDMAC_CHANCFG, chan);
		if (!(val & ENETDMAC_CHANCFG_EN_MASK))
			break;
		udelay(1);
	} while (limit--);
}

/*
 * stop callback
 */
static int bcm_enet_stop(struct net_device *dev)
{
	struct bcm_enet_priv *priv;
	struct device *kdev;
	int i;

	priv = netdev_priv(dev);
	kdev = &priv->pdev->dev;

	netif_stop_queue(dev);
	napi_disable(&priv->napi);
	if (priv->has_phy)
		phy_stop(dev->phydev);
	del_timer_sync(&priv->rx_timeout);

	/* mask all interrupts */
	enet_writel(priv, 0, ENET_IRMASK_REG);
	enet_dmac_writel(priv, 0, ENETDMAC_IRMASK, priv->rx_chan);
	enet_dmac_writel(priv, 0, ENETDMAC_IRMASK, priv->tx_chan);

	/* make sure no mib update is scheduled */
	cancel_work_sync(&priv->mib_update_task);

	/* disable dma & mac */
	bcm_enet_disable_dma(priv, priv->tx_chan);
	bcm_enet_disable_dma(priv, priv->rx_chan);
	bcm_enet_disable_mac(priv);

	/* force reclaim of all tx buffers */
	bcm_enet_tx_reclaim(dev, 1);

	/* free the rx skb ring */
	for (i = 0; i < priv->rx_ring_size; i++) {
		struct bcm_enet_desc *desc;

		if (!priv->rx_skb[i])
			continue;

		desc = &priv->rx_desc_cpu[i];
		dma_unmap_single(kdev, desc->address, priv->rx_skb_size,
				 DMA_FROM_DEVICE);
		kfree_skb(priv->rx_skb[i]);
	}

	/* free remaining allocated memory */
	kfree(priv->rx_skb);
	kfree(priv->tx_skb);
	dma_free_coherent(kdev, priv->rx_desc_alloc_size,
			  priv->rx_desc_cpu, priv->rx_desc_dma);
	dma_free_coherent(kdev, priv->tx_desc_alloc_size,
			  priv->tx_desc_cpu, priv->tx_desc_dma);
	free_irq(priv->irq_tx, dev);
	free_irq(priv->irq_rx, dev);
	free_irq(dev->irq, dev);

	/* release phy */
	if (priv->has_phy)
		phy_disconnect(dev->phydev);

	return 0;
}

/*
 * ethtool callbacks
 */
struct bcm_enet_stats {
	char stat_string[ETH_GSTRING_LEN];
	int sizeof_stat;
	int stat_offset;
	int mib_reg;
};

#define GEN_STAT(m) sizeof(((struct bcm_enet_priv *)0)->m),		\
		     offsetof(struct bcm_enet_priv, m)
#define DEV_STAT(m) sizeof(((struct net_device_stats *)0)->m),		\
		     offsetof(struct net_device_stats, m)

static const struct bcm_enet_stats bcm_enet_gstrings_stats[] = {
	{ "rx_packets", DEV_STAT(rx_packets), -1 },
	{ "tx_packets",	DEV_STAT(tx_packets), -1 },
	{ "rx_bytes", DEV_STAT(rx_bytes), -1 },
	{ "tx_bytes", DEV_STAT(tx_bytes), -1 },
	{ "rx_errors", DEV_STAT(rx_errors), -1 },
	{ "tx_errors", DEV_STAT(tx_errors), -1 },
	{ "rx_dropped",	DEV_STAT(rx_dropped), -1 },
	{ "tx_dropped",	DEV_STAT(tx_dropped), -1 },

	{ "rx_good_octets", GEN_STAT(mib.rx_gd_octets), ETH_MIB_RX_GD_OCTETS},
	{ "rx_good_pkts", GEN_STAT(mib.rx_gd_pkts), ETH_MIB_RX_GD_PKTS },
	{ "rx_broadcast", GEN_STAT(mib.rx_brdcast), ETH_MIB_RX_BRDCAST },
	{ "rx_multicast", GEN_STAT(mib.rx_mult), ETH_MIB_RX_MULT },
	{ "rx_64_octets", GEN_STAT(mib.rx_64), ETH_MIB_RX_64 },
	{ "rx_65_127_oct", GEN_STAT(mib.rx_65_127), ETH_MIB_RX_65_127 },
	{ "rx_128_255_oct", GEN_STAT(mib.rx_128_255), ETH_MIB_RX_128_255 },
	{ "rx_256_511_oct", GEN_STAT(mib.rx_256_511), ETH_MIB_RX_256_511 },
	{ "rx_512_1023_oct", GEN_STAT(mib.rx_512_1023), ETH_MIB_RX_512_1023 },
	{ "rx_1024_max_oct", GEN_STAT(mib.rx_1024_max), ETH_MIB_RX_1024_MAX },
	{ "rx_jabber", GEN_STAT(mib.rx_jab), ETH_MIB_RX_JAB },
	{ "rx_oversize", GEN_STAT(mib.rx_ovr), ETH_MIB_RX_OVR },
	{ "rx_fragment", GEN_STAT(mib.rx_frag), ETH_MIB_RX_FRAG },
	{ "rx_dropped",	GEN_STAT(mib.rx_drop), ETH_MIB_RX_DROP },
	{ "rx_crc_align", GEN_STAT(mib.rx_crc_align), ETH_MIB_RX_CRC_ALIGN },
	{ "rx_undersize", GEN_STAT(mib.rx_und), ETH_MIB_RX_UND },
	{ "rx_crc", GEN_STAT(mib.rx_crc), ETH_MIB_RX_CRC },
	{ "rx_align", GEN_STAT(mib.rx_align), ETH_MIB_RX_ALIGN },
	{ "rx_symbol_error", GEN_STAT(mib.rx_sym), ETH_MIB_RX_SYM },
	{ "rx_pause", GEN_STAT(mib.rx_pause), ETH_MIB_RX_PAUSE },
	{ "rx_control", GEN_STAT(mib.rx_cntrl), ETH_MIB_RX_CNTRL },

	{ "tx_good_octets", GEN_STAT(mib.tx_gd_octets), ETH_MIB_TX_GD_OCTETS },
	{ "tx_good_pkts", GEN_STAT(mib.tx_gd_pkts), ETH_MIB_TX_GD_PKTS },
	{ "tx_broadcast", GEN_STAT(mib.tx_brdcast), ETH_MIB_TX_BRDCAST },
	{ "tx_multicast", GEN_STAT(mib.tx_mult), ETH_MIB_TX_MULT },
	{ "tx_64_oct", GEN_STAT(mib.tx_64), ETH_MIB_TX_64 },
	{ "tx_65_127_oct", GEN_STAT(mib.tx_65_127), ETH_MIB_TX_65_127 },
	{ "tx_128_255_oct", GEN_STAT(mib.tx_128_255), ETH_MIB_TX_128_255 },
	{ "tx_256_511_oct", GEN_STAT(mib.tx_256_511), ETH_MIB_TX_256_511 },
	{ "tx_512_1023_oct", GEN_STAT(mib.tx_512_1023), ETH_MIB_TX_512_1023},
	{ "tx_1024_max_oct", GEN_STAT(mib.tx_1024_max), ETH_MIB_TX_1024_MAX },
	{ "tx_jabber", GEN_STAT(mib.tx_jab), ETH_MIB_TX_JAB },
	{ "tx_oversize", GEN_STAT(mib.tx_ovr), ETH_MIB_TX_OVR },
	{ "tx_fragment", GEN_STAT(mib.tx_frag), ETH_MIB_TX_FRAG },
	{ "tx_underrun", GEN_STAT(mib.tx_underrun), ETH_MIB_TX_UNDERRUN },
	{ "tx_collisions", GEN_STAT(mib.tx_col), ETH_MIB_TX_COL },
	{ "tx_single_collision", GEN_STAT(mib.tx_1_col), ETH_MIB_TX_1_COL },
	{ "tx_multiple_collision", GEN_STAT(mib.tx_m_col), ETH_MIB_TX_M_COL },
	{ "tx_excess_collision", GEN_STAT(mib.tx_ex_col), ETH_MIB_TX_EX_COL },
	{ "tx_late_collision", GEN_STAT(mib.tx_late), ETH_MIB_TX_LATE },
	{ "tx_deferred", GEN_STAT(mib.tx_def), ETH_MIB_TX_DEF },
	{ "tx_carrier_sense", GEN_STAT(mib.tx_crs), ETH_MIB_TX_CRS },
	{ "tx_pause", GEN_STAT(mib.tx_pause), ETH_MIB_TX_PAUSE },

};

#define BCM_ENET_STATS_LEN	ARRAY_SIZE(bcm_enet_gstrings_stats)

static const u32 unused_mib_regs[] = {
	ETH_MIB_TX_ALL_OCTETS,
	ETH_MIB_TX_ALL_PKTS,
	ETH_MIB_RX_ALL_OCTETS,
	ETH_MIB_RX_ALL_PKTS,
};


static void bcm_enet_get_drvinfo(struct net_device *netdev,
				 struct ethtool_drvinfo *drvinfo)
{
	strlcpy(drvinfo->driver, bcm_enet_driver_name, sizeof(drvinfo->driver));
	strlcpy(drvinfo->version, bcm_enet_driver_version,
		sizeof(drvinfo->version));
	strlcpy(drvinfo->fw_version, "N/A", sizeof(drvinfo->fw_version));
	strlcpy(drvinfo->bus_info, "bcm63xx", sizeof(drvinfo->bus_info));
}

static int bcm_enet_get_sset_count(struct net_device *netdev,
					int string_set)
{
	switch (string_set) {
	case ETH_SS_STATS:
		return BCM_ENET_STATS_LEN;
	default:
		return -EINVAL;
	}
}

static void bcm_enet_get_strings(struct net_device *netdev,
				 u32 stringset, u8 *data)
{
	int i;

	switch (stringset) {
	case ETH_SS_STATS:
		for (i = 0; i < BCM_ENET_STATS_LEN; i++) {
			memcpy(data + i * ETH_GSTRING_LEN,
			       bcm_enet_gstrings_stats[i].stat_string,
			       ETH_GSTRING_LEN);
		}
		break;
	}
}

static void update_mib_counters(struct bcm_enet_priv *priv)
{
	int i;

	for (i = 0; i < BCM_ENET_STATS_LEN; i++) {
		const struct bcm_enet_stats *s;
		u32 val;
		char *p;

		s = &bcm_enet_gstrings_stats[i];
		if (s->mib_reg == -1)
			continue;

		val = enet_readl(priv, ENET_MIB_REG(s->mib_reg));
		p = (char *)priv + s->stat_offset;

		if (s->sizeof_stat == sizeof(u64))
			*(u64 *)p += val;
		else
			*(u32 *)p += val;
	}

	/* also empty unused mib counters to make sure mib counter
	 * overflow interrupt is cleared */
	for (i = 0; i < ARRAY_SIZE(unused_mib_regs); i++)
		(void)enet_readl(priv, ENET_MIB_REG(unused_mib_regs[i]));
}

static void bcm_enet_update_mib_counters_defer(struct work_struct *t)
{
	struct bcm_enet_priv *priv;

	priv = container_of(t, struct bcm_enet_priv, mib_update_task);
	mutex_lock(&priv->mib_update_lock);
	update_mib_counters(priv);
	mutex_unlock(&priv->mib_update_lock);

	/* reenable mib interrupt */
	if (netif_running(priv->net_dev))
		enet_writel(priv, ENET_IR_MIB, ENET_IRMASK_REG);
}

static void bcm_enet_get_ethtool_stats(struct net_device *netdev,
				       struct ethtool_stats *stats,
				       u64 *data)
{
	struct bcm_enet_priv *priv;
	int i;

	priv = netdev_priv(netdev);

	mutex_lock(&priv->mib_update_lock);
	update_mib_counters(priv);

	for (i = 0; i < BCM_ENET_STATS_LEN; i++) {
		const struct bcm_enet_stats *s;
		char *p;

		s = &bcm_enet_gstrings_stats[i];
		if (s->mib_reg == -1)
			p = (char *)&netdev->stats;
		else
			p = (char *)priv;
		p += s->stat_offset;
		data[i] = (s->sizeof_stat == sizeof(u64)) ?
			*(u64 *)p : *(u32 *)p;
	}
	mutex_unlock(&priv->mib_update_lock);
}

static int bcm_enet_nway_reset(struct net_device *dev)
{
	struct bcm_enet_priv *priv;

	priv = netdev_priv(dev);
	if (priv->has_phy)
		return phy_ethtool_nway_reset(dev);

	return -EOPNOTSUPP;
}

static int bcm_enet_get_link_ksettings(struct net_device *dev,
				       struct ethtool_link_ksettings *cmd)
{
	struct bcm_enet_priv *priv;
	u32 supported, advertising;

	priv = netdev_priv(dev);

	if (priv->has_phy) {
		if (!dev->phydev)
			return -ENODEV;
		return phy_ethtool_ksettings_get(dev->phydev, cmd);
	} else {
		cmd->base.autoneg = 0;
		cmd->base.speed = (priv->force_speed_100) ?
			SPEED_100 : SPEED_10;
		cmd->base.duplex = (priv->force_duplex_full) ?
			DUPLEX_FULL : DUPLEX_HALF;
		supported = ADVERTISED_10baseT_Half |
			ADVERTISED_10baseT_Full |
			ADVERTISED_100baseT_Half |
			ADVERTISED_100baseT_Full;
		advertising = 0;
		ethtool_convert_legacy_u32_to_link_mode(
			cmd->link_modes.supported, supported);
		ethtool_convert_legacy_u32_to_link_mode(
			cmd->link_modes.advertising, advertising);
		cmd->base.port = PORT_MII;
	}
	return 0;
}

static int bcm_enet_set_link_ksettings(struct net_device *dev,
				       const struct ethtool_link_ksettings *cmd)
{
	struct bcm_enet_priv *priv;

	priv = netdev_priv(dev);
	if (priv->has_phy) {
		if (!dev->phydev)
			return -ENODEV;
		return phy_ethtool_ksettings_set(dev->phydev, cmd);
	} else {

		if (cmd->base.autoneg ||
		    (cmd->base.speed != SPEED_100 &&
		     cmd->base.speed != SPEED_10) ||
		    cmd->base.port != PORT_MII)
			return -EINVAL;

		priv->force_speed_100 =
			(cmd->base.speed == SPEED_100) ? 1 : 0;
		priv->force_duplex_full =
			(cmd->base.duplex == DUPLEX_FULL) ? 1 : 0;

		if (netif_running(dev))
			bcm_enet_adjust_link(dev);
		return 0;
	}
}

static void bcm_enet_get_ringparam(struct net_device *dev,
				   struct ethtool_ringparam *ering)
{
	struct bcm_enet_priv *priv;

	priv = netdev_priv(dev);

	/* rx/tx ring is actually only limited by memory */
	ering->rx_max_pending = 8192;
	ering->tx_max_pending = 8192;
	ering->rx_pending = priv->rx_ring_size;
	ering->tx_pending = priv->tx_ring_size;
}

static int bcm_enet_set_ringparam(struct net_device *dev,
				  struct ethtool_ringparam *ering)
{
	struct bcm_enet_priv *priv;
	int was_running;

	priv = netdev_priv(dev);

	was_running = 0;
	if (netif_running(dev)) {
		bcm_enet_stop(dev);
		was_running = 1;
	}

	priv->rx_ring_size = ering->rx_pending;
	priv->tx_ring_size = ering->tx_pending;

	if (was_running) {
		int err;

		err = bcm_enet_open(dev);
		if (err)
			dev_close(dev);
		else
			bcm_enet_set_multicast_list(dev);
	}
	return 0;
}

static void bcm_enet_get_pauseparam(struct net_device *dev,
				    struct ethtool_pauseparam *ecmd)
{
	struct bcm_enet_priv *priv;

	priv = netdev_priv(dev);
	ecmd->autoneg = priv->pause_auto;
	ecmd->rx_pause = priv->pause_rx;
	ecmd->tx_pause = priv->pause_tx;
}

static int bcm_enet_set_pauseparam(struct net_device *dev,
				   struct ethtool_pauseparam *ecmd)
{
	struct bcm_enet_priv *priv;

	priv = netdev_priv(dev);

	if (priv->has_phy) {
		if (ecmd->autoneg && (ecmd->rx_pause != ecmd->tx_pause)) {
			/* asymetric pause mode not supported,
			 * actually possible but integrated PHY has RO
			 * asym_pause bit */
			return -EINVAL;
		}
	} else {
		/* no pause autoneg on direct mii connection */
		if (ecmd->autoneg)
			return -EINVAL;
	}

	priv->pause_auto = ecmd->autoneg;
	priv->pause_rx = ecmd->rx_pause;
	priv->pause_tx = ecmd->tx_pause;

	return 0;
}

static const struct ethtool_ops bcm_enet_ethtool_ops = {
	.get_strings		= bcm_enet_get_strings,
	.get_sset_count		= bcm_enet_get_sset_count,
	.get_ethtool_stats      = bcm_enet_get_ethtool_stats,
	.nway_reset		= bcm_enet_nway_reset,
	.get_drvinfo		= bcm_enet_get_drvinfo,
	.get_link		= ethtool_op_get_link,
	.get_ringparam		= bcm_enet_get_ringparam,
	.set_ringparam		= bcm_enet_set_ringparam,
	.get_pauseparam		= bcm_enet_get_pauseparam,
	.set_pauseparam		= bcm_enet_set_pauseparam,
	.get_link_ksettings	= bcm_enet_get_link_ksettings,
	.set_link_ksettings	= bcm_enet_set_link_ksettings,
};

static int bcm_enet_ioctl(struct net_device *dev, struct ifreq *rq, int cmd)
{
	struct bcm_enet_priv *priv;

	priv = netdev_priv(dev);
	if (priv->has_phy) {
		if (!dev->phydev)
			return -ENODEV;
		return phy_mii_ioctl(dev->phydev, rq, cmd);
	} else {
		struct mii_if_info mii;

		mii.dev = dev;
		mii.mdio_read = bcm_enet_mdio_read_mii;
		mii.mdio_write = bcm_enet_mdio_write_mii;
		mii.phy_id = 0;
		mii.phy_id_mask = 0x3f;
		mii.reg_num_mask = 0x1f;
		return generic_mii_ioctl(&mii, if_mii(rq), cmd, NULL);
	}
}

/*
 * adjust mtu, can't be called while device is running
 */
static int bcm_enet_change_mtu(struct net_device *dev, int new_mtu)
{
	struct bcm_enet_priv *priv = netdev_priv(dev);
	int actual_mtu = new_mtu;

	if (netif_running(dev))
		return -EBUSY;

	/* add ethernet header + vlan tag size */
	actual_mtu += VLAN_ETH_HLEN;

	/*
	 * setup maximum size before we get overflow mark in
	 * descriptor, note that this will not prevent reception of
	 * big frames, they will be split into multiple buffers
	 * anyway
	 */
	priv->hw_mtu = actual_mtu;

	/*
	 * align rx buffer size to dma burst len, account FCS since
	 * it's appended
	 */
	priv->rx_skb_size = ALIGN(actual_mtu + ETH_FCS_LEN,
				  priv->dma_maxburst * 4);

	dev->mtu = new_mtu;
	return 0;
}

/*
 * preinit hardware to allow mii operation while device is down
 */
static void bcm_enet_hw_preinit(struct bcm_enet_priv *priv)
{
	u32 val;
	int limit;

	/* make sure mac is disabled */
	bcm_enet_disable_mac(priv);

	/* soft reset mac */
	val = ENET_CTL_SRESET_MASK;
	enet_writel(priv, val, ENET_CTL_REG);
	wmb();

	limit = 1000;
	do {
		val = enet_readl(priv, ENET_CTL_REG);
		if (!(val & ENET_CTL_SRESET_MASK))
			break;
		udelay(1);
	} while (limit--);

	/* select correct mii interface */
	val = enet_readl(priv, ENET_CTL_REG);
	if (priv->use_external_mii)
		val |= ENET_CTL_EPHYSEL_MASK;
	else
		val &= ~ENET_CTL_EPHYSEL_MASK;
	enet_writel(priv, val, ENET_CTL_REG);

	/* turn on mdc clock */
	enet_writel(priv, (0x1f << ENET_MIISC_MDCFREQDIV_SHIFT) |
		    ENET_MIISC_PREAMBLEEN_MASK, ENET_MIISC_REG);

	/* set mib counters to self-clear when read */
	val = enet_readl(priv, ENET_MIBCTL_REG);
	val |= ENET_MIBCTL_RDCLEAR_MASK;
	enet_writel(priv, val, ENET_MIBCTL_REG);
}

static const struct net_device_ops bcm_enet_ops = {
	.ndo_open		= bcm_enet_open,
	.ndo_stop		= bcm_enet_stop,
	.ndo_start_xmit		= bcm_enet_start_xmit,
	.ndo_set_mac_address	= bcm_enet_set_mac_address,
	.ndo_set_rx_mode	= bcm_enet_set_multicast_list,
	.ndo_do_ioctl		= bcm_enet_ioctl,
	.ndo_change_mtu		= bcm_enet_change_mtu,
};

/*
 * allocate netdevice, request register memory and register device.
 */
static int bcm_enet_probe(struct platform_device *pdev)
{
	struct bcm_enet_priv *priv;
	struct net_device *dev;
	struct bcm63xx_enet_platform_data *pd;
	struct resource *res_mem, *res_irq, *res_irq_rx, *res_irq_tx;
	struct mii_bus *bus;
	const char *clk_name;
	int i, ret;

	/* stop if shared driver failed, assume driver->probe will be
	 * called in the same order we register devices (correct ?) */
	if (!bcm_enet_shared_base[0])
		return -ENODEV;

	res_irq = platform_get_resource(pdev, IORESOURCE_IRQ, 0);
	res_irq_rx = platform_get_resource(pdev, IORESOURCE_IRQ, 1);
	res_irq_tx = platform_get_resource(pdev, IORESOURCE_IRQ, 2);
	if (!res_irq || !res_irq_rx || !res_irq_tx)
		return -ENODEV;

	ret = 0;
	dev = alloc_etherdev(sizeof(*priv));
	if (!dev)
		return -ENOMEM;
	priv = netdev_priv(dev);

	priv->enet_is_sw = false;
	priv->dma_maxburst = BCMENET_DMA_MAXBURST;

	ret = bcm_enet_change_mtu(dev, dev->mtu);
	if (ret)
		goto out;

	res_mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	priv->base = devm_ioremap_resource(&pdev->dev, res_mem);
	if (IS_ERR(priv->base)) {
		ret = PTR_ERR(priv->base);
		goto out;
	}

	dev->irq = priv->irq = res_irq->start;
	priv->irq_rx = res_irq_rx->start;
	priv->irq_tx = res_irq_tx->start;
	priv->mac_id = pdev->id;

	/* get rx & tx dma channel id for this mac */
	if (priv->mac_id == 0) {
		priv->rx_chan = 0;
		priv->tx_chan = 1;
		clk_name = "enet0";
	} else {
		priv->rx_chan = 2;
		priv->tx_chan = 3;
		clk_name = "enet1";
	}

	priv->mac_clk = clk_get(&pdev->dev, clk_name);
	if (IS_ERR(priv->mac_clk)) {
		ret = PTR_ERR(priv->mac_clk);
		goto out;
	}
	clk_prepare_enable(priv->mac_clk);

	/* initialize default and fetch platform data */
	priv->rx_ring_size = BCMENET_DEF_RX_DESC;
	priv->tx_ring_size = BCMENET_DEF_TX_DESC;

	pd = dev_get_platdata(&pdev->dev);
	if (pd) {
		memcpy(dev->dev_addr, pd->mac_addr, ETH_ALEN);
		priv->has_phy = pd->has_phy;
		priv->phy_id = pd->phy_id;
		priv->has_phy_interrupt = pd->has_phy_interrupt;
		priv->phy_interrupt = pd->phy_interrupt;
		priv->use_external_mii = !pd->use_internal_phy;
		priv->pause_auto = pd->pause_auto;
		priv->pause_rx = pd->pause_rx;
		priv->pause_tx = pd->pause_tx;
		priv->force_duplex_full = pd->force_duplex_full;
		priv->force_speed_100 = pd->force_speed_100;
		priv->dma_chan_en_mask = pd->dma_chan_en_mask;
		priv->dma_chan_int_mask = pd->dma_chan_int_mask;
		priv->dma_chan_width = pd->dma_chan_width;
		priv->dma_has_sram = pd->dma_has_sram;
		priv->dma_desc_shift = pd->dma_desc_shift;
	}

	if (priv->mac_id == 0 && priv->has_phy && !priv->use_external_mii) {
		/* using internal PHY, enable clock */
		priv->phy_clk = clk_get(&pdev->dev, "ephy");
		if (IS_ERR(priv->phy_clk)) {
			ret = PTR_ERR(priv->phy_clk);
			priv->phy_clk = NULL;
			goto out_put_clk_mac;
		}
		clk_prepare_enable(priv->phy_clk);
	}

	/* do minimal hardware init to be able to probe mii bus */
	bcm_enet_hw_preinit(priv);

	/* MII bus registration */
	if (priv->has_phy) {

		priv->mii_bus = mdiobus_alloc();
		if (!priv->mii_bus) {
			ret = -ENOMEM;
			goto out_uninit_hw;
		}

		bus = priv->mii_bus;
		bus->name = "bcm63xx_enet MII bus";
		bus->parent = &pdev->dev;
		bus->priv = priv;
		bus->read = bcm_enet_mdio_read_phylib;
		bus->write = bcm_enet_mdio_write_phylib;
		sprintf(bus->id, "%s-%d", pdev->name, priv->mac_id);

		/* only probe bus where we think the PHY is, because
		 * the mdio read operation return 0 instead of 0xffff
		 * if a slave is not present on hw */
		bus->phy_mask = ~(1 << priv->phy_id);

		if (priv->has_phy_interrupt)
			bus->irq[priv->phy_id] = priv->phy_interrupt;

		ret = mdiobus_register(bus);
		if (ret) {
			dev_err(&pdev->dev, "unable to register mdio bus\n");
			goto out_free_mdio;
		}
	} else {

		/* run platform code to initialize PHY device */
		if (pd && pd->mii_config &&
		    pd->mii_config(dev, 1, bcm_enet_mdio_read_mii,
				   bcm_enet_mdio_write_mii)) {
			dev_err(&pdev->dev, "unable to configure mdio bus\n");
			goto out_uninit_hw;
		}
	}

	spin_lock_init(&priv->rx_lock);

	/* init rx timeout (used for oom) */
	init_timer(&priv->rx_timeout);
	priv->rx_timeout.function = bcm_enet_refill_rx_timer;
	priv->rx_timeout.data = (unsigned long)dev;

	/* init the mib update lock&work */
	mutex_init(&priv->mib_update_lock);
	INIT_WORK(&priv->mib_update_task, bcm_enet_update_mib_counters_defer);

	/* zero mib counters */
	for (i = 0; i < ENET_MIB_REG_COUNT; i++)
		enet_writel(priv, 0, ENET_MIB_REG(i));

	/* register netdevice */
	dev->netdev_ops = &bcm_enet_ops;
	netif_napi_add(dev, &priv->napi, bcm_enet_poll, 16);

	dev->ethtool_ops = &bcm_enet_ethtool_ops;
	/* MTU range: 46 - 2028 */
	dev->min_mtu = ETH_ZLEN - ETH_HLEN;
	dev->max_mtu = BCMENET_MAX_MTU - VLAN_ETH_HLEN;
	SET_NETDEV_DEV(dev, &pdev->dev);

	ret = register_netdev(dev);
	if (ret)
		goto out_unregister_mdio;

	netif_carrier_off(dev);
	platform_set_drvdata(pdev, dev);
	priv->pdev = pdev;
	priv->net_dev = dev;

	return 0;

out_unregister_mdio:
	if (priv->mii_bus)
		mdiobus_unregister(priv->mii_bus);

out_free_mdio:
	if (priv->mii_bus)
		mdiobus_free(priv->mii_bus);

out_uninit_hw:
	/* turn off mdc clock */
	enet_writel(priv, 0, ENET_MIISC_REG);
	if (priv->phy_clk) {
		clk_disable_unprepare(priv->phy_clk);
		clk_put(priv->phy_clk);
	}

out_put_clk_mac:
	clk_disable_unprepare(priv->mac_clk);
	clk_put(priv->mac_clk);
out:
	free_netdev(dev);
	return ret;
}


/*
 * exit func, stops hardware and unregisters netdevice
 */
static int bcm_enet_remove(struct platform_device *pdev)
{
	struct bcm_enet_priv *priv;
	struct net_device *dev;

	/* stop netdevice */
	dev = platform_get_drvdata(pdev);
	priv = netdev_priv(dev);
	unregister_netdev(dev);

	/* turn off mdc clock */
	enet_writel(priv, 0, ENET_MIISC_REG);

	if (priv->has_phy) {
		mdiobus_unregister(priv->mii_bus);
		mdiobus_free(priv->mii_bus);
	} else {
		struct bcm63xx_enet_platform_data *pd;

		pd = dev_get_platdata(&pdev->dev);
		if (pd && pd->mii_config)
			pd->mii_config(dev, 0, bcm_enet_mdio_read_mii,
				       bcm_enet_mdio_write_mii);
	}

	/* disable hw block clocks */
	if (priv->phy_clk) {
		clk_disable_unprepare(priv->phy_clk);
		clk_put(priv->phy_clk);
	}
	clk_disable_unprepare(priv->mac_clk);
	clk_put(priv->mac_clk);

	free_netdev(dev);
	return 0;
}

struct platform_driver bcm63xx_enet_driver = {
	.probe	= bcm_enet_probe,
	.remove	= bcm_enet_remove,
	.driver	= {
		.name	= "bcm63xx_enet",
		.owner  = THIS_MODULE,
	},
};

/*
 * switch mii access callbacks
 */
static int bcmenet_sw_mdio_read(struct bcm_enet_priv *priv,
				int ext, int phy_id, int location)
{
	u32 reg;
	int ret;

	spin_lock_bh(&priv->enetsw_mdio_lock);
	enetsw_writel(priv, 0, ENETSW_MDIOC_REG);

	reg = ENETSW_MDIOC_RD_MASK |
		(phy_id << ENETSW_MDIOC_PHYID_SHIFT) |
		(location << ENETSW_MDIOC_REG_SHIFT);

	if (ext)
		reg |= ENETSW_MDIOC_EXT_MASK;

	enetsw_writel(priv, reg, ENETSW_MDIOC_REG);
	udelay(50);
	ret = enetsw_readw(priv, ENETSW_MDIOD_REG);
	spin_unlock_bh(&priv->enetsw_mdio_lock);
	return ret;
}

static void bcmenet_sw_mdio_write(struct bcm_enet_priv *priv,
				 int ext, int phy_id, int location,
				 uint16_t data)
{
	u32 reg;

	spin_lock_bh(&priv->enetsw_mdio_lock);
	enetsw_writel(priv, 0, ENETSW_MDIOC_REG);

	reg = ENETSW_MDIOC_WR_MASK |
		(phy_id << ENETSW_MDIOC_PHYID_SHIFT) |
		(location << ENETSW_MDIOC_REG_SHIFT);

	if (ext)
		reg |= ENETSW_MDIOC_EXT_MASK;

	reg |= data;

	enetsw_writel(priv, reg, ENETSW_MDIOC_REG);
	udelay(50);
	spin_unlock_bh(&priv->enetsw_mdio_lock);
}

static inline int bcm_enet_port_is_rgmii(int portid)
{
	return portid >= ENETSW_RGMII_PORT0;
}

/*
 * enet sw PHY polling
 */
static void swphy_poll_timer(unsigned long data)
{
	struct bcm_enet_priv *priv = (struct bcm_enet_priv *)data;
	unsigned int i;

	for (i = 0; i < priv->num_ports; i++) {
		struct bcm63xx_enetsw_port *port;
		int val, j, up, advertise, lpa, speed, duplex, media;
		int external_phy = bcm_enet_port_is_rgmii(i);
		u8 override;

		port = &priv->used_ports[i];
		if (!port->used)
			continue;

		if (port->bypass_link)
			continue;

		/* dummy read to clear */
		for (j = 0; j < 2; j++)
			val = bcmenet_sw_mdio_read(priv, external_phy,
						   port->phy_id, MII_BMSR);

		if (val == 0xffff)
			continue;

		up = (val & BMSR_LSTATUS) ? 1 : 0;
		if (!(up ^ priv->sw_port_link[i]))
			continue;

		priv->sw_port_link[i] = up;

		/* link changed */
		if (!up) {
			dev_info(&priv->pdev->dev, "link DOWN on %s\n",
				 port->name);
			enetsw_writeb(priv, ENETSW_PORTOV_ENABLE_MASK,
				      ENETSW_PORTOV_REG(i));
			enetsw_writeb(priv, ENETSW_PTCTRL_RXDIS_MASK |
				      ENETSW_PTCTRL_TXDIS_MASK,
				      ENETSW_PTCTRL_REG(i));
			continue;
		}

		advertise = bcmenet_sw_mdio_read(priv, external_phy,
						 port->phy_id, MII_ADVERTISE);

		lpa = bcmenet_sw_mdio_read(priv, external_phy, port->phy_id,
					   MII_LPA);

		/* figure out media and duplex from advertise and LPA values */
		media = mii_nway_result(lpa & advertise);
		duplex = (media & ADVERTISE_FULL) ? 1 : 0;

		if (media & (ADVERTISE_100FULL | ADVERTISE_100HALF))
			speed = 100;
		else
			speed = 10;

		if (val & BMSR_ESTATEN) {
			advertise = bcmenet_sw_mdio_read(priv, external_phy,
						port->phy_id, MII_CTRL1000);

			lpa = bcmenet_sw_mdio_read(priv, external_phy,
						port->phy_id, MII_STAT1000);

			if (advertise & (ADVERTISE_1000FULL | ADVERTISE_1000HALF)
					&& lpa & (LPA_1000FULL | LPA_1000HALF)) {
				speed = 1000;
				duplex = (lpa & LPA_1000FULL);
			}
		}

		dev_info(&priv->pdev->dev,
			 "link UP on %s, %dMbps, %s-duplex\n",
			 port->name, speed, duplex ? "full" : "half");

		override = ENETSW_PORTOV_ENABLE_MASK |
			ENETSW_PORTOV_LINKUP_MASK;

		if (speed == 1000)
			override |= ENETSW_IMPOV_1000_MASK;
		else if (speed == 100)
			override |= ENETSW_IMPOV_100_MASK;
		if (duplex)
			override |= ENETSW_IMPOV_FDX_MASK;

		enetsw_writeb(priv, override, ENETSW_PORTOV_REG(i));
		enetsw_writeb(priv, 0, ENETSW_PTCTRL_REG(i));
	}

	priv->swphy_poll.expires = jiffies + HZ;
	add_timer(&priv->swphy_poll);
}

/*
 * open callback, allocate dma rings & buffers and start rx operation
 */
static int bcm_enetsw_open(struct net_device *dev)
{
	struct bcm_enet_priv *priv;
	struct device *kdev;
	int i, ret;
	unsigned int size;
	void *p;
	u32 val;

	priv = netdev_priv(dev);
	kdev = &priv->pdev->dev;

	/* mask all interrupts and request them */
	enet_dmac_writel(priv, 0, ENETDMAC_IRMASK, priv->rx_chan);
	enet_dmac_writel(priv, 0, ENETDMAC_IRMASK, priv->tx_chan);

	ret = request_irq(priv->irq_rx, bcm_enet_isr_dma,
			  0, dev->name, dev);
	if (ret)
		goto out_freeirq;

	if (priv->irq_tx != -1) {
		ret = request_irq(priv->irq_tx, bcm_enet_isr_dma,
				  0, dev->name, dev);
		if (ret)
			goto out_freeirq_rx;
	}

	/* allocate rx dma ring */
	size = priv->rx_ring_size * sizeof(struct bcm_enet_desc);
	p = dma_alloc_coherent(kdev, size, &priv->rx_desc_dma, GFP_KERNEL);
	if (!p) {
		dev_err(kdev, "cannot allocate rx ring %u\n", size);
		ret = -ENOMEM;
		goto out_freeirq_tx;
	}

	memset(p, 0, size);
	priv->rx_desc_alloc_size = size;
	priv->rx_desc_cpu = p;

	/* allocate tx dma ring */
	size = priv->tx_ring_size * sizeof(struct bcm_enet_desc);
	p = dma_alloc_coherent(kdev, size, &priv->tx_desc_dma, GFP_KERNEL);
	if (!p) {
		dev_err(kdev, "cannot allocate tx ring\n");
		ret = -ENOMEM;
		goto out_free_rx_ring;
	}

	memset(p, 0, size);
	priv->tx_desc_alloc_size = size;
	priv->tx_desc_cpu = p;

	priv->tx_skb = kzalloc(sizeof(struct sk_buff *) * priv->tx_ring_size,
			       GFP_KERNEL);
	if (!priv->tx_skb) {
		dev_err(kdev, "cannot allocate rx skb queue\n");
		ret = -ENOMEM;
		goto out_free_tx_ring;
	}

	priv->tx_desc_count = priv->tx_ring_size;
	priv->tx_dirty_desc = 0;
	priv->tx_curr_desc = 0;
	spin_lock_init(&priv->tx_lock);

	/* init & fill rx ring with skbs */
	priv->rx_skb = kzalloc(sizeof(struct sk_buff *) * priv->rx_ring_size,
			       GFP_KERNEL);
	if (!priv->rx_skb) {
		dev_err(kdev, "cannot allocate rx skb queue\n");
		ret = -ENOMEM;
		goto out_free_tx_skb;
	}

	priv->rx_desc_count = 0;
	priv->rx_dirty_desc = 0;
	priv->rx_curr_desc = 0;

	/* disable all ports */
	for (i = 0; i < priv->num_ports; i++) {
		enetsw_writeb(priv, ENETSW_PORTOV_ENABLE_MASK,
			      ENETSW_PORTOV_REG(i));
		enetsw_writeb(priv, ENETSW_PTCTRL_RXDIS_MASK |
			      ENETSW_PTCTRL_TXDIS_MASK,
			      ENETSW_PTCTRL_REG(i));

		priv->sw_port_link[i] = 0;
	}

	/* reset mib */
	val = enetsw_readb(priv, ENETSW_GMCR_REG);
	val |= ENETSW_GMCR_RST_MIB_MASK;
	enetsw_writeb(priv, val, ENETSW_GMCR_REG);
	mdelay(1);
	val &= ~ENETSW_GMCR_RST_MIB_MASK;
	enetsw_writeb(priv, val, ENETSW_GMCR_REG);
	mdelay(1);

	/* force CPU port state */
	val = enetsw_readb(priv, ENETSW_IMPOV_REG);
	val |= ENETSW_IMPOV_FORCE_MASK | ENETSW_IMPOV_LINKUP_MASK;
	enetsw_writeb(priv, val, ENETSW_IMPOV_REG);

	/* enable switch forward engine */
	val = enetsw_readb(priv, ENETSW_SWMODE_REG);
	val |= ENETSW_SWMODE_FWD_EN_MASK;
	enetsw_writeb(priv, val, ENETSW_SWMODE_REG);

	/* enable jumbo on all ports */
	enetsw_writel(priv, 0x1ff, ENETSW_JMBCTL_PORT_REG);
	enetsw_writew(priv, 9728, ENETSW_JMBCTL_MAXSIZE_REG);

	/* initialize flow control buffer allocation */
	enet_dma_writel(priv, ENETDMA_BUFALLOC_FORCE_MASK | 0,
			ENETDMA_BUFALLOC_REG(priv->rx_chan));

	if (bcm_enet_refill_rx(dev)) {
		dev_err(kdev, "cannot allocate rx skb queue\n");
		ret = -ENOMEM;
		goto out;
	}

	/* write rx & tx ring addresses */
	enet_dmas_writel(priv, priv->rx_desc_dma,
			 ENETDMAS_RSTART_REG, priv->rx_chan);
	enet_dmas_writel(priv, priv->tx_desc_dma,
			 ENETDMAS_RSTART_REG, priv->tx_chan);

	/* clear remaining state ram for rx & tx channel */
	enet_dmas_writel(priv, 0, ENETDMAS_SRAM2_REG, priv->rx_chan);
	enet_dmas_writel(priv, 0, ENETDMAS_SRAM2_REG, priv->tx_chan);
	enet_dmas_writel(priv, 0, ENETDMAS_SRAM3_REG, priv->rx_chan);
	enet_dmas_writel(priv, 0, ENETDMAS_SRAM3_REG, priv->tx_chan);
	enet_dmas_writel(priv, 0, ENETDMAS_SRAM4_REG, priv->rx_chan);
	enet_dmas_writel(priv, 0, ENETDMAS_SRAM4_REG, priv->tx_chan);

	/* set dma maximum burst len */
	enet_dmac_writel(priv, priv->dma_maxburst,
			 ENETDMAC_MAXBURST, priv->rx_chan);
	enet_dmac_writel(priv, priv->dma_maxburst,
			 ENETDMAC_MAXBURST, priv->tx_chan);

	/* set flow control low/high threshold to 1/3 / 2/3 */
	val = priv->rx_ring_size / 3;
	enet_dma_writel(priv, val, ENETDMA_FLOWCL_REG(priv->rx_chan));
	val = (priv->rx_ring_size * 2) / 3;
	enet_dma_writel(priv, val, ENETDMA_FLOWCH_REG(priv->rx_chan));

	/* all set, enable mac and interrupts, start dma engine and
	 * kick rx dma channel
	 */
	wmb();
	enet_dma_writel(priv, ENETDMA_CFG_EN_MASK, ENETDMA_CFG_REG);
	enet_dmac_writel(priv, ENETDMAC_CHANCFG_EN_MASK,
			 ENETDMAC_CHANCFG, priv->rx_chan);

	/* watch "packet transferred" interrupt in rx and tx */
	enet_dmac_writel(priv, ENETDMAC_IR_PKTDONE_MASK,
			 ENETDMAC_IR, priv->rx_chan);
	enet_dmac_writel(priv, ENETDMAC_IR_PKTDONE_MASK,
			 ENETDMAC_IR, priv->tx_chan);

	/* make sure we enable napi before rx interrupt  */
	napi_enable(&priv->napi);

	enet_dmac_writel(priv, ENETDMAC_IR_PKTDONE_MASK,
			 ENETDMAC_IRMASK, priv->rx_chan);
	enet_dmac_writel(priv, ENETDMAC_IR_PKTDONE_MASK,
			 ENETDMAC_IRMASK, priv->tx_chan);

	netif_carrier_on(dev);
	netif_start_queue(dev);

	/* apply override config for bypass_link ports here. */
	for (i = 0; i < priv->num_ports; i++) {
		struct bcm63xx_enetsw_port *port;
		u8 override;
		port = &priv->used_ports[i];
		if (!port->used)
			continue;

		if (!port->bypass_link)
			continue;

		override = ENETSW_PORTOV_ENABLE_MASK |
			ENETSW_PORTOV_LINKUP_MASK;

		switch (port->force_speed) {
		case 1000:
			override |= ENETSW_IMPOV_1000_MASK;
			break;
		case 100:
			override |= ENETSW_IMPOV_100_MASK;
			break;
		case 10:
			break;
		default:
			pr_warn("invalid forced speed on port %s: assume 10\n",
			       port->name);
			break;
		}

		if (port->force_duplex_full)
			override |= ENETSW_IMPOV_FDX_MASK;


		enetsw_writeb(priv, override, ENETSW_PORTOV_REG(i));
		enetsw_writeb(priv, 0, ENETSW_PTCTRL_REG(i));
	}

	/* start phy polling timer */
	init_timer(&priv->swphy_poll);
	priv->swphy_poll.function = swphy_poll_timer;
	priv->swphy_poll.data = (unsigned long)priv;
	priv->swphy_poll.expires = jiffies;
	add_timer(&priv->swphy_poll);
	return 0;

out:
	for (i = 0; i < priv->rx_ring_size; i++) {
		struct bcm_enet_desc *desc;

		if (!priv->rx_skb[i])
			continue;

		desc = &priv->rx_desc_cpu[i];
		dma_unmap_single(kdev, desc->address, priv->rx_skb_size,
				 DMA_FROM_DEVICE);
		kfree_skb(priv->rx_skb[i]);
	}
	kfree(priv->rx_skb);

out_free_tx_skb:
	kfree(priv->tx_skb);

out_free_tx_ring:
	dma_free_coherent(kdev, priv->tx_desc_alloc_size,
			  priv->tx_desc_cpu, priv->tx_desc_dma);

out_free_rx_ring:
	dma_free_coherent(kdev, priv->rx_desc_alloc_size,
			  priv->rx_desc_cpu, priv->rx_desc_dma);

out_freeirq_tx:
	if (priv->irq_tx != -1)
		free_irq(priv->irq_tx, dev);

out_freeirq_rx:
	free_irq(priv->irq_rx, dev);

out_freeirq:
	return ret;
}

/* stop callback */
static int bcm_enetsw_stop(struct net_device *dev)
{
	struct bcm_enet_priv *priv;
	struct device *kdev;
	int i;

	priv = netdev_priv(dev);
	kdev = &priv->pdev->dev;

	del_timer_sync(&priv->swphy_poll);
	netif_stop_queue(dev);
	napi_disable(&priv->napi);
	del_timer_sync(&priv->rx_timeout);

	/* mask all interrupts */
	enet_dmac_writel(priv, 0, ENETDMAC_IRMASK, priv->rx_chan);
	enet_dmac_writel(priv, 0, ENETDMAC_IRMASK, priv->tx_chan);

	/* disable dma & mac */
	bcm_enet_disable_dma(priv, priv->tx_chan);
	bcm_enet_disable_dma(priv, priv->rx_chan);

	/* force reclaim of all tx buffers */
	bcm_enet_tx_reclaim(dev, 1);

	/* free the rx skb ring */
	for (i = 0; i < priv->rx_ring_size; i++) {
		struct bcm_enet_desc *desc;

		if (!priv->rx_skb[i])
			continue;

		desc = &priv->rx_desc_cpu[i];
		dma_unmap_single(kdev, desc->address, priv->rx_skb_size,
				 DMA_FROM_DEVICE);
		kfree_skb(priv->rx_skb[i]);
	}

	/* free remaining allocated memory */
	kfree(priv->rx_skb);
	kfree(priv->tx_skb);
	dma_free_coherent(kdev, priv->rx_desc_alloc_size,
			  priv->rx_desc_cpu, priv->rx_desc_dma);
	dma_free_coherent(kdev, priv->tx_desc_alloc_size,
			  priv->tx_desc_cpu, priv->tx_desc_dma);
	if (priv->irq_tx != -1)
		free_irq(priv->irq_tx, dev);
	free_irq(priv->irq_rx, dev);

	return 0;
}

/* try to sort out phy external status by walking the used_port field
 * in the bcm_enet_priv structure. in case the phy address is not
 * assigned to any physical port on the switch, assume it is external
 * (and yell at the user).
 */
static int bcm_enetsw_phy_is_external(struct bcm_enet_priv *priv, int phy_id)
{
	int i;

	for (i = 0; i < priv->num_ports; ++i) {
		if (!priv->used_ports[i].used)
			continue;
		if (priv->used_ports[i].phy_id == phy_id)
			return bcm_enet_port_is_rgmii(i);
	}

	printk_once(KERN_WARNING  "bcm63xx_enet: could not find a used port with phy_id %i, assuming phy is external\n",
		    phy_id);
	return 1;
}

/* can't use bcmenet_sw_mdio_read directly as we need to sort out
 * external/internal status of the given phy_id first.
 */
static int bcm_enetsw_mii_mdio_read(struct net_device *dev, int phy_id,
				    int location)
{
	struct bcm_enet_priv *priv;

	priv = netdev_priv(dev);
	return bcmenet_sw_mdio_read(priv,
				    bcm_enetsw_phy_is_external(priv, phy_id),
				    phy_id, location);
}

/* can't use bcmenet_sw_mdio_write directly as we need to sort out
 * external/internal status of the given phy_id first.
 */
static void bcm_enetsw_mii_mdio_write(struct net_device *dev, int phy_id,
				      int location,
				      int val)
{
	struct bcm_enet_priv *priv;

	priv = netdev_priv(dev);
	bcmenet_sw_mdio_write(priv, bcm_enetsw_phy_is_external(priv, phy_id),
			      phy_id, location, val);
}

static int bcm_enetsw_ioctl(struct net_device *dev, struct ifreq *rq, int cmd)
{
	struct mii_if_info mii;

	mii.dev = dev;
	mii.mdio_read = bcm_enetsw_mii_mdio_read;
	mii.mdio_write = bcm_enetsw_mii_mdio_write;
	mii.phy_id = 0;
	mii.phy_id_mask = 0x3f;
	mii.reg_num_mask = 0x1f;
	return generic_mii_ioctl(&mii, if_mii(rq), cmd, NULL);

}

static const struct net_device_ops bcm_enetsw_ops = {
	.ndo_open		= bcm_enetsw_open,
	.ndo_stop		= bcm_enetsw_stop,
	.ndo_start_xmit		= bcm_enet_start_xmit,
	.ndo_change_mtu		= bcm_enet_change_mtu,
	.ndo_do_ioctl		= bcm_enetsw_ioctl,
};


static const struct bcm_enet_stats bcm_enetsw_gstrings_stats[] = {
	{ "rx_packets", DEV_STAT(rx_packets), -1 },
	{ "tx_packets",	DEV_STAT(tx_packets), -1 },
	{ "rx_bytes", DEV_STAT(rx_bytes), -1 },
	{ "tx_bytes", DEV_STAT(tx_bytes), -1 },
	{ "rx_errors", DEV_STAT(rx_errors), -1 },
	{ "tx_errors", DEV_STAT(tx_errors), -1 },
	{ "rx_dropped",	DEV_STAT(rx_dropped), -1 },
	{ "tx_dropped",	DEV_STAT(tx_dropped), -1 },

	{ "tx_good_octets", GEN_STAT(mib.tx_gd_octets), ETHSW_MIB_RX_GD_OCT },
	{ "tx_unicast", GEN_STAT(mib.tx_unicast), ETHSW_MIB_RX_BRDCAST },
	{ "tx_broadcast", GEN_STAT(mib.tx_brdcast), ETHSW_MIB_RX_BRDCAST },
	{ "tx_multicast", GEN_STAT(mib.tx_mult), ETHSW_MIB_RX_MULT },
	{ "tx_64_octets", GEN_STAT(mib.tx_64), ETHSW_MIB_RX_64 },
	{ "tx_65_127_oct", GEN_STAT(mib.tx_65_127), ETHSW_MIB_RX_65_127 },
	{ "tx_128_255_oct", GEN_STAT(mib.tx_128_255), ETHSW_MIB_RX_128_255 },
	{ "tx_256_511_oct", GEN_STAT(mib.tx_256_511), ETHSW_MIB_RX_256_511 },
	{ "tx_512_1023_oct", GEN_STAT(mib.tx_512_1023), ETHSW_MIB_RX_512_1023},
	{ "tx_1024_1522_oct", GEN_STAT(mib.tx_1024_max),
	  ETHSW_MIB_RX_1024_1522 },
	{ "tx_1523_2047_oct", GEN_STAT(mib.tx_1523_2047),
	  ETHSW_MIB_RX_1523_2047 },
	{ "tx_2048_4095_oct", GEN_STAT(mib.tx_2048_4095),
	  ETHSW_MIB_RX_2048_4095 },
	{ "tx_4096_8191_oct", GEN_STAT(mib.tx_4096_8191),
	  ETHSW_MIB_RX_4096_8191 },
	{ "tx_8192_9728_oct", GEN_STAT(mib.tx_8192_9728),
	  ETHSW_MIB_RX_8192_9728 },
	{ "tx_oversize", GEN_STAT(mib.tx_ovr), ETHSW_MIB_RX_OVR },
	{ "tx_oversize_drop", GEN_STAT(mib.tx_ovr), ETHSW_MIB_RX_OVR_DISC },
	{ "tx_dropped",	GEN_STAT(mib.tx_drop), ETHSW_MIB_RX_DROP },
	{ "tx_undersize", GEN_STAT(mib.tx_underrun), ETHSW_MIB_RX_UND },
	{ "tx_pause", GEN_STAT(mib.tx_pause), ETHSW_MIB_RX_PAUSE },

	{ "rx_good_octets", GEN_STAT(mib.rx_gd_octets), ETHSW_MIB_TX_ALL_OCT },
	{ "rx_broadcast", GEN_STAT(mib.rx_brdcast), ETHSW_MIB_TX_BRDCAST },
	{ "rx_multicast", GEN_STAT(mib.rx_mult), ETHSW_MIB_TX_MULT },
	{ "rx_unicast", GEN_STAT(mib.rx_unicast), ETHSW_MIB_TX_MULT },
	{ "rx_pause", GEN_STAT(mib.rx_pause), ETHSW_MIB_TX_PAUSE },
	{ "rx_dropped", GEN_STAT(mib.rx_drop), ETHSW_MIB_TX_DROP_PKTS },

};

#define BCM_ENETSW_STATS_LEN	\
	(sizeof(bcm_enetsw_gstrings_stats) / sizeof(struct bcm_enet_stats))

static void bcm_enetsw_get_strings(struct net_device *netdev,
				   u32 stringset, u8 *data)
{
	int i;

	switch (stringset) {
	case ETH_SS_STATS:
		for (i = 0; i < BCM_ENETSW_STATS_LEN; i++) {
			memcpy(data + i * ETH_GSTRING_LEN,
			       bcm_enetsw_gstrings_stats[i].stat_string,
			       ETH_GSTRING_LEN);
		}
		break;
	}
}

static int bcm_enetsw_get_sset_count(struct net_device *netdev,
				     int string_set)
{
	switch (string_set) {
	case ETH_SS_STATS:
		return BCM_ENETSW_STATS_LEN;
	default:
		return -EINVAL;
	}
}

static void bcm_enetsw_get_drvinfo(struct net_device *netdev,
				   struct ethtool_drvinfo *drvinfo)
{
	strncpy(drvinfo->driver, bcm_enet_driver_name, 32);
	strncpy(drvinfo->version, bcm_enet_driver_version, 32);
	strncpy(drvinfo->fw_version, "N/A", 32);
	strncpy(drvinfo->bus_info, "bcm63xx", 32);
}

static void bcm_enetsw_get_ethtool_stats(struct net_device *netdev,
					 struct ethtool_stats *stats,
					 u64 *data)
{
	struct bcm_enet_priv *priv;
	int i;

	priv = netdev_priv(netdev);

	for (i = 0; i < BCM_ENETSW_STATS_LEN; i++) {
		const struct bcm_enet_stats *s;
		u32 lo, hi;
		char *p;
		int reg;

		s = &bcm_enetsw_gstrings_stats[i];

		reg = s->mib_reg;
		if (reg == -1)
			continue;

		lo = enetsw_readl(priv, ENETSW_MIB_REG(reg));
		p = (char *)priv + s->stat_offset;

		if (s->sizeof_stat == sizeof(u64)) {
			hi = enetsw_readl(priv, ENETSW_MIB_REG(reg + 1));
			*(u64 *)p = ((u64)hi << 32 | lo);
		} else {
			*(u32 *)p = lo;
		}
	}

	for (i = 0; i < BCM_ENETSW_STATS_LEN; i++) {
		const struct bcm_enet_stats *s;
		char *p;

		s = &bcm_enetsw_gstrings_stats[i];

		if (s->mib_reg == -1)
			p = (char *)&netdev->stats + s->stat_offset;
		else
			p = (char *)priv + s->stat_offset;

		data[i] = (s->sizeof_stat == sizeof(u64)) ?
			*(u64 *)p : *(u32 *)p;
	}
}

static void bcm_enetsw_get_ringparam(struct net_device *dev,
				     struct ethtool_ringparam *ering)
{
	struct bcm_enet_priv *priv;

	priv = netdev_priv(dev);

	/* rx/tx ring is actually only limited by memory */
	ering->rx_max_pending = 8192;
	ering->tx_max_pending = 8192;
	ering->rx_mini_max_pending = 0;
	ering->rx_jumbo_max_pending = 0;
	ering->rx_pending = priv->rx_ring_size;
	ering->tx_pending = priv->tx_ring_size;
}

static int bcm_enetsw_set_ringparam(struct net_device *dev,
				    struct ethtool_ringparam *ering)
{
	struct bcm_enet_priv *priv;
	int was_running;

	priv = netdev_priv(dev);

	was_running = 0;
	if (netif_running(dev)) {
		bcm_enetsw_stop(dev);
		was_running = 1;
	}

	priv->rx_ring_size = ering->rx_pending;
	priv->tx_ring_size = ering->tx_pending;

	if (was_running) {
		int err;

		err = bcm_enetsw_open(dev);
		if (err)
			dev_close(dev);
	}
	return 0;
}

static struct ethtool_ops bcm_enetsw_ethtool_ops = {
	.get_strings		= bcm_enetsw_get_strings,
	.get_sset_count		= bcm_enetsw_get_sset_count,
	.get_ethtool_stats      = bcm_enetsw_get_ethtool_stats,
	.get_drvinfo		= bcm_enetsw_get_drvinfo,
	.get_ringparam		= bcm_enetsw_get_ringparam,
	.set_ringparam		= bcm_enetsw_set_ringparam,
};

/* allocate netdevice, request register memory and register device. */
static int bcm_enetsw_probe(struct platform_device *pdev)
{
	struct bcm_enet_priv *priv;
	struct net_device *dev;
	struct bcm63xx_enetsw_platform_data *pd;
	struct resource *res_mem;
	int ret, irq_rx, irq_tx;

	/* stop if shared driver failed, assume driver->probe will be
	 * called in the same order we register devices (correct ?)
	 */
	if (!bcm_enet_shared_base[0])
		return -ENODEV;

	res_mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	irq_rx = platform_get_irq(pdev, 0);
	irq_tx = platform_get_irq(pdev, 1);
	if (!res_mem || irq_rx < 0)
		return -ENODEV;

	ret = 0;
	dev = alloc_etherdev(sizeof(*priv));
	if (!dev)
		return -ENOMEM;
	priv = netdev_priv(dev);
	memset(priv, 0, sizeof(*priv));

	/* initialize default and fetch platform data */
	priv->enet_is_sw = true;
	priv->irq_rx = irq_rx;
	priv->irq_tx = irq_tx;
	priv->rx_ring_size = BCMENET_DEF_RX_DESC;
	priv->tx_ring_size = BCMENET_DEF_TX_DESC;
	priv->dma_maxburst = BCMENETSW_DMA_MAXBURST;

	pd = dev_get_platdata(&pdev->dev);
	if (pd) {
		memcpy(dev->dev_addr, pd->mac_addr, ETH_ALEN);
		memcpy(priv->used_ports, pd->used_ports,
		       sizeof(pd->used_ports));
		priv->num_ports = pd->num_ports;
		priv->dma_has_sram = pd->dma_has_sram;
		priv->dma_chan_en_mask = pd->dma_chan_en_mask;
		priv->dma_chan_int_mask = pd->dma_chan_int_mask;
		priv->dma_chan_width = pd->dma_chan_width;
	}

	ret = bcm_enet_change_mtu(dev, dev->mtu);
	if (ret)
		goto out;

	if (!request_mem_region(res_mem->start, resource_size(res_mem),
				"bcm63xx_enetsw")) {
		ret = -EBUSY;
		goto out;
	}

	priv->base = ioremap(res_mem->start, resource_size(res_mem));
	if (priv->base == NULL) {
		ret = -ENOMEM;
		goto out_release_mem;
	}

	priv->mac_clk = clk_get(&pdev->dev, "enetsw");
	if (IS_ERR(priv->mac_clk)) {
		ret = PTR_ERR(priv->mac_clk);
		goto out_unmap;
	}
	clk_enable(priv->mac_clk);

	priv->rx_chan = 0;
	priv->tx_chan = 1;
	spin_lock_init(&priv->rx_lock);

	/* init rx timeout (used for oom) */
	init_timer(&priv->rx_timeout);
	priv->rx_timeout.function = bcm_enet_refill_rx_timer;
	priv->rx_timeout.data = (unsigned long)dev;

	/* register netdevice */
	dev->netdev_ops = &bcm_enetsw_ops;
	netif_napi_add(dev, &priv->napi, bcm_enet_poll, 16);
	dev->ethtool_ops = &bcm_enetsw_ethtool_ops;
	SET_NETDEV_DEV(dev, &pdev->dev);

	spin_lock_init(&priv->enetsw_mdio_lock);

	ret = register_netdev(dev);
	if (ret)
		goto out_put_clk;

	netif_carrier_off(dev);
	platform_set_drvdata(pdev, dev);
	priv->pdev = pdev;
	priv->net_dev = dev;

	return 0;

out_put_clk:
	clk_put(priv->mac_clk);

out_unmap:
	iounmap(priv->base);

out_release_mem:
	release_mem_region(res_mem->start, resource_size(res_mem));
out:
	free_netdev(dev);
	return ret;
}


/* exit func, stops hardware and unregisters netdevice */
static int bcm_enetsw_remove(struct platform_device *pdev)
{
	struct bcm_enet_priv *priv;
	struct net_device *dev;
	struct resource *res;

	/* stop netdevice */
	dev = platform_get_drvdata(pdev);
	priv = netdev_priv(dev);
	unregister_netdev(dev);

	/* release device resources */
	iounmap(priv->base);
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	release_mem_region(res->start, resource_size(res));

	free_netdev(dev);
	return 0;
}

struct platform_driver bcm63xx_enetsw_driver = {
	.probe	= bcm_enetsw_probe,
	.remove	= bcm_enetsw_remove,
	.driver	= {
		.name	= "bcm63xx_enetsw",
		.owner  = THIS_MODULE,
	},
};

/* reserve & remap memory space shared between all macs */
static int bcm_enet_shared_probe(struct platform_device *pdev)
{
	struct resource *res;
	void __iomem *p[3];
	unsigned int i;

	memset(bcm_enet_shared_base, 0, sizeof(bcm_enet_shared_base));

	for (i = 0; i < 3; i++) {
		res = platform_get_resource(pdev, IORESOURCE_MEM, i);
		p[i] = devm_ioremap_resource(&pdev->dev, res);
		if (IS_ERR(p[i]))
			return PTR_ERR(p[i]);
	}

	memcpy(bcm_enet_shared_base, p, sizeof(bcm_enet_shared_base));

	return 0;
}

static int bcm_enet_shared_remove(struct platform_device *pdev)
{
	return 0;
}

/* this "shared" driver is needed because both macs share a single
 * address space
 */
struct platform_driver bcm63xx_enet_shared_driver = {
	.probe	= bcm_enet_shared_probe,
	.remove	= bcm_enet_shared_remove,
	.driver	= {
		.name	= "bcm63xx_enet_shared",
		.owner  = THIS_MODULE,
	},
};

static struct platform_driver * const drivers[] = {
	&bcm63xx_enet_shared_driver,
	&bcm63xx_enet_driver,
	&bcm63xx_enetsw_driver,
};

/* entry point */
static int __init bcm_enet_init(void)
{
	return platform_register_drivers(drivers, ARRAY_SIZE(drivers));
}

static void __exit bcm_enet_exit(void)
{
	platform_unregister_drivers(drivers, ARRAY_SIZE(drivers));
}


module_init(bcm_enet_init);
module_exit(bcm_enet_exit);

MODULE_DESCRIPTION("BCM63xx internal ethernet mac driver");
MODULE_AUTHOR("Maxime Bizon <mbizon@freebox.fr>");
MODULE_LICENSE("GPL");
