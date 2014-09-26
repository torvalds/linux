/*
 *  drivers/mfd/rt5036-i2c.c
 *  Source file for Richtek RT5036
 *
 *  Copyright (C) 2014 Richtek Technology Corp.
 *  cy_huang <cy_huang@richtek.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/i2c.h>
#include <linux/mutex.h>

#include <linux/mfd/rt5036/rt5036.h>

static inline int rt5036_read_device(struct i2c_client *i2c,
				     int reg, int bytes, void *dest)
{
	int ret;

	if (bytes > 1) {
		ret = i2c_smbus_read_i2c_block_data(i2c, reg, bytes, dest);
	} else {
		ret = i2c_smbus_read_byte_data(i2c, reg);
		if (ret < 0)
			return ret;
		*(unsigned char *)dest = (unsigned char)ret;
	}
	return ret;
}

int rt5036_reg_block_read(struct i2c_client *i2c,
			  int reg, int bytes, void *dest)
{
	return rt5036_read_device(i2c, reg, bytes, dest);
}
EXPORT_SYMBOL(rt5036_reg_block_read);

static inline int rt5036_write_device(struct i2c_client *i2c,
				      int reg, int bytes, void *dest)
{
	int ret;

	if (bytes > 1) {
		ret = i2c_smbus_write_i2c_block_data(i2c, reg, bytes, dest);
	} else {
		ret = i2c_smbus_write_byte_data(i2c, reg, *(u8 *) dest);
		if (ret < 0)
			return ret;
	}
	return ret;
}

int rt5036_reg_block_write(struct i2c_client *i2c,
			   int reg, int bytes, void *dest)
{
	return rt5036_write_device(i2c, reg, bytes, dest);
}
EXPORT_SYMBOL(rt5036_reg_block_write);

int rt5036_reg_read(struct i2c_client *i2c, int reg)
{
	int ret;

	RTINFO("I2C Read (client : 0x%x) reg = 0x%x\n",
	       (unsigned int)i2c, (unsigned int)reg);
	ret = i2c_smbus_read_byte_data(i2c, reg);
	return ret;
}
EXPORT_SYMBOL(rt5036_reg_read);

int rt5036_reg_write(struct i2c_client *i2c, int reg, unsigned char data)
{
	int ret;

	RTINFO("I2C Write (client : 0x%x) reg = 0x%x, data = 0x%x\n",
	       (unsigned int)i2c, (unsigned int)reg, (unsigned int)data);
	ret = i2c_smbus_write_byte_data(i2c, reg, data);
	return ret;
}
EXPORT_SYMBOL(rt5036_reg_write);

int rt5036_assign_bits(struct i2c_client *i2c, int reg,
		       unsigned char mask, unsigned char data)
{
	unsigned char value;
	int ret;

	ret = rt5036_read_device(i2c, reg, 1, &value);

	if (ret < 0)
		goto out;
	value &= ~mask;
	value |= (data & mask);
	ret = i2c_smbus_write_byte_data(i2c, reg, value);
out:
	return ret;
}
EXPORT_SYMBOL(rt5036_assign_bits);

int rt5036_set_bits(struct i2c_client *i2c, int reg, unsigned char mask)
{
	return rt5036_assign_bits(i2c, reg, mask, mask);
}
EXPORT_SYMBOL(rt5036_set_bits);

int rt5036_clr_bits(struct i2c_client *i2c, int reg, unsigned char mask)
{
	return rt5036_assign_bits(i2c, reg, mask, 0);
}
EXPORT_SYMBOL(rt5036_clr_bits);

static int rt_parse_dt(struct rt5036_chip *chip, struct device *dev)
{
	RTINFO("\n");
	return 0;
}

static int rt_parse_pdata(struct rt5036_chip *chip,
				    struct device *dev)
{
	RTINFO("\n");
	return 0;
}

static int rt5036_i2c_probe(struct i2c_client *client,
				      const struct i2c_device_id *id)
{
	struct rt5036_platform_data *pdata = client->dev.platform_data;
	struct rt5036_chip *chip;
	bool use_dt = client->dev.of_node;
	int val, ret = 0;

	chip = devm_kzalloc(&client->dev, sizeof(*chip), GFP_KERNEL);
	if (!chip)
		return -ENOMEM;

	chip->i2c = client;
	mutex_init(&chip->io_lock);
	i2c_set_clientdata(client, chip);

	val = rt5036_reg_read(client, RT5036_REG_DEVID);
	if (val < 0) {
		ret = -EIO;
		goto out_err;
	} else {
		if (val != 0x36) {
			dev_err(&client->dev, "id 0x%02x is not correct\n",
				val);
			ret = ENODEV;
			goto out_err;
		}
		val = rt5036_reg_read(client, RT5036_REG_ONOFFEVENT);
		dev_info(&client->dev, "last onoff event %02x\n", val);
		/*set ldo lsw vrc to default enable*/
		rt5036_reg_write(client, 0x4D, 0xF0);
		rt5036_reg_write(client, 0x7D, 0xF0);
		rt5036_reg_write(client, 0x85, 0xCC);
	}

	if (use_dt) {
		rt_parse_dt(chip, &client->dev);
	} else {
		if (!pdata) {
			dev_err(&client->dev, "no platform data included\n");
			ret = -EINVAL;
			goto out_err;
		}
		rt_parse_pdata(chip, &client->dev);
	}

	ret = rt5036_core_init(&client->dev, pdata);
	if (ret < 0) {
		ret = -EINVAL;
		goto out_err;
	}

	if (pdata && pdata->pre_init) {
		ret = pdata->pre_init(chip);
		if (ret != 0)
			dev_err(&client->dev, "pre_init() failed: %d\n", ret);
	}
	if (pdata && pdata->post_init) {
		ret = pdata->post_init();
		if (ret != 0)
			dev_err(&client->dev, "post_init() failed: %d\n", ret);
	}
	dev_info(&client->dev, "driver successfully loaded\n");
	return 0;
