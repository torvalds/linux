/*
 * meth.c -- O2 Builtin 10/100 Ethernet driver
 *
 * Copyright (C) 2001-2003 Ilya Volynets
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License
 *	as published by the Free Software Foundation; either version
 *	2 of the License, or (at your option) any later version.
 */
#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/interrupt.h>

#include <linux/in.h>
#include <linux/in6.h>
#include <linux/device.h> /* struct device, et al */
#include <linux/netdevice.h>   /* struct device, and other headers */
#include <linux/etherdevice.h> /* eth_type_trans */
#include <linux/ip.h>          /* struct iphdr */
#include <linux/tcp.h>         /* struct tcphdr */
#include <linux/skbuff.h>
#include <linux/mii.h>         /* MII definitions */

#include <asm/ip32/mace.h>
#include <asm/ip32/ip32_ints.h>

#include <asm/io.h>

#include "meth.h"

#ifndef MFE_DEBUG
#define MFE_DEBUG 0
#endif

#if MFE_DEBUG>=1
#define DPRINTK(str,args...) printk(KERN_DEBUG "meth: %s: " str, __func__ , ## args)
#define MFE_RX_DEBUG 2
#else
#define DPRINTK(str,args...)
#define MFE_RX_DEBUG 0
#endif


static const char *meth_str="SGI O2 Fast Ethernet";

#define HAVE_TX_TIMEOUT
/* The maximum time waited (in jiffies) before assuming a Tx failed. (400ms) */
#define TX_TIMEOUT (400*HZ/1000)

#ifdef HAVE_TX_TIMEOUT
static int timeout = TX_TIMEOUT;
module_param(timeout, int, 0);
#endif

/*
 * This structure is private to each device. It is used to pass
 * packets in and out, so there is place for a packet
 */
struct meth_private {
	/* in-memory copy of MAC Control register */
	unsigned long mac_ctrl;
	/* in-memory copy of DMA Control register */
	unsigned long dma_ctrl;
	/* address of PHY, used by mdio_* functions, initialized in mdio_probe */
	unsigned long phy_addr;
	tx_packet *tx_ring;
	dma_addr_t tx_ring_dma;
	struct sk_buff *tx_skbs[TX_RING_ENTRIES];
	dma_addr_t tx_skb_dmas[TX_RING_ENTRIES];
	unsigned long tx_read, tx_write, tx_count;

	rx_packet *rx_ring[RX_RING_ENTRIES];
	dma_addr_t rx_ring_dmas[RX_RING_ENTRIES];
	struct sk_buff *rx_skbs[RX_RING_ENTRIES];
	unsigned long rx_write;

	spinlock_t meth_lock;
};

static void meth_tx_timeout(struct net_device *dev);
static irqreturn_t meth_interrupt(int irq, void *dev_id);

/* global, initialized in ip32-setup.c */
char o2meth_eaddr[8]={0,0,0,0,0,0,0,0};

static inline void load_eaddr(struct net_device *dev)
{
	int i;
	u64 macaddr;

	DPRINTK("Loading MAC Address: %pM\n", dev->dev_addr);
	macaddr = 0;
	for (i = 0; i < 6; i++)
		macaddr |= (u64)dev->dev_addr[i] << ((5 - i) * 8);

	mace->eth.mac_addr = macaddr;
}

/*
 * Waits for BUSY status of mdio bus to clear
 */
#define WAIT_FOR_PHY(___rval)					\
	while ((___rval = mace->eth.phy_data) & MDIO_BUSY) {	\
		udelay(25);					\
	}
/*read phy register, return value read */
static unsigned long mdio_read(struct meth_private *priv, unsigned long phyreg)
{
	unsigned long rval;
	WAIT_FOR_PHY(rval);
	mace->eth.phy_regs = (priv->phy_addr << 5) | (phyreg & 0x1f);
	udelay(25);
	mace->eth.phy_trans_go = 1;
	udelay(25);
	WAIT_FOR_PHY(rval);
	return rval & MDIO_DATA_MASK;
}

