/*
 * Driver for Linear Technology LTC4215 I2C Hot Swap Controller
 *
 * Copyright (C) 2009 Ira W. Snyder <iws@ovro.caltech.edu>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * Datasheet:
 * http://www.linear.com/pc/downloadDocument.do?navId=H0,C1,C1003,C1006,C1163,P17572,D12697
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/err.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/hwmon.h>
#include <linux/hwmon-sysfs.h>
#include <linux/jiffies.h>

/* Here are names of the chip's registers (a.k.a. commands) */
enum ltc4215_cmd {
	LTC4215_CONTROL			= 0x00, /* rw */
	LTC4215_ALERT			= 0x01, /* rw */
	LTC4215_STATUS			= 0x02, /* ro */
	LTC4215_FAULT			= 0x03, /* rw */
	LTC4215_SENSE			= 0x04, /* rw */
	LTC4215_SOURCE			= 0x05, /* rw */
	LTC4215_ADIN			= 0x06, /* rw */
};

struct ltc4215_data {
	struct i2c_client *client;

	struct mutex update_lock;
	bool valid;
	unsigned long last_updated; /* in jiffies */

	/* Registers */
	u8 regs[7];
};

static struct ltc4215_data *ltc4215_update_device(struct device *dev)
{
	struct ltc4215_data *data = dev_get_drvdata(dev);
	struct i2c_client *client = data->client;
	s32 val;
	int i;

	mutex_lock(&data->update_lock);

	/* The chip's A/D updates 10 times per second */
	if (time_after(jiffies, data->last_updated + HZ / 10) || !data->valid) {

		dev_dbg(&client->dev, "Starting ltc4215 update\n");

		/* Read all registers */
		for (i = 0; i < ARRAY_SIZE(data->regs); i++) {
			val = i2c_smbus_read_byte_data(client, i);
			if (unlikely(val < 0))
				data->regs[i] = 0;
			else
				data->regs[i] = val;
		}

		data->last_updated = jiffies;
		data->valid = 1;
	}

	mutex_unlock(&data->update_lock);

	return data;
}

/* Return the voltage from the given register in millivolts */
static int ltc4215_get_voltage(struct device *dev, u8 reg)
{
	struct ltc4215_data *data = ltc4215_update_device(dev);
	const u8 regval = data->regs[reg];
	u32 voltage = 0;

	switch (reg) {
	case LTC4215_SENSE:
		/* 151 uV per increment */
		voltage = regval * 151 / 1000;
		break;
	case LTC4215_SOURCE:
		/* 60.5 mV per increment */
		voltage = regval * 605 / 10;
		break;
	case LTC4215_ADIN:
		/*
		 * The ADIN input is divided by 12.5, and has 4.82 mV
		 * per increment, so we have the additional multiply
		 */
		voltage = regval * 482 * 125 / 1000;
		break;
	default:
		/* If we get here, the developer messed up */
		WARN_ON_ONCE(1);
		break;
	}

	return voltage;
}

/* Return the current from the sense resistor in mA */
static unsigned int ltc4215_get_current(struct device *dev)
{
	struct ltc4215_data *data = ltc4215_update_device(dev);

	/*
	 * The strange looking conversions that follow are fixed-point
	 * math, since we cannot do floating point in the kernel.
	 *
	 * Step 1: convert sense register to microVolts
	 * Step 2: convert voltage to milliAmperes
	 *
	 * If you play around with the V=IR equation, you come up with
	 * the following: X uV / Y mOhm == Z mA
	 *
	 * With the resistors that are fractions of a milliOhm, we multiply
	 * the voltage and resistance by 10, to shift the decimal point.
	 * Now we can use the normal division operator again.
	 */

	/* Calculate voltage in microVolts (151 uV per increment) */
	const unsigned int voltage = data->regs[LTC4215_SENSE] * 151;

	/* Calculate current in milliAmperes (4 milliOhm sense resistor) */
	const unsigned int curr = voltage / 4;

	return curr;
}

static ssize_t ltc4215_show_voltage(struct device *dev,
				    struct device_attribute *da,
				    char *buf)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(da);
	const int voltage = ltc4215_get_voltage(dev, attr->index);

	return snprintf(buf, PAGE_SIZE, "%d\n", voltage);
}

static ssize_t ltc4215_show_current(struct device *dev,
				    struct device_attribute *da,
				    char *buf)
{
	const unsigned int curr = ltc4215_get_current(dev);

	return snprintf(buf, PAGE_SIZE, "%u\n", curr);
}

