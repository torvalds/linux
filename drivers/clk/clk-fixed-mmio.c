// SPDX-License-Identifier: GPL-2.0

/*
 * Memory Mapped IO Fixed clock driver
 *
 * Copyright (C) 2018 Cadence Design Systems, Inc.
 *
 * Authors:
 *	Jan Kotas <jank@cadence.com>
 */

#include <linux/clk-provider.h>
#include <linux/of_address.h>
#include <linux/module.h>
#include <linux/platform_device.h>

static struct clk_hw *fixed_mmio_clk_setup(struct device_node *node)
{
	struct clk_hw *clk;
	const char *clk_name = node->name;
	void __iomem *base;
	u32 freq;
	int ret;

	base = of_iomap(node, 0);
	if (!base) {
		pr_err("%pOFn: failed to map address\n", node);
		return ERR_PTR(-EIO);
	}

	freq = readl(base);
	iounmap(base);
	of_property_read_string(node, "clock-output-names", &clk_name);

	clk = clk_hw_register_fixed_rate(NULL, clk_name, NULL, 0, freq);
	if (IS_ERR(clk)) {
		pr_err("%pOFn: failed to register fixed rate clock\n", node);
		return clk;
	}

	ret = of_clk_add_hw_provider(node, of_clk_hw_simple_get, clk);
	if (ret) {
		pr_err("%pOFn: failed to add clock provider\n", node);
		clk_hw_unregister(clk);
		clk = ERR_PTR(ret);
	}

	return clk;
}

static void __init of_fixed_mmio_clk_setup(struct device_node *node)
{
	fixed_mmio_clk_setup(node);
}
CLK_OF_DECLARE(fixed_mmio_clk, "fixed-mmio-clock", of_fixed_mmio_clk_setup);

/**
 * This is not executed when of_fixed_mmio_clk_setup succeeded.
 */
static int of_fixed_mmio_clk_probe(struct platform_device *pdev)
{
	struct clk_hw *clk;

	clk = fixed_mmio_clk_setup(pdev->dev.of_node);
	if (IS_ERR(clk))
		return PTR_ERR(clk);

	platform_set_drvdata(pdev, clk);

	return 0;
}

static int of_fixed_mmio_clk_remove(struct platform_device *pdev)
{
	struct clk_hw *clk = platform_get_drvdata(pdev);

	of_clk_del_provider(pdev->dev.of_node);
	clk_hw_unregister_fixed_rate(clk);

	return 0;
}

static const struct of_device_id of_fixed_mmio_clk_ids[] = {
	{ .compatible = "fixed-mmio-clock" },
	{ }
};
MODULE_DEVICE_TABLE(of, of_fixed_mmio_clk_ids);

static struct platform_driver of_fixed_mmio_clk_driver = {
	.driver = {
		.name = "of_fixed_mmio_clk",
		.of_match_table = of_fixed_mmio_clk_ids,
	},
	.probe = of_fixed_mmio_clk_probe,
	.remove = of_fixed_mmio_clk_remove,
};
module_platform_driver(of_fixed_mmio_clk_driver);

MODULE_AUTHOR("Jan Kotas <jank@cadence.com>");
MODULE_DESCRIPTION("Memory Mapped IO Fixed clock driver");
MODULE_LICENSE("GPL v2");
