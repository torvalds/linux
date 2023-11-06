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
 * 1. Activate commented out total_coulomb_count code
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
 * Copyright (C) 2021 Hans de Goede <hdegoede@redhat.com>
 */

#include <linux/devm-helpers.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/mod_devicetable.h>
#include <linux/power_supply.h>
#include <linux/workqueue.h>

#define UG3105_MOV_AVG_WINDOW					8
#define UG3105_INIT_POLL_TIME					(5 * HZ)
#define UG3105_POLL_TIME					(30 * HZ)
#define UG3105_SETTLE_TIME					(1 * HZ)

#define UG3105_INIT_POLL_COUNT					30

#define UG3105_REG_MODE						0x00
#define UG3105_REG_CTRL1					0x01
#define UG3105_REG_COULOMB_CNT					0x02
#define UG3105_REG_BAT_VOLT					0x08
#define UG3105_REG_BAT_CURR					0x0c

#define UG3105_MODE_STANDBY					0x00
#define UG3105_MODE_RUN						0x10

#define UG3105_CTRL1_RESET_COULOMB_CNT				0x03

#define UG3105_CURR_HYST_UA					65000

#define UG3105_LOW_BAT_UV					3700000
#define UG3105_FULL_BAT_HYST_UV					38000

struct ug3105_chip {
	struct i2c_client *client;
	struct power_supply *psy;
	struct power_supply_battery_info *info;
	struct delayed_work work;
	struct mutex lock;
	int ocv[UG3105_MOV_AVG_WINDOW];		/* micro-volt */
	int intern_res[UG3105_MOV_AVG_WINDOW];	/* milli-ohm */
	int poll_count;
	int ocv_avg_index;
	int ocv_avg;				/* micro-volt */
	int intern_res_poll_count;
	int intern_res_avg_index;
	int intern_res_avg;			/* milli-ohm */
	int volt;				/* micro-volt */
	int curr;				/* micro-ampere */
	int total_coulomb_count;
	int uv_per_unit;
	int ua_per_unit;
	int status;
	int capacity;
	bool supplied;
};

static int ug3105_read_word(struct i2c_client *client, u8 reg)
{
	int val;

	val = i2c_smbus_read_word_data(client, reg);
	if (val < 0)
		dev_err(&client->dev, "Error reading reg 0x%02x\n", reg);

	return val;
}

static int ug3105_get_status(struct ug3105_chip *chip)
{
	int full = chip->info->constant_charge_voltage_max_uv - UG3105_FULL_BAT_HYST_UV;

	if (chip->curr > UG3105_CURR_HYST_UA)
		return POWER_SUPPLY_STATUS_CHARGING;

	if (chip->curr < -UG3105_CURR_HYST_UA)
		return POWER_SUPPLY_STATUS_DISCHARGING;

	if (chip->supplied && chip->ocv_avg > full)
		return POWER_SUPPLY_STATUS_FULL;

	return POWER_SUPPLY_STATUS_NOT_CHARGING;
}

static int ug3105_get_capacity(struct ug3105_chip *chip)
{
	/*
	 * OCV voltages in uV for 0-110% in 5% increments, the 100-110% is
	 * for LiPo HV (High-Voltage) bateries which can go up to 4.35V
	 * instead of the usual 4.2V.
	 */
	static const int ocv_capacity_tbl[23] = {
		3350000,
		3610000,
		3690000,
		3710000,
		3730000,
		3750000,
		3770000,
		3786667,
		3803333,
		3820000,
		3836667,
		3853333,
		3870000,
		3907500,
		3945000,
		3982500,
		4020000,
		4075000,
		4110000,
		4150000,
		4200000,
		4250000,
		4300000,
	};
	int i, ocv_diff, ocv_step;

	if (chip->ocv_avg < ocv_capacity_tbl[0])
		return 0;

	if (chip->status == POWER_SUPPLY_STATUS_FULL)
		return 100;

	for (i = 1; i < ARRAY_SIZE(ocv_capacity_tbl); i++) {
		if (chip->ocv_avg > ocv_capacity_tbl[i])
			continue;

		ocv_diff = ocv_capacity_tbl[i] - chip->ocv_avg;
		ocv_step = ocv_capacity_tbl[i] - ocv_capacity_tbl[i - 1];
		/* scale 0-110% down to 0-100% for LiPo HV */
		if (chip->info->constant_charge_voltage_max_uv >= 4300000)
			return (i * 500 - ocv_diff * 500 / ocv_step) / 110;
		else
			return i * 5 - ocv_diff * 5 / ocv_step;
	}

	return 100;
}

