/*
 * Fuel gauge driver for CellWise 2013 / 2015
 *
 * Copyright (C) 2012, RockChip
 *
 * Authors: xuhuicong <xhc@rock-chips.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#include <linux/delay.h>
#include <linux/init.h>
#include <linux/i2c.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of_gpio.h>
#include <linux/platform_device.h>
#include <linux/power_supply.h>
#include <linux/slab.h>
#include <linux/workqueue.h>

#include <linux/power/cw2015_battery.h>

static int dbg_enable;
module_param_named(dbg_level, dbg_enable, int, 0644);

#define cw_printk(args...) \
	do { \
		if (dbg_enable) { \
			pr_info(args); \
		} \
	} while (0)

static int cw_read(struct i2c_client *client, u8 reg, u8 buf[])
{
	return i2c_smbus_read_i2c_block_data(client, reg, 1, buf);
}

static int cw_write(struct i2c_client *client, u8 reg, u8 const buf[])
{
	return i2c_smbus_write_i2c_block_data(client, reg, 1, &buf[0]);
}

static int cw_read_word(struct i2c_client *client, u8 reg, u8 buf[])
{
	return i2c_smbus_read_i2c_block_data(client, reg, 2, buf);
}

int cw_update_config_info(struct cw_battery *cw_bat)
{
	int ret;
	u8 reg_val;
	u8 i;
	u8 reset_val;

	cw_printk("[FGADC] test config_info = 0x%x\n",
		  cw_bat->plat_data.cw_bat_config_info[0]);

	/* make sure no in sleep mode */
	ret = cw_read(cw_bat->client, REG_MODE, &reg_val);
	if (ret < 0)
		return ret;

	reset_val = reg_val;
	if ((reg_val & MODE_SLEEP_MASK) == MODE_SLEEP) {
		dev_err(&cw_bat->client->dev,
			"device in sleep mode, cannot update battery info\n");
		return -1;
	}

	/* update new battery info */
	for (i = 0; i < SIZE_BATINFO; i++) {
		ret =
		    cw_write(cw_bat->client, REG_BATINFO + i,
			     (u8 *)&cw_bat->plat_data.cw_bat_config_info[i]);

		if (ret < 0)
			return ret;
	}

	reg_val |= CONFIG_UPDATE_FLG;	/* set UPDATE_FLAG */
	reg_val &= 0x07;	/* clear ATHD */
	reg_val |= ATHD;	/* set ATHD */
	ret = cw_write(cw_bat->client, REG_CONFIG, &reg_val);
	if (ret < 0)
		return ret;

	/* check 2015/cw2013 for ATHD & update_flag */
	ret = cw_read(cw_bat->client, REG_CONFIG, &reg_val);
	if (ret < 0)
		return ret;

	if (!(reg_val & CONFIG_UPDATE_FLG)) {
		dev_info(&cw_bat->client->dev,
			 "update flag for new battery info have not set..\n");
	}

	if ((reg_val & 0xf8) != ATHD)
		dev_info(&cw_bat->client->dev, "the new ATHD have not set..\n");

	/* reset */
	reset_val &= ~(MODE_RESTART);
	reg_val = reset_val | MODE_RESTART;
	ret = cw_write(cw_bat->client, REG_MODE, &reg_val);
	if (ret < 0)
		return ret;

	msleep(10);
	ret = cw_write(cw_bat->client, REG_MODE, &reset_val);
	if (ret < 0)
		return ret;

	cw_printk("cw2015 update config success!\n");

	return 0;
}

