/*
 * intel_pmic_xpower.c - XPower AXP288 PMIC operation region driver
 *
 * Copyright (C) 2014 Intel Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version
 * 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/init.h>
#include <linux/acpi.h>
#include <linux/mfd/axp20x.h>
#include <linux/regmap.h>
#include <linux/platform_device.h>
#include "intel_pmic.h"

#define XPOWER_GPADC_LOW	0x5b
#define XPOWER_GPI1_CTRL	0x92

#define GPI1_LDO_MASK		GENMASK(2, 0)
#define GPI1_LDO_ON		(3 << 0)
#define GPI1_LDO_OFF		(4 << 0)

static struct pmic_table power_table[] = {
	{
		.address = 0x00,
		.reg = 0x13,
		.bit = 0x05,
	}, /* ALD1 */
	{
		.address = 0x04,
		.reg = 0x13,
		.bit = 0x06,
	}, /* ALD2 */
	{
		.address = 0x08,
		.reg = 0x13,
		.bit = 0x07,
	}, /* ALD3 */
	{
		.address = 0x0c,
		.reg = 0x12,
		.bit = 0x03,
	}, /* DLD1 */
	{
		.address = 0x10,
		.reg = 0x12,
		.bit = 0x04,
	}, /* DLD2 */
	{
		.address = 0x14,
		.reg = 0x12,
		.bit = 0x05,
	}, /* DLD3 */
	{
		.address = 0x18,
		.reg = 0x12,
		.bit = 0x06,
	}, /* DLD4 */
	{
		.address = 0x1c,
		.reg = 0x12,
		.bit = 0x00,
	}, /* ELD1 */
	{
		.address = 0x20,
		.reg = 0x12,
		.bit = 0x01,
	}, /* ELD2 */
	{
		.address = 0x24,
		.reg = 0x12,
		.bit = 0x02,
	}, /* ELD3 */
	{
		.address = 0x28,
		.reg = 0x13,
		.bit = 0x02,
	}, /* FLD1 */
	{
		.address = 0x2c,
		.reg = 0x13,
		.bit = 0x03,
	}, /* FLD2 */
	{
		.address = 0x30,
		.reg = 0x13,
		.bit = 0x04,
	}, /* FLD3 */
	{
		.address = 0x34,
		.reg = 0x10,
		.bit = 0x03,
	}, /* BUC1 */
	{
		.address = 0x38,
		.reg = 0x10,
		.bit = 0x06,
	}, /* BUC2 */
	{
		.address = 0x3c,
		.reg = 0x10,
		.bit = 0x05,
	}, /* BUC3 */
	{
		.address = 0x40,
		.reg = 0x10,
		.bit = 0x04,
	}, /* BUC4 */
	{
		.address = 0x44,
		.reg = 0x10,
		.bit = 0x01,
	}, /* BUC5 */
	{
		.address = 0x48,
		.reg = 0x10,
		.bit = 0x00
	}, /* BUC6 */
	{
		.address = 0x4c,
		.reg = 0x92,
	}, /* GPI1 */
};

/* TMP0 - TMP5 are the same, all from GPADC */
static struct pmic_table thermal_table[] = {
	{
		.address = 0x00,
		.reg = XPOWER_GPADC_LOW
	},
	{
		.address = 0x0c,
		.reg = XPOWER_GPADC_LOW
	},
	{
		.address = 0x18,
		.reg = XPOWER_GPADC_LOW
	},
	{
		.address = 0x24,
		.reg = XPOWER_GPADC_LOW
	},
	{
		.address = 0x30,
		.reg = XPOWER_GPADC_LOW
	},
	{
		.address = 0x3c,
		.reg = XPOWER_GPADC_LOW
	},
};

static int intel_xpower_pmic_get_power(struct regmap *regmap, int reg,
				       int bit, u64 *value)
{
	int data;

	if (regmap_read(regmap, reg, &data))
		return -EIO;

	/* GPIO1 LDO regulator needs special handling */
	if (reg == XPOWER_GPI1_CTRL)
		*value = ((data & GPI1_LDO_MASK) == GPI1_LDO_ON);
	else
		*value = (data & BIT(bit)) ? 1 : 0;

	return 0;
}

static int intel_xpower_pmic_update_power(struct regmap *regmap, int reg,
					  int bit, bool on)
{
	int data;

	/* GPIO1 LDO regulator needs special handling */
	if (reg == XPOWER_GPI1_CTRL)
		return regmap_update_bits(regmap, reg, GPI1_LDO_MASK,
					  on ? GPI1_LDO_ON : GPI1_LDO_OFF);

	if (regmap_read(regmap, reg, &data))
		return -EIO;

	if (on)
		data |= BIT(bit);
	else
		data &= ~BIT(bit);

	if (regmap_write(regmap, reg, data))
		return -EIO;

	return 0;
}

/**
 * intel_xpower_pmic_get_raw_temp(): Get raw temperature reading from the PMIC
 *
 * @regmap: regmap of the PMIC device
 * @reg: register to get the reading
 *
 * Return a positive value on success, errno on failure.
 */
static int intel_xpower_pmic_get_raw_temp(struct regmap *regmap, int reg)
{
	u8 buf[2];

	if (regmap_bulk_read(regmap, AXP288_GP_ADC_H, buf, 2))
		return -EIO;

	return (buf[0] << 4) + ((buf[1] >> 4) & 0x0F);
}

static struct intel_pmic_opregion_data intel_xpower_pmic_opregion_data = {
	.get_power = intel_xpower_pmic_get_power,
	.update_power = intel_xpower_pmic_update_power,
	.get_raw_temp = intel_xpower_pmic_get_raw_temp,
	.power_table = power_table,
	.power_table_count = ARRAY_SIZE(power_table),
	.thermal_table = thermal_table,
	.thermal_table_count = ARRAY_SIZE(thermal_table),
};

static acpi_status intel_xpower_pmic_gpio_handler(u32 function,
		acpi_physical_address address, u32 bit_width, u64 *value,
		void *handler_context, void *region_context)
{
	return AE_OK;
}

static int intel_xpower_pmic_opregion_probe(struct platform_device *pdev)
{
	struct device *parent = pdev->dev.parent;
	struct axp20x_dev *axp20x = dev_get_drvdata(parent);
	acpi_status status;
	int result;

	status = acpi_install_address_space_handler(ACPI_HANDLE(parent),
			ACPI_ADR_SPACE_GPIO, intel_xpower_pmic_gpio_handler,
			NULL, NULL);
	if (ACPI_FAILURE(status))
		return -ENODEV;

	result = intel_pmic_install_opregion_handler(&pdev->dev,
					ACPI_HANDLE(parent), axp20x->regmap,
					&intel_xpower_pmic_opregion_data);
	if (result)
		acpi_remove_address_space_handler(ACPI_HANDLE(parent),
						  ACPI_ADR_SPACE_GPIO,
						  intel_xpower_pmic_gpio_handler);

	return result;
}

static struct platform_driver intel_xpower_pmic_opregion_driver = {
	.probe = intel_xpower_pmic_opregion_probe,
	.driver = {
		.name = "axp288_pmic_acpi",
	},
};

static int __init intel_xpower_pmic_opregion_driver_init(void)
{
	return platform_driver_register(&intel_xpower_pmic_opregion_driver);
}
device_initcall(intel_xpower_pmic_opregion_driver_init);
