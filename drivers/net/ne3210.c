/*
	ne3210.c

	Linux driver for Novell NE3210 EISA Network Adapter

	Copyright (C) 1998, Paul Gortmaker.

	This software may be used and distributed according to the terms
	of the GNU General Public License, incorporated herein by reference.

	Information and Code Sources:

	1) Based upon my other EISA 8390 drivers (lne390, es3210, smc-ultra32)
	2) The existing myriad of other Linux 8390 drivers by Donald Becker.
	3) Info for getting IRQ and sh-mem gleaned from the EISA cfg file

	The NE3210 is an EISA shared memory NS8390 implementation.  Shared
	memory address > 1MB should work with this driver.

	Note that the .cfg file (3/11/93, v1.0) has AUI and BNC switched
	around (or perhaps there are some defective/backwards cards ???)

	This driver WILL NOT WORK FOR THE NE3200 - it is completely different
	and does not use an 8390 at all.

	Updated to EISA probing API 5/2003 by Marc Zyngier.
*/

#include <linux/module.h>
#include <linux/eisa.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/mm.h>

#include <asm/io.h>
#include <asm/system.h>

#include "8390.h"

#define DRV_NAME "ne3210"

static void ne3210_reset_8390(struct net_device *dev);

static void ne3210_get_8390_hdr(struct net_device *dev, struct e8390_pkt_hdr *hdr, int ring_page);
static void ne3210_block_input(struct net_device *dev, int count, struct sk_buff *skb, int ring_offset);
static void ne3210_block_output(struct net_device *dev, int count, const unsigned char *buf, const int start_page);

#define NE3210_START_PG		0x00    /* First page of TX buffer	*/
#define NE3210_STOP_PG		0x80    /* Last page +1 of RX ring	*/

#define NE3210_IO_EXTENT	0x20
#define NE3210_SA_PROM		0x16	/* Start of e'net addr.		*/
#define NE3210_RESET_PORT	0xc84
#define NE3210_NIC_OFFSET	0x00	/* Hello, the 8390 is *here*	*/

#define NE3210_ADDR0		0x00	/* 3 byte vendor prefix		*/
#define NE3210_ADDR1		0x00
#define NE3210_ADDR2		0x1b

#define NE3210_CFG1		0xc84	/* NB: 0xc84 is also "reset" port. */
#define NE3210_CFG2		0xc90
#define NE3210_CFG_EXTENT       (NE3210_CFG2 - NE3210_CFG1 + 1)

/*
 *	You can OR any of the following bits together and assign it
 *	to NE3210_DEBUG to get verbose driver info during operation.
 *	Currently only the probe one is implemented.
 */

#define NE3210_D_PROBE	0x01
#define NE3210_D_RX_PKT	0x02
#define NE3210_D_TX_PKT	0x04
#define NE3210_D_IRQ	0x08

#define NE3210_DEBUG	0x0

static unsigned char irq_map[] __initdata = {15, 12, 11, 10, 9, 7, 5, 3};
static unsigned int shmem_map[] __initdata = {0xff0, 0xfe0, 0xfff0, 0xd8, 0xffe0, 0xffc0, 0xd0, 0x0};
static const char *ifmap[] __initdata = {"UTP", "?", "BNC", "AUI"};
static int ifmap_val[] __initdata = {
		IF_PORT_10BASET,
		IF_PORT_UNKNOWN,
		IF_PORT_10BASE2,
		IF_PORT_AUI,
};

