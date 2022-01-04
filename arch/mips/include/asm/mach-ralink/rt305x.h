/* SPDX-License-Identifier: GPL-2.0-only */
/*
 *
 * Parts of this file are based on Ralink's 2.6.21 BSP
 *
 * Copyright (C) 2008-2011 Gabor Juhos <juhosg@openwrt.org>
 * Copyright (C) 2008 Imre Kaloz <kaloz@openwrt.org>
 * Copyright (C) 2013 John Crispin <john@phrozen.org>
 */

#ifndef _RT305X_REGS_H_
#define _RT305X_REGS_H_

extern enum ralink_soc_type ralink_soc;

static inline int soc_is_rt3050(void)
{
	return ralink_soc == RT305X_SOC_RT3050;
}

static inline int soc_is_rt3052(void)
{
	return ralink_soc == RT305X_SOC_RT3052;
}

static inline int soc_is_rt305x(void)
{
	return soc_is_rt3050() || soc_is_rt3052();
}

static inline int soc_is_rt3350(void)
{
	return ralink_soc == RT305X_SOC_RT3350;
}

static inline int soc_is_rt3352(void)
{
	return ralink_soc == RT305X_SOC_RT3352;
}

static inline int soc_is_rt5350(void)
{
	return ralink_soc == RT305X_SOC_RT5350;
}

#define RT305X_SYSC_BASE		0x10000000

#define SYSC_REG_CHIP_NAME0		0x00
#define SYSC_REG_CHIP_NAME1		0x04
#define SYSC_REG_CHIP_ID		0x0c
#define SYSC_REG_SYSTEM_CONFIG		0x10

#define RT3052_CHIP_NAME0		0x30335452
#define RT3052_CHIP_NAME1		0x20203235

#define RT3350_CHIP_NAME0		0x33335452
#define RT3350_CHIP_NAME1		0x20203035

#define RT3352_CHIP_NAME0		0x33335452
#define RT3352_CHIP_NAME1		0x20203235

#define RT5350_CHIP_NAME0		0x33355452
#define RT5350_CHIP_NAME1		0x20203035

#define CHIP_ID_ID_MASK			0xff
#define CHIP_ID_ID_SHIFT		8
#define CHIP_ID_REV_MASK		0xff

#define RT305X_SYSCFG_CPUCLK_SHIFT		18
#define RT305X_SYSCFG_CPUCLK_MASK		0x1
#define RT305X_SYSCFG_CPUCLK_LOW		0x0
#define RT305X_SYSCFG_CPUCLK_HIGH		0x1

#define RT305X_SYSCFG_SRAM_CS0_MODE_SHIFT	2
#define RT305X_SYSCFG_CPUCLK_MASK		0x1
#define RT305X_SYSCFG_SRAM_CS0_MODE_WDT		0x1

#define RT3352_SYSCFG0_CPUCLK_SHIFT	8
#define RT3352_SYSCFG0_CPUCLK_MASK	0x1
#define RT3352_SYSCFG0_CPUCLK_LOW	0x0
#define RT3352_SYSCFG0_CPUCLK_HIGH	0x1

#define RT5350_SYSCFG0_CPUCLK_SHIFT	8
#define RT5350_SYSCFG0_CPUCLK_MASK	0x3
#define RT5350_SYSCFG0_CPUCLK_360	0x0
#define RT5350_SYSCFG0_CPUCLK_320	0x2
#define RT5350_SYSCFG0_CPUCLK_300	0x3

#define RT5350_SYSCFG0_DRAM_SIZE_SHIFT  12
#define RT5350_SYSCFG0_DRAM_SIZE_MASK   7
#define RT5350_SYSCFG0_DRAM_SIZE_2M     0
#define RT5350_SYSCFG0_DRAM_SIZE_8M     1
#define RT5350_SYSCFG0_DRAM_SIZE_16M    2
#define RT5350_SYSCFG0_DRAM_SIZE_32M    3
#define RT5350_SYSCFG0_DRAM_SIZE_64M    4

/* multi function gpio pins */
#define RT305X_GPIO_I2C_SD		1
#define RT305X_GPIO_I2C_SCLK		2
#define RT305X_GPIO_SPI_EN		3
#define RT305X_GPIO_SPI_CLK		4
/* GPIO 7-14 is shared between UART0, PCM  and I2S interfaces */
#define RT305X_GPIO_7			7
#define RT305X_GPIO_10			10
#define RT305X_GPIO_14			14
#define RT305X_GPIO_UART1_TXD		15
#define RT305X_GPIO_UART1_RXD		16
#define RT305X_GPIO_JTAG_TDO		17
#define RT305X_GPIO_JTAG_TDI		18
#define RT305X_GPIO_MDIO_MDC		22
#define RT305X_GPIO_MDIO_MDIO		23
#define RT305X_GPIO_SDRAM_MD16		24
#define RT305X_GPIO_SDRAM_MD31		39
#define RT305X_GPIO_GE0_TXD0		40
#define RT305X_GPIO_GE0_RXCLK		51

#define RT3352_SYSC_REG_SYSCFG0		0x010
#define RT3352_SYSC_REG_SYSCFG1         0x014
#define RT3352_SYSC_REG_CLKCFG1         0x030
#define RT3352_SYSC_REG_RSTCTRL         0x034
#define RT3352_SYSC_REG_USB_PS          0x05c

#define RT3352_CLKCFG0_XTAL_SEL		BIT(20)
#define RT3352_CLKCFG1_UPHY0_CLK_EN	BIT(18)
#define RT3352_CLKCFG1_UPHY1_CLK_EN	BIT(20)
#define RT3352_RSTCTRL_UHST		BIT(22)
#define RT3352_RSTCTRL_UDEV		BIT(25)
#define RT3352_SYSCFG1_USB0_HOST_MODE	BIT(10)

#define RT305X_SDRAM_BASE		0x00000000
#define RT305X_MEM_SIZE_MIN		2
#define RT305X_MEM_SIZE_MAX		64
#define RT3352_MEM_SIZE_MIN		2
#define RT3352_MEM_SIZE_MAX		256

#endif
