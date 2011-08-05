/*
 *	Driver for the Macintosh 68K onboard MACE controller with PSC
 *	driven DMA. The MACE driver code is derived from mace.c. The
 *	Mac68k theory of operation is courtesy of the MacBSD wizards.
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License
 *	as published by the Free Software Foundation; either version
 *	2 of the License, or (at your option) any later version.
 *
 *	Copyright (C) 1996 Paul Mackerras.
 *	Copyright (C) 1998 Alan Cox <alan@lxorguk.ukuu.org.uk>
 *
 *	Modified heavily by Joshua M. Thompson based on Dave Huang's NetBSD driver
 *
 *	Copyright (C) 2007 Finn Thain
 *
 *	Converted to DMA API, converted to unified driver model,
 *	sync'd some routines with mace.c and fixed various bugs.
 */


#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/delay.h>
#include <linux/string.h>
#include <linux/crc32.h>
#include <linux/bitrev.h>
#include <linux/dma-mapping.h>
#include <linux/platform_device.h>
#include <linux/gfp.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/macintosh.h>
#include <asm/macints.h>
#include <asm/mac_psc.h>
#include <asm/page.h>
#include "mace.h"

static char mac_mace_string[] = "macmace";

#define N_TX_BUFF_ORDER	0
#define N_TX_RING	(1 << N_TX_BUFF_ORDER)
#define N_RX_BUFF_ORDER	3
#define N_RX_RING	(1 << N_RX_BUFF_ORDER)

#define TX_TIMEOUT	HZ

#define MACE_BUFF_SIZE	0x800

/* Chip rev needs workaround on HW & multicast addr change */
#define BROKEN_ADDRCHG_REV	0x0941

/* The MACE is simply wired down on a Mac68K box */

#define MACE_BASE	(void *)(0x50F1C000)
#define MACE_PROM	(void *)(0x50F08001)

struct mace_data {
	volatile struct mace *mace;
	unsigned char *tx_ring;
	dma_addr_t tx_ring_phys;
	unsigned char *rx_ring;
	dma_addr_t rx_ring_phys;
	int dma_intr;
	int rx_slot, rx_tail;
	int tx_slot, tx_sloti, tx_count;
	int chipid;
	struct device *device;
};

struct mace_frame {
	u8	rcvcnt;
	u8	pad1;
	u8	rcvsts;
	u8	pad2;
	u8	rntpc;
	u8	pad3;
	u8	rcvcc;
	u8	pad4;
	u32	pad5;
	u32	pad6;
	u8	data[1];
	/* And frame continues.. */
};

#define PRIV_BYTES	sizeof(struct mace_data)

static int mace_open(struct net_device *dev);
static int mace_close(struct net_device *dev);
static int mace_xmit_start(struct sk_buff *skb, struct net_device *dev);
static void mace_set_multicast(struct net_device *dev);
static int mace_set_address(struct net_device *dev, void *addr);
static void mace_reset(struct net_device *dev);
static irqreturn_t mace_interrupt(int irq, void *dev_id);
static irqreturn_t mace_dma_intr(int irq, void *dev_id);
static void mace_tx_timeout(struct net_device *dev);
static void __mace_set_address(struct net_device *dev, void *addr);

/*
 * Load a receive DMA channel with a base address and ring length
 */

static void mace_load_rxdma_base(struct net_device *dev, int set)
{
	struct mace_data *mp = netdev_priv(dev);

	psc_write_word(PSC_ENETRD_CMD + set, 0x0100);
	psc_write_long(PSC_ENETRD_ADDR + set, (u32) mp->rx_ring_phys);
	psc_write_long(PSC_ENETRD_LEN + set, N_RX_RING);
	psc_write_word(PSC_ENETRD_CMD + set, 0x9800);
	mp->rx_tail = 0;
}

/*
 * Reset the receive DMA subsystem
 */

