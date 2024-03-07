// SPDX-License-Identifier: GPL-2.0-only
/*
 * STMicroelectronics st_mag40 driver
 *
 * MEMS Software Solutions Team
 *
 * Copyright 2016 STMicroelectronics Inc.
 */

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/types.h>

#include "st_mag40_core.h"

#define I2C_AUTO_INCREMENT	0x80

static int st_mag40_i2c_read(struct st_mag40_data *cdata, u8 reg_addr,
			     int len, u8 * data)
{
	struct i2c_client *client = to_i2c_client(cdata->dev);
	struct i2c_msg msg[2];

	if (len > 1)
		reg_addr |= I2C_AUTO_INCREMENT;

	msg[0].addr = client->addr;
	msg[0].flags = client->flags;
	msg[0].len = 1;
	msg[0].buf = &reg_addr;

	msg[1].addr = client->addr;
	msg[1].flags = client->flags | I2C_M_RD;
	msg[1].len = len;
	msg[1].buf = data;

	return i2c_transfer(client->adapter, msg, 2);
}

static int st_mag40_i2c_write(struct st_mag40_data *cdata, u8 reg_addr,
			      int len, u8 * data)
{
	struct i2c_client *client = to_i2c_client(cdata->dev);
	struct i2c_msg msg;
	u8 send[4];

	if (len >= ARRAY_SIZE(send))
		return -ENOMEM;

	send[0] = reg_addr;
	memcpy(&send[1], data, len * sizeof(u8));
	len++;

	msg.addr = client->addr;
	msg.flags = client->flags;
	msg.len = len;
	msg.buf = send;

	return i2c_transfer(client->adapter, &msg, 1);
}

static const struct st_mag40_transfer_function st_mag40_tf_i2c = {
	.write = st_mag40_i2c_write,
	.read = st_mag40_i2c_read,
};

static int st_mag40_i2c_probe(struct i2c_client *client,
			      const struct i2c_device_id *id)
{
	struct st_mag40_data *cdata;
	struct iio_dev *iio_dev;

	iio_dev = devm_iio_device_alloc(&client->dev, sizeof(*cdata));
	if (!iio_dev)
		return -ENOMEM;

	i2c_set_clientdata(client, iio_dev);
	iio_dev->dev.parent = &client->dev;
	iio_dev->name = client->name;

	cdata = iio_priv(iio_dev);
	cdata->dev = &client->dev;
	cdata->name = client->name;
	cdata->tf = &st_mag40_tf_i2c;
	cdata->irq = client->irq;

	return st_mag40_common_probe(iio_dev);
}

#if KERNEL_VERSION(6, 1, 0) <= LINUX_VERSION_CODE
static void st_mag40_i2c_remove(struct i2c_client *client)
{
	struct iio_dev *iio_dev = i2c_get_clientdata(client);

	st_mag40_common_remove(iio_dev);
}
#else /* LINUX_VERSION_CODE */
static int st_mag40_i2c_remove(struct i2c_client *client)
{
	struct iio_dev *iio_dev = i2c_get_clientdata(client);

	st_mag40_common_remove(iio_dev);

	return 0;
}
#endif /* LINUX_VERSION_CODE */

#ifdef CONFIG_PM
static int __maybe_unused st_mag40_i2c_suspend(struct device *dev)
{
	struct iio_dev *iio_dev = dev_get_drvdata(dev);
	struct st_mag40_data *cdata = iio_priv(iio_dev);

	return st_mag40_common_suspend(cdata);
}

static int __maybe_unused st_mag40_i2c_resume(struct device *dev)
{
	struct iio_dev *iio_dev = dev_get_drvdata(dev);
	struct st_mag40_data *cdata = iio_priv(iio_dev);

	return st_mag40_common_resume(cdata);
}

static const struct dev_pm_ops st_mag40_i2c_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(st_mag40_i2c_suspend, st_mag40_i2c_resume)
};
#define ST_MAG40_PM_OPS		(&st_mag40_i2c_pm_ops)
#else /* CONFIG_PM */
#define ST_MAG40_PM_OPS		NULL
#endif /* CONFIG_PM */

static const struct i2c_device_id st_mag40_ids[] = {
	{ LSM303AH_DEV_NAME, 0 },
	{ LSM303AGR_DEV_NAME, 0 },
	{ LIS2MDL_DEV_NAME, 0 },
	{ ISM303DAC_DEV_NAME, 0 },
	{ IIS2MDC_DEV_NAME, 0 },
	{}
};
MODULE_DEVICE_TABLE(i2c, st_mag40_ids);

#ifdef CONFIG_OF
static const struct of_device_id st_mag40_id_table[] = {
	{
		.compatible = "st,lsm303ah_magn",
		.data = LSM303AH_DEV_NAME,
	},
	{
		.compatible = "st,lsm303agr_magn",
		.data = LSM303AGR_DEV_NAME,
	},
	{
		.compatible = "st,lis2mdl_magn",
		.data = LSM303AGR_DEV_NAME,
	},
	{
		.compatible = "st,ism303dac_magn",
		.data = ISM303DAC_DEV_NAME,
	},
	{
		.compatible = "st,iis2mdc_magn",
		.data = IIS2MDC_DEV_NAME,
	},
	{},
};

MODULE_DEVICE_TABLE(of, st_mag40_id_table);
#endif /* CONFIG_OF */

static struct i2c_driver st_mag40_i2c_driver = {
	.driver = {
		   .owner = THIS_MODULE,
		   .name = ST_MAG40_DEV_NAME,
		   .pm = ST_MAG40_PM_OPS,
#ifdef CONFIG_OF
		   .of_match_table = st_mag40_id_table,
#endif /* CONFIG_OF */
		   },
	.probe = st_mag40_i2c_probe,
	.remove = st_mag40_i2c_remove,
	.id_table = st_mag40_ids,
};
module_i2c_driver(st_mag40_i2c_driver);

MODULE_DESCRIPTION("STMicroelectronics st_mag40 i2c driver");
MODULE_AUTHOR("MEMS Software Solutions Team");
MODULE_LICENSE("GPL v2");
