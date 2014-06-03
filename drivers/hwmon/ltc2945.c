/*
 * Driver for Linear Technology LTC2945 I2C Power Monitor
 *
 * Copyright (c) 2014 Guenter Roeck
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
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/err.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/hwmon.h>
#include <linux/hwmon-sysfs.h>
#include <linux/jiffies.h>
#include <linux/regmap.h>

/* chip registers */
#define LTC2945_CONTROL			0x00
#define LTC2945_ALERT			0x01
#define LTC2945_STATUS			0x02
#define LTC2945_FAULT			0x03
#define LTC2945_POWER_H			0x05
#define LTC2945_MAX_POWER_H		0x08
#define LTC2945_MIN_POWER_H		0x0b
#define LTC2945_MAX_POWER_THRES_H	0x0e
#define LTC2945_MIN_POWER_THRES_H	0x11
#define LTC2945_SENSE_H			0x14
#define LTC2945_MAX_SENSE_H		0x16
#define LTC2945_MIN_SENSE_H		0x18
#define LTC2945_MAX_SENSE_THRES_H	0x1a
#define LTC2945_MIN_SENSE_THRES_H	0x1c
#define LTC2945_VIN_H			0x1e
#define LTC2945_MAX_VIN_H		0x20
#define LTC2945_MIN_VIN_H		0x22
#define LTC2945_MAX_VIN_THRES_H		0x24
#define LTC2945_MIN_VIN_THRES_H		0x26
#define LTC2945_ADIN_H			0x28
#define LTC2945_MAX_ADIN_H		0x2a
#define LTC2945_MIN_ADIN_H		0x2c
#define LTC2945_MAX_ADIN_THRES_H	0x2e
#define LTC2945_MIN_ADIN_THRES_H	0x30
#define LTC2945_MIN_ADIN_THRES_L	0x31

/* Fault register bits */

#define FAULT_ADIN_UV		(1 << 0)
#define FAULT_ADIN_OV		(1 << 1)
#define FAULT_VIN_UV		(1 << 2)
#define FAULT_VIN_OV		(1 << 3)
#define FAULT_SENSE_UV		(1 << 4)
#define FAULT_SENSE_OV		(1 << 5)
#define FAULT_POWER_UV		(1 << 6)
#define FAULT_POWER_OV		(1 << 7)

/* Control register bits */

#define CONTROL_MULT_SELECT	(1 << 0)
#define CONTROL_TEST_MODE	(1 << 4)

static inline bool is_power_reg(u8 reg)
{
	return reg < LTC2945_SENSE_H;
}

/* Return the value from the given register in uW, mV, or mA */
static long long ltc2945_reg_to_val(struct device *dev, u8 reg)
{
	struct regmap *regmap = dev_get_drvdata(dev);
	unsigned int control;
	u8 buf[3];
	long long val;
	int ret;

	ret = regmap_bulk_read(regmap, reg, buf,
			       is_power_reg(reg) ? 3 : 2);
	if (ret < 0)
		return ret;

	if (is_power_reg(reg)) {
		/* power */
		val = (buf[0] << 16) + (buf[1] << 8) + buf[2];
	} else {
		/* current, voltage */
		val = (buf[0] << 4) + (buf[1] >> 4);
	}

	switch (reg) {
	case LTC2945_POWER_H:
	case LTC2945_MAX_POWER_H:
	case LTC2945_MIN_POWER_H:
	case LTC2945_MAX_POWER_THRES_H:
	case LTC2945_MIN_POWER_THRES_H:
		/*
		 * Convert to uW by assuming current is measured with
		 * an 1mOhm sense resistor, similar to current
		 * measurements.
		 * Control register bit 0 selects if voltage at SENSE+/VDD
		 * or voltage at ADIN is used to measure power.
		 */
		ret = regmap_read(regmap, LTC2945_CONTROL, &control);
		if (ret < 0)
			return ret;
		if (control & CONTROL_MULT_SELECT) {
			/* 25 mV * 25 uV = 0.625 uV resolution. */
			val *= 625LL;
		} else {
			/* 0.5 mV * 25 uV = 0.0125 uV resolution. */
			val = (val * 25LL) >> 1;
		}
		break;
	case LTC2945_VIN_H:
	case LTC2945_MAX_VIN_H:
	case LTC2945_MIN_VIN_H:
	case LTC2945_MAX_VIN_THRES_H:
	case LTC2945_MIN_VIN_THRES_H:
		/* 25 mV resolution. Convert to mV. */
		val *= 25;
		break;
	case LTC2945_ADIN_H:
	case LTC2945_MAX_ADIN_H:
	case LTC2945_MIN_ADIN_THRES_H:
	case LTC2945_MAX_ADIN_THRES_H:
	case LTC2945_MIN_ADIN_H:
		/* 0.5mV resolution. Convert to mV. */
		val = val >> 1;
		break;
	case LTC2945_SENSE_H:
	case LTC2945_MAX_SENSE_H:
	case LTC2945_MIN_SENSE_H:
	case LTC2945_MAX_SENSE_THRES_H:
	case LTC2945_MIN_SENSE_THRES_H:
		/*
		 * 25 uV resolution. Convert to current as measured with
		 * an 1 mOhm sense resistor, in mA. If a different sense
		 * resistor is installed, calculate the actual current by
		 * dividing the reported current by the sense resistor value
		 * in mOhm.
		 */
		val *= 25;
		break;
	default:
		return -EINVAL;
	}
	return val;
}

