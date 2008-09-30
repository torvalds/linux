/* 8390.c: A general NS8390 ethernet driver core for linux. */
/*
	Written 1992-94 by Donald Becker.

	Copyright 1993 United States Government as represented by the
	Director, National Security Agency.

	This software may be used and distributed according to the terms
	of the GNU General Public License, incorporated herein by reference.

	The author may be reached as becker@scyld.com, or C/O
	Scyld Computing Corporation
	410 Severn Ave., Suite 210
	Annapolis MD 21403


  This is the chip-specific code for many 8390-based ethernet adaptors.
  This is not a complete driver, it must be combined with board-specific
  code such as ne.c, wd.c, 3c503.c, etc.

  Seeing how at least eight drivers use this code, (not counting the
  PCMCIA ones either) it is easy to break some card by what seems like
  a simple innocent change. Please contact me or Donald if you think
  you have found something that needs changing. -- PG


  Changelog:

  Paul Gortmaker	: remove set_bit lock, other cleanups.
  Paul Gortmaker	: add ei_get_8390_hdr() so we can pass skb's to
			  ei_block_input() for eth_io_copy_and_sum().
  Paul Gortmaker	: exchange static int ei_pingpong for a #define,
			  also add better Tx error handling.
  Paul Gortmaker	: rewrite Rx overrun handling as per NS specs.
  Alexey Kuznetsov	: use the 8390's six bit hash multicast filter.
  Paul Gortmaker	: tweak ANK's above multicast changes a bit.
  Paul Gortmaker	: update packet statistics for v2.1.x
  Alan Cox		: support arbitary stupid port mappings on the
  			  68K Macintosh. Support >16bit I/O spaces
  Paul Gortmaker	: add kmod support for auto-loading of the 8390
			  module by all drivers that require it.
  Alan Cox		: Spinlocking work, added 'BUG_83C690'
  Paul Gortmaker	: Separate out Tx timeout code from Tx path.
  Paul Gortmaker	: Remove old unused single Tx buffer code.
  Hayato Fujiwara	: Add m32r support.
  Paul Gortmaker	: use skb_padto() instead of stack scratch area

  Sources:
  The National Semiconductor LAN Databook, and the 3Com 3c503 databook.

  */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/jiffies.h>
#include <linux/fs.h>
#include <linux/types.h>
#include <linux/string.h>
#include <linux/bitops.h>
#include <asm/system.h>
#include <asm/uaccess.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/fcntl.h>
#include <linux/in.h>
#include <linux/interrupt.h>
#include <linux/init.h>
#include <linux/crc32.h>

#include <linux/netdevice.h>
#include <linux/etherdevice.h>

#define NS8390_CORE
#include "8390.h"

#define BUG_83C690

/* These are the operational function interfaces to board-specific
   routines.
	void reset_8390(struct net_device *dev)
		Resets the board associated with DEV, including a hardware reset of
		the 8390.  This is only called when there is a transmit timeout, and
		it is always followed by 8390_init().
	void block_output(struct net_device *dev, int count, const unsigned char *buf,
					  int start_page)
		Write the COUNT bytes of BUF to the packet buffer at START_PAGE.  The
		"page" value uses the 8390's 256-byte pages.
	void get_8390_hdr(struct net_device *dev, struct e8390_hdr *hdr, int ring_page)
		Read the 4 byte, page aligned 8390 header. *If* there is a
		subsequent read, it will be of the rest of the packet.
	void block_input(struct net_device *dev, int count, struct sk_buff *skb, int ring_offset)
		Read COUNT bytes from the packet buffer into the skb data area. Start
		reading from RING_OFFSET, the address as the 8390 sees it.  This will always
		follow the read of the 8390 header.
*/
#define ei_reset_8390 (ei_local->reset_8390)
#define ei_block_output (ei_local->block_output)
#define ei_block_input (ei_local->block_input)
#define ei_get_8390_hdr (ei_local->get_8390_hdr)

/* use 0 for production, 1 for verification, >2 for debug */
#ifndef ei_debug
int ei_debug = 1;
#endif

/* Index to functions. */
static void ei_tx_intr(struct net_device *dev);
static void ei_tx_err(struct net_device *dev);
static void ei_tx_timeout(struct net_device *dev);
static void ei_receive(struct net_device *dev);
static void ei_rx_overrun(struct net_device *dev);

/* Routines generic to NS8390-based boards. */
static void NS8390_trigger_send(struct net_device *dev, unsigned int length,
								int start_page);
static void set_multicast_list(struct net_device *dev);
static void do_set_multicast_list(struct net_device *dev);
static void __NS8390_init(struct net_device *dev, int startp);

