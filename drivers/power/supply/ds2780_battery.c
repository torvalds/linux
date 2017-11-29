/*
 * 1-wire client/driver for the Maxim/Dallas DS2780 Stand-Alone Fuel Gauge IC
 *
 * Copyright (C) 2010 Indesign, LLC
 *
 * Author: Clifton Barnes <cabarnes@indesign-llc.com>
 *
 * Based on ds2760_battery and ds2782_battery drivers
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/param.h>
#include <linux/pm.h>
#include <linux/platform_device.h>
#include <linux/power_supply.h>
#include <linux/idr.h>

#include <linux/w1.h>
#include "../../w1/slaves/w1_ds2780.h"

/* Current unit measurement in uA for a 1 milli-ohm sense resistor */
#define DS2780_CURRENT_UNITS	1563
/* Charge unit measurement in uAh for a 1 milli-ohm sense resistor */
#define DS2780_CHARGE_UNITS		6250
/* Number of bytes in user EEPROM space */
#define DS2780_USER_EEPROM_SIZE		(DS2780_EEPROM_BLOCK0_END - \
					DS2780_EEPROM_BLOCK0_START + 1)
/* Number of bytes in parameter EEPROM space */
#define DS2780_PARAM_EEPROM_SIZE	(DS2780_EEPROM_BLOCK1_END - \
					DS2780_EEPROM_BLOCK1_START + 1)

struct ds2780_device_info {
	struct device *dev;
	struct power_supply *bat;
	struct power_supply_desc bat_desc;
	struct device *w1_dev;
};

enum current_types {
	CURRENT_NOW,
	CURRENT_AVG,
};

static const char model[] = "DS2780";
static const char manufacturer[] = "Maxim/Dallas";

static inline struct ds2780_device_info *
to_ds2780_device_info(struct power_supply *psy)
{
	return power_supply_get_drvdata(psy);
}

static inline struct power_supply *to_power_supply(struct device *dev)
{
	return dev_get_drvdata(dev);
}

static inline int ds2780_battery_io(struct ds2780_device_info *dev_info,
	char *buf, int addr, size_t count, int io)
{
	return w1_ds2780_io(dev_info->w1_dev, buf, addr, count, io);
}

static inline int ds2780_read8(struct ds2780_device_info *dev_info, u8 *val,
	int addr)
{
	return ds2780_battery_io(dev_info, val, addr, sizeof(u8), 0);
}

static int ds2780_read16(struct ds2780_device_info *dev_info, s16 *val,
	int addr)
{
	int ret;
	u8 raw[2];

	ret = ds2780_battery_io(dev_info, raw, addr, sizeof(raw), 0);
	if (ret < 0)
		return ret;

	*val = (raw[0] << 8) | raw[1];

	return 0;
}

static inline int ds2780_read_block(struct ds2780_device_info *dev_info,
	u8 *val, int addr, size_t count)
{
	return ds2780_battery_io(dev_info, val, addr, count, 0);
}

static inline int ds2780_write(struct ds2780_device_info *dev_info, u8 *val,
	int addr, size_t count)
{
	return ds2780_battery_io(dev_info, val, addr, count, 1);
}

static inline int ds2780_store_eeprom(struct device *dev, int addr)
{
	return w1_ds2780_eeprom_cmd(dev, addr, W1_DS2780_COPY_DATA);
}

static inline int ds2780_recall_eeprom(struct device *dev, int addr)
{
	return w1_ds2780_eeprom_cmd(dev, addr, W1_DS2780_RECALL_DATA);
}

static int ds2780_save_eeprom(struct ds2780_device_info *dev_info, int reg)
{
	int ret;

	ret = ds2780_store_eeprom(dev_info->w1_dev, reg);
	if (ret < 0)
		return ret;

	ret = ds2780_recall_eeprom(dev_info->w1_dev, reg);
	if (ret < 0)
		return ret;

	return 0;
}

