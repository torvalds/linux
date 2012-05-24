/* ne2.c: A NE/2 Ethernet Driver for Linux. */
/*
   Based on the NE2000 driver written by Donald Becker (1992-94).
   modified by Wim Dumon (Apr 1996)

   This software may be used and distributed according to the terms
   of the GNU General Public License, incorporated herein by reference.

   The author may be reached as wimpie@linux.cc.kuleuven.ac.be

   Currently supported: NE/2
   This patch was never tested on other MCA-ethernet adapters, but it
   might work. Just give it a try and let me know if you have problems.
   Also mail me if it really works, please!

   Changelog:
   Mon Feb  3 16:26:02 MET 1997
   - adapted the driver to work with the 2.1.25 kernel
   - multiple ne2 support (untested)
   - module support (untested)

   Fri Aug 28 00:18:36 CET 1998 (David Weinehall)
   - fixed a few minor typos
   - made the MODULE_PARM conditional (it only works with the v2.1.x kernels)
   - fixed the module support (Now it's working...)

   Mon Sep  7 19:01:44 CET 1998 (David Weinehall)
   - added support for Arco Electronics AE/2-card (experimental)

   Mon Sep 14 09:53:42 CET 1998 (David Weinehall)
   - added support for Compex ENET-16MC/P (experimental)

   Tue Sep 15 16:21:12 CET 1998 (David Weinehall, Magnus Jonsson, Tomas Ogren)
   - Miscellaneous bugfixes

   Tue Sep 19 16:21:12 CET 1998 (Magnus Jonsson)
   - Cleanup

   Wed Sep 23 14:33:34 CET 1998 (David Weinehall)
   - Restructuring and rewriting for v2.1.x compliance

   Wed Oct 14 17:19:21 CET 1998 (David Weinehall)
   - Added code that unregisters irq and proc-info
   - Version# bump

   Mon Nov 16 15:28:23 CET 1998 (Wim Dumon)
   - pass 'dev' as last parameter of request_irq in stead of 'NULL'

   Wed Feb  7 21:24:00 CET 2001 (Alfred Arnold)
   - added support for the D-Link DE-320CT

   *    WARNING
	-------
	This is alpha-test software.  It is not guaranteed to work. As a
	matter of fact, I'm quite sure there are *LOTS* of bugs in here. I
	would like to hear from you if you use this driver, even if it works.
	If it doesn't work, be sure to send me a mail with the problems !
*/

static const char *version = "ne2.c:v0.91 Nov 16 1998 Wim Dumon <wimpie@kotnet.org>\n";

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/fcntl.h>
#include <linux/interrupt.h>
#include <linux/ioport.h>
#include <linux/in.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/mca-legacy.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include <linux/bitops.h>
#include <linux/jiffies.h>

#include <asm/io.h>
#include <asm/dma.h>

#include "8390.h"

#define DRV_NAME "ne2"

/* Some defines that people can play with if so inclined. */

/* Do we perform extra sanity checks on stuff ? */
/* #define NE_SANITY_CHECK */

/* Do we implement the read before write bugfix ? */
/* #define NE_RW_BUGFIX */

/* Do we have a non std. amount of memory? (in units of 256 byte pages) */
/* #define PACKETBUF_MEMSIZE	0x40 */


/* ---- No user-serviceable parts below ---- */

#define NE_BASE	 (dev->base_addr)
#define NE_CMD	 	0x00
#define NE_DATAPORT	0x10	/* NatSemi-defined port window offset. */
#define NE_RESET	0x20	/* Issue a read to reset, a write to clear. */
#define NE_IO_EXTENT	0x30

#define NE1SM_START_PG	0x20	/* First page of TX buffer */
#define NE1SM_STOP_PG 	0x40	/* Last page +1 of RX ring */
#define NESM_START_PG	0x40	/* First page of TX buffer */
#define NESM_STOP_PG	0x80	/* Last page +1 of RX ring */

/* From the .ADF file: */
static unsigned int addresses[7] __initdata =
		{0x1000, 0x2020, 0x8020, 0xa0a0, 0xb0b0, 0xc0c0, 0xc3d0};