/*
 *	SMP and the 8390 setup.
 *
 *	The 8390 isnt exactly designed to be multithreaded on RX/TX. There is
 *	a page register that controls bank and packet buffer access. We guard
 *	this with ei_local->page_lock. Nobody should assume or set the page other
 *	than zero when the lock is not held. Lock holders must restore page 0
 *	before unlocking. Even pure readers must take the lock to protect in
 *	page 0.
 *
 *	To make life difficult the chip can also be very slow. We therefore can't
 *	just use spinlocks. For the longer lockups we disable the irq the device
 *	sits on and hold the lock. We must hold the lock because there is a dual
 *	processor case other than interrupts (get stats/set multicast list in
 *	parallel with each other and transmit).
 *
 *	Note: in theory we can just disable the irq on the card _but_ there is
 *	a latency on SMP irq delivery. So we can easily go "disable irq" "sync irqs"
 *	enter lock, take the queued irq. So we waddle instead of flying.
 *
 *	Finally by special arrangement for the purpose of being generally
 *	annoying the transmit function is called bh atomic. That places
 *	restrictions on the user context callers as disable_irq won't save
 *	them.
 *
 *	Additional explanation of problems with locking by Alan Cox:
 *
 *	"The author (me) didn't use spin_lock_irqsave because the slowness of the
 *	card means that approach caused horrible problems like losing serial data
 *	at 38400 baud on some chips. Remember many 8390 nics on PCI were ISA
 *	chips with FPGA front ends.
 *
 *	Ok the logic behind the 8390 is very simple:
 *
 *	Things to know
 *		- IRQ delivery is asynchronous to the PCI bus
 *		- Blocking the local CPU IRQ via spin locks was too slow
 *		- The chip has register windows needing locking work
 *
 *	So the path was once (I say once as people appear to have changed it
 *	in the mean time and it now looks rather bogus if the changes to use
 *	disable_irq_nosync_irqsave are disabling the local IRQ)
 *
 *
 *		Take the page lock
 *		Mask the IRQ on chip
 *		Disable the IRQ (but not mask locally- someone seems to have
 *			broken this with the lock validator stuff)
 *			[This must be _nosync as the page lock may otherwise
 *				deadlock us]
 *		Drop the page lock and turn IRQs back on
 *
 *		At this point an existing IRQ may still be running but we can't
 *		get a new one
 *
 *		Take the lock (so we know the IRQ has terminated) but don't mask
 *	the IRQs on the processor
 *		Set irqlock [for debug]
 *
 *		Transmit (slow as ****)
 *
 *		re-enable the IRQ
 *
 *
 *	We have to use disable_irq because otherwise you will get delayed
 *	interrupts on the APIC bus deadlocking the transmit path.
 *
 *	Quite hairy but the chip simply wasn't designed for SMP and you can't
 *	even ACK an interrupt without risking corrupting other parallel
 *	activities on the chip." [lkml, 25 Jul 2007]
 */



/**
 * ei_open - Open/initialize the board.
 * @dev: network device to initialize
 *
 * This routine goes all-out, setting everything
 * up anew at each open, even though many of these registers should only
 * need to be set once at boot.
 */
static int __ei_open(struct net_device *dev)
{
	unsigned long flags;
	struct ei_device *ei_local = (struct ei_device *) netdev_priv(dev);

	/* The card I/O part of the driver (e.g. 3c503) can hook a Tx timeout
	    wrapper that does e.g. media check & then calls ei_tx_timeout. */
	if (dev->tx_timeout == NULL)
		 dev->tx_timeout = ei_tx_timeout;
	if (dev->watchdog_timeo <= 0)
		 dev->watchdog_timeo = TX_TIMEOUT;

	/*
	 *	Grab the page lock so we own the register set, then call
	 *	the init function.
	 */

      	spin_lock_irqsave(&ei_local->page_lock, flags);
	__NS8390_init(dev, 1);
	/* Set the flag before we drop the lock, That way the IRQ arrives
	   after its set and we get no silly warnings */
	netif_start_queue(dev);
      	spin_unlock_irqrestore(&ei_local->page_lock, flags);
	ei_local->irqlock = 0;
	return 0;
}

/**
 * ei_close - shut down network device
 * @dev: network device to close
 *
 * Opposite of ei_open(). Only used when "ifconfig <devname> down" is done.
 */
static int __ei_close(struct net_device *dev)
{
	struct ei_device *ei_local = (struct ei_device *) netdev_priv(dev);
	unsigned long flags;

	/*
	 *	Hold the page lock during close
	 */

      	spin_lock_irqsave(&ei_local->page_lock, flags);
	__NS8390_init(dev, 0);
      	spin_unlock_irqrestore(&ei_local->page_lock, flags);
	netif_stop_queue(dev);
	return 0;
}

/**
 * ei_tx_timeout - handle transmit time out condition
 * @dev: network device which has apparently fallen asleep
 *
 * Called by kernel when device never acknowledges a transmit has
 * completed (or failed) - i.e. never posted a Tx related interrupt.
 */

static void ei_tx_timeout(struct net_device *dev)
{
	unsigned long e8390_base = dev->base_addr;
	struct ei_device *ei_local = (struct ei_device *) netdev_priv(dev);
	int txsr, isr, tickssofar = jiffies - dev->trans_start;
	unsigned long flags;

	dev->stats.tx_errors++;

	spin_lock_irqsave(&ei_local->page_lock, flags);
	txsr = ei_inb(e8390_base+EN0_TSR);
	isr = ei_inb(e8390_base+EN0_ISR);
	spin_unlock_irqrestore(&ei_local->page_lock, flags);

	printk(KERN_DEBUG "%s: Tx timed out, %s TSR=%#2x, ISR=%#2x, t=%d.\n",
		dev->name, (txsr & ENTSR_ABT) ? "excess collisions." :
		(isr) ? "lost interrupt?" : "cable problem?", txsr, isr, tickssofar);

	if (!isr && !dev->stats.tx_packets)
	{
		/* The 8390 probably hasn't gotten on the cable yet. */
		ei_local->interface_num ^= 1;   /* Try a different xcvr.  */
	}

	/* Ugly but a reset can be slow, yet must be protected */

	disable_irq_nosync_lockdep(dev->irq);
	spin_lock(&ei_local->page_lock);

	/* Try to restart the card.  Perhaps the user has fixed something. */
	ei_reset_8390(dev);
	__NS8390_init(dev, 1);

	spin_unlock(&ei_local->page_lock);
	enable_irq_lockdep(dev->irq);
	netif_wake_queue(dev);
}

