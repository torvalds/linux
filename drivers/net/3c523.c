/*
   net-3-driver for the 3c523 Etherlink/MC card (i82586 Ethernet chip)


   This is an extension to the Linux operating system, and is covered by the
   same GNU General Public License that covers that work.

   Copyright 1995, 1996 by Chris Beauregard (cpbeaure@undergrad.math.uwaterloo.ca)

   This is basically Michael Hipp's ni52 driver, with a new probing
   algorithm and some minor changes to the 82586 CA and reset routines.
   Thanks a lot Michael for a really clean i82586 implementation!  Unless
   otherwise documented in ni52.c, any bugs are mine.

   Contrary to the Ethernet-HOWTO, this isn't based on the 3c507 driver in
   any way.  The ni52 is a lot easier to modify.

   sources:
   ni52.c

   Crynwr packet driver collection was a great reference for my first
   attempt at this sucker.  The 3c507 driver also helped, until I noticed
   that ni52.c was a lot nicer.

   EtherLink/MC: Micro Channel Ethernet Adapter Technical Reference
   Manual, courtesy of 3Com CardFacts, documents the 3c523-specific
   stuff.  Information on CardFacts is found in the Ethernet HOWTO.
   Also see <a href="http://www.3com.com/">

   Microprocessor Communications Support Chips, T.J. Byers, ISBN
   0-444-01224-9, has a section on the i82586.  It tells you just enough
   to know that you really don't want to learn how to program the chip.

   The original device probe code was stolen from ps2esdi.c

   Known Problems:
   Since most of the code was stolen from ni52.c, you'll run across the
   same bugs in the 0.62 version of ni52.c, plus maybe a few because of
   the 3c523 idiosynchacies.  The 3c523 has 16K of RAM though, so there
   shouldn't be the overrun problem that the 8K ni52 has.

   This driver is for a 16K adapter.  It should work fine on the 64K
   adapters, but it will only use one of the 4 banks of RAM.  Modifying
   this for the 64K version would require a lot of heinous bank
   switching, which I'm sure not interested in doing.  If you try to
   implement a bank switching version, you'll basically have to remember
   what bank is enabled and do a switch everytime you access a memory
   location that's not current.  You'll also have to remap pointers on
   the driver side, because it only knows about 16K of the memory.
   Anyone desperate or masochistic enough to try?

   It seems to be stable now when multiple transmit buffers are used.  I
   can't see any performance difference, but then I'm working on a 386SX.

   Multicast doesn't work.  It doesn't even pretend to work.  Don't use
   it.  Don't compile your kernel with multicast support.  I don't know
   why.

   Features:
   This driver is useable as a loadable module.  If you try to specify an
   IRQ or a IO address (via insmod 3c523.o irq=xx io=0xyyy), it will
   search the MCA slots until it finds a 3c523 with the specified
   parameters.

   This driver does support multiple ethernet cards when used as a module
   (up to MAX_3C523_CARDS, the default being 4)

   This has been tested with both BNC and TP versions, internal and
   external transceivers.  Haven't tested with the 64K version (that I
   know of).

   History:
   Jan 1st, 1996
   first public release
   Feb 4th, 1996
   update to 1.3.59, incorporated multicast diffs from ni52.c
   Feb 15th, 1996
   added shared irq support
   Apr 1999
   added support for multiple cards when used as a module
   added option to disable multicast as is causes problems
       Ganesh Sittampalam <ganesh.sittampalam@magdalen.oxford.ac.uk>
       Stuart Adamson <stuart.adamson@compsoc.net>
   Nov 2001
   added support for ethtool (jgarzik)

   $Header: /fsys2/home/chrisb/linux-1.3.59-MCA/drivers/net/RCS/3c523.c,v 1.1 1996/02/05 01:53:46 chrisb Exp chrisb $
 */

#define DRV_NAME		"3c523"
#define DRV_VERSION		"17-Nov-2001"

#include <linux/init.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/ioport.h>
#include <linux/skbuff.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/mca-legacy.h>
#include <linux/ethtool.h>
#include <linux/bitops.h>
#include <linux/jiffies.h>

#include <asm/uaccess.h>
#include <asm/processor.h>
#include <asm/io.h>

#include "3c523.h"

/*************************************************************************/
#define DEBUG			/* debug on */
#define SYSBUSVAL 0		/* 1 = 8 Bit, 0 = 16 bit - 3c523 only does 16 bit */
#undef ELMC_MULTICAST		/* Disable multicast support as it is somewhat seriously broken at the moment */

#define make32(ptr16) (p->memtop + (short) (ptr16) )
#define make24(ptr32) ((char *) (ptr32) - p->base)
#define make16(ptr32) ((unsigned short) ((unsigned long) (ptr32) - (unsigned long) p->memtop ))

/*************************************************************************/
/*
   Tables to which we can map values in the configuration registers.
 */
static int irq_table[] __initdata = {
	12, 7, 3, 9
};

static int csr_table[] __initdata = {
	0x300, 0x1300, 0x2300, 0x3300
};

static int shm_table[] __initdata = {
	0x0c0000, 0x0c8000, 0x0d0000, 0x0d8000
};

/******************* how to calculate the buffers *****************************


  * IMPORTANT NOTE: if you configure only one NUM_XMIT_BUFFS, the driver works
  * --------------- in a different (more stable?) mode. Only in this mode it's
  *                 possible to configure the driver with 'NO_NOPCOMMANDS'

sizeof(scp)=12; sizeof(scb)=16; sizeof(iscp)=8;
sizeof(scp)+sizeof(iscp)+sizeof(scb) = 36 = INIT
sizeof(rfd) = 24; sizeof(rbd) = 12;
sizeof(tbd) = 8; sizeof(transmit_cmd) = 16;
sizeof(nop_cmd) = 8;

  * if you don't know the driver, better do not change this values: */

#define RECV_BUFF_SIZE 1524	/* slightly oversized */
#define XMIT_BUFF_SIZE 1524	/* slightly oversized */
#define NUM_XMIT_BUFFS 1	/* config for both, 8K and 16K shmem */
#define NUM_RECV_BUFFS_8  4	/* config for 8K shared mem */
#define NUM_RECV_BUFFS_16 9	/* config for 16K shared mem */

