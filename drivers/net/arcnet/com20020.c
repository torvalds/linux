/*
 * Linux ARCnet driver - COM20020 chipset support
 * 
 * Written 1997 by David Woodhouse.
 * Written 1994-1999 by Avery Pennarun.
 * Written 1999 by Martin Mares <mj@ucw.cz>.
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
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/ioport.h>
#include <linux/slab.h>
#include <linux/errno.h>
#include <linux/delay.h>
#include <linux/netdevice.h>
#include <linux/init.h>
#include <linux/arcdevice.h>
#include <linux/com20020.h>

#include <asm/io.h>

#define VERSION "arcnet: COM20020 chipset support (by David Woodhouse et al.)\n"

static char *clockrates[] =
{"10 Mb/s", "Reserved", "5 Mb/s",
 "2.5 Mb/s", "1.25Mb/s", "625 Kb/s", "312.5 Kb/s",
 "156.25 Kb/s", "Reserved", "Reserved", "Reserved"};

static void com20020_command(struct net_device *dev, int command);
static int com20020_status(struct net_device *dev);
static void com20020_setmask(struct net_device *dev, int mask);
static int com20020_reset(struct net_device *dev, int really_reset);
static void com20020_copy_to_card(struct net_device *dev, int bufnum,
				  int offset, void *buf, int count);
static void com20020_copy_from_card(struct net_device *dev, int bufnum,
				    int offset, void *buf, int count);
static void com20020_set_mc_list(struct net_device *dev);
static void com20020_close(struct net_device *);

static void com20020_copy_from_card(struct net_device *dev, int bufnum,
				    int offset, void *buf, int count)
{
	int ioaddr = dev->base_addr, ofs = 512 * bufnum + offset;

	/* set up the address register */
	outb((ofs >> 8) | RDDATAflag | AUTOINCflag, _ADDR_HI);
	outb(ofs & 0xff, _ADDR_LO);

	/* copy the data */
	TIME("insb", count, insb(_MEMDATA, buf, count));
}


static void com20020_copy_to_card(struct net_device *dev, int bufnum,
				  int offset, void *buf, int count)
{
	int ioaddr = dev->base_addr, ofs = 512 * bufnum + offset;

	/* set up the address register */
	outb((ofs >> 8) | AUTOINCflag, _ADDR_HI);
	outb(ofs & 0xff, _ADDR_LO);

	/* copy the data */
	TIME("outsb", count, outsb(_MEMDATA, buf, count));
}


/* Reset the card and check some basic stuff during the detection stage. */
int com20020_check(struct net_device *dev)
{
	int ioaddr = dev->base_addr, status;
	struct arcnet_local *lp = netdev_priv(dev);

	ARCRESET0;
	mdelay(RESETtime);

	lp->setup = lp->clockm ? 0 : (lp->clockp << 1);
	lp->setup2 = (lp->clockm << 4) | 8;

	/* CHECK: should we do this for SOHARD cards ? */
	/* Enable P1Mode for backplane mode */
	lp->setup = lp->setup | P1MODE;

	SET_SUBADR(SUB_SETUP1);
	outb(lp->setup, _XREG);

	if (lp->clockm != 0)
	{
		SET_SUBADR(SUB_SETUP2);
		outb(lp->setup2, _XREG);
	
		/* must now write the magic "restart operation" command */
		mdelay(1);
		outb(0x18, _COMMAND);
	}

	lp->config = 0x21 | (lp->timeout << 3) | (lp->backplane << 2);
	/* set node ID to 0x42 (but transmitter is disabled, so it's okay) */
	SETCONF;
	outb(0x42, ioaddr + BUS_ALIGN*7);

	status = ASTATUS();

	if ((status & 0x99) != (NORXflag | TXFREEflag | RESETflag)) {
		BUGMSG(D_NORMAL, "status invalid (%Xh).\n", status);
		return -ENODEV;
	}
	BUGMSG(D_INIT_REASONS, "status after reset: %X\n", status);

	/* Enable TX */
	outb(0x39, _CONFIG);
	outb(inb(ioaddr + BUS_ALIGN*8), ioaddr + BUS_ALIGN*7);

	ACOMMAND(CFLAGScmd | RESETclear | CONFIGclear);

	status = ASTATUS();
	BUGMSG(D_INIT_REASONS, "status after reset acknowledged: %X\n",
	       status);

	/* Read first location of memory */
	outb(0 | RDDATAflag | AUTOINCflag, _ADDR_HI);
	outb(0, _ADDR_LO);

	if ((status = inb(_MEMDATA)) != TESTvalue) {
		BUGMSG(D_NORMAL, "Signature byte not found (%02Xh != D1h).\n",
		       status);
		return -ENODEV;
	}
	return 0;
}

