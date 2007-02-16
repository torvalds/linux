/*
 * Atmel MACB Ethernet Controller driver
 *
 * Copyright (C) 2004-2006 Atmel Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/clk.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/mii.h>
#include <linux/mutex.h>
#include <linux/dma-mapping.h>
#include <linux/ethtool.h>
#include <linux/platform_device.h>

#include <asm/arch/board.h>

#include "macb.h"

#define RX_BUFFER_SIZE		128
#define RX_RING_SIZE		512
#define RX_RING_BYTES		(sizeof(struct dma_desc) * RX_RING_SIZE)

/* Make the IP header word-aligned (the ethernet header is 14 bytes) */
#define RX_OFFSET		2

#define TX_RING_SIZE		128
#define DEF_TX_RING_PENDING	(TX_RING_SIZE - 1)
#define TX_RING_BYTES		(sizeof(struct dma_desc) * TX_RING_SIZE)

#define TX_RING_GAP(bp)						\
	(TX_RING_SIZE - (bp)->tx_pending)
#define TX_BUFFS_AVAIL(bp)					\
	(((bp)->tx_tail <= (bp)->tx_head) ?			\
	 (bp)->tx_tail + (bp)->tx_pending - (bp)->tx_head :	\
	 (bp)->tx_tail - (bp)->tx_head - TX_RING_GAP(bp))
#define NEXT_TX(n)		(((n) + 1) & (TX_RING_SIZE - 1))

#define NEXT_RX(n)		(((n) + 1) & (RX_RING_SIZE - 1))

/* minimum number of free TX descriptors before waking up TX process */
#define MACB_TX_WAKEUP_THRESH	(TX_RING_SIZE / 4)

#define MACB_RX_INT_FLAGS	(MACB_BIT(RCOMP) | MACB_BIT(RXUBR)	\
				 | MACB_BIT(ISR_ROVR))

static void __macb_set_hwaddr(struct macb *bp)
{
	u32 bottom;
	u16 top;

	bottom = cpu_to_le32(*((u32 *)bp->dev->dev_addr));
	macb_writel(bp, SA1B, bottom);
	top = cpu_to_le16(*((u16 *)(bp->dev->dev_addr + 4)));
	macb_writel(bp, SA1T, top);
}

static void __init macb_get_hwaddr(struct macb *bp)
{
	u32 bottom;
	u16 top;
	u8 addr[6];

	bottom = macb_readl(bp, SA1B);
	top = macb_readl(bp, SA1T);

	addr[0] = bottom & 0xff;
	addr[1] = (bottom >> 8) & 0xff;
	addr[2] = (bottom >> 16) & 0xff;
	addr[3] = (bottom >> 24) & 0xff;
	addr[4] = top & 0xff;
	addr[5] = (top >> 8) & 0xff;

	if (is_valid_ether_addr(addr))
		memcpy(bp->dev->dev_addr, addr, sizeof(addr));
}

static void macb_enable_mdio(struct macb *bp)
{
	unsigned long flags;
	u32 reg;

	spin_lock_irqsave(&bp->lock, flags);
	reg = macb_readl(bp, NCR);
	reg |= MACB_BIT(MPE);
	macb_writel(bp, NCR, reg);
	macb_writel(bp, IER, MACB_BIT(MFD));
	spin_unlock_irqrestore(&bp->lock, flags);
}

static void macb_disable_mdio(struct macb *bp)
{
	unsigned long flags;
	u32 reg;

	spin_lock_irqsave(&bp->lock, flags);
	reg = macb_readl(bp, NCR);
	reg &= ~MACB_BIT(MPE);
	macb_writel(bp, NCR, reg);
	macb_writel(bp, IDR, MACB_BIT(MFD));
	spin_unlock_irqrestore(&bp->lock, flags);
}

static int macb_mdio_read(struct net_device *dev, int phy_id, int location)
{
	struct macb *bp = netdev_priv(dev);
	int value;

	mutex_lock(&bp->mdio_mutex);

	macb_enable_mdio(bp);
	macb_writel(bp, MAN, (MACB_BF(SOF, MACB_MAN_SOF)
			      | MACB_BF(RW, MACB_MAN_READ)
			      | MACB_BF(PHYA, phy_id)
			      | MACB_BF(REGA, location)
			      | MACB_BF(CODE, MACB_MAN_CODE)));

	wait_for_completion(&bp->mdio_complete);

	value = MACB_BFEXT(DATA, macb_readl(bp, MAN));
	macb_disable_mdio(bp);
	mutex_unlock(&bp->mdio_mutex);

	return value;
}

static void macb_mdio_write(struct net_device *dev, int phy_id,
			    int location, int val)
{
	struct macb *bp = netdev_priv(dev);

	dev_dbg(&bp->pdev->dev, "mdio_write %02x:%02x <- %04x\n",
		phy_id, location, val);

	mutex_lock(&bp->mdio_mutex);
	macb_enable_mdio(bp);

	macb_writel(bp, MAN, (MACB_BF(SOF, MACB_MAN_SOF)
			      | MACB_BF(RW, MACB_MAN_WRITE)
			      | MACB_BF(PHYA, phy_id)
			      | MACB_BF(REGA, location)
			      | MACB_BF(CODE, MACB_MAN_CODE)
			      | MACB_BF(DATA, val)));

	wait_for_completion(&bp->mdio_complete);

	macb_disable_mdio(bp);
	mutex_unlock(&bp->mdio_mutex);
}

