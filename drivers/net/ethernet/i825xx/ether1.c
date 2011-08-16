/*
 *  linux/drivers/acorn/net/ether1.c
 *
 *  Copyright (C) 1996-2000 Russell King
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *  Acorn ether1 driver (82586 chip) for Acorn machines
 *
 * We basically keep two queues in the cards memory - one for transmit
 * and one for receive.  Each has a head and a tail.  The head is where
 * we/the chip adds packets to be transmitted/received, and the tail
 * is where the transmitter has got to/where the receiver will stop.
 * Both of these queues are circular, and since the chip is running
 * all the time, we have to be careful when we modify the pointers etc
 * so that the buffer memory contents is valid all the time.
 *
 * Change log:
 * 1.00	RMK			Released
 * 1.01	RMK	19/03/1996	Transfers the last odd byte onto/off of the card now.
 * 1.02	RMK	25/05/1997	Added code to restart RU if it goes not ready
 * 1.03	RMK	14/09/1997	Cleaned up the handling of a reset during the TX interrupt.
 *				Should prevent lockup.
 * 1.04 RMK	17/09/1997	Added more info when initialsation of chip goes wrong.
 *				TDR now only reports failure when chip reports non-zero
 *				TDR time-distance.
 * 1.05	RMK	31/12/1997	Removed calls to dev_tint for 2.1
 * 1.06	RMK	10/02/2000	Updated for 2.3.43
 * 1.07	RMK	13/05/2000	Updated for 2.3.99-pre8
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
#include <linux/errno.h>
#include <linux/device.h>
#include <linux/init.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include <linux/bitops.h>

#include <asm/system.h>
#include <asm/io.h>
#include <asm/dma.h>
#include <asm/ecard.h>

#define __ETHER1_C
#include "ether1.h"

static unsigned int net_debug = NET_DEBUG;

#define BUFFER_SIZE	0x10000
#define TX_AREA_START	0x00100
#define TX_AREA_END	0x05000
#define RX_AREA_START	0x05000
#define RX_AREA_END	0x0fc00

static int ether1_open(struct net_device *dev);
static int ether1_sendpacket(struct sk_buff *skb, struct net_device *dev);
static irqreturn_t ether1_interrupt(int irq, void *dev_id);
static int ether1_close(struct net_device *dev);
static void ether1_setmulticastlist(struct net_device *dev);
static void ether1_timeout(struct net_device *dev);

/* ------------------------------------------------------------------------- */

static char version[] __devinitdata = "ether1 ethernet driver (c) 2000 Russell King v1.07\n";

#define BUS_16 16
#define BUS_8  8

/* ------------------------------------------------------------------------- */

#define DISABLEIRQS 1
#define NORMALIRQS  0

#define ether1_readw(dev, addr, type, offset, svflgs) ether1_inw_p (dev, addr + (int)(&((type *)0)->offset), svflgs)
#define ether1_writew(dev, val, addr, type, offset, svflgs) ether1_outw_p (dev, val, addr + (int)(&((type *)0)->offset), svflgs)

static inline unsigned short
ether1_inw_p (struct net_device *dev, int addr, int svflgs)
{
	unsigned long flags;
	unsigned short ret;

	if (svflgs)
		local_irq_save (flags);

	writeb(addr >> 12, REG_PAGE);
	ret = readw(ETHER1_RAM + ((addr & 4095) << 1));
	if (svflgs)
		local_irq_restore (flags);
	return ret;
}

static inline void
ether1_outw_p (struct net_device *dev, unsigned short val, int addr, int svflgs)
{
	unsigned long flags;

	if (svflgs)
		local_irq_save (flags);

	writeb(addr >> 12, REG_PAGE);
	writew(val, ETHER1_RAM + ((addr & 4095) << 1));
	if (svflgs)
		local_irq_restore (flags);
}

/*
 * Some inline assembler to allow fast transfers on to/off of the card.
 * Since this driver depends on some features presented by the ARM
 * specific architecture, and that you can't configure this driver
 * without specifiing ARM mode, this is not a problem.
 *
 * This routine is essentially an optimised memcpy from the card's
 * onboard RAM to kernel memory.
 */