static void mace_rxdma_reset(struct net_device *dev)
{
	struct mace_data *mp = netdev_priv(dev);
	volatile struct mace *mace = mp->mace;
	u8 maccc = mace->maccc;

	mace->maccc = maccc & ~ENRCV;

	psc_write_word(PSC_ENETRD_CTL, 0x8800);
	mace_load_rxdma_base(dev, 0x00);
	psc_write_word(PSC_ENETRD_CTL, 0x0400);

	psc_write_word(PSC_ENETRD_CTL, 0x8800);
	mace_load_rxdma_base(dev, 0x10);
	psc_write_word(PSC_ENETRD_CTL, 0x0400);

	mace->maccc = maccc;
	mp->rx_slot = 0;

	psc_write_word(PSC_ENETRD_CMD + PSC_SET0, 0x9800);
	psc_write_word(PSC_ENETRD_CMD + PSC_SET1, 0x9800);
}

/*
 * Reset the transmit DMA subsystem
 */

static void mace_txdma_reset(struct net_device *dev)
{
	struct mace_data *mp = netdev_priv(dev);
	volatile struct mace *mace = mp->mace;
	u8 maccc;

	psc_write_word(PSC_ENETWR_CTL, 0x8800);

	maccc = mace->maccc;
	mace->maccc = maccc & ~ENXMT;

	mp->tx_slot = mp->tx_sloti = 0;
	mp->tx_count = N_TX_RING;

	psc_write_word(PSC_ENETWR_CTL, 0x0400);
	mace->maccc = maccc;
}

/*
 * Disable DMA
 */

static void mace_dma_off(struct net_device *dev)
{
	psc_write_word(PSC_ENETRD_CTL, 0x8800);
	psc_write_word(PSC_ENETRD_CTL, 0x1000);
	psc_write_word(PSC_ENETRD_CMD + PSC_SET0, 0x1100);
	psc_write_word(PSC_ENETRD_CMD + PSC_SET1, 0x1100);

	psc_write_word(PSC_ENETWR_CTL, 0x8800);
	psc_write_word(PSC_ENETWR_CTL, 0x1000);
	psc_write_word(PSC_ENETWR_CMD + PSC_SET0, 0x1100);
	psc_write_word(PSC_ENETWR_CMD + PSC_SET1, 0x1100);
}

static const struct net_device_ops mace_netdev_ops = {
	.ndo_open		= mace_open,
	.ndo_stop		= mace_close,
	.ndo_start_xmit		= mace_xmit_start,
	.ndo_tx_timeout		= mace_tx_timeout,
	.ndo_set_multicast_list	= mace_set_multicast,
	.ndo_set_mac_address	= mace_set_address,
	.ndo_change_mtu		= eth_change_mtu,
	.ndo_validate_addr	= eth_validate_addr,
};

/*
 * Not really much of a probe. The hardware table tells us if this
 * model of Macintrash has a MACE (AV macintoshes)
 */

static int __devinit mace_probe(struct platform_device *pdev)
{
	int j;
	struct mace_data *mp;
	unsigned char *addr;
	struct net_device *dev;
	unsigned char checksum = 0;
	static int found = 0;
	int err;

	if (found || macintosh_config->ether_type != MAC_ETHER_MACE)
		return -ENODEV;

	found = 1;	/* prevent 'finding' one on every device probe */

	dev = alloc_etherdev(PRIV_BYTES);
	if (!dev)
		return -ENOMEM;

	mp = netdev_priv(dev);

	mp->device = &pdev->dev;
	SET_NETDEV_DEV(dev, &pdev->dev);

	dev->base_addr = (u32)MACE_BASE;
	mp->mace = MACE_BASE;

	dev->irq = IRQ_MAC_MACE;
	mp->dma_intr = IRQ_MAC_MACE_DMA;

	mp->chipid = mp->mace->chipid_hi << 8 | mp->mace->chipid_lo;

	/*
	 * The PROM contains 8 bytes which total 0xFF when XOR'd
	 * together. Due to the usual peculiar apple brain damage
	 * the bytes are spaced out in a strange boundary and the
	 * bits are reversed.
	 */

	addr = (void *)MACE_PROM;

	for (j = 0; j < 6; ++j) {
		u8 v = bitrev8(addr[j<<4]);
		checksum ^= v;
		dev->dev_addr[j] = v;
	}
	for (; j < 8; ++j) {
		checksum ^= bitrev8(addr[j<<4]);
	}

	if (checksum != 0xFF) {
		free_netdev(dev);
		return -ENODEV;
	}

	dev->netdev_ops		= &mace_netdev_ops;
	dev->watchdog_timeo	= TX_TIMEOUT;

	printk(KERN_INFO "%s: 68K MACE, hardware address %pM\n",
	       dev->name, dev->dev_addr);

	err = register_netdev(dev);
	if (!err)
		return 0;

	free_netdev(dev);
	return err;
}

