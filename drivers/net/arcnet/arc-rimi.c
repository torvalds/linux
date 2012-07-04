/*
 * Linux ARCnet driver - "RIM I" (entirely mem-mapped) cards
 * 
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
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/ioport.h>
#include <linux/delay.h>
#include <linux/netdevice.h>
#include <linux/bootmem.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <asm/io.h>
#include <linux/arcdevice.h>


#define VERSION "arcnet: RIM I (entirely mem-mapped) support\n"


/* Internal function declarations */

static int arcrimi_probe(struct net_device *dev);
static int arcrimi_found(struct net_device *dev);
static void arcrimi_command(struct net_device *dev, int command);
static int arcrimi_status(struct net_device *dev);
static void arcrimi_setmask(struct net_device *dev, int mask);
static int arcrimi_reset(struct net_device *dev, int really_reset);
static void arcrimi_copy_to_card(struct net_device *dev, int bufnum, int offset,
				 void *buf, int count);
static void arcrimi_copy_from_card(struct net_device *dev, int bufnum, int offset,
				   void *buf, int count);

/* Handy defines for ARCnet specific stuff */

/* Amount of I/O memory used by the card */
#define BUFFER_SIZE (512)
#define MIRROR_SIZE (BUFFER_SIZE*4)

/* COM 9026 controller chip --> ARCnet register addresses */
#define _INTMASK (ioaddr+0)	/* writable */
#define _STATUS  (ioaddr+0)	/* readable */
#define _COMMAND (ioaddr+1)	/* writable, returns random vals on read (?) */
#define _RESET  (ioaddr+8)	/* software reset (on read) */
#define _MEMDATA  (ioaddr+12)	/* Data port for IO-mapped memory */
#define _ADDR_HI  (ioaddr+15)	/* Control registers for said */
#define _ADDR_LO  (ioaddr+14)
#define _CONFIG  (ioaddr+2)	/* Configuration register */

#undef ASTATUS
#undef ACOMMAND
#undef AINTMASK

#define ASTATUS()	readb(_STATUS)
#define ACOMMAND(cmd)	writeb((cmd),_COMMAND)
#define AINTMASK(msk)	writeb((msk),_INTMASK)
#define SETCONF()	writeb(lp->config,_CONFIG)


/*
 * We cannot probe for a RIM I card; one reason is I don't know how to reset
 * them.  In fact, we can't even get their node ID automatically.  So, we
 * need to be passed a specific shmem address, IRQ, and node ID.
 */
static int __init arcrimi_probe(struct net_device *dev)
{
	BUGLVL(D_NORMAL) printk(VERSION);
	BUGLVL(D_NORMAL) printk("E-mail me if you actually test the RIM I driver, please!\n");

	BUGLVL(D_NORMAL) printk("Given: node %02Xh, shmem %lXh, irq %d\n",
	       dev->dev_addr[0], dev->mem_start, dev->irq);

	if (dev->mem_start <= 0 || dev->irq <= 0) {
		BUGLVL(D_NORMAL) printk("No autoprobe for RIM I; you "
		       "must specify the shmem and irq!\n");
		return -ENODEV;
	}
	if (dev->dev_addr[0] == 0) {
		BUGLVL(D_NORMAL) printk("You need to specify your card's station "
		       "ID!\n");
		return -ENODEV;
	}
	/*
	 * Grab the memory region at mem_start for MIRROR_SIZE bytes.
	 * Later in arcrimi_found() the real size will be determined
	 * and this reserve will be released and the correct size
	 * will be taken.
	 */
	if (!request_mem_region(dev->mem_start, MIRROR_SIZE, "arcnet (90xx)")) {
		BUGLVL(D_NORMAL) printk("Card memory already allocated\n");
		return -ENODEV;
	}
	return arcrimi_found(dev);
}

static int check_mirror(unsigned long addr, size_t size)
{
	void __iomem *p;
	int res = -1;

	if (!request_mem_region(addr, size, "arcnet (90xx)"))
		return -1;

	p = ioremap(addr, size);
	if (p) {
		if (readb(p) == TESTvalue)
			res = 1;
		else
			res = 0;
		iounmap(p);
	}

	release_mem_region(addr, size);
	return res;
}

