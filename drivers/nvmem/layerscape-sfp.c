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
#include <linux/regmap.h>

#define LAYERSCAPE_SFP_OTP_OFFSET	0x0200

struct layerscape_sfp_priv {
	struct regmap *regmap;
};

struct layerscape_sfp_data {
	int size;
	enum regmap_endian endian;
};

static int layerscape_sfp_read(void *context, unsigned int offset, void *val,
			       size_t bytes)
{
	struct layerscape_sfp_priv *priv = context;

	return regmap_bulk_read(priv->regmap,
				LAYERSCAPE_SFP_OTP_OFFSET + offset, val,
				bytes / 4);
}

static struct nvmem_config layerscape_sfp_nvmem_config = {
	.name = "fsl-sfp",
	.reg_read = layerscape_sfp_read,
	.word_size = 4,
	.stride = 4,
};

static int layerscape_sfp_probe(struct platform_device *pdev)
{
	const struct layerscape_sfp_data *data;
	struct layerscape_sfp_priv *priv;
	struct nvmem_device *nvmem;
	struct regmap_config config = { 0 };
	void __iomem *base;

	priv = devm_kzalloc(&pdev->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(base))
		return PTR_ERR(base);

	data = device_get_match_data(&pdev->dev);
	config.reg_bits = 32;
	config.reg_stride = 4;
	config.val_bits = 32;
	config.val_format_endian = data->endian;
	config.max_register = LAYERSCAPE_SFP_OTP_OFFSET + data->size - 4;
	priv->regmap = devm_regmap_init_mmio(&pdev->dev, base, &config);
	if (IS_ERR(priv->regmap))
		return PTR_ERR(priv->regmap);

	layerscape_sfp_nvmem_config.size = data->size;
	layerscape_sfp_nvmem_config.dev = &pdev->dev;
	layerscape_sfp_nvmem_config.priv = priv;

	nvmem = devm_nvmem_register(&pdev->dev, &layerscape_sfp_nvmem_config);

	return PTR_ERR_OR_ZERO(nvmem);
}

static const struct layerscape_sfp_data ls1021a_data = {
	.size = 0x88,
	.endian = REGMAP_ENDIAN_BIG,
};

static const struct layerscape_sfp_data ls1028a_data = {
	.size = 0x88,
	.endian = REGMAP_ENDIAN_LITTLE,
};

static const struct of_device_id layerscape_sfp_dt_ids[] = {
	{ .compatible = "fsl,ls1021a-sfp", .data = &ls1021a_data },
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
