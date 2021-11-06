/*
 * Linux ARCnet driver - COM90xx chipset (memory-mapped buffers)
 *
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

#define pr_fmt(fmt) "arcnet:" KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/ioport.h>
#include <linux/delay.h>
#include <linux/netdevice.h>
#include <linux/slab.h>
#include <linux/io.h>

#include "arcdevice.h"
#include "com9026.h"

/* Define this to speed up the autoprobe by assuming if only one io port and
 * shmem are left in the list at Stage 5, they must correspond to each
 * other.
 *
 * This is undefined by default because it might not always be true, and the
 * extra check makes the autoprobe even more careful.  Speed demons can turn
 * it on - I think it should be fine if you only have one ARCnet card
 * installed.
 *
 * If no ARCnet cards are installed, this delay never happens anyway and thus
 * the option has no effect.
 */
#undef FAST_PROBE

/* Internal function declarations */
static int com90xx_found(int ioaddr, int airq, u_long shmem, void __iomem *);
static void com90xx_command(struct net_device *dev, int command);
static int com90xx_status(struct net_device *dev);
static void com90xx_setmask(struct net_device *dev, int mask);
static int com90xx_reset(struct net_device *dev, int really_reset);
static void com90xx_copy_to_card(struct net_device *dev, int bufnum, int offset,
				 void *buf, int count);
static void com90xx_copy_from_card(struct net_device *dev, int bufnum,
				   int offset, void *buf, int count);

/* Known ARCnet cards */

static struct net_device *cards[16];
static int numcards;

/* Handy defines for ARCnet specific stuff */

/* The number of low I/O ports used by the card */
#define ARCNET_TOTAL_SIZE	16

/* Amount of I/O memory used by the card */
#define BUFFER_SIZE (512)
#define MIRROR_SIZE (BUFFER_SIZE * 4)

static int com90xx_skip_probe __initdata = 0;

/* Module parameters */

static int io;			/* use the insmod io= irq= shmem= options */
static int irq;
static int shmem;
static char device[9];		/* use eg. device=arc1 to change name */

module_param_hw(io, int, ioport, 0);
module_param_hw(irq, int, irq, 0);
module_param(shmem, int, 0);
module_param_string(device, device, sizeof(device), 0);

