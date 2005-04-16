/*
 * Linux ARCnet driver - COM20020 chipset support
 * 
 * Written 1997 by David Woodhouse.
 * Written 1994-1999 by Avery Pennarun.
 * Written 1999-2000 by Martin Mares <mj@ucw.cz>.
 * Derived from skeleton.c by Donald Becker.
 *
 * Special thanks to Contemporary Controls, Inc. (www.ccontrols.com)
 *  for sponsoring the further development of this driver.
 *
 * **********************
 *
 * The original copyright of skeleton.c was as follows:
 *
 * skeleton.c Written 1993 by Donald Becker.
 * Copyright 1993 United States Government as represented by the
 * Director, National Security Agency.  This software may only be used
 * and distributed according to the terms of the GNU General Public License as
 * modified by SRC, incorporated herein by reference.
 *
 * **********************
 *
 * For more details, see drivers/net/arcnet.c
 *
 * **********************
 */
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/ioport.h>
#include <linux/slab.h>
#include <linux/errno.h>
#include <linux/delay.h>
#include <linux/netdevice.h>
#include <linux/init.h>
#include <linux/bootmem.h>
#include <linux/arcdevice.h>
#include <linux/com20020.h>

#include <asm/io.h>

#define VERSION "arcnet: COM20020 ISA support (by David Woodhouse et al.)\n"


/*
 * We cannot (yet) probe for an IO mapped card, although we can check that
 * it's where we were told it was, and even do autoirq.
 */
static int __init com20020isa_probe(struct net_device *dev)
{
	int ioaddr;
	unsigned long airqmask;
	struct arcnet_local *lp = dev->priv;
	int err;

	BUGLVL(D_NORMAL) printk(VERSION);

	ioaddr = dev->base_addr;
	if (!ioaddr) {
		BUGMSG(D_NORMAL, "No autoprobe (yet) for IO mapped cards; you "
		       "must specify the base address!\n");
		return -ENODEV;
	}
	if (!request_region(ioaddr, ARCNET_TOTAL_SIZE, "arcnet (COM20020)")) {
		BUGMSG(D_NORMAL, "IO region %xh-%xh already allocated.\n",
		       ioaddr, ioaddr + ARCNET_TOTAL_SIZE - 1);
		return -ENXIO;
	}
	if (ASTATUS() == 0xFF) {
		BUGMSG(D_NORMAL, "IO address %x empty\n", ioaddr);
		err = -ENODEV;
		goto out;
	}
	if (com20020_check(dev)) {
		err = -ENODEV;
		goto out;
	}

	if (!dev->irq) {
		/* if we do this, we're sure to get an IRQ since the
		 * card has just reset and the NORXflag is on until
		 * we tell it to start receiving.
		 */
		BUGMSG(D_INIT_REASONS, "intmask was %02Xh\n", inb(_INTMASK));
		outb(0, _INTMASK);
		airqmask = probe_irq_on();
		outb(NORXflag, _INTMASK);
		udelay(1);
		outb(0, _INTMASK);
		dev->irq = probe_irq_off(airqmask);

		if (dev->irq <= 0) {
			BUGMSG(D_INIT_REASONS, "Autoprobe IRQ failed first time\n");
			airqmask = probe_irq_on();
			outb(NORXflag, _INTMASK);
			udelay(5);
			outb(0, _INTMASK);
			dev->irq = probe_irq_off(airqmask);
			if (dev->irq <= 0) {
				BUGMSG(D_NORMAL, "Autoprobe IRQ failed.\n");
				err = -ENODEV;
				goto out;
			}
		}
	}

	lp->card_name = "ISA COM20020";
	if ((err = com20020_found(dev, 0)) != 0)
		goto out;

	return 0;

out:
	release_region(ioaddr, ARCNET_TOTAL_SIZE);
	return err;
}

static int node = 0;
static int io = 0x0;		/* <--- EDIT THESE LINES FOR YOUR CONFIGURATION */
static int irq = 0;		/* or use the insmod io= irq= shmem= options */
static char device[9];		/* use eg. device="arc1" to change name */
static int timeout = 3;
static int backplane = 0;
static int clockp = 0;
static int clockm = 0;

module_param(node, int, 0);
module_param(io, int, 0);
module_param(irq, int, 0);
module_param_string(device, device, sizeof(device), 0);
module_param(timeout, int, 0);
module_param(backplane, int, 0);
module_param(clockp, int, 0);
module_param(clockm, int, 0);

MODULE_LICENSE("GPL");

static struct net_device *my_dev;

static int __init com20020_init(void)
{
	struct net_device *dev;
	struct arcnet_local *lp;

	dev = alloc_arcdev(device);
	if (!dev)
		return -ENOMEM;

	if (node && node != 0xff)
		dev->dev_addr[0] = node;

	lp = dev->priv;
	lp->backplane = backplane;
	lp->clockp = clockp & 7;
	lp->clockm = clockm & 3;
	lp->timeout = timeout & 3;
	lp->hw.owner = THIS_MODULE;

	dev->base_addr = io;
	dev->irq = irq;

	if (dev->irq == 2)
		dev->irq = 9;

	if (com20020isa_probe(dev)) {
		free_netdev(dev);
		return -EIO;
	}

	my_dev = dev;
	return 0;
}

static void __exit com20020_exit(void)
{
	unregister_netdev(my_dev);
	free_irq(my_dev->irq, my_dev);
	release_region(my_dev->base_addr, ARCNET_TOTAL_SIZE);
	free_netdev(my_dev);
}

#ifndef MODULE
static int __init com20020isa_setup(char *s)
{
	int ints[8];

	s = get_options(s, 8, ints);
	if (!ints[0])
		return 1;

	switch (ints[0]) {
	default:		/* ERROR */
		printk("com90xx: Too many arguments.\n");
	case 6:		/* Timeout */
		timeout = ints[6];
	case 5:		/* CKP value */
		clockp = ints[5];
	case 4:		/* Backplane flag */
		backplane = ints[4];
	case 3:		/* Node ID */
		node = ints[3];
	case 2:		/* IRQ */
		irq = ints[2];
	case 1:		/* IO address */
		io = ints[1];
	}
	if (*s)
		snprintf(device, sizeof(device), "%s", s);
	return 1;
}

__setup("com20020=", com20020isa_setup);

#endif				/* MODULE */

module_init(com20020_init)
module_exit(com20020_exit)
