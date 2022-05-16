// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) STMicroelectronics 2022 - All Rights Reserved
 * Author: Gabriel Fernandez <gabriel.fernandez@foss.st.com> for STMicroelectronics.
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/slab.h>
#include <linux/spinlock.h>

#include "clk-stm32-core.h"
#include "reset-stm32.h"

static DEFINE_SPINLOCK(rlock);

static int stm32_rcc_clock_init(struct device *dev,
				const struct of_device_id *match,
				void __iomem *base)
{
	const struct stm32_rcc_match_data *data = match->data;
	struct clk_hw_onecell_data *clk_data = data->hw_clks;
	struct device_node *np = dev_of_node(dev);
	struct clk_hw **hws;
	int n, max_binding;

	max_binding =  data->maxbinding;

	clk_data = devm_kzalloc(dev, struct_size(clk_data, hws, max_binding), GFP_KERNEL);
	if (!clk_data)
		return -ENOMEM;

	clk_data->num = max_binding;

	hws = clk_data->hws;

	for (n = 0; n < max_binding; n++)
		hws[n] = ERR_PTR(-ENOENT);

	for (n = 0; n < data->num_clocks; n++) {
		const struct clock_config *cfg_clock = &data->tab_clocks[n];
		struct clk_hw *hw = ERR_PTR(-ENOENT);

		if (cfg_clock->func)
			hw = (*cfg_clock->func)(dev, data, base, &rlock,
						cfg_clock);

		if (IS_ERR(hw)) {
			dev_err(dev, "Can't register clk %d: %ld\n", n,
				PTR_ERR(hw));
			return PTR_ERR(hw);
		}

		if (cfg_clock->id != NO_ID)
			hws[cfg_clock->id] = hw;
	}

	return of_clk_add_hw_provider(np, of_clk_hw_onecell_get, clk_data);
}

int stm32_rcc_init(struct device *dev, const struct of_device_id *match_data,
		   void __iomem *base)
{
	const struct of_device_id *match;
	int err;

	match = of_match_node(match_data, dev_of_node(dev));
	if (!match) {
		dev_err(dev, "match data not found\n");
		return -ENODEV;
	}

	/* RCC Reset Configuration */
	err = stm32_rcc_reset_init(dev, match, base);
	if (err) {
		pr_err("stm32 reset failed to initialize\n");
		return err;
	}

	/* RCC Clock Configuration */
	err = stm32_rcc_clock_init(dev, match, base);
	if (err) {
		pr_err("stm32 clock failed to initialize\n");
		return err;
	}

	return 0;
}
