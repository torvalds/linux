/* mvme147.c  : the  Linux/mvme147/lance ethernet driver
 *
 * Copyright (C) 05/1998 Peter Maydell <pmaydell@chiark.greenend.org.uk>
 * Based on the Sun Lance driver and the NetBSD HP Lance driver
 * Uses the generic 7990.c LANCE code.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/interrupt.h>
#include <linux/ioport.h>
#include <linux/string.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/errno.h>
#include <linux/gfp.h>
/* Used for the temporal inet entries and routing */
#include <linux/socket.h>
#include <linux/route.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>

#include <asm/io.h>
#include <asm/pgtable.h>
#include <asm/mvme147hw.h>

/* We have 16834 bytes of RAM for the init block and buffers. This places
 * an upper limit on the number of buffers we can use. NetBSD uses 8 Rx
 * buffers and 2 Tx buffers.
 */
#define LANCE_LOG_TX_BUFFERS 1
#define LANCE_LOG_RX_BUFFERS 3

#include "7990.h"                                 /* use generic LANCE code */

/* Our private data structure */
struct m147lance_private {
	struct lance_private lance;
	unsigned long ram;
};

/* function prototypes... This is easy because all the grot is in the
 * generic LANCE support. All we have to support is probing for boards,
 * plus board-specific init, open and close actions.
 * Oh, and we need to tell the generic code how to read and write LANCE registers...
 */
static int m147lance_open(struct net_device *dev);
static int m147lance_close(struct net_device *dev);
static void m147lance_writerap(struct lance_private *lp, unsigned short value);
static void m147lance_writerdp(struct lance_private *lp, unsigned short value);
static unsigned short m147lance_readrdp(struct lance_private *lp);

typedef void (*writerap_t)(void *, unsigned short);
typedef void (*writerdp_t)(void *, unsigned short);
typedef unsigned short (*readrdp_t)(void *);

static const struct net_device_ops lance_netdev_ops = {
	.ndo_open		= m147lance_open,
	.ndo_stop		= m147lance_close,
	.ndo_start_xmit		= lance_start_xmit,
	.ndo_set_rx_mode	= lance_set_multicast,
	.ndo_tx_timeout		= lance_tx_timeout,
	.ndo_change_mtu		= eth_change_mtu,
	.ndo_validate_addr	= eth_validate_addr,
	.ndo_set_mac_address	= eth_mac_addr,
};

/* Initialise the one and only on-board 7990 */
struct net_device * __init mvme147lance_probe(int unit)
{
	struct net_device *dev;
	static int called;
	static const char name[] = "MVME147 LANCE";
	struct m147lance_private *lp;
	u_long *addr;
	u_long address;
	int err;

	if (!MACH_IS_MVME147 || called)
		return ERR_PTR(-ENODEV);
	called++;

	dev = alloc_etherdev(sizeof(struct m147lance_private));
	if (!dev)
		return ERR_PTR(-ENOMEM);

	if (unit >= 0)
		sprintf(dev->name, "eth%d", unit);

	/* Fill the dev fields */
	dev->base_addr = (unsigned long)MVME147_LANCE_BASE;
	dev->netdev_ops = &lance_netdev_ops;
	dev->dma = 0;

	addr=(u_long *)ETHERNET_ADDRESS;
	address = *addr;
	dev->dev_addr[0]=0x08;
	dev->dev_addr[1]=0x00;
	dev->dev_addr[2]=0x3e;
	address=address>>8;
	dev->dev_addr[5]=address&0xff;
	address=address>>8;
	dev->dev_addr[4]=address&0xff;
	address=address>>8;
	dev->dev_addr[3]=address&0xff;

	printk("%s: MVME147 at 0x%08lx, irq %d, "
	       "Hardware Address %pM\n",
	       dev->name, dev->base_addr, MVME147_LANCE_IRQ,
	       dev->dev_addr);

	lp = netdev_priv(dev);
	lp->ram = __get_dma_pages(GFP_ATOMIC, 3);	/* 16K */
	if (!lp->ram)
	{
		printk("%s: No memory for LANCE buffers\n", dev->name);
		free_netdev(dev);
		return ERR_PTR(-ENOMEM);
	}

	lp->lance.name = (char*)name;                   /* discards const, shut up gcc */
	lp->lance.base = dev->base_addr;
	lp->lance.init_block = (struct lance_init_block *)(lp->ram); /* CPU addr */
	lp->lance.lance_init_block = (struct lance_init_block *)(lp->ram);                 /* LANCE addr of same RAM */
	lp->lance.busmaster_regval = LE_C3_BSWP;        /* we're bigendian */
	lp->lance.irq = MVME147_LANCE_IRQ;
	lp->lance.writerap = (writerap_t)m147lance_writerap;
	lp->lance.writerdp = (writerdp_t)m147lance_writerdp;
	lp->lance.readrdp = (readrdp_t)m147lance_readrdp;
	lp->lance.lance_log_rx_bufs = LANCE_LOG_RX_BUFFERS;
	lp->lance.lance_log_tx_bufs = LANCE_LOG_TX_BUFFERS;
	lp->lance.rx_ring_mod_mask = RX_RING_MOD_MASK;
	lp->lance.tx_ring_mod_mask = TX_RING_MOD_MASK;

	err = register_netdev(dev);
	if (err) {
		free_pages(lp->ram, 3);
		free_netdev(dev);
		return ERR_PTR(err);
	}

	return dev;
}

static void m147lance_writerap(struct lance_private *lp, unsigned short value)
{
	out_be16(lp->base + LANCE_RAP, value);
}

static void m147lance_writerdp(struct lance_private *lp, unsigned short value)
{
	out_be16(lp->base + LANCE_RDP, value);
}

static unsigned short m147lance_readrdp(struct lance_private *lp)
{
	return in_be16(lp->base + LANCE_RDP);
}

static int m147lance_open(struct net_device *dev)
{
	int status;

	status = lance_open(dev);                 /* call generic lance open code */
	if (status)
		return status;
	/* enable interrupts at board level. */
	m147_pcc->lan_cntrl=0;       /* clear the interrupts (if any) */
	m147_pcc->lan_cntrl=0x08 | 0x04;     /* Enable irq 4 */

	return 0;
}

static int m147lance_close(struct net_device *dev)
{
	/* disable interrupts at boardlevel */
	m147_pcc->lan_cntrl=0x0; /* disable interrupts */
	lance_close(dev);
	return 0;
}

#ifdef MODULE
MODULE_LICENSE("GPL");

static struct net_device *dev_mvme147_lance;
int __init init_module(void)
{
	dev_mvme147_lance = mvme147lance_probe(-1);
	return PTR_RET(dev_mvme147_lance);
}

void __exit cleanup_module(void)
{
	struct m147lance_private *lp = netdev_priv(dev_mvme147_lance);
	unregister_netdev(dev_mvme147_lance);
	free_pages(lp->ram, 3);
	free_netdev(dev_mvme147_lance);
}

#endif /* MODULE */
