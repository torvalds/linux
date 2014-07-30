/*
 * mcs5000_ts.c - Touchscreen driver for MELFAS MCS-5000 controller
 *
 * Copyright (C) 2009 Samsung Electronics Co.Ltd
 * Author: Joonyoung Shim <jy0922.shim@samsung.com>
 *
 * Based on wm97xx-core.c
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 *
 */

#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/i2c/mcs.h>
#include <linux/interrupt.h>
#include <linux/input.h>
#include <linux/irq.h>
#include <linux/slab.h>

/* Registers */
#define MCS5000_TS_STATUS		0x00
#define STATUS_OFFSET			0
#define STATUS_NO			(0 << STATUS_OFFSET)
#define STATUS_INIT			(1 << STATUS_OFFSET)
#define STATUS_SENSING			(2 << STATUS_OFFSET)
#define STATUS_COORD			(3 << STATUS_OFFSET)
#define STATUS_GESTURE			(4 << STATUS_OFFSET)
#define ERROR_OFFSET			4
#define ERROR_NO			(0 << ERROR_OFFSET)
#define ERROR_POWER_ON_RESET		(1 << ERROR_OFFSET)
#define ERROR_INT_RESET			(2 << ERROR_OFFSET)
#define ERROR_EXT_RESET			(3 << ERROR_OFFSET)
#define ERROR_INVALID_REG_ADDRESS	(8 << ERROR_OFFSET)
#define ERROR_INVALID_REG_VALUE		(9 << ERROR_OFFSET)

#define MCS5000_TS_OP_MODE		0x01
#define RESET_OFFSET			0
#define RESET_NO			(0 << RESET_OFFSET)
#define RESET_EXT_SOFT			(1 << RESET_OFFSET)
#define OP_MODE_OFFSET			1
#define OP_MODE_SLEEP			(0 << OP_MODE_OFFSET)
#define OP_MODE_ACTIVE			(1 << OP_MODE_OFFSET)
#define GESTURE_OFFSET			4
#define GESTURE_DISABLE			(0 << GESTURE_OFFSET)
#define GESTURE_ENABLE			(1 << GESTURE_OFFSET)
#define PROXIMITY_OFFSET		5
#define PROXIMITY_DISABLE		(0 << PROXIMITY_OFFSET)
#define PROXIMITY_ENABLE		(1 << PROXIMITY_OFFSET)
#define SCAN_MODE_OFFSET		6
#define SCAN_MODE_INTERRUPT		(0 << SCAN_MODE_OFFSET)
#define SCAN_MODE_POLLING		(1 << SCAN_MODE_OFFSET)
#define REPORT_RATE_OFFSET		7
#define REPORT_RATE_40			(0 << REPORT_RATE_OFFSET)
#define REPORT_RATE_80			(1 << REPORT_RATE_OFFSET)

#define MCS5000_TS_SENS_CTL		0x02
#define MCS5000_TS_FILTER_CTL		0x03
#define PRI_FILTER_OFFSET		0
#define SEC_FILTER_OFFSET		4

#define MCS5000_TS_X_SIZE_UPPER		0x08
#define MCS5000_TS_X_SIZE_LOWER		0x09
#define MCS5000_TS_Y_SIZE_UPPER		0x0A
#define MCS5000_TS_Y_SIZE_LOWER		0x0B

#define MCS5000_TS_INPUT_INFO		0x10
#define INPUT_TYPE_OFFSET		0
#define INPUT_TYPE_NONTOUCH		(0 << INPUT_TYPE_OFFSET)
#define INPUT_TYPE_SINGLE		(1 << INPUT_TYPE_OFFSET)
#define INPUT_TYPE_DUAL			(2 << INPUT_TYPE_OFFSET)
#define INPUT_TYPE_PALM			(3 << INPUT_TYPE_OFFSET)
#define INPUT_TYPE_PROXIMITY		(7 << INPUT_TYPE_OFFSET)
#define GESTURE_CODE_OFFSET		3
#define GESTURE_CODE_NO			(0 << GESTURE_CODE_OFFSET)

#define MCS5000_TS_X_POS_UPPER		0x11
#define MCS5000_TS_X_POS_LOWER		0x12
#define MCS5000_TS_Y_POS_UPPER		0x13
#define MCS5000_TS_Y_POS_LOWER		0x14
#define MCS5000_TS_Z_POS		0x15
#define MCS5000_TS_WIDTH		0x16
#define MCS5000_TS_GESTURE_VAL		0x17
#define MCS5000_TS_MODULE_REV		0x20
#define MCS5000_TS_FIRMWARE_VER		0x21

/* Touchscreen absolute values */
#define MCS5000_MAX_XC			0x3ff
#define MCS5000_MAX_YC			0x3ff

enum mcs5000_ts_read_offset {
	READ_INPUT_INFO,
	READ_X_POS_UPPER,
	READ_X_POS_LOWER,
	READ_Y_POS_UPPER,
	READ_Y_POS_LOWER,
	READ_BLOCK_SIZE,
};

/* Each client has this additional data */
struct mcs5000_ts_data {
	struct i2c_client *client;
	struct input_dev *input_dev;
	const struct mcs_platform_data *platform_data;
};