static int mdio_probe(struct meth_private *priv)
{
	int i;
	unsigned long p2, p3, flags;
	/* check if phy is detected already */
	if(priv->phy_addr>=0&&priv->phy_addr<32)
		return 0;
	spin_lock_irqsave(&priv->meth_lock, flags);
	for (i=0;i<32;++i){
		priv->phy_addr=i;
		p2=mdio_read(priv,2);
		p3=mdio_read(priv,3);
#if MFE_DEBUG>=2
		switch ((p2<<12)|(p3>>4)){
		case PHY_QS6612X:
			DPRINTK("PHY is QS6612X\n");
			break;
		case PHY_ICS1889:
			DPRINTK("PHY is ICS1889\n");
			break;
		case PHY_ICS1890:
			DPRINTK("PHY is ICS1890\n");
			break;
		case PHY_DP83840:
			DPRINTK("PHY is DP83840\n");
			break;
		}
#endif
		if(p2!=0xffff&&p2!=0x0000){
			DPRINTK("PHY code: %x\n",(p2<<12)|(p3>>4));
			break;
		}
	}
	spin_unlock_irqrestore(&priv->meth_lock, flags);
	if(priv->phy_addr<32) {
		return 0;
	}
	DPRINTK("Oopsie! PHY is not known!\n");
	priv->phy_addr=-1;
	return -ENODEV;
}

static void meth_check_link(struct net_device *dev)
{
	struct meth_private *priv = netdev_priv(dev);
	unsigned long mii_advertising = mdio_read(priv, 4);
	unsigned long mii_partner = mdio_read(priv, 5);
	unsigned long negotiated = mii_advertising & mii_partner;
	unsigned long duplex, speed;

	if (mii_partner == 0xffff)
		return;

	speed = (negotiated & 0x0380) ? METH_100MBIT : 0;
	duplex = ((negotiated & 0x0100) || (negotiated & 0x01C0) == 0x0040) ?
		 METH_PHY_FDX : 0;

	if ((priv->mac_ctrl & METH_PHY_FDX) ^ duplex) {
		DPRINTK("Setting %s-duplex\n", duplex ? "full" : "half");
		if (duplex)
			priv->mac_ctrl |= METH_PHY_FDX;
		else
			priv->mac_ctrl &= ~METH_PHY_FDX;
		mace->eth.mac_ctrl = priv->mac_ctrl;
	}

	if ((priv->mac_ctrl & METH_100MBIT) ^ speed) {
		DPRINTK("Setting %dMbs mode\n", speed ? 100 : 10);
		if (duplex)
			priv->mac_ctrl |= METH_100MBIT;
		else
			priv->mac_ctrl &= ~METH_100MBIT;
		mace->eth.mac_ctrl = priv->mac_ctrl;
	}
}


static int meth_init_tx_ring(struct meth_private *priv)
{
	/* Init TX ring */
	priv->tx_ring = dma_alloc_coherent(NULL, TX_RING_BUFFER_SIZE,
	                                   &priv->tx_ring_dma, GFP_ATOMIC);
	if (!priv->tx_ring)
		return -ENOMEM;
	memset(priv->tx_ring, 0, TX_RING_BUFFER_SIZE);
	priv->tx_count = priv->tx_read = priv->tx_write = 0;
	mace->eth.tx_ring_base = priv->tx_ring_dma;
	/* Now init skb save area */
	memset(priv->tx_skbs, 0, sizeof(priv->tx_skbs));
	memset(priv->tx_skb_dmas, 0, sizeof(priv->tx_skb_dmas));
	return 0;
}

