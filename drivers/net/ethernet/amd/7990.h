/* SPDX-License-Identifier: GPL-2.0 */
/*
 * 7990.h -- LANCE ethernet IC generic routines.
 * This is an attempt to separate out the bits of various ethernet
 * drivers that are common because they all use the AMD 7990 LANCE
 * (Local Area Network Controller for Ethernet) chip.
 *
 * Copyright (C) 05/1998 Peter Maydell <pmaydell@chiark.greenend.org.uk>
 *
 * Most of this stuff was obtained by looking at other LANCE drivers,
 * in particular a2065.[ch]. The AMD C-LANCE datasheet was also helpful.
 */

#ifndef _7990_H
#define _7990_H

/* The lance only has two register locations. We communicate mostly via memory. */
#define LANCE_RDP	0	/* Register Data Port */
#define LANCE_RAP	2	/* Register Address Port */

/* Transmit/receive ring definitions.
 * We allow the specific drivers to override these defaults if they want to.
 * NB: according to lance.c, increasing the number of buffers is a waste
 * of space and reduces the chance that an upper layer will be able to
 * reorder queued Tx packets based on priority. [Clearly there is a minimum
 * limit too: too small and we drop rx packets and can't tx at full speed.]
 * 4+4 seems to be the usual setting; the atarilance driver uses 3 and 5.
 */

/* Blast! This won't work. The problem is that we can't specify a default
 * setting because that would cause the lance_init_block struct to be
 * too long (and overflow the RAM on shared-memory cards like the HP LANCE.
 */
#ifndef LANCE_LOG_TX_BUFFERS
#define LANCE_LOG_TX_BUFFERS 1
#define LANCE_LOG_RX_BUFFERS 3
#endif

#define TX_RING_SIZE		(1 << LANCE_LOG_TX_BUFFERS)
#define RX_RING_SIZE		(1 << LANCE_LOG_RX_BUFFERS)
#define TX_RING_MOD_MASK	(TX_RING_SIZE - 1)
#define RX_RING_MOD_MASK	(RX_RING_SIZE - 1)
#define TX_RING_LEN_BITS	((LANCE_LOG_TX_BUFFERS) << 29)
#define RX_RING_LEN_BITS	((LANCE_LOG_RX_BUFFERS) << 29)
#define PKT_BUFF_SIZE		(1544)
#define RX_BUFF_SIZE		PKT_BUFF_SIZE
#define TX_BUFF_SIZE		PKT_BUFF_SIZE

/* Each receive buffer is described by a receive message descriptor (RMD) */
struct lance_rx_desc {
	volatile unsigned short rmd0;	    /* low address of packet */
	volatile unsigned char  rmd1_bits;  /* descriptor bits */
	volatile unsigned char  rmd1_hadr;  /* high address of packet */
	volatile short    length;	    /* This length is 2s complement (negative)!
					     * Buffer length */
	volatile unsigned short mblength;   /* Actual number of bytes received */
};

/* Ditto for TMD: */
struct lance_tx_desc {
	volatile unsigned short tmd0;	    /* low address of packet */
	volatile unsigned char  tmd1_bits;  /* descriptor bits */
	volatile unsigned char  tmd1_hadr;  /* high address of packet */
	volatile short    length;	    /* Length is 2s complement (negative)! */
	volatile unsigned short misc;
};

/* There are three memory structures accessed by the LANCE:
 * the initialization block, the receive and transmit descriptor rings,
 * and the data buffers themselves. In fact we might as well put the
 * init block,the Tx and Rx rings and the buffers together in memory:
 */
struct lance_init_block {
	volatile unsigned short mode;		/* Pre-set mode (reg. 15) */
	volatile unsigned char phys_addr[6];	/* Physical ethernet address */
	volatile unsigned filter[2];		/* Multicast filter (64 bits) */

	/* Receive and transmit ring base, along with extra bits. */
	volatile unsigned short rx_ptr;		/* receive descriptor addr */
	volatile unsigned short rx_len;		/* receive len and high addr */
	volatile unsigned short tx_ptr;		/* transmit descriptor addr */
	volatile unsigned short tx_len;		/* transmit len and high addr */

