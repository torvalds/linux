/*
 * tc654.c - Linux kernel modules for fan speed controller
 *
 * Copyright (C) 2016 Allied Telesis Labs NZ
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

#include <linux/bitops.h>
#include <linux/err.h>
#include <linux/hwmon.h>
#include <linux/hwmon-sysfs.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/jiffies.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/util_macros.h>

enum tc654_regs {
	TC654_REG_RPM1 = 0x00,	/* RPM Output 1 */
	TC654_REG_RPM2 = 0x01,	/* RPM Output 2 */
	TC654_REG_FAN_FAULT1 = 0x02,	/* Fan Fault 1 Threshold */
	TC654_REG_FAN_FAULT2 = 0x03,	/* Fan Fault 2 Threshold */
	TC654_REG_CONFIG = 0x04,	/* Configuration */
	TC654_REG_STATUS = 0x05,	/* Status */
	TC654_REG_DUTY_CYCLE = 0x06,	/* Fan Speed Duty Cycle */
	TC654_REG_MFR_ID = 0x07,	/* Manufacturer Identification */
	TC654_REG_VER_ID = 0x08,	/* Version Identification */
};

/* Macros to easily index the registers */
#define TC654_REG_RPM(idx)		(TC654_REG_RPM1 + (idx))
#define TC654_REG_FAN_FAULT(idx)	(TC654_REG_FAN_FAULT1 + (idx))

/* Config register bits */
#define TC654_REG_CONFIG_RES		BIT(6)	/* Resolution Selection */
#define TC654_REG_CONFIG_DUTYC		BIT(5)	/* Duty Cycle Control */
#define TC654_REG_CONFIG_SDM		BIT(0)	/* Shutdown Mode */

/* Status register bits */
#define TC654_REG_STATUS_F2F		BIT(1)	/* Fan 2 Fault */
#define TC654_REG_STATUS_F1F		BIT(0)	/* Fan 1 Fault */

/* RPM resolution for RPM Output registers */
#define TC654_HIGH_RPM_RESOLUTION	25	/* 25 RPM resolution */
#define TC654_LOW_RPM_RESOLUTION	50	/* 50 RPM resolution */

/* Convert to the fan fault RPM threshold from register value */
#define TC654_FAN_FAULT_FROM_REG(val)	((val) * 50)	/* 50 RPM resolution */

/* Convert to register value from the fan fault RPM threshold */
#define TC654_FAN_FAULT_TO_REG(val)	(((val) / 50) & 0xff)

/* Register data is read (and cached) at most once per second. */
#define TC654_UPDATE_INTERVAL		HZ

struct tc654_data {
	struct i2c_client *client;

	/* update mutex */
	struct mutex update_lock;

	/* tc654 register cache */
	bool valid;
	unsigned long last_updated;	/* in jiffies */

	u8 rpm_output[2];	/* The fan RPM data for fans 1 and 2 is then
				 * written to registers RPM1 and RPM2
				 */
	u8 fan_fault[2];	/* The Fan Fault Threshold Registers are used to
				 * set the fan fault threshold levels for fan 1
				 * and fan 2
				 */
	u8 config;	/* The Configuration Register is an 8-bit read/
			 * writable multi-function control register
			 *   7: Fan Fault Clear
			 *      1 = Clear Fan Fault
			 *      0 = Normal Operation (default)
			 *   6: Resolution Selection for RPM Output Registers
			 *      RPM Output Registers (RPM1 and RPM2) will be
			 *      set for
			 *      1 = 25 RPM (9-bit) resolution
			 *      0 = 50 RPM (8-bit) resolution (default)
			 *   5: Duty Cycle Control Method
			 *      The V OUT duty cycle will be controlled via
			 *      1 = the SMBus interface.
			 *      0 = via the V IN analog input pin. (default)
			 * 4,3: Fan 2 Pulses Per Rotation
			 *      00 = 1
			 *      01 = 2 (default)
			 *      10 = 4
			 *      11 = 8
			 * 2,1: Fan 1 Pulses Per Rotation
			 *      00 = 1
			 *      01 = 2 (default)
			 *      10 = 4
			 *      11 = 8
			 *   0: Shutdown Mode
			 *      1 = Shutdown mode.
			 *      0 = Normal operation. (default)
			 */
	u8 status;	/* The Status register provides all the information
			 * about what is going on within the TC654/TC655
			 * devices.
			 * 7,6: Unimplemented, Read as '0'
			 *   5: Over-Temperature Fault Condition
			 *      1 = Over-Temperature condition has occurred
			 *      0 = Normal operation. V IN is less than 2.6V
			 *   4: RPM2 Counter Overflow
			 *      1 = Fault condition
			 *      0 = Normal operation
			 *   3: RPM1 Counter Overflow
			 *      1 = Fault condition
			 *      0 = Normal operation
			 *   2: V IN Input Status
			 *      1 = V IN is open
			 *      0 = Normal operation. voltage present at V IN
			 *   1: Fan 2 Fault
			 *      1 = Fault condition
			 *      0 = Normal operation
			 *   0: Fan 1 Fault
			 *      1 = Fault condition
			 *      0 = Normal operation
			 */
	u8 duty_cycle;	/* The DUTY_CYCLE register is a 4-bit read/
			 * writable register used to control the duty
			 * cycle of the V OUT output.
			 */
};

