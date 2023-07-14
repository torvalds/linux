// SPDX-License-Identifier: GPL-2.0-only
/*
 *
 * Copyright (C) 2008 Imre Kaloz <kaloz@openwrt.org>
 * Copyright (C) 2008-2009 Gabor Juhos <juhosg@openwrt.org>
 * Copyright (C) 2013 John Crispin <john@phrozen.org>
 */

#include <linux/io.h>
#include <linux/clk.h>
#include <linux/export.h>
#include <linux/init.h>
#include <linux/sizes.h>
#include <linux/of_fdt.h>
#include <linux/kernel.h>
#include <linux/memblock.h>
#include <linux/of_platform.h>
#include <linux/of_address.h>

#include <asm/reboot.h>
#include <asm/bootinfo.h>
#include <asm/addrspace.h>
#include <asm/prom.h>
#include <asm/mach-ralink/ralink_regs.h>

#include "common.h"

__iomem void *rt_sysc_membase;
__iomem void *rt_memc_membase;
EXPORT_SYMBOL_GPL(rt_sysc_membase);

static const struct of_device_id mtmips_memc_match[] = {
	{ .compatible = "mediatek,mt7621-memc" },
	{ .compatible = "ralink,mt7620a-memc" },
	{ .compatible = "ralink,rt2880-memc" },
	{ .compatible = "ralink,rt3050-memc" },
	{ .compatible = "ralink,rt3883-memc" },
	{}
};

static const struct of_device_id mtmips_sysc_match[] = {
	{ .compatible = "mediatek,mt7621-sysc" },
	{ .compatible = "ralink,mt7620-sysc" },
	{ .compatible = "ralink,mt7628-sysc" },
	{ .compatible = "ralink,mt7688-sysc" },
	{ .compatible = "ralink,rt2880-sysc" },
	{ .compatible = "ralink,rt3050-sysc" },
	{ .compatible = "ralink,rt3052-sysc" },
	{ .compatible = "ralink,rt3352-sysc" },
	{ .compatible = "ralink,rt3883-sysc" },
	{ .compatible = "ralink,rt5350-sysc" },
	{}
};

static __iomem void *
mtmips_of_remap_node(const struct of_device_id *match, const char *type)
{
	struct resource res;
	struct device_node *np;

	np = of_find_matching_node(NULL, match);
	if (!np)
		panic("Failed to find %s controller node", type);

	if (of_address_to_resource(np, 0, &res))
		panic("Failed to get resource for %s node", np->name);

	if (!request_mem_region(res.start,
				resource_size(&res),
				res.name))
		panic("Failed to request resources for %s node", np->name);

	of_node_put(np);

	return ioremap(res.start, resource_size(&res));
}

void __init ralink_of_remap(void)
{
	rt_sysc_membase = mtmips_of_remap_node(mtmips_sysc_match, "system");
	rt_memc_membase = mtmips_of_remap_node(mtmips_memc_match, "memory");

	if (!rt_sysc_membase || !rt_memc_membase)
		panic("Failed to remap core resources");
}

void __init plat_mem_setup(void)
{
	void *dtb;

	set_io_port_base(KSEG1);

	/*
	 * Load the builtin devicetree. This causes the chosen node to be
	 * parsed resulting in our memory appearing.
	 */
	dtb = get_fdt();
	__dt_setup_arch(dtb);

	if (early_init_dt_scan_memory())
		return;

	if (soc_info.mem_detect)
		soc_info.mem_detect();
	else if (soc_info.mem_size)
		memblock_add(soc_info.mem_base, soc_info.mem_size * SZ_1M);
	else
		detect_memory_region(soc_info.mem_base,
				     soc_info.mem_size_min * SZ_1M,
				     soc_info.mem_size_max * SZ_1M);
}

static int __init plat_of_setup(void)
{
	__dt_register_buses(soc_info.compatible, "palmbus");

	return 0;
}

arch_initcall(plat_of_setup);
