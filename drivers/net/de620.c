/*
 *	de620.c $Revision: 1.40 $ BETA
 *
 *
 *	Linux driver for the D-Link DE-620 Ethernet pocket adapter.
 *
 *	Portions (C) Copyright 1993, 1994 by Bjorn Ekwall <bj0rn@blox.se>
 *
 *	Based on adapter information gathered from DOS packetdriver
 *	sources from D-Link Inc:  (Special thanks to Henry Ngai of D-Link.)
 *		Portions (C) Copyright D-Link SYSTEM Inc. 1991, 1992
 *		Copyright, 1988, Russell Nelson, Crynwr Software
 *
 *	Adapted to the sample network driver core for linux,
 *	written by: Donald Becker <becker@super.org>
 *		(Now at <becker@scyld.com>)
 *
 *	Valuable assistance from:
 *		J. Joshua Kopper <kopper@rtsg.mot.com>
 *		Olav Kvittem <Olav.Kvittem@uninett.no>
 *		Germano Caronni <caronni@nessie.cs.id.ethz.ch>
 *		Jeremy Fitzhardinge <jeremy@suite.sw.oz.au>
 *
 *****************************************************************************/
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
 *****************************************************************************/
static const char version[] =
	"de620.c: $Revision: 1.40 $,  Bjorn Ekwall <bj0rn@blox.se>\n";

/***********************************************************************
 *
 * "Tuning" section.
 *
 * Compile-time options: (see below for descriptions)
 * -DDE620_IO=0x378	(lpt1)
 * -DDE620_IRQ=7	(lpt1)
 * -DSHUTDOWN_WHEN_LOST
 * -DCOUNT_LOOPS
 * -DLOWSPEED
 * -DREAD_DELAY
 * -DWRITE_DELAY
 */

/*
 * This driver assumes that the printer port is a "normal",
 * dumb, uni-directional port!
 * If your port is "fancy" in any way, please try to set it to "normal"
 * with your BIOS setup.  I have no access to machines with bi-directional
 * ports, so I can't test such a driver :-(
 * (Yes, I _know_ it is possible to use DE620 with bidirectional ports...)
 *
 * There are some clones of DE620 out there, with different names.
 * If the current driver does not recognize a clone, try to change
 * the following #define to:
 *
 * #define DE620_CLONE 1
 */
#define DE620_CLONE 0

/*
 * If the adapter has problems with high speeds, enable this #define
 * otherwise full printerport speed will be attempted.
 *
 * You can tune the READ_DELAY/WRITE_DELAY below if you enable LOWSPEED
 *
#define LOWSPEED
 */

#ifndef READ_DELAY
#define READ_DELAY 100	/* adapter internal read delay in 100ns units */
#endif

#ifndef WRITE_DELAY
#define WRITE_DELAY 100	/* adapter internal write delay in 100ns units */
#endif

/*
 * Enable this #define if you want the adapter to do a "ifconfig down" on
 * itself when we have detected that something is possibly wrong with it.
 * The default behaviour is to retry with "adapter_init()" until success.
 * This should be used for debugging purposes only.
 *
#define SHUTDOWN_WHEN_LOST
 */

#ifdef LOWSPEED
/*
 * Enable this #define if you want to see debugging output that show how long
 * we have to wait before the DE-620 is ready for the next read/write/command.
 *
#define COUNT_LOOPS
 */
#endif

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/fcntl.h>
#include <linux/string.h>
#include <linux/interrupt.h>
#include <linux/ioport.h>
#include <linux/in.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/inet.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>

#include <asm/io.h>
#include <asm/system.h>

/* Constant definitions for the DE-620 registers, commands and bits */
#include "de620.h"

typedef unsigned char byte;

/*******************************************************
 *                                                     *
 * Definition of D-Link DE-620 Ethernet Pocket adapter *
 * See also "de620.h"                                  *
 *                                                     *
 *******************************************************/
#ifndef DE620_IO /* Compile-time configurable */
#define DE620_IO 0x378
#endif

#ifndef DE620_IRQ /* Compile-time configurable */
#define DE620_IRQ	7
#endif

