/* Intel EtherExpress 16 device driver for Linux
 *
 * Written by John Sullivan, 1995
 *  based on original code by Donald Becker, with changes by
 *  Alan Cox and Pauline Middelink.
 *
 * Support for 8-bit mode by Zoltan Szilagyi <zoltans@cs.arizona.edu>
 *
 * Many modifications, and currently maintained, by
 *  Philip Blundell <philb@gnu.org>
 * Added the Compaq LTE  Alan Cox <alan@lxorguk.ukuu.org.uk>
 * Added MCA support Adam Fritzler
 *
 * Note - this driver is experimental still - it has problems on faster
 * machines. Someone needs to sit down and go through it line by line with
 * a databook...
 */

/* The EtherExpress 16 is a fairly simple card, based on a shared-memory
 * design using the i82586 Ethernet coprocessor.  It bears no relationship,
 * as far as I know, to the similarly-named "EtherExpress Pro" range.
 *
 * Historically, Linux support for these cards has been very bad.  However,
 * things seem to be getting better slowly.
 */

/* If your card is confused about what sort of interface it has (eg it
 * persistently reports "10baseT" when none is fitted), running 'SOFTSET /BART'
 * or 'SOFTSET /LISA' from DOS seems to help.
 */

/* Here's the scoop on memory mapping.
 *
 * There are three ways to access EtherExpress card memory: either using the
 * shared-memory mapping, or using PIO through the dataport, or using PIO
 * through the "shadow memory" ports.
 *
 * The shadow memory system works by having the card map some of its memory
 * as follows:
 *
 * (the low five bits of the SMPTR are ignored)
 *
 *  base+0x4000..400f      memory at SMPTR+0..15
 *  base+0x8000..800f      memory at SMPTR+16..31
 *  base+0xc000..c007      dubious stuff (memory at SMPTR+16..23 apparently)
 *  base+0xc008..c00f      memory at 0x0008..0x000f
 *
 * This last set (the one at c008) is particularly handy because the SCB
 * lives at 0x0008.  So that set of ports gives us easy random access to data
 * in the SCB without having to mess around setting up pointers and the like.
 * We always use this method to access the SCB (via the scb_xx() functions).
 *
 * Dataport access works by aiming the appropriate (read or write) pointer
 * at the first address you're interested in, and then reading or writing from
 * the dataport.  The pointers auto-increment after each transfer.  We use
 * this for data transfer.
 *
 * We don't use the shared-memory system because it allegedly doesn't work on
 * all cards, and because it's a bit more prone to go wrong (it's one more
 * thing to configure...).
 */

/* Known bugs:
 *
 * - The card seems to want to give us two interrupts every time something
 *   happens, where just one would be better.
 */

/*
 *
 * Note by Zoltan Szilagyi 10-12-96:
 *
 * I've succeeded in eliminating the "CU wedged" messages, and hence the
 * lockups, which were only occurring with cards running in 8-bit mode ("force
 * 8-bit operation" in Intel's SoftSet utility). This version of the driver
 * sets the 82586 and the ASIC to 8-bit mode at startup; it also stops the
 * CU before submitting a packet for transmission, and then restarts it as soon
 * as the process of handing the packet is complete. This is definitely an
 * unnecessary slowdown if the card is running in 16-bit mode; therefore one
 * should detect 16-bit vs 8-bit mode from the EEPROM settings and act
 * accordingly. In 8-bit mode with this bugfix I'm getting about 150 K/s for
 * ftp's, which is significantly better than I get in DOS, so the overhead of
 * stopping and restarting the CU with each transmit is not prohibitive in
 * practice.
 *
 * Update by David Woodhouse 11/5/99:
 *
 * I've seen "CU wedged" messages in 16-bit mode, on the Alpha architecture.
 * I assume that this is because 16-bit accesses are actually handled as two
 * 8-bit accesses.
 */

#ifdef __alpha__
#define LOCKUP16 1
#endif
#ifndef LOCKUP16
#define LOCKUP16 0
#endif

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/fcntl.h>
#include <linux/interrupt.h>
#include <linux/ioport.h>
#include <linux/string.h>
#include <linux/in.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include <linux/slab.h>
#include <linux/mca-legacy.h>
#include <linux/spinlock.h>
#include <linux/bitops.h>
#include <linux/jiffies.h>

#include <asm/system.h>
#include <asm/io.h>
#include <asm/irq.h>

#ifndef NET_DEBUG
#define NET_DEBUG 4
#endif

#include "eexpress.h"

#define EEXP_IO_EXTENT  16

/*
 * Private data declarations
 */

struct net_local
{
	unsigned long last_tx;       /* jiffies when last transmit started */
	unsigned long init_time;     /* jiffies when eexp_hw_init586 called */
	unsigned short rx_first;     /* first rx buf, same as RX_BUF_START */
	unsigned short rx_last;      /* last rx buf */
	unsigned short rx_ptr;       /* first rx buf to look at */
	unsigned short tx_head;      /* next free tx buf */
	unsigned short tx_reap;      /* first in-use tx buf */
	unsigned short tx_tail;      /* previous tx buf to tx_head */
	unsigned short tx_link;      /* last known-executing tx buf */
	unsigned short last_tx_restart;   /* set to tx_link when we
					     restart the CU */
	unsigned char started;
	unsigned short rx_buf_start;
	unsigned short rx_buf_end;
	unsigned short num_tx_bufs;
	unsigned short num_rx_bufs;
	unsigned char width;         /* 0 for 16bit, 1 for 8bit */
	unsigned char was_promisc;
	unsigned char old_mc_count;
	spinlock_t lock;
};

/* This is the code and data that is downloaded to the EtherExpress card's
 * memory at boot time.
 */

static unsigned short start_code[] = {
/* 0x0000 */
	0x0001,                 /* ISCP: busy - cleared after reset */
	0x0008,0x0000,0x0000,   /* offset,address (lo,hi) of SCB */

	0x0000,0x0000,          /* SCB: status, commands */
	0x0000,0x0000,          /* links to first command block,
				   first receive descriptor */
	0x0000,0x0000,          /* CRC error, alignment error counts */
	0x0000,0x0000,          /* out of resources, overrun error counts */

	0x0000,0x0000,          /* pad */
	0x0000,0x0000,

/* 0x20 -- start of 82586 CU program */
#define CONF_LINK 0x20
	0x0000,Cmd_Config,
	0x0032,                 /* link to next command */
	0x080c,                 /* 12 bytes follow : fifo threshold=8 */
	0x2e40,                 /* don't rx bad frames
				 * SRDY/ARDY => ext. sync. : preamble len=8
	                         * take addresses from data buffers
				 * 6 bytes/address
				 */
	0x6000,                 /* default backoff method & priority
				 * interframe spacing = 0x60 */
	0xf200,                 /* slot time=0x200
				 * max collision retry = 0xf */
#define CONF_PROMISC  0x2e
	0x0000,                 /* no HDLC : normal CRC : enable broadcast
				 * disable promiscuous/multicast modes */
	0x003c,                 /* minimum frame length = 60 octets) */

	0x0000,Cmd_SetAddr,
	0x003e,                 /* link to next command */
#define CONF_HWADDR  0x38
	0x0000,0x0000,0x0000,   /* hardware address placed here */

	0x0000,Cmd_MCast,
	0x0076,                 /* link to next command */
#define CONF_NR_MULTICAST 0x44
	0x0000,                 /* number of bytes in multicast address(es) */
#define CONF_MULTICAST 0x46
	0x0000, 0x0000, 0x0000, /* some addresses */
	0x0000, 0x0000, 0x0000,
	0x0000, 0x0000, 0x0000,
	0x0000, 0x0000, 0x0000,
	0x0000, 0x0000, 0x0000,
	0x0000, 0x0000, 0x0000,
	0x0000, 0x0000, 0x0000,
	0x0000, 0x0000, 0x0000,

#define CONF_DIAG_RESULT  0x76
	0x0000, Cmd_Diag,
	0x007c,                 /* link to next command */

	0x0000,Cmd_TDR|Cmd_INT,
	0x0084,
#define CONF_TDR_RESULT  0x82
	0x0000,

	0x0000,Cmd_END|Cmd_Nop, /* end of configure sequence */
	0x0084                  /* dummy link */
};

