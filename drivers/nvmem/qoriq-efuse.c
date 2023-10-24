// SPDX-License-Identifier: GPL-2.0
/*
 *  Copyright (C) 2023  Westermo Network Technologies AB
 */

#include <linux/device.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/nvmem-provider.h>
#include <linux/platform_device.h>

struct qoriq_efuse_priv {
	void __iomem *base;
};

static int qoriq_efuse_read(void *context, unsigned int offset, void *val,
			    size_t bytes)
{
	struct qoriq_efuse_priv *priv = context;

	/* .stride = 4 so offset is guaranteed to be aligned */
	__ioread32_copy(val, priv->base + offset, bytes / 4);

	/* Ignore trailing bytes (there shouldn't be any) */

	return 0;
}

static int qoriq_efuse_probe(struct platform_device *pdev)
{
	struct nvmem_config config = {
		.dev = &pdev->dev,
		.read_only = true,
		.reg_read = qoriq_efuse_read,
		.stride = sizeof(u32),
		.word_size = sizeof(u32),
		.name = "qoriq_efuse_read",
		.id = NVMEM_DEVID_AUTO,
		.root_only = true,
	};
	struct qoriq_efuse_priv *priv;
	struct nvmem_device *nvmem;
	struct resource *res;

	priv = devm_kzalloc(config.dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->base = devm_platform_get_and_ioremap_resource(pdev, 0, &res);
	if (IS_ERR(priv->base))
		return PTR_ERR(priv->base);

	config.size = resource_size(res);
	config.priv = priv;
	nvmem = devm_nvmem_register(config.dev, &config);

	return PTR_ERR_OR_ZERO(nvmem);
}

static const struct of_device_id qoriq_efuse_of_match[] = {
	{ .compatible = "fsl,t1023-sfp", },
	{/* sentinel */},
};
MODULE_DEVICE_TABLE(of, qoriq_efuse_of_match);

static struct platform_driver qoriq_efuse_driver = {
	.probe = qoriq_efuse_probe,
	.driver = {
		.name = "qoriq-efuse",
		.of_match_table = qoriq_efuse_of_match,
	},
};
module_platform_driver(qoriq_efuse_driver);

MODULE_AUTHOR("Richard Alpe <richard.alpe@bit42.se>");
MODULE_DESCRIPTION("NXP QorIQ Security Fuse Processor (SFP) Reader");
MODULE_LICENSE("GPL");
