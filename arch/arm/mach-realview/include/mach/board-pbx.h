/*
 * arch/arm/mach-realview/include/mach/board-pbx.h
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

#ifndef __ASM_ARCH_BOARD_PBX_H
#define __ASM_ARCH_BOARD_PBX_H

#include <mach/platform.h>

/*
 * Peripheral addresses
 */
#define REALVIEW_PBX_UART0_BASE			0x10009000	/* UART 0 */
#define REALVIEW_PBX_UART1_BASE			0x1000A000	/* UART 1 */
#define REALVIEW_PBX_UART2_BASE			0x1000B000	/* UART 2 */
#define REALVIEW_PBX_UART3_BASE			0x1000C000	/* UART 3 */
#define REALVIEW_PBX_SSP_BASE			0x1000D000	/* Synchronous Serial Port */
#define REALVIEW_PBX_WATCHDOG0_BASE		0x1000F000	/* Watchdog 0 */
#define REALVIEW_PBX_WATCHDOG_BASE		0x10010000	/* watchdog interface */
#define REALVIEW_PBX_TIMER0_1_BASE		0x10011000	/* Timer 0 and 1 */
#define REALVIEW_PBX_TIMER2_3_BASE		0x10012000	/* Timer 2 and 3 */
#define REALVIEW_PBX_GPIO0_BASE			0x10013000	/* GPIO port 0 */
#define REALVIEW_PBX_RTC_BASE			0x10017000	/* Real Time Clock */
#define REALVIEW_PBX_TIMER4_5_BASE		0x10018000	/* Timer 4/5 */
#define REALVIEW_PBX_TIMER6_7_BASE		0x10019000	/* Timer 6/7 */
#define REALVIEW_PBX_SCTL_BASE			0x1001A000	/* System Controller */
#define REALVIEW_PBX_CLCD_BASE			0x10020000	/* CLCD */
#define REALVIEW_PBX_ONB_SRAM_BASE		0x10060000	/* On-board SRAM */
#define REALVIEW_PBX_DMC_BASE			0x100E0000	/* DMC configuration */
#define REALVIEW_PBX_SMC_BASE			0x100E1000	/* SMC configuration */
#define REALVIEW_PBX_CAN_BASE			0x100E2000	/* CAN bus */
#define REALVIEW_PBX_GIC_CPU_BASE		0x1E000000	/* Generic interrupt controller CPU interface */
#define REALVIEW_PBX_FLASH0_BASE		0x40000000
#define REALVIEW_PBX_FLASH0_SIZE		SZ_64M
#define REALVIEW_PBX_FLASH1_BASE		0x44000000
#define REALVIEW_PBX_FLASH1_SIZE		SZ_64M
#define REALVIEW_PBX_ETH_BASE			0x4E000000	/* Ethernet */
#define REALVIEW_PBX_USB_BASE			0x4F000000	/* USB */
#define REALVIEW_PBX_GIC_DIST_BASE		0x1E001000	/* Generic interrupt controller distributor */
#define REALVIEW_PBX_LT_BASE			0xC0000000	/* Logic Tile expansion */
#define REALVIEW_PBX_SDRAM6_BASE		0x70000000	/* SDRAM bank 6 256MB */
#define REALVIEW_PBX_SDRAM7_BASE		0x80000000	/* SDRAM bank 7 256MB */

/*
 * Tile-specific addresses
 */
#define REALVIEW_PBX_TILE_SCU_BASE		0x1F000000      /* SCU registers */
#define REALVIEW_PBX_TILE_GIC_CPU_BASE		0x1F000100      /* Private Generic interrupt controller CPU interface */
#define REALVIEW_PBX_TILE_TWD_BASE		0x1F000600
#define REALVIEW_PBX_TILE_TWD_PERCPU_BASE	0x1F000700
#define REALVIEW_PBX_TILE_TWD_SIZE		0x00000100
#define REALVIEW_PBX_TILE_GIC_DIST_BASE		0x1F001000      /* Private Generic interrupt controller distributor */
#define REALVIEW_PBX_TILE_L220_BASE		0x1F002000      /* L220 registers */

#define REALVIEW_PBX_SYS_PLD_CTRL1		0x74

/*
 * PBX PCI regions
 */
#define REALVIEW_PBX_PCI_BASE			0x90040000	/* PCI-X Unit base */
#define REALVIEW_PBX_PCI_IO_BASE		0x90050000	/* IO Region on AHB */
#define REALVIEW_PBX_PCI_MEM_BASE		0xA0000000	/* MEM Region on AHB */

#define REALVIEW_PBX_PCI_BASE_SIZE		0x10000		/* 16 Kb */
#define REALVIEW_PBX_PCI_IO_SIZE		0x1000		/* 4 Kb */
#define REALVIEW_PBX_PCI_MEM_SIZE		0x20000000	/* 512 MB */

/*
 * Irqs
 */
#define IRQ_PBX_GIC_START			32

/* L220
#define IRQ_PBX_L220_EVENT	(IRQ_PBX_GIC_START + 29)
#define IRQ_PBX_L220_SLAVE	(IRQ_PBX_GIC_START + 30)
#define IRQ_PBX_L220_DECODE	(IRQ_PBX_GIC_START + 31)
*/

/*
 * PB-X on-board gic irq sources
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
#define IRQ_PBX_PMU_CPU3        (IRQ_PBX_GIC_START + 44)        /* CPU PMU Interrupts */
#define IRQ_PBX_PMU_CPU2        (IRQ_PBX_GIC_START + 45)
#define IRQ_PBX_PMU_CPU1        (IRQ_PBX_GIC_START + 46)
#define IRQ_PBX_PMU_CPU0        (IRQ_PBX_GIC_START + 47)

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

/*
 * Core tile identification (REALVIEW_SYS_PROCID)
 */
#define REALVIEW_PBX_PROC_MASK          0xFF000000
#define REALVIEW_PBX_PROC_ARM7TDMI      0x00000000
#define REALVIEW_PBX_PROC_ARM9          0x02000000
#define REALVIEW_PBX_PROC_ARM11         0x04000000
#define REALVIEW_PBX_PROC_ARM11MP       0x06000000
#define REALVIEW_PBX_PROC_A9MP          0x0C000000
#define REALVIEW_PBX_PROC_A8            0x0E000000

#define check_pbx_proc(proc_type)                                            \
	((readl(__io_address(REALVIEW_SYS_PROCID)) & REALVIEW_PBX_PROC_MASK) \
	== proc_type)

#ifdef CONFIG_MACH_REALVIEW_PBX
#define core_tile_pbx11mp()     check_pbx_proc(REALVIEW_PBX_PROC_ARM11MP)
#define core_tile_pbxa9mp()     check_pbx_proc(REALVIEW_PBX_PROC_A9MP)
#define core_tile_pbxa8()       check_pbx_proc(REALVIEW_PBX_PROC_A8)
#else
#define core_tile_pbx11mp()     0
#define core_tile_pbxa9mp()     0
#define core_tile_pbxa8()       0
#endif

#endif	/* __ASM_ARCH_BOARD_PBX_H */
