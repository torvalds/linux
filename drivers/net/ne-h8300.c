/* ne-h8300.c: A NE2000 clone on H8/300 driver for linux. */
/*
    original ne.c
    Written 1992-94 by Donald Becker.

    Copyright 1993 United States Government as represented by the
    Director, National Security Agency.

    This software may be used and distributed according to the terms
    of the GNU General Public License, incorporated herein by reference.

    The author may be reached as becker@scyld.com, or C/O
    Scyld Computing Corporation, 410 Severn Ave., Suite 210, Annapolis MD 21403

    H8/300 modified
    Yoshinori Sato <ysato@users.sourceforge.jp>
*/

static const char version1[] =
"ne-h8300.c:v1.00 2004/04/11 ysato\n";

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/jiffies.h>

#include <asm/system.h>
#include <asm/io.h>
#include <asm/irq.h>

#define EI_SHIFT(x)	(ei_local->reg_offset[x])

#include "8390.h"

#define DRV_NAME "ne-h8300"

/* Some defines that people can play with if so inclined. */

/* Do we perform extra sanity checks on stuff ? */
/* #define NE_SANITY_CHECK */

/* Do we implement the read before write bugfix ? */
/* #define NE_RW_BUGFIX */

/* Do we have a non std. amount of memory? (in units of 256 byte pages) */
/* #define PACKETBUF_MEMSIZE	0x40 */

/* A zero-terminated list of I/O addresses to be probed at boot. */

/* ---- No user-serviceable parts below ---- */

static const char version[] =
    "8390.c:v1.10cvs 9/23/94 Donald Becker (becker@cesdis.gsfc.nasa.gov)\n";

#include "lib8390.c"

#define NE_BASE	 (dev->base_addr)
#define NE_CMD	 	0x00
#define NE_DATAPORT	(ei_status.word16?0x20:0x10)	/* NatSemi-defined port window offset. */
#define NE_RESET	(ei_status.word16?0x3f:0x1f)	/* Issue a read to reset, a write to clear. */
#define NE_IO_EXTENT	(ei_status.word16?0x40:0x20)

#define NESM_START_PG	0x40	/* First page of TX buffer */
#define NESM_STOP_PG	0x80	/* Last page +1 of RX ring */

static int ne_probe1(struct net_device *dev, int ioaddr);

static int ne_open(struct net_device *dev);
static int ne_close(struct net_device *dev);

static void ne_reset_8390(struct net_device *dev);
static void ne_get_8390_hdr(struct net_device *dev, struct e8390_pkt_hdr *hdr,
			  int ring_page);
static void ne_block_input(struct net_device *dev, int count,
			  struct sk_buff *skb, int ring_offset);
static void ne_block_output(struct net_device *dev, const int count,
		const unsigned char *buf, const int start_page);


static u32 reg_offset[16];

static int __init init_reg_offset(struct net_device *dev,unsigned long base_addr)
{
	struct ei_device *ei_local = (struct ei_device *) netdev_priv(dev);
	int i;
	unsigned char bus_width;

	bus_width = *(volatile unsigned char *)ABWCR;
	bus_width &= 1 << ((base_addr >> 21) & 7);

	for (i = 0; i < sizeof(reg_offset) / sizeof(u32); i++)
		if (bus_width == 0)
			reg_offset[i] = i * 2 + 1;
		else
			reg_offset[i] = i;

	ei_local->reg_offset = reg_offset;
	return 0;
}

static int __initdata h8300_ne_count = 0;
#ifdef CONFIG_H8300H_H8MAX
static unsigned long __initdata h8300_ne_base[] = { 0x800600 };
static int h8300_ne_irq[] = {EXT_IRQ4};
#endif
#ifdef CONFIG_H8300H_AKI3068NET
static unsigned long __initdata h8300_ne_base[] = { 0x200000 };
static int h8300_ne_irq[] = {EXT_IRQ5};
#endif

static inline int init_dev(struct net_device *dev)
{
	if (h8300_ne_count < (sizeof(h8300_ne_base) / sizeof(unsigned long))) {
		dev->base_addr = h8300_ne_base[h8300_ne_count];
		dev->irq       = h8300_ne_irq[h8300_ne_count];
		h8300_ne_count++;
		return 0;
	} else
		return -ENODEV;
}

