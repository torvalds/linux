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
 *	Copyright (C) 1998 Alan Cox <alan@redhat.com>
 *
 *	Modified heavily by Joshua M. Thompson based on Dave Huang's NetBSD driver
 */


#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/delay.h>
#include <linux/string.h>
#include <linux/crc32.h>
#include <linux/bitrev.h>
#include <asm/io.h>
#include <asm/pgtable.h>
#include <asm/irq.h>
#include <asm/macintosh.h>
#include <asm/macints.h>
#include <asm/mac_psc.h>
#include <asm/page.h>
#include "mace.h"

#define N_TX_RING	1
#define N_RX_RING	8
#define N_RX_PAGES	((N_RX_RING * 0x0800 + PAGE_SIZE - 1) / PAGE_SIZE)
#define TX_TIMEOUT	HZ

/* Bits in transmit DMA status */
#define TX_DMA_ERR	0x80

/* The MACE is simply wired down on a Mac68K box */

#define MACE_BASE	(void *)(0x50F1C000)
#define MACE_PROM	(void *)(0x50F08001)

struct mace_data {
	volatile struct mace *mace;
	volatile unsigned char *tx_ring;
	volatile unsigned char *tx_ring_phys;
	volatile unsigned char *rx_ring;
	volatile unsigned char *rx_ring_phys;
	int dma_intr;
	struct net_device_stats stats;
	int rx_slot, rx_tail;
	int tx_slot, tx_sloti, tx_count;
};

struct mace_frame {
	u16	len;
	u16	status;
	u16	rntpc;
	u16	rcvcc;
	u32	pad1;
	u32	pad2;
	u8	data[1];
	/* And frame continues.. */
};

#define PRIV_BYTES	sizeof(struct mace_data)

extern void psc_debug_dump(void);

static int mace_open(struct net_device *dev);
static int mace_close(struct net_device *dev);
static int mace_xmit_start(struct sk_buff *skb, struct net_device *dev);
static struct net_device_stats *mace_stats(struct net_device *dev);
static void mace_set_multicast(struct net_device *dev);
static int mace_set_address(struct net_device *dev, void *addr);
static irqreturn_t mace_interrupt(int irq, void *dev_id);
static irqreturn_t mace_dma_intr(int irq, void *dev_id);
static void mace_tx_timeout(struct net_device *dev);

/*
 * Load a receive DMA channel with a base address and ring length
 */

static void mace_load_rxdma_base(struct net_device *dev, int set)
{
	struct mace_data *mp = (struct mace_data *) dev->priv;

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
	struct mace_data *mp = (struct mace_data *) dev->priv;
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
	struct mace_data *mp = (struct mace_data *) dev->priv;
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

/*
 * Not really much of a probe. The hardware table tells us if this
 * model of Macintrash has a MACE (AV macintoshes)
 */

struct net_device *mace_probe(int unit)
{
	int j;
	struct mace_data *mp;
	unsigned char *addr;
	struct net_device *dev;
	unsigned char checksum = 0;
	static int found = 0;
	int err;

	if (found || macintosh_config->ether_type != MAC_ETHER_MACE)
		return ERR_PTR(-ENODEV);

	found = 1;	/* prevent 'finding' one on every device probe */

	dev = alloc_etherdev(PRIV_BYTES);
	if (!dev)
		return ERR_PTR(-ENOMEM);

	if (unit >= 0)
		sprintf(dev->name, "eth%d", unit);

	mp = (struct mace_data *) dev->priv;
	dev->base_addr = (u32)MACE_BASE;
	mp->mace = (volatile struct mace *) MACE_BASE;

	dev->irq = IRQ_MAC_MACE;
	mp->dma_intr = IRQ_MAC_MACE_DMA;

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
		return ERR_PTR(-ENODEV);
	}

	memset(&mp->stats, 0, sizeof(mp->stats));

	dev->open		= mace_open;
	dev->stop		= mace_close;
	dev->hard_start_xmit	= mace_xmit_start;
	dev->tx_timeout		= mace_tx_timeout;
	dev->watchdog_timeo	= TX_TIMEOUT;
	dev->get_stats		= mace_stats;
	dev->set_multicast_list	= mace_set_multicast;
	dev->set_mac_address	= mace_set_address;

	printk(KERN_INFO "%s: 68K MACE, hardware address %.2X", dev->name, dev->dev_addr[0]);
	for (j = 1 ; j < 6 ; j++) printk(":%.2X", dev->dev_addr[j]);
	printk("\n");

	err = register_netdev(dev);
	if (!err)
		return dev;

	free_netdev(dev);
	return ERR_PTR(err);
}

