/*
 * arch/arm/mach-spear3xx/include/mach/spear300.h
 *
 * SPEAr300 Machine specific definition
 *
 * Copyright (C) 2009 ST Microelectronics
 * Viresh Kumar<viresh.kumar@st.com>
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#ifdef	CONFIG_MACH_SPEAR300

#ifndef __MACH_SPEAR300_H
#define __MACH_SPEAR300_H

/* Base address of various IPs */
#define SPEAR300_TELECOM_BASE		0x50000000
#define SPEAR300_TELECOM_SIZE		0x10000000

#define SPEAR300_CLCD_BASE		0x60000000
#define SPEAR300_CLCD_SIZE		0x10000000

#define SPEAR300_SDIO_BASE		0x70000000
#define SPEAR300_SDIO_SIZE		0x10000000

#define SPEAR300_NAND_0_BASE		0x80000000
#define SPEAR300_NAND_0_SIZE		0x04000000

#define SPEAR300_NAND_1_BASE		0x84000000
#define SPEAR300_NAND_1_SIZE		0x04000000

#define SPEAR300_NAND_2_BASE		0x88000000
#define SPEAR300_NAND_2_SIZE		0x04000000

#define SPEAR300_NAND_3_BASE		0x8c000000
#define SPEAR300_NAND_3_SIZE		0x04000000

#define SPEAR300_NOR_0_BASE		0x90000000
#define SPEAR300_NOR_0_SIZE		0x01000000

#define SPEAR300_NOR_1_BASE		0x91000000
#define SPEAR300_NOR_1_SIZE		0x01000000

#define SPEAR300_NOR_2_BASE		0x92000000
#define SPEAR300_NOR_2_SIZE		0x01000000

#define SPEAR300_NOR_3_BASE		0x93000000
#define SPEAR300_NOR_3_SIZE		0x01000000

#define SPEAR300_FSMC_BASE		0x94000000
#define SPEAR300_FSMC_SIZE		0x05000000

#define SPEAR300_SOC_CONFIG_BASE	0x99000000
#define SPEAR300_SOC_CONFIG_SIZE	0x00000008

#define SPEAR300_KEYBOARD_BASE		0xA0000000
#define SPEAR300_KEYBOARD_SIZE		0x09000000

#define SPEAR300_GPIO_BASE		0xA9000000
#define SPEAR300_GPIO_SIZE		0x07000000

#endif /* __MACH_SPEAR300_H */

#endif /* CONFIG_MACH_SPEAR300 */