/*  Probe for various non-shared-memory ethercards.

   NEx000-clone boards have a Station Address PROM (SAPROM) in the packet
   buffer memory space.  NE2000 clones have 0x57,0x57 in bytes 0x0e,0x0f of
   the SAPROM, while other supposed NE2000 clones must be detected by their
   SA prefix.

   Reading the SAPROM from a word-wide card with the 8390 set in byte-wide
   mode results in doubled values, which can be detected and compensated for.

   The probe is also responsible for initializing the card and filling
   in the 'dev' and 'ei_status' structures.

   We use the minimum memory size for some ethercard product lines, iff we can't
   distinguish models.  You can increase the packet buffer size by setting
   PACKETBUF_MEMSIZE.  Reported Cabletron packet buffer locations are:
	E1010   starts at 0x100 and ends at 0x2000.
	E1010-x starts at 0x100 and ends at 0x8000. ("-x" means "more memory")
	E2010	 starts at 0x100 and ends at 0x4000.
	E2010-x starts at 0x100 and ends at 0xffff.  */

static int __init do_ne_probe(struct net_device *dev)
{
	unsigned int base_addr = dev->base_addr;

	SET_MODULE_OWNER(dev);

	/* First check any supplied i/o locations. User knows best. <cough> */
	if (base_addr > 0x1ff)	/* Check a single specified location. */
		return ne_probe1(dev, base_addr);
	else if (base_addr != 0)	/* Don't probe at all. */
		return -ENXIO;

	return -ENODEV;
}

static void cleanup_card(struct net_device *dev)
{
	free_irq(dev->irq, dev);
	release_region(dev->base_addr, NE_IO_EXTENT);
}

#ifndef MODULE
struct net_device * __init ne_probe(int unit)
{
	struct net_device *dev = ____alloc_ei_netdev(0);
	int err;

	if (!dev)
		return ERR_PTR(-ENOMEM);

	if (init_dev(dev))
		return ERR_PTR(-ENODEV);

	sprintf(dev->name, "eth%d", unit);
	netdev_boot_setup_check(dev);

	err = init_reg_offset(dev, dev->base_addr);
	if (err)
		goto out;

	err = do_ne_probe(dev);
	if (err)
		goto out;
	return dev;
out:
	free_netdev(dev);
	return ERR_PTR(err);
}
#endif

