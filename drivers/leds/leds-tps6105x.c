// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2019 Sven Van Asbroeck
 */

#include <linux/leds.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/mfd/tps6105x.h>
#include <linux/regmap.h>

struct tps6105x_priv {
	struct regmap *regmap;
	struct led_classdev cdev;
	struct fwnode_handle *fwnode;
};

static void tps6105x_handle_put(void *data)
{
	struct tps6105x_priv *priv = data;

	fwnode_handle_put(priv->fwnode);
}

static int tps6105x_brightness_set(struct led_classdev *cdev,
				  enum led_brightness brightness)
{
	struct tps6105x_priv *priv = container_of(cdev, struct tps6105x_priv,
							cdev);

	return regmap_update_bits(priv->regmap, TPS6105X_REG_0,
				  TPS6105X_REG0_TORCHC_MASK,
				  brightness << TPS6105X_REG0_TORCHC_SHIFT);
}

static int tps6105x_led_probe(struct platform_device *pdev)
{
	struct tps6105x *tps6105x = dev_get_platdata(&pdev->dev);
	struct tps6105x_platform_data *pdata = tps6105x->pdata;
	struct led_init_data init_data = { };
	struct tps6105x_priv *priv;
	int ret;

	/* This instance is not set for torch mode so bail out */
	if (pdata->mode != TPS6105X_MODE_TORCH) {
		dev_info(&pdev->dev,
			"chip not in torch mode, exit probe");
		return -EINVAL;
	}

	priv = devm_kzalloc(&pdev->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;
	/* fwnode/devicetree is optional. NULL is allowed for priv->fwnode */
	priv->fwnode = device_get_next_child_node(pdev->dev.parent, NULL);
	ret = devm_add_action_or_reset(&pdev->dev, tps6105x_handle_put, priv);
	if (ret)
		return ret;
	priv->regmap = tps6105x->regmap;
	priv->cdev.brightness_set_blocking = tps6105x_brightness_set;
	priv->cdev.max_brightness = 7;
	init_data.devicename = "tps6105x";
	init_data.default_label = ":torch";
	init_data.fwnode = priv->fwnode;

	ret = regmap_update_bits(tps6105x->regmap, TPS6105X_REG_0,
				 TPS6105X_REG0_MODE_MASK |
					TPS6105X_REG0_TORCHC_MASK,
				 TPS6105X_REG0_MODE_TORCH <<
					TPS6105X_REG0_MODE_SHIFT);
	if (ret)
		return ret;

	return devm_led_classdev_register_ext(&pdev->dev, &priv->cdev,
					      &init_data);
}

static struct platform_driver led_driver = {
	.probe = tps6105x_led_probe,
	.driver = {
		.name = "tps6105x-leds",
	},
};

module_platform_driver(led_driver);

MODULE_DESCRIPTION("TPS6105x LED driver");
MODULE_AUTHOR("Sven Van Asbroeck <TheSven73@gmail.com>");
MODULE_LICENSE("GPL v2");
