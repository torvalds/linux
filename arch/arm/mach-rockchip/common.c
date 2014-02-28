/*
 * Copyright (C) 2013-2014 ROCKCHIP, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/clk-provider.h>
#include <linux/genalloc.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/of_address.h>
#include <linux/of_platform.h>
#include <asm/hardware/cache-l2x0.h>
#include "common.h"
#include "cpu_axi.h"
#include "loader.h"
#include "pmu.h"
#include "sram.h"

static int __init rockchip_cpu_axi_init(void)
{
	struct device_node *np, *cp;
	void __iomem *base, *cbase;

	np = of_find_compatible_node(NULL, NULL, "rockchip,cpu_axi_bus");
	if (!np)
		return -ENODEV;

	base = of_iomap(np, 0);

	np = of_get_child_by_name(np, "qos");
	if (np) {
		for_each_child_of_node(np, cp) {
			u32 offset, priority[2], mode, bandwidth, saturation;
			if (of_property_read_u32(cp, "rockchip,offset", &offset))
				continue;
			pr_debug("qos: %s offset %x\n", cp->name, offset);
			cbase = base + offset;
			if (!of_property_read_u32_array(cp, "rockchip,priority", priority, ARRAY_SIZE(priority))) {
				CPU_AXI_SET_QOS_PRIORITY(priority[0], priority[1], cbase);
				pr_debug("qos: %s priority %x %x\n", cp->name, priority[0], priority[1]);
			}
			if (!of_property_read_u32(cp, "rockchip,mode", &mode)) {
				CPU_AXI_SET_QOS_MODE(mode, cbase);
				pr_debug("qos: %s mode %x\n", cp->name, mode);
			}
			if (!of_property_read_u32(cp, "rockchip,bandwidth", &bandwidth)) {
				CPU_AXI_SET_QOS_BANDWIDTH(bandwidth, cbase);
				pr_debug("qos: %s bandwidth %x\n", cp->name, bandwidth);
			}
			if (!of_property_read_u32(cp, "rockchip,saturation", &saturation)) {
				CPU_AXI_SET_QOS_SATURATION(saturation, cbase);
				pr_debug("qos: %s saturation %x\n", cp->name, saturation);
			}
		}
	};

	writel_relaxed(0x3f, base + 0x0014);	// memory scheduler read latency
	dsb();

	iounmap(base);

	return 0;
}
early_initcall(rockchip_cpu_axi_init);

static int __init rockchip_pl330_l2_cache_init(void)
{
	struct device_node *np;
	void __iomem *base;
	u32 aux[2] = { 0, ~0 }, prefetch, power;

	np = of_find_compatible_node(NULL, NULL, "rockchip,pl310-cache");
	if (!np)
		return -ENODEV;

	base = of_iomap(np, 0);
	if (!base)
		return -EINVAL;

	if (!of_property_read_u32(np, "rockchip,prefetch-ctrl", &prefetch)) {
		/* L2X0 Prefetch Control */
		writel_relaxed(prefetch, base + L2X0_PREFETCH_CTRL);
		pr_debug("l2c: prefetch %x\n", prefetch);
	}

	if (!of_property_read_u32(np, "rockchip,power-ctrl", &power)) {
		/* L2X0 Power Control */
		writel_relaxed(power, base + L2X0_POWER_CTRL);
		pr_debug("l2c: power %x\n", power);
	}

	iounmap(base);

	of_property_read_u32_array(np, "rockchip,aux-ctrl", aux, ARRAY_SIZE(aux));
	pr_debug("l2c: aux %08x mask %08x\n", aux[0], aux[1]);

	l2x0_of_init(aux[0], aux[1]);

	return 0;
}
early_initcall(rockchip_pl330_l2_cache_init);

struct gen_pool *rockchip_sram_pool = NULL;
struct pie_chunk *rockchip_pie_chunk = NULL;
void *rockchip_sram_virt = NULL;
size_t rockchip_sram_size = 0;
char *rockchip_sram_stack = NULL;

