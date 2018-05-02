// SPDX-License-Identifier: GPL-2.0
/* Register definitions for Gemini GMAC Ethernet device driver
 *
 * Copyright (C) 2006 Storlink, Corp.
 * Copyright (C) 2008-2009 Paulius Zaleckas <paulius.zaleckas@teltonika.lt>
 * Copyright (C) 2010 Michał Mirosław <mirq-linux@rere.qmqm.pl>
 * Copytight (C) 2017 Linus Walleij <linus.walleij@linaro.org>
 */
#ifndef _GEMINI_ETHERNET_H
#define _GEMINI_ETHERNET_H

#include <linux/bitops.h>

/* Base Registers */
#define TOE_NONTOE_QUE_HDR_BASE		0x2000
#define TOE_TOE_QUE_HDR_BASE		0x3000

/* Queue ID */
#define TOE_SW_FREE_QID			0x00
#define TOE_HW_FREE_QID			0x01
#define TOE_GMAC0_SW_TXQ0_QID		0x02
#define TOE_GMAC0_SW_TXQ1_QID		0x03
#define TOE_GMAC0_SW_TXQ2_QID		0x04
#define TOE_GMAC0_SW_TXQ3_QID		0x05
#define TOE_GMAC0_SW_TXQ4_QID		0x06
#define TOE_GMAC0_SW_TXQ5_QID		0x07
#define TOE_GMAC0_HW_TXQ0_QID		0x08
#define TOE_GMAC0_HW_TXQ1_QID		0x09
#define TOE_GMAC0_HW_TXQ2_QID		0x0A
#define TOE_GMAC0_HW_TXQ3_QID		0x0B
#define TOE_GMAC1_SW_TXQ0_QID		0x12
#define TOE_GMAC1_SW_TXQ1_QID		0x13
#define TOE_GMAC1_SW_TXQ2_QID		0x14
#define TOE_GMAC1_SW_TXQ3_QID		0x15
#define TOE_GMAC1_SW_TXQ4_QID		0x16
#define TOE_GMAC1_SW_TXQ5_QID		0x17
#define TOE_GMAC1_HW_TXQ0_QID		0x18
#define TOE_GMAC1_HW_TXQ1_QID		0x19
#define TOE_GMAC1_HW_TXQ2_QID		0x1A
#define TOE_GMAC1_HW_TXQ3_QID		0x1B
#define TOE_GMAC0_DEFAULT_QID		0x20
#define TOE_GMAC1_DEFAULT_QID		0x21
#define TOE_CLASSIFICATION_QID(x)	(0x22 + x)	/* 0x22 ~ 0x2F */
#define TOE_TOE_QID(x)			(0x40 + x)	/* 0x40 ~ 0x7F */

/* TOE DMA Queue Size should be 2^n, n = 6...12
 * TOE DMA Queues are the following queue types:
 *		SW Free Queue, HW Free Queue,
 *		GMAC 0/1 SW TX Q0-5, and GMAC 0/1 HW TX Q0-5
 * The base address and descriptor number are configured at
 * DMA Queues Descriptor Ring Base Address/Size Register (offset 0x0004)
 */
#define GET_WPTR(addr)			readw((addr) + 2)
#define GET_RPTR(addr)			readw((addr))
#define SET_WPTR(addr, data)		writew((data), (addr) + 2)
#define SET_RPTR(addr, data)		writew((data), (addr))
#define __RWPTR_NEXT(x, mask)		(((unsigned int)(x) + 1) & (mask))
#define __RWPTR_PREV(x, mask)		(((unsigned int)(x) - 1) & (mask))
#define __RWPTR_DISTANCE(r, w, mask)	(((unsigned int)(w) - (r)) & (mask))
#define __RWPTR_MASK(order)		((1 << (order)) - 1)
#define RWPTR_NEXT(x, order)		__RWPTR_NEXT((x), __RWPTR_MASK((order)))
#define RWPTR_PREV(x, order)		__RWPTR_PREV((x), __RWPTR_MASK((order)))
#define RWPTR_DISTANCE(r, w, order)	__RWPTR_DISTANCE((r), (w), \
						__RWPTR_MASK((order)))

/* Global registers */
#define GLOBAL_TOE_VERSION_REG		0x0000
#define GLOBAL_SW_FREEQ_BASE_SIZE_REG	0x0004
#define GLOBAL_HW_FREEQ_BASE_SIZE_REG	0x0008
#define GLOBAL_DMA_SKB_SIZE_REG		0x0010
#define GLOBAL_SWFQ_RWPTR_REG		0x0014
#define GLOBAL_HWFQ_RWPTR_REG		0x0018
#define GLOBAL_INTERRUPT_STATUS_0_REG	0x0020
#define GLOBAL_INTERRUPT_ENABLE_0_REG	0x0024
#define GLOBAL_INTERRUPT_SELECT_0_REG	0x0028
#define GLOBAL_INTERRUPT_STATUS_1_REG	0x0030
#define GLOBAL_INTERRUPT_ENABLE_1_REG	0x0034
#define GLOBAL_INTERRUPT_SELECT_1_REG	0x0038
#define GLOBAL_INTERRUPT_STATUS_2_REG	0x0040
#define GLOBAL_INTERRUPT_ENABLE_2_REG	0x0044
#define GLOBAL_INTERRUPT_SELECT_2_REG	0x0048
#define GLOBAL_INTERRUPT_STATUS_3_REG	0x0050
#define GLOBAL_INTERRUPT_ENABLE_3_REG	0x0054
#define GLOBAL_INTERRUPT_SELECT_3_REG	0x0058
#define GLOBAL_INTERRUPT_STATUS_4_REG	0x0060
#define GLOBAL_INTERRUPT_ENABLE_4_REG	0x0064
#define GLOBAL_INTERRUPT_SELECT_4_REG	0x0068
#define GLOBAL_HASH_TABLE_BASE_REG	0x006C
#define GLOBAL_QUEUE_THRESHOLD_REG	0x0070

