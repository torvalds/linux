// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (c) 2023 Neil Armstrong <neil.armstrong@linaro.org>
 */

#include <linux/of_device.h>
#include <linux/clk-provider.h>
#include <linux/module.h>
#include "meson-clkc-utils.h"

struct clk_hw *meson_clk_hw_get(struct of_phandle_args *clkspec, void *clk_hw_data)
{
	const struct meson_clk_hw_data *data = clk_hw_data;
	unsigned int idx = clkspec->args[0];

	if (idx >= data->num) {
		pr_err("%s: invalid index %u\n", __func__, idx);
		return ERR_PTR(-EINVAL);
	}

	return data->hws[idx];
}
EXPORT_SYMBOL_NS_GPL(meson_clk_hw_get, "CLK_MESON");

MODULE_DESCRIPTION("Amlogic Clock Controller Utilities");
MODULE_LICENSE("GPL");
MODULE_IMPORT_NS("CLK_MESON");