/* maps irq number to EtherExpress magic value */
static char irqrmap[] = { 0,0,1,2,3,4,0,0,0,1,5,6,0,0,0,0 };

#ifdef CONFIG_MCA_LEGACY
/* mapping of the first four bits of the second POS register */
static unsigned short mca_iomap[] = {
	0x270, 0x260, 0x250, 0x240, 0x230, 0x220, 0x210, 0x200,
	0x370, 0x360, 0x350, 0x340, 0x330, 0x320, 0x310, 0x300
};
/* bits 5-7 of the second POS register */
static char mca_irqmap[] = { 12, 9, 3, 4, 5, 10, 11, 15 };
#endif

/*
 * Prototypes for Linux interface
 */

static int eexp_open(struct net_device *dev);
static int eexp_close(struct net_device *dev);
static void eexp_timeout(struct net_device *dev);
static int eexp_xmit(struct sk_buff *buf, struct net_device *dev);

static irqreturn_t eexp_irq(int irq, void *dev_addr);
static void eexp_set_multicast(struct net_device *dev);

/*
 * Prototypes for hardware access functions
 */

static void eexp_hw_rx_pio(struct net_device *dev);
static void eexp_hw_tx_pio(struct net_device *dev, unsigned short *buf,
		       unsigned short len);
static int eexp_hw_probe(struct net_device *dev,unsigned short ioaddr);
static unsigned short eexp_hw_readeeprom(unsigned short ioaddr,
					 unsigned char location);

static unsigned short eexp_hw_lasttxstat(struct net_device *dev);
static void eexp_hw_txrestart(struct net_device *dev);

static void eexp_hw_txinit    (struct net_device *dev);
static void eexp_hw_rxinit    (struct net_device *dev);

static void eexp_hw_init586   (struct net_device *dev);
static void eexp_setup_filter (struct net_device *dev);

static char *eexp_ifmap[]={"AUI", "BNC", "RJ45"};
enum eexp_iftype {AUI=0, BNC=1, TPE=2};

#define STARTED_RU      2
#define STARTED_CU      1

/*
 * Primitive hardware access functions.
 */

static inline unsigned short scb_status(struct net_device *dev)
{
	return inw(dev->base_addr + 0xc008);
}

static inline unsigned short scb_rdcmd(struct net_device *dev)
{
	return inw(dev->base_addr + 0xc00a);
}

static inline void scb_command(struct net_device *dev, unsigned short cmd)
{
	outw(cmd, dev->base_addr + 0xc00a);
}

static inline void scb_wrcbl(struct net_device *dev, unsigned short val)
{
	outw(val, dev->base_addr + 0xc00c);
}

static inline void scb_wrrfa(struct net_device *dev, unsigned short val)
{
	outw(val, dev->base_addr + 0xc00e);
}

static inline void set_loopback(struct net_device *dev)
{
	outb(inb(dev->base_addr + Config) | 2, dev->base_addr + Config);
}

static inline void clear_loopback(struct net_device *dev)
{
	outb(inb(dev->base_addr + Config) & ~2, dev->base_addr + Config);
}

static inline unsigned short int SHADOW(short int addr)
{
	addr &= 0x1f;
	if (addr > 0xf) addr += 0x3ff0;
	return addr + 0x4000;
}

/*
 * Linux interface
 */

/*
 * checks for presence of EtherExpress card
 */

static int __init do_express_probe(struct net_device *dev)
{
	unsigned short *port;
	static unsigned short ports[] = { 0x240,0x300,0x310,0x270,0x320,0x340,0 };
	unsigned short ioaddr = dev->base_addr;
	int dev_irq = dev->irq;
	int err;

	dev->if_port = 0xff; /* not set */

#ifdef CONFIG_MCA_LEGACY
	if (MCA_bus) {
		int slot = 0;

		/*
		 * Only find one card at a time.  Subsequent calls
		 * will find others, however, proper multicard MCA
		 * probing and setup can't be done with the
		 * old-style Space.c init routines.  -- ASF
		 */
		while (slot != MCA_NOTFOUND) {
			int pos0, pos1;

			slot = mca_find_unused_adapter(0x628B, slot);
			if (slot == MCA_NOTFOUND)
				break;

			pos0 = mca_read_stored_pos(slot, 2);
			pos1 = mca_read_stored_pos(slot, 3);
			ioaddr = mca_iomap[pos1&0xf];

			dev->irq = mca_irqmap[(pos1>>4)&0x7];

			/*
			 * XXX: Transciever selection is done
			 * differently on the MCA version.
			 * How to get it to select something
			 * other than external/AUI is currently
			 * unknown.  This code is just for looks. -- ASF
			 */
			if ((pos0 & 0x7) == 0x1)
				dev->if_port = AUI;
			else if ((pos0 & 0x7) == 0x5) {
				if (pos1 & 0x80)
					dev->if_port = BNC;
				else
					dev->if_port = TPE;
			}

			mca_set_adapter_name(slot, "Intel EtherExpress 16 MCA");
			mca_set_adapter_procfn(slot, NULL, dev);
			mca_mark_as_used(slot);

			break;
		}
	}
#endif
	if (ioaddr&0xfe00) {
		if (!request_region(ioaddr, EEXP_IO_EXTENT, "EtherExpress"))
			return -EBUSY;
		err = eexp_hw_probe(dev,ioaddr);
		release_region(ioaddr, EEXP_IO_EXTENT);
		return err;
	} else if (ioaddr)
		return -ENXIO;

	for (port=&ports[0] ; *port ; port++ )
	{
		unsigned short sum = 0;
		int i;
		if (!request_region(*port, EEXP_IO_EXTENT, "EtherExpress"))
			continue;
		for ( i=0 ; i<4 ; i++ )
		{
			unsigned short t;
			t = inb(*port + ID_PORT);
			sum |= (t>>4) << ((t & 0x03)<<2);
		}
		if (sum==0xbaba && !eexp_hw_probe(dev,*port)) {
			release_region(*port, EEXP_IO_EXTENT);
			return 0;
		}
		release_region(*port, EEXP_IO_EXTENT);
		dev->irq = dev_irq;
	}
	return -ENODEV;
}

#ifndef MODULE
struct net_device * __init express_probe(int unit)
{
	struct net_device *dev = alloc_etherdev(sizeof(struct net_local));
	int err;

	if (!dev)
		return ERR_PTR(-ENOMEM);

	sprintf(dev->name, "eth%d", unit);
	netdev_boot_setup_check(dev);

	err = do_express_probe(dev);
	if (!err)
		return dev;
	free_netdev(dev);
	return ERR_PTR(err);
}
#endif

/*
 * open and initialize the adapter, ready for use
 */