/* GMAC 0/1 DMA/TOE register */
#define GMAC_DMA_CTRL_REG		0x0000
#define GMAC_TX_WEIGHTING_CTRL_0_REG	0x0004
#define GMAC_TX_WEIGHTING_CTRL_1_REG	0x0008
#define GMAC_SW_TX_QUEUE0_PTR_REG	0x000C
#define GMAC_SW_TX_QUEUE1_PTR_REG	0x0010
#define GMAC_SW_TX_QUEUE2_PTR_REG	0x0014
#define GMAC_SW_TX_QUEUE3_PTR_REG	0x0018
#define GMAC_SW_TX_QUEUE4_PTR_REG	0x001C
#define GMAC_SW_TX_QUEUE5_PTR_REG	0x0020
#define GMAC_SW_TX_QUEUE_PTR_REG(i)	(GMAC_SW_TX_QUEUE0_PTR_REG + 4 * (i))
#define GMAC_HW_TX_QUEUE0_PTR_REG	0x0024
#define GMAC_HW_TX_QUEUE1_PTR_REG	0x0028
#define GMAC_HW_TX_QUEUE2_PTR_REG	0x002C
#define GMAC_HW_TX_QUEUE3_PTR_REG	0x0030
#define GMAC_HW_TX_QUEUE_PTR_REG(i)	(GMAC_HW_TX_QUEUE0_PTR_REG + 4 * (i))
#define GMAC_DMA_TX_FIRST_DESC_REG	0x0038
#define GMAC_DMA_TX_CURR_DESC_REG	0x003C
#define GMAC_DMA_TX_DESC_WORD0_REG	0x0040
#define GMAC_DMA_TX_DESC_WORD1_REG	0x0044
#define GMAC_DMA_TX_DESC_WORD2_REG	0x0048
#define GMAC_DMA_TX_DESC_WORD3_REG	0x004C
#define GMAC_SW_TX_QUEUE_BASE_REG	0x0050
#define GMAC_HW_TX_QUEUE_BASE_REG	0x0054
#define GMAC_DMA_RX_FIRST_DESC_REG	0x0058
#define GMAC_DMA_RX_CURR_DESC_REG	0x005C
#define GMAC_DMA_RX_DESC_WORD0_REG	0x0060
#define GMAC_DMA_RX_DESC_WORD1_REG	0x0064
#define GMAC_DMA_RX_DESC_WORD2_REG	0x0068
#define GMAC_DMA_RX_DESC_WORD3_REG	0x006C
#define GMAC_HASH_ENGINE_REG0		0x0070
#define GMAC_HASH_ENGINE_REG1		0x0074
/* matching rule 0 Control register 0 */
#define GMAC_MR0CR0			0x0078
#define GMAC_MR0CR1			0x007C
#define GMAC_MR0CR2			0x0080
#define GMAC_MR1CR0			0x0084
#define GMAC_MR1CR1			0x0088
#define GMAC_MR1CR2			0x008C
#define GMAC_MR2CR0			0x0090
#define GMAC_MR2CR1			0x0094
#define GMAC_MR2CR2			0x0098
#define GMAC_MR3CR0			0x009C
#define GMAC_MR3CR1			0x00A0
#define GMAC_MR3CR2			0x00A4
/* Support Protocol Register 0 */
#define GMAC_SPR0			0x00A8
#define GMAC_SPR1			0x00AC
#define GMAC_SPR2			0x00B0
#define GMAC_SPR3			0x00B4
#define GMAC_SPR4			0x00B8
#define GMAC_SPR5			0x00BC
#define GMAC_SPR6			0x00C0
#define GMAC_SPR7			0x00C4
/* GMAC Hash/Rx/Tx AHB Weighting register */
#define GMAC_AHB_WEIGHT_REG		0x00C8

/* TOE GMAC 0/1 register */
#define GMAC_STA_ADD0			0x0000
#define GMAC_STA_ADD1			0x0004
#define GMAC_STA_ADD2			0x0008
#define GMAC_RX_FLTR			0x000c
#define GMAC_MCAST_FIL0			0x0010
#define GMAC_MCAST_FIL1			0x0014
#define GMAC_CONFIG0			0x0018
#define GMAC_CONFIG1			0x001c
#define GMAC_CONFIG2			0x0020
#define GMAC_CONFIG3			0x0024
#define GMAC_RESERVED			0x0028
#define GMAC_STATUS			0x002c
#define GMAC_IN_DISCARDS		0x0030
#define GMAC_IN_ERRORS			0x0034
#define GMAC_IN_MCAST			0x0038
#define GMAC_IN_BCAST			0x003c
#define GMAC_IN_MAC1			0x0040	/* for STA 1 MAC Address */
#define GMAC_IN_MAC2			0x0044	/* for STA 2 MAC Address */

#define RX_STATS_NUM	6

/* DMA Queues description Ring Base Address/Size Register (offset 0x0004) */
union dma_q_base_size {
	unsigned int bits32;
	unsigned int base_size;
};

#define DMA_Q_BASE_MASK		(~0x0f)

/* DMA SKB Buffer register (offset 0x0008) */
union dma_skb_size {
	unsigned int bits32;
	struct bit_0008 {
		unsigned int sw_skb_size : 16;	/* SW Free poll SKB Size */
		unsigned int hw_skb_size : 16;	/* HW Free poll SKB Size */
	} bits;
};

