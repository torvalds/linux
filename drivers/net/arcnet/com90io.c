/*
 * Linux ARCnet driver - COM90xx chipset (IO-mapped buffers)
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

#define pr_fmt(fmt) "arcnet:" KBUILD_MODNAME ": " fmt

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/ioport.h>
#include <linux/delay.h>
#include <linux/netdevice.h>
#include <linux/memblock.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/io.h>

#include "arcdevice.h"
#include "com9026.h"

/* Internal function declarations */

static int com90io_found(struct net_device *dev);
static void com90io_command(struct net_device *dev, int command);
static int com90io_status(struct net_device *dev);
static void com90io_setmask(struct net_device *dev, int mask);
static int com90io_reset(struct net_device *dev, int really_reset);
static void com90io_copy_to_card(struct net_device *dev, int bufnum, int offset,
				 void *buf, int count);
static void com90io_copy_from_card(struct net_device *dev, int bufnum,
				   int offset, void *buf, int count);

/* Handy defines for ARCnet specific stuff */

/* The number of low I/O ports used by the card. */
#define ARCNET_TOTAL_SIZE 16

/****************************************************************************
 *                                                                          *
 * IO-mapped operation routines                                             *
 *                                                                          *
 ****************************************************************************/

#undef ONE_AT_A_TIME_TX
#undef ONE_AT_A_TIME_RX

static u_char get_buffer_byte(struct net_device *dev, unsigned offset)
{
	int ioaddr = dev->base_addr;

	arcnet_outb(offset >> 8, ioaddr, COM9026_REG_W_ADDR_HI);
	arcnet_outb(offset & 0xff, ioaddr, COM9026_REG_W_ADDR_LO);

	return arcnet_inb(ioaddr, COM9026_REG_RW_MEMDATA);
}

#ifdef ONE_AT_A_TIME_TX
static void put_buffer_byte(struct net_device *dev, unsigned offset,
			    u_char datum)
{
	int ioaddr = dev->base_addr;

	arcnet_outb(offset >> 8, ioaddr, COM9026_REG_W_ADDR_HI);
	arcnet_outb(offset & 0xff, ioaddr, COM9026_REG_W_ADDR_LO);

	arcnet_outb(datum, ioaddr, COM9026_REG_RW_MEMDATA);
}

#endif

static void get_whole_buffer(struct net_device *dev, unsigned offset,
			     unsigned length, char *dest)
{
	int ioaddr = dev->base_addr;

	arcnet_outb((offset >> 8) | AUTOINCflag, ioaddr, COM9026_REG_W_ADDR_HI);
	arcnet_outb(offset & 0xff, ioaddr, COM9026_REG_W_ADDR_LO);

	while (length--)
#ifdef ONE_AT_A_TIME_RX
		*(dest++) = get_buffer_byte(dev, offset++);
#else
		*(dest++) = arcnet_inb(ioaddr, COM9026_REG_RW_MEMDATA);
#endif
}

static void put_whole_buffer(struct net_device *dev, unsigned offset,
			     unsigned length, char *dest)
{
	int ioaddr = dev->base_addr;

	arcnet_outb((offset >> 8) | AUTOINCflag, ioaddr, COM9026_REG_W_ADDR_HI);
	arcnet_outb(offset & 0xff, ioaddr,COM9026_REG_W_ADDR_LO);

	while (length--)
#ifdef ONE_AT_A_TIME_TX
		put_buffer_byte(dev, offset++, *(dest++));
#else
		arcnet_outb(*(dest++), ioaddr, COM9026_REG_RW_MEMDATA);
#endif
}

/* We cannot probe for an IO mapped card either, although we can check that
 * it's where we were told it was, and even autoirq
 */
