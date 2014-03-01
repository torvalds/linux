/*
 * 7990.c -- LANCE ethernet IC generic routines.
 * This is an attempt to separate out the bits of various ethernet
 * drivers that are common because they all use the AMD 7990 LANCE
 * (Local Area Network Controller for Ethernet) chip.
 *
 * Copyright (C) 05/1998 Peter Maydell <pmaydell@chiark.greenend.org.uk>
 *
 * Most of this stuff was obtained by looking at other LANCE drivers,
 * in particular a2065.[ch]. The AMD C-LANCE datasheet was also helpful.
 * NB: this was made easy by the fact that Jes Sorensen had cleaned up
 * most of a2025 and sunlance with the aim of merging them, so the
 * common code was pretty obvious.
 */
#include <linux/crc32.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/fcntl.h>
#include <linux/interrupt.h>
#include <linux/ioport.h>
#include <linux/in.h>
#include <linux/route.h>
#include <linux/string.h>
#include <linux/skbuff.h>
#include <asm/irq.h>
/* Used for the temporal inet entries and routing */
#include <linux/socket.h>
#include <linux/bitops.h>

#include <asm/io.h>
#include <asm/dma.h>
#include <asm/pgtable.h>
#ifdef CONFIG_HP300
#include <asm/blinken.h>
#endif

#include "7990.h"

#define WRITERAP(lp, x)	out_be16(lp->base + LANCE_RAP, (x))
#define WRITERDP(lp, x)	out_be16(lp->base + LANCE_RDP, (x))
#define READRDP(lp)	in_be16(lp->base + LANCE_RDP)

#if defined(CONFIG_HPLANCE) || defined(CONFIG_HPLANCE_MODULE)
#include "hplance.h"

#undef WRITERAP
#undef WRITERDP
#undef READRDP

#if defined(CONFIG_MVME147_NET) || defined(CONFIG_MVME147_NET_MODULE)

/* Lossage Factor Nine, Mr Sulu. */
#define WRITERAP(lp, x)	(lp->writerap(lp, x))
#define WRITERDP(lp, x)	(lp->writerdp(lp, x))
#define READRDP(lp)	(lp->readrdp(lp))

#else

/* These inlines can be used if only CONFIG_HPLANCE is defined */
static inline void WRITERAP(struct lance_private *lp, __u16 value)
{
	do {
		out_be16(lp->base + HPLANCE_REGOFF + LANCE_RAP, value);
	} while ((in_8(lp->base + HPLANCE_STATUS) & LE_ACK) == 0);
}

static inline void WRITERDP(struct lance_private *lp, __u16 value)
{
	do {
		out_be16(lp->base + HPLANCE_REGOFF + LANCE_RDP, value);
	} while ((in_8(lp->base + HPLANCE_STATUS) & LE_ACK) == 0);
}

static inline __u16 READRDP(struct lance_private *lp)
{
	__u16 value;
	do {
		value = in_be16(lp->base + HPLANCE_REGOFF + LANCE_RDP);
	} while ((in_8(lp->base + HPLANCE_STATUS) & LE_ACK) == 0);
	return value;
}

#endif
#endif /* CONFIG_HPLANCE || CONFIG_HPLANCE_MODULE */

/* debugging output macros, various flavours */
/* #define TEST_HITS */
#ifdef UNDEF
#define PRINT_RINGS() \
do { \
	int t; \
	for (t = 0; t < RX_RING_SIZE; t++) { \
		printk("R%d: @(%02X %04X) len %04X, mblen %04X, bits %02X\n", \
		       t, ib->brx_ring[t].rmd1_hadr, ib->brx_ring[t].rmd0, \
		       ib->brx_ring[t].length, \
		       ib->brx_ring[t].mblength, ib->brx_ring[t].rmd1_bits); \
	} \
	for (t = 0; t < TX_RING_SIZE; t++) { \
		printk("T%d: @(%02X %04X) len %04X, misc %04X, bits %02X\n", \
		       t, ib->btx_ring[t].tmd1_hadr, ib->btx_ring[t].tmd0, \
		       ib->btx_ring[t].length, \
		       ib->btx_ring[t].misc, ib->btx_ring[t].tmd1_bits); \
	} \
} while (0)
#else
#define PRINT_RINGS()
#endif

