// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Driver for Linear Technology LTC2945 I2C Power Monitor
 *
 * Copyright (c) 2014 Guenter Roeck
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

static const struct of_device_id __maybe_unused ltc2945_of_match[] = {
	{ .compatible = "adi,ltc2945" },
	{ }
};
MODULE_DEVICE_TABLE(of, ltc2945_of_match);

/**
 * struct ltc2945_data - LTC2945 device data
 * @regmap: regmap device
 * @shunt_resistor: shunt resistor value in micro ohms (1000 by default)
 */
struct ltc2945_data {
	struct regmap *regmap;
	u32 shunt_resistor;
};

static inline bool is_power_reg(u8 reg)
{
	return reg < LTC2945_SENSE_H;
}

/* Return the value from the given register in uW, mV, or mA */
static long long ltc2945_reg_to_val(struct device *dev, u8 reg)
{
	struct ltc2945_data *data = dev_get_drvdata(dev);
	struct regmap *regmap = data->regmap;
	u32 shunt_resistor = data->shunt_resistor;
	unsigned int control;
	u8 buf[3];
	long long val;
	int ret;

	ret = regmap_bulk_read(regmap, reg, buf,
			       is_power_reg(reg) ? 3 : 2);
	if (ret < 0)
		return ret;

	if (is_power_reg(reg)) {
		/* 24-bit power */
		val = (buf[0] << 16) + (buf[1] << 8) + buf[2];
	} else {
		/* 12-bit current, voltage */
		val = (buf[0] << 4) + (buf[1] >> 4);
	}

	switch (reg) {
	case LTC2945_POWER_H:
	case LTC2945_MAX_POWER_H:
	case LTC2945_MIN_POWER_H:
	case LTC2945_MAX_POWER_THRES_H:
	case LTC2945_MIN_POWER_THRES_H:
		/*
		 * Convert to uW
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
		val *= 1000;
		/* Overflow check: Assuming max 24-bit power, val is at most 53 bits right now. */
		val = DIV_ROUND_CLOSEST_ULL(val, shunt_resistor);
		/*
		 * Overflow check: After division, depending on shunt resistor,
		 * val can still be > 32 bits so returning long long makes sense
		 */

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
		/* 25 uV resolution. Convert to mA. */
		val *= 25 * 1000;
		/* Overflow check: Assuming max 12-bit sense, val is at most 27 bits right now */
		val = DIV_ROUND_CLOSEST_ULL(val, shunt_resistor);
		/* Overflow check: After division, <= 27 bits */
		break;
	default:
		return -EINVAL;
	}
	return val;
}

static long long ltc2945_val_to_reg(struct device *dev, u8 reg,
				    unsigned long long val)
{
	struct ltc2945_data *data = dev_get_drvdata(dev);
	struct regmap *regmap = data->regmap;
	u32 shunt_resistor = data->shunt_resistor;
	unsigned int control;
	int ret;

	/* Ensure we don't overflow */
	val = clamp_val(val, 0, U32_MAX);

	switch (reg) {
	case LTC2945_POWER_H:
	case LTC2945_MAX_POWER_H:
	case LTC2945_MIN_POWER_H:
	case LTC2945_MAX_POWER_THRES_H:
	case LTC2945_MIN_POWER_THRES_H:
		/*
		 * Control register bit 0 selects if voltage at SENSE+/VDD
		 * or voltage at ADIN is used to measure power, which in turn
		 * determines register calculations.
		 */
		ret = regmap_read(regmap, LTC2945_CONTROL, &control);
		if (ret < 0)
			return ret;
		if (control & CONTROL_MULT_SELECT) {
			/* 25 mV * 25 uV = 0.625 uV resolution. */
			val *= shunt_resistor;
			/* Overflow check: Assuming 32-bit val and shunt resistor, val <= 64bits */
			val = DIV_ROUND_CLOSEST_ULL(val, 625 * 1000);
			/* Overflow check: val is now <= 44 bits */
		} else {
			/* 0.5 mV * 25 uV = 0.0125 uV resolution. */
			val *= shunt_resistor;
			/* Overflow check: Assuming 32-bit val and shunt resistor, val <= 64bits */
			val = DIV_ROUND_CLOSEST_ULL(val, 25 * 1000) * 2;
			/* Overflow check: val is now <= 51 bits */
		}
		break;
	case LTC2945_VIN_H:
	case LTC2945_MAX_VIN_H:
	case LTC2945_MIN_VIN_H:
	case LTC2945_MAX_VIN_THRES_H:
	case LTC2945_MIN_VIN_THRES_H:
		/* 25 mV resolution. */
		val = DIV_ROUND_CLOSEST_ULL(val, 25);
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
		/* 25 uV resolution. Convert to  mA. */
		val *= shunt_resistor;
		/* Overflow check: Assuming 32-bit val and 32-bit shunt resistor, val is 64bits */
		val = DIV_ROUND_CLOSEST_ULL(val, 25 * 1000);
		/* Overflow check: val is now <= 50 bits */
		break;
	default:
		return -EINVAL;
	}
	return val;
}