/*
 * Reset the chip.
 */

static void mace_reset(struct net_device *dev)
{
	struct mace_data *mp = netdev_priv(dev);
	volatile struct mace *mb = mp->mace;
	int i;

	/* soft-reset the chip */
	i = 200;
	while (--i) {
		mb->biucc = SWRST;
		if (mb->biucc & SWRST) {
			udelay(10);
			continue;
		}
		break;
	}
	if (!i) {
		printk(KERN_ERR "macmace: cannot reset chip!\n");
		return;
	}

	mb->maccc = 0;	/* turn off tx, rx */
	mb->imr = 0xFF;	/* disable all intrs for now */
	i = mb->ir;

	mb->biucc = XMTSP_64;
	mb->utr = RTRD;
	mb->fifocc = XMTFW_8 | RCVFW_64 | XMTFWU | RCVFWU;

	mb->xmtfc = AUTO_PAD_XMIT; /* auto-pad short frames */
	mb->rcvfc = 0;

	/* load up the hardware address */
	__mace_set_address(dev, dev->dev_addr);

	/* clear the multicast filter */
	if (mp->chipid == BROKEN_ADDRCHG_REV)
		mb->iac = LOGADDR;
	else {
		mb->iac = ADDRCHG | LOGADDR;
		while ((mb->iac & ADDRCHG) != 0)
			;
	}
	for (i = 0; i < 8; ++i)
		mb->ladrf = 0;

	/* done changing address */
	if (mp->chipid != BROKEN_ADDRCHG_REV)
		mb->iac = 0;

	mb->plscc = PORTSEL_AUI;
}

/*
 * Load the address on a mace controller.
 */

static void __mace_set_address(struct net_device *dev, void *addr)
{
	struct mace_data *mp = netdev_priv(dev);
	volatile struct mace *mb = mp->mace;
	unsigned char *p = addr;
	int i;

	/* load up the hardware address */
	if (mp->chipid == BROKEN_ADDRCHG_REV)
		mb->iac = PHYADDR;
	else {
		mb->iac = ADDRCHG | PHYADDR;
		while ((mb->iac & ADDRCHG) != 0)
			;
	}
	for (i = 0; i < 6; ++i)
		mb->padr = dev->dev_addr[i] = p[i];
	if (mp->chipid != BROKEN_ADDRCHG_REV)
		mb->iac = 0;
}

static int mace_set_address(struct net_device *dev, void *addr)
{
	struct mace_data *mp = netdev_priv(dev);
	volatile struct mace *mb = mp->mace;
	unsigned long flags;
	u8 maccc;

	local_irq_save(flags);

	maccc = mb->maccc;

	__mace_set_address(dev, addr);

	mb->maccc = maccc;

	local_irq_restore(flags);

	return 0;
}

/*
 * Open the Macintosh MACE. Most of this is playing with the DMA
 * engine. The ethernet chip is quite friendly.
 */