static int __init ne3210_eisa_probe (struct device *device)
{
	unsigned long ioaddr, phys_mem;
	int i, retval, port_index;
	struct eisa_device *edev = to_eisa_device (device);
	struct net_device *dev;

	/* Allocate dev->priv and fill in 8390 specific dev fields. */
	if (!(dev = alloc_ei_netdev ())) {
		printk ("ne3210.c: unable to allocate memory for dev!\n");
		return -ENOMEM;
	}

	SET_NETDEV_DEV(dev, device);
	device->driver_data = dev;
	ioaddr = edev->base_addr;

	if (!request_region(ioaddr, NE3210_IO_EXTENT, DRV_NAME)) {
		retval = -EBUSY;
		goto out;
	}

	if (!request_region(ioaddr + NE3210_CFG1,
			    NE3210_CFG_EXTENT, DRV_NAME)) {
		retval = -EBUSY;
		goto out1;
	}

#if NE3210_DEBUG & NE3210_D_PROBE
	printk("ne3210-debug: probe at %#x, ID %s\n", ioaddr, edev->id.sig);
	printk("ne3210-debug: config regs: %#x %#x\n",
		inb(ioaddr + NE3210_CFG1), inb(ioaddr + NE3210_CFG2));
#endif

	port_index = inb(ioaddr + NE3210_CFG2) >> 6;
	for(i = 0; i < ETHER_ADDR_LEN; i++)
		dev->dev_addr[i] = inb(ioaddr + NE3210_SA_PROM + i);
	printk("ne3210.c: NE3210 in EISA slot %d, media: %s, addr: %pM.\n",
		edev->slot, ifmap[port_index], dev->dev_addr);

	/* Snarf the interrupt now. CFG file has them all listed as `edge' with share=NO */
	dev->irq = irq_map[(inb(ioaddr + NE3210_CFG2) >> 3) & 0x07];
	printk("ne3210.c: using IRQ %d, ", dev->irq);

	retval = request_irq(dev->irq, ei_interrupt, 0, DRV_NAME, dev);
	if (retval) {
		printk (" unable to get IRQ %d.\n", dev->irq);
		goto out2;
	}

	phys_mem = shmem_map[inb(ioaddr + NE3210_CFG2) & 0x07] * 0x1000;

	/*
	   BEWARE!! Some dain-bramaged EISA SCUs will allow you to put
	   the card mem within the region covered by `normal' RAM  !!!
	*/
	if (phys_mem > 1024*1024) {	/* phys addr > 1MB */
		if (phys_mem < virt_to_phys(high_memory)) {
			printk(KERN_CRIT "ne3210.c: Card RAM overlaps with normal memory!!!\n");
			printk(KERN_CRIT "ne3210.c: Use EISA SCU to set card memory below 1MB,\n");
			printk(KERN_CRIT "ne3210.c: or to an address above 0x%lx.\n", virt_to_phys(high_memory));
			printk(KERN_CRIT "ne3210.c: Driver NOT installed.\n");
			retval = -EINVAL;
			goto out3;
		}
	}

	if (!request_mem_region (phys_mem, NE3210_STOP_PG*0x100, DRV_NAME)) {
		printk ("ne3210.c: Unable to request shared memory at physical address %#lx\n",
			phys_mem);
		goto out3;
	}

	printk("%dkB memory at physical address %#lx\n",
	       NE3210_STOP_PG/4, phys_mem);

	ei_status.mem = ioremap(phys_mem, NE3210_STOP_PG*0x100);
	if (!ei_status.mem) {
		printk(KERN_ERR "ne3210.c: Unable to remap card memory !!\n");
		printk(KERN_ERR "ne3210.c: Driver NOT installed.\n");
		retval = -EAGAIN;
		goto out4;
	}
	printk("ne3210.c: remapped %dkB card memory to virtual address %p\n",
	       NE3210_STOP_PG/4, ei_status.mem);
	dev->mem_start = (unsigned long)ei_status.mem;
	dev->mem_end = dev->mem_start + (NE3210_STOP_PG - NE3210_START_PG)*256;

	/* The 8390 offset is zero for the NE3210 */
	dev->base_addr = ioaddr;

	ei_status.name = "NE3210";
	ei_status.tx_start_page = NE3210_START_PG;
	ei_status.rx_start_page = NE3210_START_PG + TX_PAGES;
	ei_status.stop_page = NE3210_STOP_PG;
	ei_status.word16 = 1;
	ei_status.priv = phys_mem;

	if (ei_debug > 0)
		printk("ne3210 loaded.\n");

	ei_status.reset_8390 = &ne3210_reset_8390;
	ei_status.block_input = &ne3210_block_input;
	ei_status.block_output = &ne3210_block_output;
	ei_status.get_8390_hdr = &ne3210_get_8390_hdr;

	dev->netdev_ops = &ei_netdev_ops;

	dev->if_port = ifmap_val[port_index];

	if ((retval = register_netdev (dev)))
		goto out5;

	NS8390_init(dev, 0);
	return 0;

 out5:
	iounmap(ei_status.mem);
 out4:
	release_mem_region (phys_mem, NE3210_STOP_PG*0x100);
 out3:
	free_irq (dev->irq, dev);
 out2:
	release_region (ioaddr + NE3210_CFG1, NE3210_CFG_EXTENT);
 out1:
	release_region (ioaddr, NE3210_IO_EXTENT);
 out:
	free_netdev (dev);

	return retval;
}

