// SPDX-License-Identifier: GPL-2.0
/*
 * Fuel gauge driver for CellWise 2017
 *
 * Copyright (C) 2012, RockChip
 *
 * Authors: Shunqing Chen <csq@rock-chips.com>
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

#define CW2017_SIZE_BATINFO		80

#define CW2017_REG_VERSION		0x00
#define CW2017_REG_VCELL_H		0x02
#define CW2017_REG_VCELL_L		0x03
#define CW2017_REG_SOC_INT		0x04
#define CW2017_REG_SOC_DECIMAL		0x05
#define CW2017_REG_TEMP			0x06
#define CW2017_REG_MODE_CONFIG		0x08
#define CW2017_REG_INT_CONFIG		0x0A
#define CW2017_REG_SOC_ALERT		0x0B
#define CW2017_REG_TEMP_MAX		0x0C
#define CW2017_REG_TEMP_MIN		0x0D
#define CW2017_REG_VOLT_ID_H		0x0E
#define CW2017_REG_VOLT_ID_L		0x0F
#define CW2017_REG_BATINFO		0x10

#define CW2017_MODE_SLEEP		0x30
#define CW2017_MODE_NORMAL		0x00
#define CW2017_MODE_DEFAULT		0xF0

#define CW2017_CONFIG_UPDATE_FLG	0x80
#define NO_START_VERSION		160

#define	TEMP_MAX_ALERT			0xFFFF
#define	TEMP_MIN_ALERT			0xFFFF
#define TEMP_ALERT_DISABLE		0xFFFF

#define INT_CONFIG_MIN_TEMP_MARK        BIT(4)
#define INT_CONFIG_MAX_TEMP_MARK        BIT(5)
#define INT_CONFIG_SOC_CHANGE_MARK      BIT(6)

#define DEF_DESIGN_CAPACITY		4000

#define CW2017_MASK_ATHD		GENMASK(7, 0)

/* reset gauge of no valid state of charge could be polled for 40s */
#define CW2017_BAT_SOC_ERROR_MS		(40 * MSEC_PER_SEC)
/* reset gauge if state of charge stuck for half an hour during charging */
#define CW2017_BAT_CHARGING_STUCK_MS	(1800 * MSEC_PER_SEC)

/* poll interval from CellWise GPL Android driver example */
#define CW2017_DEFAULT_POLL_INTERVAL_MS	8000

struct cw_battery {
	struct device *dev;
	struct workqueue_struct *battery_workqueue;
	struct delayed_work battery_delay_work;
	struct regmap *regmap;
	struct power_supply *rk_bat;
	struct power_supply_battery_info battery;
	u8 *bat_profile;

	bool charger_attached;
	bool battery_changed;

	int soc;
	int voltage_mv;
	int status;
	int charge_count;
	int design_capacity;

	u32 poll_interval_ms;
	u32 alert_level;
	int temp_max;
	int temp_min;
	int temp;

