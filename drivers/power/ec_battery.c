/*
 * ec battery driver
 *
 * Copyright (C) 2016 Rockchip Electronics Co., Ltd
 * Shunqing Chen <csq@rock-chips.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 */

#include <linux/gpio.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/power_supply.h>

static int dbg_enable;
module_param_named(dbg_level, dbg_enable, int, 0644);

#define DBG(args...) \
	do { \
		if (dbg_enable) { \
			printk(args); \
		} \
	} while (0)

struct ec_battery {
	struct i2c_client	*i2c;
	struct device		*dev;
	struct regmap		*regmap;
	struct power_supply	*bat;
	struct workqueue_struct	*bat_monitor_wq;
	struct delayed_work	bat_delay_work;
	u32			monitor_sec;
	u32			bat_mode;
	u16			status;
	int			current_now;
	u16			voltage_now;
	u16			rem_capacity;
	u16			full_charge_capacity;
	u16			design_capacity;
	int			temperature_now;
	int			soc;
	bool			is_charge;
	bool			dis_charge;
	bool			is_ctitical;
	bool			is_battery_low;
	bool			is_battery_in;
	bool			is_ac_in;
	struct gpio_desc	*ec_notify_io;
};

enum bat_mode {
	MODE_BATTARY = 0,
	MODE_VIRTUAL,
};

/* virtual params */
#define VIRTUAL_CURRENT			1000
#define VIRTUAL_VOLTAGE			3888
#define VIRTUAL_SOC			66
#define VIRTUAL_PRESET			1
#define VIRTUAL_TEMPERATURE		188
#define VIRTUAL_STATUS			POWER_SUPPLY_STATUS_CHARGING

#define TIMER_MS_COUNTS			1000
#define DEFAULT_MONITOR_SEC		5

#define EC_GET_VERSION_COMMOND		0x10
#define EC_GET_VERSION_INFO_NUM		(5)
#define EC_GET_BATTERY_INFO_COMMOND	0x07
#define EC_GET_PARAMETER_NUM		(13)
#define EC_GET_BATTERY_OTHER_COMMOND	0x08
#define EC_GET_BATTERYINFO_NUM		(7)

#define EC_GET_BIT(a, b)	(((a) & (1 << (b))) ? 1 : 0)
#define EC_DIS_CHARGE(a)	EC_GET_BIT(a, 0)
#define EC_IS_CHARGE(a)		EC_GET_BIT(a, 1)
#define EC_IS_CRITICAL(a)	EC_GET_BIT(a, 2)
#define EC_IS_BATTERY_LOW(a)	EC_GET_BIT(a, 3)
#define EC_IS_BATTERY_IN(a)	EC_GET_BIT(a, 6)
#define EC_IS_AC_IN(a)		EC_GET_BIT(a, 7)

static int ec_i2c_read(struct ec_battery *bat, u8 cmd, u8 *dest, u16 len)
{
	struct i2c_client *i2c = bat->i2c;
	int ret;
	struct i2c_msg msg[2];
	u8 buf[2];

	buf[0] = cmd; /* EC_GET_BATTERY_INFO_COMMOND; */
	msg[0].addr = i2c->addr;
	msg[0].flags = i2c->flags & I2C_M_TEN;
	msg[0].len = 1;
	msg[0].buf = buf;

	msg[1].addr = i2c->addr;
	msg[1].flags = i2c->flags & I2C_M_TEN;
	msg[1].flags |= I2C_M_RD;
	msg[1].len = len;
	msg[1].buf = dest;

	ret = i2c_transfer(i2c->adapter, msg, 2);

	return ret;
}

