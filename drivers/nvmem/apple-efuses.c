// SPDX-License-Identifier: GPL-2.0-only
/*
 * Apple SoC eFuse driver
 *
 * Copyright (C) The Asahi Linux Contributors
 */

#include <linux/io.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/nvmem-provider.h>
#include <linux/platform_device.h>

struct apple_efuses_priv {
	void __iomem *fuses;
};

static int apple_efuses_read(void *context, unsigned int offset, void *val,
			     size_t bytes)
{
	struct apple_efuses_priv *priv = context;
	u32 *dst = val;

	while (bytes >= sizeof(u32)) {
		*dst++ = readl_relaxed(priv->fuses + offset);
		bytes -= sizeof(u32);
		offset += sizeof(u32);
	}

	return 0;
}

static int apple_efuses_probe(struct platform_device *pdev)
{
	struct apple_efuses_priv *priv;
	struct resource *res;
	struct nvmem_config config = {
		.dev = &pdev->dev,
		.add_legacy_fixed_of_cells = true,
		.read_only = true,
		.reg_read = apple_efuses_read,
		.stride = sizeof(u32),
		.word_size = sizeof(u32),
		.name = "apple_efuses_nvmem",
		.id = NVMEM_DEVID_AUTO,
		.root_only = true,
	};

	priv = devm_kzalloc(config.dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->fuses = devm_platform_get_and_ioremap_resource(pdev, 0, &res);
	if (IS_ERR(priv->fuses))
		return PTR_ERR(priv->fuses);

	config.priv = priv;
	config.size = resource_size(res);

	return PTR_ERR_OR_ZERO(devm_nvmem_register(config.dev, &config));
}

static const struct of_device_id apple_efuses_of_match[] = {
	{ .compatible = "apple,efuses", },
	{}
};

MODULE_DEVICE_TABLE(of, apple_efuses_of_match);

static struct platform_driver apple_efuses_driver = {
	.driver = {
		.name = "apple_efuses",
		.of_match_table = apple_efuses_of_match,
	},
	.probe = apple_efuses_probe,
};

module_platform_driver(apple_efuses_driver);

MODULE_AUTHOR("Sven Peter <sven@svenpeter.dev>");
MODULE_DESCRIPTION("Apple SoC eFuse driver");
MODULE_LICENSE("GPL");