/* DMA SW Free Queue Read/Write Pointer Register (offset 0x000c) */
union dma_rwptr {
	unsigned int bits32;
	struct bit_000c {
		unsigned int rptr	: 16;	/* Read Ptr, RO */
		unsigned int wptr	: 16;	/* Write Ptr, RW */
	} bits;
};

/* Interrupt Status Register 0	(offset 0x0020)
 * Interrupt Mask Register 0	(offset 0x0024)
 * Interrupt Select Register 0	(offset 0x0028)
 */
#define GMAC1_TXDERR_INT_BIT		BIT(31)
#define GMAC1_TXPERR_INT_BIT		BIT(30)
#define GMAC0_TXDERR_INT_BIT		BIT(29)
#define GMAC0_TXPERR_INT_BIT		BIT(28)
#define GMAC1_RXDERR_INT_BIT		BIT(27)
#define GMAC1_RXPERR_INT_BIT		BIT(26)
#define GMAC0_RXDERR_INT_BIT		BIT(25)
#define GMAC0_RXPERR_INT_BIT		BIT(24)
#define GMAC1_SWTQ15_FIN_INT_BIT	BIT(23)
#define GMAC1_SWTQ14_FIN_INT_BIT	BIT(22)
#define GMAC1_SWTQ13_FIN_INT_BIT	BIT(21)
#define GMAC1_SWTQ12_FIN_INT_BIT	BIT(20)
#define GMAC1_SWTQ11_FIN_INT_BIT	BIT(19)
#define GMAC1_SWTQ10_FIN_INT_BIT	BIT(18)
#define GMAC0_SWTQ05_FIN_INT_BIT	BIT(17)
#define GMAC0_SWTQ04_FIN_INT_BIT	BIT(16)
#define GMAC0_SWTQ03_FIN_INT_BIT	BIT(15)
#define GMAC0_SWTQ02_FIN_INT_BIT	BIT(14)
#define GMAC0_SWTQ01_FIN_INT_BIT	BIT(13)
#define GMAC0_SWTQ00_FIN_INT_BIT	BIT(12)
#define GMAC1_SWTQ15_EOF_INT_BIT	BIT(11)
#define GMAC1_SWTQ14_EOF_INT_BIT	BIT(10)
#define GMAC1_SWTQ13_EOF_INT_BIT	BIT(9)
#define GMAC1_SWTQ12_EOF_INT_BIT	BIT(8)
#define GMAC1_SWTQ11_EOF_INT_BIT	BIT(7)
#define GMAC1_SWTQ10_EOF_INT_BIT	BIT(6)
#define GMAC0_SWTQ05_EOF_INT_BIT	BIT(5)
#define GMAC0_SWTQ04_EOF_INT_BIT	BIT(4)
#define GMAC0_SWTQ03_EOF_INT_BIT	BIT(3)
#define GMAC0_SWTQ02_EOF_INT_BIT	BIT(2)
#define GMAC0_SWTQ01_EOF_INT_BIT	BIT(1)
#define GMAC0_SWTQ00_EOF_INT_BIT	BIT(0)

/* Interrupt Status Register 1	(offset 0x0030)
 * Interrupt Mask Register 1	(offset 0x0034)
 * Interrupt Select Register 1	(offset 0x0038)
 */
#define TOE_IQ3_FULL_INT_BIT		BIT(31)
#define TOE_IQ2_FULL_INT_BIT		BIT(30)
#define TOE_IQ1_FULL_INT_BIT		BIT(29)
#define TOE_IQ0_FULL_INT_BIT		BIT(28)
#define TOE_IQ3_INT_BIT			BIT(27)
#define TOE_IQ2_INT_BIT			BIT(26)
#define TOE_IQ1_INT_BIT			BIT(25)
#define TOE_IQ0_INT_BIT			BIT(24)
#define GMAC1_HWTQ13_EOF_INT_BIT	BIT(23)
#define GMAC1_HWTQ12_EOF_INT_BIT	BIT(22)
#define GMAC1_HWTQ11_EOF_INT_BIT	BIT(21)
#define GMAC1_HWTQ10_EOF_INT_BIT	BIT(20)
#define GMAC0_HWTQ03_EOF_INT_BIT	BIT(19)
#define GMAC0_HWTQ02_EOF_INT_BIT	BIT(18)
#define GMAC0_HWTQ01_EOF_INT_BIT	BIT(17)
#define GMAC0_HWTQ00_EOF_INT_BIT	BIT(16)
#define CLASS_RX_INT_BIT(x)		BIT((x + 2))
#define DEFAULT_Q1_INT_BIT		BIT(1)
#define DEFAULT_Q0_INT_BIT		BIT(0)

#define TOE_IQ_INT_BITS		(TOE_IQ0_INT_BIT | TOE_IQ1_INT_BIT | \
				 TOE_IQ2_INT_BIT | TOE_IQ3_INT_BIT)
#define	TOE_IQ_FULL_BITS	(TOE_IQ0_FULL_INT_BIT | TOE_IQ1_FULL_INT_BIT | \
				 TOE_IQ2_FULL_INT_BIT | TOE_IQ3_FULL_INT_BIT)
#define	TOE_IQ_ALL_BITS		(TOE_IQ_INT_BITS | TOE_IQ_FULL_BITS)
#define TOE_CLASS_RX_INT_BITS	0xfffc

/* Interrupt Status Register 2	(offset 0x0040)
 * Interrupt Mask Register 2	(offset 0x0044)
 * Interrupt Select Register 2	(offset 0x0048)
 */
