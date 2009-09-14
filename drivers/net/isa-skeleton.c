/* isa-skeleton.c: A network driver outline for linux.
 *
 *	Written 1993-94 by Donald Becker.
 *
 *	Copyright 1993 United States Government as represented by the
 *	Director, National Security Agency.
 *
 *	This software may be used and distributed according to the terms
 *	of the GNU General Public License, incorporated herein by reference.
 *
 *	The author may be reached as becker@scyld.com, or C/O
 *	Scyld Computing Corporation
 *	410 Severn Ave., Suite 210
 *	Annapolis MD 21403
 *
 *	This file is an outline for writing a network device driver for the
 *	the Linux operating system.
 *
 *	To write (or understand) a driver, have a look at the "loopback.c" file to
 *	get a feel of what is going on, and then use the code below as a skeleton
 *	for the new driver.
 *
 */

static const char *version =
	"isa-skeleton.c:v1.51 9/24/94 Donald Becker (becker@cesdis.gsfc.nasa.gov)\n";

/*
 *  Sources:
 *	List your sources of programming information to document that
 *	the driver is your own creation, and give due credit to others
 *	that contributed to the work. Remember that GNU project code
 *	cannot use proprietary or trade secret information. Interface
 *	definitions are generally considered non-copyrightable to the
 *	extent that the same names and structures must be used to be
 *	compatible.
 *
 *	Finally, keep in mind that the Linux kernel is has an API, not
 *	ABI. Proprietary object-code-only distributions are not permitted
 *	under the GPL.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/fcntl.h>
#include <linux/interrupt.h>
#include <linux/ioport.h>
#include <linux/in.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/spinlock.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include <linux/bitops.h>

#include <asm/system.h>
#include <asm/io.h>
#include <asm/dma.h>

/*
 * The name of the card. Is used for messages and in the requests for
 * io regions, irqs and dma channels
 */
static const char* cardname = "netcard";

/* First, a few definitions that the brave might change. */

/* A zero-terminated list of I/O addresses to be probed. */
static unsigned int netcard_portlist[] __initdata =
   { 0x200, 0x240, 0x280, 0x2C0, 0x300, 0x320, 0x340, 0};

/* use 0 for production, 1 for verification, >2 for debug */
#ifndef NET_DEBUG
#define NET_DEBUG 2
#endif
static unsigned int net_debug = NET_DEBUG;

/* The number of low I/O ports used by the ethercard. */
#define NETCARD_IO_EXTENT	32

#define MY_TX_TIMEOUT  ((400*HZ)/1000)

/* Information that need to be kept for each board. */
struct net_local {
	struct net_device_stats stats;
	long open_time;			/* Useless example local info. */

	/* Tx control lock.  This protects the transmit buffer ring
	 * state along with the "tx full" state of the driver.  This
	 * means all netif_queue flow control actions are protected
	 * by this lock as well.
	 */
	spinlock_t lock;
};

/* The station (ethernet) address prefix, used for IDing the board. */
#define SA_ADDR0 0x00
#define SA_ADDR1 0x42
#define SA_ADDR2 0x65

/* Index to functions, as function prototypes. */

static int	netcard_probe1(struct net_device *dev, int ioaddr);
static int	net_open(struct net_device *dev);
static int	net_send_packet(struct sk_buff *skb, struct net_device *dev);
static irqreturn_t net_interrupt(int irq, void *dev_id);
static void	net_rx(struct net_device *dev);
static int	net_close(struct net_device *dev);
static struct	net_device_stats *net_get_stats(struct net_device *dev);
static void	set_multicast_list(struct net_device *dev);
static void     net_tx_timeout(struct net_device *dev);


/* Example routines you must write ;->. */
#define tx_done(dev) 1
static void	hardware_send_packet(short ioaddr, char *buf, int length);
static void 	chipset_init(struct net_device *dev, int startp);

/*
 * Check for a network adaptor of this type, and return '0' iff one exists.
 * If dev->base_addr == 0, probe all likely locations.
 * If dev->base_addr == 1, always return failure.
 * If dev->base_addr == 2, allocate space for the device and return success
 * (detachable devices only).
 */
