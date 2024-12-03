// SPDX-License-Identifier: GPL-2.0+
/*
 * Fuel gauge driver for Maxim 17201/17205
 *
 * based on max1721x_battery.c
 *
 * Copyright (C) 2024 Liebherr-Electronics and Drives GmbH
 */

#include <linux/bitfield.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/nvmem-provider.h>
#include <linux/power_supply.h>
#include <linux/regmap.h>

#include <linux/unaligned.h>

/* Nonvolatile registers */
#define MAX1720X_NXTABLE0		0x80
#define MAX1720X_NRSENSE		0xCF	/* RSense in 10^-5 Ohm */
#define MAX1720X_NDEVICE_NAME4		0xDF

/* ModelGauge m5 */
#define MAX172XX_STATUS			0x00	/* Status */
#define MAX172XX_STATUS_BAT_ABSENT	BIT(3)	/* Battery absent */
#define MAX172XX_REPCAP			0x05	/* Average capacity */
#define MAX172XX_REPSOC			0x06	/* Percentage of charge */
#define MAX172XX_TEMP			0x08	/* Temperature */
#define MAX172XX_CURRENT		0x0A	/* Actual current */
#define MAX172XX_AVG_CURRENT		0x0B	/* Average current */
#define MAX172XX_FULL_CAP		0x10	/* Calculated full capacity */
#define MAX172XX_TTE			0x11	/* Time to empty */
#define MAX172XX_AVG_TA			0x16	/* Average temperature */
#define MAX172XX_CYCLES			0x17
#define MAX172XX_DESIGN_CAP		0x18	/* Design capacity */
#define MAX172XX_AVG_VCELL		0x19
#define MAX172XX_TTF			0x20	/* Time to full */
#define MAX172XX_DEV_NAME		0x21	/* Device name */
#define MAX172XX_DEV_NAME_TYPE_MASK	GENMASK(3, 0)
#define MAX172XX_DEV_NAME_TYPE_MAX17201	BIT(0)
#define MAX172XX_DEV_NAME_TYPE_MAX17205	(BIT(0) | BIT(2))
#define MAX172XX_QR_TABLE10		0x22
#define MAX172XX_BATT			0xDA	/* Battery voltage */
#define MAX172XX_ATAVCAP		0xDF

static const char *const max1720x_manufacturer = "Maxim Integrated";
static const char *const max17201_model = "MAX17201";
static const char *const max17205_model = "MAX17205";

struct max1720x_device_info {
	struct regmap *regmap;
	struct regmap *regmap_nv;
	struct i2c_client *ancillary;
	int rsense;
};

/*
 * Model Gauge M5 Algorithm output register
 * Volatile data (must not be cached)
 */
static const struct regmap_range max1720x_volatile_allow[] = {
	regmap_reg_range(MAX172XX_STATUS, MAX172XX_CYCLES),
	regmap_reg_range(MAX172XX_AVG_VCELL, MAX172XX_TTF),
	regmap_reg_range(MAX172XX_QR_TABLE10, MAX172XX_ATAVCAP),
};

static const struct regmap_range max1720x_readable_allow[] = {
	regmap_reg_range(MAX172XX_STATUS, MAX172XX_ATAVCAP),
};

static const struct regmap_range max1720x_readable_deny[] = {
	/* unused registers */
	regmap_reg_range(0x24, 0x26),
	regmap_reg_range(0x30, 0x31),
	regmap_reg_range(0x33, 0x34),
	regmap_reg_range(0x37, 0x37),
	regmap_reg_range(0x3B, 0x3C),
	regmap_reg_range(0x40, 0x41),
	regmap_reg_range(0x43, 0x44),
	regmap_reg_range(0x47, 0x49),
	regmap_reg_range(0x4B, 0x4C),
	regmap_reg_range(0x4E, 0xAF),
	regmap_reg_range(0xB1, 0xB3),
	regmap_reg_range(0xB5, 0xB7),
	regmap_reg_range(0xBF, 0xD0),
	regmap_reg_range(0xDB, 0xDB),
	regmap_reg_range(0xE0, 0xFF),
};