#define DATA_PORT	(dev->base_addr)
#define STATUS_PORT	(dev->base_addr + 1)
#define COMMAND_PORT	(dev->base_addr + 2)

#define RUNT 60		/* Too small Ethernet packet */
#define GIANT 1514	/* largest legal size packet, no fcs */

/*
 * Force media with insmod:
 *	insmod de620.o bnc=1
 * or
 *	insmod de620.o utp=1
 *
 * Force io and/or irq with insmod:
 *	insmod de620.o io=0x378 irq=7
 *
 * Make a clone skip the Ethernet-address range check:
 *	insmod de620.o clone=1
 */
static int bnc;
static int utp;
static int io  = DE620_IO;
static int irq = DE620_IRQ;
static int clone = DE620_CLONE;

static spinlock_t de620_lock;

module_param(bnc, int, 0);
module_param(utp, int, 0);
module_param(io, int, 0);
module_param(irq, int, 0);
module_param(clone, int, 0);
MODULE_PARM_DESC(bnc, "DE-620 set BNC medium (0-1)");
MODULE_PARM_DESC(utp, "DE-620 set UTP medium (0-1)");
MODULE_PARM_DESC(io, "DE-620 I/O base address,required");
MODULE_PARM_DESC(irq, "DE-620 IRQ number,required");
MODULE_PARM_DESC(clone, "Check also for non-D-Link DE-620 clones (0-1)");

/***********************************************
 *                                             *
 * Index to functions, as function prototypes. *
 *                                             *
 ***********************************************/

/*
 * Routines used internally. (See also "convenience macros.. below")
 */

/* Put in the device structure. */
static int	de620_open(struct net_device *);
static int	de620_close(struct net_device *);
static void	de620_set_multicast_list(struct net_device *);
static int	de620_start_xmit(struct sk_buff *, struct net_device *);

/* Dispatch from interrupts. */
static irqreturn_t de620_interrupt(int, void *);
static int	de620_rx_intr(struct net_device *);

/* Initialization */
static int	adapter_init(struct net_device *);
static int	read_eeprom(struct net_device *);


/*
 * D-Link driver variables:
 */
#define SCR_DEF NIBBLEMODE |INTON | SLEEP | AUTOTX
#define	TCR_DEF RXPB			/* not used: | TXSUCINT | T16INT */
#define DE620_RX_START_PAGE 12		/* 12 pages (=3k) reserved for tx */
#define DEF_NIC_CMD IRQEN | ICEN | DS1

static volatile byte	NIC_Cmd;
static volatile byte	next_rx_page;
static byte		first_rx_page;
static byte		last_rx_page;
static byte		EIPRegister;

static struct nic {
	byte	NodeID[6];
	byte	RAM_Size;
	byte	Model;
	byte	Media;
	byte	SCR;
} nic_data;

/**********************************************************
 *                                                        *
 * Convenience macros/functions for D-Link DE-620 adapter *
 *                                                        *
 **********************************************************/
#define de620_tx_buffs(dd) (inb(STATUS_PORT) & (TXBF0 | TXBF1))
#define de620_flip_ds(dd) NIC_Cmd ^= DS0 | DS1; outb(NIC_Cmd, COMMAND_PORT);

/* Check for ready-status, and return a nibble (high 4 bits) for data input */
#ifdef COUNT_LOOPS
static int tot_cnt;
#endif
static inline byte
de620_ready(struct net_device *dev)
{
	byte value;
	register short int cnt = 0;

	while ((((value = inb(STATUS_PORT)) & READY) == 0) && (cnt <= 1000))
		++cnt;

#ifdef COUNT_LOOPS
	tot_cnt += cnt;
#endif
	return value & 0xf0; /* nibble */
}

static inline void
de620_send_command(struct net_device *dev, byte cmd)
{
	de620_ready(dev);
	if (cmd == W_DUMMY)
		outb(NIC_Cmd, COMMAND_PORT);

	outb(cmd, DATA_PORT);

	outb(NIC_Cmd ^ CS0, COMMAND_PORT);
	de620_ready(dev);
	outb(NIC_Cmd, COMMAND_PORT);
}

