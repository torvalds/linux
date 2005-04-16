/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2001 Patton Electronics Company
 * Copyright (C) 2002 Momentum Computer
 *
 * Copyright 2000 MontaVista Software Inc.
 * Author: MontaVista Software, Inc.
 *         	stevel@mvista.com or support@mvista.com
 *
 *  This program is free software; you can distribute it and/or modify it
 *  under the terms of the GNU General Public License (Version 2) as
 *  published by the Free Software Foundation.
 *
 *  This program is distributed in the hope it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 *  for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place - Suite 330, Boston MA 02111-1307, USA.
 *
 * Ethernet driver definitions for the MIPS GT96100 Advanced
 * Communication Controller.
 * 
 * Modified for the Marvellous GT64240 Retarded Communication Controller.
 */
#ifndef _GT64240ETH_H
#define _GT64240ETH_H

#include <asm/gt64240.h>

#define ETHERNET_PORTS_DIFFERENCE_OFFSETS	0x400

/* Translate those weanie names from Galileo/VxWorks header files: */

#define GT64240_MRR                    MAIN_ROUTING_REGISTER
#define GT64240_CIU_ARBITER_CONFIG     COMM_UNIT_ARBITER_CONFIGURATION_REGISTER
#define GT64240_CIU_ARBITER_CONTROL    COMM_UNIT_ARBITER_CONTROL
#define GT64240_MAIN_LOW_CAUSE         LOW_INTERRUPT_CAUSE_REGISTER
#define GT64240_MAIN_HIGH_CAUSE        HIGH_INTERRUPT_CAUSE_REGISTER
#define GT64240_CPU_LOW_MASK           CPU_INTERRUPT_MASK_REGISTER_LOW
#define GT64240_CPU_HIGH_MASK          CPU_INTERRUPT_MASK_REGISTER_HIGH
#define GT64240_CPU_SELECT_CAUSE       CPU_SELECT_CAUSE_REGISTER

#define GT64240_ETH_PHY_ADDR_REG       ETHERNET_PHY_ADDRESS_REGISTER
#define GT64240_ETH_PORT_CONFIG        ETHERNET0_PORT_CONFIGURATION_REGISTER
#define GT64240_ETH_PORT_CONFIG_EXT    ETHERNET0_PORT_CONFIGURATION_EXTEND_REGISTER
#define GT64240_ETH_PORT_COMMAND       ETHERNET0_PORT_COMMAND_REGISTER
#define GT64240_ETH_PORT_STATUS        ETHERNET0_PORT_STATUS_REGISTER
#define GT64240_ETH_IO_SIZE            ETHERNET_PORTS_DIFFERENCE_OFFSETS
#define GT64240_ETH_SMI_REG            ETHERNET_SMI_REGISTER
#define GT64240_ETH_MIB_COUNT_BASE     ETHERNET0_MIB_COUNTER_BASE
#define GT64240_ETH_SDMA_CONFIG        ETHERNET0_SDMA_CONFIGURATION_REGISTER
#define GT64240_ETH_SDMA_COMM          ETHERNET0_SDMA_COMMAND_REGISTER
#define GT64240_ETH_INT_MASK           ETHERNET0_INTERRUPT_MASK_REGISTER
#define GT64240_ETH_INT_CAUSE          ETHERNET0_INTERRUPT_CAUSE_REGISTER
#define GT64240_ETH_CURR_TX_DESC_PTR0  ETHERNET0_CURRENT_TX_DESCRIPTOR_POINTER0
#define GT64240_ETH_CURR_TX_DESC_PTR1  ETHERNET0_CURRENT_TX_DESCRIPTOR_POINTER1
#define GT64240_ETH_1ST_RX_DESC_PTR0   ETHERNET0_FIRST_RX_DESCRIPTOR_POINTER0
#define GT64240_ETH_CURR_RX_DESC_PTR0  ETHERNET0_CURRENT_RX_DESCRIPTOR_POINTER0
#define GT64240_ETH_HASH_TBL_PTR       ETHERNET0_HASH_TABLE_POINTER_REGISTER

/* Turn on NAPI by default */

#define	GT64240_NAPI			1

