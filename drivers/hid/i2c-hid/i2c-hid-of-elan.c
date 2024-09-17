// SPDX-License-Identifier: GPL-2.0
/*
 * Driver for Elan touchscreens that use the i2c-hid protocol.
 *
 * Copyright 2020 Google LLC
 */

#include <linux/delay.h>
#include <linux/device.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/pm.h>
#include <linux/regulator/consumer.h>

#include "i2c-hid.h"

struct elan_i2c_hid_chip_data {
	unsigned int post_gpio_reset_delay_ms;
	unsigned int post_power_delay_ms;
	u16 hid_descriptor_address;
};

struct i2c_hid_of_elan {
	struct i2chid_ops ops;

	struct regulator *vcc33;
	struct regulator *vccio;
	struct gpio_desc *reset_gpio;
	const struct elan_i2c_hid_chip_data *chip_data;
};

static int elan_i2c_hid_power_up(struct i2chid_ops *ops)
{
	struct i2c_hid_of_elan *ihid_elan =
		container_of(ops, struct i2c_hid_of_elan, ops);
	int ret;

	ret = regulator_enable(ihid_elan->vcc33);
	if (ret)
		return ret;

	ret = regulator_enable(ihid_elan->vccio);
	if (ret) {
		regulator_disable(ihid_elan->vcc33);
		return ret;
	}

	if (ihid_elan->chip_data->post_power_delay_ms)
		msleep(ihid_elan->chip_data->post_power_delay_ms);

	gpiod_set_value_cansleep(ihid_elan->reset_gpio, 0);
	if (ihid_elan->chip_data->post_gpio_reset_delay_ms)
		msleep(ihid_elan->chip_data->post_gpio_reset_delay_ms);

	return 0;
}

static void elan_i2c_hid_power_down(struct i2chid_ops *ops)
{
	struct i2c_hid_of_elan *ihid_elan =
		container_of(ops, struct i2c_hid_of_elan, ops);

	gpiod_set_value_cansleep(ihid_elan->reset_gpio, 1);
	regulator_disable(ihid_elan->vccio);
	regulator_disable(ihid_elan->vcc33);
}

static int i2c_hid_of_elan_probe(struct i2c_client *client,
				 const struct i2c_device_id *id)
{
	struct i2c_hid_of_elan *ihid_elan;

	ihid_elan = devm_kzalloc(&client->dev, sizeof(*ihid_elan), GFP_KERNEL);
	if (!ihid_elan)
		return -ENOMEM;

	ihid_elan->ops.power_up = elan_i2c_hid_power_up;
	ihid_elan->ops.power_down = elan_i2c_hid_power_down;

	/* Start out with reset asserted */
	ihid_elan->reset_gpio =
		devm_gpiod_get_optional(&client->dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(ihid_elan->reset_gpio))
		return PTR_ERR(ihid_elan->reset_gpio);

	ihid_elan->vccio = devm_regulator_get(&client->dev, "vccio");
	if (IS_ERR(ihid_elan->vccio))
		return PTR_ERR(ihid_elan->vccio);

	ihid_elan->vcc33 = devm_regulator_get(&client->dev, "vcc33");
	if (IS_ERR(ihid_elan->vcc33))
		return PTR_ERR(ihid_elan->vcc33);

	ihid_elan->chip_data = device_get_match_data(&client->dev);

	return i2c_hid_core_probe(client, &ihid_elan->ops,
				  ihid_elan->chip_data->hid_descriptor_address, 0);
}

static const struct elan_i2c_hid_chip_data elan_ekth6915_chip_data = {
	.post_power_delay_ms = 1,
	.post_gpio_reset_delay_ms = 300,
	.hid_descriptor_address = 0x0001,
};

static const struct of_device_id elan_i2c_hid_of_match[] = {
	{ .compatible = "elan,ekth6915", .data = &elan_ekth6915_chip_data },
	{ }
};
MODULE_DEVICE_TABLE(of, elan_i2c_hid_of_match);

static struct i2c_driver elan_i2c_hid_ts_driver = {
	.driver = {
		.name	= "i2c_hid_of_elan",
		.pm	= &i2c_hid_core_pm,
		.probe_type = PROBE_PREFER_ASYNCHRONOUS,
		.of_match_table = of_match_ptr(elan_i2c_hid_of_match),
	},
	.probe		= i2c_hid_of_elan_probe,
	.remove		= i2c_hid_core_remove,
	.shutdown	= i2c_hid_core_shutdown,
};
module_i2c_driver(elan_i2c_hid_ts_driver);

MODULE_AUTHOR("Douglas Anderson <dianders@chromium.org>");
MODULE_DESCRIPTION("Elan i2c-hid touchscreen driver");
MODULE_LICENSE("GPL");
