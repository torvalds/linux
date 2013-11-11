/*
 * Copyright (c) 2012, NVIDIA CORPORATION.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/of.h>
#include <linux/clk/tegra.h>

#include "clk.h"

/* Global data of Tegra CPU CAR ops */
static struct tegra_cpu_car_ops dummy_car_ops;
struct tegra_cpu_car_ops *tegra_cpu_car_ops = &dummy_car_ops;

void __init tegra_init_dup_clks(struct tegra_clk_duplicate *dup_list,
				struct clk *clks[], int clk_max)
{
	struct clk *clk;

	for (; dup_list->clk_id < clk_max; dup_list++) {
		clk = clks[dup_list->clk_id];
		dup_list->lookup.clk = clk;
		clkdev_add(&dup_list->lookup);
	}
}

void __init tegra_init_from_table(struct tegra_clk_init_table *tbl,
				  struct clk *clks[], int clk_max)
{
	struct clk *clk;

	for (; tbl->clk_id < clk_max; tbl++) {
		clk = clks[tbl->clk_id];
		if (IS_ERR_OR_NULL(clk))
			return;

		if (tbl->parent_id < clk_max) {
			struct clk *parent = clks[tbl->parent_id];
			if (clk_set_parent(clk, parent)) {
				pr_err("%s: Failed to set parent %s of %s\n",
				       __func__, __clk_get_name(parent),
				       __clk_get_name(clk));
				WARN_ON(1);
			}
		}

		if (tbl->rate)
			if (clk_set_rate(clk, tbl->rate)) {
				pr_err("%s: Failed to set rate %lu of %s\n",
				       __func__, tbl->rate,
				       __clk_get_name(clk));
				WARN_ON(1);
			}

		if (tbl->state)
			if (clk_prepare_enable(clk)) {
				pr_err("%s: Failed to enable %s\n", __func__,
				       __clk_get_name(clk));
				WARN_ON(1);
			}
	}
}

static const struct of_device_id tegra_dt_clk_match[] = {
	{ .compatible = "nvidia,tegra20-car", .data = tegra20_clock_init },
	{ .compatible = "nvidia,tegra30-car", .data = tegra30_clock_init },
	{ .compatible = "nvidia,tegra114-car", .data = tegra114_clock_init },
	{ }
};

void __init tegra_clocks_init(void)
{
	of_clk_init(tegra_dt_clk_match);
}

tegra_clk_apply_init_table_func tegra_clk_apply_init_table;

void __init tegra_clocks_apply_init_table(void)
{
	if (!tegra_clk_apply_init_table)
		return;

	tegra_clk_apply_init_table();
}