static int mace_open(struct net_device *dev)
{
	struct mace_data *mp = netdev_priv(dev);
	volatile struct mace *mb = mp->mace;

	/* reset the chip */
	mace_reset(dev);

	if (request_irq(dev->irq, mace_interrupt, 0, dev->name, dev)) {
		printk(KERN_ERR "%s: can't get irq %d\n", dev->name, dev->irq);
		return -EAGAIN;
	}
	if (request_irq(mp->dma_intr, mace_dma_intr, 0, dev->name, dev)) {
		printk(KERN_ERR "%s: can't get irq %d\n", dev->name, mp->dma_intr);
		free_irq(dev->irq, dev);
		return -EAGAIN;
	}

	/* Allocate the DMA ring buffers */

	mp->tx_ring = dma_alloc_coherent(mp->device,
			N_TX_RING * MACE_BUFF_SIZE,
			&mp->tx_ring_phys, GFP_KERNEL);
	if (mp->tx_ring == NULL) {
		printk(KERN_ERR "%s: unable to allocate DMA tx buffers\n", dev->name);
		goto out1;
	}

	mp->rx_ring = dma_alloc_coherent(mp->device,
			N_RX_RING * MACE_BUFF_SIZE,
			&mp->rx_ring_phys, GFP_KERNEL);
	if (mp->rx_ring == NULL) {
		printk(KERN_ERR "%s: unable to allocate DMA rx buffers\n", dev->name);
		goto out2;
	}

	mace_dma_off(dev);

	/* Not sure what these do */

	psc_write_word(PSC_ENETWR_CTL, 0x9000);
	psc_write_word(PSC_ENETRD_CTL, 0x9000);
	psc_write_word(PSC_ENETWR_CTL, 0x0400);
	psc_write_word(PSC_ENETRD_CTL, 0x0400);

	mace_rxdma_reset(dev);
	mace_txdma_reset(dev);

	/* turn it on! */
	mb->maccc = ENXMT | ENRCV;
	/* enable all interrupts except receive interrupts */
	mb->imr = RCVINT;
	return 0;

out2:
	dma_free_coherent(mp->device, N_TX_RING * MACE_BUFF_SIZE,
	                  mp->tx_ring, mp->tx_ring_phys);
out1:
	free_irq(dev->irq, dev);
	free_irq(mp->dma_intr, dev);
	return -ENOMEM;
}

/*
 * Shut down the mace and its interrupt channel
 */

static int mace_close(struct net_device *dev)
{
	struct mace_data *mp = netdev_priv(dev);
	volatile struct mace *mb = mp->mace;

	mb->maccc = 0;		/* disable rx and tx	 */
	mb->imr = 0xFF;		/* disable all irqs	 */
	mace_dma_off(dev);	/* disable rx and tx dma */

	return 0;
}

/*
 * Transmit a frame
 */

static int mace_xmit_start(struct sk_buff *skb, struct net_device *dev)
{
	struct mace_data *mp = netdev_priv(dev);
	unsigned long flags;

	/* Stop the queue since there's only the one buffer */

	local_irq_save(flags);
	netif_stop_queue(dev);
	if (!mp->tx_count) {
		printk(KERN_ERR "macmace: tx queue running but no free buffers.\n");
		local_irq_restore(flags);
		return NETDEV_TX_BUSY;
	}
	mp->tx_count--;
	local_irq_restore(flags);

	dev->stats.tx_packets++;
	dev->stats.tx_bytes += skb->len;

	/* We need to copy into our xmit buffer to take care of alignment and caching issues */
	skb_copy_from_linear_data(skb, mp->tx_ring, skb->len);

	/* load the Tx DMA and fire it off */

	psc_write_long(PSC_ENETWR_ADDR + mp->tx_slot, (u32)  mp->tx_ring_phys);
	psc_write_long(PSC_ENETWR_LEN + mp->tx_slot, skb->len);
	psc_write_word(PSC_ENETWR_CMD + mp->tx_slot, 0x9800);

	mp->tx_slot ^= 0x10;

	dev_kfree_skb(skb);

	return NETDEV_TX_OK;
}

static void mace_set_multicast(struct net_device *dev)
{
	struct mace_data *mp = netdev_priv(dev);
	volatile struct mace *mb = mp->mace;
	int i;
	u32 crc;
	u8 maccc;
	unsigned long flags;

	local_irq_save(flags);
	maccc = mb->maccc;
	mb->maccc &= ~PROM;

	if (dev->flags & IFF_PROMISC) {
		mb->maccc |= PROM;
	} else {
		unsigned char multicast_filter[8];
		struct netdev_hw_addr *ha;

		if (dev->flags & IFF_ALLMULTI) {
			for (i = 0; i < 8; i++) {
				multicast_filter[i] = 0xFF;
			}
		} else {
			for (i = 0; i < 8; i++)
				multicast_filter[i] = 0;
			netdev_for_each_mc_addr(ha, dev) {
				crc = ether_crc_le(6, ha->addr);
				/* bit number in multicast_filter */
				i = crc >> 26;
				multicast_filter[i >> 3] |= 1 << (i & 7);
			}
		}

		if (mp->chipid == BROKEN_ADDRCHG_REV)
			mb->iac = LOGADDR;
		else {
			mb->iac = ADDRCHG | LOGADDR;
			while ((mb->iac & ADDRCHG) != 0)
				;
		}
		for (i = 0; i < 8; ++i)
			mb->ladrf = multicast_filter[i];
		if (mp->chipid != BROKEN_ADDRCHG_REV)
			mb->iac = 0;
	}

	mb->maccc = maccc;
	local_irq_restore(flags);
}