static const struct regmap_access_table max1720x_readable_regs = {
	.yes_ranges	= max1720x_readable_allow,
	.n_yes_ranges	= ARRAY_SIZE(max1720x_readable_allow),
	.no_ranges	= max1720x_readable_deny,
	.n_no_ranges	= ARRAY_SIZE(max1720x_readable_deny),
};

static const struct regmap_access_table max1720x_volatile_regs = {
	.yes_ranges	= max1720x_volatile_allow,
	.n_yes_ranges	= ARRAY_SIZE(max1720x_volatile_allow),
	.no_ranges	= max1720x_readable_deny,
	.n_no_ranges	= ARRAY_SIZE(max1720x_readable_deny),
};

static const struct regmap_config max1720x_regmap_cfg = {
	.reg_bits = 8,
	.val_bits = 16,
	.max_register = MAX172XX_ATAVCAP,
	.val_format_endian = REGMAP_ENDIAN_LITTLE,
	.rd_table = &max1720x_readable_regs,
	.volatile_table = &max1720x_volatile_regs,
	.cache_type = REGCACHE_RBTREE,
};

static const struct regmap_range max1720x_nvmem_allow[] = {
	regmap_reg_range(MAX1720X_NXTABLE0, MAX1720X_NDEVICE_NAME4),
};

static const struct regmap_range max1720x_nvmem_deny[] = {
	regmap_reg_range(0x00, 0x7F),
	regmap_reg_range(0xE0, 0xFF),
};

static const struct regmap_access_table max1720x_nvmem_regs = {
	.yes_ranges	= max1720x_nvmem_allow,
	.n_yes_ranges	= ARRAY_SIZE(max1720x_nvmem_allow),
	.no_ranges	= max1720x_nvmem_deny,
	.n_no_ranges	= ARRAY_SIZE(max1720x_nvmem_deny),
};

static const struct regmap_config max1720x_nvmem_regmap_cfg = {
	.reg_bits = 8,
	.val_bits = 16,
	.max_register = MAX1720X_NDEVICE_NAME4,
	.val_format_endian = REGMAP_ENDIAN_LITTLE,
	.rd_table = &max1720x_nvmem_regs,
};

