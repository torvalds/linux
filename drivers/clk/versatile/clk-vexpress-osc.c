// SPDX-License-Identifier: GPL-2.0-only
/*
 *
 * Copyright (C) 2012 ARM Limited
 */

#include <linux/clkdev.h>
#include <linux/clk-provider.h>
#include <linux/err.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/vexpress.h>

struct vexpress_osc {
	struct regmap *reg;
	struct clk_hw hw;
	unsigned long rate_min;
	unsigned long rate_max;
};

#define to_vexpress_osc(osc) container_of(osc, struct vexpress_osc, hw)

static unsigned long vexpress_osc_recalc_rate(struct clk_hw *hw,
		unsigned long parent_rate)
{
	struct vexpress_osc *osc = to_vexpress_osc(hw);
	u32 rate;

	regmap_read(osc->reg, 0, &rate);

	return rate;
}

static int vexpress_osc_determine_rate(struct clk_hw *hw,
				       struct clk_rate_request *req)
{
	struct vexpress_osc *osc = to_vexpress_osc(hw);

	if (osc->rate_min && req->rate < osc->rate_min)
		req->rate = osc->rate_min;

	if (osc->rate_max && req->rate > osc->rate_max)
		req->rate = osc->rate_max;

	return 0;
}

static int vexpress_osc_set_rate(struct clk_hw *hw, unsigned long rate,
		unsigned long parent_rate)
{
	struct vexpress_osc *osc = to_vexpress_osc(hw);

	return regmap_write(osc->reg, 0, rate);
}

static const struct clk_ops vexpress_osc_ops = {
	.recalc_rate = vexpress_osc_recalc_rate,
	.determine_rate = vexpress_osc_determine_rate,
	.set_rate = vexpress_osc_set_rate,
};


static int vexpress_osc_probe(struct platform_device *pdev)
{
	struct clk_init_data init;
	struct vexpress_osc *osc;
	u32 range[2];
	int ret;

	osc = devm_kzalloc(&pdev->dev, sizeof(*osc), GFP_KERNEL);
	if (!osc)
		return -ENOMEM;

	osc->reg = devm_regmap_init_vexpress_config(&pdev->dev);
	if (IS_ERR(osc->reg))
		return PTR_ERR(osc->reg);

	if (of_property_read_u32_array(pdev->dev.of_node, "freq-range", range,
			ARRAY_SIZE(range)) == 0) {
		osc->rate_min = range[0];
		osc->rate_max = range[1];
	}

	if (of_property_read_string(pdev->dev.of_node, "clock-output-names",
			&init.name) != 0)
		init.name = dev_name(&pdev->dev);

	init.ops = &vexpress_osc_ops;
	init.flags = 0;
	init.num_parents = 0;

	osc->hw.init = &init;

	ret = devm_clk_hw_register(&pdev->dev, &osc->hw);
	if (ret < 0)
		return ret;

	devm_of_clk_add_hw_provider(&pdev->dev, of_clk_hw_simple_get, &osc->hw);
	clk_hw_set_rate_range(&osc->hw, osc->rate_min, osc->rate_max);

	dev_dbg(&pdev->dev, "Registered clock '%s'\n", init.name);

	return 0;
}

static const struct of_device_id vexpress_osc_of_match[] = {
	{ .compatible = "arm,vexpress-osc", },
	{}
};
MODULE_DEVICE_TABLE(of, vexpress_osc_of_match);

static struct platform_driver vexpress_osc_driver = {
	.driver	= {
		.name = "vexpress-osc",
		.of_match_table = vexpress_osc_of_match,
	},
	.probe = vexpress_osc_probe,
};
module_platform_driver(vexpress_osc_driver);
MODULE_DESCRIPTION("Clock driver for Versatile Express OSC clock generators");
MODULE_LICENSE("GPL v2");