#define TOE_QL_FULL_INT_BIT(x)		BIT(x)

/* Interrupt Status Register 3	(offset 0x0050)
 * Interrupt Mask Register 3	(offset 0x0054)
 * Interrupt Select Register 3	(offset 0x0058)
 */
#define TOE_QH_FULL_INT_BIT(x)		BIT(x - 32)

/* Interrupt Status Register 4	(offset 0x0060)
 * Interrupt Mask Register 4	(offset 0x0064)
 * Interrupt Select Register 4	(offset 0x0068)
 */
#define GMAC1_RESERVED_INT_BIT		BIT(31)
#define GMAC1_MIB_INT_BIT		BIT(30)
#define GMAC1_RX_PAUSE_ON_INT_BIT	BIT(29)
#define GMAC1_TX_PAUSE_ON_INT_BIT	BIT(28)
#define GMAC1_RX_PAUSE_OFF_INT_BIT	BIT(27)
#define GMAC1_TX_PAUSE_OFF_INT_BIT	BIT(26)
#define GMAC1_RX_OVERRUN_INT_BIT	BIT(25)
#define GMAC1_STATUS_CHANGE_INT_BIT	BIT(24)
#define GMAC0_RESERVED_INT_BIT		BIT(23)
#define GMAC0_MIB_INT_BIT		BIT(22)
#define GMAC0_RX_PAUSE_ON_INT_BIT	BIT(21)
#define GMAC0_TX_PAUSE_ON_INT_BIT	BIT(20)
#define GMAC0_RX_PAUSE_OFF_INT_BIT	BIT(19)
#define GMAC0_TX_PAUSE_OFF_INT_BIT	BIT(18)
#define GMAC0_RX_OVERRUN_INT_BIT	BIT(17)
#define GMAC0_STATUS_CHANGE_INT_BIT	BIT(16)
#define CLASS_RX_FULL_INT_BIT(x)	BIT(x + 2)
#define HWFQ_EMPTY_INT_BIT		BIT(1)
#define SWFQ_EMPTY_INT_BIT		BIT(0)

#define GMAC0_INT_BITS	(GMAC0_RESERVED_INT_BIT | GMAC0_MIB_INT_BIT | \
			 GMAC0_RX_PAUSE_ON_INT_BIT | \
			 GMAC0_TX_PAUSE_ON_INT_BIT | \
			 GMAC0_RX_PAUSE_OFF_INT_BIT | \
			 GMAC0_TX_PAUSE_OFF_INT_BIT | \
			 GMAC0_RX_OVERRUN_INT_BIT | \
			 GMAC0_STATUS_CHANGE_INT_BIT)
#define GMAC1_INT_BITS	(GMAC1_RESERVED_INT_BIT | GMAC1_MIB_INT_BIT | \
			 GMAC1_RX_PAUSE_ON_INT_BIT | \
			 GMAC1_TX_PAUSE_ON_INT_BIT | \
			 GMAC1_RX_PAUSE_OFF_INT_BIT | \
			 GMAC1_TX_PAUSE_OFF_INT_BIT | \
			 GMAC1_RX_OVERRUN_INT_BIT | \
			 GMAC1_STATUS_CHANGE_INT_BIT)

#define CLASS_RX_FULL_INT_BITS		0xfffc

/* GLOBAL_QUEUE_THRESHOLD_REG	(offset 0x0070) */
union queue_threshold {
	unsigned int bits32;
	struct bit_0070_2 {
		/*  7:0 Software Free Queue Empty Threshold */
		unsigned int swfq_empty:8;
		/* 15:8 Hardware Free Queue Empty Threshold */
		unsigned int hwfq_empty:8;
		/* 23:16 */
		unsigned int intrq:8;
		/* 31:24 */
		unsigned int toe_class:8;
	} bits;
};

/* GMAC DMA Control Register
 * GMAC0 offset 0x8000
 * GMAC1 offset 0xC000
 */
union gmac_dma_ctrl {
	unsigned int bits32;
	struct bit_8000 {
		/* bit 1:0 Peripheral Bus Width */
		unsigned int td_bus:2;
		/* bit 3:2 TxDMA max burst size for every AHB request */
		unsigned int td_burst_size:2;
		/* bit 7:4 TxDMA protection control */
		unsigned int td_prot:4;
		/* bit 9:8 Peripheral Bus Width */
		unsigned int rd_bus:2;
		/* bit 11:10 DMA max burst size for every AHB request */
		unsigned int rd_burst_size:2;
		/* bit 15:12 DMA Protection Control */
		unsigned int rd_prot:4;
		/* bit 17:16 */
		unsigned int rd_insert_bytes:2;
		/* bit 27:18 */
		unsigned int reserved:10;
		/* bit 28 1: Drop, 0: Accept */
		unsigned int drop_small_ack:1;
		/* bit 29 Loopback TxDMA to RxDMA */
		unsigned int loopback:1;
		/* bit 30 Tx DMA Enable */
		unsigned int td_enable:1;
		/* bit 31 Rx DMA Enable */
		unsigned int rd_enable:1;
	} bits;
};

/* GMAC Tx Weighting Control Register 0
 * GMAC0 offset 0x8004
 * GMAC1 offset 0xC004
 */
union gmac_tx_wcr0 {
	unsigned int bits32;
	struct bit_8004 {
		/* bit 5:0 HW TX Queue 3 */
		unsigned int hw_tq0:6;
		/* bit 11:6 HW TX Queue 2 */
		unsigned int hw_tq1:6;
		/* bit 17:12 HW TX Queue 1 */
		unsigned int hw_tq2:6;
		/* bit 23:18 HW TX Queue 0 */
		unsigned int hw_tq3:6;
		/* bit 31:24 */
		unsigned int reserved:8;
	} bits;
};

