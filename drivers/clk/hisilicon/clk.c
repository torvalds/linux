/*
 * Hisilicon clock driver
 *
 * Copyright (c) 2012-2013 Hisilicon Limited.
 * Copyright (c) 2012-2013 Linaro Limited.
 *
 * Author: Haojian Zhuang <haojian.zhuang@linaro.org>
 *	   Xin Li <li.xin@linaro.org>
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
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 */

#include <linux/kernel.h>
#include <linux/clk-provider.h>
#include <linux/clkdev.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/slab.h>
#include <linux/clk.h>

#include "clk.h"

static DEFINE_SPINLOCK(hisi_clk_lock);
static struct clk **clk_table;
static struct clk_onecell_data clk_data;

void __init hisi_clk_init(struct device_node *np, int nr_clks)
{
	clk_table = kzalloc(sizeof(struct clk *) * nr_clks, GFP_KERNEL);
	if (!clk_table) {
		pr_err("%s: could not allocate clock lookup table\n", __func__);
		return;
	}
	clk_data.clks = clk_table;
	clk_data.clk_num = nr_clks;
	of_clk_add_provider(np, of_clk_src_onecell_get, &clk_data);
}

void __init hisi_clk_register_fixed_rate(struct hisi_fixed_rate_clock *clks,
					 int nums, void __iomem *base)
{
	struct clk *clk;
	int i;

	for (i = 0; i < nums; i++) {
		clk = clk_register_fixed_rate(NULL, clks[i].name,
					      clks[i].parent_name,
					      clks[i].flags,
					      clks[i].fixed_rate);
		if (IS_ERR(clk)) {
			pr_err("%s: failed to register clock %s\n",
			       __func__, clks[i].name);
			continue;
		}
	}
}

void __init hisi_clk_register_fixed_factor(struct hisi_fixed_factor_clock *clks,
					   int nums, void __iomem *base)
{
	struct clk *clk;
	int i;

	for (i = 0; i < nums; i++) {
		clk = clk_register_fixed_factor(NULL, clks[i].name,
						clks[i].parent_name,
						clks[i].flags, clks[i].mult,
						clks[i].div);
		if (IS_ERR(clk)) {
			pr_err("%s: failed to register clock %s\n",
			       __func__, clks[i].name);
			continue;
		}
	}
}

void __init hisi_clk_register_mux(struct hisi_mux_clock *clks,
				  int nums, void __iomem *base)
{
	struct clk *clk;
	int i;

	for (i = 0; i < nums; i++) {
		clk = clk_register_mux(NULL, clks[i].name, clks[i].parent_names,
				       clks[i].num_parents, clks[i].flags,
				       base + clks[i].offset, clks[i].shift,
				       clks[i].width, clks[i].mux_flags,
				       &hisi_clk_lock);
		if (IS_ERR(clk)) {
			pr_err("%s: failed to register clock %s\n",
			       __func__, clks[i].name);
			continue;
		}

		if (clks[i].alias)
			clk_register_clkdev(clk, clks[i].alias, NULL);

		clk_table[clks[i].id] = clk;
	}
}

void __init hisi_clk_register_divider(struct hisi_divider_clock *clks,
				      int nums, void __iomem *base)
{
	struct clk *clk;
	int i;

	for (i = 0; i < nums; i++) {
		clk = clk_register_divider_table(NULL, clks[i].name,
						 clks[i].parent_name,
						 clks[i].flags,
						 base + clks[i].offset,
						 clks[i].shift, clks[i].width,
						 clks[i].div_flags,
						 clks[i].table,
						 &hisi_clk_lock);
		if (IS_ERR(clk)) {
			pr_err("%s: failed to register clock %s\n",
			       __func__, clks[i].name);
			continue;
		}

		if (clks[i].alias)
			clk_register_clkdev(clk, clks[i].alias, NULL);

		clk_table[clks[i].id] = clk;
	}
}

void __init hisi_clk_register_gate_sep(struct hisi_gate_clock *clks,
				       int nums, void __iomem *base)
{
	struct clk *clk;
	int i;

	for (i = 0; i < nums; i++) {
		clk = hisi_register_clkgate_sep(NULL, clks[i].name,
						clks[i].parent_name,
						clks[i].flags,
						base + clks[i].offset,
						clks[i].bit_idx,
						clks[i].gate_flags,
						&hisi_clk_lock);
		if (IS_ERR(clk)) {
			pr_err("%s: failed to register clock %s\n",
			       __func__, clks[i].name);
			continue;
		}

		if (clks[i].alias)
			clk_register_clkdev(clk, clks[i].alias, NULL);

		clk_table[clks[i].id] = clk;
	}
}
