/*
 * Base driver for Marvell 88PM8607
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
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/i2c.h>
#include <linux/mfd/core.h>
#include <linux/mfd/88pm8607.h>


#define PM8607_REG_RESOURCE(_start, _end)		\
{							\
	.start	= PM8607_##_start,			\
	.end	= PM8607_##_end,			\
	.flags	= IORESOURCE_IO,			\
}

static struct resource pm8607_regulator_resources[] = {
	PM8607_REG_RESOURCE(BUCK1, BUCK1),
	PM8607_REG_RESOURCE(BUCK2, BUCK2),
	PM8607_REG_RESOURCE(BUCK3, BUCK3),
	PM8607_REG_RESOURCE(LDO1,  LDO1),
	PM8607_REG_RESOURCE(LDO2,  LDO2),
	PM8607_REG_RESOURCE(LDO3,  LDO3),
	PM8607_REG_RESOURCE(LDO4,  LDO4),
	PM8607_REG_RESOURCE(LDO5,  LDO5),
	PM8607_REG_RESOURCE(LDO6,  LDO6),
	PM8607_REG_RESOURCE(LDO7,  LDO7),
	PM8607_REG_RESOURCE(LDO8,  LDO8),
	PM8607_REG_RESOURCE(LDO9,  LDO9),
	PM8607_REG_RESOURCE(LDO10, LDO10),
	PM8607_REG_RESOURCE(LDO12, LDO12),
	PM8607_REG_RESOURCE(LDO14, LDO14),
};

#define PM8607_REG_DEVS(_name, _id)					\
{									\
	.name		= "88pm8607-" #_name,				\
	.num_resources	= 1,						\
	.resources	= &pm8607_regulator_resources[PM8607_ID_##_id],	\
}

static struct mfd_cell pm8607_devs[] = {
	PM8607_REG_DEVS(buck1, BUCK1),
	PM8607_REG_DEVS(buck2, BUCK2),
	PM8607_REG_DEVS(buck3, BUCK3),
	PM8607_REG_DEVS(ldo1,  LDO1),
	PM8607_REG_DEVS(ldo2,  LDO2),
	PM8607_REG_DEVS(ldo3,  LDO3),
	PM8607_REG_DEVS(ldo4,  LDO4),
	PM8607_REG_DEVS(ldo5,  LDO5),
	PM8607_REG_DEVS(ldo6,  LDO6),
	PM8607_REG_DEVS(ldo7,  LDO7),
	PM8607_REG_DEVS(ldo8,  LDO8),
	PM8607_REG_DEVS(ldo9,  LDO9),
	PM8607_REG_DEVS(ldo10, LDO10),
	PM8607_REG_DEVS(ldo12, LDO12),
	PM8607_REG_DEVS(ldo14, LDO14),
};

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


static const struct i2c_device_id pm8607_id_table[] = {
	{ "88PM8607", 0 },
	{}
};
MODULE_DEVICE_TABLE(i2c, pm8607_id_table);


static int __devinit pm8607_probe(struct i2c_client *client,
				  const struct i2c_device_id *id)
{
	struct pm8607_platform_data *pdata = client->dev.platform_data;
	struct pm8607_chip *chip;
	int i, count;
	int ret;

	chip = kzalloc(sizeof(struct pm8607_chip), GFP_KERNEL);
	if (chip == NULL)
		return -ENOMEM;

	chip->client = client;
	chip->dev = &client->dev;
	chip->read = pm8607_read_device;
	chip->write = pm8607_write_device;
	i2c_set_clientdata(client, chip);

	mutex_init(&chip->io_lock);
	dev_set_drvdata(chip->dev, chip);

	ret = pm8607_reg_read(chip, PM8607_CHIP_ID);
	if (ret < 0) {
		dev_err(chip->dev, "Failed to read CHIP ID: %d\n", ret);
		goto out;
	}
	if ((ret & CHIP_ID_MASK) == CHIP_ID)
		dev_info(chip->dev, "Marvell 88PM8607 (ID: %02x) detected\n",
			 ret);
	else {
		dev_err(chip->dev, "Failed to detect Marvell 88PM8607. "
			"Chip ID: %02x\n", ret);
		goto out;
	}
	chip->chip_id = ret;

	ret = pm8607_reg_read(chip, PM8607_BUCK3);
	if (ret < 0) {
		dev_err(chip->dev, "Failed to read BUCK3 register: %d\n", ret);
		goto out;
	}
	if (ret & PM8607_BUCK3_DOUBLE)
		chip->buck3_double = 1;

	ret = pm8607_reg_read(chip, PM8607_MISC1);
	if (ret < 0) {
		dev_err(chip->dev, "Failed to read MISC1 register: %d\n", ret);
		goto out;
	}
	if (pdata->i2c_port == PI2C_PORT)
		ret |= PM8607_MISC1_PI2C;
	else
		ret &= ~PM8607_MISC1_PI2C;
	ret = pm8607_reg_write(chip, PM8607_MISC1, ret);
	if (ret < 0) {
		dev_err(chip->dev, "Failed to write MISC1 register: %d\n", ret);
		goto out;
	}


	count = ARRAY_SIZE(pm8607_devs);
	for (i = 0; i < count; i++) {
		ret = mfd_add_devices(chip->dev, i, &pm8607_devs[i],
				      1, NULL, 0);
		if (ret != 0) {
			dev_err(chip->dev, "Failed to add subdevs\n");
			goto out;
		}
	}

	return 0;

out:
	i2c_set_clientdata(client, NULL);
	kfree(chip);
	return ret;
}

static int __devexit pm8607_remove(struct i2c_client *client)
{
	struct pm8607_chip *chip = i2c_get_clientdata(client);

	mfd_remove_devices(chip->dev);
	kfree(chip);
	return 0;
}

static struct i2c_driver pm8607_driver = {
	.driver	= {
		.name	= "88PM8607",
		.owner	= THIS_MODULE,
	},
	.probe		= pm8607_probe,
	.remove		= __devexit_p(pm8607_remove),
	.id_table	= pm8607_id_table,
};

static int __init pm8607_init(void)
{
	int ret;
	ret = i2c_add_driver(&pm8607_driver);
	if (ret != 0)
		pr_err("Failed to register 88PM8607 I2C driver: %d\n", ret);
	return ret;
}
subsys_initcall(pm8607_init);

static void __exit pm8607_exit(void)
{
	i2c_del_driver(&pm8607_driver);
}
module_exit(pm8607_exit);

MODULE_DESCRIPTION("PMIC Driver for Marvell 88PM8607");
MODULE_AUTHOR("Haojian Zhuang <haojian.zhuang@marvell.com>");
MODULE_LICENSE("GPL");