/*
 * Load the address on a mace controller.
 */

static int mace_set_address(struct net_device *dev, void *addr)
{
	unsigned char *p = addr;
	struct mace_data *mp = (struct mace_data *) dev->priv;
	volatile struct mace *mb = mp->mace;
	int i;
	unsigned long flags;
	u8 maccc;

	local_irq_save(flags);

	maccc = mb->maccc;

	/* load up the hardware address */
	mb->iac = ADDRCHG | PHYADDR;
	while ((mb->iac & ADDRCHG) != 0);

	for (i = 0; i < 6; ++i) {
		mb->padr = dev->dev_addr[i] = p[i];
	}

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
	struct mace_data *mp = (struct mace_data *) dev->priv;
	volatile struct mace *mb = mp->mace;
#if 0
	int i;

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
		printk(KERN_ERR "%s: software reset failed!!\n", dev->name);
		return -EAGAIN;
	}
#endif

	mb->biucc = XMTSP_64;
	mb->fifocc = XMTFW_16 | RCVFW_64 | XMTFWU | RCVFWU | XMTBRST | RCVBRST;
	mb->xmtfc = AUTO_PAD_XMIT;
	mb->plscc = PORTSEL_AUI;
	/* mb->utr = RTRD; */

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

	mp->rx_ring = (void *) __get_free_pages(GFP_KERNEL | GFP_DMA, N_RX_PAGES);
	mp->tx_ring = (void *) __get_free_pages(GFP_KERNEL | GFP_DMA, 0);

	if (mp->tx_ring==NULL || mp->rx_ring==NULL) {
		if (mp->rx_ring) free_pages((u32) mp->rx_ring, N_RX_PAGES);
		if (mp->tx_ring) free_pages((u32) mp->tx_ring, 0);
		free_irq(dev->irq, dev);
		free_irq(mp->dma_intr, dev);
		printk(KERN_ERR "%s: unable to allocate DMA buffers\n", dev->name);
		return -ENOMEM;
	}

	mp->rx_ring_phys = (unsigned char *) virt_to_bus((void *)mp->rx_ring);
	mp->tx_ring_phys = (unsigned char *) virt_to_bus((void *)mp->tx_ring);

	/* We want the Rx buffer to be uncached and the Tx buffer to be writethrough */

	kernel_set_cachemode((void *)mp->rx_ring, N_RX_PAGES * PAGE_SIZE, IOMAP_NOCACHE_NONSER);
	kernel_set_cachemode((void *)mp->tx_ring, PAGE_SIZE, IOMAP_WRITETHROUGH);

	mace_dma_off(dev);

	/* Not sure what these do */

	psc_write_word(PSC_ENETWR_CTL, 0x9000);
	psc_write_word(PSC_ENETRD_CTL, 0x9000);
	psc_write_word(PSC_ENETWR_CTL, 0x0400);
	psc_write_word(PSC_ENETRD_CTL, 0x0400);

#if 0
	/* load up the hardware address */

	mb->iac = ADDRCHG | PHYADDR;

	while ((mb->iac & ADDRCHG) != 0);

	for (i = 0; i < 6; ++i)
		mb->padr = dev->dev_addr[i];

	/* clear the multicast filter */
	mb->iac = ADDRCHG | LOGADDR;

	while ((mb->iac & ADDRCHG) != 0);

	for (i = 0; i < 8; ++i)
		mb->ladrf = 0;

	mb->plscc = PORTSEL_GPSI + ENPLSIO;

	mb->maccc = ENXMT | ENRCV;
	mb->imr = RCVINT;