static void ec_dump_info(struct ec_battery *bat)
{
	int temp;

	DBG("==========================================\n");
	DBG("battery status: %x\n", bat->status);
	temp = bat->temperature_now / 10;
	DBG("Temp: %d K (%d C))\n", temp, (temp - 272));
	DBG("current_now: %d ma\n", bat->current_now);
	DBG("voltage_now: %d mv\n", bat->voltage_now);
	DBG("Charge:    %d %%\n", bat->soc);
	DBG("Remaining: %d mAh\n", bat->rem_capacity);
	DBG("Cap-full:  %d mAh\n", bat->full_charge_capacity);
	DBG("Design:    %d mAh\n", bat->design_capacity);
	DBG("==========================================\n");
}

static int ec_get_battery_info(struct ec_battery *bat)
{
	u8 buf[13] = {0};
	u16 voltage2;
	u16 full_charge_capacity_1;
	u16 design_capacity;
	u16 cur;
	int ret;
	int soc;

	ret = ec_i2c_read(bat, EC_GET_BATTERY_INFO_COMMOND, buf,
			  EC_GET_PARAMETER_NUM);
	if ((EC_GET_PARAMETER_NUM - 1) == buf[0]) {
		bat->status = buf[2] << 8 | buf[1];
		cur = (buf[4] << 8 | buf[3]);
		bat->current_now = cur;
		if (buf[4] & 0x80) {
			bat->current_now = (~cur) & 0xffff;
			bat->current_now = -(bat->current_now);
		}

		bat->rem_capacity = buf[6] << 8 | buf[5];
		bat->voltage_now = buf[8] << 8 | buf[7];
		bat->full_charge_capacity = buf[10] << 8 | buf[9];
		bat->temperature_now = buf[12] << 8 | buf[11];
		soc = (bat->rem_capacity + bat->full_charge_capacity / 101) *
			100 / bat->full_charge_capacity;
		if (soc > 100)
			bat->soc = 100;
		else if (soc < 0)
			bat->soc = 0;
		else
			bat->soc = soc;
	} else {
		dev_err(bat->dev, "get battery info from 0x07 erro\n");
	}

	ret = ec_i2c_read(bat, EC_GET_BATTERY_OTHER_COMMOND, buf,
			  EC_GET_BATTERYINFO_NUM);
	if ((EC_GET_BATTERYINFO_NUM - 1) == buf[0]) {
		full_charge_capacity_1 = buf[2] << 8 | buf[1];
		voltage2 = buf[4] << 8 | buf[3];	/* the same to uppo */
		design_capacity = buf[6] << 8 | buf[5];	/* the same to uppo */
		bat->design_capacity = design_capacity;
	}

	ec_dump_info(bat);

	return 0;
}

static int ec_get_current(struct ec_battery *bat)
{
	return bat->current_now * 1000;
}

static int ec_get_voltage(struct ec_battery *bat)
{
	return bat->voltage_now * 1000;
}

static int is_ec_bat_exist(struct ec_battery *bat)
{
	int is_exist;

	is_exist = EC_IS_BATTERY_IN(bat->status);
	return is_exist;
}

static int ec_get_capacity(struct ec_battery *bat)
{
	return bat->soc;
}

static int ec_get_temperature(struct ec_battery *bat)
{
	int temp;

	temp = bat->temperature_now - 2722;
	return temp;
}

static int ec_bat_chrg_online(struct ec_battery *bat)
{
	return EC_IS_CHARGE(bat->status);
}

#ifdef CONFIG_OF
static int ec_bat_parse_dt(struct ec_battery *bat)
{
	int ret;
	u32 out_value;
	struct device_node *np = bat->dev->of_node;

	bat->bat_mode = MODE_BATTARY;
	bat->monitor_sec = DEFAULT_MONITOR_SEC * TIMER_MS_COUNTS;

	ret = of_property_read_u32(np, "virtual_power", &bat->bat_mode);
	if (ret < 0)
		dev_err(bat->dev, "virtual_power missing!\n");

	ret = of_property_read_u32(np, "monitor_sec", &out_value);
	if (ret < 0)
		dev_err(bat->dev, "monitor_sec missing!\n");
	else
		bat->monitor_sec = out_value * TIMER_MS_COUNTS;

	bat->ec_notify_io =
		devm_gpiod_get_optional(bat->dev, "ec-notify",
					GPIOD_IN);
	if (!IS_ERR_OR_NULL(bat->ec_notify_io))
		gpiod_direction_output(bat->ec_notify_io, 0);

	return 0;
}
#else
static int ec_bat_parse_dt(struct ec_battery *bat)
{
	return -ENODEV;
}
#endif

