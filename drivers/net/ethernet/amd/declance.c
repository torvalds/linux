/*
 *    Lance ethernet driver for the MIPS processor based
 *      DECstation family
 *
 *
 *      adopted from sunlance.c by Richard van den Berg
 *
 *      Copyright (C) 2002, 2003, 2005, 2006  Maciej W. Rozycki
 *
 *      additional sources:
 *      - PMAD-AA TURBOchannel Ethernet Module Functional Specification,
 *        Revision 1.2
 *
 *      History:
 *
 *      v0.001: The kernel accepts the code and it shows the hardware address.
 *
 *      v0.002: Removed most sparc stuff, left only some module and dma stuff.
 *
 *      v0.003: Enhanced base address calculation from proposals by
 *              Harald Koerfgen and Thomas Riemer.
 *
 *      v0.004: lance-regs is pointing at the right addresses, added prom
 *              check. First start of address mapping and DMA.
 *
 *      v0.005: started to play around with LANCE-DMA. This driver will not
 *              work for non IOASIC lances. HK
 *
 *      v0.006: added pointer arrays to lance_private and setup routine for
 *              them in dec_lance_init. HK
 *
 *      v0.007: Big shit. The LANCE seems to use a different DMA mechanism to
 *              access the init block. This looks like one (short) word at a
 *              time, but the smallest amount the IOASIC can transfer is a
 *              (long) word. So we have a 2-2 padding here. Changed
 *              lance_init_block accordingly. The 16-16 padding for the buffers
 *              seems to be correct. HK
 *
 *      v0.008: mods to make PMAX_LANCE work. 01/09/1999 triemer
 *
 *      v0.009: Module support fixes, multiple interfaces support, various
 *              bits. macro
 *
 *      v0.010: Fixes for the PMAD mapping of the LANCE buffer and for the
 *              PMAX requirement to only use halfword accesses to the
 *              buffer. macro
 *
 *      v0.011: Converted the PMAD to the driver model. macro
 */

#include <linux/crc32.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/if_ether.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/spinlock.h>
#include <linux/stddef.h>
#include <linux/string.h>
#include <linux/tc.h>
#include <linux/types.h>

#include <asm/addrspace.h>

#include <asm/dec/interrupts.h>
#include <asm/dec/ioasic.h>
#include <asm/dec/ioasic_addrs.h>
#include <asm/dec/kn01.h>
#include <asm/dec/machtype.h>
#include <asm/dec/system.h>

static char version[] =
"declance.c: v0.011 by Linux MIPS DECstation task force\n";

MODULE_AUTHOR("Linux MIPS DECstation task force");
MODULE_DESCRIPTION("DEC LANCE (DECstation onboard, PMAD-xx) driver");
MODULE_LICENSE("GPL");

#define __unused __attribute__ ((unused))

/*
 * card types
 */
#define ASIC_LANCE 1
#define PMAD_LANCE 2
#define PMAX_LANCE 3


#define LE_CSR0 0
#define LE_CSR1 1
#define LE_CSR2 2
#define LE_CSR3 3

#define LE_MO_PROM      0x8000	/* Enable promiscuous mode */

#define	LE_C0_ERR	0x8000	/* Error: set if BAB, SQE, MISS or ME is set */
#define	LE_C0_BABL	0x4000	/* BAB:  Babble: tx timeout. */
#define	LE_C0_CERR	0x2000	/* SQE:  Signal quality error */
#define	LE_C0_MISS	0x1000	/* MISS: Missed a packet */
#define	LE_C0_MERR	0x0800	/* ME:   Memory error */
#define	LE_C0_RINT	0x0400	/* Received interrupt */
#define	LE_C0_TINT	0x0200	/* Transmitter Interrupt */
#define	LE_C0_IDON	0x0100	/* IFIN: Init finished. */
#define	LE_C0_INTR	0x0080	/* Interrupt or error */
#define	LE_C0_INEA	0x0040	/* Interrupt enable */
#define	LE_C0_RXON	0x0020	/* Receiver on */
#define	LE_C0_TXON	0x0010	/* Transmitter on */
#define	LE_C0_TDMD	0x0008	/* Transmitter demand */
#define	LE_C0_STOP	0x0004	/* Stop the card */
#define	LE_C0_STRT	0x0002	/* Start the card */
#define	LE_C0_INIT	0x0001	/* Init the card */

#define	LE_C3_BSWP	0x4	/* SWAP */
#define	LE_C3_ACON	0x2	/* ALE Control */
#define	LE_C3_BCON	0x1	/* Byte control */

/* Receive message descriptor 1 */
#define LE_R1_OWN	0x8000	/* Who owns the entry */
#define LE_R1_ERR	0x4000	/* Error: if FRA, OFL, CRC or BUF is set */
#define LE_R1_FRA	0x2000	/* FRA: Frame error */
#define LE_R1_OFL	0x1000	/* OFL: Frame overflow */
#define LE_R1_CRC	0x0800	/* CRC error */
#define LE_R1_BUF	0x0400	/* BUF: Buffer error */
#define LE_R1_SOP	0x0200	/* Start of packet */
#define LE_R1_EOP	0x0100	/* End of packet */
#define LE_R1_POK	0x0300	/* Packet is complete: SOP + EOP */

/* Transmit message descriptor 1 */
#define LE_T1_OWN	0x8000	/* Lance owns the packet */
#define LE_T1_ERR	0x4000	/* Error summary */
#define LE_T1_EMORE	0x1000	/* Error: more than one retry needed */
#define LE_T1_EONE	0x0800	/* Error: one retry needed */
#define LE_T1_EDEF	0x0400	/* Error: deferred */
#define LE_T1_SOP	0x0200	/* Start of packet */
#define LE_T1_EOP	0x0100	/* End of packet */
#define LE_T1_POK	0x0300	/* Packet is complete: SOP + EOP */