/* Load the CSR registers. The LANCE has to be STOPped when we do this! */
static void load_csrs(struct lance_private *lp)
{
	volatile struct lance_init_block *aib = lp->lance_init_block;
	int leptr;

	leptr = LANCE_ADDR(aib);

	WRITERAP(lp, LE_CSR1);                    /* load address of init block */
	WRITERDP(lp, leptr & 0xFFFF);
	WRITERAP(lp, LE_CSR2);
	WRITERDP(lp, leptr >> 16);
	WRITERAP(lp, LE_CSR3);
	WRITERDP(lp, lp->busmaster_regval);       /* set byteswap/ALEctrl/byte ctrl */

	/* Point back to csr0 */
	WRITERAP(lp, LE_CSR0);
}

/* #define to 0 or 1 appropriately */
#define DEBUG_IRING 0
/* Set up the Lance Rx and Tx rings and the init block */
static void lance_init_ring(struct net_device *dev)
{
	struct lance_private *lp = netdev_priv(dev);
	volatile struct lance_init_block *ib = lp->init_block;
	volatile struct lance_init_block *aib; /* for LANCE_ADDR computations */
	int leptr;
	int i;

	aib = lp->lance_init_block;

	lp->rx_new = lp->tx_new = 0;
	lp->rx_old = lp->tx_old = 0;

	ib->mode = LE_MO_PROM;                             /* normal, enable Tx & Rx */

	/* Copy the ethernet address to the lance init block
	 * Notice that we do a byteswap if we're big endian.
	 * [I think this is the right criterion; at least, sunlance,
	 * a2065 and atarilance do the byteswap and lance.c (PC) doesn't.
	 * However, the datasheet says that the BSWAP bit doesn't affect
	 * the init block, so surely it should be low byte first for
	 * everybody? Um.]
	 * We could define the ib->physaddr as three 16bit values and
	 * use (addr[1] << 8) | addr[0] & co, but this is more efficient.
	 */
#ifdef __BIG_ENDIAN
	ib->phys_addr[0] = dev->dev_addr[1];
	ib->phys_addr[1] = dev->dev_addr[0];
	ib->phys_addr[2] = dev->dev_addr[3];
	ib->phys_addr[3] = dev->dev_addr[2];
	ib->phys_addr[4] = dev->dev_addr[5];
	ib->phys_addr[5] = dev->dev_addr[4];
#else
	for (i = 0; i < 6; i++)
	       ib->phys_addr[i] = dev->dev_addr[i];
#endif

	if (DEBUG_IRING)
		printk("TX rings:\n");

	lp->tx_full = 0;
	/* Setup the Tx ring entries */
	for (i = 0; i < (1 << lp->lance_log_tx_bufs); i++) {
		leptr = LANCE_ADDR(&aib->tx_buf[i][0]);
		ib->btx_ring[i].tmd0      = leptr;
		ib->btx_ring[i].tmd1_hadr = leptr >> 16;
		ib->btx_ring[i].tmd1_bits = 0;
		ib->btx_ring[i].length    = 0xf000; /* The ones required by tmd2 */
		ib->btx_ring[i].misc      = 0;
		if (DEBUG_IRING)
			printk("%d: 0x%8.8x\n", i, leptr);
	}

	/* Setup the Rx ring entries */
	if (DEBUG_IRING)
		printk("RX rings:\n");
	for (i = 0; i < (1 << lp->lance_log_rx_bufs); i++) {
		leptr = LANCE_ADDR(&aib->rx_buf[i][0]);

		ib->brx_ring[i].rmd0      = leptr;
		ib->brx_ring[i].rmd1_hadr = leptr >> 16;
		ib->brx_ring[i].rmd1_bits = LE_R1_OWN;
		/* 0xf000 == bits that must be one (reserved, presumably) */
		ib->brx_ring[i].length    = -RX_BUFF_SIZE | 0xf000;
		ib->brx_ring[i].mblength  = 0;
		if (DEBUG_IRING)
			printk("%d: 0x%8.8x\n", i, leptr);
	}

	/* Setup the initialization block */

	/* Setup rx descriptor pointer */
	leptr = LANCE_ADDR(&aib->brx_ring);
	ib->rx_len = (lp->lance_log_rx_bufs << 13) | (leptr >> 16);
	ib->rx_ptr = leptr;
	if (DEBUG_IRING)
		printk("RX ptr: %8.8x\n", leptr);

	/* Setup tx descriptor pointer */
	leptr = LANCE_ADDR(&aib->btx_ring);
	ib->tx_len = (lp->lance_log_tx_bufs << 13) | (leptr >> 16);
	ib->tx_ptr = leptr;
	if (DEBUG_IRING)
		printk("TX ptr: %8.8x\n", leptr);

	/* Clear the multicast filter */
	ib->filter[0] = 0;
	ib->filter[1] = 0;
	PRINT_RINGS();
}