static int __init ne_probe1(struct net_device *dev, int ioaddr)
{
	int i;
	unsigned char SA_prom[16];
	int wordlength = 2;
	const char *name = NULL;
	int start_page, stop_page;
	int reg0, ret;
	static unsigned version_printed;
	struct ei_device *ei_local = (struct ei_device *) netdev_priv(dev);
	unsigned char bus_width;

	if (!request_region(ioaddr, NE_IO_EXTENT, DRV_NAME))
		return -EBUSY;

	reg0 = inb_p(ioaddr);
	if (reg0 == 0xFF) {
		ret = -ENODEV;
		goto err_out;
	}

	/* Do a preliminary verification that we have a 8390. */
	{
		int regd;
		outb_p(E8390_NODMA+E8390_PAGE1+E8390_STOP, ioaddr + E8390_CMD);
		regd = inb_p(ioaddr + EI_SHIFT(0x0d));
		outb_p(0xff, ioaddr + EI_SHIFT(0x0d));
		outb_p(E8390_NODMA+E8390_PAGE0, ioaddr + E8390_CMD);
		inb_p(ioaddr + EN0_COUNTER0); /* Clear the counter by reading. */
		if (inb_p(ioaddr + EN0_COUNTER0) != 0) {
			outb_p(reg0, ioaddr + EI_SHIFT(0));
			outb_p(regd, ioaddr + EI_SHIFT(0x0d));	/* Restore the old values. */
			ret = -ENODEV;
			goto err_out;
		}
	}

	if (ei_debug  &&  version_printed++ == 0)
		printk(KERN_INFO "%s", version1);

	printk(KERN_INFO "NE*000 ethercard probe at %08x:", ioaddr);

	/* Read the 16 bytes of station address PROM.
	   We must first initialize registers, similar to NS8390_init(eifdev, 0).
	   We can't reliably read the SAPROM address without this.
	   (I learned the hard way!). */
	{
		struct {unsigned char value, offset; } program_seq[] =
		{
			{E8390_NODMA+E8390_PAGE0+E8390_STOP, E8390_CMD}, /* Select page 0*/
			{0x48,	EN0_DCFG},	/* Set byte-wide (0x48) access. */
			{0x00,	EN0_RCNTLO},	/* Clear the count regs. */
			{0x00,	EN0_RCNTHI},
			{0x00,	EN0_IMR},	/* Mask completion irq. */
			{0xFF,	EN0_ISR},
			{E8390_RXOFF, EN0_RXCR},	/* 0x20  Set to monitor */
			{E8390_TXOFF, EN0_TXCR},	/* 0x02  and loopback mode. */
			{32,	EN0_RCNTLO},
			{0x00,	EN0_RCNTHI},
			{0x00,	EN0_RSARLO},	/* DMA starting at 0x0000. */
			{0x00,	EN0_RSARHI},
			{E8390_RREAD+E8390_START, E8390_CMD},
		};

		for (i = 0; i < sizeof(program_seq)/sizeof(program_seq[0]); i++)
			outb_p(program_seq[i].value, ioaddr + program_seq[i].offset);

	}
	bus_width = *(volatile unsigned char *)ABWCR;
	bus_width &= 1 << ((ioaddr >> 21) & 7);
	ei_status.word16 = (bus_width == 0); /* temporary setting */
	for(i = 0; i < 16 /*sizeof(SA_prom)*/; i++) {
		SA_prom[i] = inb_p(ioaddr + NE_DATAPORT);
		inb_p(ioaddr + NE_DATAPORT); /* dummy read */
	}

	start_page = NESM_START_PG;
	stop_page = NESM_STOP_PG;

	if (bus_width)
		wordlength = 1;
	else
		outb_p(0x49, ioaddr + EN0_DCFG);

	/* Set up the rest of the parameters. */
	name = (wordlength == 2) ? "NE2000" : "NE1000";

	if (! dev->irq) {
		printk(" failed to detect IRQ line.\n");
		ret = -EAGAIN;
		goto err_out;
	}

	/* Snarf the interrupt now.  There's no point in waiting since we cannot
	   share and the board will usually be enabled. */
	ret = request_irq(dev->irq, __ei_interrupt, 0, name, dev);
	if (ret) {
		printk (" unable to get IRQ %d (errno=%d).\n", dev->irq, ret);
		goto err_out;
	}

	dev->base_addr = ioaddr;

	for(i = 0; i < ETHER_ADDR_LEN; i++) {
		printk(" %2.2x", SA_prom[i]);
		dev->dev_addr[i] = SA_prom[i];
	}

	printk("\n%s: %s found at %#x, using IRQ %d.\n",
		dev->name, name, ioaddr, dev->irq);

	ei_status.name = name;
	ei_status.tx_start_page = start_page;
	ei_status.stop_page = stop_page;
	ei_status.word16 = (wordlength == 2);

	ei_status.rx_start_page = start_page + TX_PAGES;
#ifdef PACKETBUF_MEMSIZE
	 /* Allow the packet buffer size to be overridden by know-it-alls. */
	ei_status.stop_page = ei_status.tx_start_page + PACKETBUF_MEMSIZE;
#endif

	ei_status.reset_8390 = &ne_reset_8390;
	ei_status.block_input = &ne_block_input;
	ei_status.block_output = &ne_block_output;
	ei_status.get_8390_hdr = &ne_get_8390_hdr;
	ei_status.priv = 0;
	dev->open = &ne_open;
	dev->stop = &ne_close;
#ifdef CONFIG_NET_POLL_CONTROLLER
	dev->poll_controller = __ei_poll;
#endif
	__NS8390_init(dev, 0);

	ret = register_netdev(dev);
	if (ret)
		goto out_irq;
	return 0;
out_irq:
	free_irq(dev->irq, dev);
err_out:
	release_region(ioaddr, NE_IO_EXTENT);
	return ret;
}

static int ne_open(struct net_device *dev)
{
	__ei_open(dev);
	return 0;
}

static int ne_close(struct net_device *dev)
{
	if (ei_debug > 1)
		printk(KERN_DEBUG "%s: Shutting down ethercard.\n", dev->name);
	__ei_close(dev);
	return 0;
}