static int ltc2945_val_to_reg(struct device *dev, u8 reg,
			      unsigned long val)
{
	struct regmap *regmap = dev_get_drvdata(dev);
	unsigned int control;
	int ret;

	switch (reg) {
	case LTC2945_POWER_H:
	case LTC2945_MAX_POWER_H:
	case LTC2945_MIN_POWER_H:
	case LTC2945_MAX_POWER_THRES_H:
	case LTC2945_MIN_POWER_THRES_H:
		/*
		 * Convert to register value by assuming current is measured
		 * with an 1mOhm sense resistor, similar to current
		 * measurements.
		 * Control register bit 0 selects if voltage at SENSE+/VDD
		 * or voltage at ADIN is used to measure power, which in turn
		 * determines register calculations.
		 */
		ret = regmap_read(regmap, LTC2945_CONTROL, &control);
		if (ret < 0)
			return ret;
		if (control & CONTROL_MULT_SELECT) {
			/* 25 mV * 25 uV = 0.625 uV resolution. */
			val = DIV_ROUND_CLOSEST(val, 625);
		} else {
			/*
			 * 0.5 mV * 25 uV = 0.0125 uV resolution.
			 * Divide first to avoid overflow;
			 * accept loss of accuracy.
			 */
			val = DIV_ROUND_CLOSEST(val, 25) * 2;
		}
		break;
	case LTC2945_VIN_H:
	case LTC2945_MAX_VIN_H:
	case LTC2945_MIN_VIN_H:
	case LTC2945_MAX_VIN_THRES_H:
	case LTC2945_MIN_VIN_THRES_H:
		/* 25 mV resolution. */
		val /= 25;
		break;
	case LTC2945_ADIN_H:
	case LTC2945_MAX_ADIN_H:
	case LTC2945_MIN_ADIN_THRES_H:
	case LTC2945_MAX_ADIN_THRES_H:
	case LTC2945_MIN_ADIN_H:
		/* 0.5mV resolution. */
		val *= 2;
		break;
	case LTC2945_SENSE_H:
	case LTC2945_MAX_SENSE_H:
	case LTC2945_MIN_SENSE_H:
	case LTC2945_MAX_SENSE_THRES_H:
	case LTC2945_MIN_SENSE_THRES_H:
		/*
		 * 25 uV resolution. Convert to current as measured with
		 * an 1 mOhm sense resistor, in mA. If a different sense
		 * resistor is installed, calculate the actual current by
		 * dividing the reported current by the sense resistor value
		 * in mOhm.
		 */
		val = DIV_ROUND_CLOSEST(val, 25);
		break;
	default:
		return -EINVAL;
	}
	return val;
}

static ssize_t ltc2945_show_value(struct device *dev,
				  struct device_attribute *da, char *buf)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(da);
	long long value;

	value = ltc2945_reg_to_val(dev, attr->index);
	if (value < 0)
		return value;
	return snprintf(buf, PAGE_SIZE, "%lld\n", value);
}