static void __init com90xx_probe(void)
{
	int count, status, ioaddr, numprint, airq, openparen = 0;
	unsigned long airqmask;
	int ports[(0x3f0 - 0x200) / 16 + 1] = {	0 };
	unsigned long *shmems;
	void __iomem **iomem;
	int numports, numshmems, *port;
	u_long *p;
	int index;

	if (!io && !irq && !shmem && !*device && com90xx_skip_probe)
		return;

	shmems = kzalloc(((0x100000 - 0xa0000) / 0x800) * sizeof(unsigned long),
			 GFP_KERNEL);
	if (!shmems)
		return;
	iomem = kzalloc(((0x100000 - 0xa0000) / 0x800) * sizeof(void __iomem *),
			GFP_KERNEL);
	if (!iomem) {
		kfree(shmems);
		return;
	}

	if (BUGLVL(D_NORMAL))
		pr_info("%s\n", "COM90xx chipset support");

	/* set up the arrays where we'll store the possible probe addresses */
	numports = numshmems = 0;
	if (io)
		ports[numports++] = io;
	else
		for (count = 0x200; count <= 0x3f0; count += 16)
			ports[numports++] = count;
	if (shmem)
		shmems[numshmems++] = shmem;
	else
		for (count = 0xA0000; count <= 0xFF800; count += 2048)
			shmems[numshmems++] = count;

	/* Stage 1: abandon any reserved ports, or ones with status==0xFF
	 * (empty), and reset any others by reading the reset port.
	 */
	numprint = -1;
	for (port = &ports[0]; port - ports < numports; port++) {
		numprint++;
		numprint %= 8;
		if (!numprint) {
			arc_cont(D_INIT, "\n");
			arc_cont(D_INIT, "S1: ");
		}
		arc_cont(D_INIT, "%Xh ", *port);

		ioaddr = *port;

		if (!request_region(*port, ARCNET_TOTAL_SIZE,
				    "arcnet (90xx)")) {
			arc_cont(D_INIT_REASONS, "(request_region)\n");
			arc_cont(D_INIT_REASONS, "S1: ");
			if (BUGLVL(D_INIT_REASONS))
				numprint = 0;
			*port-- = ports[--numports];
			continue;
		}
		if (arcnet_inb(ioaddr, COM9026_REG_R_STATUS) == 0xFF) {
			arc_cont(D_INIT_REASONS, "(empty)\n");
			arc_cont(D_INIT_REASONS, "S1: ");
			if (BUGLVL(D_INIT_REASONS))
				numprint = 0;
			release_region(*port, ARCNET_TOTAL_SIZE);
			*port-- = ports[--numports];
			continue;
		}
		/* begin resetting card */
		arcnet_inb(ioaddr, COM9026_REG_R_RESET);

		arc_cont(D_INIT_REASONS, "\n");
		arc_cont(D_INIT_REASONS, "S1: ");
		if (BUGLVL(D_INIT_REASONS))
			numprint = 0;
	}
	arc_cont(D_INIT, "\n");

	if (!numports) {
		arc_cont(D_NORMAL, "S1: No ARCnet cards found.\n");
		kfree(shmems);
		kfree(iomem);
		return;
	}
	/* Stage 2: we have now reset any possible ARCnet cards, so we can't
	 * do anything until they finish.  If D_INIT, print the list of
	 * cards that are left.
	 */
	numprint = -1;
	for (port = &ports[0]; port < ports + numports; port++) {
		numprint++;
		numprint %= 8;
		if (!numprint) {
			arc_cont(D_INIT, "\n");
			arc_cont(D_INIT, "S2: ");
		}
		arc_cont(D_INIT, "%Xh ", *port);
	}
	arc_cont(D_INIT, "\n");
	mdelay(RESETtime);

	/* Stage 3: abandon any shmem addresses that don't have the signature
	 * 0xD1 byte in the right place, or are read-only.
	 */
	numprint = -1;
	for (index = 0, p = &shmems[0]; index < numshmems; p++, index++) {
		void __iomem *base;

		numprint++;
		numprint %= 8;
		if (!numprint) {
			arc_cont(D_INIT, "\n");
			arc_cont(D_INIT, "S3: ");
		}
		arc_cont(D_INIT, "%lXh ", *p);

		if (!request_mem_region(*p, MIRROR_SIZE, "arcnet (90xx)")) {
			arc_cont(D_INIT_REASONS, "(request_mem_region)\n");
			arc_cont(D_INIT_REASONS, "Stage 3: ");
			if (BUGLVL(D_INIT_REASONS))
				numprint = 0;
			goto out;
		}
		base = ioremap(*p, MIRROR_SIZE);
		if (!base) {
			arc_cont(D_INIT_REASONS, "(ioremap)\n");
			arc_cont(D_INIT_REASONS, "Stage 3: ");
			if (BUGLVL(D_INIT_REASONS))
				numprint = 0;
			goto out1;
		}
		if (arcnet_readb(base, COM9026_REG_R_STATUS) != TESTvalue) {
			arc_cont(D_INIT_REASONS, "(%02Xh != %02Xh)\n",
				 arcnet_readb(base, COM9026_REG_R_STATUS),
				 TESTvalue);
			arc_cont(D_INIT_REASONS, "S3: ");
			if (BUGLVL(D_INIT_REASONS))
				numprint = 0;
			goto out2;
		}
		/* By writing 0x42 to the TESTvalue location, we also make
		 * sure no "mirror" shmem areas show up - if they occur
		 * in another pass through this loop, they will be discarded
		 * because *cptr != TESTvalue.
		 */
		arcnet_writeb(0x42, base, COM9026_REG_W_INTMASK);
		if (arcnet_readb(base, COM9026_REG_R_STATUS) != 0x42) {
			arc_cont(D_INIT_REASONS, "(read only)\n");
			arc_cont(D_INIT_REASONS, "S3: ");
			goto out2;
		}
		arc_cont(D_INIT_REASONS, "\n");
		arc_cont(D_INIT_REASONS, "S3: ");
		if (BUGLVL(D_INIT_REASONS))
			numprint = 0;
		iomem[index] = base;
		continue;
	out2:
		iounmap(base);
	out1:
		release_mem_region(*p, MIRROR_SIZE);
	out:
		*p-- = shmems[--numshmems];
		index--;
	}
	arc_cont(D_INIT, "\n");

	if (!numshmems) {
		arc_cont(D_NORMAL, "S3: No ARCnet cards found.\n");
		for (port = &ports[0]; port < ports + numports; port++)
			release_region(*port, ARCNET_TOTAL_SIZE);
		kfree(shmems);
		kfree(iomem);
		return;
	}
	/* Stage 4: something of a dummy, to report the shmems that are
	 * still possible after stage 3.
	 */
	numprint = -1;
	for (p = &shmems[0]; p < shmems + numshmems; p++) {
		numprint++;
		numprint %= 8;
		if (!numprint) {
			arc_cont(D_INIT, "\n");
			arc_cont(D_INIT, "S4: ");
		}
		arc_cont(D_INIT, "%lXh ", *p);
	}
	arc_cont(D_INIT, "\n");

	/* Stage 5: for any ports that have the correct status, can disable
	 * the RESET flag, and (if no irq is given) generate an autoirq,
	 * register an ARCnet device.
	 *
	 * Currently, we can only register one device per probe, so quit
	 * after the first one is found.
	 */
	numprint = -1;
	for (port = &ports[0]; port < ports + numports; port++) {
		int found = 0;

		numprint++;
		numprint %= 8;
		if (!numprint) {
			arc_cont(D_INIT, "\n");
			arc_cont(D_INIT, "S5: ");
		}
		arc_cont(D_INIT, "%Xh ", *port);

		ioaddr = *port;
		status = arcnet_inb(ioaddr, COM9026_REG_R_STATUS);

		if ((status & 0x9D)
		    != (NORXflag | RECONflag | TXFREEflag | RESETflag)) {
			arc_cont(D_INIT_REASONS, "(status=%Xh)\n", status);
			arc_cont(D_INIT_REASONS, "S5: ");
			if (BUGLVL(D_INIT_REASONS))
				numprint = 0;
			release_region(*port, ARCNET_TOTAL_SIZE);
			*port-- = ports[--numports];
			continue;
		}
		arcnet_outb(CFLAGScmd | RESETclear | CONFIGclear,
			    ioaddr, COM9026_REG_W_COMMAND);
		status = arcnet_inb(ioaddr, COM9026_REG_R_STATUS);
		if (status & RESETflag) {
			arc_cont(D_INIT_REASONS, " (eternal reset, status=%Xh)\n",
				 status);
			arc_cont(D_INIT_REASONS, "S5: ");
			if (BUGLVL(D_INIT_REASONS))
				numprint = 0;
			release_region(*port, ARCNET_TOTAL_SIZE);
			*port-- = ports[--numports];
			continue;
		}
		/* skip this completely if an IRQ was given, because maybe
		 * we're on a machine that locks during autoirq!
		 */
		if (!irq) {
			/* if we do this, we're sure to get an IRQ since the
			 * card has just reset and the NORXflag is on until
			 * we tell it to start receiving.
			 */
			airqmask = probe_irq_on();
			arcnet_outb(NORXflag, ioaddr, COM9026_REG_W_INTMASK);
			udelay(1);
			arcnet_outb(0, ioaddr, COM9026_REG_W_INTMASK);
			airq = probe_irq_off(airqmask);

			if (airq <= 0) {
				arc_cont(D_INIT_REASONS, "(airq=%d)\n", airq);
				arc_cont(D_INIT_REASONS, "S5: ");
				if (BUGLVL(D_INIT_REASONS))
					numprint = 0;
				release_region(*port, ARCNET_TOTAL_SIZE);
				*port-- = ports[--numports];
				continue;
			}
		} else {
			airq = irq;
		}

		arc_cont(D_INIT, "(%d,", airq);
		openparen = 1;

		/* Everything seems okay.  But which shmem, if any, puts
		 * back its signature byte when the card is reset?
		 *
		 * If there are multiple cards installed, there might be
		 * multiple shmems still in the list.
		 */
#ifdef FAST_PROBE
		if (numports > 1 || numshmems > 1) {
			arcnet_inb(ioaddr, COM9026_REG_R_RESET);
			mdelay(RESETtime);
		} else {
			/* just one shmem and port, assume they match */
			arcnet_writeb(TESTvalue, iomem[0],
				      COM9026_REG_W_INTMASK);
		}
#else
		arcnet_inb(ioaddr, COM9026_REG_R_RESET);
		mdelay(RESETtime);
#endif

		for (index = 0; index < numshmems; index++) {
			u_long ptr = shmems[index];
			void __iomem *base = iomem[index];

			if (arcnet_readb(base, COM9026_REG_R_STATUS) == TESTvalue) {	/* found one */
				arc_cont(D_INIT, "%lXh)\n", *p);
				openparen = 0;

				/* register the card */
				if (com90xx_found(*port, airq, ptr, base) == 0)
					found = 1;
				numprint = -1;

				/* remove shmem from the list */
				shmems[index] = shmems[--numshmems];
				iomem[index] = iomem[numshmems];
				break;	/* go to the next I/O port */
			} else {
				arc_cont(D_INIT_REASONS, "%Xh-",
					 arcnet_readb(base, COM9026_REG_R_STATUS));
			}
		}

		if (openparen) {
			if (BUGLVL(D_INIT))
				pr_cont("no matching shmem)\n");
			if (BUGLVL(D_INIT_REASONS)) {
				pr_cont("S5: ");
				numprint = 0;
			}
		}
		if (!found)
			release_region(*port, ARCNET_TOTAL_SIZE);
		*port-- = ports[--numports];
	}

	if (BUGLVL(D_INIT_REASONS))
		pr_cont("\n");

	/* Now put back TESTvalue on all leftover shmems. */
	for (index = 0; index < numshmems; index++) {
		arcnet_writeb(TESTvalue, iomem[index], COM9026_REG_W_INTMASK);
		iounmap(iomem[index]);
		release_mem_region(shmems[index], MIRROR_SIZE);
	}
	kfree(shmems);
	kfree(iomem);
}

