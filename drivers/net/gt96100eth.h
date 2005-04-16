/*
 * Copyright 2000 MontaVista Software Inc.
 * Author: MontaVista Software, Inc.
 *         	stevel@mvista.com or source@mvista.com
 *
 * ########################################################################
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
 * ########################################################################
 *
 * Ethernet driver definitions for the MIPS GT96100 Advanced
 * Communication Controller.
 * 
 */
#ifndef _GT96100ETH_H
#define _GT96100ETH_H

#include <linux/config.h>
#include <asm/galileo-boards/gt96100.h>

#define dbg(lvl, format, arg...) \
    if (lvl <= GT96100_DEBUG) \
        printk(KERN_DEBUG "%s: " format, dev->name , ## arg)
#define err(format, arg...) \
    printk(KERN_ERR "%s: " format, dev->name , ## arg)
#define info(format, arg...) \
    printk(KERN_INFO "%s: " format, dev->name , ## arg)
#define warn(format, arg...) \
    printk(KERN_WARNING "%s: " format, dev->name , ## arg)

/* Keep the ring sizes a power of two for efficiency. */
#define TX_RING_SIZE	16
#define RX_RING_SIZE	32
#define PKT_BUF_SZ	1536	/* Size of each temporary Rx buffer.*/

#define RX_HASH_TABLE_SIZE 16384
#define HASH_HOP_NUMBER 12

#define NUM_INTERFACES 2

#define GT96100ETH_TX_TIMEOUT HZ/4

#define GT96100_ETH0_BASE (MIPS_GT96100_BASE + GT96100_ETH_PORT_CONFIG)
#define GT96100_ETH1_BASE (GT96100_ETH0_BASE + GT96100_ETH_IO_SIZE)

#ifdef CONFIG_MIPS_EV96100
#define GT96100_ETHER0_IRQ 3
#define GT96100_ETHER1_IRQ 4
#else
#define GT96100_ETHER0_IRQ -1
#define GT96100_ETHER1_IRQ -1
#endif

#define REV_GT96100  1
#define REV_GT96100A_1 2
#define REV_GT96100A 3

#define GT96100ETH_READ(gp, offset) \
    GT96100_READ((gp->port_offset + offset))

#define GT96100ETH_WRITE(gp, offset, data) \
    GT96100_WRITE((gp->port_offset + offset), data)

#define GT96100ETH_SETBIT(gp, offset, bits) {\
    u32 val = GT96100ETH_READ(gp, offset); val |= (u32)(bits); \
    GT96100ETH_WRITE(gp, offset, val); }

#define GT96100ETH_CLRBIT(gp, offset, bits) {\
    u32 val = GT96100ETH_READ(gp, offset); val &= (u32)(~(bits)); \
    GT96100ETH_WRITE(gp, offset, val); }


/* Bit definitions of the SMI Reg */
enum {
	smirDataMask = 0xffff,
	smirPhyAdMask = 0x1f<<16,
	smirPhyAdBit = 16,
	smirRegAdMask = 0x1f<<21,
	smirRegAdBit = 21,
	smirOpCode = 1<<26,
	smirReadValid = 1<<27,
	smirBusy = 1<<28
};

/* Bit definitions of the Port Config Reg */
enum pcr_bits {
	pcrPM = 1,
	pcrRBM = 2,
	pcrPBF = 4,
	pcrEN = 1<<7,
	pcrLPBKMask = 0x3<<8,
	pcrLPBKBit = 8,
	pcrFC = 1<<10,
	pcrHS = 1<<12,
	pcrHM = 1<<13,
	pcrHDM = 1<<14,
	pcrHD = 1<<15,
	pcrISLMask = 0x7<<28,
	pcrISLBit = 28,
	pcrACCS = 1<<31
};

/* Bit definitions of the Port Config Extend Reg */
enum pcxr_bits {
	pcxrIGMP = 1,
	pcxrSPAN = 2,
	pcxrPAR = 4,
	pcxrPRIOtxMask = 0x7<<3,
	pcxrPRIOtxBit = 3,
	pcxrPRIOrxMask = 0x3<<6,
	pcxrPRIOrxBit = 6,
	pcxrPRIOrxOverride = 1<<8,
	pcxrDPLXen = 1<<9,
	pcxrFCTLen = 1<<10,
	pcxrFLP = 1<<11,
	pcxrFCTL = 1<<12,
	pcxrMFLMask = 0x3<<14,
	pcxrMFLBit = 14,
	pcxrMIBclrMode = 1<<16,
	pcxrSpeed = 1<<18,
	pcxrSpeeden = 1<<19,
	pcxrRMIIen = 1<<20,
	pcxrDSCPen = 1<<21
};

/* Bit definitions of the Port Command Reg */
enum pcmr_bits {
	pcmrFJ = 1<<15
};


/* Bit definitions of the Port Status Reg */
enum psr_bits {
	psrSpeed = 1,
	psrDuplex = 2,
	psrFctl = 4,
	psrLink = 8,
	psrPause = 1<<4,
	psrTxLow = 1<<5,
	psrTxHigh = 1<<6,
	psrTxInProg = 1<<7
};

/* Bit definitions of the SDMA Config Reg */
enum sdcr_bits {
	sdcrRCMask = 0xf<<2,
	sdcrRCBit = 2,
	sdcrBLMR = 1<<6,
	sdcrBLMT = 1<<7,
	sdcrPOVR = 1<<8,
	sdcrRIFB = 1<<9,
	sdcrBSZMask = 0x3<<12,
	sdcrBSZBit = 12
};

/* Bit definitions of the SDMA Command Reg */
enum sdcmr_bits {
	sdcmrERD = 1<<7,
	sdcmrAR = 1<<15,
	sdcmrSTDH = 1<<16,
	sdcmrSTDL = 1<<17,
	sdcmrTXDH = 1<<23,
	sdcmrTXDL = 1<<24,
	sdcmrAT = 1<<31
};

/* Bit definitions of the Interrupt Cause Reg */
enum icr_bits {
	icrRxBuffer = 1,
	icrTxBufferHigh = 1<<2,
	icrTxBufferLow = 1<<3,
	icrTxEndHigh = 1<<6,
	icrTxEndLow = 1<<7,
	icrRxError = 1<<8,
	icrTxErrorHigh = 1<<10,
	icrTxErrorLow = 1<<11,
	icrRxOVR = 1<<12,
	icrTxUdr = 1<<13,
	icrRxBufferQ0 = 1<<16,
	icrRxBufferQ1 = 1<<17,
	icrRxBufferQ2 = 1<<18,
	icrRxBufferQ3 = 1<<19,
	icrRxErrorQ0 = 1<<20,
	icrRxErrorQ1 = 1<<21,
	icrRxErrorQ2 = 1<<22,
	icrRxErrorQ3 = 1<<23,
	icrMIIPhySTC = 1<<28,
	icrSMIdone = 1<<29,
	icrEtherIntSum = 1<<31
};


/* The Rx and Tx descriptor lists. */
typedef struct {
#ifdef DESC_BE
	u16 byte_cnt;
	u16 reserved;
#else
	u16 reserved;
	u16 byte_cnt;
#endif
	u32 cmdstat;
	u32 next;
	u32 buff_ptr;
} gt96100_td_t __attribute__ ((packed));

typedef struct {
#ifdef DESC_BE
	u16 buff_sz;
	u16 byte_cnt;
#else
	u16 byte_cnt;
	u16 buff_sz;
#endif
	u32 cmdstat;
	u32 next;
	u32 buff_ptr;
} gt96100_rd_t __attribute__ ((packed));


/* Values for the Tx command-status descriptor entry. */
enum td_cmdstat {
	txOwn = 1<<31,
	txAutoMode = 1<<30,
	txEI = 1<<23,
	txGenCRC = 1<<22,
	txPad = 1<<18,
	txFirst = 1<<17,
	txLast = 1<<16,
	txErrorSummary = 1<<15,
	txReTxCntMask = 0x0f<<10,
	txReTxCntBit = 10,
	txCollision = 1<<9,
	txReTxLimit = 1<<8,
	txUnderrun = 1<<6,
	txLateCollision = 1<<5
};


/* Values for the Rx command-status descriptor entry. */
enum rd_cmdstat {
	rxOwn = 1<<31,
	rxAutoMode = 1<<30,
	rxEI = 1<<23,
	rxFirst = 1<<17,
	rxLast = 1<<16,
	rxErrorSummary = 1<<15,
	rxIGMP = 1<<14,
	rxHashExpired = 1<<13,
	rxMissedFrame = 1<<12,
	rxFrameType = 1<<11,
	rxShortFrame = 1<<8,
	rxMaxFrameLen = 1<<7,
	rxOverrun = 1<<6,
	rxCollision = 1<<4,
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


struct gt96100_private {
	gt96100_rd_t* rx_ring;
	gt96100_td_t* tx_ring;
	// The Rx and Tx rings must be 16-byte aligned
	dma_addr_t rx_ring_dma;
	dma_addr_t tx_ring_dma;
	char* hash_table;
	// The Hash Table must be 8-byte aligned
	dma_addr_t hash_table_dma;
	int hash_mode;
    
	// The Rx buffers must be 8-byte aligned
	char* rx_buff;
	dma_addr_t rx_buff_dma;
	// Tx buffers (tx_skbuff[i]->data) with less than 8 bytes
	// of payload must be 8-byte aligned
	struct sk_buff* tx_skbuff[TX_RING_SIZE];
	int rx_next_out; /* The next free ring entry to receive */
	int tx_next_in;	 /* The next free ring entry to send */
	int tx_next_out; /* The last ring entry the ISR processed */
	int tx_count;    /* current # of pkts waiting to be sent in Tx ring */
	int intr_work_done; /* number of Rx and Tx pkts processed in the isr */
	int tx_full;        /* Tx ring is full */
    
	mib_counters_t mib;
	struct net_device_stats stats;

	int io_size;
	int port_num;  // 0 or 1
	int chip_rev;
	u32 port_offset;
    
	int phy_addr; // PHY address
	u32 last_psr; // last value of the port status register

	int options;     /* User-settable misc. driver options. */
	int drv_flags;
	struct timer_list timer;
	spinlock_t lock; /* Serialise access to device */
};

#endif