static int cw_init(struct cw_battery *cw_bat)
{
	int ret;
	int i;
	u8 reg_val = MODE_SLEEP;

	if ((reg_val & MODE_SLEEP_MASK) == MODE_SLEEP) {
		reg_val = MODE_NORMAL;
		ret = cw_write(cw_bat->client, REG_MODE, &reg_val);
		if (ret < 0)
			return ret;
	}

	ret = cw_read(cw_bat->client, REG_CONFIG, &reg_val);
	if (ret < 0)
		return ret;

	if ((reg_val & 0xf8) != ATHD) {
		dev_info(&cw_bat->client->dev, "the new ATHD have not set\n");
		reg_val &= 0x07;	/* clear ATHD */
		reg_val |= ATHD;	/* set ATHD */
		ret = cw_write(cw_bat->client, REG_CONFIG, &reg_val);
		if (ret < 0)
			return ret;
	}

	ret = cw_read(cw_bat->client, REG_CONFIG, &reg_val);
	if (ret < 0)
		return ret;

	if (!(reg_val & CONFIG_UPDATE_FLG)) {
		cw_printk("update config flg is true, need update config\n");
		ret = cw_update_config_info(cw_bat);
		if (ret < 0) {
			dev_info(&cw_bat->client->dev,
				 "update flag for new battery info have not set\n");
			return ret;
		}
	} else {
		for (i = 0; i < SIZE_BATINFO; i++) {
			ret = cw_read(cw_bat->client, (REG_BATINFO + i),
				      &reg_val);
			if (ret < 0)
				return ret;

			if (cw_bat->plat_data.cw_bat_config_info[i] != reg_val)
				break;
		}

		if (i != SIZE_BATINFO) {
			dev_info(&cw_bat->client->dev,
				 "update flag for new battery info have not set\n");
			ret = cw_update_config_info(cw_bat);
			if (ret < 0)
				return ret;
		}
	}

	for (i = 0; i < 30; i++) {
		ret = cw_read(cw_bat->client, REG_SOC, &reg_val);
		if (ret < 0)
			return ret;
		else if (reg_val <= 0x64)
			break;
		msleep(120);
	}

	if (i >= 30) {
		reg_val = MODE_SLEEP;
		ret = cw_write(cw_bat->client, REG_MODE, &reg_val);
		dev_info(&cw_bat->client->dev, "report battery capacity error");
		return -1;
	}

	cw_printk("cw2015 init success!\n");
	return 0;
}

static int check_chrg_usb_psy(struct device *dev, void *data)
{
	struct power_supply *psy = dev_get_drvdata(dev);
	struct cw_battery *cw_bat = (struct cw_battery *)data;

	if (psy->desc->type == POWER_SUPPLY_TYPE_USB) {
		cw_bat->chrg_usb_psy = psy;
		return 1;
	}
	return 0;
}

static int check_chrg_ac_psy(struct device *dev, void *data)
{
	struct power_supply *psy = dev_get_drvdata(dev);
	struct cw_battery *cw_bat = (struct cw_battery *)data;

	if (psy->desc->type == POWER_SUPPLY_TYPE_MAINS) {
		cw_bat->chrg_ac_psy = psy;
		return 1;
	}
	return 0;
}

static void get_chrg_psy(struct cw_battery *cw_bat)
{
	if (!cw_bat->chrg_usb_psy)
		class_for_each_device(power_supply_class, NULL, cw_bat,
				      check_chrg_usb_psy);
	if (!cw_bat->chrg_ac_psy)
		class_for_each_device(power_supply_class, NULL, cw_bat,
				      check_chrg_ac_psy);
}

static int get_charge_state(struct cw_battery *cw_bat)
{
	union power_supply_propval val;
	int ret = -ENODEV;
	int usb_online = 0;
	int ac_online = 0;
	struct power_supply *chrg_usb_psy;
	struct power_supply *chrg_ac_psy;

	if (!cw_bat->chrg_usb_psy || !cw_bat->chrg_ac_psy)
		get_chrg_psy(cw_bat);

	chrg_usb_psy = cw_bat->chrg_usb_psy;
	chrg_ac_psy = cw_bat->chrg_ac_psy;
	if (chrg_usb_psy) {
		ret = chrg_usb_psy->desc->get_property(chrg_usb_psy,
						       POWER_SUPPLY_PROP_ONLINE,
						       &val);
		if (!ret)
			usb_online = val.intval;
	}
	if (chrg_ac_psy) {
		ret = chrg_ac_psy->desc->get_property(chrg_ac_psy,
						      POWER_SUPPLY_PROP_ONLINE,
						      &val);
		if (!ret)
			ac_online = val.intval;
	}
	if (!chrg_usb_psy)
		cw_printk("Usb online didn't find\n");
	if (!chrg_ac_psy)
		cw_printk("Ac online didn't find\n");

	cw_printk("ac_online = %d, usb_online = %d\n", ac_online, usb_online);
	if (ac_online || usb_online)
		return 1;

	return 0;
}

