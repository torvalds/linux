// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Nokia RX-51 battery driver
 *
 * Copyright (C) 2012  Pali Rohár <pali@kernel.org>
 */

#include <linux/module.h>
#include <linux/param.h>
#include <linux/platform_device.h>
#include <linux/power_supply.h>
#include <linux/slab.h>
#include <linux/iio/consumer.h>
#include <linux/of.h>

struct rx51_device_info {
	struct device *dev;
	struct power_supply *bat;
	struct power_supply_desc bat_desc;
	struct iio_channel *channel_temp;
	struct iio_channel *channel_bsi;
	struct iio_channel *channel_vbat;
};

/*
 * Read ADCIN channel value, code copied from maemo kernel
 */
static int rx51_battery_read_adc(struct iio_channel *channel)
{
	int val, err;
	err = iio_read_channel_average_raw(channel, &val);
	if (err < 0)
		return err;
	return val;
}

/*
 * Read ADCIN channel 12 (voltage) and convert RAW value to micro voltage
 * This conversion formula was extracted from maemo program bsi-read
 */
static int rx51_battery_read_voltage(struct rx51_device_info *di)
{
	int voltage = rx51_battery_read_adc(di->channel_vbat);

	if (voltage < 0) {
		dev_err(di->dev, "Could not read ADC: %d\n", voltage);
		return voltage;
	}

	return 1000 * (10000 * voltage / 1705);
}

/*
 * Temperature look-up tables
 * TEMP = (1/(t1 + 1/298) - 273.15)
 * Where t1 = (1/B) * ln((RAW_ADC_U * 2.5)/(R * I * 255))
 * Formula is based on experimental data, RX-51 CAL data, maemo program bme
 * and formula from da9052 driver with values R = 100, B = 3380, I = 0.00671
 */

/*
 * Table1 (temperature for first 25 RAW values)
 * Usage: TEMP = rx51_temp_table1[RAW]
 *   RAW is between 1 and 24
 *   TEMP is between 201 C and 55 C
 */
static u8 rx51_temp_table1[] = {
	255, 201, 159, 138, 124, 114, 106,  99,  94,  89,  85,  82,  78,  75,
	 73,  70,  68,  66,  64,  62,  61,  59,  57,  56,  55
};

/*
 * Table2 (lowest RAW value for temperature)
 * Usage: RAW = rx51_temp_table2[TEMP-rx51_temp_table2_first]
 *   TEMP is between 53 C and -32 C
 *   RAW is between 25 and 993
 */
#define rx51_temp_table2_first 53
static u16 rx51_temp_table2[] = {
	 25,  26,  27,  28,  29,  30,  31,  32,  33,  34,  35,  36,  37,  39,
	 40,  41,  43,  44,  46,  48,  49,  51,  53,  55,  57,  59,  61,  64,
	 66,  69,  71,  74,  77,  80,  83,  86,  90,  94,  97, 101, 106, 110,
	115, 119, 125, 130, 136, 141, 148, 154, 161, 168, 176, 184, 202, 211,
	221, 231, 242, 254, 266, 279, 293, 308, 323, 340, 357, 375, 395, 415,
	437, 460, 485, 511, 539, 568, 600, 633, 669, 706, 747, 790, 836, 885,
	937, 993, 1024
};

/*
 * Read ADCIN channel 0 (battery temp) and convert value to tenths of Celsius
 * Use Temperature look-up tables for conversation
 */
static int rx51_battery_read_temperature(struct rx51_device_info *di)
{
	int min = 0;
	int max = ARRAY_SIZE(rx51_temp_table2) - 1;
	int raw = rx51_battery_read_adc(di->channel_temp);

	if (raw < 0)
		dev_err(di->dev, "Could not read ADC: %d\n", raw);

	/* Zero and negative values are undefined */
	if (raw <= 0)
		return INT_MAX;

	/* ADC channels are 10 bit, higher value are undefined */
	if (raw >= (1 << 10))
		return INT_MIN;

	/* First check for temperature in first direct table */
	if (raw < ARRAY_SIZE(rx51_temp_table1))
		return rx51_temp_table1[raw] * 10;

	/* Binary search RAW value in second inverse table */
	while (max - min > 1) {
		int mid = (max + min) / 2;
		if (rx51_temp_table2[mid] <= raw)
			min = mid;
		else if (rx51_temp_table2[mid] > raw)
			max = mid;
		if (rx51_temp_table2[mid] == raw)
			break;
	}

	return (rx51_temp_table2_first - min) * 10;
}