/* Set sense resistor value in mhos */
static int ds2780_set_sense_register(struct ds2780_device_info *dev_info,
	u8 conductance)
{
	int ret;

	ret = ds2780_write(dev_info, &conductance,
				DS2780_RSNSP_REG, sizeof(u8));
	if (ret < 0)
		return ret;

	return ds2780_save_eeprom(dev_info, DS2780_RSNSP_REG);
}

/* Get RSGAIN value from 0 to 1.999 in steps of 0.001 */
static int ds2780_get_rsgain_register(struct ds2780_device_info *dev_info,
	u16 *rsgain)
{
	return ds2780_read16(dev_info, rsgain, DS2780_RSGAIN_MSB_REG);
}

/* Set RSGAIN value from 0 to 1.999 in steps of 0.001 */
static int ds2780_set_rsgain_register(struct ds2780_device_info *dev_info,
	u16 rsgain)
{
	int ret;
	u8 raw[] = {rsgain >> 8, rsgain & 0xFF};

	ret = ds2780_write(dev_info, raw,
				DS2780_RSGAIN_MSB_REG, sizeof(raw));
	if (ret < 0)
		return ret;

	return ds2780_save_eeprom(dev_info, DS2780_RSGAIN_MSB_REG);
}

static int ds2780_get_voltage(struct ds2780_device_info *dev_info,
	int *voltage_uV)
{
	int ret;
	s16 voltage_raw;

	/*
	 * The voltage value is located in 10 bits across the voltage MSB
	 * and LSB registers in two's compliment form
	 * Sign bit of the voltage value is in bit 7 of the voltage MSB register
	 * Bits 9 - 3 of the voltage value are in bits 6 - 0 of the
	 * voltage MSB register
	 * Bits 2 - 0 of the voltage value are in bits 7 - 5 of the
	 * voltage LSB register
	 */
	ret = ds2780_read16(dev_info, &voltage_raw,
				DS2780_VOLT_MSB_REG);
	if (ret < 0)
		return ret;

	/*
	 * DS2780 reports voltage in units of 4.88mV, but the battery class
	 * reports in units of uV, so convert by multiplying by 4880.
	 */
	*voltage_uV = (voltage_raw / 32) * 4880;
	return 0;
}

static int ds2780_get_temperature(struct ds2780_device_info *dev_info,
	int *temperature)
{
	int ret;
	s16 temperature_raw;

	/*
	 * The temperature value is located in 10 bits across the temperature
	 * MSB and LSB registers in two's compliment form
	 * Sign bit of the temperature value is in bit 7 of the temperature
	 * MSB register
	 * Bits 9 - 3 of the temperature value are in bits 6 - 0 of the
	 * temperature MSB register
	 * Bits 2 - 0 of the temperature value are in bits 7 - 5 of the
	 * temperature LSB register
	 */
	ret = ds2780_read16(dev_info, &temperature_raw,
				DS2780_TEMP_MSB_REG);
	if (ret < 0)
		return ret;

	/*
	 * Temperature is measured in units of 0.125 degrees celcius, the
	 * power_supply class measures temperature in tenths of degrees
	 * celsius. The temperature value is stored as a 10 bit number, plus
	 * sign in the upper bits of a 16 bit register.
	 */
	*temperature = ((temperature_raw / 32) * 125) / 100;
	return 0;
}

static int ds2780_get_current(struct ds2780_device_info *dev_info,
	enum current_types type, int *current_uA)
{
	int ret, sense_res;
	s16 current_raw;
	u8 sense_res_raw, reg_msb;

	/*
	 * The units of measurement for current are dependent on the value of
	 * the sense resistor.
	 */
	ret = ds2780_read8(dev_info, &sense_res_raw, DS2780_RSNSP_REG);
	if (ret < 0)
		return ret;

	if (sense_res_raw == 0) {
		dev_err(dev_info->dev, "sense resistor value is 0\n");
		return -EINVAL;
	}
	sense_res = 1000 / sense_res_raw;