static int cw_por(struct cw_battery *cw_bat)
{
	int ret;
	unsigned char reset_val;

	reset_val = MODE_SLEEP;
	ret = cw_write(cw_bat->client, REG_MODE, &reset_val);
	if (ret < 0)
		return ret;
	reset_val = MODE_NORMAL;
	msleep(20);
	ret = cw_write(cw_bat->client, REG_MODE, &reset_val);
	if (ret < 0)
		return ret;
	ret = cw_init(cw_bat);
	if (ret)
		return ret;
	return 0;
}

static int cw_get_capacity(struct cw_battery *cw_bat)
{
	int cw_capacity;
	int ret;
	unsigned char reg_val[2];

	static int reset_loop;
	static int charging_loop;
	static int discharging_loop;
	static int jump_flag;
	static int charging_5_loop;
	int sleep_cap;

	ret = cw_read_word(cw_bat->client, REG_SOC, reg_val);
	if (ret < 0)
		return ret;

	cw_capacity = reg_val[0];

	if ((cw_capacity < 0) || (cw_capacity > 100)) {
		cw_printk("Error:  cw_capacity = %d\n", cw_capacity);
		reset_loop++;
		if (reset_loop >
		    (BATTERY_CAPACITY_ERROR / cw_bat->monitor_sec)) {
			cw_por(cw_bat);
			reset_loop = 0;
		}
		return cw_bat->capacity;
	} else {
		reset_loop = 0;
	}

	/* case 1 : aviod swing */
	if (((cw_bat->charger_mode > 0) &&
	     (cw_capacity <= cw_bat->capacity - 1) &&
	     (cw_capacity > cw_bat->capacity - 9)) ||
	    ((cw_bat->charger_mode == 0) &&
	     (cw_capacity == (cw_bat->capacity + 1)))) {
		if (!(cw_capacity == 0 && cw_bat->capacity <= 2))
			cw_capacity = cw_bat->capacity;
	}

	/* case 2 : aviod no charge full */
	if ((cw_bat->charger_mode > 0) &&
	    (cw_capacity >= 95) && (cw_capacity <= cw_bat->capacity)) {
		cw_printk("Chaman join no charge full\n");
		charging_loop++;
		if (charging_loop >
		    (BATTERY_UP_MAX_CHANGE / cw_bat->monitor_sec)) {
			cw_capacity = (cw_bat->capacity + 1) <= 100 ?
				      (cw_bat->capacity + 1) : 100;
			charging_loop = 0;
			jump_flag = 1;
		} else {
			cw_capacity = cw_bat->capacity;
		}
	}

	/* case 3 : avoid battery level jump to CW_BAT */
	if ((cw_bat->charger_mode == 0) &&
	    (cw_capacity <= cw_bat->capacity) &&
	    (cw_capacity >= 90) && (jump_flag == 1)) {
		cw_printk("Chaman join no charge full discharging\n");
#ifdef CONFIG_PM
		if (cw_bat->suspend_resume_mark == 1) {
			cw_bat->suspend_resume_mark = 0;
			sleep_cap = (cw_bat->after.tv_sec +
				     discharging_loop *
				     (cw_bat->monitor_sec / 1000)) /
				     (BATTERY_DOWN_MAX_CHANGE / 1000);
			cw_printk("sleep_cap = %d\n", sleep_cap);

			if (cw_capacity >= cw_bat->capacity - sleep_cap) {
				return cw_capacity;
			} else {
				if (!sleep_cap)
					discharging_loop = discharging_loop +
						1 + cw_bat->after.tv_sec /
						(cw_bat->monitor_sec / 1000);
				else
					discharging_loop = 0;
				cw_printk("discharging_loop = %d\n",
					  discharging_loop);
				return cw_bat->capacity - sleep_cap;
			}
		}
#endif
		discharging_loop++;
		if (discharging_loop >
		    (BATTERY_DOWN_MAX_CHANGE / cw_bat->monitor_sec)) {
			if (cw_capacity >= cw_bat->capacity - 1)
				jump_flag = 0;
			else
				cw_capacity = cw_bat->capacity - 1;

			discharging_loop = 0;
		} else {
			cw_capacity = cw_bat->capacity;
		}
	}

	/* case 4 : avoid battery level is 0% when long time charging */
	if ((cw_bat->charger_mode > 0) && (cw_capacity == 0)) {
		charging_5_loop++;
		if (charging_5_loop >
		    BATTERY_CHARGING_ZERO / cw_bat->monitor_sec) {
			cw_por(cw_bat);
			charging_5_loop = 0;
		}
	} else if (charging_5_loop != 0) {
		charging_5_loop = 0;
	}
#ifdef CONFIG_PM
	if (cw_bat->suspend_resume_mark == 1)
		cw_bat->suspend_resume_mark = 0;
#endif
	return cw_capacity;
}