/* Some 64240 settings that SHOULD eventually be setup in PROM monitor: */
/* (Board-specific to the DSL3224 Rev A board ONLY!)                    */
#define D3224_MPP_CTRL0_SETTING		0x66669900
#define D3224_MPP_CTRL1_SETTING		0x00000000
#define D3224_MPP_CTRL2_SETTING		0x00887700
#define D3224_MPP_CTRL3_SETTING		0x00000044
#define D3224_GPP_IO_CTRL_SETTING	0x0000e800
#define D3224_GPP_LEVEL_CTRL_SETTING	0xf001f703
#define D3224_GPP_VALUE_SETTING		0x00000000

/* Keep the ring sizes a power of two for efficiency. */
//-#define TX_RING_SIZE 16
#define TX_RING_SIZE	64	/* TESTING !!! */
#define RX_RING_SIZE	32
#define PKT_BUF_SZ	1536	/* Size of each temporary Rx buffer. */

#define RX_HASH_TABLE_SIZE 16384
#define HASH_HOP_NUMBER 12

#define NUM_INTERFACES 3

#define GT64240ETH_TX_TIMEOUT HZ/4

#define MIPS_GT64240_BASE 0xf4000000
#define GT64240_ETH0_BASE (MIPS_GT64240_BASE + GT64240_ETH_PORT_CONFIG)
#define GT64240_ETH1_BASE (GT64240_ETH0_BASE + GT64240_ETH_IO_SIZE)
#define GT64240_ETH2_BASE (GT64240_ETH1_BASE + GT64240_ETH_IO_SIZE)

#if defined(CONFIG_MIPS_DSL3224)
#define GT64240_ETHER0_IRQ 4
#define GT64240_ETHER1_IRQ 4
#else
#define GT64240_ETHER0_IRQ -1
#define GT64240_ETHER1_IRQ -1
#endif

#define REV_GT64240  0x1
#define REV_GT64240A 0x10

#define GT64240ETH_READ(gp, offset)					\
	GT_READ((gp)->port_offset + (offset))

#define GT64240ETH_WRITE(gp, offset, data)				\
	GT_WRITE((gp)->port_offset + (offset), (data))

#define GT64240ETH_SETBIT(gp, offset, bits)				\
	GT64240ETH_WRITE((gp), (offset),				\
	                 GT64240ETH_READ((gp), (offset)) | (bits))

#define GT64240ETH_CLRBIT(gp, offset, bits)				\
	GT64240ETH_WRITE((gp), (offset),				\
	                 GT64240ETH_READ((gp), (offset)) & ~(bits))

#define GT64240_READ(ofs)		GT_READ(ofs)
#define GT64240_WRITE(ofs, data)	GT_WRITE((ofs), (data))

/* Bit definitions of the SMI Reg */
enum {
	smirDataMask = 0xffff,
	smirPhyAdMask = 0x1f << 16,
	smirPhyAdBit = 16,
	smirRegAdMask = 0x1f << 21,
	smirRegAdBit = 21,
	smirOpCode = 1 << 26,
	smirReadValid = 1 << 27,
	smirBusy = 1 << 28
};

/* Bit definitions of the Port Config Reg */
enum pcr_bits {
	pcrPM = 1 << 0,
	pcrRBM = 1 << 1,
	pcrPBF = 1 << 2,
	pcrEN = 1 << 7,
	pcrLPBKMask = 0x3 << 8,
	pcrLPBKBit = 1 << 8,
	pcrFC = 1 << 10,
	pcrHS = 1 << 12,
	pcrHM = 1 << 13,
	pcrHDM = 1 << 14,
	pcrHD = 1 << 15,
	pcrISLMask = 0x7 << 28,
	pcrISLBit = 28,
	pcrACCS = 1 << 31
};

