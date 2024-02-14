// SPDX-License-Identifier: GPL-2.0
/*
 * Fuel gauge driver for CellWise 2013 / 2015
 *
 * Copyright (C) 2012, RockChip
 * Copyright (C) 2020, Tobias Schramm
 *
 * Authors: xuhuicong <xhc@rock-chips.com>
 * Authors: Tobias Schramm <t.schramm@manjaro.org>
 */

#include <linux/bits.h>
#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/gfp.h>
#include <linux/gpio/consumer.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/power_supply.h>
#include <linux/property.h>
#include <linux/regmap.h>
#include <linux/time.h>
#include <linux/workqueue.h>
#include <linux/devm-helpers.h>

#define CW2015_SIZE_BATINFO		64

#define CW2015_RESET_TRIES		5

#define CW2015_REG_VERSION		0x00
#define CW2015_REG_VCELL		0x02
#define CW2015_REG_SOC			0x04
#define CW2015_REG_RRT_ALERT		0x06
#define CW2015_REG_CONFIG		0x08
#define CW2015_REG_MODE			0x0A
#define CW2015_REG_BATINFO		0x10

#define CW2015_MODE_SLEEP_MASK		GENMASK(7, 6)
#define CW2015_MODE_SLEEP		(0x03 << 6)
#define CW2015_MODE_NORMAL		(0x00 << 6)
#define CW2015_MODE_QUICK_START		(0x03 << 4)
#define CW2015_MODE_RESTART		(0x0f << 0)

#define CW2015_CONFIG_UPDATE_FLG	(0x01 << 1)
#define CW2015_ATHD(x)			((x) << 3)
#define CW2015_MASK_ATHD		GENMASK(7, 3)
#define CW2015_MASK_SOC			GENMASK(12, 0)

/* reset gauge of no valid state of charge could be polled for 40s */
#define CW2015_BAT_SOC_ERROR_MS		(40 * MSEC_PER_SEC)
/* reset gauge if state of charge stuck for half an hour during charging */
#define CW2015_BAT_CHARGING_STUCK_MS	(1800 * MSEC_PER_SEC)

/* poll interval from CellWise GPL Android driver example */
#define CW2015_DEFAULT_POLL_INTERVAL_MS		8000

#define CW2015_AVERAGING_SAMPLES		3

struct cw_battery {
	struct device *dev;
	struct workqueue_struct *battery_workqueue;
	struct delayed_work battery_delay_work;
	struct regmap *regmap;
	struct power_supply *rk_bat;
	struct power_supply_battery_info *battery;
	u8 *bat_profile;

	bool charger_attached;
	bool battery_changed;

	int soc;
	int voltage_mv;
	int status;
	int time_to_empty;
	int charge_count;

	u32 poll_interval_ms;
	u8 alert_level;

	unsigned int read_errors;
	unsigned int charge_stuck_cnt;
};

static int cw_read_word(struct cw_battery *cw_bat, u8 reg, u16 *val)
{
	__be16 value;
	int ret;

	ret = regmap_bulk_read(cw_bat->regmap, reg, &value, sizeof(value));
	if (ret)
		return ret;

	*val = be16_to_cpu(value);
	return 0;
}

static int cw_update_profile(struct cw_battery *cw_bat)
{
	int ret;
	unsigned int reg_val;
	u8 reset_val;

	/* make sure gauge is not in sleep mode */
	ret = regmap_read(cw_bat->regmap, CW2015_REG_MODE, &reg_val);
	if (ret)
		return ret;

	reset_val = reg_val;
	if ((reg_val & CW2015_MODE_SLEEP_MASK) == CW2015_MODE_SLEEP) {
		dev_err(cw_bat->dev,
			"Gauge is in sleep mode, can't update battery info\n");
		return -EINVAL;
	}

	/* write new battery info */
	ret = regmap_raw_write(cw_bat->regmap, CW2015_REG_BATINFO,
			       cw_bat->bat_profile,
			       CW2015_SIZE_BATINFO);
	if (ret)
		return ret;

	/* set config update flag  */
	reg_val |= CW2015_CONFIG_UPDATE_FLG;
	reg_val &= ~CW2015_MASK_ATHD;
	reg_val |= CW2015_ATHD(cw_bat->alert_level);
	ret = regmap_write(cw_bat->regmap, CW2015_REG_CONFIG, reg_val);
	if (ret)
		return ret;

	/* reset gauge to apply new battery profile */
	reset_val &= ~CW2015_MODE_RESTART;
	reg_val = reset_val | CW2015_MODE_RESTART;
	ret = regmap_write(cw_bat->regmap, CW2015_REG_MODE, reg_val);
	if (ret)
		return ret;

	/* wait for gauge to reset */
	msleep(20);

	/* clear reset flag */
	ret = regmap_write(cw_bat->regmap, CW2015_REG_MODE, reset_val);
	if (ret)
		return ret;

	/* wait for gauge to become ready */
	ret = regmap_read_poll_timeout(cw_bat->regmap, CW2015_REG_SOC,
				       reg_val, reg_val <= 100,
				       10 * USEC_PER_MSEC, 10 * USEC_PER_SEC);
	if (ret)
		dev_err(cw_bat->dev,
			"Gauge did not become ready after profile upload\n");
	else
		dev_dbg(cw_bat->dev, "Battery profile updated\n");

	return ret;
}