/**
 * ei_start_xmit - begin packet transmission
 * @skb: packet to be sent
 * @dev: network device to which packet is sent
 *
 * Sends a packet to an 8390 network device.
 */

static int ei_start_xmit(struct sk_buff *skb, struct net_device *dev)
{
	unsigned long e8390_base = dev->base_addr;
	struct ei_device *ei_local = (struct ei_device *) netdev_priv(dev);
	int send_length = skb->len, output_page;
	unsigned long flags;
	char buf[ETH_ZLEN];
	char *data = skb->data;

	if (skb->len < ETH_ZLEN) {
		memset(buf, 0, ETH_ZLEN);	/* more efficient than doing just the needed bits */
		memcpy(buf, data, skb->len);
		send_length = ETH_ZLEN;
		data = buf;
	}

	/* Mask interrupts from the ethercard.
	   SMP: We have to grab the lock here otherwise the IRQ handler
	   on another CPU can flip window and race the IRQ mask set. We end
	   up trashing the mcast filter not disabling irqs if we don't lock */

	spin_lock_irqsave(&ei_local->page_lock, flags);
	ei_outb_p(0x00, e8390_base + EN0_IMR);
	spin_unlock_irqrestore(&ei_local->page_lock, flags);


	/*
	 *	Slow phase with lock held.
	 */

	disable_irq_nosync_lockdep_irqsave(dev->irq, &flags);

	spin_lock(&ei_local->page_lock);

	ei_local->irqlock = 1;

	/*
	 * We have two Tx slots available for use. Find the first free
	 * slot, and then perform some sanity checks. With two Tx bufs,
	 * you get very close to transmitting back-to-back packets. With
	 * only one Tx buf, the transmitter sits idle while you reload the
	 * card, leaving a substantial gap between each transmitted packet.
	 */

	if (ei_local->tx1 == 0)
	{
		output_page = ei_local->tx_start_page;
		ei_local->tx1 = send_length;
		if (ei_debug  &&  ei_local->tx2 > 0)
			printk(KERN_DEBUG "%s: idle transmitter tx2=%d, lasttx=%d, txing=%d.\n",
				dev->name, ei_local->tx2, ei_local->lasttx, ei_local->txing);
	}
	else if (ei_local->tx2 == 0)
	{
		output_page = ei_local->tx_start_page + TX_PAGES/2;
		ei_local->tx2 = send_length;
		if (ei_debug  &&  ei_local->tx1 > 0)
			printk(KERN_DEBUG "%s: idle transmitter, tx1=%d, lasttx=%d, txing=%d.\n",
				dev->name, ei_local->tx1, ei_local->lasttx, ei_local->txing);
	}
	else
	{	/* We should never get here. */
		if (ei_debug)
			printk(KERN_DEBUG "%s: No Tx buffers free! tx1=%d tx2=%d last=%d\n",
				dev->name, ei_local->tx1, ei_local->tx2, ei_local->lasttx);
		ei_local->irqlock = 0;
		netif_stop_queue(dev);
		ei_outb_p(ENISR_ALL, e8390_base + EN0_IMR);
		spin_unlock(&ei_local->page_lock);
		enable_irq_lockdep_irqrestore(dev->irq, &flags);
		dev->stats.tx_errors++;
		return 1;
	}

	/*
	 * Okay, now upload the packet and trigger a send if the transmitter
	 * isn't already sending. If it is busy, the interrupt handler will
	 * trigger the send later, upon receiving a Tx done interrupt.
	 */

	ei_block_output(dev, send_length, data, output_page);

	if (! ei_local->txing)
	{
		ei_local->txing = 1;
		NS8390_trigger_send(dev, send_length, output_page);
		dev->trans_start = jiffies;
		if (output_page == ei_local->tx_start_page)
		{
			ei_local->tx1 = -1;
			ei_local->lasttx = -1;
		}
		else
		{
			ei_local->tx2 = -1;
			ei_local->lasttx = -2;
		}
	}
	else ei_local->txqueue++;

	if (ei_local->tx1  &&  ei_local->tx2)
		netif_stop_queue(dev);
	else
		netif_start_queue(dev);

	/* Turn 8390 interrupts back on. */
	ei_local->irqlock = 0;
	ei_outb_p(ENISR_ALL, e8390_base + EN0_IMR);

	spin_unlock(&ei_local->page_lock);
	enable_irq_lockdep_irqrestore(dev->irq, &flags);

	dev_kfree_skb (skb);
	dev->stats.tx_bytes += send_length;

	return 0;
}

/**
 * ei_interrupt - handle the interrupts from an 8390
 * @irq: interrupt number
 * @dev_id: a pointer to the net_device
 *
 * Handle the ether interface interrupts. We pull packets from
 * the 8390 via the card specific functions and fire them at the networking
 * stack. We also handle transmit completions and wake the transmit path if
 * necessary. We also update the counters and do other housekeeping as
 * needed.
 */

