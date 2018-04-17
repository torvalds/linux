/*
 * Copyright (c) 2015 Pengutronix, Steffen Trumtrar <kernel@pengutronix.de>
 * Copyright (c) 2017 Pengutronix, Oleksij Rempel <kernel@pengutronix.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation.
 */

#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/nvmem-provider.h>
#include <linux/of_device.h>
#include <linux/regmap.h>

#define IMX6Q_SNVS_HPLR		0x00
#define IMX6Q_GPR_SL		BIT(5)
#define IMX6Q_SNVS_LPLR		0x34
#define IMX6Q_GPR_HL		BIT(5)
#define IMX6Q_SNVS_LPGPR	0x68

struct snvs_lpgpr_cfg {
	int offset;
	int offset_hplr;
	int offset_lplr;
};

struct snvs_lpgpr_priv {
	struct device_d			*dev;
	struct regmap			*regmap;
	struct nvmem_config		cfg;
	const struct snvs_lpgpr_cfg	*dcfg;
};

static const struct snvs_lpgpr_cfg snvs_lpgpr_cfg_imx6q = {
	.offset		= IMX6Q_SNVS_LPGPR,
	.offset_hplr	= IMX6Q_SNVS_HPLR,
	.offset_lplr	= IMX6Q_SNVS_LPLR,
};

static int snvs_lpgpr_write(void *context, unsigned int offset, void *val,
			    size_t bytes)
{
	struct snvs_lpgpr_priv *priv = context;
	const struct snvs_lpgpr_cfg *dcfg = priv->dcfg;
	unsigned int lock_reg;
	int ret;

	ret = regmap_read(priv->regmap, dcfg->offset_hplr, &lock_reg);
	if (ret < 0)
		return ret;

	if (lock_reg & IMX6Q_GPR_SL)
		return -EPERM;

	ret = regmap_read(priv->regmap, dcfg->offset_lplr, &lock_reg);
	if (ret < 0)
		return ret;

	if (lock_reg & IMX6Q_GPR_HL)
		return -EPERM;

	return regmap_bulk_write(priv->regmap, dcfg->offset + offset, val,
				bytes / 4);
}

static int snvs_lpgpr_read(void *context, unsigned int offset, void *val,
			   size_t bytes)
{
	struct snvs_lpgpr_priv *priv = context;
	const struct snvs_lpgpr_cfg *dcfg = priv->dcfg;

	return regmap_bulk_read(priv->regmap, dcfg->offset + offset,
			       val, bytes / 4);
}

static int snvs_lpgpr_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *node = dev->of_node;
	struct device_node *syscon_node;
	struct snvs_lpgpr_priv *priv;
	struct nvmem_config *cfg;
	struct nvmem_device *nvmem;
	const struct snvs_lpgpr_cfg *dcfg;

	if (!node)
		return -ENOENT;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	dcfg = of_device_get_match_data(dev);
	if (!dcfg)
		return -EINVAL;

	syscon_node = of_get_parent(node);
	if (!syscon_node)
		return -ENODEV;

	priv->regmap = syscon_node_to_regmap(syscon_node);
	of_node_put(syscon_node);
	if (IS_ERR(priv->regmap))
		return PTR_ERR(priv->regmap);

	priv->dcfg = dcfg;

	cfg = &priv->cfg;
	cfg->priv = priv;
	cfg->name = dev_name(dev);
	cfg->dev = dev;
	cfg->stride = 4,
	cfg->word_size = 4,
	cfg->size = 4,
	cfg->owner = THIS_MODULE,
	cfg->reg_read  = snvs_lpgpr_read,
	cfg->reg_write = snvs_lpgpr_write,

	nvmem = nvmem_register(cfg);
	if (IS_ERR(nvmem))
		return PTR_ERR(nvmem);

	platform_set_drvdata(pdev, nvmem);

	return 0;
}

static int snvs_lpgpr_remove(struct platform_device *pdev)
{
	struct nvmem_device *nvmem = platform_get_drvdata(pdev);

	return nvmem_unregister(nvmem);
}

static const struct of_device_id snvs_lpgpr_dt_ids[] = {
	{ .compatible = "fsl,imx6q-snvs-lpgpr", .data = &snvs_lpgpr_cfg_imx6q },
	{ .compatible = "fsl,imx6ul-snvs-lpgpr",
	  .data = &snvs_lpgpr_cfg_imx6q },
	{ },
};
MODULE_DEVICE_TABLE(of, snvs_lpgpr_dt_ids);

static struct platform_driver snvs_lpgpr_driver = {
	.probe	= snvs_lpgpr_probe,
	.remove	= snvs_lpgpr_remove,
	.driver = {
		.name	= "snvs_lpgpr",
		.of_match_table = snvs_lpgpr_dt_ids,
	},
};
module_platform_driver(snvs_lpgpr_driver);

MODULE_AUTHOR("Oleksij Rempel <o.rempel@pengutronix.de>");
MODULE_DESCRIPTION("Low Power General Purpose Register in i.MX6 Secure Non-Volatile Storage");
MODULE_LICENSE("GPL v2");