	if (type == CURRENT_NOW)
		reg_msb = DS2780_CURRENT_MSB_REG;
	else if (type == CURRENT_AVG)
		reg_msb = DS2780_IAVG_MSB_REG;
	else
		return -EINVAL;

	/*
	 * The current value is located in 16 bits across the current MSB
	 * and LSB registers in two's compliment form
	 * Sign bit of the current value is in bit 7 of the current MSB register
	 * Bits 14 - 8 of the current value are in bits 6 - 0 of the current
	 * MSB register
	 * Bits 7 - 0 of the current value are in bits 7 - 0 of the current
	 * LSB register
	 */
	ret = ds2780_read16(dev_info, &current_raw, reg_msb);
	if (ret < 0)
		return ret;

	*current_uA = current_raw * (DS2780_CURRENT_UNITS / sense_res);
	return 0;
}

static int ds2780_get_accumulated_current(struct ds2780_device_info *dev_info,
	int *accumulated_current)
{
	int ret, sense_res;
	s16 current_raw;
	u8 sense_res_raw;

	/*
	 * The units of measurement for accumulated current are dependent on
	 * the value of the sense resistor.
	 */
	ret = ds2780_read8(dev_info, &sense_res_raw, DS2780_RSNSP_REG);
	if (ret < 0)
		return ret;

	if (sense_res_raw == 0) {
		dev_err(dev_info->dev, "sense resistor value is 0\n");
		return -ENXIO;
	}
	sense_res = 1000 / sense_res_raw;

	/*
	 * The ACR value is located in 16 bits across the ACR MSB and
	 * LSB registers
	 * Bits 15 - 8 of the ACR value are in bits 7 - 0 of the ACR
	 * MSB register
	 * Bits 7 - 0 of the ACR value are in bits 7 - 0 of the ACR
	 * LSB register
	 */
	ret = ds2780_read16(dev_info, &current_raw, DS2780_ACR_MSB_REG);
	if (ret < 0)
		return ret;

	*accumulated_current = current_raw * (DS2780_CHARGE_UNITS / sense_res);
	return 0;
}

static int ds2780_get_capacity(struct ds2780_device_info *dev_info,
	int *capacity)
{
	int ret;
	u8 raw;

	ret = ds2780_read8(dev_info, &raw, DS2780_RARC_REG);
	if (ret < 0)
		return ret;

	*capacity = raw;
	return raw;
}

static int ds2780_get_status(struct ds2780_device_info *dev_info, int *status)
{
	int ret, current_uA, capacity;

	ret = ds2780_get_current(dev_info, CURRENT_NOW, &current_uA);
	if (ret < 0)
		return ret;

	ret = ds2780_get_capacity(dev_info, &capacity);
	if (ret < 0)
		return ret;

	if (capacity == 100)
		*status = POWER_SUPPLY_STATUS_FULL;
	else if (current_uA == 0)
		*status = POWER_SUPPLY_STATUS_NOT_CHARGING;
	else if (current_uA < 0)
		*status = POWER_SUPPLY_STATUS_DISCHARGING;
	else
		*status = POWER_SUPPLY_STATUS_CHARGING;

	return 0;
}

static int ds2780_get_charge_now(struct ds2780_device_info *dev_info,
	int *charge_now)
{
	int ret;
	u16 charge_raw;

	/*
	 * The RAAC value is located in 16 bits across the RAAC MSB and
	 * LSB registers
	 * Bits 15 - 8 of the RAAC value are in bits 7 - 0 of the RAAC
	 * MSB register
	 * Bits 7 - 0 of the RAAC value are in bits 7 - 0 of the RAAC
	 * LSB register
	 */
	ret = ds2780_read16(dev_info, &charge_raw, DS2780_RAAC_MSB_REG);
	if (ret < 0)
		return ret;

	*charge_now = charge_raw * 1600;
	return 0;
}

