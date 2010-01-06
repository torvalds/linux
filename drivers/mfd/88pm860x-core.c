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

int pm860x_device_init(struct pm8607_chip *chip,
		       struct pm8607_platform_data *pdata)
{
	int i, count;
	int ret;

	ret = pm8607_reg_read(chip, PM8607_CHIP_ID);
	if (ret < 0) {
		dev_err(chip->dev, "Failed to read CHIP ID: %d\n", ret);
		goto out;
	}
	if ((ret & PM8607_ID_MASK) == PM8607_ID)
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
out:
	return ret;
}

void pm8607_device_exit(struct pm8607_chip *chip)
{
	mfd_remove_devices(chip->dev);
}

MODULE_DESCRIPTION("PMIC Driver for Marvell 88PM8607");
MODULE_AUTHOR("Haojian Zhuang <haojian.zhuang@marvell.com>");
MODULE_LICENSE("GPL");