static int cw_init(struct cw_battery *cw_bat)
{
	int ret;
	unsigned int reg_val = CW2015_MODE_SLEEP;

	if ((reg_val & CW2015_MODE_SLEEP_MASK) == CW2015_MODE_SLEEP) {
		reg_val = CW2015_MODE_NORMAL;
		ret = regmap_write(cw_bat->regmap, CW2015_REG_MODE, reg_val);
		if (ret)
			return ret;
	}

	ret = regmap_read(cw_bat->regmap, CW2015_REG_CONFIG, &reg_val);
	if (ret)
		return ret;

	if ((reg_val & CW2015_MASK_ATHD) != CW2015_ATHD(cw_bat->alert_level)) {
		dev_dbg(cw_bat->dev, "Setting new alert level\n");
		reg_val &= ~CW2015_MASK_ATHD;
		reg_val |= ~CW2015_ATHD(cw_bat->alert_level);
		ret = regmap_write(cw_bat->regmap, CW2015_REG_CONFIG, reg_val);
		if (ret)
			return ret;
	}

	ret = regmap_read(cw_bat->regmap, CW2015_REG_CONFIG, &reg_val);
	if (ret)
		return ret;

	if (!(reg_val & CW2015_CONFIG_UPDATE_FLG)) {
		dev_dbg(cw_bat->dev,
			"Battery profile not present, uploading battery profile\n");
		if (cw_bat->bat_profile) {
			ret = cw_update_profile(cw_bat);
			if (ret) {
				dev_err(cw_bat->dev,
					"Failed to upload battery profile\n");
				return ret;
			}
		} else {
			dev_warn(cw_bat->dev,
				 "No profile specified, continuing without profile\n");
		}
	} else if (cw_bat->bat_profile) {
		u8 bat_info[CW2015_SIZE_BATINFO];

		ret = regmap_raw_read(cw_bat->regmap, CW2015_REG_BATINFO,
				      bat_info, CW2015_SIZE_BATINFO);
		if (ret) {
			dev_err(cw_bat->dev,
				"Failed to read stored battery profile\n");
			return ret;
		}

		if (memcmp(bat_info, cw_bat->bat_profile, CW2015_SIZE_BATINFO)) {
			dev_warn(cw_bat->dev, "Replacing stored battery profile\n");
			ret = cw_update_profile(cw_bat);
			if (ret)
				return ret;
		}
	} else {
		dev_warn(cw_bat->dev,
			 "Can't check current battery profile, no profile provided\n");
	}

	dev_dbg(cw_bat->dev, "Battery profile configured\n");
	return 0;
}

static int cw_power_on_reset(struct cw_battery *cw_bat)
{
	int ret;
	unsigned char reset_val;

	reset_val = CW2015_MODE_SLEEP;
	ret = regmap_write(cw_bat->regmap, CW2015_REG_MODE, reset_val);
	if (ret)
		return ret;

	/* wait for gauge to enter sleep */
	msleep(20);

	reset_val = CW2015_MODE_NORMAL;
	ret = regmap_write(cw_bat->regmap, CW2015_REG_MODE, reset_val);
	if (ret)
		return ret;

	ret = cw_init(cw_bat);
	if (ret)
		return ret;
	return 0;
}

#define HYSTERESIS(current, previous, up, down) \
	(((current) < (previous) + (up)) && ((current) > (previous) - (down)))

