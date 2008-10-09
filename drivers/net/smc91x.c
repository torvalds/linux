/*
 * smc91x.c
 * This is a driver for SMSC's 91C9x/91C1xx single-chip Ethernet devices.
 *
 * Copyright (C) 1996 by Erik Stahlman
 * Copyright (C) 2001 Standard Microsystems Corporation
 *	Developed by Simple Network Magic Corporation
 * Copyright (C) 2003 Monta Vista Software, Inc.
 *	Unified SMC91x driver by Nicolas Pitre
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * Arguments:
 * 	io	= for the base address
 *	irq	= for the IRQ
 *	nowait	= 0 for normal wait states, 1 eliminates additional wait states
 *
 * original author:
 * 	Erik Stahlman <erik@vt.edu>
 *
 * hardware multicast code:
 *    Peter Cammaert <pc@denkart.be>
 *
 * contributors:
 * 	Daris A Nevil <dnevil@snmc.com>
 *      Nicolas Pitre <nico@cam.org>
 *	Russell King <rmk@arm.linux.org.uk>
 *
 * History:
 *   08/20/00  Arnaldo Melo       fix kfree(skb) in smc_hardware_send_packet
 *   12/15/00  Christian Jullien  fix "Warning: kfree_skb on hard IRQ"
 *   03/16/01  Daris A Nevil      modified smc9194.c for use with LAN91C111
 *   08/22/01  Scott Anderson     merge changes from smc9194 to smc91111
 *   08/21/01  Pramod B Bhardwaj  added support for RevB of LAN91C111
 *   12/20/01  Jeff Sutherland    initial port to Xscale PXA with DMA support
 *   04/07/03  Nicolas Pitre      unified SMC91x driver, killed irq races,
 *                                more bus abstraction, big cleanup, etc.
 *   29/09/03  Russell King       - add driver model support
 *                                - ethtool support
 *                                - convert to use generic MII interface
 *                                - add link up/down notification
 *                                - don't try to handle full negotiation in
 *                                  smc_phy_configure
 *                                - clean up (and fix stack overrun) in PHY
 *                                  MII read/write functions
 *   22/09/04  Nicolas Pitre      big update (see commit log for details)
 */
static const char version[] =
	"smc91x.c: v1.1, sep 22 2004 by Nicolas Pitre <nico@cam.org>\n";

/* Debugging level */
#ifndef SMC_DEBUG
#define SMC_DEBUG		0
#endif


#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/errno.h>
#include <linux/ioport.h>
#include <linux/crc32.h>
#include <linux/platform_device.h>
#include <linux/spinlock.h>
#include <linux/ethtool.h>
#include <linux/mii.h>
#include <linux/workqueue.h>

#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>

#include <asm/io.h>

#include "smc91x.h"

#ifdef CONFIG_ISA
/*
 * the LAN91C111 can be at any of the following port addresses.  To change,
 * for a slightly different card, you can add it to the array.  Keep in
 * mind that the array must end in zero.
 */
static unsigned int smc_portlist[] __initdata = {
	0x200, 0x220, 0x240, 0x260, 0x280, 0x2A0, 0x2C0, 0x2E0,
	0x300, 0x320, 0x340, 0x360, 0x380, 0x3A0, 0x3C0, 0x3E0, 0
};

#ifndef SMC_IOADDR
# define SMC_IOADDR		-1
#endif
static unsigned long io = SMC_IOADDR;
module_param(io, ulong, 0400);
MODULE_PARM_DESC(io, "I/O base address");

#ifndef SMC_IRQ
# define SMC_IRQ		-1
#endif
static int irq = SMC_IRQ;
module_param(irq, int, 0400);
MODULE_PARM_DESC(irq, "IRQ number");

#endif  /* CONFIG_ISA */

#ifndef SMC_NOWAIT
# define SMC_NOWAIT		0
#endif
static int nowait = SMC_NOWAIT;
module_param(nowait, int, 0400);
MODULE_PARM_DESC(nowait, "set to 1 for no wait state");

/*
 * Transmit timeout, default 5 seconds.
 */
static int watchdog = 1000;
module_param(watchdog, int, 0400);
MODULE_PARM_DESC(watchdog, "transmit timeout in milliseconds");

MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:smc91x");

/*
 * The internal workings of the driver.  If you are changing anything
 * here with the SMC stuff, you should have the datasheet and know
 * what you are doing.
 */
#define CARDNAME "smc91x"

/*
 * Use power-down feature of the chip
 */
#define POWER_DOWN		1

/*
 * Wait time for memory to be free.  This probably shouldn't be
 * tuned that much, as waiting for this means nothing else happens
 * in the system
 */
#define MEMORY_WAIT_TIME	16

/*
 * The maximum number of processing loops allowed for each call to the
 * IRQ handler.
 */
#define MAX_IRQ_LOOPS		8

/*
 * This selects whether TX packets are sent one by one to the SMC91x internal
 * memory and throttled until transmission completes.  This may prevent
 * RX overruns a litle by keeping much of the memory free for RX packets
 * but to the expense of reduced TX throughput and increased IRQ overhead.
 * Note this is not a cure for a too slow data bus or too high IRQ latency.
 */
#define THROTTLE_TX_PKTS	0

/*
 * The MII clock high/low times.  2x this number gives the MII clock period
 * in microseconds. (was 50, but this gives 6.4ms for each MII transaction!)
 */
#define MII_DELAY		1

#if SMC_DEBUG > 0
#define DBG(n, args...)					\
	do {						\
		if (SMC_DEBUG >= (n))			\
			printk(args);	\
	} while (0)

#define PRINTK(args...)   printk(args)
#else
#define DBG(n, args...)   do { } while(0)
#define PRINTK(args...)   printk(KERN_DEBUG args)
#endif

#if SMC_DEBUG > 3
static void PRINT_PKT(u_char *buf, int length)
{
	int i;
	int remainder;
	int lines;

	lines = length / 16;
	remainder = length % 16;

	for (i = 0; i < lines ; i ++) {
		int cur;
		for (cur = 0; cur < 8; cur++) {
			u_char a, b;
			a = *buf++;
			b = *buf++;
			printk("%02x%02x ", a, b);
		}
		printk("\n");
	}
	for (i = 0; i < remainder/2 ; i++) {
		u_char a, b;
		a = *buf++;
		b = *buf++;
		printk("%02x%02x ", a, b);
	}
	printk("\n");
}
#else
#define PRINT_PKT(x...)  do { } while(0)
#endif


/* this enables an interrupt in the interrupt mask register */
#define SMC_ENABLE_INT(lp, x) do {					\
	unsigned char mask;						\
	spin_lock_irq(&lp->lock);					\
	mask = SMC_GET_INT_MASK(lp);					\
	mask |= (x);							\
	SMC_SET_INT_MASK(lp, mask);					\
	spin_unlock_irq(&lp->lock);					\
} while (0)

/* this disables an interrupt from the interrupt mask register */
#define SMC_DISABLE_INT(lp, x) do {					\
	unsigned char mask;						\
	spin_lock_irq(&lp->lock);					\
	mask = SMC_GET_INT_MASK(lp);					\
	mask &= ~(x);							\
	SMC_SET_INT_MASK(lp, mask);					\
	spin_unlock_irq(&lp->lock);					\
} while (0)

/*
 * Wait while MMU is busy.  This is usually in the order of a few nanosecs
 * if at all, but let's avoid deadlocking the system if the hardware
 * decides to go south.
 */
#define SMC_WAIT_MMU_BUSY(lp) do {					\
	if (unlikely(SMC_GET_MMU_CMD(lp) & MC_BUSY)) {		\
		unsigned long timeout = jiffies + 2;			\
		while (SMC_GET_MMU_CMD(lp) & MC_BUSY) {		\
			if (time_after(jiffies, timeout)) {		\
				printk("%s: timeout %s line %d\n",	\
					dev->name, __FILE__, __LINE__);	\
				break;					\
			}						\
			cpu_relax();					\
		}							\
	}								\
} while (0)


/*
 * this does a soft reset on the device
 */
static void smc_reset(struct net_device *dev)
{
	struct smc_local *lp = netdev_priv(dev);
	void __iomem *ioaddr = lp->base;
	unsigned int ctl, cfg;
	struct sk_buff *pending_skb;

	DBG(2, "%s: %s\n", dev->name, __FUNCTION__);

	/* Disable all interrupts, block TX tasklet */
	spin_lock_irq(&lp->lock);
	SMC_SELECT_BANK(lp, 2);
	SMC_SET_INT_MASK(lp, 0);
	pending_skb = lp->pending_tx_skb;
	lp->pending_tx_skb = NULL;
	spin_unlock_irq(&lp->lock);

	/* free any pending tx skb */
	if (pending_skb) {
		dev_kfree_skb(pending_skb);
		dev->stats.tx_errors++;
		dev->stats.tx_aborted_errors++;
	}

	/*
	 * This resets the registers mostly to defaults, but doesn't
	 * affect EEPROM.  That seems unnecessary
	 */
	SMC_SELECT_BANK(lp, 0);
	SMC_SET_RCR(lp, RCR_SOFTRST);

	/*
	 * Setup the Configuration Register
	 * This is necessary because the CONFIG_REG is not affected
	 * by a soft reset
	 */
	SMC_SELECT_BANK(lp, 1);

	cfg = CONFIG_DEFAULT;

	/*
	 * Setup for fast accesses if requested.  If the card/system
	 * can't handle it then there will be no recovery except for
	 * a hard reset or power cycle
	 */
	if (lp->cfg.flags & SMC91X_NOWAIT)
		cfg |= CONFIG_NO_WAIT;

	/*
	 * Release from possible power-down state
	 * Configuration register is not affected by Soft Reset
	 */
	cfg |= CONFIG_EPH_POWER_EN;

	SMC_SET_CONFIG(lp, cfg);

	/* this should pause enough for the chip to be happy */
	/*
	 * elaborate?  What does the chip _need_? --jgarzik
	 *
	 * This seems to be undocumented, but something the original
	 * driver(s) have always done.  Suspect undocumented timing
	 * info/determined empirically. --rmk
	 */
	udelay(1);

	/* Disable transmit and receive functionality */
	SMC_SELECT_BANK(lp, 0);
	SMC_SET_RCR(lp, RCR_CLEAR);
	SMC_SET_TCR(lp, TCR_CLEAR);

	SMC_SELECT_BANK(lp, 1);
	ctl = SMC_GET_CTL(lp) | CTL_LE_ENABLE;

	/*
	 * Set the control register to automatically release successfully
	 * transmitted packets, to make the best use out of our limited
	 * memory
	 */
	if(!THROTTLE_TX_PKTS)
		ctl |= CTL_AUTO_RELEASE;
	else
		ctl &= ~CTL_AUTO_RELEASE;
	SMC_SET_CTL(lp, ctl);

	/* Reset the MMU */
	SMC_SELECT_BANK(lp, 2);
	SMC_SET_MMU_CMD(lp, MC_RESET);
	SMC_WAIT_MMU_BUSY(lp);
}