static ssize_t ltc2945_set_value(struct device *dev,
				     struct device_attribute *da,
				     const char *buf, size_t count)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(da);
	struct regmap *regmap = dev_get_drvdata(dev);
	u8 reg = attr->index;
	unsigned long val;
	u8 regbuf[3];
	int num_regs;
	int regval;
	int ret;

	ret = kstrtoul(buf, 10, &val);
	if (ret)
		return ret;

	/* convert to register value, then clamp and write result */
	regval = ltc2945_val_to_reg(dev, reg, val);
	if (is_power_reg(reg)) {
		regval = clamp_val(regval, 0, 0xffffff);
		regbuf[0] = regval >> 16;
		regbuf[1] = (regval >> 8) & 0xff;
		regbuf[2] = regval;
		num_regs = 3;
	} else {
		regval = clamp_val(regval, 0, 0xfff) << 4;
		regbuf[0] = regval >> 8;
		regbuf[1] = regval & 0xff;
		num_regs = 2;
	}
	ret = regmap_bulk_write(regmap, reg, regbuf, num_regs);
	return ret < 0 ? ret : count;
}

static ssize_t ltc2945_reset_history(struct device *dev,
				     struct device_attribute *da,
				     const char *buf, size_t count)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(da);
	struct regmap *regmap = dev_get_drvdata(dev);
	u8 reg = attr->index;
	int num_regs = is_power_reg(reg) ? 3 : 2;
	u8 buf_min[3] = { 0xff, 0xff, 0xff };
	u8 buf_max[3] = { 0, 0, 0 };
	unsigned long val;
	int ret;

	ret = kstrtoul(buf, 10, &val);
	if (ret)
		return ret;
	if (val != 1)
		return -EINVAL;

	ret = regmap_update_bits(regmap, LTC2945_CONTROL, CONTROL_TEST_MODE,
				 CONTROL_TEST_MODE);

	/* Reset minimum */
	ret = regmap_bulk_write(regmap, reg, buf_min, num_regs);
	if (ret)
		return ret;

	switch (reg) {
	case LTC2945_MIN_POWER_H:
		reg = LTC2945_MAX_POWER_H;
		break;
	case LTC2945_MIN_SENSE_H:
		reg = LTC2945_MAX_SENSE_H;
		break;
	case LTC2945_MIN_VIN_H:
		reg = LTC2945_MAX_VIN_H;
		break;
	case LTC2945_MIN_ADIN_H:
		reg = LTC2945_MAX_ADIN_H;
		break;
	default:
		WARN_ONCE(1, "Bad register: 0x%x\n", reg);
		return -EINVAL;
	}
	/* Reset maximum */
	ret = regmap_bulk_write(regmap, reg, buf_max, num_regs);

	/* Try resetting test mode even if there was an error */
	regmap_update_bits(regmap, LTC2945_CONTROL, CONTROL_TEST_MODE, 0);

	return ret ? : count;
}

static ssize_t ltc2945_show_bool(struct device *dev,
				 struct device_attribute *da, char *buf)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(da);
	struct regmap *regmap = dev_get_drvdata(dev);
	unsigned int fault;
	int ret;

	ret = regmap_read(regmap, LTC2945_FAULT, &fault);
	if (ret < 0)
		return ret;

	fault &= attr->index;
	if (fault)		/* Clear reported faults in chip register */
		regmap_update_bits(regmap, LTC2945_FAULT, attr->index, 0);

	return snprintf(buf, PAGE_SIZE, "%d\n", !!fault);
}

/* Input voltages */

static SENSOR_DEVICE_ATTR(in1_input, S_IRUGO, ltc2945_show_value, NULL,
			  LTC2945_VIN_H);
static SENSOR_DEVICE_ATTR(in1_min, S_IRUGO | S_IWUSR, ltc2945_show_value,
			  ltc2945_set_value, LTC2945_MIN_VIN_THRES_H);
static SENSOR_DEVICE_ATTR(in1_max, S_IRUGO | S_IWUSR, ltc2945_show_value,
			  ltc2945_set_value, LTC2945_MAX_VIN_THRES_H);
static SENSOR_DEVICE_ATTR(in1_lowest, S_IRUGO, ltc2945_show_value, NULL,
			  LTC2945_MIN_VIN_H);
static SENSOR_DEVICE_ATTR(in1_highest, S_IRUGO, ltc2945_show_value, NULL,
			  LTC2945_MAX_VIN_H);
static SENSOR_DEVICE_ATTR(in1_reset_history, S_IWUSR, NULL,
			  ltc2945_reset_history, LTC2945_MIN_VIN_H);

