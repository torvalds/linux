/*
 * linux/arch/arc/drivers/arcvmac.c
 *
 * Copyright (C) 2003-2006 Codito Technologies, for linux-2.4 port
 * Copyright (C) 2006-2007 Celunite Inc, for linux-2.6 port
 * Copyright (C) 2007-2008 Sagem Communications, Fehmi HAFSI
 * Copyright (C) 2009 Sagem Communications, Andreas Fenkart
 * All Rights Reserved.
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
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * external PHY support based on dnet.c
 * ring management based on bcm63xx_enet.c
 *
 * Authors: amit.bhor@celunite.com, sameer.dhavale@celunite.com
 */

//#define DEBUG

#include <linux/clk.h>
#include <linux/crc32.h>
#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/etherdevice.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/netdevice.h>
#include <linux/phy.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/wakelock.h>
#include <linux/version.h>

#include <mach/iomux.h>
#include <mach/gpio.h>
#include <mach/cru.h>
#include <mach/board.h>

#include "rk29_vmac.h"

static struct wake_lock idlelock; /* add by lyx @ 20110302 */

/* Register access macros */
#define vmac_writel(port, value, reg)	\
	writel((value), (port)->regs + reg##_OFFSET)
#define vmac_readl(port, reg)	readl((port)->regs + reg##_OFFSET)

static unsigned char *read_mac_reg(struct net_device *dev,
		unsigned char hwaddr[ETH_ALEN])
{
	struct vmac_priv *ap = netdev_priv(dev);
	unsigned mac_lo, mac_hi;

	WARN_ON(!hwaddr);
	mac_lo = vmac_readl(ap, ADDRL);
	mac_hi = vmac_readl(ap, ADDRH);

	hwaddr[0] = (mac_lo >> 0) & 0xff;
	hwaddr[1] = (mac_lo >> 8) & 0xff;
	hwaddr[2] = (mac_lo >> 16) & 0xff;
	hwaddr[3] = (mac_lo >> 24) & 0xff;
	hwaddr[4] = (mac_hi >> 0) & 0xff;
	hwaddr[5] = (mac_hi >> 8) & 0xff;
	return hwaddr;
}

static void write_mac_reg(struct net_device *dev, unsigned char* hwaddr)
{
	struct vmac_priv *ap = netdev_priv(dev);
	unsigned mac_lo, mac_hi;

	mac_lo = hwaddr[3] << 24 | hwaddr[2] << 16 | hwaddr[1] << 8 | hwaddr[0];
	mac_hi = hwaddr[5] << 8 | hwaddr[4];

	vmac_writel(ap, mac_lo, ADDRL);
	vmac_writel(ap, mac_hi, ADDRH);
}

static void vmac_mdio_xmit(struct vmac_priv *ap, unsigned val)
{
	init_completion(&ap->mdio_complete);
	vmac_writel(ap, val, MDIO_DATA);
	if(!wait_for_completion_timeout(&ap->mdio_complete, msecs_to_jiffies(1000)))
		printk("Time out for waiting mdio completion\n");
}

static int vmac_mdio_read(struct mii_bus *bus, int phy_id, int phy_reg)
{
	struct vmac_priv *vmac = bus->priv;
	unsigned int val;
	/* only 5 bits allowed for phy-addr and reg_offset */
	WARN_ON(phy_id & ~0x1f || phy_reg & ~0x1f);

	val = MDIO_BASE | MDIO_OP_READ;
	val |= phy_id << 23 | phy_reg << 18;
	vmac_mdio_xmit(vmac, val);

	val = vmac_readl(vmac, MDIO_DATA);
	return val & MDIO_DATA_MASK;
}

static int vmac_mdio_write(struct mii_bus *bus, int phy_id, int phy_reg,
			 u16 value)
{
	struct vmac_priv *vmac = bus->priv;
	unsigned int val;
	/* only 5 bits allowed for phy-addr and reg_offset */
	WARN_ON(phy_id & ~0x1f || phy_reg & ~0x1f);

	val = MDIO_BASE | MDIO_OP_WRITE;
	val |= phy_id << 23 | phy_reg << 18;
	val |= (value & MDIO_DATA_MASK);
	vmac_mdio_xmit(vmac, val);
	return 0;
}

static void vmac_handle_link_change(struct net_device *dev)
{
	struct vmac_priv *ap = netdev_priv(dev);
	struct phy_device *phydev = ap->phy_dev;
	unsigned long flags;
	int report_change = 0;

	spin_lock_irqsave(&ap->lock, flags);

	if (phydev->duplex != ap->duplex) {
		unsigned tmp;

		tmp = vmac_readl(ap, CONTROL);

		if (phydev->duplex)
			tmp |= ENFL_MASK;
		else
			tmp &= ~ENFL_MASK;

		vmac_writel(ap, tmp, CONTROL);

		ap->duplex = phydev->duplex;
		report_change = 1;
	}

	if (phydev->speed != ap->speed) {
		ap->speed = phydev->speed;
		report_change = 1;
	}

	if (phydev->link != ap->link) {
		ap->link = phydev->link;
		report_change = 1;
	}

	spin_unlock_irqrestore(&ap->lock, flags);

	if (report_change)
		phy_print_status(ap->phy_dev);
}

static int __devinit vmac_mii_probe(struct net_device *dev)
{
	struct vmac_priv *ap = netdev_priv(dev);
	struct phy_device *phydev = NULL;	
	//struct clk *sys_clk;
	//unsigned long clock_rate;
	int phy_addr, err;

	/* find the first phy */
	for (phy_addr = 0; phy_addr < PHY_MAX_ADDR; phy_addr++) {
		if (ap->mii_bus->phy_map[phy_addr]) {
			phydev = ap->mii_bus->phy_map[phy_addr];
			break;
		}
	}

	if (!phydev) {
		dev_err(&dev->dev, "no PHY found\n");
		return -ENODEV;
	}

	/* add pin_irq, if avail */
	phydev = phy_connect(dev, dev_name(&phydev->dev),
			&vmac_handle_link_change, 0,
			//PHY_INTERFACE_MODE_MII);
			PHY_INTERFACE_MODE_RMII);//????????
	if (IS_ERR(phydev)) {
		err = PTR_ERR(phydev);
		dev_err(&dev->dev, "could not attach to PHY %d\n", err);
		goto err_out;
	}

	phydev->supported &= PHY_BASIC_FEATURES;
	phydev->supported |= SUPPORTED_Asym_Pause | SUPPORTED_Pause;

#if 0
	sys_clk = clk_get(NULL, "mac_ref");////////
	if (IS_ERR(sys_clk)) {
		err = PTR_ERR(sys_clk);
		goto err_disconnect;
	}
	
	clk_set_rate(sys_clk,50000000);
	clock_rate = clk_get_rate(sys_clk);
	clk_put(sys_clk);
	
	printk("%s::%d --mac clock = %d\n",__func__, __LINE__, clock_rate);
	dev_dbg(&ap->pdev->dev, "clk_get: dev_name : %s %lu\n",
			dev_name(&ap->pdev->dev),
			clock_rate);

	if (clock_rate < 25000000)
		phydev->supported &= ~(SUPPORTED_100baseT_Half |
				SUPPORTED_100baseT_Full);
#endif

	phydev->advertising = phydev->supported;

	ap->link = 0;
	ap->speed = 0;
	ap->duplex = -1;
	ap->phy_dev = phydev;

	return 0;
//err_disconnect:
//	phy_disconnect(phydev);
err_out:
	return err;
}

static int __devinit vmac_mii_init(struct vmac_priv *ap)
{
	int err, i;

	ap->mii_bus = mdiobus_alloc();
	
	if (ap->mii_bus == NULL)
		return -ENOMEM;

	ap->mii_bus->name = "vmac_mii_bus";
	ap->mii_bus->read = &vmac_mdio_read;
	ap->mii_bus->write = &vmac_mdio_write;

	snprintf(ap->mii_bus->id, MII_BUS_ID_SIZE, "%x", 0);

	ap->mii_bus->priv = ap;

	err = -ENOMEM;
	ap->mii_bus->irq = kmalloc(sizeof(int) * PHY_MAX_ADDR, GFP_KERNEL);
	if (!ap->mii_bus->irq)
		goto err_out;

	for (i = 0; i < PHY_MAX_ADDR; i++)
		ap->mii_bus->irq[i] = PHY_POLL;

#if 0
	/* FIXME: what is it used for? */
	platform_set_drvdata(ap->dev, ap->mii_bus);
#endif

	err = mdiobus_register(ap->mii_bus);
	if (err)
		goto err_out_free_mdio_irq;

	err = vmac_mii_probe(ap->dev);
	if (err)
		goto err_out_unregister_bus;

	return 0;

err_out_unregister_bus:
	mdiobus_unregister(ap->mii_bus);
err_out_free_mdio_irq:
	kfree(ap->mii_bus->irq);
err_out:
	mdiobus_free(ap->mii_bus);
	ap->mii_bus = NULL;
	return err;
}

static void vmac_mii_exit(struct net_device *dev)
{
	struct vmac_priv *ap = netdev_priv(dev);

	if (ap->phy_dev)
		phy_disconnect(ap->phy_dev);
	if (ap->mii_bus) {
		mdiobus_unregister(ap->mii_bus);
		kfree(ap->mii_bus->irq);
		mdiobus_free(ap->mii_bus);
		ap->mii_bus = NULL;
	}
}

static int vmacether_get_settings(struct net_device *dev,
		struct ethtool_cmd *cmd)
{
	struct vmac_priv *ap = netdev_priv(dev);
	struct phy_device *phydev = ap->phy_dev;

	if (!phydev)
		return -ENODEV;

	return phy_ethtool_gset(phydev, cmd);
}

static int vmacether_set_settings(struct net_device *dev,
		struct ethtool_cmd *cmd)
{
	struct vmac_priv *ap = netdev_priv(dev);
	struct phy_device *phydev = ap->phy_dev;

	if (!phydev)
		return -ENODEV;

	return phy_ethtool_sset(phydev, cmd);
}

static int vmac_ioctl(struct net_device *dev, struct ifreq *rq, int cmd)
{
	struct vmac_priv *ap = netdev_priv(dev);
	struct phy_device *phydev = ap->phy_dev;

	if (!netif_running(dev))
		return -EINVAL;

	if (!phydev)
		return -ENODEV;

	return phy_mii_ioctl(phydev, rq, cmd);
}

static void vmacether_get_drvinfo(struct net_device *dev,
		struct ethtool_drvinfo *info)
{
	struct vmac_priv *ap = netdev_priv(dev);

	strlcpy(info->driver, VMAC_NAME, sizeof(info->driver));
	strlcpy(info->version, VMAC_VERSION, sizeof(info->version));
	snprintf(info->bus_info, sizeof(info->bus_info),
			"platform 0x%x", ap->mem_base);
}

static int update_error_counters(struct net_device *dev, int status)
{
	struct vmac_priv *ap = netdev_priv(dev);
	dev_dbg(&ap->pdev->dev, "rx error counter overrun. status = 0x%x\n",
			status);

	/* programming error */
	WARN_ON(status & TXCH_MASK);
	WARN_ON(!(status & (MSER_MASK | RXCR_MASK | RXFR_MASK | RXFL_MASK)));

	if (status & MSER_MASK)
		ap->stats.rx_over_errors += 256; /* ran out of BD */
	if (status & RXCR_MASK)
		ap->stats.rx_crc_errors += 256;
	if (status & RXFR_MASK)
		ap->stats.rx_frame_errors += 256;
	if (status & RXFL_MASK)
		ap->stats.rx_fifo_errors += 256;

	return 0;
}

static void update_tx_errors(struct net_device *dev, int status)
{
	struct vmac_priv *ap = netdev_priv(dev);

	if (status & UFLO)
		ap->stats.tx_fifo_errors++;

	if (ap->duplex)
		return;

	/* half duplex flags */
	if (status & LTCL)
		ap->stats.tx_window_errors++;
	if (status & RETRY_CT)
		ap->stats.collisions += (status & RETRY_CT) >> 24;
	if (status & DROP)  /* too many retries */
		ap->stats.tx_aborted_errors++;
	if (status & DEFER)
		dev_vdbg(&ap->pdev->dev, "\"defer to traffic\"\n");
	if (status & CARLOSS)
		ap->stats.tx_carrier_errors++;
}

static int vmac_rx_reclaim_force(struct net_device *dev)
{
	struct vmac_priv *ap = netdev_priv(dev);
	int ct;

	ct = 0;

	dev_dbg(&ap->pdev->dev, "%s need to release %d rx sk_buff\n",
	    __func__, fifo_used(&ap->rx_ring));

	while (!fifo_empty(&ap->rx_ring) && ct++ < ap->rx_ring.size) {
		struct vmac_buffer_desc *desc;
		struct sk_buff *skb;
		int desc_idx;

		desc_idx = ap->rx_ring.tail;
		desc = &ap->rxbd[desc_idx];
		fifo_inc_tail(&ap->rx_ring);

		if (!ap->rx_skbuff[desc_idx]) {
			dev_err(&ap->pdev->dev, "non-populated rx_skbuff found %d\n",
					desc_idx);
			continue;
		}

		skb = ap->rx_skbuff[desc_idx];
		ap->rx_skbuff[desc_idx] = NULL;

		dma_unmap_single(&ap->pdev->dev, desc->data, skb->len,
		    DMA_TO_DEVICE);

		dev_kfree_skb(skb);
	}

	if (!fifo_empty(&ap->rx_ring)) {
		dev_err(&ap->pdev->dev, "failed to reclaim %d rx sk_buff\n",
				fifo_used(&ap->rx_ring));
	}

	return 0;
}

static int vmac_rx_refill(struct net_device *dev)
{
	struct vmac_priv *ap = netdev_priv(dev);

	WARN_ON(fifo_full(&ap->rx_ring));

	while (!fifo_full(&ap->rx_ring)) {
		struct vmac_buffer_desc *desc;
		struct sk_buff *skb;
		dma_addr_t p;
		int desc_idx;

		desc_idx = ap->rx_ring.head;
		desc = &ap->rxbd[desc_idx];

		/* make sure we read the actual descriptor status */
		rmb();

		if (ap->rx_skbuff[desc_idx]) {
			/* dropped packet / buffer chaining */
			fifo_inc_head(&ap->rx_ring);

			/* return to DMA */
			wmb();
			desc->info = OWN_MASK | ap->rx_skb_size;
			continue;
		}

		skb = netdev_alloc_skb(dev, ap->rx_skb_size + 2);
		if (!skb) {
			dev_info(&ap->pdev->dev, "failed to allocate rx_skb, skb's left %d\n",
					fifo_used(&ap->rx_ring));
			break;
		}

		/* IP header Alignment (14 byte Ethernet header) */
		skb_reserve(skb, 2);
		WARN_ON(skb->len != 0); /* nothing received yet */

		ap->rx_skbuff[desc_idx] = skb;

		p = dma_map_single(&ap->pdev->dev, skb->data, ap->rx_skb_size,
				DMA_FROM_DEVICE);

		desc->data = p;

		wmb();
		desc->info = OWN_MASK | ap->rx_skb_size;

		fifo_inc_head(&ap->rx_ring);
	}

	/* If rx ring is still empty, set a timer to try allocating
	 * again at a later time. */
	if (fifo_empty(&ap->rx_ring) && netif_running(dev)) {
		dev_warn(&ap->pdev->dev, "unable to refill rx ring\n");
		ap->rx_timeout.expires = jiffies + HZ;
		add_timer(&ap->rx_timeout);
	}

	return 0;
}

/*
 * timer callback to defer refill rx queue in case we're OOM
 */
static void vmac_refill_rx_timer(unsigned long data)
{
	struct net_device *dev;
	struct vmac_priv *ap;

	dev = (struct net_device *)data;
	ap = netdev_priv(dev);

	spin_lock(&ap->rx_lock);
	vmac_rx_refill(dev);
	spin_unlock(&ap->rx_lock);
}

/* merge buffer chaining  */
struct sk_buff *vmac_merge_rx_buffers(struct net_device *dev,
		struct vmac_buffer_desc *after,
		int pkt_len) /* data */
{
	struct vmac_priv *ap = netdev_priv(dev);
	struct sk_buff *merge_skb, *cur_skb;
	struct dma_fifo *rx_ring;
	struct vmac_buffer_desc *desc;

	rx_ring = &ap->rx_ring;
	desc = &ap->rxbd[rx_ring->tail];

	WARN_ON(desc == after);

	/* strip FCS */
	pkt_len -= 4;

	/* IP header Alignment (14 byte Ethernet header) */
	merge_skb = netdev_alloc_skb(dev, pkt_len + 2);
	if (!merge_skb) {
		dev_err(&ap->pdev->dev, "failed to allocate merged rx_skb, rx skb's left %d\n",
				fifo_used(rx_ring));

		return NULL;
	}

	skb_reserve(merge_skb, 2);

	while (desc != after && pkt_len) {
		struct vmac_buffer_desc *desc;
		int buf_len, valid;

		/* desc needs wrapping */
		desc = &ap->rxbd[rx_ring->tail];
		cur_skb = ap->rx_skbuff[rx_ring->tail];
		WARN_ON(!cur_skb);

		dma_unmap_single(&ap->pdev->dev, desc->data, ap->rx_skb_size,
				DMA_FROM_DEVICE);

		/* do not copy FCS */
		buf_len = desc->info & LEN_MASK;
		valid = min(pkt_len, buf_len);
		pkt_len -= valid;

		memcpy(skb_put(merge_skb, valid), cur_skb->data, valid);

		fifo_inc_tail(rx_ring);
	}

	/* merging_pressure++ */

	if (unlikely(pkt_len != 0))
		dev_err(&ap->pdev->dev, "buffer chaining bytes missing %d\n",
				pkt_len);

	WARN_ON(desc != after);

	return merge_skb;
}

int vmac_rx_receive(struct net_device *dev, int budget)
{
	struct vmac_priv *ap = netdev_priv(dev);
	struct vmac_buffer_desc *first;
	int processed, pkt_len, pkt_err;
	struct dma_fifo lookahead;

	processed = 0;

	first = NULL;
	pkt_err = pkt_len = 0;

	/* look ahead, till packet complete */
	lookahead = ap->rx_ring;

	do {
		struct vmac_buffer_desc *desc; /* cur_ */
		int desc_idx; /* cur_ */
		struct sk_buff *skb; /* pkt_ */

		desc_idx = lookahead.tail;
		desc = &ap->rxbd[desc_idx];

		/* make sure we read the actual descriptor status */
		rmb();

		/* break if dma ownership belongs to hw */
		if (desc->info & OWN_MASK) {
			ap->mac_rxring_head = vmac_readl(ap, MAC_RXRING_HEAD);
			break;
		}

		if (desc->info & FRST_MASK) {
			pkt_len = 0;
			pkt_err = 0;

			/* don't free current */
			ap->rx_ring.tail = lookahead.tail;
			first = desc;
		}

		fifo_inc_tail(&lookahead);

		/* check bd */

		pkt_len += desc->info & LEN_MASK;
		pkt_err |= (desc->info & BUFF);

		if (!(desc->info & LAST_MASK))
			continue;

		/* received complete packet */

		if (unlikely(pkt_err || !first)) {
			/* recycle buffers */
			ap->rx_ring.tail = lookahead.tail;
			continue;
		}

		WARN_ON(!(first->info & FRST_MASK) ||
				!(desc->info & LAST_MASK));
		WARN_ON(pkt_err);

		/* -- valid packet -- */

		if (first != desc) {
			skb = vmac_merge_rx_buffers(dev, desc, pkt_len);

			if (!skb) {
				/* kill packet */
				ap->rx_ring.tail = lookahead.tail;
				ap->rx_merge_error++;
				continue;
			}
		} else {
			dma_unmap_single(&ap->pdev->dev, desc->data,
					ap->rx_skb_size, DMA_FROM_DEVICE);

			skb = ap->rx_skbuff[desc_idx];
			ap->rx_skbuff[desc_idx] = NULL;
			/* desc->data != skb->data => desc->data DMA mapped */

			/* strip FCS */
			skb_put(skb, pkt_len - 4);
		}

		/* free buffers */
		ap->rx_ring.tail = lookahead.tail;

		WARN_ON(skb->len != pkt_len - 4);
		processed++;
		skb->dev = dev;
		skb->protocol = eth_type_trans(skb, dev);
		ap->stats.rx_packets++;
		ap->stats.rx_bytes += skb->len;
		dev->last_rx = jiffies;
		netif_rx(skb);

	} while (!fifo_empty(&lookahead) && (processed < budget));

	dev_vdbg(&ap->pdev->dev, "processed pkt %d, remaining rx buff %d\n",
			processed,
			fifo_used(&ap->rx_ring));

	if (processed || fifo_empty(&ap->rx_ring))
		vmac_rx_refill(dev);

	return processed;
}

static void vmac_toggle_irqmask(struct net_device *dev, int enable, int mask)
{
	struct vmac_priv *ap = netdev_priv(dev);
	unsigned long tmp;

	tmp = vmac_readl(ap, ENABLE);
	if (enable)
		tmp |= mask;
	else
		tmp &= ~mask;
	vmac_writel(ap, tmp, ENABLE);
}

static void vmac_toggle_txint(struct net_device *dev, int enable)
{
	struct vmac_priv *ap = netdev_priv(dev);
	unsigned long flags;

	spin_lock_irqsave(&ap->lock, flags);
	vmac_toggle_irqmask(dev, enable, TXINT_MASK);
	spin_unlock_irqrestore(&ap->lock, flags);
}

static void vmac_toggle_rxint(struct net_device *dev, int enable)
{
	vmac_toggle_irqmask(dev, enable, RXINT_MASK);
}

static int vmac_poll(struct napi_struct *napi, int budget)
{
	struct vmac_priv *ap;
	struct net_device *dev;
	int rx_work_done;
	unsigned long flags;

	ap = container_of(napi, struct vmac_priv, napi);
	dev = ap->dev;

	/* ack interrupt */
	vmac_writel(ap, RXINT_MASK, STAT);

	spin_lock(&ap->rx_lock);
	rx_work_done = vmac_rx_receive(dev, budget);
	spin_unlock(&ap->rx_lock);

#ifdef VERBOSE_DEBUG
	if (printk_ratelimit()) {
		dev_vdbg(&ap->pdev->dev, "poll budget %d receive rx_work_done %d\n",
				budget,
				rx_work_done);
	}
#endif

	if (rx_work_done >= budget) {
		/* rx queue is not yet empty/clean */
		return rx_work_done;
	}

	/* no more packet in rx/tx queue, remove device from poll
	 * queue */
	spin_lock_irqsave(&ap->lock, flags);
	napi_complete(napi);
	vmac_toggle_rxint(dev, 1);
	spin_unlock_irqrestore(&ap->lock, flags);

	return rx_work_done;
}

static int vmac_tx_reclaim(struct net_device *dev, int force);

static irqreturn_t vmac_intr(int irq, void *dev_instance)
{
	struct net_device *dev = dev_instance;
	struct vmac_priv *ap = netdev_priv(dev);
	unsigned int status;

	spin_lock(&ap->lock);

	status = vmac_readl(ap, STAT);
	vmac_writel(ap, status, STAT);

#ifdef DEBUG
	if (unlikely(ap->shutdown))
		dev_err(&ap->pdev->dev, "ISR during close\n");

	if (unlikely(!status & (RXINT_MASK|MDIO_MASK|ERR_MASK)))
		dev_err(&ap->pdev->dev, "No source of IRQ found\n");
#endif

	if ((status & RXINT_MASK) &&
			(ap->mac_rxring_head !=
			 vmac_readl(ap, MAC_RXRING_HEAD))) {
		vmac_toggle_rxint(dev, 0);
		napi_schedule(&ap->napi);
	}

	if (unlikely(netif_queue_stopped(dev) && (status & TXINT_MASK)))
		vmac_tx_reclaim(dev, 0);

	if (status & MDIO_MASK)
		complete(&ap->mdio_complete);

	if (unlikely(status & ERR_MASK))
		update_error_counters(dev, status);

	spin_unlock(&ap->lock);

	return IRQ_HANDLED;
}

static int vmac_tx_reclaim(struct net_device *dev, int force)
{
	struct vmac_priv *ap = netdev_priv(dev);
	int released = 0;

	/* buffer chaining not used, see vmac_start_xmit */

	while (!fifo_empty(&ap->tx_ring)) {
		struct vmac_buffer_desc *desc;
		struct sk_buff *skb;
		int desc_idx;

		desc_idx = ap->tx_ring.tail;
		desc = &ap->txbd[desc_idx];

		/* ensure other field of the descriptor were not read
		 * before we checked ownership */
		rmb();

		if ((desc->info & OWN_MASK) && !force)
			break;

		if (desc->info & ERR_MSK_TX) {
			update_tx_errors(dev, desc->info);
			/* recycle packet, let upper level deal with it */
		}

		skb = ap->tx_skbuff[desc_idx];
		ap->tx_skbuff[desc_idx] = NULL;
		WARN_ON(!skb);

		dma_unmap_single(&ap->pdev->dev, desc->data, skb->len,
				DMA_TO_DEVICE);

		dev_kfree_skb_any(skb);

		released++;
		fifo_inc_tail(&ap->tx_ring);
	}

	if (netif_queue_stopped(dev) && released) {
		netif_wake_queue(dev);
		vmac_toggle_txint(dev, 0);
	}

	if (unlikely(force && !fifo_empty(&ap->tx_ring))) {
		dev_err(&ap->pdev->dev, "failed to reclaim %d tx sk_buff\n",
				fifo_used(&ap->tx_ring));
	}

	return released;
}

int vmac_start_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct vmac_priv *ap = netdev_priv(dev);
	struct vmac_buffer_desc *desc;
	unsigned int tmp;

	/* running under xmit lock */

	/* no scatter/gatter see features below */
	WARN_ON(skb_shinfo(skb)->nr_frags != 0);
	WARN_ON(skb->len > MAX_TX_BUFFER_LEN);

	if (unlikely(fifo_full(&ap->tx_ring))) {
		netif_stop_queue(dev);
		vmac_toggle_txint(dev, 1);
		dev_err(&ap->pdev->dev, "xmit called with no tx desc available\n");
		return NETDEV_TX_BUSY;
	}

	if (unlikely(skb->len < ETH_ZLEN)) {
		struct sk_buff *short_skb;
		short_skb = netdev_alloc_skb(dev, ETH_ZLEN);
		if (!short_skb)
			return NETDEV_TX_LOCKED;

		memset(short_skb->data, 0, ETH_ZLEN);
		memcpy(skb_put(short_skb, ETH_ZLEN), skb->data, skb->len);
		dev_kfree_skb(skb);
		skb = short_skb;
	}

	/* fill descriptor */
	ap->tx_skbuff[ap->tx_ring.head] = skb;

	desc = &ap->txbd[ap->tx_ring.head];
	desc->data = dma_map_single(&ap->pdev->dev, skb->data, skb->len,
			DMA_TO_DEVICE);

	/* dma might already be polling */
	wmb();
	desc->info = OWN_MASK | FRST_MASK | LAST_MASK | skb->len;
	wmb();

	/* kick tx dma */
	tmp = vmac_readl(ap, STAT);
	vmac_writel(ap, tmp | TXPL_MASK, STAT);

	ap->stats.tx_packets++;
	ap->stats.tx_bytes += skb->len;
	dev->trans_start = jiffies;
	fifo_inc_head(&ap->tx_ring);

	/* vmac_tx_reclaim independent of vmac_tx_timeout */
	if (fifo_used(&ap->tx_ring) > 8)
		vmac_tx_reclaim(dev, 0);

	/* stop queue if no more desc available */
	if (fifo_full(&ap->tx_ring)) {
		netif_stop_queue(dev);
		vmac_toggle_txint(dev, 1);
	}

	return NETDEV_TX_OK;
}