static ssize_t ltc2945_value_show(struct device *dev,
				  struct device_attribute *da, char *buf)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(da);
	long long value;

	value = ltc2945_reg_to_val(dev, attr->index);
	if (value < 0)
		return value;
	return sysfs_emit(buf, "%lld\n", value);
}

static ssize_t ltc2945_value_store(struct device *dev,
				   struct device_attribute *da,
				   const char *buf, size_t count)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(da);
	struct ltc2945_data *data = dev_get_drvdata(dev);
	struct regmap *regmap = data->regmap;
	u8 reg = attr->index;
	unsigned int val;
	u8 regbuf[3];
	int num_regs;
	long long regval;
	int ret;

	ret = kstrtouint(buf, 10, &val);
	if (ret)
		return ret;

	/* convert to register value, then clamp and write result */
	regval = ltc2945_val_to_reg(dev, reg, val);
	if (regval < 0)
		return regval;
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

static ssize_t ltc2945_history_store(struct device *dev,
				     struct device_attribute *da,
				     const char *buf, size_t count)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(da);
	struct ltc2945_data *data = dev_get_drvdata(dev);
	struct regmap *regmap = data->regmap;
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

static ssize_t ltc2945_bool_show(struct device *dev,
				 struct device_attribute *da, char *buf)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(da);
	struct ltc2945_data *data = dev_get_drvdata(dev);
	struct regmap *regmap = data->regmap;
	unsigned int fault;
	int ret;

	ret = regmap_read(regmap, LTC2945_FAULT, &fault);
	if (ret < 0)
		return ret;

	fault &= attr->index;
	if (fault)		/* Clear reported faults in chip register */
		regmap_update_bits(regmap, LTC2945_FAULT, attr->index, 0);

	return sysfs_emit(buf, "%d\n", !!fault);
}

/* Input voltages */

static SENSOR_DEVICE_ATTR_RO(in1_input, ltc2945_value, LTC2945_VIN_H);
static SENSOR_DEVICE_ATTR_RW(in1_min, ltc2945_value, LTC2945_MIN_VIN_THRES_H);
static SENSOR_DEVICE_ATTR_RW(in1_max, ltc2945_value, LTC2945_MAX_VIN_THRES_H);
static SENSOR_DEVICE_ATTR_RO(in1_lowest, ltc2945_value, LTC2945_MIN_VIN_H);
static SENSOR_DEVICE_ATTR_RO(in1_highest, ltc2945_value, LTC2945_MAX_VIN_H);
static SENSOR_DEVICE_ATTR_WO(in1_reset_history, ltc2945_history,
			     LTC2945_MIN_VIN_H);

static SENSOR_DEVICE_ATTR_RO(in2_input, ltc2945_value, LTC2945_ADIN_H);
static SENSOR_DEVICE_ATTR_RW(in2_min, ltc2945_value, LTC2945_MIN_ADIN_THRES_H);
static SENSOR_DEVICE_ATTR_RW(in2_max, ltc2945_value, LTC2945_MAX_ADIN_THRES_H);
static SENSOR_DEVICE_ATTR_RO(in2_lowest, ltc2945_value, LTC2945_MIN_ADIN_H);
static SENSOR_DEVICE_ATTR_RO(in2_highest, ltc2945_value, LTC2945_MAX_ADIN_H);
static SENSOR_DEVICE_ATTR_WO(in2_reset_history, ltc2945_history,
			     LTC2945_MIN_ADIN_H);

