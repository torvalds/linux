// SPDX-License-Identifier: GPL-2.0
/*
 * Device access for Crystal Cove PMIC
 *
 * Copyright (C) 2012-2014, 2022 Intel Corporation. All rights reserved.
 *
 * Author: Yang, Bin <bin.yang@intel.com>
 * Author: Zhu, Lejun <lejun.zhu@linux.intel.com>
 */

#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/mfd/core.h>
#include <linux/mfd/intel_soc_pmic.h>
#include <linux/platform_data/x86/soc.h>
#include <linux/pwm.h>
#include <linux/regmap.h>

#define CRYSTAL_COVE_MAX_REGISTER	0xC6

#define CRYSTAL_COVE_REG_IRQLVL1	0x02
#define CRYSTAL_COVE_REG_MIRQLVL1	0x0E

#define CRYSTAL_COVE_IRQ_PWRSRC		0
#define CRYSTAL_COVE_IRQ_THRM		1
#define CRYSTAL_COVE_IRQ_BCU		2
#define CRYSTAL_COVE_IRQ_ADC		3
#define CRYSTAL_COVE_IRQ_CHGR		4
#define CRYSTAL_COVE_IRQ_GPIO		5
#define CRYSTAL_COVE_IRQ_VHDMIOCP	6

static const struct resource pwrsrc_resources[] = {
	DEFINE_RES_IRQ_NAMED(CRYSTAL_COVE_IRQ_PWRSRC, "PWRSRC"),
};

static const struct resource thermal_resources[] = {
	DEFINE_RES_IRQ_NAMED(CRYSTAL_COVE_IRQ_THRM, "THERMAL"),
};

static const struct resource bcu_resources[] = {
	DEFINE_RES_IRQ_NAMED(CRYSTAL_COVE_IRQ_BCU, "BCU"),
};

static const struct resource adc_resources[] = {
	DEFINE_RES_IRQ_NAMED(CRYSTAL_COVE_IRQ_ADC, "ADC"),
};

static const struct resource charger_resources[] = {
	DEFINE_RES_IRQ_NAMED(CRYSTAL_COVE_IRQ_CHGR, "CHGR"),
};

static const struct resource gpio_resources[] = {
	DEFINE_RES_IRQ_NAMED(CRYSTAL_COVE_IRQ_GPIO, "GPIO"),
};

static struct mfd_cell crystal_cove_byt_dev[] = {
	{
		.name = "crystal_cove_pwrsrc",
		.num_resources = ARRAY_SIZE(pwrsrc_resources),
		.resources = pwrsrc_resources,
	},
	{
		.name = "crystal_cove_thermal",
		.num_resources = ARRAY_SIZE(thermal_resources),
		.resources = thermal_resources,
	},
	{
		.name = "crystal_cove_bcu",
		.num_resources = ARRAY_SIZE(bcu_resources),
		.resources = bcu_resources,
	},
	{
		.name = "crystal_cove_adc",
		.num_resources = ARRAY_SIZE(adc_resources),
		.resources = adc_resources,
	},
	{
		.name = "crystal_cove_charger",
		.num_resources = ARRAY_SIZE(charger_resources),
		.resources = charger_resources,
	},
	{
		.name = "crystal_cove_gpio",
		.num_resources = ARRAY_SIZE(gpio_resources),
		.resources = gpio_resources,
	},
	{
		.name = "byt_crystal_cove_pmic",
	},
	{
		.name = "crystal_cove_pwm",
	},
};

static struct mfd_cell crystal_cove_cht_dev[] = {
	{
		.name = "crystal_cove_gpio",
		.num_resources = ARRAY_SIZE(gpio_resources),
		.resources = gpio_resources,
	},
	{
		.name = "cht_crystal_cove_pmic",
	},
	{
		.name = "crystal_cove_pwm",
	},
};

static const struct regmap_config crystal_cove_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,

	.max_register = CRYSTAL_COVE_MAX_REGISTER,
	.cache_type = REGCACHE_NONE,
};

static const struct regmap_irq crystal_cove_irqs[] = {
	REGMAP_IRQ_REG(CRYSTAL_COVE_IRQ_PWRSRC, 0, BIT(CRYSTAL_COVE_IRQ_PWRSRC)),
	REGMAP_IRQ_REG(CRYSTAL_COVE_IRQ_THRM, 0, BIT(CRYSTAL_COVE_IRQ_THRM)),
	REGMAP_IRQ_REG(CRYSTAL_COVE_IRQ_BCU, 0, BIT(CRYSTAL_COVE_IRQ_BCU)),
	REGMAP_IRQ_REG(CRYSTAL_COVE_IRQ_ADC, 0, BIT(CRYSTAL_COVE_IRQ_ADC)),
	REGMAP_IRQ_REG(CRYSTAL_COVE_IRQ_CHGR, 0, BIT(CRYSTAL_COVE_IRQ_CHGR)),
	REGMAP_IRQ_REG(CRYSTAL_COVE_IRQ_GPIO, 0, BIT(CRYSTAL_COVE_IRQ_GPIO)),
	REGMAP_IRQ_REG(CRYSTAL_COVE_IRQ_VHDMIOCP, 0, BIT(CRYSTAL_COVE_IRQ_VHDMIOCP)),
};