static int alloc_buffers(struct net_device *dev)
{
	struct vmac_priv *ap = netdev_priv(dev);
	int err = -ENOMEM;
	int size;

	fifo_init(&ap->rx_ring, RX_BDT_LEN);
	fifo_init(&ap->tx_ring, TX_BDT_LEN);

	/* initialize skb list */
	memset(ap->rx_skbuff, 0, sizeof(ap->rx_skbuff));
	memset(ap->tx_skbuff, 0, sizeof(ap->tx_skbuff));

	/* allocate DMA received descriptors */
	size = sizeof(*ap->rxbd) * ap->rx_ring.size;
	ap->rxbd = dma_alloc_coherent(&ap->pdev->dev, size,
			&ap->rxbd_dma,
			GFP_KERNEL);
	if (ap->rxbd == NULL)
		goto err_out;

	/* allocate DMA transmit descriptors */
	size = sizeof(*ap->txbd) * ap->tx_ring.size;
	ap->txbd = dma_alloc_coherent(&ap->pdev->dev, size,
			&ap->txbd_dma,
			GFP_KERNEL);
	if (ap->txbd == NULL)
		goto err_free_rxbd;

	/* ensure 8-byte aligned */
	WARN_ON(((int)ap->txbd & 0x7) || ((int)ap->rxbd & 0x7));

	memset(ap->txbd, 0, sizeof(*ap->txbd) * ap->tx_ring.size);
	memset(ap->rxbd, 0, sizeof(*ap->rxbd) * ap->rx_ring.size);

	/* allocate rx skb */
	err = vmac_rx_refill(dev);
	if (err)
		goto err_free_txbd;

	return 0;

err_free_txbd:
	dma_free_coherent(&ap->pdev->dev, sizeof(*ap->txbd) * ap->tx_ring.size,
			ap->txbd, ap->txbd_dma);
err_free_rxbd:
	dma_free_coherent(&ap->pdev->dev, sizeof(*ap->rxbd) * ap->rx_ring.size,
			ap->rxbd, ap->rxbd_dma);
err_out:
	return err;
}