/* GMAC Tx Weighting Control Register 1
 * GMAC0 offset 0x8008
 * GMAC1 offset 0xC008
 */
union gmac_tx_wcr1 {
	unsigned int bits32;
	struct bit_8008 {
		/* bit 4:0 SW TX Queue 0 */
		unsigned int sw_tq0:5;
		/* bit 9:5 SW TX Queue 1 */
		unsigned int sw_tq1:5;
		/* bit 14:10 SW TX Queue 2 */
		unsigned int sw_tq2:5;
		/* bit 19:15 SW TX Queue 3 */
		unsigned int sw_tq3:5;
		/* bit 24:20 SW TX Queue 4 */
		unsigned int sw_tq4:5;
		/* bit 29:25 SW TX Queue 5 */
		unsigned int sw_tq5:5;
		/* bit 31:30 */
		unsigned int reserved:2;
	} bits;
};

/* GMAC DMA Tx Description Word 0 Register
 * GMAC0 offset 0x8040
 * GMAC1 offset 0xC040
 */
union gmac_txdesc_0 {
	unsigned int bits32;
	struct bit_8040 {
		/* bit 15:0 Transfer size */
		unsigned int buffer_size:16;
		/* bit 21:16 number of descriptors used for the current frame */
		unsigned int desc_count:6;
		/* bit 22 Tx Status, 1: Successful 0: Failed */
		unsigned int status_tx_ok:1;
		/* bit 28:23 Tx Status, Reserved bits */
		unsigned int status_rvd:6;
		/* bit 29 protocol error during processing this descriptor */
		unsigned int perr:1;
		/* bit 30 data error during processing this descriptor */
		unsigned int derr:1;
		/* bit 31 */
		unsigned int reserved:1;
	} bits;
};

/* GMAC DMA Tx Description Word 1 Register
 * GMAC0 offset 0x8044
 * GMAC1 offset 0xC044
 */
union gmac_txdesc_1 {
	unsigned int bits32;
	struct txdesc_word1 {
		/* bit 15: 0 Tx Frame Byte Count */
		unsigned int byte_count:16;
		/* bit 16 TSS segmentation use MTU setting */
		unsigned int mtu_enable:1;
		/* bit 17 IPV4 Header Checksum Enable */
		unsigned int ip_chksum:1;
		/* bit 18 IPV6 Tx Enable */
		unsigned int ipv6_enable:1;
		/* bit 19 TCP Checksum Enable */
		unsigned int tcp_chksum:1;
		/* bit 20 UDP Checksum Enable */
		unsigned int udp_chksum:1;
		/* bit 21 Bypass HW offload engine */
		unsigned int bypass_tss:1;
		/* bit 22 Don't update IP length field */
		unsigned int ip_fixed_len:1;
		/* bit 31:23 Tx Flag, Reserved */
		unsigned int reserved:9;
	} bits;
};

#define TSS_IP_FIXED_LEN_BIT	BIT(22)
#define TSS_BYPASS_BIT		BIT(21)
#define TSS_UDP_CHKSUM_BIT	BIT(20)
#define TSS_TCP_CHKSUM_BIT	BIT(19)
#define TSS_IPV6_ENABLE_BIT	BIT(18)
#define TSS_IP_CHKSUM_BIT	BIT(17)
#define TSS_MTU_ENABLE_BIT	BIT(16)

#define TSS_CHECKUM_ENABLE	\
	(TSS_IP_CHKSUM_BIT | TSS_IPV6_ENABLE_BIT | \
	 TSS_TCP_CHKSUM_BIT | TSS_UDP_CHKSUM_BIT)

/* GMAC DMA Tx Description Word 2 Register
 * GMAC0 offset 0x8048
 * GMAC1 offset 0xC048
 */
union gmac_txdesc_2 {
	unsigned int	bits32;
	unsigned int	buf_adr;
};

/* GMAC DMA Tx Description Word 3 Register
 * GMAC0 offset 0x804C
 * GMAC1 offset 0xC04C
 */
union gmac_txdesc_3 {
	unsigned int bits32;
	struct txdesc_word3 {
		/* bit 12: 0 Tx Frame Byte Count */
		unsigned int mtu_size:13;
		/* bit 28:13 */
		unsigned int reserved:16;
		/* bit 29 End of frame interrupt enable */
		unsigned int eofie:1;
		/* bit 31:30 11: only one, 10: first, 01: last, 00: linking */
		unsigned int sof_eof:2;
	} bits;
};

#define SOF_EOF_BIT_MASK	0x3fffffff
#define SOF_BIT			0x80000000
#define EOF_BIT			0x40000000
#define EOFIE_BIT		BIT(29)
#define MTU_SIZE_BIT_MASK	0x1fff

/* GMAC Tx Descriptor */
struct gmac_txdesc {
	union gmac_txdesc_0 word0;
	union gmac_txdesc_1 word1;
	union gmac_txdesc_2 word2;
	union gmac_txdesc_3 word3;
};

/* GMAC DMA Rx Description Word 0 Register
 * GMAC0 offset 0x8060
 * GMAC1 offset 0xC060
 */
union gmac_rxdesc_0 {
	unsigned int bits32;
	struct bit_8060 {
		/* bit 15:0 number of descriptors used for the current frame */
		unsigned int buffer_size:16;
		/* bit 21:16 number of descriptors used for the current frame */
		unsigned int desc_count:6;
		/* bit 24:22 Status of rx frame */
		unsigned int status:4;
		/* bit 28:26 Check Sum Status */
		unsigned int chksum_status:3;
		/* bit 29 protocol error during processing this descriptor */
		unsigned int perr:1;
		/* bit 30 data error during processing this descriptor */
		unsigned int derr:1;
		/* bit 31 TOE/CIS Queue Full dropped packet to default queue */
		unsigned int drop:1;
	} bits;
};

