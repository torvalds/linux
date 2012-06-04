/*
 * Amiga Linux/68k 8390 based PCMCIA Ethernet Driver for the Amiga 1200
 *
 * (C) Copyright 1997 Alain Malek
 *                    (Alain.Malek@cryogen.com)
 *
 * ----------------------------------------------------------------------------
 *
 * This program is based on
 *
 * ne.c:       A general non-shared-memory NS8390 ethernet driver for linux
 *             Written 1992-94 by Donald Becker.
 *
 * 8390.c:     A general NS8390 ethernet driver core for linux.
 *             Written 1992-94 by Donald Becker.
 *
 * cnetdevice: A Sana-II ethernet driver for AmigaOS
 *             Written by Bruce Abbott (bhabbott@inhb.co.nz)
 *
 * ----------------------------------------------------------------------------
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file COPYING in the main directory of the Linux
 * distribution for more details.
 *
 * ----------------------------------------------------------------------------
 *
 */


#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/interrupt.h>
#include <linux/jiffies.h>

#include <asm/io.h>
#include <asm/setup.h>
#include <asm/amigaints.h>
#include <asm/amigahw.h>
#include <asm/amigayle.h>
#include <asm/amipcmcia.h>

#include "8390.h"

/* ---- No user-serviceable parts below ---- */

#define DRV_NAME "apne"

#define NE_BASE	 (dev->base_addr)
#define NE_CMD	 		0x00
#define NE_DATAPORT		0x10            /* NatSemi-defined port window offset. */
#define NE_RESET		0x1f            /* Issue a read to reset, a write to clear. */
#define NE_IO_EXTENT	        0x20

#define NE_EN0_ISR		0x07
#define NE_EN0_DCFG		0x0e

#define NE_EN0_RSARLO	        0x08
#define NE_EN0_RSARHI	        0x09
#define NE_EN0_RCNTLO	        0x0a
#define NE_EN0_RXCR		0x0c
#define NE_EN0_TXCR		0x0d
#define NE_EN0_RCNTHI	        0x0b
#define NE_EN0_IMR		0x0f

#define NE1SM_START_PG	0x20	/* First page of TX buffer */
#define NE1SM_STOP_PG 	0x40	/* Last page +1 of RX ring */
#define NESM_START_PG	0x40	/* First page of TX buffer */
#define NESM_STOP_PG	0x80	/* Last page +1 of RX ring */


struct net_device * __init apne_probe(int unit);
static int apne_probe1(struct net_device *dev, int ioaddr);

static void apne_reset_8390(struct net_device *dev);
static void apne_get_8390_hdr(struct net_device *dev, struct e8390_pkt_hdr *hdr,
			  int ring_page);
static void apne_block_input(struct net_device *dev, int count,
								struct sk_buff *skb, int ring_offset);
static void apne_block_output(struct net_device *dev, const int count,
							const unsigned char *buf, const int start_page);
static irqreturn_t apne_interrupt(int irq, void *dev_id);

static int init_pcmcia(void);

/* IO base address used for nic */

#define IOBASE 0x300

/*
   use MANUAL_CONFIG and MANUAL_OFFSET for enabling IO by hand
   you can find the values to use by looking at the cnet.device
   config file example (the default values are for the CNET40BC card)
*/

/*
#define MANUAL_CONFIG 0x20
#define MANUAL_OFFSET 0x3f8

#define MANUAL_HWADDR0 0x00
#define MANUAL_HWADDR1 0x12
#define MANUAL_HWADDR2 0x34
#define MANUAL_HWADDR3 0x56
#define MANUAL_HWADDR4 0x78
#define MANUAL_HWADDR5 0x9a
*/

static const char version[] =
    "apne.c:v1.1 7/10/98 Alain Malek (Alain.Malek@cryogen.ch)\n";

static int apne_owned;	/* signal if card already owned */

struct net_device * __init apne_probe(int unit)
{
	struct net_device *dev;
#ifndef MANUAL_CONFIG
	char tuple[8];
#endif
	int err;

	if (!MACH_IS_AMIGA)
		return ERR_PTR(-ENODEV);

	if (apne_owned)
		return ERR_PTR(-ENODEV);

	if ( !(AMIGAHW_PRESENT(PCMCIA)) )
		return ERR_PTR(-ENODEV);

	printk("Looking for PCMCIA ethernet card : ");

	/* check if a card is inserted */
	if (!(PCMCIA_INSERTED)) {
		printk("NO PCMCIA card inserted\n");
		return ERR_PTR(-ENODEV);
	}