#define LE_T3_BUF       0x8000	/* Buffer error */
#define LE_T3_UFL       0x4000	/* Error underflow */
#define LE_T3_LCOL      0x1000	/* Error late collision */
#define LE_T3_CLOS      0x0800	/* Error carrier loss */
#define LE_T3_RTY       0x0400	/* Error retry */
#define LE_T3_TDR       0x03ff	/* Time Domain Reflectometry counter */

/* Define: 2^4 Tx buffers and 2^4 Rx buffers */

#ifndef LANCE_LOG_TX_BUFFERS
#define LANCE_LOG_TX_BUFFERS 4
#define LANCE_LOG_RX_BUFFERS 4
#endif

#define TX_RING_SIZE			(1 << (LANCE_LOG_TX_BUFFERS))
#define TX_RING_MOD_MASK		(TX_RING_SIZE - 1)

#define RX_RING_SIZE			(1 << (LANCE_LOG_RX_BUFFERS))
#define RX_RING_MOD_MASK		(RX_RING_SIZE - 1)

#define PKT_BUF_SZ		1536
#define RX_BUFF_SIZE            PKT_BUF_SZ
#define TX_BUFF_SIZE            PKT_BUF_SZ

#undef TEST_HITS
#define ZERO 0

/*
 * The DS2100/3100 have a linear 64 kB buffer which supports halfword
 * accesses only.  Each halfword of the buffer is word-aligned in the
 * CPU address space.
 *
 * The PMAD-AA has a 128 kB buffer on-board.
 *
 * The IOASIC LANCE devices use a shared memory region.  This region
 * as seen from the CPU is (max) 128 kB long and has to be on an 128 kB
 * boundary.  The LANCE sees this as a 64 kB long continuous memory
 * region.
 *
 * The LANCE's DMA address is used as an index in this buffer and DMA
 * takes place in bursts of eight 16-bit words which are packed into
 * four 32-bit words by the IOASIC.  This leads to a strange padding:
 * 16 bytes of valid data followed by a 16 byte gap :-(.
 */

struct lance_rx_desc {
	unsigned short rmd0;		/* low address of packet */
	unsigned short rmd1;		/* high address of packet
					   and descriptor bits */
	short length;			/* 2s complement (negative!)
					   of buffer length */
	unsigned short mblength;	/* actual number of bytes received */
};

struct lance_tx_desc {
	unsigned short tmd0;		/* low address of packet */
	unsigned short tmd1;		/* high address of packet
					   and descriptor bits */
	short length;			/* 2s complement (negative!)
					   of buffer length */
	unsigned short misc;
};


/* First part of the LANCE initialization block, described in databook. */
struct lance_init_block {
	unsigned short mode;		/* pre-set mode (reg. 15) */

	unsigned short phys_addr[3];	/* physical ethernet address */
	unsigned short filter[4];	/* multicast filter */

	/* Receive and transmit ring base, along with extra bits. */
	unsigned short rx_ptr;		/* receive descriptor addr */
	unsigned short rx_len;		/* receive len and high addr */
	unsigned short tx_ptr;		/* transmit descriptor addr */
	unsigned short tx_len;		/* transmit len and high addr */

	short gap[4];

	/* The buffer descriptors */
	struct lance_rx_desc brx_ring[RX_RING_SIZE];
	struct lance_tx_desc btx_ring[TX_RING_SIZE];
};

#define BUF_OFFSET_CPU sizeof(struct lance_init_block)
#define BUF_OFFSET_LNC sizeof(struct lance_init_block)

#define shift_off(off, type)						\
	(type == ASIC_LANCE || type == PMAX_LANCE ? off << 1 : off)

#define lib_off(rt, type)						\
	shift_off(offsetof(struct lance_init_block, rt), type)

#define lib_ptr(ib, rt, type) 						\
	((volatile u16 *)((u8 *)(ib) + lib_off(rt, type)))

#define rds_off(rt, type)						\
	shift_off(offsetof(struct lance_rx_desc, rt), type)

#define rds_ptr(rd, rt, type) 						\
	((volatile u16 *)((u8 *)(rd) + rds_off(rt, type)))

#define tds_off(rt, type)						\
	shift_off(offsetof(struct lance_tx_desc, rt), type)

#define tds_ptr(td, rt, type) 						\
	((volatile u16 *)((u8 *)(td) + tds_off(rt, type)))

struct lance_private {
	struct net_device *next;
	int type;
	int dma_irq;
	volatile struct lance_regs *ll;

	spinlock_t	lock;

	int rx_new, tx_new;
	int rx_old, tx_old;

	unsigned short busmaster_regval;

	struct timer_list       multicast_timer;

	/* Pointers to the ring buffers as seen from the CPU */
	char *rx_buf_ptr_cpu[RX_RING_SIZE];
	char *tx_buf_ptr_cpu[TX_RING_SIZE];

	/* Pointers to the ring buffers as seen from the LANCE */
	uint rx_buf_ptr_lnc[RX_RING_SIZE];
	uint tx_buf_ptr_lnc[TX_RING_SIZE];
};

#define TX_BUFFS_AVAIL ((lp->tx_old<=lp->tx_new)?\
			lp->tx_old+TX_RING_MOD_MASK-lp->tx_new:\
			lp->tx_old - lp->tx_new-1)

/* The lance control ports are at an absolute address, machine and tc-slot
 * dependent.
 * DECstations do only 32-bit access and the LANCE uses 16 bit addresses,
 * so we have to give the structure an extra member making rap pointing
 * at the right address
 */
struct lance_regs {
	volatile unsigned short rdp;	/* register data port */
	unsigned short pad;
	volatile unsigned short rap;	/* register address port */
};

int dec_lance_debug = 2;

static struct tc_driver dec_lance_tc_driver;
static struct net_device *root_lance_dev;

static inline void writereg(volatile unsigned short *regptr, short value)
{
	*regptr = value;
	iob();
}