static int eexp_open(struct net_device *dev)
{
	int ret;
	unsigned short ioaddr = dev->base_addr;
	struct net_local *lp = netdev_priv(dev);

#if NET_DEBUG > 6
	printk(KERN_DEBUG "%s: eexp_open()\n", dev->name);
#endif

	if (!dev->irq || !irqrmap[dev->irq])
		return -ENXIO;

	ret = request_irq(dev->irq, &eexp_irq, 0, dev->name, dev);
	if (ret)
		return ret;

	if (!request_region(ioaddr, EEXP_IO_EXTENT, "EtherExpress")) {
		printk(KERN_WARNING "EtherExpress io port %x, is busy.\n"
			, ioaddr);
		goto err_out1;
	}
	if (!request_region(ioaddr+0x4000, EEXP_IO_EXTENT, "EtherExpress shadow")) {
		printk(KERN_WARNING "EtherExpress io port %x, is busy.\n"
			, ioaddr+0x4000);
		goto err_out2;
	}
	if (!request_region(ioaddr+0x8000, EEXP_IO_EXTENT, "EtherExpress shadow")) {
		printk(KERN_WARNING "EtherExpress io port %x, is busy.\n"
			, ioaddr+0x8000);
		goto err_out3;
	}
	if (!request_region(ioaddr+0xc000, EEXP_IO_EXTENT, "EtherExpress shadow")) {
		printk(KERN_WARNING "EtherExpress io port %x, is busy.\n"
			, ioaddr+0xc000);
		goto err_out4;
	}

	if (lp->width) {
		printk("%s: forcing ASIC to 8-bit mode\n", dev->name);
		outb(inb(dev->base_addr+Config)&~4, dev->base_addr+Config);
	}

	eexp_hw_init586(dev);
	netif_start_queue(dev);
#if NET_DEBUG > 6
	printk(KERN_DEBUG "%s: leaving eexp_open()\n", dev->name);
#endif
	return 0;

	err_out4:
		release_region(ioaddr+0x8000, EEXP_IO_EXTENT);
	err_out3:
		release_region(ioaddr+0x4000, EEXP_IO_EXTENT);
	err_out2:
		release_region(ioaddr, EEXP_IO_EXTENT);
	err_out1:
		free_irq(dev->irq, dev);
		return -EBUSY;
}

/*
 * close and disable the interface, leaving the 586 in reset.
 */

static int eexp_close(struct net_device *dev)
{
	unsigned short ioaddr = dev->base_addr;
	struct net_local *lp = netdev_priv(dev);

	int irq = dev->irq;

	netif_stop_queue(dev);

	outb(SIRQ_dis|irqrmap[irq],ioaddr+SET_IRQ);
	lp->started = 0;
	scb_command(dev, SCB_CUsuspend|SCB_RUsuspend);
	outb(0,ioaddr+SIGNAL_CA);
	free_irq(irq,dev);
	outb(i586_RST,ioaddr+EEPROM_Ctrl);
	release_region(ioaddr, EEXP_IO_EXTENT);
	release_region(ioaddr+0x4000, 16);
	release_region(ioaddr+0x8000, 16);
	release_region(ioaddr+0xc000, 16);

	return 0;
}

/*
 * This gets called when a higher level thinks we are broken.  Check that
 * nothing has become jammed in the CU.
 */

static void unstick_cu(struct net_device *dev)
{
	struct net_local *lp = netdev_priv(dev);
	unsigned short ioaddr = dev->base_addr;

	if (lp->started)
	{
		if (time_after(jiffies, dev->trans_start + 50))
		{
			if (lp->tx_link==lp->last_tx_restart)
			{
				unsigned short boguscount=200,rsst;
				printk(KERN_WARNING "%s: Retransmit timed out, status %04x, resetting...\n",
				       dev->name, scb_status(dev));
				eexp_hw_txinit(dev);
				lp->last_tx_restart = 0;
				scb_wrcbl(dev, lp->tx_link);
				scb_command(dev, SCB_CUstart);
				outb(0,ioaddr+SIGNAL_CA);
				while (!SCB_complete(rsst=scb_status(dev)))
				{
					if (!--boguscount)
					{
						boguscount=200;
						printk(KERN_WARNING "%s: Reset timed out status %04x, retrying...\n",
						       dev->name,rsst);
						scb_wrcbl(dev, lp->tx_link);
						scb_command(dev, SCB_CUstart);
						outb(0,ioaddr+SIGNAL_CA);
					}
				}
				netif_wake_queue(dev);
			}
			else
			{
				unsigned short status = scb_status(dev);
				if (SCB_CUdead(status))
				{
					unsigned short txstatus = eexp_hw_lasttxstat(dev);
					printk(KERN_WARNING "%s: Transmit timed out, CU not active status %04x %04x, restarting...\n",
					       dev->name, status, txstatus);
					eexp_hw_txrestart(dev);
				}
				else
				{
					unsigned short txstatus = eexp_hw_lasttxstat(dev);
					if (netif_queue_stopped(dev) && !txstatus)
					{
						printk(KERN_WARNING "%s: CU wedged, status %04x %04x, resetting...\n",
						       dev->name,status,txstatus);
						eexp_hw_init586(dev);
						netif_wake_queue(dev);
					}
					else
					{
						printk(KERN_WARNING "%s: transmit timed out\n", dev->name);
					}
				}
			}
		}
	}
	else
	{
		if (time_after(jiffies, lp->init_time + 10))
		{
			unsigned short status = scb_status(dev);
			printk(KERN_WARNING "%s: i82586 startup timed out, status %04x, resetting...\n",
			       dev->name, status);
			eexp_hw_init586(dev);
			netif_wake_queue(dev);
		}
	}
}

static void eexp_timeout(struct net_device *dev)
{
	struct net_local *lp = netdev_priv(dev);
#ifdef CONFIG_SMP
	unsigned long flags;
#endif
	int status;

	disable_irq(dev->irq);

	/*
	 *	Best would be to use synchronize_irq(); spin_lock() here
	 *	lets make it work first..
	 */

#ifdef CONFIG_SMP
	spin_lock_irqsave(&lp->lock, flags);
#endif

	status = scb_status(dev);
	unstick_cu(dev);
	printk(KERN_INFO "%s: transmit timed out, %s?\n", dev->name,
	       (SCB_complete(status)?"lost interrupt":
		"board on fire"));
	dev->stats.tx_errors++;
	lp->last_tx = jiffies;
	if (!SCB_complete(status)) {
		scb_command(dev, SCB_CUabort);
		outb(0,dev->base_addr+SIGNAL_CA);
	}
	netif_wake_queue(dev);
#ifdef CONFIG_SMP
	spin_unlock_irqrestore(&lp->lock, flags);
#endif
}

/*
 * Called to transmit a packet, or to allow us to right ourselves
 * if the kernel thinks we've died.
 */
static int eexp_xmit(struct sk_buff *buf, struct net_device *dev)
{
	short length = buf->len;
#ifdef CONFIG_SMP
	struct net_local *lp = netdev_priv(dev);
	unsigned long flags;
#endif

#if NET_DEBUG > 6
	printk(KERN_DEBUG "%s: eexp_xmit()\n", dev->name);
#endif

	if (buf->len < ETH_ZLEN) {
		if (skb_padto(buf, ETH_ZLEN))
			return 0;
		length = ETH_ZLEN;
	}

	disable_irq(dev->irq);

	/*
	 *	Best would be to use synchronize_irq(); spin_lock() here
	 *	lets make it work first..
	 */

#ifdef CONFIG_SMP
	spin_lock_irqsave(&lp->lock, flags);
#endif

	{
		unsigned short *data = (unsigned short *)buf->data;

		dev->stats.tx_bytes += length;

	        eexp_hw_tx_pio(dev,data,length);
	}
	dev_kfree_skb(buf);
#ifdef CONFIG_SMP
	spin_unlock_irqrestore(&lp->lock, flags);
#endif
	enable_irq(dev->irq);
	return 0;
}