static void ug3105_work(struct work_struct *work)
{
	struct ug3105_chip *chip = container_of(work, struct ug3105_chip,
						work.work);
	int i, val, curr_diff, volt_diff, res, win_size;
	bool prev_supplied = chip->supplied;
	int prev_status = chip->status;
	int prev_volt = chip->volt;
	int prev_curr = chip->curr;
	struct power_supply *psy;

	mutex_lock(&chip->lock);

	psy = chip->psy;
	if (!psy)
		goto out;

	val = ug3105_read_word(chip->client, UG3105_REG_BAT_VOLT);
	if (val < 0)
		goto out;
	chip->volt = val * chip->uv_per_unit;

	val = ug3105_read_word(chip->client, UG3105_REG_BAT_CURR);
	if (val < 0)
		goto out;
	chip->curr = (s16)val * chip->ua_per_unit;

	chip->ocv[chip->ocv_avg_index] =
		chip->volt - chip->curr * chip->intern_res_avg / 1000;
	chip->ocv_avg_index = (chip->ocv_avg_index + 1) % UG3105_MOV_AVG_WINDOW;
	chip->poll_count++;

	/*
	 * See possible improvements comment above.
	 *
	 * Read + reset coulomb counter every 10 polls (every 300 seconds)
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
	 */

	chip->ocv_avg = 0;
	win_size = min(chip->poll_count, UG3105_MOV_AVG_WINDOW);
	for (i = 0; i < win_size; i++)
		chip->ocv_avg += chip->ocv[i];
	chip->ocv_avg /= win_size;

	chip->supplied = power_supply_am_i_supplied(psy);
	chip->status = ug3105_get_status(chip);
	chip->capacity = ug3105_get_capacity(chip);

	/*
	 * Skip internal resistance calc on charger [un]plug and
	 * when the battery is almost empty (voltage low).
	 */
	if (chip->supplied != prev_supplied ||
	    chip->volt < UG3105_LOW_BAT_UV ||
	    chip->poll_count < 2)
		goto out;

	/*
	 * Assuming that the OCV voltage does not change significantly
	 * between 2 polls, then we can calculate the internal resistance
	 * on a significant current change by attributing all voltage
	 * change between the 2 readings to the internal resistance.
	 */
	curr_diff = abs(chip->curr - prev_curr);
	if (curr_diff < UG3105_CURR_HYST_UA)
		goto out;

	volt_diff = abs(chip->volt - prev_volt);
	res = volt_diff * 1000 / curr_diff;

	if ((res < (chip->intern_res_avg * 2 / 3)) ||
	    (res > (chip->intern_res_avg * 4 / 3))) {
		dev_dbg(&chip->client->dev, "Ignoring outlier internal resistance %d mOhm\n", res);
		goto out;
	}

	dev_dbg(&chip->client->dev, "Internal resistance %d mOhm\n", res);

	chip->intern_res[chip->intern_res_avg_index] = res;
	chip->intern_res_avg_index = (chip->intern_res_avg_index + 1) % UG3105_MOV_AVG_WINDOW;
	chip->intern_res_poll_count++;

	chip->intern_res_avg = 0;
	win_size = min(chip->intern_res_poll_count, UG3105_MOV_AVG_WINDOW);
	for (i = 0; i < win_size; i++)
		chip->intern_res_avg += chip->intern_res[i];
	chip->intern_res_avg /= win_size;

out:
	mutex_unlock(&chip->lock);

	queue_delayed_work(system_wq, &chip->work,
			   (chip->poll_count <= UG3105_INIT_POLL_COUNT) ?
					UG3105_INIT_POLL_TIME : UG3105_POLL_TIME);

	if (chip->status != prev_status && psy)
		power_supply_changed(psy);
}

static enum power_supply_property ug3105_battery_props[] = {
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_TECHNOLOGY,
	POWER_SUPPLY_PROP_SCOPE,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_VOLTAGE_OCV,
	POWER_SUPPLY_PROP_CURRENT_NOW,
	POWER_SUPPLY_PROP_CAPACITY,
};

static int ug3105_get_property(struct power_supply *psy,
			       enum power_supply_property psp,
			       union power_supply_propval *val)
{
	struct ug3105_chip *chip = power_supply_get_drvdata(psy);
	int ret = 0;

	mutex_lock(&chip->lock);

	if (!chip->psy) {
		ret = -EAGAIN;
		goto out;
	}

	switch (psp) {
	case POWER_SUPPLY_PROP_STATUS:
		val->intval = chip->status;
		break;
	case POWER_SUPPLY_PROP_PRESENT:
		val->intval = 1;
		break;
	case POWER_SUPPLY_PROP_TECHNOLOGY:
		val->intval = chip->info->technology;
		break;
	case POWER_SUPPLY_PROP_SCOPE:
		val->intval = POWER_SUPPLY_SCOPE_SYSTEM;
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
		ret = ug3105_read_word(chip->client, UG3105_REG_BAT_VOLT);
		if (ret < 0)
			break;
		val->intval = ret * chip->uv_per_unit;
		ret = 0;
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_OCV:
		val->intval = chip->ocv_avg;
		break;
	case POWER_SUPPLY_PROP_CURRENT_NOW:
		ret = ug3105_read_word(chip->client, UG3105_REG_BAT_CURR);
		if (ret < 0)
			break;
		val->intval = (s16)ret * chip->ua_per_unit;
		ret = 0;
		break;
	case POWER_SUPPLY_PROP_CAPACITY:
		val->intval = chip->capacity;
		break;
	default:
		ret = -EINVAL;
	}

out:
	mutex_unlock(&chip->lock);
	return ret;
}