static void
ether1_writebuffer (struct net_device *dev, void *data, unsigned int start, unsigned int length)
{
	unsigned int page, thislen, offset;
	void __iomem *addr;

	offset = start & 4095;
	page = start >> 12;
	addr = ETHER1_RAM + (offset << 1);

	if (offset + length > 4096)
		thislen = 4096 - offset;
	else
		thislen = length;

	do {
		int used;

		writeb(page, REG_PAGE);
		length -= thislen;

		__asm__ __volatile__(
	"subs	%3, %3, #2\n\
	bmi	2f\n\
1:	ldr	%0, [%1], #2\n\
	mov	%0, %0, lsl #16\n\
	orr	%0, %0, %0, lsr #16\n\
	str	%0, [%2], #4\n\
	subs	%3, %3, #2\n\
	bmi	2f\n\
	ldr	%0, [%1], #2\n\
	mov	%0, %0, lsl #16\n\
	orr	%0, %0, %0, lsr #16\n\
	str	%0, [%2], #4\n\
	subs	%3, %3, #2\n\
	bmi	2f\n\
	ldr	%0, [%1], #2\n\
	mov	%0, %0, lsl #16\n\
	orr	%0, %0, %0, lsr #16\n\
	str	%0, [%2], #4\n\
	subs	%3, %3, #2\n\
	bmi	2f\n\
	ldr	%0, [%1], #2\n\
	mov	%0, %0, lsl #16\n\
	orr	%0, %0, %0, lsr #16\n\
	str	%0, [%2], #4\n\
	subs	%3, %3, #2\n\
	bpl	1b\n\
2:	adds	%3, %3, #1\n\
	ldreqb	%0, [%1]\n\
	streqb	%0, [%2]"
		: "=&r" (used), "=&r" (data)
		: "r"  (addr), "r" (thislen), "1" (data));

		addr = ETHER1_RAM;

		thislen = length;
		if (thislen > 4096)
			thislen = 4096;
		page++;
	} while (thislen);
}

static void
ether1_readbuffer (struct net_device *dev, void *data, unsigned int start, unsigned int length)
{
	unsigned int page, thislen, offset;
	void __iomem *addr;

	offset = start & 4095;
	page = start >> 12;
	addr = ETHER1_RAM + (offset << 1);

	if (offset + length > 4096)
		thislen = 4096 - offset;
	else
		thislen = length;

	do {
		int used;

		writeb(page, REG_PAGE);
		length -= thislen;

		__asm__ __volatile__(
	"subs	%3, %3, #2\n\
	bmi	2f\n\
1:	ldr	%0, [%2], #4\n\
	strb	%0, [%1], #1\n\
	mov	%0, %0, lsr #8\n\
	strb	%0, [%1], #1\n\
	subs	%3, %3, #2\n\
	bmi	2f\n\
	ldr	%0, [%2], #4\n\
	strb	%0, [%1], #1\n\
	mov	%0, %0, lsr #8\n\
	strb	%0, [%1], #1\n\
	subs	%3, %3, #2\n\
	bmi	2f\n\
	ldr	%0, [%2], #4\n\
	strb	%0, [%1], #1\n\
	mov	%0, %0, lsr #8\n\
	strb	%0, [%1], #1\n\
	subs	%3, %3, #2\n\
	bmi	2f\n\
	ldr	%0, [%2], #4\n\
	strb	%0, [%1], #1\n\
	mov	%0, %0, lsr #8\n\
	strb	%0, [%1], #1\n\
	subs	%3, %3, #2\n\
	bpl	1b\n\
2:	adds	%3, %3, #1\n\
	ldreqb	%0, [%2]\n\
	streqb	%0, [%1]"
		: "=&r" (used), "=&r" (data)
		: "r"  (addr), "r" (thislen), "1" (data));

		addr = ETHER1_RAM;

		thislen = length;
		if (thislen > 4096)
			thislen = 4096;
		page++;
	} while (thislen);
}

static int __devinit
ether1_ramtest(struct net_device *dev, unsigned char byte)
{
	unsigned char *buffer = kmalloc (BUFFER_SIZE, GFP_KERNEL);
	int i, ret = BUFFER_SIZE;
	int max_errors = 15;
	int bad = -1;
	int bad_start = 0;

	if (!buffer)
		return 1;

	memset (buffer, byte, BUFFER_SIZE);
	ether1_writebuffer (dev, buffer, 0, BUFFER_SIZE);
	memset (buffer, byte ^ 0xff, BUFFER_SIZE);
	ether1_readbuffer (dev, buffer, 0, BUFFER_SIZE);

	for (i = 0; i < BUFFER_SIZE; i++) {
		if (buffer[i] != byte) {
			if (max_errors >= 0 && bad != buffer[i]) {
				if (bad != -1)
					printk ("\n");
				printk (KERN_CRIT "%s: RAM failed with (%02X instead of %02X) at 0x%04X",
					dev->name, buffer[i], byte, i);
				ret = -ENODEV;
				max_errors --;
				bad = buffer[i];
				bad_start = i;
			}
		} else {
			if (bad != -1) {
			    	if (bad_start == i - 1)
					printk ("\n");
				else
					printk (" - 0x%04X\n", i - 1);
				bad = -1;
			}
		}
	}

	if (bad != -1)
		printk (" - 0x%04X\n", BUFFER_SIZE);
	kfree (buffer);

	return ret;
}

static int
ether1_reset (struct net_device *dev)
{
	writeb(CTRL_RST|CTRL_ACK, REG_CONTROL);
	return BUS_16;
}

