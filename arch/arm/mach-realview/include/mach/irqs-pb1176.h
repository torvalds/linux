/*
 * arch/arm/mach-realview/include/mach/irqs-pb1176.h
 *
 * Copyright (C) 2008 ARM Limited
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 * MA 02110-1301, USA.
 */

#ifndef __MACH_IRQS_PB1176_H
#define __MACH_IRQS_PB1176_H

#define IRQ_DC1176_GIC_START			32
#define IRQ_PB1176_GIC_START			64

/*
 * ARM1176 DevChip interrupt sources (primary GIC)
 */
#define IRQ_DC1176_WATCHDOG	(IRQ_DC1176_GIC_START + 0)	/* Watchdog timer */
#define IRQ_DC1176_SOFTINT	(IRQ_DC1176_GIC_START + 1)	/* Software interrupt */
#define IRQ_DC1176_COMMRx	(IRQ_DC1176_GIC_START + 2)	/* Debug Comm Rx interrupt */
#define IRQ_DC1176_COMMTx	(IRQ_DC1176_GIC_START + 3)	/* Debug Comm Tx interrupt */
#define IRQ_DC1176_TIMER0	(IRQ_DC1176_GIC_START + 8)	/* Timer 0 */
#define IRQ_DC1176_TIMER1	(IRQ_DC1176_GIC_START + 9)	/* Timer 1 */
#define IRQ_DC1176_TIMER2	(IRQ_DC1176_GIC_START + 10)	/* Timer 2 */
#define IRQ_DC1176_APC		(IRQ_DC1176_GIC_START + 11)
#define IRQ_DC1176_IEC		(IRQ_DC1176_GIC_START + 12)
#define IRQ_DC1176_L2CC		(IRQ_DC1176_GIC_START + 13)
#define IRQ_DC1176_RTC		(IRQ_DC1176_GIC_START + 14)
#define IRQ_DC1176_CLCD		(IRQ_DC1176_GIC_START + 15)	/* CLCD controller */
#define IRQ_DC1176_UART0	(IRQ_DC1176_GIC_START + 18)	/* UART 0 on development chip */
#define IRQ_DC1176_UART1	(IRQ_DC1176_GIC_START + 19)	/* UART 1 on development chip */
#define IRQ_DC1176_UART2	(IRQ_DC1176_GIC_START + 20)	/* UART 2 on development chip */
#define IRQ_DC1176_UART3	(IRQ_DC1176_GIC_START + 21)	/* UART 3 on development chip */

#define IRQ_DC1176_PB_IRQ2	(IRQ_DC1176_GIC_START + 30)	/* tile GIC */
#define IRQ_DC1176_PB_IRQ1	(IRQ_DC1176_GIC_START + 31)	/* main GIC */

/*
 * RealView PB1176 interrupt sources (secondary GIC)
 */
#define IRQ_PB1176_MMCI0A	(IRQ_PB1176_GIC_START + 1)	/* Multimedia Card 0A */
#define IRQ_PB1176_MMCI0B	(IRQ_PB1176_GIC_START + 2)	/* Multimedia Card 0A */
#define IRQ_PB1176_KMI0		(IRQ_PB1176_GIC_START + 3)	/* Keyboard/Mouse port 0 */
#define IRQ_PB1176_KMI1		(IRQ_PB1176_GIC_START + 4)	/* Keyboard/Mouse port 1 */
#define IRQ_PB1176_SCI		(IRQ_PB1176_GIC_START + 5)
#define IRQ_PB1176_UART4	(IRQ_PB1176_GIC_START + 6)	/* UART 4 on baseboard */
#define IRQ_PB1176_CHARLCD	(IRQ_PB1176_GIC_START + 7)	/* Character LCD */
#define IRQ_PB1176_GPIO1	(IRQ_PB1176_GIC_START + 8)
#define IRQ_PB1176_GPIO2	(IRQ_PB1176_GIC_START + 9)
#define IRQ_PB1176_ETH		(IRQ_PB1176_GIC_START + 10)	/* Ethernet controller */
#define IRQ_PB1176_USB		(IRQ_PB1176_GIC_START + 11)	/* USB controller */

#define IRQ_PB1176_PISMO	(IRQ_PB1176_GIC_START + 16)

#define IRQ_PB1176_AACI		(IRQ_PB1176_GIC_START + 19)	/* Audio Codec */

#define IRQ_PB1176_TIMER0_1	(IRQ_PB1176_GIC_START + 22)
#define IRQ_PB1176_TIMER2_3	(IRQ_PB1176_GIC_START + 23)
#define IRQ_PB1176_DMAC		(IRQ_PB1176_GIC_START + 24)	/* DMA controller */
#define IRQ_PB1176_RTC		(IRQ_PB1176_GIC_START + 25)	/* Real Time Clock */

#define IRQ_PB1176_GPIO0	-1
#define IRQ_PB1176_SSP		-1
#define IRQ_PB1176_SCTL		-1

#define NR_GIC_PB1176		2

/*
 * Only define NR_IRQS if less than NR_IRQS_PB1176
 */
#define NR_IRQS_PB1176		(IRQ_DC1176_GIC_START + 96)

#if defined(CONFIG_MACH_REALVIEW_PB1176)

#if !defined(NR_IRQS) || (NR_IRQS < NR_IRQS_PB1176)
#undef NR_IRQS
#define NR_IRQS			NR_IRQS_PB1176
#endif

#if !defined(MAX_GIC_NR) || (MAX_GIC_NR < NR_GIC_PB1176)
#undef MAX_GIC_NR
#define MAX_GIC_NR		NR_GIC_PB1176
#endif

#endif	/* CONFIG_MACH_REALVIEW_PB1176 */

#endif	/* __MACH_IRQS_PB1176_H */