static int free_buffers(struct net_device *dev)
{
	struct vmac_priv *ap = netdev_priv(dev);

	/* free skbuff */
	vmac_tx_reclaim(dev, 1);
	vmac_rx_reclaim_force(dev);

	/* free DMA ring */
	dma_free_coherent(&ap->pdev->dev, sizeof(ap->txbd) * ap->tx_ring.size,
			ap->txbd, ap->txbd_dma);
	dma_free_coherent(&ap->pdev->dev, sizeof(ap->rxbd) * ap->rx_ring.size,
			ap->rxbd, ap->rxbd_dma);

	return 0;
}

static int vmac_hw_init(struct net_device *dev)
{
	struct vmac_priv *priv = netdev_priv(dev);

	/* clear IRQ mask */
	vmac_writel(priv, 0, ENABLE);

	/* clear pending IRQ */
	vmac_writel(priv, 0xffffffff, STAT);

	/* Initialize logical address filter */
	vmac_writel(priv, 0x0, LAFL);
	vmac_writel(priv, 0x0, LAFH);

	return 0;
}

#ifdef DEBUG
static int vmac_register_print(struct net_device *dev)
{
	struct vmac_priv *ap = netdev_priv(dev);

	printk("func::%s vmac register %s value = 0x%x\n", __func__, "ID", vmac_readl(ap, ID));
	printk("func::%s vmac register %s value = 0x%x\n", __func__, "STAT", vmac_readl(ap, STAT));
	printk("func::%s vmac register %s value = 0x%x\n", __func__, "ENABLE", vmac_readl(ap, ENABLE));
	printk("func::%s vmac register %s value = 0x%x\n", __func__, "CONTROL", vmac_readl(ap, CONTROL));
	printk("func::%s vmac register %s value = 0x%x\n", __func__, "ADDRL", vmac_readl(ap, ADDRL));
	printk("func::%s vmac register %s value = 0x%x\n", __func__, "ADDRH", vmac_readl(ap, ADDRH));
	
	return 0;
}
#endif

