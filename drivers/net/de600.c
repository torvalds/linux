static const char version[] = "de600.c: $Revision: 1.41-2.5 $,  Bjorn Ekwall (bj0rn@blox.se)\n";
/*
 *	de600.c
 *
 *	Linux driver for the D-Link DE-600 Ethernet pocket adapter.
 *
 *	Portions (C) Copyright 1993, 1994 by Bjorn Ekwall
 *	The Author may be reached as bj0rn@blox.se
 *
 *	Based on adapter information gathered from DE600.ASM by D-Link Inc.,
 *	as included on disk C in the v.2.11 of PC/TCP from FTP Software.
 *	For DE600.asm:
 *		Portions (C) Copyright 1990 D-Link, Inc.
 *		Copyright, 1988-1992, Russell Nelson, Crynwr Software
 *
 *	Adapted to the sample network driver core for linux,
 *	written by: Donald Becker <becker@super.org>
 *		(Now at <becker@scyld.com>)
 *
 **************************************************************/
/*
 *	This program is free software; you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation; either version 2, or (at your option)
 *	any later version.
 *
 *	This program is distributed in the hope that it will be useful,
 *	but WITHOUT ANY WARRANTY; without even the implied warranty of
 *	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *	GNU General Public License for more details.
 *
 *	You should have received a copy of the GNU General Public License
 *	along with this program; if not, write to the Free Software
 *	Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 **************************************************************/

/* Add more time here if your adapter won't work OK: */
#define DE600_SLOW_DOWN	udelay(delay_time)

/* use 0 for production, 1 for verification, >2 for debug */
#ifdef DE600_DEBUG
#define PRINTK(x) if (de600_debug >= 2) printk x
#else
#define DE600_DEBUG 0
#define PRINTK(x) /**/
#endif

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/fcntl.h>
#include <linux/string.h>
#include <linux/interrupt.h>
#include <linux/ioport.h>
#include <linux/in.h>
#include <asm/system.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/inet.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>

#include <asm/io.h>

#include "de600.h"

static unsigned int de600_debug = DE600_DEBUG;
module_param(de600_debug, int, 0);
MODULE_PARM_DESC(de600_debug, "DE-600 debug level (0-2)");

static unsigned int check_lost = 1;
module_param(check_lost, bool, 0);
MODULE_PARM_DESC(check_lost, "If set then check for unplugged de600");

static unsigned int delay_time = 10;
module_param(delay_time, int, 0);
MODULE_PARM_DESC(delay_time, "DE-600 deley on I/O in microseconds");


/*
 * D-Link driver variables:
 */

static volatile int		rx_page;

#define TX_PAGES 2
static volatile int		tx_fifo[TX_PAGES];
static volatile int		tx_fifo_in;
static volatile int		tx_fifo_out;
static volatile int		free_tx_pages = TX_PAGES;
static int			was_down;
static DEFINE_SPINLOCK(de600_lock);

static inline u8 de600_read_status(struct net_device *dev)
{
	u8 status;

	outb_p(STATUS, DATA_PORT);
	status = inb(STATUS_PORT);
	outb_p(NULL_COMMAND | HI_NIBBLE, DATA_PORT);

	return status;
}

static inline u8 de600_read_byte(unsigned char type, struct net_device *dev)
{
	/* dev used by macros */
	u8 lo;
	outb_p((type), DATA_PORT);
	lo = ((unsigned char)inb(STATUS_PORT)) >> 4;
	outb_p((type) | HI_NIBBLE, DATA_PORT);
	return ((unsigned char)inb(STATUS_PORT) & (unsigned char)0xf0) | lo;
}

/*
 * Open/initialize the board.  This is called (in the current kernel)
 * after booting when 'ifconfig <dev->name> $IP_ADDR' is run (in rc.inet1).
 *
 * This routine should set everything up anew at each open, even
 * registers that "should" only need to be set once at boot, so that
 * there is a non-reboot way to recover if something goes wrong.
 */