static irqreturn_t __ei_interrupt(int irq, void *dev_id)
{
	struct net_device *dev = dev_id;
	unsigned long e8390_base = dev->base_addr;
	int interrupts, nr_serviced = 0;
	struct ei_device *ei_local = netdev_priv(dev);

	/*
	 *	Protect the irq test too.
	 */

	spin_lock(&ei_local->page_lock);

	if (ei_local->irqlock)
	{
#if 1 /* This might just be an interrupt for a PCI device sharing this line */
		/* The "irqlock" check is only for testing. */
		printk(ei_local->irqlock
			   ? "%s: Interrupted while interrupts are masked! isr=%#2x imr=%#2x.\n"
			   : "%s: Reentering the interrupt handler! isr=%#2x imr=%#2x.\n",
			   dev->name, ei_inb_p(e8390_base + EN0_ISR),
			   ei_inb_p(e8390_base + EN0_IMR));
#endif
		spin_unlock(&ei_local->page_lock);
		return IRQ_NONE;
	}

	/* Change to page 0 and read the intr status reg. */
	ei_outb_p(E8390_NODMA+E8390_PAGE0, e8390_base + E8390_CMD);
	if (ei_debug > 3)
		printk(KERN_DEBUG "%s: interrupt(isr=%#2.2x).\n", dev->name,
			   ei_inb_p(e8390_base + EN0_ISR));

	/* !!Assumption!! -- we stay in page 0.	 Don't break this. */
	while ((interrupts = ei_inb_p(e8390_base + EN0_ISR)) != 0
		   && ++nr_serviced < MAX_SERVICE)
	{
		if (!netif_running(dev)) {
			printk(KERN_WARNING "%s: interrupt from stopped card\n", dev->name);
			/* rmk - acknowledge the interrupts */
			ei_outb_p(interrupts, e8390_base + EN0_ISR);
			interrupts = 0;
			break;
		}
		if (interrupts & ENISR_OVER)
			ei_rx_overrun(dev);
		else if (interrupts & (ENISR_RX+ENISR_RX_ERR))
		{
			/* Got a good (?) packet. */
			ei_receive(dev);
		}
		/* Push the next to-transmit packet through. */
		if (interrupts & ENISR_TX)
			ei_tx_intr(dev);
		else if (interrupts & ENISR_TX_ERR)
			ei_tx_err(dev);

		if (interrupts & ENISR_COUNTERS)
		{
			dev->stats.rx_frame_errors += ei_inb_p(e8390_base + EN0_COUNTER0);
			dev->stats.rx_crc_errors   += ei_inb_p(e8390_base + EN0_COUNTER1);
			dev->stats.rx_missed_errors+= ei_inb_p(e8390_base + EN0_COUNTER2);
			ei_outb_p(ENISR_COUNTERS, e8390_base + EN0_ISR); /* Ack intr. */
		}

		/* Ignore any RDC interrupts that make it back to here. */
		if (interrupts & ENISR_RDC)
		{
			ei_outb_p(ENISR_RDC, e8390_base + EN0_ISR);
		}

		ei_outb_p(E8390_NODMA+E8390_PAGE0+E8390_START, e8390_base + E8390_CMD);
	}

	if (interrupts && ei_debug)
	{
		ei_outb_p(E8390_NODMA+E8390_PAGE0+E8390_START, e8390_base + E8390_CMD);
		if (nr_serviced >= MAX_SERVICE)
		{
			/* 0xFF is valid for a card removal */
			if(interrupts!=0xFF)
				printk(KERN_WARNING "%s: Too much work at interrupt, status %#2.2x\n",
				   dev->name, interrupts);
			ei_outb_p(ENISR_ALL, e8390_base + EN0_ISR); /* Ack. most intrs. */
		} else {
			printk(KERN_WARNING "%s: unknown interrupt %#2x\n", dev->name, interrupts);
			ei_outb_p(0xff, e8390_base + EN0_ISR); /* Ack. all intrs. */
		}
	}
	spin_unlock(&ei_local->page_lock);
	return IRQ_RETVAL(nr_serviced > 0);
}

#ifdef CONFIG_NET_POLL_CONTROLLER
static void __ei_poll(struct net_device *dev)
{
	disable_irq(dev->irq);
	__ei_interrupt(dev->irq, dev);
	enable_irq(dev->irq);
}
#endif

/**
 * ei_tx_err - handle transmitter error
 * @dev: network device which threw the exception
 *
 * A transmitter error has happened. Most likely excess collisions (which
 * is a fairly normal condition). If the error is one where the Tx will
 * have been aborted, we try and send another one right away, instead of
 * letting the failed packet sit and collect dust in the Tx buffer. This
 * is a much better solution as it avoids kernel based Tx timeouts, and
 * an unnecessary card reset.
 *
 * Called with lock held.
 */

static void ei_tx_err(struct net_device *dev)
{
	unsigned long e8390_base = dev->base_addr;
	/* ei_local is used on some platforms via the EI_SHIFT macro */
	struct ei_device *ei_local __maybe_unused = netdev_priv(dev);
	unsigned char txsr = ei_inb_p(e8390_base+EN0_TSR);
	unsigned char tx_was_aborted = txsr & (ENTSR_ABT+ENTSR_FU);

#ifdef VERBOSE_ERROR_DUMP
	printk(KERN_DEBUG "%s: transmitter error (%#2x): ", dev->name, txsr);
	if (txsr & ENTSR_ABT)
		printk("excess-collisions ");
	if (txsr & ENTSR_ND)
		printk("non-deferral ");
	if (txsr & ENTSR_CRS)
		printk("lost-carrier ");
	if (txsr & ENTSR_FU)
		printk("FIFO-underrun ");
	if (txsr & ENTSR_CDH)
		printk("lost-heartbeat ");
	printk("\n");
#endif

	ei_outb_p(ENISR_TX_ERR, e8390_base + EN0_ISR); /* Ack intr. */

	if (tx_was_aborted)
		ei_tx_intr(dev);
	else
	{
		dev->stats.tx_errors++;
		if (txsr & ENTSR_CRS) dev->stats.tx_carrier_errors++;
		if (txsr & ENTSR_CDH) dev->stats.tx_heartbeat_errors++;
		if (txsr & ENTSR_OWC) dev->stats.tx_window_errors++;
	}
}

/**
 * ei_tx_intr - transmit interrupt handler
 * @dev: network device for which tx intr is handled
 *
 * We have finished a transmit: check for errors and then trigger the next
 * packet to be sent. Called with lock held.
 */

