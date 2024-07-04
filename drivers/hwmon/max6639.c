// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * max6639.c - Support for Maxim MAX6639
 *
 * 2-Channel Temperature Monitor with Dual PWM Fan-Speed Controller
 *
 * Copyright (C) 2010, 2011 Roland Stigge <stigge@antcom.de>
 *
 * based on the initial MAX6639 support from semptian.net
 * by He Changqing <hechangqing@semptian.com>
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/jiffies.h>
#include <linux/i2c.h>
#include <linux/hwmon.h>
#include <linux/hwmon-sysfs.h>
#include <linux/err.h>
#include <linux/mutex.h>
#include <linux/platform_data/max6639.h>
#include <linux/regmap.h>

/* Addresses to scan */
static const unsigned short normal_i2c[] = { 0x2c, 0x2e, 0x2f, I2C_CLIENT_END };

/* The MAX6639 registers, valid channel numbers: 0, 1 */
#define MAX6639_REG_TEMP(ch)			(0x00 + (ch))
#define MAX6639_REG_STATUS			0x02
#define MAX6639_REG_OUTPUT_MASK			0x03
#define MAX6639_REG_GCONFIG			0x04
#define MAX6639_REG_TEMP_EXT(ch)		(0x05 + (ch))
#define MAX6639_REG_ALERT_LIMIT(ch)		(0x08 + (ch))
#define MAX6639_REG_OT_LIMIT(ch)		(0x0A + (ch))
#define MAX6639_REG_THERM_LIMIT(ch)		(0x0C + (ch))
#define MAX6639_REG_FAN_CONFIG1(ch)		(0x10 + (ch) * 4)
#define MAX6639_REG_FAN_CONFIG2a(ch)		(0x11 + (ch) * 4)
#define MAX6639_REG_FAN_CONFIG2b(ch)		(0x12 + (ch) * 4)
#define MAX6639_REG_FAN_CONFIG3(ch)		(0x13 + (ch) * 4)
#define MAX6639_REG_FAN_CNT(ch)			(0x20 + (ch))
#define MAX6639_REG_TARGET_CNT(ch)		(0x22 + (ch))
#define MAX6639_REG_FAN_PPR(ch)			(0x24 + (ch))
#define MAX6639_REG_TARGTDUTY(ch)		(0x26 + (ch))
#define MAX6639_REG_FAN_START_TEMP(ch)		(0x28 + (ch))
#define MAX6639_REG_DEVID			0x3D
#define MAX6639_REG_MANUID			0x3E
#define MAX6639_REG_DEVREV			0x3F

/* Register bits */
#define MAX6639_GCONFIG_STANDBY			0x80
#define MAX6639_GCONFIG_POR			0x40
#define MAX6639_GCONFIG_DISABLE_TIMEOUT		0x20
#define MAX6639_GCONFIG_CH2_LOCAL		0x10
#define MAX6639_GCONFIG_PWM_FREQ_HI		0x08

#define MAX6639_FAN_CONFIG1_PWM			0x80

#define MAX6639_FAN_CONFIG3_THERM_FULL_SPEED	0x40

#define MAX6639_NUM_CHANNELS			2

static const int rpm_ranges[] = { 2000, 4000, 8000, 16000 };

#define FAN_FROM_REG(val, rpm_range)	((val) == 0 || (val) == 255 ? \
				0 : (rpm_ranges[rpm_range] * 30) / (val))
#define TEMP_LIMIT_TO_REG(val)	clamp_val((val) / 1000, 0, 255)

/*
 * Client data (each client gets its own)
 */
struct max6639_data {
	struct regmap *regmap;

	/* Register values initialized only once */
	u8 ppr;			/* Pulses per rotation 0..3 for 1..4 ppr */
	u8 rpm_range;		/* Index in above rpm_ranges table */

	/* Optional regulator for FAN supply */
	struct regulator *reg;
};

static ssize_t temp_input_show(struct device *dev,
			       struct device_attribute *dev_attr, char *buf)
{
	long temp;
	struct max6639_data *data = dev_get_drvdata(dev);
	struct sensor_device_attribute *attr = to_sensor_dev_attr(dev_attr);
	unsigned int val;
	int res;

	/*
	 * Lock isn't needed as MAX6639_REG_TEMP wpnt change for at least 250ms after reading
	 * MAX6639_REG_TEMP_EXT
	 */
	res = regmap_read(data->regmap, MAX6639_REG_TEMP_EXT(attr->index), &val);
	if (res < 0)
		return res;

