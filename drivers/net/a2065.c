/*
 * Amiga Linux/68k A2065 Ethernet Driver
 *
 * (C) Copyright 1995-2003 by Geert Uytterhoeven <geert@linux-m68k.org>
 *
 * Fixes and tips by:
 *	- Janos Farkas (CHEXUM@sparta.banki.hu)
 *	- Jes Degn Soerensen (jds@kom.auc.dk)
 *	- Matt Domsch (Matt_Domsch@dell.com)
 *
 * ----------------------------------------------------------------------------
 *
 * This program is based on
 *
 *	ariadne.?:	Amiga Linux/68k Ariadne Ethernet Driver
 *			(C) Copyright 1995 by Geert Uytterhoeven,
 *                                            Peter De Schrijver
 *
 *	lance.c:	An AMD LANCE ethernet driver for linux.
 *			Written 1993-94 by Donald Becker.
 *
 *	Am79C960:	PCnet(tm)-ISA Single-Chip Ethernet Controller
 *			Advanced Micro Devices
 *			Publication #16907, Rev. B, Amendment/0, May 1994
 *
 * ----------------------------------------------------------------------------
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file COPYING in the main directory of the Linux
 * distribution for more details.
 *
 * ----------------------------------------------------------------------------
 *
 * The A2065 is a Zorro-II board made by Commodore/Ameristar. It contains:
 *
 *	- an Am7990 Local Area Network Controller for Ethernet (LANCE) with
 *	  both 10BASE-2 (thin coax) and AUI (DB-15) connectors
 */

#include <linux/errno.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/module.h>
#include <linux/stddef.h>
#include <linux/kernel.h>
#include <linux/interrupt.h>
#include <linux/ioport.h>
#include <linux/skbuff.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/init.h>
#include <linux/crc32.h>
#include <linux/zorro.h>
#include <linux/bitops.h>

#include <asm/irq.h>
#include <asm/amigaints.h>
#include <asm/amigahw.h>

#include "a2065.h"


	/*
	 *		Transmit/Receive Ring Definitions
	 */

#define LANCE_LOG_TX_BUFFERS	(2)
#define LANCE_LOG_RX_BUFFERS	(4)

#define TX_RING_SIZE		(1<<LANCE_LOG_TX_BUFFERS)
#define RX_RING_SIZE		(1<<LANCE_LOG_RX_BUFFERS)

#define TX_RING_MOD_MASK	(TX_RING_SIZE-1)
#define RX_RING_MOD_MASK	(RX_RING_SIZE-1)

#define PKT_BUF_SIZE		(1544)
#define RX_BUFF_SIZE            PKT_BUF_SIZE
#define TX_BUFF_SIZE            PKT_BUF_SIZE


	/*
	 *		Layout of the Lance's RAM Buffer
	 */


struct lance_init_block {
	unsigned short mode;		/* Pre-set mode (reg. 15) */
	unsigned char phys_addr[6];     /* Physical ethernet address */
	unsigned filter[2];		/* Multicast filter. */

	/* Receive and transmit ring base, along with extra bits. */
	unsigned short rx_ptr;		/* receive descriptor addr */
	unsigned short rx_len;		/* receive len and high addr */
	unsigned short tx_ptr;		/* transmit descriptor addr */
	unsigned short tx_len;		/* transmit len and high addr */

	/* The Tx and Rx ring entries must aligned on 8-byte boundaries. */
	struct lance_rx_desc brx_ring[RX_RING_SIZE];
	struct lance_tx_desc btx_ring[TX_RING_SIZE];

	char   rx_buf [RX_RING_SIZE][RX_BUFF_SIZE];
	char   tx_buf [TX_RING_SIZE][TX_BUFF_SIZE];
};


	/*
	 *		Private Device Data
	 */

struct lance_private {
	char *name;
	volatile struct lance_regs *ll;
	volatile struct lance_init_block *init_block;	    /* Hosts view */
	volatile struct lance_init_block *lance_init_block; /* Lance view */