static int meth_init_rx_ring(struct meth_private *priv)
{
	int i;

	for (i = 0; i < RX_RING_ENTRIES; i++) {
		priv->rx_skbs[i] = alloc_skb(METH_RX_BUFF_SIZE, 0);
		/* 8byte status vector + 3quad padding + 2byte padding,
		 * to put data on 64bit aligned boundary */
		skb_reserve(priv->rx_skbs[i],METH_RX_HEAD);
		priv->rx_ring[i]=(rx_packet*)(priv->rx_skbs[i]->head);
		/* I'll need to re-sync it after each RX */
		priv->rx_ring_dmas[i] =
			dma_map_single(NULL, priv->rx_ring[i],
				       METH_RX_BUFF_SIZE, DMA_FROM_DEVICE);
		mace->eth.rx_fifo = priv->rx_ring_dmas[i];
	}
        priv->rx_write = 0;
	return 0;
}
static void meth_free_tx_ring(struct meth_private *priv)
{
	int i;

	/* Remove any pending skb */
	for (i = 0; i < TX_RING_ENTRIES; i++) {
		if (priv->tx_skbs[i])
			dev_kfree_skb(priv->tx_skbs[i]);
		priv->tx_skbs[i] = NULL;
	}
	dma_free_coherent(NULL, TX_RING_BUFFER_SIZE, priv->tx_ring,
	                  priv->tx_ring_dma);
}

/* Presumes RX DMA engine is stopped, and RX fifo ring is reset */
static void meth_free_rx_ring(struct meth_private *priv)
{
	int i;

	for (i = 0; i < RX_RING_ENTRIES; i++) {
		dma_unmap_single(NULL, priv->rx_ring_dmas[i],
				 METH_RX_BUFF_SIZE, DMA_FROM_DEVICE);
		priv->rx_ring[i] = 0;
		priv->rx_ring_dmas[i] = 0;
		kfree_skb(priv->rx_skbs[i]);
	}
}

int meth_reset(struct net_device *dev)
{
	struct meth_private *priv = netdev_priv(dev);

	/* Reset card */
	mace->eth.mac_ctrl = SGI_MAC_RESET;
	udelay(1);
	mace->eth.mac_ctrl = 0;
	udelay(25);

	/* Load ethernet address */
	load_eaddr(dev);
	/* Should load some "errata", but later */

	/* Check for device */
	if (mdio_probe(priv) < 0) {
		DPRINTK("Unable to find PHY\n");
		return -ENODEV;
	}

	/* Initial mode: 10 | Half-duplex | Accept normal packets */
	priv->mac_ctrl = METH_ACCEPT_MCAST | METH_DEFAULT_IPG;
	if (dev->flags & IFF_PROMISC)
		priv->mac_ctrl |= METH_PROMISC;
	mace->eth.mac_ctrl = priv->mac_ctrl;

	/* Autonegotiate speed and duplex mode */
	meth_check_link(dev);

	/* Now set dma control, but don't enable DMA, yet */
	priv->dma_ctrl = (4 << METH_RX_OFFSET_SHIFT) |
			 (RX_RING_ENTRIES << METH_RX_DEPTH_SHIFT);
	mace->eth.dma_ctrl = priv->dma_ctrl;

	return 0;
}

/*============End Helper Routines=====================*/

/*
 * Open and close
 */
static int meth_open(struct net_device *dev)
{
	struct meth_private *priv = netdev_priv(dev);
	int ret;

	priv->phy_addr = -1;    /* No PHY is known yet... */

	/* Initialize the hardware */
	ret = meth_reset(dev);
	if (ret < 0)
		return ret;

	/* Allocate the ring buffers */
	ret = meth_init_tx_ring(priv);
	if (ret < 0)
		return ret;
	ret = meth_init_rx_ring(priv);
	if (ret < 0)
		goto out_free_tx_ring;

	ret = request_irq(dev->irq, meth_interrupt, 0, meth_str, dev);
	if (ret) {
		printk(KERN_ERR "%s: Can't get irq %d\n", dev->name, dev->irq);
		goto out_free_rx_ring;
	}

	/* Start DMA */
	priv->dma_ctrl |= METH_DMA_TX_EN | /*METH_DMA_TX_INT_EN |*/
			  METH_DMA_RX_EN | METH_DMA_RX_INT_EN;
	mace->eth.dma_ctrl = priv->dma_ctrl;

	DPRINTK("About to start queue\n");
	netif_start_queue(dev);

	return 0;

out_free_rx_ring:
	meth_free_rx_ring(priv);
out_free_tx_ring:
	meth_free_tx_ring(priv);

	return ret;
}