#define	GMAC_RXDESC_0_T_derr			BIT(30)
#define	GMAC_RXDESC_0_T_perr			BIT(29)
#define	GMAC_RXDESC_0_T_chksum_status(x)	BIT(x + 26)
#define	GMAC_RXDESC_0_T_status(x)		BIT(x + 22)
#define	GMAC_RXDESC_0_T_desc_count(x)		BIT(x + 16)

#define	RX_CHKSUM_IP_UDP_TCP_OK			0
#define	RX_CHKSUM_IP_OK_ONLY			1
#define	RX_CHKSUM_NONE				2
#define	RX_CHKSUM_IP_ERR_UNKNOWN		4
#define	RX_CHKSUM_IP_ERR			5
#define	RX_CHKSUM_TCP_UDP_ERR			6
#define RX_CHKSUM_NUM				8

#define RX_STATUS_GOOD_FRAME			0
#define RX_STATUS_TOO_LONG_GOOD_CRC		1
#define RX_STATUS_RUNT_FRAME			2
#define RX_STATUS_SFD_NOT_FOUND			3
#define RX_STATUS_CRC_ERROR			4
#define RX_STATUS_TOO_LONG_BAD_CRC		5
#define RX_STATUS_ALIGNMENT_ERROR		6
#define RX_STATUS_TOO_LONG_BAD_ALIGN		7
#define RX_STATUS_RX_ERR			8
#define RX_STATUS_DA_FILTERED			9
#define RX_STATUS_BUFFER_FULL			10
#define RX_STATUS_NUM				16

#define RX_ERROR_LENGTH(s) \
	((s) == RX_STATUS_TOO_LONG_GOOD_CRC || \
	 (s) == RX_STATUS_TOO_LONG_BAD_CRC || \
	 (s) == RX_STATUS_TOO_LONG_BAD_ALIGN)
#define RX_ERROR_OVER(s) \
	((s) == RX_STATUS_BUFFER_FULL)
#define RX_ERROR_CRC(s) \
	((s) == RX_STATUS_CRC_ERROR || \
	 (s) == RX_STATUS_TOO_LONG_BAD_CRC)
#define RX_ERROR_FRAME(s) \
	((s) == RX_STATUS_ALIGNMENT_ERROR || \
	 (s) == RX_STATUS_TOO_LONG_BAD_ALIGN)
#define RX_ERROR_FIFO(s) \
	(0)

/* GMAC DMA Rx Description Word 1 Register
 * GMAC0 offset 0x8064
 * GMAC1 offset 0xC064
 */
union gmac_rxdesc_1 {
	unsigned int bits32;
	struct rxdesc_word1 {
		/* bit 15: 0 Rx Frame Byte Count */
		unsigned int byte_count:16;
		/* bit 31:16 Software ID */
		unsigned int sw_id:16;
	} bits;
};

/* GMAC DMA Rx Description Word 2 Register
 * GMAC0 offset 0x8068
 * GMAC1 offset 0xC068
 */
union gmac_rxdesc_2 {
	unsigned int	bits32;
	unsigned int	buf_adr;
};

#define RX_INSERT_NONE		0
#define RX_INSERT_1_BYTE	1
#define RX_INSERT_2_BYTE	2
#define RX_INSERT_3_BYTE	3

/* GMAC DMA Rx Description Word 3 Register
 * GMAC0 offset 0x806C
 * GMAC1 offset 0xC06C
 */
union gmac_rxdesc_3 {
	unsigned int bits32;
	struct rxdesc_word3 {
		/* bit 7: 0 L3 data offset */
		unsigned int l3_offset:8;
		/* bit 15: 8 L4 data offset */
		unsigned int l4_offset:8;
		/* bit 23: 16 L7 data offset */
		unsigned int l7_offset:8;
		/* bit 24 Duplicated ACK detected */
		unsigned int dup_ack:1;
		/* bit 25 abnormal case found */
		unsigned int abnormal:1;
		/* bit 26 IPV4 option or IPV6 extension header */
		unsigned int option:1;
		/* bit 27 Out of Sequence packet */
		unsigned int out_of_seq:1;
		/* bit 28 Control Flag is present */
		unsigned int ctrl_flag:1;
		/* bit 29 End of frame interrupt enable */
		unsigned int eofie:1;
		/* bit 31:30 11: only one, 10: first, 01: last, 00: linking */
		unsigned int sof_eof:2;
	} bits;
};

/* GMAC Rx Descriptor, this is simply fitted over the queue registers */
struct gmac_rxdesc {
	union gmac_rxdesc_0 word0;
	union gmac_rxdesc_1 word1;
	union gmac_rxdesc_2 word2;
	union gmac_rxdesc_3 word3;
};

/* GMAC Matching Rule Control Register 0
 * GMAC0 offset 0x8078
 * GMAC1 offset 0xC078
 */
#define MR_L2_BIT		BIT(31)
#define MR_L3_BIT		BIT(30)
#define MR_L4_BIT		BIT(29)
#define MR_L7_BIT		BIT(28)
#define MR_PORT_BIT		BIT(27)
#define MR_PRIORITY_BIT		BIT(26)
#define MR_DA_BIT		BIT(23)
#define MR_SA_BIT		BIT(22)
#define MR_ETHER_TYPE_BIT	BIT(21)
#define MR_VLAN_BIT		BIT(20)
#define MR_PPPOE_BIT		BIT(19)
#define MR_IP_VER_BIT		BIT(15)
#define MR_IP_HDR_LEN_BIT	BIT(14)
#define MR_FLOW_LABLE_BIT	BIT(13)
#define MR_TOS_TRAFFIC_BIT	BIT(12)
#define MR_SPR_BIT(x)		BIT(x)
#define MR_SPR_BITS		0xff