static int __devinit
ether1_init_2(struct net_device *dev)
{
	int i;
	dev->mem_start = 0;

	i = ether1_ramtest (dev, 0x5a);

	if (i > 0)
		i = ether1_ramtest (dev, 0x1e);

	if (i <= 0)
	    	return -ENODEV;

	dev->mem_end = i;
	return 0;
}

/*
 * These are the structures that are loaded into the ether RAM card to
 * initialise the 82586
 */

/* at 0x0100 */
#define NOP_ADDR	(TX_AREA_START)
#define NOP_SIZE	(0x06)
static nop_t  init_nop  = {
	0,
	CMD_NOP,
	NOP_ADDR
};

/* at 0x003a */
#define TDR_ADDR	(0x003a)
#define TDR_SIZE	(0x08)
static tdr_t  init_tdr	= {
	0,
	CMD_TDR | CMD_INTR,
	NOP_ADDR,
	0
};

/* at 0x002e */
#define MC_ADDR		(0x002e)
#define MC_SIZE		(0x0c)
static mc_t   init_mc   = {
	0,
	CMD_SETMULTICAST,
	TDR_ADDR,
	0,
	{ { 0, } }
};

/* at 0x0022 */
#define SA_ADDR		(0x0022)
#define SA_SIZE		(0x0c)
static sa_t   init_sa   = {
	0,
	CMD_SETADDRESS,
	MC_ADDR,
	{ 0, }
};

/* at 0x0010 */
#define CFG_ADDR	(0x0010)
#define CFG_SIZE	(0x12)
static cfg_t  init_cfg  = {
	0,
	CMD_CONFIG,
	SA_ADDR,
	8,
	8,
	CFG8_SRDY,
	CFG9_PREAMB8 | CFG9_ADDRLENBUF | CFG9_ADDRLEN(6),
	0,
	0x60,
	0,
	CFG13_RETRY(15) | CFG13_SLOTH(2),
	0,
};

/* at 0x0000 */
#define SCB_ADDR	(0x0000)
#define SCB_SIZE	(0x10)
static scb_t  init_scb  = {
	0,
	SCB_CMDACKRNR | SCB_CMDACKCNA | SCB_CMDACKFR | SCB_CMDACKCX,
	CFG_ADDR,
	RX_AREA_START,
	0,
	0,
	0,
	0
};

/* at 0xffee */
#define ISCP_ADDR	(0xffee)
#define ISCP_SIZE	(0x08)
static iscp_t init_iscp = {
	1,
	SCB_ADDR,
	0x0000,
	0x0000
};

/* at 0xfff6 */
#define SCP_ADDR	(0xfff6)
#define SCP_SIZE	(0x0a)
static scp_t  init_scp  = {
	SCP_SY_16BBUS,
	{ 0, 0 },
	ISCP_ADDR,
	0
};

#define RFD_SIZE	(0x16)
static rfd_t  init_rfd	= {
	0,
	0,
	0,
	0,
	{ 0, },
	{ 0, },
	0
};

#define RBD_SIZE	(0x0a)
static rbd_t  init_rbd	= {
	0,
	0,
	0,
	0,
	ETH_FRAME_LEN + 8
};

#define TX_SIZE		(0x08)
#define TBD_SIZE	(0x08)

