// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2018, The Linux Foundation. All rights reserved.

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/property.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/clk.h>
#include <linux/clk-provider.h>

static const struct clk_parent_data aux_parents[] = {
	{ .fw_name = "pll8_vote", .name = "pll8_vote" },
	{ .fw_name = "pxo", .name = "pxo_board" },
};

static const u32 aux_parent_map[] = {
	3,
	0,
};

static const struct of_device_id kpss_xcc_match_table[] = {
	{ .compatible = "qcom,kpss-acc-v1", .data = (void *)1UL },
	{ .compatible = "qcom,kpss-gcc" },
	{}
};
MODULE_DEVICE_TABLE(of, kpss_xcc_match_table);

static int kpss_xcc_driver_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	void __iomem *base;
	struct clk_hw *hw;
	const char *name;

	base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(base))
		return PTR_ERR(base);

	if (device_get_match_data(&pdev->dev)) {
		if (of_property_read_string_index(dev->of_node,
						  "clock-output-names",
						  0, &name))
			return -ENODEV;
		base += 0x14;
	} else {
		name = "acpu_l2_aux";
		base += 0x28;
	}

	hw = devm_clk_hw_register_mux_parent_data_table(dev, name, aux_parents,
							ARRAY_SIZE(aux_parents), 0,
							base, 0, 0x3,
							0, aux_parent_map, NULL);
	if (IS_ERR(hw))
		return PTR_ERR(hw);

	of_clk_add_hw_provider(dev->of_node, of_clk_hw_simple_get, hw);

	return 0;
}

static struct platform_driver kpss_xcc_driver = {
	.probe = kpss_xcc_driver_probe,
	.driver = {
		.name = "kpss-xcc",
		.of_match_table = kpss_xcc_match_table,
	},
};
module_platform_driver(kpss_xcc_driver);

MODULE_DESCRIPTION("Krait Processor Sub System (KPSS) Clock Driver");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:kpss-xcc");
