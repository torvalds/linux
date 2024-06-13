/* SPDX-License-Identifier: GPL-2.0-only */
/*
 *
 * Parts of this file are based on Ralink's 2.6.21 BSP
 *
 * Copyright (C) 2008-2011 Gabor Juhos <juhosg@openwrt.org>
 * Copyright (C) 2008 Imre Kaloz <kaloz@openwrt.org>
 * Copyright (C) 2013 John Crispin <john@phrozen.org>
 */

#ifndef _RT288X_REGS_H_
#define _RT288X_REGS_H_

#define RT2880_SYSC_BASE		0x00300000

#define SYSC_REG_CHIP_NAME0		0x00
#define SYSC_REG_CHIP_NAME1		0x04
#define SYSC_REG_CHIP_ID		0x0c
#define SYSC_REG_SYSTEM_CONFIG		0x10
#define SYSC_REG_CLKCFG			0x30

#define RT2880_CHIP_NAME0		0x38325452
#define RT2880_CHIP_NAME1		0x20203038

#define CHIP_ID_ID_MASK			0xff
#define CHIP_ID_ID_SHIFT		8
#define CHIP_ID_REV_MASK		0xff

#define SYSTEM_CONFIG_CPUCLK_SHIFT	20
#define SYSTEM_CONFIG_CPUCLK_MASK	0x3
#define SYSTEM_CONFIG_CPUCLK_250	0x0
#define SYSTEM_CONFIG_CPUCLK_266	0x1
#define SYSTEM_CONFIG_CPUCLK_280	0x2
#define SYSTEM_CONFIG_CPUCLK_300	0x3

#define CLKCFG_SRAM_CS_N_WDT		BIT(9)

#define RT2880_SDRAM_BASE		0x08000000
#define RT2880_MEM_SIZE_MIN		2
#define RT2880_MEM_SIZE_MAX		128

#endif