/*
 * Enable Interrupts, Receive, and Transmit
 */
static void smc_enable(struct net_device *dev)
{
	struct smc_local *lp = netdev_priv(dev);
	void __iomem *ioaddr = lp->base;
	int mask;

	DBG(2, "%s: %s\n", dev->name, __FUNCTION__);

	/* see the header file for options in TCR/RCR DEFAULT */
	SMC_SELECT_BANK(lp, 0);
	SMC_SET_TCR(lp, lp->tcr_cur_mode);
	SMC_SET_RCR(lp, lp->rcr_cur_mode);

	SMC_SELECT_BANK(lp, 1);
	SMC_SET_MAC_ADDR(lp, dev->dev_addr);

	/* now, enable interrupts */
	mask = IM_EPH_INT|IM_RX_OVRN_INT|IM_RCV_INT;
	if (lp->version >= (CHIP_91100 << 4))
		mask |= IM_MDINT;
	SMC_SELECT_BANK(lp, 2);
	SMC_SET_INT_MASK(lp, mask);

	/*
	 * From this point the register bank must _NOT_ be switched away
	 * to something else than bank 2 without proper locking against
	 * races with any tasklet or interrupt handlers until smc_shutdown()
	 * or smc_reset() is called.
	 */
}

/*
 * this puts the device in an inactive state
 */
static void smc_shutdown(struct net_device *dev)
{
	struct smc_local *lp = netdev_priv(dev);
	void __iomem *ioaddr = lp->base;
	struct sk_buff *pending_skb;

	DBG(2, "%s: %s\n", CARDNAME, __FUNCTION__);

	/* no more interrupts for me */
	spin_lock_irq(&lp->lock);
	SMC_SELECT_BANK(lp, 2);
	SMC_SET_INT_MASK(lp, 0);
	pending_skb = lp->pending_tx_skb;
	lp->pending_tx_skb = NULL;
	spin_unlock_irq(&lp->lock);
	if (pending_skb)
		dev_kfree_skb(pending_skb);

	/* and tell the card to stay away from that nasty outside world */
	SMC_SELECT_BANK(lp, 0);
	SMC_SET_RCR(lp, RCR_CLEAR);
	SMC_SET_TCR(lp, TCR_CLEAR);

#ifdef POWER_DOWN
	/* finally, shut the chip down */
	SMC_SELECT_BANK(lp, 1);
	SMC_SET_CONFIG(lp, SMC_GET_CONFIG(lp) & ~CONFIG_EPH_POWER_EN);
#endif
}

/*
 * This is the procedure to handle the receipt of a packet.
 */
static inline void  smc_rcv(struct net_device *dev)
{
	struct smc_local *lp = netdev_priv(dev);
	void __iomem *ioaddr = lp->base;
	unsigned int packet_number, status, packet_len;

	DBG(3, "%s: %s\n", dev->name, __FUNCTION__);

	packet_number = SMC_GET_RXFIFO(lp);
	if (unlikely(packet_number & RXFIFO_REMPTY)) {
		PRINTK("%s: smc_rcv with nothing on FIFO.\n", dev->name);
		return;
	}

	/* read from start of packet */
	SMC_SET_PTR(lp, PTR_READ | PTR_RCV | PTR_AUTOINC);

	/* First two words are status and packet length */
	SMC_GET_PKT_HDR(lp, status, packet_len);
	packet_len &= 0x07ff;  /* mask off top bits */
	DBG(2, "%s: RX PNR 0x%x STATUS 0x%04x LENGTH 0x%04x (%d)\n",
		dev->name, packet_number, status,
		packet_len, packet_len);

	back:
	if (unlikely(packet_len < 6 || status & RS_ERRORS)) {
		if (status & RS_TOOLONG && packet_len <= (1514 + 4 + 6)) {
			/* accept VLAN packets */
			status &= ~RS_TOOLONG;
			goto back;
		}
		if (packet_len < 6) {
			/* bloody hardware */
			printk(KERN_ERR "%s: fubar (rxlen %u status %x\n",
					dev->name, packet_len, status);
			status |= RS_TOOSHORT;
		}
		SMC_WAIT_MMU_BUSY(lp);
		SMC_SET_MMU_CMD(lp, MC_RELEASE);
		dev->stats.rx_errors++;
		if (status & RS_ALGNERR)
			dev->stats.rx_frame_errors++;
		if (status & (RS_TOOSHORT | RS_TOOLONG))
			dev->stats.rx_length_errors++;
		if (status & RS_BADCRC)
			dev->stats.rx_crc_errors++;
	} else {
		struct sk_buff *skb;
		unsigned char *data;
		unsigned int data_len;

		/* set multicast stats */
		if (status & RS_MULTICAST)
			dev->stats.multicast++;

		/*
		 * Actual payload is packet_len - 6 (or 5 if odd byte).
		 * We want skb_reserve(2) and the final ctrl word
		 * (2 bytes, possibly containing the payload odd byte).
		 * Furthermore, we add 2 bytes to allow rounding up to
		 * multiple of 4 bytes on 32 bit buses.
		 * Hence packet_len - 6 + 2 + 2 + 2.
		 */
		skb = dev_alloc_skb(packet_len);
		if (unlikely(skb == NULL)) {
			printk(KERN_NOTICE "%s: Low memory, packet dropped.\n",
				dev->name);
			SMC_WAIT_MMU_BUSY(lp);
			SMC_SET_MMU_CMD(lp, MC_RELEASE);
			dev->stats.rx_dropped++;
			return;
		}

		/* Align IP header to 32 bits */
		skb_reserve(skb, 2);

		/* BUG: the LAN91C111 rev A never sets this bit. Force it. */
		if (lp->version == 0x90)
			status |= RS_ODDFRAME;

		/*
		 * If odd length: packet_len - 5,
		 * otherwise packet_len - 6.
		 * With the trailing ctrl byte it's packet_len - 4.
		 */
		data_len = packet_len - ((status & RS_ODDFRAME) ? 5 : 6);
		data = skb_put(skb, data_len);
		SMC_PULL_DATA(lp, data, packet_len - 4);

		SMC_WAIT_MMU_BUSY(lp);
		SMC_SET_MMU_CMD(lp, MC_RELEASE);

		PRINT_PKT(data, packet_len - 4);

		dev->last_rx = jiffies;
		skb->protocol = eth_type_trans(skb, dev);
		netif_rx(skb);
		dev->stats.rx_packets++;
		dev->stats.rx_bytes += data_len;
	}
}

#ifdef CONFIG_SMP
/*
 * On SMP we have the following problem:
 *
 * 	A = smc_hardware_send_pkt()
 * 	B = smc_hard_start_xmit()
 * 	C = smc_interrupt()
 *
 * A and B can never be executed simultaneously.  However, at least on UP,
 * it is possible (and even desirable) for C to interrupt execution of
 * A or B in order to have better RX reliability and avoid overruns.
 * C, just like A and B, must have exclusive access to the chip and
 * each of them must lock against any other concurrent access.
 * Unfortunately this is not possible to have C suspend execution of A or
 * B taking place on another CPU. On UP this is no an issue since A and B
 * are run from softirq context and C from hard IRQ context, and there is
 * no other CPU where concurrent access can happen.
 * If ever there is a way to force at least B and C to always be executed
 * on the same CPU then we could use read/write locks to protect against
 * any other concurrent access and C would always interrupt B. But life
 * isn't that easy in a SMP world...
 */
#define smc_special_trylock(lock)					\
({									\
	int __ret;							\
	local_irq_disable();						\
	__ret = spin_trylock(lock);					\
	if (!__ret)							\
		local_irq_enable();					\
	__ret;								\
})
#define smc_special_lock(lock)		spin_lock_irq(lock)
#define smc_special_unlock(lock)	spin_unlock_irq(lock)
#else
#define smc_special_trylock(lock)	(1)
#define smc_special_lock(lock)		do { } while (0)
#define smc_special_unlock(lock)	do { } while (0)
#endif

/*
 * This is called to actually send a packet to the chip.
 */
