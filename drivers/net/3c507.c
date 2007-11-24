/* 3c507.c: An EtherLink16 device driver for Linux. */
/*
	Written 1993,1994 by Donald Becker.

	Copyright 1993 United States Government as represented by the
	Director, National Security Agency.

	This software may be used and distributed according to the terms
	of the GNU General Public License, incorporated herein by reference.

	The author may be reached as becker@scyld.com, or C/O
	Scyld Computing Corporation
	410 Severn Ave., Suite 210
	Annapolis MD 21403


	Thanks go to jennings@Montrouge.SMR.slb.com ( Patrick Jennings)
	and jrs@world.std.com (Rick Sladkey) for testing and bugfixes.
	Mark Salazar <leslie@access.digex.net> made the changes for cards with
	only 16K packet buffers.

	Things remaining to do:
	Verify that the tx and rx buffers don't have fencepost errors.
	Move the theory of operation and memory map documentation.
	The statistics need to be updated correctly.
*/

#define DRV_NAME		"3c507"
#define DRV_VERSION		"1.10a"
#define DRV_RELDATE		"11/17/2001"

static const char version[] =
	DRV_NAME ".c:v" DRV_VERSION " " DRV_RELDATE " Donald Becker (becker@scyld.com)\n";

/*
  Sources:
	This driver wouldn't have been written with the availability of the
	Crynwr driver source code.	It provided a known-working implementation
	that filled in the gaping holes of the Intel documentation.  Three cheers
	for Russ Nelson.

	Intel Microcommunications Databook, Vol. 1, 1990.  It provides just enough
	info that the casual reader might think that it documents the i82586 :-<.
*/

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/fcntl.h>
#include <linux/interrupt.h>
#include <linux/ioport.h>
#include <linux/in.h>
#include <linux/string.h>
#include <linux/spinlock.h>
#include <linux/ethtool.h>
#include <linux/errno.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/bitops.h>

#include <asm/dma.h>
#include <asm/io.h>
#include <asm/system.h>
#include <asm/uaccess.h>

/* use 0 for production, 1 for verification, 2..7 for debug */
#ifndef NET_DEBUG
#define NET_DEBUG 1
#endif
static unsigned int net_debug = NET_DEBUG;
#define debug net_debug


/*
  			Details of the i82586.

   You'll really need the databook to understand the details of this part,
   but the outline is that the i82586 has two separate processing units.
   Both are started from a list of three configuration tables, of which only
   the last, the System Control Block (SCB), is used after reset-time.  The SCB
   has the following fields:
		Status word
		Command word
		Tx/Command block addr.
		Rx block addr.
   The command word accepts the following controls for the Tx and Rx units:
  */

#define	 CUC_START	 0x0100
#define	 CUC_RESUME	 0x0200
#define	 CUC_SUSPEND 0x0300
#define	 RX_START	 0x0010
#define	 RX_RESUME	 0x0020
#define	 RX_SUSPEND	 0x0030

/* The Rx unit uses a list of frame descriptors and a list of data buffer
   descriptors.  We use full-sized (1518 byte) data buffers, so there is
   a one-to-one pairing of frame descriptors to buffer descriptors.

   The Tx ("command") unit executes a list of commands that look like:
		Status word		Written by the 82586 when the command is done.
		Command word	Command in lower 3 bits, post-command action in upper 3
		Link word		The address of the next command.
		Parameters		(as needed).

	Some definitions related to the Command Word are:
 */
#define CMD_EOL		0x8000			/* The last command of the list, stop. */
#define CMD_SUSP	0x4000			/* Suspend after doing cmd. */
#define CMD_INTR	0x2000			/* Interrupt after doing cmd. */

enum commands {
	CmdNOp = 0, CmdSASetup = 1, CmdConfigure = 2, CmdMulticastList = 3,
	CmdTx = 4, CmdTDR = 5, CmdDump = 6, CmdDiagnose = 7};

/* Information that need to be kept for each board. */
struct net_local {
	int last_restart;
	ushort rx_head;
	ushort rx_tail;
	ushort tx_head;
	ushort tx_cmd_link;
	ushort tx_reap;
	ushort tx_pkts_in_ring;
	spinlock_t lock;
	void __iomem *base;
};

/*
  		Details of the EtherLink16 Implementation
  The 3c507 is a generic shared-memory i82586 implementation.
  The host can map 16K, 32K, 48K, or 64K of the 64K memory into
  0x0[CD][08]0000, or all 64K into 0xF[02468]0000.
  */

