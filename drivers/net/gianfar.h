/* 
 * drivers/net/gianfar.h
 *
 * Gianfar Ethernet Driver
 * Driver for FEC on MPC8540 and TSEC on MPC8540/MPC8560
 * Based on 8260_io/fcc_enet.c
 *
 * Author: Andy Fleming
 * Maintainer: Kumar Gala (kumar.gala@freescale.com)
 *
 * Copyright (c) 2002-2004 Freescale Semiconductor, Inc.
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 *
 *  Still left to do:
 *      -Add support for module parameters
 *	-Add support for ethtool -s
 *	-Add patch for ethtool phys id
 */
#ifndef __GIANFAR_H
#define __GIANFAR_H

#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include <linux/spinlock.h>
#include <linux/mm.h>
#include <linux/fsl_devices.h>

#include <asm/io.h>
#include <asm/irq.h>
#include <asm/uaccess.h>
#include <linux/module.h>
#include <linux/version.h>
#include <linux/crc32.h>
#include <linux/workqueue.h>
#include <linux/ethtool.h>
#include <linux/netdevice.h>
#include "gianfar_phy.h"

/* The maximum number of packets to be handled in one call of gfar_poll */
#define GFAR_DEV_WEIGHT 64

/* Number of bytes to align the rx bufs to */
#define RXBUF_ALIGNMENT 64

/* The number of bytes which composes a unit for the purpose of
 * allocating data buffers.  ie-for any given MTU, the data buffer
 * will be the next highest multiple of 512 bytes. */
#define INCREMENTAL_BUFFER_SIZE 512


#define MAC_ADDR_LEN 6

#define PHY_INIT_TIMEOUT 100000
#define GFAR_PHY_CHANGE_TIME 2

#define DEVICE_NAME "%s: Gianfar Ethernet Controller Version 1.1, "
#define DRV_NAME "gfar-enet"
extern const char gfar_driver_name[];
extern const char gfar_driver_version[];

/* These need to be powers of 2 for this driver */
#ifdef CONFIG_GFAR_NAPI
#define DEFAULT_TX_RING_SIZE	256
#define DEFAULT_RX_RING_SIZE	256
#else
#define DEFAULT_TX_RING_SIZE    64
#define DEFAULT_RX_RING_SIZE    64
#endif

#define GFAR_RX_MAX_RING_SIZE   256
#define GFAR_TX_MAX_RING_SIZE   256

#define DEFAULT_RX_BUFFER_SIZE  1536
#define TX_RING_MOD_MASK(size) (size-1)
#define RX_RING_MOD_MASK(size) (size-1)
#define JUMBO_BUFFER_SIZE 9728
#define JUMBO_FRAME_SIZE 9600

/* Latency of interface clock in nanoseconds */
/* Interface clock latency , in this case, means the 
 * time described by a value of 1 in the interrupt
 * coalescing registers' time fields.  Since those fields
 * refer to the time it takes for 64 clocks to pass, the
 * latencies are as such:
 * GBIT = 125MHz => 8ns/clock => 8*64 ns / tick
 * 100 = 25 MHz => 40ns/clock => 40*64 ns / tick
 * 10 = 2.5 MHz => 400ns/clock => 400*64 ns / tick
 */
#define GFAR_GBIT_TIME  512
#define GFAR_100_TIME   2560
#define GFAR_10_TIME    25600

#define DEFAULT_TX_COALESCE 1
#define DEFAULT_TXCOUNT	16
#define DEFAULT_TXTIME	400

#define DEFAULT_RX_COALESCE 1
#define DEFAULT_RXCOUNT	16
#define DEFAULT_RXTIME	400

#define TBIPA_VALUE		0x1f
#define MIIMCFG_INIT_VALUE	0x00000007
#define MIIMCFG_RESET           0x80000000
#define MIIMIND_BUSY            0x00000001

