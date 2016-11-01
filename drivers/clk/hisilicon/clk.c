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
#include <linux/clkdev.h>
#include <linux/clk-provider.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/slab.h>

#include "clk.h"

static DEFINE_SPINLOCK(hisi_clk_lock);

struct hisi_clock_data *hisi_clk_alloc(struct platform_device *pdev,
						int nr_clks)
{
	struct hisi_clock_data *clk_data;
	struct resource *res;
	struct clk **clk_table;

	clk_data = devm_kmalloc(&pdev->dev, sizeof(*clk_data), GFP_KERNEL);
	if (!clk_data)
		return NULL;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	clk_data->base = devm_ioremap(&pdev->dev,
				res->start, resource_size(res));
	if (!clk_data->base)
		return NULL;

	clk_table = devm_kmalloc(&pdev->dev, sizeof(struct clk *) * nr_clks,
				GFP_KERNEL);
	if (!clk_table)
		return NULL;

	clk_data->clk_data.clks = clk_table;
	clk_data->clk_data.clk_num = nr_clks;

	return clk_data;
}
EXPORT_SYMBOL_GPL(hisi_clk_alloc);

struct hisi_clock_data *hisi_clk_init(struct device_node *np,
					     int nr_clks)
{
	struct hisi_clock_data *clk_data;
	struct clk **clk_table;
	void __iomem *base;

	base = of_iomap(np, 0);
	if (!base) {
		pr_err("%s: failed to map clock registers\n", __func__);
		goto err;
	}

	clk_data = kzalloc(sizeof(*clk_data), GFP_KERNEL);
	if (!clk_data) {
		pr_err("%s: could not allocate clock data\n", __func__);
		goto err;
	}
	clk_data->base = base;

	clk_table = kzalloc(sizeof(struct clk *) * nr_clks, GFP_KERNEL);
	if (!clk_table) {
		pr_err("%s: could not allocate clock lookup table\n", __func__);
		goto err_data;
	}
	clk_data->clk_data.clks = clk_table;
	clk_data->clk_data.clk_num = nr_clks;
	of_clk_add_provider(np, of_clk_src_onecell_get, &clk_data->clk_data);
	return clk_data;
err_data:
	kfree(clk_data);
err:
	return NULL;
}
EXPORT_SYMBOL_GPL(hisi_clk_init);

int hisi_clk_register_fixed_rate(const struct hisi_fixed_rate_clock *clks,
					 int nums, struct hisi_clock_data *data)
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
			goto err;
		}
		data->clk_data.clks[clks[i].id] = clk;
	}

	return 0;

err:
	while (i--)
		clk_unregister_fixed_rate(data->clk_data.clks[clks[i].id]);

	return PTR_ERR(clk);
}
EXPORT_SYMBOL_GPL(hisi_clk_register_fixed_rate);

int hisi_clk_register_fixed_factor(const struct hisi_fixed_factor_clock *clks,
					   int nums,
					   struct hisi_clock_data *data)
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
			goto err;
		}
		data->clk_data.clks[clks[i].id] = clk;
	}

	return 0;

err:
	while (i--)
		clk_unregister_fixed_factor(data->clk_data.clks[clks[i].id]);

	return PTR_ERR(clk);
}
EXPORT_SYMBOL_GPL(hisi_clk_register_fixed_factor);

int hisi_clk_register_mux(const struct hisi_mux_clock *clks,
				  int nums, struct hisi_clock_data *data)
{
	struct clk *clk;
	void __iomem *base = data->base;
	int i;

	for (i = 0; i < nums; i++) {
		u32 mask = BIT(clks[i].width) - 1;

		clk = clk_register_mux_table(NULL, clks[i].name,
					clks[i].parent_names,
					clks[i].num_parents, clks[i].flags,
					base + clks[i].offset, clks[i].shift,
					mask, clks[i].mux_flags,
					clks[i].table, &hisi_clk_lock);
		if (IS_ERR(clk)) {
			pr_err("%s: failed to register clock %s\n",
			       __func__, clks[i].name);
			goto err;
		}

		if (clks[i].alias)
			clk_register_clkdev(clk, clks[i].alias, NULL);

		data->clk_data.clks[clks[i].id] = clk;
	}

	return 0;

err:
	while (i--)
		clk_unregister_mux(data->clk_data.clks[clks[i].id]);

	return PTR_ERR(clk);
}
EXPORT_SYMBOL_GPL(hisi_clk_register_mux);

int hisi_clk_register_divider(const struct hisi_divider_clock *clks,
				      int nums, struct hisi_clock_data *data)
{
	struct clk *clk;
	void __iomem *base = data->base;
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
			goto err;
		}

		if (clks[i].alias)
			clk_register_clkdev(clk, clks[i].alias, NULL);

		data->clk_data.clks[clks[i].id] = clk;
	}

	return 0;

err:
	while (i--)
		clk_unregister_divider(data->clk_data.clks[clks[i].id]);

	return PTR_ERR(clk);
}
EXPORT_SYMBOL_GPL(hisi_clk_register_divider);

int hisi_clk_register_gate(const struct hisi_gate_clock *clks,
				       int nums, struct hisi_clock_data *data)
{
	struct clk *clk;
	void __iomem *base = data->base;
	int i;

	for (i = 0; i < nums; i++) {
		clk = clk_register_gate(NULL, clks[i].name,
						clks[i].parent_name,
						clks[i].flags,
						base + clks[i].offset,
						clks[i].bit_idx,
						clks[i].gate_flags,
						&hisi_clk_lock);
		if (IS_ERR(clk)) {
			pr_err("%s: failed to register clock %s\n",
			       __func__, clks[i].name);
			goto err;
		}

		if (clks[i].alias)
			clk_register_clkdev(clk, clks[i].alias, NULL);

		data->clk_data.clks[clks[i].id] = clk;
	}

	return 0;

err:
	while (i--)
		clk_unregister_gate(data->clk_data.clks[clks[i].id]);

	return PTR_ERR(clk);
}
EXPORT_SYMBOL_GPL(hisi_clk_register_gate);

void hisi_clk_register_gate_sep(const struct hisi_gate_clock *clks,
				       int nums, struct hisi_clock_data *data)
{
	struct clk *clk;
	void __iomem *base = data->base;
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

		data->clk_data.clks[clks[i].id] = clk;
	}
}
EXPORT_SYMBOL_GPL(hisi_clk_register_gate_sep);

void __init hi6220_clk_register_divider(const struct hi6220_divider_clock *clks,
					int nums, struct hisi_clock_data *data)
{
	struct clk *clk;
	void __iomem *base = data->base;
	int i;

	for (i = 0; i < nums; i++) {
		clk = hi6220_register_clkdiv(NULL, clks[i].name,
						clks[i].parent_name,
						clks[i].flags,
						base + clks[i].offset,
						clks[i].shift,
						clks[i].width,
						clks[i].mask_bit,
						&hisi_clk_lock);
		if (IS_ERR(clk)) {
			pr_err("%s: failed to register clock %s\n",
			       __func__, clks[i].name);
			continue;
		}

		if (clks[i].alias)
			clk_register_clkdev(clk, clks[i].alias, NULL);

		data->clk_data.clks[clks[i].id] = clk;
	}
}
