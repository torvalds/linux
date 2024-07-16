// SPDX-License-Identifier: GPL-2.0-only
/*
 * sfr.c - driver for special function registers
 *
 * Copyright (C) 2019 Bootlin.
 *
 */
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/nvmem-provider.h>
#include <linux/random.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>

#define SFR_SN0		0x4c
#define SFR_SN_SIZE	8

struct atmel_sfr_priv {
	struct regmap			*regmap;
};

static int atmel_sfr_read(void *context, unsigned int offset,
			  void *buf, size_t bytes)
{
	struct atmel_sfr_priv *priv = context;

	return regmap_bulk_read(priv->regmap, SFR_SN0 + offset,
				buf, bytes / 4);
}

static struct nvmem_config atmel_sfr_nvmem_config = {
	.name = "atmel-sfr",
	.read_only = true,
	.word_size = 4,
	.stride = 4,
	.size = SFR_SN_SIZE,
	.reg_read = atmel_sfr_read,
};

static int atmel_sfr_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	struct nvmem_device *nvmem;
	struct atmel_sfr_priv *priv;
	u8 sn[SFR_SN_SIZE];
	int ret;

	priv = devm_kmalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->regmap = syscon_node_to_regmap(np);
	if (IS_ERR(priv->regmap)) {
		dev_err(dev, "cannot get parent's regmap\n");
		return PTR_ERR(priv->regmap);
	}

	atmel_sfr_nvmem_config.dev = dev;
	atmel_sfr_nvmem_config.priv = priv;

	nvmem = devm_nvmem_register(dev, &atmel_sfr_nvmem_config);
	if (IS_ERR(nvmem)) {
		dev_err(dev, "error registering nvmem config\n");
		return PTR_ERR(nvmem);
	}

	ret = atmel_sfr_read(priv, 0, sn, SFR_SN_SIZE);
	if (ret == 0)
		add_device_randomness(sn, SFR_SN_SIZE);

	return ret;
}

static const struct of_device_id atmel_sfr_dt_ids[] = {
	{
		.compatible = "atmel,sama5d2-sfr",
	}, {
		.compatible = "atmel,sama5d4-sfr",
	}, {
		/* sentinel */
	},
};
MODULE_DEVICE_TABLE(of, atmel_sfr_dt_ids);

static struct platform_driver atmel_sfr_driver = {
	.probe = atmel_sfr_probe,
	.driver = {
		.name = "atmel-sfr",
		.of_match_table = atmel_sfr_dt_ids,
	},
};
module_platform_driver(atmel_sfr_driver);

MODULE_AUTHOR("Kamel Bouhara <kamel.bouhara@bootlin.com>");
MODULE_DESCRIPTION("Atmel SFR SN driver for SAMA5D2/4 SoC family");
MODULE_LICENSE("GPL v2");