static int de600_open(struct net_device *dev)
{
	unsigned long flags;
	int ret = request_irq(DE600_IRQ, de600_interrupt, 0, dev->name, dev);
	if (ret) {
		printk(KERN_ERR "%s: unable to get IRQ %d\n", dev->name, DE600_IRQ);
		return ret;
	}
	spin_lock_irqsave(&de600_lock, flags);
	ret = adapter_init(dev);
	spin_unlock_irqrestore(&de600_lock, flags);
	return ret;
}

/*
 * The inverse routine to de600_open().
 */

static int de600_close(struct net_device *dev)
{
	select_nic();
	rx_page = 0;
	de600_put_command(RESET);
	de600_put_command(STOP_RESET);
	de600_put_command(0);
	select_prn();
	free_irq(DE600_IRQ, dev);
	return 0;
}

static inline void trigger_interrupt(struct net_device *dev)
{
	de600_put_command(FLIP_IRQ);
	select_prn();
	DE600_SLOW_DOWN;
	select_nic();
	de600_put_command(0);
}

/*
 * Copy a buffer to the adapter transmit page memory.
 * Start sending.
 */

static int de600_start_xmit(struct sk_buff *skb, struct net_device *dev)
{
	unsigned long flags;
	int	transmit_from;
	int	len;
	int	tickssofar;
	u8	*buffer = skb->data;
	int	i;

	if (free_tx_pages <= 0) {	/* Do timeouts, to avoid hangs. */
		tickssofar = jiffies - dev->trans_start;
		if (tickssofar < 5)
			return 1;
		/* else */
		printk(KERN_WARNING "%s: transmit timed out (%d), %s?\n", dev->name, tickssofar, "network cable problem");
		/* Restart the adapter. */
		spin_lock_irqsave(&de600_lock, flags);
		if (adapter_init(dev)) {
			spin_unlock_irqrestore(&de600_lock, flags);
			return 1;
		}
		spin_unlock_irqrestore(&de600_lock, flags);
	}

	/* Start real output */
	PRINTK(("de600_start_xmit:len=%d, page %d/%d\n", skb->len, tx_fifo_in, free_tx_pages));

	if ((len = skb->len) < RUNT)
		len = RUNT;

	spin_lock_irqsave(&de600_lock, flags);
	select_nic();
	tx_fifo[tx_fifo_in] = transmit_from = tx_page_adr(tx_fifo_in) - len;
	tx_fifo_in = (tx_fifo_in + 1) % TX_PAGES; /* Next free tx page */

	if(check_lost)
	{
		/* This costs about 40 instructions per packet... */
		de600_setup_address(NODE_ADDRESS, RW_ADDR);
		de600_read_byte(READ_DATA, dev);
		if (was_down || (de600_read_byte(READ_DATA, dev) != 0xde)) {
			if (adapter_init(dev)) {
				spin_unlock_irqrestore(&de600_lock, flags);
				return 1;
			}
		}
	}

	de600_setup_address(transmit_from, RW_ADDR);
	for (i = 0;  i < skb->len ; ++i, ++buffer)
		de600_put_byte(*buffer);
	for (; i < len; ++i)
		de600_put_byte(0);

	if (free_tx_pages-- == TX_PAGES) { /* No transmission going on */
		dev->trans_start = jiffies;
		netif_start_queue(dev); /* allow more packets into adapter */
		/* Send page and generate a faked interrupt */
		de600_setup_address(transmit_from, TX_ADDR);
		de600_put_command(TX_ENABLE);
	}
	else {
		if (free_tx_pages)
			netif_start_queue(dev);
		else
			netif_stop_queue(dev);
		select_prn();
	}
	spin_unlock_irqrestore(&de600_lock, flags);
	dev_kfree_skb(skb);
	return 0;
}

/*
 * The typical workload of the driver:
 * Handle the network interface interrupts.
 */

