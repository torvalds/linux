/*
 * Copyright (c) 2014, The Linux foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License rev 2 and
 * only rev 2 as published by the free Software foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or fITNESS fOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/clk.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>

#define GSBI_CTRL_REG		0x0000
#define GSBI_PROTOCOL_SHIFT	4

static int gsbi_probe(struct platform_device *pdev)
{
	struct device_node *node = pdev->dev.of_node;
	struct resource *res;
	void __iomem *base;
	struct clk *hclk;
	u32 mode, crci = 0;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(base))
		return PTR_ERR(base);

	if (of_property_read_u32(node, "qcom,mode", &mode)) {
		dev_err(&pdev->dev, "missing mode configuration\n");
		return -EINVAL;
	}

	/* not required, so default to 0 if not present */
	of_property_read_u32(node, "qcom,crci", &crci);

	dev_info(&pdev->dev, "GSBI port protocol: %d crci: %d\n", mode, crci);

	hclk = devm_clk_get(&pdev->dev, "iface");
	if (IS_ERR(hclk))
		return PTR_ERR(hclk);

	clk_prepare_enable(hclk);

	writel_relaxed((mode << GSBI_PROTOCOL_SHIFT) | crci,
				base + GSBI_CTRL_REG);

	/* make sure the gsbi control write is not reordered */
	wmb();

	clk_disable_unprepare(hclk);

	return of_platform_populate(pdev->dev.of_node, NULL, NULL, &pdev->dev);
}

static const struct of_device_id gsbi_dt_match[] = {
	{ .compatible = "qcom,gsbi-v1.0.0", },
	{ },
};

MODULE_DEVICE_TABLE(of, gsbi_dt_match);

static struct platform_driver gsbi_driver = {
	.driver = {
		.name		= "gsbi",
		.owner		= THIS_MODULE,
		.of_match_table	= gsbi_dt_match,
	},
	.probe = gsbi_probe,
};

module_platform_driver(gsbi_driver);

MODULE_AUTHOR("Andy Gross <agross@codeaurora.org>");
MODULE_DESCRIPTION("QCOM GSBI driver");
MODULE_LICENSE("GPL v2");