/* helper to grab and cache data, at most one time per second */
static struct tc654_data *tc654_update_client(struct device *dev)
{
	struct tc654_data *data = dev_get_drvdata(dev);
	struct i2c_client *client = data->client;
	int ret = 0;

	mutex_lock(&data->update_lock);
	if (time_before(jiffies, data->last_updated + TC654_UPDATE_INTERVAL) &&
	    likely(data->valid))
		goto out;

	ret = i2c_smbus_read_byte_data(client, TC654_REG_RPM(0));
	if (ret < 0)
		goto out;
	data->rpm_output[0] = ret;

	ret = i2c_smbus_read_byte_data(client, TC654_REG_RPM(1));
	if (ret < 0)
		goto out;
	data->rpm_output[1] = ret;

	ret = i2c_smbus_read_byte_data(client, TC654_REG_FAN_FAULT(0));
	if (ret < 0)
		goto out;
	data->fan_fault[0] = ret;

	ret = i2c_smbus_read_byte_data(client, TC654_REG_FAN_FAULT(1));
	if (ret < 0)
		goto out;
	data->fan_fault[1] = ret;

	ret = i2c_smbus_read_byte_data(client, TC654_REG_CONFIG);
	if (ret < 0)
		goto out;
	data->config = ret;

	ret = i2c_smbus_read_byte_data(client, TC654_REG_STATUS);
	if (ret < 0)
		goto out;
	data->status = ret;

	ret = i2c_smbus_read_byte_data(client, TC654_REG_DUTY_CYCLE);
	if (ret < 0)
		goto out;
	data->duty_cycle = ret & 0x0f;

	data->last_updated = jiffies;
	data->valid = true;
out:
	mutex_unlock(&data->update_lock);

	if (ret < 0)		/* upon error, encode it in return value */
		data = ERR_PTR(ret);

	return data;
}

/*
 * sysfs attributes
 */

static ssize_t show_fan(struct device *dev, struct device_attribute *da,
			char *buf)
{
	int nr = to_sensor_dev_attr(da)->index;
	struct tc654_data *data = tc654_update_client(dev);
	int val;

	if (IS_ERR(data))
		return PTR_ERR(data);

	if (data->config & TC654_REG_CONFIG_RES)
		val = data->rpm_output[nr] * TC654_HIGH_RPM_RESOLUTION;
	else
		val = data->rpm_output[nr] * TC654_LOW_RPM_RESOLUTION;

	return sprintf(buf, "%d\n", val);
}

static ssize_t show_fan_min(struct device *dev, struct device_attribute *da,
			    char *buf)
{
	int nr = to_sensor_dev_attr(da)->index;
	struct tc654_data *data = tc654_update_client(dev);

	if (IS_ERR(data))
		return PTR_ERR(data);

	return sprintf(buf, "%d\n",
		       TC654_FAN_FAULT_FROM_REG(data->fan_fault[nr]));
}

static ssize_t set_fan_min(struct device *dev, struct device_attribute *da,
			   const char *buf, size_t count)
{
	int nr = to_sensor_dev_attr(da)->index;
	struct tc654_data *data = dev_get_drvdata(dev);
	struct i2c_client *client = data->client;
	unsigned long val;
	int ret;

	if (kstrtoul(buf, 10, &val))
		return -EINVAL;

	val = clamp_val(val, 0, 12750);

	mutex_lock(&data->update_lock);

	data->fan_fault[nr] = TC654_FAN_FAULT_TO_REG(val);
	ret = i2c_smbus_write_byte_data(client, TC654_REG_FAN_FAULT(nr),
					data->fan_fault[nr]);

	mutex_unlock(&data->update_lock);
	return ret < 0 ? ret : count;
}

