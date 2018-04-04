/*
 * Device access for Dollar Cove TI PMIC
 *
 * Copyright (c) 2014, Intel Corporation.
 *   Author: Ramakrishna Pallala <ramakrishna.pallala@intel.com>
 *
 * Cleanup and forward-ported
 *   Copyright (c) 2017 Takashi Iwai <tiwai@suse.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/acpi.h>
#include <linux/interrupt.h>
#include <linux/i2c.h>
#include <linux/mfd/core.h>
#include <linux/mfd/intel_soc_pmic.h>
#include <linux/module.h>
#include <linux/regmap.h>

#define CHTDC_TI_IRQLVL1	0x01
#define CHTDC_TI_MASK_IRQLVL1	0x02

/* Level 1 IRQs */
enum {
	CHTDC_TI_PWRBTN = 0,	/* power button */
	CHTDC_TI_DIETMPWARN,	/* thermal */
	CHTDC_TI_ADCCMPL,	/* ADC */
	/* No IRQ 3 */
	CHTDC_TI_VBATLOW = 4,	/* battery */
	CHTDC_TI_VBUSDET,	/* power source */
	/* No IRQ 6 */
	CHTDC_TI_CCEOCAL = 7,	/* battery */
};

static struct resource power_button_resources[] = {
	DEFINE_RES_IRQ(CHTDC_TI_PWRBTN),
};

static struct resource thermal_resources[] = {
	DEFINE_RES_IRQ(CHTDC_TI_DIETMPWARN),
};

static struct resource adc_resources[] = {
	DEFINE_RES_IRQ(CHTDC_TI_ADCCMPL),
};

static struct resource pwrsrc_resources[] = {
	DEFINE_RES_IRQ(CHTDC_TI_VBUSDET),
};

static struct resource battery_resources[] = {
	DEFINE_RES_IRQ(CHTDC_TI_VBATLOW),
	DEFINE_RES_IRQ(CHTDC_TI_CCEOCAL),
};

static struct mfd_cell chtdc_ti_dev[] = {
	{
		.name = "chtdc_ti_pwrbtn",
		.num_resources = ARRAY_SIZE(power_button_resources),
		.resources = power_button_resources,
	}, {
		.name = "chtdc_ti_adc",
		.num_resources = ARRAY_SIZE(adc_resources),
		.resources = adc_resources,
	}, {
		.name = "chtdc_ti_thermal",
		.num_resources = ARRAY_SIZE(thermal_resources),
		.resources = thermal_resources,
	}, {
		.name = "chtdc_ti_pwrsrc",
		.num_resources = ARRAY_SIZE(pwrsrc_resources),
		.resources = pwrsrc_resources,
	}, {
		.name = "chtdc_ti_battery",
		.num_resources = ARRAY_SIZE(battery_resources),
		.resources = battery_resources,
	},
	{	.name = "chtdc_ti_region", },
};

static const struct regmap_config chtdc_ti_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = 128,
	.cache_type = REGCACHE_NONE,
};

static const struct regmap_irq chtdc_ti_irqs[] = {
	REGMAP_IRQ_REG(CHTDC_TI_PWRBTN, 0, BIT(CHTDC_TI_PWRBTN)),
	REGMAP_IRQ_REG(CHTDC_TI_DIETMPWARN, 0, BIT(CHTDC_TI_DIETMPWARN)),
	REGMAP_IRQ_REG(CHTDC_TI_ADCCMPL, 0, BIT(CHTDC_TI_ADCCMPL)),
	REGMAP_IRQ_REG(CHTDC_TI_VBATLOW, 0, BIT(CHTDC_TI_VBATLOW)),
	REGMAP_IRQ_REG(CHTDC_TI_VBUSDET, 0, BIT(CHTDC_TI_VBUSDET)),
	REGMAP_IRQ_REG(CHTDC_TI_CCEOCAL, 0, BIT(CHTDC_TI_CCEOCAL)),
};

static const struct regmap_irq_chip chtdc_ti_irq_chip = {
	.name = KBUILD_MODNAME,
	.irqs = chtdc_ti_irqs,
	.num_irqs = ARRAY_SIZE(chtdc_ti_irqs),
	.num_regs = 1,
	.status_base = CHTDC_TI_IRQLVL1,
	.mask_base = CHTDC_TI_MASK_IRQLVL1,
	.ack_base = CHTDC_TI_IRQLVL1,
};

static int chtdc_ti_probe(struct i2c_client *i2c)
{
	struct device *dev = &i2c->dev;
	struct intel_soc_pmic *pmic;
	int ret;

	pmic = devm_kzalloc(dev, sizeof(*pmic), GFP_KERNEL);
	if (!pmic)
		return -ENOMEM;

	i2c_set_clientdata(i2c, pmic);

	pmic->regmap = devm_regmap_init_i2c(i2c, &chtdc_ti_regmap_config);
	if (IS_ERR(pmic->regmap))
		return PTR_ERR(pmic->regmap);
	pmic->irq = i2c->irq;

	ret = devm_regmap_add_irq_chip(dev, pmic->regmap, pmic->irq,
				       IRQF_ONESHOT, 0,
				       &chtdc_ti_irq_chip,
				       &pmic->irq_chip_data);
	if (ret)
		return ret;

	return devm_mfd_add_devices(dev, PLATFORM_DEVID_NONE, chtdc_ti_dev,
				    ARRAY_SIZE(chtdc_ti_dev), NULL, 0,
				    regmap_irq_get_domain(pmic->irq_chip_data));
}

static void chtdc_ti_shutdown(struct i2c_client *i2c)
{
	struct intel_soc_pmic *pmic = i2c_get_clientdata(i2c);

	disable_irq(pmic->irq);
}

static int __maybe_unused chtdc_ti_suspend(struct device *dev)
{
	struct intel_soc_pmic *pmic = dev_get_drvdata(dev);

	disable_irq(pmic->irq);

	return 0;
}

static int __maybe_unused chtdc_ti_resume(struct device *dev)
{
	struct intel_soc_pmic *pmic = dev_get_drvdata(dev);

	enable_irq(pmic->irq);

	return 0;
}

static SIMPLE_DEV_PM_OPS(chtdc_ti_pm_ops, chtdc_ti_suspend, chtdc_ti_resume);

static const struct acpi_device_id chtdc_ti_acpi_ids[] = {
	{ "INT33F5" },
	{ },
};
MODULE_DEVICE_TABLE(acpi, chtdc_ti_acpi_ids);

static struct i2c_driver chtdc_ti_i2c_driver = {
	.driver = {
		.name = "intel_soc_pmic_chtdc_ti",
		.pm = &chtdc_ti_pm_ops,
		.acpi_match_table = chtdc_ti_acpi_ids,
	},
	.probe_new = chtdc_ti_probe,
	.shutdown = chtdc_ti_shutdown,
};
module_i2c_driver(chtdc_ti_i2c_driver);

MODULE_DESCRIPTION("I2C driver for Intel SoC Dollar Cove TI PMIC");
MODULE_LICENSE("GPL v2");
