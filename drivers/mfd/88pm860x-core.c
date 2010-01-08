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
#include <linux/mfd/core.h>
#include <linux/mfd/88pm860x.h>


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

static void device_8606_init(struct pm860x_chip *chip, struct i2c_client *i2c,
			     struct pm860x_platform_data *pdata)
{
}

static void device_8607_init(struct pm860x_chip *chip, struct i2c_client *i2c,
			     struct pm860x_platform_data *pdata)
{
	int i, count;
	int ret;

	ret = pm860x_reg_read(i2c, PM8607_CHIP_ID);
	if (ret < 0) {
		dev_err(chip->dev, "Failed to read CHIP ID: %d\n", ret);
		goto out;
	}
	if ((ret & PM8607_VERSION_MASK) == PM8607_VERSION)
		dev_info(chip->dev, "Marvell 88PM8607 (ID: %02x) detected\n",
			 ret);
	else {
		dev_err(chip->dev, "Failed to detect Marvell 88PM8607. "
			"Chip ID: %02x\n", ret);
		goto out;
	}
	chip->chip_version = ret;

	ret = pm860x_reg_read(i2c, PM8607_BUCK3);
	if (ret < 0) {
		dev_err(chip->dev, "Failed to read BUCK3 register: %d\n", ret);
		goto out;
	}
	if (ret & PM8607_BUCK3_DOUBLE)
		chip->buck3_double = 1;

	ret = pm860x_reg_read(i2c, PM8607_MISC1);
	if (ret < 0) {
		dev_err(chip->dev, "Failed to read MISC1 register: %d\n", ret);
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
out:
	return;
}

int pm860x_device_init(struct pm860x_chip *chip,
		       struct pm860x_platform_data *pdata)
{
	switch (chip->id) {
	case CHIP_PM8606:
		device_8606_init(chip, chip->client, pdata);
		break;
	case CHIP_PM8607:
		device_8607_init(chip, chip->client, pdata);
		break;
	}

	if (chip->companion) {
		switch (chip->id) {
		case CHIP_PM8607:
			device_8606_init(chip, chip->companion, pdata);
			break;
		case CHIP_PM8606:
			device_8607_init(chip, chip->companion, pdata);
			break;
		}
	}
	return 0;
}

void pm860x_device_exit(struct pm860x_chip *chip)
{
	mfd_remove_devices(chip->dev);
}

MODULE_DESCRIPTION("PMIC Driver for Marvell 88PM860x");
MODULE_AUTHOR("Haojian Zhuang <haojian.zhuang@marvell.com>");
MODULE_LICENSE("GPL");
