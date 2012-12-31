/* linux/arch/arm/mach-exynos/setup-gsc.c
 *
 * Copyright (c) 2011 Samsung Electronics Co., Ltd.
 *             http://www.samsung.com
 *
 * Base Exynos5 G-Scaler clock configuration
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/clk.h>
#include <linux/err.h>
#include <plat/clock.h>
#include <mach/regs-clock.h>
#include <mach/map.h>
#include <media/exynos_gscaler.h>

int __init exynos5_gsc_set_parent_clock(const char *child, const char *parent)
{
	struct clk *clk_parent;
	struct clk *clk_child;

	clk_child = clk_get(NULL, child);
	if (IS_ERR(clk_child)) {
		pr_err("failed to get %s clock.\n", child);
		return PTR_ERR(clk_child);
	}

	clk_parent = clk_get(NULL, parent);
	if (IS_ERR(clk_parent)) {
		clk_put(clk_child);
		pr_err("failed to get %s clock.\n", parent);
		return PTR_ERR(clk_parent);
	}

	if (clk_set_parent(clk_child, clk_parent)) {
		pr_err("Unable to set parent %s of clock %s.\n",
				clk_parent->name, clk_child->name);
		clk_put(clk_child);
		clk_put(clk_parent);
		return PTR_ERR(clk_child);
	}

	clk_put(clk_child);
	clk_put(clk_parent);

	return 0;
}

int __init exynos5_gsc_set_clock_rate(const char *clk, unsigned long clk_rate)
{
	struct clk *gsc_clk;

	gsc_clk = clk_get(NULL, clk);
	if (IS_ERR(gsc_clk)) {
		pr_err("failed to get %s clock.\n", clk);
		return PTR_ERR(gsc_clk);
	}

	if (!clk_rate)
		clk_rate = 310000000UL;

	if (clk_set_rate(gsc_clk, clk_rate)) {
		pr_err("%s rate change failed: %lu\n", gsc_clk->name, clk_rate);
		clk_put(gsc_clk);
		return PTR_ERR(gsc_clk);
	}

	clk_put(gsc_clk);

	return 0;
}
