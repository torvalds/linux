/*
 *  mma8451.c - Linux kernel modules for 3-Axis Orientation/Motion
 *  Detection Sensor
 *
 *  Copyright (C) 2010-2014 Freescale Semiconductor, Inc. All Rights Reserved.
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

#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/pm.h>
#include <linux/mutex.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/hwmon-sysfs.h>
#include <linux/err.h>
#include <linux/hwmon.h>
#include <linux/input-polldev.h>
#include <linux/of.h>
#include <linux/regulator/consumer.h>

#define MMA8451_I2C_ADDR	0x1C
#define MMA8451_ID		0x1A
#define MMA8452_ID		0x2A
#define MMA8453_ID		0x3A

#define POLL_INTERVAL_MIN	1
#define POLL_INTERVAL_MAX	500
#define POLL_INTERVAL		100	/* msecs */
#define INPUT_FUZZ		32
#define INPUT_FLAT		32
#define MODE_CHANGE_DELAY_MS	100

#define MMA8451_STATUS_ZYXDR	0x08
#define MMA8451_BUF_SIZE	7
#define DEFAULT_POSITION	0

/* register enum for mma8451 registers */
enum {
	MMA8451_STATUS = 0x00,
	MMA8451_OUT_X_MSB,
	MMA8451_OUT_X_LSB,
	MMA8451_OUT_Y_MSB,
	MMA8451_OUT_Y_LSB,
	MMA8451_OUT_Z_MSB,
	MMA8451_OUT_Z_LSB,

	MMA8451_F_SETUP = 0x09,
	MMA8451_TRIG_CFG,
	MMA8451_SYSMOD,
	MMA8451_INT_SOURCE,
	MMA8451_WHO_AM_I,
	MMA8451_XYZ_DATA_CFG,
	MMA8451_HP_FILTER_CUTOFF,

	MMA8451_PL_STATUS,
	MMA8451_PL_CFG,
	MMA8451_PL_COUNT,
	MMA8451_PL_BF_ZCOMP,
	MMA8451_P_L_THS_REG,

	MMA8451_FF_MT_CFG,
	MMA8451_FF_MT_SRC,
	MMA8451_FF_MT_THS,
	MMA8451_FF_MT_COUNT,

	MMA8451_TRANSIENT_CFG = 0x1D,
	MMA8451_TRANSIENT_SRC,
	MMA8451_TRANSIENT_THS,
	MMA8451_TRANSIENT_COUNT,

	MMA8451_PULSE_CFG,
	MMA8451_PULSE_SRC,
	MMA8451_PULSE_THSX,
	MMA8451_PULSE_THSY,
	MMA8451_PULSE_THSZ,
	MMA8451_PULSE_TMLT,
	MMA8451_PULSE_LTCY,
	MMA8451_PULSE_WIND,

	MMA8451_ASLP_COUNT,
	MMA8451_CTRL_REG1,
	MMA8451_CTRL_REG2,
	MMA8451_CTRL_REG3,
	MMA8451_CTRL_REG4,
	MMA8451_CTRL_REG5,

	MMA8451_OFF_X,
	MMA8451_OFF_Y,
	MMA8451_OFF_Z,

	MMA8451_REG_END,
};

/* The sensitivity is represented in counts/g. In 2g mode the
sensitivity is 1024 counts/g. In 4g mode the sensitivity is 512
counts/g and in 8g mode the sensitivity is 256 counts/g.
 */
enum {
	MODE_2G = 0,
	MODE_4G,
	MODE_8G,
};

enum {
	MMA_STANDBY = 0,
	MMA_ACTIVED,
};

/* mma8451 status */
struct mma8451_status {
	u8 mode;
	u8 ctl_reg1;
	int active;
	int position;
};

static struct mma8451_status mma_status;
static struct input_polled_dev *mma8451_idev;
static struct device *hwmon_dev;
static struct i2c_client *mma8451_i2c_client;

static int senstive_mode = MODE_2G;
static int ACCHAL[8][3][3] = {
	{ {0, -1, 0}, {1, 0, 0}, {0, 0, 1} },
	{ {-1, 0, 0}, {0, -1, 0}, {0, 0, 1} },
	{ {0, 1, 0}, {-1, 0, 0}, {0, 0, 1} },
	{ {1, 0, 0}, {0, 1, 0}, {0, 0, 1} },

	{ {0, -1, 0}, {-1, 0, 0}, {0, 0, -1} },
	{ {-1, 0, 0}, {0, 1, 0}, {0, 0, -1} },
	{ {0, 1, 0}, {1, 0, 0}, {0, 0, -1} },
	{ {1, 0, 0}, {0, -1, 0}, {0, 0, -1} },
};

