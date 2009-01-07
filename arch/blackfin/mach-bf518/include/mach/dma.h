/*
 * file:        include/asm-blackfin/mach-bf518/dma.h
 * based on:	include/asm-blackfin/mach-bf527/dma.h
 * author:	Michael Hennerich (michael.hennerich@analog.com)
 *
 * created:
 * description:
 *	system DMA map
 * rev:
 *
 * modified:
 *
 *
 * bugs:         enter bugs at http://blackfin.uclinux.org/
 *
 * this program is free software; you can redistribute it and/or modify
 * it under the terms of the gnu general public license as published by
 * the free software foundation; either version 2, or (at your option)
 * any later version.
 *
 * this program is distributed in the hope that it will be useful,
 * but without any warranty; without even the implied warranty of
 * merchantability or fitness for a particular purpose.  see the
 * gnu general public license for more details.
 *
 * you should have received a copy of the gnu general public license
 * along with this program; see the file copying.
 * if not, write to the free software foundation,
 * 59 temple place - suite 330, boston, ma 02111-1307, usa.
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
