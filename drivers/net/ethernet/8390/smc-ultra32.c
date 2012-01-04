/* 	smc-ultra32.c: An SMC Ultra32 EISA ethernet driver for linux.

Sources:

	This driver is based on (cloned from) the ISA SMC Ultra driver
	written by Donald Becker. Modifications to support the EISA
	version of the card by Paul Gortmaker and Leonard N. Zubkoff.

	This software may be used and distributed according to the terms
	of the GNU General Public License, incorporated herein by reference.

Theory of Operation:

	The SMC Ultra32C card uses the SMC 83c790 chip which is also
	found on the ISA SMC Ultra cards. It has a shared memory mode of
	operation that makes it similar to the ISA version of the card.
	The main difference is that the EISA card has 32KB of RAM, but
	only an 8KB window into that memory. The EISA card also can be
	set for a bus-mastering mode of operation via the ECU, but that
	is not (and probably will never be) supported by this driver.
	The ECU should be run to enable shared memory and to disable the
	bus-mastering feature for use with linux.

	By programming the 8390 to use only 8KB RAM, the modifications
	to the ISA driver can be limited to the probe and initialization
	code. This allows easy integration of EISA support into the ISA
	driver. However, the driver development kit from SMC provided the
	register information for sliding the 8KB window, and hence the 8390
	is programmed to use the full 32KB RAM.

	Unfortunately this required code changes outside the probe/init
	routines, and thus we decided to separate the EISA driver from
	the ISA one. In this way, ISA users don't end up with a larger
	driver due to the EISA code, and EISA users don't end up with a
	larger driver due to the ISA EtherEZ PIO code. The driver is
	similar to the 3c503/16 driver, in that the window must be set
	back to the 1st 8KB of space for access to the two 8390 Tx slots.

	In testing, using only 8KB RAM (3 Tx / 5 Rx) didn't appear to
	be a limiting factor, since the EISA bus could get packets off
	the card fast enough, but having the use of lots of RAM as Rx
	space is extra insurance if interrupt latencies become excessive.

*/

static const char *version = "smc-ultra32.c: 06/97 v1.00\n";


#include <linux/module.h>
#include <linux/eisa.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>

#include <asm/io.h>
#include <asm/system.h>

#include "8390.h"

#define DRV_NAME "smc-ultra32"

static int ultra32_probe1(struct net_device *dev, int ioaddr);
static int ultra32_open(struct net_device *dev);
static void ultra32_reset_8390(struct net_device *dev);
static void ultra32_get_8390_hdr(struct net_device *dev, struct e8390_pkt_hdr *hdr,
				 int ring_page);
static void ultra32_block_input(struct net_device *dev, int count,
				struct sk_buff *skb, int ring_offset);
static void ultra32_block_output(struct net_device *dev, int count,
				 const unsigned char *buf,
				 const int start_page);
static int ultra32_close(struct net_device *dev);

#define ULTRA32_CMDREG	0	/* Offset to ASIC command register. */
#define	 ULTRA32_RESET	0x80	/* Board reset, in ULTRA32_CMDREG. */
#define	 ULTRA32_MEMENB	0x40	/* Enable the shared memory. */
#define ULTRA32_NIC_OFFSET 16	/* NIC register offset from the base_addr. */
#define ULTRA32_IO_EXTENT 32
#define EN0_ERWCNT		0x08	/* Early receive warning count. */

/*
 * Defines that apply only to the Ultra32 EISA card. Note that
 * "smc" = 10011 01101 00011 = 0x4da3, and hence !smc8010.cfg translates
 * into an EISA ID of 0x1080A34D
 */
#define ULTRA32_BASE	0xca0
#define ULTRA32_ID	0x1080a34d
#define ULTRA32_IDPORT	(-0x20)	/* 0xc80 */
/* Config regs 1->7 from the EISA !SMC8010.CFG file. */
#define ULTRA32_CFG1	0x04	/* 0xca4 */
#define ULTRA32_CFG2	0x05	/* 0xca5 */
#define ULTRA32_CFG3	(-0x18)	/* 0xc88 */
#define ULTRA32_CFG4	(-0x17)	/* 0xc89 */
#define ULTRA32_CFG5	(-0x16)	/* 0xc8a */
#define ULTRA32_CFG6	(-0x15)	/* 0xc8b */
#define ULTRA32_CFG7	0x0d	/* 0xcad */

static void cleanup_card(struct net_device *dev)
{
	int ioaddr = dev->base_addr - ULTRA32_NIC_OFFSET;
	/* NB: ultra32_close_card() does free_irq */
	release_region(ioaddr, ULTRA32_IO_EXTENT);
	iounmap(ei_status.mem);
}