static int irqs[4] __initdata = {3, 4, 5, 9};

/* From the D-Link ADF file: */
static unsigned int dlink_addresses[4] __initdata =
                {0x300, 0x320, 0x340, 0x360};
static int dlink_irqs[8] __initdata = {3, 4, 5, 9, 10, 11, 14, 15};

struct ne2_adapters_t {
	unsigned int	id;
	char		*name;
};

static struct ne2_adapters_t ne2_adapters[] __initdata = {
	{ 0x6354, "Arco Ethernet Adapter AE/2" },
	{ 0x70DE, "Compex ENET-16 MC/P" },
	{ 0x7154, "Novell Ethernet Adapter NE/2" },
        { 0x56ea, "D-Link DE-320CT" },
	{ 0x0000, NULL }
};

extern int netcard_probe(struct net_device *dev);

static int ne2_probe1(struct net_device *dev, int slot);

static void ne_reset_8390(struct net_device *dev);
static void ne_get_8390_hdr(struct net_device *dev, struct e8390_pkt_hdr *hdr,
		int ring_page);
static void ne_block_input(struct net_device *dev, int count,
		struct sk_buff *skb, int ring_offset);
static void ne_block_output(struct net_device *dev, const int count,
		const unsigned char *buf, const int start_page);


/*
 * special code to read the DE-320's MAC address EEPROM.  In contrast to a
 * standard NE design, this is a serial EEPROM (93C46) that has to be read
 * bit by bit.  The EEPROM cotrol port at base + 0x1e has the following
 * layout:
 *
 * Bit 0 = Data out (read from EEPROM)
 * Bit 1 = Data in  (write to EEPROM)
 * Bit 2 = Clock
 * Bit 3 = Chip Select
 * Bit 7 = ~50 kHz clock for defined delays
 *
 */

static void __init dlink_put_eeprom(unsigned char value, unsigned int addr)
{
	int z;
	unsigned char v1, v2;

	/* write the value to the NIC EEPROM register */

	outb(value, addr + 0x1e);

	/* now wait the clock line to toggle twice.  Effectively, we are
	   waiting (at least) for one clock cycle */

	for (z = 0; z < 2; z++) {
		do {
			v1 = inb(addr + 0x1e);
			v2 = inb(addr + 0x1e);
		}
		while (!((v1 ^ v2) & 0x80));
	}
}

static void __init dlink_send_eeprom_bit(unsigned int bit, unsigned int addr)
{
	/* shift data bit into correct position */

	bit = bit << 1;

	/* write value, keep clock line high for two cycles */

	dlink_put_eeprom(0x09 | bit, addr);
	dlink_put_eeprom(0x0d | bit, addr);
	dlink_put_eeprom(0x0d | bit, addr);
	dlink_put_eeprom(0x09 | bit, addr);
}

static void __init dlink_send_eeprom_word(unsigned int value, unsigned int len, unsigned int addr)
{
	int z;

	/* adjust bits so that they are left-aligned in a 16-bit-word */

	value = value << (16 - len);

	/* shift bits out to the EEPROM */

	for (z = 0; z < len; z++) {
		dlink_send_eeprom_bit((value & 0x8000) >> 15, addr);
		value = value << 1;
	}
}

static unsigned int __init dlink_get_eeprom(unsigned int eeaddr, unsigned int addr)
{
	int z;
	unsigned int value = 0;

	/* pull the CS line low for a moment.  This resets the EEPROM-
	   internal logic, and makes it ready for a new command. */

	dlink_put_eeprom(0x01, addr);
	dlink_put_eeprom(0x09, addr);

	/* send one start bit, read command (1 - 0), plus the address to
           the EEPROM */

	dlink_send_eeprom_word(0x0180 | (eeaddr & 0x3f), 9, addr);

	/* get the data word.  We clock by sending 0s to the EEPROM, which
	   get ignored during the read process */

	for (z = 0; z < 16; z++) {
		dlink_send_eeprom_bit(0, addr);
		value = (value << 1) | (inb(addr + 0x1e) & 0x01);
	}

	return value;
}