static SENSOR_DEVICE_ATTR(in2_input, S_IRUGO, ltc2945_show_value, NULL,
			  LTC2945_ADIN_H);
static SENSOR_DEVICE_ATTR(in2_min, S_IRUGO | S_IWUSR, ltc2945_show_value,
			  ltc2945_set_value, LTC2945_MIN_ADIN_THRES_H);
static SENSOR_DEVICE_ATTR(in2_max, S_IRUGO | S_IWUSR, ltc2945_show_value,
			  ltc2945_set_value, LTC2945_MAX_ADIN_THRES_H);
static SENSOR_DEVICE_ATTR(in2_lowest, S_IRUGO, ltc2945_show_value, NULL,
			  LTC2945_MIN_ADIN_H);
static SENSOR_DEVICE_ATTR(in2_highest, S_IRUGO, ltc2945_show_value, NULL,
			  LTC2945_MAX_ADIN_H);
static SENSOR_DEVICE_ATTR(in2_reset_history, S_IWUSR, NULL,
			  ltc2945_reset_history, LTC2945_MIN_ADIN_H);

/* Voltage alarms */

static SENSOR_DEVICE_ATTR(in1_min_alarm, S_IRUGO, ltc2945_show_bool, NULL,
			  FAULT_VIN_UV);
static SENSOR_DEVICE_ATTR(in1_max_alarm, S_IRUGO, ltc2945_show_bool, NULL,
			  FAULT_VIN_OV);
static SENSOR_DEVICE_ATTR(in2_min_alarm, S_IRUGO, ltc2945_show_bool, NULL,
			  FAULT_ADIN_UV);
static SENSOR_DEVICE_ATTR(in2_max_alarm, S_IRUGO, ltc2945_show_bool, NULL,
			  FAULT_ADIN_OV);

/* Currents (via sense resistor) */

static SENSOR_DEVICE_ATTR(curr1_input, S_IRUGO, ltc2945_show_value, NULL,
			  LTC2945_SENSE_H);
static SENSOR_DEVICE_ATTR(curr1_min, S_IRUGO | S_IWUSR, ltc2945_show_value,
			  ltc2945_set_value, LTC2945_MIN_SENSE_THRES_H);
static SENSOR_DEVICE_ATTR(curr1_max, S_IRUGO | S_IWUSR, ltc2945_show_value,
			  ltc2945_set_value, LTC2945_MAX_SENSE_THRES_H);
static SENSOR_DEVICE_ATTR(curr1_lowest, S_IRUGO, ltc2945_show_value, NULL,
			  LTC2945_MIN_SENSE_H);
static SENSOR_DEVICE_ATTR(curr1_highest, S_IRUGO, ltc2945_show_value, NULL,
			  LTC2945_MAX_SENSE_H);
static SENSOR_DEVICE_ATTR(curr1_reset_history, S_IWUSR, NULL,
			  ltc2945_reset_history, LTC2945_MIN_SENSE_H);

/* Current alarms */

static SENSOR_DEVICE_ATTR(curr1_min_alarm, S_IRUGO, ltc2945_show_bool, NULL,
			  FAULT_SENSE_UV);
static SENSOR_DEVICE_ATTR(curr1_max_alarm, S_IRUGO, ltc2945_show_bool, NULL,
			  FAULT_SENSE_OV);

/* Power */

static SENSOR_DEVICE_ATTR(power1_input, S_IRUGO, ltc2945_show_value, NULL,
			  LTC2945_POWER_H);
static SENSOR_DEVICE_ATTR(power1_min, S_IRUGO | S_IWUSR, ltc2945_show_value,
			  ltc2945_set_value, LTC2945_MIN_POWER_THRES_H);
static SENSOR_DEVICE_ATTR(power1_max, S_IRUGO | S_IWUSR, ltc2945_show_value,
			  ltc2945_set_value, LTC2945_MAX_POWER_THRES_H);
static SENSOR_DEVICE_ATTR(power1_input_lowest, S_IRUGO, ltc2945_show_value,
			  NULL, LTC2945_MIN_POWER_H);
static SENSOR_DEVICE_ATTR(power1_input_highest, S_IRUGO, ltc2945_show_value,
			  NULL, LTC2945_MAX_POWER_H);
static SENSOR_DEVICE_ATTR(power1_reset_history, S_IWUSR, NULL,
			  ltc2945_reset_history, LTC2945_MIN_POWER_H);