int __init rockchip_pie_init(void)
{
	struct device_node *np;

	np = of_find_node_by_path("/");
	if (!np)
		return -ENODEV;

	rockchip_sram_pool = of_get_named_gen_pool(np, "rockchip,sram", 0);
	if (!rockchip_sram_pool) {
		pr_err("%s: failed to get sram pool\n", __func__);
		return -ENODEV;
	}
	rockchip_sram_size = gen_pool_size(rockchip_sram_pool);

	return 0;
}

static bool is_panic = false;

static int panic_event(struct notifier_block *this, unsigned long event, void *ptr)
{
	is_panic = true;
	return NOTIFY_DONE;
}

static struct notifier_block panic_block = {
	.notifier_call	= panic_event,
};

static int boot_mode;

int rockchip_boot_mode(void)
{
	return boot_mode;
}
EXPORT_SYMBOL(rockchip_boot_mode);

static inline const char *boot_flag_name(u32 flag)
{
	flag -= SYS_KERNRL_REBOOT_FLAG;
	switch (flag) {
	case BOOT_NORMAL: return "NORMAL";
	case BOOT_LOADER: return "LOADER";
	case BOOT_MASKROM: return "MASKROM";
	case BOOT_RECOVER: return "RECOVER";
	case BOOT_NORECOVER: return "NORECOVER";
	case BOOT_SECONDOS: return "SECONDOS";
	case BOOT_WIPEDATA: return "WIPEDATA";
	case BOOT_WIPEALL: return "WIPEALL";
	case BOOT_CHECKIMG: return "CHECKIMG";
	case BOOT_FASTBOOT: return "FASTBOOT";
	default: return "";
	}
}

static inline const char *boot_mode_name(u32 mode)
{
	switch (mode) {
	case BOOT_MODE_NORMAL: return "NORMAL";
	case BOOT_MODE_FACTORY2: return "FACTORY2";
	case BOOT_MODE_RECOVERY: return "RECOVERY";
	case BOOT_MODE_CHARGE: return "CHARGE";
	case BOOT_MODE_POWER_TEST: return "POWER_TEST";
	case BOOT_MODE_OFFMODE_CHARGING: return "OFFMODE_CHARGING";
	case BOOT_MODE_REBOOT: return "REBOOT";
	case BOOT_MODE_PANIC: return "PANIC";
	case BOOT_MODE_WATCHDOG: return "WATCHDOG";
	default: return "";
	}
}

void __init rockchip_boot_mode_init(u32 flag, u32 mode)
{
	boot_mode = mode;
	if (mode || ((flag & 0xff) && ((flag & 0xffffff00) == SYS_KERNRL_REBOOT_FLAG)))
		printk("Boot mode: %s (%d) flag: %s (0x%08x)\n", boot_mode_name(mode), mode, boot_flag_name(flag), flag);
	atomic_notifier_chain_register(&panic_notifier_list, &panic_block);
}

void rockchip_restart_get_boot_mode(const char *cmd, u32 *flag, u32 *mode)
{
	*flag = 0;
	*mode = BOOT_MODE_REBOOT;

	if (cmd) {
		if (!strcmp(cmd, "loader") || !strcmp(cmd, "bootloader"))
			*flag = SYS_LOADER_REBOOT_FLAG + BOOT_LOADER;
		else if(!strcmp(cmd, "recovery"))
			*flag = SYS_LOADER_REBOOT_FLAG + BOOT_RECOVER;
		else if (!strcmp(cmd, "charge"))
			*mode = BOOT_MODE_CHARGE;
	} else {
		if (is_panic)
			*mode = BOOT_MODE_PANIC;
	}
}

struct rockchip_pmu_operations rockchip_pmu_ops;
int (*ddr_change_freq)(uint32_t nMHz) = NULL;
long (*ddr_round_rate)(uint32_t nMHz) = NULL;
void (*ddr_set_auto_self_refresh)(bool en) = NULL;