/* MAC register bits */
#define MACCFG1_SOFT_RESET	0x80000000
#define MACCFG1_RESET_RX_MC	0x00080000
#define MACCFG1_RESET_TX_MC	0x00040000
#define MACCFG1_RESET_RX_FUN	0x00020000
#define	MACCFG1_RESET_TX_FUN	0x00010000
#define MACCFG1_LOOPBACK	0x00000100
#define MACCFG1_RX_FLOW		0x00000020
#define MACCFG1_TX_FLOW		0x00000010
#define MACCFG1_SYNCD_RX_EN	0x00000008
#define MACCFG1_RX_EN		0x00000004
#define MACCFG1_SYNCD_TX_EN	0x00000002
#define MACCFG1_TX_EN		0x00000001

#define MACCFG2_INIT_SETTINGS	0x00007205
#define MACCFG2_FULL_DUPLEX	0x00000001
#define MACCFG2_IF              0x00000300
#define MACCFG2_MII             0x00000100
#define MACCFG2_GMII            0x00000200
#define MACCFG2_HUGEFRAME	0x00000020
#define MACCFG2_LENGTHCHECK	0x00000010

#define ECNTRL_INIT_SETTINGS	0x00001000
#define ECNTRL_TBI_MODE         0x00000020

#define MRBLR_INIT_SETTINGS	DEFAULT_RX_BUFFER_SIZE

#define MINFLR_INIT_SETTINGS	0x00000040

/* Init to do tx snooping for buffers and descriptors */
#define DMACTRL_INIT_SETTINGS   0x000000c3
#define DMACTRL_GRS             0x00000010
#define DMACTRL_GTS             0x00000008

#define TSTAT_CLEAR_THALT       0x80000000

/* Interrupt coalescing macros */
#define IC_ICEN			0x80000000
#define IC_ICFT_MASK		0x1fe00000
#define IC_ICFT_SHIFT		21
#define mk_ic_icft(x)		\
	(((unsigned int)x << IC_ICFT_SHIFT)&IC_ICFT_MASK)
#define IC_ICTT_MASK		0x0000ffff
#define mk_ic_ictt(x)		(x&IC_ICTT_MASK)

#define mk_ic_value(count, time) (IC_ICEN | \
				mk_ic_icft(count) | \
				mk_ic_ictt(time))

#define RCTRL_PROM		0x00000008
#define RSTAT_CLEAR_RHALT       0x00800000

#define IEVENT_INIT_CLEAR	0xffffffff
#define IEVENT_BABR		0x80000000
#define IEVENT_RXC		0x40000000
#define IEVENT_BSY		0x20000000
#define IEVENT_EBERR		0x10000000
#define IEVENT_MSRO		0x04000000
#define IEVENT_GTSC		0x02000000
#define IEVENT_BABT		0x01000000
#define IEVENT_TXC		0x00800000
#define IEVENT_TXE		0x00400000
#define IEVENT_TXB		0x00200000
#define IEVENT_TXF		0x00100000
#define IEVENT_LC		0x00040000
#define IEVENT_CRL		0x00020000
#define IEVENT_XFUN		0x00010000
#define IEVENT_RXB0		0x00008000
#define IEVENT_GRSC		0x00000100
#define IEVENT_RXF0		0x00000080
#define IEVENT_RX_MASK          (IEVENT_RXB0 | IEVENT_RXF0)
#define IEVENT_TX_MASK          (IEVENT_TXB | IEVENT_TXF)
#define IEVENT_ERR_MASK         \
(IEVENT_RXC | IEVENT_BSY | IEVENT_EBERR | IEVENT_MSRO | \
 IEVENT_BABT | IEVENT_TXC | IEVENT_TXE | IEVENT_LC \
 | IEVENT_CRL | IEVENT_XFUN)

#define IMASK_INIT_CLEAR	0x00000000
#define IMASK_BABR              0x80000000
#define IMASK_RXC               0x40000000
#define IMASK_BSY               0x20000000
#define IMASK_EBERR             0x10000000
#define IMASK_MSRO		0x04000000
#define IMASK_GRSC              0x02000000
#define IMASK_BABT		0x01000000
#define IMASK_TXC               0x00800000
#define IMASK_TXEEN		0x00400000
#define IMASK_TXBEN		0x00200000
#define IMASK_TXFEN             0x00100000
#define IMASK_LC		0x00040000
#define IMASK_CRL		0x00020000
#define IMASK_XFUN		0x00010000
#define IMASK_RXB0              0x00008000
#define IMASK_GTSC              0x00000100
#define IMASK_RXFEN0		0x00000080
#define IMASK_RX_DISABLED ~(IMASK_RXFEN0 | IMASK_BSY)
#define IMASK_DEFAULT  (IMASK_TXEEN | IMASK_TXFEN | IMASK_TXBEN | \
		IMASK_RXFEN0 | IMASK_BSY | IMASK_EBERR | IMASK_BABR | \
		IMASK_XFUN | IMASK_RXC | IMASK_BABT)