static int meth_release(struct net_device *dev)
{
	struct meth_private *priv = netdev_priv(dev);

	DPRINTK("Stopping queue\n");
	netif_stop_queue(dev); /* can't transmit any more */
	/* shut down DMA */
	priv->dma_ctrl &= ~(METH_DMA_TX_EN | METH_DMA_TX_INT_EN |
			    METH_DMA_RX_EN | METH_DMA_RX_INT_EN);
	mace->eth.dma_ctrl = priv->dma_ctrl;
	free_irq(dev->irq, dev);
	meth_free_tx_ring(priv);
	meth_free_rx_ring(priv);

	return 0;
}

/*
 * Receive a packet: retrieve, encapsulate and pass over to upper levels
 */
static void meth_rx(struct net_device* dev, unsigned long int_status)
{
	struct sk_buff *skb;
	unsigned long status, flags;
	struct meth_private *priv = netdev_priv(dev);
	unsigned long fifo_rptr = (int_status & METH_INT_RX_RPTR_MASK) >> 8;

	spin_lock_irqsave(&priv->meth_lock, flags);
	priv->dma_ctrl &= ~METH_DMA_RX_INT_EN;
	mace->eth.dma_ctrl = priv->dma_ctrl;
	spin_unlock_irqrestore(&priv->meth_lock, flags);

	if (int_status & METH_INT_RX_UNDERFLOW) {
		fifo_rptr = (fifo_rptr - 1) & 0x0f;
	}
	while (priv->rx_write != fifo_rptr) {
		dma_unmap_single(NULL, priv->rx_ring_dmas[priv->rx_write],
				 METH_RX_BUFF_SIZE, DMA_FROM_DEVICE);
		status = priv->rx_ring[priv->rx_write]->status.raw;
#if MFE_DEBUG
		if (!(status & METH_RX_ST_VALID)) {
			DPRINTK("Not received? status=%016lx\n",status);
		}
#endif
		if ((!(status & METH_RX_STATUS_ERRORS)) && (status & METH_RX_ST_VALID)) {
			int len = (status & 0xffff) - 4; /* omit CRC */
			/* length sanity check */
			if (len < 60 || len > 1518) {
				printk(KERN_DEBUG "%s: bogus packet size: %ld, status=%#2Lx.\n",
				       dev->name, priv->rx_write,
				       priv->rx_ring[priv->rx_write]->status.raw);
				dev->stats.rx_errors++;
				dev->stats.rx_length_errors++;
				skb = priv->rx_skbs[priv->rx_write];
			} else {
				skb = alloc_skb(METH_RX_BUFF_SIZE, GFP_ATOMIC);
				if (!skb) {
					/* Ouch! No memory! Drop packet on the floor */
					DPRINTK("No mem: dropping packet\n");
					dev->stats.rx_dropped++;
					skb = priv->rx_skbs[priv->rx_write];
				} else {
					struct sk_buff *skb_c = priv->rx_skbs[priv->rx_write];
					/* 8byte status vector + 3quad padding + 2byte padding,
					 * to put data on 64bit aligned boundary */
					skb_reserve(skb, METH_RX_HEAD);
					/* Write metadata, and then pass to the receive level */
					skb_put(skb_c, len);
					priv->rx_skbs[priv->rx_write] = skb;
					skb_c->protocol = eth_type_trans(skb_c, dev);
					dev->stats.rx_packets++;
					dev->stats.rx_bytes += len;
					netif_rx(skb_c);
				}
			}
		} else {
			dev->stats.rx_errors++;
			skb=priv->rx_skbs[priv->rx_write];
#if MFE_DEBUG>0
			printk(KERN_WARNING "meth: RX error: status=0x%016lx\n",status);
			if(status&METH_RX_ST_RCV_CODE_VIOLATION)
				printk(KERN_WARNING "Receive Code Violation\n");
			if(status&METH_RX_ST_CRC_ERR)
				printk(KERN_WARNING "CRC error\n");
			if(status&METH_RX_ST_INV_PREAMBLE_CTX)
				printk(KERN_WARNING "Invalid Preamble Context\n");
			if(status&METH_RX_ST_LONG_EVT_SEEN)
				printk(KERN_WARNING "Long Event Seen...\n");
			if(status&METH_RX_ST_BAD_PACKET)
				printk(KERN_WARNING "Bad Packet\n");
			if(status&METH_RX_ST_CARRIER_EVT_SEEN)
				printk(KERN_WARNING "Carrier Event Seen\n");
#endif
		}
		priv->rx_ring[priv->rx_write] = (rx_packet*)skb->head;
		priv->rx_ring[priv->rx_write]->status.raw = 0;
		priv->rx_ring_dmas[priv->rx_write] =
			dma_map_single(NULL, priv->rx_ring[priv->rx_write],
				       METH_RX_BUFF_SIZE, DMA_FROM_DEVICE);
		mace->eth.rx_fifo = priv->rx_ring_dmas[priv->rx_write];
		ADVANCE_RX_PTR(priv->rx_write);
	}
	spin_lock_irqsave(&priv->meth_lock, flags);
	/* In case there was underflow, and Rx DMA was disabled */
	priv->dma_ctrl |= METH_DMA_RX_INT_EN | METH_DMA_RX_EN;
	mace->eth.dma_ctrl = priv->dma_ctrl;
	mace->eth.int_stat = METH_INT_RX_THRESHOLD;
	spin_unlock_irqrestore(&priv->meth_lock, flags);
}

