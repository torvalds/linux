/*
 * UniPhier eFuse driver
 *
 * Copyright (C) 2017 Socionext Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/device.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/nvmem-provider.h>
#include <linux/platform_device.h>

struct uniphier_efuse_priv {
	void __iomem *base;
};

static int uniphier_reg_read(void *context,
			     unsigned int reg, void *_val, size_t bytes)
{
	struct uniphier_efuse_priv *priv = context;
	u8 *val = _val;
	int offs;

	for (offs = 0; offs < bytes; offs += sizeof(u8))
		*val++ = readb(priv->base + reg + offs);

	return 0;
}

static int uniphier_efuse_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct resource *res;
	struct nvmem_device *nvmem;
	struct nvmem_config econfig = {};
	struct uniphier_efuse_priv *priv;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	priv->base = devm_ioremap_resource(dev, res);
	if (IS_ERR(priv->base))
		return PTR_ERR(priv->base);

	econfig.stride = 1;
	econfig.word_size = 1;
	econfig.read_only = true;
	econfig.reg_read = uniphier_reg_read;
	econfig.size = resource_size(res);
	econfig.priv = priv;
	econfig.dev = dev;
	nvmem = nvmem_register(&econfig);
	if (IS_ERR(nvmem))
		return PTR_ERR(nvmem);

	platform_set_drvdata(pdev, nvmem);

	return 0;
}

static int uniphier_efuse_remove(struct platform_device *pdev)
{
	struct nvmem_device *nvmem = platform_get_drvdata(pdev);

	return nvmem_unregister(nvmem);
}

static const struct of_device_id uniphier_efuse_of_match[] = {
	{ .compatible = "socionext,uniphier-efuse",},
	{/* sentinel */},
};
MODULE_DEVICE_TABLE(of, uniphier_efuse_of_match);

static struct platform_driver uniphier_efuse_driver = {
	.probe = uniphier_efuse_probe,
	.remove = uniphier_efuse_remove,
	.driver = {
		.name = "uniphier-efuse",
		.of_match_table = uniphier_efuse_of_match,
	},
};
module_platform_driver(uniphier_efuse_driver);

MODULE_AUTHOR("Keiji Hayashibara <hayashibara.keiji@socionext.com>");
MODULE_DESCRIPTION("UniPhier eFuse driver");
MODULE_LICENSE("GPL v2");
