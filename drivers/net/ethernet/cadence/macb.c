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
#include <linux/of_net.h>
#include <linux/pinctrl/consumer.h>

#include "macb.h"

#define RX_BUFFER_SIZE		128
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

static struct macb_dma_desc *macb_tx_desc(struct macb *bp, unsigned int index)
{
	return &bp->tx_ring[macb_tx_ring_wrap(index)];
}

static struct macb_tx_skb *macb_tx_skb(struct macb *bp, unsigned int index)
{
	return &bp->tx_skb[macb_tx_ring_wrap(index)];
}

static dma_addr_t macb_tx_dma(struct macb *bp, unsigned int index)
{
	dma_addr_t offset;

	offset = macb_tx_ring_wrap(index) * sizeof(struct macb_dma_desc);

	return bp->tx_ring_dma + offset;
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
	return bp->rx_buffers + RX_BUFFER_SIZE * macb_rx_ring_wrap(index);
}

void macb_set_hwaddr(struct macb *bp)
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
EXPORT_SYMBOL_GPL(macb_set_hwaddr);

void macb_get_hwaddr(struct macb *bp)
{
	struct macb_platform_data *pdata;
	u32 bottom;
	u16 top;
	u8 addr[6];
	int i;

	pdata = bp->pdev->dev.platform_data;

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

	netdev_info(bp->dev, "invalid hw address, using random\n");
	eth_hw_addr_random(bp->dev);
}
EXPORT_SYMBOL_GPL(macb_get_hwaddr);

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

static int macb_mdio_reset(struct mii_bus *bus)
{
	return 0;
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
			if (phydev->speed == SPEED_1000)
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
		return -1;
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
	ret = phy_connect_direct(dev, phydev, &macb_handle_link_change, 0,
				 bp->phy_interface);
	if (ret) {
		netdev_err(dev, "Could not attach to PHY\n");
		return ret;
	}

	/* mask with MAC supported features */
	if (macb_is_gem(bp))
		phydev->supported &= PHY_GBIT_FEATURES;
	else
		phydev->supported &= PHY_BASIC_FEATURES;

	phydev->advertising = phydev->supported;

	bp->link = 0;
	bp->speed = 0;
	bp->duplex = -1;
	bp->phy_dev = phydev;

	return 0;
}

int macb_mii_init(struct macb *bp)
{
	struct macb_platform_data *pdata;
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
	bp->mii_bus->reset = &macb_mdio_reset;
	snprintf(bp->mii_bus->id, MII_BUS_ID_SIZE, "%s-%x",
		bp->pdev->name, bp->pdev->id);
	bp->mii_bus->priv = bp;
	bp->mii_bus->parent = &bp->dev->dev;
	pdata = bp->pdev->dev.platform_data;

	if (pdata)
		bp->mii_bus->phy_mask = pdata->phy_mask;

	bp->mii_bus->irq = kmalloc(sizeof(int)*PHY_MAX_ADDR, GFP_KERNEL);
	if (!bp->mii_bus->irq) {
		err = -ENOMEM;
		goto err_out_free_mdiobus;
	}

	for (i = 0; i < PHY_MAX_ADDR; i++)
		bp->mii_bus->irq[i] = PHY_POLL;

	dev_set_drvdata(&bp->dev->dev, bp->mii_bus);

	if (mdiobus_register(bp->mii_bus))
		goto err_out_free_mdio_irq;

	if (macb_mii_probe(bp->dev) != 0) {
		goto err_out_unregister_bus;
	}

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
EXPORT_SYMBOL_GPL(macb_mii_init);

static void macb_update_stats(struct macb *bp)
{
	u32 __iomem *reg = bp->regs + MACB_PFR;
	u32 *p = &bp->hw_stats.macb.rx_pause_frames;
	u32 *end = &bp->hw_stats.macb.tx_pause_frames + 1;

	WARN_ON((unsigned long)(end - p - 1) != (MACB_TPF - MACB_PFR) / 4);

	for(; p < end; p++, reg++)
		*p += __raw_readl(reg);
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

		usleep_range(10, 250);
	} while (time_before(halt_time, timeout));

	return -ETIMEDOUT;
}