static int __init com90io_probe(struct net_device *dev)
{
	int ioaddr = dev->base_addr, status;
	unsigned long airqmask;

	if (BUGLVL(D_NORMAL)) {
		pr_info("%s\n", "COM90xx IO-mapped mode support (by David Woodhouse et el.)");
		pr_info("E-mail me if you actually test this driver, please!\n");
	}

	if (!ioaddr) {
		arc_printk(D_NORMAL, dev, "No autoprobe for IO mapped cards; you must specify the base address!\n");
		return -ENODEV;
	}
	if (!request_region(ioaddr, ARCNET_TOTAL_SIZE, "com90io probe")) {
		arc_printk(D_INIT_REASONS, dev, "IO request_region %x-%x failed\n",
			   ioaddr, ioaddr + ARCNET_TOTAL_SIZE - 1);
		return -ENXIO;
	}
	if (arcnet_inb(ioaddr, COM9026_REG_R_STATUS) == 0xFF) {
		arc_printk(D_INIT_REASONS, dev, "IO address %x empty\n",
			   ioaddr);
		goto err_out;
	}
	arcnet_inb(ioaddr, COM9026_REG_R_RESET);
	mdelay(RESETtime);

	status = arcnet_inb(ioaddr, COM9026_REG_R_STATUS);

	if ((status & 0x9D) != (NORXflag | RECONflag | TXFREEflag | RESETflag)) {
		arc_printk(D_INIT_REASONS, dev, "Status invalid (%Xh)\n",
			   status);
		goto err_out;
	}
	arc_printk(D_INIT_REASONS, dev, "Status after reset: %X\n", status);

	arcnet_outb(CFLAGScmd | RESETclear | CONFIGclear,
		    ioaddr, COM9026_REG_W_COMMAND);

	arc_printk(D_INIT_REASONS, dev, "Status after reset acknowledged: %X\n",
		   status);

	status = arcnet_inb(ioaddr, COM9026_REG_R_STATUS);

	if (status & RESETflag) {
		arc_printk(D_INIT_REASONS, dev, "Eternal reset (status=%Xh)\n",
			   status);
		goto err_out;
	}
	arcnet_outb((0x16 | IOMAPflag) & ~ENABLE16flag,
		    ioaddr, COM9026_REG_RW_CONFIG);

	/* Read first loc'n of memory */

	arcnet_outb(AUTOINCflag, ioaddr, COM9026_REG_W_ADDR_HI);
	arcnet_outb(0, ioaddr,  COM9026_REG_W_ADDR_LO);

	status = arcnet_inb(ioaddr, COM9026_REG_RW_MEMDATA);
	if (status != 0xd1) {
		arc_printk(D_INIT_REASONS, dev, "Signature byte not found (%Xh instead).\n",
			   status);
		goto err_out;
	}
	if (!dev->irq) {
		/* if we do this, we're sure to get an IRQ since the
		 * card has just reset and the NORXflag is on until
		 * we tell it to start receiving.
		 */

		airqmask = probe_irq_on();
		arcnet_outb(NORXflag, ioaddr, COM9026_REG_W_INTMASK);
		udelay(1);
		arcnet_outb(0, ioaddr, COM9026_REG_W_INTMASK);
		dev->irq = probe_irq_off(airqmask);

		if ((int)dev->irq <= 0) {
			arc_printk(D_INIT_REASONS, dev, "Autoprobe IRQ failed\n");
			goto err_out;
		}
	}
	release_region(ioaddr, ARCNET_TOTAL_SIZE); /* end of probing */
	return com90io_found(dev);

err_out:
	release_region(ioaddr, ARCNET_TOTAL_SIZE);
	return -ENODEV;
}

/* Set up the struct net_device associated with this card.  Called after
 * probing succeeds.
 */
static int __init com90io_found(struct net_device *dev)
{
	struct arcnet_local *lp;
	int ioaddr = dev->base_addr;
	int err;

	/* Reserve the irq */
	if (request_irq(dev->irq, arcnet_interrupt, 0,
			"arcnet (COM90xx-IO)", dev)) {
		arc_printk(D_NORMAL, dev, "Can't get IRQ %d!\n", dev->irq);
		return -ENODEV;
	}
	/* Reserve the I/O region */
	if (!request_region(dev->base_addr, ARCNET_TOTAL_SIZE,
			    "arcnet (COM90xx-IO)")) {
		free_irq(dev->irq, dev);
		return -EBUSY;
	}

	lp = netdev_priv(dev);
	lp->card_name = "COM90xx I/O";
	lp->hw.command = com90io_command;
	lp->hw.status = com90io_status;
	lp->hw.intmask = com90io_setmask;
	lp->hw.reset = com90io_reset;
	lp->hw.owner = THIS_MODULE;
	lp->hw.copy_to_card = com90io_copy_to_card;
	lp->hw.copy_from_card = com90io_copy_from_card;

	lp->config = (0x16 | IOMAPflag) & ~ENABLE16flag;
	arcnet_outb(lp->config, ioaddr, COM9026_REG_RW_CONFIG);

	/* get and check the station ID from offset 1 in shmem */

	arcnet_set_addr(dev, get_buffer_byte(dev, 1));

	err = register_netdev(dev);
	if (err) {
		arcnet_outb(arcnet_inb(ioaddr, COM9026_REG_RW_CONFIG) & ~IOMAPflag,
			    ioaddr, COM9026_REG_RW_CONFIG);
		free_irq(dev->irq, dev);
		release_region(dev->base_addr, ARCNET_TOTAL_SIZE);
		return err;
	}

	arc_printk(D_NORMAL, dev, "COM90IO: station %02Xh found at %03lXh, IRQ %d.\n",
		   dev->dev_addr[0], dev->base_addr, dev->irq);

	return 0;
}

