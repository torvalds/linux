// SPDX-License-Identifier: GPL-2.0
/*
 * MFD core driver for Intel Cherrytrail Whiskey Cove PMIC
 *
 * Copyright (C) 2017 Hans de Goede <hdegoede@redhat.com>
 *
 * Based on various non upstream patches to support the CHT Whiskey Cove PMIC:
 * Copyright (C) 2013-2015 Intel Corporation. All rights reserved.
 */

#include <linux/acpi.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/mfd/core.h>
#include <linux/mfd/intel_soc_pmic.h>
#include <linux/regmap.h>

/* PMIC device registers */
#define REG_OFFSET_MASK		GENMASK(7, 0)
#define REG_ADDR_MASK		GENMASK(15, 8)
#define REG_ADDR_SHIFT		8

#define CHT_WC_IRQLVL1		0x6e02
#define CHT_WC_IRQLVL1_MASK	0x6e0e

/* Whiskey Cove PMIC share same ACPI ID between different platforms */
#define CHT_WC_HRV		3

/* Level 1 IRQs (level 2 IRQs are handled in the child device drivers) */
enum {
	CHT_WC_PWRSRC_IRQ = 0,
	CHT_WC_THRM_IRQ,
	CHT_WC_BCU_IRQ,
	CHT_WC_ADC_IRQ,
	CHT_WC_EXT_CHGR_IRQ,
	CHT_WC_GPIO_IRQ,
	/* There is no irq 6 */
	CHT_WC_CRIT_IRQ = 7,
};

static struct resource cht_wc_pwrsrc_resources[] = {
	DEFINE_RES_IRQ(CHT_WC_PWRSRC_IRQ),
};

static struct resource cht_wc_ext_charger_resources[] = {
	DEFINE_RES_IRQ(CHT_WC_EXT_CHGR_IRQ),
};

static struct mfd_cell cht_wc_dev[] = {
	{
		.name = "cht_wcove_pwrsrc",
		.num_resources = ARRAY_SIZE(cht_wc_pwrsrc_resources),
		.resources = cht_wc_pwrsrc_resources,
	}, {
		.name = "cht_wcove_ext_chgr",
		.num_resources = ARRAY_SIZE(cht_wc_ext_charger_resources),
		.resources = cht_wc_ext_charger_resources,
	},
	{	.name = "cht_wcove_region", },
};

/*
 * The CHT Whiskey Cove covers multiple I2C addresses, with a 1 Byte
 * register address space per I2C address, so we use 16 bit register
 * addresses where the high 8 bits contain the I2C client address.
 */
static int cht_wc_byte_reg_read(void *context, unsigned int reg,
				unsigned int *val)
{
	struct i2c_client *client = context;
	int ret, orig_addr = client->addr;

	if (!(reg & REG_ADDR_MASK)) {
		dev_err(&client->dev, "Error I2C address not specified\n");
		return -EINVAL;
	}

	client->addr = (reg & REG_ADDR_MASK) >> REG_ADDR_SHIFT;
	ret = i2c_smbus_read_byte_data(client, reg & REG_OFFSET_MASK);
	client->addr = orig_addr;

	if (ret < 0)
		return ret;

	*val = ret;
	return 0;
}

static int cht_wc_byte_reg_write(void *context, unsigned int reg,
				 unsigned int val)
{
	struct i2c_client *client = context;
	int ret, orig_addr = client->addr;

	if (!(reg & REG_ADDR_MASK)) {
		dev_err(&client->dev, "Error I2C address not specified\n");
		return -EINVAL;
	}

	client->addr = (reg & REG_ADDR_MASK) >> REG_ADDR_SHIFT;
	ret = i2c_smbus_write_byte_data(client, reg & REG_OFFSET_MASK, val);
	client->addr = orig_addr;

	return ret;
}

static const struct regmap_config cht_wc_regmap_cfg = {
	.reg_bits = 16,
	.val_bits = 8,
	.reg_write = cht_wc_byte_reg_write,
	.reg_read = cht_wc_byte_reg_read,
};

