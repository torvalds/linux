/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2003 Atheros Communications, Inc.,  All Rights Reserved.
 * Copyright (C) 2006 Imre Kaloz <kaloz@openwrt.org>
 * Copyright (C) 2006 Felix Fietkau <nbd@openwrt.org>
 */

#ifndef __ASM_MACH_ATH25_AR5312_REGS_H
#define __ASM_MACH_ATH25_AR5312_REGS_H

/*
 * IRQs
 */
#define AR5312_IRQ_WLAN0	(MIPS_CPU_IRQ_BASE + 2)	/* C0_CAUSE: 0x0400 */
#define AR5312_IRQ_ENET0	(MIPS_CPU_IRQ_BASE + 3)	/* C0_CAUSE: 0x0800 */
#define AR5312_IRQ_ENET1	(MIPS_CPU_IRQ_BASE + 4)	/* C0_CAUSE: 0x1000 */
#define AR5312_IRQ_WLAN1	(MIPS_CPU_IRQ_BASE + 5)	/* C0_CAUSE: 0x2000 */
#define AR5312_IRQ_MISC		(MIPS_CPU_IRQ_BASE + 6)	/* C0_CAUSE: 0x4000 */

/*
 * Miscellaneous interrupts, which share IP6.
 */
#define AR5312_MISC_IRQ_TIMER		0
#define AR5312_MISC_IRQ_AHB_PROC	1
#define AR5312_MISC_IRQ_AHB_DMA		2
#define AR5312_MISC_IRQ_GPIO		3
#define AR5312_MISC_IRQ_UART0		4
#define AR5312_MISC_IRQ_UART0_DMA	5
#define AR5312_MISC_IRQ_WATCHDOG	6
#define AR5312_MISC_IRQ_LOCAL		7
#define AR5312_MISC_IRQ_SPI		8
#define AR5312_MISC_IRQ_COUNT		9

/*
 * Address Map
 *
 * The AR5312 supports 2 enet MACS, even though many reference boards only
 * actually use 1 of them (i.e. Only MAC 0 is actually connected to an enet
 * PHY or PHY switch. The AR2312 supports 1 enet MAC.
 */
#define AR5312_WLAN0_BASE		0x18000000
#define AR5312_ENET0_BASE		0x18100000
#define AR5312_ENET1_BASE		0x18200000
#define AR5312_SDRAMCTL_BASE		0x18300000
#define AR5312_SDRAMCTL_SIZE		0x00000010
#define AR5312_FLASHCTL_BASE		0x18400000
#define AR5312_FLASHCTL_SIZE		0x00000010
#define AR5312_WLAN1_BASE		0x18500000
#define AR5312_UART0_BASE		0x1c000000	/* UART MMR */
#define AR5312_GPIO_BASE		0x1c002000
#define AR5312_GPIO_SIZE		0x00000010
#define AR5312_RST_BASE			0x1c003000
#define AR5312_RST_SIZE			0x00000100
#define AR5312_FLASH_BASE		0x1e000000
#define AR5312_FLASH_SIZE		0x00800000

/*
 * Need these defines to determine true number of ethernet MACs
 */
#define AR5312_AR5312_REV2	0x0052		/* AR5312 WMAC (AP31) */
#define AR5312_AR5312_REV7	0x0057		/* AR5312 WMAC (AP30-040) */
#define AR5312_AR2313_REV8	0x0058		/* AR2313 WMAC (AP43-030) */

/* Reset/Timer Block Address Map */
#define AR5312_TIMER		0x0000 /* countdown timer */
#define AR5312_RELOAD		0x0004 /* timer reload value */
#define AR5312_WDT_CTRL		0x0008 /* watchdog cntrl */
#define AR5312_WDT_TIMER	0x000c /* watchdog timer */
#define AR5312_ISR		0x0010 /* Intr Status Reg */
#define AR5312_IMR		0x0014 /* Intr Mask Reg */
#define AR5312_RESET		0x0020
#define AR5312_CLOCKCTL1	0x0064
#define AR5312_SCRATCH		0x006c
#define AR5312_PROCADDR		0x0070
#define AR5312_PROC1		0x0074
#define AR5312_DMAADDR		0x0078
#define AR5312_DMA1		0x007c
#define AR5312_ENABLE		0x0080 /* interface enb */
#define AR5312_REV		0x0090 /* revision */

/* AR5312_WDT_CTRL register bit field definitions */
#define AR5312_WDT_CTRL_IGNORE	0x00000000	/* ignore expiration */
#define AR5312_WDT_CTRL_NMI	0x00000001
#define AR5312_WDT_CTRL_RESET	0x00000002