/* Offsets from the base I/O address. */
#define	SA_DATA		0	/* Station address data, or 3Com signature. */
#define MISC_CTRL	6	/* Switch the SA_DATA banks, and bus config bits. */
#define RESET_IRQ	10	/* Reset the latched IRQ line. */
#define SIGNAL_CA	11	/* Frob the 82586 Channel Attention line. */
#define ROM_CONFIG	13
#define MEM_CONFIG	14
#define IRQ_CONFIG	15
#define EL16_IO_EXTENT 16

/* The ID port is used at boot-time to locate the ethercard. */
#define ID_PORT		0x100

/* Offsets to registers in the mailbox (SCB). */
#define iSCB_STATUS	0x8
#define iSCB_CMD		0xA
#define iSCB_CBL		0xC	/* Command BLock offset. */
#define iSCB_RFA		0xE	/* Rx Frame Area offset. */

/*  Since the 3c507 maps the shared memory window so that the last byte is
	at 82586 address FFFF, the first byte is at 82586 address 0, 16K, 32K, or
	48K corresponding to window sizes of 64K, 48K, 32K and 16K respectively.
	We can account for this be setting the 'SBC Base' entry in the ISCP table
	below for all the 16 bit offset addresses, and also adding the 'SCB Base'
	value to all 24 bit physical addresses (in the SCP table and the TX and RX
	Buffer Descriptors).
					-Mark
	*/
#define SCB_BASE		((unsigned)64*1024 - (dev->mem_end - dev->mem_start))

/*
  What follows in 'init_words[]' is the "program" that is downloaded to the
  82586 memory.	 It's mostly tables and command blocks, and starts at the
  reset address 0xfffff6.  This is designed to be similar to the EtherExpress,
  thus the unusual location of the SCB at 0x0008.

  Even with the additional "don't care" values, doing it this way takes less
  program space than initializing the individual tables, and I feel it's much
  cleaner.

  The databook is particularly useless for the first two structures, I had
  to use the Crynwr driver as an example.

   The memory setup is as follows:
   */

#define CONFIG_CMD	0x0018
#define SET_SA_CMD	0x0024
#define SA_OFFSET	0x002A
#define IDLELOOP	0x30
#define TDR_CMD		0x38
#define TDR_TIME	0x3C
#define DUMP_CMD	0x40
#define DIAG_CMD	0x48
#define SET_MC_CMD	0x4E
#define DUMP_DATA	0x56	/* A 170 byte buffer for dump and Set-MC into. */

#define TX_BUF_START	0x0100
#define NUM_TX_BUFS 	5
#define TX_BUF_SIZE 	(1518+14+20+16) /* packet+header+TBD */

#define RX_BUF_START	0x2000
#define RX_BUF_SIZE 	(1518+14+18)	/* packet+header+RBD */
#define RX_BUF_END		(dev->mem_end - dev->mem_start)

#define TX_TIMEOUT 5

/*
  That's it: only 86 bytes to set up the beast, including every extra
  command available.  The 170 byte buffer at DUMP_DATA is shared between the
  Dump command (called only by the diagnostic program) and the SetMulticastList
  command.

  To complete the memory setup you only have to write the station address at
  SA_OFFSET and create the Tx & Rx buffer lists.

  The Tx command chain and buffer list is setup as follows:
  A Tx command table, with the data buffer pointing to...
  A Tx data buffer descriptor.  The packet is in a single buffer, rather than
	chaining together several smaller buffers.
  A NoOp command, which initially points to itself,
  And the packet data.

  A transmit is done by filling in the Tx command table and data buffer,
  re-writing the NoOp command, and finally changing the offset of the last
  command to point to the current Tx command.  When the Tx command is finished,
  it jumps to the NoOp, when it loops until the next Tx command changes the
  "link offset" in the NoOp.  This way the 82586 never has to go through the
  slow restart sequence.

  The Rx buffer list is set up in the obvious ring structure.  We have enough
  memory (and low enough interrupt latency) that we can avoid the complicated
  Rx buffer linked lists by alway associating a full-size Rx data buffer with
  each Rx data frame.

  I current use four transmit buffers starting at TX_BUF_START (0x0100), and
  use the rest of memory, from RX_BUF_START to RX_BUF_END, for Rx buffers.

  */