static void smc_hardware_send_pkt(unsigned long data)
{
	struct net_device *dev = (struct net_device *)data;
	struct smc_local *lp = netdev_priv(dev);
	void __iomem *ioaddr = lp->base;
	struct sk_buff *skb;
	unsigned int packet_no, len;
	unsigned char *buf;

	DBG(3, "%s: %s\n", dev->name, __FUNCTION__);

	if (!smc_special_trylock(&lp->lock)) {
		netif_stop_queue(dev);
		tasklet_schedule(&lp->tx_task);
		return;
	}

	skb = lp->pending_tx_skb;
	if (unlikely(!skb)) {
		smc_special_unlock(&lp->lock);
		return;
	}
	lp->pending_tx_skb = NULL;

	packet_no = SMC_GET_AR(lp);
	if (unlikely(packet_no & AR_FAILED)) {
		printk("%s: Memory allocation failed.\n", dev->name);
		dev->stats.tx_errors++;
		dev->stats.tx_fifo_errors++;
		smc_special_unlock(&lp->lock);
		goto done;
	}

	/* point to the beginning of the packet */
	SMC_SET_PN(lp, packet_no);
	SMC_SET_PTR(lp, PTR_AUTOINC);

	buf = skb->data;
	len = skb->len;
	DBG(2, "%s: TX PNR 0x%x LENGTH 0x%04x (%d) BUF 0x%p\n",
		dev->name, packet_no, len, len, buf);
	PRINT_PKT(buf, len);

	/*
	 * Send the packet length (+6 for status words, length, and ctl.
	 * The card will pad to 64 bytes with zeroes if packet is too small.
	 */
	SMC_PUT_PKT_HDR(lp, 0, len + 6);

	/* send the actual data */
	SMC_PUSH_DATA(lp, buf, len & ~1);

	/* Send final ctl word with the last byte if there is one */
	SMC_outw(((len & 1) ? (0x2000 | buf[len-1]) : 0), ioaddr, DATA_REG(lp));

	/*
	 * If THROTTLE_TX_PKTS is set, we stop the queue here. This will
	 * have the effect of having at most one packet queued for TX
	 * in the chip's memory at all time.
	 *
	 * If THROTTLE_TX_PKTS is not set then the queue is stopped only
	 * when memory allocation (MC_ALLOC) does not succeed right away.
	 */
	if (THROTTLE_TX_PKTS)
		netif_stop_queue(dev);

	/* queue the packet for TX */
	SMC_SET_MMU_CMD(lp, MC_ENQUEUE);
	smc_special_unlock(&lp->lock);

	dev->trans_start = jiffies;
	dev->stats.tx_packets++;
	dev->stats.tx_bytes += len;

	SMC_ENABLE_INT(lp, IM_TX_INT | IM_TX_EMPTY_INT);

done:	if (!THROTTLE_TX_PKTS)
		netif_wake_queue(dev);

	dev_kfree_skb(skb);
}

/*
 * Since I am not sure if I will have enough room in the chip's ram
 * to store the packet, I call this routine which either sends it
 * now, or set the card to generates an interrupt when ready
 * for the packet.
 */
static int smc_hard_start_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct smc_local *lp = netdev_priv(dev);
	void __iomem *ioaddr = lp->base;
	unsigned int numPages, poll_count, status;

	DBG(3, "%s: %s\n", dev->name, __FUNCTION__);

	BUG_ON(lp->pending_tx_skb != NULL);

	/*
	 * The MMU wants the number of pages to be the number of 256 bytes
	 * 'pages', minus 1 (since a packet can't ever have 0 pages :))
	 *
	 * The 91C111 ignores the size bits, but earlier models don't.
	 *
	 * Pkt size for allocating is data length +6 (for additional status
	 * words, length and ctl)
	 *
	 * If odd size then last byte is included in ctl word.
	 */
	numPages = ((skb->len & ~1) + (6 - 1)) >> 8;
	if (unlikely(numPages > 7)) {
		printk("%s: Far too big packet error.\n", dev->name);
		dev->stats.tx_errors++;
		dev->stats.tx_dropped++;
		dev_kfree_skb(skb);
		return 0;
	}

	smc_special_lock(&lp->lock);

	/* now, try to allocate the memory */
	SMC_SET_MMU_CMD(lp, MC_ALLOC | numPages);

	/*
	 * Poll the chip for a short amount of time in case the
	 * allocation succeeds quickly.
	 */
	poll_count = MEMORY_WAIT_TIME;
	do {
		status = SMC_GET_INT(lp);
		if (status & IM_ALLOC_INT) {
			SMC_ACK_INT(lp, IM_ALLOC_INT);
  			break;
		}
   	} while (--poll_count);

	smc_special_unlock(&lp->lock);

	lp->pending_tx_skb = skb;
   	if (!poll_count) {
		/* oh well, wait until the chip finds memory later */
		netif_stop_queue(dev);
		DBG(2, "%s: TX memory allocation deferred.\n", dev->name);
		SMC_ENABLE_INT(lp, IM_ALLOC_INT);
   	} else {
		/*
		 * Allocation succeeded: push packet to the chip's own memory
		 * immediately.
		 */
		smc_hardware_send_pkt((unsigned long)dev);
	}

	return 0;
}

/*
 * This handles a TX interrupt, which is only called when:
 * - a TX error occurred, or
 * - CTL_AUTO_RELEASE is not set and TX of a packet completed.
 */
static void smc_tx(struct net_device *dev)
{
	struct smc_local *lp = netdev_priv(dev);
	void __iomem *ioaddr = lp->base;
	unsigned int saved_packet, packet_no, tx_status, pkt_len;

	DBG(3, "%s: %s\n", dev->name, __FUNCTION__);

	/* If the TX FIFO is empty then nothing to do */
	packet_no = SMC_GET_TXFIFO(lp);
	if (unlikely(packet_no & TXFIFO_TEMPTY)) {
		PRINTK("%s: smc_tx with nothing on FIFO.\n", dev->name);
		return;
	}

	/* select packet to read from */
	saved_packet = SMC_GET_PN(lp);
	SMC_SET_PN(lp, packet_no);

	/* read the first word (status word) from this packet */
	SMC_SET_PTR(lp, PTR_AUTOINC | PTR_READ);
	SMC_GET_PKT_HDR(lp, tx_status, pkt_len);
	DBG(2, "%s: TX STATUS 0x%04x PNR 0x%02x\n",
		dev->name, tx_status, packet_no);

	if (!(tx_status & ES_TX_SUC))
		dev->stats.tx_errors++;

	if (tx_status & ES_LOSTCARR)
		dev->stats.tx_carrier_errors++;

	if (tx_status & (ES_LATCOL | ES_16COL)) {
		PRINTK("%s: %s occurred on last xmit\n", dev->name,
		       (tx_status & ES_LATCOL) ?
			"late collision" : "too many collisions");
		dev->stats.tx_window_errors++;
		if (!(dev->stats.tx_window_errors & 63) && net_ratelimit()) {
			printk(KERN_INFO "%s: unexpectedly large number of "
			       "bad collisions. Please check duplex "
			       "setting.\n", dev->name);
		}
	}

	/* kill the packet */
	SMC_WAIT_MMU_BUSY(lp);
	SMC_SET_MMU_CMD(lp, MC_FREEPKT);

	/* Don't restore Packet Number Reg until busy bit is cleared */
	SMC_WAIT_MMU_BUSY(lp);
	SMC_SET_PN(lp, saved_packet);

	/* re-enable transmit */
	SMC_SELECT_BANK(lp, 0);
	SMC_SET_TCR(lp, lp->tcr_cur_mode);
	SMC_SELECT_BANK(lp, 2);
}


/*---PHY CONTROL AND CONFIGURATION-----------------------------------------*/

static void smc_mii_out(struct net_device *dev, unsigned int val, int bits)
{
	struct smc_local *lp = netdev_priv(dev);
	void __iomem *ioaddr = lp->base;
	unsigned int mii_reg, mask;

	mii_reg = SMC_GET_MII(lp) & ~(MII_MCLK | MII_MDOE | MII_MDO);
	mii_reg |= MII_MDOE;

	for (mask = 1 << (bits - 1); mask; mask >>= 1) {
		if (val & mask)
			mii_reg |= MII_MDO;
		else
			mii_reg &= ~MII_MDO;

		SMC_SET_MII(lp, mii_reg);
		udelay(MII_DELAY);
		SMC_SET_MII(lp, mii_reg | MII_MCLK);
		udelay(MII_DELAY);
	}
}

static unsigned int smc_mii_in(struct net_device *dev, int bits)
{
	struct smc_local *lp = netdev_priv(dev);
	void __iomem *ioaddr = lp->base;
	unsigned int mii_reg, mask, val;

	mii_reg = SMC_GET_MII(lp) & ~(MII_MCLK | MII_MDOE | MII_MDO);
	SMC_SET_MII(lp, mii_reg);

	for (mask = 1 << (bits - 1), val = 0; mask; mask >>= 1) {
		if (SMC_GET_MII(lp) & MII_MDI)
			val |= mask;

		SMC_SET_MII(lp, mii_reg);
		udelay(MII_DELAY);
		SMC_SET_MII(lp, mii_reg | MII_MCLK);
		udelay(MII_DELAY);
	}

	return val;
}

/*
 * Reads a register from the MII Management serial interface
 */
static int smc_phy_read(struct net_device *dev, int phyaddr, int phyreg)
{
	struct smc_local *lp = netdev_priv(dev);
	void __iomem *ioaddr = lp->base;
	unsigned int phydata;

	SMC_SELECT_BANK(lp, 3);

	/* Idle - 32 ones */
	smc_mii_out(dev, 0xffffffff, 32);

	/* Start code (01) + read (10) + phyaddr + phyreg */
	smc_mii_out(dev, 6 << 10 | phyaddr << 5 | phyreg, 14);

	/* Turnaround (2bits) + phydata */
	phydata = smc_mii_in(dev, 18);

	/* Return to idle state */
	SMC_SET_MII(lp, SMC_GET_MII(lp) & ~(MII_MCLK|MII_MDOE|MII_MDO));

	DBG(3, "%s: phyaddr=0x%x, phyreg=0x%x, phydata=0x%x\n",
		__FUNCTION__, phyaddr, phyreg, phydata);

	SMC_SELECT_BANK(lp, 2);
	return phydata;
}

/*
 * Writes a register to the MII Management serial interface
 */