static const struct regmap_irq cht_wc_regmap_irqs[] = {
	REGMAP_IRQ_REG(CHT_WC_PWRSRC_IRQ, 0, BIT(CHT_WC_PWRSRC_IRQ)),
	REGMAP_IRQ_REG(CHT_WC_THRM_IRQ, 0, BIT(CHT_WC_THRM_IRQ)),
	REGMAP_IRQ_REG(CHT_WC_BCU_IRQ, 0, BIT(CHT_WC_BCU_IRQ)),
	REGMAP_IRQ_REG(CHT_WC_ADC_IRQ, 0, BIT(CHT_WC_ADC_IRQ)),
	REGMAP_IRQ_REG(CHT_WC_EXT_CHGR_IRQ, 0, BIT(CHT_WC_EXT_CHGR_IRQ)),
	REGMAP_IRQ_REG(CHT_WC_GPIO_IRQ, 0, BIT(CHT_WC_GPIO_IRQ)),
	REGMAP_IRQ_REG(CHT_WC_CRIT_IRQ, 0, BIT(CHT_WC_CRIT_IRQ)),
};

static const struct regmap_irq_chip cht_wc_regmap_irq_chip = {
	.name = "cht_wc_irq_chip",
	.status_base = CHT_WC_IRQLVL1,
	.mask_base = CHT_WC_IRQLVL1_MASK,
	.irqs = cht_wc_regmap_irqs,
	.num_irqs = ARRAY_SIZE(cht_wc_regmap_irqs),
	.num_regs = 1,
};

static int cht_wc_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct intel_soc_pmic *pmic;
	acpi_status status;
	unsigned long long hrv;
	int ret;

	status = acpi_evaluate_integer(ACPI_HANDLE(dev), "_HRV", NULL, &hrv);
	if (ACPI_FAILURE(status)) {
		dev_err(dev, "Failed to get PMIC hardware revision\n");
		return -ENODEV;
	}
	if (hrv != CHT_WC_HRV) {
		dev_err(dev, "Invalid PMIC hardware revision: %llu\n", hrv);
		return -ENODEV;
	}
	if (client->irq < 0) {
		dev_err(dev, "Invalid IRQ\n");
		return -EINVAL;
	}

	pmic = devm_kzalloc(dev, sizeof(*pmic), GFP_KERNEL);
	if (!pmic)
		return -ENOMEM;

	pmic->irq = client->irq;
	pmic->dev = dev;
	i2c_set_clientdata(client, pmic);

	pmic->regmap = devm_regmap_init(dev, NULL, client, &cht_wc_regmap_cfg);
	if (IS_ERR(pmic->regmap))
		return PTR_ERR(pmic->regmap);

	ret = devm_regmap_add_irq_chip(dev, pmic->regmap, pmic->irq,
				       IRQF_ONESHOT | IRQF_SHARED, 0,
				       &cht_wc_regmap_irq_chip,
				       &pmic->irq_chip_data);
	if (ret)
		return ret;

	return devm_mfd_add_devices(dev, PLATFORM_DEVID_NONE,
				cht_wc_dev, ARRAY_SIZE(cht_wc_dev), NULL, 0,
				regmap_irq_get_domain(pmic->irq_chip_data));
}

static void cht_wc_shutdown(struct i2c_client *client)
{
	struct intel_soc_pmic *pmic = i2c_get_clientdata(client);

	disable_irq(pmic->irq);
}

static int __maybe_unused cht_wc_suspend(struct device *dev)
{
	struct intel_soc_pmic *pmic = dev_get_drvdata(dev);

	disable_irq(pmic->irq);

	return 0;
}

static int __maybe_unused cht_wc_resume(struct device *dev)
{
	struct intel_soc_pmic *pmic = dev_get_drvdata(dev);

	enable_irq(pmic->irq);

	return 0;
}
static SIMPLE_DEV_PM_OPS(cht_wc_pm_ops, cht_wc_suspend, cht_wc_resume);

static const struct i2c_device_id cht_wc_i2c_id[] = {
	{ }
};

static const struct acpi_device_id cht_wc_acpi_ids[] = {
	{ "INT34D3", },
	{ }
};

static struct i2c_driver cht_wc_driver = {
	.driver	= {
		.name	= "CHT Whiskey Cove PMIC",
		.pm     = &cht_wc_pm_ops,
		.acpi_match_table = cht_wc_acpi_ids,
	},
	.probe_new = cht_wc_probe,
	.shutdown = cht_wc_shutdown,
	.id_table = cht_wc_i2c_id,
};
builtin_i2c_driver(cht_wc_driver);