static void ei_tx_intr(struct net_device *dev)
{
	unsigned long e8390_base = dev->base_addr;
	struct ei_device *ei_local = (struct ei_device *) netdev_priv(dev);
	int status = ei_inb(e8390_base + EN0_TSR);

	ei_outb_p(ENISR_TX, e8390_base + EN0_ISR); /* Ack intr. */

	/*
	 * There are two Tx buffers, see which one finished, and trigger
	 * the send of another one if it exists.
	 */
	ei_local->txqueue--;

	if (ei_local->tx1 < 0)
	{
		if (ei_local->lasttx != 1 && ei_local->lasttx != -1)
			printk(KERN_ERR "%s: bogus last_tx_buffer %d, tx1=%d.\n",
				ei_local->name, ei_local->lasttx, ei_local->tx1);
		ei_local->tx1 = 0;
		if (ei_local->tx2 > 0)
		{
			ei_local->txing = 1;
			NS8390_trigger_send(dev, ei_local->tx2, ei_local->tx_start_page + 6);
			dev->trans_start = jiffies;
			ei_local->tx2 = -1,
			ei_local->lasttx = 2;
		}
		else ei_local->lasttx = 20, ei_local->txing = 0;
	}
	else if (ei_local->tx2 < 0)
	{
		if (ei_local->lasttx != 2  &&  ei_local->lasttx != -2)
			printk("%s: bogus last_tx_buffer %d, tx2=%d.\n",
				ei_local->name, ei_local->lasttx, ei_local->tx2);
		ei_local->tx2 = 0;
		if (ei_local->tx1 > 0)
		{
			ei_local->txing = 1;
			NS8390_trigger_send(dev, ei_local->tx1, ei_local->tx_start_page);
			dev->trans_start = jiffies;
			ei_local->tx1 = -1;
			ei_local->lasttx = 1;
		}
		else
			ei_local->lasttx = 10, ei_local->txing = 0;
	}
//	else printk(KERN_WARNING "%s: unexpected TX-done interrupt, lasttx=%d.\n",
//			dev->name, ei_local->lasttx);

	/* Minimize Tx latency: update the statistics after we restart TXing. */
	if (status & ENTSR_COL)
		dev->stats.collisions++;
	if (status & ENTSR_PTX)
		dev->stats.tx_packets++;
	else
	{
		dev->stats.tx_errors++;
		if (status & ENTSR_ABT)
		{
			dev->stats.tx_aborted_errors++;
			dev->stats.collisions += 16;
		}
		if (status & ENTSR_CRS)
			dev->stats.tx_carrier_errors++;
		if (status & ENTSR_FU)
			dev->stats.tx_fifo_errors++;
		if (status & ENTSR_CDH)
			dev->stats.tx_heartbeat_errors++;
		if (status & ENTSR_OWC)
			dev->stats.tx_window_errors++;
	}
	netif_wake_queue(dev);
}

/**
 * ei_receive - receive some packets
 * @dev: network device with which receive will be run
 *
 * We have a good packet(s), get it/them out of the buffers.
 * Called with lock held.
 */