static int cw_get_soc(struct cw_battery *cw_bat)
{
	unsigned int soc;
	int ret;

	ret = regmap_read(cw_bat->regmap, CW2015_REG_SOC, &soc);
	if (ret)
		return ret;

	if (soc > 100) {
		int max_error_cycles =
			CW2015_BAT_SOC_ERROR_MS / cw_bat->poll_interval_ms;

		dev_err(cw_bat->dev, "Invalid SoC %d%%\n", soc);
		cw_bat->read_errors++;
		if (cw_bat->read_errors > max_error_cycles) {
			dev_warn(cw_bat->dev,
				 "Too many invalid SoC reports, resetting gauge\n");
			cw_power_on_reset(cw_bat);
			cw_bat->read_errors = 0;
		}
		return cw_bat->soc;
	}
	cw_bat->read_errors = 0;

	/* Reset gauge if stuck while charging */
	if (cw_bat->status == POWER_SUPPLY_STATUS_CHARGING && soc == cw_bat->soc) {
		int max_stuck_cycles =
			CW2015_BAT_CHARGING_STUCK_MS / cw_bat->poll_interval_ms;

		cw_bat->charge_stuck_cnt++;
		if (cw_bat->charge_stuck_cnt > max_stuck_cycles) {
			dev_warn(cw_bat->dev,
				 "SoC stuck @%u%%, resetting gauge\n", soc);
			cw_power_on_reset(cw_bat);
			cw_bat->charge_stuck_cnt = 0;
		}
	} else {
		cw_bat->charge_stuck_cnt = 0;
	}

	/* Ignore voltage dips during charge */
	if (cw_bat->charger_attached && HYSTERESIS(soc, cw_bat->soc, 0, 3))
		soc = cw_bat->soc;

	/* Ignore voltage spikes during discharge */
	if (!cw_bat->charger_attached && HYSTERESIS(soc, cw_bat->soc, 3, 0))
		soc = cw_bat->soc;

	return soc;
}

static int cw_get_voltage(struct cw_battery *cw_bat)
{
	int ret, i, voltage_mv;
	u16 reg_val;
	u32 avg = 0;

	for (i = 0; i < CW2015_AVERAGING_SAMPLES; i++) {
		ret = cw_read_word(cw_bat, CW2015_REG_VCELL, &reg_val);
		if (ret)
			return ret;

		avg += reg_val;
	}
	avg /= CW2015_AVERAGING_SAMPLES;

	/*
	 * 305 uV per ADC step
	 * Use 312 / 1024  as efficient approximation of 305 / 1000
	 * Negligible error of 0.1%
	 */
	voltage_mv = avg * 312 / 1024;

	dev_dbg(cw_bat->dev, "Read voltage: %d mV, raw=0x%04x\n",
		voltage_mv, reg_val);
	return voltage_mv;
}

static int cw_get_time_to_empty(struct cw_battery *cw_bat)
{
	int ret;
	u16 value16;

	ret = cw_read_word(cw_bat, CW2015_REG_RRT_ALERT, &value16);
	if (ret)
		return ret;

	return value16 & CW2015_MASK_SOC;
}

static void cw_update_charge_status(struct cw_battery *cw_bat)
{
	int ret;

	ret = power_supply_am_i_supplied(cw_bat->rk_bat);
	if (ret < 0) {
		dev_warn(cw_bat->dev, "Failed to get supply state: %d\n", ret);
	} else {
		bool charger_attached;

		charger_attached = !!ret;
		if (cw_bat->charger_attached != charger_attached) {
			cw_bat->battery_changed = true;
			if (charger_attached)
				cw_bat->charge_count++;
		}
		cw_bat->charger_attached = charger_attached;
	}
}

static void cw_update_soc(struct cw_battery *cw_bat)
{
	int soc;

	soc = cw_get_soc(cw_bat);
	if (soc < 0)
		dev_err(cw_bat->dev, "Failed to get SoC from gauge: %d\n", soc);
	else if (cw_bat->soc != soc) {
		cw_bat->soc = soc;
		cw_bat->battery_changed = true;
	}
}

static void cw_update_voltage(struct cw_battery *cw_bat)
{
	int voltage_mv;

	voltage_mv = cw_get_voltage(cw_bat);
	if (voltage_mv < 0)
		dev_err(cw_bat->dev, "Failed to get voltage from gauge: %d\n",
			voltage_mv);
	else
		cw_bat->voltage_mv = voltage_mv;
}

