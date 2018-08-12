/*
 * intel_pmic_crc.c - Intel CrystalCove PMIC operation region driver
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
#include <linux/mfd/intel_soc_pmic.h>
#include <linux/regmap.h>
#include <linux/platform_device.h>
#include "intel_pmic.h"

#define PWR_SOURCE_SELECT	BIT(1)

#define PMIC_A0LOCK_REG		0xc5

static struct pmic_table power_table[] = {
/*	{
		.address = 0x00,
		.reg = ??,
		.bit = ??,
	}, ** VSYS */
	{
		.address = 0x04,
		.reg = 0x63,
		.bit = 0x00,
	}, /* SYSX -> VSYS_SX */
	{
		.address = 0x08,
		.reg = 0x62,
		.bit = 0x00,
	}, /* SYSU -> VSYS_U */
	{
		.address = 0x0c,
		.reg = 0x64,
		.bit = 0x00,
	}, /* SYSS -> VSYS_S */
	{
		.address = 0x10,
		.reg = 0x6a,
		.bit = 0x00,
	}, /* V50S -> V5P0S */
	{
		.address = 0x14,
		.reg = 0x6b,
		.bit = 0x00,
	}, /* HOST -> VHOST, USB2/3 host */
	{
		.address = 0x18,
		.reg = 0x6c,
		.bit = 0x00,
	}, /* VBUS -> VBUS, USB2/3 OTG */
	{
		.address = 0x1c,
		.reg = 0x6d,
		.bit = 0x00,
	}, /* HDMI -> VHDMI */
/*	{
		.address = 0x20,
		.reg = ??,
		.bit = ??,
	}, ** S285 */
	{
		.address = 0x24,
		.reg = 0x66,
		.bit = 0x00,
	}, /* X285 -> V2P85SX, camera */
/*	{
		.address = 0x28,
		.reg = ??,
		.bit = ??,
	}, ** V33A */
	{
		.address = 0x2c,
		.reg = 0x69,
		.bit = 0x00,
	}, /* V33S -> V3P3S, display/ssd/audio */
	{
		.address = 0x30,
		.reg = 0x68,
		.bit = 0x00,
	}, /* V33U -> V3P3U, SDIO wifi&bt */
/*	{
		.address = 0x34 .. 0x40,
		.reg = ??,
		.bit = ??,
	}, ** V33I, V18A, REFQ, V12A */
	{
		.address = 0x44,
		.reg = 0x5c,
		.bit = 0x00,
	}, /* V18S -> V1P8S, SOC/USB PHY/SIM */
	{
		.address = 0x48,
		.reg = 0x5d,
		.bit = 0x00,
	}, /* V18X -> V1P8SX, eMMC/camara/audio */
	{
		.address = 0x4c,
		.reg = 0x5b,
		.bit = 0x00,
	}, /* V18U -> V1P8U, LPDDR */
	{
		.address = 0x50,
		.reg = 0x61,
		.bit = 0x00,
	}, /* V12X -> V1P2SX, SOC SFR */
	{
		.address = 0x54,
		.reg = 0x60,
		.bit = 0x00,
	}, /* V12S -> V1P2S, MIPI */
/*	{
		.address = 0x58,
		.reg = ??,
		.bit = ??,
	}, ** V10A */
	{
		.address = 0x5c,
		.reg = 0x56,
		.bit = 0x00,
	}, /* V10S -> V1P0S, SOC GFX */
	{
		.address = 0x60,
		.reg = 0x57,
		.bit = 0x00,
	}, /* V10X -> V1P0SX, SOC display/DDR IO/PCIe */
	{
		.address = 0x64,
		.reg = 0x59,
		.bit = 0x00,
	}, /* V105 -> V1P05S, L2 SRAM */
};

static struct pmic_table thermal_table[] = {
	{
		.address = 0x00,
		.reg = 0x75
	},
	{
		.address = 0x04,
		.reg = 0x95
	},
	{
		.address = 0x08,
		.reg = 0x97
	},
	{
		.address = 0x0c,
		.reg = 0x77
	},
	{
		.address = 0x10,
		.reg = 0x9a
	},
	{
		.address = 0x14,
		.reg = 0x9c
	},
	{
		.address = 0x18,
		.reg = 0x79
	},
	{
		.address = 0x1c,
		.reg = 0x9f
	},
	{
		.address = 0x20,
		.reg = 0xa1
	},
	{
		.address = 0x48,
		.reg = 0x94
	},
	{
		.address = 0x4c,
		.reg = 0x99
	},
	{
		.address = 0x50,
		.reg = 0x9e
	},
};

