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
 *
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/net.h>
#include <linux/skbuff.h>
#include <linux/netdevice.h>
#include <linux/if_arp.h>
#include <linux/delay.h>
#include <linux/ioport.h>
#include <linux/init.h>
#include <net/arp.h>

#include <asm/irq.h>
#include <asm/io.h>
#include <asm/dma.h>
#include <asm/byteorder.h>
#include <net/syncppp.h>
#include "z85230.h"


struct slvl_device
{
	void *if_ptr;	/* General purpose pointer (used by SPPP) */
	struct z8530_channel *chan;
	struct ppp_device pppdev;
	int channel;
};


struct slvl_board
{
	struct slvl_device *dev[2];
	struct z8530_dev board;
	int iobase;
};

/*
 *	Network driver support routines
 */

/*
 *	Frame receive. Simple for our card as we do sync ppp and there
 *	is no funny garbage involved
 */
 
static void sealevel_input(struct z8530_channel *c, struct sk_buff *skb)
{
	/* Drop the CRC - it's not a good idea to try and negotiate it ;) */
	skb_trim(skb, skb->len-2);
	skb->protocol=htons(ETH_P_WAN_PPP);
	skb_reset_mac_header(skb);
	skb->dev=c->netdevice;
	/*
	 *	Send it to the PPP layer. We don't have time to process
	 *	it right now.
	 */
	netif_rx(skb);
	c->netdevice->last_rx = jiffies;
}
 
/*
 *	We've been placed in the UP state
 */ 
 
static int sealevel_open(struct net_device *d)
{
	struct slvl_device *slvl=d->priv;
	int err = -1;
	int unit = slvl->channel;
	
	/*
	 *	Link layer up. 
	 */

	switch(unit)
	{
		case 0:
			err=z8530_sync_dma_open(d, slvl->chan);
			break;
		case 1:
			err=z8530_sync_open(d, slvl->chan);
			break;
	}
	
	if(err)
		return err;
	/*
	 *	Begin PPP
	 */
	err=sppp_open(d);
	if(err)
	{
		switch(unit)
		{
			case 0:
				z8530_sync_dma_close(d, slvl->chan);
				break;
			case 1:
				z8530_sync_close(d, slvl->chan);
				break;
		}				
		return err;
	}
	
	slvl->chan->rx_function=sealevel_input;
	
	/*
	 *	Go go go
	 */
	netif_start_queue(d);
	return 0;
}

