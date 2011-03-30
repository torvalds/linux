/*
 * Copyright 2008 Analog Devices Inc.
 *
 * Licensed under the GPL-2 or later.
 */

#ifndef _BF538_IRQ_H_
#define _BF538_IRQ_H_

#include <mach-common/irq.h>

#define NR_PERI_INTS		(2 * 32)

#define IRQ_PLL_WAKEUP		BFIN_IRQ(0)	/* PLL Wakeup Interrupt */
#define IRQ_DMA0_ERROR		BFIN_IRQ(1)	/* DMA Error 0 (generic) */
#define IRQ_PPI_ERROR		BFIN_IRQ(2)	/* PPI Error */
#define IRQ_SPORT0_ERROR	BFIN_IRQ(3)	/* SPORT0 Status */
#define IRQ_SPORT1_ERROR	BFIN_IRQ(4)	/* SPORT1 Status */
#define IRQ_SPI0_ERROR		BFIN_IRQ(5)	/* SPI0 Status */
#define IRQ_UART0_ERROR		BFIN_IRQ(6)	/* UART0 Status */
#define IRQ_RTC			BFIN_IRQ(7)	/* RTC */
#define IRQ_PPI			BFIN_IRQ(8)	/* DMA Channel 0 (PPI) */
#define IRQ_SPORT0_RX		BFIN_IRQ(9)	/* DMA 1 Channel (SPORT0 RX) */
#define IRQ_SPORT0_TX		BFIN_IRQ(10)	/* DMA 2 Channel (SPORT0 TX) */
#define IRQ_SPORT1_RX		BFIN_IRQ(11)	/* DMA 3 Channel (SPORT1 RX) */
#define IRQ_SPORT1_TX		BFIN_IRQ(12)	/* DMA 4 Channel (SPORT1 TX) */
#define IRQ_SPI0		BFIN_IRQ(13)	/* DMA 5 Channel (SPI0) */
#define IRQ_UART0_RX		BFIN_IRQ(14)	/* DMA 6 Channel (UART0 RX) */
#define IRQ_UART0_TX		BFIN_IRQ(15)	/* DMA 7 Channel (UART0 TX) */
#define IRQ_TIMER0		BFIN_IRQ(16)	/* Timer 0 */
#define IRQ_TIMER1		BFIN_IRQ(17)	/* Timer 1 */
#define IRQ_TIMER2		BFIN_IRQ(18)	/* Timer 2 */
#define IRQ_PORTF_INTA		BFIN_IRQ(19)	/* Port F Interrupt A */
#define IRQ_PORTF_INTB		BFIN_IRQ(20)	/* Port F Interrupt B */
#define IRQ_MEM0_DMA0		BFIN_IRQ(21)	/* MDMA0 Stream 0 */
#define IRQ_MEM0_DMA1		BFIN_IRQ(22)	/* MDMA0 Stream 1 */
#define IRQ_WATCH		BFIN_IRQ(23)	/* Software Watchdog Timer */
#define IRQ_DMA1_ERROR		BFIN_IRQ(24)	/* DMA Error 1 (generic) */
#define IRQ_SPORT2_ERROR	BFIN_IRQ(25)	/* SPORT2 Status */
#define IRQ_SPORT3_ERROR	BFIN_IRQ(26)	/* SPORT3 Status */
#define IRQ_SPI1_ERROR		BFIN_IRQ(28)	/* SPI1 Status */
#define IRQ_SPI2_ERROR		BFIN_IRQ(29)	/* SPI2 Status */
#define IRQ_UART1_ERROR		BFIN_IRQ(30)	/* UART1 Status */
#define IRQ_UART2_ERROR		BFIN_IRQ(31)	/* UART2 Status */
#define IRQ_CAN_ERROR		BFIN_IRQ(32)	/* CAN Status (Error) Interrupt */
#define IRQ_SPORT2_RX		BFIN_IRQ(33)	/* DMA 8 Channel (SPORT2 RX) */
#define IRQ_SPORT2_TX		BFIN_IRQ(34)	/* DMA 9 Channel (SPORT2 TX) */
#define IRQ_SPORT3_RX		BFIN_IRQ(35)	/* DMA 10 Channel (SPORT3 RX) */
#define IRQ_SPORT3_TX		BFIN_IRQ(36)	/* DMA 11 Channel (SPORT3 TX) */
#define IRQ_SPI1		BFIN_IRQ(39)	/* DMA 14 Channel (SPI1) */
#define IRQ_SPI2		BFIN_IRQ(40)	/* DMA 15 Channel (SPI2) */
#define IRQ_UART1_RX		BFIN_IRQ(41)	/* DMA 16 Channel (UART1 RX) */
#define IRQ_UART1_TX		BFIN_IRQ(42)	/* DMA 17 Channel (UART1 TX) */
#define IRQ_UART2_RX		BFIN_IRQ(43)	/* DMA 18 Channel (UART2 RX) */
#define IRQ_UART2_TX		BFIN_IRQ(44)	/* DMA 19 Channel (UART2 TX) */
#define IRQ_TWI0		BFIN_IRQ(45)	/* TWI0 */
#define IRQ_TWI1		BFIN_IRQ(46)	/* TWI1 */
#define IRQ_CAN_RX		BFIN_IRQ(47)	/* CAN Receive Interrupt */
#define IRQ_CAN_TX		BFIN_IRQ(48)	/* CAN Transmit Interrupt */
#define IRQ_MEM1_DMA0		BFIN_IRQ(49)	/* MDMA1 Stream 0 */
#define IRQ_MEM1_DMA1		BFIN_IRQ(50)	/* MDMA1 Stream 1 */