	bool dual_cell;

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

static void cw2017_enable(struct cw_battery *cw_bat)
{
	unsigned char reg_val = CW2017_MODE_DEFAULT;

	regmap_write(cw_bat->regmap, CW2017_REG_MODE_CONFIG, reg_val);
	msleep(20);
	reg_val = CW2017_MODE_SLEEP;
	regmap_write(cw_bat->regmap, CW2017_REG_MODE_CONFIG, reg_val);
	msleep(20);
	reg_val = CW2017_MODE_NORMAL;
	regmap_write(cw_bat->regmap, CW2017_REG_MODE_CONFIG, reg_val);
	msleep(20);
}

static int cw_update_profile(struct cw_battery *cw_bat)
{
	int ret;
	unsigned int reg_val = 0;
	unsigned char int_mask = 0;

	/* write new battery info */
	ret = regmap_raw_write(cw_bat->regmap, CW2017_REG_BATINFO,
			       cw_bat->bat_profile,
			       CW2017_SIZE_BATINFO);
	if (ret)
		return ret;

	/* set config update flag  */
	reg_val |= CW2017_CONFIG_UPDATE_FLG;
	reg_val &= ~CW2017_MASK_ATHD;
	reg_val |= cw_bat->alert_level;
	regmap_write(cw_bat->regmap, CW2017_REG_SOC_ALERT, reg_val);

	if (cw_bat->alert_level)
		int_mask |= INT_CONFIG_SOC_CHANGE_MARK;

	cw_bat->temp_max = TEMP_MAX_ALERT;
	cw_bat->temp_min = TEMP_MIN_ALERT;
	if (cw_bat->temp_max != TEMP_ALERT_DISABLE) {
		int_mask |= INT_CONFIG_MAX_TEMP_MARK;
		reg_val = (cw_bat->temp_max + 40) * 2;
		regmap_write(cw_bat->regmap, CW2017_REG_TEMP_MAX, reg_val);
	}
	if (cw_bat->temp_min != TEMP_ALERT_DISABLE) {
		int_mask |= INT_CONFIG_MIN_TEMP_MARK;
		reg_val = (cw_bat->temp_min + 40) * 2;
		regmap_write(cw_bat->regmap, CW2017_REG_TEMP_MIN, reg_val);
	}
	regmap_write(cw_bat->regmap, CW2017_REG_INT_CONFIG, int_mask);

	/* wait for gauge to reset */
	msleep(20);

	/* wait for gauge to become ready */
	ret = regmap_read_poll_timeout(cw_bat->regmap, CW2017_REG_SOC_INT,
				       reg_val, reg_val <= 100,
				       10 * USEC_PER_MSEC, 10 * USEC_PER_SEC);
	if (ret)
		dev_err(cw_bat->dev,
			"Gauge did not become ready after profile upload\n");
	else
		dev_dbg(cw_bat->dev, "Battery profile updated\n");

	cw2017_enable(cw_bat);
	dev_dbg(cw_bat->dev, "Battery profile configured\n");

	return ret;
}

static int cw_init(struct cw_battery *cw_bat)
{
	int ret;
	unsigned int reg_val;
	unsigned int config_flg;

	regmap_read(cw_bat->regmap, CW2017_REG_MODE_CONFIG, &reg_val);
	regmap_read(cw_bat->regmap, CW2017_REG_SOC_ALERT, &config_flg);

	if (reg_val != CW2017_MODE_NORMAL || !(config_flg & CW2017_CONFIG_UPDATE_FLG)) {
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
		u8 bat_info[CW2017_SIZE_BATINFO];

		ret = regmap_raw_read(cw_bat->regmap, CW2017_REG_BATINFO,
				      bat_info, CW2017_SIZE_BATINFO);
		if (ret) {
			dev_err(cw_bat->dev,
				"Failed to read stored battery profile\n");
			return ret;
		}

		if (memcmp(bat_info, cw_bat->bat_profile, CW2017_SIZE_BATINFO)) {
			dev_warn(cw_bat->dev, "Replacing stored battery profile\n");
			ret = cw_update_profile(cw_bat);
			if (ret)
				return ret;
		}
	} else {
		dev_warn(cw_bat->dev,
			 "Can't check current battery profile, no profile provided\n");
	}

	return 0;
}

#define HYSTERESIS(current, previous, up, down) \
	(((current) < (previous) + (up)) && ((current) > (previous) - (down)))

static int cw_get_soc(struct cw_battery *cw_bat)
{
	unsigned int soc;

	regmap_read(cw_bat->regmap, CW2017_REG_SOC_INT, &soc);
	if (soc > 100) {
		int max_error_cycles =
			CW2017_BAT_SOC_ERROR_MS / cw_bat->poll_interval_ms;

		dev_err(cw_bat->dev, "Invalid SoC %d%%\n", soc);
		cw_bat->read_errors++;
		if (cw_bat->read_errors > max_error_cycles) {
			dev_warn(cw_bat->dev,
				 "Too many invalid SoC reports, resetting gauge\n");
			cw_bat->read_errors = 0;
		}
		return cw_bat->soc;
	}
	cw_bat->read_errors = 0;

	/* Reset gauge if stuck while charging */
	if (cw_bat->status == POWER_SUPPLY_STATUS_CHARGING && soc == cw_bat->soc) {
		int max_stuck_cycles =
			CW2017_BAT_CHARGING_STUCK_MS / cw_bat->poll_interval_ms;

		cw_bat->charge_stuck_cnt++;
		if (cw_bat->charge_stuck_cnt > max_stuck_cycles) {
			dev_warn(cw_bat->dev,
				 "SoC stuck @%u%%, resetting gauge\n", soc);
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
	int voltage_mv;
	u16 reg_val = 0;

	cw_read_word(cw_bat, CW2017_REG_VCELL_H, &reg_val);
	reg_val &= 0x3fff;
	voltage_mv = reg_val * 5 / 16;
	if (cw_bat->dual_cell)
		voltage_mv *= 2;

	dev_dbg(cw_bat->dev, "Read voltage: %d mV, raw=0x%04x\n",
		voltage_mv, reg_val);

	return voltage_mv;
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

static int cw_update_temp(struct cw_battery *cw_bat)
{
	unsigned int val = 0;

	regmap_read(cw_bat->regmap, CW2017_REG_TEMP, &val);
	cw_bat->temp = val * 10 / 2 - 400;

	return cw_bat->temp;
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

static void cw_bat_work(struct work_struct *work)
{
	struct delayed_work *delay_work;
	struct cw_battery *cw_bat;

	delay_work = to_delayed_work(work);
	cw_bat = container_of(delay_work, struct cw_battery, battery_delay_work);

	cw_update_soc(cw_bat);
	cw_update_voltage(cw_bat);
	cw_update_charge_status(cw_bat);
	cw_update_temp(cw_bat);
	cw_update_status(cw_bat);

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
		val->intval = 0;
		break;

	case POWER_SUPPLY_PROP_TECHNOLOGY:
		val->intval = POWER_SUPPLY_TECHNOLOGY_LION;
		break;

	case POWER_SUPPLY_PROP_CHARGE_COUNTER:
		val->intval = cw_bat->charge_count;
		break;

	case POWER_SUPPLY_PROP_TEMP:
		val->intval = cw_bat->temp;
		break;

	case POWER_SUPPLY_PROP_CHARGE_FULL:
	case POWER_SUPPLY_PROP_CHARGE_FULL_DESIGN:
		if (cw_bat->battery.charge_full_design_uah > 0)
			val->intval = cw_bat->battery.charge_full_design_uah;
		else
			val->intval = cw_bat->design_capacity * 1000;
		break;

	case POWER_SUPPLY_PROP_CURRENT_NOW:
		val->intval = 0;
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
	POWER_SUPPLY_PROP_TEMP,
	POWER_SUPPLY_PROP_CHARGE_FULL,
	POWER_SUPPLY_PROP_CHARGE_FULL_DESIGN,
	POWER_SUPPLY_PROP_CURRENT_NOW,
};

static const struct power_supply_desc cw2017_bat_desc = {
	.name		= "cw2017-battery",
	.type		= POWER_SUPPLY_TYPE_BATTERY,
	.properties	= cw_battery_properties,
	.num_properties	= ARRAY_SIZE(cw_battery_properties),
	.get_property	= cw_battery_get_property,
};

static int cw2017_parse_properties(struct cw_battery *cw_bat)
{
	struct device *dev = cw_bat->dev;
	int length;
	int ret;

	length = device_property_count_u8(dev, "cellwise,battery-profile");
	if (length < 0) {
		dev_warn(cw_bat->dev,
			 "No battery-profile found, using current flash contents\n");
	} else if (length != CW2017_SIZE_BATINFO) {
		dev_err(cw_bat->dev, "battery-profile must be %d bytes\n",
			CW2017_SIZE_BATINFO);
		return -EINVAL;
	}

	cw_bat->bat_profile = devm_kzalloc(dev, length, GFP_KERNEL);
	if (!cw_bat->bat_profile)
		return -ENOMEM;

	ret = device_property_read_u8_array(dev,
					    "cellwise,battery-profile",
					    cw_bat->bat_profile,
					    length);
	if (ret)
		return ret;

	ret = device_property_read_u32(dev, "cellwise,design-capacity-amh",
				       &cw_bat->design_capacity);
	if (ret) {
		dev_info(cw_bat->dev, "Missing design capacity\n");
		cw_bat->design_capacity = DEF_DESIGN_CAPACITY;
	}

	device_property_read_u32(dev, "cellwise,alert-level",
				 &cw_bat->alert_level);

	cw_bat->dual_cell = device_property_read_bool(dev, "cellwise,dual-cell");

	ret = device_property_read_u32(dev, "cellwise,monitor-interval-ms",
				       &cw_bat->poll_interval_ms);
	if (ret) {
		dev_dbg(cw_bat->dev, "Using default poll interval\n");
		cw_bat->poll_interval_ms = CW2017_DEFAULT_POLL_INTERVAL_MS;
	}

	return 0;
}

static const struct regmap_range regmap_ranges_rd_yes[] = {
	regmap_reg_range(CW2017_REG_VERSION, CW2017_REG_VERSION),
	regmap_reg_range(CW2017_REG_VCELL_H, CW2017_REG_TEMP),
	regmap_reg_range(CW2017_REG_MODE_CONFIG, CW2017_REG_MODE_CONFIG),
	regmap_reg_range(CW2017_REG_INT_CONFIG,
			 CW2017_REG_BATINFO + CW2017_SIZE_BATINFO - 1),
};

static const struct regmap_access_table regmap_rd_table = {
	.yes_ranges = regmap_ranges_rd_yes,
	.n_yes_ranges = ARRAY_SIZE(regmap_ranges_rd_yes),
};

static const struct regmap_range regmap_ranges_wr_yes[] = {
	regmap_reg_range(CW2017_REG_MODE_CONFIG, CW2017_REG_MODE_CONFIG),
	regmap_reg_range(CW2017_REG_INT_CONFIG, CW2017_REG_TEMP_MIN),
	regmap_reg_range(CW2017_REG_BATINFO,
			 CW2017_REG_BATINFO + CW2017_SIZE_BATINFO - 1),
};

static const struct regmap_access_table regmap_wr_table = {
	.yes_ranges = regmap_ranges_wr_yes,
	.n_yes_ranges = ARRAY_SIZE(regmap_ranges_wr_yes),
};

static const struct regmap_range regmap_ranges_vol_yes[] = {
	regmap_reg_range(CW2017_REG_VCELL_H, CW2017_REG_TEMP),
};

static const struct regmap_access_table regmap_vol_table = {
	.yes_ranges = regmap_ranges_vol_yes,
	.n_yes_ranges = ARRAY_SIZE(regmap_ranges_vol_yes),
};

static const struct regmap_config cw2017_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.rd_table = &regmap_rd_table,
	.wr_table = &regmap_wr_table,
	.volatile_table = &regmap_vol_table,
	.max_register = CW2017_REG_BATINFO + CW2017_SIZE_BATINFO - 1,
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

	ret = cw2017_parse_properties(cw_bat);
	if (ret) {
		dev_err(cw_bat->dev, "Failed to parse cw2017 properties\n");
		return ret;
	}

	cw_bat->regmap = devm_regmap_init_i2c(client, &cw2017_regmap_config);
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
						    &cw2017_bat_desc,
						    &psy_cfg);
	if (IS_ERR(cw_bat->rk_bat)) {
		dev_err(cw_bat->dev, "Failed to register power supply\n");
		return PTR_ERR(cw_bat->rk_bat);
	}

	ret = power_supply_get_battery_info(cw_bat->rk_bat, &cw_bat->battery);
	if (ret) {
		dev_warn(cw_bat->dev,
			 "No monitored battery, some properties will be missing\n");
	}

	cw_bat->battery_workqueue = create_singlethread_workqueue("rk_battery");
	INIT_DELAYED_WORK(&cw_bat->battery_delay_work, cw_bat_work);
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

static int cw_bat_remove(struct i2c_client *client)
{
	struct cw_battery *cw_bat = i2c_get_clientdata(client);

	cancel_delayed_work_sync(&cw_bat->battery_delay_work);
	power_supply_put_battery_info(cw_bat->rk_bat, &cw_bat->battery);
	return 0;
}

static const struct i2c_device_id cw_bat_id_table[] = {
	{ "cw2017", 0 },
	{ }
};

static const struct of_device_id cw2017_of_match[] = {
	{ .compatible = "cellwise,cw2017" },
	{ }
};
MODULE_DEVICE_TABLE(of, cw2017_of_match);

static struct i2c_driver cw_bat_driver = {
	.driver = {
		.name = "cw2017",
		.of_match_table = cw2017_of_match,
		.pm = &cw_bat_pm_ops,
	},
	.probe_new = cw_bat_probe,
	.remove = cw_bat_remove,
	.id_table = cw_bat_id_table,
};

module_i2c_driver(cw_bat_driver);

MODULE_AUTHOR("Shunqing Chen<csq@rock-chips.com>");
MODULE_DESCRIPTION("cw2017 battery driver");
MODULE_LICENSE("GPL");