static void macb_tx_error_task(struct work_struct *work)
{
	struct macb	*bp = container_of(work, struct macb, tx_error_task);
	struct macb_tx_skb	*tx_skb;
	struct sk_buff		*skb;
	unsigned int		tail;

	netdev_vdbg(bp->dev, "macb_tx_error_task: t = %u, h = %u\n",
		    bp->tx_tail, bp->tx_head);

	/* Make sure nobody is trying to queue up new packets */
	netif_stop_queue(bp->dev);

	/*
	 * Stop transmission now
	 * (in case we have just queued new packets)
	 */
	if (macb_halt_tx(bp))
		/* Just complain for now, reinitializing TX path can be good */
		netdev_err(bp->dev, "BUG: halt tx timed out\n");

	/* No need for the lock here as nobody will interrupt us anymore */

	/*
	 * Treat frames in TX queue including the ones that caused the error.
	 * Free transmit buffers in upper layer.
	 */
	for (tail = bp->tx_tail; tail != bp->tx_head; tail++) {
		struct macb_dma_desc	*desc;
		u32			ctrl;

		desc = macb_tx_desc(bp, tail);
		ctrl = desc->ctrl;
		tx_skb = macb_tx_skb(bp, tail);
		skb = tx_skb->skb;

		if (ctrl & MACB_BIT(TX_USED)) {
			netdev_vdbg(bp->dev, "txerr skb %u (data %p) TX complete\n",
				    macb_tx_ring_wrap(tail), skb->data);
			bp->stats.tx_packets++;
			bp->stats.tx_bytes += skb->len;
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

		dma_unmap_single(&bp->pdev->dev, tx_skb->mapping, skb->len,
				 DMA_TO_DEVICE);
		tx_skb->skb = NULL;
		dev_kfree_skb(skb);
	}

	/* Make descriptor updates visible to hardware */
	wmb();

	/* Reinitialize the TX desc queue */
	macb_writel(bp, TBQP, bp->tx_ring_dma);
	/* Make TX ring reflect state of hardware */
	bp->tx_head = bp->tx_tail = 0;

	/* Now we are ready to start transmission again */
	netif_wake_queue(bp->dev);

	/* Housework before enabling TX IRQ */
	macb_writel(bp, TSR, macb_readl(bp, TSR));
	macb_writel(bp, IER, MACB_TX_INT_FLAGS);
}

static void macb_tx_interrupt(struct macb *bp)
{
	unsigned int tail;
	unsigned int head;
	u32 status;

	status = macb_readl(bp, TSR);
	macb_writel(bp, TSR, status);

	netdev_vdbg(bp->dev, "macb_tx_interrupt status = 0x%03lx\n",
		(unsigned long)status);

	head = bp->tx_head;
	for (tail = bp->tx_tail; tail != head; tail++) {
		struct macb_tx_skb	*tx_skb;
		struct sk_buff		*skb;
		struct macb_dma_desc	*desc;
		u32			ctrl;

		desc = macb_tx_desc(bp, tail);

		/* Make hw descriptor updates visible to CPU */
		rmb();

		ctrl = desc->ctrl;

		if (!(ctrl & MACB_BIT(TX_USED)))
			break;

		tx_skb = macb_tx_skb(bp, tail);
		skb = tx_skb->skb;

		netdev_vdbg(bp->dev, "skb %u (data %p) TX complete\n",
			macb_tx_ring_wrap(tail), skb->data);
		dma_unmap_single(&bp->pdev->dev, tx_skb->mapping, skb->len,
				 DMA_TO_DEVICE);
		bp->stats.tx_packets++;
		bp->stats.tx_bytes += skb->len;
		tx_skb->skb = NULL;
		dev_kfree_skb_irq(skb);
	}

	bp->tx_tail = tail;
	if (netif_queue_stopped(bp->dev)
			&& CIRC_CNT(bp->tx_head, bp->tx_tail,
				    TX_RING_SIZE) <= MACB_TX_WAKEUP_THRESH)
		netif_wake_queue(bp->dev);
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
	len = MACB_BFEXT(RX_FRMLEN, desc->ctrl);

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
		unsigned int frag_len = RX_BUFFER_SIZE;

		if (offset + frag_len > len) {
			BUG_ON(frag != last_frag);
			frag_len = len - offset;
		}
		skb_copy_to_linear_data_offset(skb, offset,
				macb_rx_buffer(bp, frag), frag_len);
		offset += RX_BUFFER_SIZE;
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

	work_done = macb_rx(bp, budget);
	if (work_done < budget) {
		napi_complete(napi);

		/*
		 * We've done what we can to clean the buffers. Make sure we
		 * get notified when new packets arrive.
		 */
		macb_writel(bp, IER, MACB_RX_INT_FLAGS);
	}

	/* TODO: Handle errors */

	return work_done;
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
		/* close possible race with dev_close */
		if (unlikely(!netif_running(dev))) {
			macb_writel(bp, IDR, -1);
			break;
		}

		netdev_vdbg(bp->dev, "isr = 0x%08lx\n", (unsigned long)status);

		if (status & MACB_RX_INT_FLAGS) {
			/*
			 * There's no point taking any more interrupts
			 * until we have processed the buffers. The
			 * scheduling call may fail if the poll routine
			 * is already scheduled, so disable interrupts
			 * now.
			 */
			macb_writel(bp, IDR, MACB_RX_INT_FLAGS);

			if (napi_schedule_prep(&bp->napi)) {
				netdev_vdbg(bp->dev, "scheduling RX softirq\n");
				__napi_schedule(&bp->napi);
			}
		}

		if (unlikely(status & (MACB_TX_ERR_FLAGS))) {
			macb_writel(bp, IDR, MACB_TX_INT_FLAGS);
			schedule_work(&bp->tx_error_task);
			break;
		}

		if (status & MACB_BIT(TCOMP))
			macb_tx_interrupt(bp);

		/*
		 * Link change detection isn't possible with RMII, so we'll
		 * add that if/when we get our hands on a full-blown MII PHY.
		 */

		if (status & MACB_BIT(ISR_ROVR)) {
			/* We missed at least one packet */
			if (macb_is_gem(bp))
				bp->hw_stats.gem.rx_overruns++;
			else
				bp->hw_stats.macb.rx_overruns++;
		}

		if (status & MACB_BIT(HRESP)) {
			/*
			 * TODO: Reset the hardware, and maybe move the
			 * netdev_err to a lower-priority context as well
			 * (work queue?)
			 */
			netdev_err(dev, "DMA bus error: HRESP not OK\n");
		}

		status = macb_readl(bp, ISR);
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
	unsigned long flags;

	local_irq_save(flags);
	macb_interrupt(dev->irq, dev);
	local_irq_restore(flags);
}
#endif

static int macb_start_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct macb *bp = netdev_priv(dev);
	dma_addr_t mapping;
	unsigned int len, entry;
	struct macb_dma_desc *desc;
	struct macb_tx_skb *tx_skb;
	u32 ctrl;
	unsigned long flags;

#if defined(DEBUG) && defined(VERBOSE_DEBUG)
	netdev_vdbg(bp->dev,
		   "start_xmit: len %u head %p data %p tail %p end %p\n",
		   skb->len, skb->head, skb->data,
		   skb_tail_pointer(skb), skb_end_pointer(skb));
	print_hex_dump(KERN_DEBUG, "data: ", DUMP_PREFIX_OFFSET, 16, 1,
		       skb->data, 16, true);
#endif

	len = skb->len;
	spin_lock_irqsave(&bp->lock, flags);

	/* This is a hard error, log it. */
	if (CIRC_SPACE(bp->tx_head, bp->tx_tail, TX_RING_SIZE) < 1) {
		netif_stop_queue(dev);
		spin_unlock_irqrestore(&bp->lock, flags);
		netdev_err(bp->dev, "BUG! Tx Ring full when queue awake!\n");
		netdev_dbg(bp->dev, "tx_head = %u, tx_tail = %u\n",
			   bp->tx_head, bp->tx_tail);
		return NETDEV_TX_BUSY;
	}

	entry = macb_tx_ring_wrap(bp->tx_head);
	bp->tx_head++;
	netdev_vdbg(bp->dev, "Allocated ring entry %u\n", entry);
	mapping = dma_map_single(&bp->pdev->dev, skb->data,
				 len, DMA_TO_DEVICE);

	tx_skb = &bp->tx_skb[entry];
	tx_skb->skb = skb;
	tx_skb->mapping = mapping;
	netdev_vdbg(bp->dev, "Mapped skb data %p to DMA addr %08lx\n",
		   skb->data, (unsigned long)mapping);

	ctrl = MACB_BF(TX_FRMLEN, len);
	ctrl |= MACB_BIT(TX_LAST);
	if (entry == (TX_RING_SIZE - 1))
		ctrl |= MACB_BIT(TX_WRAP);

	desc = &bp->tx_ring[entry];
	desc->addr = mapping;
	desc->ctrl = ctrl;

	/* Make newly initialized descriptor visible to hardware */
	wmb();

	skb_tx_timestamp(skb);

	macb_writel(bp, NCR, macb_readl(bp, NCR) | MACB_BIT(TSTART));

	if (CIRC_SPACE(bp->tx_head, bp->tx_tail, TX_RING_SIZE) < 1)
		netif_stop_queue(dev);

	spin_unlock_irqrestore(&bp->lock, flags);

	return NETDEV_TX_OK;
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

	size = TX_RING_SIZE * sizeof(struct macb_tx_skb);
	bp->tx_skb = kmalloc(size, GFP_KERNEL);
	if (!bp->tx_skb)
		goto out_err;

	size = RX_RING_BYTES;
	bp->rx_ring = dma_alloc_coherent(&bp->pdev->dev, size,
					 &bp->rx_ring_dma, GFP_KERNEL);
	if (!bp->rx_ring)
		goto out_err;
	netdev_dbg(bp->dev,
		   "Allocated RX ring of %d bytes at %08lx (mapped %p)\n",
		   size, (unsigned long)bp->rx_ring_dma, bp->rx_ring);

	size = TX_RING_BYTES;
	bp->tx_ring = dma_alloc_coherent(&bp->pdev->dev, size,
					 &bp->tx_ring_dma, GFP_KERNEL);
	if (!bp->tx_ring)
		goto out_err;
	netdev_dbg(bp->dev,
		   "Allocated TX ring of %d bytes at %08lx (mapped %p)\n",
		   size, (unsigned long)bp->tx_ring_dma, bp->tx_ring);

	size = RX_RING_SIZE * RX_BUFFER_SIZE;
	bp->rx_buffers = dma_alloc_coherent(&bp->pdev->dev, size,
					    &bp->rx_buffers_dma, GFP_KERNEL);
	if (!bp->rx_buffers)
		goto out_err;
	netdev_dbg(bp->dev,
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
	macb_writel(bp, IDR, -1);
	macb_readl(bp, ISR);
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
 * Configure the receive DMA engine to use the correct receive buffer size.
 * This is a configurable parameter for GEM.
 */
static void macb_configure_dma(struct macb *bp)
{
	u32 dmacfg;

	if (macb_is_gem(bp)) {
		dmacfg = gem_readl(bp, DMACFG) & ~GEM_BF(RXBS, -1L);
		dmacfg |= GEM_BF(RXBS, RX_BUFFER_SIZE / 64);
		gem_writel(bp, DMACFG, dmacfg);
	}
}

static void macb_init_hw(struct macb *bp)
{
	u32 config;

	macb_reset_hw(bp);
	macb_set_hwaddr(bp);

	config = macb_mdc_clk_div(bp);
	config |= MACB_BF(RBOF, NET_IP_ALIGN);	/* Make eth data aligned */
	config |= MACB_BIT(PAE);		/* PAuse Enable */
	config |= MACB_BIT(DRFCS);		/* Discard Rx FCS */
	config |= MACB_BIT(BIG);		/* Receive oversized frames */
	if (bp->dev->flags & IFF_PROMISC)
		config |= MACB_BIT(CAF);	/* Copy All Frames */
	if (!(bp->dev->flags & IFF_BROADCAST))
		config |= MACB_BIT(NBC);	/* No BroadCast */
	config |= macb_dbw(bp);
	macb_writel(bp, NCFGR, config);
	bp->speed = SPEED_10;
	bp->duplex = DUPLEX_HALF;

	macb_configure_dma(bp);

	/* Initialize TX and RX buffers */
	macb_writel(bp, RBQP, bp->rx_ring_dma);
	macb_writel(bp, TBQP, bp->tx_ring_dma);

	/* Enable TX and RX */
	macb_writel(bp, NCR, MACB_BIT(RE) | MACB_BIT(TE) | MACB_BIT(MPE));

	/* Enable interrupts */
	macb_writel(bp, IER, (MACB_RX_INT_FLAGS
			      | MACB_TX_INT_FLAGS
			      | MACB_BIT(HRESP)));

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
			bitval ^= hash_bit_value(i*6 + j, addr);

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
void macb_set_rx_mode(struct net_device *dev)
{
	unsigned long cfg;
	struct macb *bp = netdev_priv(dev);

	cfg = macb_readl(bp, NCFGR);

	if (dev->flags & IFF_PROMISC)
		/* Enable promiscuous mode */
		cfg |= MACB_BIT(CAF);
	else if (dev->flags & (~IFF_PROMISC))
		 /* Disable promiscuous mode */
		cfg &= ~MACB_BIT(CAF);

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
EXPORT_SYMBOL_GPL(macb_set_rx_mode);

static int macb_open(struct net_device *dev)
{
	struct macb *bp = netdev_priv(dev);
	int err;

	netdev_dbg(bp->dev, "open\n");

	/* carrier starts down */
	netif_carrier_off(dev);

	/* if the phy is not yet register, retry later*/
	if (!bp->phy_dev)
		return -EAGAIN;

	err = macb_alloc_consistent(bp);
	if (err) {
		netdev_err(dev, "Unable to allocate DMA memory (error %d)\n",
			   err);
		return err;
	}

	napi_enable(&bp->napi);

	macb_init_rings(bp);
	macb_init_hw(bp);

	/* schedule a link state check */
	phy_start(bp->phy_dev);

	netif_start_queue(dev);

	return 0;
}

static int macb_close(struct net_device *dev)
{
	struct macb *bp = netdev_priv(dev);
	unsigned long flags;

	netif_stop_queue(dev);
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

static void gem_update_stats(struct macb *bp)
{
	u32 __iomem *reg = bp->regs + GEM_OTX;
	u32 *p = &bp->hw_stats.gem.tx_octets_31_0;
	u32 *end = &bp->hw_stats.gem.rx_udp_checksum_errors + 1;

	for (; p < end; p++, reg++)
		*p += __raw_readl(reg);
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

struct net_device_stats *macb_get_stats(struct net_device *dev)
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
EXPORT_SYMBOL_GPL(macb_get_stats);

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

	tail = macb_tx_ring_wrap(bp->tx_tail);
	head = macb_tx_ring_wrap(bp->tx_head);

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
	regs_buff[10] = macb_tx_dma(bp, tail);
	regs_buff[11] = macb_tx_dma(bp, head);

	if (macb_is_gem(bp)) {
		regs_buff[12] = gem_readl(bp, USRIO);
		regs_buff[13] = gem_readl(bp, DMACFG);
	}
}

const struct ethtool_ops macb_ethtool_ops = {
	.get_settings		= macb_get_settings,
	.set_settings		= macb_set_settings,
	.get_regs_len		= macb_get_regs_len,
	.get_regs		= macb_get_regs,
	.get_link		= ethtool_op_get_link,
	.get_ts_info		= ethtool_op_get_ts_info,
};
EXPORT_SYMBOL_GPL(macb_ethtool_ops);

int macb_ioctl(struct net_device *dev, struct ifreq *rq, int cmd)
{
	struct macb *bp = netdev_priv(dev);
	struct phy_device *phydev = bp->phy_dev;

	if (!netif_running(dev))
		return -EINVAL;

	if (!phydev)
		return -ENODEV;

	return phy_mii_ioctl(phydev, rq, cmd);
}
EXPORT_SYMBOL_GPL(macb_ioctl);

static const struct net_device_ops macb_netdev_ops = {
	.ndo_open		= macb_open,
	.ndo_stop		= macb_close,
	.ndo_start_xmit		= macb_start_xmit,
	.ndo_set_rx_mode	= macb_set_rx_mode,
	.ndo_get_stats		= macb_get_stats,
	.ndo_do_ioctl		= macb_ioctl,
	.ndo_validate_addr	= eth_validate_addr,
	.ndo_change_mtu		= eth_change_mtu,
	.ndo_set_mac_address	= eth_mac_addr,
#ifdef CONFIG_NET_POLL_CONTROLLER
	.ndo_poll_controller	= macb_poll_controller,
#endif
};

#if defined(CONFIG_OF)
static const struct of_device_id macb_dt_ids[] = {
	{ .compatible = "cdns,at32ap7000-macb" },
	{ .compatible = "cdns,at91sam9260-macb" },
	{ .compatible = "cdns,macb" },
	{ .compatible = "cdns,pc302-gem" },
	{ .compatible = "cdns,gem" },
	{ /* sentinel */ }
};

MODULE_DEVICE_TABLE(of, macb_dt_ids);

static int __devinit macb_get_phy_mode_dt(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;

	if (np)
		return of_get_phy_mode(np);

	return -ENODEV;
}

static int __devinit macb_get_hwaddr_dt(struct macb *bp)
{
	struct device_node *np = bp->pdev->dev.of_node;
	if (np) {
		const char *mac = of_get_mac_address(np);
		if (mac) {
			memcpy(bp->dev->dev_addr, mac, ETH_ALEN);
			return 0;
		}
	}

	return -ENODEV;
}
#else
static int __devinit macb_get_phy_mode_dt(struct platform_device *pdev)
{
	return -ENODEV;
}
static int __devinit macb_get_hwaddr_dt(struct macb *bp)
{
	return -ENODEV;
}
#endif

static int __init macb_probe(struct platform_device *pdev)
{
	struct macb_platform_data *pdata;
	struct resource *regs;
	struct net_device *dev;
	struct macb *bp;
	struct phy_device *phydev;
	u32 config;
	int err = -ENXIO;
	struct pinctrl *pinctrl;

	regs = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!regs) {
		dev_err(&pdev->dev, "no mmio resource defined\n");
		goto err_out;
	}

	pinctrl = devm_pinctrl_get_select_default(&pdev->dev);
	if (IS_ERR(pinctrl)) {
		err = PTR_ERR(pinctrl);
		if (err == -EPROBE_DEFER)
			goto err_out;

		dev_warn(&pdev->dev, "No pinctrl provided\n");
	}

	err = -ENOMEM;
	dev = alloc_etherdev(sizeof(*bp));
	if (!dev)
		goto err_out;

	SET_NETDEV_DEV(dev, &pdev->dev);

	/* TODO: Actually, we have some interesting features... */
	dev->features |= 0;

	bp = netdev_priv(dev);
	bp->pdev = pdev;
	bp->dev = dev;

	spin_lock_init(&bp->lock);
	INIT_WORK(&bp->tx_error_task, macb_tx_error_task);

	bp->pclk = clk_get(&pdev->dev, "pclk");
	if (IS_ERR(bp->pclk)) {
		dev_err(&pdev->dev, "failed to get macb_clk\n");
		goto err_out_free_dev;
	}
	clk_enable(bp->pclk);

	bp->hclk = clk_get(&pdev->dev, "hclk");
	if (IS_ERR(bp->hclk)) {
		dev_err(&pdev->dev, "failed to get hclk\n");
		goto err_out_put_pclk;
	}
	clk_enable(bp->hclk);

	bp->regs = ioremap(regs->start, resource_size(regs));
	if (!bp->regs) {
		dev_err(&pdev->dev, "failed to map registers, aborting.\n");
		err = -ENOMEM;
		goto err_out_disable_clocks;
	}

	dev->irq = platform_get_irq(pdev, 0);
	err = request_irq(dev->irq, macb_interrupt, 0, dev->name, dev);
	if (err) {
		dev_err(&pdev->dev, "Unable to request IRQ %d (error %d)\n",
			dev->irq, err);
		goto err_out_iounmap;
	}

	dev->netdev_ops = &macb_netdev_ops;
	netif_napi_add(dev, &bp->napi, macb_poll, 64);
	dev->ethtool_ops = &macb_ethtool_ops;

	dev->base_addr = regs->start;

	/* Set MII management clock divider */
	config = macb_mdc_clk_div(bp);
	config |= macb_dbw(bp);
	macb_writel(bp, NCFGR, config);

	err = macb_get_hwaddr_dt(bp);
	if (err < 0)
		macb_get_hwaddr(bp);

	err = macb_get_phy_mode_dt(pdev);
	if (err < 0) {
		pdata = pdev->dev.platform_data;
		if (pdata && pdata->is_rmii)
			bp->phy_interface = PHY_INTERFACE_MODE_RMII;
		else
			bp->phy_interface = PHY_INTERFACE_MODE_MII;
	} else {
		bp->phy_interface = err;
	}

	if (bp->phy_interface == PHY_INTERFACE_MODE_RGMII)
		macb_or_gem_writel(bp, USRIO, GEM_BIT(RGMII));
	else if (bp->phy_interface == PHY_INTERFACE_MODE_RMII)
#if defined(CONFIG_ARCH_AT91)
		macb_or_gem_writel(bp, USRIO, (MACB_BIT(RMII) |
					       MACB_BIT(CLKEN)));
#else
		macb_or_gem_writel(bp, USRIO, 0);
#endif
	else
#if defined(CONFIG_ARCH_AT91)
		macb_or_gem_writel(bp, USRIO, MACB_BIT(CLKEN));
#else
		macb_or_gem_writel(bp, USRIO, MACB_BIT(MII));
#endif

	err = register_netdev(dev);
	if (err) {
		dev_err(&pdev->dev, "Cannot register net device, aborting.\n");
		goto err_out_free_irq;
	}

	if (macb_mii_init(bp) != 0) {
		goto err_out_unregister_netdev;
	}

	platform_set_drvdata(pdev, dev);

	netif_carrier_off(dev);

	netdev_info(dev, "Cadence %s at 0x%08lx irq %d (%pM)\n",
		    macb_is_gem(bp) ? "GEM" : "MACB", dev->base_addr,
		    dev->irq, dev->dev_addr);

	phydev = bp->phy_dev;
	netdev_info(dev, "attached PHY driver [%s] (mii_bus:phy_addr=%s, irq=%d)\n",
		    phydev->drv->name, dev_name(&phydev->dev), phydev->irq);

	return 0;

err_out_unregister_netdev:
	unregister_netdev(dev);
err_out_free_irq:
	free_irq(dev->irq, dev);
err_out_iounmap:
	iounmap(bp->regs);
err_out_disable_clocks:
	clk_disable(bp->hclk);
	clk_put(bp->hclk);
	clk_disable(bp->pclk);
err_out_put_pclk:
	clk_put(bp->pclk);
err_out_free_dev:
	free_netdev(dev);
err_out:
	platform_set_drvdata(pdev, NULL);
	return err;
}

static int __exit macb_remove(struct platform_device *pdev)
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
		free_irq(dev->irq, dev);
		iounmap(bp->regs);
		clk_disable(bp->hclk);
		clk_put(bp->hclk);
		clk_disable(bp->pclk);
		clk_put(bp->pclk);
		free_netdev(dev);
		platform_set_drvdata(pdev, NULL);
	}

	return 0;
}

#ifdef CONFIG_PM
static int macb_suspend(struct platform_device *pdev, pm_message_t state)
{
	struct net_device *netdev = platform_get_drvdata(pdev);
	struct macb *bp = netdev_priv(netdev);

	netif_carrier_off(netdev);
	netif_device_detach(netdev);

	clk_disable(bp->hclk);
	clk_disable(bp->pclk);

	return 0;
}

static int macb_resume(struct platform_device *pdev)
{
	struct net_device *netdev = platform_get_drvdata(pdev);
	struct macb *bp = netdev_priv(netdev);

	clk_enable(bp->pclk);
	clk_enable(bp->hclk);

	netif_device_attach(netdev);

	return 0;
}
#else
#define macb_suspend	NULL
#define macb_resume	NULL
#endif

static struct platform_driver macb_driver = {
	.remove		= __exit_p(macb_remove),
	.suspend	= macb_suspend,
	.resume		= macb_resume,
	.driver		= {
		.name		= "macb",
		.owner	= THIS_MODULE,
		.of_match_table	= of_match_ptr(macb_dt_ids),
	},
};

static int __init macb_init(void)
{
	return platform_driver_probe(&macb_driver, macb_probe);
}

static void __exit macb_exit(void)
{
	platform_driver_unregister(&macb_driver);
}

module_init(macb_init);
module_exit(macb_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Cadence MACB/GEM Ethernet driver");
MODULE_AUTHOR("Haavard Skinnemoen (Atmel)");
MODULE_ALIAS("platform:macb");
