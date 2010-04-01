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

#endif /* __MACH_SPEAR310_H */

#endif /* CONFIG_MACH_SPEAR310 */
