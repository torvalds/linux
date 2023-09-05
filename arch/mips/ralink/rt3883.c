// SPDX-License-Identifier: GPL-2.0-only
/*
 *
 * Parts of this file are based on Ralink's 2.6.21 BSP
 *
 * Copyright (C) 2008 Imre Kaloz <kaloz@openwrt.org>
 * Copyright (C) 2008-2011 Gabor Juhos <juhosg@openwrt.org>
 * Copyright (C) 2013 John Crispin <john@phrozen.org>
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/sys_soc.h>

#include <asm/mipsregs.h>
#include <asm/mach-ralink/ralink_regs.h>
#include <asm/mach-ralink/rt3883.h>

#include "common.h"

static struct ralink_soc_info *soc_info_ptr;

static unsigned int __init rt3883_get_soc_name0(void)
{
	return __raw_readl(RT3883_SYSC_BASE + RT3883_SYSC_REG_CHIPID0_3);
}

static unsigned int __init rt3883_get_soc_name1(void)
{
	return __raw_readl(RT3883_SYSC_BASE + RT3883_SYSC_REG_CHIPID4_7);
}

static bool __init rt3883_soc_valid(void)
{
	if (rt3883_get_soc_name0() == RT3883_CHIP_NAME0 &&
	    rt3883_get_soc_name1() == RT3883_CHIP_NAME1)
		return true;
	else
		return false;
}

static const char __init *rt3883_get_soc_name(void)
{
	if (rt3883_soc_valid())
		return "RT3883";
	else
		return "invalid";
}

static unsigned int __init rt3883_get_soc_id(void)
{
	return __raw_readl(RT3883_SYSC_BASE + RT3883_SYSC_REG_REVID);
}

static unsigned int __init rt3883_get_soc_ver(void)
{
	return (rt3883_get_soc_id() >> RT3883_REVID_VER_ID_SHIFT) & RT3883_REVID_VER_ID_MASK;
}

static unsigned int __init rt3883_get_soc_rev(void)
{
	return (rt3883_get_soc_id() & RT3883_REVID_ECO_ID_MASK);
}

static int __init rt3883_soc_dev_init(void)
{
	struct soc_device *soc_dev;
	struct soc_device_attribute *soc_dev_attr;

	soc_dev_attr = kzalloc(sizeof(*soc_dev_attr), GFP_KERNEL);
	if (!soc_dev_attr)
		return -ENOMEM;

	soc_dev_attr->family = "Ralink";
	soc_dev_attr->soc_id = rt3883_get_soc_name();

	soc_dev_attr->data = soc_info_ptr;

	soc_dev = soc_device_register(soc_dev_attr);
	if (IS_ERR(soc_dev)) {
		kfree(soc_dev_attr);
		return PTR_ERR(soc_dev);
	}

	return 0;
}
device_initcall(rt3883_soc_dev_init);

void __init prom_soc_init(struct ralink_soc_info *soc_info)
{
	if (rt3883_soc_valid())
		soc_info->compatible = "ralink,rt3883-soc";
	else
		panic("rt3883: unknown SoC, n0:%08x n1:%08x",
		      rt3883_get_soc_name0(), rt3883_get_soc_name1());

	snprintf(soc_info->sys_type, RAMIPS_SYS_TYPE_LEN,
		"Ralink %s ver:%u eco:%u",
		rt3883_get_soc_name(),
		rt3883_get_soc_ver(),
		rt3883_get_soc_rev());

	soc_info->mem_base = RT3883_SDRAM_BASE;
	soc_info->mem_size_min = RT3883_MEM_SIZE_MIN;
	soc_info->mem_size_max = RT3883_MEM_SIZE_MAX;

	ralink_soc = RT3883_SOC;
	soc_info_ptr = soc_info;
}
