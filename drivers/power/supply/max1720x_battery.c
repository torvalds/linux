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
#include <linux/power_supply.h>
#include <linux/regmap.h>

#include <asm/unaligned.h>

/* Nonvolatile registers */
#define MAX1720X_NRSENSE		0xCF	/* RSense in 10^-5 Ohm */

/* ModelGauge m5 */
#define MAX172XX_STATUS			0x00	/* Status */
#define MAX172XX_STATUS_BAT_ABSENT	BIT(3)	/* Battery absent */
#define MAX172XX_REPCAP			0x05	/* Average capacity */
#define MAX172XX_REPSOC			0x06	/* Percentage of charge */
#define MAX172XX_TEMP			0x08	/* Temperature */
#define MAX172XX_CURRENT		0x0A	/* Actual current */
#define MAX172XX_AVG_CURRENT		0x0B	/* Average current */
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

static int max1720x_probe_sense_resistor(struct i2c_client *client,
					 struct max1720x_device_info *info)
{
	struct device *dev = &client->dev;
	struct i2c_client *ancillary;
	int ret;

	ancillary = i2c_new_ancillary_device(client, "nvmem", 0xb);
	if (IS_ERR(ancillary)) {
		dev_err(dev, "Failed to initialize ancillary i2c device\n");
		return PTR_ERR(ancillary);
	}

	ret = i2c_smbus_read_word_data(ancillary, MAX1720X_NRSENSE);
	i2c_unregister_device(ancillary);
	if (ret < 0)
		return ret;

	info->rsense = ret;
	if (!info->rsense) {
		dev_warn(dev, "RSense not calibrated, set 10 mOhms!\n");
		info->rsense = 1000; /* in regs in 10^-5 */
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
	info->regmap = devm_regmap_init_i2c(client, &max1720x_regmap_cfg);
	if (IS_ERR(info->regmap))
		return dev_err_probe(dev, PTR_ERR(info->regmap),
				     "regmap initialization failed\n");

	ret = max1720x_probe_sense_resistor(client, info);
	if (ret)
		return dev_err_probe(dev, ret,
				     "Failed to read sense resistor value\n");

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