/* Load the CSR registers */
static void load_csrs(struct lance_private *lp)
{
	volatile struct lance_regs *ll = lp->ll;
	uint leptr;

	/* The address space as seen from the LANCE
	 * begins at address 0. HK
	 */
	leptr = 0;

	writereg(&ll->rap, LE_CSR1);
	writereg(&ll->rdp, (leptr & 0xFFFF));
	writereg(&ll->rap, LE_CSR2);
	writereg(&ll->rdp, leptr >> 16);
	writereg(&ll->rap, LE_CSR3);
	writereg(&ll->rdp, lp->busmaster_regval);

	/* Point back to csr0 */
	writereg(&ll->rap, LE_CSR0);
}

/*
 * Our specialized copy routines
 *
 */
static void cp_to_buf(const int type, void *to, const void *from, int len)
{
	unsigned short *tp;
	const unsigned short *fp;
	unsigned short clen;
	unsigned char *rtp;
	const unsigned char *rfp;

	if (type == PMAD_LANCE) {
		memcpy(to, from, len);
	} else if (type == PMAX_LANCE) {
		clen = len >> 1;
		tp = to;
		fp = from;

		while (clen--) {
			*tp++ = *fp++;
			tp++;
		}

		clen = len & 1;
		rtp = tp;
		rfp = fp;
		while (clen--) {
			*rtp++ = *rfp++;
		}
	} else {
		/*
		 * copy 16 Byte chunks
		 */
		clen = len >> 4;
		tp = to;
		fp = from;
		while (clen--) {
			*tp++ = *fp++;
			*tp++ = *fp++;
			*tp++ = *fp++;
			*tp++ = *fp++;
			*tp++ = *fp++;
			*tp++ = *fp++;
			*tp++ = *fp++;
			*tp++ = *fp++;
			tp += 8;
		}

		/*
		 * do the rest, if any.
		 */
		clen = len & 15;
		rtp = (unsigned char *) tp;
		rfp = (unsigned char *) fp;
		while (clen--) {
			*rtp++ = *rfp++;
		}
	}

	iob();
}

static void cp_from_buf(const int type, void *to, const void *from, int len)
{
	unsigned short *tp;
	const unsigned short *fp;
	unsigned short clen;
	unsigned char *rtp;
	const unsigned char *rfp;

	if (type == PMAD_LANCE) {
		memcpy(to, from, len);
	} else if (type == PMAX_LANCE) {
		clen = len >> 1;
		tp = to;
		fp = from;
		while (clen--) {
			*tp++ = *fp++;
			fp++;
		}

		clen = len & 1;

		rtp = tp;
		rfp = fp;

		while (clen--) {
			*rtp++ = *rfp++;
		}
	} else {

		/*
		 * copy 16 Byte chunks
		 */
		clen = len >> 4;
		tp = to;
		fp = from;
		while (clen--) {
			*tp++ = *fp++;
			*tp++ = *fp++;
			*tp++ = *fp++;
			*tp++ = *fp++;
			*tp++ = *fp++;
			*tp++ = *fp++;
			*tp++ = *fp++;
			*tp++ = *fp++;
			fp += 8;
		}

		/*
		 * do the rest, if any.
		 */
		clen = len & 15;
		rtp = (unsigned char *) tp;
		rfp = (unsigned char *) fp;
		while (clen--) {
			*rtp++ = *rfp++;
		}


	}

}

/* Setup the Lance Rx and Tx rings */
static void lance_init_ring(struct net_device *dev)
{
	struct lance_private *lp = netdev_priv(dev);
	volatile u16 *ib = (volatile u16 *)dev->mem_start;
	uint leptr;
	int i;

	/* Lock out other processes while setting up hardware */
	netif_stop_queue(dev);
	lp->rx_new = lp->tx_new = 0;
	lp->rx_old = lp->tx_old = 0;

	/* Copy the ethernet address to the lance init block.
	 * XXX bit 0 of the physical address registers has to be zero
	 */
	*lib_ptr(ib, phys_addr[0], lp->type) = (dev->dev_addr[1] << 8) |
				     dev->dev_addr[0];
	*lib_ptr(ib, phys_addr[1], lp->type) = (dev->dev_addr[3] << 8) |
				     dev->dev_addr[2];
	*lib_ptr(ib, phys_addr[2], lp->type) = (dev->dev_addr[5] << 8) |
				     dev->dev_addr[4];
	/* Setup the initialization block */

	/* Setup rx descriptor pointer */
	leptr = offsetof(struct lance_init_block, brx_ring);
	*lib_ptr(ib, rx_len, lp->type) = (LANCE_LOG_RX_BUFFERS << 13) |
					 (leptr >> 16);
	*lib_ptr(ib, rx_ptr, lp->type) = leptr;
	if (ZERO)
		printk("RX ptr: %8.8x(%8.8x)\n",
		       leptr, lib_off(brx_ring, lp->type));

	/* Setup tx descriptor pointer */
	leptr = offsetof(struct lance_init_block, btx_ring);
	*lib_ptr(ib, tx_len, lp->type) = (LANCE_LOG_TX_BUFFERS << 13) |
					 (leptr >> 16);
	*lib_ptr(ib, tx_ptr, lp->type) = leptr;
	if (ZERO)
		printk("TX ptr: %8.8x(%8.8x)\n",
		       leptr, lib_off(btx_ring, lp->type));

	if (ZERO)
		printk("TX rings:\n");

	/* Setup the Tx ring entries */
	for (i = 0; i < TX_RING_SIZE; i++) {
		leptr = lp->tx_buf_ptr_lnc[i];
		*lib_ptr(ib, btx_ring[i].tmd0, lp->type) = leptr;
		*lib_ptr(ib, btx_ring[i].tmd1, lp->type) = (leptr >> 16) &
							   0xff;
		*lib_ptr(ib, btx_ring[i].length, lp->type) = 0xf000;
						/* The ones required by tmd2 */
		*lib_ptr(ib, btx_ring[i].misc, lp->type) = 0;
		if (i < 3 && ZERO)
			printk("%d: 0x%8.8x(0x%8.8x)\n",
			       i, leptr, (uint)lp->tx_buf_ptr_cpu[i]);
	}

	/* Setup the Rx ring entries */
	if (ZERO)
		printk("RX rings:\n");
	for (i = 0; i < RX_RING_SIZE; i++) {
		leptr = lp->rx_buf_ptr_lnc[i];
		*lib_ptr(ib, brx_ring[i].rmd0, lp->type) = leptr;
		*lib_ptr(ib, brx_ring[i].rmd1, lp->type) = ((leptr >> 16) &
							    0xff) |
							   LE_R1_OWN;
		*lib_ptr(ib, brx_ring[i].length, lp->type) = -RX_BUFF_SIZE |
							     0xf000;
		*lib_ptr(ib, brx_ring[i].mblength, lp->type) = 0;
		if (i < 3 && ZERO)
			printk("%d: 0x%8.8x(0x%8.8x)\n",
			       i, leptr, (uint)lp->rx_buf_ptr_cpu[i]);
	}
	iob();
}

