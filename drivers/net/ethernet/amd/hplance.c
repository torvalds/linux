/* hplance.c  : the  Linux/hp300/lance ethernet driver
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
/* Used for the temporal inet entries and routing */
#include <linux/socket.h>
#include <linux/route.h>
#include <linux/dio.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>

#include <asm/io.h>
#include <asm/pgtable.h>

#include "hplance.h"

/* We have 16392 bytes of RAM for the init block and buffers. This places
 * an upper limit on the number of buffers we can use. NetBSD uses 8 Rx
 * buffers and 2 Tx buffers, it takes (8 + 2) * 1544 bytes.
 */
#define LANCE_LOG_TX_BUFFERS 1
#define LANCE_LOG_RX_BUFFERS 3

#include "7990.h"                                 /* use generic LANCE code */

/* Our private data structure */
struct hplance_private {
	struct lance_private lance;
};

/* function prototypes... This is easy because all the grot is in the
 * generic LANCE support. All we have to support is probing for boards,
 * plus board-specific init, open and close actions.
 * Oh, and we need to tell the generic code how to read and write LANCE registers...
 */
static int hplance_init_one(struct dio_dev *d, const struct dio_device_id *ent);
static void hplance_init(struct net_device *dev, struct dio_dev *d);
static void hplance_remove_one(struct dio_dev *d);
static void hplance_writerap(void *priv, unsigned short value);
static void hplance_writerdp(void *priv, unsigned short value);
static unsigned short hplance_readrdp(void *priv);
static int hplance_open(struct net_device *dev);
static int hplance_close(struct net_device *dev);

static struct dio_device_id hplance_dio_tbl[] = {
	{ DIO_ID_LAN },
	{ 0 }
};

static struct dio_driver hplance_driver = {
	.name      = "hplance",
	.id_table  = hplance_dio_tbl,
	.probe     = hplance_init_one,
	.remove    = hplance_remove_one,
};

static const struct net_device_ops hplance_netdev_ops = {
	.ndo_open		= hplance_open,
	.ndo_stop		= hplance_close,
	.ndo_start_xmit		= lance_start_xmit,
	.ndo_set_rx_mode	= lance_set_multicast,
	.ndo_validate_addr	= eth_validate_addr,
	.ndo_set_mac_address	= eth_mac_addr,
#ifdef CONFIG_NET_POLL_CONTROLLER
	.ndo_poll_controller	= lance_poll,
#endif
};

/* Find all the HP Lance boards and initialise them... */
static int hplance_init_one(struct dio_dev *d, const struct dio_device_id *ent)
{
	struct net_device *dev;
	int err = -ENOMEM;

	dev = alloc_etherdev(sizeof(struct hplance_private));
	if (!dev)
		goto out;

	err = -EBUSY;
	if (!request_mem_region(dio_resource_start(d),
				dio_resource_len(d), d->name))
		goto out_free_netdev;

	hplance_init(dev, d);
	err = register_netdev(dev);
	if (err)
		goto out_release_mem_region;

	dio_set_drvdata(d, dev);

	printk(KERN_INFO "%s: %s; select code %d, addr %pM, irq %d\n",
	       dev->name, d->name, d->scode, dev->dev_addr, d->ipl);

	return 0;

 out_release_mem_region:
	release_mem_region(dio_resource_start(d), dio_resource_len(d));
 out_free_netdev:
	free_netdev(dev);
 out:
	return err;
}

static void hplance_remove_one(struct dio_dev *d)
{
	struct net_device *dev = dio_get_drvdata(d);

	unregister_netdev(dev);
	release_mem_region(dio_resource_start(d), dio_resource_len(d));
	free_netdev(dev);
}

/* Initialise a single lance board at the given DIO device */
static void hplance_init(struct net_device *dev, struct dio_dev *d)
{
	unsigned long va = (d->resource.start + DIO_VIRADDRBASE);
	struct hplance_private *lp;
	int i;

	/* reset the board */
	out_8(va + DIO_IDOFF, 0xff);
	udelay(100);                              /* ariba! ariba! udelay! udelay! */

	/* Fill the dev fields */
	dev->base_addr = va;
	dev->netdev_ops = &hplance_netdev_ops;
	dev->dma = 0;

	for (i = 0; i < 6; i++) {
		/* The NVRAM holds our ethernet address, one nibble per byte,
		 * at bytes NVRAMOFF+1,3,5,7,9...
		 */
		dev->dev_addr[i] = ((in_8(va + HPLANCE_NVRAMOFF + i*4 + 1) & 0xF) << 4)
			| (in_8(va + HPLANCE_NVRAMOFF + i*4 + 3) & 0xF);
	}

	lp = netdev_priv(dev);
	lp->lance.name = d->name;
	lp->lance.base = va;
	lp->lance.init_block = (struct lance_init_block *)(va + HPLANCE_MEMOFF); /* CPU addr */
	lp->lance.lance_init_block = NULL;              /* LANCE addr of same RAM */
	lp->lance.busmaster_regval = LE_C3_BSWP;        /* we're bigendian */
	lp->lance.irq = d->ipl;
	lp->lance.writerap = hplance_writerap;
	lp->lance.writerdp = hplance_writerdp;
	lp->lance.readrdp = hplance_readrdp;
	lp->lance.lance_log_rx_bufs = LANCE_LOG_RX_BUFFERS;
	lp->lance.lance_log_tx_bufs = LANCE_LOG_TX_BUFFERS;
	lp->lance.rx_ring_mod_mask = RX_RING_MOD_MASK;
	lp->lance.tx_ring_mod_mask = TX_RING_MOD_MASK;
}

/* This is disgusting. We have to check the DIO status register for ack every
 * time we read or write the LANCE registers.
 */
static void hplance_writerap(void *priv, unsigned short value)
{
	struct lance_private *lp = (struct lance_private *)priv;
	do {
		out_be16(lp->base + HPLANCE_REGOFF + LANCE_RAP, value);
	} while ((in_8(lp->base + HPLANCE_STATUS) & LE_ACK) == 0);
}

static void hplance_writerdp(void *priv, unsigned short value)
{
	struct lance_private *lp = (struct lance_private *)priv;
	do {
		out_be16(lp->base + HPLANCE_REGOFF + LANCE_RDP, value);
	} while ((in_8(lp->base + HPLANCE_STATUS) & LE_ACK) == 0);
}

static unsigned short hplance_readrdp(void *priv)
{
	struct lance_private *lp = (struct lance_private *)priv;
	__u16 value;
	do {
		value = in_be16(lp->base + HPLANCE_REGOFF + LANCE_RDP);
	} while ((in_8(lp->base + HPLANCE_STATUS) & LE_ACK) == 0);
	return value;
}

static int hplance_open(struct net_device *dev)
{
	int status;
	struct lance_private *lp = netdev_priv(dev);

	status = lance_open(dev);                 /* call generic lance open code */
	if (status)
		return status;
	/* enable interrupts at board level. */
	out_8(lp->base + HPLANCE_STATUS, LE_IE);

	return 0;
}

static int hplance_close(struct net_device *dev)
{
	struct lance_private *lp = netdev_priv(dev);

	out_8(lp->base + HPLANCE_STATUS, 0);	/* disable interrupts at boardlevel */
	lance_close(dev);
	return 0;
}

static int __init hplance_init_module(void)
{
	return dio_register_driver(&hplance_driver);
}

static void __exit hplance_cleanup_module(void)
{
	dio_unregister_driver(&hplance_driver);
}

module_init(hplance_init_module);
module_exit(hplance_cleanup_module);

MODULE_LICENSE("GPL");