/* GMAC_AHB_WEIGHT registers
 * GMAC0 offset 0x80C8
 * GMAC1 offset 0xC0C8
 */
union gmac_ahb_weight {
	unsigned int bits32;
	struct bit_80C8 {
		/* 4:0 */
		unsigned int hash_weight:5;
		/* 9:5 */
		unsigned int rx_weight:5;
		/* 14:10 */
		unsigned int tx_weight:5;
		/* 19:15 Rx Data Pre Request FIFO Threshold */
		unsigned int pre_req:5;
		/* 24:20 DMA TqCtrl to Start tqDV FIFO Threshold */
		unsigned int tq_dv_threshold:5;
		/* 31:25 */
		unsigned int reserved:7;
	} bits;
};

/* GMAC RX FLTR
 * GMAC0 Offset 0xA00C
 * GMAC1 Offset 0xE00C
 */
union gmac_rx_fltr {
	unsigned int bits32;
	struct bit1_000c {
		/* Enable receive of unicast frames that are sent to STA
		 * address
		 */
		unsigned int unicast:1;
		/* Enable receive of multicast frames that pass multicast
		 * filter
		 */
		unsigned int multicast:1;
		/* Enable receive of broadcast frames */
		unsigned int broadcast:1;
		/* Enable receive of all frames */
		unsigned int promiscuous:1;
		/* Enable receive of all error frames */
		unsigned int error:1;
		unsigned int reserved:27;
	} bits;
};

/* GMAC Configuration 0
 * GMAC0 Offset 0xA018
 * GMAC1 Offset 0xE018
 */
union gmac_config0 {
	unsigned int bits32;
	struct bit1_0018 {
		/* 0: disable transmit */
		unsigned int dis_tx:1;
		/* 1: disable receive */
		unsigned int dis_rx:1;
		/* 2: transmit data loopback enable */
		unsigned int loop_back:1;
		/* 3: flow control also trigged by Rx queues */
		unsigned int flow_ctrl:1;
		/* 4-7: adjust IFG from 96+/-56 */
		unsigned int adj_ifg:4;
		/* 8-10 maximum receive frame length allowed */
		unsigned int max_len:3;
		/* 11: disable back-off function */
		unsigned int dis_bkoff:1;
		/* 12: disable 16 collisions abort function */
		unsigned int dis_col:1;
		/* 13: speed up timers in simulation */
		unsigned int sim_test:1;
		/* 14: RX flow control enable */
		unsigned int rx_fc_en:1;
		/* 15: TX flow control enable */
		unsigned int tx_fc_en:1;
		/* 16: RGMII in-band status enable */
		unsigned int rgmii_en:1;
		/* 17: IPv4 RX Checksum enable */
		unsigned int ipv4_rx_chksum:1;
		/* 18: IPv6 RX Checksum enable */
		unsigned int ipv6_rx_chksum:1;
		/* 19: Remove Rx VLAN tag */
		unsigned int rx_tag_remove:1;
		/* 20 */
		unsigned int rgmm_edge:1;
		/* 21 */
		unsigned int rxc_inv:1;
		/* 22 */
		unsigned int ipv6_exthdr_order:1;
		/* 23 */
		unsigned int rx_err_detect:1;
		/* 24 */
		unsigned int port0_chk_hwq:1;
		/* 25 */
		unsigned int port1_chk_hwq:1;
		/* 26 */
		unsigned int port0_chk_toeq:1;
		/* 27 */
		unsigned int port1_chk_toeq:1;
		/* 28 */
		unsigned int port0_chk_classq:1;
		/* 29 */
		unsigned int port1_chk_classq:1;
		/* 30, 31 */
		unsigned int reserved:2;
	} bits;
};

#define CONFIG0_TX_RX_DISABLE	(BIT(1) | BIT(0))
#define CONFIG0_RX_CHKSUM	(BIT(18) | BIT(17))
#define CONFIG0_FLOW_RX		BIT(14)
#define CONFIG0_FLOW_TX		BIT(15)
#define CONFIG0_FLOW_TX_RX	(BIT(14) | BIT(15))
#define CONFIG0_FLOW_CTL	(BIT(14) | BIT(15))

#define CONFIG0_MAXLEN_SHIFT	8
#define CONFIG0_MAXLEN_MASK	(7 << CONFIG0_MAXLEN_SHIFT)
#define  CONFIG0_MAXLEN_1536	0
#define  CONFIG0_MAXLEN_1518	1
#define  CONFIG0_MAXLEN_1522	2
#define  CONFIG0_MAXLEN_1542	3
#define  CONFIG0_MAXLEN_9k	4	/* 9212 */
#define  CONFIG0_MAXLEN_10k	5	/* 10236 */
#define  CONFIG0_MAXLEN_1518__6	6
#define  CONFIG0_MAXLEN_1518__7	7

/* GMAC Configuration 1
 * GMAC0 Offset 0xA01C
 * GMAC1 Offset 0xE01C
 */
union gmac_config1 {
	unsigned int bits32;
	struct bit1_001c {
		/* Flow control set threshold */
		unsigned int set_threshold:8;
		/* Flow control release threshold */
		unsigned int rel_threshold:8;
		unsigned int reserved:16;
	} bits;
};