static int cw_get_voltage(struct cw_battery *cw_bat)
{
	int ret;
	u8 reg_val[2];
	u16 value16, value16_1, value16_2, value16_3;
	int voltage;
	int res1, res2;

	ret = cw_read_word(cw_bat->client, REG_VCELL, reg_val);
	if (ret < 0)
		return ret;
	value16 = (reg_val[0] << 8) + reg_val[1];

	ret = cw_read_word(cw_bat->client, REG_VCELL, reg_val);
	if (ret < 0)
		return ret;
	value16_1 = (reg_val[0] << 8) + reg_val[1];

	ret = cw_read_word(cw_bat->client, REG_VCELL, reg_val);
	if (ret < 0)
		return ret;
	value16_2 = (reg_val[0] << 8) + reg_val[1];

	if (value16 > value16_1) {
		value16_3 = value16;
		value16 = value16_1;
		value16_1 = value16_3;
	}

	if (value16_1 > value16_2) {
		value16_3 = value16_1;
		value16_1 = value16_2;
		value16_2 = value16_3;
	}

	if (value16 > value16_1) {
		value16_3 = value16;
		value16 = value16_1;
		value16_1 = value16_3;
	}

	voltage = value16_1 * 312 / 1024;

	if (cw_bat->plat_data.divider_res1 &&
	    cw_bat->plat_data.divider_res2) {
		res1 = cw_bat->plat_data.divider_res1;
		res2 = cw_bat->plat_data.divider_res2;
		voltage = voltage * (res1 + res2) / res2;
	} else if (cw_bat->dual_battery) {
		voltage = voltage * 2;
	}

	dev_dbg(&cw_bat->client->dev, "the cw201x voltage=%d,reg_val=%x %x\n",
		voltage, reg_val[0], reg_val[1]);
	return voltage;
}

/*This function called when get RRT from cw2015*/
static int cw_get_time_to_empty(struct cw_battery *cw_bat)
{
	int ret;
	u8 reg_val;
	u16 value16;

	ret = cw_read(cw_bat->client, REG_RRT_ALERT, &reg_val);
	if (ret < 0)
		return ret;

	value16 = reg_val;

	ret = cw_read(cw_bat->client, REG_RRT_ALERT + 1, &reg_val);
	if (ret < 0)
		return ret;

	value16 = ((value16 << 8) + reg_val) & 0x1fff;
	return value16;
}

static void cw_update_charge_status(struct cw_battery *cw_bat)
{
	int cw_charger_mode;

	cw_charger_mode = get_charge_state(cw_bat);
	if (cw_bat->charger_mode != cw_charger_mode) {
		cw_bat->charger_mode = cw_charger_mode;
		cw_bat->bat_change = 1;
		if (cw_charger_mode)
			cw_bat->charge_count++;
	}
}

static void cw_update_capacity(struct cw_battery *cw_bat)
{
	int cw_capacity;

	cw_capacity = cw_get_capacity(cw_bat);
	if ((cw_capacity >= 0) && (cw_capacity <= 100) &&
	    (cw_bat->capacity != cw_capacity)) {
		cw_bat->capacity = cw_capacity;
		cw_bat->bat_change = 1;
	}
}

static void cw_update_vol(struct cw_battery *cw_bat)
{
	int ret;

	ret = cw_get_voltage(cw_bat);
	if ((ret >= 0) && (cw_bat->voltage != ret))
		cw_bat->voltage = ret;
}