/*
 * Handle an EtherExpress interrupt
 * If we've finished initializing, start the RU and CU up.
 * If we've already started, reap tx buffers, handle any received packets,
 * check to make sure we've not become wedged.
 */

static unsigned short eexp_start_irq(struct net_device *dev,
				     unsigned short status)
{
	unsigned short ack_cmd = SCB_ack(status);
	struct net_local *lp = netdev_priv(dev);
	unsigned short ioaddr = dev->base_addr;
	if ((dev->flags & IFF_UP) && !(lp->started & STARTED_CU)) {
		short diag_status, tdr_status;
		while (SCB_CUstat(status)==2)
			status = scb_status(dev);
#if NET_DEBUG > 4
		printk("%s: CU went non-active (status %04x)\n",
		       dev->name, status);
#endif

		outw(CONF_DIAG_RESULT & ~31, ioaddr + SM_PTR);
		diag_status = inw(ioaddr + SHADOW(CONF_DIAG_RESULT));
		if (diag_status & 1<<11) {
			printk(KERN_WARNING "%s: 82586 failed self-test\n",
			       dev->name);
		} else if (!(diag_status & 1<<13)) {
			printk(KERN_WARNING "%s: 82586 self-test failed to complete\n", dev->name);
		}

		outw(CONF_TDR_RESULT & ~31, ioaddr + SM_PTR);
		tdr_status = inw(ioaddr + SHADOW(CONF_TDR_RESULT));
		if (tdr_status & (TDR_SHORT|TDR_OPEN)) {
			printk(KERN_WARNING "%s: TDR reports cable %s at %d tick%s\n", dev->name, (tdr_status & TDR_SHORT)?"short":"broken", tdr_status & TDR_TIME, ((tdr_status & TDR_TIME) != 1) ? "s" : "");
		}
		else if (tdr_status & TDR_XCVRPROBLEM) {
			printk(KERN_WARNING "%s: TDR reports transceiver problem\n", dev->name);
		}
		else if (tdr_status & TDR_LINKOK) {
#if NET_DEBUG > 4
			printk(KERN_DEBUG "%s: TDR reports link OK\n", dev->name);
#endif
		} else {
			printk("%s: TDR is ga-ga (status %04x)\n", dev->name,
			       tdr_status);
		}

		lp->started |= STARTED_CU;
		scb_wrcbl(dev, lp->tx_link);
		/* if the RU isn't running, start it now */
		if (!(lp->started & STARTED_RU)) {
			ack_cmd |= SCB_RUstart;
			scb_wrrfa(dev, lp->rx_buf_start);
			lp->rx_ptr = lp->rx_buf_start;
			lp->started |= STARTED_RU;
		}
		ack_cmd |= SCB_CUstart | 0x2000;
	}

	if ((dev->flags & IFF_UP) && !(lp->started & STARTED_RU) && SCB_RUstat(status)==4)
		lp->started|=STARTED_RU;

	return ack_cmd;
}

static void eexp_cmd_clear(struct net_device *dev)
{
	unsigned long int oldtime = jiffies;
	while (scb_rdcmd(dev) && (time_before(jiffies, oldtime + 10)));
	if (scb_rdcmd(dev)) {
		printk("%s: command didn't clear\n", dev->name);
	}
}

static irqreturn_t eexp_irq(int dummy, void *dev_info)
{
	struct net_device *dev = dev_info;
	struct net_local *lp;
	unsigned short ioaddr,status,ack_cmd;
	unsigned short old_read_ptr, old_write_ptr;

	lp = netdev_priv(dev);
	ioaddr = dev->base_addr;

	spin_lock(&lp->lock);

	old_read_ptr = inw(ioaddr+READ_PTR);
	old_write_ptr = inw(ioaddr+WRITE_PTR);

	outb(SIRQ_dis|irqrmap[dev->irq], ioaddr+SET_IRQ);

	status = scb_status(dev);

#if NET_DEBUG > 4
	printk(KERN_DEBUG "%s: interrupt (status %x)\n", dev->name, status);
#endif

	if (lp->started == (STARTED_CU | STARTED_RU)) {

		do {
			eexp_cmd_clear(dev);

			ack_cmd = SCB_ack(status);
			scb_command(dev, ack_cmd);
			outb(0,ioaddr+SIGNAL_CA);

			eexp_cmd_clear(dev);

			if (SCB_complete(status)) {
				if (!eexp_hw_lasttxstat(dev)) {
					printk("%s: tx interrupt but no status\n", dev->name);
				}
			}

			if (SCB_rxdframe(status))
				eexp_hw_rx_pio(dev);

			status = scb_status(dev);
		} while (status & 0xc000);

		if (SCB_RUdead(status))
		{
			printk(KERN_WARNING "%s: RU stopped: status %04x\n",
			       dev->name,status);
#if 0
			printk(KERN_WARNING "%s: cur_rfd=%04x, cur_rbd=%04x\n", dev->name, lp->cur_rfd, lp->cur_rbd);
			outw(lp->cur_rfd, ioaddr+READ_PTR);
			printk(KERN_WARNING "%s: [%04x]\n", dev->name, inw(ioaddr+DATAPORT));
			outw(lp->cur_rfd+6, ioaddr+READ_PTR);
			printk(KERN_WARNING "%s: rbd is %04x\n", dev->name, rbd= inw(ioaddr+DATAPORT));
			outw(rbd, ioaddr+READ_PTR);
			printk(KERN_WARNING "%s: [%04x %04x] ", dev->name, inw(ioaddr+DATAPORT), inw(ioaddr+DATAPORT));
			outw(rbd+8, ioaddr+READ_PTR);
			printk("[%04x]\n", inw(ioaddr+DATAPORT));
#endif
			dev->stats.rx_errors++;
#if 1
		        eexp_hw_rxinit(dev);
#else
			lp->cur_rfd = lp->first_rfd;
#endif
			scb_wrrfa(dev, lp->rx_buf_start);
			scb_command(dev, SCB_RUstart);
			outb(0,ioaddr+SIGNAL_CA);
		}
	} else {
		if (status & 0x8000)
			ack_cmd = eexp_start_irq(dev, status);
		else
			ack_cmd = SCB_ack(status);
		scb_command(dev, ack_cmd);
		outb(0,ioaddr+SIGNAL_CA);
	}

	eexp_cmd_clear(dev);

	outb(SIRQ_en|irqrmap[dev->irq], ioaddr+SET_IRQ);

#if NET_DEBUG > 6
	printk("%s: leaving eexp_irq()\n", dev->name);
#endif
	outw(old_read_ptr, ioaddr+READ_PTR);
	outw(old_write_ptr, ioaddr+WRITE_PTR);

	spin_unlock(&lp->lock);
	return IRQ_HANDLED;
}

/*
 * Hardware access functions
 */

/*
 * Set the cable type to use.
 */

static void eexp_hw_set_interface(struct net_device *dev)
{
	unsigned char oldval = inb(dev->base_addr + 0x300e);
	oldval &= ~0x82;
	switch (dev->if_port) {
	case TPE:
		oldval |= 0x2;
	case BNC:
		oldval |= 0x80;
		break;
	}
	outb(oldval, dev->base_addr+0x300e);
	mdelay(20);
}

/*
 * Check all the receive buffers, and hand any received packets
 * to the upper levels. Basic sanity check on each frame
 * descriptor, though we don't bother trying to fix broken ones.
 */

