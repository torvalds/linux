/*
 * intel_soc_pmic_core.c - Intel SoC PMIC MFD Driver
 *
 * Copyright (C) 2013, 2014 Intel Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version
 * 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * Author: Yang, Bin <bin.yang@intel.com>
 * Author: Zhu, Lejun <lejun.zhu@linux.intel.com>
 */

#include <linux/module.h>
#include <linux/mfd/core.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/gpio/consumer.h>
#include <linux/acpi.h>
#include <linux/regmap.h>
#include <linux/mfd/intel_soc_pmic.h>
#include <linux/gpio/machine.h>
#include <linux/pwm.h>
#include "intel_soc_pmic_core.h"

/* Lookup table for the Panel Enable/Disable line as GPIO signals */
static struct gpiod_lookup_table panel_gpio_table = {
	/* Intel GFX is consumer */
	.dev_id = "0000:00:02.0",
	.table = {
		/* Panel EN/DISABLE */
		GPIO_LOOKUP("gpio_crystalcove", 94, "panel", GPIO_ACTIVE_HIGH),
		{ },
	},
};

/* PWM consumed by the Intel GFX */
static struct pwm_lookup crc_pwm_lookup[] = {
	PWM_LOOKUP("crystal_cove_pwm", 0, "0000:00:02.0", "pwm_backlight", 0, PWM_POLARITY_NORMAL),
};

static int intel_soc_pmic_i2c_probe(struct i2c_client *i2c,
				    const struct i2c_device_id *i2c_id)
{
	struct device *dev = &i2c->dev;
	const struct acpi_device_id *id;
	struct intel_soc_pmic_config *config;
	struct intel_soc_pmic *pmic;
	int ret;

	id = acpi_match_device(dev->driver->acpi_match_table, dev);
	if (!id || !id->driver_data)
		return -ENODEV;

	config = (struct intel_soc_pmic_config *)id->driver_data;

	pmic = devm_kzalloc(dev, sizeof(*pmic), GFP_KERNEL);
	if (!pmic)
		return -ENOMEM;

	dev_set_drvdata(dev, pmic);

	pmic->regmap = devm_regmap_init_i2c(i2c, config->regmap_config);
	if (IS_ERR(pmic->regmap))
		return PTR_ERR(pmic->regmap);

	pmic->irq = i2c->irq;

	ret = regmap_add_irq_chip(pmic->regmap, pmic->irq,
				  config->irq_flags | IRQF_ONESHOT,
				  0, config->irq_chip,
				  &pmic->irq_chip_data);
	if (ret)
		return ret;

	ret = enable_irq_wake(pmic->irq);
	if (ret)
		dev_warn(dev, "Can't enable IRQ as wake source: %d\n", ret);

	/* Add lookup table binding for Panel Control to the GPIO Chip */
	gpiod_add_lookup_table(&panel_gpio_table);

	/* Add lookup table for crc-pwm */
	pwm_add_table(crc_pwm_lookup, ARRAY_SIZE(crc_pwm_lookup));

	ret = mfd_add_devices(dev, -1, config->cell_dev,
			      config->n_cell_devs, NULL, 0,
			      regmap_irq_get_domain(pmic->irq_chip_data));
	if (ret)
		goto err_del_irq_chip;

	return 0;

err_del_irq_chip:
	regmap_del_irq_chip(pmic->irq, pmic->irq_chip_data);
	return ret;
}

static int intel_soc_pmic_i2c_remove(struct i2c_client *i2c)
{
	struct intel_soc_pmic *pmic = dev_get_drvdata(&i2c->dev);

	regmap_del_irq_chip(pmic->irq, pmic->irq_chip_data);

	/* Remove lookup table for Panel Control from the GPIO Chip */
	gpiod_remove_lookup_table(&panel_gpio_table);

	/* remove crc-pwm lookup table */
	pwm_remove_table(crc_pwm_lookup, ARRAY_SIZE(crc_pwm_lookup));

	mfd_remove_devices(&i2c->dev);

	return 0;
}

static void intel_soc_pmic_shutdown(struct i2c_client *i2c)
{
	struct intel_soc_pmic *pmic = dev_get_drvdata(&i2c->dev);

	disable_irq(pmic->irq);

	return;
}

#if defined(CONFIG_PM_SLEEP)
static int intel_soc_pmic_suspend(struct device *dev)
{
	struct intel_soc_pmic *pmic = dev_get_drvdata(dev);

	disable_irq(pmic->irq);

	return 0;
}

static int intel_soc_pmic_resume(struct device *dev)
{
	struct intel_soc_pmic *pmic = dev_get_drvdata(dev);

	enable_irq(pmic->irq);

	return 0;
}
#endif

static SIMPLE_DEV_PM_OPS(intel_soc_pmic_pm_ops, intel_soc_pmic_suspend,
			 intel_soc_pmic_resume);

static const struct i2c_device_id intel_soc_pmic_i2c_id[] = {
	{ }
};
MODULE_DEVICE_TABLE(i2c, intel_soc_pmic_i2c_id);

#if defined(CONFIG_ACPI)
static const struct acpi_device_id intel_soc_pmic_acpi_match[] = {
	{"INT33FD", (kernel_ulong_t)&intel_soc_pmic_config_crc},
	{ },
};
MODULE_DEVICE_TABLE(acpi, intel_soc_pmic_acpi_match);
#endif

static struct i2c_driver intel_soc_pmic_i2c_driver = {
	.driver = {
		.name = "intel_soc_pmic_i2c",
		.pm = &intel_soc_pmic_pm_ops,
		.acpi_match_table = ACPI_PTR(intel_soc_pmic_acpi_match),
	},
	.probe = intel_soc_pmic_i2c_probe,
	.remove = intel_soc_pmic_i2c_remove,
	.id_table = intel_soc_pmic_i2c_id,
	.shutdown = intel_soc_pmic_shutdown,
};

module_i2c_driver(intel_soc_pmic_i2c_driver);

MODULE_DESCRIPTION("I2C driver for Intel SoC PMIC");
MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Yang, Bin <bin.yang@intel.com>");
MODULE_AUTHOR("Zhu, Lejun <lejun.zhu@linux.intel.com>");