static int __init check_mirror(unsigned long addr, size_t size)
{
	void __iomem *p;
	int res = -1;

	if (!request_mem_region(addr, size, "arcnet (90xx)"))
		return -1;

	p = ioremap(addr, size);
	if (p) {
		if (arcnet_readb(p, COM9026_REG_R_STATUS) == TESTvalue)
			res = 1;
		else
			res = 0;
		iounmap(p);
	}

	release_mem_region(addr, size);
	return res;
}

/* Set up the struct net_device associated with this card.  Called after
 * probing succeeds.
 */
static int __init com90xx_found(int ioaddr, int airq, u_long shmem,
				void __iomem *p)
{
	struct net_device *dev = NULL;
	struct arcnet_local *lp;
	u_long first_mirror, last_mirror;
	int mirror_size;

	/* allocate struct net_device */
	dev = alloc_arcdev(device);
	if (!dev) {
		arc_cont(D_NORMAL, "com90xx: Can't allocate device!\n");
		iounmap(p);
		release_mem_region(shmem, MIRROR_SIZE);
		return -ENOMEM;
	}
	lp = netdev_priv(dev);
	/* find the real shared memory start/end points, including mirrors */

	/* guess the actual size of one "memory mirror" - the number of
	 * bytes between copies of the shared memory.  On most cards, it's
	 * 2k (or there are no mirrors at all) but on some, it's 4k.
	 */
	mirror_size = MIRROR_SIZE;
	if (arcnet_readb(p, COM9026_REG_R_STATUS) == TESTvalue &&
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

	iounmap(p);
	release_mem_region(shmem, MIRROR_SIZE);

	if (!request_mem_region(dev->mem_start,
				dev->mem_end - dev->mem_start + 1,
				"arcnet (90xx)"))
		goto err_free_dev;

	/* reserve the irq */
	if (request_irq(airq, arcnet_interrupt, 0, "arcnet (90xx)", dev)) {
		arc_printk(D_NORMAL, dev, "Can't get IRQ %d!\n", airq);
		goto err_release_mem;
	}
	dev->irq = airq;

	/* Initialize the rest of the device structure. */
	lp->card_name = "COM90xx";
	lp->hw.command = com90xx_command;
	lp->hw.status = com90xx_status;
	lp->hw.intmask = com90xx_setmask;
	lp->hw.reset = com90xx_reset;
	lp->hw.owner = THIS_MODULE;
	lp->hw.copy_to_card = com90xx_copy_to_card;
	lp->hw.copy_from_card = com90xx_copy_from_card;
	lp->mem_start = ioremap(dev->mem_start,
				dev->mem_end - dev->mem_start + 1);
	if (!lp->mem_start) {
		arc_printk(D_NORMAL, dev, "Can't remap device memory!\n");
		goto err_free_irq;
	}

	/* get and check the station ID from offset 1 in shmem */
	arcnet_set_addr(dev, arcnet_readb(lp->mem_start,
					  COM9026_REG_R_STATION));

	dev->base_addr = ioaddr;

	arc_printk(D_NORMAL, dev, "COM90xx station %02Xh found at %03lXh, IRQ %d, ShMem %lXh (%ld*%xh).\n",
		   dev->dev_addr[0],
		   dev->base_addr, dev->irq, dev->mem_start,
		   (dev->mem_end - dev->mem_start + 1) / mirror_size,
		   mirror_size);

	if (register_netdev(dev))
		goto err_unmap;

	cards[numcards++] = dev;
	return 0;

err_unmap:
	iounmap(lp->mem_start);
err_free_irq:
	free_irq(dev->irq, dev);
err_release_mem:
	release_mem_region(dev->mem_start, dev->mem_end - dev->mem_start + 1);
err_free_dev:
	free_arcdev(dev);
	return -EIO;
}

static void com90xx_command(struct net_device *dev, int cmd)
{
	short ioaddr = dev->base_addr;

	arcnet_outb(cmd, ioaddr, COM9026_REG_W_COMMAND);
}

static int com90xx_status(struct net_device *dev)
{
	short ioaddr = dev->base_addr;

	return arcnet_inb(ioaddr, COM9026_REG_R_STATUS);
}

static void com90xx_setmask(struct net_device *dev, int mask)
{
	short ioaddr = dev->base_addr;

	arcnet_outb(mask, ioaddr, COM9026_REG_W_INTMASK);
}

/* Do a hardware reset on the card, and set up necessary registers.
 *
 * This should be called as little as possible, because it disrupts the
 * token on the network (causes a RECON) and requires a significant delay.
 *
 * However, it does make sure the card is in a defined state.
 */
static int com90xx_reset(struct net_device *dev, int really_reset)
{
	struct arcnet_local *lp = netdev_priv(dev);
	short ioaddr = dev->base_addr;

	arc_printk(D_INIT, dev, "Resetting (status=%02Xh)\n",
		   arcnet_inb(ioaddr, COM9026_REG_R_STATUS));

	if (really_reset) {
		/* reset the card */
		arcnet_inb(ioaddr, COM9026_REG_R_RESET);
		mdelay(RESETtime);
	}
	/* clear flags & end reset */
	arcnet_outb(CFLAGScmd | RESETclear, ioaddr, COM9026_REG_W_COMMAND);
	arcnet_outb(CFLAGScmd | CONFIGclear, ioaddr, COM9026_REG_W_COMMAND);

#if 0
	/* don't do this until we verify that it doesn't hurt older cards! */
	arcnet_outb(arcnet_inb(ioaddr, COM9026_REG_RW_CONFIG) | ENABLE16flag,
		    ioaddr, COM9026_REG_RW_CONFIG);
#endif

	/* verify that the ARCnet signature byte is present */
	if (arcnet_readb(lp->mem_start, COM9026_REG_R_STATUS) != TESTvalue) {
		if (really_reset)
			arc_printk(D_NORMAL, dev, "reset failed: TESTvalue not present.\n");
		return 1;
	}
	/* enable extended (512-byte) packets */
	arcnet_outb(CONFIGcmd | EXTconf, ioaddr, COM9026_REG_W_COMMAND);

	/* clean out all the memory to make debugging make more sense :) */
	if (BUGLVL(D_DURING))
		memset_io(lp->mem_start, 0x42, 2048);

	/* done!  return success. */
	return 0;
}

static void com90xx_copy_to_card(struct net_device *dev, int bufnum,
				 int offset, void *buf, int count)
{
	struct arcnet_local *lp = netdev_priv(dev);
	void __iomem *memaddr = lp->mem_start + bufnum * 512 + offset;

	TIME(dev, "memcpy_toio", count, memcpy_toio(memaddr, buf, count));
}

static void com90xx_copy_from_card(struct net_device *dev, int bufnum,
				   int offset, void *buf, int count)
{
	struct arcnet_local *lp = netdev_priv(dev);
	void __iomem *memaddr = lp->mem_start + bufnum * 512 + offset;

	TIME(dev, "memcpy_fromio", count, memcpy_fromio(buf, memaddr, count));
}

MODULE_LICENSE("GPL");

static int __init com90xx_init(void)
{
	if (irq == 2)
		irq = 9;
	com90xx_probe();
	if (!numcards)
		return -EIO;
	return 0;
}

static void __exit com90xx_exit(void)
{
	struct net_device *dev;
	struct arcnet_local *lp;
	int count;

	for (count = 0; count < numcards; count++) {
		dev = cards[count];
		lp = netdev_priv(dev);

		unregister_netdev(dev);
		free_irq(dev->irq, dev);
		iounmap(lp->mem_start);
		release_region(dev->base_addr, ARCNET_TOTAL_SIZE);
		release_mem_region(dev->mem_start,
				   dev->mem_end - dev->mem_start + 1);
		free_arcdev(dev);
	}
}

module_init(com90xx_init);
module_exit(com90xx_exit);

#ifndef MODULE
static int __init com90xx_setup(char *s)
{
	int ints[8];

	s = get_options(s, 8, ints);
	if (!ints[0] && !*s) {
		pr_notice("Disabled\n");
		return 1;
	}

	switch (ints[0]) {
	default:		/* ERROR */
		pr_err("Too many arguments\n");
		fallthrough;
	case 3:		/* Mem address */
		shmem = ints[3];
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

__setup("com90xx=", com90xx_setup);
#endif