static void eexp_hw_rx_pio(struct net_device *dev)
{
	struct net_local *lp = netdev_priv(dev);
	unsigned short rx_block = lp->rx_ptr;
	unsigned short boguscount = lp->num_rx_bufs;
	unsigned short ioaddr = dev->base_addr;
	unsigned short status;

#if NET_DEBUG > 6
	printk(KERN_DEBUG "%s: eexp_hw_rx()\n", dev->name);
#endif

 	do {
 		unsigned short rfd_cmd, rx_next, pbuf, pkt_len;

		outw(rx_block, ioaddr + READ_PTR);
		status = inw(ioaddr + DATAPORT);

		if (FD_Done(status))
		{
			rfd_cmd = inw(ioaddr + DATAPORT);
			rx_next = inw(ioaddr + DATAPORT);
			pbuf = inw(ioaddr + DATAPORT);

			outw(pbuf, ioaddr + READ_PTR);
			pkt_len = inw(ioaddr + DATAPORT);

			if (rfd_cmd!=0x0000)
  			{
				printk(KERN_WARNING "%s: rfd_cmd not zero:0x%04x\n",
				       dev->name, rfd_cmd);
				continue;
			}
			else if (pbuf!=rx_block+0x16)
			{
				printk(KERN_WARNING "%s: rfd and rbd out of sync 0x%04x 0x%04x\n",
				       dev->name, rx_block+0x16, pbuf);
				continue;
			}
			else if ((pkt_len & 0xc000)!=0xc000)
			{
				printk(KERN_WARNING "%s: EOF or F not set on received buffer (%04x)\n",
				       dev->name, pkt_len & 0xc000);
  				continue;
  			}
  			else if (!FD_OK(status))
			{
				dev->stats.rx_errors++;
				if (FD_CRC(status))
					dev->stats.rx_crc_errors++;
				if (FD_Align(status))
					dev->stats.rx_frame_errors++;
				if (FD_Resrc(status))
					dev->stats.rx_fifo_errors++;
				if (FD_DMA(status))
					dev->stats.rx_over_errors++;
				if (FD_Short(status))
					dev->stats.rx_length_errors++;
			}
			else
			{
				struct sk_buff *skb;
				pkt_len &= 0x3fff;
				skb = dev_alloc_skb(pkt_len+16);
				if (skb == NULL)
				{
					printk(KERN_WARNING "%s: Memory squeeze, dropping packet\n",dev->name);
					dev->stats.rx_dropped++;
					break;
				}
				skb_reserve(skb, 2);
				outw(pbuf+10, ioaddr+READ_PTR);
			        insw(ioaddr+DATAPORT, skb_put(skb,pkt_len),(pkt_len+1)>>1);
				skb->protocol = eth_type_trans(skb,dev);
				netif_rx(skb);
				dev->stats.rx_packets++;
				dev->stats.rx_bytes += pkt_len;
			}
			outw(rx_block, ioaddr+WRITE_PTR);
			outw(0, ioaddr+DATAPORT);
			outw(0, ioaddr+DATAPORT);
			rx_block = rx_next;
		}
	} while (FD_Done(status) && boguscount--);
	lp->rx_ptr = rx_block;
}

/*
 * Hand a packet to the card for transmission
 * If we get here, we MUST have already checked
 * to make sure there is room in the transmit
 * buffer region.
 */

static void eexp_hw_tx_pio(struct net_device *dev, unsigned short *buf,
		       unsigned short len)
{
	struct net_local *lp = netdev_priv(dev);
	unsigned short ioaddr = dev->base_addr;

	if (LOCKUP16 || lp->width) {
		/* Stop the CU so that there is no chance that it
		   jumps off to a bogus address while we are writing the
		   pointer to the next transmit packet in 8-bit mode --
		   this eliminates the "CU wedged" errors in 8-bit mode.
		   (Zoltan Szilagyi 10-12-96) */
		scb_command(dev, SCB_CUsuspend);
		outw(0xFFFF, ioaddr+SIGNAL_CA);
	}

 	outw(lp->tx_head, ioaddr + WRITE_PTR);

	outw(0x0000, ioaddr + DATAPORT);
        outw(Cmd_INT|Cmd_Xmit, ioaddr + DATAPORT);
	outw(lp->tx_head+0x08, ioaddr + DATAPORT);
	outw(lp->tx_head+0x0e, ioaddr + DATAPORT);

	outw(0x0000, ioaddr + DATAPORT);
	outw(0x0000, ioaddr + DATAPORT);
	outw(lp->tx_head+0x08, ioaddr + DATAPORT);

	outw(0x8000|len, ioaddr + DATAPORT);
	outw(-1, ioaddr + DATAPORT);
	outw(lp->tx_head+0x16, ioaddr + DATAPORT);
	outw(0, ioaddr + DATAPORT);

        outsw(ioaddr + DATAPORT, buf, (len+1)>>1);

	outw(lp->tx_tail+0xc, ioaddr + WRITE_PTR);
	outw(lp->tx_head, ioaddr + DATAPORT);

	dev->trans_start = jiffies;
	lp->tx_tail = lp->tx_head;
	if (lp->tx_head==TX_BUF_START+((lp->num_tx_bufs-1)*TX_BUF_SIZE))
		lp->tx_head = TX_BUF_START;
	else
		lp->tx_head += TX_BUF_SIZE;
	if (lp->tx_head != lp->tx_reap)
		netif_wake_queue(dev);

	if (LOCKUP16 || lp->width) {
		/* Restart the CU so that the packet can actually
		   be transmitted. (Zoltan Szilagyi 10-12-96) */
		scb_command(dev, SCB_CUresume);
		outw(0xFFFF, ioaddr+SIGNAL_CA);
	}

	dev->stats.tx_packets++;
	lp->last_tx = jiffies;
}

static const struct net_device_ops eexp_netdev_ops = {
	.ndo_open 		= eexp_open,
	.ndo_stop 		= eexp_close,
	.ndo_start_xmit		= eexp_xmit,
	.ndo_set_multicast_list = eexp_set_multicast,
	.ndo_tx_timeout		= eexp_timeout,
	.ndo_change_mtu		= eth_change_mtu,
	.ndo_set_mac_address 	= eth_mac_addr,
	.ndo_validate_addr	= eth_validate_addr,
};

/*
 * Sanity check the suspected EtherExpress card
 * Read hardware address, reset card, size memory and initialize buffer
 * memory pointers. These are held in netdev_priv(), in case someone has more
 * than one card in a machine.
 */