static inline void
de620_put_byte(struct net_device *dev, byte value)
{
	/* The de620_ready() makes 7 loops, on the average, on a DX2/66 */
	de620_ready(dev);
	outb(value, DATA_PORT);
	de620_flip_ds(dev);
}

static inline byte
de620_read_byte(struct net_device *dev)
{
	byte value;

	/* The de620_ready() makes 7 loops, on the average, on a DX2/66 */
	value = de620_ready(dev); /* High nibble */
	de620_flip_ds(dev);
	value |= de620_ready(dev) >> 4; /* Low nibble */
	return value;
}

static inline void
de620_write_block(struct net_device *dev, byte *buffer, int count, int pad)
{
#ifndef LOWSPEED
	byte uflip = NIC_Cmd ^ (DS0 | DS1);
	byte dflip = NIC_Cmd;
#else /* LOWSPEED */
#ifdef COUNT_LOOPS
	int bytes = count;
#endif /* COUNT_LOOPS */
#endif /* LOWSPEED */

#ifdef LOWSPEED
#ifdef COUNT_LOOPS
	tot_cnt = 0;
#endif /* COUNT_LOOPS */
	/* No further optimization useful, the limit is in the adapter. */
	for ( ; count > 0; --count, ++buffer) {
		de620_put_byte(dev,*buffer);
	}
	for ( count = pad ; count > 0; --count, ++buffer) {
		de620_put_byte(dev, 0);
	}
	de620_send_command(dev,W_DUMMY);
#ifdef COUNT_LOOPS
	/* trial debug output: loops per byte in de620_ready() */
	printk("WRITE(%d)\n", tot_cnt/((bytes?bytes:1)));
#endif /* COUNT_LOOPS */
#else /* not LOWSPEED */
	for ( ; count > 0; count -=2) {
		outb(*buffer++, DATA_PORT);
		outb(uflip, COMMAND_PORT);
		outb(*buffer++, DATA_PORT);
		outb(dflip, COMMAND_PORT);
	}
	de620_send_command(dev,W_DUMMY);
#endif /* LOWSPEED */
}

static inline void
de620_read_block(struct net_device *dev, byte *data, int count)
{
#ifndef LOWSPEED
	byte value;
	byte uflip = NIC_Cmd ^ (DS0 | DS1);
	byte dflip = NIC_Cmd;
#else /* LOWSPEED */
#ifdef COUNT_LOOPS
	int bytes = count;

	tot_cnt = 0;
#endif /* COUNT_LOOPS */
#endif /* LOWSPEED */

#ifdef LOWSPEED
	/* No further optimization useful, the limit is in the adapter. */
	while (count-- > 0) {
		*data++ = de620_read_byte(dev);
		de620_flip_ds(dev);
	}
#ifdef COUNT_LOOPS
	/* trial debug output: loops per byte in de620_ready() */
	printk("READ(%d)\n", tot_cnt/(2*(bytes?bytes:1)));
#endif /* COUNT_LOOPS */
#else /* not LOWSPEED */
	while (count-- > 0) {
		value = inb(STATUS_PORT) & 0xf0; /* High nibble */
		outb(uflip, COMMAND_PORT);
		*data++ = value | inb(STATUS_PORT) >> 4; /* Low nibble */
		outb(dflip , COMMAND_PORT);
	}
#endif /* LOWSPEED */
}

static inline void
de620_set_delay(struct net_device *dev)
{
	de620_ready(dev);
	outb(W_DFR, DATA_PORT);
	outb(NIC_Cmd ^ CS0, COMMAND_PORT);

	de620_ready(dev);
#ifdef LOWSPEED
	outb(WRITE_DELAY, DATA_PORT);
#else
	outb(0, DATA_PORT);
#endif
	de620_flip_ds(dev);

	de620_ready(dev);
#ifdef LOWSPEED
	outb(READ_DELAY, DATA_PORT);
#else
	outb(0, DATA_PORT);
#endif
	de620_flip_ds(dev);
}

