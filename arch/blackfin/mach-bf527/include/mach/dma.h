/* mach/dma.h - arch-specific DMA defines
 *
 * Copyright 2004-2008 Analog Devices Inc.
 *
 * Licensed under the GPL-2 or later.
 */

#ifndef _MACH_DMA_H_
#define _MACH_DMA_H_

#define MAX_DMA_CHANNELS 16

#define CH_PPI 			0	/* PPI receive/transmit or NFC */
#define CH_EMAC_RX 		1	/* Ethernet MAC receive or HOSTDP */
#define CH_EMAC_HOSTDP 		1	/* Ethernet MAC receive or HOSTDP */
#define CH_EMAC_TX 		2	/* Ethernet MAC transmit or NFC */
#define CH_SPORT0_RX 		3	/* SPORT0 receive */
#define CH_SPORT0_TX 		4	/* SPORT0 transmit */
#define CH_SPORT1_RX 		5	/* SPORT1 receive */
#define CH_SPORT1_TX 		6	/* SPORT1 transmit */
#define CH_SPI 			7	/* SPI transmit/receive */
#define CH_UART0_RX 		8	/* UART0 receive */
#define CH_UART0_TX 		9	/* UART0 transmit */
#define CH_UART1_RX 		10	/* UART1 receive */
#define CH_UART1_TX 		11	/* UART1 transmit */

#define CH_MEM_STREAM0_DEST	12	/* TX */
#define CH_MEM_STREAM0_SRC  	13	/* RX */
#define CH_MEM_STREAM1_DEST	14	/* TX */
#define CH_MEM_STREAM1_SRC 	15	/* RX */

#if defined(CONFIG_BF527_NAND_D_PORTF)
#define CH_NFC			CH_PPI	/* PPI receive/transmit or NFC */
#elif defined(CONFIG_BF527_NAND_D_PORTH)
#define CH_NFC			CH_EMAC_TX /* PPI receive/transmit or NFC */
#endif

#endif