static int ds2780_get_control_register(struct ds2780_device_info *dev_info,
	u8 *control_reg)
{
	return ds2780_read8(dev_info, control_reg, DS2780_CONTROL_REG);
}

static int ds2780_set_control_register(struct ds2780_device_info *dev_info,
	u8 control_reg)
{
	int ret;

	ret = ds2780_write(dev_info, &control_reg,
				DS2780_CONTROL_REG, sizeof(u8));
	if (ret < 0)
		return ret;

	return ds2780_save_eeprom(dev_info, DS2780_CONTROL_REG);
}

static int ds2780_battery_get_property(struct power_supply *psy,
	enum power_supply_property psp,
	union power_supply_propval *val)
{
	int ret = 0;
	struct ds2780_device_info *dev_info = to_ds2780_device_info(psy);

	switch (psp) {
	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
		ret = ds2780_get_voltage(dev_info, &val->intval);
		break;

	case POWER_SUPPLY_PROP_TEMP:
		ret = ds2780_get_temperature(dev_info, &val->intval);
		break;

	case POWER_SUPPLY_PROP_MODEL_NAME:
		val->strval = model;
		break;

	case POWER_SUPPLY_PROP_MANUFACTURER:
		val->strval = manufacturer;
		break;

	case POWER_SUPPLY_PROP_CURRENT_NOW:
		ret = ds2780_get_current(dev_info, CURRENT_NOW, &val->intval);
		break;

	case POWER_SUPPLY_PROP_CURRENT_AVG:
		ret = ds2780_get_current(dev_info, CURRENT_AVG, &val->intval);
		break;

	case POWER_SUPPLY_PROP_STATUS:
		ret = ds2780_get_status(dev_info, &val->intval);
		break;

	case POWER_SUPPLY_PROP_CAPACITY:
		ret = ds2780_get_capacity(dev_info, &val->intval);
		break;

	case POWER_SUPPLY_PROP_CHARGE_COUNTER:
		ret = ds2780_get_accumulated_current(dev_info, &val->intval);
		break;

	case POWER_SUPPLY_PROP_CHARGE_NOW:
		ret = ds2780_get_charge_now(dev_info, &val->intval);
		break;

	default:
		ret = -EINVAL;
	}

	return ret;
}

static enum power_supply_property ds2780_battery_props[] = {
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_TEMP,
	POWER_SUPPLY_PROP_MODEL_NAME,
	POWER_SUPPLY_PROP_MANUFACTURER,
	POWER_SUPPLY_PROP_CURRENT_NOW,
	POWER_SUPPLY_PROP_CURRENT_AVG,
	POWER_SUPPLY_PROP_CAPACITY,
	POWER_SUPPLY_PROP_CHARGE_COUNTER,
	POWER_SUPPLY_PROP_CHARGE_NOW,
};

static ssize_t ds2780_get_pmod_enabled(struct device *dev,
	struct device_attribute *attr,
	char *buf)
{
	int ret;
	u8 control_reg;
	struct power_supply *psy = to_power_supply(dev);
	struct ds2780_device_info *dev_info = to_ds2780_device_info(psy);

	/* Get power mode */
	ret = ds2780_get_control_register(dev_info, &control_reg);
	if (ret < 0)
		return ret;

	return sprintf(buf, "%d\n",
		 !!(control_reg & DS2780_CONTROL_REG_PMOD));
}

static ssize_t ds2780_set_pmod_enabled(struct device *dev,
	struct device_attribute *attr,
	const char *buf,
	size_t count)
{
	int ret;
	u8 control_reg, new_setting;
	struct power_supply *psy = to_power_supply(dev);
	struct ds2780_device_info *dev_info = to_ds2780_device_info(psy);

	/* Set power mode */
	ret = ds2780_get_control_register(dev_info, &control_reg);
	if (ret < 0)
		return ret;

	ret = kstrtou8(buf, 0, &new_setting);
	if (ret < 0)
		return ret;

	if ((new_setting != 0) && (new_setting != 1)) {
		dev_err(dev_info->dev, "Invalid pmod setting (0 or 1)\n");
		return -EINVAL;
	}

	if (new_setting)
		control_reg |= DS2780_CONTROL_REG_PMOD;
	else
		control_reg &= ~DS2780_CONTROL_REG_PMOD;

	ret = ds2780_set_control_register(dev_info, control_reg);
	if (ret < 0)
		return ret;

	return count;
}