static int __init eexp_hw_probe(struct net_device *dev, unsigned short ioaddr)
{
	unsigned short hw_addr[3];
	unsigned char buswidth;
	unsigned int memory_size;
	int i;
	unsigned short xsum = 0;
	struct net_local *lp = netdev_priv(dev);

	printk("%s: EtherExpress 16 at %#x ",dev->name,ioaddr);

	outb(ASIC_RST, ioaddr+EEPROM_Ctrl);
	outb(0, ioaddr+EEPROM_Ctrl);
	udelay(500);
	outb(i586_RST, ioaddr+EEPROM_Ctrl);

	hw_addr[0] = eexp_hw_readeeprom(ioaddr,2);
	hw_addr[1] = eexp_hw_readeeprom(ioaddr,3);
	hw_addr[2] = eexp_hw_readeeprom(ioaddr,4);

	/* Standard Address or Compaq LTE Address */
	if (!((hw_addr[2]==0x00aa && ((hw_addr[1] & 0xff00)==0x0000)) ||
	      (hw_addr[2]==0x0080 && ((hw_addr[1] & 0xff00)==0x5F00))))
	{
		printk(" rejected: invalid address %04x%04x%04x\n",
			hw_addr[2],hw_addr[1],hw_addr[0]);
		return -ENODEV;
	}

	/* Calculate the EEPROM checksum.  Carry on anyway if it's bad,
	 * though.
	 */
	for (i = 0; i < 64; i++)
		xsum += eexp_hw_readeeprom(ioaddr, i);
	if (xsum != 0xbaba)
		printk(" (bad EEPROM xsum 0x%02x)", xsum);

	dev->base_addr = ioaddr;
	for ( i=0 ; i<6 ; i++ )
		dev->dev_addr[i] = ((unsigned char *)hw_addr)[5-i];

	{
		static char irqmap[]={0, 9, 3, 4, 5, 10, 11, 0};
		unsigned short setupval = eexp_hw_readeeprom(ioaddr,0);

		/* Use the IRQ from EEPROM if none was given */
		if (!dev->irq)
			dev->irq = irqmap[setupval>>13];

		if (dev->if_port == 0xff) {
			dev->if_port = !(setupval & 0x1000) ? AUI :
				eexp_hw_readeeprom(ioaddr,5) & 0x1 ? TPE : BNC;
		}

		buswidth = !((setupval & 0x400) >> 10);
	}

	memset(lp, 0, sizeof(struct net_local));
	spin_lock_init(&lp->lock);

 	printk("(IRQ %d, %s connector, %d-bit bus", dev->irq,
 	       eexp_ifmap[dev->if_port], buswidth?8:16);

	if (!request_region(dev->base_addr + 0x300e, 1, "EtherExpress"))
		return -EBUSY;

 	eexp_hw_set_interface(dev);

	release_region(dev->base_addr + 0x300e, 1);

	/* Find out how much RAM we have on the card */
	outw(0, dev->base_addr + WRITE_PTR);
	for (i = 0; i < 32768; i++)
		outw(0, dev->base_addr + DATAPORT);

        for (memory_size = 0; memory_size < 64; memory_size++)
	{
		outw(memory_size<<10, dev->base_addr + READ_PTR);
		if (inw(dev->base_addr+DATAPORT))
			break;
		outw(memory_size<<10, dev->base_addr + WRITE_PTR);
		outw(memory_size | 0x5000, dev->base_addr+DATAPORT);
		outw(memory_size<<10, dev->base_addr + READ_PTR);
		if (inw(dev->base_addr+DATAPORT) != (memory_size | 0x5000))
			break;
	}

	/* Sort out the number of buffers.  We may have 16, 32, 48 or 64k
	 * of RAM to play with.
	 */
	lp->num_tx_bufs = 4;
	lp->rx_buf_end = 0x3ff6;
	switch (memory_size)
	{
	case 64:
		lp->rx_buf_end += 0x4000;
	case 48:
		lp->num_tx_bufs += 4;
		lp->rx_buf_end += 0x4000;
	case 32:
		lp->rx_buf_end += 0x4000;
	case 16:
		printk(", %dk RAM)\n", memory_size);
		break;
	default:
		printk(") bad memory size (%dk).\n", memory_size);
		return -ENODEV;
		break;
	}

	lp->rx_buf_start = TX_BUF_START + (lp->num_tx_bufs*TX_BUF_SIZE);
	lp->width = buswidth;

	dev->netdev_ops = &eexp_netdev_ops;
	dev->watchdog_timeo = 2*HZ;

	return register_netdev(dev);
}

/*
 * Read a word from the EtherExpress on-board serial EEPROM.
 * The EEPROM contains 64 words of 16 bits.
 */
static unsigned short __init eexp_hw_readeeprom(unsigned short ioaddr,
						    unsigned char location)
{
	unsigned short cmd = 0x180|(location&0x7f);
	unsigned short rval = 0,wval = EC_CS|i586_RST;
	int i;

	outb(EC_CS|i586_RST,ioaddr+EEPROM_Ctrl);
	for (i=0x100 ; i ; i>>=1 )
	{
		if (cmd&i)
			wval |= EC_Wr;
		else
			wval &= ~EC_Wr;

		outb(wval,ioaddr+EEPROM_Ctrl);
		outb(wval|EC_Clk,ioaddr+EEPROM_Ctrl);
		eeprom_delay();
		outb(wval,ioaddr+EEPROM_Ctrl);
		eeprom_delay();
	}
	wval &= ~EC_Wr;
	outb(wval,ioaddr+EEPROM_Ctrl);
	for (i=0x8000 ; i ; i>>=1 )
	{
		outb(wval|EC_Clk,ioaddr+EEPROM_Ctrl);
		eeprom_delay();
		if (inb(ioaddr+EEPROM_Ctrl)&EC_Rd)
			rval |= i;
		outb(wval,ioaddr+EEPROM_Ctrl);
		eeprom_delay();
	}
	wval &= ~EC_CS;
	outb(wval|EC_Clk,ioaddr+EEPROM_Ctrl);
	eeprom_delay();
	outb(wval,ioaddr+EEPROM_Ctrl);
	eeprom_delay();
	return rval;
}

/*
 * Reap tx buffers and return last transmit status.
 * if ==0 then either:
 *    a) we're not transmitting anything, so why are we here?
 *    b) we've died.
 * otherwise, Stat_Busy(return) means we've still got some packets
 * to transmit, Stat_Done(return) means our buffers should be empty
 * again
 */

static unsigned short eexp_hw_lasttxstat(struct net_device *dev)
{
	struct net_local *lp = netdev_priv(dev);
	unsigned short tx_block = lp->tx_reap;
	unsigned short status;

	if (!netif_queue_stopped(dev) && lp->tx_head==lp->tx_reap)
		return 0x0000;

	do
	{
		outw(tx_block & ~31, dev->base_addr + SM_PTR);
		status = inw(dev->base_addr + SHADOW(tx_block));
		if (!Stat_Done(status))
		{
			lp->tx_link = tx_block;
			return status;
		}
		else
		{
			lp->last_tx_restart = 0;
			dev->stats.collisions += Stat_NoColl(status);
			if (!Stat_OK(status))
			{
				char *whatsup = NULL;
				dev->stats.tx_errors++;
  				if (Stat_Abort(status))
					dev->stats.tx_aborted_errors++;
				if (Stat_TNoCar(status)) {
					whatsup = "aborted, no carrier";
					dev->stats.tx_carrier_errors++;
				}
				if (Stat_TNoCTS(status)) {
					whatsup = "aborted, lost CTS";
					dev->stats.tx_carrier_errors++;
				}
				if (Stat_TNoDMA(status)) {
					whatsup = "FIFO underran";
					dev->stats.tx_fifo_errors++;
				}
				if (Stat_TXColl(status)) {
					whatsup = "aborted, too many collisions";
					dev->stats.tx_aborted_errors++;
				}
				if (whatsup)
					printk(KERN_INFO "%s: transmit %s\n",
					       dev->name, whatsup);
			}
			else
				dev->stats.tx_packets++;
		}
		if (tx_block == TX_BUF_START+((lp->num_tx_bufs-1)*TX_BUF_SIZE))
			lp->tx_reap = tx_block = TX_BUF_START;
		else
			lp->tx_reap = tx_block += TX_BUF_SIZE;
		netif_wake_queue(dev);
	}
	while (lp->tx_reap != lp->tx_head);

	lp->tx_link = lp->tx_tail + 0x08;

	return status;
}

/*
 * This should never happen. It is called when some higher routine detects
 * that the CU has stopped, to try to restart it from the last packet we knew
 * we were working on, or the idle loop if we had finished for the time.
 */