/* LANCE must be STOPped before we do this, too... */
static int init_restart_lance(struct lance_private *lp)
{
	int i;

	WRITERAP(lp, LE_CSR0);
	WRITERDP(lp, LE_C0_INIT);

	/* Need a hook here for sunlance ledma stuff */

	/* Wait for the lance to complete initialization */
	for (i = 0; (i < 100) && !(READRDP(lp) & (LE_C0_ERR | LE_C0_IDON)); i++)
		barrier();
	if ((i == 100) || (READRDP(lp) & LE_C0_ERR)) {
		printk("LANCE unopened after %d ticks, csr0=%4.4x.\n", i, READRDP(lp));
		return -1;
	}

	/* Clear IDON by writing a "1", enable interrupts and start lance */
	WRITERDP(lp, LE_C0_IDON);
	WRITERDP(lp, LE_C0_INEA | LE_C0_STRT);

	return 0;
}

static int lance_reset(struct net_device *dev)
{
	struct lance_private *lp = netdev_priv(dev);
	int status;

	/* Stop the lance */
	WRITERAP(lp, LE_CSR0);
	WRITERDP(lp, LE_C0_STOP);

	load_csrs(lp);
	lance_init_ring(dev);
	dev->trans_start = jiffies; /* prevent tx timeout */
	status = init_restart_lance(lp);
#ifdef DEBUG_DRIVER
	printk("Lance restart=%d\n", status);
#endif
	return status;
}