static void cw_update_status(struct cw_battery *cw_bat)
{
	int status;

	if (cw_bat->charger_mode > 0) {
		if (cw_bat->capacity >= 100)
			status = POWER_SUPPLY_STATUS_FULL;
		else
			status = POWER_SUPPLY_STATUS_CHARGING;
	} else {
		status = POWER_SUPPLY_STATUS_DISCHARGING;
	}

	if (cw_bat->status != status) {
		cw_bat->status = status;
		cw_bat->bat_change = 1;
	}
}

static void cw_update_time_to_empty(struct cw_battery *cw_bat)
{
	int ret;

	ret = cw_get_time_to_empty(cw_bat);
	if ((ret >= 0) && (cw_bat->time_to_empty != ret)) {
		cw_bat->time_to_empty = ret;
		cw_bat->bat_change = 1;
	}
}

static void cw_bat_work(struct work_struct *work)
{
	struct delayed_work *delay_work;
	struct cw_battery *cw_bat;
	int ret;
	u8 reg_val;
	int i = 0;

	delay_work = container_of(work, struct delayed_work, work);
	cw_bat =
		container_of(delay_work, struct cw_battery, battery_delay_work);

	/* Add for battery swap start */
	ret = cw_read(cw_bat->client, REG_MODE, &reg_val);
	if (ret < 0) {
		cw_bat->bat_mode = MODE_VIRTUAL;
		cw_bat->bat_change = 1;
	} else {
		if ((reg_val & MODE_SLEEP_MASK) == MODE_SLEEP) {
			for (i = 0; i < 5; i++) {
				if (cw_por(cw_bat) == 0)
					break;
			}
		}
		cw_update_capacity(cw_bat);
		cw_update_vol(cw_bat);
		cw_update_charge_status(cw_bat);
		cw_update_status(cw_bat);
		cw_update_time_to_empty(cw_bat);
	}
	/* Add for battery swap end */
	cw_printk("charger_mod = %d\n", cw_bat->charger_mode);
	cw_printk("status = %d\n", cw_bat->status);
	cw_printk("capacity = %d\n", cw_bat->capacity);
	cw_printk("voltage = %d\n", cw_bat->voltage);

#ifdef CONFIG_PM
	if (cw_bat->suspend_resume_mark == 1)
		cw_bat->suspend_resume_mark = 0;
#endif

	if (cw_bat->bat_change == 1) {
		power_supply_changed(cw_bat->rk_bat);
		cw_bat->bat_change = 0;
	}
	queue_delayed_work(cw_bat->battery_workqueue,
			   &cw_bat->battery_delay_work,
			   msecs_to_jiffies(cw_bat->monitor_sec));
}

static int cw_battery_get_property(struct power_supply *psy,
				   enum power_supply_property psp,
				   union power_supply_propval *val)
{
	int ret = 0;
	struct cw_battery *cw_bat;

	cw_bat = power_supply_get_drvdata(psy);
	switch (psp) {
	case POWER_SUPPLY_PROP_CAPACITY:
		val->intval = cw_bat->capacity;
		if (cw_bat->bat_mode == MODE_VIRTUAL)
			val->intval = VIRTUAL_SOC;
		break;
	case POWER_SUPPLY_PROP_STATUS:
		val->intval = cw_bat->status;
		if (cw_bat->bat_mode == MODE_VIRTUAL)
			val->intval = VIRTUAL_STATUS;
		break;

	case POWER_SUPPLY_PROP_HEALTH:
		val->intval = POWER_SUPPLY_HEALTH_GOOD;
		break;
	case POWER_SUPPLY_PROP_PRESENT:
		val->intval = cw_bat->voltage <= 0 ? 0 : 1;
		if (cw_bat->bat_mode == MODE_VIRTUAL)
			val->intval = VIRTUAL_PRESET;
		break;

	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
		val->intval = cw_bat->voltage * 1000;
		if (cw_bat->bat_mode == MODE_VIRTUAL)
			val->intval = VIRTUAL_VOLTAGE * 1000;
		break;

	case POWER_SUPPLY_PROP_TIME_TO_EMPTY_NOW:
		val->intval = cw_bat->time_to_empty;
		if (cw_bat->bat_mode == MODE_VIRTUAL)
			val->intval = VIRTUAL_TIME2EMPTY;
		break;

	case POWER_SUPPLY_PROP_TECHNOLOGY:
		val->intval = POWER_SUPPLY_TECHNOLOGY_LION;
		break;

	case POWER_SUPPLY_PROP_CHARGE_COUNTER:
		val->intval = cw_bat->charge_count;
		break;

	case POWER_SUPPLY_PROP_TEMP:
		val->intval = VIRTUAL_TEMPERATURE;
		break;

	default:
		break;
	}
	return ret;
}

