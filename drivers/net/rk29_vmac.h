/*
 * linux/arch/arc/drivers/arcvmac.h
 *
 * Copyright (C) 2003-2006 Codito Technologies, for linux-2.4 port
 * Copyright (C) 2006-2007 Celunite Inc, for linux-2.6 port
 * Copyright (C) 2007-2008 Sagem Communications, Fehmi HAFSI
 * Copyright (C) 2009 Sagem Communications, Andreas Fenkart
 * All Rights Reserved.
 *
 This program is free software; you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation; either version 2 of the License, or
 (at your option) any later version.

 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with this program; if not, write to the Free Software
 Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

 * Authors: amit.bhor@celunite.com, sameer.dhavale@celunite.com
 */

#ifndef _ARCVMAC_H
#define _ARCVMAC_H

#define VMAC_NAME		"rk29 vmac"
#define VMAC_VERSION		"1.0"

/* Buffer descriptors */
#ifdef CONFIG_ARCH_RK29
#define TX_BDT_LEN		16    /* Number of receive BD's */
#else
#define TX_BDT_LEN		255   /* Number of receive BD's */
#endif
#define RX_BDT_LEN		255   /* Number of transmit BD's */

/* BD poll rate, in 1024 cycles. @100Mhz: x * 1024 cy * 10ns = 1ms */
#define POLLRATE_TIME		200

/* next power of two, bigger than ETH_FRAME_LEN + VLAN  */
#define MAX_RX_BUFFER_LEN	0x800	/* 2^11 = 2048 = 0x800 */
#define MAX_TX_BUFFER_LEN	0x800	/* 2^11 = 2048 = 0x800 */

/* 14 bytes of ethernet header, 4 bytes VLAN, FCS,
 * plus extra pad to prevent buffer chaining of
 * maximum sized ethernet packets (1514 bytes) */
#define	VMAC_BUFFER_PAD		(ETH_HLEN + 4 + ETH_FCS_LEN + 4)

/* VMAC register definitions, offsets in the ref manual are in bytes */
#define ID_OFFSET		(0x00/0x4)
#define STAT_OFFSET		(0x04/0x4)
#define ENABLE_OFFSET		(0x08/0x4)
#define CONTROL_OFFSET		(0x0c/0x4)
#define POLLRATE_OFFSET		(0x10/0x4)
#define RXERR_OFFSET		(0x14/0x4)
#define MISS_OFFSET		(0x18/0x4)
#define TXRINGPTR_OFFSET	(0x1c/0x4)
#define RXRINGPTR_OFFSET	(0x20/0x4)
#define ADDRL_OFFSET		(0x24/0x4)
#define ADDRH_OFFSET		(0x28/0x4)
#define LAFL_OFFSET		(0x2c/0x4)
#define LAFH_OFFSET		(0x30/0x4)
#define MDIO_DATA_OFFSET	(0x34/0x4)
#define MAC_TXRING_HEAD_OFFSET	(0x38/0x4)
#define MAC_RXRING_HEAD_OFFSET	(0x3C/0x4)

/* STATUS and ENABLE register bit masks */
#define TXINT_MASK		(1<<0)	/* Transmit interrupt */
#define RXINT_MASK		(1<<1)	/* Receive interrupt */
#define ERR_MASK		(1<<2)	/* Error interrupt */
#define TXCH_MASK		(1<<3)	/* Transmit chaining error interrupt */
#define MSER_MASK		(1<<4)	/* Missed packet counter error */
#define RXCR_MASK		(1<<8)	/* RXCRCERR counter rolled over	 */
#define RXFR_MASK		(1<<9)	/* RXFRAMEERR counter rolled over */
#define RXFL_MASK		(1<<10)	/* RXOFLOWERR counter rolled over */
#define MDIO_MASK		(1<<12)	/* MDIO complete */
#define TXPL_MASK		(1<<31)	/* TXPOLL */

/* CONTROL register bitmasks */
#define EN_MASK			(1<<0)	/* VMAC enable */
#define TXRN_MASK		(1<<3)	/* TX enable */
#define RXRN_MASK		(1<<4)	/* RX enable */
#define DSBC_MASK		(1<<8)	/* Disable receive broadcast */
#define ENFL_MASK		(1<<10)	/* Enable Full Duplex */            ///////
#define PROM_MASK		(1<<11)	/* Promiscuous mode */

/* RXERR register bitmasks */
#define RXERR_CRC		0x000000ff
#define RXERR_FRM		0x0000ff00
#define RXERR_OFLO		0x00ff0000 /* fifo overflow */

/* MDIO data register bit masks */
#define MDIO_SFD		0xC0000000
#define MDIO_OP			0x30000000
#define MDIO_ID_MASK		0x0F800000
#define MDIO_REG_MASK		0x007C0000
#define MDIO_TA			0x00030000
#define MDIO_DATA_MASK		0x0000FFFF

