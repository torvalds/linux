/*
 * Copyright (C) 2015 Linus Walleij
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/smp.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/regmap.h>
#include <linux/mfd/syscon.h>

#include <asm/cacheflush.h>
#include <asm/smp_plat.h>
#include <asm/smp_scu.h>

#include <plat/platsmp.h>

#include "core.h"

#define REALVIEW_SYS_FLAGSSET_OFFSET	0x30

static const struct of_device_id realview_scu_match[] = {
	{ .compatible = "arm,arm11mp-scu", },
	{ .compatible = "arm,cortex-a9-scu", },
	{ .compatible = "arm,cortex-a5-scu", },
	{ }
};

static const struct of_device_id realview_syscon_match[] = {
        { .compatible = "arm,core-module-integrator", },
        { .compatible = "arm,realview-eb-syscon", },
        { .compatible = "arm,realview-pb11mp-syscon", },
        { .compatible = "arm,realview-pbx-syscon", },
        { },
};

static void __init realview_smp_prepare_cpus(unsigned int max_cpus)
{
	struct device_node *np;
	void __iomem *scu_base;
	struct regmap *map;
	unsigned int ncores;
	int i;

	np = of_find_matching_node(NULL, realview_scu_match);
	if (!np) {
		pr_err("PLATSMP: No SCU base address\n");
		return;
	}
	scu_base = of_iomap(np, 0);
	of_node_put(np);
	if (!scu_base) {
		pr_err("PLATSMP: No SCU remap\n");
		return;
	}

	scu_enable(scu_base);
	ncores = scu_get_core_count(scu_base);
	pr_info("SCU: %d cores detected\n", ncores);
	for (i = 0; i < ncores; i++)
		set_cpu_possible(i, true);
	iounmap(scu_base);

	/* The syscon contains the magic SMP start address registers */
	np = of_find_matching_node(NULL, realview_syscon_match);
	if (!np) {
		pr_err("PLATSMP: No syscon match\n");
		return;
	}
	map = syscon_node_to_regmap(np);
	if (IS_ERR(map)) {
		pr_err("PLATSMP: No syscon regmap\n");
		return;
	}
	/* Put the boot address in this magic register */
	regmap_write(map, REALVIEW_SYS_FLAGSSET_OFFSET,
		     virt_to_phys(versatile_secondary_startup));
}

static const struct smp_operations realview_dt_smp_ops __initconst = {
	.smp_prepare_cpus	= realview_smp_prepare_cpus,
	.smp_secondary_init	= versatile_secondary_init,
	.smp_boot_secondary	= versatile_boot_secondary,
#ifdef CONFIG_HOTPLUG_CPU
	.cpu_die		= realview_cpu_die,
#endif
};
CPU_METHOD_OF_DECLARE(realview_smp, "arm,realview-smp", &realview_dt_smp_ops);
