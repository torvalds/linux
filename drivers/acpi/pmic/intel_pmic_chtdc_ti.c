// SPDX-License-Identifier: GPL-2.0
/*
 * Dollar Cove TI PMIC operation region driver
 * Copyright (C) 2014 Intel Corporation. All rights reserved.
 *
 * Rewritten and cleaned up
 * Copyright (C) 2017 Takashi Iwai <tiwai@suse.de>
 */

#include <linux/acpi.h>
#include <linux/init.h>
#include <linux/mfd/intel_soc_pmic.h>
#include <linux/platform_device.h>
#include "intel_pmic.h"

/* registers stored in 16bit BE (high:low, total 10bit) */
#define CHTDC_TI_VBAT		0x54
#define CHTDC_TI_DIETEMP	0x56
#define CHTDC_TI_BPTHERM	0x58
#define CHTDC_TI_GPADC		0x5a

static struct pmic_table chtdc_ti_power_table[] = {
	{ .address = 0x00, .reg = 0x41 },
	{ .address = 0x04, .reg = 0x42 },
	{ .address = 0x08, .reg = 0x43 },
	{ .address = 0x0c, .reg = 0x45 },
	{ .address = 0x10, .reg = 0x46 },
	{ .address = 0x14, .reg = 0x47 },
	{ .address = 0x18, .reg = 0x48 },
	{ .address = 0x1c, .reg = 0x49 },
	{ .address = 0x20, .reg = 0x4a },
	{ .address = 0x24, .reg = 0x4b },
	{ .address = 0x28, .reg = 0x4c },
	{ .address = 0x2c, .reg = 0x4d },
	{ .address = 0x30, .reg = 0x4e },
};

static struct pmic_table chtdc_ti_thermal_table[] = {
	{
		.address = 0x00,
		.reg = CHTDC_TI_GPADC
	},
	{
		.address = 0x0c,
		.reg = CHTDC_TI_GPADC
	},
	/* TMP2 -> SYSTEMP */
	{
		.address = 0x18,
		.reg = CHTDC_TI_GPADC
	},
	/* TMP3 -> BPTHERM */
	{
		.address = 0x24,
		.reg = CHTDC_TI_BPTHERM
	},
	{
		.address = 0x30,
		.reg = CHTDC_TI_GPADC
	},
	/* TMP5 -> DIETEMP */
	{
		.address = 0x3c,
		.reg = CHTDC_TI_DIETEMP
	},
};

static int chtdc_ti_pmic_get_power(struct regmap *regmap, int reg, int bit,
				   u64 *value)
{
	int data;

	if (regmap_read(regmap, reg, &data))
		return -EIO;

	*value = data & 1;
	return 0;
}

static int chtdc_ti_pmic_update_power(struct regmap *regmap, int reg, int bit,
				      bool on)
{
	return regmap_update_bits(regmap, reg, 1, on);
}

static int chtdc_ti_pmic_get_raw_temp(struct regmap *regmap, int reg)
{
	u8 buf[2];

	if (regmap_bulk_read(regmap, reg, buf, 2))
		return -EIO;

	/* stored in big-endian */
	return ((buf[0] & 0x03) << 8) | buf[1];
}

static struct intel_pmic_opregion_data chtdc_ti_pmic_opregion_data = {
	.get_power = chtdc_ti_pmic_get_power,
	.update_power = chtdc_ti_pmic_update_power,
	.get_raw_temp = chtdc_ti_pmic_get_raw_temp,
	.power_table = chtdc_ti_power_table,
	.power_table_count = ARRAY_SIZE(chtdc_ti_power_table),
	.thermal_table = chtdc_ti_thermal_table,
	.thermal_table_count = ARRAY_SIZE(chtdc_ti_thermal_table),
	.pmic_i2c_address = 0x5e,
};

static int chtdc_ti_pmic_opregion_probe(struct platform_device *pdev)
{
	struct intel_soc_pmic *pmic = dev_get_drvdata(pdev->dev.parent);
	int err;

	err = intel_pmic_install_opregion_handler(&pdev->dev,
			ACPI_HANDLE(pdev->dev.parent), pmic->regmap,
			&chtdc_ti_pmic_opregion_data);
	if (err < 0)
		return err;

	/* Re-enumerate devices depending on PMIC */
	acpi_walk_dep_device_list(ACPI_HANDLE(pdev->dev.parent));
	return 0;
}

static const struct platform_device_id chtdc_ti_pmic_opregion_id_table[] = {
	{ .name = "chtdc_ti_region" },
	{},
};

static struct platform_driver chtdc_ti_pmic_opregion_driver = {
	.probe = chtdc_ti_pmic_opregion_probe,
	.driver = {
		.name = "cht_dollar_cove_ti_pmic",
	},
	.id_table = chtdc_ti_pmic_opregion_id_table,
};
builtin_platform_driver(chtdc_ti_pmic_opregion_driver);