#define MDIO_BASE		0x40020000
#define MDIO_OP_READ		0x20000000
#define MDIO_OP_WRITE		0x10000000

/* Buffer descriptor INFO bit masks */
#define OWN_MASK		(1<<31)	/* ownership of buffer, 0 CPU, 1 DMA */
#define BUFF			(1<<30) /* buffer invalid, rx */
#define UFLO			(1<<29) /* underflow, tx */
#define LTCL			(1<<28) /* late collision, tx  */
#define RETRY_CT		(0xf<<24)  /* tx */
#define DROP			(1<<23) /* drop, more than 16 retries, tx */
#define DEFER			(1<<22) /* traffic on the wire, tx */
#define CARLOSS			(1<<21) /* carrier loss while transmission, tx, rx? */
/* 20:19 reserved */
#define ADCR			(1<<18) /* add crc, ignored if not disaddcrc */
#define LAST_MASK		(1<<17)	/* Last buffer in chain */
#define FRST_MASK		(1<<16)	/* First buffer in chain */
/* 15:11 reserved */
#define LEN_MASK		0x000007FF

#define ERR_MSK_TX		0x3fe00000 /* UFLO | LTCL | RTRY | DROP | DEFER | CRLS */


/* arcvmac private data structures */
struct vmac_buffer_desc {
	unsigned int info;
	dma_addr_t data;
};

struct dma_fifo {
	int head; /* head */
	int tail; /* tail */
	int size;
};

struct	vmac_priv {
	struct net_device *dev;
	struct platform_device *pdev;
	struct net_device_stats stats;

	spinlock_t lock; /* TODO revisit */
	struct completion mdio_complete;

	/* base address of register set */
	int *regs;
	unsigned int mem_base;

	/* DMA ring buffers */
	struct vmac_buffer_desc *rxbd;
	dma_addr_t rxbd_dma;

	struct vmac_buffer_desc *txbd;
	dma_addr_t txbd_dma;

	/* socket buffers */
	struct sk_buff *rx_skbuff[RX_BDT_LEN];
	struct sk_buff *tx_skbuff[TX_BDT_LEN];
	int rx_skb_size;

	/* skb / dma desc managing */
	struct dma_fifo rx_ring;
	struct dma_fifo tx_ring;

	/* descriptor last polled/processed by the VMAC */
	unsigned long mac_rxring_head;
	/* used when rx skb allocation failed, so we defer rx queue
	 * refill */
	struct timer_list rx_timeout;

	/* lock rx_timeout against rx normal operation */
	spinlock_t rx_lock;

	struct napi_struct napi;

	/* rx buffer chaining */
	int rx_merge_error;
	int tx_timeout_error;

	/* PHY stuff */
	struct mii_bus *mii_bus;
	struct phy_device *phy_dev;

	int link;
	int speed;
	int duplex;

	int open_flag;
	int suspending;
	struct wake_lock resume_lock;

	/* debug */
	int shutdown;
};

/* DMA ring management */

/* for a fifo with size n,
 * - [0..n] fill levels are n + 1 states
 * - there are only n different deltas (head - tail) values
 * => not all fill levels can be represented with head, tail
 *    pointers only
 * we give up the n fill level, aka fifo full */

/* sacrifice one elt as a sentinel */
static inline int fifo_used(struct dma_fifo *f);
static inline int fifo_inc_ct(int ct, int size);
static inline void fifo_dump(struct dma_fifo *fifo);

static inline int fifo_empty(struct dma_fifo *f)
{
	return (f->head == f->tail);
}

static inline int fifo_free(struct dma_fifo *f)
{
	int free;

	free = f->tail - f->head;
	if (free <= 0)
		free += f->size;

	return free;
}

static inline int fifo_used(struct dma_fifo *f)
{
	int used;

	used = f->head - f->tail;
	if (used < 0)
		used += f->size;

	return used;
}

static inline int fifo_full(struct dma_fifo *f)
{
	return (fifo_used(f) + 1) == f->size;
}

/* manipulate */
static inline void fifo_init(struct dma_fifo *fifo, int size)
{
	fifo->size = size;
	fifo->head = fifo->tail = 0; /* empty */
}

static inline void fifo_inc_head(struct dma_fifo *fifo)
{
	BUG_ON(fifo_full(fifo));
	fifo->head = fifo_inc_ct(fifo->head, fifo->size);
}

static inline void fifo_inc_tail(struct dma_fifo *fifo)
{
	BUG_ON(fifo_empty(fifo));
	fifo->tail = fifo_inc_ct(fifo->tail, fifo->size);
}

/* internal funcs */
static inline void fifo_dump(struct dma_fifo *fifo)
{
	printk(KERN_INFO "fifo: head %d, tail %d, size %d\n", fifo->head,
			fifo->tail,
			fifo->size);
}

static inline int fifo_inc_ct(int ct, int size)
{
	return (++ct == size) ? 0 : ct;
}

#endif	  /* _ARCVMAC_H */
