/* mach/dma.h - arch-specific DMA defines
 *
 * Copyright 2004-2008 Analog Devices Inc.
 *
 * Licensed under the GPL-2 or later.
 */

#ifndef _MACH_DMA_H_
#define _MACH_DMA_H_

#define MAX_DMA_CHANNELS 36

/* [#4267] IMDMA channels have no PERIPHERAL_MAP MMR */
#define MAX_DMA_SUSPEND_CHANNELS 32

#define CH_PPI0			0
#define CH_PPI			(CH_PPI0)
#define CH_PPI1			1
#define CH_SPORT0_RX		12
#define CH_SPORT0_TX		13
#define CH_SPORT1_RX		14
#define CH_SPORT1_TX		15
#define CH_SPI			16
#define CH_UART_RX		17
#define CH_UART_TX		18
#define CH_MEM_STREAM0_DEST     24	 /* TX */
#define CH_MEM_STREAM0_SRC      25	 /* RX */
#define CH_MEM_STREAM1_DEST     26	 /* TX */
#define CH_MEM_STREAM1_SRC      27	 /* RX */
#define CH_MEM_STREAM2_DEST	28
#define CH_MEM_STREAM2_SRC	29
#define CH_MEM_STREAM3_DEST	30
#define CH_MEM_STREAM3_SRC	31
#define CH_IMEM_STREAM0_DEST	32
#define CH_IMEM_STREAM0_SRC	33
#define CH_IMEM_STREAM1_DEST	34
#define CH_IMEM_STREAM1_SRC	35

#endif
