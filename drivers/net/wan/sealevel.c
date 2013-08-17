/*
 *	Sealevel Systems 4021 driver.
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License
 *	as published by the Free Software Foundation; either version
 *	2 of the License, or (at your option) any later version.
 *
 *	(c) Copyright 1999, 2001 Alan Cox
 *	(c) Copyright 2001 Red Hat Inc.
 *	Generic HDLC port Copyright (C) 2008 Krzysztof Halasa <khc@pm.waw.pl>
 *
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/net.h>
#include <linux/skbuff.h>
#include <linux/netdevice.h>
#include <linux/if_arp.h>
#include <linux/delay.h>
#include <linux/hdlc.h>
#include <linux/ioport.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <net/arp.h>

#include <asm/irq.h>
#include <asm/io.h>
#include <asm/dma.h>
#include <asm/byteorder.h>
#include "z85230.h"


struct slvl_device
{
	struct z8530_channel *chan;
	int channel;
};


struct slvl_board
{
	struct slvl_device dev[2];
	struct z8530_dev board;
	int iobase;
};

/*
 *	Network driver support routines
 */

static inline struct slvl_device* dev_to_chan(struct net_device *dev)
{
	return (struct slvl_device *)dev_to_hdlc(dev)->priv;
}

/*
 *	Frame receive. Simple for our card as we do HDLC and there
 *	is no funny garbage involved
 */

static void sealevel_input(struct z8530_channel *c, struct sk_buff *skb)
{
	/* Drop the CRC - it's not a good idea to try and negotiate it ;) */
	skb_trim(skb, skb->len - 2);
	skb->protocol = hdlc_type_trans(skb, c->netdevice);
	skb_reset_mac_header(skb);
	skb->dev = c->netdevice;
	netif_rx(skb);
}

/*
 *	We've been placed in the UP state
 */

static int sealevel_open(struct net_device *d)
{
	struct slvl_device *slvl = dev_to_chan(d);
	int err = -1;
	int unit = slvl->channel;

	/*
	 *	Link layer up.
	 */

	switch (unit) {
		case 0:
			err = z8530_sync_dma_open(d, slvl->chan);
			break;
		case 1:
			err = z8530_sync_open(d, slvl->chan);
			break;
	}

	if (err)
		return err;

	err = hdlc_open(d);
	if (err) {
		switch (unit) {
			case 0:
				z8530_sync_dma_close(d, slvl->chan);
				break;
			case 1:
				z8530_sync_close(d, slvl->chan);
				break;
		}
		return err;
	}

	slvl->chan->rx_function = sealevel_input;

	/*
	 *	Go go go
	 */
	netif_start_queue(d);
	return 0;
}

static int sealevel_close(struct net_device *d)
{
	struct slvl_device *slvl = dev_to_chan(d);
	int unit = slvl->channel;

	/*
	 *	Discard new frames
	 */

	slvl->chan->rx_function = z8530_null_rx;

	hdlc_close(d);
	netif_stop_queue(d);

	switch (unit) {
		case 0:
			z8530_sync_dma_close(d, slvl->chan);
			break;
		case 1:
			z8530_sync_close(d, slvl->chan);
			break;
	}
	return 0;
}

static int sealevel_ioctl(struct net_device *d, struct ifreq *ifr, int cmd)
{
	/* struct slvl_device *slvl=dev_to_chan(d);
	   z8530_ioctl(d,&slvl->sync.chanA,ifr,cmd) */
	return hdlc_ioctl(d, ifr, cmd);
}

/*
 *	Passed network frames, fire them downwind.
 */

static netdev_tx_t sealevel_queue_xmit(struct sk_buff *skb,
					     struct net_device *d)
{
	return z8530_queue_xmit(dev_to_chan(d)->chan, skb);
}

static int sealevel_attach(struct net_device *dev, unsigned short encoding,
			   unsigned short parity)
{
	if (encoding == ENCODING_NRZ && parity == PARITY_CRC16_PR1_CCITT)
		return 0;
	return -EINVAL;
}

static const struct net_device_ops sealevel_ops = {
	.ndo_open       = sealevel_open,
	.ndo_stop       = sealevel_close,
	.ndo_change_mtu = hdlc_change_mtu,
	.ndo_start_xmit = hdlc_start_xmit,
	.ndo_do_ioctl   = sealevel_ioctl,
};

static int slvl_setup(struct slvl_device *sv, int iobase, int irq)
{
	struct net_device *dev = alloc_hdlcdev(sv);
	if (!dev)
		return -1;

	dev_to_hdlc(dev)->attach = sealevel_attach;
	dev_to_hdlc(dev)->xmit = sealevel_queue_xmit;
	dev->netdev_ops = &sealevel_ops;
	dev->base_addr = iobase;
	dev->irq = irq;

	if (register_hdlc_device(dev)) {
		pr_err("unable to register HDLC device\n");
		free_netdev(dev);
		return -1;
	}

	sv->chan->netdevice = dev;
	return 0;
}


/*
 *	Allocate and setup Sealevel board.
 */

