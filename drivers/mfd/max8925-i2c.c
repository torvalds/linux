/*
 * I2C driver for Maxim MAX8925
 *
 * Copyright (C) 2009 Marvell International Ltd.
 *	Haojian Zhuang <haojian.zhuang@marvell.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/i2c.h>
#include <linux/mfd/max8925.h>
#include <linux/slab.h>

#define RTC_I2C_ADDR		0x68
#define ADC_I2C_ADDR		0x47

static inline int max8925_read_device(struct i2c_client *i2c,
				      int reg, int bytes, void *dest)
{
	int ret;

	if (bytes > 1)
		ret = i2c_smbus_read_i2c_block_data(i2c, reg, bytes, dest);
	else {
		ret = i2c_smbus_read_byte_data(i2c, reg);
		if (ret < 0)
			return ret;
		*(unsigned char *)dest = (unsigned char)ret;
	}
	return ret;
}

static inline int max8925_write_device(struct i2c_client *i2c,
				       int reg, int bytes, void *src)
{
	unsigned char buf[bytes + 1];
	int ret;

	buf[0] = (unsigned char)reg;
	memcpy(&buf[1], src, bytes);

	ret = i2c_master_send(i2c, buf, bytes + 1);
	if (ret < 0)
		return ret;
	return 0;
}

int max8925_reg_read(struct i2c_client *i2c, int reg)
{
	struct max8925_chip *chip = i2c_get_clientdata(i2c);
	unsigned char data = 0;
	int ret;

	mutex_lock(&chip->io_lock);
	ret = max8925_read_device(i2c, reg, 1, &data);
	mutex_unlock(&chip->io_lock);

	if (ret < 0)
		return ret;
	else
		return (int)data;
}
EXPORT_SYMBOL(max8925_reg_read);

int max8925_reg_write(struct i2c_client *i2c, int reg,
		unsigned char data)
{
	struct max8925_chip *chip = i2c_get_clientdata(i2c);
	int ret;

	mutex_lock(&chip->io_lock);
	ret = max8925_write_device(i2c, reg, 1, &data);
	mutex_unlock(&chip->io_lock);

	return ret;
}
EXPORT_SYMBOL(max8925_reg_write);

int max8925_bulk_read(struct i2c_client *i2c, int reg,
		int count, unsigned char *buf)
{
	struct max8925_chip *chip = i2c_get_clientdata(i2c);
	int ret;

	mutex_lock(&chip->io_lock);
	ret = max8925_read_device(i2c, reg, count, buf);
	mutex_unlock(&chip->io_lock);

	return ret;
}
EXPORT_SYMBOL(max8925_bulk_read);

int max8925_bulk_write(struct i2c_client *i2c, int reg,
		int count, unsigned char *buf)
{
	struct max8925_chip *chip = i2c_get_clientdata(i2c);
	int ret;

	mutex_lock(&chip->io_lock);
	ret = max8925_write_device(i2c, reg, count, buf);
	mutex_unlock(&chip->io_lock);

	return ret;
}
EXPORT_SYMBOL(max8925_bulk_write);

int max8925_set_bits(struct i2c_client *i2c, int reg,
		unsigned char mask, unsigned char data)
{
	struct max8925_chip *chip = i2c_get_clientdata(i2c);
	unsigned char value;
	int ret;

	mutex_lock(&chip->io_lock);
	ret = max8925_read_device(i2c, reg, 1, &value);
	if (ret < 0)
		goto out;
	value &= ~mask;
	value |= data;
	ret = max8925_write_device(i2c, reg, 1, &value);
out:
	mutex_unlock(&chip->io_lock);
	return ret;
}
EXPORT_SYMBOL(max8925_set_bits);


static const struct i2c_device_id max8925_id_table[] = {
	{ "max8925", 0 },
	{ },
};
MODULE_DEVICE_TABLE(i2c, max8925_id_table);

static int __devinit max8925_probe(struct i2c_client *client,
				   const struct i2c_device_id *id)
{
	struct max8925_platform_data *pdata = client->dev.platform_data;
	static struct max8925_chip *chip;

	if (!pdata) {
		pr_info("%s: platform data is missing\n", __func__);
		return -EINVAL;
	}

	chip = kzalloc(sizeof(struct max8925_chip), GFP_KERNEL);
	if (chip == NULL)
		return -ENOMEM;
	chip->i2c = client;
	chip->dev = &client->dev;
	i2c_set_clientdata(client, chip);
	dev_set_drvdata(chip->dev, chip);
	mutex_init(&chip->io_lock);

	chip->rtc = i2c_new_dummy(chip->i2c->adapter, RTC_I2C_ADDR);
	i2c_set_clientdata(chip->rtc, chip);

	chip->adc = i2c_new_dummy(chip->i2c->adapter, ADC_I2C_ADDR);
	i2c_set_clientdata(chip->adc, chip);

	max8925_device_init(chip, pdata);

	return 0;
}

static int __devexit max8925_remove(struct i2c_client *client)
{
	struct max8925_chip *chip = i2c_get_clientdata(client);

	max8925_device_exit(chip);
	i2c_unregister_device(chip->adc);
	i2c_unregister_device(chip->rtc);
	i2c_set_clientdata(chip->adc, NULL);
	i2c_set_clientdata(chip->rtc, NULL);
	i2c_set_clientdata(chip->i2c, NULL);
	kfree(chip);
	return 0;
}

static struct i2c_driver max8925_driver = {
	.driver	= {
		.name	= "max8925",
		.owner	= THIS_MODULE,
	},
	.probe		= max8925_probe,
	.remove		= __devexit_p(max8925_remove),
	.id_table	= max8925_id_table,
};

static int __init max8925_i2c_init(void)
{
	int ret;

	ret = i2c_add_driver(&max8925_driver);
	if (ret != 0)
		pr_err("Failed to register MAX8925 I2C driver: %d\n", ret);
	return ret;
}
subsys_initcall(max8925_i2c_init);

static void __exit max8925_i2c_exit(void)
{
	i2c_del_driver(&max8925_driver);
}
module_exit(max8925_i2c_exit);

MODULE_DESCRIPTION("I2C Driver for Maxim 8925");
MODULE_AUTHOR("Haojian Zhuang <haojian.zhuang@marvell.com>");
MODULE_LICENSE("GPL");