/*
 * Set up the struct net_device associated with this card.  Called after
 * probing succeeds.
 */
static int __init arcrimi_found(struct net_device *dev)
{
	struct arcnet_local *lp;
	unsigned long first_mirror, last_mirror, shmem;
	void __iomem *p;
	int mirror_size;
	int err;

	p = ioremap(dev->mem_start, MIRROR_SIZE);
	if (!p) {
		release_mem_region(dev->mem_start, MIRROR_SIZE);
		BUGMSG(D_NORMAL, "Can't ioremap\n");
		return -ENODEV;
	}

	/* reserve the irq */
	if (request_irq(dev->irq, arcnet_interrupt, 0, "arcnet (RIM I)", dev)) {
		iounmap(p);
		release_mem_region(dev->mem_start, MIRROR_SIZE);
		BUGMSG(D_NORMAL, "Can't get IRQ %d!\n", dev->irq);
		return -ENODEV;
	}

	shmem = dev->mem_start;
	writeb(TESTvalue, p);
	writeb(dev->dev_addr[0], p + 1);	/* actually the node ID */

	/* find the real shared memory start/end points, including mirrors */

	/* guess the actual size of one "memory mirror" - the number of
	 * bytes between copies of the shared memory.  On most cards, it's
	 * 2k (or there are no mirrors at all) but on some, it's 4k.
	 */
	mirror_size = MIRROR_SIZE;
	if (readb(p) == TESTvalue &&
	    check_mirror(shmem - MIRROR_SIZE, MIRROR_SIZE) == 0 &&
	    check_mirror(shmem - 2 * MIRROR_SIZE, MIRROR_SIZE) == 1)
		mirror_size = 2 * MIRROR_SIZE;

	first_mirror = shmem - mirror_size;
	while (check_mirror(first_mirror, mirror_size) == 1)
		first_mirror -= mirror_size;
	first_mirror += mirror_size;

	last_mirror = shmem + mirror_size;
	while (check_mirror(last_mirror, mirror_size) == 1)
		last_mirror += mirror_size;
	last_mirror -= mirror_size;

	dev->mem_start = first_mirror;
	dev->mem_end = last_mirror + MIRROR_SIZE - 1;

	/* initialize the rest of the device structure. */

	lp = netdev_priv(dev);
	lp->card_name = "RIM I";
	lp->hw.command = arcrimi_command;
	lp->hw.status = arcrimi_status;
	lp->hw.intmask = arcrimi_setmask;
	lp->hw.reset = arcrimi_reset;
	lp->hw.owner = THIS_MODULE;
	lp->hw.copy_to_card = arcrimi_copy_to_card;
	lp->hw.copy_from_card = arcrimi_copy_from_card;

	/*
	 * re-reserve the memory region - arcrimi_probe() alloced this reqion
	 * but didn't know the real size.  Free that region and then re-get
	 * with the correct size.  There is a VERY slim chance this could
	 * fail.
	 */
	iounmap(p);
	release_mem_region(shmem, MIRROR_SIZE);
	if (!request_mem_region(dev->mem_start,
				dev->mem_end - dev->mem_start + 1,
				"arcnet (90xx)")) {
		BUGMSG(D_NORMAL, "Card memory already allocated\n");
		goto err_free_irq;
	}

	lp->mem_start = ioremap(dev->mem_start, dev->mem_end - dev->mem_start + 1);
	if (!lp->mem_start) {
		BUGMSG(D_NORMAL, "Can't remap device memory!\n");
		goto err_release_mem;
	}

	/* get and check the station ID from offset 1 in shmem */
	dev->dev_addr[0] = readb(lp->mem_start + 1);

	BUGMSG(D_NORMAL, "ARCnet RIM I: station %02Xh found at IRQ %d, "
	       "ShMem %lXh (%ld*%d bytes).\n",
	       dev->dev_addr[0],
	       dev->irq, dev->mem_start,
	 (dev->mem_end - dev->mem_start + 1) / mirror_size, mirror_size);

	err = register_netdev(dev);
	if (err)
		goto err_unmap;

	return 0;

err_unmap:
	iounmap(lp->mem_start);
err_release_mem:
	release_mem_region(dev->mem_start, dev->mem_end - dev->mem_start + 1);
err_free_irq:
	free_irq(dev->irq, dev);
	return -EIO;
}