static int macb_phy_probe(struct macb *bp)
{
	int phy_address;
	u16 phyid1, phyid2;

	for (phy_address = 0; phy_address < 32; phy_address++) {
		phyid1 = macb_mdio_read(bp->dev, phy_address, MII_PHYSID1);
		phyid2 = macb_mdio_read(bp->dev, phy_address, MII_PHYSID2);

		if (phyid1 != 0xffff && phyid1 != 0x0000
		    && phyid2 != 0xffff && phyid2 != 0x0000)
			break;
	}

	if (phy_address == 32)
		return -ENODEV;

	dev_info(&bp->pdev->dev,
		 "detected PHY at address %d (ID %04x:%04x)\n",
		 phy_address, phyid1, phyid2);

	bp->mii.phy_id = phy_address;
	return 0;
}

static void macb_set_media(struct macb *bp, int media)
{
	u32 reg;

	spin_lock_irq(&bp->lock);
	reg = macb_readl(bp, NCFGR);
	reg &= ~(MACB_BIT(SPD) | MACB_BIT(FD));
	if (media & (ADVERTISE_100HALF | ADVERTISE_100FULL))
		reg |= MACB_BIT(SPD);
	if (media & ADVERTISE_FULL)
		reg |= MACB_BIT(FD);
	macb_writel(bp, NCFGR, reg);
	spin_unlock_irq(&bp->lock);
}

static void macb_check_media(struct macb *bp, int ok_to_print, int init_media)
{
	struct mii_if_info *mii = &bp->mii;
	unsigned int old_carrier, new_carrier;
	int advertise, lpa, media, duplex;

	/* if forced media, go no further */
	if (mii->force_media)
		return;

	/* check current and old link status */
	old_carrier = netif_carrier_ok(mii->dev) ? 1 : 0;
	new_carrier = (unsigned int) mii_link_ok(mii);

	/* if carrier state did not change, assume nothing else did */
	if (!init_media && old_carrier == new_carrier)
		return;

	/* no carrier, nothing much to do */
	if (!new_carrier) {
		netif_carrier_off(mii->dev);
		printk(KERN_INFO "%s: link down\n", mii->dev->name);
		return;
	}

	/*
	 * we have carrier, see who's on the other end
	 */
	netif_carrier_on(mii->dev);

	/* get MII advertise and LPA values */
	if (!init_media && mii->advertising) {
		advertise = mii->advertising;
	} else {
		advertise = mii->mdio_read(mii->dev, mii->phy_id, MII_ADVERTISE);
		mii->advertising = advertise;
	}
	lpa = mii->mdio_read(mii->dev, mii->phy_id, MII_LPA);

	/* figure out media and duplex from advertise and LPA values */
	media = mii_nway_result(lpa & advertise);
	duplex = (media & ADVERTISE_FULL) ? 1 : 0;

	if (ok_to_print)
		printk(KERN_INFO "%s: link up, %sMbps, %s-duplex, lpa 0x%04X\n",
		       mii->dev->name,
		       media & (ADVERTISE_100FULL | ADVERTISE_100HALF) ? "100" : "10",
		       duplex ? "full" : "half", lpa);

	mii->full_duplex = duplex;

	/* Let the MAC know about the new link state */
	macb_set_media(bp, media);
}

static void macb_update_stats(struct macb *bp)
{
	u32 __iomem *reg = bp->regs + MACB_PFR;
	u32 *p = &bp->hw_stats.rx_pause_frames;
	u32 *end = &bp->hw_stats.tx_pause_frames + 1;

	WARN_ON((unsigned long)(end - p - 1) != (MACB_TPF - MACB_PFR) / 4);

	for(; p < end; p++, reg++)
		*p += __raw_readl(reg);
}

static void macb_periodic_task(struct work_struct *work)
{
	struct macb *bp = container_of(work, struct macb, periodic_task.work);

	macb_update_stats(bp);
	macb_check_media(bp, 1, 0);

	schedule_delayed_work(&bp->periodic_task, HZ);
}