static void eexp_hw_txrestart(struct net_device *dev)
{
	struct net_local *lp = netdev_priv(dev);
	unsigned short ioaddr = dev->base_addr;

	lp->last_tx_restart = lp->tx_link;
	scb_wrcbl(dev, lp->tx_link);
	scb_command(dev, SCB_CUstart);
	outb(0,ioaddr+SIGNAL_CA);

	{
		unsigned short boguscount=50,failcount=5;
		while (!scb_status(dev))
		{
			if (!--boguscount)
			{
				if (--failcount)
				{
					printk(KERN_WARNING "%s: CU start timed out, status %04x, cmd %04x\n", dev->name, scb_status(dev), scb_rdcmd(dev));
				        scb_wrcbl(dev, lp->tx_link);
					scb_command(dev, SCB_CUstart);
					outb(0,ioaddr+SIGNAL_CA);
					boguscount = 100;
				}
				else
				{
					printk(KERN_WARNING "%s: Failed to restart CU, resetting board...\n",dev->name);
					eexp_hw_init586(dev);
					netif_wake_queue(dev);
					return;
				}
			}
		}
	}
}

/*
 * Writes down the list of transmit buffers into card memory.  Each
 * entry consists of an 82586 transmit command, followed by a jump
 * pointing to itself.  When we want to transmit a packet, we write
 * the data into the appropriate transmit buffer and then modify the
 * preceding jump to point at the new transmit command.  This means that
 * the 586 command unit is continuously active.
 */

static void eexp_hw_txinit(struct net_device *dev)
{
	struct net_local *lp = netdev_priv(dev);
	unsigned short tx_block = TX_BUF_START;
	unsigned short curtbuf;
	unsigned short ioaddr = dev->base_addr;

	for ( curtbuf=0 ; curtbuf<lp->num_tx_bufs ; curtbuf++ )
	{
		outw(tx_block, ioaddr + WRITE_PTR);

	        outw(0x0000, ioaddr + DATAPORT);
		outw(Cmd_INT|Cmd_Xmit, ioaddr + DATAPORT);
		outw(tx_block+0x08, ioaddr + DATAPORT);
		outw(tx_block+0x0e, ioaddr + DATAPORT);

		outw(0x0000, ioaddr + DATAPORT);
		outw(0x0000, ioaddr + DATAPORT);
		outw(tx_block+0x08, ioaddr + DATAPORT);

		outw(0x8000, ioaddr + DATAPORT);
		outw(-1, ioaddr + DATAPORT);
		outw(tx_block+0x16, ioaddr + DATAPORT);
		outw(0x0000, ioaddr + DATAPORT);

		tx_block += TX_BUF_SIZE;
	}
	lp->tx_head = TX_BUF_START;
	lp->tx_reap = TX_BUF_START;
	lp->tx_tail = tx_block - TX_BUF_SIZE;
	lp->tx_link = lp->tx_tail + 0x08;
	lp->rx_buf_start = tx_block;

}

/*
 * Write the circular list of receive buffer descriptors to card memory.
 * The end of the list isn't marked, which means that the 82586 receive
 * unit will loop until buffers become available (this avoids it giving us
 * "out of resources" messages).
 */

static void eexp_hw_rxinit(struct net_device *dev)
{
	struct net_local *lp = netdev_priv(dev);
	unsigned short rx_block = lp->rx_buf_start;
	unsigned short ioaddr = dev->base_addr;

	lp->num_rx_bufs = 0;
	lp->rx_first = lp->rx_ptr = rx_block;
	do
	{
		lp->num_rx_bufs++;

		outw(rx_block, ioaddr + WRITE_PTR);

		outw(0, ioaddr + DATAPORT);  outw(0, ioaddr+DATAPORT);
		outw(rx_block + RX_BUF_SIZE, ioaddr+DATAPORT);
		outw(0xffff, ioaddr+DATAPORT);

		outw(0x0000, ioaddr+DATAPORT);
		outw(0xdead, ioaddr+DATAPORT);
		outw(0xdead, ioaddr+DATAPORT);
		outw(0xdead, ioaddr+DATAPORT);
		outw(0xdead, ioaddr+DATAPORT);
		outw(0xdead, ioaddr+DATAPORT);
		outw(0xdead, ioaddr+DATAPORT);

		outw(0x0000, ioaddr+DATAPORT);
		outw(rx_block + RX_BUF_SIZE + 0x16, ioaddr+DATAPORT);
		outw(rx_block + 0x20, ioaddr+DATAPORT);
		outw(0, ioaddr+DATAPORT);
		outw(RX_BUF_SIZE-0x20, ioaddr+DATAPORT);

		lp->rx_last = rx_block;
		rx_block += RX_BUF_SIZE;
	} while (rx_block <= lp->rx_buf_end-RX_BUF_SIZE);


	/* Make first Rx frame descriptor point to first Rx buffer
           descriptor */
	outw(lp->rx_first + 6, ioaddr+WRITE_PTR);
	outw(lp->rx_first + 0x16, ioaddr+DATAPORT);

	/* Close Rx frame descriptor ring */
  	outw(lp->rx_last + 4, ioaddr+WRITE_PTR);
  	outw(lp->rx_first, ioaddr+DATAPORT);

	/* Close Rx buffer descriptor ring */
	outw(lp->rx_last + 0x16 + 2, ioaddr+WRITE_PTR);
	outw(lp->rx_first + 0x16, ioaddr+DATAPORT);

}

/*
 * Un-reset the 586, and start the configuration sequence. We don't wait for
 * this to finish, but allow the interrupt handler to start the CU and RU for
 * us.  We can't start the receive/transmission system up before we know that
 * the hardware is configured correctly.
 */