/*
 * Note that at boot, this probe only picks up one card at a time.
 */

static int __init do_ne2_probe(struct net_device *dev)
{
	static int current_mca_slot = -1;
	int i;
	int adapter_found = 0;

	/* Do not check any supplied i/o locations.
	   POS registers usually don't fail :) */

	/* MCA cards have POS registers.
	   Autodetecting MCA cards is extremely simple.
	   Just search for the card. */

	for(i = 0; (ne2_adapters[i].name != NULL) && !adapter_found; i++) {
		current_mca_slot =
			mca_find_unused_adapter(ne2_adapters[i].id, 0);

		if((current_mca_slot != MCA_NOTFOUND) && !adapter_found) {
			int res;
			mca_set_adapter_name(current_mca_slot,
					ne2_adapters[i].name);
			mca_mark_as_used(current_mca_slot);

			res = ne2_probe1(dev, current_mca_slot);
			if (res)
				mca_mark_as_unused(current_mca_slot);
			return res;
		}
	}
	return -ENODEV;
}

#ifndef MODULE
struct net_device * __init ne2_probe(int unit)
{
	struct net_device *dev = alloc_eip_netdev();
	int err;

	if (!dev)
		return ERR_PTR(-ENOMEM);

	sprintf(dev->name, "eth%d", unit);
	netdev_boot_setup_check(dev);

	err = do_ne2_probe(dev);
	if (err)
		goto out;
	return dev;
out:
	free_netdev(dev);
	return ERR_PTR(err);
}
#endif

static int ne2_procinfo(char *buf, int slot, struct net_device *dev)
{
	int len=0;

	len += sprintf(buf+len, "The NE/2 Ethernet Adapter\n" );
	len += sprintf(buf+len, "Driver written by Wim Dumon ");
	len += sprintf(buf+len, "<wimpie@kotnet.org>\n");
	len += sprintf(buf+len, "Modified by ");
	len += sprintf(buf+len, "David Weinehall <tao@acc.umu.se>\n");
	len += sprintf(buf+len, "and by Magnus Jonsson <bigfoot@acc.umu.se>\n");
	len += sprintf(buf+len, "Based on the original NE2000 drivers\n" );
	len += sprintf(buf+len, "Base IO: %#x\n", (unsigned int)dev->base_addr);
	len += sprintf(buf+len, "IRQ    : %d\n", dev->irq);
	len += sprintf(buf+len, "HW addr : %pM\n", dev->dev_addr);

	return len;
}