static void macb_tx(struct macb *bp)
{
	unsigned int tail;
	unsigned int head;
	u32 status;

	status = macb_readl(bp, TSR);
	macb_writel(bp, TSR, status);

	dev_dbg(&bp->pdev->dev, "macb_tx status = %02lx\n",
		(unsigned long)status);

	if (status & MACB_BIT(UND)) {
		printk(KERN_ERR "%s: TX underrun, resetting buffers\n",
		       bp->dev->name);
		bp->tx_head = bp->tx_tail = 0;
	}

	if (!(status & MACB_BIT(COMP)))
		/*
		 * This may happen when a buffer becomes complete
		 * between reading the ISR and scanning the
		 * descriptors.  Nothing to worry about.
		 */
		return;

	head = bp->tx_head;
	for (tail = bp->tx_tail; tail != head; tail = NEXT_TX(tail)) {
		struct ring_info *rp = &bp->tx_skb[tail];
		struct sk_buff *skb = rp->skb;
		u32 bufstat;

		BUG_ON(skb == NULL);

		rmb();
		bufstat = bp->tx_ring[tail].ctrl;

		if (!(bufstat & MACB_BIT(TX_USED)))
			break;

		dev_dbg(&bp->pdev->dev, "skb %u (data %p) TX complete\n",
			tail, skb->data);
		dma_unmap_single(&bp->pdev->dev, rp->mapping, skb->len,
				 DMA_TO_DEVICE);
		bp->stats.tx_packets++;
		bp->stats.tx_bytes += skb->len;
		rp->skb = NULL;
		dev_kfree_skb_irq(skb);
	}

	bp->tx_tail = tail;
	if (netif_queue_stopped(bp->dev) &&
	    TX_BUFFS_AVAIL(bp) > MACB_TX_WAKEUP_THRESH)
		netif_wake_queue(bp->dev);
}

static int macb_rx_frame(struct macb *bp, unsigned int first_frag,
			 unsigned int last_frag)
{
	unsigned int len;
	unsigned int frag;
	unsigned int offset = 0;
	struct sk_buff *skb;

	len = MACB_BFEXT(RX_FRMLEN, bp->rx_ring[last_frag].ctrl);

	dev_dbg(&bp->pdev->dev, "macb_rx_frame frags %u - %u (len %u)\n",
		first_frag, last_frag, len);

	skb = dev_alloc_skb(len + RX_OFFSET);
	if (!skb) {
		bp->stats.rx_dropped++;
		for (frag = first_frag; ; frag = NEXT_RX(frag)) {
			bp->rx_ring[frag].addr &= ~MACB_BIT(RX_USED);
			if (frag == last_frag)
				break;
		}
		wmb();
		return 1;
	}

	skb_reserve(skb, RX_OFFSET);
	skb->dev = bp->dev;
	skb->ip_summed = CHECKSUM_NONE;
	skb_put(skb, len);

	for (frag = first_frag; ; frag = NEXT_RX(frag)) {
		unsigned int frag_len = RX_BUFFER_SIZE;

		if (offset + frag_len > len) {
			BUG_ON(frag != last_frag);
			frag_len = len - offset;
		}
		memcpy(skb->data + offset,
		       bp->rx_buffers + (RX_BUFFER_SIZE * frag),
		       frag_len);
		offset += RX_BUFFER_SIZE;
		bp->rx_ring[frag].addr &= ~MACB_BIT(RX_USED);
		wmb();

		if (frag == last_frag)
			break;
	}

	skb->protocol = eth_type_trans(skb, bp->dev);

	bp->stats.rx_packets++;
	bp->stats.rx_bytes += len;
	bp->dev->last_rx = jiffies;
	dev_dbg(&bp->pdev->dev, "received skb of length %u, csum: %08x\n",
		skb->len, skb->csum);
	netif_receive_skb(skb);

	return 0;
}

/* Mark DMA descriptors from begin up to and not including end as unused */
static void discard_partial_frame(struct macb *bp, unsigned int begin,
				  unsigned int end)
{
	unsigned int frag;

	for (frag = begin; frag != end; frag = NEXT_RX(frag))
		bp->rx_ring[frag].addr &= ~MACB_BIT(RX_USED);
	wmb();

	/*
	 * When this happens, the hardware stats registers for
	 * whatever caused this is updated, so we don't have to record
	 * anything.
	 */
}

static int macb_rx(struct macb *bp, int budget)
{
	int received = 0;
	unsigned int tail = bp->rx_tail;
	int first_frag = -1;

	for (; budget > 0; tail = NEXT_RX(tail)) {
		u32 addr, ctrl;

		rmb();
		addr = bp->rx_ring[tail].addr;
		ctrl = bp->rx_ring[tail].ctrl;

		if (!(addr & MACB_BIT(RX_USED)))
			break;

		if (ctrl & MACB_BIT(RX_SOF)) {
			if (first_frag != -1)
				discard_partial_frame(bp, first_frag, tail);
			first_frag = tail;
		}

		if (ctrl & MACB_BIT(RX_EOF)) {
			int dropped;
			BUG_ON(first_frag == -1);

			dropped = macb_rx_frame(bp, first_frag, tail);
			first_frag = -1;
			if (!dropped) {
				received++;
				budget--;
			}
		}
	}

	if (first_frag != -1)
		bp->rx_tail = first_frag;
	else
		bp->rx_tail = tail;

	return received;
}