static void ug3105_external_power_changed(struct power_supply *psy)
{
	struct ug3105_chip *chip = power_supply_get_drvdata(psy);

	dev_dbg(&chip->client->dev, "external power changed\n");
	mod_delayed_work(system_wq, &chip->work, UG3105_SETTLE_TIME);
}

static const struct power_supply_desc ug3105_psy_desc = {
	.name		= "ug3105_battery",
	.type		= POWER_SUPPLY_TYPE_BATTERY,
	.get_property	= ug3105_get_property,
	.external_power_changed	= ug3105_external_power_changed,
	.properties	= ug3105_battery_props,
	.num_properties	= ARRAY_SIZE(ug3105_battery_props),
};

static void ug3105_init(struct ug3105_chip *chip)
{
	chip->poll_count = 0;
	chip->ocv_avg_index = 0;
	chip->total_coulomb_count = 0;
	i2c_smbus_write_byte_data(chip->client, UG3105_REG_MODE,
				  UG3105_MODE_RUN);
	i2c_smbus_write_byte_data(chip->client, UG3105_REG_CTRL1,
				  UG3105_CTRL1_RESET_COULOMB_CNT);
	queue_delayed_work(system_wq, &chip->work, 0);
	flush_delayed_work(&chip->work);
}

static int ug3105_probe(struct i2c_client *client)
{
	struct power_supply_config psy_cfg = {};
	struct device *dev = &client->dev;
	u32 curr_sense_res_uohm = 10000;
	struct power_supply *psy;
	struct ug3105_chip *chip;
	int ret;

	chip = devm_kzalloc(dev, sizeof(*chip), GFP_KERNEL);
	if (!chip)
		return -ENOMEM;

	chip->client = client;
	mutex_init(&chip->lock);
	ret = devm_delayed_work_autocancel(dev, &chip->work, ug3105_work);
	if (ret)
		return ret;

	psy_cfg.drv_data = chip;
	psy = devm_power_supply_register(dev, &ug3105_psy_desc, &psy_cfg);
	if (IS_ERR(psy))
		return PTR_ERR(psy);

	ret = power_supply_get_battery_info(psy, &chip->info);
	if (ret)
		return ret;

	if (chip->info->factory_internal_resistance_uohm == -EINVAL ||
	    chip->info->constant_charge_voltage_max_uv == -EINVAL) {
		dev_err(dev, "error required properties are missing\n");
		return -ENODEV;
	}

	device_property_read_u32(dev, "upisemi,rsns-microohm", &curr_sense_res_uohm);

	/*
	 * DAC maximum is 4.5V divided by 65536 steps + an unknown factor of 10
	 * coming from somewhere for some reason (verified with a volt-meter).
	 */
	chip->uv_per_unit = 45000000/65536;
	/* Datasheet says 8.1 uV per unit for the current ADC */
	chip->ua_per_unit = 8100000 / curr_sense_res_uohm;

	/* Use provided internal resistance as start point (in milli-ohm) */
	chip->intern_res_avg = chip->info->factory_internal_resistance_uohm / 1000;
	/* Also add it to the internal resistance moving average window */
	chip->intern_res[0] = chip->intern_res_avg;
	chip->intern_res_avg_index = 1;
	chip->intern_res_poll_count = 1;

	mutex_lock(&chip->lock);
	chip->psy = psy;
	mutex_unlock(&chip->lock);

	ug3105_init(chip);

	i2c_set_clientdata(client, chip);
	return 0;
}

static int __maybe_unused ug3105_suspend(struct device *dev)
{
	struct ug3105_chip *chip = dev_get_drvdata(dev);

	cancel_delayed_work_sync(&chip->work);
	i2c_smbus_write_byte_data(chip->client, UG3105_REG_MODE,
				  UG3105_MODE_STANDBY);

	return 0;
}

static int __maybe_unused ug3105_resume(struct device *dev)
{
	struct ug3105_chip *chip = dev_get_drvdata(dev);

	ug3105_init(chip);

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
	.id_table = ug3105_id,
};
module_i2c_driver(ug3105_i2c_driver);

MODULE_AUTHOR("Hans de Goede <hdegoede@redhat.com");
MODULE_DESCRIPTION("uPI uG3105 battery monitor driver");
MODULE_LICENSE("GPL");
