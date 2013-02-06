/* arch/arm/plat-s3c/include/plat/regs-tsi.h
 *
 * Copyright (c) 2004 Samsung
 *
 * This program is free software; yosu can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * S5PC110 TSI registers
*/
#ifndef __ASM_ARCH_REGS_TSI_H
#define __ASM_ARCH_REGS_TSI_H "regs-tsi.h"

#define S3C_TSIREG(x)			(x)

#define	S3C_TS_CLKCON			S3C_TSIREG(0x00)
#define	S3C_TS_CON			S3C_TSIREG(0x04)
#define	S3C_TS_SYNC			S3C_TSIREG(0x08)
#define	S3C_TS_CNT			S3C_TSIREG(0x0C)
#define	S3C_TS_BASE			S3C_TSIREG(0x10)
#define	S3C_TS_SIZE			S3C_TSIREG(0x14)
#define	S3C_TS_CADDR			S3C_TSIREG(0x18)
#define	S3C_TS_INTMASK			S3C_TSIREG(0x1C)
#define	S3C_TS_INT			S3C_TSIREG(0x20)
#define	S3C_TS_PID0			S3C_TSIREG(0x24)
#define	S3C_TS_PID1			S3C_TSIREG(0x28)
#define	S3C_TS_PID2			S3C_TSIREG(0x2C)
#define	S3C_TS_PID3			S3C_TSIREG(0x30)
#define	S3C_TS_PID4			S3C_TSIREG(0x34)
#define	S3C_TS_PID5			S3C_TSIREG(0x38)
#define	S3C_TS_PID6			S3C_TSIREG(0x3C)
#define	S3C_TS_PID7			S3C_TSIREG(0x40)
#define	S3C_TS_PID8			S3C_TSIREG(0x44)
#define	S3C_TS_PID9			S3C_TSIREG(0x48)
#define	S3C_TS_PID10			S3C_TSIREG(0x4C)
#define	S3C_TS_PID11			S3C_TSIREG(0x50)
#define	S3C_TS_PID12			S3C_TSIREG(0x54)
#define	S3C_TS_PID13			S3C_TSIREG(0x58)
#define	S3C_TS_PID14			S3C_TSIREG(0x5C)
#define	S3C_TS_PID15			S3C_TSIREG(0x60)
#define	S3C_TS_PID16			S3C_TSIREG(0x64)
#define	S3C_TS_PID17			S3C_TSIREG(0x68)
#define	S3C_TS_PID18			S3C_TSIREG(0x6C)
#define	S3C_TS_PID19			S3C_TSIREG(0x70)
#define	S3C_TS_PID20			S3C_TSIREG(0x74)
#define	S3C_TS_PID21			S3C_TSIREG(0x78)
#define	S3C_TS_PID22			S3C_TSIREG(0x7C)
#define	S3C_TS_PID23			S3C_TSIREG(0x80)
#define	S3C_TS_PID24			S3C_TSIREG(0x84)
#define	S3C_TS_PID25			S3C_TSIREG(0x88)
#define	S3C_TS_PID26			S3C_TSIREG(0x8C)
#define	S3C_TS_PID27			S3C_TSIREG(0x90)
#define	S3C_TS_PID28			S3C_TSIREG(0x94)
#define	S3C_TS_PID29			S3C_TSIREG(0x98)
#define	S3C_TS_PID30			S3C_TSIREG(0x9C)
#define	S3C_TS_PID31			S3C_TSIREG(0xA0)
#define	S3C_TS_BYTE_SWAP		S3C_TSIREG(0xBC)

#define TS_TIMEOUT_CNT_MAX		(0x00FFFFFF)
#define TS_NUM_PKT			(4)
#define TS_PKT_SIZE			47
#define TS_PKT_BUF_SIZE			(TS_PKT_SIZE*TS_NUM_PKT)
#define TSI_CLK_START			1
#define TSI_CLK_STOP			0




/*bit definitions*/
/*CLKCON*/

#define S3C_TSI_ON			(0x1<<0)
#define S3C_TSI_ON_MASK			(0x1<<0)
#define S3C_TSI_BLK_READY		(0x1<<1)

/*TS_CON*/
#define S3C_TSI_SWRESET			(0x1 << 31)
#define S3C_TSI_SWRESET_MASK		(0x1 << 31)
#define S3C_TSI_CLKFILTER_ON		(0x1 << 30)
#define S3C_TSI_CLKFILTER_MASK		(0x1 << 30)
#define S3C_TSI_CLKFILTER_SHIFT		30
#define S3C_TSI_BURST_LEN_0		(0x0 << 28)
#define S3C_TSI_BURST_LEN_4		(0x1 << 28)
#define S3C_TSI_BURST_LEN_8		(0x2 << 28)
#define S3C_TSI_BURST_LEN_MASK		(0x3 << 28)
#define S3C_TSI_BURST_LEN_SHIFT		(28)

#define S3C_TSI_OUT_BUF_FULL_INT_ENA	(0x1 << 27)
#define S3C_TSI_OUT_BUF_FULL_INT_MASK	(0x1 << 27)
#define S3C_TSI_INT_FIFO_FULL_INT_ENA   (0x1 << 26)
#define S3C_TSI_INT_FIFO_FULL_INT_ENA_MASK   (0x1 << 26)