	temp = val >> 5;
	res = regmap_read(data->regmap, MAX6639_REG_TEMP(attr->index), &val);
	if (res < 0)
		return res;

	temp |= val << 3;
	temp *= 125;

	return sprintf(buf, "%ld\n", temp);
}

static ssize_t temp_fault_show(struct device *dev,
			       struct device_attribute *dev_attr, char *buf)
{
	struct max6639_data *data = dev_get_drvdata(dev);
	struct sensor_device_attribute *attr = to_sensor_dev_attr(dev_attr);
	unsigned int val;
	int res;

	res = regmap_read(data->regmap, MAX6639_REG_TEMP_EXT(attr->index), &val);
	if (res < 0)
		return res;

	return sprintf(buf, "%d\n", val & 1);
}

static ssize_t temp_max_show(struct device *dev,
			     struct device_attribute *dev_attr, char *buf)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(dev_attr);
	struct max6639_data *data = dev_get_drvdata(dev);
	unsigned int val;
	int res;

	res = regmap_read(data->regmap, MAX6639_REG_THERM_LIMIT(attr->index), &val);
	if (res < 0)
		return res;

	return sprintf(buf, "%d\n", (val * 1000));
}

static ssize_t temp_max_store(struct device *dev,
			      struct device_attribute *dev_attr,
			      const char *buf, size_t count)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(dev_attr);
	struct max6639_data *data = dev_get_drvdata(dev);
	unsigned long val;
	int res;

	res = kstrtoul(buf, 10, &val);
	if (res)
		return res;

	regmap_write(data->regmap, MAX6639_REG_THERM_LIMIT(attr->index),
		     TEMP_LIMIT_TO_REG(val));
	return count;
}

static ssize_t temp_crit_show(struct device *dev,
			      struct device_attribute *dev_attr, char *buf)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(dev_attr);
	struct max6639_data *data = dev_get_drvdata(dev);
	unsigned int val;
	int res;

	res = regmap_read(data->regmap, MAX6639_REG_ALERT_LIMIT(attr->index), &val);
	if (res < 0)
		return res;

	return sprintf(buf, "%d\n", (val * 1000));
}

static ssize_t temp_crit_store(struct device *dev,
			       struct device_attribute *dev_attr,
			       const char *buf, size_t count)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(dev_attr);
	struct max6639_data *data = dev_get_drvdata(dev);
	unsigned long val;
	int res;

	res = kstrtoul(buf, 10, &val);
	if (res)
		return res;

	regmap_write(data->regmap, MAX6639_REG_ALERT_LIMIT(attr->index),
		     TEMP_LIMIT_TO_REG(val));
	return count;
}

static ssize_t temp_emergency_show(struct device *dev,
				   struct device_attribute *dev_attr,
				   char *buf)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(dev_attr);
	struct max6639_data *data = dev_get_drvdata(dev);
	unsigned int val;
	int res;

	res = regmap_read(data->regmap, MAX6639_REG_OT_LIMIT(attr->index), &val);
	if (res < 0)
		return res;

	return sprintf(buf, "%d\n", (val * 1000));
}

static ssize_t temp_emergency_store(struct device *dev,
				    struct device_attribute *dev_attr,
				    const char *buf, size_t count)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(dev_attr);
	struct max6639_data *data = dev_get_drvdata(dev);
	unsigned long val;
	int res;

	res = kstrtoul(buf, 10, &val);
	if (res)
		return res;

	regmap_write(data->regmap, MAX6639_REG_OT_LIMIT(attr->index), TEMP_LIMIT_TO_REG(val));

	return count;
}

static ssize_t pwm_show(struct device *dev, struct device_attribute *dev_attr,
			char *buf)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(dev_attr);
	struct max6639_data *data = dev_get_drvdata(dev);
	unsigned int val;
	int res;

	res = regmap_read(data->regmap, MAX6639_REG_TARGTDUTY(attr->index), &val);
	if (res < 0)
		return res;

	return sprintf(buf, "%d\n", val * 255 / 120);
}

static ssize_t pwm_store(struct device *dev,
			 struct device_attribute *dev_attr, const char *buf,
			 size_t count)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(dev_attr);
	struct max6639_data *data = dev_get_drvdata(dev);
	unsigned long val;
	int res;

	res = kstrtoul(buf, 10, &val);
	if (res)
		return res;

	val = clamp_val(val, 0, 255);

	regmap_write(data->regmap, MAX6639_REG_TARGTDUTY(attr->index), val * 120 / 255);

	return count;
}