static void ei_receive(struct net_device *dev)
{
	unsigned long e8390_base = dev->base_addr;
	struct ei_device *ei_local = (struct ei_device *) netdev_priv(dev);
	unsigned char rxing_page, this_frame, next_frame;
	unsigned short current_offset;
	int rx_pkt_count = 0;
	struct e8390_pkt_hdr rx_frame;
	int num_rx_pages = ei_local->stop_page-ei_local->rx_start_page;

	while (++rx_pkt_count < 10)
	{
		int pkt_len, pkt_stat;

		/* Get the rx page (incoming packet pointer). */
		ei_outb_p(E8390_NODMA+E8390_PAGE1, e8390_base + E8390_CMD);
		rxing_page = ei_inb_p(e8390_base + EN1_CURPAG);
		ei_outb_p(E8390_NODMA+E8390_PAGE0, e8390_base + E8390_CMD);

		/* Remove one frame from the ring.  Boundary is always a page behind. */
		this_frame = ei_inb_p(e8390_base + EN0_BOUNDARY) + 1;
		if (this_frame >= ei_local->stop_page)
			this_frame = ei_local->rx_start_page;

		/* Someday we'll omit the previous, iff we never get this message.
		   (There is at least one clone claimed to have a problem.)

		   Keep quiet if it looks like a card removal. One problem here
		   is that some clones crash in roughly the same way.
		 */
		if (ei_debug > 0  &&  this_frame != ei_local->current_page && (this_frame!=0x0 || rxing_page!=0xFF))
			printk(KERN_ERR "%s: mismatched read page pointers %2x vs %2x.\n",
				   dev->name, this_frame, ei_local->current_page);

		if (this_frame == rxing_page)	/* Read all the frames? */
			break;				/* Done for now */

		current_offset = this_frame << 8;
		ei_get_8390_hdr(dev, &rx_frame, this_frame);

		pkt_len = rx_frame.count - sizeof(struct e8390_pkt_hdr);
		pkt_stat = rx_frame.status;

		next_frame = this_frame + 1 + ((pkt_len+4)>>8);

		/* Check for bogosity warned by 3c503 book: the status byte is never
		   written.  This happened a lot during testing! This code should be
		   cleaned up someday. */
		if (rx_frame.next != next_frame
			&& rx_frame.next != next_frame + 1
			&& rx_frame.next != next_frame - num_rx_pages
			&& rx_frame.next != next_frame + 1 - num_rx_pages) {
			ei_local->current_page = rxing_page;
			ei_outb(ei_local->current_page-1, e8390_base+EN0_BOUNDARY);
			dev->stats.rx_errors++;
			continue;
		}

		if (pkt_len < 60  ||  pkt_len > 1518)
		{
			if (ei_debug)
				printk(KERN_DEBUG "%s: bogus packet size: %d, status=%#2x nxpg=%#2x.\n",
					   dev->name, rx_frame.count, rx_frame.status,
					   rx_frame.next);
			dev->stats.rx_errors++;
			dev->stats.rx_length_errors++;
		}
		 else if ((pkt_stat & 0x0F) == ENRSR_RXOK)
		{
			struct sk_buff *skb;

			skb = dev_alloc_skb(pkt_len+2);
			if (skb == NULL)
			{
				if (ei_debug > 1)
					printk(KERN_DEBUG "%s: Couldn't allocate a sk_buff of size %d.\n",
						   dev->name, pkt_len);
				dev->stats.rx_dropped++;
				break;
			}
			else
			{
				skb_reserve(skb,2);	/* IP headers on 16 byte boundaries */
				skb_put(skb, pkt_len);	/* Make room */
				ei_block_input(dev, pkt_len, skb, current_offset + sizeof(rx_frame));
				skb->protocol=eth_type_trans(skb,dev);
				netif_rx(skb);
				dev->last_rx = jiffies;
				dev->stats.rx_packets++;
				dev->stats.rx_bytes += pkt_len;
				if (pkt_stat & ENRSR_PHY)
					dev->stats.multicast++;
			}
		}
		else
		{
			if (ei_debug)
				printk(KERN_DEBUG "%s: bogus packet: status=%#2x nxpg=%#2x size=%d\n",
					   dev->name, rx_frame.status, rx_frame.next,
					   rx_frame.count);
			dev->stats.rx_errors++;
			/* NB: The NIC counts CRC, frame and missed errors. */
			if (pkt_stat & ENRSR_FO)
				dev->stats.rx_fifo_errors++;
		}
		next_frame = rx_frame.next;

		/* This _should_ never happen: it's here for avoiding bad clones. */
		if (next_frame >= ei_local->stop_page) {
			printk("%s: next frame inconsistency, %#2x\n", dev->name,
				   next_frame);
			next_frame = ei_local->rx_start_page;
		}
		ei_local->current_page = next_frame;
		ei_outb_p(next_frame-1, e8390_base+EN0_BOUNDARY);
	}

	/* We used to also ack ENISR_OVER here, but that would sometimes mask
	   a real overrun, leaving the 8390 in a stopped state with rec'vr off. */
	ei_outb_p(ENISR_RX+ENISR_RX_ERR, e8390_base+EN0_ISR);
	return;
}

/**
 * ei_rx_overrun - handle receiver overrun
 * @dev: network device which threw exception
 *
 * We have a receiver overrun: we have to kick the 8390 to get it started
 * again. Problem is that you have to kick it exactly as NS prescribes in
 * the updated datasheets, or "the NIC may act in an unpredictable manner."
 * This includes causing "the NIC to defer indefinitely when it is stopped
 * on a busy network."  Ugh.
 * Called with lock held. Don't call this with the interrupts off or your
 * computer will hate you - it takes 10ms or so.
 */

static void ei_rx_overrun(struct net_device *dev)
{
	unsigned long e8390_base = dev->base_addr;
	unsigned char was_txing, must_resend = 0;
	/* ei_local is used on some platforms via the EI_SHIFT macro */
	struct ei_device *ei_local __maybe_unused = netdev_priv(dev);

	/*
	 * Record whether a Tx was in progress and then issue the
	 * stop command.
	 */
	was_txing = ei_inb_p(e8390_base+E8390_CMD) & E8390_TRANS;
	ei_outb_p(E8390_NODMA+E8390_PAGE0+E8390_STOP, e8390_base+E8390_CMD);

	if (ei_debug > 1)
		printk(KERN_DEBUG "%s: Receiver overrun.\n", dev->name);
	dev->stats.rx_over_errors++;

	/*
	 * Wait a full Tx time (1.2ms) + some guard time, NS says 1.6ms total.
	 * Early datasheets said to poll the reset bit, but now they say that
	 * it "is not a reliable indicator and subsequently should be ignored."
	 * We wait at least 10ms.
	 */

	mdelay(10);

	/*
	 * Reset RBCR[01] back to zero as per magic incantation.
	 */
	ei_outb_p(0x00, e8390_base+EN0_RCNTLO);
	ei_outb_p(0x00, e8390_base+EN0_RCNTHI);

	/*
	 * See if any Tx was interrupted or not. According to NS, this
	 * step is vital, and skipping it will cause no end of havoc.
	 */

	if (was_txing)
	{
		unsigned char tx_completed = ei_inb_p(e8390_base+EN0_ISR) & (ENISR_TX+ENISR_TX_ERR);
		if (!tx_completed)
			must_resend = 1;
	}

	/*
	 * Have to enter loopback mode and then restart the NIC before
	 * you are allowed to slurp packets up off the ring.
	 */
	ei_outb_p(E8390_TXOFF, e8390_base + EN0_TXCR);
	ei_outb_p(E8390_NODMA + E8390_PAGE0 + E8390_START, e8390_base + E8390_CMD);

	/*
	 * Clear the Rx ring of all the debris, and ack the interrupt.
	 */
	ei_receive(dev);
	ei_outb_p(ENISR_OVER, e8390_base+EN0_ISR);

	/*
	 * Leave loopback mode, and resend any packet that got stopped.
	 */
	ei_outb_p(E8390_TXCONFIG, e8390_base + EN0_TXCR);
	if (must_resend)
    		ei_outb_p(E8390_NODMA + E8390_PAGE0 + E8390_START + E8390_TRANS, e8390_base + E8390_CMD);
}

