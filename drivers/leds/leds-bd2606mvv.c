// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2023 Andreas Kemnade
 *
 * Datasheet:
 * https://fscdn.rohm.com/en/products/databook/datasheet/ic/power/led_driver/bd2606mvv_1-e.pdf
 *
 * If LED brightness cannot be controlled independently due to shared
 * brightness registers, max_brightness is set to 1 and only on/off
 * is possible for the affected LED pair.
 */

#include <linux/i2c.h>
#include <linux/leds.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/property.h>
#include <linux/regmap.h>
#include <linux/slab.h>

#define BD2606_MAX_LEDS 6
#define BD2606_MAX_BRIGHTNESS 63
#define BD2606_REG_PWRCNT 3
#define ldev_to_led(c)	container_of(c, struct bd2606mvv_led, ldev)

struct bd2606mvv_led {
	unsigned int led_no;
	struct led_classdev ldev;
	struct bd2606mvv_priv *priv;
};

struct bd2606mvv_priv {
	struct bd2606mvv_led leds[BD2606_MAX_LEDS];
	struct regmap *regmap;
};

static int
bd2606mvv_brightness_set(struct led_classdev *led_cdev,
		      enum led_brightness brightness)
{
	struct bd2606mvv_led *led = ldev_to_led(led_cdev);
	struct bd2606mvv_priv *priv = led->priv;
	int err;

	if (brightness == 0)
		return regmap_update_bits(priv->regmap,
					  BD2606_REG_PWRCNT,
					  1 << led->led_no,
					  0);

	/* shared brightness register */
	err = regmap_write(priv->regmap, led->led_no / 2,
			   led_cdev->max_brightness == 1 ?
			   BD2606_MAX_BRIGHTNESS : brightness);
	if (err)
		return err;

	return regmap_update_bits(priv->regmap,
				  BD2606_REG_PWRCNT,
				  1 << led->led_no,
				  1 << led->led_no);
}

static const struct regmap_config bd2606mvv_regmap = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = 0x3,
};

static int bd2606mvv_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct bd2606mvv_priv *priv;
	struct fwnode_handle *led_fwnodes[BD2606_MAX_LEDS] = { 0 };
	int active_pairs[BD2606_MAX_LEDS / 2] = { 0 };
	int err, reg;
	int i, j;

	if (!dev_fwnode(dev))
		return -ENODEV;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->regmap = devm_regmap_init_i2c(client, &bd2606mvv_regmap);
	if (IS_ERR(priv->regmap)) {
		err = PTR_ERR(priv->regmap);
		dev_err(dev, "Failed to allocate register map: %d\n", err);
		return err;
	}

	i2c_set_clientdata(client, priv);

	device_for_each_child_node_scoped(dev, child) {
		struct bd2606mvv_led *led;

		err = fwnode_property_read_u32(child, "reg", &reg);
		if (err)
			return err;

		if (reg < 0 || reg >= BD2606_MAX_LEDS || led_fwnodes[reg])
			return -EINVAL;

		led = &priv->leds[reg];
		led_fwnodes[reg] = fwnode_handle_get(child);
		active_pairs[reg / 2]++;
		led->priv = priv;
		led->led_no = reg;
		led->ldev.brightness_set_blocking = bd2606mvv_brightness_set;
		led->ldev.max_brightness = BD2606_MAX_BRIGHTNESS;
	}

	for (i = 0; i < BD2606_MAX_LEDS; i++) {
		struct led_init_data init_data = {};

		if (!led_fwnodes[i])
			continue;

		init_data.fwnode = led_fwnodes[i];
		/* Check whether brightness can be independently adjusted. */
		if (active_pairs[i / 2] == 2)
			priv->leds[i].ldev.max_brightness = 1;

		err = devm_led_classdev_register_ext(dev,
						     &priv->leds[i].ldev,
						     &init_data);
		if (err < 0) {
			for (j = i; j < BD2606_MAX_LEDS; j++)
				fwnode_handle_put(led_fwnodes[j]);
			return dev_err_probe(dev, err,
					     "couldn't register LED %s\n",
					     priv->leds[i].ldev.name);
		}
	}
	return 0;
}

static const struct of_device_id __maybe_unused of_bd2606mvv_leds_match[] = {
	{ .compatible = "rohm,bd2606mvv", },
	{},
};
MODULE_DEVICE_TABLE(of, of_bd2606mvv_leds_match);

static struct i2c_driver bd2606mvv_driver = {
	.driver   = {
		.name    = "leds-bd2606mvv",
		.of_match_table = of_match_ptr(of_bd2606mvv_leds_match),
	},
	.probe = bd2606mvv_probe,
};

module_i2c_driver(bd2606mvv_driver);

MODULE_AUTHOR("Andreas Kemnade <andreas@kemnade.info>");
MODULE_DESCRIPTION("BD2606 LED driver");
MODULE_LICENSE("GPL");