static enum power_supply_property cw_battery_properties[] = {
	POWER_SUPPLY_PROP_CAPACITY,
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_HEALTH,
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_TIME_TO_EMPTY_NOW,
	POWER_SUPPLY_PROP_TECHNOLOGY,
	POWER_SUPPLY_PROP_CHARGE_COUNTER,
	POWER_SUPPLY_PROP_TEMP,
};

static const struct power_supply_desc cw2015_bat_desc = {
	.name		= "rk-bat",
	.type		= POWER_SUPPLY_TYPE_BATTERY,
	.properties	= cw_battery_properties,
	.num_properties	= ARRAY_SIZE(cw_battery_properties),
	.get_property	= cw_battery_get_property,
};

#ifdef CONFIG_OF
static int cw2015_parse_dt(struct cw_battery *cw_bat)
{
	struct device *dev = &cw_bat->client->dev;
	struct device_node *node = dev->of_node;
	struct property *prop;
	int length;
	u32 value;
	int ret;
	struct cw_bat_platform_data *data = &cw_bat->plat_data;
	struct gpio_desc *hw_id0_io;
	struct gpio_desc *hw_id1_io;
	int hw_id0_val;
	int hw_id1_val;

	if (!node)
		return -ENODEV;

	memset(data, 0, sizeof(*data));

	ret = of_property_read_u32(node, "hw_id_check", &value);
	if (!ret && value) {
		hw_id0_io = gpiod_get_optional(dev, "hw-id0", GPIOD_IN);
		if (!hw_id0_io)
			return -EINVAL;
		if (IS_ERR(hw_id0_io))
			return PTR_ERR(hw_id0_io);

		hw_id0_val = gpiod_get_value(hw_id0_io);
		gpiod_put(hw_id0_io);

		hw_id1_io = gpiod_get_optional(dev, "hw-id1", GPIOD_IN);
		if (!hw_id1_io)
			return -EINVAL;
		if (IS_ERR(hw_id1_io))
			return PTR_ERR(hw_id1_io);

		hw_id1_val = gpiod_get_value(hw_id1_io);
		gpiod_put(hw_id1_io);

		/*
		 * ID1 = 0, ID0 = 1 : Battery
		 * ID1 = 1, ID0 = 0 : Dual Battery
		 * ID1 = 0, ID0 = 0 : Adapter
		 */
		if (hw_id0_val == 1 && hw_id1_val == 0)
			cw_bat->dual_battery = false;
		else if (hw_id0_val == 0 && hw_id1_val == 1)
			cw_bat->dual_battery = true;
		else
			return -EINVAL;
	}

	/* determine the number of config info */
	prop = of_find_property(node, "bat_config_info", &length);
	if (!prop)
		return -EINVAL;

	length /= sizeof(u32);

	if (length > 0) {
		size_t size = sizeof(*data->cw_bat_config_info) * length;

		data->cw_bat_config_info = devm_kzalloc(dev, size, GFP_KERNEL);
		if (!data->cw_bat_config_info)
			return -ENOMEM;

		ret = of_property_read_u32_array(node, "bat_config_info",
						 data->cw_bat_config_info,
						 length);
		if (ret < 0)
			return ret;
	}

	cw_bat->bat_mode = MODE_BATTARY;
	cw_bat->monitor_sec = DEFAULT_MONITOR_SEC * TIMER_MS_COUNTS;

	ret = of_property_read_u32(node, "divider_res1", &value);
	if (ret < 0)
		value = 0;
	data->divider_res1 = value;

	ret = of_property_read_u32(node, "divider_res2", &value);
	if (ret < 0)
		value = 0;
	data->divider_res2 = value;

	ret = of_property_read_u32(node, "virtual_power", &value);
	if (ret < 0)
		value = 0;
	cw_bat->bat_mode = value;

	ret = of_property_read_u32(node, "monitor_sec", &value);
	if (ret < 0)
		dev_err(dev, "monitor_sec missing!\n");
	else
		cw_bat->monitor_sec = value * TIMER_MS_COUNTS;

	return 0;
}
#else
static int cw2015_parse_dt(struct cw_battery *cw_bat)
{
	return -ENODEV;
}
#endif

