/* mach/dma.h - arch-specific DMA defines
 *
 * Copyright 2004-2008 Analog Devices Inc.
 *
 * Licensed under the GPL-2 or later.
 */

#ifndef _MACH_DMA_H_
#define _MACH_DMA_H_

#define CH_PPI			0
#define CH_SPORT0_RX		1
#define CH_SPORT0_TX		2
#define CH_SPORT1_RX		3
#define CH_SPORT1_TX		4
#define CH_SPI0			5
#define CH_UART0_RX		6
#define CH_UART0_TX		7
#define CH_SPORT2_RX		8
#define CH_SPORT2_TX		9
#define CH_SPORT3_RX		10
#define CH_SPORT3_TX		11
#define CH_SPI1			14
#define CH_SPI2			15
#define CH_UART1_RX		16
#define CH_UART1_TX		17
#define CH_UART2_RX		18
#define CH_UART2_TX		19

#define CH_MEM_STREAM0_DEST	20
#define CH_MEM_STREAM0_SRC	21
#define CH_MEM_STREAM1_DEST	22
#define CH_MEM_STREAM1_SRC	23
#define CH_MEM_STREAM2_DEST	24
#define CH_MEM_STREAM2_SRC	25
#define CH_MEM_STREAM3_DEST	26
#define CH_MEM_STREAM3_SRC	27

#define MAX_DMA_CHANNELS 28

#endif