static ssize_t ds2780_get_sense_resistor_value(struct device *dev,
	struct device_attribute *attr,
	char *buf)
{
	int ret;
	u8 sense_resistor;
	struct power_supply *psy = to_power_supply(dev);
	struct ds2780_device_info *dev_info = to_ds2780_device_info(psy);

	ret = ds2780_read8(dev_info, &sense_resistor, DS2780_RSNSP_REG);
	if (ret < 0)
		return ret;

	ret = sprintf(buf, "%d\n", sense_resistor);
	return ret;
}

static ssize_t ds2780_set_sense_resistor_value(struct device *dev,
	struct device_attribute *attr,
	const char *buf,
	size_t count)
{
	int ret;
	u8 new_setting;
	struct power_supply *psy = to_power_supply(dev);
	struct ds2780_device_info *dev_info = to_ds2780_device_info(psy);

	ret = kstrtou8(buf, 0, &new_setting);
	if (ret < 0)
		return ret;

	ret = ds2780_set_sense_register(dev_info, new_setting);
	if (ret < 0)
		return ret;

	return count;
}

static ssize_t ds2780_get_rsgain_setting(struct device *dev,
	struct device_attribute *attr,
	char *buf)
{
	int ret;
	u16 rsgain;
	struct power_supply *psy = to_power_supply(dev);
	struct ds2780_device_info *dev_info = to_ds2780_device_info(psy);

	ret = ds2780_get_rsgain_register(dev_info, &rsgain);
	if (ret < 0)
		return ret;

	return sprintf(buf, "%d\n", rsgain);
}

static ssize_t ds2780_set_rsgain_setting(struct device *dev,
	struct device_attribute *attr,
	const char *buf,
	size_t count)
{
	int ret;
	u16 new_setting;
	struct power_supply *psy = to_power_supply(dev);
	struct ds2780_device_info *dev_info = to_ds2780_device_info(psy);

	ret = kstrtou16(buf, 0, &new_setting);
	if (ret < 0)
		return ret;

	/* Gain can only be from 0 to 1.999 in steps of .001 */
	if (new_setting > 1999) {
		dev_err(dev_info->dev, "Invalid rsgain setting (0 - 1999)\n");
		return -EINVAL;
	}

	ret = ds2780_set_rsgain_register(dev_info, new_setting);
	if (ret < 0)
		return ret;

	return count;
}

static ssize_t ds2780_get_pio_pin(struct device *dev,
	struct device_attribute *attr,
	char *buf)
{
	int ret;
	u8 sfr;
	struct power_supply *psy = to_power_supply(dev);
	struct ds2780_device_info *dev_info = to_ds2780_device_info(psy);

	ret = ds2780_read8(dev_info, &sfr, DS2780_SFR_REG);
	if (ret < 0)
		return ret;

	ret = sprintf(buf, "%d\n", sfr & DS2780_SFR_REG_PIOSC);
	return ret;
}

static ssize_t ds2780_set_pio_pin(struct device *dev,
	struct device_attribute *attr,
	const char *buf,
	size_t count)
{
	int ret;
	u8 new_setting;
	struct power_supply *psy = to_power_supply(dev);
	struct ds2780_device_info *dev_info = to_ds2780_device_info(psy);

	ret = kstrtou8(buf, 0, &new_setting);
	if (ret < 0)
		return ret;

	if ((new_setting != 0) && (new_setting != 1)) {
		dev_err(dev_info->dev, "Invalid pio_pin setting (0 or 1)\n");
		return -EINVAL;
	}

	ret = ds2780_write(dev_info, &new_setting,
				DS2780_SFR_REG, sizeof(u8));
	if (ret < 0)
		return ret;

	return count;
}

