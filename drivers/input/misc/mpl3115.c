/*
 *  Copyright (C) 2010-2015 Freescale , Inc. All Rights Reserved.
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

#define MPL3115_DRV_NAME			"mpl3115"
#define ABS_TEMPTERAURE				ABS_MISC

#define INPUT_FUZZ				32
#define INPUT_FLAT				32
#define MPL_ACTIVED    		 		1
#define MPL_STANDBY 				0
#define POLL_INTERVAL_MAX			500
#define POLL_INTERVAL				100
#define POLL_INTERVAL_MIN			1
#define MPL3115_ID				0xc4
#define MPLL_ACTIVE_MASK       			0x01
#define MPL3115_STATUS_DR			0x08

/* MPL3115A register address */
#define MPL3115_STATUS				0x00
#define MPL3115_PRESSURE_DATA			0x01
#define MPL3115_DR_STATUS			0x06
#define MPL3115_DELTA_DATA			0x07
#define MPL3115_WHO_AM_I			0x0c
#define MPL3115_FIFO_STATUS 			0x0d
#define MPL3115_FIFO_DATA 			0x0e
#define MPL3115_FIFO_SETUP 			0x0e
#define MPL3115_TIME_DELAY 			0x10
#define MPL3115_SYS_MODE 			0x11
#define MPL3115_INT_SORCE 			0x12
#define MPL3115_PT_DATA_CFG 			0x13
#define MPL3115_BAR_IN_MSB			0x14
#define MPL3115_P_ARLARM_MSB			0x16
#define MPL3115_T_ARLARM			0x18
#define MPL3115_P_ARLARM_WND_MSB		0x19
#define MPL3115_T_ARLARM_WND			0x1b
#define MPL3115_P_MIN_DATA			0x1c
#define MPL3115_T_MIN_DATA			0x1f
#define MPL3115_P_MAX_DATA			0x21
#define MPL3115_T_MAX_DATA			0x24
#define MPL3115_CTRL_REG1			0x26
#define MPL3115_CTRL_REG2			0x27
#define MPL3115_CTRL_REG3			0x28
#define MPL3115_CTRL_REG4			0x29
#define MPL3115_CTRL_REG5			0x2a
#define MPL3115_OFFSET_P			0x2b
#define MPL3115_OFFSET_T			0x2c
#define MPL3115_OFFSET_H			0x2d

#define DATA_SHIFT_BIT(data, bit)     ((data << bit) & (0xff << bit))

struct mpl3115_data {
	struct i2c_client *client;
	struct input_polled_dev *poll_dev;
	struct mutex data_lock;
	int active;
};

static int mpl3115_i2c_read(struct i2c_client *client, u8 reg, u8 *values, u8 length)
{
	return i2c_smbus_read_i2c_block_data(client, reg, length, values);
}

static int mpl3115_i2c_write(struct i2c_client *client, u8 reg, const u8 *values, u8 length)
{
	return i2c_smbus_write_i2c_block_data(client, reg, length, values);
}

static ssize_t mpl3115_enable_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	int val;
	u8 sysmode;

	struct input_polled_dev *poll_dev = dev_get_drvdata(dev);
	struct mpl3115_data *pdata = (struct mpl3115_data *)(poll_dev->private);
	struct i2c_client *client = pdata->client;
	mutex_lock(&pdata->data_lock);
	mpl3115_i2c_read(client, MPL3115_CTRL_REG1, &sysmode, 1);
	sysmode &= MPLL_ACTIVE_MASK;
	val = (sysmode ? 1 : 0);
	mutex_unlock(&pdata->data_lock);

	return sprintf(buf, "%d\n", val);
}

