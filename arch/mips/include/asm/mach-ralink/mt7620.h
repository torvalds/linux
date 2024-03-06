/* SPDX-License-Identifier: GPL-2.0-only */
/*
 *
 * Parts of this file are based on Ralink's 2.6.21 BSP
 *
 * Copyright (C) 2008-2011 Gabor Juhos <juhosg@openwrt.org>
 * Copyright (C) 2008 Imre Kaloz <kaloz@openwrt.org>
 * Copyright (C) 2013 John Crispin <john@phrozen.org>
 */

#ifndef _MT7620_REGS_H_
#define _MT7620_REGS_H_

#define IOMEM(x)			((void __iomem *)(KSEG1ADDR(x)))
#define MT7620_SYSC_BASE		IOMEM(0x10000000)

#define SYSC_REG_CHIP_NAME0		0x00
#define SYSC_REG_CHIP_NAME1		0x04
#define SYSC_REG_EFUSE_CFG		0x08
#define SYSC_REG_CHIP_REV		0x0c
#define SYSC_REG_SYSTEM_CONFIG0		0x10
#define SYSC_REG_SYSTEM_CONFIG1		0x14

#define MT7620_CHIP_NAME0		0x3637544d
#define MT7620_CHIP_NAME1		0x20203032
#define MT7628_CHIP_NAME1		0x20203832

#define CHIP_REV_PKG_MASK		0x1
#define CHIP_REV_PKG_SHIFT		16
#define CHIP_REV_VER_MASK		0xf
#define CHIP_REV_VER_SHIFT		8
#define CHIP_REV_ECO_MASK		0xf

#define SYSCFG0_DRAM_TYPE_MASK		0x3
#define SYSCFG0_DRAM_TYPE_SHIFT		4
#define SYSCFG0_DRAM_TYPE_SDRAM		0
#define SYSCFG0_DRAM_TYPE_DDR1		1
#define SYSCFG0_DRAM_TYPE_DDR2		2
#define SYSCFG0_DRAM_TYPE_UNKNOWN	3

#define SYSCFG0_DRAM_TYPE_DDR2_MT7628	0
#define SYSCFG0_DRAM_TYPE_DDR1_MT7628	1

#define MT7620_DRAM_BASE		0x0
#define MT7620_SDRAM_SIZE_MIN		2
#define MT7620_SDRAM_SIZE_MAX		64
#define MT7620_DDR1_SIZE_MIN		32
#define MT7620_DDR1_SIZE_MAX		128
#define MT7620_DDR2_SIZE_MIN		32
#define MT7620_DDR2_SIZE_MAX		256

extern enum ralink_soc_type ralink_soc;

static inline int is_mt76x8(void)
{
	return ralink_soc == MT762X_SOC_MT7628AN ||
	       ralink_soc == MT762X_SOC_MT7688;
}

static inline int mt7620_get_eco(void)
{
	return rt_sysc_r32(SYSC_REG_CHIP_REV) & CHIP_REV_ECO_MASK;
}

#endif