static unsigned short init_words[] = {
	/*	System Configuration Pointer (SCP). */
	0x0000,					/* Set bus size to 16 bits. */
	0,0,					/* pad words. */
	0x0000,0x0000,			/* ISCP phys addr, set in init_82586_mem(). */

	/*	Intermediate System Configuration Pointer (ISCP). */
	0x0001,					/* Status word that's cleared when init is done. */
	0x0008,0,0,				/* SCB offset, (skip, skip) */

	/* System Control Block (SCB). */
	0,0xf000|RX_START|CUC_START,	/* SCB status and cmd. */
	CONFIG_CMD,				/* Command list pointer, points to Configure. */
	RX_BUF_START,				/* Rx block list. */
	0,0,0,0,				/* Error count: CRC, align, buffer, overrun. */

	/* 0x0018: Configure command.  Change to put MAC data with packet. */
	0, CmdConfigure,		/* Status, command.		*/
	SET_SA_CMD,				/* Next command is Set Station Addr. */
	0x0804,					/* "4" bytes of config data, 8 byte FIFO. */
	0x2e40,					/* Magic values, including MAC data location. */
	0,						/* Unused pad word. */

	/* 0x0024: Setup station address command. */
	0, CmdSASetup,
	SET_MC_CMD,				/* Next command. */
	0xaa00,0xb000,0x0bad,	/* Station address (to be filled in) */

	/* 0x0030: NOP, looping back to itself.	 Point to first Tx buffer to Tx. */
	0, CmdNOp, IDLELOOP, 0 /* pad */,

	/* 0x0038: A unused Time-Domain Reflectometer command. */
	0, CmdTDR, IDLELOOP, 0,

	/* 0x0040: An unused Dump State command. */
	0, CmdDump, IDLELOOP, DUMP_DATA,

	/* 0x0048: An unused Diagnose command. */
	0, CmdDiagnose, IDLELOOP,

	/* 0x004E: An empty set-multicast-list command. */
	0, CmdMulticastList, IDLELOOP, 0,
};

/* Index to functions, as function prototypes. */

static int	el16_probe1(struct net_device *dev, int ioaddr);
static int	el16_open(struct net_device *dev);
static int	el16_send_packet(struct sk_buff *skb, struct net_device *dev);
static irqreturn_t el16_interrupt(int irq, void *dev_id);
static void el16_rx(struct net_device *dev);
static int	el16_close(struct net_device *dev);
static void el16_tx_timeout (struct net_device *dev);

static void hardware_send_packet(struct net_device *dev, void *buf, short length, short pad);
static void init_82586_mem(struct net_device *dev);
static const struct ethtool_ops netdev_ethtool_ops;
static void init_rx_bufs(struct net_device *);

static int io = 0x300;
static int irq;
static int mem_start;


/* Check for a network adaptor of this type, and return '0' iff one exists.
	If dev->base_addr == 0, probe all likely locations.
	If dev->base_addr == 1, always return failure.
	If dev->base_addr == 2, (detachable devices only) allocate space for the
	device and return success.
	*/

struct net_device * __init el16_probe(int unit)
{
	struct net_device *dev = alloc_etherdev(sizeof(struct net_local));
	static unsigned ports[] = { 0x300, 0x320, 0x340, 0x280, 0};
	unsigned *port;
	int err = -ENODEV;

	if (!dev)
		return ERR_PTR(-ENODEV);

	if (unit >= 0) {
		sprintf(dev->name, "eth%d", unit);
		netdev_boot_setup_check(dev);
		io = dev->base_addr;
		irq = dev->irq;
		mem_start = dev->mem_start & 15;
	}

	if (io > 0x1ff) 	/* Check a single specified location. */
		err = el16_probe1(dev, io);
	else if (io != 0)
		err = -ENXIO;		/* Don't probe at all. */
	else {
		for (port = ports; *port; port++) {
			err = el16_probe1(dev, *port);
			if (!err)
				break;
		}
	}

	if (err)
		goto out;
	err = register_netdev(dev);
	if (err)
		goto out1;
	return dev;
out1:
	free_irq(dev->irq, dev);
	iounmap(((struct net_local *)netdev_priv(dev))->base);
	release_region(dev->base_addr, EL16_IO_EXTENT);
out:
	free_netdev(dev);
	return ERR_PTR(err);
}

