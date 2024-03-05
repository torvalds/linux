// SPDX-License-Identifier: GPL-2.0-only
/*
 * STMicroelectronics lis2hh12 driver
 *
 * MEMS Software Solutions Team
 *
 * Copyright 2016 STMicroelectronics Inc.
 */

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/types.h>

#include "st_lis2hh12.h"

static int lis2hh12_i2c_read(struct lis2hh12_data *cdata, u8 reg_addr, int len,
								u8 * data)
{
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

	return(i2c_transfer(client->adapter, msg, 2));
}

static int lis2hh12_i2c_write(struct lis2hh12_data *cdata,
			      u8 reg_addr, int len, u8 *data)
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

	return(i2c_transfer(client->adapter, &msg, 1));
}

static const struct lis2hh12_transfer_function lis2hh12_tf_i2c = {
	.write = lis2hh12_i2c_write,
	.read = lis2hh12_i2c_read,
};

static int lis2hh12_i2c_probe(struct i2c_client *client,
			    const struct i2c_device_id *id)
{
	int err;
	struct lis2hh12_data *cdata;

	cdata = kmalloc(sizeof(*cdata), GFP_KERNEL);
	if (!cdata)
		return -ENOMEM;

	cdata->dev = &client->dev;
	cdata->name = client->name;
	cdata->tf = &lis2hh12_tf_i2c;
	i2c_set_clientdata(client, cdata);

	err = lis2hh12_common_probe(cdata, client->irq);
	if (err < 0)
		goto free_data;

	return 0;

free_data:
	kfree(cdata);
	return err;
}

#if KERNEL_VERSION(6, 1, 0) <= LINUX_VERSION_CODE
static void lis2hh12_i2c_remove(struct i2c_client *client)
{
	struct lis2hh12_data *cdata = i2c_get_clientdata(client);

	lis2hh12_common_remove(cdata, client->irq);
	dev_info(cdata->dev, "%s: removed\n", LIS2HH12_DEV_NAME);
	kfree(cdata);
}
#else /* LINUX_VERSION_CODE */
static int lis2hh12_i2c_remove(struct i2c_client *client)
{
	struct lis2hh12_data *cdata = i2c_get_clientdata(client);

	lis2hh12_common_remove(cdata, client->irq);
	dev_info(cdata->dev, "%s: removed\n", LIS2HH12_DEV_NAME);
	kfree(cdata);

	return 0;
}
#endif /* LINUX_VERSION_CODE */

#ifdef CONFIG_PM
static int __maybe_unused lis2hh12_suspend(struct device *dev)
{
	struct lis2hh12_data *cdata = i2c_get_clientdata(to_i2c_client(dev));

	return lis2hh12_common_suspend(cdata);
}

static int __maybe_unused lis2hh12_resume(struct device *dev)
{
	struct lis2hh12_data *cdata = i2c_get_clientdata(to_i2c_client(dev));

	return lis2hh12_common_resume(cdata);
}

static const struct dev_pm_ops lis2hh12_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(lis2hh12_suspend, lis2hh12_resume)
};

#define LIS2HH12_PM_OPS		(&lis2hh12_pm_ops)
#else /* CONFIG_PM */
#define LIS2HH12_PM_OPS		NULL
#endif /* CONFIG_PM */

static const struct i2c_device_id lis2hh12_ids[] = {
	{"lis2hh12", 0},
	{}
};

MODULE_DEVICE_TABLE(i2c, lis2hh12_ids);

#ifdef CONFIG_OF
static const struct of_device_id lis2hh12_id_table[] = {
	{.compatible = "st,lis2hh12",},
	{},
};

MODULE_DEVICE_TABLE(of, lis2hh12_id_table);
#endif /* CONFIG_OF */

static struct i2c_driver lis2hh12_i2c_driver = {
	.driver = {
		   .owner = THIS_MODULE,
		   .name = LIS2HH12_DEV_NAME,
		   .pm = LIS2HH12_PM_OPS,
#ifdef CONFIG_OF
		   .of_match_table = lis2hh12_id_table,
#endif /* CONFIG_OF */
		   },
	.probe = lis2hh12_i2c_probe,
	.remove = lis2hh12_i2c_remove,
	.id_table = lis2hh12_ids,
};

module_i2c_driver(lis2hh12_i2c_driver);

MODULE_DESCRIPTION("STMicroelectronics lis2hh12 i2c driver");
MODULE_AUTHOR("MEMS Software Solutions Team");
MODULE_LICENSE("GPL v2");
