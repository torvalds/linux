/*
 * TM2 touchkey device driver
 *
 * Copyright 2005 Phil Blundell
 * Copyright 2016 Samsung Electronics Co., Ltd.
 *
 * Author: Beomho Seo <beomho.seo@samsung.com>
 * Author: Jaechul Lee <jcsing.lee@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/bitops.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/i2c.h>
#include <linux/input.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/leds.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/pm.h>
#include <linux/regulator/consumer.h>

#define TM2_TOUCHKEY_DEV_NAME		"tm2-touchkey"

#define ARIES_TOUCHKEY_CMD_LED_ON	0x1
#define ARIES_TOUCHKEY_CMD_LED_OFF	0x2
#define TM2_TOUCHKEY_CMD_LED_ON		0x10
#define TM2_TOUCHKEY_CMD_LED_OFF	0x20
#define TM2_TOUCHKEY_BIT_PRESS_EV	BIT(3)
#define TM2_TOUCHKEY_BIT_KEYCODE	GENMASK(2, 0)
#define TM2_TOUCHKEY_LED_VOLTAGE_MIN	2500000
#define TM2_TOUCHKEY_LED_VOLTAGE_MAX	3300000

struct touchkey_variant {
	u8 keycode_reg;
	u8 base_reg;
	u8 cmd_led_on;
	u8 cmd_led_off;
	bool no_reg;
	bool fixed_regulator;
};

struct tm2_touchkey_data {
	struct i2c_client *client;
	struct input_dev *input_dev;
	struct led_classdev led_dev;
	struct regulator *vdd;
	struct regulator_bulk_data regulators[2];
	const struct touchkey_variant *variant;
	u32 keycodes[4];
	int num_keycodes;
};

static const struct touchkey_variant tm2_touchkey_variant = {
	.keycode_reg = 0x03,
	.base_reg = 0x00,
	.cmd_led_on = TM2_TOUCHKEY_CMD_LED_ON,
	.cmd_led_off = TM2_TOUCHKEY_CMD_LED_OFF,
};

static const struct touchkey_variant midas_touchkey_variant = {
	.keycode_reg = 0x00,
	.base_reg = 0x00,
	.cmd_led_on = TM2_TOUCHKEY_CMD_LED_ON,
	.cmd_led_off = TM2_TOUCHKEY_CMD_LED_OFF,
};

static struct touchkey_variant aries_touchkey_variant = {
	.no_reg = true,
	.fixed_regulator = true,
	.cmd_led_on = ARIES_TOUCHKEY_CMD_LED_ON,
	.cmd_led_off = ARIES_TOUCHKEY_CMD_LED_OFF,
};

static int tm2_touchkey_led_brightness_set(struct led_classdev *led_dev,
					    enum led_brightness brightness)
{
	struct tm2_touchkey_data *touchkey =
		container_of(led_dev, struct tm2_touchkey_data, led_dev);
	u32 volt;
	u8 data;

	if (brightness == LED_OFF) {
		volt = TM2_TOUCHKEY_LED_VOLTAGE_MIN;
		data = touchkey->variant->cmd_led_off;
	} else {
		volt = TM2_TOUCHKEY_LED_VOLTAGE_MAX;
		data = touchkey->variant->cmd_led_on;
	}

	if (!touchkey->variant->fixed_regulator)
		regulator_set_voltage(touchkey->vdd, volt, volt);

	return touchkey->variant->no_reg ?
		i2c_smbus_write_byte(touchkey->client, data) :
		i2c_smbus_write_byte_data(touchkey->client,
					  touchkey->variant->base_reg, data);
}

static int tm2_touchkey_power_enable(struct tm2_touchkey_data *touchkey)
{
	int error;

	error = regulator_bulk_enable(ARRAY_SIZE(touchkey->regulators),
				      touchkey->regulators);
	if (error)
		return error;

	/* waiting for device initialization, at least 150ms */
	msleep(150);

	return 0;
}

