/* lasi_82596.c -- driver for the intel 82596 ethernet controller, as
   munged into HPPA boxen .

   This driver is based upon 82596.c, original credits are below...
   but there were too many hoops which HP wants jumped through to
   keep this code in there in a sane manner.

   3 primary sources of the mess --
   1) hppa needs *lots* of cacheline flushing to keep this kind of
   MMIO running.

   2) The 82596 needs to see all of its pointers as their physical
   address.  Thus virt_to_bus/bus_to_virt are *everywhere*.

   3) The implementation HP is using seems to be significantly pickier
   about when and how the command and RX units are started.  some
   command ordering was changed.

   Examination of the mach driver leads one to believe that there
   might be a saner way to pull this off...  anyone who feels like a
   full rewrite can be my guest.

   Split 02/13/2000 Sam Creasey (sammy@oh.verio.com)

   02/01/2000  Initial modifications for parisc by Helge Deller (deller@gmx.de)
   03/02/2000  changes for better/correct(?) cache-flushing (deller)
*/

/* 82596.c: A generic 82596 ethernet driver for linux. */
/*
   Based on Apricot.c
   Written 1994 by Mark Evans.
   This driver is for the Apricot 82596 bus-master interface

   Modularised 12/94 Mark Evans


   Modified to support the 82596 ethernet chips on 680x0 VME boards.
   by Richard Hirst <richard@sleepie.demon.co.uk>
   Renamed to be 82596.c

   980825:  Changed to receive directly in to sk_buffs which are
   allocated at open() time.  Eliminates copy on incoming frames
   (small ones are still copied).  Shared data now held in a
   non-cached page, so we can run on 68060 in copyback mode.

   TBD:
   * look at deferring rx frames rather than discarding (as per tulip)
   * handle tx ring full as per tulip
   * performance test to tune rx_copybreak

   Most of my modifications relate to the braindead big-endian
   implementation by Intel.  When the i596 is operating in
   'big-endian' mode, it thinks a 32 bit value of 0x12345678
   should be stored as 0x56781234.  This is a real pain, when
   you have linked lists which are shared by the 680x0 and the
   i596.

   Driver skeleton
   Written 1993 by Donald Becker.
   Copyright 1993 United States Government as represented by the Director,
   National Security Agency. This software may only be used and distributed
   according to the terms of the GNU General Public License as modified by SRC,
   incorporated herein by reference.

   The author may be reached as becker@scyld.com, or C/O
   Scyld Computing Corporation, 410 Severn Ave., Suite 210, Annapolis MD 21403

 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/ptrace.h>
#include <linux/errno.h>
#include <linux/ioport.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include <linux/types.h>
#include <linux/bitops.h>
#include <linux/dma-mapping.h>

#include <asm/io.h>
#include <asm/irq.h>
#include <asm/pdc.h>
#include <asm/parisc-device.h>

#define LASI_82596_DRIVER_VERSION "LASI 82596 driver - Revision: 1.30"

#define PA_I82596_RESET		0	/* Offsets relative to LASI-LAN-Addr.*/
#define PA_CPU_PORT_L_ACCESS	4
#define PA_CHANNEL_ATTENTION	8

#define OPT_SWAP_PORT	0x0001	/* Need to wordswp on the MPU port */

#define LIB82596_DMA_ATTR	DMA_ATTR_NON_CONSISTENT

#define DMA_WBACK(ndev, addr, len) \
	do { dma_cache_sync((ndev)->dev.parent, (void *)addr, len, DMA_TO_DEVICE); } while (0)

#define DMA_INV(ndev, addr, len) \
	do { dma_cache_sync((ndev)->dev.parent, (void *)addr, len, DMA_FROM_DEVICE); } while (0)

#define DMA_WBACK_INV(ndev, addr, len) \
	do { dma_cache_sync((ndev)->dev.parent, (void *)addr, len, DMA_BIDIRECTIONAL); } while (0)

#define SYSBUS      0x0000006c;

/* big endian CPU, 82596 "big" endian mode */
#define SWAP32(x)   (((u32)(x)<<16) | ((((u32)(x)))>>16))
#define SWAP16(x)   (x)

