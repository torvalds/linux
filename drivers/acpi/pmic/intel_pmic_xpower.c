// SPDX-License-Identifier: GPL-2.0
/*
 * XPower AXP288 PMIC operation region driver
 *
 * Copyright (C) 2014 Intel Corporation. All rights reserved.
 */

#include <linux/acpi.h>
#include <linux/init.h>
#include <linux/mfd/axp20x.h>
#include <linux/regmap.h>
#include <linux/platform_device.h>
#include <asm/iosf_mbi.h>
#include "intel_pmic.h"

#define XPOWER_GPADC_LOW	0x5b
#define XPOWER_GPI1_CTRL	0x92

#define GPI1_LDO_MASK		GENMASK(2, 0)
#define GPI1_LDO_ON		(3 << 0)
#define GPI1_LDO_OFF		(4 << 0)

#define AXP288_ADC_TS_CURRENT_ON_OFF_MASK		GENMASK(1, 0)
#define AXP288_ADC_TS_CURRENT_OFF			(0 << 0)
#define AXP288_ADC_TS_CURRENT_ON_WHEN_CHARGING		(1 << 0)
#define AXP288_ADC_TS_CURRENT_ON_ONDEMAND		(2 << 0)
#define AXP288_ADC_TS_CURRENT_ON			(3 << 0)

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
	int data, ret;

	ret = iosf_mbi_block_punit_i2c_access();
	if (ret)
		return ret;

	/* GPIO1 LDO regulator needs special handling */
	if (reg == XPOWER_GPI1_CTRL) {
		ret = regmap_update_bits(regmap, reg, GPI1_LDO_MASK,
					 on ? GPI1_LDO_ON : GPI1_LDO_OFF);
		goto out;
	}

	if (regmap_read(regmap, reg, &data)) {
		ret = -EIO;
		goto out;
	}

	if (on)
		data |= BIT(bit);
	else
		data &= ~BIT(bit);

	if (regmap_write(regmap, reg, data))
		ret = -EIO;
out:
	iosf_mbi_unblock_punit_i2c_access();

	return ret;
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
	int ret, adc_ts_pin_ctrl;
	u8 buf[2];

	/*
	 * The current-source used for the battery temp-sensor (TS) is shared
	 * with the GPADC. For proper fuel-gauge and charger operation the TS
	 * current-source needs to be permanently on. But to read the GPADC we
	 * need to temporary switch the TS current-source to ondemand, so that
	 * the GPADC can use it, otherwise we will always read an all 0 value.
	 *
	 * Note that the switching from on to on-ondemand is not necessary
	 * when the TS current-source is off (this happens on devices which
	 * do not use the TS-pin).
	 */
	ret = regmap_read(regmap, AXP288_ADC_TS_PIN_CTRL, &adc_ts_pin_ctrl);
	if (ret)
		return ret;

	if (adc_ts_pin_ctrl & AXP288_ADC_TS_CURRENT_ON_OFF_MASK) {
		/*
		 * AXP288_ADC_TS_PIN_CTRL reads are cached by the regmap, so
		 * this does to a single I2C-transfer, and thus there is no
		 * need to explicitly call iosf_mbi_block_punit_i2c_access().
		 */
		ret = regmap_update_bits(regmap, AXP288_ADC_TS_PIN_CTRL,
					 AXP288_ADC_TS_CURRENT_ON_OFF_MASK,
					 AXP288_ADC_TS_CURRENT_ON_ONDEMAND);
		if (ret)
			return ret;

		/* Wait a bit after switching the current-source */
		usleep_range(6000, 10000);
	}

	ret = iosf_mbi_block_punit_i2c_access();
	if (ret)
		return ret;

	ret = regmap_bulk_read(regmap, AXP288_GP_ADC_H, buf, 2);
	if (ret == 0)
		ret = (buf[0] << 4) + ((buf[1] >> 4) & 0x0f);

	if (adc_ts_pin_ctrl & AXP288_ADC_TS_CURRENT_ON_OFF_MASK) {
		regmap_update_bits(regmap, AXP288_ADC_TS_PIN_CTRL,
				   AXP288_ADC_TS_CURRENT_ON_OFF_MASK,
				   AXP288_ADC_TS_CURRENT_ON);
	}

	iosf_mbi_unblock_punit_i2c_access();

	return ret;
}

static int intel_xpower_exec_mipi_pmic_seq_element(struct regmap *regmap,
						   u16 i2c_address, u32 reg_address,
						   u32 value, u32 mask)
{
	int ret;

	if (i2c_address != 0x34) {
		pr_err("%s: Unexpected i2c-addr: 0x%02x (reg-addr 0x%x value 0x%x mask 0x%x)\n",
		       __func__, i2c_address, reg_address, value, mask);
		return -ENXIO;
	}

	ret = iosf_mbi_block_punit_i2c_access();
	if (ret)
		return ret;

	ret = regmap_update_bits(regmap, reg_address, mask, value);

	iosf_mbi_unblock_punit_i2c_access();

	return ret;
}

static int intel_xpower_lpat_raw_to_temp(struct acpi_lpat_conversion_table *lpat_table,
					 int raw)
{
	struct acpi_lpat first = lpat_table->lpat[0];
	struct acpi_lpat last = lpat_table->lpat[lpat_table->lpat_count - 1];

	/*
	 * Some LPAT tables in the ACPI Device for the AXP288 PMIC for some
	 * reason only describe a small temperature range, e.g. 27° - 37°
	 * Celcius. Resulting in errors when the tablet is idle in a cool room.
	 *
	 * To avoid these errors clamp the raw value to be inside the LPAT.
	 */
	if (first.raw < last.raw)
		raw = clamp(raw, first.raw, last.raw);
	else
		raw = clamp(raw, last.raw, first.raw);

	return acpi_lpat_raw_to_temp(lpat_table, raw);
}

static const struct intel_pmic_opregion_data intel_xpower_pmic_opregion_data = {
	.get_power = intel_xpower_pmic_get_power,
	.update_power = intel_xpower_pmic_update_power,
	.get_raw_temp = intel_xpower_pmic_get_raw_temp,
	.exec_mipi_pmic_seq_element = intel_xpower_exec_mipi_pmic_seq_element,
	.lpat_raw_to_temp = intel_xpower_lpat_raw_to_temp,
	.power_table = power_table,
	.power_table_count = ARRAY_SIZE(power_table),
	.thermal_table = thermal_table,
	.thermal_table_count = ARRAY_SIZE(thermal_table),
	.pmic_i2c_address = 0x34,
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
builtin_platform_driver(intel_xpower_pmic_opregion_driver);
