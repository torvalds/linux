/* ac3200.c: A driver for the Ansel Communications EISA ethernet adaptor. */
/*
	Written 1993, 1994 by Donald Becker.
	Copyright 1993 United States Government as represented by the Director,
	National Security Agency.  This software may only be used and distributed
	according to the terms of the GNU General Public License as modified by SRC,
	incorporated herein by reference.

	The author may be reached as becker@scyld.com, or C/O
	Scyld Computing Corporation
	410 Severn Ave., Suite 210
	Annapolis MD 21403

	This is driver for the Ansel Communications Model 3200 EISA Ethernet LAN
	Adapter.  The programming information is from the users manual, as related
	by glee@ardnassak.math.clemson.edu.

	Changelog:

	Paul Gortmaker 05/98	: add support for shared mem above 1MB.

  */

static const char version[] =
	"ac3200.c:v1.01 7/1/94 Donald Becker (becker@cesdis.gsfc.nasa.gov)\n";

#include <linux/module.h>
#include <linux/eisa.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/init.h>

#include <asm/system.h>
#include <asm/io.h>
#include <asm/irq.h>

#include "8390.h"

#define DRV_NAME	"ac3200"

/* Offsets from the base address. */
#define AC_NIC_BASE	0x00
#define AC_SA_PROM	0x16			/* The station address PROM. */
#define AC_ADDR0	0x00			/* Prefix station address values. */
#define AC_ADDR1	0x40			
#define AC_ADDR2	0x90
#define AC_ID_PORT	0xC80
#define AC_EISA_ID	0x0110d305
#define AC_RESET_PORT	0xC84
#define AC_RESET	0x00
#define AC_ENABLE	0x01
#define AC_CONFIG	0xC90	/* The configuration port. */

#define AC_IO_EXTENT 0x20
                                /* Actually accessed is:
								 * AC_NIC_BASE (0-15)
								 * AC_SA_PROM (0-5)
								 * AC_ID_PORT (0-3)
								 * AC_RESET_PORT
								 * AC_CONFIG
								 */

/* Decoding of the configuration register. */
static unsigned char config2irqmap[8] __initdata = {15, 12, 11, 10, 9, 7, 5, 3};
static int addrmap[8] =
{0xFF0000, 0xFE0000, 0xFD0000, 0xFFF0000, 0xFFE0000, 0xFFC0000,  0xD0000, 0 };
static const char *port_name[4] = { "10baseT", "invalid", "AUI", "10base2"};

#define config2irq(configval)	config2irqmap[((configval) >> 3) & 7]
#define config2mem(configval)	addrmap[(configval) & 7]
#define config2name(configval)	port_name[((configval) >> 6) & 3]

/* First and last 8390 pages. */
#define AC_START_PG		0x00	/* First page of 8390 TX buffer */
#define AC_STOP_PG		0x80	/* Last page +1 of the 8390 RX ring */

static int ac_probe1(int ioaddr, struct net_device *dev);

static int ac_open(struct net_device *dev);
static void ac_reset_8390(struct net_device *dev);
static void ac_block_input(struct net_device *dev, int count,
					struct sk_buff *skb, int ring_offset);
static void ac_block_output(struct net_device *dev, const int count,
							const unsigned char *buf, const int start_page);
static void ac_get_8390_hdr(struct net_device *dev, struct e8390_pkt_hdr *hdr,
					int ring_page);

static int ac_close_card(struct net_device *dev);


/*	Probe for the AC3200.

	The AC3200 can be identified by either the EISA configuration registers,
	or the unique value in the station address PROM.
	*/

static int __init do_ac3200_probe(struct net_device *dev)
{
	unsigned short ioaddr = dev->base_addr;
	int irq = dev->irq;
	int mem_start = dev->mem_start;

	SET_MODULE_OWNER(dev);

	if (ioaddr > 0x1ff)		/* Check a single specified location. */
		return ac_probe1(ioaddr, dev);
	else if (ioaddr > 0)		/* Don't probe at all. */
		return -ENXIO;

	if ( ! EISA_bus)
		return -ENXIO;

	for (ioaddr = 0x1000; ioaddr < 0x9000; ioaddr += 0x1000) {
		if (ac_probe1(ioaddr, dev) == 0)
			return 0;
		dev->irq = irq;
		dev->mem_start = mem_start;
	}

	return -ENODEV;
}

static void cleanup_card(struct net_device *dev)
{
	/* Someday free_irq may be in ac_close_card() */
	free_irq(dev->irq, dev);
	release_region(dev->base_addr, AC_IO_EXTENT);
	iounmap(ei_status.mem);
}

#ifndef MODULE
struct net_device * __init ac3200_probe(int unit)
{
	struct net_device *dev = alloc_ei_netdev();
	int err;

	if (!dev)
		return ERR_PTR(-ENOMEM);

	sprintf(dev->name, "eth%d", unit);
	netdev_boot_setup_check(dev);