int vmac_open(struct net_device *dev)
{
	struct vmac_priv *ap = netdev_priv(dev);
	struct phy_device *phydev;
	unsigned int temp;
	int err = 0;
	struct clk *mac_clk = NULL;
	struct clk *mac_parent = NULL;
	struct clk *arm_clk = NULL;
	struct rk29_vmac_platform_data *pdata = ap->pdev->dev.platform_data;

	printk("enter func %s...\n", __func__);

	if (ap == NULL)
		return -ENODEV;

	wake_lock_timeout(&ap->resume_lock, 5*HZ);

	ap->shutdown = 0;
		
	//set rmii ref clock 50MHz
	mac_clk = clk_get(NULL, "mac_ref_div");
	if (IS_ERR(mac_clk))
		mac_clk = NULL;
	arm_clk = clk_get(NULL, "arm_pll");
	if (IS_ERR(arm_clk))
		arm_clk = NULL;
	if (mac_clk) {
		mac_parent = clk_get_parent(mac_clk);
		if (IS_ERR(mac_parent))
			mac_parent = NULL;
	}
	if (arm_clk && mac_parent && (arm_clk == mac_parent))
		wake_lock(&idlelock);
	
	clk_set_rate(mac_clk, 50000000);
	clk_enable(mac_clk);
	clk_enable(clk_get(NULL,"mii_rx"));
	clk_enable(clk_get(NULL,"mii_tx"));
	clk_enable(clk_get(NULL,"hclk_mac"));
	clk_enable(clk_get(NULL,"mac_ref"));

	//phy power on
	if (pdata && pdata->rmii_power_control)
		pdata->rmii_power_control(1);

	msleep(1000);

	vmac_hw_init(dev);

	/* mac address changed? */
	write_mac_reg(dev, dev->dev_addr);

	err = alloc_buffers(dev);
	if (err)
		goto err_out;

	err = request_irq(dev->irq, &vmac_intr, 0, dev->name, dev);
	if (err) {
		dev_err(&ap->pdev->dev, "Unable to request IRQ %d (error %d)\n",
				dev->irq, err);
		goto err_free_buffers;
	}

	/* install DMA ring pointers */
	vmac_writel(ap, ap->rxbd_dma, RXRINGPTR);
	vmac_writel(ap, ap->txbd_dma, TXRINGPTR);

	/* set poll rate to 1 ms */
	vmac_writel(ap, POLLRATE_TIME, POLLRATE);

	/* make sure we enable napi before rx interrupt  */
	napi_enable(&ap->napi);

	/* IRQ mask */
	temp = RXINT_MASK | ERR_MASK | TXCH_MASK | MDIO_MASK;
	vmac_writel(ap, temp, ENABLE);

	/* Set control */
	temp = (RX_BDT_LEN << 24) | (TX_BDT_LEN << 16) | TXRN_MASK | RXRN_MASK;
	vmac_writel(ap, temp, CONTROL);

	/* enable, after all other bits are set */
	vmac_writel(ap, temp | EN_MASK, CONTROL);
	
	netif_start_queue(dev);
	netif_carrier_off(dev);

#ifdef DEBUG
	vmac_register_print(dev);
#endif

	/* register the PHY board fixup, if needed */
	err = vmac_mii_init(ap);
	if (err)
		goto err_free_irq;

	/* schedule a link state check */
	phy_start(ap->phy_dev);

	phydev = ap->phy_dev;
	dev_info(&ap->pdev->dev, "PHY driver [%s] (mii_bus:phy_addr=%s, irq=%d)\n",
	       phydev->drv->name, dev_name(&phydev->dev), phydev->irq);

	ap->suspending = 0;
	ap->open_flag = 1;

	return 0;

err_free_irq:
	free_irq(dev->irq, dev);
err_free_buffers:
	free_buffers(dev);
err_out:	
	if (arm_clk && mac_parent && (arm_clk == mac_parent))
		wake_unlock(&idlelock);

	return err;
}