static int meth_tx_full(struct net_device *dev)
{
	struct meth_private *priv = netdev_priv(dev);

	return (priv->tx_count >= TX_RING_ENTRIES - 1);
}

static void meth_tx_cleanup(struct net_device* dev, unsigned long int_status)
{
	struct meth_private *priv = netdev_priv(dev);
	unsigned long status, flags;
	struct sk_buff *skb;
	unsigned long rptr = (int_status&TX_INFO_RPTR) >> 16;

	spin_lock_irqsave(&priv->meth_lock, flags);

	/* Stop DMA notification */
	priv->dma_ctrl &= ~(METH_DMA_TX_INT_EN);
	mace->eth.dma_ctrl = priv->dma_ctrl;

	while (priv->tx_read != rptr) {
		skb = priv->tx_skbs[priv->tx_read];
		status = priv->tx_ring[priv->tx_read].header.raw;
#if MFE_DEBUG>=1
		if (priv->tx_read == priv->tx_write)
			DPRINTK("Auchi! tx_read=%d,tx_write=%d,rptr=%d?\n", priv->tx_read, priv->tx_write,rptr);
#endif
		if (status & METH_TX_ST_DONE) {
			if (status & METH_TX_ST_SUCCESS){
				dev->stats.tx_packets++;
				dev->stats.tx_bytes += skb->len;
			} else {
				dev->stats.tx_errors++;
#if MFE_DEBUG>=1
				DPRINTK("TX error: status=%016lx <",status);
				if(status & METH_TX_ST_SUCCESS)
					printk(" SUCCESS");
				if(status & METH_TX_ST_TOOLONG)
					printk(" TOOLONG");
				if(status & METH_TX_ST_UNDERRUN)
					printk(" UNDERRUN");
				if(status & METH_TX_ST_EXCCOLL)
					printk(" EXCCOLL");
				if(status & METH_TX_ST_DEFER)
					printk(" DEFER");
				if(status & METH_TX_ST_LATECOLL)
					printk(" LATECOLL");
				printk(" >\n");
#endif
			}
		} else {
			DPRINTK("RPTR points us here, but packet not done?\n");
			break;
		}
		dev_kfree_skb_irq(skb);
		priv->tx_skbs[priv->tx_read] = NULL;
		priv->tx_ring[priv->tx_read].header.raw = 0;
		priv->tx_read = (priv->tx_read+1)&(TX_RING_ENTRIES-1);
		priv->tx_count--;
	}

	/* wake up queue if it was stopped */
	if (netif_queue_stopped(dev) && !meth_tx_full(dev)) {
		netif_wake_queue(dev);
	}

	mace->eth.int_stat = METH_INT_TX_EMPTY | METH_INT_TX_PKT;
	spin_unlock_irqrestore(&priv->meth_lock, flags);
}

