// SPDX-License-Identifier: GPL-2.0+
/*
 * Battery monitor driver for the uPI uG3105 battery monitor
 *
 * Note the uG3105 is not a full-featured autonomous fuel-gauge. Instead it is
 * expected to be use in combination with some always on microcontroller reading
 * its coulomb-counter before it can wrap (must be read every 400 seconds!).
 *
 * Since Linux does not monitor coulomb-counter changes while the device
 * is off or suspended, the coulomb counter is not used atm.
 *
 * Possible improvements:
 * 1. Add coulumb counter reading, e.g. something like this:
 * Read + reset coulomb counter every 10 polls (every 300 seconds)
 *
 * if ((chip->poll_count % 10) == 0) {
 *	val = ug3105_read_word(chip->client, UG3105_REG_COULOMB_CNT);
 *	if (val < 0)
 *		goto out;
 *
 *	i2c_smbus_write_byte_data(chip->client, UG3105_REG_CTRL1,
 *				  UG3105_CTRL1_RESET_COULOMB_CNT);
 *
 *	chip->total_coulomb_count += (s16)val;
 *	dev_dbg(&chip->client->dev, "coulomb count %d total %d\n",
 *		(s16)val, chip->total_coulomb_count);
 * }
 *
 * 2. Reset total_coulomb_count val to 0 when the battery is as good as empty
 *    and remember that we did this (and clear the flag for this on susp/resume)
 * 3. When the battery is full check if the flag that we set total_coulomb_count
 *    to when the battery was empty is set. If so we now know the capacity,
 *    not the design, but actual capacity, of the battery
 * 4. Add some mechanism (needs userspace help, or maybe use efivar?) to remember
 *    the actual capacity of the battery over reboots
 * 5. When we know the actual capacity at probe time, add energy_now and
 *    energy_full attributes. Guess boot + resume energy_now value based on ocv
 *    and then use total_coulomb_count to report energy_now over time, resetting
 *    things to adjust for drift when empty/full. This should give more accurate
 *    readings, esp. in the 30-70% range and allow userspace to estimate time
 *    remaining till empty/full
 * 6. Maybe unregister + reregister the psy device when we learn the actual
 *    capacity during run-time ?
 *
 * The above will also require some sort of mwh_per_unit calculation. Testing
 * has shown that an estimated 7404mWh increase of the battery's energy results
 * in a total_coulomb_count increase of 3277 units with a 5 milli-ohm sense R.
 *
 * Copyright (C) 2021 - 2025 Hans de Goede <hansg@kernel.org>
 */

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/mod_devicetable.h>
#include <linux/power_supply.h>

#include "adc-battery-helper.h"

#define UG3105_REG_MODE						0x00
#define UG3105_REG_CTRL1					0x01
#define UG3105_REG_COULOMB_CNT					0x02
#define UG3105_REG_BAT_VOLT					0x08
#define UG3105_REG_BAT_CURR					0x0c

#define UG3105_MODE_STANDBY					0x00
#define UG3105_MODE_RUN						0x10

#define UG3105_CTRL1_RESET_COULOMB_CNT				0x03

struct ug3105_chip {
	/* Must be the first member see adc-battery-helper documentation */
	struct adc_battery_helper helper;
	struct i2c_client *client;
	struct power_supply *psy;
	int uv_per_unit;
	int ua_per_unit;
};

static int ug3105_read_word(struct i2c_client *client, u8 reg)
{
	int val;

	val = i2c_smbus_read_word_data(client, reg);
	if (val < 0)
		dev_err(&client->dev, "Error reading reg 0x%02x\n", reg);

	return val;
}

static int ug3105_get_voltage_and_current_now(struct power_supply *psy, int *volt, int *curr)
{
	struct ug3105_chip *chip = power_supply_get_drvdata(psy);
	int ret;

	ret = ug3105_read_word(chip->client, UG3105_REG_BAT_VOLT);
	if (ret < 0)
		return ret;

	*volt = ret * chip->uv_per_unit;

	ret = ug3105_read_word(chip->client, UG3105_REG_BAT_CURR);
	if (ret < 0)
		return ret;

	*curr = (s16)ret * chip->ua_per_unit;
	return 0;
}