static irqreturn_t mcs5000_ts_interrupt(int irq, void *dev_id)
{
	struct mcs5000_ts_data *data = dev_id;
	struct i2c_client *client = data->client;
	u8 buffer[READ_BLOCK_SIZE];
	int err;
	int x;
	int y;

	err = i2c_smbus_read_i2c_block_data(client, MCS5000_TS_INPUT_INFO,
			READ_BLOCK_SIZE, buffer);
	if (err < 0) {
		dev_err(&client->dev, "%s, err[%d]\n", __func__, err);
		goto out;
	}

	switch (buffer[READ_INPUT_INFO]) {
	case INPUT_TYPE_NONTOUCH:
		input_report_key(data->input_dev, BTN_TOUCH, 0);
		input_sync(data->input_dev);
		break;

	case INPUT_TYPE_SINGLE:
		x = (buffer[READ_X_POS_UPPER] << 8) | buffer[READ_X_POS_LOWER];
		y = (buffer[READ_Y_POS_UPPER] << 8) | buffer[READ_Y_POS_LOWER];

		input_report_key(data->input_dev, BTN_TOUCH, 1);
		input_report_abs(data->input_dev, ABS_X, x);
		input_report_abs(data->input_dev, ABS_Y, y);
		input_sync(data->input_dev);
		break;

	case INPUT_TYPE_DUAL:
		/* TODO */
		break;

	case INPUT_TYPE_PALM:
		/* TODO */
		break;

	case INPUT_TYPE_PROXIMITY:
		/* TODO */
		break;

	default:
		dev_err(&client->dev, "Unknown ts input type %d\n",
				buffer[READ_INPUT_INFO]);
		break;
	}

 out:
	return IRQ_HANDLED;
}

static void mcs5000_ts_phys_init(struct mcs5000_ts_data *data,
				 const struct mcs_platform_data *platform_data)
{
	struct i2c_client *client = data->client;

	/* Touch reset & sleep mode */
	i2c_smbus_write_byte_data(client, MCS5000_TS_OP_MODE,
			RESET_EXT_SOFT | OP_MODE_SLEEP);

	/* Touch size */
	i2c_smbus_write_byte_data(client, MCS5000_TS_X_SIZE_UPPER,
			platform_data->x_size >> 8);
	i2c_smbus_write_byte_data(client, MCS5000_TS_X_SIZE_LOWER,
			platform_data->x_size & 0xff);
	i2c_smbus_write_byte_data(client, MCS5000_TS_Y_SIZE_UPPER,
			platform_data->y_size >> 8);
	i2c_smbus_write_byte_data(client, MCS5000_TS_Y_SIZE_LOWER,
			platform_data->y_size & 0xff);

	/* Touch active mode & 80 report rate */
	i2c_smbus_write_byte_data(data->client, MCS5000_TS_OP_MODE,
			OP_MODE_ACTIVE | REPORT_RATE_80);
}

static int mcs5000_ts_probe(struct i2c_client *client,
			    const struct i2c_device_id *id)
{
	const struct mcs_platform_data *pdata;
	struct mcs5000_ts_data *data;
	struct input_dev *input_dev;
	int error;

	pdata = dev_get_platdata(&client->dev);
	if (!pdata)
		return -EINVAL;

	data = devm_kzalloc(&client->dev, sizeof(*data), GFP_KERNEL);
	if (!data) {
		dev_err(&client->dev, "Failed to allocate memory\n");
		return -ENOMEM;
	}

	data->client = client;

	input_dev = devm_input_allocate_device(&client->dev);
	if (!input_dev) {
		dev_err(&client->dev, "Failed to allocate input device\n");
		return -ENOMEM;
	}

	input_dev->name = "MELFAS MCS-5000 Touchscreen";
	input_dev->id.bustype = BUS_I2C;
	input_dev->dev.parent = &client->dev;

	__set_bit(EV_ABS, input_dev->evbit);
	__set_bit(EV_KEY, input_dev->evbit);
	__set_bit(BTN_TOUCH, input_dev->keybit);
	input_set_abs_params(input_dev, ABS_X, 0, MCS5000_MAX_XC, 0, 0);
	input_set_abs_params(input_dev, ABS_Y, 0, MCS5000_MAX_YC, 0, 0);

	input_set_drvdata(input_dev, data);
	data->input_dev = input_dev;

	if (pdata->cfg_pin)
		pdata->cfg_pin();

	error = devm_request_threaded_irq(&client->dev, client->irq,
					  NULL, mcs5000_ts_interrupt,
					  IRQF_TRIGGER_LOW | IRQF_ONESHOT,
					  "mcs5000_ts", data);
	if (error) {
		dev_err(&client->dev, "Failed to register interrupt\n");
		return error;
	}

	error = input_register_device(data->input_dev);
	if (error) {
		dev_err(&client->dev, "Failed to register input device\n");
		return error;
	}

	mcs5000_ts_phys_init(data, pdata);
	i2c_set_clientdata(client, data);

	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int mcs5000_ts_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);

	/* Touch sleep mode */
	i2c_smbus_write_byte_data(client, MCS5000_TS_OP_MODE, OP_MODE_SLEEP);

	return 0;
}

static int mcs5000_ts_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct mcs5000_ts_data *data = i2c_get_clientdata(client);
	const struct mcs_platform_data *pdata = dev_get_platdata(dev);

	mcs5000_ts_phys_init(data, pdata);

	return 0;
}
#endif

static SIMPLE_DEV_PM_OPS(mcs5000_ts_pm, mcs5000_ts_suspend, mcs5000_ts_resume);

static const struct i2c_device_id mcs5000_ts_id[] = {
	{ "mcs5000_ts", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, mcs5000_ts_id);

static struct i2c_driver mcs5000_ts_driver = {
	.probe		= mcs5000_ts_probe,
	.driver = {
		.name = "mcs5000_ts",
		.pm   = &mcs5000_ts_pm,
	},
	.id_table	= mcs5000_ts_id,
};

module_i2c_driver(mcs5000_ts_driver);

/* Module information */
MODULE_AUTHOR("Joonyoung Shim <jy0922.shim@samsung.com>");
MODULE_DESCRIPTION("Touchscreen driver for MELFAS MCS-5000 controller");
MODULE_LICENSE("GPL");
