/*
 * arch/arm/mach-spear3xx/include/mach/spear310.h
 *
 * SPEAr310 Machine specific definition
 *
 * Copyright (C) 2009 ST Microelectronics
 * Viresh Kumar<viresh.kumar@st.com>
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#ifdef	CONFIG_MACH_SPEAR310

#ifndef __MACH_SPEAR310_H
#define __MACH_SPEAR310_H

#define SPEAR310_NAND_BASE		UL(0x40000000)
#define SPEAR310_FSMC_BASE		UL(0x44000000)
#define SPEAR310_UART1_BASE		UL(0xB2000000)
#define SPEAR310_UART2_BASE		UL(0xB2080000)
#define SPEAR310_UART3_BASE		UL(0xB2100000)
#define SPEAR310_UART4_BASE		UL(0xB2180000)
#define SPEAR310_UART5_BASE		UL(0xB2200000)
#define SPEAR310_HDLC_BASE		UL(0xB2800000)
#define SPEAR310_RS485_0_BASE		UL(0xB3000000)
#define SPEAR310_RS485_1_BASE		UL(0xB3800000)
#define SPEAR310_SOC_CONFIG_BASE	UL(0xB4000000)

/* Interrupt registers offsets and masks */
#define SPEAR310_INT_STS_MASK_REG	0x04
#define SPEAR310_SMII0_IRQ_MASK		(1 << 0)
#define SPEAR310_SMII1_IRQ_MASK		(1 << 1)
#define SPEAR310_SMII2_IRQ_MASK		(1 << 2)
#define SPEAR310_SMII3_IRQ_MASK		(1 << 3)
#define SPEAR310_WAKEUP_SMII0_IRQ_MASK	(1 << 4)
#define SPEAR310_WAKEUP_SMII1_IRQ_MASK	(1 << 5)
#define SPEAR310_WAKEUP_SMII2_IRQ_MASK	(1 << 6)
#define SPEAR310_WAKEUP_SMII3_IRQ_MASK	(1 << 7)
#define SPEAR310_UART1_IRQ_MASK		(1 << 8)
#define SPEAR310_UART2_IRQ_MASK		(1 << 9)
#define SPEAR310_UART3_IRQ_MASK		(1 << 10)
#define SPEAR310_UART4_IRQ_MASK		(1 << 11)
#define SPEAR310_UART5_IRQ_MASK		(1 << 12)
#define SPEAR310_EMI_IRQ_MASK		(1 << 13)
#define SPEAR310_TDM_HDLC_IRQ_MASK	(1 << 14)
#define SPEAR310_RS485_0_IRQ_MASK	(1 << 15)
#define SPEAR310_RS485_1_IRQ_MASK	(1 << 16)

#define SPEAR310_SHIRQ_RAS1_MASK	0x000FF
#define SPEAR310_SHIRQ_RAS2_MASK	0x01F00
#define SPEAR310_SHIRQ_RAS3_MASK	0x02000
#define SPEAR310_SHIRQ_INTRCOMM_RAS_MASK	0x1C000

#endif /* __MACH_SPEAR310_H */

#endif /* CONFIG_MACH_SPEAR310 */
