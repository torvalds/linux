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

#include <asm/io.h>
#include <asm/mipsregs.h>
#include <asm/mach-ralink/ralink_regs.h>
#include <asm/mach-ralink/rt305x.h>

#include "common.h"

static struct ralink_soc_info *soc_info_ptr;

static unsigned long rt5350_get_mem_size(void)
{
	unsigned long ret;
	u32 t;

	t = __raw_readl(RT305X_SYSC_BASE + SYSC_REG_SYSTEM_CONFIG);
	t = (t >> RT5350_SYSCFG0_DRAM_SIZE_SHIFT) &
		RT5350_SYSCFG0_DRAM_SIZE_MASK;

	switch (t) {
	case RT5350_SYSCFG0_DRAM_SIZE_2M:
		ret = 2;
		break;
	case RT5350_SYSCFG0_DRAM_SIZE_8M:
		ret = 8;
		break;
	case RT5350_SYSCFG0_DRAM_SIZE_16M:
		ret = 16;
		break;
	case RT5350_SYSCFG0_DRAM_SIZE_32M:
		ret = 32;
		break;
	case RT5350_SYSCFG0_DRAM_SIZE_64M:
		ret = 64;
		break;
	default:
		panic("rt5350: invalid DRAM size: %u", t);
		break;
	}

	return ret;
}

void __init ralink_clk_init(void)
{
	unsigned long cpu_rate, sys_rate, wdt_rate, uart_rate;
	unsigned long wmac_rate = 40000000;

	u32 t = rt_sysc_r32(SYSC_REG_SYSTEM_CONFIG);

	if (soc_is_rt305x() || soc_is_rt3350()) {
		t = (t >> RT305X_SYSCFG_CPUCLK_SHIFT) &
		     RT305X_SYSCFG_CPUCLK_MASK;
		switch (t) {
		case RT305X_SYSCFG_CPUCLK_LOW:
			cpu_rate = 320000000;
			break;
		case RT305X_SYSCFG_CPUCLK_HIGH:
			cpu_rate = 384000000;
			break;
		}
		sys_rate = uart_rate = wdt_rate = cpu_rate / 3;
	} else if (soc_is_rt3352()) {
		t = (t >> RT3352_SYSCFG0_CPUCLK_SHIFT) &
		     RT3352_SYSCFG0_CPUCLK_MASK;
		switch (t) {
		case RT3352_SYSCFG0_CPUCLK_LOW:
			cpu_rate = 384000000;
			break;
		case RT3352_SYSCFG0_CPUCLK_HIGH:
			cpu_rate = 400000000;
			break;
		}
		sys_rate = wdt_rate = cpu_rate / 3;
		uart_rate = 40000000;
	} else if (soc_is_rt5350()) {
		t = (t >> RT5350_SYSCFG0_CPUCLK_SHIFT) &
		     RT5350_SYSCFG0_CPUCLK_MASK;
		switch (t) {
		case RT5350_SYSCFG0_CPUCLK_360:
			cpu_rate = 360000000;
			sys_rate = cpu_rate / 3;
			break;
		case RT5350_SYSCFG0_CPUCLK_320:
			cpu_rate = 320000000;
			sys_rate = cpu_rate / 4;
			break;
		case RT5350_SYSCFG0_CPUCLK_300:
			cpu_rate = 300000000;
			sys_rate = cpu_rate / 3;
			break;
		default:
			BUG();
		}
		uart_rate = 40000000;
		wdt_rate = sys_rate;
	} else {
		BUG();
	}

	if (soc_is_rt3352() || soc_is_rt5350()) {
		u32 val = rt_sysc_r32(RT3352_SYSC_REG_SYSCFG0);

		if (!(val & RT3352_CLKCFG0_XTAL_SEL))
			wmac_rate = 20000000;
	}

	ralink_clk_add("cpu", cpu_rate);
	ralink_clk_add("sys", sys_rate);
	ralink_clk_add("10000900.i2c", uart_rate);
	ralink_clk_add("10000a00.i2s", uart_rate);
	ralink_clk_add("10000b00.spi", sys_rate);
	ralink_clk_add("10000b40.spi", sys_rate);
	ralink_clk_add("10000100.timer", wdt_rate);
	ralink_clk_add("10000120.watchdog", wdt_rate);
	ralink_clk_add("10000500.uart", uart_rate);
	ralink_clk_add("10000c00.uartlite", uart_rate);
	ralink_clk_add("10100000.ethernet", sys_rate);
	ralink_clk_add("10180000.wmac", wmac_rate);
}

void __init ralink_of_remap(void)
{
	rt_sysc_membase = plat_of_remap_node("ralink,rt3050-sysc");
	rt_memc_membase = plat_of_remap_node("ralink,rt3050-memc");

	if (!rt_sysc_membase || !rt_memc_membase)
		panic("Failed to remap core resources");
}

static unsigned int __init rt305x_get_soc_name0(void)
{
	return __raw_readl(RT305X_SYSC_BASE + SYSC_REG_CHIP_NAME0);
}

static unsigned int __init rt305x_get_soc_name1(void)
{
	return __raw_readl(RT305X_SYSC_BASE + SYSC_REG_CHIP_NAME1);
}