static int macb_poll(struct net_device *dev, int *budget)
{
	struct macb *bp = netdev_priv(dev);
	int orig_budget, work_done, retval = 0;
	u32 status;

	status = macb_readl(bp, RSR);
	macb_writel(bp, RSR, status);

	if (!status) {
		/*
		 * This may happen if an interrupt was pending before
		 * this function was called last time, and no packets
		 * have been received since.
		 */
		netif_rx_complete(dev);
		goto out;
	}

	dev_dbg(&bp->pdev->dev, "poll: status = %08lx, budget = %d\n",
		(unsigned long)status, *budget);

	if (!(status & MACB_BIT(REC))) {
		dev_warn(&bp->pdev->dev,
			 "No RX buffers complete, status = %02lx\n",
			 (unsigned long)status);
		netif_rx_complete(dev);
		goto out;
	}

	orig_budget = *budget;
	if (orig_budget > dev->quota)
		orig_budget = dev->quota;

	work_done = macb_rx(bp, orig_budget);
	if (work_done < orig_budget) {
		netif_rx_complete(dev);
		retval = 0;
	} else {
		retval = 1;
	}

	/*
	 * We've done what we can to clean the buffers. Make sure we
	 * get notified when new packets arrive.
	 */
out:
	macb_writel(bp, IER, MACB_RX_INT_FLAGS);

	/* TODO: Handle errors */

	return retval;
}

static irqreturn_t macb_interrupt(int irq, void *dev_id)
{
	struct net_device *dev = dev_id;
	struct macb *bp = netdev_priv(dev);
	u32 status;

	status = macb_readl(bp, ISR);

	if (unlikely(!status))
		return IRQ_NONE;

	spin_lock(&bp->lock);

	while (status) {
		if (status & MACB_BIT(MFD))
			complete(&bp->mdio_complete);

		/* close possible race with dev_close */
		if (unlikely(!netif_running(dev))) {
			macb_writel(bp, IDR, ~0UL);
			break;
		}

		if (status & MACB_RX_INT_FLAGS) {
			if (netif_rx_schedule_prep(dev)) {
				/*
				 * There's no point taking any more interrupts
				 * until we have processed the buffers
				 */
				macb_writel(bp, IDR, MACB_RX_INT_FLAGS);
				dev_dbg(&bp->pdev->dev, "scheduling RX softirq\n");
				__netif_rx_schedule(dev);
			}
		}

		if (status & (MACB_BIT(TCOMP) | MACB_BIT(ISR_TUND)))
			macb_tx(bp);

		/*
		 * Link change detection isn't possible with RMII, so we'll
		 * add that if/when we get our hands on a full-blown MII PHY.
		 */

		if (status & MACB_BIT(HRESP)) {
			/*
			 * TODO: Reset the hardware, and maybe move the printk
			 * to a lower-priority context as well (work queue?)
			 */
			printk(KERN_ERR "%s: DMA bus error: HRESP not OK\n",
			       dev->name);
		}

		status = macb_readl(bp, ISR);
	}

	spin_unlock(&bp->lock);

	return IRQ_HANDLED;
}

static int macb_start_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct macb *bp = netdev_priv(dev);
	dma_addr_t mapping;
	unsigned int len, entry;
	u32 ctrl;

#ifdef DEBUG
	int i;
	dev_dbg(&bp->pdev->dev,
		"start_xmit: len %u head %p data %p tail %p end %p\n",
		skb->len, skb->head, skb->data, skb->tail, skb->end);
	dev_dbg(&bp->pdev->dev,
		"data:");
	for (i = 0; i < 16; i++)
		printk(" %02x", (unsigned int)skb->data[i]);
	printk("\n");
#endif

	len = skb->len;
	spin_lock_irq(&bp->lock);

	/* This is a hard error, log it. */
	if (TX_BUFFS_AVAIL(bp) < 1) {
		netif_stop_queue(dev);
		spin_unlock_irq(&bp->lock);
		dev_err(&bp->pdev->dev,
			"BUG! Tx Ring full when queue awake!\n");
		dev_dbg(&bp->pdev->dev, "tx_head = %u, tx_tail = %u\n",
			bp->tx_head, bp->tx_tail);
		return 1;
	}

	entry = bp->tx_head;
	dev_dbg(&bp->pdev->dev, "Allocated ring entry %u\n", entry);
	mapping = dma_map_single(&bp->pdev->dev, skb->data,
				 len, DMA_TO_DEVICE);
	bp->tx_skb[entry].skb = skb;
	bp->tx_skb[entry].mapping = mapping;
	dev_dbg(&bp->pdev->dev, "Mapped skb data %p to DMA addr %08lx\n",
		skb->data, (unsigned long)mapping);

	ctrl = MACB_BF(TX_FRMLEN, len);
	ctrl |= MACB_BIT(TX_LAST);
	if (entry == (TX_RING_SIZE - 1))
		ctrl |= MACB_BIT(TX_WRAP);

	bp->tx_ring[entry].addr = mapping;
	bp->tx_ring[entry].ctrl = ctrl;
	wmb();

	entry = NEXT_TX(entry);
	bp->tx_head = entry;

	macb_writel(bp, NCR, macb_readl(bp, NCR) | MACB_BIT(TSTART));

	if (TX_BUFFS_AVAIL(bp) < 1)
		netif_stop_queue(dev);

	spin_unlock_irq(&bp->lock);

	dev->trans_start = jiffies;

	return 0;
}

