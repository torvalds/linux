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

struct usb_phy_ctrl {
	u32 offset;
	u32 set_bit;
};

static const struct of_device_id aspeed_usb_phy_dt_ids[] = {
	{
		.compatible = "aspeed,ast2600-uphyb",
	},
	{ }
};
MODULE_DEVICE_TABLE(of, aspeed_usb_phy_dt_ids);

static int aspeed_usb_phy_probe(struct platform_device *pdev)
{
	struct device_node *node = pdev->dev.of_node;
	struct usb_phy_ctrl *ctrl_data;
	void __iomem *base;
	int ret;

	ctrl_data = devm_kzalloc(&pdev->dev, sizeof(struct usb_phy_ctrl), GFP_KERNEL);
	if (!ctrl_data)
		return -ENOMEM;

	base = of_iomap(node, 0);

	ret = of_property_read_u32_array(node, "ctrl", (u32 *)ctrl_data, 2);
	if (ret < 0) {
		dev_err(&pdev->dev, "Could not read ctrl property\n");
		return -EINVAL;
	}

	writel(readl(base + ctrl_data->offset) | BIT(ctrl_data->set_bit),
		base + ctrl_data->offset);

	dev_info(&pdev->dev, "Initialized USB PHY\n");

	return 0;
}

static int aspeed_usb_phy_remove(struct platform_device *pdev)
{
	return 0;
}

static struct platform_driver aspeed_usb_phy_driver = {
	.probe		= aspeed_usb_phy_probe,
	.remove		= aspeed_usb_phy_remove,
	.driver		= {
		.name	= KBUILD_MODNAME,
		.of_match_table	= aspeed_usb_phy_dt_ids,
	},
};
module_platform_driver(aspeed_usb_phy_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Neal Liu <neal_liu@aspeedtech.com>");
