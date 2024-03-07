// SPDX-License-Identifier: GPL-2.0-only
/*
 * STMicroelectronics ism303dac i2c driver
 *
 * MEMS Software Solutions Team
 *
 * Copyright 2018 STMicroelectronics Inc.
 */

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/hrtimer.h>
#include <linux/types.h>

#include "st_ism303dac_accel.h"

static int ism303dac_i2c_read(struct ism303dac_data *cdata, u8 reg_addr, int len,
			      u8 * data, bool b_lock)
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
		mutex_lock(&cdata->regs_lock);
		err = i2c_transfer(client->adapter, msg, 2);
		mutex_unlock(&cdata->regs_lock);
	} else
		err = i2c_transfer(client->adapter, msg, 2);

	return err;
}

static int ism303dac_i2c_write(struct ism303dac_data *cdata, u8 reg_addr, int len,
			       u8 * data, bool b_lock)
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
		mutex_lock(&cdata->regs_lock);
		err = i2c_transfer(client->adapter, &msg, 1);
		mutex_unlock(&cdata->regs_lock);
	} else
		err = i2c_transfer(client->adapter, &msg, 1);

	return err;
}

static const struct ism303dac_transfer_function ism303dac_tf_i2c = {
	.write = ism303dac_i2c_write,
	.read = ism303dac_i2c_read,
};

static int ism303dac_i2c_probe(struct i2c_client *client,
			       const struct i2c_device_id *id)
{
	int err;
	struct ism303dac_data *cdata;

	cdata = kzalloc(sizeof(*cdata), GFP_KERNEL);
	if (!cdata)
		return -ENOMEM;

	cdata->dev = &client->dev;
	cdata->name = client->name;
	cdata->tf = &ism303dac_tf_i2c;
	i2c_set_clientdata(client, cdata);

	err = ism303dac_common_probe(cdata, client->irq);
	if (err < 0)
		goto free_data;

	return 0;

free_data:
	kfree(cdata);
	return err;
}

#if KERNEL_VERSION(6, 1, 0) <= LINUX_VERSION_CODE
static void ism303dac_i2c_remove(struct i2c_client *client)
{
	struct ism303dac_data *cdata = i2c_get_clientdata(client);

	ism303dac_common_remove(cdata, client->irq);
	dev_info(cdata->dev, "%s: removed\n", ISM303DAC_DEV_NAME);
	kfree(cdata);
}
#else /* LINUX_VERSION_CODE */
static int ism303dac_i2c_remove(struct i2c_client *client)
{
	struct ism303dac_data *cdata = i2c_get_clientdata(client);

	ism303dac_common_remove(cdata, client->irq);
	dev_info(cdata->dev, "%s: removed\n", ISM303DAC_DEV_NAME);
	kfree(cdata);

	return 0;
}
#endif /* LINUX_VERSION_CODE */

#ifdef CONFIG_PM
static int __maybe_unused ism303dac_suspend(struct device *dev)
{
	struct ism303dac_data *cdata = i2c_get_clientdata(to_i2c_client(dev));

	return ism303dac_common_suspend(cdata);
}

static int __maybe_unused ism303dac_resume(struct device *dev)
{
	struct ism303dac_data *cdata = i2c_get_clientdata(to_i2c_client(dev));

	return ism303dac_common_resume(cdata);
}

static const struct dev_pm_ops ism303dac_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(ism303dac_suspend, ism303dac_resume)
};

#define ISM303DAC_PM_OPS		(&ism303dac_pm_ops)
#else /* CONFIG_PM */
#define ISM303DAC_PM_OPS		NULL
#endif /* CONFIG_PM */

static const struct i2c_device_id ism303dac_ids[] = {
	{ ISM303DAC_DEV_NAME, 0 },
	{}
};

MODULE_DEVICE_TABLE(i2c, ism303dac_ids);

#ifdef CONFIG_OF
static const struct of_device_id ism303dac_id_table[] = {
	{.compatible = "st,ism303dac_accel",},
	{},
};

MODULE_DEVICE_TABLE(of, ism303dac_id_table);
#endif

static struct i2c_driver ism303dac_i2c_driver = {
	.driver = {
		   .owner = THIS_MODULE,
		   .name = ISM303DAC_DEV_NAME,
		   .pm = ISM303DAC_PM_OPS,
#ifdef CONFIG_OF
		   .of_match_table = ism303dac_id_table,
#endif
		   },
	.probe = ism303dac_i2c_probe,
	.remove = ism303dac_i2c_remove,
	.id_table = ism303dac_ids,
};

module_i2c_driver(ism303dac_i2c_driver);

MODULE_DESCRIPTION("STMicroelectronics ism303dac i2c driver");
MODULE_AUTHOR("MEMS Software Solutions Team");
MODULE_LICENSE("GPL v2");