static int __init el16_probe1(struct net_device *dev, int ioaddr)
{
	static unsigned char init_ID_done, version_printed;
	int i, irq, irqval, retval;
	struct net_local *lp;
	DECLARE_MAC_BUF(mac);

	if (init_ID_done == 0) {
		ushort lrs_state = 0xff;
		/* Send the ID sequence to the ID_PORT to enable the board(s). */
		outb(0x00, ID_PORT);
		for(i = 0; i < 255; i++) {
			outb(lrs_state, ID_PORT);
			lrs_state <<= 1;
			if (lrs_state & 0x100)
				lrs_state ^= 0xe7;
		}
		outb(0x00, ID_PORT);
		init_ID_done = 1;
	}

	if (!request_region(ioaddr, EL16_IO_EXTENT, DRV_NAME))
		return -ENODEV;

	if ((inb(ioaddr) != '*') || (inb(ioaddr + 1) != '3') ||
	    (inb(ioaddr + 2) != 'C') || (inb(ioaddr + 3) != 'O')) {
		retval = -ENODEV;
		goto out;
	}

	if (net_debug  &&  version_printed++ == 0)
		printk(version);

	printk("%s: 3c507 at %#x,", dev->name, ioaddr);

	/* We should make a few more checks here, like the first three octets of
	   the S.A. for the manufacturer's code. */

	irq = inb(ioaddr + IRQ_CONFIG) & 0x0f;

	irqval = request_irq(irq, &el16_interrupt, 0, DRV_NAME, dev);
	if (irqval) {
		printk(KERN_ERR "3c507: unable to get IRQ %d (irqval=%d).\n", irq, irqval);
		retval = -EAGAIN;
		goto out;
	}

	/* We've committed to using the board, and can start filling in *dev. */
	dev->base_addr = ioaddr;

	outb(0x01, ioaddr + MISC_CTRL);
	for (i = 0; i < 6; i++)
		dev->dev_addr[i] = inb(ioaddr + i);
	printk(" %s", print_mac(mac, dev->dev_addr));

	if (mem_start)
		net_debug = mem_start & 7;

#ifdef MEM_BASE
	dev->mem_start = MEM_BASE;
	dev->mem_end = dev->mem_start + 0x10000;
#else
	{
		int base;
		int size;
		char mem_config = inb(ioaddr + MEM_CONFIG);
		if (mem_config & 0x20) {
			size = 64*1024;
			base = 0xf00000 + (mem_config & 0x08 ? 0x080000
							   : ((mem_config & 3) << 17));
		} else {
			size = ((mem_config & 3) + 1) << 14;
			base = 0x0c0000 + ( (mem_config & 0x18) << 12);
		}
		dev->mem_start = base;
		dev->mem_end = base + size;
	}
#endif

	dev->if_port = (inb(ioaddr + ROM_CONFIG) & 0x80) ? 1 : 0;
	dev->irq = inb(ioaddr + IRQ_CONFIG) & 0x0f;

	printk(", IRQ %d, %sternal xcvr, memory %#lx-%#lx.\n", dev->irq,
		   dev->if_port ? "ex" : "in", dev->mem_start, dev->mem_end-1);

	if (net_debug)
		printk(version);

	lp = netdev_priv(dev);
 	memset(lp, 0, sizeof(*lp));
	spin_lock_init(&lp->lock);
	lp->base = ioremap(dev->mem_start, RX_BUF_END);
	if (!lp->base) {
		printk(KERN_ERR "3c507: unable to remap memory\n");
		retval = -EAGAIN;
		goto out1;
	}

 	dev->open = el16_open;
 	dev->stop = el16_close;
	dev->hard_start_xmit = el16_send_packet;
	dev->tx_timeout = el16_tx_timeout;
	dev->watchdog_timeo = TX_TIMEOUT;
	dev->ethtool_ops = &netdev_ethtool_ops;
 	dev->flags &= ~IFF_MULTICAST;	/* Multicast doesn't work */
	return 0;
out1:
	free_irq(dev->irq, dev);
out:
	release_region(ioaddr, EL16_IO_EXTENT);
	return retval;
}

static int el16_open(struct net_device *dev)
{
	/* Initialize the 82586 memory and start it. */
	init_82586_mem(dev);

	netif_start_queue(dev);
	return 0;
}


static void el16_tx_timeout (struct net_device *dev)
{
	struct net_local *lp = netdev_priv(dev);
	int ioaddr = dev->base_addr;
	void __iomem *shmem = lp->base;

	if (net_debug > 1)
		printk ("%s: transmit timed out, %s?  ", dev->name,
			readw(shmem + iSCB_STATUS) & 0x8000 ? "IRQ conflict" :
			"network cable problem");
	/* Try to restart the adaptor. */
	if (lp->last_restart == dev->stats.tx_packets) {
		if (net_debug > 1)
			printk ("Resetting board.\n");
		/* Completely reset the adaptor. */
		init_82586_mem (dev);
		lp->tx_pkts_in_ring = 0;
	} else {
		/* Issue the channel attention signal and hope it "gets better". */
		if (net_debug > 1)
			printk ("Kicking board.\n");
		writew(0xf000 | CUC_START | RX_START, shmem + iSCB_CMD);
		outb (0, ioaddr + SIGNAL_CA);	/* Issue channel-attn. */
		lp->last_restart = dev->stats.tx_packets;
	}
	dev->trans_start = jiffies;
	netif_wake_queue (dev);
}