static int init_restart_lance(struct lance_private *lp)
{
	volatile struct lance_regs *ll = lp->ll;
	int i;

	writereg(&ll->rap, LE_CSR0);
	writereg(&ll->rdp, LE_C0_INIT);

	/* Wait for the lance to complete initialization */
	for (i = 0; (i < 100) && !(ll->rdp & LE_C0_IDON); i++) {
		udelay(10);
	}
	if ((i == 100) || (ll->rdp & LE_C0_ERR)) {
		printk("LANCE unopened after %d ticks, csr0=%4.4x.\n",
		       i, ll->rdp);
		return -1;
	}
	if ((ll->rdp & LE_C0_ERR)) {
		printk("LANCE unopened after %d ticks, csr0=%4.4x.\n",
		       i, ll->rdp);
		return -1;
	}
	writereg(&ll->rdp, LE_C0_IDON);
	writereg(&ll->rdp, LE_C0_STRT);
	writereg(&ll->rdp, LE_C0_INEA);

	return 0;
}

static int lance_rx(struct net_device *dev)
{
	struct lance_private *lp = netdev_priv(dev);
	volatile u16 *ib = (volatile u16 *)dev->mem_start;
	volatile u16 *rd;
	unsigned short bits;
	int entry, len;
	struct sk_buff *skb;

#ifdef TEST_HITS
	{
		int i;

		printk("[");
		for (i = 0; i < RX_RING_SIZE; i++) {
			if (i == lp->rx_new)
				printk("%s", *lib_ptr(ib, brx_ring[i].rmd1,
						      lp->type) &
					     LE_R1_OWN ? "_" : "X");
			else
				printk("%s", *lib_ptr(ib, brx_ring[i].rmd1,
						      lp->type) &
					     LE_R1_OWN ? "." : "1");
		}
		printk("]");
	}
#endif

	for (rd = lib_ptr(ib, brx_ring[lp->rx_new], lp->type);
	     !((bits = *rds_ptr(rd, rmd1, lp->type)) & LE_R1_OWN);
	     rd = lib_ptr(ib, brx_ring[lp->rx_new], lp->type)) {
		entry = lp->rx_new;

		/* We got an incomplete frame? */
		if ((bits & LE_R1_POK) != LE_R1_POK) {
			dev->stats.rx_over_errors++;
			dev->stats.rx_errors++;
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
			len = (*rds_ptr(rd, mblength, lp->type) & 0xfff) - 4;
			skb = netdev_alloc_skb(dev, len + 2);

			if (skb == 0) {
				dev->stats.rx_dropped++;
				*rds_ptr(rd, mblength, lp->type) = 0;
				*rds_ptr(rd, rmd1, lp->type) =
					((lp->rx_buf_ptr_lnc[entry] >> 16) &
					 0xff) | LE_R1_OWN;
				lp->rx_new = (entry + 1) & RX_RING_MOD_MASK;
				return 0;
			}
			dev->stats.rx_bytes += len;

			skb_reserve(skb, 2);	/* 16 byte align */
			skb_put(skb, len);	/* make room */

			cp_from_buf(lp->type, skb->data,
				    lp->rx_buf_ptr_cpu[entry], len);

			skb->protocol = eth_type_trans(skb, dev);
			netif_rx(skb);
			dev->stats.rx_packets++;
		}

		/* Return the packet to the pool */
		*rds_ptr(rd, mblength, lp->type) = 0;
		*rds_ptr(rd, length, lp->type) = -RX_BUFF_SIZE | 0xf000;
		*rds_ptr(rd, rmd1, lp->type) =
			((lp->rx_buf_ptr_lnc[entry] >> 16) & 0xff) | LE_R1_OWN;
		lp->rx_new = (entry + 1) & RX_RING_MOD_MASK;
	}
	return 0;
}