static const struct nvmem_cell_info max1720x_nvmem_cells[] = {
	{ .name = "nXTable0",  .offset = 0,  .bytes = 2, },
	{ .name = "nXTable1",  .offset = 2,  .bytes = 2, },
	{ .name = "nXTable2",  .offset = 4,  .bytes = 2, },
	{ .name = "nXTable3",  .offset = 6,  .bytes = 2, },
	{ .name = "nXTable4",  .offset = 8,  .bytes = 2, },
	{ .name = "nXTable5",  .offset = 10, .bytes = 2, },
	{ .name = "nXTable6",  .offset = 12, .bytes = 2, },
	{ .name = "nXTable7",  .offset = 14, .bytes = 2, },
	{ .name = "nXTable8",  .offset = 16, .bytes = 2, },
	{ .name = "nXTable9",  .offset = 18, .bytes = 2, },
	{ .name = "nXTable10", .offset = 20, .bytes = 2, },
	{ .name = "nXTable11", .offset = 22, .bytes = 2, },
	{ .name = "nUser18C",  .offset = 24, .bytes = 2, },
	{ .name = "nUser18D",  .offset = 26, .bytes = 2, },
	{ .name = "nODSCTh",   .offset = 28, .bytes = 2, },
	{ .name = "nODSCCfg",  .offset = 30, .bytes = 2, },

	{ .name = "nOCVTable0",  .offset = 32, .bytes = 2, },
	{ .name = "nOCVTable1",  .offset = 34, .bytes = 2, },
	{ .name = "nOCVTable2",  .offset = 36, .bytes = 2, },
	{ .name = "nOCVTable3",  .offset = 38, .bytes = 2, },
	{ .name = "nOCVTable4",  .offset = 40, .bytes = 2, },
	{ .name = "nOCVTable5",  .offset = 42, .bytes = 2, },
	{ .name = "nOCVTable6",  .offset = 44, .bytes = 2, },
	{ .name = "nOCVTable7",  .offset = 46, .bytes = 2, },
	{ .name = "nOCVTable8",  .offset = 48, .bytes = 2, },
	{ .name = "nOCVTable9",  .offset = 50, .bytes = 2, },
	{ .name = "nOCVTable10", .offset = 52, .bytes = 2, },
	{ .name = "nOCVTable11", .offset = 54, .bytes = 2, },
	{ .name = "nIChgTerm",   .offset = 56, .bytes = 2, },
	{ .name = "nFilterCfg",  .offset = 58, .bytes = 2, },
	{ .name = "nVEmpty",     .offset = 60, .bytes = 2, },
	{ .name = "nLearnCfg",   .offset = 62, .bytes = 2, },

	{ .name = "nQRTable00",  .offset = 64, .bytes = 2, },
	{ .name = "nQRTable10",  .offset = 66, .bytes = 2, },
	{ .name = "nQRTable20",  .offset = 68, .bytes = 2, },
	{ .name = "nQRTable30",  .offset = 70, .bytes = 2, },
	{ .name = "nCycles",     .offset = 72, .bytes = 2, },
	{ .name = "nFullCapNom", .offset = 74, .bytes = 2, },
	{ .name = "nRComp0",     .offset = 76, .bytes = 2, },
	{ .name = "nTempCo",     .offset = 78, .bytes = 2, },
	{ .name = "nIAvgEmpty",  .offset = 80, .bytes = 2, },
	{ .name = "nFullCapRep", .offset = 82, .bytes = 2, },
	{ .name = "nVoltTemp",   .offset = 84, .bytes = 2, },
	{ .name = "nMaxMinCurr", .offset = 86, .bytes = 2, },
	{ .name = "nMaxMinVolt", .offset = 88, .bytes = 2, },
	{ .name = "nMaxMinTemp", .offset = 90, .bytes = 2, },
	{ .name = "nSOC",        .offset = 92, .bytes = 2, },
	{ .name = "nTimerH",     .offset = 94, .bytes = 2, },

	{ .name = "nConfig",    .offset = 96,  .bytes = 2, },
	{ .name = "nRippleCfg", .offset = 98,  .bytes = 2, },
	{ .name = "nMiscCfg",   .offset = 100, .bytes = 2, },
	{ .name = "nDesignCap", .offset = 102, .bytes = 2, },
	{ .name = "nHibCfg",    .offset = 104, .bytes = 2, },
	{ .name = "nPackCfg",   .offset = 106, .bytes = 2, },
	{ .name = "nRelaxCfg",  .offset = 108, .bytes = 2, },
	{ .name = "nConvgCfg",  .offset = 110, .bytes = 2, },
	{ .name = "nNVCfg0",    .offset = 112, .bytes = 2, },
	{ .name = "nNVCfg1",    .offset = 114, .bytes = 2, },
	{ .name = "nNVCfg2",    .offset = 116, .bytes = 2, },
	{ .name = "nSBSCfg",    .offset = 118, .bytes = 2, },
	{ .name = "nROMID0",    .offset = 120, .bytes = 2, },
	{ .name = "nROMID1",    .offset = 122, .bytes = 2, },
	{ .name = "nROMID2",    .offset = 124, .bytes = 2, },
	{ .name = "nROMID3",    .offset = 126, .bytes = 2, },

	{ .name = "nVAlrtTh",      .offset = 128, .bytes = 2, },
	{ .name = "nTAlrtTh",      .offset = 130, .bytes = 2, },
	{ .name = "nSAlrtTh",      .offset = 132, .bytes = 2, },
	{ .name = "nIAlrtTh",      .offset = 134, .bytes = 2, },
	{ .name = "nUser1C4",      .offset = 136, .bytes = 2, },
	{ .name = "nUser1C5",      .offset = 138, .bytes = 2, },
	{ .name = "nFullSOCThr",   .offset = 140, .bytes = 2, },
	{ .name = "nTTFCfg",       .offset = 142, .bytes = 2, },
	{ .name = "nCGain",        .offset = 144, .bytes = 2, },
	{ .name = "nTCurve",       .offset = 146, .bytes = 2, },
	{ .name = "nTGain",        .offset = 148, .bytes = 2, },
	{ .name = "nTOff",         .offset = 150, .bytes = 2, },
	{ .name = "nManfctrName0", .offset = 152, .bytes = 2, },
	{ .name = "nManfctrName1", .offset = 154, .bytes = 2, },
	{ .name = "nManfctrName2", .offset = 156, .bytes = 2, },
	{ .name = "nRSense",       .offset = 158, .bytes = 2, },

	{ .name = "nUser1D0",       .offset = 160, .bytes = 2, },
	{ .name = "nUser1D1",       .offset = 162, .bytes = 2, },
	{ .name = "nAgeFcCfg",      .offset = 164, .bytes = 2, },
	{ .name = "nDesignVoltage", .offset = 166, .bytes = 2, },
	{ .name = "nUser1D4",       .offset = 168, .bytes = 2, },
	{ .name = "nRFastVShdn",    .offset = 170, .bytes = 2, },
	{ .name = "nManfctrDate",   .offset = 172, .bytes = 2, },
	{ .name = "nFirstUsed",     .offset = 174, .bytes = 2, },
	{ .name = "nSerialNumber0", .offset = 176, .bytes = 2, },
	{ .name = "nSerialNumber1", .offset = 178, .bytes = 2, },
	{ .name = "nSerialNumber2", .offset = 180, .bytes = 2, },
	{ .name = "nDeviceName0",   .offset = 182, .bytes = 2, },
	{ .name = "nDeviceName1",   .offset = 184, .bytes = 2, },
	{ .name = "nDeviceName2",   .offset = 186, .bytes = 2, },
	{ .name = "nDeviceName3",   .offset = 188, .bytes = 2, },
	{ .name = "nDeviceName4",   .offset = 190, .bytes = 2, },
};