static int el16_send_packet (struct sk_buff *skb, struct net_device *dev)
{
	struct net_local *lp = netdev_priv(dev);
	int ioaddr = dev->base_addr;
	unsigned long flags;
	short length = ETH_ZLEN < skb->len ? skb->len : ETH_ZLEN;
	unsigned char *buf = skb->data;

	netif_stop_queue (dev);

	spin_lock_irqsave (&lp->lock, flags);

	dev->stats.tx_bytes += length;
	/* Disable the 82586's input to the interrupt line. */
	outb (0x80, ioaddr + MISC_CTRL);

	hardware_send_packet (dev, buf, skb->len, length - skb->len);

	dev->trans_start = jiffies;
	/* Enable the 82586 interrupt input. */
	outb (0x84, ioaddr + MISC_CTRL);

	spin_unlock_irqrestore (&lp->lock, flags);

	dev_kfree_skb (skb);

	/* You might need to clean up and record Tx statistics here. */

	return 0;
}

/*	The typical workload of the driver:
	Handle the network interface interrupts. */
static irqreturn_t el16_interrupt(int irq, void *dev_id)
{
	struct net_device *dev = dev_id;
	struct net_local *lp;
	int ioaddr, status, boguscount = 0;
	ushort ack_cmd = 0;
	void __iomem *shmem;

	if (dev == NULL) {
		printk ("net_interrupt(): irq %d for unknown device.\n", irq);
		return IRQ_NONE;
	}

	ioaddr = dev->base_addr;
	lp = netdev_priv(dev);
	shmem = lp->base;

	spin_lock(&lp->lock);

	status = readw(shmem+iSCB_STATUS);

	if (net_debug > 4) {
		printk("%s: 3c507 interrupt, status %4.4x.\n", dev->name, status);
	}

	/* Disable the 82586's input to the interrupt line. */
	outb(0x80, ioaddr + MISC_CTRL);

	/* Reap the Tx packet buffers. */
	while (lp->tx_pkts_in_ring) {
	  unsigned short tx_status = readw(shmem+lp->tx_reap);
	  if (!(tx_status & 0x8000)) {
		if (net_debug > 5)
			printk("Tx command incomplete (%#x).\n", lp->tx_reap);
		break;
	  }
	  /* Tx unsuccessful or some interesting status bit set. */
	  if (!(tx_status & 0x2000) || (tx_status & 0x0f3f)) {
		dev->stats.tx_errors++;
		if (tx_status & 0x0600)  dev->stats.tx_carrier_errors++;
		if (tx_status & 0x0100)  dev->stats.tx_fifo_errors++;
		if (!(tx_status & 0x0040))  dev->stats.tx_heartbeat_errors++;
		if (tx_status & 0x0020)  dev->stats.tx_aborted_errors++;
		dev->stats.collisions += tx_status & 0xf;
	  }
	  dev->stats.tx_packets++;
	  if (net_debug > 5)
		  printk("Reaped %x, Tx status %04x.\n" , lp->tx_reap, tx_status);
	  lp->tx_reap += TX_BUF_SIZE;
	  if (lp->tx_reap > RX_BUF_START - TX_BUF_SIZE)
		lp->tx_reap = TX_BUF_START;

	  lp->tx_pkts_in_ring--;
	  /* There is always more space in the Tx ring buffer now. */
	  netif_wake_queue(dev);

	  if (++boguscount > 10)
		break;
	}

	if (status & 0x4000) { /* Packet received. */
		if (net_debug > 5)
			printk("Received packet, rx_head %04x.\n", lp->rx_head);
		el16_rx(dev);
	}

	/* Acknowledge the interrupt sources. */
	ack_cmd = status & 0xf000;

	if ((status & 0x0700) != 0x0200 && netif_running(dev)) {
		if (net_debug)
			printk("%s: Command unit stopped, status %04x, restarting.\n",
				   dev->name, status);
		/* If this ever occurs we should really re-write the idle loop, reset
		   the Tx list, and do a complete restart of the command unit.
		   For now we rely on the Tx timeout if the resume doesn't work. */
		ack_cmd |= CUC_RESUME;
	}

	if ((status & 0x0070) != 0x0040 && netif_running(dev)) {
		/* The Rx unit is not ready, it must be hung.  Restart the receiver by
		   initializing the rx buffers, and issuing an Rx start command. */
		if (net_debug)
			printk("%s: Rx unit stopped, status %04x, restarting.\n",
				   dev->name, status);
		init_rx_bufs(dev);
		writew(RX_BUF_START,shmem+iSCB_RFA);
		ack_cmd |= RX_START;
	}

	writew(ack_cmd,shmem+iSCB_CMD);
	outb(0, ioaddr + SIGNAL_CA);			/* Issue channel-attn. */

	/* Clear the latched interrupt. */
	outb(0, ioaddr + RESET_IRQ);

	/* Enable the 82586's interrupt input. */
	outb(0x84, ioaddr + MISC_CTRL);
	spin_unlock(&lp->lock);
	return IRQ_HANDLED;
}