static void mace_handle_misc_intrs(struct net_device *dev, int intr)
{
	struct mace_data *mp = netdev_priv(dev);
	volatile struct mace *mb = mp->mace;
	static int mace_babbles, mace_jabbers;

	if (intr & MPCO)
		dev->stats.rx_missed_errors += 256;
	dev->stats.rx_missed_errors += mb->mpc;   /* reading clears it */
	if (intr & RNTPCO)
		dev->stats.rx_length_errors += 256;
	dev->stats.rx_length_errors += mb->rntpc; /* reading clears it */
	if (intr & CERR)
		++dev->stats.tx_heartbeat_errors;
	if (intr & BABBLE)
		if (mace_babbles++ < 4)
			printk(KERN_DEBUG "macmace: babbling transmitter\n");
	if (intr & JABBER)
		if (mace_jabbers++ < 4)
			printk(KERN_DEBUG "macmace: jabbering transceiver\n");
}

static irqreturn_t mace_interrupt(int irq, void *dev_id)
{
	struct net_device *dev = (struct net_device *) dev_id;
	struct mace_data *mp = netdev_priv(dev);
	volatile struct mace *mb = mp->mace;
	int intr, fs;
	unsigned long flags;

	/* don't want the dma interrupt handler to fire */
	local_irq_save(flags);

	intr = mb->ir; /* read interrupt register */
	mace_handle_misc_intrs(dev, intr);

	if (intr & XMTINT) {
		fs = mb->xmtfs;
		if ((fs & XMTSV) == 0) {
			printk(KERN_ERR "macmace: xmtfs not valid! (fs=%x)\n", fs);
			mace_reset(dev);
			/*
			 * XXX mace likes to hang the machine after a xmtfs error.
			 * This is hard to reproduce, reseting *may* help
			 */
		}
		/* dma should have finished */
		if (!mp->tx_count) {
			printk(KERN_DEBUG "macmace: tx ring ran out? (fs=%x)\n", fs);
		}
		/* Update stats */
		if (fs & (UFLO|LCOL|LCAR|RTRY)) {
			++dev->stats.tx_errors;
			if (fs & LCAR)
				++dev->stats.tx_carrier_errors;
			else if (fs & (UFLO|LCOL|RTRY)) {
				++dev->stats.tx_aborted_errors;
				if (mb->xmtfs & UFLO) {
					printk(KERN_ERR "%s: DMA underrun.\n", dev->name);
					dev->stats.tx_fifo_errors++;
					mace_txdma_reset(dev);
				}
			}
		}
	}

	if (mp->tx_count)
		netif_wake_queue(dev);

	local_irq_restore(flags);

	return IRQ_HANDLED;
}

static void mace_tx_timeout(struct net_device *dev)
{
	struct mace_data *mp = netdev_priv(dev);
	volatile struct mace *mb = mp->mace;
	unsigned long flags;

	local_irq_save(flags);

	/* turn off both tx and rx and reset the chip */
	mb->maccc = 0;
	printk(KERN_ERR "macmace: transmit timeout - resetting\n");
	mace_txdma_reset(dev);
	mace_reset(dev);

	/* restart rx dma */
	mace_rxdma_reset(dev);

	mp->tx_count = N_TX_RING;
	netif_wake_queue(dev);

	/* turn it on! */
	mb->maccc = ENXMT | ENRCV;
	/* enable all interrupts except receive interrupts */
	mb->imr = RCVINT;

	local_irq_restore(flags);
}

/*
 * Handle a newly arrived frame
 */