/*
 * Read ADCIN channel 4 (BSI) and convert RAW value to micro Ah
 * This conversion formula was extracted from maemo program bsi-read
 */
static int rx51_battery_read_capacity(struct rx51_device_info *di)
{
	int capacity = rx51_battery_read_adc(di->channel_bsi);

	if (capacity < 0) {
		dev_err(di->dev, "Could not read ADC: %d\n", capacity);
		return capacity;
	}

	return 1280 * (1200 * capacity)/(1024 - capacity);
}

/*
 * Return power_supply property
 */
static int rx51_battery_get_property(struct power_supply *psy,
					enum power_supply_property psp,
					union power_supply_propval *val)
{
	struct rx51_device_info *di = power_supply_get_drvdata(psy);

	switch (psp) {
	case POWER_SUPPLY_PROP_TECHNOLOGY:
		val->intval = POWER_SUPPLY_TECHNOLOGY_LION;
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_MAX_DESIGN:
		val->intval = 4200000;
		break;
	case POWER_SUPPLY_PROP_PRESENT:
		val->intval = rx51_battery_read_voltage(di) ? 1 : 0;
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
		val->intval = rx51_battery_read_voltage(di);
		break;
	case POWER_SUPPLY_PROP_TEMP:
		val->intval = rx51_battery_read_temperature(di);
		break;
	case POWER_SUPPLY_PROP_CHARGE_FULL_DESIGN:
		val->intval = rx51_battery_read_capacity(di);
		break;
	default:
		return -EINVAL;
	}

	if (val->intval == INT_MAX || val->intval == INT_MIN)
		return -EINVAL;

	return 0;
}

static enum power_supply_property rx51_battery_props[] = {
	POWER_SUPPLY_PROP_TECHNOLOGY,
	POWER_SUPPLY_PROP_VOLTAGE_MAX_DESIGN,
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_TEMP,
	POWER_SUPPLY_PROP_CHARGE_FULL_DESIGN,
};

static int rx51_battery_probe(struct platform_device *pdev)
{
	struct power_supply_config psy_cfg = {};
	struct rx51_device_info *di;

	di = devm_kzalloc(&pdev->dev, sizeof(*di), GFP_KERNEL);
	if (!di)
		return -ENOMEM;

	di->dev = &pdev->dev;
	di->bat_desc.name = "rx51-battery";
	di->bat_desc.type = POWER_SUPPLY_TYPE_BATTERY;
	di->bat_desc.properties = rx51_battery_props;
	di->bat_desc.num_properties = ARRAY_SIZE(rx51_battery_props);
	di->bat_desc.get_property = rx51_battery_get_property;

	psy_cfg.drv_data = di;

	di->channel_temp = devm_iio_channel_get(di->dev, "temp");
	if (IS_ERR(di->channel_temp))
		return PTR_ERR(di->channel_temp);

	di->channel_bsi  = devm_iio_channel_get(di->dev, "bsi");
	if (IS_ERR(di->channel_bsi))
		return PTR_ERR(di->channel_bsi);

	di->channel_vbat = devm_iio_channel_get(di->dev, "vbat");
	if (IS_ERR(di->channel_vbat))
		return PTR_ERR(di->channel_vbat);

	di->bat = devm_power_supply_register(di->dev, &di->bat_desc, &psy_cfg);
	if (IS_ERR(di->bat))
		return PTR_ERR(di->bat);

	return 0;
}

#ifdef CONFIG_OF
static const struct of_device_id n900_battery_of_match[] = {
	{.compatible = "nokia,n900-battery", },
	{ },
};
MODULE_DEVICE_TABLE(of, n900_battery_of_match);
#endif

static struct platform_driver rx51_battery_driver = {
	.probe = rx51_battery_probe,
	.driver = {
		.name = "rx51-battery",
		.of_match_table = of_match_ptr(n900_battery_of_match),
	},
};
module_platform_driver(rx51_battery_driver);

MODULE_ALIAS("platform:rx51-battery");
MODULE_AUTHOR("Pali Rohár <pali@kernel.org>");
MODULE_DESCRIPTION("Nokia RX-51 battery driver");
MODULE_LICENSE("GPL");