/* Hard reset the card.  This used to pause for the same period that a
   8390 reset command required, but that shouldn't be necessary. */

static void ne_reset_8390(struct net_device *dev)
{
	unsigned long reset_start_time = jiffies;
	struct ei_device *ei_local = (struct ei_device *) netdev_priv(dev);

	if (ei_debug > 1)
		printk(KERN_DEBUG "resetting the 8390 t=%ld...", jiffies);

	/* DON'T change these to inb_p/outb_p or reset will fail on clones. */
	outb(inb(NE_BASE + NE_RESET), NE_BASE + NE_RESET);

	ei_status.txing = 0;
	ei_status.dmaing = 0;

	/* This check _should_not_ be necessary, omit eventually. */
	while ((inb_p(NE_BASE+EN0_ISR) & ENISR_RESET) == 0)
		if (time_after(jiffies, reset_start_time + 2*HZ/100)) {
			printk(KERN_WARNING "%s: ne_reset_8390() did not complete.\n", dev->name);
			break;
		}
	outb_p(ENISR_RESET, NE_BASE + EN0_ISR);	/* Ack intr. */
}

/* Grab the 8390 specific header. Similar to the block_input routine, but
   we don't need to be concerned with ring wrap as the header will be at
   the start of a page, so we optimize accordingly. */

static void ne_get_8390_hdr(struct net_device *dev, struct e8390_pkt_hdr *hdr, int ring_page)
{
	struct ei_device *ei_local = (struct ei_device *) netdev_priv(dev);
	/* This *shouldn't* happen. If it does, it's the last thing you'll see */

	if (ei_status.dmaing)
	{
		printk(KERN_EMERG "%s: DMAing conflict in ne_get_8390_hdr "
			"[DMAstat:%d][irqlock:%d].\n",
			dev->name, ei_status.dmaing, ei_status.irqlock);
		return;
	}

	ei_status.dmaing |= 0x01;
	outb_p(E8390_NODMA+E8390_PAGE0+E8390_START, NE_BASE + NE_CMD);
	outb_p(sizeof(struct e8390_pkt_hdr), NE_BASE + EN0_RCNTLO);
	outb_p(0, NE_BASE + EN0_RCNTHI);
	outb_p(0, NE_BASE + EN0_RSARLO);		/* On page boundary */
	outb_p(ring_page, NE_BASE + EN0_RSARHI);
	outb_p(E8390_RREAD+E8390_START, NE_BASE + NE_CMD);

	if (ei_status.word16) {
		int len;
		unsigned short *p = (unsigned short *)hdr;
		for (len = sizeof(struct e8390_pkt_hdr)>>1; len > 0; len--)
			*p++ = inw(NE_BASE + NE_DATAPORT);
	} else
		insb(NE_BASE + NE_DATAPORT, hdr, sizeof(struct e8390_pkt_hdr));

	outb_p(ENISR_RDC, NE_BASE + EN0_ISR);	/* Ack intr. */
	ei_status.dmaing &= ~0x01;

	le16_to_cpus(&hdr->count);
}

/* Block input and output, similar to the Crynwr packet driver.  If you
   are porting to a new ethercard, look at the packet driver source for hints.
   The NEx000 doesn't share the on-board packet memory -- you have to put
   the packet out through the "remote DMA" dataport using outb. */