static irqreturn_t de600_interrupt(int irq, void *dev_id)
{
	struct net_device	*dev = dev_id;
	u8		irq_status;
	int		retrig = 0;
	int		boguscount = 0;

	spin_lock(&de600_lock);

	select_nic();
	irq_status = de600_read_status(dev);

	do {
		PRINTK(("de600_interrupt (%02X)\n", irq_status));

		if (irq_status & RX_GOOD)
			de600_rx_intr(dev);
		else if (!(irq_status & RX_BUSY))
			de600_put_command(RX_ENABLE);

		/* Any transmission in progress? */
		if (free_tx_pages < TX_PAGES)
			retrig = de600_tx_intr(dev, irq_status);
		else
			retrig = 0;

		irq_status = de600_read_status(dev);
	} while ( (irq_status & RX_GOOD) || ((++boguscount < 100) && retrig) );
	/*
	 * Yeah, it _looks_ like busy waiting, smells like busy waiting
	 * and I know it's not PC, but please, it will only occur once
	 * in a while and then only for a loop or so (< 1ms for sure!)
	 */

	/* Enable adapter interrupts */
	select_prn();
	if (retrig)
		trigger_interrupt(dev);
	spin_unlock(&de600_lock);
	return IRQ_HANDLED;
}

static int de600_tx_intr(struct net_device *dev, int irq_status)
{
	/*
	 * Returns 1 if tx still not done
	 */

	/* Check if current transmission is done yet */
	if (irq_status & TX_BUSY)
		return 1; /* tx not done, try again */

	/* else */
	/* If last transmission OK then bump fifo index */
	if (!(irq_status & TX_FAILED16)) {
		tx_fifo_out = (tx_fifo_out + 1) % TX_PAGES;
		++free_tx_pages;
		dev->stats.tx_packets++;
		netif_wake_queue(dev);
	}

	/* More to send, or resend last packet? */
	if ((free_tx_pages < TX_PAGES) || (irq_status & TX_FAILED16)) {
		dev->trans_start = jiffies;
		de600_setup_address(tx_fifo[tx_fifo_out], TX_ADDR);
		de600_put_command(TX_ENABLE);
		return 1;
	}
	/* else */

	return 0;
}

/*
 * We have a good packet, get it out of the adapter.
 */
static void de600_rx_intr(struct net_device *dev)
{
	struct sk_buff	*skb;
	int		i;
	int		read_from;
	int		size;
	unsigned char	*buffer;

	/* Get size of received packet */
	size = de600_read_byte(RX_LEN, dev);	/* low byte */
	size += (de600_read_byte(RX_LEN, dev) << 8);	/* high byte */
	size -= 4;	/* Ignore trailing 4 CRC-bytes */

	/* Tell adapter where to store next incoming packet, enable receiver */
	read_from = rx_page_adr();
	next_rx_page();
	de600_put_command(RX_ENABLE);

	if ((size < 32)  ||  (size > 1535)) {
		printk(KERN_WARNING "%s: Bogus packet size %d.\n", dev->name, size);
		if (size > 10000)
			adapter_init(dev);
		return;
	}

	skb = dev_alloc_skb(size+2);
	if (skb == NULL) {
		printk("%s: Couldn't allocate a sk_buff of size %d.\n", dev->name, size);
		return;
	}
	/* else */

	skb_reserve(skb,2);	/* Align */

	/* 'skb->data' points to the start of sk_buff data area. */
	buffer = skb_put(skb,size);

	/* copy the packet into the buffer */
	de600_setup_address(read_from, RW_ADDR);
	for (i = size; i > 0; --i, ++buffer)
		*buffer = de600_read_byte(READ_DATA, dev);

	skb->protocol=eth_type_trans(skb,dev);

	netif_rx(skb);

	/* update stats */
	dev->last_rx = jiffies;
	dev->stats.rx_packets++; /* count all receives */
	dev->stats.rx_bytes += size; /* count all received bytes */

	/*
	 * If any worth-while packets have been received, netif_rx()
	 * will work on them when we get to the tasklets.
	 */
}

static struct net_device * __init de600_probe(void)
{
	int	i;
	struct net_device *dev;
	int err;
	DECLARE_MAC_BUF(mac);

	dev = alloc_etherdev(0);
	if (!dev)
		return ERR_PTR(-ENOMEM);


	if (!request_region(DE600_IO, 3, "de600")) {
		printk(KERN_WARNING "DE600: port 0x%x busy\n", DE600_IO);
		err = -EBUSY;
		goto out;
	}