static const enum power_supply_property max1720x_battery_props[] = {
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_CAPACITY,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_CHARGE_FULL_DESIGN,
	POWER_SUPPLY_PROP_CHARGE_AVG,
	POWER_SUPPLY_PROP_TIME_TO_EMPTY_AVG,
	POWER_SUPPLY_PROP_TIME_TO_FULL_AVG,
	POWER_SUPPLY_PROP_TEMP,
	POWER_SUPPLY_PROP_CURRENT_NOW,
	POWER_SUPPLY_PROP_CURRENT_AVG,
	POWER_SUPPLY_PROP_CHARGE_FULL,
	POWER_SUPPLY_PROP_MODEL_NAME,
	POWER_SUPPLY_PROP_MANUFACTURER,
};

/* Convert regs value to power_supply units */

static int max172xx_time_to_ps(unsigned int reg)
{
	return reg * 5625 / 1000;	/* in sec. */
}

static int max172xx_percent_to_ps(unsigned int reg)
{
	return reg / 256;	/* in percent from 0 to 100 */
}

static int max172xx_voltage_to_ps(unsigned int reg)
{
	return reg * 1250;	/* in uV */
}

static int max172xx_capacity_to_ps(unsigned int reg)
{
	return reg * 500;	/* in uAh */
}

/*
 * Current and temperature is signed values, so unsigned regs
 * value must be converted to signed type
 */

static int max172xx_temperature_to_ps(unsigned int reg)
{
	int val = (int16_t)reg;

	return val * 10 / 256; /* in tenths of deg. C */
}

/*
 * Calculating current registers resolution:
 *
 * RSense stored in 10^-5 Ohm, so mesaurment voltage must be
 * in 10^-11 Volts for get current in uA.
 * 16 bit current reg fullscale +/-51.2mV is 102400 uV.
 * So: 102400 / 65535 * 10^5 = 156252
 */
static int max172xx_current_to_voltage(unsigned int reg)
{
	int val = (int16_t)reg;

	return val * 156252;
}

static int max1720x_battery_get_property(struct power_supply *psy,
					 enum power_supply_property psp,
					 union power_supply_propval *val)
{
	struct max1720x_device_info *info = power_supply_get_drvdata(psy);
	unsigned int reg_val;
	int ret = 0;

