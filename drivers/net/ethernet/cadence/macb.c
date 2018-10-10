/*
 * Cadence MACB/GEM Ethernet Controller driver
 *
 * Copyright (C) 2004-2006 Atmel Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt
#include <linux/clk.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/circ_buf.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/gpio.h>
#include <linux/interrupt.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/dma-mapping.h>
#include <linux/platform_data/macb.h>
#include <linux/platform_device.h>
#include <linux/phy.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_mdio.h>
#include <linux/of_net.h>

#include "macb.h"

#define MACB_RX_BUFFER_SIZE	128
#define RX_BUFFER_MULTIPLE	64  /* bytes */
#define RX_RING_SIZE		512 /* must be power of 2 */
#define RX_RING_BYTES		(sizeof(struct macb_dma_desc) * RX_RING_SIZE)

#define TX_RING_SIZE		128 /* must be power of 2 */
#define TX_RING_BYTES		(sizeof(struct macb_dma_desc) * TX_RING_SIZE)

/* level of occupied TX descriptors under which we wake up TX process */
#define MACB_TX_WAKEUP_THRESH	(3 * TX_RING_SIZE / 4)

#define MACB_RX_INT_FLAGS	(MACB_BIT(RCOMP) | MACB_BIT(RXUBR)	\
				 | MACB_BIT(ISR_ROVR))
#define MACB_TX_ERR_FLAGS	(MACB_BIT(ISR_TUND)			\
					| MACB_BIT(ISR_RLE)		\
					| MACB_BIT(TXERR))
#define MACB_TX_INT_FLAGS	(MACB_TX_ERR_FLAGS | MACB_BIT(TCOMP))

#define MACB_MAX_TX_LEN		((unsigned int)((1 << MACB_TX_FRMLEN_SIZE) - 1))
#define GEM_MAX_TX_LEN		((unsigned int)((1 << GEM_TX_FRMLEN_SIZE) - 1))

#define GEM_MTU_MIN_SIZE	68

/*
 * Graceful stop timeouts in us. We should allow up to
 * 1 frame time (10 Mbits/s, full-duplex, ignoring collisions)
 */
#define MACB_HALT_TIMEOUT	1230

/* Ring buffer accessors */
static unsigned int macb_tx_ring_wrap(unsigned int index)
{
	return index & (TX_RING_SIZE - 1);
}

static struct macb_dma_desc *macb_tx_desc(struct macb_queue *queue,
					  unsigned int index)
{
	return &queue->tx_ring[macb_tx_ring_wrap(index)];
}

static struct macb_tx_skb *macb_tx_skb(struct macb_queue *queue,
				       unsigned int index)
{
	return &queue->tx_skb[macb_tx_ring_wrap(index)];
}

static dma_addr_t macb_tx_dma(struct macb_queue *queue, unsigned int index)
{
	dma_addr_t offset;

	offset = macb_tx_ring_wrap(index) * sizeof(struct macb_dma_desc);

	return queue->tx_ring_dma + offset;
}

static unsigned int macb_rx_ring_wrap(unsigned int index)
{
	return index & (RX_RING_SIZE - 1);
}

static struct macb_dma_desc *macb_rx_desc(struct macb *bp, unsigned int index)
{
	return &bp->rx_ring[macb_rx_ring_wrap(index)];
}

static void *macb_rx_buffer(struct macb *bp, unsigned int index)
{
	return bp->rx_buffers + bp->rx_buffer_size * macb_rx_ring_wrap(index);
}

/* I/O accessors */
static u32 hw_readl_native(struct macb *bp, int offset)
{
	return __raw_readl(bp->regs + offset);
}

static void hw_writel_native(struct macb *bp, int offset, u32 value)
{
	__raw_writel(value, bp->regs + offset);
}

static u32 hw_readl(struct macb *bp, int offset)
{
	return readl_relaxed(bp->regs + offset);
}

static void hw_writel(struct macb *bp, int offset, u32 value)
{
	writel_relaxed(value, bp->regs + offset);
}

/*
 * Find the CPU endianness by using the loopback bit of NCR register. When the
 * CPU is in big endian we need to program swaped mode for management
 * descriptor access.
 */
static bool hw_is_native_io(void __iomem *addr)
{
	u32 value = MACB_BIT(LLB);

	__raw_writel(value, addr + MACB_NCR);
	value = __raw_readl(addr + MACB_NCR);

	/* Write 0 back to disable everything */
	__raw_writel(0, addr + MACB_NCR);

	return value == MACB_BIT(LLB);
}

static bool hw_is_gem(void __iomem *addr, bool native_io)
{
	u32 id;

	if (native_io)
		id = __raw_readl(addr + MACB_MID);
	else
		id = readl_relaxed(addr + MACB_MID);

	return MACB_BFEXT(IDNUM, id) >= 0x2;
}

static void macb_set_hwaddr(struct macb *bp)
{
	u32 bottom;
	u16 top;

	bottom = cpu_to_le32(*((u32 *)bp->dev->dev_addr));
	macb_or_gem_writel(bp, SA1B, bottom);
	top = cpu_to_le16(*((u16 *)(bp->dev->dev_addr + 4)));
	macb_or_gem_writel(bp, SA1T, top);

	/* Clear unused address register sets */
	macb_or_gem_writel(bp, SA2B, 0);
	macb_or_gem_writel(bp, SA2T, 0);
	macb_or_gem_writel(bp, SA3B, 0);
	macb_or_gem_writel(bp, SA3T, 0);
	macb_or_gem_writel(bp, SA4B, 0);
	macb_or_gem_writel(bp, SA4T, 0);
}

static void macb_get_hwaddr(struct macb *bp)
{
	struct macb_platform_data *pdata;
	u32 bottom;
	u16 top;
	u8 addr[6];
	int i;

	pdata = dev_get_platdata(&bp->pdev->dev);

	/* Check all 4 address register for vaild address */
	for (i = 0; i < 4; i++) {
		bottom = macb_or_gem_readl(bp, SA1B + i * 8);
		top = macb_or_gem_readl(bp, SA1T + i * 8);

		if (pdata && pdata->rev_eth_addr) {
			addr[5] = bottom & 0xff;
			addr[4] = (bottom >> 8) & 0xff;
			addr[3] = (bottom >> 16) & 0xff;
			addr[2] = (bottom >> 24) & 0xff;
			addr[1] = top & 0xff;
			addr[0] = (top & 0xff00) >> 8;
		} else {
			addr[0] = bottom & 0xff;
			addr[1] = (bottom >> 8) & 0xff;
			addr[2] = (bottom >> 16) & 0xff;
			addr[3] = (bottom >> 24) & 0xff;
			addr[4] = top & 0xff;
			addr[5] = (top >> 8) & 0xff;
		}

		if (is_valid_ether_addr(addr)) {
			memcpy(bp->dev->dev_addr, addr, sizeof(addr));
			return;
		}
	}

	dev_info(&bp->pdev->dev, "invalid hw address, using random\n");
	eth_hw_addr_random(bp->dev);
}

static int macb_mdio_read(struct mii_bus *bus, int mii_id, int regnum)
{
	struct macb *bp = bus->priv;
	int value;

	macb_writel(bp, MAN, (MACB_BF(SOF, MACB_MAN_SOF)
			      | MACB_BF(RW, MACB_MAN_READ)
			      | MACB_BF(PHYA, mii_id)
			      | MACB_BF(REGA, regnum)
			      | MACB_BF(CODE, MACB_MAN_CODE)));

	/* wait for end of transfer */
	while (!MACB_BFEXT(IDLE, macb_readl(bp, NSR)))
		cpu_relax();

	value = MACB_BFEXT(DATA, macb_readl(bp, MAN));

	return value;
}

static int macb_mdio_write(struct mii_bus *bus, int mii_id, int regnum,
			   u16 value)
{
	struct macb *bp = bus->priv;

	macb_writel(bp, MAN, (MACB_BF(SOF, MACB_MAN_SOF)
			      | MACB_BF(RW, MACB_MAN_WRITE)
			      | MACB_BF(PHYA, mii_id)
			      | MACB_BF(REGA, regnum)
			      | MACB_BF(CODE, MACB_MAN_CODE)
			      | MACB_BF(DATA, value)));

	/* wait for end of transfer */
	while (!MACB_BFEXT(IDLE, macb_readl(bp, NSR)))
		cpu_relax();

	return 0;
}

/**
 * macb_set_tx_clk() - Set a clock to a new frequency
 * @clk		Pointer to the clock to change
 * @rate	New frequency in Hz
 * @dev		Pointer to the struct net_device
 */
static void macb_set_tx_clk(struct clk *clk, int speed, struct net_device *dev)
{
	long ferr, rate, rate_rounded;

	if (!clk)
		return;

	switch (speed) {
	case SPEED_10:
		rate = 2500000;
		break;
	case SPEED_100:
		rate = 25000000;
		break;
	case SPEED_1000:
		rate = 125000000;
		break;
	default:
		return;
	}

	rate_rounded = clk_round_rate(clk, rate);
	if (rate_rounded < 0)
		return;

	/* RGMII allows 50 ppm frequency error. Test and warn if this limit
	 * is not satisfied.
	 */
	ferr = abs(rate_rounded - rate);
	ferr = DIV_ROUND_UP(ferr, rate / 100000);
	if (ferr > 5)
		netdev_warn(dev, "unable to generate target frequency: %ld Hz\n",
				rate);

	if (clk_set_rate(clk, rate_rounded))
		netdev_err(dev, "adjusting tx_clk failed.\n");
}

static void macb_handle_link_change(struct net_device *dev)
{
	struct macb *bp = netdev_priv(dev);
	struct phy_device *phydev = bp->phy_dev;
	unsigned long flags;
	int status_change = 0;

	spin_lock_irqsave(&bp->lock, flags);

	if (phydev->link) {
		if ((bp->speed != phydev->speed) ||
		    (bp->duplex != phydev->duplex)) {
			u32 reg;

			reg = macb_readl(bp, NCFGR);
			reg &= ~(MACB_BIT(SPD) | MACB_BIT(FD));
			if (macb_is_gem(bp))
				reg &= ~GEM_BIT(GBE);

			if (phydev->duplex)
				reg |= MACB_BIT(FD);
			if (phydev->speed == SPEED_100)
				reg |= MACB_BIT(SPD);
			if (phydev->speed == SPEED_1000 &&
			    bp->caps & MACB_CAPS_GIGABIT_MODE_AVAILABLE)
				reg |= GEM_BIT(GBE);

			macb_or_gem_writel(bp, NCFGR, reg);

			bp->speed = phydev->speed;
			bp->duplex = phydev->duplex;
			status_change = 1;
		}
	}

	if (phydev->link != bp->link) {
		if (!phydev->link) {
			bp->speed = 0;
			bp->duplex = -1;
		}
		bp->link = phydev->link;

		status_change = 1;
	}

	spin_unlock_irqrestore(&bp->lock, flags);

	if (status_change) {
		if (phydev->link) {
			/* Update the TX clock rate if and only if the link is
			 * up and there has been a link change.
			 */
			macb_set_tx_clk(bp->tx_clk, phydev->speed, dev);

			netif_carrier_on(dev);
			netdev_info(dev, "link up (%d/%s)\n",
				    phydev->speed,
				    phydev->duplex == DUPLEX_FULL ?
				    "Full" : "Half");
		} else {
			netif_carrier_off(dev);
			netdev_info(dev, "link down\n");
		}
	}
}

/* based on au1000_eth. c*/
static int macb_mii_probe(struct net_device *dev)
{
	struct macb *bp = netdev_priv(dev);
	struct macb_platform_data *pdata;
	struct phy_device *phydev;
	int phy_irq;
	int ret;

	phydev = phy_find_first(bp->mii_bus);
	if (!phydev) {
		netdev_err(dev, "no PHY found\n");
		return -ENXIO;
	}

	pdata = dev_get_platdata(&bp->pdev->dev);
	if (pdata && gpio_is_valid(pdata->phy_irq_pin)) {
		ret = devm_gpio_request(&bp->pdev->dev, pdata->phy_irq_pin, "phy int");
		if (!ret) {
			phy_irq = gpio_to_irq(pdata->phy_irq_pin);
			phydev->irq = (phy_irq < 0) ? PHY_POLL : phy_irq;
		}
	}

	/* attach the mac to the phy */
	ret = phy_connect_direct(dev, phydev, &macb_handle_link_change,
				 bp->phy_interface);
	if (ret) {
		netdev_err(dev, "Could not attach to PHY\n");
		return ret;
	}

	/* mask with MAC supported features */
	if (macb_is_gem(bp) && bp->caps & MACB_CAPS_GIGABIT_MODE_AVAILABLE)
		phydev->supported &= PHY_GBIT_FEATURES;
	else
		phydev->supported &= PHY_BASIC_FEATURES;

	if (bp->caps & MACB_CAPS_NO_GIGABIT_HALF)
		phydev->supported &= ~SUPPORTED_1000baseT_Half;

	phydev->advertising = phydev->supported;

	bp->link = 0;
	bp->speed = 0;
	bp->duplex = -1;
	bp->phy_dev = phydev;

	return 0;
}