/*	Probe for the Ultra32.  This looks like a 8013 with the station
	address PROM at I/O ports <base>+8 to <base>+13, with a checksum
	following.
*/

struct net_device * __init ultra32_probe(int unit)
{
	struct net_device *dev;
	int base;
	int irq;
	int err = -ENODEV;

	if (!EISA_bus)
		return ERR_PTR(-ENODEV);

	dev = alloc_ei_netdev();

	if (!dev)
		return ERR_PTR(-ENOMEM);

	if (unit >= 0) {
		sprintf(dev->name, "eth%d", unit);
		netdev_boot_setup_check(dev);
	}

	irq = dev->irq;

	/* EISA spec allows for up to 16 slots, but 8 is typical. */
	for (base = 0x1000 + ULTRA32_BASE; base < 0x9000; base += 0x1000) {
		if (ultra32_probe1(dev, base) == 0)
			break;
		dev->irq = irq;
	}
	if (base >= 0x9000)
		goto out;
	err = register_netdev(dev);
	if (err)
		goto out1;
	return dev;
out1:
	cleanup_card(dev);
out:
	free_netdev(dev);
	return ERR_PTR(err);
}


static const struct net_device_ops ultra32_netdev_ops = {
	.ndo_open 		= ultra32_open,
	.ndo_stop 		= ultra32_close,
	.ndo_start_xmit		= ei_start_xmit,
	.ndo_tx_timeout		= ei_tx_timeout,
	.ndo_get_stats		= ei_get_stats,
	.ndo_set_rx_mode	= ei_set_multicast_list,
	.ndo_validate_addr	= eth_validate_addr,
	.ndo_set_mac_address 	= eth_mac_addr,
	.ndo_change_mtu		= eth_change_mtu,
#ifdef CONFIG_NET_POLL_CONTROLLER
	.ndo_poll_controller	= ei_poll,
#endif
};

static int __init ultra32_probe1(struct net_device *dev, int ioaddr)
{
	int i, edge, media, retval;
	int checksum = 0;
	const char *model_name;
	static unsigned version_printed;
	/* Values from various config regs. */
	unsigned char idreg;
	unsigned char reg4;
	const char *ifmap[] = {"UTP No Link", "", "UTP/AUI", "UTP/BNC"};

	if (!request_region(ioaddr, ULTRA32_IO_EXTENT, DRV_NAME))
		return -EBUSY;

	if (inb(ioaddr + ULTRA32_IDPORT) == 0xff ||
	    inl(ioaddr + ULTRA32_IDPORT) != ULTRA32_ID) {
		retval = -ENODEV;
		goto out;
	}

	media = inb(ioaddr + ULTRA32_CFG7) & 0x03;
	edge = inb(ioaddr + ULTRA32_CFG5) & 0x08;
	printk("SMC Ultra32 in EISA Slot %d, Media: %s, %s IRQs.\n",
		ioaddr >> 12, ifmap[media],
		(edge ? "Edge Triggered" : "Level Sensitive"));

	idreg = inb(ioaddr + 7);
	reg4 = inb(ioaddr + 4) & 0x7f;

	/* Check the ID nibble. */
	if ((idreg & 0xf0) != 0x20) {			/* SMC Ultra */
		retval = -ENODEV;
		goto out;
	}

	/* Select the station address register set. */
	outb(reg4, ioaddr + 4);

	for (i = 0; i < 8; i++)
		checksum += inb(ioaddr + 8 + i);
	if ((checksum & 0xff) != 0xff) {
		retval = -ENODEV;
		goto out;
	}

	if (ei_debug  &&  version_printed++ == 0)
		printk(version);

	model_name = "SMC Ultra32";

	for (i = 0; i < 6; i++)
		dev->dev_addr[i] = inb(ioaddr + 8 + i);

	printk("%s: %s at 0x%X, %pM",
	       dev->name, model_name, ioaddr, dev->dev_addr);

	/* Switch from the station address to the alternate register set and
	   read the useful registers there. */
	outb(0x80 | reg4, ioaddr + 4);

	/* Enable FINE16 mode to avoid BIOS ROM width mismatches @ reboot. */
	outb(0x80 | inb(ioaddr + 0x0c), ioaddr + 0x0c);

	/* Reset RAM addr. */
	outb(0x00, ioaddr + 0x0b);

	/* Switch back to the station address register set so that the
	   MS-DOS driver can find the card after a warm boot. */
	outb(reg4, ioaddr + 4);

	if ((inb(ioaddr + ULTRA32_CFG5) & 0x40) == 0) {
		printk("\nsmc-ultra32: Card RAM is disabled!  "
		       "Run EISA config utility.\n");
		retval = -ENODEV;
		goto out;
	}
	if ((inb(ioaddr + ULTRA32_CFG2) & 0x04) == 0)
		printk("\nsmc-ultra32: Ignoring Bus-Master enable bit.  "
		       "Run EISA config utility.\n");

	if (dev->irq < 2) {
		unsigned char irqmap[] = {0, 9, 3, 5, 7, 10, 11, 15};
		int irq = irqmap[inb(ioaddr + ULTRA32_CFG5) & 0x07];
		if (irq == 0) {
			printk(", failed to detect IRQ line.\n");
			retval = -EAGAIN;
			goto out;
		}
		dev->irq = irq;
	}

	/* The 8390 isn't at the base address, so fake the offset */
	dev->base_addr = ioaddr + ULTRA32_NIC_OFFSET;

	/* Save RAM address in the unused reg0 to avoid excess inb's. */
	ei_status.reg0 = inb(ioaddr + ULTRA32_CFG3) & 0xfc;

	dev->mem_start =  0xc0000 + ((ei_status.reg0 & 0x7c) << 11);

	ei_status.name = model_name;
	ei_status.word16 = 1;
	ei_status.tx_start_page = 0;
	ei_status.rx_start_page = TX_PAGES;
	/* All Ultra32 cards have 32KB memory with an 8KB window. */
	ei_status.stop_page = 128;

	ei_status.mem = ioremap(dev->mem_start, 0x2000);
	if (!ei_status.mem) {
		printk(", failed to ioremap.\n");
		retval = -ENOMEM;
		goto out;
	}
	dev->mem_end = dev->mem_start + 0x1fff;

	printk(", IRQ %d, 32KB memory, 8KB window at 0x%lx-0x%lx.\n",
	       dev->irq, dev->mem_start, dev->mem_end);
	ei_status.block_input = &ultra32_block_input;
	ei_status.block_output = &ultra32_block_output;
	ei_status.get_8390_hdr = &ultra32_get_8390_hdr;
	ei_status.reset_8390 = &ultra32_reset_8390;

	dev->netdev_ops = &ultra32_netdev_ops;
	NS8390_init(dev, 0);

	return 0;
out:
	release_region(ioaddr, ULTRA32_IO_EXTENT);
	return retval;
}

