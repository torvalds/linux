// SPDX-License-Identifier: GPL-2.0
/*
 * Driver for Goodix touchscreens that use the i2c-hid protocol.
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

struct goodix_i2c_hid_timing_data {
	unsigned int post_gpio_reset_delay_ms;
	unsigned int post_power_delay_ms;
};

struct i2c_hid_of_goodix {
	struct i2chid_ops ops;

	struct regulator *vdd;
	struct gpio_desc *reset_gpio;
	const struct goodix_i2c_hid_timing_data *timings;
};

static int goodix_i2c_hid_power_up(struct i2chid_ops *ops)
{
	struct i2c_hid_of_goodix *ihid_goodix =
		container_of(ops, struct i2c_hid_of_goodix, ops);
	int ret;

	ret = regulator_enable(ihid_goodix->vdd);
	if (ret)
		return ret;

	if (ihid_goodix->timings->post_power_delay_ms)
		msleep(ihid_goodix->timings->post_power_delay_ms);

	gpiod_set_value_cansleep(ihid_goodix->reset_gpio, 0);
	if (ihid_goodix->timings->post_gpio_reset_delay_ms)
		msleep(ihid_goodix->timings->post_gpio_reset_delay_ms);

	return 0;
}

static void goodix_i2c_hid_power_down(struct i2chid_ops *ops)
{
	struct i2c_hid_of_goodix *ihid_goodix =
		container_of(ops, struct i2c_hid_of_goodix, ops);

	gpiod_set_value_cansleep(ihid_goodix->reset_gpio, 1);
	regulator_disable(ihid_goodix->vdd);
}

static int i2c_hid_of_goodix_probe(struct i2c_client *client,
				   const struct i2c_device_id *id)
{
	struct i2c_hid_of_goodix *ihid_goodix;

	ihid_goodix = devm_kzalloc(&client->dev, sizeof(*ihid_goodix),
				   GFP_KERNEL);
	if (!ihid_goodix)
		return -ENOMEM;

	ihid_goodix->ops.power_up = goodix_i2c_hid_power_up;
	ihid_goodix->ops.power_down = goodix_i2c_hid_power_down;

	/* Start out with reset asserted */
	ihid_goodix->reset_gpio =
		devm_gpiod_get_optional(&client->dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(ihid_goodix->reset_gpio))
		return PTR_ERR(ihid_goodix->reset_gpio);

	ihid_goodix->vdd = devm_regulator_get(&client->dev, "vdd");
	if (IS_ERR(ihid_goodix->vdd))
		return PTR_ERR(ihid_goodix->vdd);

	ihid_goodix->timings = device_get_match_data(&client->dev);

	return i2c_hid_core_probe(client, &ihid_goodix->ops, 0x0001);
}

static const struct goodix_i2c_hid_timing_data goodix_gt7375p_timing_data = {
	.post_power_delay_ms = 10,
	.post_gpio_reset_delay_ms = 180,
};

static const struct of_device_id goodix_i2c_hid_of_match[] = {
	{ .compatible = "goodix,gt7375p", .data = &goodix_gt7375p_timing_data },
	{ }
};
MODULE_DEVICE_TABLE(of, goodix_i2c_hid_of_match);

static struct i2c_driver goodix_i2c_hid_ts_driver = {
	.driver = {
		.name	= "i2c_hid_of_goodix",
		.pm	= &i2c_hid_core_pm,
		.probe_type = PROBE_PREFER_ASYNCHRONOUS,
		.of_match_table = of_match_ptr(goodix_i2c_hid_of_match),
	},
	.probe		= i2c_hid_of_goodix_probe,
	.remove		= i2c_hid_core_remove,
	.shutdown	= i2c_hid_core_shutdown,
};
module_i2c_driver(goodix_i2c_hid_ts_driver);

MODULE_AUTHOR("Douglas Anderson <dianders@chromium.org>");
MODULE_DESCRIPTION("Goodix i2c-hid touchscreen driver");
MODULE_LICENSE("GPL v2");