/* Do a hardware reset on the card, and set up necessary registers.
 *
 * This should be called as little as possible, because it disrupts the
 * token on the network (causes a RECON) and requires a significant delay.
 *
 * However, it does make sure the card is in a defined state.
 */
static int com90io_reset(struct net_device *dev, int really_reset)
{
	struct arcnet_local *lp = netdev_priv(dev);
	short ioaddr = dev->base_addr;

	arc_printk(D_INIT, dev, "Resetting %s (status=%02Xh)\n",
		   dev->name, arcnet_inb(ioaddr, COM9026_REG_R_STATUS));

	if (really_reset) {
		/* reset the card */
		arcnet_inb(ioaddr, COM9026_REG_R_RESET);
		mdelay(RESETtime);
	}
	/* Set the thing to IO-mapped, 8-bit  mode */
	lp->config = (0x1C | IOMAPflag) & ~ENABLE16flag;
	arcnet_outb(lp->config, ioaddr, COM9026_REG_RW_CONFIG);

	arcnet_outb(CFLAGScmd | RESETclear, ioaddr, COM9026_REG_W_COMMAND);
					/* clear flags & end reset */
	arcnet_outb(CFLAGScmd | CONFIGclear, ioaddr, COM9026_REG_W_COMMAND);

	/* verify that the ARCnet signature byte is present */
	if (get_buffer_byte(dev, 0) != TESTvalue) {
		arc_printk(D_NORMAL, dev, "reset failed: TESTvalue not present.\n");
		return 1;
	}
	/* enable extended (512-byte) packets */
	arcnet_outb(CONFIGcmd | EXTconf, ioaddr, COM9026_REG_W_COMMAND);
	/* done!  return success. */
	return 0;
}

static void com90io_command(struct net_device *dev, int cmd)
{
	short ioaddr = dev->base_addr;

	arcnet_outb(cmd, ioaddr, COM9026_REG_W_COMMAND);
}

static int com90io_status(struct net_device *dev)
{
	short ioaddr = dev->base_addr;

	return arcnet_inb(ioaddr, COM9026_REG_R_STATUS);
}

static void com90io_setmask(struct net_device *dev, int mask)
{
	short ioaddr = dev->base_addr;

	arcnet_outb(mask, ioaddr, COM9026_REG_W_INTMASK);
}

static void com90io_copy_to_card(struct net_device *dev, int bufnum,
				 int offset, void *buf, int count)
{
	TIME(dev, "put_whole_buffer", count,
	     put_whole_buffer(dev, bufnum * 512 + offset, count, buf));
}

static void com90io_copy_from_card(struct net_device *dev, int bufnum,
				   int offset, void *buf, int count)
{
	TIME(dev, "get_whole_buffer", count,
	     get_whole_buffer(dev, bufnum * 512 + offset, count, buf));
}

static int io;			/* use the insmod io= irq= shmem= options */
static int irq;
static char device[9];		/* use eg. device=arc1 to change name */

module_param_hw(io, int, ioport, 0);
module_param_hw(irq, int, irq, 0);
module_param_string(device, device, sizeof(device), 0);
MODULE_LICENSE("GPL");

#ifndef MODULE
static int __init com90io_setup(char *s)
{
	int ints[4];

	s = get_options(s, 4, ints);
	if (!ints[0])
		return 0;
	switch (ints[0]) {
	default:		/* ERROR */
		pr_err("Too many arguments\n");
		fallthrough;
	case 2:		/* IRQ */
		irq = ints[2];
		fallthrough;
	case 1:		/* IO address */
		io = ints[1];
	}
	if (*s)
		snprintf(device, sizeof(device), "%s", s);
	return 1;
}
__setup("com90io=", com90io_setup);
#endif

static struct net_device *my_dev;

static int __init com90io_init(void)
{
	struct net_device *dev;
	int err;

	dev = alloc_arcdev(device);
	if (!dev)
		return -ENOMEM;

	dev->base_addr = io;
	dev->irq = irq;
	if (dev->irq == 2)
		dev->irq = 9;

	err = com90io_probe(dev);

	if (err) {
		free_arcdev(dev);
		return err;
	}

	my_dev = dev;
	return 0;
}

static void __exit com90io_exit(void)
{
	struct net_device *dev = my_dev;
	int ioaddr = dev->base_addr;

	unregister_netdev(dev);

	/* In case the old driver is loaded later,
	 * set the thing back to MMAP mode
	 */
	arcnet_outb(arcnet_inb(ioaddr, COM9026_REG_RW_CONFIG) & ~IOMAPflag,
		    ioaddr, COM9026_REG_RW_CONFIG);

	free_irq(dev->irq, dev);
	release_region(dev->base_addr, ARCNET_TOTAL_SIZE);
	free_arcdev(dev);
}

module_init(com90io_init)
module_exit(com90io_exit)
