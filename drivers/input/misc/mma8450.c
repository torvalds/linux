/*
 *  Driver for Freescale's 3-Axis Accelerometer MMA8450
 *
 *  Copyright (C) 2011 Freescale Semiconductor, Inc. All Rights Reserved.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/input-polldev.h>
#include <linux/of_device.h>

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

/* mma8450 status */
struct mma8450 {
	struct i2c_client	*client;
	struct input_polled_dev	*idev;
};

static int mma8450_read(struct mma8450 *m, unsigned off)
{
	struct i2c_client *c = m->client;
	int ret;

	ret = i2c_smbus_read_byte_data(c, off);
	if (ret < 0)
		dev_err(&c->dev,
			"failed to read register 0x%02x, error %d\n",
			off, ret);

	return ret;
}

static int mma8450_write(struct mma8450 *m, unsigned off, u8 v)
{
	struct i2c_client *c = m->client;
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

static int mma8450_read_block(struct mma8450 *m, unsigned off,
			      u8 *buf, size_t size)
{
	struct i2c_client *c = m->client;
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

static void mma8450_poll(struct input_polled_dev *dev)
{
	struct mma8450 *m = dev->private;
	int x, y, z;
	int ret;
	u8 buf[6];

	ret = mma8450_read(m, MMA8450_STATUS);
	if (ret < 0)
		return;

	if (!(ret & MMA8450_STATUS_ZXYDR))
		return;

	ret = mma8450_read_block(m, MMA8450_OUT_X_LSB, buf, sizeof(buf));
	if (ret < 0)
		return;

	x = ((buf[1] << 4) & 0xff0) | (buf[0] & 0xf);
	y = ((buf[3] << 4) & 0xff0) | (buf[2] & 0xf);
	z = ((buf[5] << 4) & 0xff0) | (buf[4] & 0xf);

	input_report_abs(dev->input, ABS_X, x);
	input_report_abs(dev->input, ABS_Y, y);
	input_report_abs(dev->input, ABS_Z, z);
	input_sync(dev->input);
}

/* Initialize the MMA8450 chip */
static void mma8450_open(struct input_polled_dev *dev)
{
	struct mma8450 *m = dev->private;
	int err;

	/* enable all events from X/Y/Z, no FIFO */
	err = mma8450_write(m, MMA8450_XYZ_DATA_CFG, 0x07);
	if (err)
		return;

	/*
	 * Sleep mode poll rate - 50Hz
	 * System output data rate - 400Hz
	 * Full scale selection - Active, +/- 2G
	 */
	err = mma8450_write(m, MMA8450_CTRL_REG1, 0x01);
	if (err < 0)
		return;

	msleep(MODE_CHANGE_DELAY_MS);
}

static void mma8450_close(struct input_polled_dev *dev)
{
	struct mma8450 *m = dev->private;

	mma8450_write(m, MMA8450_CTRL_REG1, 0x00);
	mma8450_write(m, MMA8450_CTRL_REG2, 0x01);
}

/*
 * I2C init/probing/exit functions
 */
static int __devinit mma8450_probe(struct i2c_client *c,
				   const struct i2c_device_id *id)
{
	struct input_polled_dev *idev;
	struct mma8450 *m;
	int err;

	m = kzalloc(sizeof(struct mma8450), GFP_KERNEL);
	idev = input_allocate_polled_device();
	if (!m || !idev) {
		err = -ENOMEM;
		goto err_free_mem;
	}

	m->client = c;
	m->idev = idev;

	idev->private		= m;
	idev->input->name	= MMA8450_DRV_NAME;
	idev->input->id.bustype	= BUS_I2C;
	idev->poll		= mma8450_poll;
	idev->poll_interval	= POLL_INTERVAL;
	idev->poll_interval_max	= POLL_INTERVAL_MAX;
	idev->open		= mma8450_open;
	idev->close		= mma8450_close;

	__set_bit(EV_ABS, idev->input->evbit);
	input_set_abs_params(idev->input, ABS_X, -2048, 2047, 32, 32);
	input_set_abs_params(idev->input, ABS_Y, -2048, 2047, 32, 32);
	input_set_abs_params(idev->input, ABS_Z, -2048, 2047, 32, 32);

	err = input_register_polled_device(idev);
	if (err) {
		dev_err(&c->dev, "failed to register polled input device\n");
		goto err_free_mem;
	}

	return 0;

err_free_mem:
	input_free_polled_device(idev);
	kfree(m);
	return err;
}

static int __devexit mma8450_remove(struct i2c_client *c)
{
	struct mma8450 *m = i2c_get_clientdata(c);
	struct input_polled_dev *idev = m->idev;

	input_unregister_polled_device(idev);
	input_free_polled_device(idev);
	kfree(m);

	return 0;
}

static const struct i2c_device_id mma8450_id[] = {
	{ MMA8450_DRV_NAME, 0 },
	{ },
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
		.owner	= THIS_MODULE,
		.of_match_table = mma8450_dt_ids,
	},
	.probe		= mma8450_probe,
	.remove		= mma8450_remove,
	.id_table	= mma8450_id,
};

module_i2c_driver(mma8450_driver);

MODULE_AUTHOR("Freescale Semiconductor, Inc.");
MODULE_DESCRIPTION("MMA8450 3-Axis Accelerometer Driver");
MODULE_LICENSE("GPL");