/* AR5312_ISR register bit field definitions */
#define AR5312_ISR_TIMER	0x00000001
#define AR5312_ISR_AHBPROC	0x00000002
#define AR5312_ISR_AHBDMA	0x00000004
#define AR5312_ISR_GPIO		0x00000008
#define AR5312_ISR_UART0	0x00000010
#define AR5312_ISR_UART0DMA	0x00000020
#define AR5312_ISR_WD		0x00000040
#define AR5312_ISR_LOCAL	0x00000080

/* AR5312_RESET register bit field definitions */
#define AR5312_RESET_SYSTEM		0x00000001  /* cold reset full system */
#define AR5312_RESET_PROC		0x00000002  /* cold reset MIPS core */
#define AR5312_RESET_WLAN0		0x00000004  /* cold reset WLAN MAC/BB */
#define AR5312_RESET_EPHY0		0x00000008  /* cold reset ENET0 phy */
#define AR5312_RESET_EPHY1		0x00000010  /* cold reset ENET1 phy */
#define AR5312_RESET_ENET0		0x00000020  /* cold reset ENET0 MAC */
#define AR5312_RESET_ENET1		0x00000040  /* cold reset ENET1 MAC */
#define AR5312_RESET_UART0		0x00000100  /* cold reset UART0 */
#define AR5312_RESET_WLAN1		0x00000200  /* cold reset WLAN MAC/BB */
#define AR5312_RESET_APB		0x00000400  /* cold reset APB ar5312 */
#define AR5312_RESET_WARM_PROC		0x00001000  /* warm reset MIPS core */
#define AR5312_RESET_WARM_WLAN0_MAC	0x00002000  /* warm reset WLAN0 MAC */
#define AR5312_RESET_WARM_WLAN0_BB	0x00004000  /* warm reset WLAN0 BB */
#define AR5312_RESET_NMI		0x00010000  /* send an NMI to the CPU */
#define AR5312_RESET_WARM_WLAN1_MAC	0x00020000  /* warm reset WLAN1 MAC */
#define AR5312_RESET_WARM_WLAN1_BB	0x00040000  /* warm reset WLAN1 BB */
#define AR5312_RESET_LOCAL_BUS		0x00080000  /* reset local bus */
#define AR5312_RESET_WDOG		0x00100000  /* last reset was a wdt */

#define AR5312_RESET_WMAC0_BITS		(AR5312_RESET_WLAN0 |\
					 AR5312_RESET_WARM_WLAN0_MAC |\
					 AR5312_RESET_WARM_WLAN0_BB)

#define AR5312_RESET_WMAC1_BITS		(AR5312_RESET_WLAN1 |\
					 AR5312_RESET_WARM_WLAN1_MAC |\
					 AR5312_RESET_WARM_WLAN1_BB)

/* AR5312_CLOCKCTL1 register bit field definitions */
#define AR5312_CLOCKCTL1_PREDIVIDE_MASK		0x00000030
#define AR5312_CLOCKCTL1_PREDIVIDE_SHIFT	4
#define AR5312_CLOCKCTL1_MULTIPLIER_MASK	0x00001f00
#define AR5312_CLOCKCTL1_MULTIPLIER_SHIFT	8
#define AR5312_CLOCKCTL1_DOUBLER_MASK		0x00010000

/* Valid for AR5312 and AR2312 */
#define AR5312_CLOCKCTL1_PREDIVIDE_MASK		0x00000030
#define AR5312_CLOCKCTL1_PREDIVIDE_SHIFT	4
#define AR5312_CLOCKCTL1_MULTIPLIER_MASK	0x00001f00
#define AR5312_CLOCKCTL1_MULTIPLIER_SHIFT	8
#define AR5312_CLOCKCTL1_DOUBLER_MASK		0x00010000

/* Valid for AR2313 */
#define AR2313_CLOCKCTL1_PREDIVIDE_MASK		0x00003000
#define AR2313_CLOCKCTL1_PREDIVIDE_SHIFT	12
#define AR2313_CLOCKCTL1_MULTIPLIER_MASK	0x001f0000
#define AR2313_CLOCKCTL1_MULTIPLIER_SHIFT	16
#define AR2313_CLOCKCTL1_DOUBLER_MASK		0x00000000

/* AR5312_ENABLE register bit field definitions */
#define AR5312_ENABLE_WLAN0			0x00000001
#define AR5312_ENABLE_ENET0			0x00000002
#define AR5312_ENABLE_ENET1			0x00000004
#define AR5312_ENABLE_UART_AND_WLAN1_PIO	0x00000008/* UART & WLAN1 PIO */
#define AR5312_ENABLE_WLAN1_DMA			0x00000010/* WLAN1 DMAs */
#define AR5312_ENABLE_WLAN1		(AR5312_ENABLE_UART_AND_WLAN1_PIO |\
					 AR5312_ENABLE_WLAN1_DMA)

