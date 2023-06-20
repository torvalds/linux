// SPDX-License-Identifier: GPL-2.0-only
/*
 *
 * Parts of this file are based on Ralink's 2.6.21 BSP
 *
 * Copyright (C) 2008-2011 Gabor Juhos <juhosg@openwrt.org>
 * Copyright (C) 2008 Imre Kaloz <kaloz@openwrt.org>
 * Copyright (C) 2013 John Crispin <john@phrozen.org>
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/bug.h>
#include <linux/slab.h>
#include <linux/sys_soc.h>

#include <asm/mipsregs.h>
#include <asm/mach-ralink/ralink_regs.h>
#include <asm/mach-ralink/mt7620.h>

#include "common.h"

/* analog */
#define PMU0_CFG		0x88
#define PMU_SW_SET		BIT(28)
#define A_DCDC_EN		BIT(24)
#define A_SSC_PERI		BIT(19)
#define A_SSC_GEN		BIT(18)
#define A_SSC_M			0x3
#define A_SSC_S			16
#define A_DLY_M			0x7
#define A_DLY_S			8
#define A_VTUNE_M		0xff

/* digital */
#define PMU1_CFG		0x8C
#define DIG_SW_SEL		BIT(25)

/* EFUSE bits */
#define EFUSE_MT7688		0x100000

/* DRAM type bit */
#define DRAM_TYPE_MT7628_MASK	0x1

/* does the board have sdram or ddram */
static int dram_type;

static struct ralink_soc_info *soc_info_ptr;

static __init void
mt7620_dram_init(struct ralink_soc_info *soc_info)
{
	switch (dram_type) {
	case SYSCFG0_DRAM_TYPE_SDRAM:
		pr_info("Board has SDRAM\n");
		soc_info->mem_size_min = MT7620_SDRAM_SIZE_MIN;
		soc_info->mem_size_max = MT7620_SDRAM_SIZE_MAX;
		break;

	case SYSCFG0_DRAM_TYPE_DDR1:
		pr_info("Board has DDR1\n");
		soc_info->mem_size_min = MT7620_DDR1_SIZE_MIN;
		soc_info->mem_size_max = MT7620_DDR1_SIZE_MAX;
		break;

	case SYSCFG0_DRAM_TYPE_DDR2:
		pr_info("Board has DDR2\n");
		soc_info->mem_size_min = MT7620_DDR2_SIZE_MIN;
		soc_info->mem_size_max = MT7620_DDR2_SIZE_MAX;
		break;
	default:
		BUG();
	}
}

static __init void
mt7628_dram_init(struct ralink_soc_info *soc_info)
{
	switch (dram_type) {
	case SYSCFG0_DRAM_TYPE_DDR1_MT7628:
		pr_info("Board has DDR1\n");
		soc_info->mem_size_min = MT7620_DDR1_SIZE_MIN;
		soc_info->mem_size_max = MT7620_DDR1_SIZE_MAX;
		break;

	case SYSCFG0_DRAM_TYPE_DDR2_MT7628:
		pr_info("Board has DDR2\n");
		soc_info->mem_size_min = MT7620_DDR2_SIZE_MIN;
		soc_info->mem_size_max = MT7620_DDR2_SIZE_MAX;
		break;
	default:
		BUG();
	}
}

static unsigned int __init mt7620_get_soc_name0(void)
{
	return __raw_readl(MT7620_SYSC_BASE + SYSC_REG_CHIP_NAME0);
}

static unsigned int __init mt7620_get_soc_name1(void)
{
	return __raw_readl(MT7620_SYSC_BASE + SYSC_REG_CHIP_NAME1);
}

static bool __init mt7620_soc_valid(void)
{
	if (mt7620_get_soc_name0() == MT7620_CHIP_NAME0 &&
	    mt7620_get_soc_name1() == MT7620_CHIP_NAME1)
		return true;
	else
		return false;
}

static bool __init mt7628_soc_valid(void)
{
	if (mt7620_get_soc_name0() == MT7620_CHIP_NAME0 &&
	    mt7620_get_soc_name1() == MT7628_CHIP_NAME1)
		return true;
	else
		return false;
}

static unsigned int __init mt7620_get_rev(void)
{
	return __raw_readl(MT7620_SYSC_BASE + SYSC_REG_CHIP_REV);
}

static unsigned int __init mt7620_get_bga(void)
{
	return (mt7620_get_rev() >> CHIP_REV_PKG_SHIFT) & CHIP_REV_PKG_MASK;
}