static void ne_block_input(struct net_device *dev, int count, struct sk_buff *skb, int ring_offset)
{
	struct ei_device *ei_local = (struct ei_device *) netdev_priv(dev);
#ifdef NE_SANITY_CHECK
	int xfer_count = count;
#endif
	char *buf = skb->data;

	/* This *shouldn't* happen. If it does, it's the last thing you'll see */
	if (ei_status.dmaing)
	{
		printk(KERN_EMERG "%s: DMAing conflict in ne_block_input "
			"[DMAstat:%d][irqlock:%d].\n",
			dev->name, ei_status.dmaing, ei_status.irqlock);
		return;
	}
	ei_status.dmaing |= 0x01;
	outb_p(E8390_NODMA+E8390_PAGE0+E8390_START, NE_BASE + NE_CMD);
	outb_p(count & 0xff, NE_BASE + EN0_RCNTLO);
	outb_p(count >> 8, NE_BASE + EN0_RCNTHI);
	outb_p(ring_offset & 0xff, NE_BASE + EN0_RSARLO);
	outb_p(ring_offset >> 8, NE_BASE + EN0_RSARHI);
	outb_p(E8390_RREAD+E8390_START, NE_BASE + NE_CMD);
	if (ei_status.word16)
	{
		int len;
		unsigned short *p = (unsigned short *)buf;
		for (len = count>>1; len > 0; len--)
			*p++ = inw(NE_BASE + NE_DATAPORT);
		if (count & 0x01)
		{
			buf[count-1] = inb(NE_BASE + NE_DATAPORT);
#ifdef NE_SANITY_CHECK
			xfer_count++;
#endif
		}
	} else {
		insb(NE_BASE + NE_DATAPORT, buf, count);
	}

#ifdef NE_SANITY_CHECK
	/* This was for the ALPHA version only, but enough people have
	   been encountering problems so it is still here.  If you see
	   this message you either 1) have a slightly incompatible clone
	   or 2) have noise/speed problems with your bus. */

	if (ei_debug > 1)
	{
		/* DMA termination address check... */
		int addr, tries = 20;
		do {
			/* DON'T check for 'inb_p(EN0_ISR) & ENISR_RDC' here
			   -- it's broken for Rx on some cards! */
			int high = inb_p(NE_BASE + EN0_RSARHI);
			int low = inb_p(NE_BASE + EN0_RSARLO);
			addr = (high << 8) + low;
			if (((ring_offset + xfer_count) & 0xff) == low)
				break;
		} while (--tries > 0);
	 	if (tries <= 0)
			printk(KERN_WARNING "%s: RX transfer address mismatch,"
				"%#4.4x (expected) vs. %#4.4x (actual).\n",
				dev->name, ring_offset + xfer_count, addr);
	}
#endif
	outb_p(ENISR_RDC, NE_BASE + EN0_ISR);	/* Ack intr. */
	ei_status.dmaing &= ~0x01;
}

static void ne_block_output(struct net_device *dev, int count,
		const unsigned char *buf, const int start_page)
{
	struct ei_device *ei_local = (struct ei_device *) netdev_priv(dev);
	unsigned long dma_start;
#ifdef NE_SANITY_CHECK
	int retries = 0;
#endif

	/* Round the count up for word writes.  Do we need to do this?
	   What effect will an odd byte count have on the 8390?
	   I should check someday. */

	if (ei_status.word16 && (count & 0x01))
		count++;

	/* This *shouldn't* happen. If it does, it's the last thing you'll see */
	if (ei_status.dmaing)
	{
		printk(KERN_EMERG "%s: DMAing conflict in ne_block_output."
			"[DMAstat:%d][irqlock:%d]\n",
			dev->name, ei_status.dmaing, ei_status.irqlock);
		return;
	}
	ei_status.dmaing |= 0x01;
	/* We should already be in page 0, but to be safe... */
	outb_p(E8390_PAGE0+E8390_START+E8390_NODMA, NE_BASE + NE_CMD);

#ifdef NE_SANITY_CHECK
retry:
#endif

#ifdef NE8390_RW_BUGFIX
	/* Handle the read-before-write bug the same way as the
	   Crynwr packet driver -- the NatSemi method doesn't work.
	   Actually this doesn't always work either, but if you have
	   problems with your NEx000 this is better than nothing! */

	outb_p(0x42, NE_BASE + EN0_RCNTLO);
	outb_p(0x00, NE_BASE + EN0_RCNTHI);
	outb_p(0x42, NE_BASE + EN0_RSARLO);
	outb_p(0x00, NE_BASE + EN0_RSARHI);
	outb_p(E8390_RREAD+E8390_START, NE_BASE + NE_CMD);
	/* Make certain that the dummy read has occurred. */
	udelay(6);
#endif

	outb_p(ENISR_RDC, NE_BASE + EN0_ISR);

	/* Now the normal output. */
	outb_p(count & 0xff, NE_BASE + EN0_RCNTLO);
	outb_p(count >> 8,   NE_BASE + EN0_RCNTHI);
	outb_p(0x00, NE_BASE + EN0_RSARLO);
	outb_p(start_page, NE_BASE + EN0_RSARHI);

	outb_p(E8390_RWRITE+E8390_START, NE_BASE + NE_CMD);
	if (ei_status.word16) {
		int len;
		unsigned short *p = (unsigned short *)buf;
		for (len = count>>1; len > 0; len--)
			outw(*p++, NE_BASE + NE_DATAPORT);
	} else {
		outsb(NE_BASE + NE_DATAPORT, buf, count);
	}

	dma_start = jiffies;

#ifdef NE_SANITY_CHECK
	/* This was for the ALPHA version only, but enough people have
	   been encountering problems so it is still here. */

	if (ei_debug > 1)
	{
		/* DMA termination address check... */
		int addr, tries = 20;
		do {
			int high = inb_p(NE_BASE + EN0_RSARHI);
			int low = inb_p(NE_BASE + EN0_RSARLO);
			addr = (high << 8) + low;
			if ((start_page << 8) + count == addr)
				break;
		} while (--tries > 0);

		if (tries <= 0)
		{
			printk(KERN_WARNING "%s: Tx packet transfer address mismatch,"
				"%#4.4x (expected) vs. %#4.4x (actual).\n",
				dev->name, (start_page << 8) + count, addr);
			if (retries++ == 0)
				goto retry;
		}
	}
#endif

	while ((inb_p(NE_BASE + EN0_ISR) & ENISR_RDC) == 0)
		if (time_after(jiffies, dma_start + 2*HZ/100)) {		/* 20ms */
			printk(KERN_WARNING "%s: timeout waiting for Tx RDC.\n", dev->name);
			ne_reset_8390(dev);
			__NS8390_init(dev,1);
			break;
		}

	outb_p(ENISR_RDC, NE_BASE + EN0_ISR);	/* Ack intr. */
	ei_status.dmaing &= ~0x01;
	return;
}