	/* The Tx and Rx ring entries must be aligned on 8-byte boundaries.
	 * This will be true if this whole struct is 8-byte aligned.
	 */
	volatile struct lance_tx_desc btx_ring[TX_RING_SIZE];
	volatile struct lance_rx_desc brx_ring[RX_RING_SIZE];

	volatile char tx_buf[TX_RING_SIZE][TX_BUFF_SIZE];
	volatile char rx_buf[RX_RING_SIZE][RX_BUFF_SIZE];
	/* we use this just to make the struct big enough that we can move its startaddr
	 * in order to force alignment to an eight byte boundary.
	 */
};

/* This is where we keep all the stuff the driver needs to know about.
 * I'm definitely unhappy about the mechanism for allowing specific
 * drivers to add things...
 */
struct lance_private {
	const char *name;
	unsigned long base;
	volatile struct lance_init_block *init_block; /* CPU address of RAM */
	volatile struct lance_init_block *lance_init_block; /* LANCE address of RAM */

	int rx_new, tx_new;
	int rx_old, tx_old;

	int lance_log_rx_bufs, lance_log_tx_bufs;
	int rx_ring_mod_mask, tx_ring_mod_mask;

	int tpe;			/* TPE is selected */
	int auto_select;		/* cable-selection is by carrier */
	unsigned short busmaster_regval;

	unsigned int irq;		/* IRQ to register */

	/* This is because the HP LANCE is disgusting and you have to check
	 * a DIO-specific register every time you read/write the LANCE regs :-<
	 * [could we get away with making these some sort of macro?]
	 */
	void (*writerap)(void *, unsigned short);
	void (*writerdp)(void *, unsigned short);
	unsigned short (*readrdp)(void *);
	spinlock_t devlock;
	char tx_full;
};

/*
 *		Am7990 Control and Status Registers
 */
#define LE_CSR0		0x0000	/* LANCE Controller Status */
#define LE_CSR1		0x0001	/* IADR[15:0] (bit0==0 ie word aligned) */
#define LE_CSR2		0x0002	/* IADR[23:16] (high bits reserved) */
#define LE_CSR3		0x0003	/* Misc */

/*
 *		Bit definitions for CSR0 (LANCE Controller Status)
 */
#define LE_C0_ERR	0x8000	/* Error = BABL | CERR | MISS | MERR */
#define LE_C0_BABL	0x4000	/* Babble: Transmitted too many bits */
#define LE_C0_CERR	0x2000	/* No Heartbeat (10BASE-T) */
#define LE_C0_MISS	0x1000	/* Missed Frame (no rx buffer to put it in) */
#define LE_C0_MERR	0x0800	/* Memory Error */
#define LE_C0_RINT	0x0400	/* Receive Interrupt */
#define LE_C0_TINT	0x0200	/* Transmit Interrupt */
#define LE_C0_IDON	0x0100	/* Initialization Done */
#define LE_C0_INTR	0x0080	/* Interrupt Flag
				   = BABL | MISS | MERR | RINT | TINT | IDON */
#define LE_C0_INEA	0x0040	/* Interrupt Enable */
#define LE_C0_RXON	0x0020	/* Receive On */
#define LE_C0_TXON	0x0010	/* Transmit On */
#define LE_C0_TDMD	0x0008	/* Transmit Demand */
#define LE_C0_STOP	0x0004	/* Stop */
#define LE_C0_STRT	0x0002	/* Start */
#define LE_C0_INIT	0x0001	/* Initialize */


/*
 *		Bit definitions for CSR3
 */
#define LE_C3_BSWP	0x0004	/* Byte Swap (on for big endian byte order) */
#define LE_C3_ACON	0x0002	/* ALE Control (on for active low ALE) */
#define LE_C3_BCON	0x0001	/* Byte Control */


/*
 *		Mode Flags
 */
#define LE_MO_PROM	0x8000	/* Promiscuous Mode */
/* these next ones 0x4000 -- 0x0080 are not available on the LANCE 7990,
 * but they are in NetBSD's am7990.h, presumably for backwards-compatible chips
 */