static ssize_t mpl3115_enable_store(struct device *dev,
				    struct device_attribute *attr,
				    const char *buf, size_t count)
{
	int ret, enable;
	u8 val;
	struct input_polled_dev *poll_dev = dev_get_drvdata(dev);
	struct mpl3115_data *pdata = (struct mpl3115_data *)(poll_dev->private);
	struct i2c_client *client = pdata->client;

	enable = simple_strtoul(buf, NULL, 10);
	mutex_lock(&pdata->data_lock);
	mpl3115_i2c_read(client, MPL3115_CTRL_REG1, &val, 1);
	if (enable && pdata->active == MPL_STANDBY) {
		val |= MPLL_ACTIVE_MASK;
		ret = mpl3115_i2c_write(client, MPL3115_CTRL_REG1, &val, 1);
		if (!ret)
			pdata->active = MPL_ACTIVED;
		printk("mpl3115 set active\n");
	} else if (!enable && pdata->active == MPL_ACTIVED) {
		val &= ~MPLL_ACTIVE_MASK;
		ret = mpl3115_i2c_write(client, MPL3115_CTRL_REG1, &val, 1);
		if (!ret)
			pdata->active = MPL_STANDBY;
		printk("mpl3115 set inactive\n");
	}
	mutex_unlock(&pdata->data_lock);

	return count;
}

static DEVICE_ATTR(enable, S_IWUSR | S_IRUGO, mpl3115_enable_show, mpl3115_enable_store);

static struct attribute *mpl3115_attributes[] = {
	&dev_attr_enable.attr,
	NULL
};

static const struct attribute_group mpl3115_attr_group = {
	.attrs = mpl3115_attributes,
};

static void mpl3115_device_init(struct i2c_client *client)
{
	u8 val[8];

	val[0] = 0x28;
	mpl3115_i2c_write(client, MPL3115_CTRL_REG1, val, 1);
}

static int mpl3115_read_data(struct i2c_client *client, int *pres, short *temp)
{
	u8 tmp[5];

	mpl3115_i2c_read(client, MPL3115_PRESSURE_DATA, tmp, 5);
	*pres = (DATA_SHIFT_BIT(tmp[0], 24) | DATA_SHIFT_BIT(tmp[1], 16) | DATA_SHIFT_BIT(tmp[2], 8)) >> 12;
	*temp = (DATA_SHIFT_BIT(tmp[3], 8) | DATA_SHIFT_BIT(tmp[4], 0)) >> 4;
	return 0;
}

static void report_abs(struct mpl3115_data *pdata)
{
	struct input_dev *idev;
	int pressure = 0;
	short temperature = 0;

	mutex_lock(&pdata->data_lock);
	if (pdata->active == MPL_STANDBY)
		goto out;
	idev = pdata->poll_dev->input;
	mpl3115_read_data(pdata->client, &pressure, &temperature);
	input_report_abs(idev, ABS_PRESSURE, pressure);
	input_report_abs(idev, ABS_TEMPTERAURE, temperature);
	input_sync(idev);
out:
	mutex_unlock(&pdata->data_lock);
}

static void mpl3115_dev_poll(struct input_polled_dev *dev)
{
     struct mpl3115_data *pdata = (struct mpl3115_data *)dev->private;

     report_abs(pdata);
}

