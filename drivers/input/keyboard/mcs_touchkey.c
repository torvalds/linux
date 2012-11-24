/*
 * Touchkey driver for MELFAS MCS5000/5080 controller
 *
 * Copyright (C) 2010 Samsung Electronics Co.Ltd
 * Author: HeungJun Kim <riverful.kim@samsung.com>
 * Author: Joonyoung Shim <jy0922.shim@samsung.com>
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/i2c.h>
#include <linux/i2c/mcs.h>
#include <linux/interrupt.h>
#include <linux/input.h>
#include <linux/irq.h>
#include <linux/slab.h>
#include <linux/pm.h>

/* MCS5000 Touchkey */
#define MCS5000_TOUCHKEY_STATUS		0x04
#define MCS5000_TOUCHKEY_STATUS_PRESS	7
#define MCS5000_TOUCHKEY_FW		0x0a
#define MCS5000_TOUCHKEY_BASE_VAL	0x61

/* MCS5080 Touchkey */
#define MCS5080_TOUCHKEY_STATUS		0x00
#define MCS5080_TOUCHKEY_STATUS_PRESS	3
#define MCS5080_TOUCHKEY_FW		0x01
#define MCS5080_TOUCHKEY_BASE_VAL	0x1

enum mcs_touchkey_type {
	MCS5000_TOUCHKEY,
	MCS5080_TOUCHKEY,
};

struct mcs_touchkey_chip {
	unsigned int status_reg;
	unsigned int pressbit;
	unsigned int press_invert;
	unsigned int baseval;
};

struct mcs_touchkey_data {
	void (*poweron)(bool);

	struct i2c_client *client;
	struct input_dev *input_dev;
	struct mcs_touchkey_chip chip;
	unsigned int key_code;
	unsigned int key_val;
	unsigned short keycodes[];
};

static irqreturn_t mcs_touchkey_interrupt(int irq, void *dev_id)
{
	struct mcs_touchkey_data *data = dev_id;
	struct mcs_touchkey_chip *chip = &data->chip;
	struct i2c_client *client = data->client;
	struct input_dev *input = data->input_dev;
	unsigned int key_val;
	unsigned int pressed;
	int val;

	val = i2c_smbus_read_byte_data(client, chip->status_reg);
	if (val < 0) {
		dev_err(&client->dev, "i2c read error [%d]\n", val);
		goto out;
	}

	pressed = (val & (1 << chip->pressbit)) >> chip->pressbit;
	if (chip->press_invert)
		pressed ^= chip->press_invert;

	/* key_val is 0 when released, so we should use key_val of press. */
	if (pressed) {
		key_val = val & (0xff >> (8 - chip->pressbit));
		if (!key_val)
			goto out;
		key_val -= chip->baseval;
		data->key_code = data->keycodes[key_val];
		data->key_val = key_val;
	}

	input_event(input, EV_MSC, MSC_SCAN, data->key_val);
	input_report_key(input, data->key_code, pressed);
	input_sync(input);

	dev_dbg(&client->dev, "key %d %d %s\n", data->key_val, data->key_code,
		pressed ? "pressed" : "released");

 out:
	return IRQ_HANDLED;
}

