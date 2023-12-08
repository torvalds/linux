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

#define MT7620_SYSC_BASE		0x10000000

#define SYSC_REG_CHIP_NAME0		0x00
#define SYSC_REG_CHIP_NAME1		0x04
#define SYSC_REG_EFUSE_CFG		0x08
#define SYSC_REG_CHIP_REV		0x0c
#define SYSC_REG_SYSTEM_CONFIG0		0x10
#define SYSC_REG_SYSTEM_CONFIG1		0x14
#define SYSC_REG_CLKCFG0		0x2c
#define SYSC_REG_CPU_SYS_CLKCFG		0x3c
#define SYSC_REG_CPLL_CONFIG0		0x54
#define SYSC_REG_CPLL_CONFIG1		0x58

#define MT7620_CHIP_NAME0		0x3637544d
#define MT7620_CHIP_NAME1		0x20203032
#define MT7628_CHIP_NAME1		0x20203832

#define SYSCFG0_XTAL_FREQ_SEL		BIT(6)

#define CHIP_REV_PKG_MASK		0x1
#define CHIP_REV_PKG_SHIFT		16
#define CHIP_REV_VER_MASK		0xf
#define CHIP_REV_VER_SHIFT		8
#define CHIP_REV_ECO_MASK		0xf

#define CLKCFG0_PERI_CLK_SEL		BIT(4)

#define CPU_SYS_CLKCFG_OCP_RATIO_SHIFT	16
#define CPU_SYS_CLKCFG_OCP_RATIO_MASK	0xf
#define CPU_SYS_CLKCFG_OCP_RATIO_1	0	/* 1:1   (Reserved) */
#define CPU_SYS_CLKCFG_OCP_RATIO_1_5	1	/* 1:1.5 (Reserved) */
#define CPU_SYS_CLKCFG_OCP_RATIO_2	2	/* 1:2   */
#define CPU_SYS_CLKCFG_OCP_RATIO_2_5	3       /* 1:2.5 (Reserved) */
#define CPU_SYS_CLKCFG_OCP_RATIO_3	4	/* 1:3   */
#define CPU_SYS_CLKCFG_OCP_RATIO_3_5	5	/* 1:3.5 (Reserved) */
#define CPU_SYS_CLKCFG_OCP_RATIO_4	6	/* 1:4   */
#define CPU_SYS_CLKCFG_OCP_RATIO_5	7	/* 1:5   */
#define CPU_SYS_CLKCFG_OCP_RATIO_10	8	/* 1:10  */
#define CPU_SYS_CLKCFG_CPU_FDIV_SHIFT	8
#define CPU_SYS_CLKCFG_CPU_FDIV_MASK	0x1f
#define CPU_SYS_CLKCFG_CPU_FFRAC_SHIFT	0
#define CPU_SYS_CLKCFG_CPU_FFRAC_MASK	0x1f

#define CPLL_CFG0_SW_CFG		BIT(31)
#define CPLL_CFG0_PLL_MULT_RATIO_SHIFT	16
#define CPLL_CFG0_PLL_MULT_RATIO_MASK   0x7
#define CPLL_CFG0_LC_CURFCK		BIT(15)
#define CPLL_CFG0_BYPASS_REF_CLK	BIT(14)
#define CPLL_CFG0_PLL_DIV_RATIO_SHIFT	10
#define CPLL_CFG0_PLL_DIV_RATIO_MASK	0x3

#define CPLL_CFG1_CPU_AUX1		BIT(25)
#define CPLL_CFG1_CPU_AUX0		BIT(24)

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