#include "lib82596.c"

MODULE_AUTHOR("Richard Hirst");
MODULE_DESCRIPTION("i82596 driver");
MODULE_LICENSE("GPL");
module_param(i596_debug, int, 0);
MODULE_PARM_DESC(i596_debug, "lasi_82596 debug mask");

static inline void ca(struct net_device *dev)
{
	gsc_writel(0, dev->base_addr + PA_CHANNEL_ATTENTION);
}


static void mpu_port(struct net_device *dev, int c, dma_addr_t x)
{
	struct i596_private *lp = netdev_priv(dev);

	u32 v = (u32) (c) | (u32) (x);
	u16 a, b;

	if (lp->options & OPT_SWAP_PORT) {
		a = v >> 16;
		b = v & 0xffff;
	} else {
		a = v & 0xffff;
		b = v >> 16;
	}

	gsc_writel(a, dev->base_addr + PA_CPU_PORT_L_ACCESS);
	udelay(1);
	gsc_writel(b, dev->base_addr + PA_CPU_PORT_L_ACCESS);
}

#define LAN_PROM_ADDR	0xF0810000

static int __init
lan_init_chip(struct parisc_device *dev)
{
	struct	net_device *netdevice;
	struct i596_private *lp;
	int	retval;
	int i;

	if (!dev->irq) {
		printk(KERN_ERR "%s: IRQ not found for i82596 at 0x%lx\n",
			__FILE__, (unsigned long)dev->hpa.start);
		return -ENODEV;
	}

	printk(KERN_INFO "Found i82596 at 0x%lx, IRQ %d\n",
			(unsigned long)dev->hpa.start, dev->irq);

	netdevice = alloc_etherdev(sizeof(struct i596_private));
	if (!netdevice)
		return -ENOMEM;
	SET_NETDEV_DEV(netdevice, &dev->dev);
	parisc_set_drvdata (dev, netdevice);

	netdevice->base_addr = dev->hpa.start;
	netdevice->irq = dev->irq;

	if (pdc_lan_station_id(netdevice->dev_addr, netdevice->base_addr)) {
		for (i = 0; i < 6; i++) {
			netdevice->dev_addr[i] = gsc_readb(LAN_PROM_ADDR + i);
		}
		printk(KERN_INFO
		       "%s: MAC of HP700 LAN read from EEPROM\n", __FILE__);
	}

	lp = netdev_priv(netdevice);
	lp->options = dev->id.sversion == 0x72 ? OPT_SWAP_PORT : 0;

	retval = i82596_probe(netdevice);
	if (retval) {
		free_netdev(netdevice);
		return -ENODEV;
	}
	return retval;
}

static int __exit lan_remove_chip(struct parisc_device *pdev)
{
	struct net_device *dev = parisc_get_drvdata(pdev);
	struct i596_private *lp = netdev_priv(dev);

	unregister_netdev (dev);
	dma_free_attrs(&pdev->dev, sizeof(struct i596_private), lp->dma,
		       lp->dma_addr, LIB82596_DMA_ATTR);
	free_netdev (dev);
	return 0;
}

static const struct parisc_device_id lan_tbl[] __initconst = {
	{ HPHW_FIO, HVERSION_REV_ANY_ID, HVERSION_ANY_ID, 0x0008a },
	{ HPHW_FIO, HVERSION_REV_ANY_ID, HVERSION_ANY_ID, 0x00072 },
	{ 0, }
};

MODULE_DEVICE_TABLE(parisc, lan_tbl);

static struct parisc_driver lan_driver __refdata = {
	.name		= "lasi_82596",
	.id_table	= lan_tbl,
	.probe		= lan_init_chip,
	.remove         = __exit_p(lan_remove_chip),
};

static int lasi_82596_init(void)
{
	printk(KERN_INFO LASI_82596_DRIVER_VERSION "\n");
	return register_parisc_driver(&lan_driver);
}

module_init(lasi_82596_init);

static void __exit lasi_82596_exit(void)
{
	unregister_parisc_driver(&lan_driver);
}

module_exit(lasi_82596_exit);