static void mace_dma_rx_frame(struct net_device *dev, struct mace_frame *mf)
{
	struct sk_buff *skb;
	unsigned int frame_status = mf->rcvsts;

	if (frame_status & (RS_OFLO | RS_CLSN | RS_FRAMERR | RS_FCSERR)) {
		dev->stats.rx_errors++;
		if (frame_status & RS_OFLO) {
			printk(KERN_DEBUG "%s: fifo overflow.\n", dev->name);
			dev->stats.rx_fifo_errors++;
		}
		if (frame_status & RS_CLSN)
			dev->stats.collisions++;
		if (frame_status & RS_FRAMERR)
			dev->stats.rx_frame_errors++;
		if (frame_status & RS_FCSERR)
			dev->stats.rx_crc_errors++;
	} else {
		unsigned int frame_length = mf->rcvcnt + ((frame_status & 0x0F) << 8 );

		skb = dev_alloc_skb(frame_length + 2);
		if (!skb) {
			dev->stats.rx_dropped++;
			return;
		}
		skb_reserve(skb, 2);
		memcpy(skb_put(skb, frame_length), mf->data, frame_length);

		skb->protocol = eth_type_trans(skb, dev);
		netif_rx(skb);
		dev->stats.rx_packets++;
		dev->stats.rx_bytes += frame_length;
	}
}

/*
 * The PSC has passed us a DMA interrupt event.
 */

static irqreturn_t mace_dma_intr(int irq, void *dev_id)
{
	struct net_device *dev = (struct net_device *) dev_id;
	struct mace_data *mp = netdev_priv(dev);
	int left, head;
	u16 status;
	u32 baka;

	/* Not sure what this does */

	while ((baka = psc_read_long(PSC_MYSTERY)) != psc_read_long(PSC_MYSTERY));
	if (!(baka & 0x60000000)) return IRQ_NONE;

	/*
	 * Process the read queue
	 */

	status = psc_read_word(PSC_ENETRD_CTL);

	if (status & 0x2000) {
		mace_rxdma_reset(dev);
	} else if (status & 0x0100) {
		psc_write_word(PSC_ENETRD_CMD + mp->rx_slot, 0x1100);

		left = psc_read_long(PSC_ENETRD_LEN + mp->rx_slot);
		head = N_RX_RING - left;

		/* Loop through the ring buffer and process new packages */

		while (mp->rx_tail < head) {
			mace_dma_rx_frame(dev, (struct mace_frame*) (mp->rx_ring
				+ (mp->rx_tail * MACE_BUFF_SIZE)));
			mp->rx_tail++;
		}

		/* If we're out of buffers in this ring then switch to */
		/* the other set, otherwise just reactivate this one.  */

		if (!left) {
			mace_load_rxdma_base(dev, mp->rx_slot);
			mp->rx_slot ^= 0x10;
		} else {
			psc_write_word(PSC_ENETRD_CMD + mp->rx_slot, 0x9800);
		}
	}

	/*
	 * Process the write queue
	 */

	status = psc_read_word(PSC_ENETWR_CTL);

	if (status & 0x2000) {
		mace_txdma_reset(dev);
	} else if (status & 0x0100) {
		psc_write_word(PSC_ENETWR_CMD + mp->tx_sloti, 0x0100);
		mp->tx_sloti ^= 0x10;
		mp->tx_count++;
	}
	return IRQ_HANDLED;
}

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Macintosh MACE ethernet driver");
MODULE_ALIAS("platform:macmace");

static int __devexit mac_mace_device_remove (struct platform_device *pdev)
{
	struct net_device *dev = platform_get_drvdata(pdev);
	struct mace_data *mp = netdev_priv(dev);

	unregister_netdev(dev);

	free_irq(dev->irq, dev);
	free_irq(IRQ_MAC_MACE_DMA, dev);

	dma_free_coherent(mp->device, N_RX_RING * MACE_BUFF_SIZE,
	                  mp->rx_ring, mp->rx_ring_phys);
	dma_free_coherent(mp->device, N_TX_RING * MACE_BUFF_SIZE,
	                  mp->tx_ring, mp->tx_ring_phys);

	free_netdev(dev);

	return 0;
}

static struct platform_driver mac_mace_driver = {
	.probe  = mace_probe,
	.remove = __devexit_p(mac_mace_device_remove),
	.driver	= {
		.name	= mac_mace_string,
		.owner	= THIS_MODULE,
	},
};

static int __init mac_mace_init_module(void)
{
	if (!MACH_IS_MAC)
		return -ENODEV;

	return platform_driver_register(&mac_mace_driver);
}

static void __exit mac_mace_cleanup_module(void)
{
	platform_driver_unregister(&mac_mace_driver);
}

module_init(mac_mace_init_module);
module_exit(mac_mace_cleanup_module);