/* Set up the struct net_device associated with this card.  Called after
 * probing succeeds.
 */
int com20020_found(struct net_device *dev, int shared)
{
	struct arcnet_local *lp;
	int ioaddr = dev->base_addr;

	/* Initialize the rest of the device structure. */

	lp = netdev_priv(dev);

	lp->hw.owner = THIS_MODULE;
	lp->hw.command = com20020_command;
	lp->hw.status = com20020_status;
	lp->hw.intmask = com20020_setmask;
	lp->hw.reset = com20020_reset;
	lp->hw.copy_to_card = com20020_copy_to_card;
	lp->hw.copy_from_card = com20020_copy_from_card;
	lp->hw.close = com20020_close;

	dev->set_multicast_list = com20020_set_mc_list;

	if (!dev->dev_addr[0])
		dev->dev_addr[0] = inb(ioaddr + BUS_ALIGN*8);	/* FIXME: do this some other way! */

	SET_SUBADR(SUB_SETUP1);
	outb(lp->setup, _XREG);

	if (lp->card_flags & ARC_CAN_10MBIT)
	{
		SET_SUBADR(SUB_SETUP2);
		outb(lp->setup2, _XREG);
	
		/* must now write the magic "restart operation" command */
		mdelay(1);
		outb(0x18, _COMMAND);
	}

	lp->config = 0x20 | (lp->timeout << 3) | (lp->backplane << 2) | 1;
	/* Default 0x38 + register: Node ID */
	SETCONF;
	outb(dev->dev_addr[0], _XREG);

	/* reserve the irq */
	if (request_irq(dev->irq, &arcnet_interrupt, shared,
			"arcnet (COM20020)", dev)) {
		BUGMSG(D_NORMAL, "Can't get IRQ %d!\n", dev->irq);
		return -ENODEV;
	}

	dev->base_addr = ioaddr;

	BUGMSG(D_NORMAL, "%s: station %02Xh found at %03lXh, IRQ %d.\n",
	       lp->card_name, dev->dev_addr[0], dev->base_addr, dev->irq);

	if (lp->backplane)
		BUGMSG(D_NORMAL, "Using backplane mode.\n");

	if (lp->timeout != 3)
		BUGMSG(D_NORMAL, "Using extended timeout value of %d.\n", lp->timeout);

	BUGMSG(D_NORMAL, "Using CKP %d - data rate %s.\n",
	       lp->setup >> 1, 
	       clockrates[3 - ((lp->setup2 & 0xF0) >> 4) + ((lp->setup & 0x0F) >> 1)]);

	if (register_netdev(dev)) {
		free_irq(dev->irq, dev);
		return -EIO;
	}
	return 0;
}


/* 
 * Do a hardware reset on the card, and set up necessary registers.
 * 
 * This should be called as little as possible, because it disrupts the
 * token on the network (causes a RECON) and requires a significant delay.
 *
 * However, it does make sure the card is in a defined state.
 */
