/*
 * arch/arm/mach-realview/include/mach/irqs-pbx.h
 *
 * Copyright (C) 2009 ARM Limited
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
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */

#ifndef __MACH_IRQS_PBX_H
#define __MACH_IRQS_PBX_H

#define IRQ_PBX_GIC_START			32

/*
 * PBX on-board gic irq sources
 */
#define IRQ_PBX_WATCHDOG	(IRQ_PBX_GIC_START + 0)	/* Watchdog timer */
#define IRQ_PBX_SOFT		(IRQ_PBX_GIC_START + 1)	/* Software interrupt */
#define IRQ_PBX_COMMRx		(IRQ_PBX_GIC_START + 2)	/* Debug Comm Rx interrupt */
#define IRQ_PBX_COMMTx		(IRQ_PBX_GIC_START + 3)	/* Debug Comm Tx interrupt */
#define IRQ_PBX_TIMER0_1	(IRQ_PBX_GIC_START + 4)	/* Timer 0/1 (default timer) */
#define IRQ_PBX_TIMER2_3	(IRQ_PBX_GIC_START + 5)	/* Timer 2/3 */
#define IRQ_PBX_GPIO0		(IRQ_PBX_GIC_START + 6)	/* GPIO 0 */
#define IRQ_PBX_GPIO1		(IRQ_PBX_GIC_START + 7)	/* GPIO 1 */
#define IRQ_PBX_GPIO2		(IRQ_PBX_GIC_START + 8)	/* GPIO 2 */
								/* 9 reserved */
#define IRQ_PBX_RTC		(IRQ_PBX_GIC_START + 10)	/* Real Time Clock */
#define IRQ_PBX_SSP		(IRQ_PBX_GIC_START + 11)	/* Synchronous Serial Port */
#define IRQ_PBX_UART0		(IRQ_PBX_GIC_START + 12)	/* UART 0 on development chip */
#define IRQ_PBX_UART1		(IRQ_PBX_GIC_START + 13)	/* UART 1 on development chip */
#define IRQ_PBX_UART2		(IRQ_PBX_GIC_START + 14)	/* UART 2 on development chip */
#define IRQ_PBX_UART3		(IRQ_PBX_GIC_START + 15)	/* UART 3 on development chip */
#define IRQ_PBX_SCI		(IRQ_PBX_GIC_START + 16)	/* Smart Card Interface */
#define IRQ_PBX_MMCI0A		(IRQ_PBX_GIC_START + 17)	/* Multimedia Card 0A */
#define IRQ_PBX_MMCI0B		(IRQ_PBX_GIC_START + 18)	/* Multimedia Card 0B */
#define IRQ_PBX_AACI		(IRQ_PBX_GIC_START + 19)	/* Audio Codec */
#define IRQ_PBX_KMI0		(IRQ_PBX_GIC_START + 20)	/* Keyboard/Mouse port 0 */
#define IRQ_PBX_KMI1		(IRQ_PBX_GIC_START + 21)	/* Keyboard/Mouse port 1 */
#define IRQ_PBX_CHARLCD		(IRQ_PBX_GIC_START + 22)	/* Character LCD */
#define IRQ_PBX_CLCD		(IRQ_PBX_GIC_START + 23)	/* CLCD controller */
#define IRQ_PBX_DMAC		(IRQ_PBX_GIC_START + 24)	/* DMA controller */
#define IRQ_PBX_PWRFAIL		(IRQ_PBX_GIC_START + 25)	/* Power failure */
#define IRQ_PBX_PISMO		(IRQ_PBX_GIC_START + 26)	/* PISMO interface */
#define IRQ_PBX_DoC		(IRQ_PBX_GIC_START + 27)	/* Disk on Chip memory controller */
#define IRQ_PBX_ETH		(IRQ_PBX_GIC_START + 28)	/* Ethernet controller */
#define IRQ_PBX_USB		(IRQ_PBX_GIC_START + 29)	/* USB controller */
#define IRQ_PBX_TSPEN		(IRQ_PBX_GIC_START + 30)	/* Touchscreen pen */
#define IRQ_PBX_TSKPAD		(IRQ_PBX_GIC_START + 31)	/* Touchscreen keypad */

#define IRQ_PBX_PMU_SCU0        (IRQ_PBX_GIC_START + 32)        /* SCU PMU Interrupts (11mp) */
#define IRQ_PBX_PMU_SCU1        (IRQ_PBX_GIC_START + 33)
#define IRQ_PBX_PMU_SCU2        (IRQ_PBX_GIC_START + 34)
#define IRQ_PBX_PMU_SCU3        (IRQ_PBX_GIC_START + 35)
#define IRQ_PBX_PMU_SCU4        (IRQ_PBX_GIC_START + 36)
#define IRQ_PBX_PMU_SCU5        (IRQ_PBX_GIC_START + 37)
#define IRQ_PBX_PMU_SCU6        (IRQ_PBX_GIC_START + 38)
#define IRQ_PBX_PMU_SCU7        (IRQ_PBX_GIC_START + 39)

#define IRQ_PBX_WATCHDOG1       (IRQ_PBX_GIC_START + 40)        /* Watchdog1 timer */
#define IRQ_PBX_TIMER4_5        (IRQ_PBX_GIC_START + 41)        /* Timer 0/1 (default timer) */
#define IRQ_PBX_TIMER6_7        (IRQ_PBX_GIC_START + 42)        /* Timer 2/3 */
/* ... */
#define IRQ_PBX_PMU_CPU0        (IRQ_PBX_GIC_START + 44)        /* CPU PMU Interrupts */
#define IRQ_PBX_PMU_CPU1        (IRQ_PBX_GIC_START + 45)
#define IRQ_PBX_PMU_CPU2        (IRQ_PBX_GIC_START + 46)
#define IRQ_PBX_PMU_CPU3        (IRQ_PBX_GIC_START + 47)

/* ... */
#define IRQ_PBX_PCI0		(IRQ_PBX_GIC_START + 50)
#define IRQ_PBX_PCI1		(IRQ_PBX_GIC_START + 51)
#define IRQ_PBX_PCI2		(IRQ_PBX_GIC_START + 52)
#define IRQ_PBX_PCI3		(IRQ_PBX_GIC_START + 53)

#define IRQ_PBX_SMC		-1
#define IRQ_PBX_SCTL		-1

#define NR_GIC_PBX		1

/*
 * Only define NR_IRQS if less than NR_IRQS_PBX
 */
#define NR_IRQS_PBX		(IRQ_PBX_GIC_START + 96)

#if defined(CONFIG_MACH_REALVIEW_PBX)

#if !defined(NR_IRQS) || (NR_IRQS < NR_IRQS_PBX)
#undef NR_IRQS
#define NR_IRQS			NR_IRQS_PBX
#endif

#if !defined(MAX_GIC_NR) || (MAX_GIC_NR < NR_GIC_PBX)
#undef MAX_GIC_NR
#define MAX_GIC_NR		NR_GIC_PBX
#endif

#endif	/* CONFIG_MACH_REALVIEW_PBX */

#endif	/* __MACH_IRQS_PBX_H */