#define LE_MO_DRCVBC	0x4000	/* disable receive broadcast */
#define LE_MO_DRCVPA	0x2000	/* disable physical address detection */
#define LE_MO_DLNKTST	0x1000	/* disable link status */
#define LE_MO_DAPC	0x0800	/* disable automatic polarity correction */
#define LE_MO_MENDECL	0x0400	/* MENDEC loopback mode */
#define LE_MO_LRTTSEL	0x0200	/* lower RX threshold / TX mode selection */
#define LE_MO_PSEL1	0x0100	/* port selection bit1 */
#define LE_MO_PSEL0	0x0080	/* port selection bit0 */
/* and this one is from the C-LANCE data sheet... */
#define LE_MO_EMBA	0x0080	/* Enable Modified Backoff Algorithm
				   (C-LANCE, not original LANCE) */
#define LE_MO_INTL	0x0040	/* Internal Loopback */
#define LE_MO_DRTY	0x0020	/* Disable Retry */
#define LE_MO_FCOLL	0x0010	/* Force Collision */
#define LE_MO_DXMTFCS	0x0008	/* Disable Transmit CRC */
#define LE_MO_LOOP	0x0004	/* Loopback Enable */
#define LE_MO_DTX	0x0002	/* Disable Transmitter */
#define LE_MO_DRX	0x0001	/* Disable Receiver */


/*
 *		Receive Flags
 */
#define LE_R1_OWN	0x80	/* LANCE owns the descriptor */
#define LE_R1_ERR	0x40	/* Error */
#define LE_R1_FRA	0x20	/* Framing Error */
#define LE_R1_OFL	0x10	/* Overflow Error */
#define LE_R1_CRC	0x08	/* CRC Error */
#define LE_R1_BUF	0x04	/* Buffer Error */
#define LE_R1_SOP	0x02	/* Start of Packet */
#define LE_R1_EOP	0x01	/* End of Packet */
#define LE_R1_POK	0x03	/* Packet is complete: SOP + EOP */


/*
 *		Transmit Flags
 */
#define LE_T1_OWN	0x80	/* LANCE owns the descriptor */
#define LE_T1_ERR	0x40	/* Error */
#define LE_T1_RES	0x20	/* Reserved, LANCE writes this with a zero */
#define LE_T1_EMORE	0x10	/* More than one retry needed */
#define LE_T1_EONE	0x08	/* One retry needed */
#define LE_T1_EDEF	0x04	/* Deferred */
#define LE_T1_SOP	0x02	/* Start of Packet */
#define LE_T1_EOP	0x01	/* End of Packet */
#define LE_T1_POK	0x03	/* Packet is complete: SOP + EOP */

/*
 *		Error Flags
 */
#define LE_T3_BUF	0x8000	/* Buffer Error */
#define LE_T3_UFL	0x4000	/* Underflow Error */
#define LE_T3_LCOL	0x1000	/* Late Collision */
#define LE_T3_CLOS	0x0800	/* Loss of Carrier */
#define LE_T3_RTY	0x0400	/* Retry Error */
#define LE_T3_TDR	0x03ff	/* Time Domain Reflectometry */

/* Miscellaneous useful macros */

#define TX_BUFFS_AVAIL ((lp->tx_old <= lp->tx_new) ? \
			lp->tx_old + lp->tx_ring_mod_mask - lp->tx_new : \
			lp->tx_old - lp->tx_new - 1)

/* The LANCE only uses 24 bit addresses. This does the obvious thing. */
#define LANCE_ADDR(x) ((int)(x) & ~0xff000000)

/* Now the prototypes we export */
int lance_open(struct net_device *dev);
int lance_close(struct net_device *dev);
int lance_start_xmit(struct sk_buff *skb, struct net_device *dev);
void lance_set_multicast(struct net_device *dev);
void lance_tx_timeout(struct net_device *dev, unsigned int txqueue);
#ifdef CONFIG_NET_POLL_CONTROLLER
void lance_poll(struct net_device *dev);
#endif

#endif /* ndef _7990_H */