	dev = alloc_ei_netdev();
	if (!dev)
		return ERR_PTR(-ENOMEM);
	if (unit >= 0) {
		sprintf(dev->name, "eth%d", unit);
		netdev_boot_setup_check(dev);
	}

	/* disable pcmcia irq for readtuple */
	pcmcia_disable_irq();

#ifndef MANUAL_CONFIG
	if ((pcmcia_copy_tuple(CISTPL_FUNCID, tuple, 8) < 3) ||
		(tuple[2] != CISTPL_FUNCID_NETWORK)) {
		printk("not an ethernet card\n");
		/* XXX: shouldn't we re-enable irq here? */
		free_netdev(dev);
		return ERR_PTR(-ENODEV);
	}
#endif

	printk("ethernet PCMCIA card inserted\n");

	if (!init_pcmcia()) {
		/* XXX: shouldn't we re-enable irq here? */
		free_netdev(dev);
		return ERR_PTR(-ENODEV);
	}

	if (!request_region(IOBASE, 0x20, DRV_NAME)) {
		free_netdev(dev);
		return ERR_PTR(-EBUSY);
	}

	err = apne_probe1(dev, IOBASE);
	if (err) {
		release_region(IOBASE, 0x20);
		free_netdev(dev);
		return ERR_PTR(err);
	}
	err = register_netdev(dev);
	if (!err)
		return dev;

	pcmcia_disable_irq();
	free_irq(IRQ_AMIGA_PORTS, dev);
	pcmcia_reset();
	release_region(IOBASE, 0x20);
	free_netdev(dev);
	return ERR_PTR(err);
}

