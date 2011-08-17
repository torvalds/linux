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
#define SPEAR300_TELECOM_BASE		UL(0x50000000)

/* Interrupt registers offsets and masks */
#define SPEAR300_INT_ENB_MASK_REG	0x54
#define SPEAR300_INT_STS_MASK_REG	0x58
#define SPEAR300_IT_PERS_S_IRQ_MASK	(1 << 0)
#define SPEAR300_IT_CHANGE_S_IRQ_MASK	(1 << 1)
#define SPEAR300_I2S_IRQ_MASK		(1 << 2)
#define SPEAR300_TDM_IRQ_MASK		(1 << 3)
#define SPEAR300_CAMERA_L_IRQ_MASK	(1 << 4)
#define SPEAR300_CAMERA_F_IRQ_MASK	(1 << 5)
#define SPEAR300_CAMERA_V_IRQ_MASK	(1 << 6)
#define SPEAR300_KEYBOARD_IRQ_MASK	(1 << 7)
#define SPEAR300_GPIO1_IRQ_MASK		(1 << 8)

#define SPEAR300_SHIRQ_RAS1_MASK	0x1FF

#define SPEAR300_CLCD_BASE		UL(0x60000000)
#define SPEAR300_SDHCI_BASE		UL(0x70000000)
#define SPEAR300_NAND_0_BASE		UL(0x80000000)
#define SPEAR300_NAND_1_BASE		UL(0x84000000)
#define SPEAR300_NAND_2_BASE		UL(0x88000000)
#define SPEAR300_NAND_3_BASE		UL(0x8c000000)
#define SPEAR300_NOR_0_BASE		UL(0x90000000)
#define SPEAR300_NOR_1_BASE		UL(0x91000000)
#define SPEAR300_NOR_2_BASE		UL(0x92000000)
#define SPEAR300_NOR_3_BASE		UL(0x93000000)
#define SPEAR300_FSMC_BASE		UL(0x94000000)
#define SPEAR300_SOC_CONFIG_BASE	UL(0x99000000)
#define SPEAR300_KEYBOARD_BASE		UL(0xA0000000)
#define SPEAR300_GPIO_BASE		UL(0xA9000000)

#endif /* __MACH_SPEAR300_H */

#endif /* CONFIG_MACH_SPEAR300 */