static __init struct slvl_board *slvl_init(int iobase, int irq,
					   int txdma, int rxdma, int slow)
{
	struct z8530_dev *dev;
	struct slvl_board *b;

	/*
	 *	Get the needed I/O space
	 */

	if (!request_region(iobase, 8, "Sealevel 4021")) {
		pr_warn("I/O 0x%X already in use\n", iobase);
		return NULL;
	}

	b = kzalloc(sizeof(struct slvl_board), GFP_KERNEL);
	if (!b)
		goto err_kzalloc;

	b->dev[0].chan = &b->board.chanA;
	b->dev[0].channel = 0;

	b->dev[1].chan = &b->board.chanB;
	b->dev[1].channel = 1;

	dev = &b->board;

	/*
	 *	Stuff in the I/O addressing
	 */

	dev->active = 0;

	b->iobase = iobase;

	/*
	 *	Select 8530 delays for the old board
	 */

	if (slow)
		iobase |= Z8530_PORT_SLEEP;

	dev->chanA.ctrlio = iobase + 1;
	dev->chanA.dataio = iobase;
	dev->chanB.ctrlio = iobase + 3;
	dev->chanB.dataio = iobase + 2;

	dev->chanA.irqs = &z8530_nop;
	dev->chanB.irqs = &z8530_nop;

	/*
	 *	Assert DTR enable DMA
	 */

	outb(3 | (1 << 7), b->iobase + 4);


	/* We want a fast IRQ for this device. Actually we'd like an even faster
	   IRQ ;) - This is one driver RtLinux is made for */

	if (request_irq(irq, z8530_interrupt, IRQF_DISABLED,
			"SeaLevel", dev) < 0) {
		pr_warn("IRQ %d already in use\n", irq);
		goto err_request_irq;
	}

	dev->irq = irq;
	dev->chanA.private = &b->dev[0];
	dev->chanB.private = &b->dev[1];
	dev->chanA.dev = dev;
	dev->chanB.dev = dev;

	dev->chanA.txdma = 3;
	dev->chanA.rxdma = 1;
	if (request_dma(dev->chanA.txdma, "SeaLevel (TX)"))
		goto err_dma_tx;

	if (request_dma(dev->chanA.rxdma, "SeaLevel (RX)"))
		goto err_dma_rx;

	disable_irq(irq);

	/*
	 *	Begin normal initialise
	 */

	if (z8530_init(dev) != 0) {
		pr_err("Z8530 series device not found\n");
		enable_irq(irq);
		goto free_hw;
	}
	if (dev->type == Z85C30) {
		z8530_channel_load(&dev->chanA, z8530_hdlc_kilostream);
		z8530_channel_load(&dev->chanB, z8530_hdlc_kilostream);
	} else {
		z8530_channel_load(&dev->chanA, z8530_hdlc_kilostream_85230);
		z8530_channel_load(&dev->chanB, z8530_hdlc_kilostream_85230);
	}

	/*
	 *	Now we can take the IRQ
	 */

	enable_irq(irq);

	if (slvl_setup(&b->dev[0], iobase, irq))
		goto free_hw;
	if (slvl_setup(&b->dev[1], iobase, irq))
		goto free_netdev0;

	z8530_describe(dev, "I/O", iobase);
	dev->active = 1;
	return b;

free_netdev0:
	unregister_hdlc_device(b->dev[0].chan->netdevice);
	free_netdev(b->dev[0].chan->netdevice);
free_hw:
	free_dma(dev->chanA.rxdma);
err_dma_rx:
	free_dma(dev->chanA.txdma);
err_dma_tx:
	free_irq(irq, dev);
err_request_irq:
	kfree(b);
err_kzalloc:
	release_region(iobase, 8);
	return NULL;
}

static void __exit slvl_shutdown(struct slvl_board *b)
{
	int u;

	z8530_shutdown(&b->board);

	for (u = 0; u < 2; u++) {
		struct net_device *d = b->dev[u].chan->netdevice;
		unregister_hdlc_device(d);
		free_netdev(d);
	}

	free_irq(b->board.irq, &b->board);
	free_dma(b->board.chanA.rxdma);
	free_dma(b->board.chanA.txdma);
	/* DMA off on the card, drop DTR */
	outb(0, b->iobase);
	release_region(b->iobase, 8);
	kfree(b);
}


static int io=0x238;
static int txdma=1;
static int rxdma=3;
static int irq=5;
static bool slow=false;

module_param(io, int, 0);
MODULE_PARM_DESC(io, "The I/O base of the Sealevel card");
module_param(txdma, int, 0);
MODULE_PARM_DESC(txdma, "Transmit DMA channel");
module_param(rxdma, int, 0);
MODULE_PARM_DESC(rxdma, "Receive DMA channel");
module_param(irq, int, 0);
MODULE_PARM_DESC(irq, "The interrupt line setting for the SeaLevel card");
module_param(slow, bool, 0);
MODULE_PARM_DESC(slow, "Set this for an older Sealevel card such as the 4012");

MODULE_AUTHOR("Alan Cox");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Modular driver for the SeaLevel 4021");

static struct slvl_board *slvl_unit;

static int __init slvl_init_module(void)
{
	slvl_unit = slvl_init(io, irq, txdma, rxdma, slow);

	return slvl_unit ? 0 : -ENODEV;
}

static void __exit slvl_cleanup_module(void)
{
	if (slvl_unit)
		slvl_shutdown(slvl_unit);
}

module_init(slvl_init_module);
module_exit(slvl_cleanup_module);