int vmac_close(struct net_device *dev)
{
	struct vmac_priv *ap = netdev_priv(dev);
	unsigned int temp;
	struct clk *mac_clk = NULL;
	struct clk *arm_clk = NULL;
	struct clk *mac_parent = NULL;
	struct rk29_vmac_platform_data *pdata = ap->pdev->dev.platform_data;

	printk("enter func %s...\n", __func__);
	
	if (ap->suspending == 1) 
		return 0;

	ap->open_flag = 0;

	netif_stop_queue(dev);
	napi_disable(&ap->napi);

	/* stop running transfers */
	temp = vmac_readl(ap, CONTROL);
	temp &= ~(TXRN_MASK | RXRN_MASK);
	vmac_writel(ap, temp, CONTROL);

	del_timer_sync(&ap->rx_timeout);

	/* disable phy */
	phy_stop(ap->phy_dev);
	vmac_mii_exit(dev);
	netif_carrier_off(dev);

	/* disable interrupts */
	vmac_writel(ap, 0, ENABLE);
	free_irq(dev->irq, dev);

	/* turn off vmac */
	vmac_writel(ap, 0, CONTROL);
	/* vmac_reset_hw(vmac) */

	ap->shutdown = 1;
	wmb();

	free_buffers(dev);

	//phy power off
	if (pdata && pdata->rmii_power_control)
		pdata->rmii_power_control(0);

	//clock close
	mac_clk = clk_get(NULL, "mac_ref_div");
	if (IS_ERR(mac_clk))
		mac_clk = NULL;
	if (mac_clk) {
		mac_parent = clk_get_parent(mac_clk);
		if (IS_ERR(mac_parent))
			mac_parent = NULL;
	}
	arm_clk = clk_get(NULL, "arm_pll");
	if (IS_ERR(arm_clk))
		arm_clk = NULL;

	if (arm_clk && mac_parent && (arm_clk == mac_parent))
		wake_unlock(&idlelock);
	
	clk_disable(mac_clk);
	clk_disable(clk_get(NULL,"mii_rx"));
	clk_disable(clk_get(NULL,"mii_tx"));
	clk_disable(clk_get(NULL,"hclk_mac"));
	clk_disable(clk_get(NULL,"mac_ref"));

	return 0;
}