static inline void
de620_set_register(struct net_device *dev, byte reg, byte value)
{
	de620_ready(dev);
	outb(reg, DATA_PORT);
	outb(NIC_Cmd ^ CS0, COMMAND_PORT);

	de620_put_byte(dev, value);
}

static inline byte
de620_get_register(struct net_device *dev, byte reg)
{
	byte value;

	de620_send_command(dev,reg);
	value = de620_read_byte(dev);
	de620_send_command(dev,W_DUMMY);

	return value;
}

/*********************************************************************
 *
 * Open/initialize the board.
 *
 * This routine should set everything up anew at each open, even
 * registers that "should" only need to be set once at boot, so that
 * there is a non-reboot way to recover if something goes wrong.
 *
 */
static int de620_open(struct net_device *dev)
{
	int ret = request_irq(dev->irq, de620_interrupt, 0, dev->name, dev);
	if (ret) {
		printk (KERN_ERR "%s: unable to get IRQ %d\n", dev->name, dev->irq);
		return ret;
	}

	if (adapter_init(dev)) {
		ret = -EIO;
		goto out_free_irq;
	}

	netif_start_queue(dev);
	return 0;

out_free_irq:
	free_irq(dev->irq, dev);
	return ret;
}

/************************************************
 *
 * The inverse routine to de620_open().
 *
 */

static int de620_close(struct net_device *dev)
{
	netif_stop_queue(dev);
	/* disable recv */
	de620_set_register(dev, W_TCR, RXOFF);
	free_irq(dev->irq, dev);
	return 0;
}

/*********************************************
 *
 * Set or clear the multicast filter for this adaptor.
 * (no real multicast implemented for the DE-620, but she can be promiscuous...)
 *
 */

static void de620_set_multicast_list(struct net_device *dev)
{
	if (!netdev_mc_empty(dev) || dev->flags&(IFF_ALLMULTI|IFF_PROMISC))
	{ /* Enable promiscuous mode */
		de620_set_register(dev, W_TCR, (TCR_DEF & ~RXPBM) | RXALL);
	}
	else
	{ /* Disable promiscuous mode, use normal mode */
		de620_set_register(dev, W_TCR, TCR_DEF);
	}
}

/*******************************************************
 *
 * Handle timeouts on transmit
 */

static void de620_timeout(struct net_device *dev)
{
	printk(KERN_WARNING "%s: transmit timed out, %s?\n", dev->name, "network cable problem");
	/* Restart the adapter. */
	if (!adapter_init(dev)) /* maybe close it */
		netif_wake_queue(dev);
}

/*******************************************************
 *
 * Copy a buffer to the adapter transmit page memory.
 * Start sending.
 */
static int de620_start_xmit(struct sk_buff *skb, struct net_device *dev)
{
	unsigned long flags;
	int len;
	byte *buffer = skb->data;
	byte using_txbuf;

	using_txbuf = de620_tx_buffs(dev); /* Peek at the adapter */

	netif_stop_queue(dev);


	if ((len = skb->len) < RUNT)
		len = RUNT;
	if (len & 1) /* send an even number of bytes */
		++len;

	/* Start real output */

	spin_lock_irqsave(&de620_lock, flags);
	pr_debug("de620_start_xmit: len=%d, bufs 0x%02x\n",
		(int)skb->len, using_txbuf);

	/* select a free tx buffer. if there is one... */
	switch (using_txbuf) {
	default: /* both are free: use TXBF0 */
	case TXBF1: /* use TXBF0 */
		de620_send_command(dev,W_CR | RW0);
		using_txbuf |= TXBF0;
		break;

	case TXBF0: /* use TXBF1 */
		de620_send_command(dev,W_CR | RW1);
		using_txbuf |= TXBF1;
		break;

	case (TXBF0 | TXBF1): /* NONE!!! */
		printk(KERN_WARNING "%s: No tx-buffer available!\n", dev->name);
		spin_unlock_irqrestore(&de620_lock, flags);
		return NETDEV_TX_BUSY;
	}
	de620_write_block(dev, buffer, skb->len, len-skb->len);

	dev->trans_start = jiffies;
	if(!(using_txbuf == (TXBF0 | TXBF1)))
		netif_wake_queue(dev);

	dev->stats.tx_packets++;
	spin_unlock_irqrestore(&de620_lock, flags);
	dev_kfree_skb (skb);
	return NETDEV_TX_OK;
}

