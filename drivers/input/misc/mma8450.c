// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  Driver for Freescale's 3-Axis Accelerometer MMA8450
 *
 *  Copyright (C) 2011 Freescale Semiconductor, Inc. All Rights Reserved.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/input.h>
#include <linux/mod_devicetable.h>

#define MMA8450_DRV_NAME	"mma8450"

#define MODE_CHANGE_DELAY_MS	100
#define POLL_INTERVAL		100
#define POLL_INTERVAL_MAX	500

/* register definitions */
#define MMA8450_STATUS		0x00
#define MMA8450_STATUS_ZXYDR	0x08

#define MMA8450_OUT_X8		0x01
#define MMA8450_OUT_Y8		0x02
#define MMA8450_OUT_Z8		0x03

#define MMA8450_OUT_X_LSB	0x05
#define MMA8450_OUT_X_MSB	0x06
#define MMA8450_OUT_Y_LSB	0x07
#define MMA8450_OUT_Y_MSB	0x08
#define MMA8450_OUT_Z_LSB	0x09
#define MMA8450_OUT_Z_MSB	0x0a

#define MMA8450_XYZ_DATA_CFG	0x16

#define MMA8450_CTRL_REG1	0x38
#define MMA8450_CTRL_REG2	0x39

static int mma8450_read(struct i2c_client *c, unsigned int off)
{
	int ret;

	ret = i2c_smbus_read_byte_data(c, off);
	if (ret < 0)
		dev_err(&c->dev,
			"failed to read register 0x%02x, error %d\n",
			off, ret);

	return ret;
}

static int mma8450_write(struct i2c_client *c, unsigned int off, u8 v)
{
	int error;

	error = i2c_smbus_write_byte_data(c, off, v);
	if (error < 0) {
		dev_err(&c->dev,
			"failed to write to register 0x%02x, error %d\n",
			off, error);
		return error;
	}

	return 0;
}

static int mma8450_read_block(struct i2c_client *c, unsigned int off,
			      u8 *buf, size_t size)
{
	int err;

	err = i2c_smbus_read_i2c_block_data(c, off, size, buf);
	if (err < 0) {
		dev_err(&c->dev,
			"failed to read block data at 0x%02x, error %d\n",
			MMA8450_OUT_X_LSB, err);
		return err;
	}

	return 0;
}

static void mma8450_poll(struct input_dev *input)
{
	struct i2c_client *c = input_get_drvdata(input);
	int x, y, z;
	int ret;
	u8 buf[6];

	ret = mma8450_read(c, MMA8450_STATUS);
	if (ret < 0)
		return;

	if (!(ret & MMA8450_STATUS_ZXYDR))
		return;

	ret = mma8450_read_block(c, MMA8450_OUT_X_LSB, buf, sizeof(buf));
	if (ret < 0)
		return;

	x = ((int)(s8)buf[1] << 4) | (buf[0] & 0xf);
	y = ((int)(s8)buf[3] << 4) | (buf[2] & 0xf);
	z = ((int)(s8)buf[5] << 4) | (buf[4] & 0xf);

	input_report_abs(input, ABS_X, x);
	input_report_abs(input, ABS_Y, y);
	input_report_abs(input, ABS_Z, z);
	input_sync(input);
}

/* Initialize the MMA8450 chip */
static int mma8450_open(struct input_dev *input)
{
	struct i2c_client *c = input_get_drvdata(input);
	int err;

	/* enable all events from X/Y/Z, no FIFO */
	err = mma8450_write(c, MMA8450_XYZ_DATA_CFG, 0x07);
	if (err)
		return err;

	/*
	 * Sleep mode poll rate - 50Hz
	 * System output data rate - 400Hz
	 * Full scale selection - Active, +/- 2G
	 */
	err = mma8450_write(c, MMA8450_CTRL_REG1, 0x01);
	if (err)
		return err;

	msleep(MODE_CHANGE_DELAY_MS);
	return 0;
}

static void mma8450_close(struct input_dev *input)
{
	struct i2c_client *c = input_get_drvdata(input);

	mma8450_write(c, MMA8450_CTRL_REG1, 0x00);
	mma8450_write(c, MMA8450_CTRL_REG2, 0x01);
}

/*
 * I2C init/probing/exit functions
 */
static int mma8450_probe(struct i2c_client *c)
{
	struct input_dev *input;
	int err;

	input = devm_input_allocate_device(&c->dev);
	if (!input)
		return -ENOMEM;

	input_set_drvdata(input, c);

	input->name = MMA8450_DRV_NAME;
	input->id.bustype = BUS_I2C;

	input->open = mma8450_open;
	input->close = mma8450_close;

	input_set_abs_params(input, ABS_X, -2048, 2047, 32, 32);
	input_set_abs_params(input, ABS_Y, -2048, 2047, 32, 32);
	input_set_abs_params(input, ABS_Z, -2048, 2047, 32, 32);

	err = input_setup_polling(input, mma8450_poll);
	if (err) {
		dev_err(&c->dev, "failed to set up polling\n");
		return err;
	}

	input_set_poll_interval(input, POLL_INTERVAL);
	input_set_max_poll_interval(input, POLL_INTERVAL_MAX);

	err = input_register_device(input);
	if (err) {
		dev_err(&c->dev, "failed to register input device\n");
		return err;
	}

	return 0;
}

static const struct i2c_device_id mma8450_id[] = {
	{ MMA8450_DRV_NAME },
	{ }
};
MODULE_DEVICE_TABLE(i2c, mma8450_id);

static const struct of_device_id mma8450_dt_ids[] = {
	{ .compatible = "fsl,mma8450", },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, mma8450_dt_ids);

static struct i2c_driver mma8450_driver = {
	.driver = {
		.name	= MMA8450_DRV_NAME,
		.of_match_table = mma8450_dt_ids,
	},
	.probe		= mma8450_probe,
	.id_table	= mma8450_id,
};

module_i2c_driver(mma8450_driver);

MODULE_AUTHOR("Freescale Semiconductor, Inc.");
MODULE_DESCRIPTION("MMA8450 3-Axis Accelerometer Driver");
MODULE_LICENSE("GPL");
