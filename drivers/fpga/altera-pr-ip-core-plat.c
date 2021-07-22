// SPDX-License-Identifier: GPL-2.0
/*
 * Driver for Altera Partial Reconfiguration IP Core
 *
 * Copyright (C) 2016-2017 Intel Corporation
 *
 * Based on socfpga-a10.c Copyright (C) 2015-2016 Altera Corporation
 *  by Alan Tull <atull@opensource.altera.com>
 */
#include <linux/fpga/altera-pr-ip-core.h>
#include <linux/module.h>
#include <linux/of_device.h>

static int alt_pr_platform_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	void __iomem *reg_base;
	struct resource *res;

	/* First mmio base is for register access */
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);

	reg_base = devm_ioremap_resource(dev, res);

	if (IS_ERR(reg_base))
		return PTR_ERR(reg_base);

	return alt_pr_register(dev, reg_base);
}

static const struct of_device_id alt_pr_of_match[] = {
	{ .compatible = "altr,a10-pr-ip", },
	{},
};

MODULE_DEVICE_TABLE(of, alt_pr_of_match);

static struct platform_driver alt_pr_platform_driver = {
	.probe = alt_pr_platform_probe,
	.driver = {
		.name	= "alt_a10_pr_ip",
		.of_match_table = alt_pr_of_match,
	},
};

module_platform_driver(alt_pr_platform_driver);
MODULE_AUTHOR("Matthew Gerlach <matthew.gerlach@linux.intel.com>");
MODULE_DESCRIPTION("Altera Partial Reconfiguration IP Platform Driver");
MODULE_LICENSE("GPL v2");