static int el16_close(struct net_device *dev)
{
	struct net_local *lp = netdev_priv(dev);
	int ioaddr = dev->base_addr;
	void __iomem *shmem = lp->base;

	netif_stop_queue(dev);

	/* Flush the Tx and disable Rx. */
	writew(RX_SUSPEND | CUC_SUSPEND,shmem+iSCB_CMD);
	outb(0, ioaddr + SIGNAL_CA);

	/* Disable the 82586's input to the interrupt line. */
	outb(0x80, ioaddr + MISC_CTRL);

	/* We always physically use the IRQ line, so we don't do free_irq(). */

	/* Update the statistics here. */

	return 0;
}

/* Initialize the Rx-block list. */
static void init_rx_bufs(struct net_device *dev)
{
	struct net_local *lp = netdev_priv(dev);
	void __iomem *write_ptr;
	unsigned short SCB_base = SCB_BASE;

	int cur_rxbuf = lp->rx_head = RX_BUF_START;

	/* Initialize each Rx frame + data buffer. */
	do {	/* While there is room for one more. */

		write_ptr = lp->base + cur_rxbuf;

		writew(0x0000,write_ptr);			/* Status */
		writew(0x0000,write_ptr+=2);			/* Command */
		writew(cur_rxbuf + RX_BUF_SIZE,write_ptr+=2);	/* Link */
		writew(cur_rxbuf + 22,write_ptr+=2);		/* Buffer offset */
		writew(0x0000,write_ptr+=2);			/* Pad for dest addr. */
		writew(0x0000,write_ptr+=2);
		writew(0x0000,write_ptr+=2);
		writew(0x0000,write_ptr+=2);			/* Pad for source addr. */
		writew(0x0000,write_ptr+=2);
		writew(0x0000,write_ptr+=2);
		writew(0x0000,write_ptr+=2);			/* Pad for protocol. */

		writew(0x0000,write_ptr+=2);			/* Buffer: Actual count */
		writew(-1,write_ptr+=2);			/* Buffer: Next (none). */
		writew(cur_rxbuf + 0x20 + SCB_base,write_ptr+=2);/* Buffer: Address low */
		writew(0x0000,write_ptr+=2);
		/* Finally, the number of bytes in the buffer. */
		writew(0x8000 + RX_BUF_SIZE-0x20,write_ptr+=2);

		lp->rx_tail = cur_rxbuf;
		cur_rxbuf += RX_BUF_SIZE;
	} while (cur_rxbuf <= RX_BUF_END - RX_BUF_SIZE);

	/* Terminate the list by setting the EOL bit, and wrap the pointer to make
	   the list a ring. */
	write_ptr = lp->base + lp->rx_tail + 2;
	writew(0xC000,write_ptr);				/* Command, mark as last. */
	writew(lp->rx_head,write_ptr+2);			/* Link */
}

