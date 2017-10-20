/*
 * Copyright (c) 2015 MediaTek Inc.
 * Author: Andrew-CT Chen <andrew-ct.chen@mediatek.com>
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
#include <linux/module.h>
#include <linux/io.h>
#include <linux/nvmem-provider.h>
#include <linux/platform_device.h>

static int mtk_reg_read(void *context,
			unsigned int reg, void *_val, size_t bytes)
{
	void __iomem *base = context;
	u32 *val = _val;
	int i = 0, words = bytes / 4;

	while (words--)
		*val++ = readl(base + reg + (i++ * 4));

	return 0;
}

static int mtk_reg_write(void *context,
			 unsigned int reg, void *_val, size_t bytes)
{
	void __iomem *base = context;
	u32 *val = _val;
	int i = 0, words = bytes / 4;

	while (words--)
		writel(*val++, base + reg + (i++ * 4));

	return 0;
}

static int mtk_efuse_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct resource *res;
	struct nvmem_device *nvmem;
	struct nvmem_config econfig = {};
	void __iomem *base;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	base = devm_ioremap_resource(dev, res);
	if (IS_ERR(base))
		return PTR_ERR(base);

	econfig.stride = 4;
	econfig.word_size = 4;
	econfig.reg_read = mtk_reg_read;
	econfig.reg_write = mtk_reg_write;
	econfig.size = resource_size(res);
	econfig.priv = base;
	econfig.dev = dev;
	econfig.owner = THIS_MODULE;
	nvmem = nvmem_register(&econfig);
	if (IS_ERR(nvmem))
		return PTR_ERR(nvmem);

	platform_set_drvdata(pdev, nvmem);

	return 0;
}

static int mtk_efuse_remove(struct platform_device *pdev)
{
	struct nvmem_device *nvmem = platform_get_drvdata(pdev);

	return nvmem_unregister(nvmem);
}

static const struct of_device_id mtk_efuse_of_match[] = {
	{ .compatible = "mediatek,mt8173-efuse",},
	{ .compatible = "mediatek,efuse",},
	{/* sentinel */},
};
MODULE_DEVICE_TABLE(of, mtk_efuse_of_match);

static struct platform_driver mtk_efuse_driver = {
	.probe = mtk_efuse_probe,
	.remove = mtk_efuse_remove,
	.driver = {
		.name = "mediatek,efuse",
		.of_match_table = mtk_efuse_of_match,
	},
};

static int __init mtk_efuse_init(void)
{
	int ret;

	ret = platform_driver_register(&mtk_efuse_driver);
	if (ret) {
		pr_err("Failed to register efuse driver\n");
		return ret;
	}

	return 0;
}

static void __exit mtk_efuse_exit(void)
{
	return platform_driver_unregister(&mtk_efuse_driver);
}

subsys_initcall(mtk_efuse_init);
module_exit(mtk_efuse_exit);

MODULE_AUTHOR("Andrew-CT Chen <andrew-ct.chen@mediatek.com>");
MODULE_DESCRIPTION("Mediatek EFUSE driver");
MODULE_LICENSE("GPL v2");