static int lance_rx(struct net_device *dev)
{
	struct lance_private *lp = netdev_priv(dev);
	volatile struct lance_init_block *ib = lp->init_block;
	volatile struct lance_rx_desc *rd;
	unsigned char bits;
#ifdef TEST_HITS
	int i;
#endif

#ifdef TEST_HITS
	printk("[");
	for (i = 0; i < RX_RING_SIZE; i++) {
		if (i == lp->rx_new)
			printk("%s",
			       ib->brx_ring[i].rmd1_bits & LE_R1_OWN ? "_" : "X");
		else
			printk("%s",
			      ib->brx_ring[i].rmd1_bits & LE_R1_OWN ? "." : "1");
	}
	printk("]");
#endif
#ifdef CONFIG_HP300
	blinken_leds(0x40, 0);
#endif
	WRITERDP(lp, LE_C0_RINT | LE_C0_INEA);     /* ack Rx int, reenable ints */
	for (rd = &ib->brx_ring[lp->rx_new];     /* For each Rx ring we own... */
	     !((bits = rd->rmd1_bits) & LE_R1_OWN);
	     rd = &ib->brx_ring[lp->rx_new]) {

		/* We got an incomplete frame? */
		if ((bits & LE_R1_POK) != LE_R1_POK) {
			dev->stats.rx_over_errors++;
			dev->stats.rx_errors++;
			continue;
		} else if (bits & LE_R1_ERR) {
			/* Count only the end frame as a rx error,
			 * not the beginning
			 */
			if (bits & LE_R1_BUF)
				dev->stats.rx_fifo_errors++;
			if (bits & LE_R1_CRC)
				dev->stats.rx_crc_errors++;
			if (bits & LE_R1_OFL)
				dev->stats.rx_over_errors++;
			if (bits & LE_R1_FRA)
				dev->stats.rx_frame_errors++;
			if (bits & LE_R1_EOP)
				dev->stats.rx_errors++;
		} else {
			int len = (rd->mblength & 0xfff) - 4;
			struct sk_buff *skb = netdev_alloc_skb(dev, len + 2);

			if (!skb) {
				dev->stats.rx_dropped++;
				rd->mblength = 0;
				rd->rmd1_bits = LE_R1_OWN;
				lp->rx_new = (lp->rx_new + 1) & lp->rx_ring_mod_mask;
				return 0;
			}

			skb_reserve(skb, 2);           /* 16 byte align */
			skb_put(skb, len);             /* make room */
			skb_copy_to_linear_data(skb,
					 (unsigned char *)&(ib->rx_buf[lp->rx_new][0]),
					 len);
			skb->protocol = eth_type_trans(skb, dev);
			netif_rx(skb);
			dev->stats.rx_packets++;
			dev->stats.rx_bytes += len;
		}

		/* Return the packet to the pool */
		rd->mblength = 0;
		rd->rmd1_bits = LE_R1_OWN;
		lp->rx_new = (lp->rx_new + 1) & lp->rx_ring_mod_mask;
	}
	return 0;
}

static int lance_tx(struct net_device *dev)
{
	struct lance_private *lp = netdev_priv(dev);
	volatile struct lance_init_block *ib = lp->init_block;
	volatile struct lance_tx_desc *td;
	int i, j;
	int status;

#ifdef CONFIG_HP300
	blinken_leds(0x80, 0);
#endif
	/* csr0 is 2f3 */
	WRITERDP(lp, LE_C0_TINT | LE_C0_INEA);
	/* csr0 is 73 */

	j = lp->tx_old;
	for (i = j; i != lp->tx_new; i = j) {
		td = &ib->btx_ring[i];

		/* If we hit a packet not owned by us, stop */
		if (td->tmd1_bits & LE_T1_OWN)
			break;

		if (td->tmd1_bits & LE_T1_ERR) {
			status = td->misc;

			dev->stats.tx_errors++;
			if (status & LE_T3_RTY)
				dev->stats.tx_aborted_errors++;
			if (status & LE_T3_LCOL)
				dev->stats.tx_window_errors++;

			if (status & LE_T3_CLOS) {
				dev->stats.tx_carrier_errors++;
				if (lp->auto_select) {
					lp->tpe = 1 - lp->tpe;
					printk("%s: Carrier Lost, trying %s\n",
					       dev->name,
					       lp->tpe ? "TPE" : "AUI");
					/* Stop the lance */
					WRITERAP(lp, LE_CSR0);
					WRITERDP(lp, LE_C0_STOP);
					lance_init_ring(dev);
					load_csrs(lp);
					init_restart_lance(lp);
					return 0;
				}
			}

			/* buffer errors and underflows turn off the transmitter */
			/* Restart the adapter */
			if (status & (LE_T3_BUF|LE_T3_UFL)) {
				dev->stats.tx_fifo_errors++;

				printk("%s: Tx: ERR_BUF|ERR_UFL, restarting\n",
				       dev->name);
				/* Stop the lance */
				WRITERAP(lp, LE_CSR0);
				WRITERDP(lp, LE_C0_STOP);
				lance_init_ring(dev);
				load_csrs(lp);
				init_restart_lance(lp);
				return 0;
			}
		} else if ((td->tmd1_bits & LE_T1_POK) == LE_T1_POK) {
			/*
			 * So we don't count the packet more than once.
			 */
			td->tmd1_bits &= ~(LE_T1_POK);

			/* One collision before packet was sent. */
			if (td->tmd1_bits & LE_T1_EONE)
				dev->stats.collisions++;

			/* More than one collision, be optimistic. */
			if (td->tmd1_bits & LE_T1_EMORE)
				dev->stats.collisions += 2;

			dev->stats.tx_packets++;
		}

		j = (j + 1) & lp->tx_ring_mod_mask;
	}
	lp->tx_old = j;
	WRITERDP(lp, LE_C0_TINT | LE_C0_INEA);
	return 0;
}