static ssize_t show_fan_alarm(struct device *dev, struct device_attribute *da,
			      char *buf)
{
	int nr = to_sensor_dev_attr(da)->index;
	struct tc654_data *data = tc654_update_client(dev);
	int val;

	if (IS_ERR(data))
		return PTR_ERR(data);

	if (nr == 0)
		val = !!(data->status & TC654_REG_STATUS_F1F);
	else
		val = !!(data->status & TC654_REG_STATUS_F2F);

	return sprintf(buf, "%d\n", val);
}

static const u8 TC654_FAN_PULSE_SHIFT[] = { 1, 3 };

static ssize_t show_fan_pulses(struct device *dev, struct device_attribute *da,
			       char *buf)
{
	int nr = to_sensor_dev_attr(da)->index;
	struct tc654_data *data = tc654_update_client(dev);
	u8 val;

	if (IS_ERR(data))
		return PTR_ERR(data);

	val = BIT((data->config >> TC654_FAN_PULSE_SHIFT[nr]) & 0x03);
	return sprintf(buf, "%d\n", val);
}

static ssize_t set_fan_pulses(struct device *dev, struct device_attribute *da,
			      const char *buf, size_t count)
{
	int nr = to_sensor_dev_attr(da)->index;
	struct tc654_data *data = dev_get_drvdata(dev);
	struct i2c_client *client = data->client;
	u8 config;
	unsigned long val;
	int ret;

	if (kstrtoul(buf, 10, &val))
		return -EINVAL;

	switch (val) {
	case 1:
		config = 0;
		break;
	case 2:
		config = 1;
		break;
	case 4:
		config = 2;
		break;
	case 8:
		config = 3;
		break;
	default:
		return -EINVAL;
	}

	mutex_lock(&data->update_lock);

	data->config &= ~(0x03 << TC654_FAN_PULSE_SHIFT[nr]);
	data->config |= (config << TC654_FAN_PULSE_SHIFT[nr]);
	ret = i2c_smbus_write_byte_data(client, TC654_REG_CONFIG, data->config);

	mutex_unlock(&data->update_lock);
	return ret < 0 ? ret : count;
}

static ssize_t show_pwm_mode(struct device *dev,
			     struct device_attribute *da, char *buf)
{
	struct tc654_data *data = tc654_update_client(dev);

	if (IS_ERR(data))
		return PTR_ERR(data);

	return sprintf(buf, "%d\n", !!(data->config & TC654_REG_CONFIG_DUTYC));
}

static ssize_t set_pwm_mode(struct device *dev,
			    struct device_attribute *da,
			    const char *buf, size_t count)
{
	struct tc654_data *data = dev_get_drvdata(dev);
	struct i2c_client *client = data->client;
	unsigned long val;
	int ret;

	if (kstrtoul(buf, 10, &val))
		return -EINVAL;

	if (val != 0 && val != 1)
		return -EINVAL;

	mutex_lock(&data->update_lock);

	if (val)
		data->config |= TC654_REG_CONFIG_DUTYC;
	else
		data->config &= ~TC654_REG_CONFIG_DUTYC;

	ret = i2c_smbus_write_byte_data(client, TC654_REG_CONFIG, data->config);

	mutex_unlock(&data->update_lock);
	return ret < 0 ? ret : count;
}

static const int tc654_pwm_map[16] = { 77,  88, 102, 112, 124, 136, 148, 160,
				      172, 184, 196, 207, 219, 231, 243, 255};

static ssize_t show_pwm(struct device *dev, struct device_attribute *da,
			char *buf)
{
	struct tc654_data *data = tc654_update_client(dev);
	int pwm;

	if (IS_ERR(data))
		return PTR_ERR(data);

	if (data->config & TC654_REG_CONFIG_SDM)
		pwm = 0;
	else
		pwm = tc654_pwm_map[data->duty_cycle];

	return sprintf(buf, "%d\n", pwm);
}