/* Bit definitions of the Port Config Extend Reg */
enum pcxr_bits {
	pcxrIGMP = 1,
	pcxrSPAN = 2,
	pcxrPAR = 4,
	pcxrPRIOtxMask = 0x7 << 3,
	pcxrPRIOtxBit = 3,
	pcxrPRIOrxMask = 0x3 << 6,
	pcxrPRIOrxBit = 6,
	pcxrPRIOrxOverride = 1 << 8,
	pcxrDPLXen = 1 << 9,
	pcxrFCTLen = 1 << 10,
	pcxrFLP = 1 << 11,
	pcxrFCTL = 1 << 12,
	pcxrMFLMask = 0x3 << 14,
	pcxrMFLBit = 14,
	pcxrMIBclrMode = 1 << 16,
	pcxrSpeed = 1 << 18,
	pcxrSpeeden = 1 << 19,
	pcxrRMIIen = 1 << 20,
	pcxrDSCPen = 1 << 21
};

/* Bit definitions of the Port Command Reg */
enum pcmr_bits {
	pcmrFJ = 1 << 15
};


/* Bit definitions of the Port Status Reg */
enum psr_bits {
	psrSpeed = 1,
	psrDuplex = 2,
	psrFctl = 4,
	psrLink = 8,
	psrPause = 1 << 4,
	psrTxLow = 1 << 5,
	psrTxHigh = 1 << 6,
	psrTxInProg = 1 << 7
};

/* Bit definitions of the SDMA Config Reg */
enum sdcr_bits {
	sdcrRCMask = 0xf << 2,
	sdcrRCBit = 2,
	sdcrBLMR = 1 << 6,
	sdcrBLMT = 1 << 7,
	sdcrPOVR = 1 << 8,
	sdcrRIFB = 1 << 9,
	sdcrBSZMask = 0x3 << 12,
	sdcrBSZBit = 12
};

/* Bit definitions of the SDMA Command Reg */
enum sdcmr_bits {
	sdcmrERD = 1 << 7,
	sdcmrAR = 1 << 15,
	sdcmrSTDH = 1 << 16,
	sdcmrSTDL = 1 << 17,
	sdcmrTXDH = 1 << 23,
	sdcmrTXDL = 1 << 24,
	sdcmrAT = 1 << 31
};

/* Bit definitions of the Interrupt Cause Reg */
enum icr_bits {
	icrRxBuffer = 1,
	icrTxBufferHigh = 1 << 2,
	icrTxBufferLow = 1 << 3,
	icrTxEndHigh = 1 << 6,
	icrTxEndLow = 1 << 7,
	icrRxError = 1 << 8,
	icrTxErrorHigh = 1 << 10,
	icrTxErrorLow = 1 << 11,
	icrRxOVR = 1 << 12,
	icrTxUdr = 1 << 13,
	icrRxBufferQ0 = 1 << 16,
	icrRxBufferQ1 = 1 << 17,
	icrRxBufferQ2 = 1 << 18,
	icrRxBufferQ3 = 1 << 19,
	icrRxErrorQ0 = 1 << 20,
	icrRxErrorQ1 = 1 << 21,
	icrRxErrorQ2 = 1 << 22,
	icrRxErrorQ3 = 1 << 23,
	icrMIIPhySTC = 1 << 28,
	icrSMIdone = 1 << 29,
	icrEtherIntSum = 1 << 31
};


/* The Rx and Tx descriptor lists. */
#ifdef __LITTLE_ENDIAN
typedef struct {
	u32 cmdstat;
	u16 reserved;		//-prk21aug01    u32 reserved:16;
	u16 byte_cnt;		//-prk21aug01    u32 byte_cnt:16;
	u32 buff_ptr;
	u32 next;
} gt64240_td_t;

typedef struct {
	u32 cmdstat;
	u16 byte_cnt;		//-prk21aug01    u32 byte_cnt:16;
	u16 buff_sz;		//-prk21aug01    u32 buff_sz:16;
	u32 buff_ptr;
	u32 next;
} gt64240_rd_t;
#elif defined(__BIG_ENDIAN)
typedef struct {
	u16 byte_cnt;		//-prk21aug01    u32 byte_cnt:16;
	u16 reserved;		//-prk21aug01    u32 reserved:16;
	u32 cmdstat;
	u32 next;
	u32 buff_ptr;
} gt64240_td_t;