static DEFINE_MUTEX(mma8451_lock);
static int mma8451_adjust_position(short *x, short *y, short *z)
{
	short rawdata[3], data[3];
	int i, j;
	int position = mma_status.position;
	if (position < 0 || position > 7)
		position = 0;
	rawdata[0] = *x;
	rawdata[1] = *y;
	rawdata[2] = *z;
	for (i = 0; i < 3; i++) {
		data[i] = 0;
		for (j = 0; j < 3; j++)
			data[i] += rawdata[j] * ACCHAL[position][i][j];
	}
	*x = data[0];
	*y = data[1];
	*z = data[2];
	return 0;
}

static int mma8451_change_mode(struct i2c_client *client, int mode)
{
	int result;

	mma_status.ctl_reg1 = 0;
	result = i2c_smbus_write_byte_data(client, MMA8451_CTRL_REG1, 0);
	if (result < 0)
		goto out;
	mma_status.active = MMA_STANDBY;

	result = i2c_smbus_write_byte_data(client, MMA8451_XYZ_DATA_CFG,
					   mode);
	if (result < 0)
		goto out;
	mdelay(MODE_CHANGE_DELAY_MS);
	mma_status.mode = mode;

	return 0;
out:
	dev_err(&client->dev, "error when init mma8451:(%d)", result);
	return result;
}

static int mma8451_read_data(short *x, short *y, short *z)
{
	u8 tmp_data[MMA8451_BUF_SIZE];
	int ret;

	ret = i2c_smbus_read_i2c_block_data(mma8451_i2c_client,
					    MMA8451_OUT_X_MSB, 7, tmp_data);
	if (ret < MMA8451_BUF_SIZE) {
		dev_err(&mma8451_i2c_client->dev, "i2c block read failed\n");
		return -EIO;
	}

	*x = ((tmp_data[0] << 8) & 0xff00) | tmp_data[1];
	*y = ((tmp_data[2] << 8) & 0xff00) | tmp_data[3];
	*z = ((tmp_data[4] << 8) & 0xff00) | tmp_data[5];
	return 0;
}

static void report_abs(void)
{
	short x, y, z;
	int result;
	int retry = 3;

	mutex_lock(&mma8451_lock);
	if (mma_status.active == MMA_STANDBY)
		goto out;
	/* wait for the data ready */
	do {
		result = i2c_smbus_read_byte_data(mma8451_i2c_client,
						  MMA8451_STATUS);
		retry--;
		msleep(1);
	} while (!(result & MMA8451_STATUS_ZYXDR) && retry > 0);
	if (retry == 0)
		goto out;
	if (mma8451_read_data(&x, &y, &z) != 0)
		goto out;
	mma8451_adjust_position(&x, &y, &z);
	input_report_abs(mma8451_idev->input, ABS_X, x);
	input_report_abs(mma8451_idev->input, ABS_Y, y);
	input_report_abs(mma8451_idev->input, ABS_Z, z);
	input_sync(mma8451_idev->input);
out:
	mutex_unlock(&mma8451_lock);
}

static void mma8451_dev_poll(struct input_polled_dev *dev)
{
	report_abs();
}

static ssize_t mma8451_enable_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	struct i2c_client *client;
	u8 val;
	int enable;

	mutex_lock(&mma8451_lock);
	client = mma8451_i2c_client;
	val = i2c_smbus_read_byte_data(client, MMA8451_CTRL_REG1);
	if ((val & 0x01) && mma_status.active == MMA_ACTIVED)
		enable = 1;
	else
		enable = 0;
	mutex_unlock(&mma8451_lock);
	return sprintf(buf, "%d\n", enable);
}

static ssize_t mma8451_enable_store(struct device *dev,
				    struct device_attribute *attr,
				    const char *buf, size_t count)
{
	struct i2c_client *client;
	int ret;
	unsigned long enable;
	u8 val = 0;

	ret = kstrtoul(buf, 10, &enable);
	if (ret) {
		dev_err(dev, "string transform error\n");
		return ret;
	}

	mutex_lock(&mma8451_lock);
	client = mma8451_i2c_client;
	enable = (enable > 0) ? 1 : 0;
	if (enable && mma_status.active == MMA_STANDBY) {
		val = i2c_smbus_read_byte_data(client, MMA8451_CTRL_REG1);
		ret =
		    i2c_smbus_write_byte_data(client, MMA8451_CTRL_REG1,
					      val | 0x01);
		if (!ret)
			mma_status.active = MMA_ACTIVED;

	} else if (enable == 0 && mma_status.active == MMA_ACTIVED) {
		val = i2c_smbus_read_byte_data(client, MMA8451_CTRL_REG1);
		ret =
		    i2c_smbus_write_byte_data(client, MMA8451_CTRL_REG1,
					      val & 0xFE);
		if (!ret)
			mma_status.active = MMA_STANDBY;

	}
	mutex_unlock(&mma8451_lock);
	return count;
}