#if (NUM_XMIT_BUFFS == 1)
#define NO_NOPCOMMANDS		/* only possible with NUM_XMIT_BUFFS=1 */
#endif

/**************************************************************************/

#define DELAY(x) { mdelay(32 * x); }

/* a much shorter delay: */
#define DELAY_16(); { udelay(16) ; }

/* wait for command with timeout: */
#define WAIT_4_SCB_CMD() { int i; \
  for(i=0;i<1024;i++) { \
    if(!p->scb->cmd) break; \
    DELAY_16(); \
    if(i == 1023) { \
      printk(KERN_WARNING "%s:%d: scb_cmd timed out .. resetting i82586\n",\
      	dev->name,__LINE__); \
      elmc_id_reset586(); } } }

static irqreturn_t elmc_interrupt(int irq, void *dev_id);
static int elmc_open(struct net_device *dev);
static int elmc_close(struct net_device *dev);
static int elmc_send_packet(struct sk_buff *, struct net_device *);
static struct net_device_stats *elmc_get_stats(struct net_device *dev);
static void elmc_timeout(struct net_device *dev);
#ifdef ELMC_MULTICAST
static void set_multicast_list(struct net_device *dev);
#endif
static const struct ethtool_ops netdev_ethtool_ops;

/* helper-functions */
static int init586(struct net_device *dev);
static int check586(struct net_device *dev, unsigned long where, unsigned size);
static void alloc586(struct net_device *dev);
static void startrecv586(struct net_device *dev);
static void *alloc_rfa(struct net_device *dev, void *ptr);
static void elmc_rcv_int(struct net_device *dev);
static void elmc_xmt_int(struct net_device *dev);
static void elmc_rnr_int(struct net_device *dev);

struct priv {
	struct net_device_stats stats;
	unsigned long base;
	char *memtop;
	unsigned long mapped_start;		/* Start of ioremap */
	volatile struct rfd_struct *rfd_last, *rfd_top, *rfd_first;
	volatile struct scp_struct *scp;	/* volatile is important */
	volatile struct iscp_struct *iscp;	/* volatile is important */
	volatile struct scb_struct *scb;	/* volatile is important */
	volatile struct tbd_struct *xmit_buffs[NUM_XMIT_BUFFS];
#if (NUM_XMIT_BUFFS == 1)
	volatile struct transmit_cmd_struct *xmit_cmds[2];
	volatile struct nop_cmd_struct *nop_cmds[2];
#else
	volatile struct transmit_cmd_struct *xmit_cmds[NUM_XMIT_BUFFS];
	volatile struct nop_cmd_struct *nop_cmds[NUM_XMIT_BUFFS];
#endif
	volatile int nop_point, num_recv_buffs;
	volatile char *xmit_cbuffs[NUM_XMIT_BUFFS];
	volatile int xmit_count, xmit_last;
	volatile int slot;
};

#define elmc_attn586()  {elmc_do_attn586(dev->base_addr,ELMC_CTRL_INTE);}
#define elmc_reset586() {elmc_do_reset586(dev->base_addr,ELMC_CTRL_INTE);}

/* with interrupts disabled - this will clear the interrupt bit in the
   3c523 control register, and won't put it back.  This effectively
   disables interrupts on the card. */
#define elmc_id_attn586()  {elmc_do_attn586(dev->base_addr,0);}
#define elmc_id_reset586() {elmc_do_reset586(dev->base_addr,0);}

/*************************************************************************/
/*
   Do a Channel Attention on the 3c523.  This is extremely board dependent.
 */
static void elmc_do_attn586(int ioaddr, int ints)
{
	/* the 3c523 requires a minimum of 500 ns.  The delays here might be
	   a little too large, and hence they may cut the performance of the
	   card slightly.  If someone who knows a little more about Linux
	   timing would care to play with these, I'd appreciate it. */

	/* this bit masking stuff is crap.  I'd rather have separate
	   registers with strobe triggers for each of these functions.  <sigh>
	   Ya take what ya got. */

	outb(ELMC_CTRL_RST | 0x3 | ELMC_CTRL_CA | ints, ioaddr + ELMC_CTRL);
	DELAY_16();		/* > 500 ns */
	outb(ELMC_CTRL_RST | 0x3 | ints, ioaddr + ELMC_CTRL);
}

/*************************************************************************/
/*
   Reset the 82586 on the 3c523.  Also very board dependent.
 */
static void elmc_do_reset586(int ioaddr, int ints)
{
	/* toggle the RST bit low then high */
	outb(0x3 | ELMC_CTRL_LBK, ioaddr + ELMC_CTRL);
	DELAY_16();		/* > 500 ns */
	outb(ELMC_CTRL_RST | ELMC_CTRL_LBK | 0x3, ioaddr + ELMC_CTRL);

	elmc_do_attn586(ioaddr, ints);
}

/**********************************************
 * close device
 */

static int elmc_close(struct net_device *dev)
{
	netif_stop_queue(dev);
	elmc_id_reset586();	/* the hard way to stop the receiver */
	free_irq(dev->irq, dev);
	return 0;
}

/**********************************************
 * open device
 */

static int elmc_open(struct net_device *dev)
{
	int ret;

	elmc_id_attn586();	/* disable interrupts */

	ret = request_irq(dev->irq, &elmc_interrupt, IRQF_SHARED | IRQF_SAMPLE_RANDOM,
			  dev->name, dev);
	if (ret) {
		printk(KERN_ERR "%s: couldn't get irq %d\n", dev->name, dev->irq);
		elmc_id_reset586();
		return ret;
	}
	alloc586(dev);
	init586(dev);
	startrecv586(dev);
	netif_start_queue(dev);
	return 0;		/* most done by init */
}

/**********************************************
 * Check to see if there's an 82586 out there.
 */