static int __init ne2_probe1(struct net_device *dev, int slot)
{
	int i, base_addr, irq, retval;
	unsigned char POS;
	unsigned char SA_prom[32];
	const char *name = "NE/2";
	int start_page, stop_page;
	static unsigned version_printed;

	if (ei_debug && version_printed++ == 0)
		printk(version);

	printk("NE/2 ethercard found in slot %d:", slot);

	/* Read base IO and IRQ from the POS-registers */
	POS = mca_read_stored_pos(slot, 2);
	if(!(POS % 2)) {
		printk(" disabled.\n");
		return -ENODEV;
	}

	/* handle different POS register structure for D-Link card */

	if (mca_read_stored_pos(slot, 0) == 0xea) {
		base_addr = dlink_addresses[(POS >> 5) & 0x03];
		irq = dlink_irqs[(POS >> 2) & 0x07];
	}
        else {
		i = (POS & 0xE)>>1;
		/* printk("Halleluja sdog, als er na de pijl een 1 staat is 1 - 1 == 0"
	   	" en zou het moeten werken -> %d\n", i);
	   	The above line was for remote testing, thanx to sdog ... */
		base_addr = addresses[i - 1];
		irq = irqs[(POS & 0x60)>>5];
	}

	if (!request_region(base_addr, NE_IO_EXTENT, DRV_NAME))
		return -EBUSY;

#ifdef DEBUG
	printk("POS info : pos 2 = %#x ; base = %#x ; irq = %ld\n", POS,
			base_addr, irq);
#endif

#ifndef CRYNWR_WAY
	/* Reset the card the way they do it in the Crynwr packet driver */
	for (i=0; i<8; i++)
		outb(0x0, base_addr + NE_RESET);
	inb(base_addr + NE_RESET);
	outb(0x21, base_addr + NE_CMD);
	if (inb(base_addr + NE_CMD) != 0x21) {
		printk("NE/2 adapter not responding\n");
		retval = -ENODEV;
		goto out;
	}

	/* In the crynwr sources they do a RAM-test here. I skip it. I suppose
	   my RAM is okay.  Suppose your memory is broken.  Then this test
	   should fail and you won't be able to use your card.  But if I do not
	   test, you won't be able to use your card, neither.  So this test
	   won't help you. */

#else  /* _I_ never tested it this way .. Go ahead and try ...*/
	/* Reset card. Who knows what dain-bramaged state it was left in. */
	{
		unsigned long reset_start_time = jiffies;

		/* DON'T change these to inb_p/outb_p or reset will fail on
		   clones.. */
		outb(inb(base_addr + NE_RESET), base_addr + NE_RESET);

		while ((inb_p(base_addr + EN0_ISR) & ENISR_RESET) == 0)
			if (time_after(jiffies, reset_start_time + 2*HZ/100)) {
				printk(" not found (no reset ack).\n");
				retval = -ENODEV;
				goto out;
			}

		outb_p(0xff, base_addr + EN0_ISR);         /* Ack all intr. */
	}
#endif


	/* Read the 16 bytes of station address PROM.
	   We must first initialize registers, similar to
	   NS8390p_init(eifdev, 0).
	   We can't reliably read the SAPROM address without this.
	   (I learned the hard way!). */
	{
		struct {
			unsigned char value, offset;
		} program_seq[] = {
						/* Select page 0 */
			{E8390_NODMA+E8390_PAGE0+E8390_STOP, E8390_CMD},
			{0x49,	EN0_DCFG},  /* Set WORD-wide (0x49) access. */
			{0x00,	EN0_RCNTLO},  /* Clear the count regs. */
			{0x00,	EN0_RCNTHI},
			{0x00,	EN0_IMR},  /* Mask completion irq. */
			{0xFF,	EN0_ISR},
			{E8390_RXOFF, EN0_RXCR},  /* 0x20  Set to monitor */
			{E8390_TXOFF, EN0_TXCR},  /* 0x02  and loopback mode. */
			{32,	EN0_RCNTLO},
			{0x00,	EN0_RCNTHI},
			{0x00,	EN0_RSARLO},  /* DMA starting at 0x0000. */
			{0x00,	EN0_RSARHI},
			{E8390_RREAD+E8390_START, E8390_CMD},
		};

		for (i = 0; i < ARRAY_SIZE(program_seq); i++)
			outb_p(program_seq[i].value, base_addr +
				program_seq[i].offset);

	}
	for(i = 0; i < 6 /*sizeof(SA_prom)*/; i+=1) {
		SA_prom[i] = inb(base_addr + NE_DATAPORT);
	}

	/* I don't know whether the previous sequence includes the general
           board reset procedure, so better don't omit it and just overwrite
           the garbage read from a DE-320 with correct stuff. */

	if (mca_read_stored_pos(slot, 0) == 0xea) {
		unsigned int v;

		for (i = 0; i < 3; i++) {
 			v = dlink_get_eeprom(i, base_addr);
			SA_prom[(i << 1)    ] = v & 0xff;
			SA_prom[(i << 1) + 1] = (v >> 8) & 0xff;
		}
	}

	start_page = NESM_START_PG;
	stop_page = NESM_STOP_PG;

	dev->irq=irq;

	/* Snarf the interrupt now.  There's no point in waiting since we cannot
	   share and the board will usually be enabled. */
	retval = request_irq(dev->irq, eip_interrupt, 0, DRV_NAME, dev);
	if (retval) {
		printk (" unable to get IRQ %d (irqval=%d).\n",
				dev->irq, retval);
		goto out;
	}

	dev->base_addr = base_addr;

	for (i = 0; i < ETH_ALEN; i++)
		dev->dev_addr[i] = SA_prom[i];

	printk(" %pM\n", dev->dev_addr);

	printk("%s: %s found at %#x, using IRQ %d.\n",
			dev->name, name, base_addr, dev->irq);

	mca_set_adapter_procfn(slot, (MCA_ProcFn) ne2_procinfo, dev);

	ei_status.name = name;
	ei_status.tx_start_page = start_page;
	ei_status.stop_page = stop_page;
	ei_status.word16 = (2 == 2);

	ei_status.rx_start_page = start_page + TX_PAGES;
#ifdef PACKETBUF_MEMSIZE
	/* Allow the packet buffer size to be overridden by know-it-alls. */
	ei_status.stop_page = ei_status.tx_start_page + PACKETBUF_MEMSIZE;
#endif

	ei_status.reset_8390 = &ne_reset_8390;
	ei_status.block_input = &ne_block_input;
	ei_status.block_output = &ne_block_output;
	ei_status.get_8390_hdr = &ne_get_8390_hdr;

	ei_status.priv = slot;

	dev->netdev_ops = &eip_netdev_ops;
	NS8390p_init(dev, 0);

	retval = register_netdev(dev);
	if (retval)
		goto out1;
	return 0;
out1:
	mca_set_adapter_procfn( ei_status.priv, NULL, NULL);
	free_irq(dev->irq, dev);
out:
	release_region(base_addr, NE_IO_EXTENT);
	return retval;
}