static void tm2_touchkey_power_disable(void *data)
{
	struct tm2_touchkey_data *touchkey = data;

	regulator_bulk_disable(ARRAY_SIZE(touchkey->regulators),
			       touchkey->regulators);
}

static irqreturn_t tm2_touchkey_irq_handler(int irq, void *devid)
{
	struct tm2_touchkey_data *touchkey = devid;
	int data;
	int index;
	int i;

	if (touchkey->variant->no_reg)
		data = i2c_smbus_read_byte(touchkey->client);
	else
		data = i2c_smbus_read_byte_data(touchkey->client,
						touchkey->variant->keycode_reg);
	if (data < 0) {
		dev_err(&touchkey->client->dev,
			"failed to read i2c data: %d\n", data);
		goto out;
	}

	index = (data & TM2_TOUCHKEY_BIT_KEYCODE) - 1;
	if (index < 0 || index >= touchkey->num_keycodes) {
		dev_warn(&touchkey->client->dev,
			 "invalid keycode index %d\n", index);
		goto out;
	}

	if (data & TM2_TOUCHKEY_BIT_PRESS_EV) {
		for (i = 0; i < touchkey->num_keycodes; i++)
			input_report_key(touchkey->input_dev,
					 touchkey->keycodes[i], 0);
	} else {
		input_report_key(touchkey->input_dev,
				 touchkey->keycodes[index], 1);
	}

	input_sync(touchkey->input_dev);

out:
	if (touchkey->variant->fixed_regulator &&
				data & TM2_TOUCHKEY_BIT_PRESS_EV) {
		/* touch turns backlight on, so make sure we're in sync */
		if (touchkey->led_dev.brightness == LED_OFF)
			tm2_touchkey_led_brightness_set(&touchkey->led_dev,
							LED_OFF);
	}

	return IRQ_HANDLED;
}

static int tm2_touchkey_probe(struct i2c_client *client,
			      const struct i2c_device_id *id)
{
	struct device_node *np = client->dev.of_node;
	struct tm2_touchkey_data *touchkey;
	int error;
	int i;

	if (!i2c_check_functionality(client->adapter,
			I2C_FUNC_SMBUS_BYTE | I2C_FUNC_SMBUS_BYTE_DATA)) {
		dev_err(&client->dev, "incompatible I2C adapter\n");
		return -EIO;
	}

	touchkey = devm_kzalloc(&client->dev, sizeof(*touchkey), GFP_KERNEL);
	if (!touchkey)
		return -ENOMEM;

	touchkey->client = client;
	i2c_set_clientdata(client, touchkey);

	touchkey->variant = of_device_get_match_data(&client->dev);

	touchkey->regulators[0].supply = "vcc";
	touchkey->regulators[1].supply = "vdd";
	error = devm_regulator_bulk_get(&client->dev,
					ARRAY_SIZE(touchkey->regulators),
					touchkey->regulators);
	if (error) {
		dev_err(&client->dev, "failed to get regulators: %d\n", error);
		return error;
	}

	/* Save VDD for easy access */
	touchkey->vdd = touchkey->regulators[1].consumer;

	touchkey->num_keycodes = of_property_read_variable_u32_array(np,
					"linux,keycodes", touchkey->keycodes, 0,
					ARRAY_SIZE(touchkey->keycodes));
	if (touchkey->num_keycodes <= 0) {
		/* default keycodes */
		touchkey->keycodes[0] = KEY_PHONE;
		touchkey->keycodes[1] = KEY_BACK;
		touchkey->num_keycodes = 2;
	}

	error = tm2_touchkey_power_enable(touchkey);
	if (error) {
		dev_err(&client->dev, "failed to power up device: %d\n", error);
		return error;
	}

	error = devm_add_action_or_reset(&client->dev,
					 tm2_touchkey_power_disable, touchkey);
	if (error) {
		dev_err(&client->dev,
			"failed to install poweroff handler: %d\n", error);
		return error;
	}

	/* input device */
	touchkey->input_dev = devm_input_allocate_device(&client->dev);
	if (!touchkey->input_dev) {
		dev_err(&client->dev, "failed to allocate input device\n");
		return -ENOMEM;
	}

	touchkey->input_dev->name = TM2_TOUCHKEY_DEV_NAME;
	touchkey->input_dev->id.bustype = BUS_I2C;

	for (i = 0; i < touchkey->num_keycodes; i++)
		input_set_capability(touchkey->input_dev, EV_KEY,
				     touchkey->keycodes[i]);

	error = input_register_device(touchkey->input_dev);
	if (error) {
		dev_err(&client->dev,
			"failed to register input device: %d\n", error);
		return error;
	}

	error = devm_request_threaded_irq(&client->dev, client->irq,
					  NULL, tm2_touchkey_irq_handler,
					  IRQF_ONESHOT,
					  TM2_TOUCHKEY_DEV_NAME, touchkey);
	if (error) {
		dev_err(&client->dev,
			"failed to request threaded irq: %d\n", error);
		return error;
	}

	/* led device */
	touchkey->led_dev.name = TM2_TOUCHKEY_DEV_NAME;
	touchkey->led_dev.brightness = LED_ON;
	touchkey->led_dev.max_brightness = LED_ON;
	touchkey->led_dev.brightness_set_blocking =
					tm2_touchkey_led_brightness_set;

	error = devm_led_classdev_register(&client->dev, &touchkey->led_dev);
	if (error) {
		dev_err(&client->dev,
			"failed to register touchkey led: %d\n", error);
		return error;
	}

	if (touchkey->variant->fixed_regulator)
		tm2_touchkey_led_brightness_set(&touchkey->led_dev, LED_ON);

	return 0;
}