static int __init check586(struct net_device *dev, unsigned long where, unsigned size)
{
	struct priv *p = (struct priv *) dev->priv;
	char *iscp_addrs[2];
	int i = 0;

	p->base = (unsigned long) isa_bus_to_virt((unsigned long)where) + size - 0x01000000;
	p->memtop = isa_bus_to_virt((unsigned long)where) + size;
	p->scp = (struct scp_struct *)(p->base + SCP_DEFAULT_ADDRESS);
	memset((char *) p->scp, 0, sizeof(struct scp_struct));
	p->scp->sysbus = SYSBUSVAL;	/* 1 = 8Bit-Bus, 0 = 16 Bit */

	iscp_addrs[0] = isa_bus_to_virt((unsigned long)where);
	iscp_addrs[1] = (char *) p->scp - sizeof(struct iscp_struct);

	for (i = 0; i < 2; i++) {
		p->iscp = (struct iscp_struct *) iscp_addrs[i];
		memset((char *) p->iscp, 0, sizeof(struct iscp_struct));

		p->scp->iscp = make24(p->iscp);
		p->iscp->busy = 1;

		elmc_id_reset586();

		/* reset586 does an implicit CA */

		/* apparently, you sometimes have to kick the 82586 twice... */
		elmc_id_attn586();
		DELAY(1);

		if (p->iscp->busy) {	/* i82586 clears 'busy' after successful init */
			return 0;
		}
	}
	return 1;
}

/******************************************************************
 * set iscp at the right place, called by elmc_probe and open586.
 */

void alloc586(struct net_device *dev)
{
	struct priv *p = (struct priv *) dev->priv;

	elmc_id_reset586();
	DELAY(2);

	p->scp = (struct scp_struct *) (p->base + SCP_DEFAULT_ADDRESS);
	p->scb = (struct scb_struct *) isa_bus_to_virt(dev->mem_start);
	p->iscp = (struct iscp_struct *) ((char *) p->scp - sizeof(struct iscp_struct));

	memset((char *) p->iscp, 0, sizeof(struct iscp_struct));
	memset((char *) p->scp, 0, sizeof(struct scp_struct));

	p->scp->iscp = make24(p->iscp);
	p->scp->sysbus = SYSBUSVAL;
	p->iscp->scb_offset = make16(p->scb);

	p->iscp->busy = 1;
	elmc_id_reset586();
	elmc_id_attn586();

	DELAY(2);

	if (p->iscp->busy) {
		printk(KERN_ERR "%s: Init-Problems (alloc).\n", dev->name);
	}
	memset((char *) p->scb, 0, sizeof(struct scb_struct));
}

/*****************************************************************/

static int elmc_getinfo(char *buf, int slot, void *d)
{
	int len = 0;
	struct net_device *dev = d;
	DECLARE_MAC_BUF(mac);

	if (dev == NULL)
		return len;

	len += sprintf(buf + len, "Revision: 0x%x\n",
		       inb(dev->base_addr + ELMC_REVISION) & 0xf);
	len += sprintf(buf + len, "IRQ: %d\n", dev->irq);
	len += sprintf(buf + len, "IO Address: %#lx-%#lx\n", dev->base_addr,
		       dev->base_addr + ELMC_IO_EXTENT);
	len += sprintf(buf + len, "Memory: %#lx-%#lx\n", dev->mem_start,
		       dev->mem_end - 1);
	len += sprintf(buf + len, "Transceiver: %s\n", dev->if_port ?
		       "External" : "Internal");
	len += sprintf(buf + len, "Device: %s\n", dev->name);
	len += sprintf(buf + len, "Hardware Address: %s\n",
		       print_mac(mac, dev->dev_addr));

	return len;
}				/* elmc_getinfo() */

/*****************************************************************/

