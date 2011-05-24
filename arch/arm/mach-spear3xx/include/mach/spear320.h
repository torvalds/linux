/*
 * arch/arm/mach-spear3xx/include/mach/spear320.h
 *
 * SPEAr320 Machine specific definition
 *
 * Copyright (C) 2009 ST Microelectronics
 * Viresh Kumar<viresh.kumar@st.com>
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#ifdef	CONFIG_MACH_SPEAR320

#ifndef __MACH_SPEAR320_H
#define __MACH_SPEAR320_H

#define SPEAR320_EMI_CTRL_BASE		UL(0x40000000)
#define SPEAR320_FSMC_BASE		UL(0x4C000000)
#define SPEAR320_NAND_BASE		UL(0x50000000)
#define SPEAR320_I2S_BASE		UL(0x60000000)
#define SPEAR320_SDHCI_BASE		UL(0x70000000)
#define SPEAR320_CLCD_BASE		UL(0x90000000)
#define SPEAR320_PAR_PORT_BASE		UL(0xA0000000)
#define SPEAR320_CAN0_BASE		UL(0xA1000000)
#define SPEAR320_CAN1_BASE		UL(0xA2000000)
#define SPEAR320_UART1_BASE		UL(0xA3000000)
#define SPEAR320_UART2_BASE		UL(0xA4000000)
#define SPEAR320_SSP0_BASE		UL(0xA5000000)
#define SPEAR320_SSP1_BASE		UL(0xA6000000)
#define SPEAR320_I2C_BASE		UL(0xA7000000)
#define SPEAR320_PWM_BASE		UL(0xA8000000)
#define SPEAR320_SMII0_BASE		UL(0xAA000000)
#define SPEAR320_SMII1_BASE		UL(0xAB000000)
#define SPEAR320_SOC_CONFIG_BASE	UL(0xB3000000)

/* Interrupt registers offsets and masks */
#define INT_STS_MASK_REG		0x04
#define INT_CLR_MASK_REG		0x04
#define INT_ENB_MASK_REG		0x08
#define GPIO_IRQ_MASK			(1 << 0)
#define I2S_PLAY_IRQ_MASK		(1 << 1)
#define I2S_REC_IRQ_MASK		(1 << 2)
#define EMI_IRQ_MASK			(1 << 7)
#define CLCD_IRQ_MASK			(1 << 8)
#define SPP_IRQ_MASK			(1 << 9)
#define SDHCI_IRQ_MASK			(1 << 10)
#define CAN_U_IRQ_MASK			(1 << 11)
#define CAN_L_IRQ_MASK			(1 << 12)
#define UART1_IRQ_MASK			(1 << 13)
#define UART2_IRQ_MASK			(1 << 14)
#define SSP1_IRQ_MASK			(1 << 15)
#define SSP2_IRQ_MASK			(1 << 16)
#define SMII0_IRQ_MASK			(1 << 17)
#define MII1_SMII1_IRQ_MASK		(1 << 18)
#define WAKEUP_SMII0_IRQ_MASK		(1 << 19)
#define WAKEUP_MII1_SMII1_IRQ_MASK	(1 << 20)
#define I2C1_IRQ_MASK			(1 << 21)

#define SHIRQ_RAS1_MASK			0x000380
#define SHIRQ_RAS3_MASK			0x000007
#define SHIRQ_INTRCOMM_RAS_MASK		0x3FF800

#endif /* __MACH_SPEAR320_H */

#endif /* CONFIG_MACH_SPEAR320 */