static enum power_supply_property ec_bat_props[] = {
	POWER_SUPPLY_PROP_CURRENT_NOW,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_HEALTH,
	POWER_SUPPLY_PROP_CAPACITY,
	POWER_SUPPLY_PROP_TEMP,
	POWER_SUPPLY_PROP_STATUS,
};

static int ec_battery_get_property(struct power_supply *psy,
				   enum power_supply_property psp,
				   union power_supply_propval *val)
{
	struct ec_battery *bat = power_supply_get_drvdata(psy);

	switch (psp) {
	case POWER_SUPPLY_PROP_CURRENT_NOW:
		val->intval = ec_get_current(bat);/*uA*/
		if (bat->bat_mode == MODE_VIRTUAL)
			val->intval = VIRTUAL_CURRENT * 1000;
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
		val->intval = ec_get_voltage(bat);/*uV*/
		if (bat->bat_mode == MODE_VIRTUAL)
			val->intval = VIRTUAL_VOLTAGE * 1000;
		break;
	case POWER_SUPPLY_PROP_PRESENT:
		val->intval = is_ec_bat_exist(bat);
		if (bat->bat_mode == MODE_VIRTUAL)
			val->intval = VIRTUAL_PRESET;
		break;
	case POWER_SUPPLY_PROP_CAPACITY:
		val->intval = ec_get_capacity(bat);
		if (bat->bat_mode == MODE_VIRTUAL)
			val->intval = VIRTUAL_SOC;
		break;
	case POWER_SUPPLY_PROP_HEALTH:
		val->intval = POWER_SUPPLY_HEALTH_GOOD;
		break;
	case POWER_SUPPLY_PROP_TEMP:
		val->intval = ec_get_temperature(bat);
		if (bat->bat_mode == MODE_VIRTUAL)
			val->intval = VIRTUAL_TEMPERATURE;
		break;
	case POWER_SUPPLY_PROP_STATUS:
		if (bat->bat_mode == MODE_VIRTUAL)
			val->intval = VIRTUAL_STATUS;
		else if (ec_get_capacity(bat) == 100)
			val->intval = POWER_SUPPLY_STATUS_FULL;
		else if (ec_bat_chrg_online(bat))
			val->intval = POWER_SUPPLY_STATUS_CHARGING;
		else
			val->intval = POWER_SUPPLY_STATUS_DISCHARGING;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static const struct power_supply_desc ec_bat_desc = {
	.name		= "battery",
	.type		= POWER_SUPPLY_TYPE_BATTERY,
	.properties	= ec_bat_props,
	.num_properties	= ARRAY_SIZE(ec_bat_props),
	.get_property	= ec_battery_get_property,
};

static int ec_bat_init_power_supply(struct ec_battery *bat)
{
	struct power_supply_config psy_cfg = { .drv_data = bat, };

	bat->bat = power_supply_register(bat->dev, &ec_bat_desc, &psy_cfg);
	if (IS_ERR(bat->bat)) {
		dev_err(bat->dev, "register bat power supply fail\n");
		return PTR_ERR(bat->bat);
	}

	return 0;
}

static void ec_bat_power_supply_changed(struct ec_battery *ec_bat)
{
	bool state_changed;
	static int old_cap = -1;
	static int old_temperature;

	state_changed = false;
	if (ec_get_capacity(ec_bat) != old_cap)
		state_changed = true;
	else if (ec_get_temperature(ec_bat) != old_temperature)
		state_changed = true;

	if (state_changed) {
		power_supply_changed(ec_bat->bat);
		old_cap = ec_get_capacity(ec_bat);
		old_temperature = ec_get_temperature(ec_bat);
	}
}

static void ec_battery_work(struct work_struct *work)
{
	struct ec_battery *ec_bat =
		container_of(work, struct ec_battery, bat_delay_work.work);

	ec_get_battery_info(ec_bat);
	ec_bat_power_supply_changed(ec_bat);

	queue_delayed_work(ec_bat->bat_monitor_wq, &ec_bat->bat_delay_work,
			   msecs_to_jiffies(ec_bat->monitor_sec));
}

static int ec_charger_probe(struct i2c_client *client,
			    const struct i2c_device_id *id)
{
	struct ec_battery *ec_bat;
	int ret;

	ec_bat = devm_kzalloc(&client->dev, sizeof(*ec_bat), GFP_KERNEL);
	if (!ec_bat)
		return -ENOMEM;
	ec_bat->dev = &client->dev;
	ec_bat->i2c = client;
	i2c_set_clientdata(client, ec_bat);

	ret = ec_bat_parse_dt(ec_bat);
	if (ret < 0) {
		dev_err(ec_bat->dev, "parse dt failed!\n");
		return ret;
	}

	ret = ec_bat_init_power_supply(ec_bat);
	if (ret) {
		dev_err(ec_bat->dev, "init power supply fail!\n");
		return ret;
	}

	ec_bat->bat_monitor_wq =
		alloc_ordered_workqueue("%s",
					WQ_MEM_RECLAIM | WQ_FREEZABLE,
					"ec-bat-monitor-wq");
	INIT_DELAYED_WORK(&ec_bat->bat_delay_work, ec_battery_work);
	queue_delayed_work(ec_bat->bat_monitor_wq, &ec_bat->bat_delay_work,
			   msecs_to_jiffies(TIMER_MS_COUNTS * 5));

	return ret;
}

#ifdef CONFIG_PM_SLEEP
static int ec_bat_pm_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct ec_battery *ec_bat = i2c_get_clientdata(client);

	cancel_delayed_work_sync(&ec_bat->bat_delay_work);

	if (!IS_ERR_OR_NULL(ec_bat->ec_notify_io))
		gpiod_direction_output(ec_bat->ec_notify_io, 1);

	return 0;
}

static int ec_bat_pm_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct ec_battery *ec_bat = i2c_get_clientdata(client);

