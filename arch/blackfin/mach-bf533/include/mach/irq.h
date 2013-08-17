/*
 * Copyright 2005-2008 Analog Devices Inc.
 *
 * Licensed under the GPL-2 or later
 */

#ifndef _BF533_IRQ_H_
#define _BF533_IRQ_H_

#include <mach-common/irq.h>

#define NR_PERI_INTS		24

#define IRQ_PLL_WAKEUP		BFIN_IRQ(0)	/* PLL Wakeup Interrupt */
#define IRQ_DMA_ERROR		BFIN_IRQ(1)	/* DMA Error (general) */
#define IRQ_PPI_ERROR		BFIN_IRQ(2)	/* PPI Error Interrupt */
#define IRQ_SPORT0_ERROR	BFIN_IRQ(3)	/* SPORT0 Error Interrupt */
#define IRQ_SPORT1_ERROR	BFIN_IRQ(4)	/* SPORT1 Error Interrupt */
#define IRQ_SPI_ERROR		BFIN_IRQ(5)	/* SPI Error Interrupt */
#define IRQ_UART0_ERROR		BFIN_IRQ(6)	/* UART Error Interrupt */
#define IRQ_RTC			BFIN_IRQ(7)	/* RTC Interrupt */
#define IRQ_PPI			BFIN_IRQ(8)	/* DMA0 Interrupt (PPI) */
#define IRQ_SPORT0_RX		BFIN_IRQ(9)	/* DMA1 Interrupt (SPORT0 RX) */
#define IRQ_SPORT0_TX		BFIN_IRQ(10)	/* DMA2 Interrupt (SPORT0 TX) */
#define IRQ_SPORT1_RX		BFIN_IRQ(11)	/* DMA3 Interrupt (SPORT1 RX) */
#define IRQ_SPORT1_TX		BFIN_IRQ(12)	/* DMA4 Interrupt (SPORT1 TX) */
#define IRQ_SPI			BFIN_IRQ(13)	/* DMA5 Interrupt (SPI) */
#define IRQ_UART0_RX		BFIN_IRQ(14)	/* DMA6 Interrupt (UART RX) */
#define IRQ_UART0_TX		BFIN_IRQ(15)	/* DMA7 Interrupt (UART TX) */
#define IRQ_TIMER0		BFIN_IRQ(16)	/* Timer 0 */
#define IRQ_TIMER1		BFIN_IRQ(17)	/* Timer 1 */
#define IRQ_TIMER2		BFIN_IRQ(18)	/* Timer 2 */
#define IRQ_PROG_INTA		BFIN_IRQ(19)	/* Programmable Flags A (8) */
#define IRQ_PROG_INTB		BFIN_IRQ(20)	/* Programmable Flags B (8) */
#define IRQ_MEM_DMA0		BFIN_IRQ(21)	/* DMA8/9 Interrupt (Memory DMA Stream 0) */
#define IRQ_MEM_DMA1		BFIN_IRQ(22)	/* DMA10/11 Interrupt (Memory DMA Stream 1) */
#define IRQ_WATCH		BFIN_IRQ(23)	/* Watch Dog Timer */

#define SYS_IRQS		31

#define IRQ_PF0			33
#define IRQ_PF1			34
#define IRQ_PF2			35
#define IRQ_PF3			36
#define IRQ_PF4			37
#define IRQ_PF5			38
#define IRQ_PF6			39
#define IRQ_PF7			40
#define IRQ_PF8			41
#define IRQ_PF9			42
#define IRQ_PF10		43
#define IRQ_PF11		44
#define IRQ_PF12		45
#define IRQ_PF13		46
#define IRQ_PF14		47
#define IRQ_PF15		48

#define GPIO_IRQ_BASE		IRQ_PF0

#define NR_MACH_IRQS		(IRQ_PF15 + 1)

/* IAR0 BIT FIELDS */
#define RTC_ERROR_POS		28
#define UART_ERROR_POS		24
#define SPORT1_ERROR_POS	20
#define SPI_ERROR_POS		16
#define SPORT0_ERROR_POS	12
#define PPI_ERROR_POS		8
#define DMA_ERROR_POS		4
#define PLLWAKE_ERROR_POS	0

/* IAR1 BIT FIELDS */
#define DMA7_UARTTX_POS		28
#define DMA6_UARTRX_POS		24
#define DMA5_SPI_POS		20
#define DMA4_SPORT1TX_POS	16
#define DMA3_SPORT1RX_POS	12
#define DMA2_SPORT0TX_POS	8
#define DMA1_SPORT0RX_POS	4
#define DMA0_PPI_POS		0

/* IAR2 BIT FIELDS */
#define WDTIMER_POS		28
#define MEMDMA1_POS		24
#define MEMDMA0_POS		20
#define PFB_POS			16
#define PFA_POS			12
#define TIMER2_POS		8
#define TIMER1_POS		4
#define TIMER0_POS		0

#endif