static int
ether1_init_for_open (struct net_device *dev)
{
	int i, status, addr, next, next2;
	int failures = 0;
	unsigned long timeout;

	writeb(CTRL_RST|CTRL_ACK, REG_CONTROL);

	for (i = 0; i < 6; i++)
		init_sa.sa_addr[i] = dev->dev_addr[i];

	/* load data structures into ether1 RAM */
	ether1_writebuffer (dev, &init_scp,  SCP_ADDR,  SCP_SIZE);
	ether1_writebuffer (dev, &init_iscp, ISCP_ADDR, ISCP_SIZE);
	ether1_writebuffer (dev, &init_scb,  SCB_ADDR,  SCB_SIZE);
	ether1_writebuffer (dev, &init_cfg,  CFG_ADDR,  CFG_SIZE);
	ether1_writebuffer (dev, &init_sa,   SA_ADDR,   SA_SIZE);
	ether1_writebuffer (dev, &init_mc,   MC_ADDR,   MC_SIZE);
	ether1_writebuffer (dev, &init_tdr,  TDR_ADDR,  TDR_SIZE);
	ether1_writebuffer (dev, &init_nop,  NOP_ADDR,  NOP_SIZE);

	if (ether1_readw(dev, CFG_ADDR, cfg_t, cfg_command, NORMALIRQS) != CMD_CONFIG) {
		printk (KERN_ERR "%s: detected either RAM fault or compiler bug\n",
			dev->name);
		return 1;
	}

	/*
	 * setup circularly linked list of { rfd, rbd, buffer }, with
	 * all rfds circularly linked, rbds circularly linked.
	 * First rfd is linked to scp, first rbd is linked to first
	 * rfd.  Last rbd has a suspend command.
	 */
	addr = RX_AREA_START;
	do {
		next = addr + RFD_SIZE + RBD_SIZE + ETH_FRAME_LEN + 10;
		next2 = next + RFD_SIZE + RBD_SIZE + ETH_FRAME_LEN + 10;

		if (next2 >= RX_AREA_END) {
			next = RX_AREA_START;
			init_rfd.rfd_command = RFD_CMDEL | RFD_CMDSUSPEND;
			priv(dev)->rx_tail = addr;
		} else
			init_rfd.rfd_command = 0;
		if (addr == RX_AREA_START)
			init_rfd.rfd_rbdoffset = addr + RFD_SIZE;
		else
			init_rfd.rfd_rbdoffset = 0;
		init_rfd.rfd_link = next;
		init_rbd.rbd_link = next + RFD_SIZE;
		init_rbd.rbd_bufl = addr + RFD_SIZE + RBD_SIZE;

		ether1_writebuffer (dev, &init_rfd, addr, RFD_SIZE);
		ether1_writebuffer (dev, &init_rbd, addr + RFD_SIZE, RBD_SIZE);
		addr = next;
	} while (next2 < RX_AREA_END);

	priv(dev)->tx_link = NOP_ADDR;
	priv(dev)->tx_head = NOP_ADDR + NOP_SIZE;
	priv(dev)->tx_tail = TDR_ADDR;
	priv(dev)->rx_head = RX_AREA_START;

	/* release reset & give 586 a prod */
	priv(dev)->resetting = 1;
	priv(dev)->initialising = 1;
	writeb(CTRL_RST, REG_CONTROL);
	writeb(0, REG_CONTROL);
	writeb(CTRL_CA, REG_CONTROL);

	/* 586 should now unset iscp.busy */
	timeout = jiffies + HZ/2;
	while (ether1_readw(dev, ISCP_ADDR, iscp_t, iscp_busy, DISABLEIRQS) == 1) {
		if (time_after(jiffies, timeout)) {
			printk (KERN_WARNING "%s: can't initialise 82586: iscp is busy\n", dev->name);
			return 1;
		}
	}

	/* check status of commands that we issued */
	timeout += HZ/10;
	while (((status = ether1_readw(dev, CFG_ADDR, cfg_t, cfg_status, DISABLEIRQS))
			& STAT_COMPLETE) == 0) {
		if (time_after(jiffies, timeout))
			break;
	}

	if ((status & (STAT_COMPLETE | STAT_OK)) != (STAT_COMPLETE | STAT_OK)) {
		printk (KERN_WARNING "%s: can't initialise 82586: config status %04X\n", dev->name, status);
		printk (KERN_DEBUG "%s: SCB=[STS=%04X CMD=%04X CBL=%04X RFA=%04X]\n", dev->name,
			ether1_readw(dev, SCB_ADDR, scb_t, scb_status, NORMALIRQS),
			ether1_readw(dev, SCB_ADDR, scb_t, scb_command, NORMALIRQS),
			ether1_readw(dev, SCB_ADDR, scb_t, scb_cbl_offset, NORMALIRQS),
			ether1_readw(dev, SCB_ADDR, scb_t, scb_rfa_offset, NORMALIRQS));
		failures += 1;
	}

	timeout += HZ/10;
	while (((status = ether1_readw(dev, SA_ADDR, sa_t, sa_status, DISABLEIRQS))
			& STAT_COMPLETE) == 0) {
		if (time_after(jiffies, timeout))
			break;
	}

	if ((status & (STAT_COMPLETE | STAT_OK)) != (STAT_COMPLETE | STAT_OK)) {
		printk (KERN_WARNING "%s: can't initialise 82586: set address status %04X\n", dev->name, status);
		printk (KERN_DEBUG "%s: SCB=[STS=%04X CMD=%04X CBL=%04X RFA=%04X]\n", dev->name,
			ether1_readw(dev, SCB_ADDR, scb_t, scb_status, NORMALIRQS),
			ether1_readw(dev, SCB_ADDR, scb_t, scb_command, NORMALIRQS),
			ether1_readw(dev, SCB_ADDR, scb_t, scb_cbl_offset, NORMALIRQS),
			ether1_readw(dev, SCB_ADDR, scb_t, scb_rfa_offset, NORMALIRQS));
		failures += 1;
	}

	timeout += HZ/10;
	while (((status = ether1_readw(dev, MC_ADDR, mc_t, mc_status, DISABLEIRQS))
			& STAT_COMPLETE) == 0) {
		if (time_after(jiffies, timeout))
			break;
	}

	if ((status & (STAT_COMPLETE | STAT_OK)) != (STAT_COMPLETE | STAT_OK)) {
		printk (KERN_WARNING "%s: can't initialise 82586: set multicast status %04X\n", dev->name, status);
		printk (KERN_DEBUG "%s: SCB=[STS=%04X CMD=%04X CBL=%04X RFA=%04X]\n", dev->name,
			ether1_readw(dev, SCB_ADDR, scb_t, scb_status, NORMALIRQS),
			ether1_readw(dev, SCB_ADDR, scb_t, scb_command, NORMALIRQS),
			ether1_readw(dev, SCB_ADDR, scb_t, scb_cbl_offset, NORMALIRQS),
			ether1_readw(dev, SCB_ADDR, scb_t, scb_rfa_offset, NORMALIRQS));
		failures += 1;
	}

	timeout += HZ;
	while (((status = ether1_readw(dev, TDR_ADDR, tdr_t, tdr_status, DISABLEIRQS))
			& STAT_COMPLETE) == 0) {
		if (time_after(jiffies, timeout))
			break;
	}

	if ((status & (STAT_COMPLETE | STAT_OK)) != (STAT_COMPLETE | STAT_OK)) {
		printk (KERN_WARNING "%s: can't tdr (ignored)\n", dev->name);
		printk (KERN_DEBUG "%s: SCB=[STS=%04X CMD=%04X CBL=%04X RFA=%04X]\n", dev->name,
			ether1_readw(dev, SCB_ADDR, scb_t, scb_status, NORMALIRQS),
			ether1_readw(dev, SCB_ADDR, scb_t, scb_command, NORMALIRQS),
			ether1_readw(dev, SCB_ADDR, scb_t, scb_cbl_offset, NORMALIRQS),
			ether1_readw(dev, SCB_ADDR, scb_t, scb_rfa_offset, NORMALIRQS));
	} else {
		status = ether1_readw(dev, TDR_ADDR, tdr_t, tdr_result, DISABLEIRQS);
		if (status & TDR_XCVRPROB)
			printk (KERN_WARNING "%s: i/f failed tdr: transceiver problem\n", dev->name);
		else if ((status & (TDR_SHORT|TDR_OPEN)) && (status & TDR_TIME)) {
#ifdef FANCY
			printk (KERN_WARNING "%s: i/f failed tdr: cable %s %d.%d us away\n", dev->name,
				status & TDR_SHORT ? "short" : "open", (status & TDR_TIME) / 10,
				(status & TDR_TIME) % 10);
#else
			printk (KERN_WARNING "%s: i/f failed tdr: cable %s %d clks away\n", dev->name,
				status & TDR_SHORT ? "short" : "open", (status & TDR_TIME));
#endif
		}
	}

	if (failures)
		ether1_reset (dev);
	return failures ? 1 : 0;
}