static int __init do_elmc_probe(struct net_device *dev)
{
	static int slot;
	int base_addr = dev->base_addr;
	int irq = dev->irq;
	u_char status = 0;
	u_char revision = 0;
	int i = 0;
	unsigned int size = 0;
	int retval;
	struct priv *pr = dev->priv;
	DECLARE_MAC_BUF(mac);

	if (MCA_bus == 0) {
		return -ENODEV;
	}
	/* search through the slots for the 3c523. */
	slot = mca_find_adapter(ELMC_MCA_ID, 0);
	while (slot != -1) {
		status = mca_read_stored_pos(slot, 2);

		dev->irq=irq_table[(status & ELMC_STATUS_IRQ_SELECT) >> 6];
		dev->base_addr=csr_table[(status & ELMC_STATUS_CSR_SELECT) >> 1];

		/*
		   If we're trying to match a specified irq or IO address,
		   we'll reject a match unless it's what we're looking for.
		   Also reject it if the card is already in use.
		 */

		if ((irq && irq != dev->irq) ||
		    (base_addr && base_addr != dev->base_addr)) {
			slot = mca_find_adapter(ELMC_MCA_ID, slot + 1);
			continue;
		}
		if (!request_region(dev->base_addr, ELMC_IO_EXTENT, DRV_NAME)) {
			slot = mca_find_adapter(ELMC_MCA_ID, slot + 1);
			continue;
		}

		/* found what we're looking for... */
		break;
	}

	/* we didn't find any 3c523 in the slots we checked for */
	if (slot == MCA_NOTFOUND)
		return ((base_addr || irq) ? -ENXIO : -ENODEV);

	mca_set_adapter_name(slot, "3Com 3c523 Etherlink/MC");
	mca_set_adapter_procfn(slot, (MCA_ProcFn) elmc_getinfo, dev);

	/* if we get this far, adapter has been found - carry on */
	printk(KERN_INFO "%s: 3c523 adapter found in slot %d\n", dev->name, slot + 1);

	/* Now we extract configuration info from the card.
	   The 3c523 provides information in two of the POS registers, but
	   the second one is only needed if we want to tell the card what IRQ
	   to use.  I suspect that whoever sets the thing up initially would
	   prefer we don't screw with those things.

	   Note that we read the status info when we found the card...

	   See 3c523.h for more details.
	 */

	/* revision is stored in the first 4 bits of the revision register */
	revision = inb(dev->base_addr + ELMC_REVISION) & 0xf;

	/* according to docs, we read the interrupt and write it back to
	   the IRQ select register, since the POST might not configure the IRQ
	   properly. */
	switch (dev->irq) {
	case 3:
		mca_write_pos(slot, 3, 0x04);
		break;
	case 7:
		mca_write_pos(slot, 3, 0x02);
		break;
	case 9:
		mca_write_pos(slot, 3, 0x08);
		break;
	case 12:
		mca_write_pos(slot, 3, 0x01);
		break;
	}

	memset(pr, 0, sizeof(struct priv));
	pr->slot = slot;

	printk(KERN_INFO "%s: 3Com 3c523 Rev 0x%x at %#lx\n", dev->name, (int) revision,
	       dev->base_addr);

	/* Determine if we're using the on-board transceiver (i.e. coax) or
	   an external one.  The information is pretty much useless, but I
	   guess it's worth brownie points. */
	dev->if_port = (status & ELMC_STATUS_DISABLE_THIN);

	/* The 3c523 has a 24K chunk of memory.  The first 16K is the
	   shared memory, while the last 8K is for the EtherStart BIOS ROM.
	   Which we don't care much about here.  We'll just tell Linux that
	   we're using 16K.  MCA won't permit address space conflicts caused
	   by not mapping the other 8K. */
	dev->mem_start = shm_table[(status & ELMC_STATUS_MEMORY_SELECT) >> 3];

	/* We're using MCA, so it's a given that the information about memory
	   size is correct.  The Crynwr drivers do something like this. */

	elmc_id_reset586();	/* seems like a good idea before checking it... */

	size = 0x4000;		/* check for 16K mem */
	if (!check586(dev, dev->mem_start, size)) {
		printk(KERN_ERR "%s: memprobe, Can't find memory at 0x%lx!\n", dev->name,
		       dev->mem_start);
		retval = -ENODEV;
		goto err_out;
	}
	dev->mem_end = dev->mem_start + size;	/* set mem_end showed by 'ifconfig' */

	pr->memtop = isa_bus_to_virt(dev->mem_start) + size;
	pr->base = (unsigned long) isa_bus_to_virt(dev->mem_start) + size - 0x01000000;
	alloc586(dev);

	elmc_id_reset586();	/* make sure it doesn't generate spurious ints */

	/* set number of receive-buffs according to memsize */
	pr->num_recv_buffs = NUM_RECV_BUFFS_16;

	/* dump all the assorted information */
	printk(KERN_INFO "%s: IRQ %d, %sternal xcvr, memory %#lx-%#lx.\n", dev->name,
	       dev->irq, dev->if_port ? "ex" : "in",
	       dev->mem_start, dev->mem_end - 1);

	/* The hardware address for the 3c523 is stored in the first six
	   bytes of the IO address. */
	for (i = 0; i < 6; i++)
		dev->dev_addr[i] = inb(dev->base_addr + i);

	printk(KERN_INFO "%s: hardware address %s\n",
	       dev->name, print_mac(mac, dev->dev_addr));

	dev->open = &elmc_open;
	dev->stop = &elmc_close;
	dev->get_stats = &elmc_get_stats;
	dev->hard_start_xmit = &elmc_send_packet;
	dev->tx_timeout = &elmc_timeout;
	dev->watchdog_timeo = HZ;
#ifdef ELMC_MULTICAST
	dev->set_multicast_list = &set_multicast_list;
#else
	dev->set_multicast_list = NULL;
#endif
	dev->ethtool_ops = &netdev_ethtool_ops;

	/* note that we haven't actually requested the IRQ from the kernel.
	   That gets done in elmc_open().  I'm not sure that's such a good idea,
	   but it works, so I'll go with it. */

#ifndef ELMC_MULTICAST
        dev->flags&=~IFF_MULTICAST;     /* Multicast doesn't work */
#endif

	retval = register_netdev(dev);
	if (retval)
		goto err_out;

	return 0;
err_out:
	mca_set_adapter_procfn(slot, NULL, NULL);
	release_region(dev->base_addr, ELMC_IO_EXTENT);
	return retval;
}

static void cleanup_card(struct net_device *dev)
{
	mca_set_adapter_procfn(((struct priv *) (dev->priv))->slot, NULL, NULL);
	release_region(dev->base_addr, ELMC_IO_EXTENT);
}

#ifndef MODULE
struct net_device * __init elmc_probe(int unit)
{
	struct net_device *dev = alloc_etherdev(sizeof(struct priv));
	int err;

	if (!dev)
		return ERR_PTR(-ENOMEM);

	sprintf(dev->name, "eth%d", unit);
	netdev_boot_setup_check(dev);

	err = do_elmc_probe(dev);
	if (err)
		goto out;
	return dev;
out:
	free_netdev(dev);
	return ERR_PTR(err);
}
#endif

/**********************************************
 * init the chip (elmc-interrupt should be disabled?!)
 * needs a correct 'allocated' memory
 */