#define SYS_IRQS		BFIN_IRQ(63)	/* 70 */

#define IRQ_PF0			71
#define IRQ_PF1			72
#define IRQ_PF2			73
#define IRQ_PF3			74
#define IRQ_PF4			75
#define IRQ_PF5			76
#define IRQ_PF6			77
#define IRQ_PF7			78
#define IRQ_PF8			79
#define IRQ_PF9			80
#define IRQ_PF10		81
#define IRQ_PF11		82
#define IRQ_PF12		83
#define IRQ_PF13		84
#define IRQ_PF14		85
#define IRQ_PF15		86

#define GPIO_IRQ_BASE		IRQ_PF0

#define NR_MACH_IRQS		(IRQ_PF15 + 1)

/* IAR0 BIT FIELDS */
#define IRQ_PLL_WAKEUP_POS	0
#define IRQ_DMA0_ERROR_POS	4
#define IRQ_PPI_ERROR_POS	8
#define IRQ_SPORT0_ERROR_POS	12
#define IRQ_SPORT1_ERROR_POS	16
#define IRQ_SPI0_ERROR_POS	20
#define IRQ_UART0_ERROR_POS	24
#define IRQ_RTC_POS		28

/* IAR1 BIT FIELDS */
#define IRQ_PPI_POS		0
#define IRQ_SPORT0_RX_POS	4
#define IRQ_SPORT0_TX_POS	8
#define IRQ_SPORT1_RX_POS	12
#define IRQ_SPORT1_TX_POS	16
#define IRQ_SPI0_POS		20
#define IRQ_UART0_RX_POS	24
#define IRQ_UART0_TX_POS	28

/* IAR2 BIT FIELDS */
#define IRQ_TIMER0_POS		0
#define IRQ_TIMER1_POS		4
#define IRQ_TIMER2_POS		8
#define IRQ_PORTF_INTA_POS	12
#define IRQ_PORTF_INTB_POS	16
#define IRQ_MEM0_DMA0_POS	20
#define IRQ_MEM0_DMA1_POS	24
#define IRQ_WATCH_POS		28

/* IAR3 BIT FIELDS */
#define IRQ_DMA1_ERROR_POS	0
#define IRQ_SPORT2_ERROR_POS	4
#define IRQ_SPORT3_ERROR_POS	8
#define IRQ_SPI1_ERROR_POS	16
#define IRQ_SPI2_ERROR_POS	20
#define IRQ_UART1_ERROR_POS	24
#define IRQ_UART2_ERROR_POS	28

/* IAR4 BIT FIELDS */
#define IRQ_CAN_ERROR_POS	0
#define IRQ_SPORT2_RX_POS	4
#define IRQ_SPORT2_TX_POS	8
#define IRQ_SPORT3_RX_POS	12
#define IRQ_SPORT3_TX_POS	16
#define IRQ_SPI1_POS		28

/* IAR5 BIT FIELDS */
#define IRQ_SPI2_POS		0
#define IRQ_UART1_RX_POS	4
#define IRQ_UART1_TX_POS	8
#define IRQ_UART2_RX_POS	12
#define IRQ_UART2_TX_POS	16
#define IRQ_TWI0_POS		20
#define IRQ_TWI1_POS		24
#define IRQ_CAN_RX_POS		28

/* IAR6 BIT FIELDS */
#define IRQ_CAN_TX_POS		0
#define IRQ_MEM1_DMA0_POS	4
#define IRQ_MEM1_DMA1_POS	8

#endif