static int __init apne_probe1(struct net_device *dev, int ioaddr)
{
    int i;
    unsigned char SA_prom[32];
    int wordlength = 2;
    const char *name = NULL;
    int start_page, stop_page;
#ifndef MANUAL_HWADDR0
    int neX000, ctron;
#endif
    static unsigned version_printed;

    if (ei_debug  &&  version_printed++ == 0)
	printk(version);

    printk("PCMCIA NE*000 ethercard probe");

    /* Reset card. Who knows what dain-bramaged state it was left in. */
    {	unsigned long reset_start_time = jiffies;

	outb(inb(ioaddr + NE_RESET), ioaddr + NE_RESET);

	while ((inb(ioaddr + NE_EN0_ISR) & ENISR_RESET) == 0)
		if (time_after(jiffies, reset_start_time + 2*HZ/100)) {
			printk(" not found (no reset ack).\n");
			return -ENODEV;
		}

	outb(0xff, ioaddr + NE_EN0_ISR);		/* Ack all intr. */
    }

#ifndef MANUAL_HWADDR0

    /* Read the 16 bytes of station address PROM.
       We must first initialize registers, similar to NS8390_init(eifdev, 0).
       We can't reliably read the SAPROM address without this.
       (I learned the hard way!). */
    {
	struct {unsigned long value, offset; } program_seq[] = {
	    {E8390_NODMA+E8390_PAGE0+E8390_STOP, NE_CMD}, /* Select page 0*/
	    {0x48,	NE_EN0_DCFG},	/* Set byte-wide (0x48) access. */
	    {0x00,	NE_EN0_RCNTLO},	/* Clear the count regs. */
	    {0x00,	NE_EN0_RCNTHI},
	    {0x00,	NE_EN0_IMR},	/* Mask completion irq. */
	    {0xFF,	NE_EN0_ISR},
	    {E8390_RXOFF, NE_EN0_RXCR},	/* 0x20  Set to monitor */
	    {E8390_TXOFF, NE_EN0_TXCR},	/* 0x02  and loopback mode. */
	    {32,	NE_EN0_RCNTLO},
	    {0x00,	NE_EN0_RCNTHI},
	    {0x00,	NE_EN0_RSARLO},	/* DMA starting at 0x0000. */
	    {0x00,	NE_EN0_RSARHI},
	    {E8390_RREAD+E8390_START, NE_CMD},
	};
	for (i = 0; i < ARRAY_SIZE(program_seq); i++) {
	    outb(program_seq[i].value, ioaddr + program_seq[i].offset);
	}

    }
    for(i = 0; i < 32 /*sizeof(SA_prom)*/; i+=2) {
	SA_prom[i] = inb(ioaddr + NE_DATAPORT);
	SA_prom[i+1] = inb(ioaddr + NE_DATAPORT);
	if (SA_prom[i] != SA_prom[i+1])
	    wordlength = 1;
    }

    /*	At this point, wordlength *only* tells us if the SA_prom is doubled
	up or not because some broken PCI cards don't respect the byte-wide
	request in program_seq above, and hence don't have doubled up values.
	These broken cards would otherwise be detected as an ne1000.  */

    if (wordlength == 2)
	for (i = 0; i < 16; i++)
		SA_prom[i] = SA_prom[i+i];

    if (wordlength == 2) {
	/* We must set the 8390 for word mode. */
	outb(0x49, ioaddr + NE_EN0_DCFG);
	start_page = NESM_START_PG;
	stop_page = NESM_STOP_PG;
    } else {
	start_page = NE1SM_START_PG;
	stop_page = NE1SM_STOP_PG;
    }

    neX000 = (SA_prom[14] == 0x57  &&  SA_prom[15] == 0x57);
    ctron =  (SA_prom[0] == 0x00 && SA_prom[1] == 0x00 && SA_prom[2] == 0x1d);

    /* Set up the rest of the parameters. */
    if (neX000) {
	name = (wordlength == 2) ? "NE2000" : "NE1000";
    } else if (ctron) {
	name = (wordlength == 2) ? "Ctron-8" : "Ctron-16";
	start_page = 0x01;
	stop_page = (wordlength == 2) ? 0x40 : 0x20;
    } else {
	printk(" not found.\n");
	return -ENXIO;

    }

#else
    wordlength = 2;
    /* We must set the 8390 for word mode. */
    outb(0x49, ioaddr + NE_EN0_DCFG);
    start_page = NESM_START_PG;
    stop_page = NESM_STOP_PG;

    SA_prom[0] = MANUAL_HWADDR0;
    SA_prom[1] = MANUAL_HWADDR1;
    SA_prom[2] = MANUAL_HWADDR2;
    SA_prom[3] = MANUAL_HWADDR3;
    SA_prom[4] = MANUAL_HWADDR4;
    SA_prom[5] = MANUAL_HWADDR5;
    name = "NE2000";
#endif

    dev->base_addr = ioaddr;
    dev->irq = IRQ_AMIGA_PORTS;
    dev->netdev_ops = &ei_netdev_ops;

    /* Install the Interrupt handler */
    i = request_irq(dev->irq, apne_interrupt, IRQF_SHARED, DRV_NAME, dev);
    if (i) return i;

    for (i = 0; i < ETH_ALEN; i++)
	dev->dev_addr[i] = SA_prom[i];

    printk(" %pM\n", dev->dev_addr);

    printk("%s: %s found.\n", dev->name, name);

    ei_status.name = name;
    ei_status.tx_start_page = start_page;
    ei_status.stop_page = stop_page;
    ei_status.word16 = (wordlength == 2);

    ei_status.rx_start_page = start_page + TX_PAGES;

    ei_status.reset_8390 = &apne_reset_8390;
    ei_status.block_input = &apne_block_input;
    ei_status.block_output = &apne_block_output;
    ei_status.get_8390_hdr = &apne_get_8390_hdr;

    NS8390_init(dev, 0);

    pcmcia_ack_int(pcmcia_get_intreq());		/* ack PCMCIA int req */
    pcmcia_enable_irq();

    apne_owned = 1;

    return 0;
}

/* Hard reset the card.  This used to pause for the same period that a
   8390 reset command required, but that shouldn't be necessary. */
static void
apne_reset_8390(struct net_device *dev)
{
    unsigned long reset_start_time = jiffies;

    init_pcmcia();

    if (ei_debug > 1) printk("resetting the 8390 t=%ld...", jiffies);

    outb(inb(NE_BASE + NE_RESET), NE_BASE + NE_RESET);

    ei_status.txing = 0;
    ei_status.dmaing = 0;

    /* This check _should_not_ be necessary, omit eventually. */
    while ((inb(NE_BASE+NE_EN0_ISR) & ENISR_RESET) == 0)
	if (time_after(jiffies, reset_start_time + 2*HZ/100)) {
	    printk("%s: ne_reset_8390() did not complete.\n", dev->name);
	    break;
	}
    outb(ENISR_RESET, NE_BASE + NE_EN0_ISR);	/* Ack intr. */
}

/* Grab the 8390 specific header. Similar to the block_input routine, but
   we don't need to be concerned with ring wrap as the header will be at
   the start of a page, so we optimize accordingly. */

