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
	struct regulator *vddio;
	struct gpio_desc *reset_gpio;
	bool no_reset_during_suspend;
	const struct goodix_i2c_hid_timing_data *timings;
};

static int goodix_i2c_hid_power_up(struct i2chid_ops *ops)
{
	struct i2c_hid_of_goodix *ihid_goodix =
		container_of(ops, struct i2c_hid_of_goodix, ops);
	int ret;

	/*
	 * We assert reset GPIO here (instead of during power-down) to ensure
	 * the device will have a clean state after powering up, just like the
	 * normal scenarios will have.
	 */
	if (ihid_goodix->no_reset_during_suspend)
		gpiod_set_value_cansleep(ihid_goodix->reset_gpio, 1);

	ret = regulator_enable(ihid_goodix->vdd);
	if (ret)
		return ret;

	ret = regulator_enable(ihid_goodix->vddio);
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

	if (!ihid_goodix->no_reset_during_suspend)
		gpiod_set_value_cansleep(ihid_goodix->reset_gpio, 1);

	regulator_disable(ihid_goodix->vddio);
	regulator_disable(ihid_goodix->vdd);
}

static int i2c_hid_of_goodix_probe(struct i2c_client *client)
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

	ihid_goodix->vddio = devm_regulator_get(&client->dev, "mainboard-vddio");
	if (IS_ERR(ihid_goodix->vddio))
		return PTR_ERR(ihid_goodix->vddio);

	ihid_goodix->no_reset_during_suspend =
		of_property_read_bool(client->dev.of_node, "goodix,no-reset-during-suspend");

	ihid_goodix->timings = device_get_match_data(&client->dev);

	return i2c_hid_core_probe(client, &ihid_goodix->ops, 0x0001, 0);
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
	.probe_new	= i2c_hid_of_goodix_probe,
	.remove		= i2c_hid_core_remove,
	.shutdown	= i2c_hid_core_shutdown,
};
module_i2c_driver(goodix_i2c_hid_ts_driver);

MODULE_AUTHOR("Douglas Anderson <dianders@chromium.org>");
MODULE_DESCRIPTION("Goodix i2c-hid touchscreen driver");
MODULE_LICENSE("GPL v2");
