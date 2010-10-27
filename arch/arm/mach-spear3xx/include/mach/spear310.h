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

#define SPEAR310_NAND_BASE		0x40000000
#define SPEAR310_NAND_SIZE		0x04000000

#define SPEAR310_FSMC_BASE		0x44000000
#define SPEAR310_FSMC_SIZE		0x01000000

#define SPEAR310_UART1_BASE		0xB2000000
#define SPEAR310_UART2_BASE		0xB2080000
#define SPEAR310_UART3_BASE		0xB2100000
#define SPEAR310_UART4_BASE		0xB2180000
#define SPEAR310_UART5_BASE		0xB2200000
#define SPEAR310_UART_SIZE		0x00080000

#define SPEAR310_HDLC_BASE		0xB2800000
#define SPEAR310_HDLC_SIZE		0x00800000

#define SPEAR310_RS485_0_BASE		0xB3000000
#define SPEAR310_RS485_0_SIZE		0x00800000

#define SPEAR310_RS485_1_BASE		0xB3800000
#define SPEAR310_RS485_1_SIZE		0x00800000

#define SPEAR310_SOC_CONFIG_BASE	0xB4000000
#define SPEAR310_SOC_CONFIG_SIZE	0x00000070
/* Interrupt registers offsets and masks */
#define INT_STS_MASK_REG		0x04
#define SMII0_IRQ_MASK			(1 << 0)
#define SMII1_IRQ_MASK			(1 << 1)
#define SMII2_IRQ_MASK			(1 << 2)
#define SMII3_IRQ_MASK			(1 << 3)
#define WAKEUP_SMII0_IRQ_MASK		(1 << 4)
#define WAKEUP_SMII1_IRQ_MASK		(1 << 5)
#define WAKEUP_SMII2_IRQ_MASK		(1 << 6)
#define WAKEUP_SMII3_IRQ_MASK		(1 << 7)
#define UART1_IRQ_MASK			(1 << 8)
#define UART2_IRQ_MASK			(1 << 9)
#define UART3_IRQ_MASK			(1 << 10)
#define UART4_IRQ_MASK			(1 << 11)
#define UART5_IRQ_MASK			(1 << 12)
#define EMI_IRQ_MASK			(1 << 13)
#define TDM_HDLC_IRQ_MASK		(1 << 14)
#define RS485_0_IRQ_MASK		(1 << 15)
#define RS485_1_IRQ_MASK		(1 << 16)

#define SHIRQ_RAS1_MASK			0x000FF
#define SHIRQ_RAS2_MASK			0x01F00
#define SHIRQ_RAS3_MASK			0x02000
#define SHIRQ_INTRCOMM_RAS_MASK		0x1C000

#endif /* __MACH_SPEAR310_H */

#endif /* CONFIG_MACH_SPEAR310 */