/*
 *	Collect the stats. This is called unlocked and from several contexts.
 */

static struct net_device_stats *get_stats(struct net_device *dev)
{
	unsigned long ioaddr = dev->base_addr;
	struct ei_device *ei_local = (struct ei_device *) netdev_priv(dev);
	unsigned long flags;

	/* If the card is stopped, just return the present stats. */
	if (!netif_running(dev))
		return &dev->stats;

	spin_lock_irqsave(&ei_local->page_lock,flags);
	/* Read the counter registers, assuming we are in page 0. */
	dev->stats.rx_frame_errors += ei_inb_p(ioaddr + EN0_COUNTER0);
	dev->stats.rx_crc_errors   += ei_inb_p(ioaddr + EN0_COUNTER1);
	dev->stats.rx_missed_errors+= ei_inb_p(ioaddr + EN0_COUNTER2);
	spin_unlock_irqrestore(&ei_local->page_lock, flags);

	return &dev->stats;
}

/*
 * Form the 64 bit 8390 multicast table from the linked list of addresses
 * associated with this dev structure.
 */

static inline void make_mc_bits(u8 *bits, struct net_device *dev)
{
	struct dev_mc_list *dmi;

	for (dmi=dev->mc_list; dmi; dmi=dmi->next)
	{
		u32 crc;
		if (dmi->dmi_addrlen != ETH_ALEN)
		{
			printk(KERN_INFO "%s: invalid multicast address length given.\n", dev->name);
			continue;
		}
		crc = ether_crc(ETH_ALEN, dmi->dmi_addr);
		/*
		 * The 8390 uses the 6 most significant bits of the
		 * CRC to index the multicast table.
		 */
		bits[crc>>29] |= (1<<((crc>>26)&7));
	}
}

/**
 * do_set_multicast_list - set/clear multicast filter
 * @dev: net device for which multicast filter is adjusted
 *
 *	Set or clear the multicast filter for this adaptor. May be called
 *	from a BH in 2.1.x. Must be called with lock held.
 */

static void do_set_multicast_list(struct net_device *dev)
{
	unsigned long e8390_base = dev->base_addr;
	int i;
	struct ei_device *ei_local = (struct ei_device*)netdev_priv(dev);

	if (!(dev->flags&(IFF_PROMISC|IFF_ALLMULTI)))
	{
		memset(ei_local->mcfilter, 0, 8);
		if (dev->mc_list)
			make_mc_bits(ei_local->mcfilter, dev);
	}
	else
		memset(ei_local->mcfilter, 0xFF, 8);	/* mcast set to accept-all */

	/*
	 * DP8390 manuals don't specify any magic sequence for altering
	 * the multicast regs on an already running card. To be safe, we
	 * ensure multicast mode is off prior to loading up the new hash
	 * table. If this proves to be not enough, we can always resort
	 * to stopping the NIC, loading the table and then restarting.
	 *
	 * Bug Alert!  The MC regs on the SMC 83C690 (SMC Elite and SMC
	 * Elite16) appear to be write-only. The NS 8390 data sheet lists
	 * them as r/w so this is a bug.  The SMC 83C790 (SMC Ultra and
	 * Ultra32 EISA) appears to have this bug fixed.
	 */

	if (netif_running(dev))
		ei_outb_p(E8390_RXCONFIG, e8390_base + EN0_RXCR);
	ei_outb_p(E8390_NODMA + E8390_PAGE1, e8390_base + E8390_CMD);
	for(i = 0; i < 8; i++)
	{
		ei_outb_p(ei_local->mcfilter[i], e8390_base + EN1_MULT_SHIFT(i));
#ifndef BUG_83C690
		if(ei_inb_p(e8390_base + EN1_MULT_SHIFT(i))!=ei_local->mcfilter[i])
			printk(KERN_ERR "Multicast filter read/write mismap %d\n",i);
#endif
	}
	ei_outb_p(E8390_NODMA + E8390_PAGE0, e8390_base + E8390_CMD);

  	if(dev->flags&IFF_PROMISC)
  		ei_outb_p(E8390_RXCONFIG | 0x18, e8390_base + EN0_RXCR);
	else if(dev->flags&IFF_ALLMULTI || dev->mc_list)
  		ei_outb_p(E8390_RXCONFIG | 0x08, e8390_base + EN0_RXCR);
  	else
  		ei_outb_p(E8390_RXCONFIG, e8390_base + EN0_RXCR);
 }

/*
 *	Called without lock held. This is invoked from user context and may
 *	be parallel to just about everything else. Its also fairly quick and
 *	not called too often. Must protect against both bh and irq users
 */

static void set_multicast_list(struct net_device *dev)
{
	unsigned long flags;
	struct ei_device *ei_local = (struct ei_device*)netdev_priv(dev);

	spin_lock_irqsave(&ei_local->page_lock, flags);
	do_set_multicast_list(dev);
	spin_unlock_irqrestore(&ei_local->page_lock, flags);
}

/**
 * ethdev_setup - init rest of 8390 device struct
 * @dev: network device structure to init
 *
 * Initialize the rest of the 8390 device structure.  Do NOT __init
 * this, as it is used by 8390 based modular drivers too.
 */

static void ethdev_setup(struct net_device *dev)
{
	struct ei_device *ei_local = (struct ei_device *) netdev_priv(dev);
	if (ei_debug > 1)
		printk(version);

	dev->hard_start_xmit = &ei_start_xmit;
	dev->get_stats	= get_stats;
	dev->set_multicast_list = &set_multicast_list;

	ether_setup(dev);

	spin_lock_init(&ei_local->page_lock);
}

