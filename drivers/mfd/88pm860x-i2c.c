/*
 * I2C driver for Marvell 88PM8607
 *
 * Copyright (C) 2009 Marvell International Ltd.
 * 	Haojian Zhuang <haojian.zhuang@marvell.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/i2c.h>
#include <linux/mfd/88pm8607.h>

static inline int pm8607_read_device(struct pm8607_chip *chip,
				     int reg, int bytes, void *dest)
{
	struct i2c_client *i2c = chip->client;
	unsigned char data;
	int ret;

	data = (unsigned char)reg;
	ret = i2c_master_send(i2c, &data, 1);
	if (ret < 0)
		return ret;

	ret = i2c_master_recv(i2c, dest, bytes);
	if (ret < 0)
		return ret;
	return 0;
}

static inline int pm8607_write_device(struct pm8607_chip *chip,
				      int reg, int bytes, void *src)
{
	struct i2c_client *i2c = chip->client;
	unsigned char buf[bytes + 1];
	int ret;

	buf[0] = (unsigned char)reg;
	memcpy(&buf[1], src, bytes);

	ret = i2c_master_send(i2c, buf, bytes + 1);
	if (ret < 0)
		return ret;
	return 0;
}

int pm8607_reg_read(struct pm8607_chip *chip, int reg)
{
	unsigned char data;
	int ret;

	mutex_lock(&chip->io_lock);
	ret = chip->read(chip, reg, 1, &data);
	mutex_unlock(&chip->io_lock);

	if (ret < 0)
		return ret;
	else
		return (int)data;
}
EXPORT_SYMBOL(pm8607_reg_read);

int pm8607_reg_write(struct pm8607_chip *chip, int reg,
		     unsigned char data)
{
	int ret;

	mutex_lock(&chip->io_lock);
	ret = chip->write(chip, reg, 1, &data);
	mutex_unlock(&chip->io_lock);

	return ret;
}
EXPORT_SYMBOL(pm8607_reg_write);

int pm8607_bulk_read(struct pm8607_chip *chip, int reg,
		     int count, unsigned char *buf)
{
	int ret;

	mutex_lock(&chip->io_lock);
	ret = chip->read(chip, reg, count, buf);
	mutex_unlock(&chip->io_lock);

	return ret;
}
EXPORT_SYMBOL(pm8607_bulk_read);

int pm8607_bulk_write(struct pm8607_chip *chip, int reg,
		      int count, unsigned char *buf)
{
	int ret;

	mutex_lock(&chip->io_lock);
	ret = chip->write(chip, reg, count, buf);
	mutex_unlock(&chip->io_lock);

	return ret;
}
EXPORT_SYMBOL(pm8607_bulk_write);

int pm8607_set_bits(struct pm8607_chip *chip, int reg,
		    unsigned char mask, unsigned char data)
{
	unsigned char value;
	int ret;

	mutex_lock(&chip->io_lock);
	ret = chip->read(chip, reg, 1, &value);
	if (ret < 0)
		goto out;
	value &= ~mask;
	value |= data;
	ret = chip->write(chip, reg, 1, &value);
out:
	mutex_unlock(&chip->io_lock);
	return ret;
}
EXPORT_SYMBOL(pm8607_set_bits);


static const struct i2c_device_id pm860x_id_table[] = {
	{ "88PM8607", 0 },
	{}
};
MODULE_DEVICE_TABLE(i2c, pm860x_id_table);

static int __devinit pm860x_probe(struct i2c_client *client,
				  const struct i2c_device_id *id)
{
	struct pm8607_platform_data *pdata = client->dev.platform_data;
	struct pm8607_chip *chip;
	int ret;

	chip = kzalloc(sizeof(struct pm8607_chip), GFP_KERNEL);
	if (chip == NULL)
		return -ENOMEM;

	chip->client = client;
	chip->dev = &client->dev;
	chip->read = pm8607_read_device;
	chip->write = pm8607_write_device;
	memcpy(&chip->id, id, sizeof(struct i2c_device_id));
	i2c_set_clientdata(client, chip);

	mutex_init(&chip->io_lock);
	dev_set_drvdata(chip->dev, chip);

	ret = pm860x_device_init(chip, pdata);
	if (ret < 0)
		goto out;


	return 0;

out:
	i2c_set_clientdata(client, NULL);
	kfree(chip);
	return ret;
}

static int __devexit pm860x_remove(struct i2c_client *client)
{
	struct pm8607_chip *chip = i2c_get_clientdata(client);

	kfree(chip);
	return 0;
}

static struct i2c_driver pm860x_driver = {
	.driver	= {
		.name	= "88PM860x",
		.owner	= THIS_MODULE,
	},
	.probe		= pm860x_probe,
	.remove		= __devexit_p(pm860x_remove),
	.id_table	= pm860x_id_table,
};

static int __init pm860x_i2c_init(void)
{
	int ret;
	ret = i2c_add_driver(&pm860x_driver);
	if (ret != 0)
		pr_err("Failed to register 88PM860x I2C driver: %d\n", ret);
	return ret;
}
subsys_initcall(pm860x_i2c_init);

static void __exit pm860x_i2c_exit(void)
{
	i2c_del_driver(&pm860x_driver);
}
module_exit(pm860x_i2c_exit);

MODULE_DESCRIPTION("I2C Driver for Marvell 88PM860x");
MODULE_AUTHOR("Haojian Zhuang <haojian.zhuang@marvell.com>");
MODULE_LICENSE("GPL");