static int init586(struct net_device *dev)
{
	void *ptr;
	unsigned long s;
	int i, result = 0;
	struct priv *p = (struct priv *) dev->priv;
	volatile struct configure_cmd_struct *cfg_cmd;
	volatile struct iasetup_cmd_struct *ias_cmd;
	volatile struct tdr_cmd_struct *tdr_cmd;
	volatile struct mcsetup_cmd_struct *mc_cmd;
	struct dev_mc_list *dmi = dev->mc_list;
	int num_addrs = dev->mc_count;

	ptr = (void *) ((char *) p->scb + sizeof(struct scb_struct));

	cfg_cmd = (struct configure_cmd_struct *) ptr;	/* configure-command */
	cfg_cmd->cmd_status = 0;
	cfg_cmd->cmd_cmd = CMD_CONFIGURE | CMD_LAST;
	cfg_cmd->cmd_link = 0xffff;

	cfg_cmd->byte_cnt = 0x0a;	/* number of cfg bytes */
	cfg_cmd->fifo = 0x08;	/* fifo-limit (8=tx:32/rx:64) */
	cfg_cmd->sav_bf = 0x40;	/* hold or discard bad recv frames (bit 7) */
	cfg_cmd->adr_len = 0x2e;	/* addr_len |!src_insert |pre-len |loopback */
	cfg_cmd->priority = 0x00;
	cfg_cmd->ifs = 0x60;
	cfg_cmd->time_low = 0x00;
	cfg_cmd->time_high = 0xf2;
	cfg_cmd->promisc = 0;
	if (dev->flags & (IFF_ALLMULTI | IFF_PROMISC)) {
		cfg_cmd->promisc = 1;
		dev->flags |= IFF_PROMISC;
	}
	cfg_cmd->carr_coll = 0x00;

	p->scb->cbl_offset = make16(cfg_cmd);

	p->scb->cmd = CUC_START;	/* cmd.-unit start */
	elmc_id_attn586();

	s = jiffies;		/* warning: only active with interrupts on !! */
	while (!(cfg_cmd->cmd_status & STAT_COMPL)) {
		if (time_after(jiffies, s + 30*HZ/100))
			break;
	}

	if ((cfg_cmd->cmd_status & (STAT_OK | STAT_COMPL)) != (STAT_COMPL | STAT_OK)) {
		printk(KERN_WARNING "%s (elmc): configure command failed: %x\n", dev->name, cfg_cmd->cmd_status);
		return 1;
	}
	/*
	 * individual address setup
	 */
	ias_cmd = (struct iasetup_cmd_struct *) ptr;

	ias_cmd->cmd_status = 0;
	ias_cmd->cmd_cmd = CMD_IASETUP | CMD_LAST;
	ias_cmd->cmd_link = 0xffff;

	memcpy((char *) &ias_cmd->iaddr, (char *) dev->dev_addr, ETH_ALEN);

	p->scb->cbl_offset = make16(ias_cmd);

	p->scb->cmd = CUC_START;	/* cmd.-unit start */
	elmc_id_attn586();

	s = jiffies;
	while (!(ias_cmd->cmd_status & STAT_COMPL)) {
		if (time_after(jiffies, s + 30*HZ/100))
			break;
	}

	if ((ias_cmd->cmd_status & (STAT_OK | STAT_COMPL)) != (STAT_OK | STAT_COMPL)) {
		printk(KERN_WARNING "%s (elmc): individual address setup command failed: %04x\n", dev->name, ias_cmd->cmd_status);
		return 1;
	}
	/*
	 * TDR, wire check .. e.g. no resistor e.t.c
	 */
	tdr_cmd = (struct tdr_cmd_struct *) ptr;

	tdr_cmd->cmd_status = 0;
	tdr_cmd->cmd_cmd = CMD_TDR | CMD_LAST;
	tdr_cmd->cmd_link = 0xffff;
	tdr_cmd->status = 0;

	p->scb->cbl_offset = make16(tdr_cmd);

	p->scb->cmd = CUC_START;	/* cmd.-unit start */
	elmc_attn586();

	s = jiffies;
	while (!(tdr_cmd->cmd_status & STAT_COMPL)) {
		if (time_after(jiffies, s + 30*HZ/100)) {
			printk(KERN_WARNING "%s: %d Problems while running the TDR.\n", dev->name, __LINE__);
			result = 1;
			break;
		}
	}

	if (!result) {
		DELAY(2);	/* wait for result */
		result = tdr_cmd->status;

		p->scb->cmd = p->scb->status & STAT_MASK;
		elmc_id_attn586();	/* ack the interrupts */

		if (result & TDR_LNK_OK) {
			/* empty */
		} else if (result & TDR_XCVR_PRB) {
			printk(KERN_WARNING "%s: TDR: Transceiver problem!\n", dev->name);
		} else if (result & TDR_ET_OPN) {
			printk(KERN_WARNING "%s: TDR: No correct termination %d clocks away.\n", dev->name, result & TDR_TIMEMASK);
		} else if (result & TDR_ET_SRT) {
			if (result & TDR_TIMEMASK)	/* time == 0 -> strange :-) */
				printk(KERN_WARNING "%s: TDR: Detected a short circuit %d clocks away.\n", dev->name, result & TDR_TIMEMASK);
		} else {
			printk(KERN_WARNING "%s: TDR: Unknown status %04x\n", dev->name, result);
		}
	}
	/*
	 * ack interrupts
	 */
	p->scb->cmd = p->scb->status & STAT_MASK;
	elmc_id_attn586();

	/*
	 * alloc nop/xmit-cmds
	 */
#if (NUM_XMIT_BUFFS == 1)
	for (i = 0; i < 2; i++) {
		p->nop_cmds[i] = (struct nop_cmd_struct *) ptr;
		p->nop_cmds[i]->cmd_cmd = CMD_NOP;
		p->nop_cmds[i]->cmd_status = 0;
		p->nop_cmds[i]->cmd_link = make16((p->nop_cmds[i]));
		ptr = (char *) ptr + sizeof(struct nop_cmd_struct);
	}
	p->xmit_cmds[0] = (struct transmit_cmd_struct *) ptr;	/* transmit cmd/buff 0 */
	ptr = (char *) ptr + sizeof(struct transmit_cmd_struct);
#else
	for (i = 0; i < NUM_XMIT_BUFFS; i++) {
		p->nop_cmds[i] = (struct nop_cmd_struct *) ptr;
		p->nop_cmds[i]->cmd_cmd = CMD_NOP;
		p->nop_cmds[i]->cmd_status = 0;
		p->nop_cmds[i]->cmd_link = make16((p->nop_cmds[i]));
		ptr = (char *) ptr + sizeof(struct nop_cmd_struct);
		p->xmit_cmds[i] = (struct transmit_cmd_struct *) ptr;	/*transmit cmd/buff 0 */
		ptr = (char *) ptr + sizeof(struct transmit_cmd_struct);
	}
#endif

	ptr = alloc_rfa(dev, (void *) ptr);	/* init receive-frame-area */

	/*
	 * Multicast setup
	 */

	if (dev->mc_count) {
		/* I don't understand this: do we really need memory after the init? */
		int len = ((char *) p->iscp - (char *) ptr - 8) / 6;
		if (len <= 0) {
			printk(KERN_ERR "%s: Ooooops, no memory for MC-Setup!\n", dev->name);
		} else {
			if (len < num_addrs) {
				num_addrs = len;
				printk(KERN_WARNING "%s: Sorry, can only apply %d MC-Address(es).\n",
				       dev->name, num_addrs);
			}
			mc_cmd = (struct mcsetup_cmd_struct *) ptr;
			mc_cmd->cmd_status = 0;
			mc_cmd->cmd_cmd = CMD_MCSETUP | CMD_LAST;
			mc_cmd->cmd_link = 0xffff;
			mc_cmd->mc_cnt = num_addrs * 6;
			for (i = 0; i < num_addrs; i++) {
				memcpy((char *) mc_cmd->mc_list[i], dmi->dmi_addr, 6);
				dmi = dmi->next;
			}
			p->scb->cbl_offset = make16(mc_cmd);
			p->scb->cmd = CUC_START;
			elmc_id_attn586();
			s = jiffies;
			while (!(mc_cmd->cmd_status & STAT_COMPL)) {
				if (time_after(jiffies, s + 30*HZ/100))
					break;
			}
			if (!(mc_cmd->cmd_status & STAT_COMPL)) {
				printk(KERN_WARNING "%s: Can't apply multicast-address-list.\n", dev->name);
			}
		}
	}
	/*
	 * alloc xmit-buffs / init xmit_cmds
	 */
	for (i = 0; i < NUM_XMIT_BUFFS; i++) {
		p->xmit_cbuffs[i] = (char *) ptr;	/* char-buffs */
		ptr = (char *) ptr + XMIT_BUFF_SIZE;
		p->xmit_buffs[i] = (struct tbd_struct *) ptr;	/* TBD */
		ptr = (char *) ptr + sizeof(struct tbd_struct);
		if ((void *) ptr > (void *) p->iscp) {
			printk(KERN_ERR "%s: not enough shared-mem for your configuration!\n", dev->name);
			return 1;
		}
		memset((char *) (p->xmit_cmds[i]), 0, sizeof(struct transmit_cmd_struct));
		memset((char *) (p->xmit_buffs[i]), 0, sizeof(struct tbd_struct));
		p->xmit_cmds[i]->cmd_status = STAT_COMPL;
		p->xmit_cmds[i]->cmd_cmd = CMD_XMIT | CMD_INT;
		p->xmit_cmds[i]->tbd_offset = make16((p->xmit_buffs[i]));
		p->xmit_buffs[i]->next = 0xffff;
		p->xmit_buffs[i]->buffer = make24((p->xmit_cbuffs[i]));
	}

	p->xmit_count = 0;
	p->xmit_last = 0;
#ifndef NO_NOPCOMMANDS
	p->nop_point = 0;
#endif

	/*
	 * 'start transmitter' (nop-loop)
	 */
#ifndef NO_NOPCOMMANDS
	p->scb->cbl_offset = make16(p->nop_cmds[0]);
	p->scb->cmd = CUC_START;
	elmc_id_attn586();
	WAIT_4_SCB_CMD();
#else
	p->xmit_cmds[0]->cmd_link = 0xffff;
	p->xmit_cmds[0]->cmd_cmd = CMD_XMIT | CMD_LAST | CMD_INT;
#endif

	return 0;
}