/* ------------------------------------------------------------------------- */

static int
ether1_txalloc (struct net_device *dev, int size)
{
	int start, tail;

	size = (size + 1) & ~1;
	tail = priv(dev)->tx_tail;

	if (priv(dev)->tx_head + size > TX_AREA_END) {
		if (tail > priv(dev)->tx_head)
			return -1;
		start = TX_AREA_START;
		if (start + size > tail)
			return -1;
		priv(dev)->tx_head = start + size;
	} else {
		if (priv(dev)->tx_head < tail && (priv(dev)->tx_head + size) > tail)
			return -1;
		start = priv(dev)->tx_head;
		priv(dev)->tx_head += size;
	}

	return start;
}

static int
ether1_open (struct net_device *dev)
{
	if (!is_valid_ether_addr(dev->dev_addr)) {
		printk(KERN_WARNING "%s: invalid ethernet MAC address\n",
			dev->name);
		return -EINVAL;
	}

	if (request_irq(dev->irq, ether1_interrupt, 0, "ether1", dev))
		return -EAGAIN;

	if (ether1_init_for_open (dev)) {
		free_irq (dev->irq, dev);
		return -EAGAIN;
	}

	netif_start_queue(dev);

	return 0;
}

static void
ether1_timeout(struct net_device *dev)
{
	printk(KERN_WARNING "%s: transmit timeout, network cable problem?\n",
		dev->name);
	printk(KERN_WARNING "%s: resetting device\n", dev->name);

	ether1_reset (dev);

	if (ether1_init_for_open (dev))
		printk (KERN_ERR "%s: unable to restart interface\n", dev->name);

	dev->stats.tx_errors++;
	netif_wake_queue(dev);
}