static int cw_bat_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	int ret;
	struct cw_battery *cw_bat;
	struct power_supply_config psy_cfg = {0};

	cw_bat = devm_kzalloc(&client->dev, sizeof(*cw_bat), GFP_KERNEL);
	if (!cw_bat) {
		dev_err(&client->dev,
			"fail to allocate memory for cw2015\n");
		return -ENOMEM;
	}

	i2c_set_clientdata(client, cw_bat);
	cw_bat->client = client;

	ret = cw2015_parse_dt(cw_bat);
	if (ret < 0) {
		dev_err(&client->dev,
			"failed to find cw2015 platform data\n");
		return -1;
	}

	cw_bat->capacity = 1;
	cw_bat->voltage = 0;
	cw_bat->status = 0;
	cw_bat->suspend_resume_mark = 0;
	cw_bat->charger_mode = NO_CHARGING;
	cw_bat->bat_change = 0;

	ret = cw_init(cw_bat);
	if (ret) {
		pr_err("%s cw_init error\n", __func__);
		return ret;
	}

	psy_cfg.drv_data = cw_bat;

	cw_bat->rk_bat = devm_power_supply_register(&client->dev,
		&cw2015_bat_desc, &psy_cfg);
	if (IS_ERR(cw_bat->rk_bat)) {
		dev_err(&cw_bat->client->dev,
			"power supply register rk_bat error\n");
		return -1;
	}

	cw_bat->battery_workqueue = create_singlethread_workqueue("rk_battery");
	INIT_DELAYED_WORK(&cw_bat->battery_delay_work, cw_bat_work);
	queue_delayed_work(cw_bat->battery_workqueue,
			   &cw_bat->battery_delay_work, msecs_to_jiffies(10));

	dev_info(&cw_bat->client->dev,
		 "cw2015/cw2013 driver v1.2 probe sucess\n");
	return 0;
}

#ifdef CONFIG_PM
static int cw_bat_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct cw_battery *cw_bat = i2c_get_clientdata(client);
	read_persistent_clock(&cw_bat->suspend_time_before);
	cancel_delayed_work(&cw_bat->battery_delay_work);
	return 0;
}

static int cw_bat_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct cw_battery *cw_bat = i2c_get_clientdata(client);
	cw_bat->suspend_resume_mark = 1;
	read_persistent_clock(&cw_bat->after);
	cw_bat->after = timespec_sub(cw_bat->after,
				     cw_bat->suspend_time_before);
	queue_delayed_work(cw_bat->battery_workqueue,
			   &cw_bat->battery_delay_work, msecs_to_jiffies(2));
	return 0;
}

static const struct dev_pm_ops cw_bat_pm_ops = {
	.suspend  = cw_bat_suspend,
	.resume   = cw_bat_resume,
};
#endif

static int cw_bat_remove(struct i2c_client *client)
{
	struct cw_battery *cw_bat = i2c_get_clientdata(client);

	dev_dbg(&cw_bat->client->dev, "%s\n", __func__);
	cancel_delayed_work(&cw_bat->battery_delay_work);
	return 0;
}

static const struct i2c_device_id cw_bat_id_table[] = {
	{"cw201x", 0},
	{}
};

static struct i2c_driver cw_bat_driver = {
	.driver = {
		.name = "cw201x",
#ifdef CONFIG_PM
		.pm = &cw_bat_pm_ops,
#endif
	},
	.probe = cw_bat_probe,
	.remove = cw_bat_remove,
	.id_table = cw_bat_id_table,
};

static int __init cw_bat_init(void)
{
	return i2c_add_driver(&cw_bat_driver);
}

static void __exit cw_bat_exit(void)
{
	i2c_del_driver(&cw_bat_driver);
}

module_init(cw_bat_init);
module_exit(cw_bat_exit);

MODULE_AUTHOR("xhc<xhc@rock-chips.com>");
MODULE_DESCRIPTION("cw2015/cw2013 battery driver");
MODULE_LICENSE("GPL");