static int macb_mii_init(struct macb *bp)
{
	struct macb_platform_data *pdata;
	struct device_node *np;
	int err = -ENXIO, i;

	/* Enable management port */
	macb_writel(bp, NCR, MACB_BIT(MPE));

	bp->mii_bus = mdiobus_alloc();
	if (bp->mii_bus == NULL) {
		err = -ENOMEM;
		goto err_out;
	}

	bp->mii_bus->name = "MACB_mii_bus";
	bp->mii_bus->read = &macb_mdio_read;
	bp->mii_bus->write = &macb_mdio_write;
	snprintf(bp->mii_bus->id, MII_BUS_ID_SIZE, "%s-%x",
		bp->pdev->name, bp->pdev->id);
	bp->mii_bus->priv = bp;
	bp->mii_bus->parent = &bp->dev->dev;
	pdata = dev_get_platdata(&bp->pdev->dev);

	bp->mii_bus->irq = kmalloc(sizeof(int)*PHY_MAX_ADDR, GFP_KERNEL);
	if (!bp->mii_bus->irq) {
		err = -ENOMEM;
		goto err_out_free_mdiobus;
	}

	dev_set_drvdata(&bp->dev->dev, bp->mii_bus);

	np = bp->pdev->dev.of_node;
	if (np) {
		/* try dt phy registration */
		err = of_mdiobus_register(bp->mii_bus, np);

		/* fallback to standard phy registration if no phy were
		   found during dt phy registration */
		if (!err && !phy_find_first(bp->mii_bus)) {
			for (i = 0; i < PHY_MAX_ADDR; i++) {
				struct phy_device *phydev;

				phydev = mdiobus_scan(bp->mii_bus, i);
				if (IS_ERR(phydev)) {
					err = PTR_ERR(phydev);
					break;
				}
			}

			if (err)
				goto err_out_unregister_bus;
		}
	} else {
		for (i = 0; i < PHY_MAX_ADDR; i++)
			bp->mii_bus->irq[i] = PHY_POLL;

		if (pdata)
			bp->mii_bus->phy_mask = pdata->phy_mask;

		err = mdiobus_register(bp->mii_bus);
	}

	if (err)
		goto err_out_free_mdio_irq;

	err = macb_mii_probe(bp->dev);
	if (err)
		goto err_out_unregister_bus;

	return 0;

err_out_unregister_bus:
	mdiobus_unregister(bp->mii_bus);
err_out_free_mdio_irq:
	kfree(bp->mii_bus->irq);
err_out_free_mdiobus:
	mdiobus_free(bp->mii_bus);
err_out:
	return err;
}

static void macb_update_stats(struct macb *bp)
{
	u32 *p = &bp->hw_stats.macb.rx_pause_frames;
	u32 *end = &bp->hw_stats.macb.tx_pause_frames + 1;
	int offset = MACB_PFR;

	WARN_ON((unsigned long)(end - p - 1) != (MACB_TPF - MACB_PFR) / 4);

	for(; p < end; p++, offset += 4)
		*p += bp->macb_reg_readl(bp, offset);
}

static int macb_halt_tx(struct macb *bp)
{
	unsigned long	halt_time, timeout;
	u32		status;

	macb_writel(bp, NCR, macb_readl(bp, NCR) | MACB_BIT(THALT));

	timeout = jiffies + usecs_to_jiffies(MACB_HALT_TIMEOUT);
	do {
		halt_time = jiffies;
		status = macb_readl(bp, TSR);
		if (!(status & MACB_BIT(TGO)))
			return 0;

		udelay(250);
	} while (time_before(halt_time, timeout));

	return -ETIMEDOUT;
}

static void macb_tx_unmap(struct macb *bp, struct macb_tx_skb *tx_skb)
{
	if (tx_skb->mapping) {
		if (tx_skb->mapped_as_page)
			dma_unmap_page(&bp->pdev->dev, tx_skb->mapping,
				       tx_skb->size, DMA_TO_DEVICE);
		else
			dma_unmap_single(&bp->pdev->dev, tx_skb->mapping,
					 tx_skb->size, DMA_TO_DEVICE);
		tx_skb->mapping = 0;
	}

	if (tx_skb->skb) {
		dev_kfree_skb_any(tx_skb->skb);
		tx_skb->skb = NULL;
	}
}

static void macb_tx_error_task(struct work_struct *work)
{
	struct macb_queue	*queue = container_of(work, struct macb_queue,
						      tx_error_task);
	struct macb		*bp = queue->bp;
	struct macb_tx_skb	*tx_skb;
	struct macb_dma_desc	*desc;
	struct sk_buff		*skb;
	unsigned int		tail;
	unsigned long		flags;

	netdev_vdbg(bp->dev, "macb_tx_error_task: q = %u, t = %u, h = %u\n",
		    (unsigned int)(queue - bp->queues),
		    queue->tx_tail, queue->tx_head);

	/* Prevent the queue IRQ handlers from running: each of them may call
	 * macb_tx_interrupt(), which in turn may call netif_wake_subqueue().
	 * As explained below, we have to halt the transmission before updating
	 * TBQP registers so we call netif_tx_stop_all_queues() to notify the
	 * network engine about the macb/gem being halted.
	 */
	spin_lock_irqsave(&bp->lock, flags);

	/* Make sure nobody is trying to queue up new packets */
	netif_tx_stop_all_queues(bp->dev);

	/*
	 * Stop transmission now
	 * (in case we have just queued new packets)
	 * macb/gem must be halted to write TBQP register
	 */
	if (macb_halt_tx(bp))
		/* Just complain for now, reinitializing TX path can be good */
		netdev_err(bp->dev, "BUG: halt tx timed out\n");

	/*
	 * Treat frames in TX queue including the ones that caused the error.
	 * Free transmit buffers in upper layer.
	 */
	for (tail = queue->tx_tail; tail != queue->tx_head; tail++) {
		u32	ctrl;

		desc = macb_tx_desc(queue, tail);
		ctrl = desc->ctrl;
		tx_skb = macb_tx_skb(queue, tail);
		skb = tx_skb->skb;

		if (ctrl & MACB_BIT(TX_USED)) {
			/* skb is set for the last buffer of the frame */
			while (!skb) {
				macb_tx_unmap(bp, tx_skb);
				tail++;
				tx_skb = macb_tx_skb(queue, tail);
				skb = tx_skb->skb;
			}

			/* ctrl still refers to the first buffer descriptor
			 * since it's the only one written back by the hardware
			 */
			if (!(ctrl & MACB_BIT(TX_BUF_EXHAUSTED))) {
				netdev_vdbg(bp->dev, "txerr skb %u (data %p) TX complete\n",
					    macb_tx_ring_wrap(tail), skb->data);
				bp->stats.tx_packets++;
				bp->stats.tx_bytes += skb->len;
			}
		} else {
			/*
			 * "Buffers exhausted mid-frame" errors may only happen
			 * if the driver is buggy, so complain loudly about those.
			 * Statistics are updated by hardware.
			 */
			if (ctrl & MACB_BIT(TX_BUF_EXHAUSTED))
				netdev_err(bp->dev,
					   "BUG: TX buffers exhausted mid-frame\n");

			desc->ctrl = ctrl | MACB_BIT(TX_USED);
		}

		macb_tx_unmap(bp, tx_skb);
	}

	/* Set end of TX queue */
	desc = macb_tx_desc(queue, 0);
	desc->addr = 0;
	desc->ctrl = MACB_BIT(TX_USED);

	/* Make descriptor updates visible to hardware */
	wmb();

	/* Reinitialize the TX desc queue */
	queue_writel(queue, TBQP, queue->tx_ring_dma);
	/* Make TX ring reflect state of hardware */
	queue->tx_head = 0;
	queue->tx_tail = 0;

	/* Housework before enabling TX IRQ */
	macb_writel(bp, TSR, macb_readl(bp, TSR));
	queue_writel(queue, IER, MACB_TX_INT_FLAGS);

	/* Now we are ready to start transmission again */
	netif_tx_start_all_queues(bp->dev);
	macb_writel(bp, NCR, macb_readl(bp, NCR) | MACB_BIT(TSTART));

	spin_unlock_irqrestore(&bp->lock, flags);
}

static void macb_tx_interrupt(struct macb_queue *queue)
{
	unsigned int tail;
	unsigned int head;
	u32 status;
	struct macb *bp = queue->bp;
	u16 queue_index = queue - bp->queues;

	status = macb_readl(bp, TSR);
	macb_writel(bp, TSR, status);

	if (bp->caps & MACB_CAPS_ISR_CLEAR_ON_WRITE)
		queue_writel(queue, ISR, MACB_BIT(TCOMP));

	netdev_vdbg(bp->dev, "macb_tx_interrupt status = 0x%03lx\n",
		(unsigned long)status);

	head = queue->tx_head;
	for (tail = queue->tx_tail; tail != head; tail++) {
		struct macb_tx_skb	*tx_skb;
		struct sk_buff		*skb;
		struct macb_dma_desc	*desc;
		u32			ctrl;

		desc = macb_tx_desc(queue, tail);

		/* Make hw descriptor updates visible to CPU */
		rmb();

		ctrl = desc->ctrl;

		/* TX_USED bit is only set by hardware on the very first buffer
		 * descriptor of the transmitted frame.
		 */
		if (!(ctrl & MACB_BIT(TX_USED)))
			break;

		/* Process all buffers of the current transmitted frame */
		for (;; tail++) {
			tx_skb = macb_tx_skb(queue, tail);
			skb = tx_skb->skb;

			/* First, update TX stats if needed */
			if (skb) {
				netdev_vdbg(bp->dev, "skb %u (data %p) TX complete\n",
					    macb_tx_ring_wrap(tail), skb->data);
				bp->stats.tx_packets++;
				bp->stats.tx_bytes += skb->len;
			}

			/* Now we can safely release resources */
			macb_tx_unmap(bp, tx_skb);

			/* skb is set only for the last buffer of the frame.
			 * WARNING: at this point skb has been freed by
			 * macb_tx_unmap().
			 */
			if (skb)
				break;
		}
	}

	queue->tx_tail = tail;
	if (__netif_subqueue_stopped(bp->dev, queue_index) &&
	    CIRC_CNT(queue->tx_head, queue->tx_tail,
		     TX_RING_SIZE) <= MACB_TX_WAKEUP_THRESH)
		netif_wake_subqueue(bp->dev, queue_index);
}

static void gem_rx_refill(struct macb *bp)
{
	unsigned int		entry;
	struct sk_buff		*skb;
	dma_addr_t		paddr;

	while (CIRC_SPACE(bp->rx_prepared_head, bp->rx_tail, RX_RING_SIZE) > 0) {
		entry = macb_rx_ring_wrap(bp->rx_prepared_head);

		/* Make hw descriptor updates visible to CPU */
		rmb();

		bp->rx_prepared_head++;

		if (bp->rx_skbuff[entry] == NULL) {
			/* allocate sk_buff for this free entry in ring */
			skb = netdev_alloc_skb(bp->dev, bp->rx_buffer_size);
			if (unlikely(skb == NULL)) {
				netdev_err(bp->dev,
					   "Unable to allocate sk_buff\n");
				break;
			}

			/* now fill corresponding descriptor entry */
			paddr = dma_map_single(&bp->pdev->dev, skb->data,
					       bp->rx_buffer_size, DMA_FROM_DEVICE);
			if (dma_mapping_error(&bp->pdev->dev, paddr)) {
				dev_kfree_skb(skb);
				break;
			}

			bp->rx_skbuff[entry] = skb;

			if (entry == RX_RING_SIZE - 1)
				paddr |= MACB_BIT(RX_WRAP);
			bp->rx_ring[entry].addr = paddr;
			bp->rx_ring[entry].ctrl = 0;

			/* properly align Ethernet header */
			skb_reserve(skb, NET_IP_ALIGN);
		} else {
			bp->rx_ring[entry].addr &= ~MACB_BIT(RX_USED);
			bp->rx_ring[entry].ctrl = 0;
		}
	}

	/* Make descriptor updates visible to hardware */
	wmb();

	netdev_vdbg(bp->dev, "rx ring: prepared head %d, tail %d\n",
		   bp->rx_prepared_head, bp->rx_tail);
}

/* Mark DMA descriptors from begin up to and not including end as unused */
static void discard_partial_frame(struct macb *bp, unsigned int begin,
				  unsigned int end)
{
	unsigned int frag;

	for (frag = begin; frag != end; frag++) {
		struct macb_dma_desc *desc = macb_rx_desc(bp, frag);
		desc->addr &= ~MACB_BIT(RX_USED);
	}

	/* Make descriptor updates visible to hardware */
	wmb();

	/*
	 * When this happens, the hardware stats registers for
	 * whatever caused this is updated, so we don't have to record
	 * anything.
	 */
}

