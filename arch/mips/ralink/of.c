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

__iomem void *plat_of_remap_node(const char *node)
{
	struct resource res;
	struct device_node *np;

	np = of_find_compatible_node(NULL, NULL, node);
	if (!np)
		panic("Failed to find %s node", node);

	if (of_address_to_resource(np, 0, &res))
		panic("Failed to get resource for %s", node);

	of_node_put(np);

	if (!request_mem_region(res.start,
				resource_size(&res),
				res.name))
		panic("Failed to request resources for %s", node);

	return ioremap(res.start, resource_size(&res));
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

	/* make sure that the reset controller is setup early */
	if (ralink_soc != MT762X_SOC_MT7621AT)
		ralink_rst_init();

	return 0;
}

arch_initcall(plat_of_setup);