#endif

	mace_rxdma_reset(dev);
	mace_txdma_reset(dev);

	return 0;
}

/*
 * Shut down the mace and its interrupt channel
 */

static int mace_close(struct net_device *dev)
{
	struct mace_data *mp = (struct mace_data *) dev->priv;
	volatile struct mace *mb = mp->mace;

	mb->maccc = 0;		/* disable rx and tx	 */
	mb->imr = 0xFF;		/* disable all irqs	 */
	mace_dma_off(dev);	/* disable rx and tx dma */

	free_irq(dev->irq, dev);
	free_irq(IRQ_MAC_MACE_DMA, dev);

	free_pages((u32) mp->rx_ring, N_RX_PAGES);
	free_pages((u32) mp->tx_ring, 0);

	return 0;
}

/*
 * Transmit a frame
 */

static int mace_xmit_start(struct sk_buff *skb, struct net_device *dev)
{
	struct mace_data *mp = (struct mace_data *) dev->priv;

	/* Stop the queue if the buffer is full */

	if (!mp->tx_count) {
		netif_stop_queue(dev);
		return 1;
	}
	mp->tx_count--;

	mp->stats.tx_packets++;
	mp->stats.tx_bytes += skb->len;

	/* We need to copy into our xmit buffer to take care of alignment and caching issues */
	skb_copy_from_linear_data(skb, mp->tx_ring, skb->len);

	/* load the Tx DMA and fire it off */

	psc_write_long(PSC_ENETWR_ADDR + mp->tx_slot, (u32)  mp->tx_ring_phys);
	psc_write_long(PSC_ENETWR_LEN + mp->tx_slot, skb->len);
	psc_write_word(PSC_ENETWR_CMD + mp->tx_slot, 0x9800);

	mp->tx_slot ^= 0x10;

	dev_kfree_skb(skb);

	return 0;
}

static struct net_device_stats *mace_stats(struct net_device *dev)
{
	struct mace_data *p = (struct mace_data *) dev->priv;
	return &p->stats;
}

static void mace_set_multicast(struct net_device *dev)
{
	struct mace_data *mp = (struct mace_data *) dev->priv;
	volatile struct mace *mb = mp->mace;
	int i, j;
	u32 crc;
	u8 maccc;

	maccc = mb->maccc;
	mb->maccc &= ~PROM;

	if (dev->flags & IFF_PROMISC) {
		mb->maccc |= PROM;
	} else {
		unsigned char multicast_filter[8];
		struct dev_mc_list *dmi = dev->mc_list;

		if (dev->flags & IFF_ALLMULTI) {
			for (i = 0; i < 8; i++) {
				multicast_filter[i] = 0xFF;
			}
		} else {
			for (i = 0; i < 8; i++)
				multicast_filter[i] = 0;
			for (i = 0; i < dev->mc_count; i++) {
				crc = ether_crc_le(6, dmi->dmi_addr);
				j = crc >> 26;	/* bit number in multicast_filter */
				multicast_filter[j >> 3] |= 1 << (j & 7);
				dmi = dmi->next;
			}
		}

		mb->iac = ADDRCHG | LOGADDR;
		while (mb->iac & ADDRCHG);

		for (i = 0; i < 8; ++i) {
			mb->ladrf = multicast_filter[i];
		}
	}

	mb->maccc = maccc;
}

/*
 * Miscellaneous interrupts are handled here. We may end up
 * having to bash the chip on the head for bad errors
 */

static void mace_handle_misc_intrs(struct mace_data *mp, int intr)
{
	volatile struct mace *mb = mp->mace;
	static int mace_babbles, mace_jabbers;

	if (intr & MPCO) {
		mp->stats.rx_missed_errors += 256;
	}
	mp->stats.rx_missed_errors += mb->mpc;	/* reading clears it */

	if (intr & RNTPCO) {
		mp->stats.rx_length_errors += 256;
	}
	mp->stats.rx_length_errors += mb->rntpc;	/* reading clears it */

	if (intr & CERR) {
		++mp->stats.tx_heartbeat_errors;
	}
	if (intr & BABBLE) {
		if (mace_babbles++ < 4) {
			printk(KERN_DEBUG "mace: babbling transmitter\n");
		}
	}
	if (intr & JABBER) {
		if (mace_jabbers++ < 4) {
			printk(KERN_DEBUG "mace: jabbering transceiver\n");
		}
	}
}

