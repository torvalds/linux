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
#include <linux/mfd/syscon.h>
#include <asm/io.h>

struct usb_phy_ctrl {
	u32 offset;
	u32 value;
};

static const struct of_device_id aspeed_usb_phy_dt_ids[] = {
	{
		.compatible = "aspeed,ast2600-uphyb",
	},
	{
		.compatible = "aspeed,ast2700-phy2a1",
	},
	{
		.compatible = "aspeed,ast2700-phy2b1",
	},
	{ }
};
MODULE_DEVICE_TABLE(of, aspeed_usb_phy_dt_ids);

static int aspeed_usb_phy_probe(struct platform_device *pdev)
{
	struct device_node *node = pdev->dev.of_node;
	struct usb_phy_ctrl *ctrl_data;
	void __iomem *base;
	struct regmap *scu;
	int ctrl_num = 1;
	int ret, i;
	u32 val;

	scu = syscon_regmap_lookup_by_phandle(pdev->dev.of_node, "aspeed,scu");
	if (IS_ERR(scu)) {
		dev_err(&pdev->dev, "cannot to find SCU regmap\n");
		return -ENODEV;
	}

	if (of_device_is_compatible(pdev->dev.of_node,
				    "aspeed,ast2600-uphyb")) {
		/* Check SCU040[3] USB port B controller reset is deassert */
		regmap_read(scu, 0x40, &val);
		if ((val & BIT(3)))
			return -EPROBE_DEFER;
	}

	if (of_device_is_compatible(pdev->dev.of_node,
				    "aspeed,ast2700-phy2a1")) {
		/* Check SCU220[0] USB vHubA1 controller reset is deassert */
		regmap_read(scu, 0x220, &val);
		if ((val & BIT(0)))
			return -EPROBE_DEFER;
	}

	if (of_device_is_compatible(pdev->dev.of_node,
				    "aspeed,ast2700-phy2b1")) {
		/* Check SCU220[2] USB vHubB1 controller reset is deassert */
		regmap_read(scu, 0x220, &val);
		if ((val & BIT(2)))
			return -EPROBE_DEFER;
	}

	ctrl_data = devm_kzalloc(&pdev->dev,
				 sizeof(struct usb_phy_ctrl) * ctrl_num,
				 GFP_KERNEL);
	if (!ctrl_data)
		return -ENOMEM;

	base = of_iomap(node, 0);

	ret = of_property_read_u32_array(node, "ctrl", (u32 *)ctrl_data,
					 ctrl_num * 2);
	if (ret < 0) {
		dev_err(&pdev->dev, "Could not read ctrl property\n");
		return -EINVAL;
	}

	for (i = 0; i < ctrl_num; i++)
		writel(ctrl_data[i].value, base + ctrl_data[i].offset);

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
