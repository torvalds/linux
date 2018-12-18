/*
 * da9063-core.c: Device access for Dialog DA9063 modules
 *
 * Copyright 2012 Dialog Semiconductors Ltd.
 * Copyright 2013 Philipp Zabel, Pengutronix
 *
 * Author: Krystian Garbaciak, Dialog Semiconductor
 * Author: Michal Hajduk, Dialog Semiconductor
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
		.name	= "ONKEY",
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


static const struct mfd_cell da9063_common_devs[] = {
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
		.of_compatible	= "dlg,da9063-watchdog",
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
		.of_compatible = "dlg,da9063-onkey",
	},
	{
		.name		= DA9063_DRVNAME_VIBRATION,
	},
};

/* Only present on DA9063 , not on DA9063L */
static const struct mfd_cell da9063_devs[] = {
	{
		.name		= DA9063_DRVNAME_RTC,
		.num_resources	= ARRAY_SIZE(da9063_rtc_resources),
		.resources	= da9063_rtc_resources,
		.of_compatible	= "dlg,da9063-rtc",
	},
};

static int da9063_clear_fault_log(struct da9063 *da9063)
{
	int ret = 0;
	int fault_log = 0;

	ret = regmap_read(da9063->regmap, DA9063_REG_FAULT_LOG, &fault_log);
	if (ret < 0) {
		dev_err(da9063->dev, "Cannot read FAULT_LOG.\n");
		return -EIO;
	}

	if (fault_log) {
		if (fault_log & DA9063_TWD_ERROR)
			dev_dbg(da9063->dev,
				"Fault log entry detected: DA9063_TWD_ERROR\n");
		if (fault_log & DA9063_POR)
			dev_dbg(da9063->dev,
				"Fault log entry detected: DA9063_POR\n");
		if (fault_log & DA9063_VDD_FAULT)
			dev_dbg(da9063->dev,
				"Fault log entry detected: DA9063_VDD_FAULT\n");
		if (fault_log & DA9063_VDD_START)
			dev_dbg(da9063->dev,
				"Fault log entry detected: DA9063_VDD_START\n");
		if (fault_log & DA9063_TEMP_CRIT)
			dev_dbg(da9063->dev,
				"Fault log entry detected: DA9063_TEMP_CRIT\n");
		if (fault_log & DA9063_KEY_RESET)
			dev_dbg(da9063->dev,
				"Fault log entry detected: DA9063_KEY_RESET\n");
		if (fault_log & DA9063_NSHUTDOWN)
			dev_dbg(da9063->dev,
				"Fault log entry detected: DA9063_NSHUTDOWN\n");
		if (fault_log & DA9063_WAIT_SHUT)
			dev_dbg(da9063->dev,
				"Fault log entry detected: DA9063_WAIT_SHUT\n");
	}

	ret = regmap_write(da9063->regmap,
			   DA9063_REG_FAULT_LOG,
			   fault_log);
	if (ret < 0)
		dev_err(da9063->dev,
			"Cannot reset FAULT_LOG values %d\n", ret);

	return ret;
}

int da9063_device_init(struct da9063 *da9063, unsigned int irq)
{
	struct da9063_pdata *pdata = da9063->dev->platform_data;
	int model, variant_id, variant_code;
	int ret;

	ret = da9063_clear_fault_log(da9063);
	if (ret < 0)
		dev_err(da9063->dev, "Cannot clear fault log\n");

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
	if (model != PMIC_CHIP_ID_DA9063) {
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

	da9063->variant_code = variant_code;

	ret = da9063_irq_init(da9063);
	if (ret) {
		dev_err(da9063->dev, "Cannot initialize interrupts.\n");
		return ret;
	}

	da9063->irq_base = regmap_irq_chip_get_base(da9063->regmap_irq);

	ret = devm_mfd_add_devices(da9063->dev, PLATFORM_DEVID_NONE,
				   da9063_common_devs,
				   ARRAY_SIZE(da9063_common_devs),
				   NULL, da9063->irq_base, NULL);
	if (ret) {
		dev_err(da9063->dev, "Failed to add child devices\n");
		return ret;
	}

	if (da9063->type == PMIC_TYPE_DA9063) {
		ret = devm_mfd_add_devices(da9063->dev, PLATFORM_DEVID_NONE,
					   da9063_devs, ARRAY_SIZE(da9063_devs),
					   NULL, da9063->irq_base, NULL);
		if (ret) {
			dev_err(da9063->dev, "Failed to add child devices\n");
			return ret;
		}
	}

	return ret;
}

MODULE_DESCRIPTION("PMIC driver for Dialog DA9063");
MODULE_AUTHOR("Krystian Garbaciak");
MODULE_AUTHOR("Michal Hajduk");
MODULE_LICENSE("GPL");
