// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright 2022 NXP
 */

#include <linux/module.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>

static int imx93_src_probe(struct platform_device *pdev)
{
	return devm_of_platform_populate(&pdev->dev);
}

static const struct of_device_id imx93_src_ids[] = {
	{ .compatible = "fsl,imx93-src" },
	{ }
};
MODULE_DEVICE_TABLE(of, imx93_src_ids);

static struct platform_driver imx93_src_driver = {
	.driver = {
		.name	= "imx93_src",
		.owner	= THIS_MODULE,
		.of_match_table = imx93_src_ids,
	},
	.probe = imx93_src_probe,
};
module_platform_driver(imx93_src_driver);

MODULE_AUTHOR("Peng Fan <peng.fan@nxp.com>");
MODULE_DESCRIPTION("NXP i.MX93 src driver");
MODULE_LICENSE("GPL");