static int ultra32_open(struct net_device *dev)
{
	int ioaddr = dev->base_addr - ULTRA32_NIC_OFFSET; /* ASIC addr */
	int irq_flags = (inb(ioaddr + ULTRA32_CFG5) & 0x08) ? 0 : IRQF_SHARED;
	int retval;

	retval = request_irq(dev->irq, ei_interrupt, irq_flags, dev->name, dev);
	if (retval)
		return retval;

	outb(ULTRA32_MEMENB, ioaddr); /* Enable Shared Memory. */
	outb(0x80, ioaddr + ULTRA32_CFG6); /* Enable Interrupts. */
	outb(0x84, ioaddr + 5);	/* Enable MEM16 & Disable Bus Master. */
	outb(0x01, ioaddr + 6);	/* Enable Interrupts. */
	/* Set the early receive warning level in window 0 high enough not
	   to receive ERW interrupts. */
	outb_p(E8390_NODMA+E8390_PAGE0, dev->base_addr);
	outb(0xff, dev->base_addr + EN0_ERWCNT);
	ei_open(dev);
	return 0;
}

static int ultra32_close(struct net_device *dev)
{
	int ioaddr = dev->base_addr - ULTRA32_NIC_OFFSET; /* CMDREG */

	netif_stop_queue(dev);

	if (ei_debug > 1)
		printk("%s: Shutting down ethercard.\n", dev->name);

	outb(0x00, ioaddr + ULTRA32_CFG6); /* Disable Interrupts. */
	outb(0x00, ioaddr + 6);		/* Disable interrupts. */
	free_irq(dev->irq, dev);

	NS8390_init(dev, 0);

	return 0;
}

