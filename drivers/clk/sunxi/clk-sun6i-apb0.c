// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2014 Free Electrons
 *
 * Author: Boris BREZILLON <boris.brezillon@free-electrons.com>
 *
 * Allwinner A31 APB0 clock driver
 */

#include <linux/clk-provider.h>
#include <linux/init.h>
#include <linux/of.h>
#include <linux/platform_device.h>

/*
 * The APB0 clk has a configurable divisor.
 *
 * We must use a clk_div_table and not a regular power of 2
 * divisor here, because the first 2 values divide the clock
 * by 2.
 */
static const struct clk_div_table sun6i_a31_apb0_divs[] = {
	{ .val = 0, .div = 2, },
	{ .val = 1, .div = 2, },
	{ .val = 2, .div = 4, },
	{ .val = 3, .div = 8, },
	{ /* sentinel */ },
};

static int sun6i_a31_apb0_clk_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	const char *clk_name = np->name;
	const char *clk_parent;
	struct resource *r;
	void __iomem *reg;
	struct clk *clk;

	r = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	reg = devm_ioremap_resource(&pdev->dev, r);
	if (IS_ERR(reg))
		return PTR_ERR(reg);

	clk_parent = of_clk_get_parent_name(np, 0);
	if (!clk_parent)
		return -EINVAL;

	of_property_read_string(np, "clock-output-names", &clk_name);

	clk = clk_register_divider_table(&pdev->dev, clk_name, clk_parent,
					 0, reg, 0, 2, 0, sun6i_a31_apb0_divs,
					 NULL);
	if (IS_ERR(clk))
		return PTR_ERR(clk);

	return of_clk_add_provider(np, of_clk_src_simple_get, clk);
}

static const struct of_device_id sun6i_a31_apb0_clk_dt_ids[] = {
	{ .compatible = "allwinner,sun6i-a31-apb0-clk" },
	{ /* sentinel */ }
};

static struct platform_driver sun6i_a31_apb0_clk_driver = {
	.driver = {
		.name = "sun6i-a31-apb0-clk",
		.of_match_table = sun6i_a31_apb0_clk_dt_ids,
	},
	.probe = sun6i_a31_apb0_clk_probe,
};
builtin_platform_driver(sun6i_a31_apb0_clk_driver);