static void smc_phy_write(struct net_device *dev, int phyaddr, int phyreg,
			  int phydata)
{
	struct smc_local *lp = netdev_priv(dev);
	void __iomem *ioaddr = lp->base;

	SMC_SELECT_BANK(lp, 3);

	/* Idle - 32 ones */
	smc_mii_out(dev, 0xffffffff, 32);

	/* Start code (01) + write (01) + phyaddr + phyreg + turnaround + phydata */
	smc_mii_out(dev, 5 << 28 | phyaddr << 23 | phyreg << 18 | 2 << 16 | phydata, 32);

	/* Return to idle state */
	SMC_SET_MII(lp, SMC_GET_MII(lp) & ~(MII_MCLK|MII_MDOE|MII_MDO));

	DBG(3, "%s: phyaddr=0x%x, phyreg=0x%x, phydata=0x%x\n",
		__FUNCTION__, phyaddr, phyreg, phydata);

	SMC_SELECT_BANK(lp, 2);
}

/*
 * Finds and reports the PHY address
 */
static void smc_phy_detect(struct net_device *dev)
{
	struct smc_local *lp = netdev_priv(dev);
	int phyaddr;

	DBG(2, "%s: %s\n", dev->name, __FUNCTION__);

	lp->phy_type = 0;

	/*
	 * Scan all 32 PHY addresses if necessary, starting at
	 * PHY#1 to PHY#31, and then PHY#0 last.
	 */
	for (phyaddr = 1; phyaddr < 33; ++phyaddr) {
		unsigned int id1, id2;

		/* Read the PHY identifiers */
		id1 = smc_phy_read(dev, phyaddr & 31, MII_PHYSID1);
		id2 = smc_phy_read(dev, phyaddr & 31, MII_PHYSID2);

		DBG(3, "%s: phy_id1=0x%x, phy_id2=0x%x\n",
			dev->name, id1, id2);

		/* Make sure it is a valid identifier */
		if (id1 != 0x0000 && id1 != 0xffff && id1 != 0x8000 &&
		    id2 != 0x0000 && id2 != 0xffff && id2 != 0x8000) {
			/* Save the PHY's address */
			lp->mii.phy_id = phyaddr & 31;
			lp->phy_type = id1 << 16 | id2;
			break;
		}
	}
}

/*
 * Sets the PHY to a configuration as determined by the user
 */
static int smc_phy_fixed(struct net_device *dev)
{
	struct smc_local *lp = netdev_priv(dev);
	void __iomem *ioaddr = lp->base;
	int phyaddr = lp->mii.phy_id;
	int bmcr, cfg1;

	DBG(3, "%s: %s\n", dev->name, __FUNCTION__);

	/* Enter Link Disable state */
	cfg1 = smc_phy_read(dev, phyaddr, PHY_CFG1_REG);
	cfg1 |= PHY_CFG1_LNKDIS;
	smc_phy_write(dev, phyaddr, PHY_CFG1_REG, cfg1);

	/*
	 * Set our fixed capabilities
	 * Disable auto-negotiation
	 */
	bmcr = 0;

	if (lp->ctl_rfduplx)
		bmcr |= BMCR_FULLDPLX;

	if (lp->ctl_rspeed == 100)
		bmcr |= BMCR_SPEED100;

	/* Write our capabilities to the phy control register */
	smc_phy_write(dev, phyaddr, MII_BMCR, bmcr);

	/* Re-Configure the Receive/Phy Control register */
	SMC_SELECT_BANK(lp, 0);
	SMC_SET_RPC(lp, lp->rpc_cur_mode);
	SMC_SELECT_BANK(lp, 2);

	return 1;
}

/*
 * smc_phy_reset - reset the phy
 * @dev: net device
 * @phy: phy address
 *
 * Issue a software reset for the specified PHY and
 * wait up to 100ms for the reset to complete.  We should
 * not access the PHY for 50ms after issuing the reset.
 *
 * The time to wait appears to be dependent on the PHY.
 *
 * Must be called with lp->lock locked.
 */
static int smc_phy_reset(struct net_device *dev, int phy)
{
	struct smc_local *lp = netdev_priv(dev);
	unsigned int bmcr;
	int timeout;

	smc_phy_write(dev, phy, MII_BMCR, BMCR_RESET);

	for (timeout = 2; timeout; timeout--) {
		spin_unlock_irq(&lp->lock);
		msleep(50);
		spin_lock_irq(&lp->lock);

		bmcr = smc_phy_read(dev, phy, MII_BMCR);
		if (!(bmcr & BMCR_RESET))
			break;
	}

	return bmcr & BMCR_RESET;
}

/*
 * smc_phy_powerdown - powerdown phy
 * @dev: net device
 *
 * Power down the specified PHY
 */
static void smc_phy_powerdown(struct net_device *dev)
{
	struct smc_local *lp = netdev_priv(dev);
	unsigned int bmcr;
	int phy = lp->mii.phy_id;

	if (lp->phy_type == 0)
		return;

	/* We need to ensure that no calls to smc_phy_configure are
	   pending.
	*/
	cancel_work_sync(&lp->phy_configure);

	bmcr = smc_phy_read(dev, phy, MII_BMCR);
	smc_phy_write(dev, phy, MII_BMCR, bmcr | BMCR_PDOWN);
}

/*
 * smc_phy_check_media - check the media status and adjust TCR
 * @dev: net device
 * @init: set true for initialisation
 *
 * Select duplex mode depending on negotiation state.  This
 * also updates our carrier state.
 */
static void smc_phy_check_media(struct net_device *dev, int init)
{
	struct smc_local *lp = netdev_priv(dev);
	void __iomem *ioaddr = lp->base;

	if (mii_check_media(&lp->mii, netif_msg_link(lp), init)) {
		/* duplex state has changed */
		if (lp->mii.full_duplex) {
			lp->tcr_cur_mode |= TCR_SWFDUP;
		} else {
			lp->tcr_cur_mode &= ~TCR_SWFDUP;
		}

		SMC_SELECT_BANK(lp, 0);
		SMC_SET_TCR(lp, lp->tcr_cur_mode);
	}
}

/*
 * Configures the specified PHY through the MII management interface
 * using Autonegotiation.
 * Calls smc_phy_fixed() if the user has requested a certain config.
 * If RPC ANEG bit is set, the media selection is dependent purely on
 * the selection by the MII (either in the MII BMCR reg or the result
 * of autonegotiation.)  If the RPC ANEG bit is cleared, the selection
 * is controlled by the RPC SPEED and RPC DPLX bits.
 */
static void smc_phy_configure(struct work_struct *work)
{
	struct smc_local *lp =
		container_of(work, struct smc_local, phy_configure);
	struct net_device *dev = lp->dev;
	void __iomem *ioaddr = lp->base;
	int phyaddr = lp->mii.phy_id;
	int my_phy_caps; /* My PHY capabilities */
	int my_ad_caps; /* My Advertised capabilities */
	int status;

	DBG(3, "%s:smc_program_phy()\n", dev->name);

	spin_lock_irq(&lp->lock);

	/*
	 * We should not be called if phy_type is zero.
	 */
	if (lp->phy_type == 0)
		goto smc_phy_configure_exit;

	if (smc_phy_reset(dev, phyaddr)) {
		printk("%s: PHY reset timed out\n", dev->name);
		goto smc_phy_configure_exit;
	}

	/*
	 * Enable PHY Interrupts (for register 18)
	 * Interrupts listed here are disabled
	 */
	smc_phy_write(dev, phyaddr, PHY_MASK_REG,
		PHY_INT_LOSSSYNC | PHY_INT_CWRD | PHY_INT_SSD |
		PHY_INT_ESD | PHY_INT_RPOL | PHY_INT_JAB |
		PHY_INT_SPDDET | PHY_INT_DPLXDET);

	/* Configure the Receive/Phy Control register */
	SMC_SELECT_BANK(lp, 0);
	SMC_SET_RPC(lp, lp->rpc_cur_mode);

	/* If the user requested no auto neg, then go set his request */
	if (lp->mii.force_media) {
		smc_phy_fixed(dev);
		goto smc_phy_configure_exit;
	}

	/* Copy our capabilities from MII_BMSR to MII_ADVERTISE */
	my_phy_caps = smc_phy_read(dev, phyaddr, MII_BMSR);

	if (!(my_phy_caps & BMSR_ANEGCAPABLE)) {
		printk(KERN_INFO "Auto negotiation NOT supported\n");
		smc_phy_fixed(dev);
		goto smc_phy_configure_exit;
	}

	my_ad_caps = ADVERTISE_CSMA; /* I am CSMA capable */

	if (my_phy_caps & BMSR_100BASE4)
		my_ad_caps |= ADVERTISE_100BASE4;
	if (my_phy_caps & BMSR_100FULL)
		my_ad_caps |= ADVERTISE_100FULL;
	if (my_phy_caps & BMSR_100HALF)
		my_ad_caps |= ADVERTISE_100HALF;
	if (my_phy_caps & BMSR_10FULL)
		my_ad_caps |= ADVERTISE_10FULL;
	if (my_phy_caps & BMSR_10HALF)
		my_ad_caps |= ADVERTISE_10HALF;

	/* Disable capabilities not selected by our user */
	if (lp->ctl_rspeed != 100)
		my_ad_caps &= ~(ADVERTISE_100BASE4|ADVERTISE_100FULL|ADVERTISE_100HALF);

	if (!lp->ctl_rfduplx)
		my_ad_caps &= ~(ADVERTISE_100FULL|ADVERTISE_10FULL);

	/* Update our Auto-Neg Advertisement Register */
	smc_phy_write(dev, phyaddr, MII_ADVERTISE, my_ad_caps);
	lp->mii.advertising = my_ad_caps;

	/*
	 * Read the register back.  Without this, it appears that when
	 * auto-negotiation is restarted, sometimes it isn't ready and
	 * the link does not come up.
	 */
	status = smc_phy_read(dev, phyaddr, MII_ADVERTISE);

	DBG(2, "%s: phy caps=%x\n", dev->name, my_phy_caps);
	DBG(2, "%s: phy advertised caps=%x\n", dev->name, my_ad_caps);

	/* Restart auto-negotiation process in order to advertise my caps */
	smc_phy_write(dev, phyaddr, MII_BMCR, BMCR_ANENABLE | BMCR_ANRESTART);

	smc_phy_check_media(dev, 1);

smc_phy_configure_exit:
	SMC_SELECT_BANK(lp, 2);
	spin_unlock_irq(&lp->lock);
}