static void lance_tx(struct net_device *dev)
{
	struct lance_private *lp = netdev_priv(dev);
	volatile u16 *ib = (volatile u16 *)dev->mem_start;
	volatile struct lance_regs *ll = lp->ll;
	volatile u16 *td;
	int i, j;
	int status;

	j = lp->tx_old;

	spin_lock(&lp->lock);

	for (i = j; i != lp->tx_new; i = j) {
		td = lib_ptr(ib, btx_ring[i], lp->type);
		/* If we hit a packet not owned by us, stop */
		if (*tds_ptr(td, tmd1, lp->type) & LE_T1_OWN)
			break;

		if (*tds_ptr(td, tmd1, lp->type) & LE_T1_ERR) {
			status = *tds_ptr(td, misc, lp->type);

			dev->stats.tx_errors++;
			if (status & LE_T3_RTY)
				dev->stats.tx_aborted_errors++;
			if (status & LE_T3_LCOL)
				dev->stats.tx_window_errors++;

			if (status & LE_T3_CLOS) {
				dev->stats.tx_carrier_errors++;
				printk("%s: Carrier Lost\n", dev->name);
				/* Stop the lance */
				writereg(&ll->rap, LE_CSR0);
				writereg(&ll->rdp, LE_C0_STOP);
				lance_init_ring(dev);
				load_csrs(lp);
				init_restart_lance(lp);
				goto out;
			}
			/* Buffer errors and underflows turn off the
			 * transmitter, restart the adapter.
			 */
			if (status & (LE_T3_BUF | LE_T3_UFL)) {
				dev->stats.tx_fifo_errors++;

				printk("%s: Tx: ERR_BUF|ERR_UFL, restarting\n",
				       dev->name);
				/* Stop the lance */
				writereg(&ll->rap, LE_CSR0);
				writereg(&ll->rdp, LE_C0_STOP);
				lance_init_ring(dev);
				load_csrs(lp);
				init_restart_lance(lp);
				goto out;
			}
		} else if ((*tds_ptr(td, tmd1, lp->type) & LE_T1_POK) ==
			   LE_T1_POK) {
			/*
			 * So we don't count the packet more than once.
			 */
			*tds_ptr(td, tmd1, lp->type) &= ~(LE_T1_POK);

			/* One collision before packet was sent. */
			if (*tds_ptr(td, tmd1, lp->type) & LE_T1_EONE)
				dev->stats.collisions++;

			/* More than one collision, be optimistic. */
			if (*tds_ptr(td, tmd1, lp->type) & LE_T1_EMORE)
				dev->stats.collisions += 2;

			dev->stats.tx_packets++;
		}
		j = (j + 1) & TX_RING_MOD_MASK;
	}
	lp->tx_old = j;
out:
	if (netif_queue_stopped(dev) &&
	    TX_BUFFS_AVAIL > 0)
		netif_wake_queue(dev);

	spin_unlock(&lp->lock);
}

static irqreturn_t lance_dma_merr_int(int irq, void *dev_id)
{
	struct net_device *dev = dev_id;

	printk(KERN_ERR "%s: DMA error\n", dev->name);
	return IRQ_HANDLED;
}

static irqreturn_t lance_interrupt(int irq, void *dev_id)
{
	struct net_device *dev = dev_id;
	struct lance_private *lp = netdev_priv(dev);
	volatile struct lance_regs *ll = lp->ll;
	int csr0;

	writereg(&ll->rap, LE_CSR0);
	csr0 = ll->rdp;

	/* Acknowledge all the interrupt sources ASAP */
	writereg(&ll->rdp, csr0 & (LE_C0_INTR | LE_C0_TINT | LE_C0_RINT));

	if ((csr0 & LE_C0_ERR)) {
		/* Clear the error condition */
		writereg(&ll->rdp, LE_C0_BABL | LE_C0_ERR | LE_C0_MISS |
			 LE_C0_CERR | LE_C0_MERR);
	}
	if (csr0 & LE_C0_RINT)
		lance_rx(dev);

	if (csr0 & LE_C0_TINT)
		lance_tx(dev);

	if (csr0 & LE_C0_BABL)
		dev->stats.tx_errors++;

	if (csr0 & LE_C0_MISS)
		dev->stats.rx_errors++;

	if (csr0 & LE_C0_MERR) {
		printk("%s: Memory error, status %04x\n", dev->name, csr0);

		writereg(&ll->rdp, LE_C0_STOP);

		lance_init_ring(dev);
		load_csrs(lp);
		init_restart_lance(lp);
		netif_wake_queue(dev);
	}

	writereg(&ll->rdp, LE_C0_INEA);
	writereg(&ll->rdp, LE_C0_INEA);
	return IRQ_HANDLED;
}

static int lance_open(struct net_device *dev)
{
	volatile u16 *ib = (volatile u16 *)dev->mem_start;
	struct lance_private *lp = netdev_priv(dev);
	volatile struct lance_regs *ll = lp->ll;
	int status = 0;

	/* Stop the Lance */
	writereg(&ll->rap, LE_CSR0);
	writereg(&ll->rdp, LE_C0_STOP);

	/* Set mode and clear multicast filter only at device open,
	 * so that lance_init_ring() called at any error will not
	 * forget multicast filters.
	 *
	 * BTW it is common bug in all lance drivers! --ANK
	 */
	*lib_ptr(ib, mode, lp->type) = 0;
	*lib_ptr(ib, filter[0], lp->type) = 0;
	*lib_ptr(ib, filter[1], lp->type) = 0;
	*lib_ptr(ib, filter[2], lp->type) = 0;
	*lib_ptr(ib, filter[3], lp->type) = 0;

	lance_init_ring(dev);
	load_csrs(lp);

	netif_start_queue(dev);

	/* Associate IRQ with lance_interrupt */
	if (request_irq(dev->irq, lance_interrupt, 0, "lance", dev)) {
		printk("%s: Can't get IRQ %d\n", dev->name, dev->irq);
		return -EAGAIN;
	}
	if (lp->dma_irq >= 0) {
		unsigned long flags;

		if (request_irq(lp->dma_irq, lance_dma_merr_int, 0,
				"lance error", dev)) {
			free_irq(dev->irq, dev);
			printk("%s: Can't get DMA IRQ %d\n", dev->name,
				lp->dma_irq);
			return -EAGAIN;
		}

		spin_lock_irqsave(&ioasic_ssr_lock, flags);

		fast_mb();
		/* Enable I/O ASIC LANCE DMA.  */
		ioasic_write(IO_REG_SSR,
			     ioasic_read(IO_REG_SSR) | IO_SSR_LANCE_DMA_EN);

		fast_mb();
		spin_unlock_irqrestore(&ioasic_ssr_lock, flags);
	}

	status = init_restart_lance(lp);
	return status;
}

