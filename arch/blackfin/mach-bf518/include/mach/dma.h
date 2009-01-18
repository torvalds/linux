/* mach/dma.h - arch-specific DMA defines
 *
 * Copyright 2004-2008 Analog Devices Inc.
 *
 * Licensed under the GPL-2 or later.
 */

#ifndef _MACH_DMA_H_
#define _MACH_DMA_H_

#define MAX_DMA_CHANNELS 16

#define CH_PPI 			0	/* PPI receive/transmit */
#define CH_EMAC_RX 		1	/* Ethernet MAC receive */
#define CH_EMAC_TX 		2	/* Ethernet MAC transmit */
#define CH_SPORT0_RX 		3	/* SPORT0 receive */
#define CH_SPORT0_TX 		4	/* SPORT0 transmit */
#define CH_RSI 			4	/* RSI */
#define CH_SPORT1_RX 		5	/* SPORT1 receive */
#define CH_SPI1 		5	/* SPI1 transmit/receive */
#define CH_SPORT1_TX 		6	/* SPORT1 transmit */
#define CH_SPI0 		7	/* SPI0 transmit/receive */
#define CH_UART0_RX 		8	/* UART0 receive */
#define CH_UART0_TX 		9	/* UART0 transmit */
#define CH_UART1_RX 		10	/* UART1 receive */
#define CH_UART1_TX 		11	/* UART1 transmit */

#define CH_MEM_STREAM0_SRC 	12	/* RX */
#define CH_MEM_STREAM0_DEST	13	/* TX */
#define CH_MEM_STREAM1_SRC 	14	/* RX */
#define CH_MEM_STREAM1_DEST	15	/* TX */

#endif