static ssize_t ltc4215_show_power(struct device *dev,
				  struct device_attribute *da,
				  char *buf)
{
	const unsigned int curr = ltc4215_get_current(dev);
	const int output_voltage = ltc4215_get_voltage(dev, LTC4215_ADIN);

	/* current in mA * voltage in mV == power in uW */
	const unsigned int power = abs(output_voltage * curr);

	return snprintf(buf, PAGE_SIZE, "%u\n", power);
}

static ssize_t ltc4215_show_alarm(struct device *dev,
					  struct device_attribute *da,
					  char *buf)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(da);
	struct ltc4215_data *data = ltc4215_update_device(dev);
	const u8 reg = data->regs[LTC4215_STATUS];
	const u32 mask = attr->index;

	return snprintf(buf, PAGE_SIZE, "%u\n", !!(reg & mask));
}

/*
 * These macros are used below in constructing device attribute objects
 * for use with sysfs_create_group() to make a sysfs device file
 * for each register.
 */

/* Construct a sensor_device_attribute structure for each register */

/* Current */
static SENSOR_DEVICE_ATTR(curr1_input, S_IRUGO, ltc4215_show_current, NULL, 0);
static SENSOR_DEVICE_ATTR(curr1_max_alarm, S_IRUGO, ltc4215_show_alarm, NULL,
			  1 << 2);

/* Power (virtual) */
static SENSOR_DEVICE_ATTR(power1_input, S_IRUGO, ltc4215_show_power, NULL, 0);

/* Input Voltage */
static SENSOR_DEVICE_ATTR(in1_input, S_IRUGO, ltc4215_show_voltage, NULL,
			  LTC4215_ADIN);
static SENSOR_DEVICE_ATTR(in1_max_alarm, S_IRUGO, ltc4215_show_alarm, NULL,
			  1 << 0);
static SENSOR_DEVICE_ATTR(in1_min_alarm, S_IRUGO, ltc4215_show_alarm, NULL,
			  1 << 1);

/* Output Voltage */
static SENSOR_DEVICE_ATTR(in2_input, S_IRUGO, ltc4215_show_voltage, NULL,
			  LTC4215_SOURCE);
static SENSOR_DEVICE_ATTR(in2_min_alarm, S_IRUGO, ltc4215_show_alarm, NULL,
			  1 << 3);

/*
 * Finally, construct an array of pointers to members of the above objects,
 * as required for sysfs_create_group()
 */
static struct attribute *ltc4215_attrs[] = {
	&sensor_dev_attr_curr1_input.dev_attr.attr,
	&sensor_dev_attr_curr1_max_alarm.dev_attr.attr,

	&sensor_dev_attr_power1_input.dev_attr.attr,

	&sensor_dev_attr_in1_input.dev_attr.attr,
	&sensor_dev_attr_in1_max_alarm.dev_attr.attr,
	&sensor_dev_attr_in1_min_alarm.dev_attr.attr,

	&sensor_dev_attr_in2_input.dev_attr.attr,
	&sensor_dev_attr_in2_min_alarm.dev_attr.attr,

	NULL,
};
ATTRIBUTE_GROUPS(ltc4215);

static int ltc4215_probe(struct i2c_client *client,
			 const struct i2c_device_id *id)
{
	struct i2c_adapter *adapter = client->adapter;
	struct device *dev = &client->dev;
	struct ltc4215_data *data;
	struct device *hwmon_dev;

	if (!i2c_check_functionality(adapter, I2C_FUNC_SMBUS_BYTE_DATA))
		return -ENODEV;

	data = devm_kzalloc(dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->client = client;
	mutex_init(&data->update_lock);

	/* Initialize the LTC4215 chip */
	i2c_smbus_write_byte_data(client, LTC4215_FAULT, 0x00);

	hwmon_dev = devm_hwmon_device_register_with_groups(dev, client->name,
							   data,
							   ltc4215_groups);
	return PTR_ERR_OR_ZERO(hwmon_dev);
}

static const struct i2c_device_id ltc4215_id[] = {
	{ "ltc4215", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, ltc4215_id);

/* This is the driver that will be inserted */
static struct i2c_driver ltc4215_driver = {
	.driver = {
		.name	= "ltc4215",
	},
	.probe		= ltc4215_probe,
	.id_table	= ltc4215_id,
};

module_i2c_driver(ltc4215_driver);

MODULE_AUTHOR("Ira W. Snyder <iws@ovro.caltech.edu>");
MODULE_DESCRIPTION("LTC4215 driver");
MODULE_LICENSE("GPL");