static irqreturn_t
lance_interrupt(int irq, void *dev_id)
{
	struct net_device *dev = (struct net_device *)dev_id;
	struct lance_private *lp = netdev_priv(dev);
	int csr0;

	spin_lock(&lp->devlock);

	WRITERAP(lp, LE_CSR0);              /* LANCE Controller Status */
	csr0 = READRDP(lp);

	PRINT_RINGS();

	if (!(csr0 & LE_C0_INTR)) {     /* Check if any interrupt has */
		spin_unlock(&lp->devlock);
		return IRQ_NONE;        /* been generated by the Lance. */
	}

	/* Acknowledge all the interrupt sources ASAP */
	WRITERDP(lp, csr0 & ~(LE_C0_INEA|LE_C0_TDMD|LE_C0_STOP|LE_C0_STRT|LE_C0_INIT));

	if ((csr0 & LE_C0_ERR)) {
		/* Clear the error condition */
		WRITERDP(lp, LE_C0_BABL|LE_C0_ERR|LE_C0_MISS|LE_C0_INEA);
	}

	if (csr0 & LE_C0_RINT)
		lance_rx(dev);

	if (csr0 & LE_C0_TINT)
		lance_tx(dev);

	/* Log misc errors. */
	if (csr0 & LE_C0_BABL)
		dev->stats.tx_errors++;       /* Tx babble. */
	if (csr0 & LE_C0_MISS)
		dev->stats.rx_errors++;       /* Missed a Rx frame. */
	if (csr0 & LE_C0_MERR) {
		printk("%s: Bus master arbitration failure, status %4.4x.\n",
		       dev->name, csr0);
		/* Restart the chip. */
		WRITERDP(lp, LE_C0_STRT);
	}

	if (lp->tx_full && netif_queue_stopped(dev) && (TX_BUFFS_AVAIL >= 0)) {
		lp->tx_full = 0;
		netif_wake_queue(dev);
	}

	WRITERAP(lp, LE_CSR0);
	WRITERDP(lp, LE_C0_BABL|LE_C0_CERR|LE_C0_MISS|LE_C0_MERR|LE_C0_IDON|LE_C0_INEA);

	spin_unlock(&lp->devlock);
	return IRQ_HANDLED;
}

int lance_open(struct net_device *dev)
{
	struct lance_private *lp = netdev_priv(dev);
	int res;

	/* Install the Interrupt handler. Or we could shunt this out to specific drivers? */
	if (request_irq(lp->irq, lance_interrupt, IRQF_SHARED, lp->name, dev))
		return -EAGAIN;

	res = lance_reset(dev);
	spin_lock_init(&lp->devlock);
	netif_start_queue(dev);

	return res;
}
EXPORT_SYMBOL_GPL(lance_open);

int lance_close(struct net_device *dev)
{
	struct lance_private *lp = netdev_priv(dev);

	netif_stop_queue(dev);

	/* Stop the LANCE */
	WRITERAP(lp, LE_CSR0);
	WRITERDP(lp, LE_C0_STOP);

	free_irq(lp->irq, dev);

	return 0;
}
EXPORT_SYMBOL_GPL(lance_close);

void lance_tx_timeout(struct net_device *dev)
{
	printk("lance_tx_timeout\n");
	lance_reset(dev);
	dev->trans_start = jiffies; /* prevent tx timeout */
	netif_wake_queue(dev);
}
EXPORT_SYMBOL_GPL(lance_tx_timeout);