/*****************************************************
 *
 * Handle the network interface interrupts.
 *
 */
static irqreturn_t
de620_interrupt(int irq_in, void *dev_id)
{
	struct net_device *dev = dev_id;
	byte irq_status;
	int bogus_count = 0;
	int again = 0;

	spin_lock(&de620_lock);

	/* Read the status register (_not_ the status port) */
	irq_status = de620_get_register(dev, R_STS);

	pr_debug("de620_interrupt (%2.2X)\n", irq_status);

	if (irq_status & RXGOOD) {
		do {
			again = de620_rx_intr(dev);
			pr_debug("again=%d\n", again);
		}
		while (again && (++bogus_count < 100));
	}

	if(de620_tx_buffs(dev) != (TXBF0 | TXBF1))
		netif_wake_queue(dev);

	spin_unlock(&de620_lock);
	return IRQ_HANDLED;
}

/**************************************
 *
 * Get a packet from the adapter
 *
 * Send it "upstairs"
 *
 */
static int de620_rx_intr(struct net_device *dev)
{
	struct header_buf {
		byte		status;
		byte		Rx_NextPage;
		unsigned short	Rx_ByteCount;
	} header_buf;
	struct sk_buff *skb;
	int size;
	byte *buffer;
	byte pagelink;
	byte curr_page;

	pr_debug("de620_rx_intr: next_rx_page = %d\n", next_rx_page);

	/* Tell the adapter that we are going to read data, and from where */
	de620_send_command(dev, W_CR | RRN);
	de620_set_register(dev, W_RSA1, next_rx_page);
	de620_set_register(dev, W_RSA0, 0);

	/* Deep breath, and away we goooooo */
	de620_read_block(dev, (byte *)&header_buf, sizeof(struct header_buf));
	pr_debug("page status=0x%02x, nextpage=%d, packetsize=%d\n",
		header_buf.status, header_buf.Rx_NextPage,
		header_buf.Rx_ByteCount);

	/* Plausible page header? */
	pagelink = header_buf.Rx_NextPage;
	if ((pagelink < first_rx_page) || (last_rx_page < pagelink)) {
		/* Ouch... Forget it! Skip all and start afresh... */
		printk(KERN_WARNING "%s: Ring overrun? Restoring...\n", dev->name);
		/* You win some, you lose some. And sometimes plenty... */
		adapter_init(dev);
		netif_wake_queue(dev);
		dev->stats.rx_over_errors++;
		return 0;
	}

	/* OK, this look good, so far. Let's see if it's consistent... */
	/* Let's compute the start of the next packet, based on where we are */
	pagelink = next_rx_page +
		((header_buf.Rx_ByteCount + (4 - 1 + 0x100)) >> 8);

	/* Are we going to wrap around the page counter? */
	if (pagelink > last_rx_page)
		pagelink -= (last_rx_page - first_rx_page + 1);

	/* Is the _computed_ next page number equal to what the adapter says? */
	if (pagelink != header_buf.Rx_NextPage) {
		/* Naah, we'll skip this packet. Probably bogus data as well */
		printk(KERN_WARNING "%s: Page link out of sync! Restoring...\n", dev->name);
		next_rx_page = header_buf.Rx_NextPage; /* at least a try... */
		de620_send_command(dev, W_DUMMY);
		de620_set_register(dev, W_NPRF, next_rx_page);
		dev->stats.rx_over_errors++;
		return 0;
	}
	next_rx_page = pagelink;

	size = header_buf.Rx_ByteCount - 4;
	if ((size < RUNT) || (GIANT < size)) {
		printk(KERN_WARNING "%s: Illegal packet size: %d!\n", dev->name, size);
	}
	else { /* Good packet? */
		skb = dev_alloc_skb(size+2);
		if (skb == NULL) { /* Yeah, but no place to put it... */
			printk(KERN_WARNING "%s: Couldn't allocate a sk_buff of size %d.\n", dev->name, size);
			dev->stats.rx_dropped++;
		}
		else { /* Yep! Go get it! */
			skb_reserve(skb,2);	/* Align */
			/* skb->data points to the start of sk_buff data area */
			buffer = skb_put(skb,size);
			/* copy the packet into the buffer */
			de620_read_block(dev, buffer, size);
			pr_debug("Read %d bytes\n", size);
			skb->protocol=eth_type_trans(skb,dev);
			netif_rx(skb); /* deliver it "upstairs" */
			/* count all receives */
			dev->stats.rx_packets++;
			dev->stats.rx_bytes += size;
		}
	}

	/* Let's peek ahead to see if we have read the last current packet */
	/* NOTE! We're _not_ checking the 'EMPTY'-flag! This seems better... */
	curr_page = de620_get_register(dev, R_CPR);
	de620_set_register(dev, W_NPRF, next_rx_page);
	pr_debug("next_rx_page=%d CPR=%d\n", next_rx_page, curr_page);

	return (next_rx_page != curr_page); /* That was slightly tricky... */
}