/* Attribute fields */

/* This enables rx snooping for buffers and descriptors */
#ifdef CONFIG_GFAR_BDSTASH
#define ATTR_BDSTASH		0x00000800
#else
#define ATTR_BDSTASH		0x00000000
#endif

#ifdef CONFIG_GFAR_BUFSTASH
#define ATTR_BUFSTASH		0x00004000
#define STASH_LENGTH		64
#else
#define ATTR_BUFSTASH		0x00000000
#endif

#define ATTR_SNOOPING		0x000000c0
#define ATTR_INIT_SETTINGS      (ATTR_SNOOPING \
		| ATTR_BDSTASH | ATTR_BUFSTASH)

#define ATTRELI_INIT_SETTINGS   0x0


/* TxBD status field bits */
#define TXBD_READY		0x8000
#define TXBD_PADCRC		0x4000
#define TXBD_WRAP		0x2000
#define TXBD_INTERRUPT		0x1000
#define TXBD_LAST		0x0800
#define TXBD_CRC		0x0400
#define TXBD_DEF		0x0200
#define TXBD_HUGEFRAME		0x0080
#define TXBD_LATECOLLISION	0x0080
#define TXBD_RETRYLIMIT		0x0040
#define	TXBD_RETRYCOUNTMASK	0x003c
#define TXBD_UNDERRUN		0x0002

/* RxBD status field bits */
#define RXBD_EMPTY		0x8000
#define RXBD_RO1		0x4000
#define RXBD_WRAP		0x2000
#define RXBD_INTERRUPT		0x1000
#define RXBD_LAST		0x0800
#define RXBD_FIRST		0x0400
#define RXBD_MISS		0x0100
#define RXBD_BROADCAST		0x0080
#define RXBD_MULTICAST		0x0040
#define RXBD_LARGE		0x0020
#define RXBD_NONOCTET		0x0010
#define RXBD_SHORT		0x0008
#define RXBD_CRCERR		0x0004
#define RXBD_OVERRUN		0x0002
#define RXBD_TRUNCATED		0x0001
#define RXBD_STATS		0x01ff

struct txbd8
{
	u16	status;	/* Status Fields */
	u16	length;	/* Buffer length */
	u32	bufPtr;	/* Buffer Pointer */
};

struct rxbd8
{
	u16	status;	/* Status Fields */
	u16	length;	/* Buffer Length */
	u32	bufPtr;	/* Buffer Pointer */
};