static ssize_t mma8451_position_show(struct device *dev,
				     struct device_attribute *attr, char *buf)
{
	int position = 0;
	mutex_lock(&mma8451_lock);
	position = mma_status.position;
	mutex_unlock(&mma8451_lock);
	return sprintf(buf, "%d\n", position);
}

static ssize_t mma8451_position_store(struct device *dev,
				      struct device_attribute *attr,
				      const char *buf, size_t count)
{
	unsigned long  position;
	int ret;
	ret = kstrtoul(buf, 10, &position);
	if (ret) {
		dev_err(dev, "string transform error\n");
		return ret;
	}

	mutex_lock(&mma8451_lock);
	mma_status.position = (int)position;
	mutex_unlock(&mma8451_lock);
	return count;
}

static ssize_t mma8451_scalemode_show(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	int mode = 0;
	mutex_lock(&mma8451_lock);
	mode = (int)mma_status.mode;
	mutex_unlock(&mma8451_lock);

	return sprintf(buf, "%d\n", mode);
}

static ssize_t mma8451_scalemode_store(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t count)
{
	unsigned long  mode;
	int ret, active_save;
	struct i2c_client *client = mma8451_i2c_client;

	ret = kstrtoul(buf, 10, &mode);
	if (ret) {
		dev_err(dev, "string transform error\n");
		goto out;
	}

	if (mode > MODE_8G) {
		dev_warn(dev, "not supported mode\n");
		ret = count;
		goto out;
	}

	mutex_lock(&mma8451_lock);
	if (mode == mma_status.mode) {
		ret = count;
		goto out_unlock;
	}

	active_save = mma_status.active;
	ret = mma8451_change_mode(client, mode);
	if (ret)
		goto out_unlock;

	if (active_save == MMA_ACTIVED) {
		ret = i2c_smbus_write_byte_data(client, MMA8451_CTRL_REG1, 1);

		if (ret)
			goto out_unlock;
		mma_status.active = active_save;
	}

out_unlock:
	mutex_unlock(&mma8451_lock);
out:
	return ret;
}

static DEVICE_ATTR(enable, S_IWUSR | S_IRUGO,
			mma8451_enable_show, mma8451_enable_store);
static DEVICE_ATTR(position, S_IWUSR | S_IRUGO,
			mma8451_position_show, mma8451_position_store);
static DEVICE_ATTR(scalemode, S_IWUSR | S_IRUGO,
			mma8451_scalemode_show, mma8451_scalemode_store);

static struct attribute *mma8451_attributes[] = {
	&dev_attr_enable.attr,
	&dev_attr_position.attr,
	&dev_attr_scalemode.attr,
	NULL
};

static const struct attribute_group mma8451_attr_group = {
	.attrs = mma8451_attributes,
};

