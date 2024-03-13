// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2021 Oleh Kravchenko <oleg@kaa.org.ua>
 *
 * SparkFun Qwiic Joystick
 * Product page:https://www.sparkfun.com/products/15168
 * Firmware and hardware sources:https://github.com/sparkfun/Qwiic_Joystick
 */

#include <linux/bits.h>
#include <linux/i2c.h>
#include <linux/input.h>
#include <linux/kernel.h>
#include <linux/module.h>

#define DRV_NAME "qwiic-joystick"

#define QWIIC_JSK_REG_VERS	1
#define QWIIC_JSK_REG_DATA	3

#define QWIIC_JSK_MAX_AXIS	GENMASK(9, 0)
#define QWIIC_JSK_FUZZ		2
#define QWIIC_JSK_FLAT		2
#define QWIIC_JSK_POLL_INTERVAL	16
#define QWIIC_JSK_POLL_MIN	8
#define QWIIC_JSK_POLL_MAX	32

struct qwiic_jsk {
	char phys[32];
	struct input_dev *dev;
	struct i2c_client *client;
};

struct qwiic_ver {
	u8 major;
	u8 minor;
};

struct qwiic_data {
	__be16 x;
	__be16 y;
	u8 thumb;
};

static void qwiic_poll(struct input_dev *input)
{
	struct qwiic_jsk *priv = input_get_drvdata(input);
	struct qwiic_data data;
	int err;

	err = i2c_smbus_read_i2c_block_data(priv->client, QWIIC_JSK_REG_DATA,
					    sizeof(data), (u8 *)&data);
	if (err != sizeof(data))
		return;

	input_report_abs(input, ABS_X, be16_to_cpu(data.x) >> 6);
	input_report_abs(input, ABS_Y, be16_to_cpu(data.y) >> 6);
	input_report_key(input, BTN_THUMBL, !data.thumb);
	input_sync(input);
}

static int qwiic_probe(struct i2c_client *client)
{
	struct qwiic_jsk *priv;
	struct qwiic_ver vers;
	int err;

	err = i2c_smbus_read_i2c_block_data(client, QWIIC_JSK_REG_VERS,
					    sizeof(vers), (u8 *)&vers);
	if (err < 0)
		return err;
	if (err != sizeof(vers))
		return -EIO;

	dev_dbg(&client->dev, "SparkFun Qwiic Joystick, FW: %u.%u\n",
		vers.major, vers.minor);

	priv = devm_kzalloc(&client->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->client = client;
	snprintf(priv->phys, sizeof(priv->phys),
		 "i2c/%s", dev_name(&client->dev));
	i2c_set_clientdata(client, priv);

	priv->dev = devm_input_allocate_device(&client->dev);
	if (!priv->dev)
		return -ENOMEM;

	priv->dev->id.bustype = BUS_I2C;
	priv->dev->name = "SparkFun Qwiic Joystick";
	priv->dev->phys = priv->phys;
	input_set_drvdata(priv->dev, priv);

	input_set_abs_params(priv->dev, ABS_X, 0, QWIIC_JSK_MAX_AXIS,
			     QWIIC_JSK_FUZZ, QWIIC_JSK_FLAT);
	input_set_abs_params(priv->dev, ABS_Y, 0, QWIIC_JSK_MAX_AXIS,
			     QWIIC_JSK_FUZZ, QWIIC_JSK_FLAT);
	input_set_capability(priv->dev, EV_KEY, BTN_THUMBL);

	err = input_setup_polling(priv->dev, qwiic_poll);
	if (err) {
		dev_err(&client->dev, "failed to set up polling: %d\n", err);
		return err;
	}
	input_set_poll_interval(priv->dev, QWIIC_JSK_POLL_INTERVAL);
	input_set_min_poll_interval(priv->dev, QWIIC_JSK_POLL_MIN);
	input_set_max_poll_interval(priv->dev, QWIIC_JSK_POLL_MAX);

	err = input_register_device(priv->dev);
	if (err) {
		dev_err(&client->dev, "failed to register joystick: %d\n", err);
		return err;
	}

	return 0;
}

#ifdef CONFIG_OF
static const struct of_device_id of_qwiic_match[] = {
	{ .compatible = "sparkfun,qwiic-joystick", },
	{ },
};
MODULE_DEVICE_TABLE(of, of_qwiic_match);
#endif /* CONFIG_OF */

static const struct i2c_device_id qwiic_id_table[] = {
	{ KBUILD_MODNAME, 0 },
	{ },
};
MODULE_DEVICE_TABLE(i2c, qwiic_id_table);

static struct i2c_driver qwiic_driver = {
	.driver = {
		.name		= DRV_NAME,
		.of_match_table	= of_match_ptr(of_qwiic_match),
	},
	.id_table	= qwiic_id_table,
	.probe_new	= qwiic_probe,
};
module_i2c_driver(qwiic_driver);

MODULE_AUTHOR("Oleh Kravchenko <oleg@kaa.org.ua>");
MODULE_DESCRIPTION("SparkFun Qwiic Joystick driver");
MODULE_LICENSE("GPL v2");