	int rx_new, tx_new;
	int rx_old, tx_old;

	int lance_log_rx_bufs, lance_log_tx_bufs;
	int rx_ring_mod_mask, tx_ring_mod_mask;

	int tpe;		      /* cable-selection is TPE */
	int auto_select;	      /* cable-selection by carrier */
	unsigned short busmaster_regval;

#ifdef CONFIG_SUNLANCE
	struct Linux_SBus_DMA *ledma; /* if set this points to ledma and arch=4m */
	int burst_sizes;	      /* ledma SBus burst sizes */
#endif
	struct timer_list         multicast_timer;
};

#define TX_BUFFS_AVAIL ((lp->tx_old<=lp->tx_new)?\
			lp->tx_old+lp->tx_ring_mod_mask-lp->tx_new:\
			lp->tx_old - lp->tx_new-1)


#define LANCE_ADDR(x) ((int)(x) & ~0xff000000)

/* Load the CSR registers */
static void load_csrs (struct lance_private *lp)
{
	volatile struct lance_regs *ll = lp->ll;
	volatile struct lance_init_block *aib = lp->lance_init_block;
	int leptr;

	leptr = LANCE_ADDR (aib);

	ll->rap = LE_CSR1;
	ll->rdp = (leptr & 0xFFFF);
	ll->rap = LE_CSR2;
	ll->rdp = leptr >> 16;
	ll->rap = LE_CSR3;
	ll->rdp = lp->busmaster_regval;

	/* Point back to csr0 */
	ll->rap = LE_CSR0;
}

#define ZERO 0

/* Setup the Lance Rx and Tx rings */
static void lance_init_ring (struct net_device *dev)
{
	struct lance_private *lp = netdev_priv(dev);
	volatile struct lance_init_block *ib = lp->init_block;
	volatile struct lance_init_block *aib; /* for LANCE_ADDR computations */
	int leptr;
	int i;

	aib = lp->lance_init_block;

	/* Lock out other processes while setting up hardware */
	netif_stop_queue(dev);
	lp->rx_new = lp->tx_new = 0;
	lp->rx_old = lp->tx_old = 0;

	ib->mode = 0;

	/* Copy the ethernet address to the lance init block
	 * Note that on the sparc you need to swap the ethernet address.
	 */
	ib->phys_addr [0] = dev->dev_addr [1];
	ib->phys_addr [1] = dev->dev_addr [0];
	ib->phys_addr [2] = dev->dev_addr [3];
	ib->phys_addr [3] = dev->dev_addr [2];
	ib->phys_addr [4] = dev->dev_addr [5];
	ib->phys_addr [5] = dev->dev_addr [4];

	if (ZERO)
		printk(KERN_DEBUG "TX rings:\n");

	/* Setup the Tx ring entries */
	for (i = 0; i <= (1<<lp->lance_log_tx_bufs); i++) {
		leptr = LANCE_ADDR(&aib->tx_buf[i][0]);
		ib->btx_ring [i].tmd0      = leptr;
		ib->btx_ring [i].tmd1_hadr = leptr >> 16;
		ib->btx_ring [i].tmd1_bits = 0;
		ib->btx_ring [i].length    = 0xf000; /* The ones required by tmd2 */
		ib->btx_ring [i].misc      = 0;
		if (i < 3 && ZERO)
			printk(KERN_DEBUG "%d: 0x%8.8x\n", i, leptr);
	}

	/* Setup the Rx ring entries */
	if (ZERO)
		printk(KERN_DEBUG "RX rings:\n");
	for (i = 0; i < (1<<lp->lance_log_rx_bufs); i++) {
		leptr = LANCE_ADDR(&aib->rx_buf[i][0]);

		ib->brx_ring [i].rmd0      = leptr;
		ib->brx_ring [i].rmd1_hadr = leptr >> 16;
		ib->brx_ring [i].rmd1_bits = LE_R1_OWN;
		ib->brx_ring [i].length    = -RX_BUFF_SIZE | 0xf000;
		ib->brx_ring [i].mblength  = 0;
		if (i < 3 && ZERO)
			printk(KERN_DEBUG "%d: 0x%8.8x\n", i, leptr);
	}

	/* Setup the initialization block */

	/* Setup rx descriptor pointer */
	leptr = LANCE_ADDR(&aib->brx_ring);
	ib->rx_len = (lp->lance_log_rx_bufs << 13) | (leptr >> 16);
	ib->rx_ptr = leptr;
	if (ZERO)
		printk(KERN_DEBUG "RX ptr: %8.8x\n", leptr);

	/* Setup tx descriptor pointer */
	leptr = LANCE_ADDR(&aib->btx_ring);
	ib->tx_len = (lp->lance_log_tx_bufs << 13) | (leptr >> 16);
	ib->tx_ptr = leptr;
	if (ZERO)
		printk(KERN_DEBUG "TX ptr: %8.8x\n", leptr);

	/* Clear the multicast filter */
	ib->filter [0] = 0;
	ib->filter [1] = 0;
}