static unsigned int __init mt7620_get_efuse(void)
{
	return __raw_readl(MT7620_SYSC_BASE + SYSC_REG_EFUSE_CFG);
}

static unsigned int __init mt7620_get_soc_ver(void)
{
	return (mt7620_get_rev() >> CHIP_REV_VER_SHIFT) & CHIP_REV_VER_MASK;
}

static unsigned int __init mt7620_get_soc_eco(void)
{
	return (mt7620_get_rev() & CHIP_REV_ECO_MASK);
}

static const char __init *mt7620_get_soc_name(struct ralink_soc_info *soc_info)
{
	if (mt7620_soc_valid()) {
		u32 bga = mt7620_get_bga();

		if (bga) {
			ralink_soc = MT762X_SOC_MT7620A;
			soc_info->compatible = "ralink,mt7620a-soc";
			return "MT7620A";
		} else {
			ralink_soc = MT762X_SOC_MT7620N;
			soc_info->compatible = "ralink,mt7620n-soc";
			return "MT7620N";
		}
	} else if (mt7628_soc_valid()) {
		u32 efuse = mt7620_get_efuse();
		unsigned char *name = NULL;

		if (efuse & EFUSE_MT7688) {
			ralink_soc = MT762X_SOC_MT7688;
			name = "MT7688";
		} else {
			ralink_soc = MT762X_SOC_MT7628AN;
			name = "MT7628AN";
		}
		soc_info->compatible = "ralink,mt7628an-soc";
		return name;
	} else {
		panic("mt762x: unknown SoC, n0:%08x n1:%08x\n",
		      mt7620_get_soc_name0(), mt7620_get_soc_name1());
	}
}

static const char __init *mt7620_get_soc_id_name(void)
{
	if (ralink_soc == MT762X_SOC_MT7620A)
		return "mt7620a";
	else if (ralink_soc == MT762X_SOC_MT7620N)
		return "mt7620n";
	else if (ralink_soc == MT762X_SOC_MT7688)
		return "mt7688";
	else if (ralink_soc == MT762X_SOC_MT7628AN)
		return "mt7628n";
	else
		return "invalid";
}

static int __init mt7620_soc_dev_init(void)
{
	struct soc_device *soc_dev;
	struct soc_device_attribute *soc_dev_attr;

	soc_dev_attr = kzalloc(sizeof(*soc_dev_attr), GFP_KERNEL);
	if (!soc_dev_attr)
		return -ENOMEM;

	soc_dev_attr->family = "Ralink";
	soc_dev_attr->soc_id = mt7620_get_soc_id_name();

	soc_dev_attr->data = soc_info_ptr;

	soc_dev = soc_device_register(soc_dev_attr);
	if (IS_ERR(soc_dev)) {
		kfree(soc_dev_attr);
		return PTR_ERR(soc_dev);
	}

	return 0;
}
device_initcall(mt7620_soc_dev_init);

void __init prom_soc_init(struct ralink_soc_info *soc_info)
{
	const char *name = mt7620_get_soc_name(soc_info);
	u32 cfg0;
	u32 pmu0;
	u32 pmu1;

	snprintf(soc_info->sys_type, RAMIPS_SYS_TYPE_LEN,
		"MediaTek %s ver:%u eco:%u",
		name, mt7620_get_soc_ver(), mt7620_get_soc_eco());

	cfg0 = __raw_readl(MT7620_SYSC_BASE + SYSC_REG_SYSTEM_CONFIG0);
	if (is_mt76x8()) {
		dram_type = cfg0 & DRAM_TYPE_MT7628_MASK;
	} else {
		dram_type = (cfg0 >> SYSCFG0_DRAM_TYPE_SHIFT) &
			    SYSCFG0_DRAM_TYPE_MASK;
		if (dram_type == SYSCFG0_DRAM_TYPE_UNKNOWN)
			dram_type = SYSCFG0_DRAM_TYPE_SDRAM;
	}

	soc_info->mem_base = MT7620_DRAM_BASE;
	if (is_mt76x8())
		mt7628_dram_init(soc_info);
	else
		mt7620_dram_init(soc_info);

	pmu0 = __raw_readl(MT7620_SYSC_BASE + PMU0_CFG);
	pmu1 = __raw_readl(MT7620_SYSC_BASE + PMU1_CFG);

	pr_info("Analog PMU set to %s control\n",
		(pmu0 & PMU_SW_SET) ? ("sw") : ("hw"));
	pr_info("Digital PMU set to %s control\n",
		(pmu1 & DIG_SW_SEL) ? ("sw") : ("hw"));

	soc_info_ptr = soc_info;
}