/*
 * smc_phy_interrupt
 *
 * Purpose:  Handle interrupts relating to PHY register 18. This is
 *  called from the "hard" interrupt handler under our private spinlock.
 */
static void smc_phy_interrupt(struct net_device *dev)
{
	struct smc_local *lp = netdev_priv(dev);
	int phyaddr = lp->mii.phy_id;
	int phy18;

	DBG(2, "%s: %s\n", dev->name, __FUNCTION__);

	if (lp->phy_type == 0)
		return;

	for(;;) {
		smc_phy_check_media(dev, 0);

		/* Read PHY Register 18, Status Output */
		phy18 = smc_phy_read(dev, phyaddr, PHY_INT_REG);
		if ((phy18 & PHY_INT_INT) == 0)
			break;
	}
}

/*--- END PHY CONTROL AND CONFIGURATION-------------------------------------*/

static void smc_10bt_check_media(struct net_device *dev, int init)
{
	struct smc_local *lp = netdev_priv(dev);
	void __iomem *ioaddr = lp->base;
	unsigned int old_carrier, new_carrier;

	old_carrier = netif_carrier_ok(dev) ? 1 : 0;

	SMC_SELECT_BANK(lp, 0);
	new_carrier = (SMC_GET_EPH_STATUS(lp) & ES_LINK_OK) ? 1 : 0;
	SMC_SELECT_BANK(lp, 2);

	if (init || (old_carrier != new_carrier)) {
		if (!new_carrier) {
			netif_carrier_off(dev);
		} else {
			netif_carrier_on(dev);
		}
		if (netif_msg_link(lp))
			printk(KERN_INFO "%s: link %s\n", dev->name,
			       new_carrier ? "up" : "down");
	}
}

static void smc_eph_interrupt(struct net_device *dev)
{
	struct smc_local *lp = netdev_priv(dev);
	void __iomem *ioaddr = lp->base;
	unsigned int ctl;

	smc_10bt_check_media(dev, 0);

	SMC_SELECT_BANK(lp, 1);
	ctl = SMC_GET_CTL(lp);
	SMC_SET_CTL(lp, ctl & ~CTL_LE_ENABLE);
	SMC_SET_CTL(lp, ctl);
	SMC_SELECT_BANK(lp, 2);
}

/*
 * This is the main routine of the driver, to handle the device when
 * it needs some attention.
 */
static irqreturn_t smc_interrupt(int irq, void *dev_id)
{
	struct net_device *dev = dev_id;
	struct smc_local *lp = netdev_priv(dev);
	void __iomem *ioaddr = lp->base;
	int status, mask, timeout, card_stats;
	int saved_pointer;

	DBG(3, "%s: %s\n", dev->name, __FUNCTION__);

	spin_lock(&lp->lock);

	/* A preamble may be used when there is a potential race
	 * between the interruptible transmit functions and this
	 * ISR. */
	SMC_INTERRUPT_PREAMBLE;

	saved_pointer = SMC_GET_PTR(lp);
	mask = SMC_GET_INT_MASK(lp);
	SMC_SET_INT_MASK(lp, 0);

	/* set a timeout value, so I don't stay here forever */
	timeout = MAX_IRQ_LOOPS;

	do {
		status = SMC_GET_INT(lp);

		DBG(2, "%s: INT 0x%02x MASK 0x%02x MEM 0x%04x FIFO 0x%04x\n",
			dev->name, status, mask,
			({ int meminfo; SMC_SELECT_BANK(lp, 0);
			   meminfo = SMC_GET_MIR(lp);
			   SMC_SELECT_BANK(lp, 2); meminfo; }),
			SMC_GET_FIFO(lp));

		status &= mask;
		if (!status)
			break;

		if (status & IM_TX_INT) {
			/* do this before RX as it will free memory quickly */
			DBG(3, "%s: TX int\n", dev->name);
			smc_tx(dev);
			SMC_ACK_INT(lp, IM_TX_INT);
			if (THROTTLE_TX_PKTS)
				netif_wake_queue(dev);
		} else if (status & IM_RCV_INT) {
			DBG(3, "%s: RX irq\n", dev->name);
			smc_rcv(dev);
		} else if (status & IM_ALLOC_INT) {
			DBG(3, "%s: Allocation irq\n", dev->name);
			tasklet_hi_schedule(&lp->tx_task);
			mask &= ~IM_ALLOC_INT;
		} else if (status & IM_TX_EMPTY_INT) {
			DBG(3, "%s: TX empty\n", dev->name);
			mask &= ~IM_TX_EMPTY_INT;

			/* update stats */
			SMC_SELECT_BANK(lp, 0);
			card_stats = SMC_GET_COUNTER(lp);
			SMC_SELECT_BANK(lp, 2);

			/* single collisions */
			dev->stats.collisions += card_stats & 0xF;
			card_stats >>= 4;

			/* multiple collisions */
			dev->stats.collisions += card_stats & 0xF;
		} else if (status & IM_RX_OVRN_INT) {
			DBG(1, "%s: RX overrun (EPH_ST 0x%04x)\n", dev->name,
			       ({ int eph_st; SMC_SELECT_BANK(lp, 0);
				  eph_st = SMC_GET_EPH_STATUS(lp);
				  SMC_SELECT_BANK(lp, 2); eph_st; }));
			SMC_ACK_INT(lp, IM_RX_OVRN_INT);
			dev->stats.rx_errors++;
			dev->stats.rx_fifo_errors++;
		} else if (status & IM_EPH_INT) {
			smc_eph_interrupt(dev);
		} else if (status & IM_MDINT) {
			SMC_ACK_INT(lp, IM_MDINT);
			smc_phy_interrupt(dev);
		} else if (status & IM_ERCV_INT) {
			SMC_ACK_INT(lp, IM_ERCV_INT);
			PRINTK("%s: UNSUPPORTED: ERCV INTERRUPT \n", dev->name);
		}
	} while (--timeout);

	/* restore register states */
	SMC_SET_PTR(lp, saved_pointer);
	SMC_SET_INT_MASK(lp, mask);
	spin_unlock(&lp->lock);

#ifndef CONFIG_NET_POLL_CONTROLLER
	if (timeout == MAX_IRQ_LOOPS)
		PRINTK("%s: spurious interrupt (mask = 0x%02x)\n",
		       dev->name, mask);
#endif
	DBG(3, "%s: Interrupt done (%d loops)\n",
	       dev->name, MAX_IRQ_LOOPS - timeout);

	/*
	 * We return IRQ_HANDLED unconditionally here even if there was
	 * nothing to do.  There is a possibility that a packet might
	 * get enqueued into the chip right after TX_EMPTY_INT is raised
	 * but just before the CPU acknowledges the IRQ.
	 * Better take an unneeded IRQ in some occasions than complexifying
	 * the code for all cases.
	 */
	return IRQ_HANDLED;
}

#ifdef CONFIG_NET_POLL_CONTROLLER
/*
 * Polling receive - used by netconsole and other diagnostic tools
 * to allow network i/o with interrupts disabled.
 */
static void smc_poll_controller(struct net_device *dev)
{
	disable_irq(dev->irq);
	smc_interrupt(dev->irq, dev);
	enable_irq(dev->irq);
}
#endif

/* Our watchdog timed out. Called by the networking layer */
static void smc_timeout(struct net_device *dev)
{
	struct smc_local *lp = netdev_priv(dev);
	void __iomem *ioaddr = lp->base;
	int status, mask, eph_st, meminfo, fifo;

	DBG(2, "%s: %s\n", dev->name, __FUNCTION__);

	spin_lock_irq(&lp->lock);
	status = SMC_GET_INT(lp);
	mask = SMC_GET_INT_MASK(lp);
	fifo = SMC_GET_FIFO(lp);
	SMC_SELECT_BANK(lp, 0);
	eph_st = SMC_GET_EPH_STATUS(lp);
	meminfo = SMC_GET_MIR(lp);
	SMC_SELECT_BANK(lp, 2);
	spin_unlock_irq(&lp->lock);
	PRINTK( "%s: TX timeout (INT 0x%02x INTMASK 0x%02x "
		"MEM 0x%04x FIFO 0x%04x EPH_ST 0x%04x)\n",
		dev->name, status, mask, meminfo, fifo, eph_st );

	smc_reset(dev);
	smc_enable(dev);

	/*
	 * Reconfiguring the PHY doesn't seem like a bad idea here, but
	 * smc_phy_configure() calls msleep() which calls schedule_timeout()
	 * which calls schedule().  Hence we use a work queue.
	 */
	if (lp->phy_type != 0)
		schedule_work(&lp->phy_configure);

	/* We can accept TX packets again */
	dev->trans_start = jiffies;
	netif_wake_queue(dev);
}

/*
 * This routine will, depending on the values passed to it,
 * either make it accept multicast packets, go into
 * promiscuous mode (for TCPDUMP and cousins) or accept
 * a select set of multicast packets
 */