static void macb_free_consistent(struct macb *bp)
{
	if (bp->tx_skb) {
		kfree(bp->tx_skb);
		bp->tx_skb = NULL;
	}
	if (bp->rx_ring) {
		dma_free_coherent(&bp->pdev->dev, RX_RING_BYTES,
				  bp->rx_ring, bp->rx_ring_dma);
		bp->rx_ring = NULL;
	}
	if (bp->tx_ring) {
		dma_free_coherent(&bp->pdev->dev, TX_RING_BYTES,
				  bp->tx_ring, bp->tx_ring_dma);
		bp->tx_ring = NULL;
	}
	if (bp->rx_buffers) {
		dma_free_coherent(&bp->pdev->dev,
				  RX_RING_SIZE * RX_BUFFER_SIZE,
				  bp->rx_buffers, bp->rx_buffers_dma);
		bp->rx_buffers = NULL;
	}
}

static int macb_alloc_consistent(struct macb *bp)
{
	int size;

	size = TX_RING_SIZE * sizeof(struct ring_info);
	bp->tx_skb = kmalloc(size, GFP_KERNEL);
	if (!bp->tx_skb)
		goto out_err;

	size = RX_RING_BYTES;
	bp->rx_ring = dma_alloc_coherent(&bp->pdev->dev, size,
					 &bp->rx_ring_dma, GFP_KERNEL);
	if (!bp->rx_ring)
		goto out_err;
	dev_dbg(&bp->pdev->dev,
		"Allocated RX ring of %d bytes at %08lx (mapped %p)\n",
		size, (unsigned long)bp->rx_ring_dma, bp->rx_ring);

	size = TX_RING_BYTES;
	bp->tx_ring = dma_alloc_coherent(&bp->pdev->dev, size,
					 &bp->tx_ring_dma, GFP_KERNEL);
	if (!bp->tx_ring)
		goto out_err;
	dev_dbg(&bp->pdev->dev,
		"Allocated TX ring of %d bytes at %08lx (mapped %p)\n",
		size, (unsigned long)bp->tx_ring_dma, bp->tx_ring);

	size = RX_RING_SIZE * RX_BUFFER_SIZE;
	bp->rx_buffers = dma_alloc_coherent(&bp->pdev->dev, size,
					    &bp->rx_buffers_dma, GFP_KERNEL);
	if (!bp->rx_buffers)
		goto out_err;
	dev_dbg(&bp->pdev->dev,
		"Allocated RX buffers of %d bytes at %08lx (mapped %p)\n",
		size, (unsigned long)bp->rx_buffers_dma, bp->rx_buffers);

	return 0;

out_err:
	macb_free_consistent(bp);
	return -ENOMEM;
}

static void macb_init_rings(struct macb *bp)
{
	int i;
	dma_addr_t addr;

	addr = bp->rx_buffers_dma;
	for (i = 0; i < RX_RING_SIZE; i++) {
		bp->rx_ring[i].addr = addr;
		bp->rx_ring[i].ctrl = 0;
		addr += RX_BUFFER_SIZE;
	}
	bp->rx_ring[RX_RING_SIZE - 1].addr |= MACB_BIT(RX_WRAP);

	for (i = 0; i < TX_RING_SIZE; i++) {
		bp->tx_ring[i].addr = 0;
		bp->tx_ring[i].ctrl = MACB_BIT(TX_USED);
	}
	bp->tx_ring[TX_RING_SIZE - 1].ctrl |= MACB_BIT(TX_WRAP);

	bp->rx_tail = bp->tx_head = bp->tx_tail = 0;
}

static void macb_reset_hw(struct macb *bp)
{
	/* Make sure we have the write buffer for ourselves */
	wmb();

	/*
	 * Disable RX and TX (XXX: Should we halt the transmission
	 * more gracefully?)
	 */
	macb_writel(bp, NCR, 0);

	/* Clear the stats registers (XXX: Update stats first?) */
	macb_writel(bp, NCR, MACB_BIT(CLRSTAT));

	/* Clear all status flags */
	macb_writel(bp, TSR, ~0UL);
	macb_writel(bp, RSR, ~0UL);

	/* Disable all interrupts */
	macb_writel(bp, IDR, ~0UL);
	macb_readl(bp, ISR);
}

static void macb_init_hw(struct macb *bp)
{
	u32 config;

	macb_reset_hw(bp);
	__macb_set_hwaddr(bp);

	config = macb_readl(bp, NCFGR) & MACB_BF(CLK, -1L);
	config |= MACB_BIT(PAE);		/* PAuse Enable */
	config |= MACB_BIT(DRFCS);		/* Discard Rx FCS */
	if (bp->dev->flags & IFF_PROMISC)
		config |= MACB_BIT(CAF);	/* Copy All Frames */
	if (!(bp->dev->flags & IFF_BROADCAST))
		config |= MACB_BIT(NBC);	/* No BroadCast */
	macb_writel(bp, NCFGR, config);

	/* Initialize TX and RX buffers */
	macb_writel(bp, RBQP, bp->rx_ring_dma);
	macb_writel(bp, TBQP, bp->tx_ring_dma);

	/* Enable TX and RX */
	macb_writel(bp, NCR, MACB_BIT(RE) | MACB_BIT(TE));

	/* Enable interrupts */
	macb_writel(bp, IER, (MACB_BIT(RCOMP)
			      | MACB_BIT(RXUBR)
			      | MACB_BIT(ISR_TUND)
			      | MACB_BIT(ISR_RLE)
			      | MACB_BIT(TXERR)
			      | MACB_BIT(TCOMP)
			      | MACB_BIT(ISR_ROVR)
			      | MACB_BIT(HRESP)));
}

