// SPDX-License-Identifier: GPL-2.0-only
/*
 * STMicroelectronics ism330dlc i2c driver
 *
 * MEMS Software Solutions Team
 *
 * Copyright 2016 STMicroelectronics Inc.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/of.h>
#include <linux/iio/iio.h>
#include <linux/iio/buffer.h>
#include <linux/version.h>

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,11,0)
#include <linux/iio/buffer_impl.h>
#endif /* LINUX_VERSION_CODE */

#include "st_ism330dlc.h"

static int st_ism330dlc_i2c_read(struct ism330dlc_data *cdata,
				 u8 reg_addr, int len, u8 *data, bool b_lock)
{
	int err = 0;
	struct i2c_msg msg[2];
	struct i2c_client *client = to_i2c_client(cdata->dev);

	msg[0].addr = client->addr;
	msg[0].flags = client->flags;
	msg[0].len = 1;
	msg[0].buf = &reg_addr;

	msg[1].addr = client->addr;
	msg[1].flags = client->flags | I2C_M_RD;
	msg[1].len = len;
	msg[1].buf = data;

	if (b_lock) {
		mutex_lock(&cdata->bank_registers_lock);
		err = i2c_transfer(client->adapter, msg, 2);
		mutex_unlock(&cdata->bank_registers_lock);
	} else
		err = i2c_transfer(client->adapter, msg, 2);

	return err;
}

static int st_ism330dlc_i2c_write(struct ism330dlc_data *cdata,
				  u8 reg_addr, int len, u8 *data, bool b_lock)
{
	struct i2c_client *client = to_i2c_client(cdata->dev);
	struct i2c_msg msg;
	int err = 0;
	u8 send[8];

	if (len >= ARRAY_SIZE(send))
		return -ENOMEM;

	send[0] = reg_addr;
	memcpy(&send[1], data, len * sizeof(u8));
	len++;

	msg.addr = client->addr;
	msg.flags = client->flags;
	msg.len = len;
	msg.buf = send;

	if (b_lock) {
		mutex_lock(&cdata->bank_registers_lock);
		err = i2c_transfer(client->adapter, &msg, 1);
		mutex_unlock(&cdata->bank_registers_lock);
	} else
		err = i2c_transfer(client->adapter, &msg, 1);

	return err;
}

static const struct st_ism330dlc_transfer_function st_ism330dlc_tf_i2c = {
	.write = st_ism330dlc_i2c_write,
	.read = st_ism330dlc_i2c_read,
};

static int st_ism330dlc_i2c_probe(struct i2c_client *client,
				  const struct i2c_device_id *id)
{
	int err;
	struct ism330dlc_data *cdata;

	cdata = kmalloc(sizeof(*cdata), GFP_KERNEL);
	if (!cdata)
		return -ENOMEM;

	cdata->dev = &client->dev;
	cdata->name = client->name;
	i2c_set_clientdata(client, cdata);

	cdata->tf = &st_ism330dlc_tf_i2c;

	err = st_ism330dlc_common_probe(cdata, client->irq);
	if (err < 0)
		goto free_data;

	return 0;

free_data:
	kfree(cdata);
	return err;
}

#if KERNEL_VERSION(6, 1, 0) <= LINUX_VERSION_CODE
static void st_ism330dlc_i2c_remove(struct i2c_client *client)
{
	struct ism330dlc_data *cdata = i2c_get_clientdata(client);

	st_ism330dlc_common_remove(cdata, client->irq);
	kfree(cdata);
}
#else /* LINUX_VERSION_CODE */
static int st_ism330dlc_i2c_remove(struct i2c_client *client)
{
	struct ism330dlc_data *cdata = i2c_get_clientdata(client);

	st_ism330dlc_common_remove(cdata, client->irq);
	kfree(cdata);

	return 0;
}
#endif /* LINUX_VERSION_CODE */

#ifdef CONFIG_PM
static int __maybe_unused st_ism330dlc_suspend(struct device *dev)
{
	struct ism330dlc_data *cdata = i2c_get_clientdata(to_i2c_client(dev));

	return st_ism330dlc_common_suspend(cdata);
}

static int __maybe_unused st_ism330dlc_resume(struct device *dev)
{
	struct ism330dlc_data *cdata = i2c_get_clientdata(to_i2c_client(dev));

	return st_ism330dlc_common_resume(cdata);
}

static const struct dev_pm_ops st_ism330dlc_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(st_ism330dlc_suspend, st_ism330dlc_resume)
};

#define ST_ISM330DLC_PM_OPS		(&st_ism330dlc_pm_ops)
#else /* CONFIG_PM */
#define ST_ISM330DLC_PM_OPS		NULL
#endif /* CONFIG_PM */

static const struct i2c_device_id st_ism330dlc_id_table[] = {
	{ ISM330DLC_DEV_NAME },
	{},
};
MODULE_DEVICE_TABLE(i2c, st_ism330dlc_id_table);

#ifdef CONFIG_OF
static const struct of_device_id ism330dlc_of_match[] = {
	{
		.compatible = "st,ism330dlc",
		.data = ISM330DLC_DEV_NAME,
	},
	{},
};
MODULE_DEVICE_TABLE(of, ism330dlc_of_match);
#else /* CONFIG_OF */
#define ism330dlc_of_match		NULL
#endif /* CONFIG_OF */

static struct i2c_driver st_ism330dlc_driver = {
	.driver = {
		.owner = THIS_MODULE,
		.name = "st-ism330dlc-i2c",
		.pm = ST_ISM330DLC_PM_OPS,
		.of_match_table = of_match_ptr(ism330dlc_of_match),
	},
	.probe = st_ism330dlc_i2c_probe,
	.remove = st_ism330dlc_i2c_remove,
	.id_table = st_ism330dlc_id_table,
};
module_i2c_driver(st_ism330dlc_driver);

MODULE_AUTHOR("MEMS Software Solutions Team");
MODULE_DESCRIPTION("STMicroelectronics ism330dlc i2c driver");
MODULE_LICENSE("GPL v2");