	err = do_ac3200_probe(dev);
	if (err)
		goto out;
	return dev;
out:
	free_netdev(dev);
	return ERR_PTR(err);
}
#endif

static int __init ac_probe1(int ioaddr, struct net_device *dev)
{
	int i, retval;

	if (!request_region(ioaddr, AC_IO_EXTENT, DRV_NAME))
		return -EBUSY;

	if (inb_p(ioaddr + AC_ID_PORT) == 0xff) {
		retval = -ENODEV;
		goto out;
	}

	if (inl(ioaddr + AC_ID_PORT) != AC_EISA_ID) {
		retval = -ENODEV;
		goto out;
	}

#ifndef final_version
	printk(KERN_DEBUG "AC3200 ethercard configuration register is %#02x,"
		   " EISA ID %02x %02x %02x %02x.\n", inb(ioaddr + AC_CONFIG),
		   inb(ioaddr + AC_ID_PORT + 0), inb(ioaddr + AC_ID_PORT + 1),
		   inb(ioaddr + AC_ID_PORT + 2), inb(ioaddr + AC_ID_PORT + 3));
#endif

	printk("AC3200 in EISA slot %d, node", ioaddr/0x1000);
	for(i = 0; i < 6; i++)
		printk(" %02x", dev->dev_addr[i] = inb(ioaddr + AC_SA_PROM + i));

#if 0
	/* Check the vendor ID/prefix. Redundant after checking the EISA ID */
	if (inb(ioaddr + AC_SA_PROM + 0) != AC_ADDR0
		|| inb(ioaddr + AC_SA_PROM + 1) != AC_ADDR1
		|| inb(ioaddr + AC_SA_PROM + 2) != AC_ADDR2 ) {
		printk(", not found (invalid prefix).\n");
		retval = -ENODEV;
		goto out;
	}
#endif

	/* Assign and allocate the interrupt now. */
	if (dev->irq == 0) {
		dev->irq = config2irq(inb(ioaddr + AC_CONFIG));
		printk(", using");
	} else {
		dev->irq = irq_canonicalize(dev->irq);
		printk(", assigning");
	}

	retval = request_irq(dev->irq, ei_interrupt, 0, DRV_NAME, dev);
	if (retval) {
		printk (" nothing! Unable to get IRQ %d.\n", dev->irq);
		goto out1;
	}

	printk(" IRQ %d, %s port\n", dev->irq, port_name[dev->if_port]);

	dev->base_addr = ioaddr;

#ifdef notyet
	if (dev->mem_start)	{		/* Override the value from the board. */
		for (i = 0; i < 7; i++)
			if (addrmap[i] == dev->mem_start)
				break;
		if (i >= 7)
			i = 0;
		outb((inb(ioaddr + AC_CONFIG) & ~7) | i, ioaddr + AC_CONFIG);
	}
#endif

	dev->if_port = inb(ioaddr + AC_CONFIG) >> 6;
	dev->mem_start = config2mem(inb(ioaddr + AC_CONFIG));

	printk("%s: AC3200 at %#3x with %dkB memory at physical address %#lx.\n", 
			dev->name, ioaddr, AC_STOP_PG/4, dev->mem_start);

	/*
	 *  BEWARE!! Some dain-bramaged EISA SCUs will allow you to put
	 *  the card mem within the region covered by `normal' RAM  !!!
	 *
	 *  ioremap() will fail in that case.
	 */
	ei_status.mem = ioremap(dev->mem_start, AC_STOP_PG*0x100);
	if (!ei_status.mem) {
		printk(KERN_ERR "ac3200.c: Unable to remap card memory above 1MB !!\n");
		printk(KERN_ERR "ac3200.c: Try using EISA SCU to set memory below 1MB.\n");
		printk(KERN_ERR "ac3200.c: Driver NOT installed.\n");
		retval = -EINVAL;
		goto out1;
	}
	printk("ac3200.c: remapped %dkB card memory to virtual address %p\n",
			AC_STOP_PG/4, ei_status.mem);

	dev->mem_start = (unsigned long)ei_status.mem;
	dev->mem_end = dev->mem_start + (AC_STOP_PG - AC_START_PG)*256;

	ei_status.name = "AC3200";
	ei_status.tx_start_page = AC_START_PG;
	ei_status.rx_start_page = AC_START_PG + TX_PAGES;
	ei_status.stop_page = AC_STOP_PG;
	ei_status.word16 = 1;

	if (ei_debug > 0)
		printk(version);

	ei_status.reset_8390 = &ac_reset_8390;
	ei_status.block_input = &ac_block_input;
	ei_status.block_output = &ac_block_output;
	ei_status.get_8390_hdr = &ac_get_8390_hdr;

	dev->open = &ac_open;
	dev->stop = &ac_close_card;
#ifdef CONFIG_NET_POLL_CONTROLLER
	dev->poll_controller = ei_poll;
#endif
	NS8390_init(dev, 0);

	retval = register_netdev(dev);
	if (retval)
		goto out2;
	return 0;
out2:
	if (ei_status.reg0)
		iounmap(ei_status.mem);
out1:
	free_irq(dev->irq, dev);
out:
	release_region(ioaddr, AC_IO_EXTENT);
	return retval;
}