static int mpl3115_probe(struct i2c_client *client,
			 const struct i2c_device_id *id)
{
	int result, client_id;
	struct input_dev *idev;
	struct i2c_adapter *adapter;
	struct mpl3115_data *pdata;
	adapter = to_i2c_adapter(client->dev.parent);
	result = i2c_check_functionality(adapter,
					 I2C_FUNC_SMBUS_BYTE |
					 I2C_FUNC_SMBUS_BYTE_DATA);
	if (!result)
		goto err_out;

	client_id = i2c_smbus_read_byte_data(client, MPL3115_WHO_AM_I);
	printk("read mpl3115 chip id 0x%x\n", client_id);
	if (client_id != MPL3115_ID) {
		dev_err(&client->dev,
			"read chip ID 0x%x is not equal to 0x%x!\n",
			result, MPL3115_ID);
		result = -EINVAL;
		goto err_out;
	}
	pdata = kzalloc(sizeof(struct mpl3115_data), GFP_KERNEL);
	if (!pdata)
		goto err_out;
	pdata->client = client;
	i2c_set_clientdata(client, pdata);
	mutex_init(&pdata->data_lock);
	pdata->poll_dev = input_allocate_polled_device();
	if (!pdata->poll_dev) {
		result = -ENOMEM;
		dev_err(&client->dev, "alloc poll device failed!\n");
		goto err_alloc_data;
	}
	pdata->poll_dev->poll = mpl3115_dev_poll;
	pdata->poll_dev->private = pdata;
	pdata->poll_dev->poll_interval = POLL_INTERVAL;
	pdata->poll_dev->poll_interval_min = POLL_INTERVAL_MIN;
	pdata->poll_dev->poll_interval_max = POLL_INTERVAL_MAX;
	idev = pdata->poll_dev->input;
	idev->name = MPL3115_DRV_NAME;
	idev->id.bustype = BUS_I2C;
	idev->evbit[0] = BIT_MASK(EV_ABS);

	input_set_abs_params(idev, ABS_PRESSURE, -0x7FFFFFFF, 0x7FFFFFFF, 0, 0);
	input_set_abs_params(idev, ABS_TEMPTERAURE, -0x7FFFFFFF, 0x7FFFFFFF, 0, 0);
	result = input_register_polled_device(pdata->poll_dev);
	if (result) {
		dev_err(&client->dev, "register poll device failed!\n");
		goto error_free_poll_dev;
	}
	result = sysfs_create_group(&idev->dev.kobj, &mpl3115_attr_group);
	if (result) {
		dev_err(&client->dev, "create device file failed!\n");
		result = -EINVAL;
		goto error_register_polled_device;
	}
	mpl3115_device_init(client);
	printk("mpl3115 device driver probe successfully");
	return 0;
error_register_polled_device:
	input_unregister_polled_device(pdata->poll_dev);
error_free_poll_dev:
	input_free_polled_device(pdata->poll_dev);
err_alloc_data:
	kfree(pdata);
err_out:
	return result;
}

static int mpl3115_stop_chip(struct i2c_client *client)
{
	u8 val;
	int ret;

	mpl3115_i2c_read(client, MPL3115_CTRL_REG1, &val, 1);
	val &= ~MPLL_ACTIVE_MASK;
	ret = mpl3115_i2c_write(client, MPL3115_CTRL_REG1, &val, 1);

	return 0;
}
static int mpl3115_start_chip(struct i2c_client *client)
{
	u8 val;
	int ret;

	mpl3115_i2c_read(client, MPL3115_CTRL_REG1, &val, 1);
	val |= MPLL_ACTIVE_MASK;
	ret = mpl3115_i2c_write(client, MPL3115_CTRL_REG1, &val, 1);

	return 0;
}
static int mpl3115_remove(struct i2c_client *client)
{
	struct mpl3115_data *pdata = i2c_get_clientdata(client);
	struct input_dev *idev = pdata->poll_dev->input;

	mpl3115_stop_chip(client);
	sysfs_remove_group(&idev->dev.kobj, &mpl3115_attr_group);
	input_unregister_polled_device(pdata->poll_dev);
	kfree(pdata);

	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int mpl3115_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct mpl3115_data *pdata = i2c_get_clientdata(client);
	if (pdata->active == MPL_ACTIVED)
		mpl3115_stop_chip(client);
	return 0;
}

static int mpl3115_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct mpl3115_data *pdata = i2c_get_clientdata(client);
	if (pdata->active == MPL_ACTIVED)
		mpl3115_start_chip(client);
	return 0;
}
#endif

static const struct i2c_device_id mpl3115_id[] = {
	{MPL3115_DRV_NAME, 0},
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(i2c, mpl3115_id);

static SIMPLE_DEV_PM_OPS(mpl3115_pm_ops, mpl3115_suspend, mpl3115_resume);
static struct i2c_driver mpl3115_driver = {
	.driver = {
		   .name = MPL3115_DRV_NAME,
		   .owner = THIS_MODULE,
		   .pm = &mpl3115_pm_ops,
		   },
	.probe = mpl3115_probe,
	.remove = mpl3115_remove,
	.id_table = mpl3115_id,
};

module_i2c_driver(mpl3115_driver);

MODULE_AUTHOR("Freescale Semiconductor, Inc.");
MODULE_DESCRIPTION("MPL3115 Smart Pressure Sensor driver");
MODULE_LICENSE("GPL");