/**
 * alloc_ei_netdev - alloc_etherdev counterpart for 8390
 * @size: extra bytes to allocate
 *
 * Allocate 8390-specific net_device.
 */
static struct net_device *____alloc_ei_netdev(int size)
{
	return alloc_netdev(sizeof(struct ei_device) + size, "eth%d",
				ethdev_setup);
}




/* This page of functions should be 8390 generic */
/* Follow National Semi's recommendations for initializing the "NIC". */

/**
 * NS8390_init - initialize 8390 hardware
 * @dev: network device to initialize
 * @startp: boolean.  non-zero value to initiate chip processing
 *
 *	Must be called with lock held.
 */

static void __NS8390_init(struct net_device *dev, int startp)
{
	unsigned long e8390_base = dev->base_addr;
	struct ei_device *ei_local = (struct ei_device *) netdev_priv(dev);
	int i;
	int endcfg = ei_local->word16
	    ? (0x48 | ENDCFG_WTS | (ei_local->bigendian ? ENDCFG_BOS : 0))
	    : 0x48;

	if(sizeof(struct e8390_pkt_hdr)!=4)
    		panic("8390.c: header struct mispacked\n");
	/* Follow National Semi's recommendations for initing the DP83902. */
	ei_outb_p(E8390_NODMA+E8390_PAGE0+E8390_STOP, e8390_base+E8390_CMD); /* 0x21 */
	ei_outb_p(endcfg, e8390_base + EN0_DCFG);	/* 0x48 or 0x49 */
	/* Clear the remote byte count registers. */
	ei_outb_p(0x00,  e8390_base + EN0_RCNTLO);
	ei_outb_p(0x00,  e8390_base + EN0_RCNTHI);
	/* Set to monitor and loopback mode -- this is vital!. */
	ei_outb_p(E8390_RXOFF, e8390_base + EN0_RXCR); /* 0x20 */
	ei_outb_p(E8390_TXOFF, e8390_base + EN0_TXCR); /* 0x02 */
	/* Set the transmit page and receive ring. */
	ei_outb_p(ei_local->tx_start_page, e8390_base + EN0_TPSR);
	ei_local->tx1 = ei_local->tx2 = 0;
	ei_outb_p(ei_local->rx_start_page, e8390_base + EN0_STARTPG);
	ei_outb_p(ei_local->stop_page-1, e8390_base + EN0_BOUNDARY);	/* 3c503 says 0x3f,NS0x26*/
	ei_local->current_page = ei_local->rx_start_page;		/* assert boundary+1 */
	ei_outb_p(ei_local->stop_page, e8390_base + EN0_STOPPG);
	/* Clear the pending interrupts and mask. */
	ei_outb_p(0xFF, e8390_base + EN0_ISR);
	ei_outb_p(0x00,  e8390_base + EN0_IMR);

	/* Copy the station address into the DS8390 registers. */

	ei_outb_p(E8390_NODMA + E8390_PAGE1 + E8390_STOP, e8390_base+E8390_CMD); /* 0x61 */
	for(i = 0; i < 6; i++)
	{
		ei_outb_p(dev->dev_addr[i], e8390_base + EN1_PHYS_SHIFT(i));
		if (ei_debug > 1 && ei_inb_p(e8390_base + EN1_PHYS_SHIFT(i))!=dev->dev_addr[i])
			printk(KERN_ERR "Hw. address read/write mismap %d\n",i);
	}

	ei_outb_p(ei_local->rx_start_page, e8390_base + EN1_CURPAG);
	ei_outb_p(E8390_NODMA+E8390_PAGE0+E8390_STOP, e8390_base+E8390_CMD);

	netif_start_queue(dev);
	ei_local->tx1 = ei_local->tx2 = 0;
	ei_local->txing = 0;

	if (startp)
	{
		ei_outb_p(0xff,  e8390_base + EN0_ISR);
		ei_outb_p(ENISR_ALL,  e8390_base + EN0_IMR);
		ei_outb_p(E8390_NODMA+E8390_PAGE0+E8390_START, e8390_base+E8390_CMD);
		ei_outb_p(E8390_TXCONFIG, e8390_base + EN0_TXCR); /* xmit on. */
		/* 3c503 TechMan says rxconfig only after the NIC is started. */
		ei_outb_p(E8390_RXCONFIG, e8390_base + EN0_RXCR); /* rx on,  */
		do_set_multicast_list(dev);	/* (re)load the mcast table */
	}
}

/* Trigger a transmit start, assuming the length is valid.
   Always called with the page lock held */

static void NS8390_trigger_send(struct net_device *dev, unsigned int length,
								int start_page)
{
	unsigned long e8390_base = dev->base_addr;
 	struct ei_device *ei_local __attribute((unused)) = (struct ei_device *) netdev_priv(dev);

	ei_outb_p(E8390_NODMA+E8390_PAGE0, e8390_base+E8390_CMD);

	if (ei_inb_p(e8390_base + E8390_CMD) & E8390_TRANS)
	{
		printk(KERN_WARNING "%s: trigger_send() called with the transmitter busy.\n",
			dev->name);
		return;
	}
	ei_outb_p(length & 0xff, e8390_base + EN0_TCNTLO);
	ei_outb_p(length >> 8, e8390_base + EN0_TCNTHI);
	ei_outb_p(start_page, e8390_base + EN0_TPSR);
	ei_outb_p(E8390_NODMA+E8390_TRANS+E8390_START, e8390_base+E8390_CMD);
}
