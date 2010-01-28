/*
 * Tehuti Networks(R) Network Driver
 * Copyright (C) 2007 Tehuti Networks Ltd. All rights reserved
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef _TEHUTI_H
#define _TEHUTI_H

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/pci.h>
#include <linux/delay.h>
#include <linux/ethtool.h>
#include <linux/mii.h>
#include <linux/crc32.h>
#include <linux/uaccess.h>
#include <linux/in.h>
#include <linux/ip.h>
#include <linux/tcp.h>
#include <linux/sched.h>
#include <linux/tty.h>
#include <linux/if_vlan.h>
#include <linux/interrupt.h>
#include <linux/vmalloc.h>
#include <linux/firmware.h>
#include <asm/byteorder.h>
#include <linux/dma-mapping.h>

/* Compile Time Switches */
/* start */
#define BDX_TSO
#define BDX_LLTX
#define BDX_DELAY_WPTR
/* #define BDX_MSI */
/* end */

#if !defined CONFIG_PCI_MSI
#   undef BDX_MSI
#endif

#define BDX_DEF_MSG_ENABLE	(NETIF_MSG_DRV          | \
				NETIF_MSG_PROBE        | \
				NETIF_MSG_LINK)

/* ioctl ops */
#define BDX_OP_READ  1
#define BDX_OP_WRITE 2

/* RX copy break size */
#define BDX_COPYBREAK    257

#define DRIVER_AUTHOR     "Tehuti Networks(R)"
#define BDX_DRV_DESC      "Tehuti Networks(R) Network Driver"
#define BDX_DRV_NAME      "tehuti"
#define BDX_NIC_NAME      "Tehuti 10 Giga TOE SmartNIC"
#define BDX_NIC2PORT_NAME "Tehuti 2-Port 10 Giga TOE SmartNIC"
#define BDX_DRV_VERSION   "7.29.3"

#ifdef BDX_MSI
#    define BDX_MSI_STRING "msi "
#else
#    define BDX_MSI_STRING ""
#endif

/* netdev tx queue len for Luxor. default value is, btw, 1000
 * ifcontig eth1 txqueuelen 3000 - to change it at runtime */
#define BDX_NDEV_TXQ_LEN 3000

#define FIFO_SIZE  4096
#define FIFO_EXTRA_SPACE            1024

#if BITS_PER_LONG == 64
#    define H32_64(x)  (u32) ((u64)(x) >> 32)
#    define L32_64(x)  (u32) ((u64)(x) & 0xffffffff)
#elif BITS_PER_LONG == 32
#    define H32_64(x)  0
#    define L32_64(x)  ((u32) (x))
#else				/* BITS_PER_LONG == ?? */
#    error BITS_PER_LONG is undefined. Must be 64 or 32
#endif				/* BITS_PER_LONG */

#ifdef __BIG_ENDIAN
#   define CPU_CHIP_SWAP32(x) swab32(x)
#   define CPU_CHIP_SWAP16(x) swab16(x)
#else
#   define CPU_CHIP_SWAP32(x) (x)
#   define CPU_CHIP_SWAP16(x) (x)
#endif

#define READ_REG(pp, reg)         readl(pp->pBdxRegs + reg)
#define WRITE_REG(pp, reg, val)   writel(val, pp->pBdxRegs + reg)

#ifndef NET_IP_ALIGN
#   define NET_IP_ALIGN 2
#endif

#ifndef NETDEV_TX_OK
#   define NETDEV_TX_OK 0
#endif

#define LUXOR_MAX_PORT     2
#define BDX_MAX_RX_DONE    150
#define BDX_TXF_DESC_SZ    16
#define BDX_MAX_TX_LEVEL   (priv->txd_fifo0.m.memsz - 16)
#define BDX_MIN_TX_LEVEL   256
#define BDX_NO_UPD_PACKETS 40

struct pci_nic {
	int port_num;
	void __iomem *regs;
	int irq_type;
	struct bdx_priv *priv[LUXOR_MAX_PORT];
};

enum { IRQ_INTX, IRQ_MSI, IRQ_MSIX };

#define PCK_TH_MULT   128
#define INT_COAL_MULT 2

#define BITS_MASK(nbits)			((1<<nbits)-1)
#define GET_BITS_SHIFT(x, nbits, nshift)	(((x)>>nshift)&BITS_MASK(nbits))
#define BITS_SHIFT_MASK(nbits, nshift)		(BITS_MASK(nbits)<<nshift)
#define BITS_SHIFT_VAL(x, nbits, nshift)	(((x)&BITS_MASK(nbits))<<nshift)
#define BITS_SHIFT_CLEAR(x, nbits, nshift)	\
	((x)&(~BITS_SHIFT_MASK(nbits, nshift)))