static int
ether1_sendpacket (struct sk_buff *skb, struct net_device *dev)
{
	int tmp, tst, nopaddr, txaddr, tbdaddr, dataddr;
	unsigned long flags;
	tx_t tx;
	tbd_t tbd;
	nop_t nop;

	if (priv(dev)->restart) {
		printk(KERN_WARNING "%s: resetting device\n", dev->name);

		ether1_reset(dev);

		if (ether1_init_for_open(dev))
			printk(KERN_ERR "%s: unable to restart interface\n", dev->name);
		else
			priv(dev)->restart = 0;
	}

	if (skb->len < ETH_ZLEN) {
		if (skb_padto(skb, ETH_ZLEN))
			goto out;
	}

	/*
	 * insert packet followed by a nop
	 */
	txaddr = ether1_txalloc (dev, TX_SIZE);
	tbdaddr = ether1_txalloc (dev, TBD_SIZE);
	dataddr = ether1_txalloc (dev, skb->len);
	nopaddr = ether1_txalloc (dev, NOP_SIZE);

	tx.tx_status = 0;
	tx.tx_command = CMD_TX | CMD_INTR;
	tx.tx_link = nopaddr;
	tx.tx_tbdoffset = tbdaddr;
	tbd.tbd_opts = TBD_EOL | skb->len;
	tbd.tbd_link = I82586_NULL;
	tbd.tbd_bufl = dataddr;
	tbd.tbd_bufh = 0;
	nop.nop_status = 0;
	nop.nop_command = CMD_NOP;
	nop.nop_link = nopaddr;

	local_irq_save(flags);
	ether1_writebuffer (dev, &tx, txaddr, TX_SIZE);
	ether1_writebuffer (dev, &tbd, tbdaddr, TBD_SIZE);
	ether1_writebuffer (dev, skb->data, dataddr, skb->len);
	ether1_writebuffer (dev, &nop, nopaddr, NOP_SIZE);
	tmp = priv(dev)->tx_link;
	priv(dev)->tx_link = nopaddr;

	/* now reset the previous nop pointer */
	ether1_writew(dev, txaddr, tmp, nop_t, nop_link, NORMALIRQS);

	local_irq_restore(flags);

	/* handle transmit */

	/* check to see if we have room for a full sized ether frame */
	tmp = priv(dev)->tx_head;
	tst = ether1_txalloc (dev, TX_SIZE + TBD_SIZE + NOP_SIZE + ETH_FRAME_LEN);
	priv(dev)->tx_head = tmp;
	dev_kfree_skb (skb);

	if (tst == -1)
		netif_stop_queue(dev);

 out:
	return NETDEV_TX_OK;
}

static void
ether1_xmit_done (struct net_device *dev)
{
	nop_t nop;
	int caddr, tst;

	caddr = priv(dev)->tx_tail;

again:
	ether1_readbuffer (dev, &nop, caddr, NOP_SIZE);

	switch (nop.nop_command & CMD_MASK) {
	case CMD_TDR:
		/* special case */
		if (ether1_readw(dev, SCB_ADDR, scb_t, scb_cbl_offset, NORMALIRQS)
				!= (unsigned short)I82586_NULL) {
			ether1_writew(dev, SCB_CMDCUCSTART | SCB_CMDRXSTART, SCB_ADDR, scb_t,
				    scb_command, NORMALIRQS);
			writeb(CTRL_CA, REG_CONTROL);
		}
		priv(dev)->tx_tail = NOP_ADDR;
		return;

	case CMD_NOP:
		if (nop.nop_link == caddr) {
			if (priv(dev)->initialising == 0)
				printk (KERN_WARNING "%s: strange command complete with no tx command!\n", dev->name);
			else
			        priv(dev)->initialising = 0;
			return;
		}
		if (caddr == nop.nop_link)
			return;
		caddr = nop.nop_link;
		goto again;

	case CMD_TX:
		if (nop.nop_status & STAT_COMPLETE)
			break;
		printk (KERN_ERR "%s: strange command complete without completed command\n", dev->name);
		priv(dev)->restart = 1;
		return;

	default:
		printk (KERN_WARNING "%s: strange command %d complete! (offset %04X)", dev->name,
			nop.nop_command & CMD_MASK, caddr);
		priv(dev)->restart = 1;
		return;
	}

	while (nop.nop_status & STAT_COMPLETE) {
		if (nop.nop_status & STAT_OK) {
			dev->stats.tx_packets++;
			dev->stats.collisions += (nop.nop_status & STAT_COLLISIONS);
		} else {
			dev->stats.tx_errors++;

			if (nop.nop_status & STAT_COLLAFTERTX)
				dev->stats.collisions++;
			if (nop.nop_status & STAT_NOCARRIER)
				dev->stats.tx_carrier_errors++;
			if (nop.nop_status & STAT_TXLOSTCTS)
				printk (KERN_WARNING "%s: cts lost\n", dev->name);
			if (nop.nop_status & STAT_TXSLOWDMA)
				dev->stats.tx_fifo_errors++;
			if (nop.nop_status & STAT_COLLEXCESSIVE)
				dev->stats.collisions += 16;
		}

		if (nop.nop_link == caddr) {
			printk (KERN_ERR "%s: tx buffer chaining error: tx command points to itself\n", dev->name);
			break;
		}

		caddr = nop.nop_link;
		ether1_readbuffer (dev, &nop, caddr, NOP_SIZE);
		if ((nop.nop_command & CMD_MASK) != CMD_NOP) {
			printk (KERN_ERR "%s: tx buffer chaining error: no nop after tx command\n", dev->name);
			break;
		}

		if (caddr == nop.nop_link)
			break;

		caddr = nop.nop_link;
		ether1_readbuffer (dev, &nop, caddr, NOP_SIZE);
		if ((nop.nop_command & CMD_MASK) != CMD_TX) {
			printk (KERN_ERR "%s: tx buffer chaining error: no tx command after nop\n", dev->name);
			break;
		}
	}
	priv(dev)->tx_tail = caddr;

	caddr = priv(dev)->tx_head;
	tst = ether1_txalloc (dev, TX_SIZE + TBD_SIZE + NOP_SIZE + ETH_FRAME_LEN);
	priv(dev)->tx_head = caddr;
	if (tst != -1)
		netif_wake_queue(dev);
}