static void meth_error(struct net_device* dev, unsigned status)
{
	struct meth_private *priv = netdev_priv(dev);
	unsigned long flags;

	printk(KERN_WARNING "meth: error status: 0x%08x\n",status);
	/* check for errors too... */
	if (status & (METH_INT_TX_LINK_FAIL))
		printk(KERN_WARNING "meth: link failure\n");
	/* Should I do full reset in this case? */
	if (status & (METH_INT_MEM_ERROR))
		printk(KERN_WARNING "meth: memory error\n");
	if (status & (METH_INT_TX_ABORT))
		printk(KERN_WARNING "meth: aborted\n");
	if (status & (METH_INT_RX_OVERFLOW))
		printk(KERN_WARNING "meth: Rx overflow\n");
	if (status & (METH_INT_RX_UNDERFLOW)) {
		printk(KERN_WARNING "meth: Rx underflow\n");
		spin_lock_irqsave(&priv->meth_lock, flags);
		mace->eth.int_stat = METH_INT_RX_UNDERFLOW;
		/* more underflow interrupts will be delivered,
		 * effectively throwing us into an infinite loop.
		 *  Thus I stop processing Rx in this case. */
		priv->dma_ctrl &= ~METH_DMA_RX_EN;
		mace->eth.dma_ctrl = priv->dma_ctrl;
		DPRINTK("Disabled meth Rx DMA temporarily\n");
		spin_unlock_irqrestore(&priv->meth_lock, flags);
	}
	mace->eth.int_stat = METH_INT_ERROR;
}

/*
 * The typical interrupt entry point
 */
static irqreturn_t meth_interrupt(int irq, void *dev_id)
{
	struct net_device *dev = (struct net_device *)dev_id;
	struct meth_private *priv = netdev_priv(dev);
	unsigned long status;

	status = mace->eth.int_stat;
	while (status & 0xff) {
		/* First handle errors - if we get Rx underflow,
		 * Rx DMA will be disabled, and Rx handler will reenable
		 * it. I don't think it's possible to get Rx underflow,
		 * without getting Rx interrupt */
		if (status & METH_INT_ERROR) {
			meth_error(dev, status);
		}
		if (status & (METH_INT_TX_EMPTY | METH_INT_TX_PKT)) {
			/* a transmission is over: free the skb */
			meth_tx_cleanup(dev, status);
		}
		if (status & METH_INT_RX_THRESHOLD) {
			if (!(priv->dma_ctrl & METH_DMA_RX_INT_EN))
				break;
			/* send it to meth_rx for handling */
			meth_rx(dev, status);
		}
		status = mace->eth.int_stat;
	}

	return IRQ_HANDLED;
}

/*
 * Transmits packets that fit into TX descriptor (are <=120B)
 */
static void meth_tx_short_prepare(struct meth_private *priv,
				  struct sk_buff *skb)
{
	tx_packet *desc = &priv->tx_ring[priv->tx_write];
	int len = (skb->len < ETH_ZLEN) ? ETH_ZLEN : skb->len;

	desc->header.raw = METH_TX_CMD_INT_EN | (len-1) | ((128-len) << 16);
	/* maybe I should set whole thing to 0 first... */
	skb_copy_from_linear_data(skb, desc->data.dt + (120 - len), skb->len);
	if (skb->len < len)
		memset(desc->data.dt + 120 - len + skb->len, 0, len-skb->len);
}
#define TX_CATBUF1 BIT(25)
static void meth_tx_1page_prepare(struct meth_private *priv,
				  struct sk_buff *skb)
{
	tx_packet *desc = &priv->tx_ring[priv->tx_write];
	void *buffer_data = (void *)(((unsigned long)skb->data + 7) & ~7);
	int unaligned_len = (int)((unsigned long)buffer_data - (unsigned long)skb->data);
	int buffer_len = skb->len - unaligned_len;
	dma_addr_t catbuf;

	desc->header.raw = METH_TX_CMD_INT_EN | TX_CATBUF1 | (skb->len - 1);

