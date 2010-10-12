/*
 * Core driver for TI TPS6586x PMIC family
 *
 * Copyright (c) 2010 CompuLab Ltd.
 * Mike Rapoport <mike@compulab.co.il>
 *
 * Based on da903x.c.
 * Copyright (C) 2008 Compulab, Ltd.
 * Mike Rapoport <mike@compulab.co.il>
 * Copyright (C) 2006-2008 Marvell International Ltd.
 * Eric Miao <eric.miao@marvell.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/gpio.h>
#include <linux/i2c.h>

#include <linux/mfd/core.h>
#include <linux/mfd/tps6586x.h>

/* GPIO control registers */
#define TPS6586X_GPIOSET1	0x5d
#define TPS6586X_GPIOSET2	0x5e

/* device id */
#define TPS6586X_VERSIONCRC	0xcd
#define TPS658621A_VERSIONCRC	0x15

struct tps6586x {
	struct mutex		lock;
	struct device		*dev;
	struct i2c_client	*client;

	struct gpio_chip	gpio;
};

static inline int __tps6586x_read(struct i2c_client *client,
				  int reg, uint8_t *val)
{
	int ret;

	ret = i2c_smbus_read_byte_data(client, reg);
	if (ret < 0) {
		dev_err(&client->dev, "failed reading at 0x%02x\n", reg);
		return ret;
	}

	*val = (uint8_t)ret;

	return 0;
}

static inline int __tps6586x_reads(struct i2c_client *client, int reg,
				   int len, uint8_t *val)
{
	int ret;

	ret = i2c_smbus_read_i2c_block_data(client, reg, len, val);
	if (ret < 0) {
		dev_err(&client->dev, "failed reading from 0x%02x\n", reg);
		return ret;
	}

	return 0;
}

static inline int __tps6586x_write(struct i2c_client *client,
				 int reg, uint8_t val)
{
	int ret;

	ret = i2c_smbus_write_byte_data(client, reg, val);
	if (ret < 0) {
		dev_err(&client->dev, "failed writing 0x%02x to 0x%02x\n",
				val, reg);
		return ret;
	}

	return 0;
}

static inline int __tps6586x_writes(struct i2c_client *client, int reg,
				  int len, uint8_t *val)
{
	int ret;

	ret = i2c_smbus_write_i2c_block_data(client, reg, len, val);
	if (ret < 0) {
		dev_err(&client->dev, "failed writings to 0x%02x\n", reg);
		return ret;
	}

	return 0;
}

int tps6586x_write(struct device *dev, int reg, uint8_t val)
{
	return __tps6586x_write(to_i2c_client(dev), reg, val);
}
EXPORT_SYMBOL_GPL(tps6586x_write);

int tps6586x_writes(struct device *dev, int reg, int len, uint8_t *val)
{
	return __tps6586x_writes(to_i2c_client(dev), reg, len, val);
}
EXPORT_SYMBOL_GPL(tps6586x_writes);

int tps6586x_read(struct device *dev, int reg, uint8_t *val)
{
	return __tps6586x_read(to_i2c_client(dev), reg, val);
}
EXPORT_SYMBOL_GPL(tps6586x_read);

int tps6586x_reads(struct device *dev, int reg, int len, uint8_t *val)
{
	return __tps6586x_reads(to_i2c_client(dev), reg, len, val);
}
EXPORT_SYMBOL_GPL(tps6586x_reads);

int tps6586x_set_bits(struct device *dev, int reg, uint8_t bit_mask)
{
	struct tps6586x *tps6586x = dev_get_drvdata(dev);
	uint8_t reg_val;
	int ret = 0;

	mutex_lock(&tps6586x->lock);

	ret = __tps6586x_read(to_i2c_client(dev), reg, &reg_val);
	if (ret)
		goto out;

	if ((reg_val & bit_mask) == 0) {
		reg_val |= bit_mask;
		ret = __tps6586x_write(to_i2c_client(dev), reg, reg_val);
	}
out:
	mutex_unlock(&tps6586x->lock);
	return ret;
}
EXPORT_SYMBOL_GPL(tps6586x_set_bits);

int tps6586x_clr_bits(struct device *dev, int reg, uint8_t bit_mask)
{
	struct tps6586x *tps6586x = dev_get_drvdata(dev);
	uint8_t reg_val;
	int ret = 0;

	mutex_lock(&tps6586x->lock);

	ret = __tps6586x_read(to_i2c_client(dev), reg, &reg_val);
	if (ret)
		goto out;

	if (reg_val & bit_mask) {
		reg_val &= ~bit_mask;
		ret = __tps6586x_write(to_i2c_client(dev), reg, reg_val);
	}
out:
	mutex_unlock(&tps6586x->lock);
	return ret;
}
EXPORT_SYMBOL_GPL(tps6586x_clr_bits);

int tps6586x_update(struct device *dev, int reg, uint8_t val, uint8_t mask)
{
	struct tps6586x *tps6586x = dev_get_drvdata(dev);
	uint8_t reg_val;
	int ret = 0;

	mutex_lock(&tps6586x->lock);

	ret = __tps6586x_read(tps6586x->client, reg, &reg_val);
	if (ret)
		goto out;

	if ((reg_val & mask) != val) {
		reg_val = (reg_val & ~mask) | val;
		ret = __tps6586x_write(tps6586x->client, reg, reg_val);
	}
out:
	mutex_unlock(&tps6586x->lock);
	return ret;
}
EXPORT_SYMBOL_GPL(tps6586x_update);

