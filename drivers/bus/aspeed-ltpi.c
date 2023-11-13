// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright ASPEED Technology

#include <linux/module.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/clk.h>

static int aspeed_ltpi_probe(struct platform_device *pdev)
{
	const struct device *dev = &pdev->dev;
	const struct of_dev_auxdata *lookup = dev_get_platdata(dev);
	struct device_node *np = dev->of_node;
	const struct of_device_id *match;
	struct clk *ltpi_clk;

	match = of_match_device(dev->driver->of_match_table, dev);
	if (match && match->data) {
		if (of_property_match_string(np, "compatible", match->compatible) == 0)
			return 0;
		else
			return -ENODEV;
	}

	ltpi_clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(ltpi_clk))
		return PTR_ERR(ltpi_clk);

	clk_prepare_enable(ltpi_clk);

	if (np)
		of_platform_populate(np, NULL, lookup, &pdev->dev);

	return 0;
}

static int aspeed_ltpi_remove(struct platform_device *pdev)
{
	struct clk *ltpi_clk;

	ltpi_clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(ltpi_clk))
		return PTR_ERR(ltpi_clk);

	clk_disable_unprepare(ltpi_clk);

	return 0;
}

static const struct of_device_id aspeed_ltpi_of_match[] = {
	{ .compatible = "aspeed-ltpi", },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, aspeed_ltpi_of_match);

static struct platform_driver aspeed_ltpi_driver = {
	.probe = aspeed_ltpi_probe,
	.remove = aspeed_ltpi_remove,
	.driver = {
		.name = "aspeed-ltpi",
		.of_match_table = aspeed_ltpi_of_match,
	},
};

module_platform_driver(aspeed_ltpi_driver);

MODULE_DESCRIPTION("LVDS Tunneling Protocol and Interface Bus Driver");
MODULE_AUTHOR("Dylan Hung <dylan_hung@aspeedtech.com>");
MODULE_LICENSE("GPL");