	/* unaligned part */
	if (unaligned_len) {
		skb_copy_from_linear_data(skb, desc->data.dt + (120 - unaligned_len),
			      unaligned_len);
		desc->header.raw |= (128 - unaligned_len) << 16;
	}

	/* first page */
	catbuf = dma_map_single(NULL, buffer_data, buffer_len,
				DMA_TO_DEVICE);
	desc->data.cat_buf[0].form.start_addr = catbuf >> 3;
	desc->data.cat_buf[0].form.len = buffer_len - 1;
}
#define TX_CATBUF2 BIT(26)
static void meth_tx_2page_prepare(struct meth_private *priv,
				  struct sk_buff *skb)
{
	tx_packet *desc = &priv->tx_ring[priv->tx_write];
	void *buffer1_data = (void *)(((unsigned long)skb->data + 7) & ~7);
	void *buffer2_data = (void *)PAGE_ALIGN((unsigned long)skb->data);
	int unaligned_len = (int)((unsigned long)buffer1_data - (unsigned long)skb->data);
	int buffer1_len = (int)((unsigned long)buffer2_data - (unsigned long)buffer1_data);
	int buffer2_len = skb->len - buffer1_len - unaligned_len;
	dma_addr_t catbuf1, catbuf2;

	desc->header.raw = METH_TX_CMD_INT_EN | TX_CATBUF1 | TX_CATBUF2| (skb->len - 1);
	/* unaligned part */
	if (unaligned_len){
		skb_copy_from_linear_data(skb, desc->data.dt + (120 - unaligned_len),
			      unaligned_len);
		desc->header.raw |= (128 - unaligned_len) << 16;
	}

	/* first page */
	catbuf1 = dma_map_single(NULL, buffer1_data, buffer1_len,
				 DMA_TO_DEVICE);
	desc->data.cat_buf[0].form.start_addr = catbuf1 >> 3;
	desc->data.cat_buf[0].form.len = buffer1_len - 1;
	/* second page */
	catbuf2 = dma_map_single(NULL, buffer2_data, buffer2_len,
				 DMA_TO_DEVICE);
	desc->data.cat_buf[1].form.start_addr = catbuf2 >> 3;
	desc->data.cat_buf[1].form.len = buffer2_len - 1;
}

static void meth_add_to_tx_ring(struct meth_private *priv, struct sk_buff *skb)
{
	/* Remember the skb, so we can free it at interrupt time */
	priv->tx_skbs[priv->tx_write] = skb;
	if (skb->len <= 120) {
		/* Whole packet fits into descriptor */
		meth_tx_short_prepare(priv, skb);
	} else if (PAGE_ALIGN((unsigned long)skb->data) !=
		   PAGE_ALIGN((unsigned long)skb->data + skb->len - 1)) {
		/* Packet crosses page boundary */
		meth_tx_2page_prepare(priv, skb);
	} else {
		/* Packet is in one page */
		meth_tx_1page_prepare(priv, skb);
	}
	priv->tx_write = (priv->tx_write + 1) & (TX_RING_ENTRIES - 1);
	mace->eth.tx_info = priv->tx_write;
	priv->tx_count++;
}

/*
 * Transmit a packet (called by the kernel)
 */
static int meth_tx(struct sk_buff *skb, struct net_device *dev)
{
	struct meth_private *priv = netdev_priv(dev);
	unsigned long flags;

	spin_lock_irqsave(&priv->meth_lock, flags);
	/* Stop DMA notification */
	priv->dma_ctrl &= ~(METH_DMA_TX_INT_EN);
	mace->eth.dma_ctrl = priv->dma_ctrl;

	meth_add_to_tx_ring(priv, skb);
	dev->trans_start = jiffies; /* save the timestamp */

	/* If TX ring is full, tell the upper layer to stop sending packets */
	if (meth_tx_full(dev)) {
	        printk(KERN_DEBUG "TX full: stopping\n");
		netif_stop_queue(dev);
	}

	/* Restart DMA notification */
	priv->dma_ctrl |= METH_DMA_TX_INT_EN;
	mace->eth.dma_ctrl = priv->dma_ctrl;

	spin_unlock_irqrestore(&priv->meth_lock, flags);

	return 0;
}