static int __init do_netcard_probe(struct net_device *dev)
{
	int i;
	int base_addr = dev->base_addr;
	int irq = dev->irq;

	if (base_addr > 0x1ff)    /* Check a single specified location. */
		return netcard_probe1(dev, base_addr);
	else if (base_addr != 0)  /* Don't probe at all. */
		return -ENXIO;

	for (i = 0; netcard_portlist[i]; i++) {
		int ioaddr = netcard_portlist[i];
		if (netcard_probe1(dev, ioaddr) == 0)
			return 0;
		dev->irq = irq;
	}

	return -ENODEV;
}

static void cleanup_card(struct net_device *dev)
{
#ifdef jumpered_dma
	free_dma(dev->dma);
#endif
#ifdef jumpered_interrupts
	free_irq(dev->irq, dev);
#endif
	release_region(dev->base_addr, NETCARD_IO_EXTENT);
}

#ifndef MODULE
struct net_device * __init netcard_probe(int unit)
{
	struct net_device *dev = alloc_etherdev(sizeof(struct net_local));
	int err;

	if (!dev)
		return ERR_PTR(-ENOMEM);

	sprintf(dev->name, "eth%d", unit);
	netdev_boot_setup_check(dev);

	err = do_netcard_probe(dev);
	if (err)
		goto out;
	return dev;
out:
	free_netdev(dev);
	return ERR_PTR(err);
}
#endif

static const struct net_device_ops netcard_netdev_ops = {
	.ndo_open		= net_open,
	.ndo_stop		= net_close,
	.ndo_start_xmit		= net_send_packet,
	.ndo_get_stats		= net_get_stats,
	.ndo_set_multicast_list	= set_multicast_list,
	.ndo_tx_timeout		= net_tx_timeout,
	.ndo_validate_addr	= eth_validate_addr,
	.ndo_set_mac_address	= eth_mac_addr,
	.ndo_change_mtu		= eth_change_mtu,
};

/*
 * This is the real probe routine. Linux has a history of friendly device
 * probes on the ISA bus. A good device probes avoids doing writes, and
 * verifies that the correct device exists and functions.
 */
static int __init netcard_probe1(struct net_device *dev, int ioaddr)
{
	struct net_local *np;
	static unsigned version_printed;
	int i;
	int err = -ENODEV;

	/* Grab the region so that no one else tries to probe our ioports. */
	if (!request_region(ioaddr, NETCARD_IO_EXTENT, cardname))
		return -EBUSY;

	/*
	 * For ethernet adaptors the first three octets of the station address
	 * contains the manufacturer's unique code. That might be a good probe
	 * method. Ideally you would add additional checks.
	 */
	if (inb(ioaddr + 0) != SA_ADDR0
		||	 inb(ioaddr + 1) != SA_ADDR1
		||	 inb(ioaddr + 2) != SA_ADDR2)
		goto out;

	if (net_debug  &&  version_printed++ == 0)
		printk(KERN_DEBUG "%s", version);

	printk(KERN_INFO "%s: %s found at %#3x, ", dev->name, cardname, ioaddr);

	/* Fill in the 'dev' fields. */
	dev->base_addr = ioaddr;

	/* Retrieve and print the ethernet address. */
	for (i = 0; i < 6; i++)
		dev->dev_addr[i] = inb(ioaddr + i);

	printk("%pM", dev->dev_addr);

	err = -EAGAIN;
#ifdef jumpered_interrupts
	/*
	 * If this board has jumpered interrupts, allocate the interrupt
	 * vector now. There is no point in waiting since no other device
	 * can use the interrupt, and this marks the irq as busy. Jumpered
	 * interrupts are typically not reported by the boards, and we must
	 * used autoIRQ to find them.
	 */

	if (dev->irq == -1)
		;	/* Do nothing: a user-level program will set it. */
	else if (dev->irq < 2) {	/* "Auto-IRQ" */
		unsigned long irq_mask = probe_irq_on();
		/* Trigger an interrupt here. */

		dev->irq = probe_irq_off(irq_mask);
		if (net_debug >= 2)
			printk(" autoirq is %d", dev->irq);
	} else if (dev->irq == 2)
		/*
		 * Fixup for users that don't know that IRQ 2 is really
		 * IRQ9, or don't know which one to set.
		 */
		dev->irq = 9;

	{
		int irqval = request_irq(dev->irq, &net_interrupt, 0, cardname, dev);
		if (irqval) {
			printk("%s: unable to get IRQ %d (irqval=%d).\n",
				   dev->name, dev->irq, irqval);
			goto out;
		}
	}
#endif	/* jumpered interrupt */
#ifdef jumpered_dma
	/*
	 * If we use a jumpered DMA channel, that should be probed for and
	 * allocated here as well. See lance.c for an example.
	 */
	if (dev->dma == 0) {
		if (request_dma(dev->dma, cardname)) {
			printk("DMA %d allocation failed.\n", dev->dma);
			goto out1;
		} else
			printk(", assigned DMA %d.\n", dev->dma);
	} else {
		short dma_status, new_dma_status;

		/* Read the DMA channel status registers. */
		dma_status = ((inb(DMA1_STAT_REG) >> 4) & 0x0f) |
			(inb(DMA2_STAT_REG) & 0xf0);
		/* Trigger a DMA request, perhaps pause a bit. */
		outw(0x1234, ioaddr + 8);
		/* Re-read the DMA status registers. */
		new_dma_status = ((inb(DMA1_STAT_REG) >> 4) & 0x0f) |
			(inb(DMA2_STAT_REG) & 0xf0);
		/*
		 * Eliminate the old and floating requests,
		 * and DMA4 the cascade.
		 */
		new_dma_status ^= dma_status;
		new_dma_status &= ~0x10;
		for (i = 7; i > 0; i--)
			if (test_bit(i, &new_dma_status)) {
				dev->dma = i;
				break;
			}
		if (i <= 0) {
			printk("DMA probe failed.\n");
			goto out1;
		}
		if (request_dma(dev->dma, cardname)) {
			printk("probed DMA %d allocation failed.\n", dev->dma);
			goto out1;
		}
	}
#endif	/* jumpered DMA */

	np = netdev_priv(dev);
	spin_lock_init(&np->lock);

        dev->netdev_ops		= &netcard_netdev_ops;
        dev->watchdog_timeo	= MY_TX_TIMEOUT;

	err = register_netdev(dev);
	if (err)
		goto out2;
	return 0;
out2:
#ifdef jumpered_dma
	free_dma(dev->dma);
#endif
out1:
#ifdef jumpered_interrupts
	free_irq(dev->irq, dev);
#endif
out:
	release_region(base_addr, NETCARD_IO_EXTENT);
	return err;
}