static void
apne_get_8390_hdr(struct net_device *dev, struct e8390_pkt_hdr *hdr, int ring_page)
{

    int nic_base = dev->base_addr;
    int cnt;
    char *ptrc;
    short *ptrs;

    /* This *shouldn't* happen. If it does, it's the last thing you'll see */
    if (ei_status.dmaing) {
	printk("%s: DMAing conflict in ne_get_8390_hdr "
	   "[DMAstat:%d][irqlock:%d][intr:%d].\n",
	   dev->name, ei_status.dmaing, ei_status.irqlock, dev->irq);
	return;
    }

    ei_status.dmaing |= 0x01;
    outb(E8390_NODMA+E8390_PAGE0+E8390_START, nic_base+ NE_CMD);
    outb(ENISR_RDC, nic_base + NE_EN0_ISR);
    outb(sizeof(struct e8390_pkt_hdr), nic_base + NE_EN0_RCNTLO);
    outb(0, nic_base + NE_EN0_RCNTHI);
    outb(0, nic_base + NE_EN0_RSARLO);		/* On page boundary */
    outb(ring_page, nic_base + NE_EN0_RSARHI);
    outb(E8390_RREAD+E8390_START, nic_base + NE_CMD);

    if (ei_status.word16) {
        ptrs = (short*)hdr;
        for(cnt = 0; cnt < (sizeof(struct e8390_pkt_hdr)>>1); cnt++)
            *ptrs++ = inw(NE_BASE + NE_DATAPORT);
    } else {
        ptrc = (char*)hdr;
        for(cnt = 0; cnt < sizeof(struct e8390_pkt_hdr); cnt++)
            *ptrc++ = inb(NE_BASE + NE_DATAPORT);
    }

    outb(ENISR_RDC, nic_base + NE_EN0_ISR);	/* Ack intr. */
    ei_status.dmaing &= ~0x01;

    le16_to_cpus(&hdr->count);
}

/* Block input and output, similar to the Crynwr packet driver.  If you
   are porting to a new ethercard, look at the packet driver source for hints.
   The NEx000 doesn't share the on-board packet memory -- you have to put
   the packet out through the "remote DMA" dataport using outb. */

static void
apne_block_input(struct net_device *dev, int count, struct sk_buff *skb, int ring_offset)
{
    int nic_base = dev->base_addr;
    char *buf = skb->data;
    char *ptrc;
    short *ptrs;
    int cnt;

    /* This *shouldn't* happen. If it does, it's the last thing you'll see */
    if (ei_status.dmaing) {
	printk("%s: DMAing conflict in ne_block_input "
	   "[DMAstat:%d][irqlock:%d][intr:%d].\n",
	   dev->name, ei_status.dmaing, ei_status.irqlock, dev->irq);
	return;
    }
    ei_status.dmaing |= 0x01;
    outb(E8390_NODMA+E8390_PAGE0+E8390_START, nic_base+ NE_CMD);
    outb(ENISR_RDC, nic_base + NE_EN0_ISR);
    outb(count & 0xff, nic_base + NE_EN0_RCNTLO);
    outb(count >> 8, nic_base + NE_EN0_RCNTHI);
    outb(ring_offset & 0xff, nic_base + NE_EN0_RSARLO);
    outb(ring_offset >> 8, nic_base + NE_EN0_RSARHI);
    outb(E8390_RREAD+E8390_START, nic_base + NE_CMD);
    if (ei_status.word16) {
      ptrs = (short*)buf;
      for (cnt = 0; cnt < (count>>1); cnt++)
        *ptrs++ = inw(NE_BASE + NE_DATAPORT);
      if (count & 0x01) {
	buf[count-1] = inb(NE_BASE + NE_DATAPORT);
      }
    } else {
      ptrc = buf;
      for (cnt = 0; cnt < count; cnt++)
        *ptrc++ = inb(NE_BASE + NE_DATAPORT);
    }

    outb(ENISR_RDC, nic_base + NE_EN0_ISR);	/* Ack intr. */
    ei_status.dmaing &= ~0x01;
}

