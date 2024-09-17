// SPDX-License-Identifier: GPL-2.0-only
/*
 * Layerscape FlexSPI clock driver
 *
 * Copyright 2020 Michael Walle <michael@walle.cc>
 */

#include <linux/clk-provider.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/platform_device.h>

static const struct clk_div_table ls1028a_flexspi_divs[] = {
	{ .val = 0, .div = 1, },
	{ .val = 1, .div = 2, },
	{ .val = 2, .div = 3, },
	{ .val = 3, .div = 4, },
	{ .val = 4, .div = 5, },
	{ .val = 5, .div = 6, },
	{ .val = 6, .div = 7, },
	{ .val = 7, .div = 8, },
	{ .val = 11, .div = 12, },
	{ .val = 15, .div = 16, },
	{ .val = 16, .div = 20, },
	{ .val = 17, .div = 24, },
	{ .val = 18, .div = 28, },
	{ .val = 19, .div = 32, },
	{ .val = 20, .div = 80, },
	{}
};

static const struct clk_div_table lx2160a_flexspi_divs[] = {
	{ .val = 1, .div = 2, },
	{ .val = 3, .div = 4, },
	{ .val = 5, .div = 6, },
	{ .val = 7, .div = 8, },
	{ .val = 11, .div = 12, },
	{ .val = 15, .div = 16, },
	{ .val = 16, .div = 20, },
	{ .val = 17, .div = 24, },
	{ .val = 18, .div = 28, },
	{ .val = 19, .div = 32, },
	{ .val = 20, .div = 80, },
	{}
};

static int fsl_flexspi_clk_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	const char *clk_name = np->name;
	const char *clk_parent;
	struct resource *res;
	void __iomem *reg;
	struct clk_hw *hw;
	const struct clk_div_table *divs;

	divs = device_get_match_data(dev);
	if (!divs)
		return -ENOENT;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res)
		return -ENOENT;

	/*
	 * Can't use devm_ioremap_resource() or devm_of_iomap() because the
	 * resource might already be taken by the parent device.
	 */
	reg = devm_ioremap(dev, res->start, resource_size(res));
	if (!reg)
		return -ENOMEM;

	clk_parent = of_clk_get_parent_name(np, 0);
	if (!clk_parent)
		return -EINVAL;

	of_property_read_string(np, "clock-output-names", &clk_name);

	hw = devm_clk_hw_register_divider_table(dev, clk_name, clk_parent, 0,
						reg, 0, 5, 0, divs, NULL);
	if (IS_ERR(hw))
		return PTR_ERR(hw);

	return devm_of_clk_add_hw_provider(dev, of_clk_hw_simple_get, hw);
}

static const struct of_device_id fsl_flexspi_clk_dt_ids[] = {
	{ .compatible = "fsl,ls1028a-flexspi-clk", .data = &ls1028a_flexspi_divs },
	{ .compatible = "fsl,lx2160a-flexspi-clk", .data = &lx2160a_flexspi_divs },
	{}
};
MODULE_DEVICE_TABLE(of, fsl_flexspi_clk_dt_ids);

static struct platform_driver fsl_flexspi_clk_driver = {
	.driver = {
		.name = "fsl-flexspi-clk",
		.of_match_table = fsl_flexspi_clk_dt_ids,
	},
	.probe = fsl_flexspi_clk_probe,
};
module_platform_driver(fsl_flexspi_clk_driver);

MODULE_DESCRIPTION("FlexSPI clock driver for Layerscape SoCs");
MODULE_AUTHOR("Michael Walle <michael@walle.cc>");
MODULE_LICENSE("GPL");