static ssize_t ds2780_read_param_eeprom_bin(struct file *filp,
				struct kobject *kobj,
				struct bin_attribute *bin_attr,
				char *buf, loff_t off, size_t count)
{
	struct device *dev = container_of(kobj, struct device, kobj);
	struct power_supply *psy = to_power_supply(dev);
	struct ds2780_device_info *dev_info = to_ds2780_device_info(psy);

	return ds2780_read_block(dev_info, buf,
				DS2780_EEPROM_BLOCK1_START + off, count);
}

static ssize_t ds2780_write_param_eeprom_bin(struct file *filp,
				struct kobject *kobj,
				struct bin_attribute *bin_attr,
				char *buf, loff_t off, size_t count)
{
	struct device *dev = container_of(kobj, struct device, kobj);
	struct power_supply *psy = to_power_supply(dev);
	struct ds2780_device_info *dev_info = to_ds2780_device_info(psy);
	int ret;

	ret = ds2780_write(dev_info, buf,
				DS2780_EEPROM_BLOCK1_START + off, count);
	if (ret < 0)
		return ret;

	ret = ds2780_save_eeprom(dev_info, DS2780_EEPROM_BLOCK1_START);
	if (ret < 0)
		return ret;

	return count;
}

static const struct bin_attribute ds2780_param_eeprom_bin_attr = {
	.attr = {
		.name = "param_eeprom",
		.mode = S_IRUGO | S_IWUSR,
	},
	.size = DS2780_PARAM_EEPROM_SIZE,
	.read = ds2780_read_param_eeprom_bin,
	.write = ds2780_write_param_eeprom_bin,
};

static ssize_t ds2780_read_user_eeprom_bin(struct file *filp,
				struct kobject *kobj,
				struct bin_attribute *bin_attr,
				char *buf, loff_t off, size_t count)
{
	struct device *dev = container_of(kobj, struct device, kobj);
	struct power_supply *psy = to_power_supply(dev);
	struct ds2780_device_info *dev_info = to_ds2780_device_info(psy);

	return ds2780_read_block(dev_info, buf,
				DS2780_EEPROM_BLOCK0_START + off, count);
}

static ssize_t ds2780_write_user_eeprom_bin(struct file *filp,
				struct kobject *kobj,
				struct bin_attribute *bin_attr,
				char *buf, loff_t off, size_t count)
{
	struct device *dev = container_of(kobj, struct device, kobj);
	struct power_supply *psy = to_power_supply(dev);
	struct ds2780_device_info *dev_info = to_ds2780_device_info(psy);
	int ret;

	ret = ds2780_write(dev_info, buf,
				DS2780_EEPROM_BLOCK0_START + off, count);
	if (ret < 0)
		return ret;

	ret = ds2780_save_eeprom(dev_info, DS2780_EEPROM_BLOCK0_START);
	if (ret < 0)
		return ret;

	return count;
}

static const struct bin_attribute ds2780_user_eeprom_bin_attr = {
	.attr = {
		.name = "user_eeprom",
		.mode = S_IRUGO | S_IWUSR,
	},
	.size = DS2780_USER_EEPROM_SIZE,
	.read = ds2780_read_user_eeprom_bin,
	.write = ds2780_write_user_eeprom_bin,
};

static DEVICE_ATTR(pmod_enabled, S_IRUGO | S_IWUSR, ds2780_get_pmod_enabled,
	ds2780_set_pmod_enabled);
static DEVICE_ATTR(sense_resistor_value, S_IRUGO | S_IWUSR,
	ds2780_get_sense_resistor_value, ds2780_set_sense_resistor_value);
static DEVICE_ATTR(rsgain_setting, S_IRUGO | S_IWUSR, ds2780_get_rsgain_setting,
	ds2780_set_rsgain_setting);