	printk(KERN_INFO "%s: D-Link DE-600 pocket adapter", dev->name);
	/* Alpha testers must have the version number to report bugs. */
	if (de600_debug > 1)
		printk(version);

	/* probe for adapter */
	err = -ENODEV;
	rx_page = 0;
	select_nic();
	(void)de600_read_status(dev);
	de600_put_command(RESET);
	de600_put_command(STOP_RESET);
	if (de600_read_status(dev) & 0xf0) {
		printk(": not at I/O %#3x.\n", DATA_PORT);
		goto out1;
	}

	/*
	 * Maybe we found one,
	 * have to check if it is a D-Link DE-600 adapter...
	 */

	/* Get the adapter ethernet address from the ROM */
	de600_setup_address(NODE_ADDRESS, RW_ADDR);
	for (i = 0; i < ETH_ALEN; i++) {
		dev->dev_addr[i] = de600_read_byte(READ_DATA, dev);
		dev->broadcast[i] = 0xff;
	}

	/* Check magic code */
	if ((dev->dev_addr[1] == 0xde) && (dev->dev_addr[2] == 0x15)) {
		/* OK, install real address */
		dev->dev_addr[0] = 0x00;
		dev->dev_addr[1] = 0x80;
		dev->dev_addr[2] = 0xc8;
		dev->dev_addr[3] &= 0x0f;
		dev->dev_addr[3] |= 0x70;
	} else {
		printk(" not identified in the printer port\n");
		goto out1;
	}

	printk(", Ethernet Address: %s\n", print_mac(mac, dev->dev_addr));

	dev->open = de600_open;
	dev->stop = de600_close;
	dev->hard_start_xmit = &de600_start_xmit;

	dev->flags&=~IFF_MULTICAST;

	select_prn();

	err = register_netdev(dev);
	if (err)
		goto out1;

	return dev;

out1:
	release_region(DE600_IO, 3);
out:
	free_netdev(dev);
	return ERR_PTR(err);
}

static int adapter_init(struct net_device *dev)
{
	int	i;

	select_nic();
	rx_page = 0; /* used by RESET */
	de600_put_command(RESET);
	de600_put_command(STOP_RESET);

	/* Check if it is still there... */
	/* Get the some bytes of the adapter ethernet address from the ROM */
	de600_setup_address(NODE_ADDRESS, RW_ADDR);
	de600_read_byte(READ_DATA, dev);
	if ((de600_read_byte(READ_DATA, dev) != 0xde) ||
	    (de600_read_byte(READ_DATA, dev) != 0x15)) {
	/* was: if (de600_read_status(dev) & 0xf0) { */
		printk("Something has happened to the DE-600!  Please check it and do a new ifconfig!\n");
		/* Goodbye, cruel world... */
		dev->flags &= ~IFF_UP;
		de600_close(dev);
		was_down = 1;
		netif_stop_queue(dev); /* Transmit busy...  */
		return 1; /* failed */
	}

	if (was_down) {
		printk(KERN_INFO "%s: Thanks, I feel much better now!\n", dev->name);
		was_down = 0;
	}

	tx_fifo_in = 0;
	tx_fifo_out = 0;
	free_tx_pages = TX_PAGES;


	/* set the ether address. */
	de600_setup_address(NODE_ADDRESS, RW_ADDR);
	for (i = 0; i < ETH_ALEN; i++)
		de600_put_byte(dev->dev_addr[i]);

	/* where to start saving incoming packets */
	rx_page = RX_BP | RX_BASE_PAGE;
	de600_setup_address(MEM_4K, RW_ADDR);
	/* Enable receiver */
	de600_put_command(RX_ENABLE);
	select_prn();

	netif_start_queue(dev);

	return 0; /* OK */
}

static struct net_device *de600_dev;

static int __init de600_init(void)
{
	de600_dev = de600_probe();
	if (IS_ERR(de600_dev))
		return PTR_ERR(de600_dev);
	return 0;
}

static void __exit de600_exit(void)
{
	unregister_netdev(de600_dev);
	release_region(DE600_IO, 3);
	free_netdev(de600_dev);
}

module_init(de600_init);
module_exit(de600_exit);

MODULE_LICENSE("GPL");