static int mma8451_probe(struct i2c_client *client,
				   const struct i2c_device_id *id)
{
	int result, client_id;
	struct input_dev *idev;
	struct i2c_adapter *adapter;
	u32 pos;
	struct device_node *of_node = client->dev.of_node;
	struct regulator *vdd, *vdd_io;

	mma8451_i2c_client = client;

	vdd = devm_regulator_get(&client->dev, "vdd");
	if (!IS_ERR(vdd)) {
		result = regulator_enable(vdd);
		if (result) {
			dev_err(&client->dev, "vdd set voltage error\n");
			return result;
		}
	}

	vdd_io = devm_regulator_get(&client->dev, "vddio");
	if (!IS_ERR(vdd_io)) {
		result = regulator_enable(vdd_io);
		if (result) {
			dev_err(&client->dev, "vddio set voltage error\n");
			return result;
		}
	}

	adapter = to_i2c_adapter(client->dev.parent);
	result = i2c_check_functionality(adapter,
					 I2C_FUNC_SMBUS_BYTE |
					 I2C_FUNC_SMBUS_BYTE_DATA);
	if (!result)
		goto err_out;

	client_id = i2c_smbus_read_byte_data(client, MMA8451_WHO_AM_I);
	if (client_id != MMA8451_ID && client_id != MMA8452_ID
	    && client_id != MMA8453_ID) {
		dev_err(&client->dev,
			"read chip ID 0x%x is not equal to 0x%x or 0x%x!\n",
			result, MMA8451_ID, MMA8452_ID);
		result = -EINVAL;
		goto err_out;
	}

	/* Initialize the MMA8451 chip */
	result = mma8451_change_mode(client, senstive_mode);
	if (result) {
		dev_err(&client->dev,
			"error when init mma8451 chip:(%d)\n", result);
		goto err_out;
	}

	hwmon_dev = hwmon_device_register(&client->dev);
	if (!hwmon_dev) {
		result = -ENOMEM;
		dev_err(&client->dev, "error when register hwmon device\n");
		goto err_out;
	}

	mma8451_idev = input_allocate_polled_device();
	if (!mma8451_idev) {
		result = -ENOMEM;
		dev_err(&client->dev, "alloc poll device failed!\n");
		goto err_alloc_poll_device;
	}
	mma8451_idev->poll = mma8451_dev_poll;
	mma8451_idev->poll_interval = POLL_INTERVAL;
	mma8451_idev->poll_interval_min = POLL_INTERVAL_MIN;
	mma8451_idev->poll_interval_max = POLL_INTERVAL_MAX;
	idev = mma8451_idev->input;
	idev->name = "mma845x";
	idev->id.bustype = BUS_I2C;
	idev->evbit[0] = BIT_MASK(EV_ABS);

	input_set_abs_params(idev, ABS_X, -8192, 8191, INPUT_FUZZ, INPUT_FLAT);
	input_set_abs_params(idev, ABS_Y, -8192, 8191, INPUT_FUZZ, INPUT_FLAT);
	input_set_abs_params(idev, ABS_Z, -8192, 8191, INPUT_FUZZ, INPUT_FLAT);

	result = input_register_polled_device(mma8451_idev);
	if (result) {
		dev_err(&client->dev, "register poll device failed!\n");
		goto err_register_polled_device;
	}
	result = sysfs_create_group(&idev->dev.kobj, &mma8451_attr_group);
	if (result) {
		dev_err(&client->dev, "create device file failed!\n");
		result = -EINVAL;
		goto err_register_polled_device;
	}

	result = of_property_read_u32(of_node, "position", &pos);
	if (result)
		pos = DEFAULT_POSITION;
	mma_status.position = (int)pos;

	return 0;
err_register_polled_device:
	input_free_polled_device(mma8451_idev);
err_alloc_poll_device:
	hwmon_device_unregister(&client->dev);
err_out:
	return result;
}

static int mma8451_stop_chip(struct i2c_client *client)
{
	int ret = 0;
	if (mma_status.active == MMA_ACTIVED) {
		mma_status.ctl_reg1 = i2c_smbus_read_byte_data(client,
							       MMA8451_CTRL_REG1);
		ret = i2c_smbus_write_byte_data(client, MMA8451_CTRL_REG1,
						mma_status.ctl_reg1 & 0xFE);
	}
	return ret;
}

static int mma8451_remove(struct i2c_client *client)
{
	int ret;
	ret = mma8451_stop_chip(client);
	hwmon_device_unregister(hwmon_dev);

	return ret;
}

#ifdef CONFIG_PM_SLEEP
static int mma8451_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);

	return mma8451_stop_chip(client);
}

static int mma8451_resume(struct device *dev)
{
	int ret = 0;
	struct i2c_client *client = to_i2c_client(dev);
	if (mma_status.active == MMA_ACTIVED)
		ret = i2c_smbus_write_byte_data(client, MMA8451_CTRL_REG1,
						mma_status.ctl_reg1);
	return ret;

}
#endif

static const struct i2c_device_id mma8451_id[] = {
	{"mma8451", 0},
};

MODULE_DEVICE_TABLE(i2c, mma8451_id);

static SIMPLE_DEV_PM_OPS(mma8451_pm_ops, mma8451_suspend, mma8451_resume);
static struct i2c_driver mma8451_driver = {
	.driver = {
		   .name = "mma8451",
		   .owner = THIS_MODULE,
		   .pm = &mma8451_pm_ops,
		   },
	.probe = mma8451_probe,
	.remove = mma8451_remove,
	.id_table = mma8451_id,
};

static int __init mma8451_init(void)
{
	/* register driver */
	int res;

	res = i2c_add_driver(&mma8451_driver);
	if (res < 0) {
		printk(KERN_INFO "add mma8451 i2c driver failed\n");
		return -ENODEV;
	}
	return res;
}

static void __exit mma8451_exit(void)
{
	i2c_del_driver(&mma8451_driver);
}

MODULE_AUTHOR("Freescale Semiconductor, Inc.");
MODULE_DESCRIPTION("MMA8451 3-Axis Orientation/Motion Detection Sensor driver");
MODULE_LICENSE("GPL");

module_init(mma8451_init);
module_exit(mma8451_exit);
