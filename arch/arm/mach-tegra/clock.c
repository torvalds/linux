/*
 *
 * Copyright (C) 2010 Google, Inc.
 * Copyright (c) 2012 NVIDIA CORPORATION.  All rights reserved.
 *
 * Author:
 *	Colin Cross <ccross@google.com>
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/kernel.h>
#include <linux/clk.h>
#include <linux/clkdev.h>
#include <linux/init.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/seq_file.h>
#include <linux/slab.h>

#include <mach/clk.h>

#include "board.h"
#include "clock.h"
#include "tegra_cpu_car.h"

/* Global data of Tegra CPU CAR ops */
struct tegra_cpu_car_ops *tegra_cpu_car_ops;

/*
 * Locking:
 *
 * An additional mutex, clock_list_lock, is used to protect the list of all
 * clocks.
 *
 */
static DEFINE_MUTEX(clock_list_lock);
static LIST_HEAD(clocks);

void tegra_clk_add(struct clk *clk)
{
	struct clk_tegra *c = to_clk_tegra(__clk_get_hw(clk));

	mutex_lock(&clock_list_lock);
	list_add(&c->node, &clocks);
	mutex_unlock(&clock_list_lock);
}

struct clk *tegra_get_clock_by_name(const char *name)
{
	struct clk_tegra *c;
	struct clk *ret = NULL;
	mutex_lock(&clock_list_lock);
	list_for_each_entry(c, &clocks, node) {
		if (strcmp(__clk_get_name(c->hw.clk), name) == 0) {
			ret = c->hw.clk;
			break;
		}
	}
	mutex_unlock(&clock_list_lock);
	return ret;
}

static int tegra_clk_init_one_from_table(struct tegra_clk_init_table *table)
{
	struct clk *c;
	struct clk *p;
	struct clk *parent;

	int ret = 0;

	c = tegra_get_clock_by_name(table->name);

	if (!c) {
		pr_warn("Unable to initialize clock %s\n",
			table->name);
		return -ENODEV;
	}

	parent = clk_get_parent(c);

	if (table->parent) {
		p = tegra_get_clock_by_name(table->parent);
		if (!p) {
			pr_warn("Unable to find parent %s of clock %s\n",
				table->parent, table->name);
			return -ENODEV;
		}

		if (parent != p) {
			ret = clk_set_parent(c, p);
			if (ret) {
				pr_warn("Unable to set parent %s of clock %s: %d\n",
					table->parent, table->name, ret);
				return -EINVAL;
			}
		}
	}

	if (table->rate && table->rate != clk_get_rate(c)) {
		ret = clk_set_rate(c, table->rate);
		if (ret) {
			pr_warn("Unable to set clock %s to rate %lu: %d\n",
				table->name, table->rate, ret);
			return -EINVAL;
		}
	}

	if (table->enabled) {
		ret = clk_prepare_enable(c);
		if (ret) {
			pr_warn("Unable to enable clock %s: %d\n",
				table->name, ret);
			return -EINVAL;
		}
	}

	return 0;
}

void tegra_clk_init_from_table(struct tegra_clk_init_table *table)
{
	for (; table->name; table++)
		tegra_clk_init_one_from_table(table);
}

void tegra_periph_reset_deassert(struct clk *c)
{
	struct clk_tegra *clk = to_clk_tegra(__clk_get_hw(c));
	BUG_ON(!clk->reset);
	clk->reset(__clk_get_hw(c), false);
}
EXPORT_SYMBOL(tegra_periph_reset_deassert);

void tegra_periph_reset_assert(struct clk *c)
{
	struct clk_tegra *clk = to_clk_tegra(__clk_get_hw(c));
	BUG_ON(!clk->reset);
	clk->reset(__clk_get_hw(c), true);
}
EXPORT_SYMBOL(tegra_periph_reset_assert);

/* Several extended clock configuration bits (e.g., clock routing, clock
 * phase control) are included in PLL and peripheral clock source
 * registers. */
int tegra_clk_cfg_ex(struct clk *c, enum tegra_clk_ex_param p, u32 setting)
{
	int ret = 0;
	struct clk_tegra *clk = to_clk_tegra(__clk_get_hw(c));

	if (!clk->clk_cfg_ex) {
		ret = -ENOSYS;
		goto out;
	}
	ret = clk->clk_cfg_ex(__clk_get_hw(c), p, setting);

out:
	return ret;
}