static void cw_update_status(struct cw_battery *cw_bat)
{
	int status = POWER_SUPPLY_STATUS_DISCHARGING;

	if (cw_bat->charger_attached) {
		if (cw_bat->soc >= 100)
			status = POWER_SUPPLY_STATUS_FULL;
		else
			status = POWER_SUPPLY_STATUS_CHARGING;
	}

	if (cw_bat->status != status)
		cw_bat->battery_changed = true;
	cw_bat->status = status;
}

static void cw_update_time_to_empty(struct cw_battery *cw_bat)
{
	int time_to_empty;

	time_to_empty = cw_get_time_to_empty(cw_bat);
	if (time_to_empty < 0)
		dev_err(cw_bat->dev, "Failed to get time to empty from gauge: %d\n",
			time_to_empty);
	else if (cw_bat->time_to_empty != time_to_empty) {
		cw_bat->time_to_empty = time_to_empty;
		cw_bat->battery_changed = true;
	}
}

static void cw_bat_work(struct work_struct *work)
{
	struct delayed_work *delay_work;
	struct cw_battery *cw_bat;
	int ret;
	unsigned int reg_val;

	delay_work = to_delayed_work(work);
	cw_bat = container_of(delay_work, struct cw_battery, battery_delay_work);
	ret = regmap_read(cw_bat->regmap, CW2015_REG_MODE, &reg_val);
	if (ret) {
		dev_err(cw_bat->dev, "Failed to read mode from gauge: %d\n", ret);
	} else {
		if ((reg_val & CW2015_MODE_SLEEP_MASK) == CW2015_MODE_SLEEP) {
			int i;

			for (i = 0; i < CW2015_RESET_TRIES; i++) {
				if (!cw_power_on_reset(cw_bat))
					break;
			}
		}
		cw_update_soc(cw_bat);
		cw_update_voltage(cw_bat);
		cw_update_charge_status(cw_bat);
		cw_update_status(cw_bat);
		cw_update_time_to_empty(cw_bat);
	}
	dev_dbg(cw_bat->dev, "charger_attached = %d\n", cw_bat->charger_attached);
	dev_dbg(cw_bat->dev, "status = %d\n", cw_bat->status);
	dev_dbg(cw_bat->dev, "soc = %d%%\n", cw_bat->soc);
	dev_dbg(cw_bat->dev, "voltage = %dmV\n", cw_bat->voltage_mv);

	if (cw_bat->battery_changed)
		power_supply_changed(cw_bat->rk_bat);
	cw_bat->battery_changed = false;

	queue_delayed_work(cw_bat->battery_workqueue,
			   &cw_bat->battery_delay_work,
			   msecs_to_jiffies(cw_bat->poll_interval_ms));
}

static bool cw_battery_valid_time_to_empty(struct cw_battery *cw_bat)
{
	return	cw_bat->time_to_empty > 0 &&
		cw_bat->time_to_empty < CW2015_MASK_SOC &&
		cw_bat->status == POWER_SUPPLY_STATUS_DISCHARGING;
}

static int cw_battery_get_property(struct power_supply *psy,
				   enum power_supply_property psp,
				   union power_supply_propval *val)
{
	struct cw_battery *cw_bat;

	cw_bat = power_supply_get_drvdata(psy);
	switch (psp) {
	case POWER_SUPPLY_PROP_CAPACITY:
		val->intval = cw_bat->soc;
		break;

	case POWER_SUPPLY_PROP_STATUS:
		val->intval = cw_bat->status;
		break;

	case POWER_SUPPLY_PROP_PRESENT:
		val->intval = !!cw_bat->voltage_mv;
		break;

	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
		val->intval = cw_bat->voltage_mv * 1000;
		break;

	case POWER_SUPPLY_PROP_TIME_TO_EMPTY_NOW:
		if (cw_battery_valid_time_to_empty(cw_bat))
			val->intval = cw_bat->time_to_empty;
		else
			val->intval = 0;
		break;

	case POWER_SUPPLY_PROP_TECHNOLOGY:
		val->intval = POWER_SUPPLY_TECHNOLOGY_LION;
		break;

	case POWER_SUPPLY_PROP_CHARGE_COUNTER:
		val->intval = cw_bat->charge_count;
		break;

	case POWER_SUPPLY_PROP_CHARGE_FULL:
	case POWER_SUPPLY_PROP_CHARGE_FULL_DESIGN:
		if (cw_bat->battery->charge_full_design_uah > 0)
			val->intval = cw_bat->battery->charge_full_design_uah;
		else
			val->intval = 0;
		break;

	case POWER_SUPPLY_PROP_CHARGE_NOW:
		val->intval = cw_bat->battery->charge_full_design_uah;
		val->intval = val->intval * cw_bat->soc / 100;
		break;

	case POWER_SUPPLY_PROP_CURRENT_NOW:
		if (cw_battery_valid_time_to_empty(cw_bat) &&
		    cw_bat->battery->charge_full_design_uah > 0) {
			/* calculate remaining capacity */
			val->intval = cw_bat->battery->charge_full_design_uah;
			val->intval = val->intval * cw_bat->soc / 100;

			/* estimate current based on time to empty */
			val->intval = 60 * val->intval / cw_bat->time_to_empty;
		} else {
			val->intval = 0;
		}

		break;

	default:
		break;
	}
	return 0;
}