/* Hard reset the card.  This used to pause for the same period that a
   8390 reset command required, but that shouldn't be necessary. */
static void ne_reset_8390(struct net_device *dev)
{
	unsigned long reset_start_time = jiffies;

	if (ei_debug > 1)
		printk("resetting the 8390 t=%ld...", jiffies);

	/* DON'T change these to inb_p/outb_p or reset will fail on clones. */
	outb(inb(NE_BASE + NE_RESET), NE_BASE + NE_RESET);

	ei_status.txing = 0;
	ei_status.dmaing = 0;

	/* This check _should_not_ be necessary, omit eventually. */
	while ((inb_p(NE_BASE+EN0_ISR) & ENISR_RESET) == 0)
		if (time_after(jiffies, reset_start_time + 2*HZ/100)) {
			printk("%s: ne_reset_8390() did not complete.\n",
					dev->name);
			break;
		}
	outb_p(ENISR_RESET, NE_BASE + EN0_ISR);	/* Ack intr. */
}

/* Grab the 8390 specific header. Similar to the block_input routine, but
   we don't need to be concerned with ring wrap as the header will be at
   the start of a page, so we optimize accordingly. */

static void ne_get_8390_hdr(struct net_device *dev, struct e8390_pkt_hdr *hdr,
		int ring_page)
{

	int nic_base = dev->base_addr;

	/* This *shouldn't* happen.
	   If it does, it's the last thing you'll see */
	if (ei_status.dmaing) {
		printk("%s: DMAing conflict in ne_get_8390_hdr "
				"[DMAstat:%d][irqlock:%d].\n",
				dev->name, ei_status.dmaing, ei_status.irqlock);
		return;
	}

	ei_status.dmaing |= 0x01;
	outb_p(E8390_NODMA+E8390_PAGE0+E8390_START, nic_base+ NE_CMD);
	outb_p(sizeof(struct e8390_pkt_hdr), nic_base + EN0_RCNTLO);
	outb_p(0, nic_base + EN0_RCNTHI);
	outb_p(0, nic_base + EN0_RSARLO);		/* On page boundary */
	outb_p(ring_page, nic_base + EN0_RSARHI);
	outb_p(E8390_RREAD+E8390_START, nic_base + NE_CMD);

	if (ei_status.word16)
		insw(NE_BASE + NE_DATAPORT, hdr,
				sizeof(struct e8390_pkt_hdr)>>1);
	else
		insb(NE_BASE + NE_DATAPORT, hdr,
				sizeof(struct e8390_pkt_hdr));

	outb_p(ENISR_RDC, nic_base + EN0_ISR);	/* Ack intr. */
	ei_status.dmaing &= ~0x01;
}