int vmac_shutdown(struct net_device *dev)
{
	struct vmac_priv *ap = netdev_priv(dev);
	unsigned int temp;
	
	printk("enter func %s...\n", __func__);

	netif_stop_queue(dev);
	napi_disable(&ap->napi);

	/* stop running transfers */
	temp = vmac_readl(ap, CONTROL);
	temp &= ~(TXRN_MASK | RXRN_MASK);
	vmac_writel(ap, temp, CONTROL);

	del_timer_sync(&ap->rx_timeout);

	/* disable phy */
	phy_stop(ap->phy_dev);
	vmac_mii_exit(dev);
	netif_carrier_off(dev);

	/* disable interrupts */
	vmac_writel(ap, 0, ENABLE);
	free_irq(dev->irq, dev);

	/* turn off vmac */
	vmac_writel(ap, 0, CONTROL);
	/* vmac_reset_hw(vmac) */

	ap->shutdown = 1;
	wmb();

	free_buffers(dev);

	return 0;
}

void vmac_update_stats(struct vmac_priv *ap)
{
	struct net_device_stats *_stats = &ap->stats;
	unsigned long miss, rxerr;
	unsigned long rxfram, rxcrc, rxoflow;

	/* compare with /proc/net/dev,
	 * see net/core/dev.c:dev_seq_printf_stats */

	/* rx stats */
	rxerr = vmac_readl(ap, RXERR);
	miss = vmac_readl(ap, MISS);

	rxcrc = (rxerr & RXERR_CRC);
	rxfram = (rxerr & RXERR_FRM) >> 8;
	rxoflow = (rxerr & RXERR_OFLO) >> 16;

	_stats->rx_length_errors = 0;
	_stats->rx_over_errors += miss;
	_stats->rx_crc_errors += rxcrc;
	_stats->rx_frame_errors += rxfram;
	_stats->rx_fifo_errors += rxoflow;
	_stats->rx_missed_errors = 0;

	/* TODO check rx_dropped/rx_errors/tx_dropped/tx_errors have not
	 * been updated elsewhere */
	_stats->rx_dropped = _stats->rx_over_errors +
		_stats->rx_fifo_errors +
		ap->rx_merge_error;

	_stats->rx_errors = _stats->rx_length_errors + _stats->rx_crc_errors +
		_stats->rx_frame_errors +
		_stats->rx_missed_errors +
		_stats->rx_dropped;

	/* tx stats */
	_stats->tx_dropped = 0; /* otherwise queue stopped */

	_stats->tx_errors = _stats->tx_aborted_errors +
		_stats->tx_carrier_errors +
		_stats->tx_fifo_errors +
		_stats->tx_heartbeat_errors +
		_stats->tx_window_errors +
		_stats->tx_dropped +
		ap->tx_timeout_error;
}