/*********************************************
 *
 * Reset the adapter to a known state
 *
 */
static int adapter_init(struct net_device *dev)
{
	int i;
	static int was_down;

	if ((nic_data.Model == 3) || (nic_data.Model == 0)) { /* CT */
		EIPRegister = NCTL0;
		if (nic_data.Media != 1)
			EIPRegister |= NIS0;	/* not BNC */
	}
	else if (nic_data.Model == 2) { /* UTP */
		EIPRegister = NCTL0 | NIS0;
	}

	if (utp)
		EIPRegister = NCTL0 | NIS0;
	if (bnc)
		EIPRegister = NCTL0;

	de620_send_command(dev, W_CR | RNOP | CLEAR);
	de620_send_command(dev, W_CR | RNOP);

	de620_set_register(dev, W_SCR, SCR_DEF);
	/* disable recv to wait init */
	de620_set_register(dev, W_TCR, RXOFF);

	/* Set the node ID in the adapter */
	for (i = 0; i < 6; ++i) { /* W_PARn = 0xaa + n */
		de620_set_register(dev, W_PAR0 + i, dev->dev_addr[i]);
	}

	de620_set_register(dev, W_EIP, EIPRegister);

	next_rx_page = first_rx_page = DE620_RX_START_PAGE;
	if (nic_data.RAM_Size)
		last_rx_page = nic_data.RAM_Size - 1;
	else /* 64k RAM */
		last_rx_page = 255;

	de620_set_register(dev, W_SPR, first_rx_page); /* Start Page Register*/
	de620_set_register(dev, W_EPR, last_rx_page);  /* End Page Register */
	de620_set_register(dev, W_CPR, first_rx_page);/*Current Page Register*/
	de620_send_command(dev, W_NPR | first_rx_page); /* Next Page Register*/
	de620_send_command(dev, W_DUMMY);
	de620_set_delay(dev);

	/* Final sanity check: Anybody out there? */
	/* Let's hope some bits from the statusregister make a good check */
#define CHECK_MASK (  0 | TXSUC |  T16  |  0  | RXCRC | RXSHORT |  0  |  0  )
#define CHECK_OK   (  0 |   0   |  0    |  0  |   0   |   0     |  0  |  0  )
        /* success:   X     0      0       X      0       0        X     X  */
        /* ignore:   EEDI                RXGOOD                   COLS  LNKS*/

	if (((i = de620_get_register(dev, R_STS)) & CHECK_MASK) != CHECK_OK) {
		printk(KERN_ERR "%s: Something has happened to the DE-620!  Please check it"
#ifdef SHUTDOWN_WHEN_LOST
			" and do a new ifconfig"
#endif
			"! (%02x)\n", dev->name, i);
#ifdef SHUTDOWN_WHEN_LOST
		/* Goodbye, cruel world... */
		dev->flags &= ~IFF_UP;
		de620_close(dev);
#endif
		was_down = 1;
		return 1; /* failed */
	}
	if (was_down) {
		printk(KERN_WARNING "%s: Thanks, I feel much better now!\n", dev->name);
		was_down = 0;
	}

	/* All OK, go ahead... */
	de620_set_register(dev, W_TCR, TCR_DEF);

	return 0; /* all ok */
}