static int com20020_reset(struct net_device *dev, int really_reset)
{
	struct arcnet_local *lp = netdev_priv(dev);
	u_int ioaddr = dev->base_addr;
	u_char inbyte;

	BUGMSG(D_DEBUG, "%s: %d: %s: dev: %p, lp: %p, dev->name: %s\n",
		__FILE__,__LINE__,__func__,dev,lp,dev->name);
	BUGMSG(D_INIT, "Resetting %s (status=%02Xh)\n",
	       dev->name, ASTATUS());

	BUGMSG(D_DEBUG, "%s: %d: %s\n",__FILE__,__LINE__,__func__);
	lp->config = TXENcfg | (lp->timeout << 3) | (lp->backplane << 2);
	/* power-up defaults */
	SETCONF;
	BUGMSG(D_DEBUG, "%s: %d: %s\n",__FILE__,__LINE__,__func__);

	if (really_reset) {
		/* reset the card */
		ARCRESET;
		mdelay(RESETtime * 2);	/* COM20020 seems to be slower sometimes */
	}
	/* clear flags & end reset */
	BUGMSG(D_DEBUG, "%s: %d: %s\n",__FILE__,__LINE__,__func__);
	ACOMMAND(CFLAGScmd | RESETclear | CONFIGclear);

	/* verify that the ARCnet signature byte is present */
	BUGMSG(D_DEBUG, "%s: %d: %s\n",__FILE__,__LINE__,__func__);

	com20020_copy_from_card(dev, 0, 0, &inbyte, 1);
	BUGMSG(D_DEBUG, "%s: %d: %s\n",__FILE__,__LINE__,__func__);
	if (inbyte != TESTvalue) {
		BUGMSG(D_DEBUG, "%s: %d: %s\n",__FILE__,__LINE__,__func__);
		BUGMSG(D_NORMAL, "reset failed: TESTvalue not present.\n");
		return 1;
	}
	/* enable extended (512-byte) packets */
	ACOMMAND(CONFIGcmd | EXTconf);
	BUGMSG(D_DEBUG, "%s: %d: %s\n",__FILE__,__LINE__,__func__);

	/* done!  return success. */
	return 0;
}


static void com20020_setmask(struct net_device *dev, int mask)
{
	u_int ioaddr = dev->base_addr;
	BUGMSG(D_DURING, "Setting mask to %x at %x\n",mask,ioaddr);
	AINTMASK(mask);
}


static void com20020_command(struct net_device *dev, int cmd)
{
	u_int ioaddr = dev->base_addr;
	ACOMMAND(cmd);
}


static int com20020_status(struct net_device *dev)
{
	u_int ioaddr = dev->base_addr;

	return ASTATUS() + (ADIAGSTATUS()<<8);
}

static void com20020_close(struct net_device *dev)
{
	struct arcnet_local *lp = netdev_priv(dev);
	int ioaddr = dev->base_addr;

	/* disable transmitter */
	lp->config &= ~TXENcfg;
	SETCONF;
}

/* Set or clear the multicast filter for this adaptor.
 * num_addrs == -1    Promiscuous mode, receive all packets
 * num_addrs == 0       Normal mode, clear multicast list
 * num_addrs > 0        Multicast mode, receive normal and MC packets, and do
 *                      best-effort filtering.
 *      FIXME - do multicast stuff, not just promiscuous.
 */
static void com20020_set_mc_list(struct net_device *dev)
{
	struct arcnet_local *lp = netdev_priv(dev);
	int ioaddr = dev->base_addr;

	if ((dev->flags & IFF_PROMISC) && (dev->flags & IFF_UP)) {	/* Enable promiscuous mode */
		if (!(lp->setup & PROMISCset))
			BUGMSG(D_NORMAL, "Setting promiscuous flag...\n");
		SET_SUBADR(SUB_SETUP1);
		lp->setup |= PROMISCset;
		outb(lp->setup, _XREG);
	} else
		/* Disable promiscuous mode, use normal mode */
	{
		if ((lp->setup & PROMISCset))
			BUGMSG(D_NORMAL, "Resetting promiscuous flag...\n");
		SET_SUBADR(SUB_SETUP1);
		lp->setup &= ~PROMISCset;
		outb(lp->setup, _XREG);
	}
}

#if defined(CONFIG_ARCNET_COM20020_PCI_MODULE) || \
    defined(CONFIG_ARCNET_COM20020_ISA_MODULE) || \
    defined(CONFIG_ARCNET_COM20020_CS_MODULE)
EXPORT_SYMBOL(com20020_check);
EXPORT_SYMBOL(com20020_found);
#endif

MODULE_LICENSE("GPL");

#ifdef MODULE

static int __init com20020_module_init(void)
{
	BUGLVL(D_NORMAL) printk(VERSION);
	return 0;
}

static void __exit com20020_module_exit(void)
{
}
module_init(com20020_module_init);
module_exit(com20020_module_exit);
#endif				/* MODULE */