static int __devexit ne3210_eisa_remove (struct device *device)
{
	struct net_device  *dev    = device->driver_data;
	unsigned long       ioaddr = to_eisa_device (device)->base_addr;

	unregister_netdev (dev);
	iounmap(ei_status.mem);
	release_mem_region (ei_status.priv, NE3210_STOP_PG*0x100);
	free_irq (dev->irq, dev);
	release_region (ioaddr + NE3210_CFG1, NE3210_CFG_EXTENT);
	release_region (ioaddr, NE3210_IO_EXTENT);
	free_netdev (dev);

	return 0;
}

/*
 *	Reset by toggling the "Board Enable" bits (bit 2 and 0).
 */

static void ne3210_reset_8390(struct net_device *dev)
{
	unsigned short ioaddr = dev->base_addr;

	outb(0x04, ioaddr + NE3210_RESET_PORT);
	if (ei_debug > 1) printk("%s: resetting the NE3210...", dev->name);

	mdelay(2);

	ei_status.txing = 0;
	outb(0x01, ioaddr + NE3210_RESET_PORT);
	if (ei_debug > 1) printk("reset done\n");

	return;
}

/*
 *	Note: In the following three functions is the implicit assumption
 *	that the associated memcpy will only use "rep; movsl" as long as
 *	we keep the counts as some multiple of doublewords. This is a
 *	requirement of the hardware, and also prevents us from using
 *	eth_io_copy_and_sum() since we can't guarantee it will limit
 *	itself to doubleword access.
 */

/*
 *	Grab the 8390 specific header. Similar to the block_input routine, but
 *	we don't need to be concerned with ring wrap as the header will be at
 *	the start of a page, so we optimize accordingly. (A single doubleword.)
 */

static void
ne3210_get_8390_hdr(struct net_device *dev, struct e8390_pkt_hdr *hdr, int ring_page)
{
	void __iomem *hdr_start = ei_status.mem + ((ring_page - NE3210_START_PG)<<8);
	memcpy_fromio(hdr, hdr_start, sizeof(struct e8390_pkt_hdr));
	hdr->count = (hdr->count + 3) & ~3;     /* Round up allocation. */
}

/*
 *	Block input and output are easy on shared memory ethercards, the only
 *	complication is when the ring buffer wraps. The count will already
 *	be rounded up to a doubleword value via ne3210_get_8390_hdr() above.
 */

static void ne3210_block_input(struct net_device *dev, int count, struct sk_buff *skb,
						  int ring_offset)
{
	void __iomem *start = ei_status.mem + ring_offset - NE3210_START_PG*256;

	if (ring_offset + count > NE3210_STOP_PG*256) {
		/* Packet wraps over end of ring buffer. */
		int semi_count = NE3210_STOP_PG*256 - ring_offset;
		memcpy_fromio(skb->data, start, semi_count);
		count -= semi_count;
		memcpy_fromio(skb->data + semi_count,
				ei_status.mem + TX_PAGES*256, count);
	} else {
		/* Packet is in one chunk. */
		memcpy_fromio(skb->data, start, count);
	}
}

static void ne3210_block_output(struct net_device *dev, int count,
				const unsigned char *buf, int start_page)
{
	void __iomem *shmem = ei_status.mem + ((start_page - NE3210_START_PG)<<8);

	count = (count + 3) & ~3;     /* Round up to doubleword */
	memcpy_toio(shmem, buf, count);
}

static struct eisa_device_id ne3210_ids[] = {
	{ "EGL0101" },
	{ "NVL1801" },
	{ "" },
};
MODULE_DEVICE_TABLE(eisa, ne3210_ids);

static struct eisa_driver ne3210_eisa_driver = {
	.id_table = ne3210_ids,
	.driver   = {
		.name   = "ne3210",
		.probe  = ne3210_eisa_probe,
		.remove = __devexit_p (ne3210_eisa_remove),
	},
};

MODULE_DESCRIPTION("NE3210 EISA Ethernet driver");
MODULE_LICENSE("GPL");
MODULE_DEVICE_TABLE(eisa, ne3210_ids);

static int ne3210_init(void)
{
	return eisa_driver_register (&ne3210_eisa_driver);
}

static void ne3210_cleanup(void)
{
	eisa_driver_unregister (&ne3210_eisa_driver);
}

module_init (ne3210_init);
module_exit (ne3210_cleanup);