static void net_tx_timeout(struct net_device *dev)
{
	struct net_local *np = netdev_priv(dev);

	printk(KERN_WARNING "%s: transmit timed out, %s?\n", dev->name,
	       tx_done(dev) ? "IRQ conflict" : "network cable problem");

	/* Try to restart the adaptor. */
	chipset_init(dev, 1);

	np->stats.tx_errors++;

	/* If we have space available to accept new transmit
	 * requests, wake up the queueing layer.  This would
	 * be the case if the chipset_init() call above just
	 * flushes out the tx queue and empties it.
	 *
	 * If instead, the tx queue is retained then the
	 * netif_wake_queue() call should be placed in the
	 * TX completion interrupt handler of the driver instead
	 * of here.
	 */
	if (!tx_full(dev))
		netif_wake_queue(dev);
}

/*
 * Open/initialize the board. This is called (in the current kernel)
 * sometime after booting when the 'ifconfig' program is run.
 *
 * This routine should set everything up anew at each open, even
 * registers that "should" only need to be set once at boot, so that
 * there is non-reboot way to recover if something goes wrong.
 */
static int
net_open(struct net_device *dev)
{
	struct net_local *np = netdev_priv(dev);
	int ioaddr = dev->base_addr;
	/*
	 * This is used if the interrupt line can turned off (shared).
	 * See 3c503.c for an example of selecting the IRQ at config-time.
	 */
	if (request_irq(dev->irq, &net_interrupt, 0, cardname, dev)) {
		return -EAGAIN;
	}
	/*
	 * Always allocate the DMA channel after the IRQ,
	 * and clean up on failure.
	 */
	if (request_dma(dev->dma, cardname)) {
		free_irq(dev->irq, dev);
		return -EAGAIN;
	}

	/* Reset the hardware here. Don't forget to set the station address. */
	chipset_init(dev, 1);
	outb(0x00, ioaddr);
	np->open_time = jiffies;

	/* We are now ready to accept transmit requeusts from
	 * the queueing layer of the networking.
	 */
	netif_start_queue(dev);

	return 0;
}

