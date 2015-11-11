/*
 * Copyright (C) 2014 Marvell
 *
 * Thomas Petazzoni <thomas.petazzoni@free-electrons.com>
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2.  This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#define pr_fmt(fmt) "mvebu-cpureset: " fmt

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/of_address.h>
#include <linux/io.h>
#include <linux/resource.h>

static void __iomem *cpu_reset_base;
static size_t cpu_reset_size;

#define CPU_RESET_OFFSET(cpu) (cpu * 0x8)
#define CPU_RESET_ASSERT      BIT(0)

int mvebu_cpu_reset_deassert(int cpu)
{
	u32 reg;

	if (!cpu_reset_base)
		return -ENODEV;

	if (CPU_RESET_OFFSET(cpu) >= cpu_reset_size)
		return -EINVAL;

	reg = readl(cpu_reset_base + CPU_RESET_OFFSET(cpu));
	reg &= ~CPU_RESET_ASSERT;
	writel(reg, cpu_reset_base + CPU_RESET_OFFSET(cpu));

	return 0;
}

static int mvebu_cpu_reset_map(struct device_node *np, int res_idx)
{
	struct resource res;

	if (of_address_to_resource(np, res_idx, &res)) {
		pr_err("unable to get resource\n");
		return -ENOENT;
	}

	if (!request_mem_region(res.start, resource_size(&res),
				np->full_name)) {
		pr_err("unable to request region\n");
		return -EBUSY;
	}

	cpu_reset_base = ioremap(res.start, resource_size(&res));
	if (!cpu_reset_base) {
		pr_err("unable to map registers\n");
		release_mem_region(res.start, resource_size(&res));
		return -ENOMEM;
	}

	cpu_reset_size = resource_size(&res);

	return 0;
}

static int __init mvebu_cpu_reset_init(void)
{
	struct device_node *np;
	int res_idx;
	int ret;

	np = of_find_compatible_node(NULL, NULL,
				     "marvell,armada-370-cpu-reset");
	if (np) {
		res_idx = 0;
	} else {
		/*
		 * This code is kept for backward compatibility with
		 * old Device Trees.
		 */
		np = of_find_compatible_node(NULL, NULL,
					     "marvell,armada-370-xp-pmsu");
		if (np) {
			pr_warn(FW_WARN "deprecated pmsu binding\n");
			res_idx = 1;
		}
	}

	/* No reset node found */
	if (!np)
		return -ENODEV;

	ret = mvebu_cpu_reset_map(np, res_idx);
	of_node_put(np);

	return ret;
}

early_initcall(mvebu_cpu_reset_init);
