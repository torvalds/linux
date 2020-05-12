// SPDX-License-Identifier: GPL-2.0+
/*
 * Amlogic Meson SDHC clock controller
 *
 * Copyright (C) 2020 Martin Blumenstingl <martin.blumenstingl@googlemail.com>
 */

#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/device.h>
#include <linux/platform_device.h>

#include "meson-mx-sdhc.h"

#define MESON_SDHC_NUM_BUILTIN_CLKS	6

struct meson_mx_sdhc_clkc {
	struct clk_mux			src_sel;
	struct clk_divider		div;
	struct clk_gate			mod_clk_en;
	struct clk_gate			tx_clk_en;
	struct clk_gate			rx_clk_en;
	struct clk_gate			sd_clk_en;
};

static const struct clk_parent_data meson_mx_sdhc_src_sel_parents[4] = {
	{ .fw_name = "clkin0" },
	{ .fw_name = "clkin1" },
	{ .fw_name = "clkin2" },
	{ .fw_name = "clkin3" },
};

static const struct clk_div_table meson_mx_sdhc_div_table[] = {
	{ .div = 6, .val = 5, },
	{ .div = 8, .val = 7, },
	{ .div = 9, .val = 8, },
	{ .div = 10, .val = 9, },
	{ .div = 12, .val = 11, },
	{ .div = 16, .val = 15, },
	{ .div = 18, .val = 17, },
	{ .div = 34, .val = 33, },
	{ .div = 142, .val = 141, },
	{ .div = 850, .val = 849, },
	{ .div = 2126, .val = 2125, },
	{ .div = 4096, .val = 4095, },
	{ /* sentinel */ }
};

static int meson_mx_sdhc_clk_hw_register(struct device *dev,
					 const char *name_suffix,
					 const struct clk_parent_data *parents,
					 unsigned int num_parents,
					 const struct clk_ops *ops,
					 struct clk_hw *hw)
{
	struct clk_init_data init = { 0 };
	char clk_name[32];

	snprintf(clk_name, sizeof(clk_name), "%s#%s", dev_name(dev),
		 name_suffix);

	init.name = clk_name;
	init.ops = ops;
	init.flags = CLK_SET_RATE_PARENT;
	init.parent_data = parents;
	init.num_parents = num_parents;

	hw->init = &init;

	return devm_clk_hw_register(dev, hw);
}

static int meson_mx_sdhc_gate_clk_hw_register(struct device *dev,
					      const char *name_suffix,
					      struct clk_hw *parent,
					      struct clk_hw *hw)
{
	struct clk_parent_data parent_data = { .hw = parent };

	return meson_mx_sdhc_clk_hw_register(dev, name_suffix, &parent_data, 1,
					     &clk_gate_ops, hw);
}

int meson_mx_sdhc_register_clkc(struct device *dev, void __iomem *base,
				struct clk_bulk_data *clk_bulk_data)
{
	struct clk_parent_data div_parent = { 0 };
	struct meson_mx_sdhc_clkc *clkc_data;
	int ret;

	clkc_data = devm_kzalloc(dev, sizeof(*clkc_data), GFP_KERNEL);
	if (!clkc_data)
		return -ENOMEM;

	clkc_data->src_sel.reg = base + MESON_SDHC_CLKC;
	clkc_data->src_sel.mask = 0x3;
	clkc_data->src_sel.shift = 16;
	ret = meson_mx_sdhc_clk_hw_register(dev, "src_sel",
					    meson_mx_sdhc_src_sel_parents, 4,
					    &clk_mux_ops,
					    &clkc_data->src_sel.hw);
	if (ret)
		return ret;

	clkc_data->div.reg = base + MESON_SDHC_CLKC;
	clkc_data->div.shift = 0;
	clkc_data->div.width = 12;
	clkc_data->div.table = meson_mx_sdhc_div_table;
	div_parent.hw = &clkc_data->src_sel.hw;
	ret = meson_mx_sdhc_clk_hw_register(dev, "div", &div_parent, 1,
					    &clk_divider_ops,
					    &clkc_data->div.hw);
	if (ret)
		return ret;

	clkc_data->mod_clk_en.reg = base + MESON_SDHC_CLKC;
	clkc_data->mod_clk_en.bit_idx = 15;
	ret = meson_mx_sdhc_gate_clk_hw_register(dev, "mod_clk_on",
						 &clkc_data->div.hw,
						 &clkc_data->mod_clk_en.hw);
	if (ret)
		return ret;

	clkc_data->tx_clk_en.reg = base + MESON_SDHC_CLKC;
	clkc_data->tx_clk_en.bit_idx = 14;
	ret = meson_mx_sdhc_gate_clk_hw_register(dev, "tx_clk_on",
						 &clkc_data->div.hw,
						 &clkc_data->tx_clk_en.hw);
	if (ret)
		return ret;

	clkc_data->rx_clk_en.reg = base + MESON_SDHC_CLKC;
	clkc_data->rx_clk_en.bit_idx = 13;
	ret = meson_mx_sdhc_gate_clk_hw_register(dev, "rx_clk_on",
						 &clkc_data->div.hw,
						 &clkc_data->rx_clk_en.hw);
	if (ret)
		return ret;

	clkc_data->sd_clk_en.reg = base + MESON_SDHC_CLKC;
	clkc_data->sd_clk_en.bit_idx = 12;
	ret = meson_mx_sdhc_gate_clk_hw_register(dev, "sd_clk_on",
						 &clkc_data->div.hw,
						 &clkc_data->sd_clk_en.hw);
	if (ret)
		return ret;

	/*
	 * TODO: Replace clk_hw.clk with devm_clk_hw_get_clk() once that is
	 * available.
	 */
	clk_bulk_data[0].clk = clkc_data->mod_clk_en.hw.clk;
	clk_bulk_data[1].clk = clkc_data->sd_clk_en.hw.clk;
	clk_bulk_data[2].clk = clkc_data->tx_clk_en.hw.clk;
	clk_bulk_data[3].clk = clkc_data->rx_clk_en.hw.clk;

	return 0;
}