static void ultra32_reset_8390(struct net_device *dev)
{
	int ioaddr = dev->base_addr - ULTRA32_NIC_OFFSET; /* ASIC base addr */

	outb(ULTRA32_RESET, ioaddr);
	if (ei_debug > 1) printk("resetting Ultra32, t=%ld...", jiffies);
	ei_status.txing = 0;

	outb(ULTRA32_MEMENB, ioaddr); /* Enable Shared Memory. */
	outb(0x80, ioaddr + ULTRA32_CFG6); /* Enable Interrupts. */
	outb(0x84, ioaddr + 5);	/* Enable MEM16 & Disable Bus Master. */
	outb(0x01, ioaddr + 6);	/* Enable Interrupts. */
	if (ei_debug > 1) printk("reset done\n");
}

/* Grab the 8390 specific header. Similar to the block_input routine, but
   we don't need to be concerned with ring wrap as the header will be at
   the start of a page, so we optimize accordingly. */

static void ultra32_get_8390_hdr(struct net_device *dev,
				 struct e8390_pkt_hdr *hdr,
				 int ring_page)
{
	void __iomem *hdr_start = ei_status.mem + ((ring_page & 0x1f) << 8);
	unsigned int RamReg = dev->base_addr - ULTRA32_NIC_OFFSET + ULTRA32_CFG3;

	/* Select correct 8KB Window. */
	outb(ei_status.reg0 | ((ring_page & 0x60) >> 5), RamReg);

#ifdef __BIG_ENDIAN
	/* Officially this is what we are doing, but the readl() is faster */
	/* unfortunately it isn't endian aware of the struct               */
	memcpy_fromio(hdr, hdr_start, sizeof(struct e8390_pkt_hdr));
	hdr->count = le16_to_cpu(hdr->count);
#else
	((unsigned int*)hdr)[0] = readl(hdr_start);
#endif
}

/* Block input and output are easy on shared memory ethercards, the only
   complication is when the ring buffer wraps, or in this case, when a
   packet spans an 8KB boundary. Note that the current 8KB segment is
   already set by the get_8390_hdr routine. */

static void ultra32_block_input(struct net_device *dev,
				int count,
				struct sk_buff *skb,
				int ring_offset)
{
	void __iomem *xfer_start = ei_status.mem + (ring_offset & 0x1fff);
	unsigned int RamReg = dev->base_addr - ULTRA32_NIC_OFFSET + ULTRA32_CFG3;

	if ((ring_offset & ~0x1fff) != ((ring_offset + count - 1) & ~0x1fff)) {
		int semi_count = 8192 - (ring_offset & 0x1FFF);
		memcpy_fromio(skb->data, xfer_start, semi_count);
		count -= semi_count;
		if (ring_offset < 96*256) {
			/* Select next 8KB Window. */
			ring_offset += semi_count;
			outb(ei_status.reg0 | ((ring_offset & 0x6000) >> 13), RamReg);
			memcpy_fromio(skb->data + semi_count, ei_status.mem, count);
		} else {
			/* Select first 8KB Window. */
			outb(ei_status.reg0, RamReg);
			memcpy_fromio(skb->data + semi_count, ei_status.mem + TX_PAGES * 256, count);
		}
	} else {
		memcpy_fromio(skb->data, xfer_start, count);
	}
}

static void ultra32_block_output(struct net_device *dev,
				 int count,
				 const unsigned char *buf,
				 int start_page)
{
	void __iomem *xfer_start = ei_status.mem + (start_page<<8);
	unsigned int RamReg = dev->base_addr - ULTRA32_NIC_OFFSET + ULTRA32_CFG3;

	/* Select first 8KB Window. */
	outb(ei_status.reg0, RamReg);

	memcpy_toio(xfer_start, buf, count);
}

#ifdef MODULE
#define MAX_ULTRA32_CARDS   4	/* Max number of Ultra cards per module */
static struct net_device *dev_ultra[MAX_ULTRA32_CARDS];

MODULE_DESCRIPTION("SMC Ultra32 EISA ethernet driver");
MODULE_LICENSE("GPL");

int __init init_module(void)
{
	int this_dev, found = 0;

	for (this_dev = 0; this_dev < MAX_ULTRA32_CARDS; this_dev++) {
		struct net_device *dev = ultra32_probe(-1);
		if (IS_ERR(dev))
			break;
		dev_ultra[found++] = dev;
	}
	if (found)
		return 0;
	printk(KERN_WARNING "smc-ultra32.c: No SMC Ultra32 found.\n");
	return -ENXIO;
}

void __exit cleanup_module(void)
{
	int this_dev;

	for (this_dev = 0; this_dev < MAX_ULTRA32_CARDS; this_dev++) {
		struct net_device *dev = dev_ultra[this_dev];
		if (dev) {
			unregister_netdev(dev);
			cleanup_card(dev);
			free_netdev(dev);
		}
	}
}
#endif /* MODULE */