#define GET_INT_COAL(x)				GET_BITS_SHIFT(x, 15, 0)
#define GET_INT_COAL_RC(x)			GET_BITS_SHIFT(x, 1, 15)
#define GET_RXF_TH(x)				GET_BITS_SHIFT(x, 4, 16)
#define GET_PCK_TH(x)				GET_BITS_SHIFT(x, 4, 20)

#define INT_REG_VAL(coal, coal_rc, rxf_th, pck_th)	\
	((coal)|((coal_rc)<<15)|((rxf_th)<<16)|((pck_th)<<20))

struct fifo {
	dma_addr_t da;		/* physical address of fifo (used by HW) */
	char *va;		/* virtual address of fifo (used by SW) */
	u32 rptr, wptr;		/* cached values of RPTR and WPTR registers,
				   they're 32 bits on both 32 and 64 archs */
	u16 reg_CFG0, reg_CFG1;
	u16 reg_RPTR, reg_WPTR;
	u16 memsz;		/* memory size allocated for fifo */
	u16 size_mask;
	u16 pktsz;		/* skb packet size to allocate */
	u16 rcvno;		/* number of buffers that come from this RXF */
};

struct txf_fifo {
	struct fifo m;		/* minimal set of variables used by all fifos */
};

struct txd_fifo {
	struct fifo m;		/* minimal set of variables used by all fifos */
};

struct rxf_fifo {
	struct fifo m;		/* minimal set of variables used by all fifos */
};

struct rxd_fifo {
	struct fifo m;		/* minimal set of variables used by all fifos */
};

struct rx_map {
	u64 dma;
	struct sk_buff *skb;
};

struct rxdb {
	int *stack;
	struct rx_map *elems;
	int nelem;
	int top;
};

union bdx_dma_addr {
	dma_addr_t dma;
	struct sk_buff *skb;
};

/* Entry in the db.
 * if len == 0 addr is dma
 * if len != 0 addr is skb */
struct tx_map {
	union bdx_dma_addr addr;
	int len;
};

/* tx database - implemented as circular fifo buffer*/
struct txdb {
	struct tx_map *start;	/* points to the first element */
	struct tx_map *end;	/* points just AFTER the last element */
	struct tx_map *rptr;	/* points to the next element to read */
	struct tx_map *wptr;	/* points to the next element to write */
	int size;		/* number of elements in the db */
};

/*Internal stats structure*/
struct bdx_stats {
	u64 InUCast;			/* 0x7200 */
	u64 InMCast;			/* 0x7210 */
	u64 InBCast;			/* 0x7220 */
	u64 InPkts;			/* 0x7230 */
	u64 InErrors;			/* 0x7240 */
	u64 InDropped;			/* 0x7250 */
	u64 FrameTooLong;		/* 0x7260 */
	u64 FrameSequenceErrors;	/* 0x7270 */
	u64 InVLAN;			/* 0x7280 */
	u64 InDroppedDFE;		/* 0x7290 */
	u64 InDroppedIntFull;		/* 0x72A0 */
	u64 InFrameAlignErrors;		/* 0x72B0 */

	/* 0x72C0-0x72E0 RSRV */

	u64 OutUCast;			/* 0x72F0 */
	u64 OutMCast;			/* 0x7300 */
	u64 OutBCast;			/* 0x7310 */
	u64 OutPkts;			/* 0x7320 */

	/* 0x7330-0x7360 RSRV */

	u64 OutVLAN;			/* 0x7370 */
	u64 InUCastOctects;		/* 0x7380 */
	u64 OutUCastOctects;		/* 0x7390 */

	/* 0x73A0-0x73B0 RSRV */

	u64 InBCastOctects;		/* 0x73C0 */
	u64 OutBCastOctects;		/* 0x73D0 */
	u64 InOctects;			/* 0x73E0 */
	u64 OutOctects;			/* 0x73F0 */
};

struct bdx_priv {
	void __iomem *pBdxRegs;
	struct net_device *ndev;

	struct napi_struct napi;

	/* RX FIFOs: 1 for data (full) descs, and 2 for free descs */
	struct rxd_fifo rxd_fifo0;
	struct rxf_fifo rxf_fifo0;
	struct rxdb *rxdb;	/* rx dbs to store skb pointers */
	int napi_stop;
	struct vlan_group *vlgrp;