struct rmon_mib
{
	u32	tr64;	/* 0x.680 - Transmit and Receive 64-byte Frame Counter */
	u32	tr127;	/* 0x.684 - Transmit and Receive 65-127 byte Frame Counter */
	u32	tr255;	/* 0x.688 - Transmit and Receive 128-255 byte Frame Counter */
	u32	tr511;	/* 0x.68c - Transmit and Receive 256-511 byte Frame Counter */
	u32	tr1k;	/* 0x.690 - Transmit and Receive 512-1023 byte Frame Counter */
	u32	trmax;	/* 0x.694 - Transmit and Receive 1024-1518 byte Frame Counter */
	u32	trmgv;	/* 0x.698 - Transmit and Receive 1519-1522 byte Good VLAN Frame */
	u32	rbyt;	/* 0x.69c - Receive Byte Counter */
	u32	rpkt;	/* 0x.6a0 - Receive Packet Counter */
	u32	rfcs;	/* 0x.6a4 - Receive FCS Error Counter */
	u32	rmca;	/* 0x.6a8 - Receive Multicast Packet Counter */
	u32	rbca;	/* 0x.6ac - Receive Broadcast Packet Counter */
	u32	rxcf;	/* 0x.6b0 - Receive Control Frame Packet Counter */
	u32	rxpf;	/* 0x.6b4 - Receive Pause Frame Packet Counter */
	u32	rxuo;	/* 0x.6b8 - Receive Unknown OP Code Counter */
	u32	raln;	/* 0x.6bc - Receive Alignment Error Counter */
	u32	rflr;	/* 0x.6c0 - Receive Frame Length Error Counter */
	u32	rcde;	/* 0x.6c4 - Receive Code Error Counter */
	u32	rcse;	/* 0x.6c8 - Receive Carrier Sense Error Counter */
	u32	rund;	/* 0x.6cc - Receive Undersize Packet Counter */
	u32	rovr;	/* 0x.6d0 - Receive Oversize Packet Counter */
	u32	rfrg;	/* 0x.6d4 - Receive Fragments Counter */
	u32	rjbr;	/* 0x.6d8 - Receive Jabber Counter */
	u32	rdrp;	/* 0x.6dc - Receive Drop Counter */
	u32	tbyt;	/* 0x.6e0 - Transmit Byte Counter Counter */
	u32	tpkt;	/* 0x.6e4 - Transmit Packet Counter */
	u32	tmca;	/* 0x.6e8 - Transmit Multicast Packet Counter */
	u32	tbca;	/* 0x.6ec - Transmit Broadcast Packet Counter */
	u32	txpf;	/* 0x.6f0 - Transmit Pause Control Frame Counter */
	u32	tdfr;	/* 0x.6f4 - Transmit Deferral Packet Counter */
	u32	tedf;	/* 0x.6f8 - Transmit Excessive Deferral Packet Counter */
	u32	tscl;	/* 0x.6fc - Transmit Single Collision Packet Counter */
	u32	tmcl;	/* 0x.700 - Transmit Multiple Collision Packet Counter */
	u32	tlcl;	/* 0x.704 - Transmit Late Collision Packet Counter */
	u32	txcl;	/* 0x.708 - Transmit Excessive Collision Packet Counter */
	u32	tncl;	/* 0x.70c - Transmit Total Collision Counter */
	u8	res1[4];
	u32	tdrp;	/* 0x.714 - Transmit Drop Frame Counter */
	u32	tjbr;	/* 0x.718 - Transmit Jabber Frame Counter */
	u32	tfcs;	/* 0x.71c - Transmit FCS Error Counter */
	u32	txcf;	/* 0x.720 - Transmit Control Frame Counter */
	u32	tovr;	/* 0x.724 - Transmit Oversize Frame Counter */
	u32	tund;	/* 0x.728 - Transmit Undersize Frame Counter */
	u32	tfrg;	/* 0x.72c - Transmit Fragments Frame Counter */
	u32	car1;	/* 0x.730 - Carry Register One */
	u32	car2;	/* 0x.734 - Carry Register Two */
	u32	cam1;	/* 0x.738 - Carry Mask Register One */
	u32	cam2;	/* 0x.73c - Carry Mask Register Two */
};

struct gfar_extra_stats {
	u64 kernel_dropped;
	u64 rx_large;
	u64 rx_short;
	u64 rx_nonoctet;
	u64 rx_crcerr;
	u64 rx_overrun;
	u64 rx_bsy;
	u64 rx_babr;
	u64 rx_trunc;
	u64 eberr;
	u64 tx_babt;
	u64 tx_underrun;
	u64 rx_skbmissing;
	u64 tx_timeout;
};

#define GFAR_RMON_LEN ((sizeof(struct rmon_mib) - 16)/sizeof(u32))
#define GFAR_EXTRA_STATS_LEN (sizeof(struct gfar_extra_stats)/sizeof(u64))

/* Number of stats in the stats structure (ignore car and cam regs)*/
#define GFAR_STATS_LEN (GFAR_RMON_LEN + GFAR_EXTRA_STATS_LEN)

#define GFAR_INFOSTR_LEN 32

struct gfar_stats {
	u64 extra[GFAR_EXTRA_STATS_LEN];
	u64 rmon[GFAR_RMON_LEN];
};