/* This will only be invoked if your driver is _not_ in XOFF state.
 * What this means is that you need not check it, and that this
 * invariant will hold if you make sure that the netif_*_queue()
 * calls are done at the proper times.
 */
static int net_send_packet(struct sk_buff *skb, struct net_device *dev)
{
	struct net_local *np = netdev_priv(dev);
	int ioaddr = dev->base_addr;
	short length = ETH_ZLEN < skb->len ? skb->len : ETH_ZLEN;
	unsigned char *buf = skb->data;

	/* If some error occurs while trying to transmit this
	 * packet, you should return '1' from this function.
	 * In such a case you _may not_ do anything to the
	 * SKB, it is still owned by the network queueing
	 * layer when an error is returned.  This means you
	 * may not modify any SKB fields, you may not free
	 * the SKB, etc.
	 */

#if TX_RING
	/* This is the most common case for modern hardware.
	 * The spinlock protects this code from the TX complete
	 * hardware interrupt handler.  Queue flow control is
	 * thus managed under this lock as well.
	 */
	unsigned long flags;
	spin_lock_irqsave(&np->lock, flags);

	add_to_tx_ring(np, skb, length);
	dev->trans_start = jiffies;

	/* If we just used up the very last entry in the
	 * TX ring on this device, tell the queueing
	 * layer to send no more.
	 */
	if (tx_full(dev))
		netif_stop_queue(dev);

	/* When the TX completion hw interrupt arrives, this
	 * is when the transmit statistics are updated.
	 */

	spin_unlock_irqrestore(&np->lock, flags);
#else
	/* This is the case for older hardware which takes
	 * a single transmit buffer at a time, and it is
	 * just written to the device via PIO.
	 *
	 * No spin locking is needed since there is no TX complete
	 * event.  If by chance your card does have a TX complete
	 * hardware IRQ then you may need to utilize np->lock here.
	 */
	hardware_send_packet(ioaddr, buf, length);
	np->stats.tx_bytes += skb->len;

	dev->trans_start = jiffies;

	/* You might need to clean up and record Tx statistics here. */
	if (inw(ioaddr) == /*RU*/81)
		np->stats.tx_aborted_errors++;
	dev_kfree_skb (skb);
#endif

	return NETDEV_TX_OK;
}

#if TX_RING
/* This handles TX complete events posted by the device
 * via interrupts.
 */
void net_tx(struct net_device *dev)
{
	struct net_local *np = netdev_priv(dev);
	int entry;

	/* This protects us from concurrent execution of
	 * our dev->hard_start_xmit function above.
	 */
	spin_lock(&np->lock);

	entry = np->tx_old;
	while (tx_entry_is_sent(np, entry)) {
		struct sk_buff *skb = np->skbs[entry];

		np->stats.tx_bytes += skb->len;
		dev_kfree_skb_irq (skb);

		entry = next_tx_entry(np, entry);
	}
	np->tx_old = entry;

	/* If we had stopped the queue due to a "tx full"
	 * condition, and space has now been made available,
	 * wake up the queue.
	 */
	if (netif_queue_stopped(dev) && ! tx_full(dev))
		netif_wake_queue(dev);

	spin_unlock(&np->lock);
}
#endif

/*
 * The typical workload of the driver:
 * Handle the network interface interrupts.
 */
static irqreturn_t net_interrupt(int irq, void *dev_id)
{
	struct net_device *dev = dev_id;
	struct net_local *np;
	int ioaddr, status;
	int handled = 0;

	ioaddr = dev->base_addr;

	np = netdev_priv(dev);
	status = inw(ioaddr + 0);

	if (status == 0)
		goto out;
	handled = 1;

	if (status & RX_INTR) {
		/* Got a packet(s). */
		net_rx(dev);
	}
#if TX_RING
	if (status & TX_INTR) {
		/* Transmit complete. */
		net_tx(dev);
		np->stats.tx_packets++;
		netif_wake_queue(dev);
	}
#endif
	if (status & COUNTERS_INTR) {
		/* Increment the appropriate 'localstats' field. */
		np->stats.tx_window_errors++;
	}
out:
	return IRQ_RETVAL(handled);
}

