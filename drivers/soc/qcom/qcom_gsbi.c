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

struct gsbi_info {
	struct clk *hclk;
	u32 mode;
	u32 crci;
};

static int gsbi_probe(struct platform_device *pdev)
{
	struct device_node *node = pdev->dev.of_node;
	struct resource *res;
	void __iomem *base;
	struct gsbi_info *gsbi;

	gsbi = devm_kzalloc(&pdev->dev, sizeof(*gsbi), GFP_KERNEL);

	if (!gsbi)
		return -ENOMEM;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(base))
		return PTR_ERR(base);

	if (of_property_read_u32(node, "qcom,mode", &gsbi->mode)) {
		dev_err(&pdev->dev, "missing mode configuration\n");
		return -EINVAL;
	}

	/* not required, so default to 0 if not present */
	of_property_read_u32(node, "qcom,crci", &gsbi->crci);

	dev_info(&pdev->dev, "GSBI port protocol: %d crci: %d\n",
		 gsbi->mode, gsbi->crci);
	gsbi->hclk = devm_clk_get(&pdev->dev, "iface");
	if (IS_ERR(gsbi->hclk))
		return PTR_ERR(gsbi->hclk);

	clk_prepare_enable(gsbi->hclk);

	writel_relaxed((gsbi->mode << GSBI_PROTOCOL_SHIFT) | gsbi->crci,
				base + GSBI_CTRL_REG);

	/* make sure the gsbi control write is not reordered */
	wmb();

	platform_set_drvdata(pdev, gsbi);

	return of_platform_populate(node, NULL, NULL, &pdev->dev);
}

static int gsbi_remove(struct platform_device *pdev)
{
	struct gsbi_info *gsbi = platform_get_drvdata(pdev);

	clk_disable_unprepare(gsbi->hclk);

	return 0;
}

static const struct of_device_id gsbi_dt_match[] = {
	{ .compatible = "qcom,gsbi-v1.0.0", },
	{ },
};

MODULE_DEVICE_TABLE(of, gsbi_dt_match);

static struct platform_driver gsbi_driver = {
	.driver = {
		.name		= "gsbi",
		.of_match_table	= gsbi_dt_match,
	},
	.probe = gsbi_probe,
	.remove	= gsbi_remove,
};

module_platform_driver(gsbi_driver);

MODULE_AUTHOR("Andy Gross <agross@codeaurora.org>");
MODULE_DESCRIPTION("QCOM GSBI driver");
MODULE_LICENSE("GPL v2");