static int ac_open(struct net_device *dev)
{
#ifdef notyet
	/* Someday we may enable the IRQ and shared memory here. */
	int ioaddr = dev->base_addr;
#endif

	ei_open(dev);
	return 0;
}

static void ac_reset_8390(struct net_device *dev)
{
	ushort ioaddr = dev->base_addr;

	outb(AC_RESET, ioaddr + AC_RESET_PORT);
	if (ei_debug > 1) printk("resetting AC3200, t=%ld...", jiffies);

	ei_status.txing = 0;
	outb(AC_ENABLE, ioaddr + AC_RESET_PORT);
	if (ei_debug > 1) printk("reset done\n");

	return;
}

/* Grab the 8390 specific header. Similar to the block_input routine, but
   we don't need to be concerned with ring wrap as the header will be at
   the start of a page, so we optimize accordingly. */

static void
ac_get_8390_hdr(struct net_device *dev, struct e8390_pkt_hdr *hdr, int ring_page)
{
	void __iomem *hdr_start = ei_status.mem + ((ring_page - AC_START_PG)<<8);
	memcpy_fromio(hdr, hdr_start, sizeof(struct e8390_pkt_hdr));
}

/*  Block input and output are easy on shared memory ethercards, the only
	complication is when the ring buffer wraps. */

static void ac_block_input(struct net_device *dev, int count, struct sk_buff *skb,
						  int ring_offset)
{
	void __iomem *start = ei_status.mem + ring_offset - AC_START_PG*256;

	if (ring_offset + count > AC_STOP_PG*256) {
		/* We must wrap the input move. */
		int semi_count = AC_STOP_PG*256 - ring_offset;
		memcpy_fromio(skb->data, start, semi_count);
		count -= semi_count;
		memcpy_fromio(skb->data + semi_count,
				ei_status.mem + TX_PAGES*256, count);
	} else {
		/* Packet is in one chunk -- we can copy + cksum. */
		eth_io_copy_and_sum(skb, start, count, 0);
	}
}

static void ac_block_output(struct net_device *dev, int count,
							const unsigned char *buf, int start_page)
{
	void __iomem *shmem = ei_status.mem + ((start_page - AC_START_PG)<<8);

	memcpy_toio(shmem, buf, count);
}

static int ac_close_card(struct net_device *dev)
{
	if (ei_debug > 1)
		printk("%s: Shutting down ethercard.\n", dev->name);

#ifdef notyet
	/* We should someday disable shared memory and interrupts. */
	outb(0x00, ioaddr + 6);	/* Disable interrupts. */
	free_irq(dev->irq, dev);
#endif

	ei_close(dev);
	return 0;
}

#ifdef MODULE
#define MAX_AC32_CARDS	4	/* Max number of AC32 cards per module */
static struct net_device *dev_ac32[MAX_AC32_CARDS];
static int io[MAX_AC32_CARDS];
static int irq[MAX_AC32_CARDS];
static int mem[MAX_AC32_CARDS];
module_param_array(io, int, NULL, 0);
module_param_array(irq, int, NULL, 0);
module_param_array(mem, int, NULL, 0);
MODULE_PARM_DESC(io, "I/O base address(es)");
MODULE_PARM_DESC(irq, "IRQ number(s)");
MODULE_PARM_DESC(mem, "Memory base address(es)");
MODULE_DESCRIPTION("Ansel AC3200 EISA ethernet driver");
MODULE_LICENSE("GPL");

int
init_module(void)
{
	struct net_device *dev;
	int this_dev, found = 0;

	for (this_dev = 0; this_dev < MAX_AC32_CARDS; this_dev++) {
		if (io[this_dev] == 0 && this_dev != 0)
			break;
		dev = alloc_ei_netdev();
		if (!dev)
			break;
		dev->irq = irq[this_dev];
		dev->base_addr = io[this_dev];
		dev->mem_start = mem[this_dev];		/* Currently ignored by driver */
		if (do_ac3200_probe(dev) == 0) {
			dev_ac32[found++] = dev;
			continue;
		}
		free_netdev(dev);
		printk(KERN_WARNING "ac3200.c: No ac3200 card found (i/o = 0x%x).\n", io[this_dev]);
		break;
	}
	if (found)
		return 0;
	return -ENXIO;
}

void
cleanup_module(void)
{
	int this_dev;

	for (this_dev = 0; this_dev < MAX_AC32_CARDS; this_dev++) {
		struct net_device *dev = dev_ac32[this_dev];
		if (dev) {
			unregister_netdev(dev);
			cleanup_card(dev);
			free_netdev(dev);
		}
	}
}
#endif /* MODULE */
