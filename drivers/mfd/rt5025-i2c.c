/* drivers/mfd/rt5025-i2c.c
 * I2C Driver for Richtek RT5025
 * Multi function device - multi functional baseband PMIC
 *
 * Copyright (C) 2014 Richtek Technology Corp.
 * Author: CY Huang <cy_huang@richtek.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/i2c.h>
#include <linux/mfd/rt5025.h>

static inline int rt5025_read_device(struct i2c_client *i2c,
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

int rt5025_reg_block_read(struct i2c_client *i2c, \
			int reg, int bytes, void *dest)
{
	return rt5025_read_device(i2c, reg, bytes, dest);
}
EXPORT_SYMBOL(rt5025_reg_block_read);

static inline int rt5025_write_device(struct i2c_client *i2c,
				      int reg, int bytes, void *dest)
{
	int ret;

	if (bytes > 1) {
		ret = i2c_smbus_write_i2c_block_data(i2c, reg, bytes, dest);
	} else {
		ret = i2c_smbus_write_byte_data(i2c, reg, *(u8 *)dest);
		if (ret < 0)
			return ret;
	}
	return ret;
}

int rt5025_reg_block_write(struct i2c_client *i2c, \
			int reg, int bytes, void *dest)
{
	return rt5025_write_device(i2c, reg, bytes, dest);
}
EXPORT_SYMBOL(rt5025_reg_block_write);

int rt5025_reg_read(struct i2c_client *i2c, int reg)
{
	int ret;

	RTINFO("I2C Read (client : 0x%x) reg = 0x%x\n",
		(unsigned int) i2c, (unsigned int) reg);
	ret = i2c_smbus_read_byte_data(i2c, reg);
	return ret;
}
EXPORT_SYMBOL(rt5025_reg_read);

int rt5025_reg_write(struct i2c_client *i2c, int reg, unsigned char data)
{
	int ret;

	RTINFO("I2C Write (client : 0x%x) reg = 0x%x, data = 0x%x\n",
		(unsigned int) i2c, (unsigned int) reg, (unsigned int) data);
	ret = i2c_smbus_write_byte_data(i2c, reg, data);
	return ret;
}
EXPORT_SYMBOL(rt5025_reg_write);

int rt5025_assign_bits(struct i2c_client *i2c, int reg,
		unsigned char mask, unsigned char data)
{
	unsigned char value;
	int ret;

	ret = rt5025_read_device(i2c, reg, 1, &value);

	if (ret < 0)
		goto out;
	value &= ~mask;
	value |= (data&mask);
	ret = i2c_smbus_write_byte_data(i2c, reg, value);
out:
	return ret;
}
EXPORT_SYMBOL(rt5025_assign_bits);

int rt5025_set_bits(struct i2c_client *i2c, int reg,
		unsigned char mask)
{
	return rt5025_assign_bits(i2c, reg, mask, mask);
}
EXPORT_SYMBOL(rt5025_set_bits);

int rt5025_clr_bits(struct i2c_client *i2c, int reg,
		unsigned char mask)
{
	return rt5025_assign_bits(i2c, reg, mask, 0);
}
EXPORT_SYMBOL(rt5025_clr_bits);

static int rt_parse_dt(struct rt5025_chip *chip, struct device *dev)
{
	return 0;
}

static int rt_parse_pdata(struct rt5025_chip *chip, struct device *dev)
{
	return 0;
}

static int rt5025_i2c_probe(struct i2c_client *client,
	const struct i2c_device_id *id)
{
	struct rt5025_platform_data *pdata = client->dev.platform_data;
	struct rt5025_chip *chip;
	bool use_dt = client->dev.of_node;
	int ret = 0;
	u8 val = 0;

	chip = devm_kzalloc(&client->dev, sizeof(*chip), GFP_KERNEL);
	if (!chip)
		return -ENOMEM;

	if (use_dt) {
		rt_parse_dt(chip, &client->dev);
	} else {
		if (!pdata) {
			ret = -EINVAL;
			goto err_init;
		}
		rt_parse_pdata(chip, &client->dev);
	}

	chip->i2c = client;
	chip->dev = &client->dev;
	mutex_init(&chip->io_lock);
	i2c_set_clientdata(client, chip);
	/* off event */
	rt5025_read_device(client, 0x20, 1, &val);
	RTINFO("off event = %d\n", val);

	rt5025_read_device(client, 0x00, 1, &val);
	if (val != 0x81) {
		dev_info(&client->dev, "The PMIC is not RT5025\n");
		return -ENODEV;
	}

	ret = rt5025_core_init(chip, pdata);
	if (ret < 0) {
		dev_err(&client->dev, "rt5025_core_init_fail\n");
		goto err_init;
	}

	dev_info(&client->dev, "driver successfully probed\n");
	if (pdata && pdata->pre_init) {
		ret = pdata->pre_init(chip);
		if (ret != 0)
			dev_err(chip->dev, "pre_init() failed: %d\n", ret);
	}

	if (pdata && pdata->post_init) {
		ret = pdata->post_init();
		if (ret != 0)
			dev_err(chip->dev, "post_init() failed: %d\n", ret);
	}
	return 0;
err_init:
	return ret;

}

static int rt5025_i2c_remove(struct i2c_client *client)
{
	struct rt5025_chip *chip = i2c_get_clientdata(client);

	rt5025_core_deinit(chip);
	dev_info(&client->dev, "%s\n", __func__);
	return 0;
}

static int rt5025_i2c_suspend(struct device *dev)
{
	struct i2c_client *i2c = container_of(dev, struct i2c_client, dev);
	struct rt5025_chip *chip = i2c_get_clientdata(i2c);

	RTINFO("\n");
	chip->suspend = 1;
	return 0;
}

static int rt5025_i2c_resume(struct device *dev)
{
	struct i2c_client *i2c = container_of(dev, struct i2c_client, dev);
	struct rt5025_chip *chip = i2c_get_clientdata(i2c);

	RTINFO("\n");
	chip->suspend = 0;
	return 0;
}

static const struct dev_pm_ops rt5025_pm_ops = {
	.suspend = rt5025_i2c_suspend,
	.resume =  rt5025_i2c_resume,
};

static const struct i2c_device_id rt5025_id_table[] = {
	{ RT5025_DEV_NAME, 0 },
	{ },
};
MODULE_DEVICE_TABLE(i2c, rt5025_id_table);

static struct of_device_id rt_match_table[] = {
	{ .compatible = "rt,rt5025",},
	{},
};

static struct i2c_driver rt5025_i2c_driver = {
	.driver	= {
		.name	= RT5025_DEV_NAME,
		.owner	= THIS_MODULE,
		 .pm = &rt5025_pm_ops,
		.of_match_table = rt_match_table,
	},
	.probe		= rt5025_i2c_probe,
	.remove		= rt5025_i2c_remove,
	.id_table	= rt5025_id_table,
};

static int rt5025_i2c_init(void)
{
	return i2c_add_driver(&rt5025_i2c_driver);
}
subsys_initcall_sync(rt5025_i2c_init);

static void rt5025_i2c_exit(void)
{
	i2c_del_driver(&rt5025_i2c_driver);
}
module_exit(rt5025_i2c_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("I2C Driver for Richtek RT5025");
MODULE_AUTHOR("CY Huang <cy_huang@richtek.com>");
MODULE_VERSION(RT5025_DRV_VER);