/* Power alarms */

static SENSOR_DEVICE_ATTR(power1_min_alarm, S_IRUGO, ltc2945_show_bool, NULL,
			  FAULT_POWER_UV);
static SENSOR_DEVICE_ATTR(power1_max_alarm, S_IRUGO, ltc2945_show_bool, NULL,
			  FAULT_POWER_OV);

static struct attribute *ltc2945_attrs[] = {
	&sensor_dev_attr_in1_input.dev_attr.attr,
	&sensor_dev_attr_in1_min.dev_attr.attr,
	&sensor_dev_attr_in1_max.dev_attr.attr,
	&sensor_dev_attr_in1_lowest.dev_attr.attr,
	&sensor_dev_attr_in1_highest.dev_attr.attr,
	&sensor_dev_attr_in1_reset_history.dev_attr.attr,
	&sensor_dev_attr_in1_min_alarm.dev_attr.attr,
	&sensor_dev_attr_in1_max_alarm.dev_attr.attr,

	&sensor_dev_attr_in2_input.dev_attr.attr,
	&sensor_dev_attr_in2_min.dev_attr.attr,
	&sensor_dev_attr_in2_max.dev_attr.attr,
	&sensor_dev_attr_in2_lowest.dev_attr.attr,
	&sensor_dev_attr_in2_highest.dev_attr.attr,
	&sensor_dev_attr_in2_reset_history.dev_attr.attr,
	&sensor_dev_attr_in2_min_alarm.dev_attr.attr,
	&sensor_dev_attr_in2_max_alarm.dev_attr.attr,

	&sensor_dev_attr_curr1_input.dev_attr.attr,
	&sensor_dev_attr_curr1_min.dev_attr.attr,
	&sensor_dev_attr_curr1_max.dev_attr.attr,
	&sensor_dev_attr_curr1_lowest.dev_attr.attr,
	&sensor_dev_attr_curr1_highest.dev_attr.attr,
	&sensor_dev_attr_curr1_reset_history.dev_attr.attr,
	&sensor_dev_attr_curr1_min_alarm.dev_attr.attr,
	&sensor_dev_attr_curr1_max_alarm.dev_attr.attr,

	&sensor_dev_attr_power1_input.dev_attr.attr,
	&sensor_dev_attr_power1_min.dev_attr.attr,
	&sensor_dev_attr_power1_max.dev_attr.attr,
	&sensor_dev_attr_power1_input_lowest.dev_attr.attr,
	&sensor_dev_attr_power1_input_highest.dev_attr.attr,
	&sensor_dev_attr_power1_reset_history.dev_attr.attr,
	&sensor_dev_attr_power1_min_alarm.dev_attr.attr,
	&sensor_dev_attr_power1_max_alarm.dev_attr.attr,

	NULL,
};
ATTRIBUTE_GROUPS(ltc2945);

static struct regmap_config ltc2945_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = LTC2945_MIN_ADIN_THRES_L,
};

static int ltc2945_probe(struct i2c_client *client,
			 const struct i2c_device_id *id)
{
	struct device *dev = &client->dev;
	struct device *hwmon_dev;
	struct regmap *regmap;

	regmap = devm_regmap_init_i2c(client, &ltc2945_regmap_config);
	if (IS_ERR(regmap)) {
		dev_err(dev, "failed to allocate register map\n");
		return PTR_ERR(regmap);
	}

	/* Clear faults */
	regmap_write(regmap, LTC2945_FAULT, 0x00);

	hwmon_dev = devm_hwmon_device_register_with_groups(dev, client->name,
							   regmap,
							   ltc2945_groups);
	return PTR_ERR_OR_ZERO(hwmon_dev);
}

static const struct i2c_device_id ltc2945_id[] = {
	{"ltc2945", 0},
	{ }
};

MODULE_DEVICE_TABLE(i2c, ltc2945_id);

static struct i2c_driver ltc2945_driver = {
	.driver = {
		   .name = "ltc2945",
		   },
	.probe = ltc2945_probe,
	.id_table = ltc2945_id,
};

module_i2c_driver(ltc2945_driver);

MODULE_AUTHOR("Guenter Roeck <linux@roeck-us.net>");
MODULE_DESCRIPTION("LTC2945 driver");
MODULE_LICENSE("GPL");