	switch (psp) {
	case POWER_SUPPLY_PROP_PRESENT:
		/*
		 * POWER_SUPPLY_PROP_PRESENT will always readable via
		 * sysfs interface. Value return 0 if battery not
		 * present or unaccesable via I2c.
		 */
		ret = regmap_read(info->regmap, MAX172XX_STATUS, &reg_val);
		if (ret < 0) {
			val->intval = 0;
			return 0;
		}

		val->intval = !FIELD_GET(MAX172XX_STATUS_BAT_ABSENT, reg_val);
		break;
	case POWER_SUPPLY_PROP_CAPACITY:
		ret = regmap_read(info->regmap, MAX172XX_REPSOC, &reg_val);
		val->intval = max172xx_percent_to_ps(reg_val);
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
		ret = regmap_read(info->regmap, MAX172XX_BATT, &reg_val);
		val->intval = max172xx_voltage_to_ps(reg_val);
		break;
	case POWER_SUPPLY_PROP_CHARGE_FULL_DESIGN:
		ret = regmap_read(info->regmap, MAX172XX_DESIGN_CAP, &reg_val);
		val->intval = max172xx_capacity_to_ps(reg_val);
		break;
	case POWER_SUPPLY_PROP_CHARGE_AVG:
		ret = regmap_read(info->regmap, MAX172XX_REPCAP, &reg_val);
		val->intval = max172xx_capacity_to_ps(reg_val);
		break;
	case POWER_SUPPLY_PROP_TIME_TO_EMPTY_AVG:
		ret = regmap_read(info->regmap, MAX172XX_TTE, &reg_val);
		val->intval = max172xx_time_to_ps(reg_val);
		break;
	case POWER_SUPPLY_PROP_TIME_TO_FULL_AVG:
		ret = regmap_read(info->regmap, MAX172XX_TTF, &reg_val);
		val->intval = max172xx_time_to_ps(reg_val);
		break;
	case POWER_SUPPLY_PROP_TEMP:
		ret = regmap_read(info->regmap, MAX172XX_TEMP, &reg_val);
		val->intval = max172xx_temperature_to_ps(reg_val);
		break;
	case POWER_SUPPLY_PROP_CURRENT_NOW:
		ret = regmap_read(info->regmap, MAX172XX_CURRENT, &reg_val);
		val->intval = max172xx_current_to_voltage(reg_val) / info->rsense;
		break;
	case POWER_SUPPLY_PROP_CURRENT_AVG:
		ret = regmap_read(info->regmap, MAX172XX_AVG_CURRENT, &reg_val);
		val->intval = max172xx_current_to_voltage(reg_val) / info->rsense;
		break;
	case POWER_SUPPLY_PROP_CHARGE_FULL:
		ret = regmap_read(info->regmap, MAX172XX_FULL_CAP, &reg_val);
		val->intval = max172xx_capacity_to_ps(reg_val);
		break;
	case POWER_SUPPLY_PROP_MODEL_NAME:
		ret = regmap_read(info->regmap, MAX172XX_DEV_NAME, &reg_val);
		reg_val = FIELD_GET(MAX172XX_DEV_NAME_TYPE_MASK, reg_val);
		if (reg_val == MAX172XX_DEV_NAME_TYPE_MAX17201)
			val->strval = max17201_model;
		else if (reg_val == MAX172XX_DEV_NAME_TYPE_MAX17205)
			val->strval = max17205_model;
		else
			return -ENODEV;
		break;
	case POWER_SUPPLY_PROP_MANUFACTURER:
		val->strval = max1720x_manufacturer;
		break;
	default:
		return -EINVAL;
	}

	return ret;
}

static
int max1720x_nvmem_reg_read(void *priv, unsigned int off, void *val, size_t len)
{
	struct max1720x_device_info *info = priv;
	unsigned int reg = MAX1720X_NXTABLE0 + (off / 2);

	return regmap_bulk_read(info->regmap_nv, reg, val, len / 2);
}

static void max1720x_unregister_ancillary(void *data)
{
	struct max1720x_device_info *info = data;

	i2c_unregister_device(info->ancillary);
}