static ssize_t fan_input_show(struct device *dev,
			      struct device_attribute *dev_attr, char *buf)
{
	struct max6639_data *data = dev_get_drvdata(dev);
	struct sensor_device_attribute *attr = to_sensor_dev_attr(dev_attr);
	unsigned int val;
	int res;

	res = regmap_read(data->regmap, MAX6639_REG_FAN_CNT(attr->index), &val);
	if (res < 0)
		return res;

	return sprintf(buf, "%d\n", FAN_FROM_REG(val, data->rpm_range));
}

static ssize_t alarm_show(struct device *dev,
			  struct device_attribute *dev_attr, char *buf)
{
	struct max6639_data *data = dev_get_drvdata(dev);
	struct sensor_device_attribute *attr = to_sensor_dev_attr(dev_attr);
	unsigned int val;
	int res;

	res = regmap_read(data->regmap, MAX6639_REG_STATUS, &val);
	if (res < 0)
		return res;

	return sprintf(buf, "%d\n", !!(val & (1 << attr->index)));
}

static SENSOR_DEVICE_ATTR_RO(temp1_input, temp_input, 0);
static SENSOR_DEVICE_ATTR_RO(temp2_input, temp_input, 1);
static SENSOR_DEVICE_ATTR_RO(temp1_fault, temp_fault, 0);
static SENSOR_DEVICE_ATTR_RO(temp2_fault, temp_fault, 1);
static SENSOR_DEVICE_ATTR_RW(temp1_max, temp_max, 0);
static SENSOR_DEVICE_ATTR_RW(temp2_max, temp_max, 1);
static SENSOR_DEVICE_ATTR_RW(temp1_crit, temp_crit, 0);
static SENSOR_DEVICE_ATTR_RW(temp2_crit, temp_crit, 1);
static SENSOR_DEVICE_ATTR_RW(temp1_emergency, temp_emergency, 0);
static SENSOR_DEVICE_ATTR_RW(temp2_emergency, temp_emergency, 1);
static SENSOR_DEVICE_ATTR_RW(pwm1, pwm, 0);
static SENSOR_DEVICE_ATTR_RW(pwm2, pwm, 1);
static SENSOR_DEVICE_ATTR_RO(fan1_input, fan_input, 0);
static SENSOR_DEVICE_ATTR_RO(fan2_input, fan_input, 1);
static SENSOR_DEVICE_ATTR_RO(fan1_fault, alarm, 1);
static SENSOR_DEVICE_ATTR_RO(fan2_fault, alarm, 0);
static SENSOR_DEVICE_ATTR_RO(temp1_max_alarm, alarm, 3);
static SENSOR_DEVICE_ATTR_RO(temp2_max_alarm, alarm, 2);
static SENSOR_DEVICE_ATTR_RO(temp1_crit_alarm, alarm, 7);
static SENSOR_DEVICE_ATTR_RO(temp2_crit_alarm, alarm, 6);
static SENSOR_DEVICE_ATTR_RO(temp1_emergency_alarm, alarm, 5);
static SENSOR_DEVICE_ATTR_RO(temp2_emergency_alarm, alarm, 4);


static struct attribute *max6639_attrs[] = {
	&sensor_dev_attr_temp1_input.dev_attr.attr,
	&sensor_dev_attr_temp2_input.dev_attr.attr,
	&sensor_dev_attr_temp1_fault.dev_attr.attr,
	&sensor_dev_attr_temp2_fault.dev_attr.attr,
	&sensor_dev_attr_temp1_max.dev_attr.attr,
	&sensor_dev_attr_temp2_max.dev_attr.attr,
	&sensor_dev_attr_temp1_crit.dev_attr.attr,
	&sensor_dev_attr_temp2_crit.dev_attr.attr,
	&sensor_dev_attr_temp1_emergency.dev_attr.attr,
	&sensor_dev_attr_temp2_emergency.dev_attr.attr,
	&sensor_dev_attr_pwm1.dev_attr.attr,
	&sensor_dev_attr_pwm2.dev_attr.attr,
	&sensor_dev_attr_fan1_input.dev_attr.attr,
	&sensor_dev_attr_fan2_input.dev_attr.attr,
	&sensor_dev_attr_fan1_fault.dev_attr.attr,
	&sensor_dev_attr_fan2_fault.dev_attr.attr,
	&sensor_dev_attr_temp1_max_alarm.dev_attr.attr,
	&sensor_dev_attr_temp2_max_alarm.dev_attr.attr,
	&sensor_dev_attr_temp1_crit_alarm.dev_attr.attr,
	&sensor_dev_attr_temp2_crit_alarm.dev_attr.attr,
	&sensor_dev_attr_temp1_emergency_alarm.dev_attr.attr,
	&sensor_dev_attr_temp2_emergency_alarm.dev_attr.attr,
	NULL
};
ATTRIBUTE_GROUPS(max6639);