static void macb_init_phy(struct net_device *dev)
{
	struct macb *bp = netdev_priv(dev);

	/* Set some reasonable default settings */
	macb_mdio_write(dev, bp->mii.phy_id, MII_ADVERTISE,
			ADVERTISE_CSMA | ADVERTISE_ALL);
	macb_mdio_write(dev, bp->mii.phy_id, MII_BMCR,
			(BMCR_SPEED100 | BMCR_ANENABLE
			 | BMCR_ANRESTART | BMCR_FULLDPLX));
}

static int macb_open(struct net_device *dev)
{
	struct macb *bp = netdev_priv(dev);
	int err;

	dev_dbg(&bp->pdev->dev, "open\n");

	if (!is_valid_ether_addr(dev->dev_addr))
		return -EADDRNOTAVAIL;

	err = macb_alloc_consistent(bp);
	if (err) {
		printk(KERN_ERR
		       "%s: Unable to allocate DMA memory (error %d)\n",
		       dev->name, err);
		return err;
	}

	macb_init_rings(bp);
	macb_init_hw(bp);
	macb_init_phy(dev);

	macb_check_media(bp, 1, 1);
	netif_start_queue(dev);

	schedule_delayed_work(&bp->periodic_task, HZ);

	return 0;
}

static int macb_close(struct net_device *dev)
{
	struct macb *bp = netdev_priv(dev);
	unsigned long flags;

	cancel_rearming_delayed_work(&bp->periodic_task);

	netif_stop_queue(dev);

	spin_lock_irqsave(&bp->lock, flags);
	macb_reset_hw(bp);
	netif_carrier_off(dev);
	spin_unlock_irqrestore(&bp->lock, flags);

	macb_free_consistent(bp);

	return 0;
}

static struct net_device_stats *macb_get_stats(struct net_device *dev)
{
	struct macb *bp = netdev_priv(dev);
	struct net_device_stats *nstat = &bp->stats;
	struct macb_stats *hwstat = &bp->hw_stats;

	/* Convert HW stats into netdevice stats */
	nstat->rx_errors = (hwstat->rx_fcs_errors +
			    hwstat->rx_align_errors +
			    hwstat->rx_resource_errors +
			    hwstat->rx_overruns +
			    hwstat->rx_oversize_pkts +
			    hwstat->rx_jabbers +
			    hwstat->rx_undersize_pkts +
			    hwstat->sqe_test_errors +
			    hwstat->rx_length_mismatch);
	nstat->tx_errors = (hwstat->tx_late_cols +
			    hwstat->tx_excessive_cols +
			    hwstat->tx_underruns +
			    hwstat->tx_carrier_errors);
	nstat->collisions = (hwstat->tx_single_cols +
			     hwstat->tx_multiple_cols +
			     hwstat->tx_excessive_cols);
	nstat->rx_length_errors = (hwstat->rx_oversize_pkts +
				   hwstat->rx_jabbers +
				   hwstat->rx_undersize_pkts +
				   hwstat->rx_length_mismatch);
	nstat->rx_over_errors = hwstat->rx_resource_errors;
	nstat->rx_crc_errors = hwstat->rx_fcs_errors;
	nstat->rx_frame_errors = hwstat->rx_align_errors;
	nstat->rx_fifo_errors = hwstat->rx_overruns;
	/* XXX: What does "missed" mean? */
	nstat->tx_aborted_errors = hwstat->tx_excessive_cols;
	nstat->tx_carrier_errors = hwstat->tx_carrier_errors;
	nstat->tx_fifo_errors = hwstat->tx_underruns;
	/* Don't know about heartbeat or window errors... */

	return nstat;
}

static int macb_get_settings(struct net_device *dev, struct ethtool_cmd *cmd)
{
	struct macb *bp = netdev_priv(dev);

	return mii_ethtool_gset(&bp->mii, cmd);
}

static int macb_set_settings(struct net_device *dev, struct ethtool_cmd *cmd)
{
	struct macb *bp = netdev_priv(dev);

	return mii_ethtool_sset(&bp->mii, cmd);
}

static void macb_get_drvinfo(struct net_device *dev, struct ethtool_drvinfo *info)
{
	struct macb *bp = netdev_priv(dev);

	strcpy(info->driver, bp->pdev->dev.driver->name);
	strcpy(info->version, "$Revision: 1.14 $");
	strcpy(info->bus_info, bp->pdev->dev.bus_id);
}