static void init_82586_mem(struct net_device *dev)
{
	struct net_local *lp = netdev_priv(dev);
	short ioaddr = dev->base_addr;
	void __iomem *shmem = lp->base;

	/* Enable loopback to protect the wire while starting up,
	   and hold the 586 in reset during the memory initialization. */
	outb(0x20, ioaddr + MISC_CTRL);

	/* Fix the ISCP address and base. */
	init_words[3] = SCB_BASE;
	init_words[7] = SCB_BASE;

	/* Write the words at 0xfff6 (address-aliased to 0xfffff6). */
	memcpy_toio(lp->base + RX_BUF_END - 10, init_words, 10);

	/* Write the words at 0x0000. */
	memcpy_toio(lp->base, init_words + 5, sizeof(init_words) - 10);

	/* Fill in the station address. */
	memcpy_toio(lp->base+SA_OFFSET, dev->dev_addr,
		   sizeof(dev->dev_addr));

	/* The Tx-block list is written as needed.  We just set up the values. */
	lp->tx_cmd_link = IDLELOOP + 4;
	lp->tx_head = lp->tx_reap = TX_BUF_START;

	init_rx_bufs(dev);

	/* Start the 586 by releasing the reset line, but leave loopback. */
	outb(0xA0, ioaddr + MISC_CTRL);

	/* This was time consuming to track down: you need to give two channel
	   attention signals to reliably start up the i82586. */
	outb(0, ioaddr + SIGNAL_CA);

	{
		int boguscnt = 50;
		while (readw(shmem+iSCB_STATUS) == 0)
			if (--boguscnt == 0) {
				printk("%s: i82586 initialization timed out with status %04x, "
					   "cmd %04x.\n", dev->name,
					   readw(shmem+iSCB_STATUS), readw(shmem+iSCB_CMD));
				break;
			}
		/* Issue channel-attn -- the 82586 won't start. */
		outb(0, ioaddr + SIGNAL_CA);
	}

	/* Disable loopback and enable interrupts. */
	outb(0x84, ioaddr + MISC_CTRL);
	if (net_debug > 4)
		printk("%s: Initialized 82586, status %04x.\n", dev->name,
			   readw(shmem+iSCB_STATUS));
	return;
}

static void hardware_send_packet(struct net_device *dev, void *buf, short length, short pad)
{
	struct net_local *lp = netdev_priv(dev);
	short ioaddr = dev->base_addr;
	ushort tx_block = lp->tx_head;
	void __iomem *write_ptr = lp->base + tx_block;
	static char padding[ETH_ZLEN];

	/* Set the write pointer to the Tx block, and put out the header. */
	writew(0x0000,write_ptr);			/* Tx status */
	writew(CMD_INTR|CmdTx,write_ptr+=2);		/* Tx command */
	writew(tx_block+16,write_ptr+=2);		/* Next command is a NoOp. */
	writew(tx_block+8,write_ptr+=2);			/* Data Buffer offset. */

	/* Output the data buffer descriptor. */
	writew((pad + length) | 0x8000,write_ptr+=2);		/* Byte count parameter. */
	writew(-1,write_ptr+=2);			/* No next data buffer. */
	writew(tx_block+22+SCB_BASE,write_ptr+=2);	/* Buffer follows the NoOp command. */
	writew(0x0000,write_ptr+=2);			/* Buffer address high bits (always zero). */

	/* Output the Loop-back NoOp command. */
	writew(0x0000,write_ptr+=2);			/* Tx status */
	writew(CmdNOp,write_ptr+=2);			/* Tx command */
	writew(tx_block+16,write_ptr+=2);		/* Next is myself. */

	/* Output the packet at the write pointer. */
	memcpy_toio(write_ptr+2, buf, length);
	if (pad)
		memcpy_toio(write_ptr+length+2, padding, pad);

	/* Set the old command link pointing to this send packet. */
	writew(tx_block,lp->base + lp->tx_cmd_link);
	lp->tx_cmd_link = tx_block + 20;

	/* Set the next free tx region. */
	lp->tx_head = tx_block + TX_BUF_SIZE;
	if (lp->tx_head > RX_BUF_START - TX_BUF_SIZE)
		lp->tx_head = TX_BUF_START;

	if (net_debug > 4) {
		printk("%s: 3c507 @%x send length = %d, tx_block %3x, next %3x.\n",
			   dev->name, ioaddr, length, tx_block, lp->tx_head);
	}

	/* Grimly block further packets if there has been insufficient reaping. */
	if (++lp->tx_pkts_in_ring < NUM_TX_BUFS)
		netif_wake_queue(dev);
}