struct gfar {
	u8	res1[16];
	u32	ievent;			/* 0x.010 - Interrupt Event Register */
	u32	imask;			/* 0x.014 - Interrupt Mask Register */
	u32	edis;			/* 0x.018 - Error Disabled Register */
	u8	res2[4];
	u32	ecntrl;			/* 0x.020 - Ethernet Control Register */
	u32	minflr;			/* 0x.024 - Minimum Frame Length Register */
	u32	ptv;			/* 0x.028 - Pause Time Value Register */
	u32	dmactrl;		/* 0x.02c - DMA Control Register */
	u32	tbipa;			/* 0x.030 - TBI PHY Address Register */
	u8	res3[88];
	u32	fifo_tx_thr;		/* 0x.08c - FIFO transmit threshold register */
	u8	res4[8];
	u32	fifo_tx_starve;		/* 0x.098 - FIFO transmit starve register */
	u32	fifo_tx_starve_shutoff;	/* 0x.09c - FIFO transmit starve shutoff register */
	u8	res5[96];
	u32	tctrl;			/* 0x.100 - Transmit Control Register */
	u32	tstat;			/* 0x.104 - Transmit Status Register */
	u8	res6[4];
	u32	tbdlen;			/* 0x.10c - Transmit Buffer Descriptor Data Length Register */
	u32	txic;			/* 0x.110 - Transmit Interrupt Coalescing Configuration Register */
	u8	res7[16];
	u32	ctbptr;			/* 0x.124 - Current Transmit Buffer Descriptor Pointer Register */
	u8	res8[92];
	u32	tbptr;			/* 0x.184 - Transmit Buffer Descriptor Pointer Low Register */
	u8	res9[124];
	u32	tbase;			/* 0x.204 - Transmit Descriptor Base Address Register */
	u8	res10[168];
	u32	ostbd;			/* 0x.2b0 - Out-of-Sequence Transmit Buffer Descriptor Register */
	u32	ostbdp;			/* 0x.2b4 - Out-of-Sequence Transmit Data Buffer Pointer Register */
	u8	res11[72];
	u32	rctrl;			/* 0x.300 - Receive Control Register */
	u32	rstat;			/* 0x.304 - Receive Status Register */
	u8	res12[4];
	u32	rbdlen;			/* 0x.30c - RxBD Data Length Register */
	u32	rxic;			/* 0x.310 - Receive Interrupt Coalescing Configuration Register */
	u8	res13[16];
	u32	crbptr;			/* 0x.324 - Current Receive Buffer Descriptor Pointer */
	u8	res14[24];
	u32	mrblr;			/* 0x.340 - Maximum Receive Buffer Length Register */
	u8	res15[64];
	u32	rbptr;			/* 0x.384 - Receive Buffer Descriptor Pointer */
	u8	res16[124];
	u32	rbase;			/* 0x.404 - Receive Descriptor Base Address */
	u8	res17[248];
	u32	maccfg1;		/* 0x.500 - MAC Configuration 1 Register */
	u32	maccfg2;		/* 0x.504 - MAC Configuration 2 Register */
	u32	ipgifg;			/* 0x.508 - Inter Packet Gap/Inter Frame Gap Register */
	u32	hafdup;			/* 0x.50c - Half Duplex Register */
	u32	maxfrm;			/* 0x.510 - Maximum Frame Length Register */
	u8	res18[12];
	u32	miimcfg;		/* 0x.520 - MII Management Configuration Register */
	u32	miimcom;		/* 0x.524 - MII Management Command Register */
	u32	miimadd;		/* 0x.528 - MII Management Address Register */
	u32	miimcon;		/* 0x.52c - MII Management Control Register */
	u32	miimstat;		/* 0x.530 - MII Management Status Register */
	u32	miimind;		/* 0x.534 - MII Management Indicator Register */
	u8	res19[4];
	u32	ifstat;			/* 0x.53c - Interface Status Register */
	u32	macstnaddr1;		/* 0x.540 - Station Address Part 1 Register */
	u32	macstnaddr2;		/* 0x.544 - Station Address Part 2 Register */
	u8	res20[312];
	struct rmon_mib	rmon;
	u8	res21[192];
	u32	iaddr0;			/* 0x.800 - Indivdual address register 0 */
	u32	iaddr1;			/* 0x.804 - Indivdual address register 1 */
	u32	iaddr2;			/* 0x.808 - Indivdual address register 2 */
	u32	iaddr3;			/* 0x.80c - Indivdual address register 3 */
	u32	iaddr4;			/* 0x.810 - Indivdual address register 4 */
	u32	iaddr5;			/* 0x.814 - Indivdual address register 5 */
	u32	iaddr6;			/* 0x.818 - Indivdual address register 6 */
	u32	iaddr7;			/* 0x.81c - Indivdual address register 7 */
	u8	res22[96];
	u32	gaddr0;			/* 0x.880 - Global address register 0 */
	u32	gaddr1;			/* 0x.884 - Global address register 1 */
	u32	gaddr2;			/* 0x.888 - Global address register 2 */
	u32	gaddr3;			/* 0x.88c - Global address register 3 */
	u32	gaddr4;			/* 0x.890 - Global address register 4 */
	u32	gaddr5;			/* 0x.894 - Global address register 5 */
	u32	gaddr6;			/* 0x.898 - Global address register 6 */
	u32	gaddr7;			/* 0x.89c - Global address register 7 */
	u8	res23[856];
	u32	attr;			/* 0x.bf8 - Attributes Register */
	u32	attreli;		/* 0x.bfc - Attributes Extract Length and Extract Index Register */
	u8	res24[1024];

};

