/*
 * include/asm-arm/arch-realview/board-pba8.h
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

#ifndef __ASM_ARCH_BOARD_PBA8_H
#define __ASM_ARCH_BOARD_PBA8_H

#include <mach/platform.h>

/*
 * Peripheral addresses
 */
#define REALVIEW_PBA8_UART0_BASE		0x10009000	/* UART 0 */
#define REALVIEW_PBA8_UART1_BASE		0x1000A000	/* UART 1 */
#define REALVIEW_PBA8_UART2_BASE		0x1000B000	/* UART 2 */
#define REALVIEW_PBA8_UART3_BASE		0x1000C000	/* UART 3 */
#define REALVIEW_PBA8_SSP_BASE			0x1000D000	/* Synchronous Serial Port */
#define REALVIEW_PBA8_WATCHDOG0_BASE		0x1000F000	/* Watchdog 0 */
#define REALVIEW_PBA8_WATCHDOG_BASE		0x10010000	/* watchdog interface */
#define REALVIEW_PBA8_TIMER0_1_BASE		0x10011000	/* Timer 0 and 1 */
#define REALVIEW_PBA8_TIMER2_3_BASE		0x10012000	/* Timer 2 and 3 */
#define REALVIEW_PBA8_GPIO0_BASE		0x10013000	/* GPIO port 0 */
#define REALVIEW_PBA8_RTC_BASE			0x10017000	/* Real Time Clock */
#define REALVIEW_PBA8_TIMER4_5_BASE		0x10018000	/* Timer 4/5 */
#define REALVIEW_PBA8_TIMER6_7_BASE		0x10019000	/* Timer 6/7 */
#define REALVIEW_PBA8_SCTL_BASE			0x1001A000	/* System Controller */
#define REALVIEW_PBA8_CLCD_BASE			0x10020000	/* CLCD */
#define REALVIEW_PBA8_ONB_SRAM_BASE		0x10060000	/* On-board SRAM */
#define REALVIEW_PBA8_DMC_BASE			0x100E0000	/* DMC configuration */
#define REALVIEW_PBA8_SMC_BASE			0x100E1000	/* SMC configuration */
#define REALVIEW_PBA8_CAN_BASE			0x100E2000	/* CAN bus */
#define REALVIEW_PBA8_GIC_CPU_BASE		0x1E000000	/* Generic interrupt controller CPU interface */
#define REALVIEW_PBA8_FLASH0_BASE		0x40000000
#define REALVIEW_PBA8_FLASH0_SIZE		SZ_64M
#define REALVIEW_PBA8_FLASH1_BASE		0x44000000
#define REALVIEW_PBA8_FLASH1_SIZE		SZ_64M
#define REALVIEW_PBA8_ETH_BASE			0x4E000000	/* Ethernet */
#define REALVIEW_PBA8_USB_BASE			0x4F000000	/* USB */
#define REALVIEW_PBA8_GIC_DIST_BASE		0x1E001000	/* Generic interrupt controller distributor */
#define REALVIEW_PBA8_LT_BASE			0xC0000000	/* Logic Tile expansion */
#define REALVIEW_PBA8_SDRAM6_BASE		0x70000000	/* SDRAM bank 6 256MB */
#define REALVIEW_PBA8_SDRAM7_BASE		0x80000000	/* SDRAM bank 7 256MB */

#define REALVIEW_PBA8_SYS_PLD_CTRL1		0x74

/*
 * PBA8 PCI regions
 */
#define REALVIEW_PBA8_PCI_BASE			0x90040000	/* PCI-X Unit base */
#define REALVIEW_PBA8_PCI_IO_BASE		0x90050000	/* IO Region on AHB */
#define REALVIEW_PBA8_PCI_MEM_BASE		0xA0000000	/* MEM Region on AHB */

#define REALVIEW_PBA8_PCI_BASE_SIZE		0x10000		/* 16 Kb */
#define REALVIEW_PBA8_PCI_IO_SIZE		0x1000		/* 4 Kb */
#define REALVIEW_PBA8_PCI_MEM_SIZE		0x20000000	/* 512 MB */

/*
 * Irqs
 */
#define IRQ_PBA8_GIC_START			32

/* L220
#define IRQ_PBA8_L220_EVENT	(IRQ_PBA8_GIC_START + 29)
#define IRQ_PBA8_L220_SLAVE	(IRQ_PBA8_GIC_START + 30)
#define IRQ_PBA8_L220_DECODE	(IRQ_PBA8_GIC_START + 31)
*/

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

/* ... */
#define IRQ_PBA8_PCI0		(IRQ_PBA8_GIC_START + 50)
#define IRQ_PBA8_PCI1		(IRQ_PBA8_GIC_START + 51)
#define IRQ_PBA8_PCI2		(IRQ_PBA8_GIC_START + 52)
#define IRQ_PBA8_PCI3		(IRQ_PBA8_GIC_START + 53)

#define IRQ_PBA8_SMC		-1
#define IRQ_PBA8_SCTL		-1

#define NR_GIC_PBA8		1

/*
 * Only define NR_IRQS if less than NR_IRQS_PBA8
 */
#define NR_IRQS_PBA8		(IRQ_PBA8_GIC_START + 64)

#if defined(CONFIG_MACH_REALVIEW_PBA8)

#if !defined(NR_IRQS) || (NR_IRQS < NR_IRQS_PBA8)
#undef NR_IRQS
#define NR_IRQS			NR_IRQS_PBA8
#endif

#if !defined(MAX_GIC_NR) || (MAX_GIC_NR < NR_GIC_PBA8)
#undef MAX_GIC_NR
#define MAX_GIC_NR		NR_GIC_PBA8
#endif

#endif	/* CONFIG_MACH_REALVIEW_PBA8 */

#endif	/* __ASM_ARCH_BOARD_PBA8_H */