/******************************************************
 * This is a helper routine for elmc_rnr_int() and init586().
 * It sets up the Receive Frame Area (RFA).
 */

static void *alloc_rfa(struct net_device *dev, void *ptr)
{
	volatile struct rfd_struct *rfd = (struct rfd_struct *) ptr;
	volatile struct rbd_struct *rbd;
	int i;
	struct priv *p = (struct priv *) dev->priv;

	memset((char *) rfd, 0, sizeof(struct rfd_struct) * p->num_recv_buffs);
	p->rfd_first = rfd;

	for (i = 0; i < p->num_recv_buffs; i++) {
		rfd[i].next = make16(rfd + (i + 1) % p->num_recv_buffs);
	}
	rfd[p->num_recv_buffs - 1].last = RFD_SUSP;	/* RU suspend */

	ptr = (void *) (rfd + p->num_recv_buffs);

	rbd = (struct rbd_struct *) ptr;
	ptr = (void *) (rbd + p->num_recv_buffs);

	/* clr descriptors */
	memset((char *) rbd, 0, sizeof(struct rbd_struct) * p->num_recv_buffs);

	for (i = 0; i < p->num_recv_buffs; i++) {
		rbd[i].next = make16((rbd + (i + 1) % p->num_recv_buffs));
		rbd[i].size = RECV_BUFF_SIZE;
		rbd[i].buffer = make24(ptr);
		ptr = (char *) ptr + RECV_BUFF_SIZE;
	}

	p->rfd_top = p->rfd_first;
	p->rfd_last = p->rfd_first + p->num_recv_buffs - 1;

	p->scb->rfa_offset = make16(p->rfd_first);
	p->rfd_first->rbd_offset = make16(rbd);

	return ptr;
}


/**************************************************
 * Interrupt Handler ...
 */

static irqreturn_t
elmc_interrupt(int irq, void *dev_id)
{
	struct net_device *dev = dev_id;
	unsigned short stat;
	struct priv *p;

	if (!netif_running(dev)) {
		/* The 3c523 has this habit of generating interrupts during the
		   reset.  I'm not sure if the ni52 has this same problem, but it's
		   really annoying if we haven't finished initializing it.  I was
		   hoping all the elmc_id_* commands would disable this, but I
		   might have missed a few. */

		elmc_id_attn586();	/* ack inter. and disable any more */
		return IRQ_HANDLED;
	} else if (!(ELMC_CTRL_INT & inb(dev->base_addr + ELMC_CTRL))) {
		/* wasn't this device */
		return IRQ_NONE;
	}
	/* reading ELMC_CTRL also clears the INT bit. */

	p = (struct priv *) dev->priv;

	while ((stat = p->scb->status & STAT_MASK))
	{
		p->scb->cmd = stat;
		elmc_attn586();	/* ack inter. */

		if (stat & STAT_CX) {
			/* command with I-bit set complete */
			elmc_xmt_int(dev);
		}
		if (stat & STAT_FR) {
			/* received a frame */
			elmc_rcv_int(dev);
		}
#ifndef NO_NOPCOMMANDS
		if (stat & STAT_CNA) {
			/* CU went 'not ready' */
			if (netif_running(dev)) {
				printk(KERN_WARNING "%s: oops! CU has left active state. stat: %04x/%04x.\n", dev->name, (int) stat, (int) p->scb->status);
			}
		}
#endif

		if (stat & STAT_RNR) {
			/* RU went 'not ready' */

			if (p->scb->status & RU_SUSPEND) {
				/* special case: RU_SUSPEND */

				WAIT_4_SCB_CMD();
				p->scb->cmd = RUC_RESUME;
				elmc_attn586();
			} else {
				printk(KERN_WARNING "%s: Receiver-Unit went 'NOT READY': %04x/%04x.\n", dev->name, (int) stat, (int) p->scb->status);
				elmc_rnr_int(dev);
			}
		}
		WAIT_4_SCB_CMD();	/* wait for ack. (elmc_xmt_int can be faster than ack!!) */
		if (p->scb->cmd) {	/* timed out? */
			break;
		}
	}
	return IRQ_HANDLED;
}

/*******************************************************
 * receive-interrupt
 */