static const struct power_supply_desc ug3105_psy_desc = {
	.name		= "ug3105_battery",
	.type		= POWER_SUPPLY_TYPE_BATTERY,
	.get_property	= adc_battery_helper_get_property,
	.external_power_changed	= adc_battery_helper_external_power_changed,
	.properties	= adc_battery_helper_properties,
	.num_properties	= ADC_HELPER_NUM_PROPERTIES,
};

static void ug3105_start(struct i2c_client *client)
{
	i2c_smbus_write_byte_data(client, UG3105_REG_MODE, UG3105_MODE_RUN);
	i2c_smbus_write_byte_data(client, UG3105_REG_CTRL1, UG3105_CTRL1_RESET_COULOMB_CNT);
}

static void ug3105_stop(struct i2c_client *client)
{
	i2c_smbus_write_byte_data(client, UG3105_REG_MODE, UG3105_MODE_STANDBY);
}

static int ug3105_probe(struct i2c_client *client)
{
	struct power_supply_config psy_cfg = {};
	struct device *dev = &client->dev;
	u32 curr_sense_res_uohm = 10000;
	struct ug3105_chip *chip;
	int ret;

	chip = devm_kzalloc(dev, sizeof(*chip), GFP_KERNEL);
	if (!chip)
		return -ENOMEM;

	chip->client = client;

	ug3105_start(client);

	device_property_read_u32(dev, "upisemi,rsns-microohm", &curr_sense_res_uohm);

	/*
	 * DAC maximum is 4.5V divided by 65536 steps + an unknown factor of 10
	 * coming from somewhere for some reason (verified with a volt-meter).
	 */
	chip->uv_per_unit = 45000000 / 65536;
	/* Datasheet says 8.1 uV per unit for the current ADC */
	chip->ua_per_unit = 8100000 / curr_sense_res_uohm;

	psy_cfg.drv_data = chip;
	chip->psy = devm_power_supply_register(dev, &ug3105_psy_desc, &psy_cfg);
	if (IS_ERR(chip->psy)) {
		ret = PTR_ERR(chip->psy);
		goto stop;
	}

	ret = adc_battery_helper_init(&chip->helper, chip->psy,
				      ug3105_get_voltage_and_current_now, NULL);
	if (ret)
		goto stop;

	i2c_set_clientdata(client, chip);
	return 0;

stop:
	ug3105_stop(client);
	return ret;
}

static int __maybe_unused ug3105_suspend(struct device *dev)
{
	struct ug3105_chip *chip = dev_get_drvdata(dev);

	adc_battery_helper_suspend(dev);
	ug3105_stop(chip->client);
	return 0;
}

static int __maybe_unused ug3105_resume(struct device *dev)
{
	struct ug3105_chip *chip = dev_get_drvdata(dev);

	ug3105_start(chip->client);
	adc_battery_helper_resume(dev);
	return 0;
}

static SIMPLE_DEV_PM_OPS(ug3105_pm_ops, ug3105_suspend,
			ug3105_resume);

static const struct i2c_device_id ug3105_id[] = {
	{ "ug3105" },
	{ }
};
MODULE_DEVICE_TABLE(i2c, ug3105_id);

static struct i2c_driver ug3105_i2c_driver = {
	.driver	= {
		.name = "ug3105",
		.pm = &ug3105_pm_ops,
	},
	.probe = ug3105_probe,
	.remove = ug3105_stop,
	.shutdown = ug3105_stop,
	.id_table = ug3105_id,
};
module_i2c_driver(ug3105_i2c_driver);

MODULE_AUTHOR("Hans de Goede <hansg@kernel.org");
MODULE_DESCRIPTION("uPI uG3105 battery monitor driver");
MODULE_LICENSE("GPL");