int lance_start_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct lance_private *lp = netdev_priv(dev);
	volatile struct lance_init_block *ib = lp->init_block;
	int entry, skblen, len;
	static int outs;
	unsigned long flags;

	if (!TX_BUFFS_AVAIL)
		return NETDEV_TX_LOCKED;

	netif_stop_queue(dev);

	skblen = skb->len;

#ifdef DEBUG_DRIVER
	/* dump the packet */
	{
		int i;

		for (i = 0; i < 64; i++) {
			if ((i % 16) == 0)
				printk("\n");
			printk("%2.2x ", skb->data[i]);
		}
	}
#endif
	len = (skblen <= ETH_ZLEN) ? ETH_ZLEN : skblen;
	entry = lp->tx_new & lp->tx_ring_mod_mask;
	ib->btx_ring[entry].length = (-len) | 0xf000;
	ib->btx_ring[entry].misc = 0;

	if (skb->len < ETH_ZLEN)
		memset((void *)&ib->tx_buf[entry][0], 0, ETH_ZLEN);
	skb_copy_from_linear_data(skb, (void *)&ib->tx_buf[entry][0], skblen);

	/* Now, give the packet to the lance */
	ib->btx_ring[entry].tmd1_bits = (LE_T1_POK|LE_T1_OWN);
	lp->tx_new = (lp->tx_new + 1) & lp->tx_ring_mod_mask;

	outs++;
	/* Kick the lance: transmit now */
	WRITERDP(lp, LE_C0_INEA | LE_C0_TDMD);
	dev_kfree_skb(skb);

	spin_lock_irqsave(&lp->devlock, flags);
	if (TX_BUFFS_AVAIL)
		netif_start_queue(dev);
	else
		lp->tx_full = 1;
	spin_unlock_irqrestore(&lp->devlock, flags);

	return NETDEV_TX_OK;
}
EXPORT_SYMBOL_GPL(lance_start_xmit);

/* taken from the depca driver via a2065.c */
static void lance_load_multicast(struct net_device *dev)
{
	struct lance_private *lp = netdev_priv(dev);
	volatile struct lance_init_block *ib = lp->init_block;
	volatile u16 *mcast_table = (u16 *)&ib->filter;
	struct netdev_hw_addr *ha;
	u32 crc;

	/* set all multicast bits */
	if (dev->flags & IFF_ALLMULTI) {
		ib->filter[0] = 0xffffffff;
		ib->filter[1] = 0xffffffff;
		return;
	}
	/* clear the multicast filter */
	ib->filter[0] = 0;
	ib->filter[1] = 0;

	/* Add addresses */
	netdev_for_each_mc_addr(ha, dev) {
		crc = ether_crc_le(6, ha->addr);
		crc = crc >> 26;
		mcast_table[crc >> 4] |= 1 << (crc & 0xf);
	}
}


void lance_set_multicast(struct net_device *dev)
{
	struct lance_private *lp = netdev_priv(dev);
	volatile struct lance_init_block *ib = lp->init_block;
	int stopped;

	stopped = netif_queue_stopped(dev);
	if (!stopped)
		netif_stop_queue(dev);

	while (lp->tx_old != lp->tx_new)
		schedule();

	WRITERAP(lp, LE_CSR0);
	WRITERDP(lp, LE_C0_STOP);
	lance_init_ring(dev);

	if (dev->flags & IFF_PROMISC) {
		ib->mode |= LE_MO_PROM;
	} else {
		ib->mode &= ~LE_MO_PROM;
		lance_load_multicast(dev);
	}
	load_csrs(lp);
	init_restart_lance(lp);

	if (!stopped)
		netif_start_queue(dev);
}
EXPORT_SYMBOL_GPL(lance_set_multicast);

#ifdef CONFIG_NET_POLL_CONTROLLER
void lance_poll(struct net_device *dev)
{
	struct lance_private *lp = netdev_priv(dev);

	spin_lock(&lp->devlock);
	WRITERAP(lp, LE_CSR0);
	WRITERDP(lp, LE_C0_STRT);
	spin_unlock(&lp->devlock);
	lance_interrupt(dev->irq, dev);
}
#endif

MODULE_LICENSE("GPL");