static void elmc_rcv_int(struct net_device *dev)
{
	int status;
	unsigned short totlen;
	struct sk_buff *skb;
	struct rbd_struct *rbd;
	struct priv *p = (struct priv *) dev->priv;

	for (; (status = p->rfd_top->status) & STAT_COMPL;) {
		rbd = (struct rbd_struct *) make32(p->rfd_top->rbd_offset);

		if (status & STAT_OK) {		/* frame received without error? */
			if ((totlen = rbd->status) & RBD_LAST) {	/* the first and the last buffer? */
				totlen &= RBD_MASK;	/* length of this frame */
				rbd->status = 0;
				skb = (struct sk_buff *) dev_alloc_skb(totlen + 2);
				if (skb != NULL) {
					skb_reserve(skb, 2);	/* 16 byte alignment */
					skb_put(skb,totlen);
					skb_copy_to_linear_data(skb, (char *) p->base+(unsigned long) rbd->buffer,totlen);
					skb->protocol = eth_type_trans(skb, dev);
					netif_rx(skb);
					dev->last_rx = jiffies;
					p->stats.rx_packets++;
					p->stats.rx_bytes += totlen;
				} else {
					p->stats.rx_dropped++;
				}
			} else {
				printk(KERN_WARNING "%s: received oversized frame.\n", dev->name);
				p->stats.rx_dropped++;
			}
		} else {	/* frame !(ok), only with 'save-bad-frames' */
			printk(KERN_WARNING "%s: oops! rfd-error-status: %04x\n", dev->name, status);
			p->stats.rx_errors++;
		}
		p->rfd_top->status = 0;
		p->rfd_top->last = RFD_SUSP;
		p->rfd_last->last = 0;	/* delete RU_SUSP  */
		p->rfd_last = p->rfd_top;
		p->rfd_top = (struct rfd_struct *) make32(p->rfd_top->next);	/* step to next RFD */
	}
}

/**********************************************************
 * handle 'Receiver went not ready'.
 */

static void elmc_rnr_int(struct net_device *dev)
{
	struct priv *p = (struct priv *) dev->priv;

	p->stats.rx_errors++;

	WAIT_4_SCB_CMD();	/* wait for the last cmd */
	p->scb->cmd = RUC_ABORT;	/* usually the RU is in the 'no resource'-state .. abort it now. */
	elmc_attn586();
	WAIT_4_SCB_CMD();	/* wait for accept cmd. */

	alloc_rfa(dev, (char *) p->rfd_first);
	startrecv586(dev);	/* restart RU */

	printk(KERN_WARNING "%s: Receive-Unit restarted. Status: %04x\n", dev->name, p->scb->status);

}

/**********************************************************
 * handle xmit - interrupt
 */

static void elmc_xmt_int(struct net_device *dev)
{
	int status;
	struct priv *p = (struct priv *) dev->priv;

	status = p->xmit_cmds[p->xmit_last]->cmd_status;
	if (!(status & STAT_COMPL)) {
		printk(KERN_WARNING "%s: strange .. xmit-int without a 'COMPLETE'\n", dev->name);
	}
	if (status & STAT_OK) {
		p->stats.tx_packets++;
		p->stats.collisions += (status & TCMD_MAXCOLLMASK);
	} else {
		p->stats.tx_errors++;
		if (status & TCMD_LATECOLL) {
			printk(KERN_WARNING "%s: late collision detected.\n", dev->name);
			p->stats.collisions++;
		} else if (status & TCMD_NOCARRIER) {
			p->stats.tx_carrier_errors++;
			printk(KERN_WARNING "%s: no carrier detected.\n", dev->name);
		} else if (status & TCMD_LOSTCTS) {
			printk(KERN_WARNING "%s: loss of CTS detected.\n", dev->name);
		} else if (status & TCMD_UNDERRUN) {
			p->stats.tx_fifo_errors++;
			printk(KERN_WARNING "%s: DMA underrun detected.\n", dev->name);
		} else if (status & TCMD_MAXCOLL) {
			printk(KERN_WARNING "%s: Max. collisions exceeded.\n", dev->name);
			p->stats.collisions += 16;
		}
	}

#if (NUM_XMIT_BUFFS != 1)
	if ((++p->xmit_last) == NUM_XMIT_BUFFS) {
		p->xmit_last = 0;
	}
#endif

	netif_wake_queue(dev);
}

/***********************************************************
 * (re)start the receiver
 */

static void startrecv586(struct net_device *dev)
{
	struct priv *p = (struct priv *) dev->priv;

	p->scb->rfa_offset = make16(p->rfd_first);
	p->scb->cmd = RUC_START;
	elmc_attn586();		/* start cmd. */
	WAIT_4_SCB_CMD();	/* wait for accept cmd. (no timeout!!) */
}

/******************************************************
 * timeout
 */

static void elmc_timeout(struct net_device *dev)
{
	struct priv *p = (struct priv *) dev->priv;
	/* COMMAND-UNIT active? */
	if (p->scb->status & CU_ACTIVE) {
#ifdef DEBUG
		printk("%s: strange ... timeout with CU active?!?\n", dev->name);
		printk("%s: X0: %04x N0: %04x N1: %04x %d\n", dev->name, (int) p->xmit_cmds[0]->cmd_status, (int) p->nop_cmds[0]->cmd_status, (int) p->nop_cmds[1]->cmd_status, (int) p->nop_point);
#endif
		p->scb->cmd = CUC_ABORT;
		elmc_attn586();
		WAIT_4_SCB_CMD();
		p->scb->cbl_offset = make16(p->nop_cmds[p->nop_point]);
		p->scb->cmd = CUC_START;
		elmc_attn586();
		WAIT_4_SCB_CMD();
		netif_wake_queue(dev);
	} else {
#ifdef DEBUG
		printk("%s: xmitter timed out, try to restart! stat: %04x\n", dev->name, p->scb->status);
		printk("%s: command-stats: %04x %04x\n", dev->name, p->xmit_cmds[0]->cmd_status, p->xmit_cmds[1]->cmd_status);
#endif
		elmc_close(dev);
		elmc_open(dev);
	}
}

/******************************************************
 * send frame
 */

