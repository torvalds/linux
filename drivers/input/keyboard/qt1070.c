// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  Atmel AT42QT1070 QTouch Sensor Controller
 *
 *  Copyright (C) 2011 Atmel
 *
 *  Authors: Bo Shen <voice.shen@atmel.com>
 *
 *  Base on AT42QT2160 driver by:
 *  Raphael Derosso Pereira <raphaelpereira@gmail.com>
 *  Copyright (C) 2009
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/input.h>
#include <linux/slab.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/jiffies.h>
#include <linux/delay.h>

/* Address for each register */
#define CHIP_ID            0x00
#define QT1070_CHIP_ID     0x2E

#define FW_VERSION         0x01
#define QT1070_FW_VERSION  0x15

#define DET_STATUS         0x02

#define KEY_STATUS         0x03

/* Calibrate */
#define CALIBRATE_CMD      0x38
#define QT1070_CAL_TIME    200

/* Reset */
#define RESET              0x39
#define QT1070_RESET_TIME  255

/* AT42QT1070 support up to 7 keys */
static const unsigned short qt1070_key2code[] = {
	KEY_0, KEY_1, KEY_2, KEY_3,
	KEY_4, KEY_5, KEY_6,
};

struct qt1070_data {
	struct i2c_client *client;
	struct input_dev *input;
	unsigned int irq;
	unsigned short keycodes[ARRAY_SIZE(qt1070_key2code)];
	u8 last_keys;
};

static int qt1070_read(struct i2c_client *client, u8 reg)
{
	int ret;

	ret = i2c_smbus_read_byte_data(client, reg);
	if (ret < 0)
		dev_err(&client->dev,
			"can not read register, returned %d\n", ret);

	return ret;
}

static int qt1070_write(struct i2c_client *client, u8 reg, u8 data)
{
	int ret;

	ret = i2c_smbus_write_byte_data(client, reg, data);
	if (ret < 0)
		dev_err(&client->dev,
			"can not write register, returned %d\n", ret);

	return ret;
}

static bool qt1070_identify(struct i2c_client *client)
{
	int id, ver;

	/* Read Chip ID */
	id = qt1070_read(client, CHIP_ID);
	if (id != QT1070_CHIP_ID) {
		dev_err(&client->dev, "ID %d not supported\n", id);
		return false;
	}

	/* Read firmware version */
	ver = qt1070_read(client, FW_VERSION);
	if (ver < 0) {
		dev_err(&client->dev, "could not read the firmware version\n");
		return false;
	}

	dev_info(&client->dev, "AT42QT1070 firmware version %x\n", ver);

	return true;
}

static irqreturn_t qt1070_interrupt(int irq, void *dev_id)
{
	struct qt1070_data *data = dev_id;
	struct i2c_client *client = data->client;
	struct input_dev *input = data->input;
	int i;
	u8 new_keys, keyval, mask = 0x01;

	/* Read the detected status register, thus clearing interrupt */
	qt1070_read(client, DET_STATUS);

	/* Read which key changed */
	new_keys = qt1070_read(client, KEY_STATUS);

	for (i = 0; i < ARRAY_SIZE(qt1070_key2code); i++) {
		keyval = new_keys & mask;
		if ((data->last_keys & mask) != keyval)
			input_report_key(input, data->keycodes[i], keyval);
		mask <<= 1;
	}
	input_sync(input);

	data->last_keys = new_keys;
	return IRQ_HANDLED;
}

static int qt1070_probe(struct i2c_client *client)
{
	struct qt1070_data *data;
	struct input_dev *input;
	int i;
	int err;

	err = i2c_check_functionality(client->adapter, I2C_FUNC_SMBUS_BYTE);
	if (!err) {
		dev_err(&client->dev, "%s adapter not supported\n",
			dev_driver_string(&client->adapter->dev));
		return -ENODEV;
	}

	if (!client->irq) {
		dev_err(&client->dev, "please assign the irq to this device\n");
		return -EINVAL;
	}

	/* Identify the qt1070 chip */
	if (!qt1070_identify(client))
		return -ENODEV;

	data = devm_kzalloc(&client->dev, sizeof(struct qt1070_data),
			    GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	input = devm_input_allocate_device(&client->dev);
	if (!input)
		return -ENOMEM;

	data->client = client;
	data->input = input;
	data->irq = client->irq;

	input->name = "AT42QT1070 QTouch Sensor";
	input->id.bustype = BUS_I2C;

	/* Add the keycode */
	input->keycode = data->keycodes;
	input->keycodesize = sizeof(data->keycodes[0]);
	input->keycodemax = ARRAY_SIZE(qt1070_key2code);

	__set_bit(EV_KEY, input->evbit);

	for (i = 0; i < ARRAY_SIZE(qt1070_key2code); i++) {
		data->keycodes[i] = qt1070_key2code[i];
		__set_bit(qt1070_key2code[i], input->keybit);
	}

	/* Calibrate device */
	qt1070_write(client, CALIBRATE_CMD, 1);
	msleep(QT1070_CAL_TIME);

	/* Soft reset */
	qt1070_write(client, RESET, 1);
	msleep(QT1070_RESET_TIME);

	err = devm_request_threaded_irq(&client->dev, client->irq,
					NULL, qt1070_interrupt,
					IRQF_TRIGGER_NONE | IRQF_ONESHOT,
					client->dev.driver->name, data);
	if (err) {
		dev_err(&client->dev, "fail to request irq\n");
		return err;
	}

	/* Register the input device */
	err = input_register_device(data->input);
	if (err) {
		dev_err(&client->dev, "Failed to register input device\n");
		return err;
	}

	i2c_set_clientdata(client, data);

	/* Read to clear the chang line */
	qt1070_read(client, DET_STATUS);

	return 0;
}

static int qt1070_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct qt1070_data *data = i2c_get_clientdata(client);

	if (device_may_wakeup(dev))
		enable_irq_wake(data->irq);

	return 0;
}

static int qt1070_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct qt1070_data *data = i2c_get_clientdata(client);

	if (device_may_wakeup(dev))
		disable_irq_wake(data->irq);

	return 0;
}

static DEFINE_SIMPLE_DEV_PM_OPS(qt1070_pm_ops, qt1070_suspend, qt1070_resume);

static const struct i2c_device_id qt1070_id[] = {
	{ "qt1070" },
	{ }
};
MODULE_DEVICE_TABLE(i2c, qt1070_id);

#ifdef CONFIG_OF
static const struct of_device_id qt1070_of_match[] = {
	{ .compatible = "qt1070", },
	{ },
};
MODULE_DEVICE_TABLE(of, qt1070_of_match);
#endif

static struct i2c_driver qt1070_driver = {
	.driver	= {
		.name	= "qt1070",
		.of_match_table = of_match_ptr(qt1070_of_match),
		.pm	= pm_sleep_ptr(&qt1070_pm_ops),
	},
	.id_table	= qt1070_id,
	.probe		= qt1070_probe,
};

module_i2c_driver(qt1070_driver);

MODULE_AUTHOR("Bo Shen <voice.shen@atmel.com>");
MODULE_DESCRIPTION("Driver for AT42QT1070 QTouch sensor");
MODULE_LICENSE("GPL");