static void el16_rx(struct net_device *dev)
{
	struct net_local *lp = netdev_priv(dev);
	void __iomem *shmem = lp->base;
	ushort rx_head = lp->rx_head;
	ushort rx_tail = lp->rx_tail;
	ushort boguscount = 10;
	short frame_status;

	while ((frame_status = readw(shmem+rx_head)) < 0) {   /* Command complete */
		void __iomem *read_frame = lp->base + rx_head;
		ushort rfd_cmd = readw(read_frame+2);
		ushort next_rx_frame = readw(read_frame+4);
		ushort data_buffer_addr = readw(read_frame+6);
		void __iomem *data_frame = lp->base + data_buffer_addr;
		ushort pkt_len = readw(data_frame);

		if (rfd_cmd != 0 || data_buffer_addr != rx_head + 22
			|| (pkt_len & 0xC000) != 0xC000) {
			printk(KERN_ERR "%s: Rx frame at %#x corrupted, "
			       "status %04x cmd %04x next %04x "
			       "data-buf @%04x %04x.\n",
			       dev->name, rx_head, frame_status, rfd_cmd,
			       next_rx_frame, data_buffer_addr, pkt_len);
		} else if ((frame_status & 0x2000) == 0) {
			/* Frame Rxed, but with error. */
			dev->stats.rx_errors++;
			if (frame_status & 0x0800) dev->stats.rx_crc_errors++;
			if (frame_status & 0x0400) dev->stats.rx_frame_errors++;
			if (frame_status & 0x0200) dev->stats.rx_fifo_errors++;
			if (frame_status & 0x0100) dev->stats.rx_over_errors++;
			if (frame_status & 0x0080) dev->stats.rx_length_errors++;
		} else {
			/* Malloc up new buffer. */
			struct sk_buff *skb;

			pkt_len &= 0x3fff;
			skb = dev_alloc_skb(pkt_len+2);
			if (skb == NULL) {
				printk(KERN_ERR "%s: Memory squeeze, "
				       "dropping packet.\n",
				       dev->name);
				dev->stats.rx_dropped++;
				break;
			}

			skb_reserve(skb,2);

			/* 'skb->data' points to the start of sk_buff data area. */
			memcpy_fromio(skb_put(skb,pkt_len), data_frame + 10, pkt_len);

			skb->protocol=eth_type_trans(skb,dev);
			netif_rx(skb);
			dev->last_rx = jiffies;
			dev->stats.rx_packets++;
			dev->stats.rx_bytes += pkt_len;
		}

		/* Clear the status word and set End-of-List on the rx frame. */
		writew(0,read_frame);
		writew(0xC000,read_frame+2);
		/* Clear the end-of-list on the prev. RFD. */
		writew(0x0000,lp->base + rx_tail + 2);

		rx_tail = rx_head;
		rx_head = next_rx_frame;
		if (--boguscount == 0)
			break;
	}

	lp->rx_head = rx_head;
	lp->rx_tail = rx_tail;
}

static void netdev_get_drvinfo(struct net_device *dev,
			       struct ethtool_drvinfo *info)
{
	strcpy(info->driver, DRV_NAME);
	strcpy(info->version, DRV_VERSION);
	sprintf(info->bus_info, "ISA 0x%lx", dev->base_addr);
}

static u32 netdev_get_msglevel(struct net_device *dev)
{
	return debug;
}

static void netdev_set_msglevel(struct net_device *dev, u32 level)
{
	debug = level;
}

static const struct ethtool_ops netdev_ethtool_ops = {
	.get_drvinfo		= netdev_get_drvinfo,
	.get_msglevel		= netdev_get_msglevel,
	.set_msglevel		= netdev_set_msglevel,
};

#ifdef MODULE
static struct net_device *dev_3c507;
module_param(io, int, 0);
module_param(irq, int, 0);
MODULE_PARM_DESC(io, "EtherLink16 I/O base address");
MODULE_PARM_DESC(irq, "(ignored)");

int __init init_module(void)
{
	if (io == 0)
		printk("3c507: You should not use auto-probing with insmod!\n");
	dev_3c507 = el16_probe(-1);
	return IS_ERR(dev_3c507) ? PTR_ERR(dev_3c507) : 0;
}

void __exit
cleanup_module(void)
{
	struct net_device *dev = dev_3c507;
	unregister_netdev(dev);
	free_irq(dev->irq, dev);
	iounmap(((struct net_local *)netdev_priv(dev))->base);
	release_region(dev->base_addr, EL16_IO_EXTENT);
	free_netdev(dev);
}
#endif /* MODULE */
MODULE_LICENSE("GPL");


/*
 * Local variables:
 *  compile-command: "gcc -D__KERNEL__ -I/usr/src/linux/net/inet -I/usr/src/linux/drivers/net -Wall -Wstrict-prototypes -O6 -m486 -c 3c507.c"
 *  version-control: t
 *  kept-new-versions: 5
 *  tab-width: 4
 *  c-indent-level: 4
 * End:
 */
