/* mach/dma.h - arch-specific DMA defines
 *
 * Copyright 2004-2008 Analog Devices Inc.
 *
 * Licensed under the GPL-2 or later.
 */

#ifndef _MACH_DMA_H_
#define _MACH_DMA_H_

#define CH_SPORT0_RX		0
#define CH_SPORT0_TX		1
#define CH_SPORT1_RX		2
#define CH_SPORT1_TX		3
#define CH_SPI0			4
#define CH_SPI1			5
#define CH_UART0_RX 		6
#define CH_UART0_TX 		7
#define CH_UART1_RX 		8
#define CH_UART1_TX 		9
#define CH_ATAPI_RX		10
#define CH_ATAPI_TX		11
#define CH_EPPI0		12
#define CH_EPPI1		13
#define CH_EPPI2		14
#define CH_PIXC_IMAGE		15
#define CH_PIXC_OVERLAY		16
#define CH_PIXC_OUTPUT		17
#define CH_SPORT2_RX		18
#define CH_SPORT2_TX		19
#define CH_SPORT3_RX		20
#define CH_SPORT3_TX		21
#define CH_SDH			22
#define CH_NFC			22
#define CH_SPI2			23

#if defined(CONFIG_UART2_DMA_RX_ON_DMA13)
#define CH_UART2_RX		13
#define IRQ_UART2_RX		BFIN_IRQ(37)	/* UART2 RX USE EPP1 (DMA13) Interrupt */
#define CH_UART2_TX		14
#define IRQ_UART2_TX		BFIN_IRQ(38)	/* UART2 RX USE EPP1 (DMA14) Interrupt */
#else						/* Default USE SPORT2's DMA Channel */
#define CH_UART2_RX		18
#define IRQ_UART2_RX		BFIN_IRQ(33)	/* UART2 RX (DMA18) Interrupt */
#define CH_UART2_TX		19
#define IRQ_UART2_TX		BFIN_IRQ(34)	/* UART2 TX (DMA19) Interrupt */
#endif

#if defined(CONFIG_UART3_DMA_RX_ON_DMA15)
#define CH_UART3_RX		15
#define IRQ_UART3_RX		BFIN_IRQ(64)	/* UART3 RX USE PIXC IN0 (DMA15) Interrupt */
#define CH_UART3_TX		16
#define IRQ_UART3_TX		BFIN_IRQ(65)	/* UART3 TX USE PIXC IN1 (DMA16) Interrupt */
#else						/* Default USE SPORT3's DMA Channel */
#define CH_UART3_RX		20
#define IRQ_UART3_RX		BFIN_IRQ(35)	/* UART3 RX (DMA20) Interrupt */
#define CH_UART3_TX		21
#define IRQ_UART3_TX		BFIN_IRQ(36)	/* UART3 TX (DMA21) Interrupt */
#endif

#define CH_MEM_STREAM0_DEST	24
#define CH_MEM_STREAM0_SRC	25
#define CH_MEM_STREAM1_DEST	26
#define CH_MEM_STREAM1_SRC	27
#define CH_MEM_STREAM2_DEST	28
#define CH_MEM_STREAM2_SRC	29
#define CH_MEM_STREAM3_DEST	30
#define CH_MEM_STREAM3_SRC	31

#define MAX_DMA_CHANNELS 32

#endif