/*
 *  returns respective index in rpm_ranges table
 *  1 by default on invalid range
 */
static int rpm_range_to_reg(int range)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(rpm_ranges); i++) {
		if (rpm_ranges[i] == range)
			return i;
	}

	return 1; /* default: 4000 RPM */
}

static int max6639_set_ppr(struct max6639_data *data, u8 channel, u8 ppr)
{
	return regmap_write(data->regmap, MAX6639_REG_FAN_PPR(channel), ppr << 6);
}

static int max6639_init_client(struct i2c_client *client,
			       struct max6639_data *data)
{
	struct max6639_platform_data *max6639_info =
		dev_get_platdata(&client->dev);
	int i;
	int rpm_range = 1; /* default: 4000 RPM */
	int err, ppr;

	/* Reset chip to default values, see below for GCONFIG setup */
	err = regmap_write(data->regmap, MAX6639_REG_GCONFIG, MAX6639_GCONFIG_POR);
	if (err)
		return err;

	/* Fans pulse per revolution is 2 by default */
	if (max6639_info && max6639_info->ppr > 0 &&
			max6639_info->ppr < 5)
		ppr = max6639_info->ppr;
	else
		ppr = 2;
	ppr -= 1;

	if (max6639_info)
		rpm_range = rpm_range_to_reg(max6639_info->rpm_range);
	data->rpm_range = rpm_range;

	for (i = 0; i < MAX6639_NUM_CHANNELS; i++) {

		/* Set Fan pulse per revolution */
		err = max6639_set_ppr(data, i, ppr);
		if (err)
			return err;

		/* Fans config PWM, RPM */
		err = regmap_write(data->regmap, MAX6639_REG_FAN_CONFIG1(i),
				   MAX6639_FAN_CONFIG1_PWM | rpm_range);
		if (err)
			return err;

		/* Fans PWM polarity high by default */
		if (max6639_info && max6639_info->pwm_polarity == 0)
			err = regmap_write(data->regmap, MAX6639_REG_FAN_CONFIG2a(i), 0x00);
		else
			err = regmap_write(data->regmap, MAX6639_REG_FAN_CONFIG2a(i), 0x02);
		if (err)
			return err;

		/*
		 * /THERM full speed enable,
		 * PWM frequency 25kHz, see also GCONFIG below
		 */
		err = regmap_write(data->regmap, MAX6639_REG_FAN_CONFIG3(i),
				   MAX6639_FAN_CONFIG3_THERM_FULL_SPEED | 0x03);
		if (err)
			return err;

		/* Max. temp. 80C/90C/100C */
		err = regmap_write(data->regmap, MAX6639_REG_THERM_LIMIT(i), 80);
		if (err)
			return err;
		err = regmap_write(data->regmap, MAX6639_REG_ALERT_LIMIT(i), 90);
		if (err)
			return err;
		err = regmap_write(data->regmap, MAX6639_REG_OT_LIMIT(i), 100);
		if (err)
			return err;

		/* PWM 120/120 (i.e. 100%) */
		err = regmap_write(data->regmap, MAX6639_REG_TARGTDUTY(i), 120);
		if (err)
			return err;
	}
	/* Start monitoring */
	return regmap_write(data->regmap, MAX6639_REG_GCONFIG,
			    MAX6639_GCONFIG_DISABLE_TIMEOUT | MAX6639_GCONFIG_CH2_LOCAL |
			    MAX6639_GCONFIG_PWM_FREQ_HI);

}

/* Return 0 if detection is successful, -ENODEV otherwise */
static int max6639_detect(struct i2c_client *client,
			  struct i2c_board_info *info)
{
	struct i2c_adapter *adapter = client->adapter;
	int dev_id, manu_id;

	if (!i2c_check_functionality(adapter, I2C_FUNC_SMBUS_BYTE_DATA))
		return -ENODEV;

