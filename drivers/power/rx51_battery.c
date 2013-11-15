/*
 * Nokia RX-51 battery driver
 *
 * Copyright (C) 2012  Pali Rohár <pali.rohar@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <linux/module.h>
#include <linux/param.h>
#include <linux/platform_device.h>
#include <linux/power_supply.h>
#include <linux/slab.h>
#include <linux/i2c/twl4030-madc.h>

/* RX51 specific channels */
#define TWL4030_MADC_BTEMP_RX51	TWL4030_MADC_ADCIN0
#define TWL4030_MADC_BCI_RX51	TWL4030_MADC_ADCIN4

struct rx51_device_info {
	struct device *dev;
	struct power_supply bat;
};

/*
 * Read ADCIN channel value, code copied from maemo kernel
 */
static int rx51_battery_read_adc(int channel)
{
	struct twl4030_madc_request req;

	req.channels = channel;
	req.do_avg = 1;
	req.method = TWL4030_MADC_SW1;
	req.func_cb = NULL;
	req.type = TWL4030_MADC_WAIT;
	req.raw = true;

	if (twl4030_madc_conversion(&req) <= 0)
		return -ENODATA;

	return req.rbuf[ffs(channel) - 1];
}

/*
 * Read ADCIN channel 12 (voltage) and convert RAW value to micro voltage
 * This conversion formula was extracted from maemo program bsi-read
 */
static int rx51_battery_read_voltage(struct rx51_device_info *di)
{
	int voltage = rx51_battery_read_adc(TWL4030_MADC_VBAT);

	if (voltage < 0)
		return voltage;

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
	int raw = rx51_battery_read_adc(TWL4030_MADC_BTEMP_RX51);

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
	int capacity = rx51_battery_read_adc(TWL4030_MADC_BCI_RX51);

	if (capacity < 0)
		return capacity;

	return 1280 * (1200 * capacity)/(1024 - capacity);
}

/*
 * Return power_supply property
 */
static int rx51_battery_get_property(struct power_supply *psy,
					enum power_supply_property psp,
					union power_supply_propval *val)
{
	struct rx51_device_info *di = container_of((psy),
				struct rx51_device_info, bat);

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
	struct rx51_device_info *di;
	int ret;

	di = devm_kzalloc(&pdev->dev, sizeof(*di), GFP_KERNEL);
	if (!di)
		return -ENOMEM;

	platform_set_drvdata(pdev, di);

	di->bat.name = dev_name(&pdev->dev);
	di->bat.type = POWER_SUPPLY_TYPE_BATTERY;
	di->bat.properties = rx51_battery_props;
	di->bat.num_properties = ARRAY_SIZE(rx51_battery_props);
	di->bat.get_property = rx51_battery_get_property;

	ret = power_supply_register(di->dev, &di->bat);
	if (ret)
		return ret;

	return 0;
}

static int rx51_battery_remove(struct platform_device *pdev)
{
	struct rx51_device_info *di = platform_get_drvdata(pdev);

	power_supply_unregister(&di->bat);

	return 0;
}

static struct platform_driver rx51_battery_driver = {
	.probe = rx51_battery_probe,
	.remove = rx51_battery_remove,
	.driver = {
		.name = "rx51-battery",
		.owner = THIS_MODULE,
	},
};
module_platform_driver(rx51_battery_driver);

MODULE_ALIAS("platform:rx51-battery");
MODULE_AUTHOR("Pali Rohár <pali.rohar@gmail.com>");
MODULE_DESCRIPTION("Nokia RX-51 battery driver");
MODULE_LICENSE("GPL");