static const struct regmap_irq_chip crystal_cove_irq_chip = {
	.name = "Crystal Cove",
	.irqs = crystal_cove_irqs,
	.num_irqs = ARRAY_SIZE(crystal_cove_irqs),
	.num_regs = 1,
	.status_base = CRYSTAL_COVE_REG_IRQLVL1,
	.mask_base = CRYSTAL_COVE_REG_MIRQLVL1,
};

/* PWM consumed by the Intel GFX */
static struct pwm_lookup crc_pwm_lookup[] = {
	PWM_LOOKUP("crystal_cove_pwm", 0, "0000:00:02.0", "pwm_pmic_backlight", 0, PWM_POLARITY_NORMAL),
};

struct crystal_cove_config {
	unsigned long irq_flags;
	struct mfd_cell *cell_dev;
	int n_cell_devs;
	const struct regmap_config *regmap_config;
	const struct regmap_irq_chip *irq_chip;
};

static const struct crystal_cove_config crystal_cove_config_byt_crc = {
	.irq_flags = IRQF_TRIGGER_RISING,
	.cell_dev = crystal_cove_byt_dev,
	.n_cell_devs = ARRAY_SIZE(crystal_cove_byt_dev),
	.regmap_config = &crystal_cove_regmap_config,
	.irq_chip = &crystal_cove_irq_chip,
};

static const struct crystal_cove_config crystal_cove_config_cht_crc = {
	.irq_flags = IRQF_TRIGGER_RISING,
	.cell_dev = crystal_cove_cht_dev,
	.n_cell_devs = ARRAY_SIZE(crystal_cove_cht_dev),
	.regmap_config = &crystal_cove_regmap_config,
	.irq_chip = &crystal_cove_irq_chip,
};

static int crystal_cove_i2c_probe(struct i2c_client *i2c)
{
	const struct crystal_cove_config *config;
	struct device *dev = &i2c->dev;
	struct intel_soc_pmic *pmic;
	int ret;

	if (soc_intel_is_byt())
		config = &crystal_cove_config_byt_crc;
	else
		config = &crystal_cove_config_cht_crc;

	pmic = devm_kzalloc(dev, sizeof(*pmic), GFP_KERNEL);
	if (!pmic)
		return -ENOMEM;

	i2c_set_clientdata(i2c, pmic);

	pmic->regmap = devm_regmap_init_i2c(i2c, config->regmap_config);
	if (IS_ERR(pmic->regmap))
		return PTR_ERR(pmic->regmap);

	pmic->irq = i2c->irq;

	ret = devm_regmap_add_irq_chip(dev, pmic->regmap, pmic->irq,
				       config->irq_flags | IRQF_ONESHOT,
				       0, config->irq_chip, &pmic->irq_chip_data);
	if (ret)
		return ret;

	ret = enable_irq_wake(pmic->irq);
	if (ret)
		dev_warn(dev, "Can't enable IRQ as wake source: %d\n", ret);

	/* Add lookup table for crc-pwm */
	pwm_add_table(crc_pwm_lookup, ARRAY_SIZE(crc_pwm_lookup));

	/* To distuingish this domain from the GPIO/charger's irqchip domains */
	irq_domain_update_bus_token(regmap_irq_get_domain(pmic->irq_chip_data),
				    DOMAIN_BUS_NEXUS);

	ret = mfd_add_devices(dev, PLATFORM_DEVID_NONE, config->cell_dev,
			      config->n_cell_devs, NULL, 0,
			      regmap_irq_get_domain(pmic->irq_chip_data));
	if (ret)
		pwm_remove_table(crc_pwm_lookup, ARRAY_SIZE(crc_pwm_lookup));

	return ret;
}

static void crystal_cove_i2c_remove(struct i2c_client *i2c)
{
	/* remove crc-pwm lookup table */
	pwm_remove_table(crc_pwm_lookup, ARRAY_SIZE(crc_pwm_lookup));

	mfd_remove_devices(&i2c->dev);
}

static void crystal_cove_shutdown(struct i2c_client *i2c)
{
	struct intel_soc_pmic *pmic = i2c_get_clientdata(i2c);

	disable_irq(pmic->irq);

	return;
}

static int crystal_cove_suspend(struct device *dev)
{
	struct intel_soc_pmic *pmic = dev_get_drvdata(dev);

	disable_irq(pmic->irq);

	return 0;
}

static int crystal_cove_resume(struct device *dev)
{
	struct intel_soc_pmic *pmic = dev_get_drvdata(dev);

	enable_irq(pmic->irq);

	return 0;
}

static DEFINE_SIMPLE_DEV_PM_OPS(crystal_cove_pm_ops, crystal_cove_suspend, crystal_cove_resume);

static const struct acpi_device_id crystal_cove_acpi_match[] = {
	{ "INT33FD" },
	{ },
};
MODULE_DEVICE_TABLE(acpi, crystal_cove_acpi_match);

static struct i2c_driver crystal_cove_i2c_driver = {
	.driver = {
		.name = "crystal_cove_i2c",
		.pm = pm_sleep_ptr(&crystal_cove_pm_ops),
		.acpi_match_table = crystal_cove_acpi_match,
	},
	.probe = crystal_cove_i2c_probe,
	.remove = crystal_cove_i2c_remove,
	.shutdown = crystal_cove_shutdown,
};

module_i2c_driver(crystal_cove_i2c_driver);

MODULE_DESCRIPTION("I2C driver for Intel SoC PMIC");
MODULE_AUTHOR("Yang, Bin <bin.yang@intel.com>");
MODULE_AUTHOR("Zhu, Lejun <lejun.zhu@linux.intel.com>");