struct net_device_stats *vmac_stats(struct net_device *dev)
{
	struct vmac_priv *ap = netdev_priv(dev);
	unsigned long flags;

	spin_lock_irqsave(&ap->lock, flags);
	vmac_update_stats(ap);
	spin_unlock_irqrestore(&ap->lock, flags);

	return &ap->stats;
}

void vmac_tx_timeout(struct net_device *dev)
{
	struct vmac_priv *ap = netdev_priv(dev);
	unsigned int status;
	unsigned long flags;

	spin_lock_irqsave(&ap->lock, flags);

	/* queue did not progress for timeo jiffies */
	WARN_ON(!netif_queue_stopped(dev));
	WARN_ON(!fifo_full(&ap->tx_ring));

	/* TX IRQ lost? */
	status = vmac_readl(ap, STAT);
	if (status & TXINT_MASK) {
		dev_err(&ap->pdev->dev, "lost tx interrupt, IRQ mask %x\n",
				vmac_readl(ap, ENABLE));
		vmac_writel(ap, TXINT_MASK, STAT);
	}

	/* TODO RX/MDIO/ERR as well? */

	vmac_tx_reclaim(dev, 0);
	if (fifo_full(&ap->tx_ring))
		dev_err(&ap->pdev->dev, "DMA state machine not active\n");

	/* We can accept TX packets again */
	ap->tx_timeout_error++;
	dev->trans_start = jiffies;
	netif_wake_queue(dev);

	spin_unlock_irqrestore(&ap->lock, flags);
}

static void create_multicast_filter(struct net_device *dev,
	unsigned long *bitmask)
{
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 34))
	struct netdev_hw_addr *ha;
	unsigned long crc;
	char *addrs;
	struct netdev_hw_addr_list *list = &dev->dev_addrs;
	
	//printk("-----------------func %s-------------------\n", __func__);

	WARN_ON(dev->mc_count == 0);
	WARN_ON(dev->flags & IFF_ALLMULTI);

	bitmask[0] = bitmask[1] = 0;

	list_for_each_entry(ha, &list->list, list) {
		addrs = ha->addr;

		/* skip non-multicast addresses */
		if (!(*addrs & 1))
			continue;

		crc = ether_crc_le(ETH_ALEN, addrs);
		set_bit(crc >> 26, bitmask);
		
	}
#else
	struct netdev_hw_addr *ha;
	unsigned long crc;
	char *addrs;

	WARN_ON(netdev_mc_count(dev) == 0);
	WARN_ON(dev->flags & IFF_ALLMULTI);

	bitmask[0] = bitmask[1] = 0;

	netdev_for_each_mc_addr(ha, dev) {
		addrs = ha->addr;

		/* skip non-multicast addresses */
		if (!(*addrs & 1))
			continue;

		crc = ether_crc_le(ETH_ALEN, addrs);
		set_bit(crc >> 26, bitmask);
	}
#endif
}
static void vmac_set_multicast_list(struct net_device *dev)
{
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 34))
	struct vmac_priv *ap = netdev_priv(dev);
	unsigned long flags, bitmask[2];
	int promisc, reg;

	//printk("-----------------func %s-------------------\n", __func__);

	spin_lock_irqsave(&ap->lock, flags);

	promisc = !!(dev->flags & IFF_PROMISC);
	reg = vmac_readl(ap, CONTROL);
	if (promisc != !!(reg & PROM_MASK)) {
		reg ^= PROM_MASK;
		vmac_writel(ap, reg, CONTROL);
	}

	if (dev->flags & IFF_ALLMULTI)
		memset(bitmask, 1, sizeof(bitmask));
	else if (dev->mc_count == 0)
		memset(bitmask, 0, sizeof(bitmask));
	else
		create_multicast_filter(dev, bitmask);

	vmac_writel(ap, bitmask[0], LAFL);
	vmac_writel(ap, bitmask[1], LAFH);

	spin_unlock_irqrestore(&ap->lock, flags);
#else
	struct vmac_priv *ap = netdev_priv(dev);
	unsigned long flags, bitmask[2];
	int promisc, reg;

	spin_lock_irqsave(&ap->lock, flags);

	promisc = !!(dev->flags & IFF_PROMISC);
	reg = vmac_readl(ap, ENABLE);
	if (promisc != !!(reg & PROM_MASK)) {
		reg ^= PROM_MASK;
		vmac_writel(ap, reg, ENABLE);
	}

	if (dev->flags & IFF_ALLMULTI)
		memset(bitmask, 1, sizeof(bitmask));
	else if (netdev_mc_count(dev) == 0)
		memset(bitmask, 0, sizeof(bitmask));
	else
		create_multicast_filter(dev, bitmask);

	vmac_writel(ap, bitmask[0], LAFL);
	vmac_writel(ap, bitmask[1], LAFH);

	spin_unlock_irqrestore(&ap->lock, flags);
#endif
}

static struct ethtool_ops vmac_ethtool_ops = {
	.get_settings		= vmacether_get_settings,
	.set_settings		= vmacether_set_settings,
	.get_drvinfo		= vmacether_get_drvinfo,
	.get_link		= ethtool_op_get_link,
};

static const struct net_device_ops vmac_netdev_ops = {
	.ndo_open		= vmac_open,
	.ndo_stop		= vmac_close,
	.ndo_get_stats		= vmac_stats,
	.ndo_start_xmit		= vmac_start_xmit,
	.ndo_do_ioctl		= vmac_ioctl,
	.ndo_set_mac_address	= eth_mac_addr,
	.ndo_tx_timeout		= vmac_tx_timeout,
	.ndo_set_multicast_list = vmac_set_multicast_list,
	.ndo_validate_addr	= eth_validate_addr,
	.ndo_change_mtu		= eth_change_mtu,
};