static int tps6586x_gpio_get(struct gpio_chip *gc, unsigned offset)
{
	struct tps6586x *tps6586x = container_of(gc, struct tps6586x, gpio);
	uint8_t val;
	int ret;

	ret = __tps6586x_read(tps6586x->client, TPS6586X_GPIOSET2, &val);
	if (ret)
		return ret;

	return !!(val & (1 << offset));
}


static void tps6586x_gpio_set(struct gpio_chip *chip, unsigned offset,
			      int value)
{
	struct tps6586x *tps6586x = container_of(chip, struct tps6586x, gpio);

	__tps6586x_write(tps6586x->client, TPS6586X_GPIOSET2,
			 value << offset);
}

static int tps6586x_gpio_output(struct gpio_chip *gc, unsigned offset,
				int value)
{
	struct tps6586x *tps6586x = container_of(gc, struct tps6586x, gpio);
	uint8_t val, mask;

	tps6586x_gpio_set(gc, offset, value);

	val = 0x1 << (offset * 2);
	mask = 0x3 << (offset * 2);

	return tps6586x_update(tps6586x->dev, TPS6586X_GPIOSET1, val, mask);
}

static void tps6586x_gpio_init(struct tps6586x *tps6586x, int gpio_base)
{
	int ret;

	if (!gpio_base)
		return;

	tps6586x->gpio.owner		= THIS_MODULE;
	tps6586x->gpio.label		= tps6586x->client->name;
	tps6586x->gpio.dev		= tps6586x->dev;
	tps6586x->gpio.base		= gpio_base;
	tps6586x->gpio.ngpio		= 4;
	tps6586x->gpio.can_sleep	= 1;

	/* FIXME: add handling of GPIOs as dedicated inputs */
	tps6586x->gpio.direction_output	= tps6586x_gpio_output;
	tps6586x->gpio.set		= tps6586x_gpio_set;
	tps6586x->gpio.get		= tps6586x_gpio_get;

	ret = gpiochip_add(&tps6586x->gpio);
	if (ret)
		dev_warn(tps6586x->dev, "GPIO registration failed: %d\n", ret);
}

static int __remove_subdev(struct device *dev, void *unused)
{
	platform_device_unregister(to_platform_device(dev));
	return 0;
}

static int tps6586x_remove_subdevs(struct tps6586x *tps6586x)
{
	return device_for_each_child(tps6586x->dev, NULL, __remove_subdev);
}

static int __devinit tps6586x_add_subdevs(struct tps6586x *tps6586x,
					  struct tps6586x_platform_data *pdata)
{
	struct tps6586x_subdev_info *subdev;
	struct platform_device *pdev;
	int i, ret = 0;

	for (i = 0; i < pdata->num_subdevs; i++) {
		subdev = &pdata->subdevs[i];

		pdev = platform_device_alloc(subdev->name, subdev->id);

		pdev->dev.parent = tps6586x->dev;
		pdev->dev.platform_data = subdev->platform_data;

		ret = platform_device_add(pdev);
		if (ret)
			goto failed;
	}
	return 0;

failed:
	tps6586x_remove_subdevs(tps6586x);
	return ret;
}

static int __devinit tps6586x_i2c_probe(struct i2c_client *client,
					const struct i2c_device_id *id)
{
	struct tps6586x_platform_data *pdata = client->dev.platform_data;
	struct tps6586x *tps6586x;
	int ret;

	if (!pdata) {
		dev_err(&client->dev, "tps6586x requires platform data\n");
		return -ENOTSUPP;
	}

	ret = i2c_smbus_read_byte_data(client, TPS6586X_VERSIONCRC);
	if (ret < 0) {
		dev_err(&client->dev, "Chip ID read failed: %d\n", ret);
		return -EIO;
	}

	if (ret != TPS658621A_VERSIONCRC) {
		dev_err(&client->dev, "Unsupported chip ID: %x\n", ret);
		return -ENODEV;
	}

	tps6586x = kzalloc(sizeof(struct tps6586x), GFP_KERNEL);
	if (tps6586x == NULL)
		return -ENOMEM;

	tps6586x->client = client;
	tps6586x->dev = &client->dev;
	i2c_set_clientdata(client, tps6586x);

	mutex_init(&tps6586x->lock);

	ret = tps6586x_add_subdevs(tps6586x, pdata);
	if (ret) {
		dev_err(&client->dev, "add devices failed: %d\n", ret);
		goto err_add_devs;
	}

	tps6586x_gpio_init(tps6586x, pdata->gpio_base);

	return 0;

err_add_devs:
	kfree(tps6586x);
	return ret;
}

static int __devexit tps6586x_i2c_remove(struct i2c_client *client)
{
	return 0;
}

static const struct i2c_device_id tps6586x_id_table[] = {
	{ "tps6586x", 0 },
	{ },
};
MODULE_DEVICE_TABLE(i2c, tps6586x_id_table);

static struct i2c_driver tps6586x_driver = {
	.driver	= {
		.name	= "tps6586x",
		.owner	= THIS_MODULE,
	},
	.probe		= tps6586x_i2c_probe,
	.remove		= __devexit_p(tps6586x_i2c_remove),
	.id_table	= tps6586x_id_table,
};

static int __init tps6586x_init(void)
{
	return i2c_add_driver(&tps6586x_driver);
}
subsys_initcall(tps6586x_init);

static void __exit tps6586x_exit(void)
{
	i2c_del_driver(&tps6586x_driver);
}
module_exit(tps6586x_exit);

MODULE_DESCRIPTION("TPS6586X core driver");
MODULE_AUTHOR("Mike Rapoport <mike@compulab.co.il>");
MODULE_LICENSE("GPL");