static void smc_set_multicast_list(struct net_device *dev)
{
	struct smc_local *lp = netdev_priv(dev);
	void __iomem *ioaddr = lp->base;
	unsigned char multicast_table[8];
	int update_multicast = 0;

	DBG(2, "%s: %s\n", dev->name, __FUNCTION__);

	if (dev->flags & IFF_PROMISC) {
		DBG(2, "%s: RCR_PRMS\n", dev->name);
		lp->rcr_cur_mode |= RCR_PRMS;
	}

/* BUG?  I never disable promiscuous mode if multicasting was turned on.
   Now, I turn off promiscuous mode, but I don't do anything to multicasting
   when promiscuous mode is turned on.
*/

	/*
	 * Here, I am setting this to accept all multicast packets.
	 * I don't need to zero the multicast table, because the flag is
	 * checked before the table is
	 */
	else if (dev->flags & IFF_ALLMULTI || dev->mc_count > 16) {
		DBG(2, "%s: RCR_ALMUL\n", dev->name);
		lp->rcr_cur_mode |= RCR_ALMUL;
	}

	/*
	 * This sets the internal hardware table to filter out unwanted
	 * multicast packets before they take up memory.
	 *
	 * The SMC chip uses a hash table where the high 6 bits of the CRC of
	 * address are the offset into the table.  If that bit is 1, then the
	 * multicast packet is accepted.  Otherwise, it's dropped silently.
	 *
	 * To use the 6 bits as an offset into the table, the high 3 bits are
	 * the number of the 8 bit register, while the low 3 bits are the bit
	 * within that register.
	 */
	else if (dev->mc_count)  {
		int i;
		struct dev_mc_list *cur_addr;

		/* table for flipping the order of 3 bits */
		static const unsigned char invert3[] = {0, 4, 2, 6, 1, 5, 3, 7};

		/* start with a table of all zeros: reject all */
		memset(multicast_table, 0, sizeof(multicast_table));

		cur_addr = dev->mc_list;
		for (i = 0; i < dev->mc_count; i++, cur_addr = cur_addr->next) {
			int position;

			/* do we have a pointer here? */
			if (!cur_addr)
				break;
			/* make sure this is a multicast address -
		   	   shouldn't this be a given if we have it here ? */
			if (!(*cur_addr->dmi_addr & 1))
				continue;

			/* only use the low order bits */
			position = crc32_le(~0, cur_addr->dmi_addr, 6) & 0x3f;

			/* do some messy swapping to put the bit in the right spot */
			multicast_table[invert3[position&7]] |=
				(1<<invert3[(position>>3)&7]);
		}

		/* be sure I get rid of flags I might have set */
		lp->rcr_cur_mode &= ~(RCR_PRMS | RCR_ALMUL);

		/* now, the table can be loaded into the chipset */
		update_multicast = 1;
	} else  {
		DBG(2, "%s: ~(RCR_PRMS|RCR_ALMUL)\n", dev->name);
		lp->rcr_cur_mode &= ~(RCR_PRMS | RCR_ALMUL);

		/*
		 * since I'm disabling all multicast entirely, I need to
		 * clear the multicast list
		 */
		memset(multicast_table, 0, sizeof(multicast_table));
		update_multicast = 1;
	}

	spin_lock_irq(&lp->lock);
	SMC_SELECT_BANK(lp, 0);
	SMC_SET_RCR(lp, lp->rcr_cur_mode);
	if (update_multicast) {
		SMC_SELECT_BANK(lp, 3);
		SMC_SET_MCAST(lp, multicast_table);
	}
	SMC_SELECT_BANK(lp, 2);
	spin_unlock_irq(&lp->lock);
}


/*
 * Open and Initialize the board
 *
 * Set up everything, reset the card, etc..
 */
static int
smc_open(struct net_device *dev)
{
	struct smc_local *lp = netdev_priv(dev);

	DBG(2, "%s: %s\n", dev->name, __FUNCTION__);

	/*
	 * Check that the address is valid.  If its not, refuse
	 * to bring the device up.  The user must specify an
	 * address using ifconfig eth0 hw ether xx:xx:xx:xx:xx:xx
	 */
	if (!is_valid_ether_addr(dev->dev_addr)) {
		PRINTK("%s: no valid ethernet hw addr\n", __FUNCTION__);
		return -EINVAL;
	}

	/* Setup the default Register Modes */
	lp->tcr_cur_mode = TCR_DEFAULT;
	lp->rcr_cur_mode = RCR_DEFAULT;
	lp->rpc_cur_mode = RPC_DEFAULT |
				lp->cfg.leda << RPC_LSXA_SHFT |
				lp->cfg.ledb << RPC_LSXB_SHFT;

	/*
	 * If we are not using a MII interface, we need to
	 * monitor our own carrier signal to detect faults.
	 */
	if (lp->phy_type == 0)
		lp->tcr_cur_mode |= TCR_MON_CSN;

	/* reset the hardware */
	smc_reset(dev);
	smc_enable(dev);

	/* Configure the PHY, initialize the link state */
	if (lp->phy_type != 0)
		smc_phy_configure(&lp->phy_configure);
	else {
		spin_lock_irq(&lp->lock);
		smc_10bt_check_media(dev, 1);
		spin_unlock_irq(&lp->lock);
	}

	netif_start_queue(dev);
	return 0;
}

/*
 * smc_close
 *
 * this makes the board clean up everything that it can
 * and not talk to the outside world.   Caused by
 * an 'ifconfig ethX down'
 */
static int smc_close(struct net_device *dev)
{
	struct smc_local *lp = netdev_priv(dev);

	DBG(2, "%s: %s\n", dev->name, __FUNCTION__);

	netif_stop_queue(dev);
	netif_carrier_off(dev);

	/* clear everything */
	smc_shutdown(dev);
	tasklet_kill(&lp->tx_task);
	smc_phy_powerdown(dev);
	return 0;
}

/*
 * Ethtool support
 */
static int
smc_ethtool_getsettings(struct net_device *dev, struct ethtool_cmd *cmd)
{
	struct smc_local *lp = netdev_priv(dev);
	int ret;

	cmd->maxtxpkt = 1;
	cmd->maxrxpkt = 1;

	if (lp->phy_type != 0) {
		spin_lock_irq(&lp->lock);
		ret = mii_ethtool_gset(&lp->mii, cmd);
		spin_unlock_irq(&lp->lock);
	} else {
		cmd->supported = SUPPORTED_10baseT_Half |
				 SUPPORTED_10baseT_Full |
				 SUPPORTED_TP | SUPPORTED_AUI;

		if (lp->ctl_rspeed == 10)
			cmd->speed = SPEED_10;
		else if (lp->ctl_rspeed == 100)
			cmd->speed = SPEED_100;

		cmd->autoneg = AUTONEG_DISABLE;
		cmd->transceiver = XCVR_INTERNAL;
		cmd->port = 0;
		cmd->duplex = lp->tcr_cur_mode & TCR_SWFDUP ? DUPLEX_FULL : DUPLEX_HALF;

		ret = 0;
	}

	return ret;
}

static int
smc_ethtool_setsettings(struct net_device *dev, struct ethtool_cmd *cmd)
{
	struct smc_local *lp = netdev_priv(dev);
	int ret;

	if (lp->phy_type != 0) {
		spin_lock_irq(&lp->lock);
		ret = mii_ethtool_sset(&lp->mii, cmd);
		spin_unlock_irq(&lp->lock);
	} else {
		if (cmd->autoneg != AUTONEG_DISABLE ||
		    cmd->speed != SPEED_10 ||
		    (cmd->duplex != DUPLEX_HALF && cmd->duplex != DUPLEX_FULL) ||
		    (cmd->port != PORT_TP && cmd->port != PORT_AUI))
			return -EINVAL;

//		lp->port = cmd->port;
		lp->ctl_rfduplx = cmd->duplex == DUPLEX_FULL;

//		if (netif_running(dev))
//			smc_set_port(dev);

		ret = 0;
	}

	return ret;
}

static void
smc_ethtool_getdrvinfo(struct net_device *dev, struct ethtool_drvinfo *info)
{
	strncpy(info->driver, CARDNAME, sizeof(info->driver));
	strncpy(info->version, version, sizeof(info->version));
	strncpy(info->bus_info, dev->dev.parent->bus_id, sizeof(info->bus_info));
}

static int smc_ethtool_nwayreset(struct net_device *dev)
{
	struct smc_local *lp = netdev_priv(dev);
	int ret = -EINVAL;

	if (lp->phy_type != 0) {
		spin_lock_irq(&lp->lock);
		ret = mii_nway_restart(&lp->mii);
		spin_unlock_irq(&lp->lock);
	}

	return ret;
}

static u32 smc_ethtool_getmsglevel(struct net_device *dev)
{
	struct smc_local *lp = netdev_priv(dev);
	return lp->msg_enable;
}

static void smc_ethtool_setmsglevel(struct net_device *dev, u32 level)
{
	struct smc_local *lp = netdev_priv(dev);
	lp->msg_enable = level;
}

static const struct ethtool_ops smc_ethtool_ops = {
	.get_settings	= smc_ethtool_getsettings,
	.set_settings	= smc_ethtool_setsettings,
	.get_drvinfo	= smc_ethtool_getdrvinfo,

	.get_msglevel	= smc_ethtool_getmsglevel,
	.set_msglevel	= smc_ethtool_setmsglevel,
	.nway_reset	= smc_ethtool_nwayreset,
	.get_link	= ethtool_op_get_link,
//	.get_eeprom	= smc_ethtool_geteeprom,
//	.set_eeprom	= smc_ethtool_seteeprom,
};

/*
 * smc_findirq
 *
 * This routine has a simple purpose -- make the SMC chip generate an
 * interrupt, so an auto-detect routine can detect it, and find the IRQ,
 */
/*
 * does this still work?
 *
 * I just deleted auto_irq.c, since it was never built...
 *   --jgarzik
 */