static int intel_crc_pmic_get_power(struct regmap *regmap, int reg,
				    int bit, u64 *value)
{
	int data;

	if (regmap_read(regmap, reg, &data))
		return -EIO;

	*value = (data & PWR_SOURCE_SELECT) && (data & BIT(bit)) ? 1 : 0;
	return 0;
}

static int intel_crc_pmic_update_power(struct regmap *regmap, int reg,
				       int bit, bool on)
{
	int data;

	if (regmap_read(regmap, reg, &data))
		return -EIO;

	if (on) {
		data |= PWR_SOURCE_SELECT | BIT(bit);
	} else {
		data &= ~BIT(bit);
		data |= PWR_SOURCE_SELECT;
	}

	if (regmap_write(regmap, reg, data))
		return -EIO;
	return 0;
}

static int intel_crc_pmic_get_raw_temp(struct regmap *regmap, int reg)
{
	int temp_l, temp_h;

	/*
	 * Raw temperature value is 10bits: 8bits in reg
	 * and 2bits in reg-1: bit0,1
	 */
	if (regmap_read(regmap, reg, &temp_l) ||
	    regmap_read(regmap, reg - 1, &temp_h))
		return -EIO;

	return temp_l | (temp_h & 0x3) << 8;
}

static int intel_crc_pmic_update_aux(struct regmap *regmap, int reg, int raw)
{
	return regmap_write(regmap, reg, raw) ||
		regmap_update_bits(regmap, reg - 1, 0x3, raw >> 8) ? -EIO : 0;
}

static int intel_crc_pmic_get_policy(struct regmap *regmap,
					int reg, int bit, u64 *value)
{
	int pen;

	if (regmap_read(regmap, reg, &pen))
		return -EIO;
	*value = pen >> 7;
	return 0;
}

static int intel_crc_pmic_update_policy(struct regmap *regmap,
					int reg, int bit, int enable)
{
	int alert0;

	/* Update to policy enable bit requires unlocking a0lock */
	if (regmap_read(regmap, PMIC_A0LOCK_REG, &alert0))
		return -EIO;

	if (regmap_update_bits(regmap, PMIC_A0LOCK_REG, 0x01, 0))
		return -EIO;

	if (regmap_update_bits(regmap, reg, 0x80, enable << 7))
		return -EIO;

	/* restore alert0 */
	if (regmap_write(regmap, PMIC_A0LOCK_REG, alert0))
		return -EIO;

	return 0;
}

static struct intel_pmic_opregion_data intel_crc_pmic_opregion_data = {
	.get_power	= intel_crc_pmic_get_power,
	.update_power	= intel_crc_pmic_update_power,
	.get_raw_temp	= intel_crc_pmic_get_raw_temp,
	.update_aux	= intel_crc_pmic_update_aux,
	.get_policy	= intel_crc_pmic_get_policy,
	.update_policy	= intel_crc_pmic_update_policy,
	.power_table	= power_table,
	.power_table_count= ARRAY_SIZE(power_table),
	.thermal_table	= thermal_table,
	.thermal_table_count = ARRAY_SIZE(thermal_table),
};

static int intel_crc_pmic_opregion_probe(struct platform_device *pdev)
{
	struct intel_soc_pmic *pmic = dev_get_drvdata(pdev->dev.parent);
	return intel_pmic_install_opregion_handler(&pdev->dev,
			ACPI_HANDLE(pdev->dev.parent), pmic->regmap,
			&intel_crc_pmic_opregion_data);
}

static struct platform_driver intel_crc_pmic_opregion_driver = {
	.probe = intel_crc_pmic_opregion_probe,
	.driver = {
		.name = "crystal_cove_pmic",
	},
};
builtin_platform_driver(intel_crc_pmic_opregion_driver);