static int lance_close(struct net_device *dev)
{
	struct lance_private *lp = netdev_priv(dev);
	volatile struct lance_regs *ll = lp->ll;

	netif_stop_queue(dev);
	del_timer_sync(&lp->multicast_timer);

	/* Stop the card */
	writereg(&ll->rap, LE_CSR0);
	writereg(&ll->rdp, LE_C0_STOP);

	if (lp->dma_irq >= 0) {
		unsigned long flags;

		spin_lock_irqsave(&ioasic_ssr_lock, flags);

		fast_mb();
		/* Disable I/O ASIC LANCE DMA.  */
		ioasic_write(IO_REG_SSR,
			     ioasic_read(IO_REG_SSR) & ~IO_SSR_LANCE_DMA_EN);

		fast_iob();
		spin_unlock_irqrestore(&ioasic_ssr_lock, flags);

		free_irq(lp->dma_irq, dev);
	}
	free_irq(dev->irq, dev);
	return 0;
}

static inline int lance_reset(struct net_device *dev)
{
	struct lance_private *lp = netdev_priv(dev);
	volatile struct lance_regs *ll = lp->ll;
	int status;

	/* Stop the lance */
	writereg(&ll->rap, LE_CSR0);
	writereg(&ll->rdp, LE_C0_STOP);

	lance_init_ring(dev);
	load_csrs(lp);
	dev->trans_start = jiffies; /* prevent tx timeout */
	status = init_restart_lance(lp);
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

static int lance_start_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct lance_private *lp = netdev_priv(dev);
	volatile struct lance_regs *ll = lp->ll;
	volatile u16 *ib = (volatile u16 *)dev->mem_start;
	unsigned long flags;
	int entry, len;

	len = skb->len;

	if (len < ETH_ZLEN) {
		if (skb_padto(skb, ETH_ZLEN))
			return NETDEV_TX_OK;
		len = ETH_ZLEN;
	}

	dev->stats.tx_bytes += len;

	spin_lock_irqsave(&lp->lock, flags);

	entry = lp->tx_new;
	*lib_ptr(ib, btx_ring[entry].length, lp->type) = (-len);
	*lib_ptr(ib, btx_ring[entry].misc, lp->type) = 0;

	cp_to_buf(lp->type, lp->tx_buf_ptr_cpu[entry], skb->data, len);

	/* Now, give the packet to the lance */
	*lib_ptr(ib, btx_ring[entry].tmd1, lp->type) =
		((lp->tx_buf_ptr_lnc[entry] >> 16) & 0xff) |
		(LE_T1_POK | LE_T1_OWN);
	lp->tx_new = (entry + 1) & TX_RING_MOD_MASK;

	if (TX_BUFFS_AVAIL <= 0)
		netif_stop_queue(dev);

	/* Kick the lance: transmit now */
	writereg(&ll->rdp, LE_C0_INEA | LE_C0_TDMD);

	spin_unlock_irqrestore(&lp->lock, flags);

	dev_kfree_skb(skb);

 	return NETDEV_TX_OK;
}

static void lance_load_multicast(struct net_device *dev)
{
	struct lance_private *lp = netdev_priv(dev);
	volatile u16 *ib = (volatile u16 *)dev->mem_start;
	struct netdev_hw_addr *ha;
	u32 crc;

	/* set all multicast bits */
	if (dev->flags & IFF_ALLMULTI) {
		*lib_ptr(ib, filter[0], lp->type) = 0xffff;
		*lib_ptr(ib, filter[1], lp->type) = 0xffff;
		*lib_ptr(ib, filter[2], lp->type) = 0xffff;
		*lib_ptr(ib, filter[3], lp->type) = 0xffff;
		return;
	}
	/* clear the multicast filter */
	*lib_ptr(ib, filter[0], lp->type) = 0;
	*lib_ptr(ib, filter[1], lp->type) = 0;
	*lib_ptr(ib, filter[2], lp->type) = 0;
	*lib_ptr(ib, filter[3], lp->type) = 0;

	/* Add addresses */
	netdev_for_each_mc_addr(ha, dev) {
		crc = ether_crc_le(ETH_ALEN, ha->addr);
		crc = crc >> 26;
		*lib_ptr(ib, filter[crc >> 4], lp->type) |= 1 << (crc & 0xf);
	}
}

static void lance_set_multicast(struct net_device *dev)
{
	struct lance_private *lp = netdev_priv(dev);
	volatile u16 *ib = (volatile u16 *)dev->mem_start;
	volatile struct lance_regs *ll = lp->ll;

	if (!netif_running(dev))
		return;

	if (lp->tx_old != lp->tx_new) {
		mod_timer(&lp->multicast_timer, jiffies + 4 * HZ/100);
		netif_wake_queue(dev);
		return;
	}

	netif_stop_queue(dev);

	writereg(&ll->rap, LE_CSR0);
	writereg(&ll->rdp, LE_C0_STOP);

	lance_init_ring(dev);

	if (dev->flags & IFF_PROMISC) {
		*lib_ptr(ib, mode, lp->type) |= LE_MO_PROM;
	} else {
		*lib_ptr(ib, mode, lp->type) &= ~LE_MO_PROM;
		lance_load_multicast(dev);
	}
	load_csrs(lp);
	init_restart_lance(lp);
	netif_wake_queue(dev);
}

static void lance_set_multicast_retry(unsigned long _opaque)
{
	struct net_device *dev = (struct net_device *) _opaque;

	lance_set_multicast(dev);
}

static const struct net_device_ops lance_netdev_ops = {
	.ndo_open		= lance_open,
	.ndo_stop		= lance_close,
	.ndo_start_xmit		= lance_start_xmit,
	.ndo_tx_timeout		= lance_tx_timeout,
	.ndo_set_rx_mode	= lance_set_multicast,
	.ndo_change_mtu		= eth_change_mtu,
	.ndo_validate_addr	= eth_validate_addr,
	.ndo_set_mac_address	= eth_mac_addr,
};