	if (!IS_ERR_OR_NULL(ec_bat->ec_notify_io))
		gpiod_direction_output(ec_bat->ec_notify_io, 0);

	queue_delayed_work(ec_bat->bat_monitor_wq, &ec_bat->bat_delay_work,
			   msecs_to_jiffies(TIMER_MS_COUNTS * 1));

	return 0;
}
#endif

static SIMPLE_DEV_PM_OPS(ec_bat_pm_ops, ec_bat_pm_suspend, ec_bat_pm_resume);

static const struct i2c_device_id ec_battery_i2c_ids[] = {
	{ "ec_battery" },
	{ },
};
MODULE_DEVICE_TABLE(i2c, ec_battery_i2c_ids);

#ifdef CONFIG_OF
static const struct of_device_id ec_of_match[] = {
	{ .compatible = "rockchip,ec-battery" },
	{ },
};
MODULE_DEVICE_TABLE(of, ec_of_match);
#else
static const struct of_device_id ec_of_match[] = {
	{ },
};
#endif

static struct i2c_driver ec_i2c_driver = {
	.driver = {
		.name		= "ec_battery",
		.pm		= &ec_bat_pm_ops,
		.of_match_table	= ec_of_match,
	},
	.id_table	= ec_battery_i2c_ids,
	.probe		= ec_charger_probe,
};

module_i2c_driver(ec_i2c_driver);

MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:ec-charger");
MODULE_AUTHOR("Shunqing Chen<csq@rock-chips.com>");