static DEVICE_ATTR(pio_pin, S_IRUGO | S_IWUSR, ds2780_get_pio_pin,
	ds2780_set_pio_pin);


static struct attribute *ds2780_attributes[] = {
	&dev_attr_pmod_enabled.attr,
	&dev_attr_sense_resistor_value.attr,
	&dev_attr_rsgain_setting.attr,
	&dev_attr_pio_pin.attr,
	NULL
};

static const struct attribute_group ds2780_attr_group = {
	.attrs = ds2780_attributes,
};

static int ds2780_battery_probe(struct platform_device *pdev)
{
	struct power_supply_config psy_cfg = {};
	int ret = 0;
	struct ds2780_device_info *dev_info;

	dev_info = devm_kzalloc(&pdev->dev, sizeof(*dev_info), GFP_KERNEL);
	if (!dev_info) {
		ret = -ENOMEM;
		goto fail;
	}

	platform_set_drvdata(pdev, dev_info);

	dev_info->dev			= &pdev->dev;
	dev_info->w1_dev		= pdev->dev.parent;
	dev_info->bat_desc.name		= dev_name(&pdev->dev);
	dev_info->bat_desc.type		= POWER_SUPPLY_TYPE_BATTERY;
	dev_info->bat_desc.properties	= ds2780_battery_props;
	dev_info->bat_desc.num_properties = ARRAY_SIZE(ds2780_battery_props);
	dev_info->bat_desc.get_property	= ds2780_battery_get_property;

	psy_cfg.drv_data		= dev_info;

	dev_info->bat = power_supply_register(&pdev->dev, &dev_info->bat_desc,
					      &psy_cfg);
	if (IS_ERR(dev_info->bat)) {
		dev_err(dev_info->dev, "failed to register battery\n");
		ret = PTR_ERR(dev_info->bat);
		goto fail;
	}

	ret = sysfs_create_group(&dev_info->bat->dev.kobj, &ds2780_attr_group);
	if (ret) {
		dev_err(dev_info->dev, "failed to create sysfs group\n");
		goto fail_unregister;
	}

	ret = sysfs_create_bin_file(&dev_info->bat->dev.kobj,
					&ds2780_param_eeprom_bin_attr);
	if (ret) {
		dev_err(dev_info->dev,
				"failed to create param eeprom bin file");
		goto fail_remove_group;
	}

	ret = sysfs_create_bin_file(&dev_info->bat->dev.kobj,
					&ds2780_user_eeprom_bin_attr);
	if (ret) {
		dev_err(dev_info->dev,
				"failed to create user eeprom bin file");
		goto fail_remove_bin_file;
	}

	return 0;

fail_remove_bin_file:
	sysfs_remove_bin_file(&dev_info->bat->dev.kobj,
				&ds2780_param_eeprom_bin_attr);
fail_remove_group:
	sysfs_remove_group(&dev_info->bat->dev.kobj, &ds2780_attr_group);
fail_unregister:
	power_supply_unregister(dev_info->bat);
fail:
	return ret;
}

static int ds2780_battery_remove(struct platform_device *pdev)
{
	struct ds2780_device_info *dev_info = platform_get_drvdata(pdev);

	/*
	 * Remove attributes before unregistering power supply
	 * because 'bat' will be freed on power_supply_unregister() call.
	 */
	sysfs_remove_group(&dev_info->bat->dev.kobj, &ds2780_attr_group);

	power_supply_unregister(dev_info->bat);

	return 0;
}

static struct platform_driver ds2780_battery_driver = {
	.driver = {
		.name = "ds2780-battery",
	},
	.probe	  = ds2780_battery_probe,
	.remove   = ds2780_battery_remove,
};

module_platform_driver(ds2780_battery_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Clifton Barnes <cabarnes@indesign-llc.com>");
MODULE_DESCRIPTION("Maxim/Dallas DS2780 Stand-Alone Fuel Gauage IC driver");
MODULE_ALIAS("platform:ds2780-battery");