static void
ether1_recv_done (struct net_device *dev)
{
	int status;
	int nexttail, rbdaddr;
	rbd_t rbd;

	do {
		status = ether1_readw(dev, priv(dev)->rx_head, rfd_t, rfd_status, NORMALIRQS);
		if ((status & RFD_COMPLETE) == 0)
			break;

		rbdaddr = ether1_readw(dev, priv(dev)->rx_head, rfd_t, rfd_rbdoffset, NORMALIRQS);
		ether1_readbuffer (dev, &rbd, rbdaddr, RBD_SIZE);

		if ((rbd.rbd_status & (RBD_EOF | RBD_ACNTVALID)) == (RBD_EOF | RBD_ACNTVALID)) {
			int length = rbd.rbd_status & RBD_ACNT;
			struct sk_buff *skb;

			length = (length + 1) & ~1;
			skb = dev_alloc_skb (length + 2);

			if (skb) {
				skb_reserve (skb, 2);

				ether1_readbuffer (dev, skb_put (skb, length), rbd.rbd_bufl, length);

				skb->protocol = eth_type_trans (skb, dev);
				netif_rx (skb);
				dev->stats.rx_packets++;
			} else
				dev->stats.rx_dropped++;
		} else {
			printk(KERN_WARNING "%s: %s\n", dev->name,
				(rbd.rbd_status & RBD_EOF) ? "oversized packet" : "acnt not valid");
			dev->stats.rx_dropped++;
		}

		nexttail = ether1_readw(dev, priv(dev)->rx_tail, rfd_t, rfd_link, NORMALIRQS);
		/* nexttail should be rx_head */
		if (nexttail != priv(dev)->rx_head)
			printk(KERN_ERR "%s: receiver buffer chaining error (%04X != %04X)\n",
				dev->name, nexttail, priv(dev)->rx_head);
		ether1_writew(dev, RFD_CMDEL | RFD_CMDSUSPEND, nexttail, rfd_t, rfd_command, NORMALIRQS);
		ether1_writew(dev, 0, priv(dev)->rx_tail, rfd_t, rfd_command, NORMALIRQS);
		ether1_writew(dev, 0, priv(dev)->rx_tail, rfd_t, rfd_status, NORMALIRQS);
		ether1_writew(dev, 0, priv(dev)->rx_tail, rfd_t, rfd_rbdoffset, NORMALIRQS);
	
		priv(dev)->rx_tail = nexttail;
		priv(dev)->rx_head = ether1_readw(dev, priv(dev)->rx_head, rfd_t, rfd_link, NORMALIRQS);
	} while (1);
}

static irqreturn_t
ether1_interrupt (int irq, void *dev_id)
{
	struct net_device *dev = (struct net_device *)dev_id;
	int status;

	status = ether1_readw(dev, SCB_ADDR, scb_t, scb_status, NORMALIRQS);

	if (status) {
		ether1_writew(dev, status & (SCB_STRNR | SCB_STCNA | SCB_STFR | SCB_STCX),
			    SCB_ADDR, scb_t, scb_command, NORMALIRQS);
		writeb(CTRL_CA | CTRL_ACK, REG_CONTROL);
		if (status & SCB_STCX) {
			ether1_xmit_done (dev);
		}
		if (status & SCB_STCNA) {
			if (priv(dev)->resetting == 0)
				printk (KERN_WARNING "%s: CU went not ready ???\n", dev->name);
			else
				priv(dev)->resetting += 1;
			if (ether1_readw(dev, SCB_ADDR, scb_t, scb_cbl_offset, NORMALIRQS)
					!= (unsigned short)I82586_NULL) {
				ether1_writew(dev, SCB_CMDCUCSTART, SCB_ADDR, scb_t, scb_command, NORMALIRQS);
				writeb(CTRL_CA, REG_CONTROL);
			}
			if (priv(dev)->resetting == 2)
				priv(dev)->resetting = 0;
		}
		if (status & SCB_STFR) {
			ether1_recv_done (dev);
		}
		if (status & SCB_STRNR) {
			if (ether1_readw(dev, SCB_ADDR, scb_t, scb_status, NORMALIRQS) & SCB_STRXSUSP) {
				printk (KERN_WARNING "%s: RU went not ready: RU suspended\n", dev->name);
				ether1_writew(dev, SCB_CMDRXRESUME, SCB_ADDR, scb_t, scb_command, NORMALIRQS);
				writeb(CTRL_CA, REG_CONTROL);
				dev->stats.rx_dropped++;	/* we suspended due to lack of buffer space */
			} else
				printk(KERN_WARNING "%s: RU went not ready: %04X\n", dev->name,
					ether1_readw(dev, SCB_ADDR, scb_t, scb_status, NORMALIRQS));
			printk (KERN_WARNING "RU ptr = %04X\n", ether1_readw(dev, SCB_ADDR, scb_t, scb_rfa_offset,
						NORMALIRQS));
		}
	} else
		writeb(CTRL_ACK, REG_CONTROL);

	return IRQ_HANDLED;
}

