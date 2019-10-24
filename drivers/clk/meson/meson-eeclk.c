// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2019 BayLibre, SAS.
 * Author: Jerome Brunet <jbrunet@baylibre.com>
 */

#include <linux/clk-provider.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/mfd/syscon.h>
#include <linux/regmap.h>

#include "clk-input.h"
#include "clk-regmap.h"
#include "meson-eeclk.h"

int meson_eeclkc_probe(struct platform_device *pdev)
{
	const struct meson_eeclkc_data *data;
	struct device *dev = &pdev->dev;
	struct clk_hw *input;
	struct regmap *map;
	int ret, i;

	data = of_device_get_match_data(dev);
	if (!data)
		return -EINVAL;

	/* Get the hhi system controller node */
	map = syscon_node_to_regmap(of_get_parent(dev->of_node));
	if (IS_ERR(map)) {
		dev_err(dev,
			"failed to get HHI regmap\n");
		return PTR_ERR(map);
	}

	if (data->init_count)
		regmap_multi_reg_write(map, data->init_regs, data->init_count);

	input = meson_clk_hw_register_input(dev, "xtal", IN_PREFIX "xtal", 0);
	if (IS_ERR(input)) {
		ret = PTR_ERR(input);
		if (ret != -EPROBE_DEFER)
			dev_err(dev, "failed to get input clock");
		return ret;
	}

	/* Populate regmap for the regmap backed clocks */
	for (i = 0; i < data->regmap_clk_num; i++)
		data->regmap_clks[i]->map = map;

	for (i = 0; i < data->hw_onecell_data->num; i++) {
		/* array might be sparse */
		if (!data->hw_onecell_data->hws[i])
			continue;

		ret = devm_clk_hw_register(dev, data->hw_onecell_data->hws[i]);
		if (ret) {
			dev_err(dev, "Clock registration failed\n");
			return ret;
		}
	}

	return devm_of_clk_add_hw_provider(dev, of_clk_hw_onecell_get,
					   data->hw_onecell_data);
}