static int max1720x_probe_nvmem(struct i2c_client *client,
				struct max1720x_device_info *info)
{
	struct device *dev = &client->dev;
	struct nvmem_config nvmem_config = {
		.dev = dev,
		.name = "max1720x_nvmem",
		.cells = max1720x_nvmem_cells,
		.ncells = ARRAY_SIZE(max1720x_nvmem_cells),
		.read_only = true,
		.root_only = true,
		.reg_read = max1720x_nvmem_reg_read,
		.size = ARRAY_SIZE(max1720x_nvmem_cells) * 2,
		.word_size = 2,
		.stride = 2,
		.priv = info,
	};
	struct nvmem_device *nvmem;
	unsigned int val;
	int ret;

	info->ancillary = i2c_new_ancillary_device(client, "nvmem", 0xb);
	if (IS_ERR(info->ancillary)) {
		dev_err(dev, "Failed to initialize ancillary i2c device\n");
		return PTR_ERR(info->ancillary);
	}

	ret = devm_add_action_or_reset(dev, max1720x_unregister_ancillary, info);
	if (ret) {
		dev_err(dev, "Failed to add unregister callback\n");
		return ret;
	}

	info->regmap_nv = devm_regmap_init_i2c(info->ancillary,
					       &max1720x_nvmem_regmap_cfg);
	if (IS_ERR(info->regmap_nv)) {
		dev_err(dev, "regmap initialization of nvmem failed\n");
		return PTR_ERR(info->regmap_nv);
	}

	ret = regmap_read(info->regmap_nv, MAX1720X_NRSENSE, &val);
	if (ret < 0) {
		dev_err(dev, "Failed to read sense resistor value\n");
		return ret;
	}

	info->rsense = val;
	if (!info->rsense) {
		dev_warn(dev, "RSense not calibrated, set 10 mOhms!\n");
		info->rsense = 1000; /* in regs in 10^-5 */
	}

	nvmem = devm_nvmem_register(dev, &nvmem_config);
	if (IS_ERR(nvmem)) {
		dev_err(dev, "Could not register nvmem!");
		return PTR_ERR(nvmem);
	}

	return 0;
}

static const struct power_supply_desc max1720x_bat_desc = {
	.name = "max1720x",
	.no_thermal = true,
	.type = POWER_SUPPLY_TYPE_BATTERY,
	.properties = max1720x_battery_props,
	.num_properties = ARRAY_SIZE(max1720x_battery_props),
	.get_property = max1720x_battery_get_property,
};

static int max1720x_probe(struct i2c_client *client)
{
	struct power_supply_config psy_cfg = {};
	struct device *dev = &client->dev;
	struct max1720x_device_info *info;
	struct power_supply *bat;
	int ret;

	info = devm_kzalloc(dev, sizeof(*info), GFP_KERNEL);
	if (!info)
		return -ENOMEM;

	psy_cfg.drv_data = info;
	psy_cfg.fwnode = dev_fwnode(dev);
	i2c_set_clientdata(client, info);
	info->regmap = devm_regmap_init_i2c(client, &max1720x_regmap_cfg);
	if (IS_ERR(info->regmap))
		return dev_err_probe(dev, PTR_ERR(info->regmap),
				     "regmap initialization failed\n");

	ret = max1720x_probe_nvmem(client, info);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to probe nvmem\n");

	bat = devm_power_supply_register(dev, &max1720x_bat_desc, &psy_cfg);
	if (IS_ERR(bat))
		return dev_err_probe(dev, PTR_ERR(bat),
				     "Failed to register power supply\n");

	return 0;
}

static const struct of_device_id max1720x_of_match[] = {
	{ .compatible = "maxim,max17201" },
	{}
};
MODULE_DEVICE_TABLE(of, max1720x_of_match);

static struct i2c_driver max1720x_i2c_driver = {
	.driver = {
		.name = "max1720x",
		.of_match_table = max1720x_of_match,
	},
	.probe = max1720x_probe,
};
module_i2c_driver(max1720x_i2c_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Dimitri Fedrau <dima.fedrau@gmail.com>");
MODULE_DESCRIPTION("Maxim MAX17201/MAX17205 Fuel Gauge IC driver");