static int
ether1_close (struct net_device *dev)
{
	ether1_reset (dev);

	free_irq(dev->irq, dev);

	return 0;
}

/*
 * Set or clear the multicast filter for this adaptor.
 * num_addrs == -1	Promiscuous mode, receive all packets.
 * num_addrs == 0	Normal mode, clear multicast list.
 * num_addrs > 0	Multicast mode, receive normal and MC packets, and do
 *			best-effort filtering.
 */
static void
ether1_setmulticastlist (struct net_device *dev)
{
}

/* ------------------------------------------------------------------------- */

static void __devinit ether1_banner(void)
{
	static unsigned int version_printed = 0;

	if (net_debug && version_printed++ == 0)
		printk(KERN_INFO "%s", version);
}

static const struct net_device_ops ether1_netdev_ops = {
	.ndo_open		= ether1_open,
	.ndo_stop		= ether1_close,
	.ndo_start_xmit		= ether1_sendpacket,
	.ndo_set_rx_mode	= ether1_setmulticastlist,
	.ndo_tx_timeout		= ether1_timeout,
	.ndo_validate_addr	= eth_validate_addr,
	.ndo_change_mtu		= eth_change_mtu,
	.ndo_set_mac_address	= eth_mac_addr,
};

static int __devinit
ether1_probe(struct expansion_card *ec, const struct ecard_id *id)
{
	struct net_device *dev;
	int i, ret = 0;

	ether1_banner();

	ret = ecard_request_resources(ec);
	if (ret)
		goto out;

	dev = alloc_etherdev(sizeof(struct ether1_priv));
	if (!dev) {
		ret = -ENOMEM;
		goto release;
	}

	SET_NETDEV_DEV(dev, &ec->dev);

	dev->irq = ec->irq;
	priv(dev)->base = ecardm_iomap(ec, ECARD_RES_IOCFAST, 0, 0);
	if (!priv(dev)->base) {
		ret = -ENOMEM;
		goto free;
	}

	if ((priv(dev)->bus_type = ether1_reset(dev)) == 0) {
		ret = -ENODEV;
		goto free;
	}

	for (i = 0; i < 6; i++)
		dev->dev_addr[i] = readb(IDPROM_ADDRESS + (i << 2));

	if (ether1_init_2(dev)) {
		ret = -ENODEV;
		goto free;
	}

	dev->netdev_ops		= &ether1_netdev_ops;
	dev->watchdog_timeo	= 5 * HZ / 100;

	ret = register_netdev(dev);
	if (ret)
		goto free;

	printk(KERN_INFO "%s: ether1 in slot %d, %pM\n",
		dev->name, ec->slot_no, dev->dev_addr);
    
	ecard_set_drvdata(ec, dev);
	return 0;

 free:
	free_netdev(dev);
 release:
	ecard_release_resources(ec);
 out:
	return ret;
}

static void __devexit ether1_remove(struct expansion_card *ec)
{
	struct net_device *dev = ecard_get_drvdata(ec);

	ecard_set_drvdata(ec, NULL);	

	unregister_netdev(dev);
	free_netdev(dev);
	ecard_release_resources(ec);
}

static const struct ecard_id ether1_ids[] = {
	{ MANU_ACORN, PROD_ACORN_ETHER1 },
	{ 0xffff, 0xffff }
};

static struct ecard_driver ether1_driver = {
	.probe		= ether1_probe,
	.remove		= __devexit_p(ether1_remove),
	.id_table	= ether1_ids,
	.drv = {
		.name	= "ether1",
	},
};

static int __init ether1_init(void)
{
	return ecard_register_driver(&ether1_driver);
}

static void __exit ether1_exit(void)
{
	ecard_remove_driver(&ether1_driver);
}

module_init(ether1_init);
module_exit(ether1_exit);

MODULE_LICENSE("GPL");
