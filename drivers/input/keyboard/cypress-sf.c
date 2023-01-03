// SPDX-License-Identifier: GPL-2.0-only
/*
 * Cypress StreetFighter Touchkey Driver
 *
 * Copyright (c) 2021 Yassine Oudjana <y.oudjana@protonmail.com>
 */

#include <linux/bitmap.h>
#include <linux/bitops.h>
#include <linux/device.h>
#include <linux/i2c.h>
#include <linux/input.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/pm.h>
#include <linux/regulator/consumer.h>

#define CYPRESS_SF_DEV_NAME "cypress-sf"

#define CYPRESS_SF_REG_BUTTON_STATUS	0x4a

struct cypress_sf_data {
	struct i2c_client *client;
	struct input_dev *input_dev;
	struct regulator_bulk_data regulators[2];
	u32 *keycodes;
	unsigned long keystates;
	int num_keys;
};

static irqreturn_t cypress_sf_irq_handler(int irq, void *devid)
{
	struct cypress_sf_data *touchkey = devid;
	unsigned long keystates, changed;
	bool new_state;
	int val, key;

	val = i2c_smbus_read_byte_data(touchkey->client,
				       CYPRESS_SF_REG_BUTTON_STATUS);
	if (val < 0) {
		dev_err(&touchkey->client->dev,
			"Failed to read button status: %d", val);
		return IRQ_NONE;
	}
	keystates = val;

	bitmap_xor(&changed, &keystates, &touchkey->keystates,
		   touchkey->num_keys);

	for_each_set_bit(key, &changed, touchkey->num_keys) {
		new_state = keystates & BIT(key);
		dev_dbg(&touchkey->client->dev,
			"Key %d changed to %d", key, new_state);
		input_report_key(touchkey->input_dev,
				 touchkey->keycodes[key], new_state);
	}

	input_sync(touchkey->input_dev);
	touchkey->keystates = keystates;

	return IRQ_HANDLED;
}

static void cypress_sf_disable_regulators(void *arg)
{
	struct cypress_sf_data *touchkey = arg;

	regulator_bulk_disable(ARRAY_SIZE(touchkey->regulators),
			       touchkey->regulators);
}

static int cypress_sf_probe(struct i2c_client *client)
{
	struct cypress_sf_data *touchkey;
	int key, error;

	touchkey = devm_kzalloc(&client->dev, sizeof(*touchkey), GFP_KERNEL);
	if (!touchkey)
		return -ENOMEM;

	touchkey->client = client;
	i2c_set_clientdata(client, touchkey);

	touchkey->regulators[0].supply = "vdd";
	touchkey->regulators[1].supply = "avdd";

	error = devm_regulator_bulk_get(&client->dev,
					ARRAY_SIZE(touchkey->regulators),
					touchkey->regulators);
	if (error) {
		dev_err(&client->dev, "Failed to get regulators: %d\n", error);
		return error;
	}

	touchkey->num_keys = device_property_read_u32_array(&client->dev,
							    "linux,keycodes",
							    NULL, 0);
	if (touchkey->num_keys < 0) {
		/* Default key count */
		touchkey->num_keys = 2;
	}

	touchkey->keycodes = devm_kcalloc(&client->dev,
					  touchkey->num_keys,
					  sizeof(*touchkey->keycodes),
					  GFP_KERNEL);
	if (!touchkey->keycodes)
		return -ENOMEM;

	error = device_property_read_u32_array(&client->dev, "linux,keycodes",
					       touchkey->keycodes,
					       touchkey->num_keys);

	if (error) {
		dev_warn(&client->dev,
			 "Failed to read keycodes: %d, using defaults\n",
			 error);

		/* Default keycodes */
		touchkey->keycodes[0] = KEY_BACK;
		touchkey->keycodes[1] = KEY_MENU;
	}

	error = regulator_bulk_enable(ARRAY_SIZE(touchkey->regulators),
				      touchkey->regulators);
	if (error) {
		dev_err(&client->dev,
			"Failed to enable regulators: %d\n", error);
		return error;
	}

	error = devm_add_action_or_reset(&client->dev,
					 cypress_sf_disable_regulators,
					 touchkey);
	if (error)
		return error;

	touchkey->input_dev = devm_input_allocate_device(&client->dev);
	if (!touchkey->input_dev) {
		dev_err(&client->dev, "Failed to allocate input device\n");
		return -ENOMEM;
	}

	touchkey->input_dev->name = CYPRESS_SF_DEV_NAME;
	touchkey->input_dev->id.bustype = BUS_I2C;

	for (key = 0; key < touchkey->num_keys; ++key)
		input_set_capability(touchkey->input_dev,
				     EV_KEY, touchkey->keycodes[key]);

	error = input_register_device(touchkey->input_dev);
	if (error) {
		dev_err(&client->dev,
			"Failed to register input device: %d\n", error);
		return error;
	}

	error = devm_request_threaded_irq(&client->dev, client->irq,
					  NULL, cypress_sf_irq_handler,
					  IRQF_ONESHOT,
					  CYPRESS_SF_DEV_NAME, touchkey);
	if (error) {
		dev_err(&client->dev,
			"Failed to register threaded irq: %d", error);
		return error;
	}

	return 0;
};

static int cypress_sf_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct cypress_sf_data *touchkey = i2c_get_clientdata(client);
	int error;

	disable_irq(client->irq);

	error = regulator_bulk_disable(ARRAY_SIZE(touchkey->regulators),
				       touchkey->regulators);
	if (error) {
		dev_err(dev, "Failed to disable regulators: %d", error);
		enable_irq(client->irq);
		return error;
	}

	return 0;
}

static int cypress_sf_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct cypress_sf_data *touchkey = i2c_get_clientdata(client);
	int error;

	error = regulator_bulk_enable(ARRAY_SIZE(touchkey->regulators),
				      touchkey->regulators);
	if (error) {
		dev_err(dev, "Failed to enable regulators: %d", error);
		return error;
	}

	enable_irq(client->irq);

	return 0;
}

static DEFINE_SIMPLE_DEV_PM_OPS(cypress_sf_pm_ops,
				cypress_sf_suspend, cypress_sf_resume);

static struct i2c_device_id cypress_sf_id_table[] = {
	{ CYPRESS_SF_DEV_NAME, 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, cypress_sf_id_table);

#ifdef CONFIG_OF
static const struct of_device_id cypress_sf_of_match[] = {
	{ .compatible = "cypress,sf3155", },
	{ },
};
MODULE_DEVICE_TABLE(of, cypress_sf_of_match);
#endif

static struct i2c_driver cypress_sf_driver = {
	.driver = {
		.name = CYPRESS_SF_DEV_NAME,
		.pm = pm_sleep_ptr(&cypress_sf_pm_ops),
		.of_match_table = of_match_ptr(cypress_sf_of_match),
	},
	.id_table = cypress_sf_id_table,
	.probe_new = cypress_sf_probe,
};
module_i2c_driver(cypress_sf_driver);

MODULE_AUTHOR("Yassine Oudjana <y.oudjana@protonmail.com>");
MODULE_DESCRIPTION("Cypress StreetFighter Touchkey Driver");
MODULE_LICENSE("GPL v2");