static void eexp_hw_init586(struct net_device *dev)
{
	struct net_local *lp = netdev_priv(dev);
	unsigned short ioaddr = dev->base_addr;
	int i;

#if NET_DEBUG > 6
	printk("%s: eexp_hw_init586()\n", dev->name);
#endif

	lp->started = 0;

	set_loopback(dev);

	outb(SIRQ_dis|irqrmap[dev->irq],ioaddr+SET_IRQ);

	/* Download the startup code */
	outw(lp->rx_buf_end & ~31, ioaddr + SM_PTR);
	outw(lp->width?0x0001:0x0000, ioaddr + 0x8006);
	outw(0x0000, ioaddr + 0x8008);
	outw(0x0000, ioaddr + 0x800a);
	outw(0x0000, ioaddr + 0x800c);
	outw(0x0000, ioaddr + 0x800e);

	for (i = 0; i < (sizeof(start_code)); i+=32) {
		int j;
		outw(i, ioaddr + SM_PTR);
		for (j = 0; j < 16; j+=2)
			outw(start_code[(i+j)/2],
			     ioaddr+0x4000+j);
		for (j = 0; j < 16; j+=2)
			outw(start_code[(i+j+16)/2],
			     ioaddr+0x8000+j);
	}

	/* Do we want promiscuous mode or multicast? */
	outw(CONF_PROMISC & ~31, ioaddr+SM_PTR);
	i = inw(ioaddr+SHADOW(CONF_PROMISC));
	outw((dev->flags & IFF_PROMISC)?(i|1):(i & ~1),
	     ioaddr+SHADOW(CONF_PROMISC));
	lp->was_promisc = dev->flags & IFF_PROMISC;
#if 0
	eexp_setup_filter(dev);
#endif

	/* Write our hardware address */
	outw(CONF_HWADDR & ~31, ioaddr+SM_PTR);
	outw(((unsigned short *)dev->dev_addr)[0], ioaddr+SHADOW(CONF_HWADDR));
	outw(((unsigned short *)dev->dev_addr)[1],
	     ioaddr+SHADOW(CONF_HWADDR+2));
	outw(((unsigned short *)dev->dev_addr)[2],
	     ioaddr+SHADOW(CONF_HWADDR+4));

	eexp_hw_txinit(dev);
	eexp_hw_rxinit(dev);

	outb(0,ioaddr+EEPROM_Ctrl);
	mdelay(5);

	scb_command(dev, 0xf000);
	outb(0,ioaddr+SIGNAL_CA);

	outw(0, ioaddr+SM_PTR);

	{
		unsigned short rboguscount=50,rfailcount=5;
		while (inw(ioaddr+0x4000))
		{
			if (!--rboguscount)
			{
				printk(KERN_WARNING "%s: i82586 reset timed out, kicking...\n",
					dev->name);
				scb_command(dev, 0);
				outb(0,ioaddr+SIGNAL_CA);
				rboguscount = 100;
				if (!--rfailcount)
				{
					printk(KERN_WARNING "%s: i82586 not responding, giving up.\n",
						dev->name);
					return;
				}
			}
		}
	}

        scb_wrcbl(dev, CONF_LINK);
	scb_command(dev, 0xf000|SCB_CUstart);
	outb(0,ioaddr+SIGNAL_CA);

	{
		unsigned short iboguscount=50,ifailcount=5;
		while (!scb_status(dev))
		{
			if (!--iboguscount)
			{
				if (--ifailcount)
				{
					printk(KERN_WARNING "%s: i82586 initialization timed out, status %04x, cmd %04x\n",
						dev->name, scb_status(dev), scb_rdcmd(dev));
					scb_wrcbl(dev, CONF_LINK);
				        scb_command(dev, 0xf000|SCB_CUstart);
					outb(0,ioaddr+SIGNAL_CA);
					iboguscount = 100;
				}
				else
				{
					printk(KERN_WARNING "%s: Failed to initialize i82586, giving up.\n",dev->name);
					return;
				}
			}
		}
	}

	clear_loopback(dev);
	outb(SIRQ_en|irqrmap[dev->irq],ioaddr+SET_IRQ);

	lp->init_time = jiffies;
#if NET_DEBUG > 6
        printk("%s: leaving eexp_hw_init586()\n", dev->name);
#endif
	return;
}

static void eexp_setup_filter(struct net_device *dev)
{
	struct dev_mc_list *dmi;
	unsigned short ioaddr = dev->base_addr;
	int count = dev->mc_count;
	int i;
	if (count > 8) {
		printk(KERN_INFO "%s: too many multicast addresses (%d)\n",
		       dev->name, count);
		count = 8;
	}

	outw(CONF_NR_MULTICAST & ~31, ioaddr+SM_PTR);
	outw(6*count, ioaddr+SHADOW(CONF_NR_MULTICAST));
	for (i = 0, dmi = dev->mc_list; i < count; i++, dmi = dmi->next) {
		unsigned short *data;
		if (!dmi) {
			printk(KERN_INFO "%s: too few multicast addresses\n", dev->name);
			break;
		}
		if (dmi->dmi_addrlen != ETH_ALEN) {
			printk(KERN_INFO "%s: invalid multicast address length given.\n", dev->name);
			continue;
		}
		data = (unsigned short *)dmi->dmi_addr;
		outw((CONF_MULTICAST+(6*i)) & ~31, ioaddr+SM_PTR);
		outw(data[0], ioaddr+SHADOW(CONF_MULTICAST+(6*i)));
		outw((CONF_MULTICAST+(6*i)+2) & ~31, ioaddr+SM_PTR);
		outw(data[1], ioaddr+SHADOW(CONF_MULTICAST+(6*i)+2));
		outw((CONF_MULTICAST+(6*i)+4) & ~31, ioaddr+SM_PTR);
		outw(data[2], ioaddr+SHADOW(CONF_MULTICAST+(6*i)+4));
	}
}

/*
 * Set or clear the multicast filter for this adaptor.
 */
static void
eexp_set_multicast(struct net_device *dev)
{
        unsigned short ioaddr = dev->base_addr;
        struct net_local *lp = netdev_priv(dev);
        int kick = 0, i;
        if ((dev->flags & IFF_PROMISC) != lp->was_promisc) {
                outw(CONF_PROMISC & ~31, ioaddr+SM_PTR);
                i = inw(ioaddr+SHADOW(CONF_PROMISC));
                outw((dev->flags & IFF_PROMISC)?(i|1):(i & ~1),
                     ioaddr+SHADOW(CONF_PROMISC));
                lp->was_promisc = dev->flags & IFF_PROMISC;
                kick = 1;
        }
        if (!(dev->flags & IFF_PROMISC)) {
                eexp_setup_filter(dev);
                if (lp->old_mc_count != dev->mc_count) {
                        kick = 1;
                        lp->old_mc_count = dev->mc_count;
                }
        }
        if (kick) {
                unsigned long oj;
                scb_command(dev, SCB_CUsuspend);
                outb(0, ioaddr+SIGNAL_CA);
                outb(0, ioaddr+SIGNAL_CA);
#if 0
                printk("%s: waiting for CU to go suspended\n", dev->name);
#endif
                oj = jiffies;
                while ((SCB_CUstat(scb_status(dev)) == 2) &&
                       (time_before(jiffies, oj + 2000)));
		if (SCB_CUstat(scb_status(dev)) == 2)
			printk("%s: warning, CU didn't stop\n", dev->name);
                lp->started &= ~(STARTED_CU);
                scb_wrcbl(dev, CONF_LINK);
                scb_command(dev, SCB_CUstart);
                outb(0, ioaddr+SIGNAL_CA);
        }
}


/*
 * MODULE stuff
 */

#ifdef MODULE

#define EEXP_MAX_CARDS     4    /* max number of cards to support */

static struct net_device *dev_eexp[EEXP_MAX_CARDS];
static int irq[EEXP_MAX_CARDS];
static int io[EEXP_MAX_CARDS];

module_param_array(io, int, NULL, 0);
module_param_array(irq, int, NULL, 0);
MODULE_PARM_DESC(io, "EtherExpress 16 I/O base address(es)");
MODULE_PARM_DESC(irq, "EtherExpress 16 IRQ number(s)");
MODULE_LICENSE("GPL");


/* Ideally the user would give us io=, irq= for every card.  If any parameters
 * are specified, we verify and then use them.  If no parameters are given, we
 * autoprobe for one card only.
 */
int __init init_module(void)
{
	struct net_device *dev;
	int this_dev, found = 0;

	for (this_dev = 0; this_dev < EEXP_MAX_CARDS; this_dev++) {
		dev = alloc_etherdev(sizeof(struct net_local));
		dev->irq = irq[this_dev];
		dev->base_addr = io[this_dev];
		if (io[this_dev] == 0) {
			if (this_dev)
				break;
			printk(KERN_NOTICE "eexpress.c: Module autoprobe not recommended, give io=xx.\n");
		}
		if (do_express_probe(dev) == 0) {
			dev_eexp[this_dev] = dev;
			found++;
			continue;
		}
		printk(KERN_WARNING "eexpress.c: Failed to register card at 0x%x.\n", io[this_dev]);
		free_netdev(dev);
		break;
	}
	if (found)
		return 0;
	return -ENXIO;
}

void __exit cleanup_module(void)
{
	int this_dev;

	for (this_dev = 0; this_dev < EEXP_MAX_CARDS; this_dev++) {
		struct net_device *dev = dev_eexp[this_dev];
		if (dev) {
			unregister_netdev(dev);
			free_netdev(dev);
		}
	}
}
#endif

/*
 * Local Variables:
 *  c-file-style: "linux"
 *  tab-width: 8
 * End:
 */