/*
 * Do a hardware reset on the card, and set up necessary registers.
 *
 * This should be called as little as possible, because it disrupts the
 * token on the network (causes a RECON) and requires a significant delay.
 *
 * However, it does make sure the card is in a defined state.
 */
static int arcrimi_reset(struct net_device *dev, int really_reset)
{
	struct arcnet_local *lp = netdev_priv(dev);
	void __iomem *ioaddr = lp->mem_start + 0x800;

	BUGMSG(D_INIT, "Resetting %s (status=%02Xh)\n", dev->name, ASTATUS());

	if (really_reset) {
		writeb(TESTvalue, ioaddr - 0x800);	/* fake reset */
		return 0;
	}
	ACOMMAND(CFLAGScmd | RESETclear);	/* clear flags & end reset */
	ACOMMAND(CFLAGScmd | CONFIGclear);

	/* enable extended (512-byte) packets */
	ACOMMAND(CONFIGcmd | EXTconf);

	/* done!  return success. */
	return 0;
}

static void arcrimi_setmask(struct net_device *dev, int mask)
{
	struct arcnet_local *lp = netdev_priv(dev);
	void __iomem *ioaddr = lp->mem_start + 0x800;

	AINTMASK(mask);
}

static int arcrimi_status(struct net_device *dev)
{
	struct arcnet_local *lp = netdev_priv(dev);
	void __iomem *ioaddr = lp->mem_start + 0x800;

	return ASTATUS();
}

static void arcrimi_command(struct net_device *dev, int cmd)
{
	struct arcnet_local *lp = netdev_priv(dev);
	void __iomem *ioaddr = lp->mem_start + 0x800;

	ACOMMAND(cmd);
}

static void arcrimi_copy_to_card(struct net_device *dev, int bufnum, int offset,
				 void *buf, int count)
{
	struct arcnet_local *lp = netdev_priv(dev);
	void __iomem *memaddr = lp->mem_start + 0x800 + bufnum * 512 + offset;
	TIME("memcpy_toio", count, memcpy_toio(memaddr, buf, count));
}


static void arcrimi_copy_from_card(struct net_device *dev, int bufnum, int offset,
				   void *buf, int count)
{
	struct arcnet_local *lp = netdev_priv(dev);
	void __iomem *memaddr = lp->mem_start + 0x800 + bufnum * 512 + offset;
	TIME("memcpy_fromio", count, memcpy_fromio(buf, memaddr, count));
}

static int node;
static int io;			/* use the insmod io= irq= node= options */
static int irq;
static char device[9];		/* use eg. device=arc1 to change name */

module_param(node, int, 0);
module_param(io, int, 0);
module_param(irq, int, 0);
module_param_string(device, device, sizeof(device), 0);
MODULE_LICENSE("GPL");

static struct net_device *my_dev;

static int __init arc_rimi_init(void)
{
	struct net_device *dev;

	dev = alloc_arcdev(device);
	if (!dev)
		return -ENOMEM;

	if (node && node != 0xff)
		dev->dev_addr[0] = node;

	dev->mem_start = io;
	dev->irq = irq;
	if (dev->irq == 2)
		dev->irq = 9;

	if (arcrimi_probe(dev)) {
		free_netdev(dev);
		return -EIO;
	}

	my_dev = dev;
	return 0;
}

static void __exit arc_rimi_exit(void)
{
	struct net_device *dev = my_dev;
	struct arcnet_local *lp = netdev_priv(dev);

	unregister_netdev(dev);
	iounmap(lp->mem_start);
	release_mem_region(dev->mem_start, dev->mem_end - dev->mem_start + 1);
	free_irq(dev->irq, dev);
	free_netdev(dev);
}

#ifndef MODULE
static int __init arcrimi_setup(char *s)
{
	int ints[8];
	s = get_options(s, 8, ints);
	if (!ints[0])
		return 1;
	switch (ints[0]) {
	default:		/* ERROR */
		printk("arcrimi: Too many arguments.\n");
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
__setup("arcrimi=", arcrimi_setup);
#endif				/* MODULE */

module_init(arc_rimi_init)
module_exit(arc_rimi_exit)
