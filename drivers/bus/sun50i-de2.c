// SPDX-License-Identifier: GPL-2.0
/*
 * Allwinner A64 Display Engine 2.0 Bus Driver
 *
 * Copyright (C) 2018 Icenowy Zheng <icenowy@aosc.io>
 */

#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/soc/sunxi/sunxi_sram.h>

static int sun50i_de2_bus_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	int ret;

	ret = sunxi_sram_claim(&pdev->dev);
	if (ret)
		return dev_err_probe(&pdev->dev, ret,
				     "Couldn't map SRAM to device\n");

	of_platform_populate(np, NULL, NULL, &pdev->dev);

	return 0;
}

static int sun50i_de2_bus_remove(struct platform_device *pdev)
{
	sunxi_sram_release(&pdev->dev);
	return 0;
}

static const struct of_device_id sun50i_de2_bus_of_match[] = {
	{ .compatible = "allwinner,sun50i-a64-de2", },
	{ /* sentinel */ }
};

static struct platform_driver sun50i_de2_bus_driver = {
	.probe = sun50i_de2_bus_probe,
	.remove = sun50i_de2_bus_remove,
	.driver = {
		.name = "sun50i-de2-bus",
		.of_match_table = sun50i_de2_bus_of_match,
	},
};

builtin_platform_driver(sun50i_de2_bus_driver);
