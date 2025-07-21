// SPDX-License-Identifier: (GPL-2.0 OR MIT)
/*
 * Copyright (c) 2019 BayLibre, SAS.
 * Author: Neil Armstrong <narmstrong@baylibre.com>
 */

#include <linux/clk-provider.h>
#include <linux/module.h>

#include "clk-regmap.h"
#include "clk-cpu-dyndiv.h"

static inline struct meson_clk_cpu_dyndiv_data *
meson_clk_cpu_dyndiv_data(struct clk_regmap *clk)
{
	return (struct meson_clk_cpu_dyndiv_data *)clk->data;
}

static unsigned long meson_clk_cpu_dyndiv_recalc_rate(struct clk_hw *hw,
						      unsigned long prate)
{
	struct clk_regmap *clk = to_clk_regmap(hw);
	struct meson_clk_cpu_dyndiv_data *data = meson_clk_cpu_dyndiv_data(clk);

	return divider_recalc_rate(hw, prate,
				   meson_parm_read(clk->map, &data->div),
				   NULL, 0, data->div.width);
}

static int meson_clk_cpu_dyndiv_determine_rate(struct clk_hw *hw,
					       struct clk_rate_request *req)
{
	struct clk_regmap *clk = to_clk_regmap(hw);
	struct meson_clk_cpu_dyndiv_data *data = meson_clk_cpu_dyndiv_data(clk);

	return divider_determine_rate(hw, req, NULL, data->div.width, 0);
}

static int meson_clk_cpu_dyndiv_set_rate(struct clk_hw *hw, unsigned long rate,
					  unsigned long parent_rate)
{
	struct clk_regmap *clk = to_clk_regmap(hw);
	struct meson_clk_cpu_dyndiv_data *data = meson_clk_cpu_dyndiv_data(clk);
	unsigned int val;
	int ret;

	ret = divider_get_val(rate, parent_rate, NULL, data->div.width, 0);
	if (ret < 0)
		return ret;

	val = (unsigned int)ret << data->div.shift;

	/* Write the SYS_CPU_DYN_ENABLE bit before changing the divider */
	meson_parm_write(clk->map, &data->dyn, 1);

	/* Update the divider while removing the SYS_CPU_DYN_ENABLE bit */
	return regmap_update_bits(clk->map, data->div.reg_off,
				  SETPMASK(data->div.width, data->div.shift) |
				  SETPMASK(data->dyn.width, data->dyn.shift),
				  val);
};

const struct clk_ops meson_clk_cpu_dyndiv_ops = {
	.init = clk_regmap_init,
	.recalc_rate = meson_clk_cpu_dyndiv_recalc_rate,
	.determine_rate = meson_clk_cpu_dyndiv_determine_rate,
	.set_rate = meson_clk_cpu_dyndiv_set_rate,
};
EXPORT_SYMBOL_NS_GPL(meson_clk_cpu_dyndiv_ops, "CLK_MESON");

MODULE_DESCRIPTION("Amlogic CPU Dynamic Clock divider");
MODULE_AUTHOR("Neil Armstrong <narmstrong@baylibre.com>");
MODULE_LICENSE("GPL");
MODULE_IMPORT_NS("CLK_MESON");