#ifdef MODULE
#define MAX_NE_CARDS	1	/* Max number of NE cards per module */
static struct net_device *dev_ne[MAX_NE_CARDS];
static int io[MAX_NE_CARDS];
static int irq[MAX_NE_CARDS];
static int bad[MAX_NE_CARDS];	/* 0xbad = bad sig or no reset ack */

module_param_array(io, int, NULL, 0);
module_param_array(irq, int, NULL, 0);
module_param_array(bad, int, NULL, 0);
MODULE_PARM_DESC(io, "I/O base address(es)");
MODULE_PARM_DESC(irq, "IRQ number(s)");
MODULE_DESCRIPTION("H8/300 NE2000 Ethernet driver");
MODULE_LICENSE("GPL");

/* This is set up so that no ISA autoprobe takes place. We can't guarantee
that the ne2k probe is the last 8390 based probe to take place (as it
is at boot) and so the probe will get confused by any other 8390 cards.
ISA device autoprobes on a running machine are not recommended anyway. */

int init_module(void)
{
	int this_dev, found = 0;
	int err;

	for (this_dev = 0; this_dev < MAX_NE_CARDS; this_dev++) {
		struct net_device *dev = ____alloc_ei_netdev(0);
		if (!dev)
			break;
		if (io[this_dev]) {
			dev->irq = irq[this_dev];
			dev->mem_end = bad[this_dev];
			dev->base_addr = io[this_dev];
		} else {
			dev->base_addr = h8300_ne_base[this_dev];
			dev->irq = h8300_ne_irq[this_dev];
		}
		err = init_reg_offset(dev, dev->base_addr);
		if (!err) {
			if (do_ne_probe(dev) == 0) {
				dev_ne[found++] = dev;
				continue;
			}
		}
		free_netdev(dev);
		if (found)
			break;
		if (io[this_dev] != 0)
			printk(KERN_WARNING "ne.c: No NE*000 card found at i/o = %#x\n", dev->base_addr);
		else
			printk(KERN_NOTICE "ne.c: You must supply \"io=0xNNN\" value(s) for ISA cards.\n");
		return -ENXIO;
	}
	if (found)
		return 0;
	return -ENODEV;
}

void cleanup_module(void)
{
	int this_dev;

	for (this_dev = 0; this_dev < MAX_NE_CARDS; this_dev++) {
		struct net_device *dev = dev_ne[this_dev];
		if (dev) {
			unregister_netdev(dev);
			cleanup_card(dev);
			free_netdev(dev);
		}
	}
}
#endif /* MODULE */
