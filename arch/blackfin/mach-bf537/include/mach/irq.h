/*
 * Copyright 2005-2008 Analog Devices Inc.
 *
 * Licensed under the GPL-2 or later
 */

#ifndef _BF537_IRQ_H_
#define _BF537_IRQ_H_

#include <mach-common/irq.h>

#define NR_PERI_INTS		32

#define IRQ_PLL_WAKEUP		BFIN_IRQ(0)	/* PLL Wakeup Interrupt */
#define IRQ_DMA_ERROR		BFIN_IRQ(1)	/* DMA Error (general) */
#define IRQ_GENERIC_ERROR	BFIN_IRQ(2)	/* GENERIC Error Interrupt */
#define IRQ_RTC			BFIN_IRQ(3)	/* RTC Interrupt */
#define IRQ_PPI			BFIN_IRQ(4)	/* DMA0 Interrupt (PPI) */
#define IRQ_SPORT0_RX		BFIN_IRQ(5)	/* DMA3 Interrupt (SPORT0 RX) */
#define IRQ_SPORT0_TX		BFIN_IRQ(6)	/* DMA4 Interrupt (SPORT0 TX) */
#define IRQ_SPORT1_RX		BFIN_IRQ(7)	/* DMA5 Interrupt (SPORT1 RX) */
#define IRQ_SPORT1_TX		BFIN_IRQ(8)	/* DMA6 Interrupt (SPORT1 TX) */
#define IRQ_TWI			BFIN_IRQ(9)	/* TWI Interrupt */
#define IRQ_SPI			BFIN_IRQ(10)	/* DMA7 Interrupt (SPI) */
#define IRQ_UART0_RX		BFIN_IRQ(11)	/* DMA8 Interrupt (UART0 RX) */
#define IRQ_UART0_TX		BFIN_IRQ(12)	/* DMA9 Interrupt (UART0 TX) */
#define IRQ_UART1_RX		BFIN_IRQ(13)	/* DMA10 Interrupt (UART1 RX) */
#define IRQ_UART1_TX		BFIN_IRQ(14)	/* DMA11 Interrupt (UART1 TX) */
#define IRQ_CAN_RX		BFIN_IRQ(15)	/* CAN Receive Interrupt */
#define IRQ_CAN_TX		BFIN_IRQ(16)	/* CAN Transmit Interrupt */
#define IRQ_PH_INTA_MAC_RX	BFIN_IRQ(17)	/* Port H Interrupt A & DMA1 Interrupt (Ethernet RX) */
#define IRQ_PH_INTB_MAC_TX	BFIN_IRQ(18)	/* Port H Interrupt B & DMA2 Interrupt (Ethernet TX) */
#define IRQ_TIMER0		BFIN_IRQ(19)	/* Timer 0 */
#define IRQ_TIMER1		BFIN_IRQ(20)	/* Timer 1 */
#define IRQ_TIMER2		BFIN_IRQ(21)	/* Timer 2 */
#define IRQ_TIMER3		BFIN_IRQ(22)	/* Timer 3 */
#define IRQ_TIMER4		BFIN_IRQ(23)	/* Timer 4 */
#define IRQ_TIMER5		BFIN_IRQ(24)	/* Timer 5 */
#define IRQ_TIMER6		BFIN_IRQ(25)	/* Timer 6 */
#define IRQ_TIMER7		BFIN_IRQ(26)	/* Timer 7 */
#define IRQ_PF_INTA_PG_INTA	BFIN_IRQ(27)	/* Ports F&G Interrupt A */
#define IRQ_PORTG_INTB		BFIN_IRQ(28)	/* Port G Interrupt B */
#define IRQ_MEM_DMA0		BFIN_IRQ(29)	/* (Memory DMA Stream 0) */
#define IRQ_MEM_DMA1		BFIN_IRQ(30)	/* (Memory DMA Stream 1) */
#define IRQ_PF_INTB_WATCH	BFIN_IRQ(31)	/* Watchdog & Port F Interrupt B */

#define SYS_IRQS		39

#define IRQ_PPI_ERROR		42	/* PPI Error Interrupt */
#define IRQ_CAN_ERROR		43	/* CAN Error Interrupt */
#define IRQ_MAC_ERROR		44	/* MAC Status/Error Interrupt */
#define IRQ_SPORT0_ERROR	45	/* SPORT0 Error Interrupt */
#define IRQ_SPORT1_ERROR	46	/* SPORT1 Error Interrupt */
#define IRQ_SPI_ERROR		47	/* SPI Error Interrupt */
#define IRQ_UART0_ERROR		48	/* UART Error Interrupt */
#define IRQ_UART1_ERROR		49	/* UART Error Interrupt */