static int __devinit mcs_touchkey_probe(struct i2c_client *client,
		const struct i2c_device_id *id)
{
	const struct mcs_platform_data *pdata;
	struct mcs_touchkey_data *data;
	struct input_dev *input_dev;
	unsigned int fw_reg;
	int fw_ver;
	int error;
	int i;

	pdata = client->dev.platform_data;
	if (!pdata) {
		dev_err(&client->dev, "no platform data defined\n");
		return -EINVAL;
	}

	data = kzalloc(sizeof(struct mcs_touchkey_data) +
			sizeof(data->keycodes[0]) * (pdata->key_maxval + 1),
			GFP_KERNEL);
	input_dev = input_allocate_device();
	if (!data || !input_dev) {
		dev_err(&client->dev, "Failed to allocate memory\n");
		error = -ENOMEM;
		goto err_free_mem;
	}

	data->client = client;
	data->input_dev = input_dev;

	if (id->driver_data == MCS5000_TOUCHKEY) {
		data->chip.status_reg = MCS5000_TOUCHKEY_STATUS;
		data->chip.pressbit = MCS5000_TOUCHKEY_STATUS_PRESS;
		data->chip.baseval = MCS5000_TOUCHKEY_BASE_VAL;
		fw_reg = MCS5000_TOUCHKEY_FW;
	} else {
		data->chip.status_reg = MCS5080_TOUCHKEY_STATUS;
		data->chip.pressbit = MCS5080_TOUCHKEY_STATUS_PRESS;
		data->chip.press_invert = 1;
		data->chip.baseval = MCS5080_TOUCHKEY_BASE_VAL;
		fw_reg = MCS5080_TOUCHKEY_FW;
	}

	fw_ver = i2c_smbus_read_byte_data(client, fw_reg);
	if (fw_ver < 0) {
		error = fw_ver;
		dev_err(&client->dev, "i2c read error[%d]\n", error);
		goto err_free_mem;
	}
	dev_info(&client->dev, "Firmware version: %d\n", fw_ver);

	input_dev->name = "MELPAS MCS Touchkey";
	input_dev->id.bustype = BUS_I2C;
	input_dev->dev.parent = &client->dev;
	input_dev->evbit[0] = BIT_MASK(EV_KEY);
	if (!pdata->no_autorepeat)
		input_dev->evbit[0] |= BIT_MASK(EV_REP);
	input_dev->keycode = data->keycodes;
	input_dev->keycodesize = sizeof(data->keycodes[0]);
	input_dev->keycodemax = pdata->key_maxval + 1;

	for (i = 0; i < pdata->keymap_size; i++) {
		unsigned int val = MCS_KEY_VAL(pdata->keymap[i]);
		unsigned int code = MCS_KEY_CODE(pdata->keymap[i]);

		data->keycodes[val] = code;
		__set_bit(code, input_dev->keybit);
	}

	input_set_capability(input_dev, EV_MSC, MSC_SCAN);
	input_set_drvdata(input_dev, data);

	if (pdata->cfg_pin)
		pdata->cfg_pin();

	if (pdata->poweron) {
		data->poweron = pdata->poweron;
		data->poweron(true);
	}

	error = request_threaded_irq(client->irq, NULL, mcs_touchkey_interrupt,
				     IRQF_TRIGGER_FALLING | IRQF_ONESHOT,
				     client->dev.driver->name, data);
	if (error) {
		dev_err(&client->dev, "Failed to register interrupt\n");
		goto err_free_mem;
	}

	error = input_register_device(input_dev);
	if (error)
		goto err_free_irq;

	i2c_set_clientdata(client, data);
	return 0;

err_free_irq:
	free_irq(client->irq, data);
err_free_mem:
	input_free_device(input_dev);
	kfree(data);
	return error;
}

static int __devexit mcs_touchkey_remove(struct i2c_client *client)
{
	struct mcs_touchkey_data *data = i2c_get_clientdata(client);

	free_irq(client->irq, data);
	if (data->poweron)
		data->poweron(false);
	input_unregister_device(data->input_dev);
	kfree(data);

	return 0;
}

static void mcs_touchkey_shutdown(struct i2c_client *client)
{
	struct mcs_touchkey_data *data = i2c_get_clientdata(client);

	if (data->poweron)
		data->poweron(false);
}

#ifdef CONFIG_PM_SLEEP
static int mcs_touchkey_suspend(struct device *dev)
{
	struct mcs_touchkey_data *data = dev_get_drvdata(dev);
	struct i2c_client *client = data->client;

	/* Disable the work */
	disable_irq(client->irq);

	/* Finally turn off the power */
	if (data->poweron)
		data->poweron(false);

	return 0;
}

static int mcs_touchkey_resume(struct device *dev)
{
	struct mcs_touchkey_data *data = dev_get_drvdata(dev);
	struct i2c_client *client = data->client;

	/* Enable the device first */
	if (data->poweron)
		data->poweron(true);

	/* Enable irq again */
	enable_irq(client->irq);

	return 0;
}
#endif

static SIMPLE_DEV_PM_OPS(mcs_touchkey_pm_ops,
			 mcs_touchkey_suspend, mcs_touchkey_resume);

static const struct i2c_device_id mcs_touchkey_id[] = {
	{ "mcs5000_touchkey", MCS5000_TOUCHKEY },
	{ "mcs5080_touchkey", MCS5080_TOUCHKEY },
	{ }
};
MODULE_DEVICE_TABLE(i2c, mcs_touchkey_id);

static struct i2c_driver mcs_touchkey_driver = {
	.driver = {
		.name	= "mcs_touchkey",
		.owner	= THIS_MODULE,
		.pm	= &mcs_touchkey_pm_ops,
	},
	.probe		= mcs_touchkey_probe,
	.remove		= mcs_touchkey_remove,
	.shutdown       = mcs_touchkey_shutdown,
	.id_table	= mcs_touchkey_id,
};

module_i2c_driver(mcs_touchkey_driver);

/* Module information */
MODULE_AUTHOR("Joonyoung Shim <jy0922.shim@samsung.com>");
MODULE_AUTHOR("HeungJun Kim <riverful.kim@samsung.com>");
MODULE_DESCRIPTION("Touchkey driver for MELFAS MCS5000/5080 controller");
MODULE_LICENSE("GPL");