	/* Tx FIFOs: 1 for data desc, 1 for empty (acks) desc */
	struct txd_fifo txd_fifo0;
	struct txf_fifo txf_fifo0;

	struct txdb txdb;
	int tx_level;
#ifdef BDX_DELAY_WPTR
	int tx_update_mark;
	int tx_noupd;
#endif
	spinlock_t tx_lock;	/* NETIF_F_LLTX mode */

	/* rarely used */
	u8 port;
	u32 msg_enable;
	int stats_flag;
	struct bdx_stats hw_stats;
	struct net_device_stats net_stats;
	struct pci_dev *pdev;

	struct pci_nic *nic;

	u8 txd_size;
	u8 txf_size;
	u8 rxd_size;
	u8 rxf_size;
	u32 rdintcm;
	u32 tdintcm;
};

/* RX FREE descriptor - 64bit*/
struct rxf_desc {
	u32 info;		/* Buffer Count + Info - described below */
	u32 va_lo;		/* VAdr[31:0] */
	u32 va_hi;		/* VAdr[63:32] */
	u32 pa_lo;		/* PAdr[31:0] */
	u32 pa_hi;		/* PAdr[63:32] */
	u32 len;		/* Buffer Length */
};

#define GET_RXD_BC(x)			GET_BITS_SHIFT((x), 5, 0)
#define GET_RXD_RXFQ(x)			GET_BITS_SHIFT((x), 2, 8)
#define GET_RXD_TO(x)			GET_BITS_SHIFT((x), 1, 15)
#define GET_RXD_TYPE(x)			GET_BITS_SHIFT((x), 4, 16)
#define GET_RXD_ERR(x)			GET_BITS_SHIFT((x), 6, 21)
#define GET_RXD_RXP(x)			GET_BITS_SHIFT((x), 1, 27)
#define GET_RXD_PKT_ID(x)		GET_BITS_SHIFT((x), 3, 28)
#define GET_RXD_VTAG(x)			GET_BITS_SHIFT((x), 1, 31)
#define GET_RXD_VLAN_ID(x)		GET_BITS_SHIFT((x), 12, 0)
#define GET_RXD_VLAN_TCI(x)		GET_BITS_SHIFT((x), 16, 0)
#define GET_RXD_CFI(x)			GET_BITS_SHIFT((x), 1, 12)
#define GET_RXD_PRIO(x)			GET_BITS_SHIFT((x), 3, 13)

struct rxd_desc {
	u32 rxd_val1;
	u16 len;
	u16 rxd_vlan;
	u32 va_lo;
	u32 va_hi;
};

/* PBL describes each virtual buffer to be */
/* transmitted from the host.*/
struct pbl {
	u32 pa_lo;
	u32 pa_hi;
	u32 len;
};

/* First word for TXD descriptor. It means: type = 3 for regular Tx packet,
 * hw_csum = 7 for ip+udp+tcp hw checksums */
#define TXD_W1_VAL(bc, checksum, vtag, lgsnd, vlan_id)	\
	((bc) | ((checksum)<<5) | ((vtag)<<8) | \
	((lgsnd)<<9) | (0x30000) | ((vlan_id)<<20))

struct txd_desc {
	u32 txd_val1;
	u16 mss;
	u16 length;
	u32 va_lo;
	u32 va_hi;
	struct pbl pbl[0];	/* Fragments */
} __attribute__ ((packed));

/* Register region size */
#define BDX_REGS_SIZE	  0x1000

/* Registers from 0x0000-0x00fc were remapped to 0x4000-0x40fc */
#define regTXD_CFG1_0   0x4000
#define regRXF_CFG1_0   0x4010
#define regRXD_CFG1_0   0x4020
#define regTXF_CFG1_0   0x4030
#define regTXD_CFG0_0   0x4040
#define regRXF_CFG0_0   0x4050
#define regRXD_CFG0_0   0x4060
#define regTXF_CFG0_0   0x4070
#define regTXD_WPTR_0   0x4080
#define regRXF_WPTR_0   0x4090
#define regRXD_WPTR_0   0x40A0
#define regTXF_WPTR_0   0x40B0
#define regTXD_RPTR_0   0x40C0
#define regRXF_RPTR_0   0x40D0
#define regRXD_RPTR_0   0x40E0
#define regTXF_RPTR_0   0x40F0
#define regTXF_RPTR_3   0x40FC

