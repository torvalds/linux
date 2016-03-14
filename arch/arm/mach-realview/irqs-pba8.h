/*
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

#ifndef __MACH_IRQS_PBA8_H
#define __MACH_IRQS_PBA8_H

#define IRQ_PBA8_GIC_START			32

/*
 * PB-A8 on-board gic irq sources
 */
#define IRQ_PBA8_WATCHDOG	(IRQ_PBA8_GIC_START + 0)	/* Watchdog timer */
#define IRQ_PBA8_SOFT		(IRQ_PBA8_GIC_START + 1)	/* Software interrupt */
#define IRQ_PBA8_COMMRx		(IRQ_PBA8_GIC_START + 2)	/* Debug Comm Rx interrupt */
#define IRQ_PBA8_COMMTx		(IRQ_PBA8_GIC_START + 3)	/* Debug Comm Tx interrupt */
#define IRQ_PBA8_TIMER0_1	(IRQ_PBA8_GIC_START + 4)	/* Timer 0/1 (default timer) */
#define IRQ_PBA8_TIMER2_3	(IRQ_PBA8_GIC_START + 5)	/* Timer 2/3 */
#define IRQ_PBA8_GPIO0		(IRQ_PBA8_GIC_START + 6)	/* GPIO 0 */
#define IRQ_PBA8_GPIO1		(IRQ_PBA8_GIC_START + 7)	/* GPIO 1 */
#define IRQ_PBA8_GPIO2		(IRQ_PBA8_GIC_START + 8)	/* GPIO 2 */
								/* 9 reserved */
#define IRQ_PBA8_RTC		(IRQ_PBA8_GIC_START + 10)	/* Real Time Clock */
#define IRQ_PBA8_SSP		(IRQ_PBA8_GIC_START + 11)	/* Synchronous Serial Port */
#define IRQ_PBA8_UART0		(IRQ_PBA8_GIC_START + 12)	/* UART 0 on development chip */
#define IRQ_PBA8_UART1		(IRQ_PBA8_GIC_START + 13)	/* UART 1 on development chip */
#define IRQ_PBA8_UART2		(IRQ_PBA8_GIC_START + 14)	/* UART 2 on development chip */
#define IRQ_PBA8_UART3		(IRQ_PBA8_GIC_START + 15)	/* UART 3 on development chip */
#define IRQ_PBA8_SCI		(IRQ_PBA8_GIC_START + 16)	/* Smart Card Interface */
#define IRQ_PBA8_MMCI0A		(IRQ_PBA8_GIC_START + 17)	/* Multimedia Card 0A */
#define IRQ_PBA8_MMCI0B		(IRQ_PBA8_GIC_START + 18)	/* Multimedia Card 0B */
#define IRQ_PBA8_AACI		(IRQ_PBA8_GIC_START + 19)	/* Audio Codec */
#define IRQ_PBA8_KMI0		(IRQ_PBA8_GIC_START + 20)	/* Keyboard/Mouse port 0 */
#define IRQ_PBA8_KMI1		(IRQ_PBA8_GIC_START + 21)	/* Keyboard/Mouse port 1 */
#define IRQ_PBA8_CHARLCD	(IRQ_PBA8_GIC_START + 22)	/* Character LCD */
#define IRQ_PBA8_CLCD		(IRQ_PBA8_GIC_START + 23)	/* CLCD controller */
#define IRQ_PBA8_DMAC		(IRQ_PBA8_GIC_START + 24)	/* DMA controller */
#define IRQ_PBA8_PWRFAIL	(IRQ_PBA8_GIC_START + 25)	/* Power failure */
#define IRQ_PBA8_PISMO		(IRQ_PBA8_GIC_START + 26)	/* PISMO interface */
#define IRQ_PBA8_DoC		(IRQ_PBA8_GIC_START + 27)	/* Disk on Chip memory controller */
#define IRQ_PBA8_ETH		(IRQ_PBA8_GIC_START + 28)	/* Ethernet controller */
#define IRQ_PBA8_USB		(IRQ_PBA8_GIC_START + 29)	/* USB controller */
#define IRQ_PBA8_TSPEN		(IRQ_PBA8_GIC_START + 30)	/* Touchscreen pen */
#define IRQ_PBA8_TSKPAD		(IRQ_PBA8_GIC_START + 31)	/* Touchscreen keypad */

#define IRQ_PBA8_PMU		(IRQ_PBA8_GIC_START + 47)	/* Cortex-A8 PMU */

/* ... */
#define IRQ_PBA8_PCI0		(IRQ_PBA8_GIC_START + 50)
#define IRQ_PBA8_PCI1		(IRQ_PBA8_GIC_START + 51)
#define IRQ_PBA8_PCI2		(IRQ_PBA8_GIC_START + 52)
#define IRQ_PBA8_PCI3		(IRQ_PBA8_GIC_START + 53)

#define IRQ_PBA8_SMC		-1
#define IRQ_PBA8_SCTL		-1

#endif	/* __MACH_IRQS_PBA8_H */
