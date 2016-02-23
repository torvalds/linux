/* mach/dma.h - arch-specific DMA defines
 *
 * Copyright 2004-2008 Analog Devices Inc.
 *
 * Licensed under the GPL-2 or later.
 */

#ifndef _MACH_DMA_H_
#define _MACH_DMA_H_

#define MAX_DMA_CHANNELS 12

#define CH_PPI          0
#define CH_SPORT0_RX    1
#define CH_SPORT0_TX    2
#define CH_SPORT1_RX    3
#define CH_SPORT1_TX    4
#define CH_SPI          5
#define CH_UART0_RX     6
#define CH_UART0_TX     7
#define CH_MEM_STREAM0_DEST     8	 /* TX */
#define CH_MEM_STREAM0_SRC      9	 /* RX */
#define CH_MEM_STREAM1_DEST     10	 /* TX */
#define CH_MEM_STREAM1_SRC      11	 /* RX */

#endif