#define GMAC_FLOWCTRL_SET_MAX		32
#define GMAC_FLOWCTRL_SET_MIN		0
#define GMAC_FLOWCTRL_RELEASE_MAX	32
#define GMAC_FLOWCTRL_RELEASE_MIN	0

/* GMAC Configuration 2
 * GMAC0 Offset 0xA020
 * GMAC1 Offset 0xE020
 */
union gmac_config2 {
	unsigned int bits32;
	struct bit1_0020 {
		/* Flow control set threshold */
		unsigned int set_threshold:16;
		/* Flow control release threshold */
		unsigned int rel_threshold:16;
	} bits;
};

/* GMAC Configuration 3
 * GMAC0 Offset 0xA024
 * GMAC1 Offset 0xE024
 */
union gmac_config3 {
	unsigned int bits32;
	struct bit1_0024 {
		/* Flow control set threshold */
		unsigned int set_threshold:16;
		/* Flow control release threshold */
		unsigned int rel_threshold:16;
	} bits;
};

/* GMAC STATUS
 * GMAC0 Offset 0xA02C
 * GMAC1 Offset 0xE02C
 */
union gmac_status {
	unsigned int bits32;
	struct bit1_002c {
		/* Link status */
		unsigned int link:1;
		/* Link speed(00->2.5M 01->25M 10->125M) */
		unsigned int speed:2;
		/* Duplex mode */
		unsigned int duplex:1;
		unsigned int reserved_1:1;
		/* PHY interface type */
		unsigned int mii_rmii:2;
		unsigned int reserved_2:25;
	} bits;
};

#define GMAC_SPEED_10			0
#define GMAC_SPEED_100			1
#define GMAC_SPEED_1000			2

#define GMAC_PHY_MII			0
#define GMAC_PHY_GMII			1
#define GMAC_PHY_RGMII_100_10		2
#define GMAC_PHY_RGMII_1000		3

/* Queue Header
 *	(1) TOE Queue Header
 *	(2) Non-TOE Queue Header
 *	(3) Interrupt Queue Header
 *
 * memory Layout
 *	TOE Queue Header
 *		     0x60003000 +---------------------------+ 0x0000
 *				|     TOE Queue 0 Header    |
 *				|         8 * 4 Bytes	    |
 *				+---------------------------+ 0x0020
 *				|     TOE Queue 1 Header    |
 *				|         8 * 4 Bytes	    |
 *				+---------------------------+ 0x0040
 *				|          ......           |
 *				|                           |
 *				+---------------------------+
 *
 *	Non TOE Queue Header
 *		     0x60002000 +---------------------------+ 0x0000
 *				|   Default Queue 0 Header  |
 *				|         2 * 4 Bytes       |
 *				+---------------------------+ 0x0008
 *				|   Default Queue 1 Header  |
 *				|         2 * 4 Bytes       |
 *				+---------------------------+ 0x0010
 *				|   Classification Queue 0  |
 *				|	  2 * 4 Bytes       |
 *				+---------------------------+
 *				|   Classification Queue 1  |
 *				|	  2 * 4 Bytes       |
 *				+---------------------------+ (n * 8 + 0x10)
 *				|		...	    |
 *				|	  2 * 4 Bytes	    |
 *				+---------------------------+ (13 * 8 + 0x10)
 *				|   Classification Queue 13 |
 *				|	  2 * 4 Bytes	    |
 *				+---------------------------+ 0x80
 *				|      Interrupt Queue 0    |
 *				|	  2 * 4 Bytes	    |
 *				+---------------------------+
 *				|      Interrupt Queue 1    |
 *				|	  2 * 4 Bytes	    |
 *				+---------------------------+
 *				|      Interrupt Queue 2    |
 *				|	  2 * 4 Bytes	    |
 *				+---------------------------+
 *				|      Interrupt Queue 3    |
 *				|	  2 * 4 Bytes	    |
 *				+---------------------------+
 *
 */
#define TOE_QUEUE_HDR_ADDR(n)	(TOE_TOE_QUE_HDR_BASE + n * 32)
#define TOE_Q_HDR_AREA_END	(TOE_QUEUE_HDR_ADDR(TOE_TOE_QUEUE_MAX + 1))
#define TOE_DEFAULT_Q_HDR_BASE(x) (TOE_NONTOE_QUE_HDR_BASE + 0x08 * (x))
#define TOE_CLASS_Q_HDR_BASE	(TOE_NONTOE_QUE_HDR_BASE + 0x10)
#define TOE_INTR_Q_HDR_BASE	(TOE_NONTOE_QUE_HDR_BASE + 0x80)
#define INTERRUPT_QUEUE_HDR_ADDR(n) (TOE_INTR_Q_HDR_BASE + n * 8)
#define NONTOE_Q_HDR_AREA_END (INTERRUPT_QUEUE_HDR_ADDR(TOE_INTR_QUEUE_MAX + 1))

/* NONTOE Queue Header Word 0 */
union nontoe_qhdr0 {
	unsigned int bits32;
	unsigned int base_size;
};

#define NONTOE_QHDR0_BASE_MASK	(~0x0f)

/* NONTOE Queue Header Word 1 */
union nontoe_qhdr1 {
	unsigned int bits32;
	struct bit_nonqhdr1 {
		/* bit 15:0 */
		unsigned int rptr:16;
		/* bit 31:16 */
		unsigned int wptr:16;
	} bits;
};

/* Non-TOE Queue Header */
struct nontoe_qhdr {
	union nontoe_qhdr0 word0;
	union nontoe_qhdr1 word1;
};

#endif /* _GEMINI_ETHERNET_H */