/* hardware versioning */
#define  FW_VER         0x5010
#define  SROM_VER       0x5020
#define  FPGA_VER       0x5030
#define  FPGA_SEED      0x5040

/* Registers from 0x0100-0x0150 were remapped to 0x5100-0x5150 */
#define regISR regISR0
#define regISR0          0x5100

#define regIMR regIMR0
#define regIMR0          0x5110

#define regRDINTCM0      0x5120
#define regRDINTCM2      0x5128

#define regTDINTCM0      0x5130

#define regISR_MSK0      0x5140

#define regINIT_SEMAPHORE 0x5170
#define regINIT_STATUS    0x5180

#define regMAC_LNK_STAT  0x0200
#define MAC_LINK_STAT    0x4	/* Link state */

#define regGMAC_RXF_A   0x1240

#define regUNC_MAC0_A   0x1250
#define regUNC_MAC1_A   0x1260
#define regUNC_MAC2_A   0x1270

#define regVLAN_0       0x1800

#define regMAX_FRAME_A  0x12C0

#define regRX_MAC_MCST0    0x1A80
#define regRX_MAC_MCST1    0x1A84
#define MAC_MCST_NUM       15
#define regRX_MCST_HASH0   0x1A00
#define MAC_MCST_HASH_NUM  8

#define regVPC                  0x2300
#define regVIC                  0x2320
#define regVGLB                 0x2340

#define regCLKPLL               0x5000

/*for 10G only*/
#define regREVISION        0x6000
#define regSCRATCH         0x6004
#define regCTRLST          0x6008
#define regMAC_ADDR_0      0x600C
#define regMAC_ADDR_1      0x6010
#define regFRM_LENGTH      0x6014
#define regPAUSE_QUANT     0x6018
#define regRX_FIFO_SECTION 0x601C
#define regTX_FIFO_SECTION 0x6020
#define regRX_FULLNESS     0x6024
#define regTX_FULLNESS     0x6028
#define regHASHTABLE       0x602C
#define regMDIO_ST         0x6030
#define regMDIO_CTL        0x6034
#define regMDIO_DATA       0x6038
#define regMDIO_ADDR       0x603C

#define regRST_PORT        0x7000
#define regDIS_PORT        0x7010
#define regRST_QU          0x7020
#define regDIS_QU          0x7030

#define regCTRLST_TX_ENA   0x0001
#define regCTRLST_RX_ENA   0x0002
#define regCTRLST_PRM_ENA  0x0010
#define regCTRLST_PAD_ENA  0x0020

#define regCTRLST_BASE     (regCTRLST_PAD_ENA|regCTRLST_PRM_ENA)

#define regRX_FLT   0x1400

/* TXD TXF RXF RXD  CONFIG 0x0000 --- 0x007c*/
#define  TX_RX_CFG1_BASE          0xffffffff	/*0-31 */
#define  TX_RX_CFG0_BASE          0xfffff000	/*31:12 */
#define  TX_RX_CFG0_RSVD          0x0ffc	/*11:2 */
#define  TX_RX_CFG0_SIZE          0x0003	/*1:0 */

/*  TXD TXF RXF RXD  WRITE 0x0080 --- 0x00BC */
#define  TXF_WPTR_WR_PTR        0x7ff8	/*14:3 */

/*  TXD TXF RXF RXD  READ  0x00CO --- 0x00FC */
#define  TXF_RPTR_RD_PTR        0x7ff8	/*14:3 */

#define TXF_WPTR_MASK 0x7ff0	/* last 4 bits are dropped
				 * size is rounded to 16 */