static ssize_t set_pwm(struct device *dev, struct device_attribute *da,
		       const char *buf, size_t count)
{
	struct tc654_data *data = dev_get_drvdata(dev);
	struct i2c_client *client = data->client;
	unsigned long val;
	int ret;

	if (kstrtoul(buf, 10, &val))
		return -EINVAL;
	if (val > 255)
		return -EINVAL;

	mutex_lock(&data->update_lock);

	if (val == 0)
		data->config |= TC654_REG_CONFIG_SDM;
	else
		data->config &= ~TC654_REG_CONFIG_SDM;

	data->duty_cycle = find_closest(val, tc654_pwm_map,
					ARRAY_SIZE(tc654_pwm_map));

	ret = i2c_smbus_write_byte_data(client, TC654_REG_CONFIG, data->config);
	if (ret < 0)
		goto out;

	ret = i2c_smbus_write_byte_data(client, TC654_REG_DUTY_CYCLE,
					data->duty_cycle);

out:
	mutex_unlock(&data->update_lock);
	return ret < 0 ? ret : count;
}

static SENSOR_DEVICE_ATTR(fan1_input, S_IRUGO, show_fan, NULL, 0);
static SENSOR_DEVICE_ATTR(fan2_input, S_IRUGO, show_fan, NULL, 1);
static SENSOR_DEVICE_ATTR(fan1_min, S_IWUSR | S_IRUGO, show_fan_min,
			  set_fan_min, 0);
static SENSOR_DEVICE_ATTR(fan2_min, S_IWUSR | S_IRUGO, show_fan_min,
			  set_fan_min, 1);
static SENSOR_DEVICE_ATTR(fan1_alarm, S_IRUGO, show_fan_alarm, NULL, 0);
static SENSOR_DEVICE_ATTR(fan2_alarm, S_IRUGO, show_fan_alarm, NULL, 1);
static SENSOR_DEVICE_ATTR(fan1_pulses, S_IWUSR | S_IRUGO, show_fan_pulses,
			  set_fan_pulses, 0);
static SENSOR_DEVICE_ATTR(fan2_pulses, S_IWUSR | S_IRUGO, show_fan_pulses,
			  set_fan_pulses, 1);
static SENSOR_DEVICE_ATTR(pwm1_mode, S_IWUSR | S_IRUGO,
			  show_pwm_mode, set_pwm_mode, 0);
static SENSOR_DEVICE_ATTR(pwm1, S_IWUSR | S_IRUGO, show_pwm,
			  set_pwm, 0);

/* Driver data */
static struct attribute *tc654_attrs[] = {
	&sensor_dev_attr_fan1_input.dev_attr.attr,
	&sensor_dev_attr_fan2_input.dev_attr.attr,
	&sensor_dev_attr_fan1_min.dev_attr.attr,
	&sensor_dev_attr_fan2_min.dev_attr.attr,
	&sensor_dev_attr_fan1_alarm.dev_attr.attr,
	&sensor_dev_attr_fan2_alarm.dev_attr.attr,
	&sensor_dev_attr_fan1_pulses.dev_attr.attr,
	&sensor_dev_attr_fan2_pulses.dev_attr.attr,
	&sensor_dev_attr_pwm1_mode.dev_attr.attr,
	&sensor_dev_attr_pwm1.dev_attr.attr,
	NULL
};

ATTRIBUTE_GROUPS(tc654);

/*
 * device probe and removal
 */

static int tc654_probe(struct i2c_client *client,
		       const struct i2c_device_id *id)
{
	struct device *dev = &client->dev;
	struct tc654_data *data;
	struct device *hwmon_dev;
	int ret;

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_SMBUS_BYTE_DATA))
		return -ENODEV;

	data = devm_kzalloc(dev, sizeof(struct tc654_data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->client = client;
	mutex_init(&data->update_lock);

	ret = i2c_smbus_read_byte_data(client, TC654_REG_CONFIG);
	if (ret < 0)
		return ret;

	data->config = ret;

	hwmon_dev =
	    devm_hwmon_device_register_with_groups(dev, client->name, data,
						   tc654_groups);
	return PTR_ERR_OR_ZERO(hwmon_dev);
}

static const struct i2c_device_id tc654_id[] = {
	{"tc654", 0},
	{"tc655", 0},
	{}
};

MODULE_DEVICE_TABLE(i2c, tc654_id);

static struct i2c_driver tc654_driver = {
	.driver = {
		   .name = "tc654",
		   },
	.probe = tc654_probe,
	.id_table = tc654_id,
};

module_i2c_driver(tc654_driver);

MODULE_AUTHOR("Allied Telesis Labs");
MODULE_DESCRIPTION("Microchip TC654/TC655 driver");
MODULE_LICENSE("GPL");