/*
 *	A transmit error has occurred. (We kick the transmit side from
 *	the DMA completion)
 */

static void mace_xmit_error(struct net_device *dev)
{
	struct mace_data *mp = (struct mace_data *) dev->priv;
	volatile struct mace *mb = mp->mace;
	u8 xmtfs, xmtrc;

	xmtfs = mb->xmtfs;
	xmtrc = mb->xmtrc;

	if (xmtfs & XMTSV) {
		if (xmtfs & UFLO) {
			printk("%s: DMA underrun.\n", dev->name);
			mp->stats.tx_errors++;
			mp->stats.tx_fifo_errors++;
			mace_txdma_reset(dev);
		}
		if (xmtfs & RTRY) {
			mp->stats.collisions++;
		}
	}
}

/*
 *	A receive interrupt occurred.
 */

static void mace_recv_interrupt(struct net_device *dev)
{
/*	struct mace_data *mp = (struct mace_data *) dev->priv; */
//	volatile struct mace *mb = mp->mace;
}

/*
 * Process the chip interrupt
 */

static irqreturn_t mace_interrupt(int irq, void *dev_id)
{
	struct net_device *dev = (struct net_device *) dev_id;
	struct mace_data *mp = (struct mace_data *) dev->priv;
	volatile struct mace *mb = mp->mace;
	u8 ir;

	ir = mb->ir;
	mace_handle_misc_intrs(mp, ir);

	if (ir & XMTINT) {
		mace_xmit_error(dev);
	}
	if (ir & RCVINT) {
		mace_recv_interrupt(dev);
	}
	return IRQ_HANDLED;
}

static void mace_tx_timeout(struct net_device *dev)
{
/*	struct mace_data *mp = (struct mace_data *) dev->priv; */
//	volatile struct mace *mb = mp->mace;
}

/*
 * Handle a newly arrived frame
 */

static void mace_dma_rx_frame(struct net_device *dev, struct mace_frame *mf)
{
	struct mace_data *mp = (struct mace_data *) dev->priv;
	struct sk_buff *skb;

	if (mf->status & RS_OFLO) {
		printk("%s: fifo overflow.\n", dev->name);
		mp->stats.rx_errors++;
		mp->stats.rx_fifo_errors++;
	}
	if (mf->status&(RS_CLSN|RS_FRAMERR|RS_FCSERR))
		mp->stats.rx_errors++;

	if (mf->status&RS_CLSN) {
		mp->stats.collisions++;
	}
	if (mf->status&RS_FRAMERR) {
		mp->stats.rx_frame_errors++;
	}
	if (mf->status&RS_FCSERR) {
		mp->stats.rx_crc_errors++;
	}

	skb = dev_alloc_skb(mf->len+2);
	if (!skb) {
		mp->stats.rx_dropped++;
		return;
	}
	skb_reserve(skb,2);
	memcpy(skb_put(skb, mf->len), mf->data, mf->len);

	skb->protocol = eth_type_trans(skb, dev);
	netif_rx(skb);
	dev->last_rx = jiffies;
	mp->stats.rx_packets++;
	mp->stats.rx_bytes += mf->len;
}

/*
 * The PSC has passed us a DMA interrupt event.
 */

static irqreturn_t mace_dma_intr(int irq, void *dev_id)
{
	struct net_device *dev = (struct net_device *) dev_id;
	struct mace_data *mp = (struct mace_data *) dev->priv;
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
			mace_dma_rx_frame(dev, (struct mace_frame *) (mp->rx_ring + (mp->rx_tail * 0x0800)));
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
		netif_wake_queue(dev);
	}
	return IRQ_HANDLED;
}

MODULE_LICENSE("GPL");