/*  regISR 0x0100 */
/*  regIMR 0x0110 */
#define  IMR_INPROG   0x80000000	/*31 */
#define  IR_LNKCHG1   0x10000000	/*28 */
#define  IR_LNKCHG0   0x08000000	/*27 */
#define  IR_GPIO      0x04000000	/*26 */
#define  IR_RFRSH     0x02000000	/*25 */
#define  IR_RSVD      0x01000000	/*24 */
#define  IR_SWI       0x00800000	/*23 */
#define  IR_RX_FREE_3 0x00400000	/*22 */
#define  IR_RX_FREE_2 0x00200000	/*21 */
#define  IR_RX_FREE_1 0x00100000	/*20 */
#define  IR_RX_FREE_0 0x00080000	/*19 */
#define  IR_TX_FREE_3 0x00040000	/*18 */
#define  IR_TX_FREE_2 0x00020000	/*17 */
#define  IR_TX_FREE_1 0x00010000	/*16 */
#define  IR_TX_FREE_0 0x00008000	/*15 */
#define  IR_RX_DESC_3 0x00004000	/*14 */
#define  IR_RX_DESC_2 0x00002000	/*13 */
#define  IR_RX_DESC_1 0x00001000	/*12 */
#define  IR_RX_DESC_0 0x00000800	/*11 */
#define  IR_PSE       0x00000400	/*10 */
#define  IR_TMR3      0x00000200	/*9 */
#define  IR_TMR2      0x00000100	/*8 */
#define  IR_TMR1      0x00000080	/*7 */
#define  IR_TMR0      0x00000040	/*6 */
#define  IR_VNT       0x00000020	/*5 */
#define  IR_RxFL      0x00000010	/*4 */
#define  IR_SDPERR    0x00000008	/*3 */
#define  IR_TR        0x00000004	/*2 */
#define  IR_PCIE_LINK 0x00000002	/*1 */
#define  IR_PCIE_TOUT 0x00000001	/*0 */

#define  IR_EXTRA (IR_RX_FREE_0 | IR_LNKCHG0 | IR_PSE | \
    IR_TMR0 | IR_PCIE_LINK | IR_PCIE_TOUT)
#define  IR_RUN (IR_EXTRA | IR_RX_DESC_0 | IR_TX_FREE_0)
#define  IR_ALL 0xfdfffff7

#define  IR_LNKCHG0_ofst        27

#define  GMAC_RX_FILTER_OSEN  0x1000	/* shared OS enable */
#define  GMAC_RX_FILTER_TXFC  0x0400	/* Tx flow control */
#define  GMAC_RX_FILTER_RSV0  0x0200	/* reserved */
#define  GMAC_RX_FILTER_FDA   0x0100	/* filter out direct address */
#define  GMAC_RX_FILTER_AOF   0x0080	/* accept over run */
#define  GMAC_RX_FILTER_ACF   0x0040	/* accept control frames */
#define  GMAC_RX_FILTER_ARUNT 0x0020	/* accept under run */
#define  GMAC_RX_FILTER_ACRC  0x0010	/* accept crc error */
#define  GMAC_RX_FILTER_AM    0x0008	/* accept multicast */
#define  GMAC_RX_FILTER_AB    0x0004	/* accept broadcast */
#define  GMAC_RX_FILTER_PRM   0x0001	/* [0:1] promiscous mode */

#define  MAX_FRAME_AB_VAL       0x3fff	/* 13:0 */

#define  CLKPLL_PLLLKD          0x0200	/*9 */
#define  CLKPLL_RSTEND          0x0100	/*8 */
#define  CLKPLL_SFTRST          0x0001	/*0 */

#define  CLKPLL_LKD             (CLKPLL_PLLLKD|CLKPLL_RSTEND)

/*
 * PCI-E Device Control Register (Offset 0x88)
 * Source: Luxor Data Sheet, 7.1.3.3.3
 */
#define PCI_DEV_CTRL_REG 0x88
#define GET_DEV_CTRL_MAXPL(x)           GET_BITS_SHIFT(x, 3, 5)
#define GET_DEV_CTRL_MRRS(x)            GET_BITS_SHIFT(x, 3, 12)

/*
 * PCI-E Link Status Register (Offset 0x92)
 * Source: Luxor Data Sheet, 7.1.3.3.7
 */
#define PCI_LINK_STATUS_REG 0x92
#define GET_LINK_STATUS_LANES(x)		GET_BITS_SHIFT(x, 6, 4)

/* Debugging Macros */

#define ERR(fmt, args...) printk(KERN_ERR fmt, ## args)
#define DBG2(fmt, args...)	\
	printk(KERN_ERR  "%s:%-5d: " fmt, __func__, __LINE__, ## args)

#define BDX_ASSERT(x) BUG_ON(x)

#ifdef DEBUG

#define ENTER          do { \
	printk(KERN_ERR  "%s:%-5d: ENTER\n", __func__, __LINE__); \
} while (0)

#define RET(args...)   do { \
	printk(KERN_ERR  "%s:%-5d: RETURN\n", __func__, __LINE__); \
return args; } while (0)

#define DBG(fmt, args...)	\
	printk(KERN_ERR  "%s:%-5d: " fmt, __func__, __LINE__, ## args)
#else
#define ENTER         do {  } while (0)
#define RET(args...)   return args
#define DBG(fmt, args...)   do {  } while (0)
#endif

#endif /* _BDX__H */