static int __init smc_findirq(struct smc_local *lp)
{
	void __iomem *ioaddr = lp->base;
	int timeout = 20;
	unsigned long cookie;

	DBG(2, "%s: %s\n", CARDNAME, __FUNCTION__);

	cookie = probe_irq_on();

	/*
	 * What I try to do here is trigger an ALLOC_INT. This is done
	 * by allocating a small chunk of memory, which will give an interrupt
	 * when done.
	 */
	/* enable ALLOCation interrupts ONLY */
	SMC_SELECT_BANK(lp, 2);
	SMC_SET_INT_MASK(lp, IM_ALLOC_INT);

	/*
 	 * Allocate 512 bytes of memory.  Note that the chip was just
	 * reset so all the memory is available
	 */
	SMC_SET_MMU_CMD(lp, MC_ALLOC | 1);

	/*
	 * Wait until positive that the interrupt has been generated
	 */
	do {
		int int_status;
		udelay(10);
		int_status = SMC_GET_INT(lp);
		if (int_status & IM_ALLOC_INT)
			break;		/* got the interrupt */
	} while (--timeout);

	/*
	 * there is really nothing that I can do here if timeout fails,
	 * as autoirq_report will return a 0 anyway, which is what I
	 * want in this case.   Plus, the clean up is needed in both
	 * cases.
	 */

	/* and disable all interrupts again */
	SMC_SET_INT_MASK(lp, 0);

	/* and return what I found */
	return probe_irq_off(cookie);
}

/*
 * Function: smc_probe(unsigned long ioaddr)
 *
 * Purpose:
 *	Tests to see if a given ioaddr points to an SMC91x chip.
 *	Returns a 0 on success
 *
 * Algorithm:
 *	(1) see if the high byte of BANK_SELECT is 0x33
 * 	(2) compare the ioaddr with the base register's address
 *	(3) see if I recognize the chip ID in the appropriate register
 *
 * Here I do typical initialization tasks.
 *
 * o  Initialize the structure if needed
 * o  print out my vanity message if not done so already
 * o  print out what type of hardware is detected
 * o  print out the ethernet address
 * o  find the IRQ
 * o  set up my private data
 * o  configure the dev structure with my subroutines
 * o  actually GRAB the irq.
 * o  GRAB the region
 */
static int __init smc_probe(struct net_device *dev, void __iomem *ioaddr,
			    unsigned long irq_flags)
{
	struct smc_local *lp = netdev_priv(dev);
	static int version_printed = 0;
	int retval;
	unsigned int val, revision_register;
	const char *version_string;
	DECLARE_MAC_BUF(mac);

	DBG(2, "%s: %s\n", CARDNAME, __FUNCTION__);

	/* First, see if the high byte is 0x33 */
	val = SMC_CURRENT_BANK(lp);
	DBG(2, "%s: bank signature probe returned 0x%04x\n", CARDNAME, val);
	if ((val & 0xFF00) != 0x3300) {
		if ((val & 0xFF) == 0x33) {
			printk(KERN_WARNING
				"%s: Detected possible byte-swapped interface"
				" at IOADDR %p\n", CARDNAME, ioaddr);
		}
		retval = -ENODEV;
		goto err_out;
	}

	/*
	 * The above MIGHT indicate a device, but I need to write to
	 * further test this.
	 */
	SMC_SELECT_BANK(lp, 0);
	val = SMC_CURRENT_BANK(lp);
	if ((val & 0xFF00) != 0x3300) {
		retval = -ENODEV;
		goto err_out;
	}

	/*
	 * well, we've already written once, so hopefully another
	 * time won't hurt.  This time, I need to switch the bank
	 * register to bank 1, so I can access the base address
	 * register
	 */
	SMC_SELECT_BANK(lp, 1);
	val = SMC_GET_BASE(lp);
	val = ((val & 0x1F00) >> 3) << SMC_IO_SHIFT;
	if (((unsigned int)ioaddr & (0x3e0 << SMC_IO_SHIFT)) != val) {
		printk("%s: IOADDR %p doesn't match configuration (%x).\n",
			CARDNAME, ioaddr, val);
	}

	/*
	 * check if the revision register is something that I
	 * recognize.  These might need to be added to later,
	 * as future revisions could be added.
	 */
	SMC_SELECT_BANK(lp, 3);
	revision_register = SMC_GET_REV(lp);
	DBG(2, "%s: revision = 0x%04x\n", CARDNAME, revision_register);
	version_string = chip_ids[ (revision_register >> 4) & 0xF];
	if (!version_string || (revision_register & 0xff00) != 0x3300) {
		/* I don't recognize this chip, so... */
		printk("%s: IO %p: Unrecognized revision register 0x%04x"
			", Contact author.\n", CARDNAME,
			ioaddr, revision_register);

		retval = -ENODEV;
		goto err_out;
	}

	/* At this point I'll assume that the chip is an SMC91x. */
	if (version_printed++ == 0)
		printk("%s", version);

	/* fill in some of the fields */
	dev->base_addr = (unsigned long)ioaddr;
	lp->base = ioaddr;
	lp->version = revision_register & 0xff;
	spin_lock_init(&lp->lock);

	/* Get the MAC address */
	SMC_SELECT_BANK(lp, 1);
	SMC_GET_MAC_ADDR(lp, dev->dev_addr);

	/* now, reset the chip, and put it into a known state */
	smc_reset(dev);

	/*
	 * If dev->irq is 0, then the device has to be banged on to see
	 * what the IRQ is.
 	 *
	 * This banging doesn't always detect the IRQ, for unknown reasons.
	 * a workaround is to reset the chip and try again.
	 *
	 * Interestingly, the DOS packet driver *SETS* the IRQ on the card to
	 * be what is requested on the command line.   I don't do that, mostly
	 * because the card that I have uses a non-standard method of accessing
	 * the IRQs, and because this _should_ work in most configurations.
	 *
	 * Specifying an IRQ is done with the assumption that the user knows
	 * what (s)he is doing.  No checking is done!!!!
	 */
	if (dev->irq < 1) {
		int trials;

		trials = 3;
		while (trials--) {
			dev->irq = smc_findirq(lp);
			if (dev->irq)
				break;
			/* kick the card and try again */
			smc_reset(dev);
		}
	}
	if (dev->irq == 0) {
		printk("%s: Couldn't autodetect your IRQ. Use irq=xx.\n",
			dev->name);
		retval = -ENODEV;
		goto err_out;
	}
	dev->irq = irq_canonicalize(dev->irq);

	/* Fill in the fields of the device structure with ethernet values. */
	ether_setup(dev);

	dev->open = smc_open;
	dev->stop = smc_close;
	dev->hard_start_xmit = smc_hard_start_xmit;
	dev->tx_timeout = smc_timeout;
	dev->watchdog_timeo = msecs_to_jiffies(watchdog);
	dev->set_multicast_list = smc_set_multicast_list;
	dev->ethtool_ops = &smc_ethtool_ops;
#ifdef CONFIG_NET_POLL_CONTROLLER
	dev->poll_controller = smc_poll_controller;
#endif

	tasklet_init(&lp->tx_task, smc_hardware_send_pkt, (unsigned long)dev);
	INIT_WORK(&lp->phy_configure, smc_phy_configure);
	lp->dev = dev;
	lp->mii.phy_id_mask = 0x1f;
	lp->mii.reg_num_mask = 0x1f;
	lp->mii.force_media = 0;
	lp->mii.full_duplex = 0;
	lp->mii.dev = dev;
	lp->mii.mdio_read = smc_phy_read;
	lp->mii.mdio_write = smc_phy_write;

	/*
	 * Locate the phy, if any.
	 */
	if (lp->version >= (CHIP_91100 << 4))
		smc_phy_detect(dev);

	/* then shut everything down to save power */
	smc_shutdown(dev);
	smc_phy_powerdown(dev);

	/* Set default parameters */
	lp->msg_enable = NETIF_MSG_LINK;
	lp->ctl_rfduplx = 0;
	lp->ctl_rspeed = 10;

	if (lp->version >= (CHIP_91100 << 4)) {
		lp->ctl_rfduplx = 1;
		lp->ctl_rspeed = 100;
	}

	/* Grab the IRQ */
	retval = request_irq(dev->irq, &smc_interrupt, irq_flags, dev->name, dev);
      	if (retval)
      		goto err_out;

#ifdef CONFIG_ARCH_PXA
#  ifdef SMC_USE_PXA_DMA
	lp->cfg.flags |= SMC91X_USE_DMA;
#  endif
	if (lp->cfg.flags & SMC91X_USE_DMA) {
		int dma = pxa_request_dma(dev->name, DMA_PRIO_LOW,
					  smc_pxa_dma_irq, NULL);
		if (dma >= 0)
			dev->dma = dma;
	}
#endif

	retval = register_netdev(dev);
	if (retval == 0) {
		/* now, print out the card info, in a short format.. */
		printk("%s: %s (rev %d) at %p IRQ %d",
			dev->name, version_string, revision_register & 0x0f,
			lp->base, dev->irq);

		if (dev->dma != (unsigned char)-1)
			printk(" DMA %d", dev->dma);

		printk("%s%s\n", nowait ? " [nowait]" : "",
			THROTTLE_TX_PKTS ? " [throttle_tx]" : "");

		if (!is_valid_ether_addr(dev->dev_addr)) {
			printk("%s: Invalid ethernet MAC address.  Please "
			       "set using ifconfig\n", dev->name);
		} else {
			/* Print the Ethernet address */
			printk("%s: Ethernet addr: %s\n",
			       dev->name, print_mac(mac, dev->dev_addr));
		}

		if (lp->phy_type == 0) {
			PRINTK("%s: No PHY found\n", dev->name);
		} else if ((lp->phy_type & 0xfffffff0) == 0x0016f840) {
			PRINTK("%s: PHY LAN83C183 (LAN91C111 Internal)\n", dev->name);
		} else if ((lp->phy_type & 0xfffffff0) == 0x02821c50) {
			PRINTK("%s: PHY LAN83C180\n", dev->name);
		}
	}

err_out:
#ifdef CONFIG_ARCH_PXA
	if (retval && dev->dma != (unsigned char)-1)
		pxa_free_dma(dev->dma);
#endif
	return retval;
}