#define IRQ_PF0			50
#define IRQ_PF1			51
#define IRQ_PF2			52
#define IRQ_PF3			53
#define IRQ_PF4			54
#define IRQ_PF5			55
#define IRQ_PF6			56
#define IRQ_PF7			57
#define IRQ_PF8			58
#define IRQ_PF9			59
#define IRQ_PF10		60
#define IRQ_PF11		61
#define IRQ_PF12		62
#define IRQ_PF13		63
#define IRQ_PF14		64
#define IRQ_PF15		65

#define IRQ_PG0			66
#define IRQ_PG1			67
#define IRQ_PG2			68
#define IRQ_PG3			69
#define IRQ_PG4			70
#define IRQ_PG5			71
#define IRQ_PG6			72
#define IRQ_PG7			73
#define IRQ_PG8			74
#define IRQ_PG9			75
#define IRQ_PG10		76
#define IRQ_PG11		77
#define IRQ_PG12		78
#define IRQ_PG13		79
#define IRQ_PG14		80
#define IRQ_PG15		81

#define IRQ_PH0			82
#define IRQ_PH1			83
#define IRQ_PH2			84
#define IRQ_PH3			85
#define IRQ_PH4			86
#define IRQ_PH5			87
#define IRQ_PH6			88
#define IRQ_PH7			89
#define IRQ_PH8			90
#define IRQ_PH9			91
#define IRQ_PH10		92
#define IRQ_PH11		93
#define IRQ_PH12		94
#define IRQ_PH13		95
#define IRQ_PH14		96
#define IRQ_PH15		97

#define GPIO_IRQ_BASE		IRQ_PF0

#define IRQ_MAC_PHYINT		98	/* PHY_INT Interrupt */
#define IRQ_MAC_MMCINT		99	/* MMC Counter Interrupt */
#define IRQ_MAC_RXFSINT		100	/* RX Frame-Status Interrupt */
#define IRQ_MAC_TXFSINT		101	/* TX Frame-Status Interrupt */
#define IRQ_MAC_WAKEDET		102	/* Wake-Up Interrupt */
#define IRQ_MAC_RXDMAERR	103	/* RX DMA Direction Error Interrupt */
#define IRQ_MAC_TXDMAERR	104	/* TX DMA Direction Error Interrupt */
#define IRQ_MAC_STMDONE		105	/* Station Mgt. Transfer Done Interrupt */

#define IRQ_MAC_RX		106	/* DMA1 Interrupt (Ethernet RX) */
#define IRQ_PORTH_INTA		107	/* Port H Interrupt A */

#if 0 /* No Interrupt B support (yet) */
#define IRQ_MAC_TX		108	/* DMA2 Interrupt (Ethernet TX) */
#define IRQ_PORTH_INTB		109	/* Port H Interrupt B */
#else
#define IRQ_MAC_TX		IRQ_PH_INTB_MAC_TX
#endif

#define IRQ_PORTF_INTA		110	/* Port F Interrupt A */
#define IRQ_PORTG_INTA		111	/* Port G Interrupt A */

#if 0 /* No Interrupt B support (yet) */
#define IRQ_WATCH		112	/* Watchdog Timer */
#define IRQ_PORTF_INTB		113	/* Port F Interrupt B */
#else
#define IRQ_WATCH		IRQ_PF_INTB_WATCH
#endif

#define NR_MACH_IRQS		(113 + 1)

/* IAR0 BIT FIELDS */
#define IRQ_PLL_WAKEUP_POS	0
#define IRQ_DMA_ERROR_POS	4
#define IRQ_ERROR_POS		8
#define IRQ_RTC_POS		12
#define IRQ_PPI_POS		16
#define IRQ_SPORT0_RX_POS	20
#define IRQ_SPORT0_TX_POS	24
#define IRQ_SPORT1_RX_POS	28

/* IAR1 BIT FIELDS */
#define IRQ_SPORT1_TX_POS	0
#define IRQ_TWI_POS		4
#define IRQ_SPI_POS		8
#define IRQ_UART0_RX_POS	12
#define IRQ_UART0_TX_POS	16
#define IRQ_UART1_RX_POS	20
#define IRQ_UART1_TX_POS	24
#define IRQ_CAN_RX_POS		28

/* IAR2 BIT FIELDS */
#define IRQ_CAN_TX_POS		0
#define IRQ_MAC_RX_POS		4
#define IRQ_MAC_TX_POS		8
#define IRQ_TIMER0_POS		12
#define IRQ_TIMER1_POS		16
#define IRQ_TIMER2_POS		20
#define IRQ_TIMER3_POS		24
#define IRQ_TIMER4_POS		28

/* IAR3 BIT FIELDS */
#define IRQ_TIMER5_POS		0
#define IRQ_TIMER6_POS		4
#define IRQ_TIMER7_POS		8
#define IRQ_PROG_INTA_POS	12
#define IRQ_PORTG_INTB_POS	16
#define IRQ_MEM_DMA0_POS	20
#define IRQ_MEM_DMA1_POS	24
#define IRQ_WATCH_POS		28

#define init_mach_irq init_mach_irq

#endif