static enum power_supply_property cw_battery_properties[] = {
	POWER_SUPPLY_PROP_CAPACITY,
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_TIME_TO_EMPTY_NOW,
	POWER_SUPPLY_PROP_TECHNOLOGY,
	POWER_SUPPLY_PROP_CHARGE_COUNTER,
	POWER_SUPPLY_PROP_CHARGE_FULL,
	POWER_SUPPLY_PROP_CHARGE_FULL_DESIGN,
	POWER_SUPPLY_PROP_CHARGE_NOW,
	POWER_SUPPLY_PROP_CURRENT_NOW,
};

static const struct power_supply_desc cw2015_bat_desc = {
	.name		= "cw2015-battery",
	.type		= POWER_SUPPLY_TYPE_BATTERY,
	.properties	= cw_battery_properties,
	.num_properties	= ARRAY_SIZE(cw_battery_properties),
	.get_property	= cw_battery_get_property,
};

static int cw2015_parse_properties(struct cw_battery *cw_bat)
{
	struct device *dev = cw_bat->dev;
	int length;
	int ret;

	length = device_property_count_u8(dev, "cellwise,battery-profile");
	if (length < 0) {
		dev_warn(cw_bat->dev,
			 "No battery-profile found, using current flash contents\n");
	} else if (length != CW2015_SIZE_BATINFO) {
		dev_err(cw_bat->dev, "battery-profile must be %d bytes\n",
			CW2015_SIZE_BATINFO);
		return -EINVAL;
	} else {
		cw_bat->bat_profile = devm_kzalloc(dev, length, GFP_KERNEL);
		if (!cw_bat->bat_profile)
			return -ENOMEM;

		ret = device_property_read_u8_array(dev,
						"cellwise,battery-profile",
						cw_bat->bat_profile,
						length);
		if (ret)
			return ret;
	}

	ret = device_property_read_u32(dev, "cellwise,monitor-interval-ms",
				       &cw_bat->poll_interval_ms);
	if (ret) {
		dev_dbg(cw_bat->dev, "Using default poll interval\n");
		cw_bat->poll_interval_ms = CW2015_DEFAULT_POLL_INTERVAL_MS;
	}

	return 0;
}

static const struct regmap_range regmap_ranges_rd_yes[] = {
	regmap_reg_range(CW2015_REG_VERSION, CW2015_REG_VERSION),
	regmap_reg_range(CW2015_REG_VCELL, CW2015_REG_CONFIG),
	regmap_reg_range(CW2015_REG_MODE, CW2015_REG_MODE),
	regmap_reg_range(CW2015_REG_BATINFO,
			CW2015_REG_BATINFO + CW2015_SIZE_BATINFO - 1),
};

static const struct regmap_access_table regmap_rd_table = {
	.yes_ranges = regmap_ranges_rd_yes,
	.n_yes_ranges = 4,
};

static const struct regmap_range regmap_ranges_wr_yes[] = {
	regmap_reg_range(CW2015_REG_RRT_ALERT, CW2015_REG_CONFIG),
	regmap_reg_range(CW2015_REG_MODE, CW2015_REG_MODE),
	regmap_reg_range(CW2015_REG_BATINFO,
			CW2015_REG_BATINFO + CW2015_SIZE_BATINFO - 1),
};

static const struct regmap_access_table regmap_wr_table = {
	.yes_ranges = regmap_ranges_wr_yes,
	.n_yes_ranges = 3,
};

static const struct regmap_range regmap_ranges_vol_yes[] = {
	regmap_reg_range(CW2015_REG_VCELL, CW2015_REG_SOC + 1),
};

static const struct regmap_access_table regmap_vol_table = {
	.yes_ranges = regmap_ranges_vol_yes,
	.n_yes_ranges = 1,
};