/* Struct stolen almost completely (and shamelessly) from the FCC enet source
 * (Ok, that's not so true anymore, but there is a family resemblence)
 * The GFAR buffer descriptors track the ring buffers.  The rx_bd_base
 * and tx_bd_base always point to the currently available buffer.
 * The dirty_tx tracks the current buffer that is being sent by the
 * controller.  The cur_tx and dirty_tx are equal under both completely
 * empty and completely full conditions.  The empty/ready indicator in
 * the buffer descriptor determines the actual condition.
 */
struct gfar_private {
	/* pointers to arrays of skbuffs for tx and rx */
	struct sk_buff ** tx_skbuff;
	struct sk_buff ** rx_skbuff;

	/* indices pointing to the next free sbk in skb arrays */
	u16 skb_curtx;
	u16 skb_currx;

	/* index of the first skb which hasn't been transmitted
	 * yet. */
	u16 skb_dirtytx;

	/* Configuration info for the coalescing features */
	unsigned char txcoalescing;
	unsigned short txcount;
	unsigned short txtime;
	unsigned char rxcoalescing;
	unsigned short rxcount;
	unsigned short rxtime;

	/* GFAR addresses */
	struct rxbd8 *rx_bd_base;	/* Base addresses of Rx and Tx Buffers */
	struct txbd8 *tx_bd_base;
	struct rxbd8 *cur_rx;           /* Next free rx ring entry */
	struct txbd8 *cur_tx;	        /* Next free ring entry */
	struct txbd8 *dirty_tx;		/* The Ring entry to be freed. */
	struct gfar *regs;	/* Pointer to the GFAR memory mapped Registers */
	struct gfar *phyregs;
	struct work_struct tq;
	struct timer_list phy_info_timer;
	struct net_device_stats stats; /* linux network statistics */
	struct gfar_extra_stats extra_stats;
	spinlock_t lock;
	unsigned int rx_buffer_size;
	unsigned int rx_stash_size;
	unsigned int tx_ring_size;
	unsigned int rx_ring_size;
	wait_queue_head_t rxcleanupq;
	unsigned int rxclean;

	/* Info structure initialized by board setup code */
	unsigned int interruptTransmit;
	unsigned int interruptReceive;
	unsigned int interruptError;
	struct gianfar_platform_data *einfo;

	struct gfar_mii_info *mii_info;
	int oldspeed;
	int oldduplex;
	int oldlink;
};

extern inline u32 gfar_read(volatile unsigned *addr)
{
	u32 val;
	val = in_be32(addr);
	return val;
}

extern inline void gfar_write(volatile unsigned *addr, u32 val)
{
	out_be32(addr, val);
}

extern struct ethtool_ops *gfar_op_array[];

#endif /* __GIANFAR_H */
