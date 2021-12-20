// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright 2021 Aspeed Technology Inc.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/regmap.h>
#include <asm/io.h>

static const struct of_device_id aspeed_usb_ahp_dt_ids[] = {
	{
		.compatible = "aspeed,ast2600-usb2ahp",
	},
	{ }
};
MODULE_DEVICE_TABLE(of, aspeed_usb_ahp_dt_ids);

static int aspeed_usb_ahp_probe(struct platform_device *pdev)
{
	dev_info(&pdev->dev, "Initialized USB2AHP\n");

	return 0;
}

static int aspeed_usb_ahp_remove(struct platform_device *pdev)
{
	dev_info(&pdev->dev, "Remove USB2AHP\n");

	return 0;
}

static struct platform_driver aspeed_usb_ahp_driver = {
	.probe		= aspeed_usb_ahp_probe,
	.remove		= aspeed_usb_ahp_remove,
	.driver		= {
		.name	= KBUILD_MODNAME,
		.of_match_table	= aspeed_usb_ahp_dt_ids,
	},
};
module_platform_driver(aspeed_usb_ahp_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Neal Liu <neal_liu@aspeedtech.com>");
