/*
 * da9063-core.c: Device access for Dialog DA9063 modules
 *
 * Copyright 2012 Dialog Semiconductors Ltd.
 * Copyright 2013 Philipp Zabel, Pengutronix
 *
 * Author: Krystian Garbaciak <krystian.garbaciak@diasemi.com>,
 *         Michal Hajduk <michal.hajduk@diasemi.com>
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 *
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/device.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/mutex.h>
#include <linux/mfd/core.h>
#include <linux/regmap.h>

#include <linux/mfd/da9063/core.h>
#include <linux/mfd/da9063/pdata.h>
#include <linux/mfd/da9063/registers.h>

#include <linux/proc_fs.h>
#include <linux/kthread.h>
#include <linux/uaccess.h>


static struct resource da9063_regulators_resources[] = {
	{
		.name	= "LDO_LIM",
		.start	= DA9063_IRQ_LDO_LIM,
		.end	= DA9063_IRQ_LDO_LIM,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct resource da9063_rtc_resources[] = {
	{
		.name	= "ALARM",
		.start	= DA9063_IRQ_ALARM,
		.end	= DA9063_IRQ_ALARM,
		.flags	= IORESOURCE_IRQ,
	},
	{
		.name	= "TICK",
		.start	= DA9063_IRQ_TICK,
		.end	= DA9063_IRQ_TICK,
		.flags	= IORESOURCE_IRQ,
	}
};

static struct resource da9063_onkey_resources[] = {
	{
		.start	= DA9063_IRQ_ONKEY,
		.end	= DA9063_IRQ_ONKEY,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct resource da9063_hwmon_resources[] = {
	{
		.start	= DA9063_IRQ_ADC_RDY,
		.end	= DA9063_IRQ_ADC_RDY,
		.flags	= IORESOURCE_IRQ,
	},
};


static const struct mfd_cell da9063_devs[] = {
	{
		.name		= DA9063_DRVNAME_REGULATORS,
		.num_resources	= ARRAY_SIZE(da9063_regulators_resources),
		.resources	= da9063_regulators_resources,
	},
	{
		.name		= DA9063_DRVNAME_LEDS,
	},
	{
		.name		= DA9063_DRVNAME_WATCHDOG,
	},
	{
		.name		= DA9063_DRVNAME_HWMON,
		.num_resources	= ARRAY_SIZE(da9063_hwmon_resources),
		.resources	= da9063_hwmon_resources,
	},
	{
		.name		= DA9063_DRVNAME_ONKEY,
		.num_resources	= ARRAY_SIZE(da9063_onkey_resources),
		.resources	= da9063_onkey_resources,
	},
	{
		.name		= DA9063_DRVNAME_RTC,
		.num_resources	= ARRAY_SIZE(da9063_rtc_resources),
		.resources	= da9063_rtc_resources,
	},
	{
		.name		= DA9063_DRVNAME_VIBRATION,
	},
};

int da9063_device_init(struct da9063 *da9063, unsigned int irq)
{
	struct da9063_pdata *pdata = da9063->dev->platform_data;
	int model, variant_id, variant_code;
	int ret;

	if (pdata) {
		da9063->flags = pdata->flags;
		da9063->irq_base = pdata->irq_base;
	} else {
		da9063->flags = 0;
		da9063->irq_base = -1;
	}
	da9063->chip_irq = irq;

	if (pdata && pdata->init != NULL) {
		ret = pdata->init(da9063);
		if (ret != 0) {
			dev_err(da9063->dev,
				"Platform initialization failed.\n");
			return ret;
		}
	}

	ret = regmap_read(da9063->regmap, DA9063_REG_CHIP_ID, &model);
	if (ret < 0) {
		dev_err(da9063->dev, "Cannot read chip model id.\n");
		return -EIO;
	}
	if (model != PMIC_DA9063) {
		dev_err(da9063->dev, "Invalid chip model id: 0x%02x\n", model);
		return -ENODEV;
	}

	ret = regmap_read(da9063->regmap, DA9063_REG_CHIP_VARIANT, &variant_id);
	if (ret < 0) {
		dev_err(da9063->dev, "Cannot read chip variant id.\n");
		return -EIO;
	}

	variant_code = variant_id >> DA9063_CHIP_VARIANT_SHIFT;

	dev_info(da9063->dev,
		 "Device detected (chip-ID: 0x%02X, var-ID: 0x%02X)\n",
		 model, variant_id);

	if (variant_code < PMIC_DA9063_BB && variant_code != PMIC_DA9063_AD) {
		dev_err(da9063->dev,
			"Cannot support variant code: 0x%02X\n", variant_code);
		return -ENODEV;
	}

	da9063->model = model;
	da9063->variant_code = variant_code;

	ret = da9063_irq_init(da9063);
	if (ret) {
		dev_err(da9063->dev, "Cannot initialize interrupts.\n");
		return ret;
	}

	da9063->irq_base = regmap_irq_chip_get_base(da9063->regmap_irq);

	ret = mfd_add_devices(da9063->dev, -1, da9063_devs,
			      ARRAY_SIZE(da9063_devs), NULL, da9063->irq_base,
			      NULL);
	if (ret)
		dev_err(da9063->dev, "Cannot add MFD cells\n");

	return ret;
}

void da9063_device_exit(struct da9063 *da9063)
{
	mfd_remove_devices(da9063->dev);
	da9063_irq_exit(da9063);
}

MODULE_DESCRIPTION("PMIC driver for Dialog DA9063");
MODULE_AUTHOR("Krystian Garbaciak <krystian.garbaciak@diasemi.com>, Michal Hajduk <michal.hajduk@diasemi.com>");
MODULE_LICENSE("GPL");
