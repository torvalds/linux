/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * arch/arm/mach-pxa/include/mach/viper.h
 *
 * Author:	Ian Campbell
 * Created:	Feb 03, 2003
 * Copyright:	Arcom Control Systems.
 *
 * Maintained by Marc Zyngier <maz@misterjones.org>
 *			      <marc.zyngier@altran.com>
 *
 * Created based on lubbock.h:
 *  Author:	Nicolas Pitre
 *  Created:	Jun 15, 2001
 *  Copyright:	MontaVista Software Inc.
 */

#ifndef ARCH_VIPER_H
#define ARCH_VIPER_H

#define VIPER_BOOT_PHYS		PXA_CS0_PHYS
#define VIPER_FLASH_PHYS	PXA_CS1_PHYS
#define VIPER_ETH_PHYS		PXA_CS2_PHYS
#define VIPER_USB_PHYS		PXA_CS3_PHYS
#define VIPER_ETH_DATA_PHYS	PXA_CS4_PHYS
#define VIPER_CPLD_PHYS		PXA_CS5_PHYS

#define VIPER_CPLD_BASE		(0xf0000000)
#define VIPER_PC104IO_BASE	(0xf1000000)
#define VIPER_USB_BASE		(0xf1800000)

#define VIPER_ETH_GPIO		(0)
#define VIPER_CPLD_GPIO		(1)
#define VIPER_USB_GPIO		(2)
#define VIPER_UARTA_GPIO	(4)
#define VIPER_UARTB_GPIO	(3)
#define VIPER_CF_CD_GPIO	(32)
#define VIPER_CF_RDY_GPIO	(8)
#define VIPER_BCKLIGHT_EN_GPIO	(9)
#define VIPER_LCD_EN_GPIO	(10)
#define VIPER_PSU_DATA_GPIO	(6)
#define VIPER_PSU_CLK_GPIO	(11)
#define VIPER_UART_SHDN_GPIO	(12)
#define VIPER_BRIGHTNESS_GPIO	(16)
#define VIPER_PSU_nCS_LD_GPIO	(19)
#define VIPER_UPS_GPIO		(20)
#define VIPER_CF_POWER_GPIO	(82)
#define VIPER_TPM_I2C_SDA_GPIO	(26)
#define VIPER_TPM_I2C_SCL_GPIO	(27)
#define VIPER_RTC_I2C_SDA_GPIO	(83)
#define VIPER_RTC_I2C_SCL_GPIO	(84)

#define VIPER_CPLD_P2V(x)	((x) - VIPER_CPLD_PHYS + VIPER_CPLD_BASE)
#define VIPER_CPLD_V2P(x)	((x) - VIPER_CPLD_BASE + VIPER_CPLD_PHYS)

#ifndef __ASSEMBLY__
#  define __VIPER_CPLD_REG(x)	(*((volatile u16 *)VIPER_CPLD_P2V(x)))
#endif

/* board level registers in the CPLD: (offsets from CPLD_BASE) ... */

/* ... Physical addresses */
#define _VIPER_LO_IRQ_STATUS	(VIPER_CPLD_PHYS + 0x100000)
#define _VIPER_ICR_PHYS		(VIPER_CPLD_PHYS + 0x100002)
#define _VIPER_HI_IRQ_STATUS	(VIPER_CPLD_PHYS + 0x100004)
#define _VIPER_VERSION_PHYS	(VIPER_CPLD_PHYS + 0x100006)
#define VIPER_UARTA_PHYS	(VIPER_CPLD_PHYS + 0x300010)
#define VIPER_UARTB_PHYS	(VIPER_CPLD_PHYS + 0x300000)
#define _VIPER_SRAM_BASE	(VIPER_CPLD_PHYS + 0x800000)

/* ... Virtual addresses */
#define VIPER_LO_IRQ_STATUS	__VIPER_CPLD_REG(_VIPER_LO_IRQ_STATUS)
#define VIPER_HI_IRQ_STATUS	__VIPER_CPLD_REG(_VIPER_HI_IRQ_STATUS)
#define VIPER_VERSION		__VIPER_CPLD_REG(_VIPER_VERSION_PHYS)
#define VIPER_ICR		__VIPER_CPLD_REG(_VIPER_ICR_PHYS)

/* Decode VIPER_VERSION register */
#define VIPER_CPLD_REVISION(x)	(((x) >> 5) & 0x7)
#define VIPER_BOARD_VERSION(x)	(((x) >> 3) & 0x3)
#define VIPER_BOARD_ISSUE(x)	(((x) >> 0) & 0x7)

/* Interrupt and Configuration Register (VIPER_ICR) */
/* This is a write only register. Only CF_RST is used under Linux */

#define VIPER_ICR_RETRIG	(1 << 0)
#define VIPER_ICR_AUTO_CLR	(1 << 1)
#define VIPER_ICR_R_DIS		(1 << 2)
#define VIPER_ICR_CF_RST	(1 << 3)

#endif