typedef struct {
	u16 buff_sz;		//-prk21aug01    u32 buff_sz:16;
	u16 byte_cnt;		//-prk21aug01    u32 byte_cnt:16;
	u32 cmdstat;
	u32 next;
	u32 buff_ptr;
} gt64240_rd_t;
#else
#error Either __BIG_ENDIAN or __LITTLE_ENDIAN must be defined!
#endif


/* Values for the Tx command-status descriptor entry. */
enum td_cmdstat {
	txOwn = 1 << 31,
	txAutoMode = 1 << 30,
	txEI = 1 << 23,
	txGenCRC = 1 << 22,
	txPad = 1 << 18,
	txFirst = 1 << 17,
	txLast = 1 << 16,
	txErrorSummary = 1 << 15,
	txReTxCntMask = 0x0f << 10,
	txReTxCntBit = 10,
	txCollision = 1 << 9,
	txReTxLimit = 1 << 8,
	txUnderrun = 1 << 6,
	txLateCollision = 1 << 5
};


/* Values for the Rx command-status descriptor entry. */
enum rd_cmdstat {
	rxOwn = 1 << 31,
	rxAutoMode = 1 << 30,
	rxEI = 1 << 23,
	rxFirst = 1 << 17,
	rxLast = 1 << 16,
	rxErrorSummary = 1 << 15,
	rxIGMP = 1 << 14,
	rxHashExpired = 1 << 13,
	rxMissedFrame = 1 << 12,
	rxFrameType = 1 << 11,
	rxShortFrame = 1 << 8,
	rxMaxFrameLen = 1 << 7,
	rxOverrun = 1 << 6,
	rxCollision = 1 << 4,
	rxCRCError = 1
};

/* Bit fields of a Hash Table Entry */
enum hash_table_entry {
	hteValid = 1,
	hteSkip = 2,
	hteRD = 4
};

// The MIB counters
typedef struct {
	u32 byteReceived;
	u32 byteSent;
	u32 framesReceived;
	u32 framesSent;
	u32 totalByteReceived;
	u32 totalFramesReceived;
	u32 broadcastFramesReceived;
	u32 multicastFramesReceived;
	u32 cRCError;
	u32 oversizeFrames;
	u32 fragments;
	u32 jabber;
	u32 collision;
	u32 lateCollision;
	u32 frames64;
	u32 frames65_127;
	u32 frames128_255;
	u32 frames256_511;
	u32 frames512_1023;
	u32 frames1024_MaxSize;
	u32 macRxError;
	u32 droppedFrames;
	u32 outMulticastFrames;
	u32 outBroadcastFrames;
	u32 undersizeFrames;
} mib_counters_t;


struct gt64240_private {
	gt64240_rd_t *rx_ring;
	gt64240_td_t *tx_ring;
	// The Rx and Tx rings must be 16-byte aligned
	dma_addr_t rx_ring_dma;
	dma_addr_t tx_ring_dma;
	char *hash_table;
	// The Hash Table must be 8-byte aligned
	dma_addr_t hash_table_dma;
	int hash_mode;

	// The Rx buffers must be 8-byte aligned
	char *rx_buff;
	dma_addr_t rx_buff_dma;
	// Tx buffers (tx_skbuff[i]->data) with less than 8 bytes
	// of payload must be 8-byte aligned
	struct sk_buff *tx_skbuff[TX_RING_SIZE];
	int rx_next_out;	/* The next free ring entry to receive */
	int tx_next_in;		/* The next free ring entry to send */
	int tx_next_out;	/* The last ring entry the ISR processed */
	int tx_count;		/* current # of pkts waiting to be sent in Tx ring */
	int intr_work_done;	/* number of Rx and Tx pkts processed in the isr */
	int tx_full;		/* Tx ring is full */

	mib_counters_t mib;
	struct net_device_stats stats;

	int io_size;
	int port_num;		// 0 or 1
	u32 port_offset;

	int phy_addr;		// PHY address
	u32 last_psr;		// last value of the port status register

	int options;		/* User-settable misc. driver options. */
	int drv_flags;
	spinlock_t lock;	/* Serialise access to device */
	struct mii_if_info mii_if;

	u32 msg_enable;
};

#endif /* _GT64240ETH_H */
