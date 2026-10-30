// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Stefan Dösinger
 */

#include <linux/clk-provider.h>
#include <linux/clk.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>

#include "clk-zx.h"

static struct clk_hw *zx_clk_register_fixed_div(struct device *dev,
						const struct zx_fixed_divider_desc *desc,
						struct clk_hw * const *clocks)
{
	const struct zx_parent_desc *p = &desc->parent;

	switch (p->type) {
	case ZX_PARENT_FW:
		return devm_clk_hw_register_fixed_factor_fwname(dev, NULL, desc->name, p->fw_name,
								CLK_SET_RATE_PARENT, 1, desc->div);

	case ZX_PARENT_ID:
		return devm_clk_hw_register_fixed_factor_parent_hw(dev, desc->name, clocks[p->id],
								   CLK_SET_RATE_PARENT, 1,
								   desc->div);
	}
	WARN_ON_ONCE(1);
	return ERR_PTR(-EINVAL);
}

static int zx_clk_validate(struct device *dev, struct device_node *of_node,
			   const struct zx_clk_data *data)
{
	const struct zx_parent_desc *parents;
	unsigned int i, p, num_parents;
	struct clk *clk;

	/*
	 * Sanity check: Make sure all parents are there and write a clear message rather than
	 * leave potential orphans.
	 *
	 * This check is unusual in clock drivers. It is in this driver because the clock tree is
	 * based on manual reverse engineering rather than generated from a SoC datasheet.
	 */
	for (i = 0; i < data->num_clocks; ++i) {
		switch (data->clocks[i].type) {
		case ZX_CLOCK_PLL:
			parents = data->clocks[i].pll.parents;
			num_parents = data->clocks[i].pll.num_parents;
			break;

		case ZX_CLOCK_FIXED_DIV:
			parents = &data->clocks[i].fixed_div.parent;
			num_parents = 1;
			break;

		case ZX_CLOCK_MUX:
			parents = data->clocks[i].mux.parents;
			num_parents = data->clocks[i].mux.num_parents;
			break;

		case ZX_CLOCK_DIV:
			parents = &data->clocks[i].div.parent;
			num_parents = 1;
			break;

		case ZX_CLOCK_GATE:
			parents = &data->clocks[i].gate.parent;
			num_parents = 1;
			break;

		default:
			return dev_err_probe(dev, -EINVAL, "Invalid clock entry %u\n", i);
		}

		for (p = 0; p < num_parents; ++p) {
			switch (parents[p].type) {
			case ZX_PARENT_FW:
				clk = of_clk_get_by_name(of_node, parents[p].fw_name);
				if (IS_ERR(clk))
					return dev_err_probe(dev, PTR_ERR(clk),
							     "Input clk %s failure\n",
							     parents[p].fw_name);
				clk_put(clk);
				break;

			case ZX_PARENT_ID:
				if (parents[p].id >= i)
					return dev_err_probe(dev, -EINVAL,
							     "Clock %u has parent %u\n",
							     i, parents[p].id);
				break;

			default:
				return dev_err_probe(dev, -EINVAL,
						     "Clock %u has unexpected parent of type %u\n",
						     i, parents[p].type);
			}
		}
	}

	return 0;
}

int zx_clk_common_probe(struct device *dev, struct device_node *of_node,
			const struct zx_clk_data *data)
{
	struct clk_hw_onecell_data *exports;
	struct clk_hw **clocks;
	struct regmap *map;
	unsigned int i;
	int res;

	res = zx_clk_validate(dev, of_node, data);
	if (res)
		return res;

	map = device_node_to_regmap(of_node);
	if (IS_ERR(map))
		return PTR_ERR(map);

	clocks = devm_kcalloc(dev, data->num_clocks, sizeof(*clocks), GFP_KERNEL);
	if (!clocks)
		return -ENOMEM;

	if (data->init) {
		res = data->init(map);
		if (res)
			return res;
	}

	for (i = 0; i < data->num_clocks; ++i) {
		struct clk_hw *hw;

		switch (data->clocks[i].type) {
		case ZX_CLOCK_PLL:
			hw = zx_clk_register_pll(dev, map, &data->clocks[i].pll, clocks);
			break;

		case ZX_CLOCK_FIXED_DIV:
			hw = zx_clk_register_fixed_div(dev, &data->clocks[i].fixed_div, clocks);
			break;

		case ZX_CLOCK_MUX:
			hw = zx_clk_register_mux(dev, map, &data->clocks[i].mux, clocks);
			break;

		case ZX_CLOCK_DIV:
			hw = zx_clk_register_divider(dev, map, &data->clocks[i].div, clocks);
			break;

		case ZX_CLOCK_GATE:
			hw = zx_clk_register_gate(dev, map, &data->clocks[i].gate, clocks);
			break;

		default:
			return -EINVAL;
		}

		if (IS_ERR(hw))
			return dev_err_probe(dev, PTR_ERR(hw), "Failed to register clk %u\n", i);

		clocks[i] = hw;
	}

	exports = devm_kzalloc(dev, struct_size(exports, hws, data->num_exports), GFP_KERNEL);
	if (!exports)
		return -ENOMEM;
	exports->num = data->num_exports;

	for (i = 0; i < data->num_exports; ++i) {
		if (data->exports[i] >= data->num_clocks)
			return dev_err_probe(dev, -EINVAL,
					     "Export %u points to out of range clock %u\n",
					     i, data->exports[i]);

		exports->hws[i] = clocks[data->exports[i]];
	}

	devm_kfree(dev, clocks);

	return devm_of_clk_add_hw_provider(dev, of_clk_hw_onecell_get, exports);
}
EXPORT_SYMBOL_NS_GPL(zx_clk_common_probe, "ZTE_CLK");

MODULE_AUTHOR("Stefan Dösinger <stefandoesinger@gmail.com>");
MODULE_DESCRIPTION("ZTE common clock driver");
MODULE_LICENSE("GPL");
