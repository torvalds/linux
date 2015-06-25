/*
 * Copyright (C) 2015 Masahiro Yamada <yamada.masahiro@socionext.com>
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

#include <linux/sizes.h>
#include <linux/compiler.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/regmap.h>
#include <linux/mfd/syscon.h>
#include <asm/smp.h>
#include <asm/smp_scu.h>

static struct regmap *sbcm_regmap;

static void __init uniphier_smp_prepare_cpus(unsigned int max_cpus)
{
	static cpumask_t only_cpu_0 = { CPU_BITS_CPU0 };
	unsigned long scu_base_phys = 0;
	void __iomem *scu_base;

	sbcm_regmap = syscon_regmap_lookup_by_compatible(
			"socionext,uniphier-system-bus-controller-misc");
	if (IS_ERR(sbcm_regmap)) {
		pr_err("failed to regmap system-bus-controller-misc\n");
		goto err;
	}

	if (scu_a9_has_base())
		scu_base_phys = scu_a9_get_base();

	if (!scu_base_phys) {
		pr_err("failed to get scu base\n");
		goto err;
	}

	scu_base = ioremap(scu_base_phys, SZ_128);
	if (!scu_base) {
		pr_err("failed to remap scu base (0x%08lx)\n", scu_base_phys);
		goto err;
	}

	scu_enable(scu_base);
	iounmap(scu_base);

	return;
err:
	pr_warn("disabling SMP\n");
	init_cpu_present(&only_cpu_0);
	sbcm_regmap = NULL;
}

static void __naked uniphier_secondary_startup(void)
{
	asm("bl		v7_invalidate_l1\n"
	    "b		secondary_startup\n");
};

static int uniphier_boot_secondary(unsigned int cpu,
				   struct task_struct *idle)
{
	int ret;

	if (!sbcm_regmap)
		return -ENODEV;

	ret = regmap_write(sbcm_regmap, 0x1208,
			   virt_to_phys(uniphier_secondary_startup));
	if (!ret)
		asm("sev"); /* wake up secondary CPU */

	return ret;
}

struct smp_operations uniphier_smp_ops __initdata = {
	.smp_prepare_cpus	= uniphier_smp_prepare_cpus,
	.smp_boot_secondary	= uniphier_boot_secondary,
};
CPU_METHOD_OF_DECLARE(uniphier_smp, "socionext,uniphier-smp",
		      &uniphier_smp_ops);