static int __maybe_unused tm2_touchkey_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct tm2_touchkey_data *touchkey = i2c_get_clientdata(client);

	disable_irq(client->irq);
	tm2_touchkey_power_disable(touchkey);

	return 0;
}

static int __maybe_unused tm2_touchkey_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct tm2_touchkey_data *touchkey = i2c_get_clientdata(client);
	int ret;

	enable_irq(client->irq);

	ret = tm2_touchkey_power_enable(touchkey);
	if (ret)
		dev_err(dev, "failed to enable power: %d\n", ret);

	return ret;
}

static SIMPLE_DEV_PM_OPS(tm2_touchkey_pm_ops,
			 tm2_touchkey_suspend, tm2_touchkey_resume);

static const struct i2c_device_id tm2_touchkey_id_table[] = {
	{ TM2_TOUCHKEY_DEV_NAME, 0 },
	{ },
};
MODULE_DEVICE_TABLE(i2c, tm2_touchkey_id_table);

static const struct of_device_id tm2_touchkey_of_match[] = {
	{
		.compatible = "cypress,tm2-touchkey",
		.data = &tm2_touchkey_variant,
	}, {
		.compatible = "cypress,midas-touchkey",
		.data = &midas_touchkey_variant,
	}, {
		.compatible = "cypress,aries-touchkey",
		.data = &aries_touchkey_variant,
	},
	{ },
};
MODULE_DEVICE_TABLE(of, tm2_touchkey_of_match);

static struct i2c_driver tm2_touchkey_driver = {
	.driver = {
		.name = TM2_TOUCHKEY_DEV_NAME,
		.pm = &tm2_touchkey_pm_ops,
		.of_match_table = of_match_ptr(tm2_touchkey_of_match),
	},
	.probe = tm2_touchkey_probe,
	.id_table = tm2_touchkey_id_table,
};
module_i2c_driver(tm2_touchkey_driver);

MODULE_AUTHOR("Beomho Seo <beomho.seo@samsung.com>");
MODULE_AUTHOR("Jaechul Lee <jcsing.lee@samsung.com>");
MODULE_DESCRIPTION("Samsung touchkey driver");
MODULE_LICENSE("GPL v2");