static bool __init rt3052_soc_valid(void)
{
	if (rt305x_get_soc_name0() == RT3052_CHIP_NAME0 &&
	    rt305x_get_soc_name1() == RT3052_CHIP_NAME1)
		return true;
	else
		return false;
}

static bool __init rt3350_soc_valid(void)
{
	if (rt305x_get_soc_name0() == RT3350_CHIP_NAME0 &&
	    rt305x_get_soc_name1() == RT3350_CHIP_NAME1)
		return true;
	else
		return false;
}

static bool __init rt3352_soc_valid(void)
{
	if (rt305x_get_soc_name0() == RT3352_CHIP_NAME0 &&
	    rt305x_get_soc_name1() == RT3352_CHIP_NAME1)
		return true;
	else
		return false;
}

static bool __init rt5350_soc_valid(void)
{
	if (rt305x_get_soc_name0() == RT5350_CHIP_NAME0 &&
	    rt305x_get_soc_name1() == RT5350_CHIP_NAME1)
		return true;
	else
		return false;
}

static const char __init *rt305x_get_soc_name(struct ralink_soc_info *soc_info)
{
	if (rt3052_soc_valid()) {
		unsigned long icache_sets;

		icache_sets = (read_c0_config1() >> 22) & 7;
		if (icache_sets == 1) {
			ralink_soc = RT305X_SOC_RT3050;
			soc_info->compatible = "ralink,rt3050-soc";
			return "RT3050";
		} else {
			ralink_soc = RT305X_SOC_RT3052;
			soc_info->compatible = "ralink,rt3052-soc";
			return "RT3052";
		}
	} else if (rt3350_soc_valid()) {
		ralink_soc = RT305X_SOC_RT3350;
		soc_info->compatible = "ralink,rt3350-soc";
		return "RT3350";
	} else if (rt3352_soc_valid()) {
		ralink_soc = RT305X_SOC_RT3352;
		soc_info->compatible = "ralink,rt3352-soc";
		return "RT3352";
	} else if (rt5350_soc_valid()) {
		ralink_soc = RT305X_SOC_RT5350;
		soc_info->compatible = "ralink,rt5350-soc";
		return "RT5350";
	} else {
		panic("rt305x: unknown SoC, n0:%08x n1:%08x",
		      rt305x_get_soc_name0(), rt305x_get_soc_name1());
	}
}

static unsigned int __init rt305x_get_soc_id(void)
{
	return __raw_readl(RT305X_SYSC_BASE + SYSC_REG_CHIP_ID);
}

static unsigned int __init rt305x_get_soc_ver(void)
{
	return (rt305x_get_soc_id() >> CHIP_ID_ID_SHIFT) & CHIP_ID_ID_MASK;
}

static unsigned int __init rt305x_get_soc_rev(void)
{
	return (rt305x_get_soc_id() & CHIP_ID_REV_MASK);
}

static const char __init *rt305x_get_soc_id_name(void)
{
	if (soc_is_rt3050())
		return "rt3050";
	else if (soc_is_rt3052())
		return "rt3052";
	else if (soc_is_rt3350())
		return "rt3350";
	else if (soc_is_rt3352())
		return "rt3352";
	else if (soc_is_rt5350())
		return "rt5350";
	else
		return "invalid";
}

static int __init rt305x_soc_dev_init(void)
{
	struct soc_device *soc_dev;
	struct soc_device_attribute *soc_dev_attr;

	soc_dev_attr = kzalloc(sizeof(*soc_dev_attr), GFP_KERNEL);
	if (!soc_dev_attr)
		return -ENOMEM;

	soc_dev_attr->family = "Ralink";
	soc_dev_attr->soc_id = rt305x_get_soc_id_name();

	soc_dev_attr->data = soc_info_ptr;

	soc_dev = soc_device_register(soc_dev_attr);
	if (IS_ERR(soc_dev)) {
		kfree(soc_dev_attr);
		return PTR_ERR(soc_dev);
	}

	return 0;
}
device_initcall(rt305x_soc_dev_init);

void __init prom_soc_init(struct ralink_soc_info *soc_info)
{
	const char *name = rt305x_get_soc_name(soc_info);

	snprintf(soc_info->sys_type, RAMIPS_SYS_TYPE_LEN,
		"Ralink %s id:%u rev:%u",
		name,
		rt305x_get_soc_ver(),
		rt305x_get_soc_rev());

	soc_info->mem_base = RT305X_SDRAM_BASE;
	if (soc_is_rt5350()) {
		soc_info->mem_size = rt5350_get_mem_size();
	} else if (soc_is_rt305x() || soc_is_rt3350()) {
		soc_info->mem_size_min = RT305X_MEM_SIZE_MIN;
		soc_info->mem_size_max = RT305X_MEM_SIZE_MAX;
	} else if (soc_is_rt3352()) {
		soc_info->mem_size_min = RT3352_MEM_SIZE_MIN;
		soc_info->mem_size_max = RT3352_MEM_SIZE_MAX;
	}

	soc_info_ptr = soc_info;
}
