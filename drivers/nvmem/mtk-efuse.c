// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2015 MediaTek Inc.
 * Author: Andrew-CT Chen <andrew-ct.chen@mediatek.com>
 */

#include <linux/device.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/io.h>
#include <linux/nvmem-provider.h>
#include <linux/platform_device.h>

struct mtk_efuse_priv {
	void __iomem *base;
};

static int mtk_reg_read(void *context,
			unsigned int reg, void *_val, size_t bytes)
{
	struct mtk_efuse_priv *priv = context;
	u32 *val = _val;
	int i = 0, words = bytes / 4;

	while (words--)
		*val++ = readl(priv->base + reg + (i++ * 4));

	return 0;
}

static int mtk_efuse_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct resource *res;
	struct nvmem_device *nvmem;
	struct nvmem_config econfig = {};
	struct mtk_efuse_priv *priv;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	priv->base = devm_ioremap_resource(dev, res);
	if (IS_ERR(priv->base))
		return PTR_ERR(priv->base);

	econfig.stride = 4;
	econfig.word_size = 4;
	econfig.reg_read = mtk_reg_read;
	econfig.size = resource_size(res);
	econfig.priv = priv;
	econfig.dev = dev;
	nvmem = devm_nvmem_register(dev, &econfig);

	return PTR_ERR_OR_ZERO(nvmem);
}

static const struct of_device_id mtk_efuse_of_match[] = {
	{ .compatible = "mediatek,mt8173-efuse",},
	{ .compatible = "mediatek,efuse",},
	{/* sentinel */},
};
MODULE_DEVICE_TABLE(of, mtk_efuse_of_match);

static struct platform_driver mtk_efuse_driver = {
	.probe = mtk_efuse_probe,
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