static int dec_lance_probe(struct device *bdev, const int type)
{
	static unsigned version_printed;
	static const char fmt[] = "declance%d";
	char name[10];
	struct net_device *dev;
	struct lance_private *lp;
	volatile struct lance_regs *ll;
	resource_size_t start = 0, len = 0;
	int i, ret;
	unsigned long esar_base;
	unsigned char *esar;

	if (dec_lance_debug && version_printed++ == 0)
		printk(version);

	if (bdev)
		snprintf(name, sizeof(name), "%s", dev_name(bdev));
	else {
		i = 0;
		dev = root_lance_dev;
		while (dev) {
			i++;
			lp = netdev_priv(dev);
			dev = lp->next;
		}
		snprintf(name, sizeof(name), fmt, i);
	}

	dev = alloc_etherdev(sizeof(struct lance_private));
	if (!dev) {
		ret = -ENOMEM;
		goto err_out;
	}

	/*
	 * alloc_etherdev ensures the data structures used by the LANCE
	 * are aligned.
	 */
	lp = netdev_priv(dev);
	spin_lock_init(&lp->lock);

	lp->type = type;
	switch (type) {
	case ASIC_LANCE:
		dev->base_addr = CKSEG1ADDR(dec_kn_slot_base + IOASIC_LANCE);

		/* buffer space for the on-board LANCE shared memory */
		/*
		 * FIXME: ugly hack!
		 */
		dev->mem_start = CKSEG1ADDR(0x00020000);
		dev->mem_end = dev->mem_start + 0x00020000;
		dev->irq = dec_interrupt[DEC_IRQ_LANCE];
		esar_base = CKSEG1ADDR(dec_kn_slot_base + IOASIC_ESAR);

		/* Workaround crash with booting KN04 2.1k from Disk */
		memset((void *)dev->mem_start, 0,
		       dev->mem_end - dev->mem_start);

		/*
		 * setup the pointer arrays, this sucks [tm] :-(
		 */
		for (i = 0; i < RX_RING_SIZE; i++) {
			lp->rx_buf_ptr_cpu[i] =
				(char *)(dev->mem_start + 2 * BUF_OFFSET_CPU +
					 2 * i * RX_BUFF_SIZE);
			lp->rx_buf_ptr_lnc[i] =
				(BUF_OFFSET_LNC + i * RX_BUFF_SIZE);
		}
		for (i = 0; i < TX_RING_SIZE; i++) {
			lp->tx_buf_ptr_cpu[i] =
				(char *)(dev->mem_start + 2 * BUF_OFFSET_CPU +
					 2 * RX_RING_SIZE * RX_BUFF_SIZE +
					 2 * i * TX_BUFF_SIZE);
			lp->tx_buf_ptr_lnc[i] =
				(BUF_OFFSET_LNC +
				 RX_RING_SIZE * RX_BUFF_SIZE +
				 i * TX_BUFF_SIZE);
		}

		/* Setup I/O ASIC LANCE DMA.  */
		lp->dma_irq = dec_interrupt[DEC_IRQ_LANCE_MERR];
		ioasic_write(IO_REG_LANCE_DMA_P,
			     CPHYSADDR(dev->mem_start) << 3);

		break;
#ifdef CONFIG_TC
	case PMAD_LANCE:
		dev_set_drvdata(bdev, dev);

		start = to_tc_dev(bdev)->resource.start;
		len = to_tc_dev(bdev)->resource.end - start + 1;
		if (!request_mem_region(start, len, dev_name(bdev))) {
			printk(KERN_ERR
			       "%s: Unable to reserve MMIO resource\n",
			       dev_name(bdev));
			ret = -EBUSY;
			goto err_out_dev;
		}

		dev->mem_start = CKSEG1ADDR(start);
		dev->mem_end = dev->mem_start + 0x100000;
		dev->base_addr = dev->mem_start + 0x100000;
		dev->irq = to_tc_dev(bdev)->interrupt;
		esar_base = dev->mem_start + 0x1c0002;
		lp->dma_irq = -1;

		for (i = 0; i < RX_RING_SIZE; i++) {
			lp->rx_buf_ptr_cpu[i] =
				(char *)(dev->mem_start + BUF_OFFSET_CPU +
					 i * RX_BUFF_SIZE);
			lp->rx_buf_ptr_lnc[i] =
				(BUF_OFFSET_LNC + i * RX_BUFF_SIZE);
		}
		for (i = 0; i < TX_RING_SIZE; i++) {
			lp->tx_buf_ptr_cpu[i] =
				(char *)(dev->mem_start + BUF_OFFSET_CPU +
					 RX_RING_SIZE * RX_BUFF_SIZE +
					 i * TX_BUFF_SIZE);
			lp->tx_buf_ptr_lnc[i] =
				(BUF_OFFSET_LNC +
				 RX_RING_SIZE * RX_BUFF_SIZE +
				 i * TX_BUFF_SIZE);
		}

		break;
#endif
	case PMAX_LANCE:
		dev->irq = dec_interrupt[DEC_IRQ_LANCE];
		dev->base_addr = CKSEG1ADDR(KN01_SLOT_BASE + KN01_LANCE);
		dev->mem_start = CKSEG1ADDR(KN01_SLOT_BASE + KN01_LANCE_MEM);
		dev->mem_end = dev->mem_start + KN01_SLOT_SIZE;
		esar_base = CKSEG1ADDR(KN01_SLOT_BASE + KN01_ESAR + 1);
		lp->dma_irq = -1;

		/*
		 * setup the pointer arrays, this sucks [tm] :-(
		 */
		for (i = 0; i < RX_RING_SIZE; i++) {
			lp->rx_buf_ptr_cpu[i] =
				(char *)(dev->mem_start + 2 * BUF_OFFSET_CPU +
					 2 * i * RX_BUFF_SIZE);
			lp->rx_buf_ptr_lnc[i] =
				(BUF_OFFSET_LNC + i * RX_BUFF_SIZE);
		}
		for (i = 0; i < TX_RING_SIZE; i++) {
			lp->tx_buf_ptr_cpu[i] =
				(char *)(dev->mem_start + 2 * BUF_OFFSET_CPU +
					 2 * RX_RING_SIZE * RX_BUFF_SIZE +
					 2 * i * TX_BUFF_SIZE);
			lp->tx_buf_ptr_lnc[i] =
				(BUF_OFFSET_LNC +
				 RX_RING_SIZE * RX_BUFF_SIZE +
				 i * TX_BUFF_SIZE);
		}

		break;

	default:
		printk(KERN_ERR "%s: declance_init called with unknown type\n",
			name);
		ret = -ENODEV;
		goto err_out_dev;
	}

	ll = (struct lance_regs *) dev->base_addr;
	esar = (unsigned char *) esar_base;

	/* prom checks */
	/* First, check for test pattern */
	if (esar[0x60] != 0xff && esar[0x64] != 0x00 &&
	    esar[0x68] != 0x55 && esar[0x6c] != 0xaa) {
		printk(KERN_ERR
			"%s: Ethernet station address prom not found!\n",
			name);
		ret = -ENODEV;
		goto err_out_resource;
	}
	/* Check the prom contents */
	for (i = 0; i < 8; i++) {
		if (esar[i * 4] != esar[0x3c - i * 4] &&
		    esar[i * 4] != esar[0x40 + i * 4] &&
		    esar[0x3c - i * 4] != esar[0x40 + i * 4]) {
			printk(KERN_ERR "%s: Something is wrong with the "
				"ethernet station address prom!\n", name);
			ret = -ENODEV;
			goto err_out_resource;
		}
	}

	/* Copy the ethernet address to the device structure, later to the
	 * lance initialization block so the lance gets it every time it's
	 * (re)initialized.
	 */
	switch (type) {
	case ASIC_LANCE:
		printk("%s: IOASIC onboard LANCE", name);
		break;
	case PMAD_LANCE:
		printk("%s: PMAD-AA", name);
		break;
	case PMAX_LANCE:
		printk("%s: PMAX onboard LANCE", name);
		break;
	}
	for (i = 0; i < 6; i++)
		dev->dev_addr[i] = esar[i * 4];

	printk(", addr = %pM, irq = %d\n", dev->dev_addr, dev->irq);

	dev->netdev_ops = &lance_netdev_ops;
	dev->watchdog_timeo = 5*HZ;

	/* lp->ll is the location of the registers for lance card */
	lp->ll = ll;

	/* busmaster_regval (CSR3) should be zero according to the PMAD-AA
	 * specification.
	 */
	lp->busmaster_regval = 0;

	dev->dma = 0;

	/* We cannot sleep if the chip is busy during a
	 * multicast list update event, because such events
	 * can occur from interrupts (ex. IPv6).  So we
	 * use a timer to try again later when necessary. -DaveM
	 */
	init_timer(&lp->multicast_timer);
	lp->multicast_timer.data = (unsigned long) dev;
	lp->multicast_timer.function = lance_set_multicast_retry;

	ret = register_netdev(dev);
	if (ret) {
		printk(KERN_ERR
			"%s: Unable to register netdev, aborting.\n", name);
		goto err_out_resource;
	}

	if (!bdev) {
		lp->next = root_lance_dev;
		root_lance_dev = dev;
	}

	printk("%s: registered as %s.\n", name, dev->name);
	return 0;

err_out_resource:
	if (bdev)
		release_mem_region(start, len);

err_out_dev:
	free_netdev(dev);

err_out:
	return ret;
}