static int __devinit vmac_probe(struct platform_device *pdev)
{
	struct net_device *dev;
	struct vmac_priv *ap;
	struct resource *res;
	unsigned int mem_base, mem_size, irq;
	int err;
	struct rk29_vmac_platform_data *pdata = pdev->dev.platform_data;

	dev = alloc_etherdev(sizeof(*ap));
	if (!dev) {
		dev_err(&pdev->dev, "etherdev alloc failed, aborting.\n");
		return -ENOMEM;
	}

	ap = netdev_priv(dev);

	err = -ENODEV;
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(&pdev->dev, "no mmio resource defined\n");
		goto err_out;
	}
	mem_base = res->start;
	mem_size = resource_size(res);
	irq = platform_get_irq(pdev, 0);

	err = -EBUSY;
	if (!request_mem_region(mem_base, mem_size, VMAC_NAME)) {
		dev_err(&pdev->dev, "no memory region available\n");
		goto err_out;
	}

	err = -ENOMEM;
	ap->regs = ioremap(mem_base, mem_size);
	if (!ap->regs) {
		dev_err(&pdev->dev, "failed to map registers, aborting.\n");
		goto err_out_release_mem;
	}

	/* no checksum support, hence no scatter/gather */
	dev->features |= NETIF_F_HIGHDMA;

	spin_lock_init(&ap->lock);

	SET_NETDEV_DEV(dev, &pdev->dev);
	ap->dev = dev;
	ap->pdev = pdev;

	/* init rx timeout (used for oom) */
	init_timer(&ap->rx_timeout);
	ap->rx_timeout.function = vmac_refill_rx_timer;
	ap->rx_timeout.data = (unsigned long)dev;

	netif_napi_add(dev, &ap->napi, vmac_poll, 2);
	dev->netdev_ops = &vmac_netdev_ops;
	dev->ethtool_ops = &vmac_ethtool_ops;
	dev->irq = irq;

	dev->flags |= IFF_MULTICAST;////////////////////

	dev->base_addr = (unsigned long)ap->regs;
	ap->mem_base = mem_base;

	/* prevent buffer chaining, favor speed over space */
	ap->rx_skb_size = ETH_FRAME_LEN + VMAC_BUFFER_PAD;

	/* private struct functional */

	/* mac address intialize, set vmac_open  */
	read_mac_reg(dev, dev->dev_addr);

	if (!is_valid_ether_addr(dev->dev_addr))
		random_ether_addr(dev->dev_addr);

	err = register_netdev(dev);
	if (err) {
		dev_err(&pdev->dev, "Cannot register net device, aborting.\n");
		goto err_out_iounmap;
	}

	dev_info(&pdev->dev, "ARC VMAC at 0x%08x irq %d %pM\n", mem_base,
	    dev->irq, dev->dev_addr);
	platform_set_drvdata(pdev, dev);

	ap->suspending = 0;
	ap->open_flag = 0;
	wake_lock_init(&idlelock, WAKE_LOCK_IDLE, "vmac");
	wake_lock_init(&ap->resume_lock, WAKE_LOCK_SUSPEND, "vmac_resume");

	//config rk29 vmac as rmii, 100MHz 
	if (pdata && pdata->vmac_register_set)
		pdata->vmac_register_set();

	//power gpio init, phy power off default for power reduce
	if (pdata && pdata->rmii_io_init)
		pdata->rmii_io_init();

	return 0;

err_out_iounmap:
	iounmap(ap->regs);
err_out_release_mem:
	release_mem_region(mem_base, mem_size);
err_out:
	free_netdev(dev);
	return err;
}

static int __devexit vmac_remove(struct platform_device *pdev)
{
	struct net_device *dev;
	struct vmac_priv *ap;
	struct resource *res;
	struct rk29_vmac_platform_data *pdata = pdev->dev.platform_data;

	wake_lock_destroy(&idlelock);

	//power gpio deinit, phy power off
	if (pdata && pdata->rmii_io_deinit)
		pdata->rmii_io_deinit();

	dev = platform_get_drvdata(pdev);
	if (!dev) {
		dev_err(&pdev->dev, "%s no valid dev found\n", __func__);
		return 0;
	}

	ap = netdev_priv(dev);

	/* MAC */
	unregister_netdev(dev);
	iounmap(ap->regs);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	release_mem_region(res->start, resource_size(res));

	platform_set_drvdata(pdev, NULL);
	free_netdev(dev);
	return 0;
}

static void rk29_vmac_power_off(struct net_device *dev)
{
	struct vmac_priv *ap = netdev_priv(dev);
	struct rk29_vmac_platform_data *pdata = ap->pdev->dev.platform_data;

	printk("enter func %s...\n", __func__);

	//phy power off
	if (pdata && pdata->rmii_power_control)
		pdata->rmii_power_control(0);

	//clock close
	clk_disable(clk_get(NULL, "mac_ref_div"));
	clk_disable(clk_get(NULL,"mii_rx"));
	clk_disable(clk_get(NULL,"mii_tx"));
	clk_disable(clk_get(NULL,"hclk_mac"));
	clk_disable(clk_get(NULL,"mac_ref"));

}

static int
rk29_vmac_suspend(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct net_device *ndev = platform_get_drvdata(pdev);
	struct vmac_priv *ap = netdev_priv(ndev);
	
	if (ndev) {
		if (ap->open_flag == 1) {
			netif_stop_queue(ndev);
			netif_device_detach(ndev);
			if (ap->suspending == 0) {
				vmac_shutdown(ndev);
				rk29_vmac_power_off(ndev);
				ap->suspending = 1;
			}
		}
	}
	return 0;
}

static int
rk29_vmac_resume(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct net_device *ndev = platform_get_drvdata(pdev);
	struct vmac_priv *ap = netdev_priv(ndev);
	
	if (ndev) {
		if (ap->open_flag == 1) {
			netif_device_attach(ndev);
			netif_start_queue(ndev);
		}
	}
	return 0;
}

static struct dev_pm_ops rk29_vmac_pm_ops = {
	.suspend	= rk29_vmac_suspend,
	.resume 	= rk29_vmac_resume,
};


static struct platform_driver rk29_vmac_driver = {
	.probe		= vmac_probe,
	.remove		= __devexit_p(vmac_remove),
	.driver		= {
		.name		= "rk29 vmac",
		.owner	 = THIS_MODULE,
		.pm  = &rk29_vmac_pm_ops,
	},
};

static int __init vmac_init(void)
{
	return platform_driver_register(&rk29_vmac_driver);
}

static void __exit vmac_exit(void)
{
	platform_driver_unregister(&rk29_vmac_driver);
}

module_init(vmac_init);
module_exit(vmac_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("RK29 VMAC Ethernet driver");
MODULE_AUTHOR("amit.bhor@celunite.com, sameer.dhavale@celunite.com, andreas.fenkart@streamunlimited.com");
