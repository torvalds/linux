/*
 * Copyright (C) 2014 Chen-Yu Tsai
 * Author: Chen-Yu Tsai <wens@csie.org>
 *
 * Allwinner A23 APB0 clock driver
 *
 * License Terms: GNU General Public License v2
 *
 * Based on clk-sun6i-apb0.c
 * Allwinner A31 APB0 clock driver
 *
 * Copyright (C) 2014 Free Electrons
 * Author: Boris BREZILLON <boris.brezillon@free-electrons.com>
 *
 */

#include <linux/clk-provider.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>

static int sun8i_a23_apb0_clk_probe(struct platform_device *pdev)
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

	/* The A23 APB0 clock is a standard 2 bit wide divider clock */
	clk = clk_register_divider(&pdev->dev, clk_name, clk_parent, 0, reg,
				   0, 2, CLK_DIVIDER_POWER_OF_TWO, NULL);
	if (IS_ERR(clk))
		return PTR_ERR(clk);

	return of_clk_add_provider(np, of_clk_src_simple_get, clk);
}

static const struct of_device_id sun8i_a23_apb0_clk_dt_ids[] = {
	{ .compatible = "allwinner,sun8i-a23-apb0-clk" },
	{ /* sentinel */ }
};

static struct platform_driver sun8i_a23_apb0_clk_driver = {
	.driver = {
		.name = "sun8i-a23-apb0-clk",
		.of_match_table = sun8i_a23_apb0_clk_dt_ids,
	},
	.probe = sun8i_a23_apb0_clk_probe,
};
module_platform_driver(sun8i_a23_apb0_clk_driver);

MODULE_AUTHOR("Chen-Yu Tsai <wens@csie.org>");
MODULE_DESCRIPTION("Allwinner A23 APB0 clock Driver");
MODULE_LICENSE("GPL v2");