static void __exit dec_lance_remove(struct device *bdev)
{
	struct net_device *dev = dev_get_drvdata(bdev);
	resource_size_t start, len;

	unregister_netdev(dev);
	start = to_tc_dev(bdev)->resource.start;
	len = to_tc_dev(bdev)->resource.end - start + 1;
	release_mem_region(start, len);
	free_netdev(dev);
}

/* Find all the lance cards on the system and initialize them */
static int __init dec_lance_platform_probe(void)
{
	int count = 0;

	if (dec_interrupt[DEC_IRQ_LANCE] >= 0) {
		if (dec_interrupt[DEC_IRQ_LANCE_MERR] >= 0) {
			if (dec_lance_probe(NULL, ASIC_LANCE) >= 0)
				count++;
		} else if (!TURBOCHANNEL) {
			if (dec_lance_probe(NULL, PMAX_LANCE) >= 0)
				count++;
		}
	}

	return (count > 0) ? 0 : -ENODEV;
}

static void __exit dec_lance_platform_remove(void)
{
	while (root_lance_dev) {
		struct net_device *dev = root_lance_dev;
		struct lance_private *lp = netdev_priv(dev);

		unregister_netdev(dev);
		root_lance_dev = lp->next;
		free_netdev(dev);
	}
}

#ifdef CONFIG_TC
static int dec_lance_tc_probe(struct device *dev);
static int __exit dec_lance_tc_remove(struct device *dev);

static const struct tc_device_id dec_lance_tc_table[] = {
	{ "DEC     ", "PMAD-AA " },
	{ }
};
MODULE_DEVICE_TABLE(tc, dec_lance_tc_table);

static struct tc_driver dec_lance_tc_driver = {
	.id_table	= dec_lance_tc_table,
	.driver		= {
		.name	= "declance",
		.bus	= &tc_bus_type,
		.probe	= dec_lance_tc_probe,
		.remove	= __exit_p(dec_lance_tc_remove),
	},
};

static int dec_lance_tc_probe(struct device *dev)
{
        int status = dec_lance_probe(dev, PMAD_LANCE);
        if (!status)
                get_device(dev);
        return status;
}

static int __exit dec_lance_tc_remove(struct device *dev)
{
        put_device(dev);
        dec_lance_remove(dev);
        return 0;
}
#endif

static int __init dec_lance_init(void)
{
	int status;

	status = tc_register_driver(&dec_lance_tc_driver);
	if (!status)
		dec_lance_platform_probe();
	return status;
}

static void __exit dec_lance_exit(void)
{
	dec_lance_platform_remove();
	tc_unregister_driver(&dec_lance_tc_driver);
}


module_init(dec_lance_init);
module_exit(dec_lance_exit);