static int elmc_send_packet(struct sk_buff *skb, struct net_device *dev)
{
	int len;
	int i;
#ifndef NO_NOPCOMMANDS
	int next_nop;
#endif
	struct priv *p = (struct priv *) dev->priv;

	netif_stop_queue(dev);

	len = (ETH_ZLEN < skb->len) ? skb->len : ETH_ZLEN;

	if (len != skb->len)
		memset((char *) p->xmit_cbuffs[p->xmit_count], 0, ETH_ZLEN);
	skb_copy_from_linear_data(skb, (char *) p->xmit_cbuffs[p->xmit_count], skb->len);

#if (NUM_XMIT_BUFFS == 1)
#ifdef NO_NOPCOMMANDS
	p->xmit_buffs[0]->size = TBD_LAST | len;
	for (i = 0; i < 16; i++) {
		p->scb->cbl_offset = make16(p->xmit_cmds[0]);
		p->scb->cmd = CUC_START;
		p->xmit_cmds[0]->cmd_status = 0;
			elmc_attn586();
		dev->trans_start = jiffies;
		if (!i) {
			dev_kfree_skb(skb);
		}
		WAIT_4_SCB_CMD();
		if ((p->scb->status & CU_ACTIVE)) {	/* test it, because CU sometimes doesn't start immediately */
			break;
		}
		if (p->xmit_cmds[0]->cmd_status) {
			break;
		}
		if (i == 15) {
			printk(KERN_WARNING "%s: Can't start transmit-command.\n", dev->name);
		}
	}
#else
	next_nop = (p->nop_point + 1) & 0x1;
	p->xmit_buffs[0]->size = TBD_LAST | len;

	p->xmit_cmds[0]->cmd_link = p->nop_cmds[next_nop]->cmd_link
	    = make16((p->nop_cmds[next_nop]));
	p->xmit_cmds[0]->cmd_status = p->nop_cmds[next_nop]->cmd_status = 0;

	p->nop_cmds[p->nop_point]->cmd_link = make16((p->xmit_cmds[0]));
	dev->trans_start = jiffies;
	p->nop_point = next_nop;
	dev_kfree_skb(skb);
#endif
#else
	p->xmit_buffs[p->xmit_count]->size = TBD_LAST | len;
	if ((next_nop = p->xmit_count + 1) == NUM_XMIT_BUFFS) {
		next_nop = 0;
	}
	p->xmit_cmds[p->xmit_count]->cmd_status = 0;
	p->xmit_cmds[p->xmit_count]->cmd_link = p->nop_cmds[next_nop]->cmd_link
	    = make16((p->nop_cmds[next_nop]));
	p->nop_cmds[next_nop]->cmd_status = 0;
		p->nop_cmds[p->xmit_count]->cmd_link = make16((p->xmit_cmds[p->xmit_count]));
	dev->trans_start = jiffies;
	p->xmit_count = next_nop;
	if (p->xmit_count != p->xmit_last)
		netif_wake_queue(dev);
	dev_kfree_skb(skb);
#endif
	return 0;
}

/*******************************************
 * Someone wanna have the statistics
 */

static struct net_device_stats *elmc_get_stats(struct net_device *dev)
{
	struct priv *p = (struct priv *) dev->priv;
	unsigned short crc, aln, rsc, ovrn;

	crc = p->scb->crc_errs;	/* get error-statistic from the ni82586 */
	p->scb->crc_errs -= crc;
	aln = p->scb->aln_errs;
	p->scb->aln_errs -= aln;
	rsc = p->scb->rsc_errs;
	p->scb->rsc_errs -= rsc;
	ovrn = p->scb->ovrn_errs;
	p->scb->ovrn_errs -= ovrn;

	p->stats.rx_crc_errors += crc;
	p->stats.rx_fifo_errors += ovrn;
	p->stats.rx_frame_errors += aln;
	p->stats.rx_dropped += rsc;

	return &p->stats;
}

/********************************************************
 * Set MC list ..
 */

#ifdef ELMC_MULTICAST
static void set_multicast_list(struct net_device *dev)
{
	if (!dev->start) {
		/* without a running interface, promiscuous doesn't work */
		return;
	}
	dev->start = 0;
	alloc586(dev);
	init586(dev);
	startrecv586(dev);
	dev->start = 1;
}
#endif

static void netdev_get_drvinfo(struct net_device *dev,
			       struct ethtool_drvinfo *info)
{
	strcpy(info->driver, DRV_NAME);
	strcpy(info->version, DRV_VERSION);
	sprintf(info->bus_info, "MCA 0x%lx", dev->base_addr);
}

static const struct ethtool_ops netdev_ethtool_ops = {
	.get_drvinfo		= netdev_get_drvinfo,
};

#ifdef MODULE

/* Increase if needed ;) */
#define MAX_3C523_CARDS 4

static struct net_device *dev_elmc[MAX_3C523_CARDS];
static int irq[MAX_3C523_CARDS];
static int io[MAX_3C523_CARDS];
module_param_array(irq, int, NULL, 0);
module_param_array(io, int, NULL, 0);
MODULE_PARM_DESC(io, "EtherLink/MC I/O base address(es)");
MODULE_PARM_DESC(irq, "EtherLink/MC IRQ number(s)");
MODULE_LICENSE("GPL");

int __init init_module(void)
{
	int this_dev,found = 0;

	/* Loop until we either can't find any more cards, or we have MAX_3C523_CARDS */
	for(this_dev=0; this_dev<MAX_3C523_CARDS; this_dev++) {
		struct net_device *dev = alloc_etherdev(sizeof(struct priv));
		if (!dev)
			break;
		dev->irq=irq[this_dev];
		dev->base_addr=io[this_dev];
		if (do_elmc_probe(dev) == 0) {
			dev_elmc[this_dev] = dev;
			found++;
			continue;
		}
		free_netdev(dev);
		if (io[this_dev]==0)
			break;
		printk(KERN_WARNING "3c523.c: No 3c523 card found at io=%#x\n",io[this_dev]);
	}

	if(found==0) {
		if(io[0]==0) printk(KERN_NOTICE "3c523.c: No 3c523 cards found\n");
		return -ENXIO;
	} else return 0;
}

void __exit cleanup_module(void)
{
	int this_dev;
	for (this_dev=0; this_dev<MAX_3C523_CARDS; this_dev++) {
		struct net_device *dev = dev_elmc[this_dev];
		if (dev) {
			unregister_netdev(dev);
			cleanup_card(dev);
			free_netdev(dev);
		}
	}
}

#endif				/* MODULE */