/* We have a good packet(s), get it/them out of the buffers. */
static void
net_rx(struct net_device *dev)
{
	struct net_local *lp = netdev_priv(dev);
	int ioaddr = dev->base_addr;
	int boguscount = 10;

	do {
		int status = inw(ioaddr);
		int pkt_len = inw(ioaddr);

		if (pkt_len == 0)		/* Read all the frames? */
			break;			/* Done for now */

		if (status & 0x40) {	/* There was an error. */
			lp->stats.rx_errors++;
			if (status & 0x20) lp->stats.rx_frame_errors++;
			if (status & 0x10) lp->stats.rx_over_errors++;
			if (status & 0x08) lp->stats.rx_crc_errors++;
			if (status & 0x04) lp->stats.rx_fifo_errors++;
		} else {
			/* Malloc up new buffer. */
			struct sk_buff *skb;

			lp->stats.rx_bytes+=pkt_len;

			skb = dev_alloc_skb(pkt_len);
			if (skb == NULL) {
				printk(KERN_NOTICE "%s: Memory squeeze, dropping packet.\n",
					   dev->name);
				lp->stats.rx_dropped++;
				break;
			}
			skb->dev = dev;

			/* 'skb->data' points to the start of sk_buff data area. */
			memcpy(skb_put(skb,pkt_len), (void*)dev->rmem_start,
				   pkt_len);
			/* or */
			insw(ioaddr, skb->data, (pkt_len + 1) >> 1);

			netif_rx(skb);
			lp->stats.rx_packets++;
			lp->stats.rx_bytes += pkt_len;
		}
	} while (--boguscount);

	return;
}

/* The inverse routine to net_open(). */
static int
net_close(struct net_device *dev)
{
	struct net_local *lp = netdev_priv(dev);
	int ioaddr = dev->base_addr;

	lp->open_time = 0;

	netif_stop_queue(dev);

	/* Flush the Tx and disable Rx here. */

	disable_dma(dev->dma);

	/* If not IRQ or DMA jumpered, free up the line. */
	outw(0x00, ioaddr+0);	/* Release the physical interrupt line. */

	free_irq(dev->irq, dev);
	free_dma(dev->dma);

	/* Update the statistics here. */

	return 0;

}

/*
 * Get the current statistics.
 * This may be called with the card open or closed.
 */
static struct net_device_stats *net_get_stats(struct net_device *dev)
{
	struct net_local *lp = netdev_priv(dev);
	short ioaddr = dev->base_addr;

	/* Update the statistics from the device registers. */
	lp->stats.rx_missed_errors = inw(ioaddr+1);
	return &lp->stats;
}

/*
 * Set or clear the multicast filter for this adaptor.
 * num_addrs == -1	Promiscuous mode, receive all packets
 * num_addrs == 0	Normal mode, clear multicast list
 * num_addrs > 0	Multicast mode, receive normal and MC packets,
 *			and do best-effort filtering.
 */
static void
set_multicast_list(struct net_device *dev)
{
	short ioaddr = dev->base_addr;
	if (dev->flags&IFF_PROMISC)
	{
		/* Enable promiscuous mode */
		outw(MULTICAST|PROMISC, ioaddr);
	}
	else if((dev->flags&IFF_ALLMULTI) || dev->mc_count > HW_MAX_ADDRS)
	{
		/* Disable promiscuous mode, use normal mode. */
		hardware_set_filter(NULL);

		outw(MULTICAST, ioaddr);
	}
	else if(dev->mc_count)
	{
		/* Walk the address list, and load the filter */
		hardware_set_filter(dev->mc_list);

		outw(MULTICAST, ioaddr);
	}
	else
		outw(0, ioaddr);
}

#ifdef MODULE

static struct net_device *this_device;
static int io = 0x300;
static int irq;
static int dma;
static int mem;
MODULE_LICENSE("GPL");

int init_module(void)
{
	struct net_device *dev;
	int result;

	if (io == 0)
		printk(KERN_WARNING "%s: You shouldn't use auto-probing with insmod!\n",
			   cardname);
	dev = alloc_etherdev(sizeof(struct net_local));
	if (!dev)
		return -ENOMEM;

	/* Copy the parameters from insmod into the device structure. */
	dev->base_addr = io;
	dev->irq       = irq;
	dev->dma       = dma;
	dev->mem_start = mem;
	if (do_netcard_probe(dev) == 0) {
		this_device = dev;
		return 0;
	}
	free_netdev(dev);
	return -ENXIO;
}

void
cleanup_module(void)
{
	unregister_netdev(this_device);
	cleanup_card(this_device);
	free_netdev(this_device);
}

#endif /* MODULE */
