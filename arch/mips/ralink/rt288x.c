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
#include <linux/slab.h>
#include <linux/sys_soc.h>

#include <asm/mipsregs.h>
#include <asm/mach-ralink/ralink_regs.h>
#include <asm/mach-ralink/rt288x.h>

#include "common.h"

static struct ralink_soc_info *soc_info_ptr;

static unsigned int __init rt2880_get_soc_name0(void)
{
	return __raw_readl(RT2880_SYSC_BASE + SYSC_REG_CHIP_NAME0);
}

static unsigned int __init rt2880_get_soc_name1(void)
{
	return __raw_readl(RT2880_SYSC_BASE + SYSC_REG_CHIP_NAME1);
}

static bool __init rt2880_soc_valid(void)
{
	if (rt2880_get_soc_name0() == RT2880_CHIP_NAME0 &&
	    rt2880_get_soc_name1() == RT2880_CHIP_NAME1)
		return true;
	else
		return false;
}

static const char __init *rt2880_get_soc_name(void)
{
	if (rt2880_soc_valid())
		return "RT2880";
	else
		return "invalid";
}

static unsigned int __init rt2880_get_soc_id(void)
{
	return __raw_readl(RT2880_SYSC_BASE + SYSC_REG_CHIP_ID);
}

static unsigned int __init rt2880_get_soc_ver(void)
{
	return (rt2880_get_soc_id() >> CHIP_ID_ID_SHIFT) & CHIP_ID_ID_MASK;
}

static unsigned int __init rt2880_get_soc_rev(void)
{
	return (rt2880_get_soc_id() & CHIP_ID_REV_MASK);
}

static int __init rt2880_soc_dev_init(void)
{
	struct soc_device *soc_dev;
	struct soc_device_attribute *soc_dev_attr;

	soc_dev_attr = kzalloc(sizeof(*soc_dev_attr), GFP_KERNEL);
	if (!soc_dev_attr)
		return -ENOMEM;

	soc_dev_attr->family = "Ralink";
	soc_dev_attr->soc_id = rt2880_get_soc_name();

	soc_dev_attr->data = soc_info_ptr;

	soc_dev = soc_device_register(soc_dev_attr);
	if (IS_ERR(soc_dev)) {
		kfree(soc_dev_attr);
		return PTR_ERR(soc_dev);
	}

	return 0;
}
device_initcall(rt2880_soc_dev_init);

void __init prom_soc_init(struct ralink_soc_info *soc_info)
{
	if (rt2880_soc_valid())
		soc_info->compatible = "ralink,r2880-soc";
	else
		panic("rt288x: unknown SoC, n0:%08x n1:%08x",
		      rt2880_get_soc_name0(), rt2880_get_soc_name1());

	snprintf(soc_info->sys_type, RAMIPS_SYS_TYPE_LEN,
		"Ralink %s id:%u rev:%u",
		rt2880_get_soc_name(),
		rt2880_get_soc_ver(),
		rt2880_get_soc_rev());

	soc_info->mem_base = RT2880_SDRAM_BASE;
	soc_info->mem_size_min = RT2880_MEM_SIZE_MIN;
	soc_info->mem_size_max = RT2880_MEM_SIZE_MAX;

	ralink_soc = RT2880_SOC;
	soc_info_ptr = soc_info;
}