static const struct net_device_ops de620_netdev_ops = {
	.ndo_open 		= de620_open,
	.ndo_stop 		= de620_close,
	.ndo_start_xmit 	= de620_start_xmit,
	.ndo_tx_timeout 	= de620_timeout,
	.ndo_set_multicast_list = de620_set_multicast_list,
	.ndo_change_mtu		= eth_change_mtu,
	.ndo_set_mac_address 	= eth_mac_addr,
	.ndo_validate_addr	= eth_validate_addr,
};

/******************************************************************************
 *
 * Only start-up code below
 *
 */
/****************************************
 *
 * Check if there is a DE-620 connected
 */
struct net_device * __init de620_probe(int unit)
{
	byte checkbyte = 0xa5;
	struct net_device *dev;
	int err = -ENOMEM;
	int i;

	dev = alloc_etherdev(0);
	if (!dev)
		goto out;

	spin_lock_init(&de620_lock);

	/*
	 * This is where the base_addr and irq gets set.
	 * Tunable at compile-time and insmod-time
	 */
	dev->base_addr = io;
	dev->irq       = irq;

	/* allow overriding parameters on command line */
	if (unit >= 0) {
		sprintf(dev->name, "eth%d", unit);
		netdev_boot_setup_check(dev);
	}

	pr_debug("%s", version);

	printk(KERN_INFO "D-Link DE-620 pocket adapter");

	if (!request_region(dev->base_addr, 3, "de620")) {
		printk(" io 0x%3lX, which is busy.\n", dev->base_addr);
		err = -EBUSY;
		goto out1;
	}

	/* Initially, configure basic nibble mode, so we can read the EEPROM */
	NIC_Cmd = DEF_NIC_CMD;
	de620_set_register(dev, W_EIP, EIPRegister);

	/* Anybody out there? */
	de620_set_register(dev, W_CPR, checkbyte);
	checkbyte = de620_get_register(dev, R_CPR);

	if ((checkbyte != 0xa5) || (read_eeprom(dev) != 0)) {
		printk(" not identified in the printer port\n");
		err = -ENODEV;
		goto out2;
	}

	/* else, got it! */
	dev->dev_addr[0] = nic_data.NodeID[0];
	for (i = 1; i < ETH_ALEN; i++) {
		dev->dev_addr[i] = nic_data.NodeID[i];
		dev->broadcast[i] = 0xff;
	}

	printk(", Ethernet Address: %pM", dev->dev_addr);

	printk(" (%dk RAM,",
		(nic_data.RAM_Size) ? (nic_data.RAM_Size >> 2) : 64);

	if (nic_data.Media == 1)
		printk(" BNC)\n");
	else
		printk(" UTP)\n");

	dev->netdev_ops = &de620_netdev_ops;
	dev->watchdog_timeo	= HZ*2;

	/* base_addr and irq are already set, see above! */

	/* dump eeprom */
	pr_debug("\nEEPROM contents:\n"
		"RAM_Size = 0x%02X\n"
		"NodeID = %pM\n"
		"Model = %d\n"
		"Media = %d\n"
		"SCR = 0x%02x\n", nic_data.RAM_Size, nic_data.NodeID,
		nic_data.Model, nic_data.Media, nic_data.SCR);

	err = register_netdev(dev);
	if (err)
		goto out2;
	return dev;

out2:
	release_region(dev->base_addr, 3);
out1:
	free_netdev(dev);
out:
	return ERR_PTR(err);
}

/**********************************
 *
 * Read info from on-board EEPROM
 *
 * Note: Bitwise serial I/O to/from the EEPROM vi the status _register_!
 */
#define sendit(dev,data) de620_set_register(dev, W_EIP, data | EIPRegister);

