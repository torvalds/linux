/*
 * Copyright (c) 2015, National Instruments Corp.
 *
 * Xilinx Zynq Reset controller driver
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */

#include <linux/err.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/mfd/syscon.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/reset-controller.h>
#include <linux/regmap.h>
#include <linux/types.h>

struct zynq_reset_data {
	struct regmap *slcr;
	struct reset_controller_dev rcdev;
	u32 offset;
};

#define to_zynq_reset_data(p)		\
	container_of((p), struct zynq_reset_data, rcdev)

static int zynq_reset_assert(struct reset_controller_dev *rcdev,
			     unsigned long id)
{
	struct zynq_reset_data *priv = to_zynq_reset_data(rcdev);

	int bank = id / BITS_PER_LONG;
	int offset = id % BITS_PER_LONG;

	pr_debug("%s: %s reset bank %u offset %u\n", KBUILD_MODNAME, __func__,
		 bank, offset);

	return regmap_update_bits(priv->slcr,
				  priv->offset + (bank * 4),
				  BIT(offset),
				  BIT(offset));
}

static int zynq_reset_deassert(struct reset_controller_dev *rcdev,
			       unsigned long id)
{
	struct zynq_reset_data *priv = to_zynq_reset_data(rcdev);

	int bank = id / BITS_PER_LONG;
	int offset = id % BITS_PER_LONG;

	pr_debug("%s: %s reset bank %u offset %u\n", KBUILD_MODNAME, __func__,
		 bank, offset);

	return regmap_update_bits(priv->slcr,
				  priv->offset + (bank * 4),
				  BIT(offset),
				  ~BIT(offset));
}

static int zynq_reset_status(struct reset_controller_dev *rcdev,
			     unsigned long id)
{
	struct zynq_reset_data *priv = to_zynq_reset_data(rcdev);

	int bank = id / BITS_PER_LONG;
	int offset = id % BITS_PER_LONG;
	int ret;
	u32 reg;

	pr_debug("%s: %s reset bank %u offset %u\n", KBUILD_MODNAME, __func__,
		 bank, offset);

	ret = regmap_read(priv->slcr, priv->offset + (bank * 4), &reg);
	if (ret)
		return ret;

	return !!(reg & BIT(offset));
}

static struct reset_control_ops zynq_reset_ops = {
	.assert		= zynq_reset_assert,
	.deassert	= zynq_reset_deassert,
	.status		= zynq_reset_status,
};

static int zynq_reset_probe(struct platform_device *pdev)
{
	struct resource *res;
	struct zynq_reset_data *priv;

	priv = devm_kzalloc(&pdev->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;
	platform_set_drvdata(pdev, priv);

	priv->slcr = syscon_regmap_lookup_by_phandle(pdev->dev.of_node,
						     "syscon");
	if (IS_ERR(priv->slcr)) {
		dev_err(&pdev->dev, "unable to get zynq-slcr regmap");
		return PTR_ERR(priv->slcr);
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(&pdev->dev, "missing IO resource\n");
		return -ENODEV;
	}

	priv->offset = res->start;

	priv->rcdev.owner = THIS_MODULE;
	priv->rcdev.nr_resets = resource_size(res) / 4 * BITS_PER_LONG;
	priv->rcdev.ops = &zynq_reset_ops;
	priv->rcdev.of_node = pdev->dev.of_node;

	return reset_controller_register(&priv->rcdev);
}

static int zynq_reset_remove(struct platform_device *pdev)
{
	struct zynq_reset_data *priv = platform_get_drvdata(pdev);

	reset_controller_unregister(&priv->rcdev);

	return 0;
}

static const struct of_device_id zynq_reset_dt_ids[] = {
	{ .compatible = "xlnx,zynq-reset", },
	{ /* sentinel */ },
};

static struct platform_driver zynq_reset_driver = {
	.probe	= zynq_reset_probe,
	.remove	= zynq_reset_remove,
	.driver = {
		.name		= KBUILD_MODNAME,
		.of_match_table	= zynq_reset_dt_ids,
	},
};
module_platform_driver(zynq_reset_driver);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Moritz Fischer <moritz.fischer@ettus.com>");
MODULE_DESCRIPTION("Zynq Reset Controller Driver");