#define S3C_TSI_SYNC_MISMATCH_INT_SKIP	(0x2 << 24)
#define S3C_TSI_SYNC_MISMATCH_INT_STOP	(0x3 << 24)
#define S3C_TSI_SYNC_MISMATCH_INT_MASK	(0x3 << 24)

#define S3C_TSI_PSUF_INT_SKIP		(0x2 << 22)
#define S3C_TSI_PSUF_INT_STOP		(0x3 << 22)
#define S3C_TSI_PSUF_INT_MASK		(0x3 << 22)

#define S3C_TSI_PSOF_INT_SKIP		(0x2 << 20)
#define S3C_TSI_PSOF_INT_STOP		(0x3 << 20)
#define S3C_TSI_PSOF_INT_MASK		(0x3 << 20)

#define S3C_TSI_TS_CLK_TIME_OUT_INT	(0x1 << 19)
#define S3C_TSI_TS_CLK_TIME_OUT_INT_MASK (0x1 << 19)

#define S3C_TSI_TS_ERROR_SKIP_SIZE_INT		(4<<16)
#define S3C_TSI_TS_ERROR_STOP_SIZE_INT		(5<<16)
#define S3C_TSI_TS_ERROR_SKIP_PKT_INT		(6<<16)
#define S3C_TSI_TS_ERROR_STOP_PKT_INT		(7<<16)
#define S3C_TSI_TS_ERROR_MASK			(7<<16)
#define S3C_TSI_PAD_PATTERN_SHIFT		(8)
#define S3C_TSI_PID_FILTER_ENA			(1 << 7)
#define S3C_TSI_PID_FILTER_MASK			(1 << 7)
#define S3C_TSI_PID_FILTER_SHIFT		(7)
#define S3C_TSI_ERROR_ACTIVE_LOW		(1<<6)
#define S3C_TSI_ERROR_ACTIVE_HIGH		(0<<6)
#define S3C_TSI_ERROR_ACTIVE_MASK		(1<<6)

#define S3C_TSI_DATA_BYTE_ORDER_M2L		(0 << 5)
#define S3C_TSI_DATA_BYTE_ORDER_L2M		(1 << 5)
#define S3C_TSI_DATA_BYTE_ORDER_MASK		(1 << 5)
#define S3C_TSI_DATA_BYTE_ORDER_SHIFT		(5)
#define S3C_TSI_TS_VALID_ACTIVE_HIGH		(0<<4)
#define S3C_TSI_TS_VALID_ACTIVE_LOW		(1<<4)
#define S3C_TSI_TS_VALID_ACTIVE_MASK		(1<<4)

#define S3C_TSI_SYNC_ACTIVE_HIGH		(0 << 3)
#define S3C_TSI_SYNC_ACTIVE_LOW			(1 << 3)
#define S3C_TSI_SYNC_ACTIVE_MASK		(1 << 3)

#define S3C_TSI_CLK_INVERT_HIGH			(0 << 2)
#define S3C_TSI_CLK_INVERT_LOW			(1 << 2)
#define S3C_TSI_CLK_INVERT_MASK			(1 << 2)

/*TS_SYNC*/
#define S3C_TSI_SYNC_DET_MODE_TS_SYNC8			(0<<0)
#define S3C_TSI_SYNC_DET_MODE_TS_SYNC1			(1<<0)
#define S3C_TSI_SYNC_DET_MODE_TS_SYNC_BYTE		(2<<0)
#define S3C_TSI_SYNC_DET_MODE_TS_SYNC_MASK		(3<<0)


/* TS_INT_MASK */
#define S3C_TSI_DMA_COMPLETE_ENA			(1 << 7)
#define S3C_TSI_OUTPUT_BUF_FULL_ENA			(1 << 6)
#define S3C_TSI_INT_FIFO_FULL_ENA			(1 << 5)
#define S3C_TSI_SYNC_MISMATCH_ENA			(1 << 4)
#define S3C_TSI_PKT_SIZE_UNDERFLOW_ENA			(1 << 3)
#define S3C_TSI_PKT_SIZE_OVERFLOW_ENA			(1 << 2)
#define S3C_TSI_TS_CLK_ENA				(1 << 1)
#define S3C_TSI_TS_ERROR_ENA				(1 << 0)

/* TS_INT_FLAG */
#define S3C_TSI_DMA_COMPLETE				(1<<7)
#define S3C_TSI_OUT_BUF_FULL				(1<<6)
#define S3C_TSI_INT_FIFO_FULL				(1<<5)
#define S3C_TSI_SYNC_MISMATCH				(1<<4)
#define S3C_TSI_PKT_UNDERFLOW				(1<<3)
#define S3C_TSI_PKT_OVERFLOW				(1<<2)
#define S3C_TSI_PKT_CLK					(1<<1)
#define S3C_TSI_ERROR					(1<<0)
#endif /* __ASM_ARCH_REGS_TSI_H */