static int smc_enable_device(struct platform_device *pdev)
{
	struct net_device *ndev = platform_get_drvdata(pdev);
	struct smc_local *lp = netdev_priv(ndev);
	unsigned long flags;
	unsigned char ecor, ecsr;
	void __iomem *addr;
	struct resource * res;

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "smc91x-attrib");
	if (!res)
		return 0;

	/*
	 * Map the attribute space.  This is overkill, but clean.
	 */
	addr = ioremap(res->start, ATTRIB_SIZE);
	if (!addr)
		return -ENOMEM;

	/*
	 * Reset the device.  We must disable IRQs around this
	 * since a reset causes the IRQ line become active.
	 */
	local_irq_save(flags);
	ecor = readb(addr + (ECOR << SMC_IO_SHIFT)) & ~ECOR_RESET;
	writeb(ecor | ECOR_RESET, addr + (ECOR << SMC_IO_SHIFT));
	readb(addr + (ECOR << SMC_IO_SHIFT));

	/*
	 * Wait 100us for the chip to reset.
	 */
	udelay(100);

	/*
	 * The device will ignore all writes to the enable bit while
	 * reset is asserted, even if the reset bit is cleared in the
	 * same write.  Must clear reset first, then enable the device.
	 */
	writeb(ecor, addr + (ECOR << SMC_IO_SHIFT));
	writeb(ecor | ECOR_ENABLE, addr + (ECOR << SMC_IO_SHIFT));

	/*
	 * Set the appropriate byte/word mode.
	 */
	ecsr = readb(addr + (ECSR << SMC_IO_SHIFT)) & ~ECSR_IOIS8;
	if (!SMC_16BIT(lp))
		ecsr |= ECSR_IOIS8;
	writeb(ecsr, addr + (ECSR << SMC_IO_SHIFT));
	local_irq_restore(flags);

	iounmap(addr);

	/*
	 * Wait for the chip to wake up.  We could poll the control
	 * register in the main register space, but that isn't mapped
	 * yet.  We know this is going to take 750us.
	 */
	msleep(1);

	return 0;
}

static int smc_request_attrib(struct platform_device *pdev,
			      struct net_device *ndev)
{
	struct resource * res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "smc91x-attrib");
	struct smc_local *lp = netdev_priv(ndev);

	if (!res)
		return 0;

	if (!request_mem_region(res->start, ATTRIB_SIZE, CARDNAME))
		return -EBUSY;

	return 0;
}

static void smc_release_attrib(struct platform_device *pdev,
			       struct net_device *ndev)
{
	struct resource * res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "smc91x-attrib");
	struct smc_local *lp = netdev_priv(ndev);

	if (res)
		release_mem_region(res->start, ATTRIB_SIZE);
}

static inline void smc_request_datacs(struct platform_device *pdev, struct net_device *ndev)
{
	if (SMC_CAN_USE_DATACS) {
		struct resource * res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "smc91x-data32");
		struct smc_local *lp = netdev_priv(ndev);

		if (!res)
			return;

		if(!request_mem_region(res->start, SMC_DATA_EXTENT, CARDNAME)) {
			printk(KERN_INFO "%s: failed to request datacs memory region.\n", CARDNAME);
			return;
		}

		lp->datacs = ioremap(res->start, SMC_DATA_EXTENT);
	}
}

static void smc_release_datacs(struct platform_device *pdev, struct net_device *ndev)
{
	if (SMC_CAN_USE_DATACS) {
		struct smc_local *lp = netdev_priv(ndev);
		struct resource * res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "smc91x-data32");

		if (lp->datacs)
			iounmap(lp->datacs);

		lp->datacs = NULL;

		if (res)
			release_mem_region(res->start, SMC_DATA_EXTENT);
	}
}

/*
 * smc_init(void)
 *   Input parameters:
 *	dev->base_addr == 0, try to find all possible locations
 *	dev->base_addr > 0x1ff, this is the address to check
 *	dev->base_addr == <anything else>, return failure code
 *
 *   Output:
 *	0 --> there is a device
 *	anything else, error
 */
static int smc_drv_probe(struct platform_device *pdev)
{
	struct smc91x_platdata *pd = pdev->dev.platform_data;
	struct smc_local *lp;
	struct net_device *ndev;
	struct resource *res, *ires;
	unsigned int __iomem *addr;
	unsigned long irq_flags = SMC_IRQ_FLAGS;
	int ret;

	ndev = alloc_etherdev(sizeof(struct smc_local));
	if (!ndev) {
		printk("%s: could not allocate device.\n", CARDNAME);
		ret = -ENOMEM;
		goto out;
	}
	SET_NETDEV_DEV(ndev, &pdev->dev);

	/* get configuration from platform data, only allow use of
	 * bus width if both SMC_CAN_USE_xxx and SMC91X_USE_xxx are set.
	 */

	lp = netdev_priv(ndev);

	if (pd) {
		memcpy(&lp->cfg, pd, sizeof(lp->cfg));
		lp->io_shift = SMC91X_IO_SHIFT(lp->cfg.flags);
	} else {
		lp->cfg.flags |= (SMC_CAN_USE_8BIT)  ? SMC91X_USE_8BIT  : 0;
		lp->cfg.flags |= (SMC_CAN_USE_16BIT) ? SMC91X_USE_16BIT : 0;
		lp->cfg.flags |= (SMC_CAN_USE_32BIT) ? SMC91X_USE_32BIT : 0;
		lp->cfg.flags |= (nowait) ? SMC91X_NOWAIT : 0;
	}

	if (!lp->cfg.leda && !lp->cfg.ledb) {
		lp->cfg.leda = RPC_LSA_DEFAULT;
		lp->cfg.ledb = RPC_LSB_DEFAULT;
	}

	ndev->dma = (unsigned char)-1;

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "smc91x-regs");
	if (!res)
		res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		ret = -ENODEV;
		goto out_free_netdev;
	}


	if (!request_mem_region(res->start, SMC_IO_EXTENT, CARDNAME)) {
		ret = -EBUSY;
		goto out_free_netdev;
	}

	ires = platform_get_resource(pdev, IORESOURCE_IRQ, 0);
	if (!ires) {
		ret = -ENODEV;
		goto out_release_io;
	}

	ndev->irq = ires->start;

	if (ires->flags & IRQF_TRIGGER_MASK)
		irq_flags = ires->flags & IRQF_TRIGGER_MASK;

	ret = smc_request_attrib(pdev, ndev);
	if (ret)
		goto out_release_io;
#if defined(CONFIG_SA1100_ASSABET)
	NCR_0 |= NCR_ENET_OSC_EN;
#endif
	platform_set_drvdata(pdev, ndev);
	ret = smc_enable_device(pdev);
	if (ret)
		goto out_release_attrib;

	addr = ioremap(res->start, SMC_IO_EXTENT);
	if (!addr) {
		ret = -ENOMEM;
		goto out_release_attrib;
	}

#ifdef CONFIG_ARCH_PXA
	{
		struct smc_local *lp = netdev_priv(ndev);
		lp->device = &pdev->dev;
		lp->physaddr = res->start;
	}
#endif

	ret = smc_probe(ndev, addr, irq_flags);
	if (ret != 0)
		goto out_iounmap;

	smc_request_datacs(pdev, ndev);

	return 0;

 out_iounmap:
	platform_set_drvdata(pdev, NULL);
	iounmap(addr);
 out_release_attrib:
	smc_release_attrib(pdev, ndev);
 out_release_io:
	release_mem_region(res->start, SMC_IO_EXTENT);
 out_free_netdev:
	free_netdev(ndev);
 out:
	printk("%s: not found (%d).\n", CARDNAME, ret);

	return ret;
}

static int smc_drv_remove(struct platform_device *pdev)
{
	struct net_device *ndev = platform_get_drvdata(pdev);
	struct smc_local *lp = netdev_priv(ndev);
	struct resource *res;

	platform_set_drvdata(pdev, NULL);

	unregister_netdev(ndev);

	free_irq(ndev->irq, ndev);

#ifdef CONFIG_ARCH_PXA
	if (ndev->dma != (unsigned char)-1)
		pxa_free_dma(ndev->dma);
#endif
	iounmap(lp->base);

	smc_release_datacs(pdev,ndev);
	smc_release_attrib(pdev,ndev);

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "smc91x-regs");
	if (!res)
		platform_get_resource(pdev, IORESOURCE_MEM, 0);
	release_mem_region(res->start, SMC_IO_EXTENT);

	free_netdev(ndev);

	return 0;
}

static int smc_drv_suspend(struct platform_device *dev, pm_message_t state)
{
	struct net_device *ndev = platform_get_drvdata(dev);

	if (ndev) {
		if (netif_running(ndev)) {
			netif_device_detach(ndev);
			smc_shutdown(ndev);
			smc_phy_powerdown(ndev);
		}
	}
	return 0;
}

static int smc_drv_resume(struct platform_device *dev)
{
	struct net_device *ndev = platform_get_drvdata(dev);

	if (ndev) {
		struct smc_local *lp = netdev_priv(ndev);
		smc_enable_device(dev);
		if (netif_running(ndev)) {
			smc_reset(ndev);
			smc_enable(ndev);
			if (lp->phy_type != 0)
				smc_phy_configure(&lp->phy_configure);
			netif_device_attach(ndev);
		}
	}
	return 0;
}

static struct platform_driver smc_driver = {
	.probe		= smc_drv_probe,
	.remove		= smc_drv_remove,
	.suspend	= smc_drv_suspend,
	.resume		= smc_drv_resume,
	.driver		= {
		.name	= CARDNAME,
		.owner	= THIS_MODULE,
	},
};

static int __init smc_init(void)
{
#ifdef MODULE
#ifdef CONFIG_ISA
	if (io == -1)
		printk(KERN_WARNING
			"%s: You shouldn't use auto-probing with insmod!\n",
			CARDNAME);
#endif
#endif

	return platform_driver_register(&smc_driver);
}

static void __exit smc_cleanup(void)
{
	platform_driver_unregister(&smc_driver);
}

module_init(smc_init);
module_exit(smc_cleanup);