static int gem_rx(struct macb *bp, int budget)
{
	unsigned int		len;
	unsigned int		entry;
	struct sk_buff		*skb;
	struct macb_dma_desc	*desc;
	int			count = 0;

	while (count < budget) {
		u32 addr, ctrl;

		entry = macb_rx_ring_wrap(bp->rx_tail);
		desc = &bp->rx_ring[entry];

		/* Make hw descriptor updates visible to CPU */
		rmb();

		addr = desc->addr;
		ctrl = desc->ctrl;

		if (!(addr & MACB_BIT(RX_USED)))
			break;

		bp->rx_tail++;
		count++;

		if (!(ctrl & MACB_BIT(RX_SOF) && ctrl & MACB_BIT(RX_EOF))) {
			netdev_err(bp->dev,
				   "not whole frame pointed by descriptor\n");
			bp->stats.rx_dropped++;
			break;
		}
		skb = bp->rx_skbuff[entry];
		if (unlikely(!skb)) {
			netdev_err(bp->dev,
				   "inconsistent Rx descriptor chain\n");
			bp->stats.rx_dropped++;
			break;
		}
		/* now everything is ready for receiving packet */
		bp->rx_skbuff[entry] = NULL;
		len = ctrl & bp->rx_frm_len_mask;

		netdev_vdbg(bp->dev, "gem_rx %u (len %u)\n", entry, len);

		skb_put(skb, len);
		addr = MACB_BF(RX_WADDR, MACB_BFEXT(RX_WADDR, addr));
		dma_unmap_single(&bp->pdev->dev, addr,
				 bp->rx_buffer_size, DMA_FROM_DEVICE);

		skb->protocol = eth_type_trans(skb, bp->dev);
		skb_checksum_none_assert(skb);
		if (bp->dev->features & NETIF_F_RXCSUM &&
		    !(bp->dev->flags & IFF_PROMISC) &&
		    GEM_BFEXT(RX_CSUM, ctrl) & GEM_RX_CSUM_CHECKED_MASK)
			skb->ip_summed = CHECKSUM_UNNECESSARY;

		bp->stats.rx_packets++;
		bp->stats.rx_bytes += skb->len;

#if defined(DEBUG) && defined(VERBOSE_DEBUG)
		netdev_vdbg(bp->dev, "received skb of length %u, csum: %08x\n",
			    skb->len, skb->csum);
		print_hex_dump(KERN_DEBUG, " mac: ", DUMP_PREFIX_ADDRESS, 16, 1,
			       skb_mac_header(skb), 16, true);
		print_hex_dump(KERN_DEBUG, "data: ", DUMP_PREFIX_ADDRESS, 16, 1,
			       skb->data, 32, true);
#endif

		netif_receive_skb(skb);
	}

	gem_rx_refill(bp);

	return count;
}

static int macb_rx_frame(struct macb *bp, unsigned int first_frag,
			 unsigned int last_frag)
{
	unsigned int len;
	unsigned int frag;
	unsigned int offset;
	struct sk_buff *skb;
	struct macb_dma_desc *desc;

	desc = macb_rx_desc(bp, last_frag);
	len = desc->ctrl & bp->rx_frm_len_mask;

	netdev_vdbg(bp->dev, "macb_rx_frame frags %u - %u (len %u)\n",
		macb_rx_ring_wrap(first_frag),
		macb_rx_ring_wrap(last_frag), len);

	/*
	 * The ethernet header starts NET_IP_ALIGN bytes into the
	 * first buffer. Since the header is 14 bytes, this makes the
	 * payload word-aligned.
	 *
	 * Instead of calling skb_reserve(NET_IP_ALIGN), we just copy
	 * the two padding bytes into the skb so that we avoid hitting
	 * the slowpath in memcpy(), and pull them off afterwards.
	 */
	skb = netdev_alloc_skb(bp->dev, len + NET_IP_ALIGN);
	if (!skb) {
		bp->stats.rx_dropped++;
		for (frag = first_frag; ; frag++) {
			desc = macb_rx_desc(bp, frag);
			desc->addr &= ~MACB_BIT(RX_USED);
			if (frag == last_frag)
				break;
		}

		/* Make descriptor updates visible to hardware */
		wmb();

		return 1;
	}

	offset = 0;
	len += NET_IP_ALIGN;
	skb_checksum_none_assert(skb);
	skb_put(skb, len);

	for (frag = first_frag; ; frag++) {
		unsigned int frag_len = bp->rx_buffer_size;

		if (offset + frag_len > len) {
			BUG_ON(frag != last_frag);
			frag_len = len - offset;
		}
		skb_copy_to_linear_data_offset(skb, offset,
				macb_rx_buffer(bp, frag), frag_len);
		offset += bp->rx_buffer_size;
		desc = macb_rx_desc(bp, frag);
		desc->addr &= ~MACB_BIT(RX_USED);

		if (frag == last_frag)
			break;
	}

	/* Make descriptor updates visible to hardware */
	wmb();

	__skb_pull(skb, NET_IP_ALIGN);
	skb->protocol = eth_type_trans(skb, bp->dev);

	bp->stats.rx_packets++;
	bp->stats.rx_bytes += skb->len;
	netdev_vdbg(bp->dev, "received skb of length %u, csum: %08x\n",
		   skb->len, skb->csum);
	netif_receive_skb(skb);

	return 0;
}