static int macb_nway_reset(struct net_device *dev)
{
	struct macb *bp = netdev_priv(dev);
	return mii_nway_restart(&bp->mii);
}

static struct ethtool_ops macb_ethtool_ops = {
	.get_settings		= macb_get_settings,
	.set_settings		= macb_set_settings,
	.get_drvinfo		= macb_get_drvinfo,
	.nway_reset		= macb_nway_reset,
	.get_link		= ethtool_op_get_link,
};

static int macb_ioctl(struct net_device *dev, struct ifreq *rq, int cmd)
{
	struct macb *bp = netdev_priv(dev);

	if (!netif_running(dev))
		return -EINVAL;

	return generic_mii_ioctl(&bp->mii, if_mii(rq), cmd, NULL);
}

static ssize_t macb_mii_show(const struct device *_dev, char *buf,
			unsigned long addr)
{
	struct net_device *dev = to_net_dev(_dev);
	struct macb *bp = netdev_priv(dev);
	ssize_t ret = -EINVAL;

	if (netif_running(dev)) {
		int value;
		value = macb_mdio_read(dev, bp->mii.phy_id, addr);
		ret = sprintf(buf, "0x%04x\n", (uint16_t)value);
	}

	return ret;
}

#define MII_ENTRY(name, addr)					\
static ssize_t show_##name(struct device *_dev,			\
			   struct device_attribute *attr,	\
			   char *buf)				\
{								\
	return macb_mii_show(_dev, buf, addr);			\
}								\
static DEVICE_ATTR(name, S_IRUGO, show_##name, NULL)

MII_ENTRY(bmcr, MII_BMCR);
MII_ENTRY(bmsr, MII_BMSR);
MII_ENTRY(physid1, MII_PHYSID1);
MII_ENTRY(physid2, MII_PHYSID2);
MII_ENTRY(advertise, MII_ADVERTISE);
MII_ENTRY(lpa, MII_LPA);
MII_ENTRY(expansion, MII_EXPANSION);

static struct attribute *macb_mii_attrs[] = {
	&dev_attr_bmcr.attr,
	&dev_attr_bmsr.attr,
	&dev_attr_physid1.attr,
	&dev_attr_physid2.attr,
	&dev_attr_advertise.attr,
	&dev_attr_lpa.attr,
	&dev_attr_expansion.attr,
	NULL,
};

static struct attribute_group macb_mii_group = {
	.name	= "mii",
	.attrs	= macb_mii_attrs,
};

static void macb_unregister_sysfs(struct net_device *net)
{
	struct device *_dev = &net->dev;

	sysfs_remove_group(&_dev->kobj, &macb_mii_group);
}

static int macb_register_sysfs(struct net_device *net)
{
	struct device *_dev = &net->dev;
	int ret;

	ret = sysfs_create_group(&_dev->kobj, &macb_mii_group);
	if (ret)
		printk(KERN_WARNING
		       "%s: sysfs mii attribute registration failed: %d\n",
		       net->name, ret);
	return ret;
}
static int __devinit macb_probe(struct platform_device *pdev)
{
	struct eth_platform_data *pdata;
	struct resource *regs;
	struct net_device *dev;
	struct macb *bp;
	unsigned long pclk_hz;
	u32 config;
	int err = -ENXIO;

	regs = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!regs) {
		dev_err(&pdev->dev, "no mmio resource defined\n");
		goto err_out;
	}

	err = -ENOMEM;
	dev = alloc_etherdev(sizeof(*bp));
	if (!dev) {
		dev_err(&pdev->dev, "etherdev alloc failed, aborting.\n");
		goto err_out;
	}

	SET_MODULE_OWNER(dev);
	SET_NETDEV_DEV(dev, &pdev->dev);

	/* TODO: Actually, we have some interesting features... */
	dev->features |= 0;

	bp = netdev_priv(dev);
	bp->pdev = pdev;
	bp->dev = dev;

	spin_lock_init(&bp->lock);

#if defined(CONFIG_ARCH_AT91)
	bp->pclk = clk_get(&pdev->dev, "macb_clk");
	if (IS_ERR(bp->pclk)) {
		dev_err(&pdev->dev, "failed to get macb_clk\n");
		goto err_out_free_dev;
	}
	clk_enable(bp->pclk);
#else
	bp->pclk = clk_get(&pdev->dev, "pclk");
	if (IS_ERR(bp->pclk)) {
		dev_err(&pdev->dev, "failed to get pclk\n");
		goto err_out_free_dev;
	}
	bp->hclk = clk_get(&pdev->dev, "hclk");
	if (IS_ERR(bp->hclk)) {
		dev_err(&pdev->dev, "failed to get hclk\n");
		goto err_out_put_pclk;
	}

	clk_enable(bp->pclk);
	clk_enable(bp->hclk);
#endif

	bp->regs = ioremap(regs->start, regs->end - regs->start + 1);
	if (!bp->regs) {
		dev_err(&pdev->dev, "failed to map registers, aborting.\n");
		err = -ENOMEM;
		goto err_out_disable_clocks;
	}

	dev->irq = platform_get_irq(pdev, 0);
	err = request_irq(dev->irq, macb_interrupt, IRQF_SAMPLE_RANDOM,
			  dev->name, dev);
	if (err) {
		printk(KERN_ERR
		       "%s: Unable to request IRQ %d (error %d)\n",
		       dev->name, dev->irq, err);
		goto err_out_iounmap;
	}

	dev->open = macb_open;
	dev->stop = macb_close;
	dev->hard_start_xmit = macb_start_xmit;
	dev->get_stats = macb_get_stats;
	dev->do_ioctl = macb_ioctl;
	dev->poll = macb_poll;
	dev->weight = 64;
	dev->ethtool_ops = &macb_ethtool_ops;

	dev->base_addr = regs->start;

	INIT_DELAYED_WORK(&bp->periodic_task, macb_periodic_task);
	mutex_init(&bp->mdio_mutex);
	init_completion(&bp->mdio_complete);

	/* Set MII management clock divider */
	pclk_hz = clk_get_rate(bp->pclk);
	if (pclk_hz <= 20000000)
		config = MACB_BF(CLK, MACB_CLK_DIV8);
	else if (pclk_hz <= 40000000)
		config = MACB_BF(CLK, MACB_CLK_DIV16);
	else if (pclk_hz <= 80000000)
		config = MACB_BF(CLK, MACB_CLK_DIV32);
	else
		config = MACB_BF(CLK, MACB_CLK_DIV64);
	macb_writel(bp, NCFGR, config);

	bp->mii.dev = dev;
	bp->mii.mdio_read = macb_mdio_read;
	bp->mii.mdio_write = macb_mdio_write;
	bp->mii.phy_id_mask = 0x1f;
	bp->mii.reg_num_mask = 0x1f;

	macb_get_hwaddr(bp);
	err = macb_phy_probe(bp);
	if (err) {
		dev_err(&pdev->dev, "Failed to detect PHY, aborting.\n");
		goto err_out_free_irq;
	}

	pdata = pdev->dev.platform_data;
	if (pdata && pdata->is_rmii)
#if defined(CONFIG_ARCH_AT91)
		macb_writel(bp, USRIO, (MACB_BIT(RMII) | MACB_BIT(CLKEN)) );
#else
		macb_writel(bp, USRIO, 0);
#endif
	else
#if defined(CONFIG_ARCH_AT91)
		macb_writel(bp, USRIO, MACB_BIT(CLKEN));
#else
		macb_writel(bp, USRIO, MACB_BIT(MII));
#endif

	bp->tx_pending = DEF_TX_RING_PENDING;

	err = register_netdev(dev);
	if (err) {
		dev_err(&pdev->dev, "Cannot register net device, aborting.\n");
		goto err_out_free_irq;
	}

	platform_set_drvdata(pdev, dev);

	macb_register_sysfs(dev);

	printk(KERN_INFO "%s: Atmel MACB at 0x%08lx irq %d "
	       "(%02x:%02x:%02x:%02x:%02x:%02x)\n",
	       dev->name, dev->base_addr, dev->irq,
	       dev->dev_addr[0], dev->dev_addr[1], dev->dev_addr[2],
	       dev->dev_addr[3], dev->dev_addr[4], dev->dev_addr[5]);

	return 0;

err_out_free_irq:
	free_irq(dev->irq, dev);
err_out_iounmap:
	iounmap(bp->regs);
err_out_disable_clocks:
#ifndef CONFIG_ARCH_AT91
	clk_disable(bp->hclk);
	clk_put(bp->hclk);
#endif
	clk_disable(bp->pclk);
err_out_put_pclk:
	clk_put(bp->pclk);
err_out_free_dev:
	free_netdev(dev);
err_out:
	platform_set_drvdata(pdev, NULL);
	return err;
}

static int __devexit macb_remove(struct platform_device *pdev)
{
	struct net_device *dev;
	struct macb *bp;

	dev = platform_get_drvdata(pdev);

	if (dev) {
		bp = netdev_priv(dev);
		macb_unregister_sysfs(dev);
		unregister_netdev(dev);
		free_irq(dev->irq, dev);
		iounmap(bp->regs);
#ifndef CONFIG_ARCH_AT91
		clk_disable(bp->hclk);
		clk_put(bp->hclk);
#endif
		clk_disable(bp->pclk);
		clk_put(bp->pclk);
		free_netdev(dev);
		platform_set_drvdata(pdev, NULL);
	}

	return 0;
}

static struct platform_driver macb_driver = {
	.probe		= macb_probe,
	.remove		= __devexit_p(macb_remove),
	.driver		= {
		.name		= "macb",
	},
};

static int __init macb_init(void)
{
	return platform_driver_register(&macb_driver);
}

static void __exit macb_exit(void)
{
	platform_driver_unregister(&macb_driver);
}

module_init(macb_init);
module_exit(macb_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Atmel MACB Ethernet driver");
MODULE_AUTHOR("Haavard Skinnemoen <hskinnemoen@atmel.com>");