static int sealevel_close(struct net_device *d)
{
	struct slvl_device *slvl=d->priv;
	int unit = slvl->channel;
	
	/*
	 *	Discard new frames
	 */
	
	slvl->chan->rx_function=z8530_null_rx;
		
	/*
	 *	PPP off
	 */
	sppp_close(d);
	/*
	 *	Link layer down
	 */

	netif_stop_queue(d);
		
	switch(unit)
	{
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
	/* struct slvl_device *slvl=d->priv;
	   z8530_ioctl(d,&slvl->sync.chanA,ifr,cmd) */
	return sppp_do_ioctl(d, ifr,cmd);
}

static struct net_device_stats *sealevel_get_stats(struct net_device *d)
{
	struct slvl_device *slvl=d->priv;
	if(slvl)
		return z8530_get_stats(slvl->chan);
	else
		return NULL;
}

/*
 *	Passed PPP frames, fire them downwind.
 */
 
static int sealevel_queue_xmit(struct sk_buff *skb, struct net_device *d)
{
	struct slvl_device *slvl=d->priv;
	return z8530_queue_xmit(slvl->chan, skb);
}

static int sealevel_neigh_setup(struct neighbour *n)
{
	if (n->nud_state == NUD_NONE) {
		n->ops = &arp_broken_ops;
		n->output = n->ops->output;
	}
	return 0;
}

static int sealevel_neigh_setup_dev(struct net_device *dev, struct neigh_parms *p)
{
	if (p->tbl->family == AF_INET) {
		p->neigh_setup = sealevel_neigh_setup;
		p->ucast_probes = 0;
		p->mcast_probes = 0;
	}
	return 0;
}

static int sealevel_attach(struct net_device *dev)
{
	struct slvl_device *sv = dev->priv;
	sppp_attach(&sv->pppdev);
	return 0;
}

static void sealevel_detach(struct net_device *dev)
{
	sppp_detach(dev);
}
		
static void slvl_setup(struct net_device *d)
{
	d->open = sealevel_open;
	d->stop = sealevel_close;
	d->init = sealevel_attach;
	d->uninit = sealevel_detach;
	d->hard_start_xmit = sealevel_queue_xmit;
	d->get_stats = sealevel_get_stats;
	d->set_multicast_list = NULL;
	d->do_ioctl = sealevel_ioctl;
	d->neigh_setup = sealevel_neigh_setup_dev;
	d->set_mac_address = NULL;

}

static inline struct slvl_device *slvl_alloc(int iobase, int irq)
{
	struct net_device *d;
	struct slvl_device *sv;

	d = alloc_netdev(sizeof(struct slvl_device), "hdlc%d",
			 slvl_setup);

	if (!d) 
		return NULL;

	sv = d->priv;
	d->ml_priv = sv;
	sv->if_ptr = &sv->pppdev;
	sv->pppdev.dev = d;
	d->base_addr = iobase;
	d->irq = irq;
		
	return sv;
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

	if(!request_region(iobase, 8, "Sealevel 4021")) 
	{	
		printk(KERN_WARNING "sealevel: I/O 0x%X already in use.\n", iobase);
		return NULL;
	}
	
	b = kzalloc(sizeof(struct slvl_board), GFP_KERNEL);
	if(!b)
		goto fail3;

	if (!(b->dev[0]= slvl_alloc(iobase, irq)))
		goto fail2;

	b->dev[0]->chan = &b->board.chanA;	
	b->dev[0]->channel = 0;
	
	if (!(b->dev[1] = slvl_alloc(iobase, irq)))
		goto fail1_0;

	b->dev[1]->chan = &b->board.chanB;
	b->dev[1]->channel = 1;

	dev = &b->board;
	
	/*
	 *	Stuff in the I/O addressing
	 */
	 
	dev->active = 0;

	b->iobase = iobase;
	
	/*
	 *	Select 8530 delays for the old board
	 */
	 
	if(slow)
		iobase |= Z8530_PORT_SLEEP;
		
	dev->chanA.ctrlio=iobase+1;
	dev->chanA.dataio=iobase;
	dev->chanB.ctrlio=iobase+3;
	dev->chanB.dataio=iobase+2;
	
	dev->chanA.irqs=&z8530_nop;
	dev->chanB.irqs=&z8530_nop;
	
	/*
	 *	Assert DTR enable DMA
	 */
	 
	outb(3|(1<<7), b->iobase+4);	
	

	/* We want a fast IRQ for this device. Actually we'd like an even faster
	   IRQ ;) - This is one driver RtLinux is made for */
   
	if(request_irq(irq, &z8530_interrupt, IRQF_DISABLED, "SeaLevel", dev)<0)
	{
		printk(KERN_WARNING "sealevel: IRQ %d already in use.\n", irq);
		goto fail1_1;
	}
	
	dev->irq=irq;
	dev->chanA.private=&b->dev[0];
	dev->chanB.private=&b->dev[1];
	dev->chanA.netdevice=b->dev[0]->pppdev.dev;
	dev->chanB.netdevice=b->dev[1]->pppdev.dev;
	dev->chanA.dev=dev;
	dev->chanB.dev=dev;

	dev->chanA.txdma=3;
	dev->chanA.rxdma=1;
	if(request_dma(dev->chanA.txdma, "SeaLevel (TX)")!=0)
		goto fail;
		
	if(request_dma(dev->chanA.rxdma, "SeaLevel (RX)")!=0)
		goto dmafail;
	
	disable_irq(irq);
		
	/*
	 *	Begin normal initialise
	 */
	 
	if(z8530_init(dev)!=0)
	{
		printk(KERN_ERR "Z8530 series device not found.\n");
		enable_irq(irq);
		goto dmafail2;
	}
	if(dev->type==Z85C30)
	{
		z8530_channel_load(&dev->chanA, z8530_hdlc_kilostream);
		z8530_channel_load(&dev->chanB, z8530_hdlc_kilostream);
	}
	else
	{
		z8530_channel_load(&dev->chanA, z8530_hdlc_kilostream_85230);
		z8530_channel_load(&dev->chanB, z8530_hdlc_kilostream_85230);
	}

	/*
	 *	Now we can take the IRQ
	 */
	
	enable_irq(irq);

	if (register_netdev(b->dev[0]->pppdev.dev)) 
		goto dmafail2;
		
	if (register_netdev(b->dev[1]->pppdev.dev)) 
		goto fail_unit;

	z8530_describe(dev, "I/O", iobase);
	dev->active=1;
	return b;

fail_unit:
	unregister_netdev(b->dev[0]->pppdev.dev);
	
dmafail2:
	free_dma(dev->chanA.rxdma);
dmafail:
	free_dma(dev->chanA.txdma);
fail:
	free_irq(irq, dev);
fail1_1:
	free_netdev(b->dev[1]->pppdev.dev);
fail1_0:
	free_netdev(b->dev[0]->pppdev.dev);
fail2:
	kfree(b);
fail3:
	release_region(iobase,8);
	return NULL;
}

static void __exit slvl_shutdown(struct slvl_board *b)
{
	int u;

	z8530_shutdown(&b->board);
	
	for(u=0; u<2; u++)
	{
		struct net_device *d = b->dev[u]->pppdev.dev;
		unregister_netdev(d);
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
static int slow=0;

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
#ifdef MODULE
	printk(KERN_INFO "SeaLevel Z85230 Synchronous Driver v 0.02.\n");
	printk(KERN_INFO "(c) Copyright 1998, Building Number Three Ltd.\n");
#endif
	slvl_unit = slvl_init(io, irq, txdma, rxdma, slow);

	return slvl_unit ? 0 : -ENODEV;
}

static void __exit slvl_cleanup_module(void)
{
	if(slvl_unit)
		slvl_shutdown(slvl_unit);
}

module_init(slvl_init_module);
module_exit(slvl_cleanup_module);