static unsigned short __init ReadAWord(struct net_device *dev, int from)
{
	unsigned short data;
	int nbits;

	/* cs   [__~~] SET SEND STATE */
	/* di   [____]                */
	/* sck  [_~~_]                */
	sendit(dev, 0); sendit(dev, 1); sendit(dev, 5); sendit(dev, 4);

	/* Send the 9-bit address from where we want to read the 16-bit word */
	for (nbits = 9; nbits > 0; --nbits, from <<= 1) {
		if (from & 0x0100) { /* bit set? */
			/* cs    [~~~~] SEND 1 */
			/* di    [~~~~]        */
			/* sck   [_~~_]        */
			sendit(dev, 6); sendit(dev, 7); sendit(dev, 7); sendit(dev, 6);
		}
		else {
			/* cs    [~~~~] SEND 0 */
			/* di    [____]        */
			/* sck   [_~~_]        */
			sendit(dev, 4); sendit(dev, 5); sendit(dev, 5); sendit(dev, 4);
		}
	}

	/* Shift in the 16-bit word. The bits appear serially in EEDI (=0x80) */
	for (data = 0, nbits = 16; nbits > 0; --nbits) {
		/* cs    [~~~~] SEND 0 */
		/* di    [____]        */
		/* sck   [_~~_]        */
		sendit(dev, 4); sendit(dev, 5); sendit(dev, 5); sendit(dev, 4);
		data = (data << 1) | ((de620_get_register(dev, R_STS) & EEDI) >> 7);
	}
	/* cs    [____] RESET SEND STATE */
	/* di    [____]                  */
	/* sck   [_~~_]                  */
	sendit(dev, 0); sendit(dev, 1); sendit(dev, 1); sendit(dev, 0);

	return data;
}

static int __init read_eeprom(struct net_device *dev)
{
	unsigned short wrd;

	/* D-Link Ethernet addresses are in the series  00:80:c8:7X:XX:XX:XX */
	wrd = ReadAWord(dev, 0x1aa);	/* bytes 0 + 1 of NodeID */
	if (!clone && (wrd != htons(0x0080))) /* Valid D-Link ether sequence? */
		return -1; /* Nope, not a DE-620 */
	nic_data.NodeID[0] = wrd & 0xff;
	nic_data.NodeID[1] = wrd >> 8;

	wrd = ReadAWord(dev, 0x1ab);	/* bytes 2 + 3 of NodeID */
	if (!clone && ((wrd & 0xff) != 0xc8)) /* Valid D-Link ether sequence? */
		return -1; /* Nope, not a DE-620 */
	nic_data.NodeID[2] = wrd & 0xff;
	nic_data.NodeID[3] = wrd >> 8;

	wrd = ReadAWord(dev, 0x1ac);	/* bytes 4 + 5 of NodeID */
	nic_data.NodeID[4] = wrd & 0xff;
	nic_data.NodeID[5] = wrd >> 8;

	wrd = ReadAWord(dev, 0x1ad);	/* RAM size in pages (256 bytes). 0 = 64k */
	nic_data.RAM_Size = (wrd >> 8);

	wrd = ReadAWord(dev, 0x1ae);	/* hardware model (CT = 3) */
	nic_data.Model = (wrd & 0xff);

	wrd = ReadAWord(dev, 0x1af); /* media (indicates BNC/UTP) */
	nic_data.Media = (wrd & 0xff);

	wrd = ReadAWord(dev, 0x1a8); /* System Configuration Register */
	nic_data.SCR = (wrd >> 8);

	return 0; /* no errors */
}

/******************************************************************************
 *
 * Loadable module skeleton
 *
 */
#ifdef MODULE
static struct net_device *de620_dev;

int __init init_module(void)
{
	de620_dev = de620_probe(-1);
	if (IS_ERR(de620_dev))
		return PTR_ERR(de620_dev);
	return 0;
}

void cleanup_module(void)
{
	unregister_netdev(de620_dev);
	release_region(de620_dev->base_addr, 3);
	free_netdev(de620_dev);
}
#endif /* MODULE */
MODULE_LICENSE("GPL");