static int init_restart_lance (struct lance_private *lp)
{
	volatile struct lance_regs *ll = lp->ll;
	int i;

	ll->rap = LE_CSR0;
	ll->rdp = LE_C0_INIT;

	/* Wait for the lance to complete initialization */
	for (i = 0; (i < 100) && !(ll->rdp & (LE_C0_ERR | LE_C0_IDON)); i++)
		barrier();
	if ((i == 100) || (ll->rdp & LE_C0_ERR)) {
		printk(KERN_ERR "LANCE unopened after %d ticks, csr0=%4.4x.\n",
		       i, ll->rdp);
		return -EIO;
	}

	/* Clear IDON by writing a "1", enable interrupts and start lance */
	ll->rdp = LE_C0_IDON;
	ll->rdp = LE_C0_INEA | LE_C0_STRT;

	return 0;
}

static int lance_rx (struct net_device *dev)
{
	struct lance_private *lp = netdev_priv(dev);
	volatile struct lance_init_block *ib = lp->init_block;
	volatile struct lance_regs *ll = lp->ll;
	volatile struct lance_rx_desc *rd;
	unsigned char bits;

#ifdef TEST_HITS
	int i;
	printk(KERN_DEBUG "[");
	for (i = 0; i < RX_RING_SIZE; i++) {
		if (i == lp->rx_new)
			printk ("%s",
				ib->brx_ring [i].rmd1_bits & LE_R1_OWN ? "_" : "X");
		else
			printk ("%s",
				ib->brx_ring [i].rmd1_bits & LE_R1_OWN ? "." : "1");
	}
	printk ("]\n");
#endif

	ll->rdp = LE_C0_RINT|LE_C0_INEA;
	for (rd = &ib->brx_ring [lp->rx_new];
	     !((bits = rd->rmd1_bits) & LE_R1_OWN);
	     rd = &ib->brx_ring [lp->rx_new]) {

		/* We got an incomplete frame? */
		if ((bits & LE_R1_POK) != LE_R1_POK) {
			dev->stats.rx_over_errors++;
			dev->stats.rx_errors++;
			continue;
		} else if (bits & LE_R1_ERR) {
			/* Count only the end frame as a rx error,
			 * not the beginning
			 */
			if (bits & LE_R1_BUF) dev->stats.rx_fifo_errors++;
			if (bits & LE_R1_CRC) dev->stats.rx_crc_errors++;
			if (bits & LE_R1_OFL) dev->stats.rx_over_errors++;
			if (bits & LE_R1_FRA) dev->stats.rx_frame_errors++;
			if (bits & LE_R1_EOP) dev->stats.rx_errors++;
		} else {
			int len = (rd->mblength & 0xfff) - 4;
			struct sk_buff *skb = dev_alloc_skb (len+2);

			if (!skb) {
				printk(KERN_WARNING "%s: Memory squeeze, "
				       "deferring packet.\n", dev->name);
				dev->stats.rx_dropped++;
				rd->mblength = 0;
				rd->rmd1_bits = LE_R1_OWN;
				lp->rx_new = (lp->rx_new + 1) & lp->rx_ring_mod_mask;
				return 0;
			}

			skb_reserve (skb, 2);		/* 16 byte align */
			skb_put (skb, len);		/* make room */
			skb_copy_to_linear_data(skb,
					 (unsigned char *)&(ib->rx_buf [lp->rx_new][0]),
					 len);
			skb->protocol = eth_type_trans (skb, dev);
			netif_rx (skb);
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

static int lance_tx (struct net_device *dev)
{
	struct lance_private *lp = netdev_priv(dev);
	volatile struct lance_init_block *ib = lp->init_block;
	volatile struct lance_regs *ll = lp->ll;
	volatile struct lance_tx_desc *td;
	int i, j;
	int status;

	/* csr0 is 2f3 */
	ll->rdp = LE_C0_TINT | LE_C0_INEA;
	/* csr0 is 73 */

	j = lp->tx_old;
	for (i = j; i != lp->tx_new; i = j) {
		td = &ib->btx_ring [i];

		/* If we hit a packet not owned by us, stop */
		if (td->tmd1_bits & LE_T1_OWN)
			break;

		if (td->tmd1_bits & LE_T1_ERR) {
			status = td->misc;

			dev->stats.tx_errors++;
			if (status & LE_T3_RTY)  dev->stats.tx_aborted_errors++;
			if (status & LE_T3_LCOL) dev->stats.tx_window_errors++;

			if (status & LE_T3_CLOS) {
				dev->stats.tx_carrier_errors++;
				if (lp->auto_select) {
					lp->tpe = 1 - lp->tpe;
					printk(KERN_ERR "%s: Carrier Lost, "
					       "trying %s\n", dev->name,
					       lp->tpe?"TPE":"AUI");
					/* Stop the lance */
					ll->rap = LE_CSR0;
					ll->rdp = LE_C0_STOP;
					lance_init_ring (dev);
					load_csrs (lp);
					init_restart_lance (lp);
					return 0;
				}
			}

			/* buffer errors and underflows turn off the transmitter */
			/* Restart the adapter */
			if (status & (LE_T3_BUF|LE_T3_UFL)) {
				dev->stats.tx_fifo_errors++;

				printk(KERN_ERR "%s: Tx: ERR_BUF|ERR_UFL, "
				       "restarting\n", dev->name);
				/* Stop the lance */
				ll->rap = LE_CSR0;
				ll->rdp = LE_C0_STOP;
				lance_init_ring (dev);
				load_csrs (lp);
				init_restart_lance (lp);
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
	ll->rdp = LE_C0_TINT | LE_C0_INEA;
	return 0;
}

static irqreturn_t lance_interrupt (int irq, void *dev_id)
{
	struct net_device *dev;
	struct lance_private *lp;
	volatile struct lance_regs *ll;
	int csr0;

	dev = (struct net_device *) dev_id;

	lp = netdev_priv(dev);
	ll = lp->ll;

	ll->rap = LE_CSR0;		/* LANCE Controller Status */
	csr0 = ll->rdp;

	if (!(csr0 & LE_C0_INTR))	/* Check if any interrupt has */
		return IRQ_NONE;	/* been generated by the Lance. */

	/* Acknowledge all the interrupt sources ASAP */
	ll->rdp = csr0 & ~(LE_C0_INEA|LE_C0_TDMD|LE_C0_STOP|LE_C0_STRT|
			   LE_C0_INIT);

	if ((csr0 & LE_C0_ERR)) {
		/* Clear the error condition */
		ll->rdp = LE_C0_BABL|LE_C0_ERR|LE_C0_MISS|LE_C0_INEA;
	}

	if (csr0 & LE_C0_RINT)
		lance_rx (dev);

	if (csr0 & LE_C0_TINT)
		lance_tx (dev);

	/* Log misc errors. */
	if (csr0 & LE_C0_BABL)
		dev->stats.tx_errors++;       /* Tx babble. */
	if (csr0 & LE_C0_MISS)
		dev->stats.rx_errors++;       /* Missed a Rx frame. */
	if (csr0 & LE_C0_MERR) {
		printk(KERN_ERR "%s: Bus master arbitration failure, status "
		       "%4.4x.\n", dev->name, csr0);
		/* Restart the chip. */
		ll->rdp = LE_C0_STRT;
	}

	if (netif_queue_stopped(dev) && TX_BUFFS_AVAIL > 0)
		netif_wake_queue(dev);

	ll->rap = LE_CSR0;
	ll->rdp = LE_C0_BABL|LE_C0_CERR|LE_C0_MISS|LE_C0_MERR|
					LE_C0_IDON|LE_C0_INEA;
	return IRQ_HANDLED;
}

static int lance_open (struct net_device *dev)
{
	struct lance_private *lp = netdev_priv(dev);
	volatile struct lance_regs *ll = lp->ll;
	int ret;

	/* Stop the Lance */
	ll->rap = LE_CSR0;
	ll->rdp = LE_C0_STOP;

	/* Install the Interrupt handler */
	ret = request_irq(IRQ_AMIGA_PORTS, lance_interrupt, IRQF_SHARED,
			  dev->name, dev);
	if (ret) return ret;

	load_csrs (lp);
	lance_init_ring (dev);

	netif_start_queue(dev);

	return init_restart_lance (lp);
}

static int lance_close (struct net_device *dev)
{
	struct lance_private *lp = netdev_priv(dev);
	volatile struct lance_regs *ll = lp->ll;

	netif_stop_queue(dev);
	del_timer_sync(&lp->multicast_timer);

	/* Stop the card */
	ll->rap = LE_CSR0;
	ll->rdp = LE_C0_STOP;

	free_irq(IRQ_AMIGA_PORTS, dev);
	return 0;
}

static inline int lance_reset (struct net_device *dev)
{
	struct lance_private *lp = netdev_priv(dev);
	volatile struct lance_regs *ll = lp->ll;
	int status;

	/* Stop the lance */
	ll->rap = LE_CSR0;
	ll->rdp = LE_C0_STOP;

	load_csrs (lp);

	lance_init_ring (dev);
	dev->trans_start = jiffies;
	netif_start_queue(dev);

	status = init_restart_lance (lp);
#ifdef DEBUG_DRIVER
	printk(KERN_DEBUG "Lance restart=%d\n", status);
#endif
	return status;
}

static void lance_tx_timeout(struct net_device *dev)
{
	struct lance_private *lp = netdev_priv(dev);
	volatile struct lance_regs *ll = lp->ll;

	printk(KERN_ERR "%s: transmit timed out, status %04x, reset\n",
	       dev->name, ll->rdp);
	lance_reset(dev);
	netif_wake_queue(dev);
}

static int lance_start_xmit (struct sk_buff *skb, struct net_device *dev)
{
	struct lance_private *lp = netdev_priv(dev);
	volatile struct lance_regs *ll = lp->ll;
	volatile struct lance_init_block *ib = lp->init_block;
	int entry, skblen;
	int status = 0;
	unsigned long flags;

	if (skb_padto(skb, ETH_ZLEN))
		return 0;
	skblen = max_t(unsigned, skb->len, ETH_ZLEN);

	local_irq_save(flags);

	if (!TX_BUFFS_AVAIL){
		local_irq_restore(flags);
		return NETDEV_TX_LOCKED;
	}

#ifdef DEBUG_DRIVER
	/* dump the packet */
	print_hex_dump(KERN_DEBUG, "skb->data: ", DUMP_PREFIX_NONE,
		       16, 1, skb->data, 64, true);
#endif
	entry = lp->tx_new & lp->tx_ring_mod_mask;
	ib->btx_ring [entry].length = (-skblen) | 0xf000;
	ib->btx_ring [entry].misc = 0;

	skb_copy_from_linear_data(skb, (void *)&ib->tx_buf [entry][0], skblen);

	/* Now, give the packet to the lance */
	ib->btx_ring [entry].tmd1_bits = (LE_T1_POK|LE_T1_OWN);
	lp->tx_new = (lp->tx_new+1) & lp->tx_ring_mod_mask;
	dev->stats.tx_bytes += skblen;

	if (TX_BUFFS_AVAIL <= 0)
		netif_stop_queue(dev);

	/* Kick the lance: transmit now */
	ll->rdp = LE_C0_INEA | LE_C0_TDMD;
	dev->trans_start = jiffies;
	dev_kfree_skb (skb);

	local_irq_restore(flags);

	return status;
}

/* taken from the depca driver */
static void lance_load_multicast (struct net_device *dev)
{
	struct lance_private *lp = netdev_priv(dev);
	volatile struct lance_init_block *ib = lp->init_block;
	volatile u16 *mcast_table = (u16 *)&ib->filter;
	struct dev_mc_list *dmi=dev->mc_list;
	char *addrs;
	int i;
	u32 crc;

	/* set all multicast bits */
	if (dev->flags & IFF_ALLMULTI){
		ib->filter [0] = 0xffffffff;
		ib->filter [1] = 0xffffffff;
		return;
	}
	/* clear the multicast filter */
	ib->filter [0] = 0;
	ib->filter [1] = 0;

	/* Add addresses */
	for (i = 0; i < dev->mc_count; i++){
		addrs = dmi->dmi_addr;
		dmi   = dmi->next;

		/* multicast address? */
		if (!(*addrs & 1))
			continue;

		crc = ether_crc_le(6, addrs);
		crc = crc >> 26;
		mcast_table [crc >> 4] |= 1 << (crc & 0xf);
	}
	return;
}

static void lance_set_multicast (struct net_device *dev)
{
	struct lance_private *lp = netdev_priv(dev);
	volatile struct lance_init_block *ib = lp->init_block;
	volatile struct lance_regs *ll = lp->ll;

	if (!netif_running(dev))
		return;

	if (lp->tx_old != lp->tx_new) {
		mod_timer(&lp->multicast_timer, jiffies + 4);
		netif_wake_queue(dev);
		return;
	}

	netif_stop_queue(dev);

	ll->rap = LE_CSR0;
	ll->rdp = LE_C0_STOP;
	lance_init_ring (dev);

	if (dev->flags & IFF_PROMISC) {
		ib->mode |= LE_MO_PROM;
	} else {
		ib->mode &= ~LE_MO_PROM;
		lance_load_multicast (dev);
	}
	load_csrs (lp);
	init_restart_lance (lp);
	netif_wake_queue(dev);
}

static int __devinit a2065_init_one(struct zorro_dev *z,
				    const struct zorro_device_id *ent);
static void __devexit a2065_remove_one(struct zorro_dev *z);


static struct zorro_device_id a2065_zorro_tbl[] __devinitdata = {
	{ ZORRO_PROD_CBM_A2065_1 },
	{ ZORRO_PROD_CBM_A2065_2 },
	{ ZORRO_PROD_AMERISTAR_A2065 },
	{ 0 }
};

static struct zorro_driver a2065_driver = {
	.name		= "a2065",
	.id_table	= a2065_zorro_tbl,
	.probe		= a2065_init_one,
	.remove		= __devexit_p(a2065_remove_one),
};

static const struct net_device_ops lance_netdev_ops = {
	.ndo_open		= lance_open,
	.ndo_stop		= lance_close,
	.ndo_start_xmit		= lance_start_xmit,
	.ndo_tx_timeout		= lance_tx_timeout,
	.ndo_set_multicast_list	= lance_set_multicast,
	.ndo_validate_addr	= eth_validate_addr,
	.ndo_change_mtu		= eth_change_mtu,
	.ndo_set_mac_address	= eth_mac_addr,
};

static int __devinit a2065_init_one(struct zorro_dev *z,
				    const struct zorro_device_id *ent)
{
	struct net_device *dev;
	struct lance_private *priv;
	unsigned long board, base_addr, mem_start;
	struct resource *r1, *r2;
	int err;

	board = z->resource.start;
	base_addr = board+A2065_LANCE;
	mem_start = board+A2065_RAM;

	r1 = request_mem_region(base_addr, sizeof(struct lance_regs),
				"Am7990");
	if (!r1)
		return -EBUSY;
	r2 = request_mem_region(mem_start, A2065_RAM_SIZE, "RAM");
	if (!r2) {
		release_resource(r1);
		return -EBUSY;
	}

	dev = alloc_etherdev(sizeof(struct lance_private));
	if (dev == NULL) {
		release_resource(r1);
		release_resource(r2);
		return -ENOMEM;
	}

	priv = netdev_priv(dev);

	r1->name = dev->name;
	r2->name = dev->name;

	dev->dev_addr[0] = 0x00;
	if (z->id != ZORRO_PROD_AMERISTAR_A2065) {	/* Commodore */
		dev->dev_addr[1] = 0x80;
		dev->dev_addr[2] = 0x10;
	} else {					/* Ameristar */
		dev->dev_addr[1] = 0x00;
		dev->dev_addr[2] = 0x9f;
	}
	dev->dev_addr[3] = (z->rom.er_SerialNumber>>16) & 0xff;
	dev->dev_addr[4] = (z->rom.er_SerialNumber>>8) & 0xff;
	dev->dev_addr[5] = z->rom.er_SerialNumber & 0xff;
	dev->base_addr = ZTWO_VADDR(base_addr);
	dev->mem_start = ZTWO_VADDR(mem_start);
	dev->mem_end = dev->mem_start+A2065_RAM_SIZE;

	priv->ll = (volatile struct lance_regs *)dev->base_addr;
	priv->init_block = (struct lance_init_block *)dev->mem_start;
	priv->lance_init_block = (struct lance_init_block *)A2065_RAM;
	priv->auto_select = 0;
	priv->busmaster_regval = LE_C3_BSWP;

	priv->lance_log_rx_bufs = LANCE_LOG_RX_BUFFERS;
	priv->lance_log_tx_bufs = LANCE_LOG_TX_BUFFERS;
	priv->rx_ring_mod_mask = RX_RING_MOD_MASK;
	priv->tx_ring_mod_mask = TX_RING_MOD_MASK;

	dev->netdev_ops = &lance_netdev_ops;
	dev->watchdog_timeo = 5*HZ;
	dev->dma = 0;

	init_timer(&priv->multicast_timer);
	priv->multicast_timer.data = (unsigned long) dev;
	priv->multicast_timer.function =
		(void (*)(unsigned long)) &lance_set_multicast;

	err = register_netdev(dev);
	if (err) {
		release_resource(r1);
		release_resource(r2);
		free_netdev(dev);
		return err;
	}
	zorro_set_drvdata(z, dev);

	printk(KERN_INFO "%s: A2065 at 0x%08lx, Ethernet Address "
	       "%pM\n", dev->name, board, dev->dev_addr);

	return 0;
}


static void __devexit a2065_remove_one(struct zorro_dev *z)
{
	struct net_device *dev = zorro_get_drvdata(z);

	unregister_netdev(dev);
	release_mem_region(ZTWO_PADDR(dev->base_addr),
			   sizeof(struct lance_regs));
	release_mem_region(ZTWO_PADDR(dev->mem_start), A2065_RAM_SIZE);
	free_netdev(dev);
}

static int __init a2065_init_module(void)
{
	return zorro_register_driver(&a2065_driver);
}

static void __exit a2065_cleanup_module(void)
{
	zorro_unregister_driver(&a2065_driver);
}

module_init(a2065_init_module);
module_exit(a2065_cleanup_module);

MODULE_LICENSE("GPL");
