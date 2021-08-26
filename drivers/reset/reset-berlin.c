/*
 * Copyright (C) 2014 Marvell Technology Group Ltd.
 *
 * Marvell Berlin reset driver
 *
 * Antoine Tenart <antoine.tenart@free-electrons.com>
 * Sebastian Hesselbarth <sebastian.hesselbarth@gmail.com>
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <linux/delay.h>
#include <linux/io.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/reset-controller.h>
#include <linux/slab.h>
#include <linux/types.h>

#define BERLIN_MAX_RESETS	32

#define to_berlin_reset_priv(p)		\
	container_of((p), struct berlin_reset_priv, rcdev)

struct berlin_reset_priv {
	struct regmap			*regmap;
	struct reset_controller_dev	rcdev;
};

static int berlin_reset_reset(struct reset_controller_dev *rcdev,
			      unsigned long id)
{
	struct berlin_reset_priv *priv = to_berlin_reset_priv(rcdev);
	int offset = id >> 8;
	int mask = BIT(id & 0x1f);

	regmap_write(priv->regmap, offset, mask);

	/* let the reset be effective */
	udelay(10);

	return 0;
}

static const struct reset_control_ops berlin_reset_ops = {
	.reset	= berlin_reset_reset,
};

static int berlin_reset_xlate(struct reset_controller_dev *rcdev,
			      const struct of_phandle_args *reset_spec)
{
	unsigned int offset, bit;

	offset = reset_spec->args[0];
	bit = reset_spec->args[1];

	if (bit >= BERLIN_MAX_RESETS)
		return -EINVAL;

	return (offset << 8) | bit;
}

static int berlin2_reset_probe(struct platform_device *pdev)
{
	struct device_node *parent_np = of_get_parent(pdev->dev.of_node);
	struct berlin_reset_priv *priv;

	priv = devm_kzalloc(&pdev->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->regmap = syscon_node_to_regmap(parent_np);
	of_node_put(parent_np);
	if (IS_ERR(priv->regmap))
		return PTR_ERR(priv->regmap);

	priv->rcdev.owner = THIS_MODULE;
	priv->rcdev.ops = &berlin_reset_ops;
	priv->rcdev.of_node = pdev->dev.of_node;
	priv->rcdev.of_reset_n_cells = 2;
	priv->rcdev.of_xlate = berlin_reset_xlate;

	return reset_controller_register(&priv->rcdev);
}

static const struct of_device_id berlin_reset_dt_match[] = {
	{ .compatible = "marvell,berlin2-reset" },
	{ },
};
MODULE_DEVICE_TABLE(of, berlin_reset_dt_match);

static struct platform_driver berlin_reset_driver = {
	.probe	= berlin2_reset_probe,
	.driver	= {
		.name = "berlin2-reset",
		.of_match_table = berlin_reset_dt_match,
	},
};
module_platform_driver(berlin_reset_driver);

MODULE_AUTHOR("Antoine Tenart <antoine.tenart@free-electrons.com>");
MODULE_AUTHOR("Sebastian Hesselbarth <sebastian.hesselbarth@gmail.com>");
MODULE_DESCRIPTION("Synaptics Berlin reset controller");
MODULE_LICENSE("GPL");