/* Block input and output, similar to the Crynwr packet driver.  If you
   are porting to a new ethercard, look at the packet driver source for
   hints. The NEx000 doesn't share the on-board packet memory -- you have
   to put the packet out through the "remote DMA" dataport using outb. */

static void ne_block_input(struct net_device *dev, int count, struct sk_buff *skb,
		int ring_offset)
{
#ifdef NE_SANITY_CHECK
	int xfer_count = count;
#endif
	int nic_base = dev->base_addr;
	char *buf = skb->data;

	/* This *shouldn't* happen.
	   If it does, it's the last thing you'll see */
	if (ei_status.dmaing) {
		printk("%s: DMAing conflict in ne_block_input "
				"[DMAstat:%d][irqlock:%d].\n",
				dev->name, ei_status.dmaing, ei_status.irqlock);
		return;
	}
	ei_status.dmaing |= 0x01;
	outb_p(E8390_NODMA+E8390_PAGE0+E8390_START, nic_base+ NE_CMD);
	outb_p(count & 0xff, nic_base + EN0_RCNTLO);
	outb_p(count >> 8, nic_base + EN0_RCNTHI);
	outb_p(ring_offset & 0xff, nic_base + EN0_RSARLO);
	outb_p(ring_offset >> 8, nic_base + EN0_RSARHI);
	outb_p(E8390_RREAD+E8390_START, nic_base + NE_CMD);
	if (ei_status.word16) {
		insw(NE_BASE + NE_DATAPORT,buf,count>>1);
		if (count & 0x01) {
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
	if (ei_debug > 1) {	/* DMA termination address check... */
		int addr, tries = 20;
		do {
			/* DON'T check for 'inb_p(EN0_ISR) & ENISR_RDC' here
			   -- it's broken for Rx on some cards! */
			int high = inb_p(nic_base + EN0_RSARHI);
			int low = inb_p(nic_base + EN0_RSARLO);
			addr = (high << 8) + low;
			if (((ring_offset + xfer_count) & 0xff) == low)
				break;
		} while (--tries > 0);
		if (tries <= 0)
			printk("%s: RX transfer address mismatch,"
				"%#4.4x (expected) vs. %#4.4x (actual).\n",
				dev->name, ring_offset + xfer_count, addr);
	}
#endif
	outb_p(ENISR_RDC, nic_base + EN0_ISR);	/* Ack intr. */
	ei_status.dmaing &= ~0x01;
}

static void ne_block_output(struct net_device *dev, int count,
		const unsigned char *buf, const int start_page)
{
	int nic_base = NE_BASE;
	unsigned long dma_start;
#ifdef NE_SANITY_CHECK
	int retries = 0;
#endif

	/* Round the count up for word writes. Do we need to do this?
	   What effect will an odd byte count have on the 8390?
	   I should check someday. */
	if (ei_status.word16 && (count & 0x01))
		count++;

	/* This *shouldn't* happen.
	   If it does, it's the last thing you'll see */
	if (ei_status.dmaing) {
		printk("%s: DMAing conflict in ne_block_output."
				"[DMAstat:%d][irqlock:%d]\n",
				dev->name, ei_status.dmaing, ei_status.irqlock);
		return;
	}
	ei_status.dmaing |= 0x01;
	/* We should already be in page 0, but to be safe... */
	outb_p(E8390_PAGE0+E8390_START+E8390_NODMA, nic_base + NE_CMD);

#ifdef NE_SANITY_CHECK
retry:
#endif

#ifdef NE8390_RW_BUGFIX
	/* Handle the read-before-write bug the same way as the
	   Crynwr packet driver -- the NatSemi method doesn't work.
	   Actually this doesn't always work either, but if you have
	   problems with your NEx000 this is better than nothing! */
	outb_p(0x42, nic_base + EN0_RCNTLO);
	outb_p(0x00, nic_base + EN0_RCNTHI);
	outb_p(0x42, nic_base + EN0_RSARLO);
	outb_p(0x00, nic_base + EN0_RSARHI);
	outb_p(E8390_RREAD+E8390_START, nic_base + NE_CMD);
	/* Make certain that the dummy read has occurred. */
	SLOW_DOWN_IO;
	SLOW_DOWN_IO;
	SLOW_DOWN_IO;
#endif

	outb_p(ENISR_RDC, nic_base + EN0_ISR);

	/* Now the normal output. */
	outb_p(count & 0xff, nic_base + EN0_RCNTLO);
	outb_p(count >> 8,   nic_base + EN0_RCNTHI);
	outb_p(0x00, nic_base + EN0_RSARLO);
	outb_p(start_page, nic_base + EN0_RSARHI);

	outb_p(E8390_RWRITE+E8390_START, nic_base + NE_CMD);
	if (ei_status.word16) {
		outsw(NE_BASE + NE_DATAPORT, buf, count>>1);
	} else {
		outsb(NE_BASE + NE_DATAPORT, buf, count);
	}

	dma_start = jiffies;

#ifdef NE_SANITY_CHECK
	/* This was for the ALPHA version only, but enough people have
	   been encountering problems so it is still here. */

	if (ei_debug > 1) {		/* DMA termination address check... */
		int addr, tries = 20;
		do {
			int high = inb_p(nic_base + EN0_RSARHI);
			int low = inb_p(nic_base + EN0_RSARLO);
			addr = (high << 8) + low;
			if ((start_page << 8) + count == addr)
				break;
		} while (--tries > 0);
		if (tries <= 0) {
			printk("%s: Tx packet transfer address mismatch,"
					"%#4.4x (expected) vs. %#4.4x (actual).\n",
					dev->name, (start_page << 8) + count, addr);
			if (retries++ == 0)
				goto retry;
		}
	}
#endif

	while ((inb_p(nic_base + EN0_ISR) & ENISR_RDC) == 0)
		if (time_after(jiffies, dma_start + 2*HZ/100)) {		/* 20ms */
			printk("%s: timeout waiting for Tx RDC.\n", dev->name);
			ne_reset_8390(dev);
			NS8390p_init(dev, 1);
			break;
		}

	outb_p(ENISR_RDC, nic_base + EN0_ISR);	/* Ack intr. */
	ei_status.dmaing &= ~0x01;
}


#ifdef MODULE
#define MAX_NE_CARDS	4	/* Max number of NE cards per module */
static struct net_device *dev_ne[MAX_NE_CARDS];
static int io[MAX_NE_CARDS];
static int irq[MAX_NE_CARDS];
static int bad[MAX_NE_CARDS];	/* 0xbad = bad sig or no reset ack */
MODULE_LICENSE("GPL");

module_param_array(io, int, NULL, 0);
module_param_array(irq, int, NULL, 0);
module_param_array(bad, int, NULL, 0);
MODULE_PARM_DESC(io, "(ignored)");
MODULE_PARM_DESC(irq, "(ignored)");
MODULE_PARM_DESC(bad, "(ignored)");

/* Module code fixed by David Weinehall */

int __init init_module(void)
{
	struct net_device *dev;
	int this_dev, found = 0;

	for (this_dev = 0; this_dev < MAX_NE_CARDS; this_dev++) {
		dev = alloc_eip_netdev();
		if (!dev)
			break;
		dev->irq = irq[this_dev];
		dev->mem_end = bad[this_dev];
		dev->base_addr = io[this_dev];
		if (do_ne2_probe(dev) == 0) {
			dev_ne[found++] = dev;
			continue;
		}
		free_netdev(dev);
		break;
	}
	if (found)
		return 0;
	printk(KERN_WARNING "ne2.c: No NE/2 card found\n");
	return -ENXIO;
}

static void cleanup_card(struct net_device *dev)
{
	mca_mark_as_unused(ei_status.priv);
	mca_set_adapter_procfn( ei_status.priv, NULL, NULL);
	free_irq(dev->irq, dev);
	release_region(dev->base_addr, NE_IO_EXTENT);
}

void __exit cleanup_module(void)
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
