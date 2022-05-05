// SPDX-License-Identifier: GPL-2.0-only
/*
 * Layerscape SFP driver
 *
 * Copyright (c) 2022 Michael Walle <michael@walle.cc>
 *
 */

#include <linux/device.h>
#include <linux/io.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/nvmem-provider.h>
#include <linux/platform_device.h>
#include <linux/property.h>

#define LAYERSCAPE_SFP_OTP_OFFSET	0x0200

struct layerscape_sfp_priv {
	void __iomem *base;
};

struct layerscape_sfp_data {
	int size;
};

static int layerscape_sfp_read(void *context, unsigned int offset, void *val,
			       size_t bytes)
{
	struct layerscape_sfp_priv *priv = context;

	memcpy_fromio(val, priv->base + LAYERSCAPE_SFP_OTP_OFFSET + offset,
		      bytes);

	return 0;
}

static struct nvmem_config layerscape_sfp_nvmem_config = {
	.name = "fsl-sfp",
	.reg_read = layerscape_sfp_read,
};

static int layerscape_sfp_probe(struct platform_device *pdev)
{
	const struct layerscape_sfp_data *data;
	struct layerscape_sfp_priv *priv;
	struct nvmem_device *nvmem;

	priv = devm_kzalloc(&pdev->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(priv->base))
		return PTR_ERR(priv->base);

	data = device_get_match_data(&pdev->dev);

	layerscape_sfp_nvmem_config.size = data->size;
	layerscape_sfp_nvmem_config.dev = &pdev->dev;
	layerscape_sfp_nvmem_config.priv = priv;

	nvmem = devm_nvmem_register(&pdev->dev, &layerscape_sfp_nvmem_config);

	return PTR_ERR_OR_ZERO(nvmem);
}

static const struct layerscape_sfp_data ls1028a_data = {
	.size = 0x88,
};

static const struct of_device_id layerscape_sfp_dt_ids[] = {
	{ .compatible = "fsl,ls1028a-sfp", .data = &ls1028a_data },
	{},
};
MODULE_DEVICE_TABLE(of, layerscape_sfp_dt_ids);

static struct platform_driver layerscape_sfp_driver = {
	.probe	= layerscape_sfp_probe,
	.driver = {
		.name	= "layerscape_sfp",
		.of_match_table = layerscape_sfp_dt_ids,
	},
};
module_platform_driver(layerscape_sfp_driver);

MODULE_AUTHOR("Michael Walle <michael@walle.cc>");
MODULE_DESCRIPTION("Layerscape Security Fuse Processor driver");
MODULE_LICENSE("GPL");