static const struct regmap_config cw2015_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.rd_table = &regmap_rd_table,
	.wr_table = &regmap_wr_table,
	.volatile_table = &regmap_vol_table,
	.max_register = CW2015_REG_BATINFO + CW2015_SIZE_BATINFO - 1,
};

static int cw_bat_probe(struct i2c_client *client)
{
	int ret;
	struct cw_battery *cw_bat;
	struct power_supply_config psy_cfg = { 0 };

	cw_bat = devm_kzalloc(&client->dev, sizeof(*cw_bat), GFP_KERNEL);
	if (!cw_bat)
		return -ENOMEM;

	i2c_set_clientdata(client, cw_bat);
	cw_bat->dev = &client->dev;
	cw_bat->soc = 1;

	ret = cw2015_parse_properties(cw_bat);
	if (ret) {
		dev_err(cw_bat->dev, "Failed to parse cw2015 properties\n");
		return ret;
	}

	cw_bat->regmap = devm_regmap_init_i2c(client, &cw2015_regmap_config);
	if (IS_ERR(cw_bat->regmap)) {
		dev_err(cw_bat->dev, "Failed to allocate regmap: %ld\n",
			PTR_ERR(cw_bat->regmap));
		return PTR_ERR(cw_bat->regmap);
	}

	ret = cw_init(cw_bat);
	if (ret) {
		dev_err(cw_bat->dev, "Init failed: %d\n", ret);
		return ret;
	}

	psy_cfg.drv_data = cw_bat;
	psy_cfg.fwnode = dev_fwnode(cw_bat->dev);

	cw_bat->rk_bat = devm_power_supply_register(&client->dev,
						    &cw2015_bat_desc,
						    &psy_cfg);
	if (IS_ERR(cw_bat->rk_bat)) {
		/* try again if this happens */
		dev_err_probe(&client->dev, PTR_ERR(cw_bat->rk_bat),
			"Failed to register power supply\n");
		return PTR_ERR(cw_bat->rk_bat);
	}

	ret = power_supply_get_battery_info(cw_bat->rk_bat, &cw_bat->battery);
	if (ret) {
		/* Allocate an empty battery */
		cw_bat->battery = devm_kzalloc(&client->dev,
					       sizeof(*cw_bat->battery),
					       GFP_KERNEL);
		if (!cw_bat->battery)
			return -ENOMEM;
		dev_warn(cw_bat->dev,
			 "No monitored battery, some properties will be missing\n");
	}

	cw_bat->battery_workqueue = create_singlethread_workqueue("rk_battery");
	if (!cw_bat->battery_workqueue)
		return -ENOMEM;

	devm_delayed_work_autocancel(&client->dev,
							  &cw_bat->battery_delay_work, cw_bat_work);
	queue_delayed_work(cw_bat->battery_workqueue,
			   &cw_bat->battery_delay_work, msecs_to_jiffies(10));
	return 0;
}

static int __maybe_unused cw_bat_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct cw_battery *cw_bat = i2c_get_clientdata(client);

	cancel_delayed_work_sync(&cw_bat->battery_delay_work);
	return 0;
}

static int __maybe_unused cw_bat_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct cw_battery *cw_bat = i2c_get_clientdata(client);

	queue_delayed_work(cw_bat->battery_workqueue,
			   &cw_bat->battery_delay_work, 0);
	return 0;
}

static SIMPLE_DEV_PM_OPS(cw_bat_pm_ops, cw_bat_suspend, cw_bat_resume);

static const struct i2c_device_id cw_bat_id_table[] = {
	{ "cw2015", 0 },
	{ }
};

static const struct of_device_id cw2015_of_match[] = {
	{ .compatible = "cellwise,cw2015" },
	{ }
};
MODULE_DEVICE_TABLE(of, cw2015_of_match);

static struct i2c_driver cw_bat_driver = {
	.driver = {
		.name = "cw2015",
		.of_match_table = cw2015_of_match,
		.pm = &cw_bat_pm_ops,
	},
	.probe_new = cw_bat_probe,
	.id_table = cw_bat_id_table,
};

module_i2c_driver(cw_bat_driver);

MODULE_AUTHOR("xhc<xhc@rock-chips.com>");
MODULE_AUTHOR("Tobias Schramm <t.schramm@manjaro.org>");
MODULE_DESCRIPTION("cw2015/cw2013 battery driver");
MODULE_LICENSE("GPL");