static int macb_rx(struct macb *bp, int budget)
{
	int received = 0;
	unsigned int tail;
	int first_frag = -1;

	for (tail = bp->rx_tail; budget > 0; tail++) {
		struct macb_dma_desc *desc = macb_rx_desc(bp, tail);
		u32 addr, ctrl;

		/* Make hw descriptor updates visible to CPU */
		rmb();

		addr = desc->addr;
		ctrl = desc->ctrl;

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

static int macb_poll(struct napi_struct *napi, int budget)
{
	struct macb *bp = container_of(napi, struct macb, napi);
	int work_done;
	u32 status;

	status = macb_readl(bp, RSR);
	macb_writel(bp, RSR, status);

	work_done = 0;

	netdev_vdbg(bp->dev, "poll: status = %08lx, budget = %d\n",
		   (unsigned long)status, budget);

	work_done = bp->macbgem_ops.mog_rx(bp, budget);
	if (work_done < budget) {
		napi_complete(napi);

		/* Packets received while interrupts were disabled */
		status = macb_readl(bp, RSR);
		if (status) {
			if (bp->caps & MACB_CAPS_ISR_CLEAR_ON_WRITE)
				macb_writel(bp, ISR, MACB_BIT(RCOMP));
			napi_reschedule(napi);
		} else {
			macb_writel(bp, IER, MACB_RX_INT_FLAGS);
		}
	}

	/* TODO: Handle errors */

	return work_done;
}

static irqreturn_t macb_interrupt(int irq, void *dev_id)
{
	struct macb_queue *queue = dev_id;
	struct macb *bp = queue->bp;
	struct net_device *dev = bp->dev;
	u32 status, ctrl;

	status = queue_readl(queue, ISR);

	if (unlikely(!status))
		return IRQ_NONE;

	spin_lock(&bp->lock);

	while (status) {
		/* close possible race with dev_close */
		if (unlikely(!netif_running(dev))) {
			queue_writel(queue, IDR, -1);
			break;
		}

		netdev_vdbg(bp->dev, "queue = %u, isr = 0x%08lx\n",
			    (unsigned int)(queue - bp->queues),
			    (unsigned long)status);

		if (status & MACB_RX_INT_FLAGS) {
			/*
			 * There's no point taking any more interrupts
			 * until we have processed the buffers. The
			 * scheduling call may fail if the poll routine
			 * is already scheduled, so disable interrupts
			 * now.
			 */
			queue_writel(queue, IDR, MACB_RX_INT_FLAGS);
			if (bp->caps & MACB_CAPS_ISR_CLEAR_ON_WRITE)
				queue_writel(queue, ISR, MACB_BIT(RCOMP));

			if (napi_schedule_prep(&bp->napi)) {
				netdev_vdbg(bp->dev, "scheduling RX softirq\n");
				__napi_schedule(&bp->napi);
			}
		}

		if (unlikely(status & (MACB_TX_ERR_FLAGS))) {
			queue_writel(queue, IDR, MACB_TX_INT_FLAGS);
			schedule_work(&queue->tx_error_task);

			if (bp->caps & MACB_CAPS_ISR_CLEAR_ON_WRITE)
				queue_writel(queue, ISR, MACB_TX_ERR_FLAGS);

			break;
		}

		if (status & MACB_BIT(TCOMP))
			macb_tx_interrupt(queue);

		/*
		 * Link change detection isn't possible with RMII, so we'll
		 * add that if/when we get our hands on a full-blown MII PHY.
		 */

		/* There is a hardware issue under heavy load where DMA can
		 * stop, this causes endless "used buffer descriptor read"
		 * interrupts but it can be cleared by re-enabling RX. See
		 * the at91 manual, section 41.3.1 or the Zynq manual
		 * section 16.7.4 for details.
		 */
		if (status & MACB_BIT(RXUBR)) {
			ctrl = macb_readl(bp, NCR);
			macb_writel(bp, NCR, ctrl & ~MACB_BIT(RE));
			macb_writel(bp, NCR, ctrl | MACB_BIT(RE));

			if (bp->caps & MACB_CAPS_ISR_CLEAR_ON_WRITE)
				macb_writel(bp, ISR, MACB_BIT(RXUBR));
		}

		if (status & MACB_BIT(ISR_ROVR)) {
			/* We missed at least one packet */
			if (macb_is_gem(bp))
				bp->hw_stats.gem.rx_overruns++;
			else
				bp->hw_stats.macb.rx_overruns++;

			if (bp->caps & MACB_CAPS_ISR_CLEAR_ON_WRITE)
				queue_writel(queue, ISR, MACB_BIT(ISR_ROVR));
		}

		if (status & MACB_BIT(HRESP)) {
			/*
			 * TODO: Reset the hardware, and maybe move the
			 * netdev_err to a lower-priority context as well
			 * (work queue?)
			 */
			netdev_err(dev, "DMA bus error: HRESP not OK\n");

			if (bp->caps & MACB_CAPS_ISR_CLEAR_ON_WRITE)
				queue_writel(queue, ISR, MACB_BIT(HRESP));
		}

		status = queue_readl(queue, ISR);
	}

	spin_unlock(&bp->lock);

	return IRQ_HANDLED;
}

#ifdef CONFIG_NET_POLL_CONTROLLER
/*
 * Polling receive - used by netconsole and other diagnostic tools
 * to allow network i/o with interrupts disabled.
 */
static void macb_poll_controller(struct net_device *dev)
{
	struct macb *bp = netdev_priv(dev);
	struct macb_queue *queue;
	unsigned long flags;
	unsigned int q;

	local_irq_save(flags);
	for (q = 0, queue = bp->queues; q < bp->num_queues; ++q, ++queue)
		macb_interrupt(dev->irq, queue);
	local_irq_restore(flags);
}
#endif

static unsigned int macb_tx_map(struct macb *bp,
				struct macb_queue *queue,
				struct sk_buff *skb)
{
	dma_addr_t mapping;
	unsigned int len, entry, i, tx_head = queue->tx_head;
	struct macb_tx_skb *tx_skb = NULL;
	struct macb_dma_desc *desc;
	unsigned int offset, size, count = 0;
	unsigned int f, nr_frags = skb_shinfo(skb)->nr_frags;
	unsigned int eof = 1;
	u32 ctrl;

	/* First, map non-paged data */
	len = skb_headlen(skb);
	offset = 0;
	while (len) {
		size = min(len, bp->max_tx_length);
		entry = macb_tx_ring_wrap(tx_head);
		tx_skb = &queue->tx_skb[entry];

		mapping = dma_map_single(&bp->pdev->dev,
					 skb->data + offset,
					 size, DMA_TO_DEVICE);
		if (dma_mapping_error(&bp->pdev->dev, mapping))
			goto dma_error;

		/* Save info to properly release resources */
		tx_skb->skb = NULL;
		tx_skb->mapping = mapping;
		tx_skb->size = size;
		tx_skb->mapped_as_page = false;

		len -= size;
		offset += size;
		count++;
		tx_head++;
	}

	/* Then, map paged data from fragments */
	for (f = 0; f < nr_frags; f++) {
		const skb_frag_t *frag = &skb_shinfo(skb)->frags[f];

		len = skb_frag_size(frag);
		offset = 0;
		while (len) {
			size = min(len, bp->max_tx_length);
			entry = macb_tx_ring_wrap(tx_head);
			tx_skb = &queue->tx_skb[entry];

			mapping = skb_frag_dma_map(&bp->pdev->dev, frag,
						   offset, size, DMA_TO_DEVICE);
			if (dma_mapping_error(&bp->pdev->dev, mapping))
				goto dma_error;

			/* Save info to properly release resources */
			tx_skb->skb = NULL;
			tx_skb->mapping = mapping;
			tx_skb->size = size;
			tx_skb->mapped_as_page = true;

			len -= size;
			offset += size;
			count++;
			tx_head++;
		}
	}

	/* Should never happen */
	if (unlikely(tx_skb == NULL)) {
		netdev_err(bp->dev, "BUG! empty skb!\n");
		return 0;
	}

	/* This is the last buffer of the frame: save socket buffer */
	tx_skb->skb = skb;

	/* Update TX ring: update buffer descriptors in reverse order
	 * to avoid race condition
	 */

	/* Set 'TX_USED' bit in buffer descriptor at tx_head position
	 * to set the end of TX queue
	 */
	i = tx_head;
	entry = macb_tx_ring_wrap(i);
	ctrl = MACB_BIT(TX_USED);
	desc = &queue->tx_ring[entry];
	desc->ctrl = ctrl;

	do {
		i--;
		entry = macb_tx_ring_wrap(i);
		tx_skb = &queue->tx_skb[entry];
		desc = &queue->tx_ring[entry];

		ctrl = (u32)tx_skb->size;
		if (eof) {
			ctrl |= MACB_BIT(TX_LAST);
			eof = 0;
		}
		if (unlikely(entry == (TX_RING_SIZE - 1)))
			ctrl |= MACB_BIT(TX_WRAP);

		/* Set TX buffer descriptor */
		desc->addr = tx_skb->mapping;
		/* desc->addr must be visible to hardware before clearing
		 * 'TX_USED' bit in desc->ctrl.
		 */
		wmb();
		desc->ctrl = ctrl;
	} while (i != queue->tx_head);

	queue->tx_head = tx_head;

	return count;

dma_error:
	netdev_err(bp->dev, "TX DMA map failed\n");

	for (i = queue->tx_head; i != tx_head; i++) {
		tx_skb = macb_tx_skb(queue, i);

		macb_tx_unmap(bp, tx_skb);
	}

	return 0;
}

static int macb_start_xmit(struct sk_buff *skb, struct net_device *dev)
{
	u16 queue_index = skb_get_queue_mapping(skb);
	struct macb *bp = netdev_priv(dev);
	struct macb_queue *queue = &bp->queues[queue_index];
	unsigned long flags;
	unsigned int count, nr_frags, frag_size, f;

#if defined(DEBUG) && defined(VERBOSE_DEBUG)
	netdev_vdbg(bp->dev,
		   "start_xmit: queue %hu len %u head %p data %p tail %p end %p\n",
		   queue_index, skb->len, skb->head, skb->data,
		   skb_tail_pointer(skb), skb_end_pointer(skb));
	print_hex_dump(KERN_DEBUG, "data: ", DUMP_PREFIX_OFFSET, 16, 1,
		       skb->data, 16, true);
#endif

	/* Count how many TX buffer descriptors are needed to send this
	 * socket buffer: skb fragments of jumbo frames may need to be
	 * splitted into many buffer descriptors.
	 */
	count = DIV_ROUND_UP(skb_headlen(skb), bp->max_tx_length);
	nr_frags = skb_shinfo(skb)->nr_frags;
	for (f = 0; f < nr_frags; f++) {
		frag_size = skb_frag_size(&skb_shinfo(skb)->frags[f]);
		count += DIV_ROUND_UP(frag_size, bp->max_tx_length);
	}

	spin_lock_irqsave(&bp->lock, flags);

	/* This is a hard error, log it. */
	if (CIRC_SPACE(queue->tx_head, queue->tx_tail, TX_RING_SIZE) < count) {
		netif_stop_subqueue(dev, queue_index);
		spin_unlock_irqrestore(&bp->lock, flags);
		netdev_dbg(bp->dev, "tx_head = %u, tx_tail = %u\n",
			   queue->tx_head, queue->tx_tail);
		return NETDEV_TX_BUSY;
	}

	/* Map socket buffer for DMA transfer */
	if (!macb_tx_map(bp, queue, skb)) {
		dev_kfree_skb_any(skb);
		goto unlock;
	}

	/* Make newly initialized descriptor visible to hardware */
	wmb();

	skb_tx_timestamp(skb);

	macb_writel(bp, NCR, macb_readl(bp, NCR) | MACB_BIT(TSTART));

	if (CIRC_SPACE(queue->tx_head, queue->tx_tail, TX_RING_SIZE) < 1)
		netif_stop_subqueue(dev, queue_index);

unlock:
	spin_unlock_irqrestore(&bp->lock, flags);

	return NETDEV_TX_OK;
}

static void macb_init_rx_buffer_size(struct macb *bp, size_t size)
{
	if (!macb_is_gem(bp)) {
		bp->rx_buffer_size = MACB_RX_BUFFER_SIZE;
	} else {
		bp->rx_buffer_size = size;

		if (bp->rx_buffer_size % RX_BUFFER_MULTIPLE) {
			netdev_dbg(bp->dev,
				    "RX buffer must be multiple of %d bytes, expanding\n",
				    RX_BUFFER_MULTIPLE);
			bp->rx_buffer_size =
				roundup(bp->rx_buffer_size, RX_BUFFER_MULTIPLE);
		}
	}

	netdev_dbg(bp->dev, "mtu [%u] rx_buffer_size [%Zu]\n",
		   bp->dev->mtu, bp->rx_buffer_size);
}

static void gem_free_rx_buffers(struct macb *bp)
{
	struct sk_buff		*skb;
	struct macb_dma_desc	*desc;
	dma_addr_t		addr;
	int i;

	if (!bp->rx_skbuff)
		return;

	for (i = 0; i < RX_RING_SIZE; i++) {
		skb = bp->rx_skbuff[i];

		if (skb == NULL)
			continue;

		desc = &bp->rx_ring[i];
		addr = MACB_BF(RX_WADDR, MACB_BFEXT(RX_WADDR, desc->addr));
		dma_unmap_single(&bp->pdev->dev, addr, bp->rx_buffer_size,
				 DMA_FROM_DEVICE);
		dev_kfree_skb_any(skb);
		skb = NULL;
	}

	kfree(bp->rx_skbuff);
	bp->rx_skbuff = NULL;
}

static void macb_free_rx_buffers(struct macb *bp)
{
	if (bp->rx_buffers) {
		dma_free_coherent(&bp->pdev->dev,
				  RX_RING_SIZE * bp->rx_buffer_size,
				  bp->rx_buffers, bp->rx_buffers_dma);
		bp->rx_buffers = NULL;
	}
}

static void macb_free_consistent(struct macb *bp)
{
	struct macb_queue *queue;
	unsigned int q;

	bp->macbgem_ops.mog_free_rx_buffers(bp);
	if (bp->rx_ring) {
		dma_free_coherent(&bp->pdev->dev, RX_RING_BYTES,
				  bp->rx_ring, bp->rx_ring_dma);
		bp->rx_ring = NULL;
	}

	for (q = 0, queue = bp->queues; q < bp->num_queues; ++q, ++queue) {
		kfree(queue->tx_skb);
		queue->tx_skb = NULL;
		if (queue->tx_ring) {
			dma_free_coherent(&bp->pdev->dev, TX_RING_BYTES,
					  queue->tx_ring, queue->tx_ring_dma);
			queue->tx_ring = NULL;
		}
	}
}

static int gem_alloc_rx_buffers(struct macb *bp)
{
	int size;

	size = RX_RING_SIZE * sizeof(struct sk_buff *);
	bp->rx_skbuff = kzalloc(size, GFP_KERNEL);
	if (!bp->rx_skbuff)
		return -ENOMEM;
	else
		netdev_dbg(bp->dev,
			   "Allocated %d RX struct sk_buff entries at %p\n",
			   RX_RING_SIZE, bp->rx_skbuff);
	return 0;
}

static int macb_alloc_rx_buffers(struct macb *bp)
{
	int size;

	size = RX_RING_SIZE * bp->rx_buffer_size;
	bp->rx_buffers = dma_alloc_coherent(&bp->pdev->dev, size,
					    &bp->rx_buffers_dma, GFP_KERNEL);
	if (!bp->rx_buffers)
		return -ENOMEM;
	else
		netdev_dbg(bp->dev,
			   "Allocated RX buffers of %d bytes at %08lx (mapped %p)\n",
			   size, (unsigned long)bp->rx_buffers_dma, bp->rx_buffers);
	return 0;
}

static int macb_alloc_consistent(struct macb *bp)
{
	struct macb_queue *queue;
	unsigned int q;
	int size;

	for (q = 0, queue = bp->queues; q < bp->num_queues; ++q, ++queue) {
		size = TX_RING_BYTES;
		queue->tx_ring = dma_alloc_coherent(&bp->pdev->dev, size,
						    &queue->tx_ring_dma,
						    GFP_KERNEL);
		if (!queue->tx_ring)
			goto out_err;
		netdev_dbg(bp->dev,
			   "Allocated TX ring for queue %u of %d bytes at %08lx (mapped %p)\n",
			   q, size, (unsigned long)queue->tx_ring_dma,
			   queue->tx_ring);

		size = TX_RING_SIZE * sizeof(struct macb_tx_skb);
		queue->tx_skb = kmalloc(size, GFP_KERNEL);
		if (!queue->tx_skb)
			goto out_err;
	}

	size = RX_RING_BYTES;
	bp->rx_ring = dma_alloc_coherent(&bp->pdev->dev, size,
					 &bp->rx_ring_dma, GFP_KERNEL);
	if (!bp->rx_ring)
		goto out_err;
	netdev_dbg(bp->dev,
		   "Allocated RX ring of %d bytes at %08lx (mapped %p)\n",
		   size, (unsigned long)bp->rx_ring_dma, bp->rx_ring);

	if (bp->macbgem_ops.mog_alloc_rx_buffers(bp))
		goto out_err;

	return 0;

out_err:
	macb_free_consistent(bp);
	return -ENOMEM;
}

static void gem_init_rings(struct macb *bp)
{
	struct macb_queue *queue;
	unsigned int q;
	int i;

	for (q = 0, queue = bp->queues; q < bp->num_queues; ++q, ++queue) {
		for (i = 0; i < TX_RING_SIZE; i++) {
			queue->tx_ring[i].addr = 0;
			queue->tx_ring[i].ctrl = MACB_BIT(TX_USED);
		}
		queue->tx_ring[TX_RING_SIZE - 1].ctrl |= MACB_BIT(TX_WRAP);
		queue->tx_head = 0;
		queue->tx_tail = 0;
	}

	bp->rx_tail = 0;
	bp->rx_prepared_head = 0;

	gem_rx_refill(bp);
}

static void macb_init_rings(struct macb *bp)
{
	int i;
	dma_addr_t addr;

	addr = bp->rx_buffers_dma;
	for (i = 0; i < RX_RING_SIZE; i++) {
		bp->rx_ring[i].addr = addr;
		bp->rx_ring[i].ctrl = 0;
		addr += bp->rx_buffer_size;
	}
	bp->rx_ring[RX_RING_SIZE - 1].addr |= MACB_BIT(RX_WRAP);

	for (i = 0; i < TX_RING_SIZE; i++) {
		bp->queues[0].tx_ring[i].addr = 0;
		bp->queues[0].tx_ring[i].ctrl = MACB_BIT(TX_USED);
	}
	bp->queues[0].tx_head = 0;
	bp->queues[0].tx_tail = 0;
	bp->queues[0].tx_ring[TX_RING_SIZE - 1].ctrl |= MACB_BIT(TX_WRAP);

	bp->rx_tail = 0;
}

static void macb_reset_hw(struct macb *bp)
{
	struct macb_queue *queue;
	unsigned int q;

	/*
	 * Disable RX and TX (XXX: Should we halt the transmission
	 * more gracefully?)
	 */
	macb_writel(bp, NCR, 0);

	/* Clear the stats registers (XXX: Update stats first?) */
	macb_writel(bp, NCR, MACB_BIT(CLRSTAT));

	/* Clear all status flags */
	macb_writel(bp, TSR, -1);
	macb_writel(bp, RSR, -1);

	/* Disable all interrupts */
	for (q = 0, queue = bp->queues; q < bp->num_queues; ++q, ++queue) {
		queue_writel(queue, IDR, -1);
		queue_readl(queue, ISR);
	}
}

static u32 gem_mdc_clk_div(struct macb *bp)
{
	u32 config;
	unsigned long pclk_hz = clk_get_rate(bp->pclk);

	if (pclk_hz <= 20000000)
		config = GEM_BF(CLK, GEM_CLK_DIV8);
	else if (pclk_hz <= 40000000)
		config = GEM_BF(CLK, GEM_CLK_DIV16);
	else if (pclk_hz <= 80000000)
		config = GEM_BF(CLK, GEM_CLK_DIV32);
	else if (pclk_hz <= 120000000)
		config = GEM_BF(CLK, GEM_CLK_DIV48);
	else if (pclk_hz <= 160000000)
		config = GEM_BF(CLK, GEM_CLK_DIV64);
	else
		config = GEM_BF(CLK, GEM_CLK_DIV96);

	return config;
}

static u32 macb_mdc_clk_div(struct macb *bp)
{
	u32 config;
	unsigned long pclk_hz;

	if (macb_is_gem(bp))
		return gem_mdc_clk_div(bp);

	pclk_hz = clk_get_rate(bp->pclk);
	if (pclk_hz <= 20000000)
		config = MACB_BF(CLK, MACB_CLK_DIV8);
	else if (pclk_hz <= 40000000)
		config = MACB_BF(CLK, MACB_CLK_DIV16);
	else if (pclk_hz <= 80000000)
		config = MACB_BF(CLK, MACB_CLK_DIV32);
	else
		config = MACB_BF(CLK, MACB_CLK_DIV64);

	return config;
}

/*
 * Get the DMA bus width field of the network configuration register that we
 * should program.  We find the width from decoding the design configuration
 * register to find the maximum supported data bus width.
 */
static u32 macb_dbw(struct macb *bp)
{
	if (!macb_is_gem(bp))
		return 0;

	switch (GEM_BFEXT(DBWDEF, gem_readl(bp, DCFG1))) {
	case 4:
		return GEM_BF(DBW, GEM_DBW128);
	case 2:
		return GEM_BF(DBW, GEM_DBW64);
	case 1:
	default:
		return GEM_BF(DBW, GEM_DBW32);
	}
}

/*
 * Configure the receive DMA engine
 * - use the correct receive buffer size
 * - set best burst length for DMA operations
 *   (if not supported by FIFO, it will fallback to default)
 * - set both rx/tx packet buffers to full memory size
 * These are configurable parameters for GEM.
 */
static void macb_configure_dma(struct macb *bp)
{
	u32 dmacfg;

	if (macb_is_gem(bp)) {
		dmacfg = gem_readl(bp, DMACFG) & ~GEM_BF(RXBS, -1L);
		dmacfg |= GEM_BF(RXBS, bp->rx_buffer_size / RX_BUFFER_MULTIPLE);
		if (bp->dma_burst_length)
			dmacfg = GEM_BFINS(FBLDO, bp->dma_burst_length, dmacfg);
		dmacfg |= GEM_BIT(TXPBMS) | GEM_BF(RXBMS, -1L);
		dmacfg &= ~GEM_BIT(ENDIA_PKT);

		if (bp->native_io)
			dmacfg &= ~GEM_BIT(ENDIA_DESC);
		else
			dmacfg |= GEM_BIT(ENDIA_DESC); /* CPU in big endian */

		if (bp->dev->features & NETIF_F_HW_CSUM)
			dmacfg |= GEM_BIT(TXCOEN);
		else
			dmacfg &= ~GEM_BIT(TXCOEN);
		netdev_dbg(bp->dev, "Cadence configure DMA with 0x%08x\n",
			   dmacfg);
		gem_writel(bp, DMACFG, dmacfg);
	}
}

static void macb_init_hw(struct macb *bp)
{
	struct macb_queue *queue;
	unsigned int q;

	u32 config;

	macb_reset_hw(bp);
	macb_set_hwaddr(bp);

	config = macb_mdc_clk_div(bp);
	if (bp->phy_interface == PHY_INTERFACE_MODE_SGMII)
		config |= GEM_BIT(SGMIIEN) | GEM_BIT(PCSSEL);
	config |= MACB_BF(RBOF, NET_IP_ALIGN);	/* Make eth data aligned */
	config |= MACB_BIT(PAE);		/* PAuse Enable */
	config |= MACB_BIT(DRFCS);		/* Discard Rx FCS */
	if (bp->caps & MACB_CAPS_JUMBO)
		config |= MACB_BIT(JFRAME);	/* Enable jumbo frames */
	else
		config |= MACB_BIT(BIG);	/* Receive oversized frames */
	if (bp->dev->flags & IFF_PROMISC)
		config |= MACB_BIT(CAF);	/* Copy All Frames */
	else if (macb_is_gem(bp) && bp->dev->features & NETIF_F_RXCSUM)
		config |= GEM_BIT(RXCOEN);
	if (!(bp->dev->flags & IFF_BROADCAST))
		config |= MACB_BIT(NBC);	/* No BroadCast */
	config |= macb_dbw(bp);
	macb_writel(bp, NCFGR, config);
	if ((bp->caps & MACB_CAPS_JUMBO) && bp->jumbo_max_len)
		gem_writel(bp, JML, bp->jumbo_max_len);
	bp->speed = SPEED_10;
	bp->duplex = DUPLEX_HALF;
	bp->rx_frm_len_mask = MACB_RX_FRMLEN_MASK;
	if (bp->caps & MACB_CAPS_JUMBO)
		bp->rx_frm_len_mask = MACB_RX_JFRMLEN_MASK;

	macb_configure_dma(bp);

	/* Initialize TX and RX buffers */
	macb_writel(bp, RBQP, bp->rx_ring_dma);
	for (q = 0, queue = bp->queues; q < bp->num_queues; ++q, ++queue) {
		queue_writel(queue, TBQP, queue->tx_ring_dma);

		/* Enable interrupts */
		queue_writel(queue, IER,
			     MACB_RX_INT_FLAGS |
			     MACB_TX_INT_FLAGS |
			     MACB_BIT(HRESP));
	}

	/* Enable TX and RX */
	macb_writel(bp, NCR, MACB_BIT(RE) | MACB_BIT(TE) | MACB_BIT(MPE));
}

/*
 * The hash address register is 64 bits long and takes up two
 * locations in the memory map.  The least significant bits are stored
 * in EMAC_HSL and the most significant bits in EMAC_HSH.
 *
 * The unicast hash enable and the multicast hash enable bits in the
 * network configuration register enable the reception of hash matched
 * frames. The destination address is reduced to a 6 bit index into
 * the 64 bit hash register using the following hash function.  The
 * hash function is an exclusive or of every sixth bit of the
 * destination address.
 *
 * hi[5] = da[5] ^ da[11] ^ da[17] ^ da[23] ^ da[29] ^ da[35] ^ da[41] ^ da[47]
 * hi[4] = da[4] ^ da[10] ^ da[16] ^ da[22] ^ da[28] ^ da[34] ^ da[40] ^ da[46]
 * hi[3] = da[3] ^ da[09] ^ da[15] ^ da[21] ^ da[27] ^ da[33] ^ da[39] ^ da[45]
 * hi[2] = da[2] ^ da[08] ^ da[14] ^ da[20] ^ da[26] ^ da[32] ^ da[38] ^ da[44]
 * hi[1] = da[1] ^ da[07] ^ da[13] ^ da[19] ^ da[25] ^ da[31] ^ da[37] ^ da[43]
 * hi[0] = da[0] ^ da[06] ^ da[12] ^ da[18] ^ da[24] ^ da[30] ^ da[36] ^ da[42]
 *
 * da[0] represents the least significant bit of the first byte
 * received, that is, the multicast/unicast indicator, and da[47]
 * represents the most significant bit of the last byte received.  If
 * the hash index, hi[n], points to a bit that is set in the hash
 * register then the frame will be matched according to whether the
 * frame is multicast or unicast.  A multicast match will be signalled
 * if the multicast hash enable bit is set, da[0] is 1 and the hash
 * index points to a bit set in the hash register.  A unicast match
 * will be signalled if the unicast hash enable bit is set, da[0] is 0
 * and the hash index points to a bit set in the hash register.  To
 * receive all multicast frames, the hash register should be set with
 * all ones and the multicast hash enable bit should be set in the
 * network configuration register.
 */

static inline int hash_bit_value(int bitnr, __u8 *addr)
{
	if (addr[bitnr / 8] & (1 << (bitnr % 8)))
		return 1;
	return 0;
}

/*
 * Return the hash index value for the specified address.
 */
static int hash_get_index(__u8 *addr)
{
	int i, j, bitval;
	int hash_index = 0;

	for (j = 0; j < 6; j++) {
		for (i = 0, bitval = 0; i < 8; i++)
			bitval ^= hash_bit_value(i * 6 + j, addr);

		hash_index |= (bitval << j);
	}

	return hash_index;
}

/*
 * Add multicast addresses to the internal multicast-hash table.
 */
static void macb_sethashtable(struct net_device *dev)
{
	struct netdev_hw_addr *ha;
	unsigned long mc_filter[2];
	unsigned int bitnr;
	struct macb *bp = netdev_priv(dev);

	mc_filter[0] = mc_filter[1] = 0;

	netdev_for_each_mc_addr(ha, dev) {
		bitnr = hash_get_index(ha->addr);
		mc_filter[bitnr >> 5] |= 1 << (bitnr & 31);
	}

	macb_or_gem_writel(bp, HRB, mc_filter[0]);
	macb_or_gem_writel(bp, HRT, mc_filter[1]);
}

/*
 * Enable/Disable promiscuous and multicast modes.
 */
static void macb_set_rx_mode(struct net_device *dev)
{
	unsigned long cfg;
	struct macb *bp = netdev_priv(dev);

	cfg = macb_readl(bp, NCFGR);

	if (dev->flags & IFF_PROMISC) {
		/* Enable promiscuous mode */
		cfg |= MACB_BIT(CAF);

		/* Disable RX checksum offload */
		if (macb_is_gem(bp))
			cfg &= ~GEM_BIT(RXCOEN);
	} else {
		/* Disable promiscuous mode */
		cfg &= ~MACB_BIT(CAF);

		/* Enable RX checksum offload only if requested */
		if (macb_is_gem(bp) && dev->features & NETIF_F_RXCSUM)
			cfg |= GEM_BIT(RXCOEN);
	}

	if (dev->flags & IFF_ALLMULTI) {
		/* Enable all multicast mode */
		macb_or_gem_writel(bp, HRB, -1);
		macb_or_gem_writel(bp, HRT, -1);
		cfg |= MACB_BIT(NCFGR_MTI);
	} else if (!netdev_mc_empty(dev)) {
		/* Enable specific multicasts */
		macb_sethashtable(dev);
		cfg |= MACB_BIT(NCFGR_MTI);
	} else if (dev->flags & (~IFF_ALLMULTI)) {
		/* Disable all multicast mode */
		macb_or_gem_writel(bp, HRB, 0);
		macb_or_gem_writel(bp, HRT, 0);
		cfg &= ~MACB_BIT(NCFGR_MTI);
	}

	macb_writel(bp, NCFGR, cfg);
}

static int macb_open(struct net_device *dev)
{
	struct macb *bp = netdev_priv(dev);
	size_t bufsz = dev->mtu + ETH_HLEN + ETH_FCS_LEN + NET_IP_ALIGN;
	int err;

	netdev_dbg(bp->dev, "open\n");

	/* carrier starts down */
	netif_carrier_off(dev);

	/* if the phy is not yet register, retry later*/
	if (!bp->phy_dev)
		return -EAGAIN;

	/* RX buffers initialization */
	macb_init_rx_buffer_size(bp, bufsz);

	err = macb_alloc_consistent(bp);
	if (err) {
		netdev_err(dev, "Unable to allocate DMA memory (error %d)\n",
			   err);
		return err;
	}

	napi_enable(&bp->napi);

	bp->macbgem_ops.mog_init_rings(bp);
	macb_init_hw(bp);

	/* schedule a link state check */
	phy_start(bp->phy_dev);

	netif_tx_start_all_queues(dev);

	return 0;
}

static int macb_close(struct net_device *dev)
{
	struct macb *bp = netdev_priv(dev);
	unsigned long flags;

	netif_tx_stop_all_queues(dev);
	napi_disable(&bp->napi);

	if (bp->phy_dev)
		phy_stop(bp->phy_dev);

	spin_lock_irqsave(&bp->lock, flags);
	macb_reset_hw(bp);
	netif_carrier_off(dev);
	spin_unlock_irqrestore(&bp->lock, flags);

	macb_free_consistent(bp);

	return 0;
}

static int macb_change_mtu(struct net_device *dev, int new_mtu)
{
	struct macb *bp = netdev_priv(dev);
	u32 max_mtu;

	if (netif_running(dev))
		return -EBUSY;

	max_mtu = ETH_DATA_LEN;
	if (bp->caps & MACB_CAPS_JUMBO)
		max_mtu = gem_readl(bp, JML) - ETH_HLEN - ETH_FCS_LEN;

	if ((new_mtu > max_mtu) || (new_mtu < GEM_MTU_MIN_SIZE))
		return -EINVAL;

	dev->mtu = new_mtu;

	return 0;
}

static void gem_update_stats(struct macb *bp)
{
	unsigned int i;
	u32 *p = &bp->hw_stats.gem.tx_octets_31_0;

	for (i = 0; i < GEM_STATS_LEN; ++i, ++p) {
		u32 offset = gem_statistics[i].offset;
		u64 val = bp->macb_reg_readl(bp, offset);

		bp->ethtool_stats[i] += val;
		*p += val;

		if (offset == GEM_OCTTXL || offset == GEM_OCTRXL) {
			/* Add GEM_OCTTXH, GEM_OCTRXH */
			val = bp->macb_reg_readl(bp, offset + 4);
			bp->ethtool_stats[i] += ((u64)val) << 32;
			*(++p) += val;
		}
	}
}

static struct net_device_stats *gem_get_stats(struct macb *bp)
{
	struct gem_stats *hwstat = &bp->hw_stats.gem;
	struct net_device_stats *nstat = &bp->stats;

	gem_update_stats(bp);

	nstat->rx_errors = (hwstat->rx_frame_check_sequence_errors +
			    hwstat->rx_alignment_errors +
			    hwstat->rx_resource_errors +
			    hwstat->rx_overruns +
			    hwstat->rx_oversize_frames +
			    hwstat->rx_jabbers +
			    hwstat->rx_undersized_frames +
			    hwstat->rx_length_field_frame_errors);
	nstat->tx_errors = (hwstat->tx_late_collisions +
			    hwstat->tx_excessive_collisions +
			    hwstat->tx_underrun +
			    hwstat->tx_carrier_sense_errors);
	nstat->multicast = hwstat->rx_multicast_frames;
	nstat->collisions = (hwstat->tx_single_collision_frames +
			     hwstat->tx_multiple_collision_frames +
			     hwstat->tx_excessive_collisions);
	nstat->rx_length_errors = (hwstat->rx_oversize_frames +
				   hwstat->rx_jabbers +
				   hwstat->rx_undersized_frames +
				   hwstat->rx_length_field_frame_errors);
	nstat->rx_over_errors = hwstat->rx_resource_errors;
	nstat->rx_crc_errors = hwstat->rx_frame_check_sequence_errors;
	nstat->rx_frame_errors = hwstat->rx_alignment_errors;
	nstat->rx_fifo_errors = hwstat->rx_overruns;
	nstat->tx_aborted_errors = hwstat->tx_excessive_collisions;
	nstat->tx_carrier_errors = hwstat->tx_carrier_sense_errors;
	nstat->tx_fifo_errors = hwstat->tx_underrun;

	return nstat;
}

static void gem_get_ethtool_stats(struct net_device *dev,
				  struct ethtool_stats *stats, u64 *data)
{
	struct macb *bp;

	bp = netdev_priv(dev);
	gem_update_stats(bp);
	memcpy(data, &bp->ethtool_stats, sizeof(u64) * GEM_STATS_LEN);
}

static int gem_get_sset_count(struct net_device *dev, int sset)
{
	switch (sset) {
	case ETH_SS_STATS:
		return GEM_STATS_LEN;
	default:
		return -EOPNOTSUPP;
	}
}

static void gem_get_ethtool_strings(struct net_device *dev, u32 sset, u8 *p)
{
	unsigned int i;

	switch (sset) {
	case ETH_SS_STATS:
		for (i = 0; i < GEM_STATS_LEN; i++, p += ETH_GSTRING_LEN)
			memcpy(p, gem_statistics[i].stat_string,
			       ETH_GSTRING_LEN);
		break;
	}
}

static struct net_device_stats *macb_get_stats(struct net_device *dev)
{
	struct macb *bp = netdev_priv(dev);
	struct net_device_stats *nstat = &bp->stats;
	struct macb_stats *hwstat = &bp->hw_stats.macb;

	if (macb_is_gem(bp))
		return gem_get_stats(bp);

	/* read stats from hardware */
	macb_update_stats(bp);

	/* Convert HW stats into netdevice stats */
	nstat->rx_errors = (hwstat->rx_fcs_errors +
			    hwstat->rx_align_errors +
			    hwstat->rx_resource_errors +
			    hwstat->rx_overruns +
			    hwstat->rx_oversize_pkts +
			    hwstat->rx_jabbers +
			    hwstat->rx_undersize_pkts +
			    hwstat->rx_length_mismatch);
	nstat->tx_errors = (hwstat->tx_late_cols +
			    hwstat->tx_excessive_cols +
			    hwstat->tx_underruns +
			    hwstat->tx_carrier_errors +
			    hwstat->sqe_test_errors);
	nstat->collisions = (hwstat->tx_single_cols +
			     hwstat->tx_multiple_cols +
			     hwstat->tx_excessive_cols);
	nstat->rx_length_errors = (hwstat->rx_oversize_pkts +
				   hwstat->rx_jabbers +
				   hwstat->rx_undersize_pkts +
				   hwstat->rx_length_mismatch);
	nstat->rx_over_errors = hwstat->rx_resource_errors +
				   hwstat->rx_overruns;
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
	struct phy_device *phydev = bp->phy_dev;

	if (!phydev)
		return -ENODEV;

	return phy_ethtool_gset(phydev, cmd);
}

static int macb_set_settings(struct net_device *dev, struct ethtool_cmd *cmd)
{
	struct macb *bp = netdev_priv(dev);
	struct phy_device *phydev = bp->phy_dev;

	if (!phydev)
		return -ENODEV;

	return phy_ethtool_sset(phydev, cmd);
}

static int macb_get_regs_len(struct net_device *netdev)
{
	return MACB_GREGS_NBR * sizeof(u32);
}

static void macb_get_regs(struct net_device *dev, struct ethtool_regs *regs,
			  void *p)
{
	struct macb *bp = netdev_priv(dev);
	unsigned int tail, head;
	u32 *regs_buff = p;

	regs->version = (macb_readl(bp, MID) & ((1 << MACB_REV_SIZE) - 1))
			| MACB_GREGS_VERSION;

	tail = macb_tx_ring_wrap(bp->queues[0].tx_tail);
	head = macb_tx_ring_wrap(bp->queues[0].tx_head);

	regs_buff[0]  = macb_readl(bp, NCR);
	regs_buff[1]  = macb_or_gem_readl(bp, NCFGR);
	regs_buff[2]  = macb_readl(bp, NSR);
	regs_buff[3]  = macb_readl(bp, TSR);
	regs_buff[4]  = macb_readl(bp, RBQP);
	regs_buff[5]  = macb_readl(bp, TBQP);
	regs_buff[6]  = macb_readl(bp, RSR);
	regs_buff[7]  = macb_readl(bp, IMR);

	regs_buff[8]  = tail;
	regs_buff[9]  = head;
	regs_buff[10] = macb_tx_dma(&bp->queues[0], tail);
	regs_buff[11] = macb_tx_dma(&bp->queues[0], head);

	regs_buff[12] = macb_or_gem_readl(bp, USRIO);
	if (macb_is_gem(bp)) {
		regs_buff[13] = gem_readl(bp, DMACFG);
	}
}

static const struct ethtool_ops macb_ethtool_ops = {
	.get_settings		= macb_get_settings,
	.set_settings		= macb_set_settings,
	.get_regs_len		= macb_get_regs_len,
	.get_regs		= macb_get_regs,
	.get_link		= ethtool_op_get_link,
	.get_ts_info		= ethtool_op_get_ts_info,
};

static const struct ethtool_ops gem_ethtool_ops = {
	.get_settings		= macb_get_settings,
	.set_settings		= macb_set_settings,
	.get_regs_len		= macb_get_regs_len,
	.get_regs		= macb_get_regs,
	.get_link		= ethtool_op_get_link,
	.get_ts_info		= ethtool_op_get_ts_info,
	.get_ethtool_stats	= gem_get_ethtool_stats,
	.get_strings		= gem_get_ethtool_strings,
	.get_sset_count		= gem_get_sset_count,
};

static int macb_ioctl(struct net_device *dev, struct ifreq *rq, int cmd)
{
	struct macb *bp = netdev_priv(dev);
	struct phy_device *phydev = bp->phy_dev;

	if (!netif_running(dev))
		return -EINVAL;

	if (!phydev)
		return -ENODEV;

	return phy_mii_ioctl(phydev, rq, cmd);
}

static int macb_set_features(struct net_device *netdev,
			     netdev_features_t features)
{
	struct macb *bp = netdev_priv(netdev);
	netdev_features_t changed = features ^ netdev->features;

	/* TX checksum offload */
	if ((changed & NETIF_F_HW_CSUM) && macb_is_gem(bp)) {
		u32 dmacfg;

		dmacfg = gem_readl(bp, DMACFG);
		if (features & NETIF_F_HW_CSUM)
			dmacfg |= GEM_BIT(TXCOEN);
		else
			dmacfg &= ~GEM_BIT(TXCOEN);
		gem_writel(bp, DMACFG, dmacfg);
	}

	/* RX checksum offload */
	if ((changed & NETIF_F_RXCSUM) && macb_is_gem(bp)) {
		u32 netcfg;

		netcfg = gem_readl(bp, NCFGR);
		if (features & NETIF_F_RXCSUM &&
		    !(netdev->flags & IFF_PROMISC))
			netcfg |= GEM_BIT(RXCOEN);
		else
			netcfg &= ~GEM_BIT(RXCOEN);
		gem_writel(bp, NCFGR, netcfg);
	}

	return 0;
}

static const struct net_device_ops macb_netdev_ops = {
	.ndo_open		= macb_open,
	.ndo_stop		= macb_close,
	.ndo_start_xmit		= macb_start_xmit,
	.ndo_set_rx_mode	= macb_set_rx_mode,
	.ndo_get_stats		= macb_get_stats,
	.ndo_do_ioctl		= macb_ioctl,
	.ndo_validate_addr	= eth_validate_addr,
	.ndo_change_mtu		= macb_change_mtu,
	.ndo_set_mac_address	= eth_mac_addr,
#ifdef CONFIG_NET_POLL_CONTROLLER
	.ndo_poll_controller	= macb_poll_controller,
#endif
	.ndo_set_features	= macb_set_features,
};

/*
 * Configure peripheral capabilities according to device tree
 * and integration options used
 */
static void macb_configure_caps(struct macb *bp, const struct macb_config *dt_conf)
{
	u32 dcfg;

	if (dt_conf)
		bp->caps = dt_conf->caps;

	if (hw_is_gem(bp->regs, bp->native_io)) {
		bp->caps |= MACB_CAPS_MACB_IS_GEM;

		dcfg = gem_readl(bp, DCFG1);
		if (GEM_BFEXT(IRQCOR, dcfg) == 0)
			bp->caps |= MACB_CAPS_ISR_CLEAR_ON_WRITE;
		dcfg = gem_readl(bp, DCFG2);
		if ((dcfg & (GEM_BIT(RX_PKT_BUFF) | GEM_BIT(TX_PKT_BUFF))) == 0)
			bp->caps |= MACB_CAPS_FIFO_MODE;
	}

	dev_dbg(&bp->pdev->dev, "Cadence caps 0x%08x\n", bp->caps);
}

static void macb_probe_queues(void __iomem *mem,
			      bool native_io,
			      unsigned int *queue_mask,
			      unsigned int *num_queues)
{
	unsigned int hw_q;

	*queue_mask = 0x1;
	*num_queues = 1;

	/* is it macb or gem ?
	 *
	 * We need to read directly from the hardware here because
	 * we are early in the probe process and don't have the
	 * MACB_CAPS_MACB_IS_GEM flag positioned
	 */
	if (!hw_is_gem(mem, native_io))
		return;

	/* bit 0 is never set but queue 0 always exists */
	*queue_mask = readl_relaxed(mem + GEM_DCFG6) & 0xff;

	*queue_mask |= 0x1;

	for (hw_q = 1; hw_q < MACB_MAX_QUEUES; ++hw_q)
		if (*queue_mask & (1 << hw_q))
			(*num_queues)++;
}

static int macb_clk_init(struct platform_device *pdev, struct clk **pclk,
			 struct clk **hclk, struct clk **tx_clk)
{
	int err;

	*pclk = devm_clk_get(&pdev->dev, "pclk");
	if (IS_ERR(*pclk)) {
		err = PTR_ERR(*pclk);
		dev_err(&pdev->dev, "failed to get macb_clk (%u)\n", err);
		return err;
	}

	*hclk = devm_clk_get(&pdev->dev, "hclk");
	if (IS_ERR(*hclk)) {
		err = PTR_ERR(*hclk);
		dev_err(&pdev->dev, "failed to get hclk (%u)\n", err);
		return err;
	}

	*tx_clk = devm_clk_get(&pdev->dev, "tx_clk");
	if (IS_ERR(*tx_clk))
		*tx_clk = NULL;

	err = clk_prepare_enable(*pclk);
	if (err) {
		dev_err(&pdev->dev, "failed to enable pclk (%u)\n", err);
		return err;
	}

	err = clk_prepare_enable(*hclk);
	if (err) {
		dev_err(&pdev->dev, "failed to enable hclk (%u)\n", err);
		goto err_disable_pclk;
	}

	err = clk_prepare_enable(*tx_clk);
	if (err) {
		dev_err(&pdev->dev, "failed to enable tx_clk (%u)\n", err);
		goto err_disable_hclk;
	}

	return 0;

err_disable_hclk:
	clk_disable_unprepare(*hclk);

err_disable_pclk:
	clk_disable_unprepare(*pclk);

	return err;
}

static int macb_init(struct platform_device *pdev)
{
	struct net_device *dev = platform_get_drvdata(pdev);
	unsigned int hw_q, q;
	struct macb *bp = netdev_priv(dev);
	struct macb_queue *queue;
	int err;
	u32 val;

	/* set the queue register mapping once for all: queue0 has a special
	 * register mapping but we don't want to test the queue index then
	 * compute the corresponding register offset at run time.
	 */
	for (hw_q = 0, q = 0; hw_q < MACB_MAX_QUEUES; ++hw_q) {
		if (!(bp->queue_mask & (1 << hw_q)))
			continue;

		queue = &bp->queues[q];
		queue->bp = bp;
		if (hw_q) {
			queue->ISR  = GEM_ISR(hw_q - 1);
			queue->IER  = GEM_IER(hw_q - 1);
			queue->IDR  = GEM_IDR(hw_q - 1);
			queue->IMR  = GEM_IMR(hw_q - 1);
			queue->TBQP = GEM_TBQP(hw_q - 1);
		} else {
			/* queue0 uses legacy registers */
			queue->ISR  = MACB_ISR;
			queue->IER  = MACB_IER;
			queue->IDR  = MACB_IDR;
			queue->IMR  = MACB_IMR;
			queue->TBQP = MACB_TBQP;
		}

		/* get irq: here we use the linux queue index, not the hardware
		 * queue index. the queue irq definitions in the device tree
		 * must remove the optional gaps that could exist in the
		 * hardware queue mask.
		 */
		queue->irq = platform_get_irq(pdev, q);
		err = devm_request_irq(&pdev->dev, queue->irq, macb_interrupt,
				       IRQF_SHARED, dev->name, queue);
		if (err) {
			dev_err(&pdev->dev,
				"Unable to request IRQ %d (error %d)\n",
				queue->irq, err);
			return err;
		}

		INIT_WORK(&queue->tx_error_task, macb_tx_error_task);
		q++;
	}

	dev->netdev_ops = &macb_netdev_ops;
	netif_napi_add(dev, &bp->napi, macb_poll, 64);

	/* setup appropriated routines according to adapter type */
	if (macb_is_gem(bp)) {
		bp->max_tx_length = GEM_MAX_TX_LEN;
		bp->macbgem_ops.mog_alloc_rx_buffers = gem_alloc_rx_buffers;
		bp->macbgem_ops.mog_free_rx_buffers = gem_free_rx_buffers;
		bp->macbgem_ops.mog_init_rings = gem_init_rings;
		bp->macbgem_ops.mog_rx = gem_rx;
		dev->ethtool_ops = &gem_ethtool_ops;
	} else {
		bp->max_tx_length = MACB_MAX_TX_LEN;
		bp->macbgem_ops.mog_alloc_rx_buffers = macb_alloc_rx_buffers;
		bp->macbgem_ops.mog_free_rx_buffers = macb_free_rx_buffers;
		bp->macbgem_ops.mog_init_rings = macb_init_rings;
		bp->macbgem_ops.mog_rx = macb_rx;
		dev->ethtool_ops = &macb_ethtool_ops;
	}

	/* Set features */
	dev->hw_features = NETIF_F_SG;
	/* Checksum offload is only available on gem with packet buffer */
	if (macb_is_gem(bp) && !(bp->caps & MACB_CAPS_FIFO_MODE))
		dev->hw_features |= NETIF_F_HW_CSUM | NETIF_F_RXCSUM;
	if (bp->caps & MACB_CAPS_SG_DISABLED)
		dev->hw_features &= ~NETIF_F_SG;
	dev->features = dev->hw_features;

	val = 0;
	if (bp->phy_interface == PHY_INTERFACE_MODE_RGMII)
		val = GEM_BIT(RGMII);
	else if (bp->phy_interface == PHY_INTERFACE_MODE_RMII &&
		 (bp->caps & MACB_CAPS_USRIO_DEFAULT_IS_MII_GMII))
		val = MACB_BIT(RMII);
	else if (!(bp->caps & MACB_CAPS_USRIO_DEFAULT_IS_MII_GMII))
		val = MACB_BIT(MII);

	if (bp->caps & MACB_CAPS_USRIO_HAS_CLKEN)
		val |= MACB_BIT(CLKEN);

	macb_or_gem_writel(bp, USRIO, val);

	/* Set MII management clock divider */
	val = macb_mdc_clk_div(bp);
	val |= macb_dbw(bp);
	if (bp->phy_interface == PHY_INTERFACE_MODE_SGMII)
		val |= GEM_BIT(SGMIIEN) | GEM_BIT(PCSSEL);
	macb_writel(bp, NCFGR, val);

	return 0;
}

#if defined(CONFIG_OF)
/* 1518 rounded up */
#define AT91ETHER_MAX_RBUFF_SZ	0x600
/* max number of receive buffers */
#define AT91ETHER_MAX_RX_DESCR	9

/* Initialize and start the Receiver and Transmit subsystems */
static int at91ether_start(struct net_device *dev)
{
	struct macb *lp = netdev_priv(dev);
	dma_addr_t addr;
	u32 ctl;
	int i;

	lp->rx_ring = dma_alloc_coherent(&lp->pdev->dev,
					 (AT91ETHER_MAX_RX_DESCR *
					  sizeof(struct macb_dma_desc)),
					 &lp->rx_ring_dma, GFP_KERNEL);
	if (!lp->rx_ring)
		return -ENOMEM;

	lp->rx_buffers = dma_alloc_coherent(&lp->pdev->dev,
					    AT91ETHER_MAX_RX_DESCR *
					    AT91ETHER_MAX_RBUFF_SZ,
					    &lp->rx_buffers_dma, GFP_KERNEL);
	if (!lp->rx_buffers) {
		dma_free_coherent(&lp->pdev->dev,
				  AT91ETHER_MAX_RX_DESCR *
				  sizeof(struct macb_dma_desc),
				  lp->rx_ring, lp->rx_ring_dma);
		lp->rx_ring = NULL;
		return -ENOMEM;
	}

	addr = lp->rx_buffers_dma;
	for (i = 0; i < AT91ETHER_MAX_RX_DESCR; i++) {
		lp->rx_ring[i].addr = addr;
		lp->rx_ring[i].ctrl = 0;
		addr += AT91ETHER_MAX_RBUFF_SZ;
	}

	/* Set the Wrap bit on the last descriptor */
	lp->rx_ring[AT91ETHER_MAX_RX_DESCR - 1].addr |= MACB_BIT(RX_WRAP);

	/* Reset buffer index */
	lp->rx_tail = 0;

	/* Program address of descriptor list in Rx Buffer Queue register */
	macb_writel(lp, RBQP, lp->rx_ring_dma);

	/* Enable Receive and Transmit */
	ctl = macb_readl(lp, NCR);
	macb_writel(lp, NCR, ctl | MACB_BIT(RE) | MACB_BIT(TE));

	return 0;
}

/* Open the ethernet interface */
static int at91ether_open(struct net_device *dev)
{
	struct macb *lp = netdev_priv(dev);
	u32 ctl;
	int ret;

	/* Clear internal statistics */
	ctl = macb_readl(lp, NCR);
	macb_writel(lp, NCR, ctl | MACB_BIT(CLRSTAT));

	macb_set_hwaddr(lp);

	ret = at91ether_start(dev);
	if (ret)
		return ret;

	/* Enable MAC interrupts */
	macb_writel(lp, IER, MACB_BIT(RCOMP)	|
			     MACB_BIT(RXUBR)	|
			     MACB_BIT(ISR_TUND)	|
			     MACB_BIT(ISR_RLE)	|
			     MACB_BIT(TCOMP)	|
			     MACB_BIT(ISR_ROVR)	|
			     MACB_BIT(HRESP));

	/* schedule a link state check */
	phy_start(lp->phy_dev);

	netif_start_queue(dev);

	return 0;
}

/* Close the interface */
static int at91ether_close(struct net_device *dev)
{
	struct macb *lp = netdev_priv(dev);
	u32 ctl;

	/* Disable Receiver and Transmitter */
	ctl = macb_readl(lp, NCR);
	macb_writel(lp, NCR, ctl & ~(MACB_BIT(TE) | MACB_BIT(RE)));

	/* Disable MAC interrupts */
	macb_writel(lp, IDR, MACB_BIT(RCOMP)	|
			     MACB_BIT(RXUBR)	|
			     MACB_BIT(ISR_TUND)	|
			     MACB_BIT(ISR_RLE)	|
			     MACB_BIT(TCOMP)	|
			     MACB_BIT(ISR_ROVR) |
			     MACB_BIT(HRESP));

	netif_stop_queue(dev);

	dma_free_coherent(&lp->pdev->dev,
			  AT91ETHER_MAX_RX_DESCR *
			  sizeof(struct macb_dma_desc),
			  lp->rx_ring, lp->rx_ring_dma);
	lp->rx_ring = NULL;

	dma_free_coherent(&lp->pdev->dev,
			  AT91ETHER_MAX_RX_DESCR * AT91ETHER_MAX_RBUFF_SZ,
			  lp->rx_buffers, lp->rx_buffers_dma);
	lp->rx_buffers = NULL;

	return 0;
}

/* Transmit packet */
static int at91ether_start_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct macb *lp = netdev_priv(dev);

	if (macb_readl(lp, TSR) & MACB_BIT(RM9200_BNQ)) {
		netif_stop_queue(dev);

		/* Store packet information (to free when Tx completed) */
		lp->skb = skb;
		lp->skb_length = skb->len;
		lp->skb_physaddr = dma_map_single(NULL, skb->data, skb->len,
							DMA_TO_DEVICE);

		/* Set address of the data in the Transmit Address register */
		macb_writel(lp, TAR, lp->skb_physaddr);
		/* Set length of the packet in the Transmit Control register */
		macb_writel(lp, TCR, skb->len);

	} else {
		netdev_err(dev, "%s called, but device is busy!\n", __func__);
		return NETDEV_TX_BUSY;
	}

	return NETDEV_TX_OK;
}

/* Extract received frame from buffer descriptors and sent to upper layers.
 * (Called from interrupt context)
 */
static void at91ether_rx(struct net_device *dev)
{
	struct macb *lp = netdev_priv(dev);
	unsigned char *p_recv;
	struct sk_buff *skb;
	unsigned int pktlen;

	while (lp->rx_ring[lp->rx_tail].addr & MACB_BIT(RX_USED)) {
		p_recv = lp->rx_buffers + lp->rx_tail * AT91ETHER_MAX_RBUFF_SZ;
		pktlen = MACB_BF(RX_FRMLEN, lp->rx_ring[lp->rx_tail].ctrl);
		skb = netdev_alloc_skb(dev, pktlen + 2);
		if (skb) {
			skb_reserve(skb, 2);
			memcpy(skb_put(skb, pktlen), p_recv, pktlen);

			skb->protocol = eth_type_trans(skb, dev);
			lp->stats.rx_packets++;
			lp->stats.rx_bytes += pktlen;
			netif_rx(skb);
		} else {
			lp->stats.rx_dropped++;
		}

		if (lp->rx_ring[lp->rx_tail].ctrl & MACB_BIT(RX_MHASH_MATCH))
			lp->stats.multicast++;

		/* reset ownership bit */
		lp->rx_ring[lp->rx_tail].addr &= ~MACB_BIT(RX_USED);

		/* wrap after last buffer */
		if (lp->rx_tail == AT91ETHER_MAX_RX_DESCR - 1)
			lp->rx_tail = 0;
		else
			lp->rx_tail++;
	}
}

/* MAC interrupt handler */
static irqreturn_t at91ether_interrupt(int irq, void *dev_id)
{
	struct net_device *dev = dev_id;
	struct macb *lp = netdev_priv(dev);
	u32 intstatus, ctl;

	/* MAC Interrupt Status register indicates what interrupts are pending.
	 * It is automatically cleared once read.
	 */
	intstatus = macb_readl(lp, ISR);

	/* Receive complete */
	if (intstatus & MACB_BIT(RCOMP))
		at91ether_rx(dev);

	/* Transmit complete */
	if (intstatus & MACB_BIT(TCOMP)) {
		/* The TCOM bit is set even if the transmission failed */
		if (intstatus & (MACB_BIT(ISR_TUND) | MACB_BIT(ISR_RLE)))
			lp->stats.tx_errors++;

		if (lp->skb) {
			dev_kfree_skb_irq(lp->skb);
			lp->skb = NULL;
			dma_unmap_single(NULL, lp->skb_physaddr,
					 lp->skb_length, DMA_TO_DEVICE);
			lp->stats.tx_packets++;
			lp->stats.tx_bytes += lp->skb_length;
		}
		netif_wake_queue(dev);
	}

	/* Work-around for EMAC Errata section 41.3.1 */
	if (intstatus & MACB_BIT(RXUBR)) {
		ctl = macb_readl(lp, NCR);
		macb_writel(lp, NCR, ctl & ~MACB_BIT(RE));
		macb_writel(lp, NCR, ctl | MACB_BIT(RE));
	}

	if (intstatus & MACB_BIT(ISR_ROVR))
		netdev_err(dev, "ROVR error\n");

	return IRQ_HANDLED;
}

#ifdef CONFIG_NET_POLL_CONTROLLER
static void at91ether_poll_controller(struct net_device *dev)
{
	unsigned long flags;

	local_irq_save(flags);
	at91ether_interrupt(dev->irq, dev);
	local_irq_restore(flags);
}
#endif

static const struct net_device_ops at91ether_netdev_ops = {
	.ndo_open		= at91ether_open,
	.ndo_stop		= at91ether_close,
	.ndo_start_xmit		= at91ether_start_xmit,
	.ndo_get_stats		= macb_get_stats,
	.ndo_set_rx_mode	= macb_set_rx_mode,
	.ndo_set_mac_address	= eth_mac_addr,
	.ndo_do_ioctl		= macb_ioctl,
	.ndo_validate_addr	= eth_validate_addr,
	.ndo_change_mtu		= eth_change_mtu,
#ifdef CONFIG_NET_POLL_CONTROLLER
	.ndo_poll_controller	= at91ether_poll_controller,
#endif
};

static int at91ether_clk_init(struct platform_device *pdev, struct clk **pclk,
			      struct clk **hclk, struct clk **tx_clk)
{
	int err;

	*hclk = NULL;
	*tx_clk = NULL;

	*pclk = devm_clk_get(&pdev->dev, "ether_clk");
	if (IS_ERR(*pclk))
		return PTR_ERR(*pclk);

	err = clk_prepare_enable(*pclk);
	if (err) {
		dev_err(&pdev->dev, "failed to enable pclk (%u)\n", err);
		return err;
	}

	return 0;
}

static int at91ether_init(struct platform_device *pdev)
{
	struct net_device *dev = platform_get_drvdata(pdev);
	struct macb *bp = netdev_priv(dev);
	int err;
	u32 reg;

	dev->netdev_ops = &at91ether_netdev_ops;
	dev->ethtool_ops = &macb_ethtool_ops;

	err = devm_request_irq(&pdev->dev, dev->irq, at91ether_interrupt,
			       0, dev->name, dev);
	if (err)
		return err;

	macb_writel(bp, NCR, 0);

	reg = MACB_BF(CLK, MACB_CLK_DIV32) | MACB_BIT(BIG);
	if (bp->phy_interface == PHY_INTERFACE_MODE_RMII)
		reg |= MACB_BIT(RM9200_RMII);

	macb_writel(bp, NCFGR, reg);

	return 0;
}

static const struct macb_config at91sam9260_config = {
	.caps = MACB_CAPS_USRIO_HAS_CLKEN | MACB_CAPS_USRIO_DEFAULT_IS_MII_GMII,
	.clk_init = macb_clk_init,
	.init = macb_init,
};

static const struct macb_config pc302gem_config = {
	.caps = MACB_CAPS_SG_DISABLED | MACB_CAPS_GIGABIT_MODE_AVAILABLE,
	.dma_burst_length = 16,
	.clk_init = macb_clk_init,
	.init = macb_init,
};

static const struct macb_config sama5d2_config = {
	.caps = MACB_CAPS_USRIO_DEFAULT_IS_MII_GMII,
	.dma_burst_length = 16,
	.clk_init = macb_clk_init,
	.init = macb_init,
};

static const struct macb_config sama5d3_config = {
	.caps = MACB_CAPS_SG_DISABLED | MACB_CAPS_GIGABIT_MODE_AVAILABLE
	      | MACB_CAPS_USRIO_DEFAULT_IS_MII_GMII,
	.dma_burst_length = 16,
	.clk_init = macb_clk_init,
	.init = macb_init,
};

static const struct macb_config sama5d4_config = {
	.caps = MACB_CAPS_USRIO_DEFAULT_IS_MII_GMII,
	.dma_burst_length = 4,
	.clk_init = macb_clk_init,
	.init = macb_init,
};

static const struct macb_config emac_config = {
	.clk_init = at91ether_clk_init,
	.init = at91ether_init,
};


static const struct macb_config zynqmp_config = {
	.caps = MACB_CAPS_GIGABIT_MODE_AVAILABLE | MACB_CAPS_JUMBO,
	.dma_burst_length = 16,
	.clk_init = macb_clk_init,
	.init = macb_init,
	.jumbo_max_len = 10240,
};

static const struct macb_config zynq_config = {
	.caps = MACB_CAPS_GIGABIT_MODE_AVAILABLE | MACB_CAPS_NO_GIGABIT_HALF,
	.dma_burst_length = 16,
	.clk_init = macb_clk_init,
	.init = macb_init,
};

static const struct of_device_id macb_dt_ids[] = {
	{ .compatible = "cdns,at32ap7000-macb" },
	{ .compatible = "cdns,at91sam9260-macb", .data = &at91sam9260_config },
	{ .compatible = "cdns,macb" },
	{ .compatible = "cdns,pc302-gem", .data = &pc302gem_config },
	{ .compatible = "cdns,gem", .data = &pc302gem_config },
	{ .compatible = "atmel,sama5d2-gem", .data = &sama5d2_config },
	{ .compatible = "atmel,sama5d3-gem", .data = &sama5d3_config },
	{ .compatible = "atmel,sama5d4-gem", .data = &sama5d4_config },
	{ .compatible = "cdns,at91rm9200-emac", .data = &emac_config },
	{ .compatible = "cdns,emac", .data = &emac_config },
	{ .compatible = "cdns,zynqmp-gem", .data = &zynqmp_config},
	{ .compatible = "cdns,zynq-gem", .data = &zynq_config },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, macb_dt_ids);
#endif /* CONFIG_OF */

static int macb_probe(struct platform_device *pdev)
{
	int (*clk_init)(struct platform_device *, struct clk **,
			struct clk **, struct clk **)
					      = macb_clk_init;
	int (*init)(struct platform_device *) = macb_init;
	struct device_node *np = pdev->dev.of_node;
	const struct macb_config *macb_config = NULL;
	struct clk *pclk, *hclk, *tx_clk;
	unsigned int queue_mask, num_queues;
	struct macb_platform_data *pdata;
	bool native_io;
	struct phy_device *phydev;
	struct net_device *dev;
	struct resource *regs;
	void __iomem *mem;
	const char *mac;
	struct macb *bp;
	int err;

	regs = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	mem = devm_ioremap_resource(&pdev->dev, regs);
	if (IS_ERR(mem))
		return PTR_ERR(mem);

	if (np) {
		const struct of_device_id *match;

		match = of_match_node(macb_dt_ids, np);
		if (match && match->data) {
			macb_config = match->data;
			clk_init = macb_config->clk_init;
			init = macb_config->init;
		}
	}

	err = clk_init(pdev, &pclk, &hclk, &tx_clk);
	if (err)
		return err;

	native_io = hw_is_native_io(mem);

	macb_probe_queues(mem, native_io, &queue_mask, &num_queues);
	dev = alloc_etherdev_mq(sizeof(*bp), num_queues);
	if (!dev) {
		err = -ENOMEM;
		goto err_disable_clocks;
	}

	dev->base_addr = regs->start;

	SET_NETDEV_DEV(dev, &pdev->dev);

	bp = netdev_priv(dev);
	bp->pdev = pdev;
	bp->dev = dev;
	bp->regs = mem;
	bp->native_io = native_io;
	if (native_io) {
		bp->macb_reg_readl = hw_readl_native;
		bp->macb_reg_writel = hw_writel_native;
	} else {
		bp->macb_reg_readl = hw_readl;
		bp->macb_reg_writel = hw_writel;
	}
	bp->num_queues = num_queues;
	bp->queue_mask = queue_mask;
	if (macb_config)
		bp->dma_burst_length = macb_config->dma_burst_length;
	bp->pclk = pclk;
	bp->hclk = hclk;
	bp->tx_clk = tx_clk;
	if (macb_config)
		bp->jumbo_max_len = macb_config->jumbo_max_len;

	spin_lock_init(&bp->lock);

	/* setup capabilities */
	macb_configure_caps(bp, macb_config);

	platform_set_drvdata(pdev, dev);

	dev->irq = platform_get_irq(pdev, 0);
	if (dev->irq < 0) {
		err = dev->irq;
		goto err_disable_clocks;
	}

	mac = of_get_mac_address(np);
	if (mac)
		memcpy(bp->dev->dev_addr, mac, ETH_ALEN);
	else
		macb_get_hwaddr(bp);

	err = of_get_phy_mode(np);
	if (err < 0) {
		pdata = dev_get_platdata(&pdev->dev);
		if (pdata && pdata->is_rmii)
			bp->phy_interface = PHY_INTERFACE_MODE_RMII;
		else
			bp->phy_interface = PHY_INTERFACE_MODE_MII;
	} else {
		bp->phy_interface = err;
	}

	/* IP specific init */
	err = init(pdev);
	if (err)
		goto err_out_free_netdev;

	err = register_netdev(dev);
	if (err) {
		dev_err(&pdev->dev, "Cannot register net device, aborting.\n");
		goto err_out_unregister_netdev;
	}

	err = macb_mii_init(bp);
	if (err)
		goto err_out_unregister_netdev;

	netif_carrier_off(dev);

	netdev_info(dev, "Cadence %s rev 0x%08x at 0x%08lx irq %d (%pM)\n",
		    macb_is_gem(bp) ? "GEM" : "MACB", macb_readl(bp, MID),
		    dev->base_addr, dev->irq, dev->dev_addr);

	phydev = bp->phy_dev;
	netdev_info(dev, "attached PHY driver [%s] (mii_bus:phy_addr=%s, irq=%d)\n",
		    phydev->drv->name, dev_name(&phydev->dev), phydev->irq);

	return 0;

err_out_unregister_netdev:
	unregister_netdev(dev);

err_out_free_netdev:
	free_netdev(dev);

err_disable_clocks:
	clk_disable_unprepare(tx_clk);
	clk_disable_unprepare(hclk);
	clk_disable_unprepare(pclk);

	return err;
}

static int macb_remove(struct platform_device *pdev)
{
	struct net_device *dev;
	struct macb *bp;

	dev = platform_get_drvdata(pdev);

	if (dev) {
		bp = netdev_priv(dev);
		if (bp->phy_dev)
			phy_disconnect(bp->phy_dev);
		mdiobus_unregister(bp->mii_bus);
		kfree(bp->mii_bus->irq);
		mdiobus_free(bp->mii_bus);
		unregister_netdev(dev);
		clk_disable_unprepare(bp->tx_clk);
		clk_disable_unprepare(bp->hclk);
		clk_disable_unprepare(bp->pclk);
		free_netdev(dev);
	}

	return 0;
}

static int __maybe_unused macb_suspend(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct net_device *netdev = platform_get_drvdata(pdev);
	struct macb *bp = netdev_priv(netdev);

	netif_carrier_off(netdev);
	netif_device_detach(netdev);

	clk_disable_unprepare(bp->tx_clk);
	clk_disable_unprepare(bp->hclk);
	clk_disable_unprepare(bp->pclk);

	return 0;
}

static int __maybe_unused macb_resume(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct net_device *netdev = platform_get_drvdata(pdev);
	struct macb *bp = netdev_priv(netdev);

	clk_prepare_enable(bp->pclk);
	clk_prepare_enable(bp->hclk);
	clk_prepare_enable(bp->tx_clk);

	netif_device_attach(netdev);

	return 0;
}

static SIMPLE_DEV_PM_OPS(macb_pm_ops, macb_suspend, macb_resume);

static struct platform_driver macb_driver = {
	.probe		= macb_probe,
	.remove		= macb_remove,
	.driver		= {
		.name		= "macb",
		.of_match_table	= of_match_ptr(macb_dt_ids),
		.pm	= &macb_pm_ops,
	},
};

module_platform_driver(macb_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Cadence MACB/GEM Ethernet driver");
MODULE_AUTHOR("Haavard Skinnemoen (Atmel)");
MODULE_ALIAS("platform:macb");