out_err:
	return ret;
}

static int rt5036_i2c_remove(struct i2c_client *client)
{
	RTINFO("\n");
	rt5036_core_deinit(&client->dev);
	return 0;
}

static int rt5036_i2c_suspend(struct device *dev)
{
	struct i2c_client *i2c = container_of(dev, struct i2c_client, dev);
	struct rt5036_chip *chip = i2c_get_clientdata(i2c);

	RTINFO("\n");
	chip->suspend = 1;
	return 0;
}

static int rt5036_i2c_resume(struct device *dev)
{
	struct i2c_client *i2c = container_of(dev, struct i2c_client, dev);
	struct rt5036_chip *chip = i2c_get_clientdata(i2c);

	RTINFO("\n");
	chip->suspend = 0;
	return 0;
}

static const struct dev_pm_ops rt5036_pm_ops = {
	.suspend = rt5036_i2c_suspend,
	.resume =  rt5036_i2c_resume,
};

static const struct i2c_device_id rt5036_id_table[] = {
	{RT5036_DEV_NAME, 0},
	{},
};
MODULE_DEVICE_TABLE(i2c, rt5036_id_table);

static const struct of_device_id rt_match_table[] = {
	{.compatible = "rt,rt5036",},
	{},
};

static struct i2c_driver rt5036_driver = {
	.driver = {
		   .name = RT5036_DEV_NAME,
		   .owner = THIS_MODULE,
		   .pm = &rt5036_pm_ops,
		   .of_match_table = rt_match_table,
		   },
	.probe = rt5036_i2c_probe,
	.remove = rt5036_i2c_remove,
	.id_table = rt5036_id_table,
};

static int __init rt5036_i2c_init(void)
{
	return i2c_add_driver(&rt5036_driver);
}
subsys_initcall_sync(rt5036_i2c_init);

static void __exit rt5036_i2c_exit(void)
{
	i2c_del_driver(&rt5036_driver);
}
module_exit(rt5036_i2c_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("I2C Driver for Richtek RT5036");
MODULE_AUTHOR("CY Huang <cy_huang@richtek.com>");
MODULE_VERSION(RT5036_DRV_VER);