static void
apne_block_output(struct net_device *dev, int count,
		const unsigned char *buf, const int start_page)
{
    int nic_base = NE_BASE;
    unsigned long dma_start;
    char *ptrc;
    short *ptrs;
    int cnt;

    /* Round the count up for word writes.  Do we need to do this?
       What effect will an odd byte count have on the 8390?
       I should check someday. */
    if (ei_status.word16 && (count & 0x01))
      count++;

    /* This *shouldn't* happen. If it does, it's the last thing you'll see */
    if (ei_status.dmaing) {
	printk("%s: DMAing conflict in ne_block_output."
	   "[DMAstat:%d][irqlock:%d][intr:%d]\n",
	   dev->name, ei_status.dmaing, ei_status.irqlock, dev->irq);
	return;
    }
    ei_status.dmaing |= 0x01;
    /* We should already be in page 0, but to be safe... */
    outb(E8390_PAGE0+E8390_START+E8390_NODMA, nic_base + NE_CMD);

    outb(ENISR_RDC, nic_base + NE_EN0_ISR);

   /* Now the normal output. */
    outb(count & 0xff, nic_base + NE_EN0_RCNTLO);
    outb(count >> 8,   nic_base + NE_EN0_RCNTHI);
    outb(0x00, nic_base + NE_EN0_RSARLO);
    outb(start_page, nic_base + NE_EN0_RSARHI);

    outb(E8390_RWRITE+E8390_START, nic_base + NE_CMD);
    if (ei_status.word16) {
        ptrs = (short*)buf;
        for (cnt = 0; cnt < count>>1; cnt++)
            outw(*ptrs++, NE_BASE+NE_DATAPORT);
    } else {
        ptrc = (char*)buf;
        for (cnt = 0; cnt < count; cnt++)
	    outb(*ptrc++, NE_BASE + NE_DATAPORT);
    }

    dma_start = jiffies;

    while ((inb(NE_BASE + NE_EN0_ISR) & ENISR_RDC) == 0)
	if (time_after(jiffies, dma_start + 2*HZ/100)) {	/* 20ms */
		printk("%s: timeout waiting for Tx RDC.\n", dev->name);
		apne_reset_8390(dev);
		NS8390_init(dev,1);
		break;
	}

    outb(ENISR_RDC, nic_base + NE_EN0_ISR);	/* Ack intr. */
    ei_status.dmaing &= ~0x01;
}

static irqreturn_t apne_interrupt(int irq, void *dev_id)
{
    unsigned char pcmcia_intreq;

    if (!(gayle.inten & GAYLE_IRQ_IRQ))
        return IRQ_NONE;

    pcmcia_intreq = pcmcia_get_intreq();

    if (!(pcmcia_intreq & GAYLE_IRQ_IRQ)) {
        pcmcia_ack_int(pcmcia_intreq);
        return IRQ_NONE;
    }
    if (ei_debug > 3)
        printk("pcmcia intreq = %x\n", pcmcia_intreq);
    pcmcia_disable_irq();			/* to get rid of the sti() within ei_interrupt */
    ei_interrupt(irq, dev_id);
    pcmcia_ack_int(pcmcia_get_intreq());
    pcmcia_enable_irq();
    return IRQ_HANDLED;
}

#ifdef MODULE
static struct net_device *apne_dev;

static int __init apne_module_init(void)
{
	apne_dev = apne_probe(-1);
	if (IS_ERR(apne_dev))
		return PTR_ERR(apne_dev);
	return 0;
}

static void __exit apne_module_exit(void)
{
	unregister_netdev(apne_dev);

	pcmcia_disable_irq();

	free_irq(IRQ_AMIGA_PORTS, apne_dev);

	pcmcia_reset();

	release_region(IOBASE, 0x20);

	free_netdev(apne_dev);
}
module_init(apne_module_init);
module_exit(apne_module_exit);
#endif

static int init_pcmcia(void)
{
	u_char config;
#ifndef MANUAL_CONFIG
	u_char tuple[32];
	int offset_len;
#endif
	u_long offset;

	pcmcia_reset();
	pcmcia_program_voltage(PCMCIA_0V);
	pcmcia_access_speed(PCMCIA_SPEED_250NS);
	pcmcia_write_enable();

#ifdef MANUAL_CONFIG
	config = MANUAL_CONFIG;
#else
	/* get and write config byte to enable IO port */

	if (pcmcia_copy_tuple(CISTPL_CFTABLE_ENTRY, tuple, 32) < 3)
		return 0;

	config = tuple[2] & 0x3f;
#endif
#ifdef MANUAL_OFFSET
	offset = MANUAL_OFFSET;
#else
	if (pcmcia_copy_tuple(CISTPL_CONFIG, tuple, 32) < 6)
		return 0;

	offset_len = (tuple[2] & 0x3) + 1;
	offset = 0;
	while(offset_len--) {
		offset = (offset << 8) | tuple[4+offset_len];
	}
#endif

	out_8(GAYLE_ATTRIBUTE+offset, config);

	return 1;
}

MODULE_LICENSE("GPL");