/* AR5312_REV register bit field definitions */
#define AR5312_REV_WMAC_MAJ	0x0000f000
#define AR5312_REV_WMAC_MAJ_S	12
#define AR5312_REV_WMAC_MIN	0x00000f00
#define AR5312_REV_WMAC_MIN_S	8
#define AR5312_REV_MAJ		0x000000f0
#define AR5312_REV_MAJ_S	4
#define AR5312_REV_MIN		0x0000000f
#define AR5312_REV_MIN_S	0
#define AR5312_REV_CHIP		(AR5312_REV_MAJ|AR5312_REV_MIN)

/* Major revision numbers, bits 7..4 of Revision ID register */
#define AR5312_REV_MAJ_AR5312		0x4
#define AR5312_REV_MAJ_AR2313		0x5

/* Minor revision numbers, bits 3..0 of Revision ID register */
#define AR5312_REV_MIN_DUAL		0x0	/* Dual WLAN version */
#define AR5312_REV_MIN_SINGLE		0x1	/* Single WLAN version */

/*
 * ARM Flash Controller -- 3 flash banks with either x8 or x16 devices
 */
#define AR5312_FLASHCTL0	0x0000
#define AR5312_FLASHCTL1	0x0004
#define AR5312_FLASHCTL2	0x0008

/* AR5312_FLASHCTL register bit field definitions */
#define AR5312_FLASHCTL_IDCY	0x0000000f	/* Idle cycle turnaround time */
#define AR5312_FLASHCTL_IDCY_S	0
#define AR5312_FLASHCTL_WST1	0x000003e0	/* Wait state 1 */
#define AR5312_FLASHCTL_WST1_S	5
#define AR5312_FLASHCTL_RBLE	0x00000400	/* Read byte lane enable */
#define AR5312_FLASHCTL_WST2	0x0000f800	/* Wait state 2 */
#define AR5312_FLASHCTL_WST2_S	11
#define AR5312_FLASHCTL_AC	0x00070000	/* Flash addr check (added) */
#define AR5312_FLASHCTL_AC_S	16
#define AR5312_FLASHCTL_AC_128K	0x00000000
#define AR5312_FLASHCTL_AC_256K	0x00010000
#define AR5312_FLASHCTL_AC_512K	0x00020000
#define AR5312_FLASHCTL_AC_1M	0x00030000
#define AR5312_FLASHCTL_AC_2M	0x00040000
#define AR5312_FLASHCTL_AC_4M	0x00050000
#define AR5312_FLASHCTL_AC_8M	0x00060000
#define AR5312_FLASHCTL_AC_RES	0x00070000	/* 16MB is not supported */
#define AR5312_FLASHCTL_E	0x00080000	/* Flash bank enable (added) */
#define AR5312_FLASHCTL_BUSERR	0x01000000	/* Bus transfer error flag */
#define AR5312_FLASHCTL_WPERR	0x02000000	/* Write protect error flag */
#define AR5312_FLASHCTL_WP	0x04000000	/* Write protect */
#define AR5312_FLASHCTL_BM	0x08000000	/* Burst mode */
#define AR5312_FLASHCTL_MW	0x30000000	/* Mem width */
#define AR5312_FLASHCTL_MW8	0x00000000	/* Mem width x8 */
#define AR5312_FLASHCTL_MW16	0x10000000	/* Mem width x16 */
#define AR5312_FLASHCTL_MW32	0x20000000	/* Mem width x32 (not supp) */
#define AR5312_FLASHCTL_ATNR	0x00000000	/* Access == no retry */
#define AR5312_FLASHCTL_ATR	0x80000000	/* Access == retry every */
#define AR5312_FLASHCTL_ATR4	0xc0000000	/* Access == retry every 4 */

/*
 * ARM SDRAM Controller -- just enough to determine memory size
 */
#define AR5312_MEM_CFG1		0x0004

#define AR5312_MEM_CFG1_AC0_M	0x00000700	/* bank 0: SDRAM addr check */
#define AR5312_MEM_CFG1_AC0_S	8
#define AR5312_MEM_CFG1_AC1_M	0x00007000	/* bank 1: SDRAM addr check */
#define AR5312_MEM_CFG1_AC1_S	12

#endif	/* __ASM_MACH_ATH25_AR5312_REGS_H */