/* Voltage alarms */

static SENSOR_DEVICE_ATTR_RO(in1_min_alarm, ltc2945_bool, FAULT_VIN_UV);
static SENSOR_DEVICE_ATTR_RO(in1_max_alarm, ltc2945_bool, FAULT_VIN_OV);
static SENSOR_DEVICE_ATTR_RO(in2_min_alarm, ltc2945_bool, FAULT_ADIN_UV);
static SENSOR_DEVICE_ATTR_RO(in2_max_alarm, ltc2945_bool, FAULT_ADIN_OV);

/* Currents (via sense resistor) */

static SENSOR_DEVICE_ATTR_RO(curr1_input, ltc2945_value, LTC2945_SENSE_H);
static SENSOR_DEVICE_ATTR_RW(curr1_min, ltc2945_value,
			     LTC2945_MIN_SENSE_THRES_H);
static SENSOR_DEVICE_ATTR_RW(curr1_max, ltc2945_value,
			     LTC2945_MAX_SENSE_THRES_H);
static SENSOR_DEVICE_ATTR_RO(curr1_lowest, ltc2945_value, LTC2945_MIN_SENSE_H);
static SENSOR_DEVICE_ATTR_RO(curr1_highest, ltc2945_value,
			     LTC2945_MAX_SENSE_H);
static SENSOR_DEVICE_ATTR_WO(curr1_reset_history, ltc2945_history,
			     LTC2945_MIN_SENSE_H);

/* Current alarms */

static SENSOR_DEVICE_ATTR_RO(curr1_min_alarm, ltc2945_bool, FAULT_SENSE_UV);
static SENSOR_DEVICE_ATTR_RO(curr1_max_alarm, ltc2945_bool, FAULT_SENSE_OV);

/* Power */

static SENSOR_DEVICE_ATTR_RO(power1_input, ltc2945_value, LTC2945_POWER_H);
static SENSOR_DEVICE_ATTR_RW(power1_min, ltc2945_value,
			     LTC2945_MIN_POWER_THRES_H);
static SENSOR_DEVICE_ATTR_RW(power1_max, ltc2945_value,
			     LTC2945_MAX_POWER_THRES_H);
static SENSOR_DEVICE_ATTR_RO(power1_input_lowest, ltc2945_value,
			     LTC2945_MIN_POWER_H);
static SENSOR_DEVICE_ATTR_RO(power1_input_highest, ltc2945_value,
			     LTC2945_MAX_POWER_H);
static SENSOR_DEVICE_ATTR_WO(power1_reset_history, ltc2945_history,
			     LTC2945_MIN_POWER_H);

/* Power alarms */

static SENSOR_DEVICE_ATTR_RO(power1_min_alarm, ltc2945_bool, FAULT_POWER_UV);
static SENSOR_DEVICE_ATTR_RO(power1_max_alarm, ltc2945_bool, FAULT_POWER_OV);

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

static const struct regmap_config ltc2945_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = LTC2945_MIN_ADIN_THRES_L,
};

static int ltc2945_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct device *hwmon_dev;
	struct regmap *regmap;
	struct ltc2945_data *data;

	data = devm_kzalloc(dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;
	dev_set_drvdata(dev, data);

	regmap = devm_regmap_init_i2c(client, &ltc2945_regmap_config);
	if (IS_ERR(regmap)) {
		dev_err(dev, "failed to allocate register map\n");
		return PTR_ERR(regmap);
	}

	data->regmap = regmap;
	if (device_property_read_u32(dev, "shunt-resistor-micro-ohms",
				     &data->shunt_resistor))
		data->shunt_resistor = 1000;

	if (data->shunt_resistor == 0)
		return -EINVAL;

	/* Clear faults */
	regmap_write(regmap, LTC2945_FAULT, 0x00);

	hwmon_dev = devm_hwmon_device_register_with_groups(dev, client->name,
							   data,
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
		.of_match_table = of_match_ptr(ltc2945_of_match),
	},
	.probe = ltc2945_probe,
	.id_table = ltc2945_id,
};

module_i2c_driver(ltc2945_driver);

MODULE_AUTHOR("Guenter Roeck <linux@roeck-us.net>");
MODULE_DESCRIPTION("LTC2945 driver");
MODULE_LICENSE("GPL");