/*
 * Deal with a transmit timeout.
 */
static void meth_tx_timeout(struct net_device *dev)
{
	struct meth_private *priv = netdev_priv(dev);
	unsigned long flags;

	printk(KERN_WARNING "%s: transmit timed out\n", dev->name);

	/* Protect against concurrent rx interrupts */
	spin_lock_irqsave(&priv->meth_lock,flags);

	/* Try to reset the interface. */
	meth_reset(dev);

	dev->stats.tx_errors++;

	/* Clear all rings */
	meth_free_tx_ring(priv);
	meth_free_rx_ring(priv);
	meth_init_tx_ring(priv);
	meth_init_rx_ring(priv);

	/* Restart dma */
	priv->dma_ctrl |= METH_DMA_TX_EN | METH_DMA_RX_EN | METH_DMA_RX_INT_EN;
	mace->eth.dma_ctrl = priv->dma_ctrl;

	/* Enable interrupt */
	spin_unlock_irqrestore(&priv->meth_lock, flags);

	dev->trans_start = jiffies;
	netif_wake_queue(dev);

	return;
}

/*
 * Ioctl commands
 */
static int meth_ioctl(struct net_device *dev, struct ifreq *rq, int cmd)
{
	/* XXX Not yet implemented */
	switch(cmd) {
	case SIOCGMIIPHY:
	case SIOCGMIIREG:
	case SIOCSMIIREG:
	default:
		return -EOPNOTSUPP;
	}
}

static const struct net_device_ops meth_netdev_ops = {
	.ndo_open		= meth_open,
	.ndo_stop		= meth_release,
	.ndo_start_xmit		= meth_tx,
	.ndo_do_ioctl		= meth_ioctl,
	.ndo_tx_timeout		= meth_tx_timeout,
	.ndo_change_mtu		= eth_change_mtu,
	.ndo_validate_addr	= eth_validate_addr,
	.ndo_set_mac_address	= eth_mac_addr,
};

/*
 * The init function.
 */
static int __init meth_probe(struct platform_device *pdev)
{
	struct net_device *dev;
	struct meth_private *priv;
	int err;

	dev = alloc_etherdev(sizeof(struct meth_private));
	if (!dev)
		return -ENOMEM;

	dev->netdev_ops		= &meth_netdev_ops;
	dev->watchdog_timeo	= timeout;
	dev->irq		= MACE_ETHERNET_IRQ;
	dev->base_addr		= (unsigned long)&mace->eth;
	memcpy(dev->dev_addr, o2meth_eaddr, 6);

	priv = netdev_priv(dev);
	spin_lock_init(&priv->meth_lock);
	SET_NETDEV_DEV(dev, &pdev->dev);

	err = register_netdev(dev);
	if (err) {
		free_netdev(dev);
		return err;
	}

	printk(KERN_INFO "%s: SGI MACE Ethernet rev. %d\n",
	       dev->name, (unsigned int)(mace->eth.mac_ctrl >> 29));
	return 0;
}

static int __exit meth_remove(struct platform_device *pdev)
{
	struct net_device *dev = platform_get_drvdata(pdev);

	unregister_netdev(dev);
	free_netdev(dev);
	platform_set_drvdata(pdev, NULL);

	return 0;
}

static struct platform_driver meth_driver = {
	.probe	= meth_probe,
	.remove	= __devexit_p(meth_remove),
	.driver = {
		.name	= "meth",
		.owner	= THIS_MODULE,
	}
};

static int __init meth_init_module(void)
{
	int err;

	err = platform_driver_register(&meth_driver);
	if (err)
		printk(KERN_ERR "Driver registration failed\n");

	return err;
}

static void __exit meth_exit_module(void)
{
	platform_driver_unregister(&meth_driver);
}

module_init(meth_init_module);
module_exit(meth_exit_module);

MODULE_AUTHOR("Ilya Volynets <ilya@theIlya.com>");
MODULE_DESCRIPTION("SGI O2 Builtin Fast Ethernet driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:meth");