	/* Actual detection via device and manufacturer ID */
	dev_id = i2c_smbus_read_byte_data(client, MAX6639_REG_DEVID);
	manu_id = i2c_smbus_read_byte_data(client, MAX6639_REG_MANUID);
	if (dev_id != 0x58 || manu_id != 0x4D)
		return -ENODEV;

	strscpy(info->type, "max6639", I2C_NAME_SIZE);

	return 0;
}

static void max6639_regulator_disable(void *data)
{
	regulator_disable(data);
}

static bool max6639_regmap_is_volatile(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case MAX6639_REG_TEMP(0):
	case MAX6639_REG_TEMP_EXT(0):
	case MAX6639_REG_TEMP(1):
	case MAX6639_REG_TEMP_EXT(1):
	case MAX6639_REG_STATUS:
	case MAX6639_REG_FAN_CNT(0):
	case MAX6639_REG_FAN_CNT(1):
	case MAX6639_REG_TARGTDUTY(0):
	case MAX6639_REG_TARGTDUTY(1):
		return true;
	default:
		return false;
	}
}

static const struct regmap_config max6639_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = MAX6639_REG_DEVREV,
	.cache_type = REGCACHE_MAPLE,
	.volatile_reg = max6639_regmap_is_volatile,
};

static int max6639_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct max6639_data *data;
	struct device *hwmon_dev;
	int err;

	data = devm_kzalloc(dev, sizeof(struct max6639_data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->regmap = devm_regmap_init_i2c(client, &max6639_regmap_config);
	if (IS_ERR(data->regmap))
		return dev_err_probe(dev,
				     PTR_ERR(data->regmap),
				     "regmap initialization failed\n");

	data->reg = devm_regulator_get_optional(dev, "fan");
	if (IS_ERR(data->reg)) {
		if (PTR_ERR(data->reg) != -ENODEV)
			return PTR_ERR(data->reg);

		data->reg = NULL;
	} else {
		/* Spin up fans */
		err = regulator_enable(data->reg);
		if (err) {
			dev_err(dev, "Failed to enable fan supply: %d\n", err);
			return err;
		}
		err = devm_add_action_or_reset(dev, max6639_regulator_disable,
					       data->reg);
		if (err) {
			dev_err(dev, "Failed to register action: %d\n", err);
			return err;
		}
	}

	/* Initialize the max6639 chip */
	err = max6639_init_client(client, data);
	if (err < 0)
		return err;

	hwmon_dev = devm_hwmon_device_register_with_groups(dev, client->name,
							   data,
							   max6639_groups);
	return PTR_ERR_OR_ZERO(hwmon_dev);
}

static int max6639_suspend(struct device *dev)
{
	struct max6639_data *data = dev_get_drvdata(dev);

	if (data->reg)
		regulator_disable(data->reg);

	return regmap_write_bits(data->regmap, MAX6639_REG_GCONFIG, MAX6639_GCONFIG_STANDBY,
				 MAX6639_GCONFIG_STANDBY);
}

static int max6639_resume(struct device *dev)
{
	struct max6639_data *data = dev_get_drvdata(dev);
	int ret;

	if (data->reg) {
		ret = regulator_enable(data->reg);
		if (ret) {
			dev_err(dev, "Failed to enable fan supply: %d\n", ret);
			return ret;
		}
	}

	return regmap_write_bits(data->regmap, MAX6639_REG_GCONFIG, MAX6639_GCONFIG_STANDBY,
				 ~MAX6639_GCONFIG_STANDBY);
}

static const struct i2c_device_id max6639_id[] = {
	{"max6639"},
	{ }
};

MODULE_DEVICE_TABLE(i2c, max6639_id);

static DEFINE_SIMPLE_DEV_PM_OPS(max6639_pm_ops, max6639_suspend, max6639_resume);

static const struct of_device_id max6639_of_match[] = {
	{ .compatible = "maxim,max6639", },
	{ },
};

static struct i2c_driver max6639_driver = {
	.class = I2C_CLASS_HWMON,
	.driver = {
		   .name = "max6639",
		   .pm = pm_sleep_ptr(&max6639_pm_ops),
		   .of_match_table = max6639_of_match,
		   },
	.probe = max6639_probe,
	.id_table = max6639_id,
	.detect = max6639_detect,
	.address_list = normal_i2c,
};

module_i2c_driver(max6639_driver);

MODULE_AUTHOR("Roland Stigge <stigge@antcom.de>");
MODULE_DESCRIPTION("max6639 driver");
MODULE_LICENSE("GPL");
