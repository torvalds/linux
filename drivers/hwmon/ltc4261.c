// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Driver for Linear Technology LTC4261 I2C Negative Voltage Hot Swap Controller
 *
 * Copyright (C) 2010 Ericsson AB.
 *
 * Derived from:
 *
 *  Driver for Linear Technology LTC4245 I2C Multiple Supply Hot Swap Controller
 *  Copyright (C) 2008 Ira W. Snyder <iws@ovro.caltech.edu>
 *
 * Datasheet: http://cds.linear.com/docs/Datasheet/42612fb.pdf
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

/* chip registers */
#define LTC4261_STATUS	0x00	/* readonly */
#define LTC4261_FAULT	0x01
#define LTC4261_ALERT	0x02
#define LTC4261_CONTROL	0x03
#define LTC4261_SENSE_H	0x04
#define LTC4261_SENSE_L	0x05
#define LTC4261_ADIN2_H	0x06
#define LTC4261_ADIN2_L	0x07
#define LTC4261_ADIN_H	0x08
#define LTC4261_ADIN_L	0x09

/*
 * Fault register bits
 */
#define FAULT_OV	(1<<0)
#define FAULT_UV	(1<<1)
#define FAULT_OC	(1<<2)

struct ltc4261_data {
	struct i2c_client *client;

	struct mutex update_lock;
	bool valid;
	unsigned long last_updated;	/* in jiffies */

	/* Registers */
	u8 regs[10];
};

static struct ltc4261_data *ltc4261_update_device(struct device *dev)
{
	struct ltc4261_data *data = dev_get_drvdata(dev);
	struct i2c_client *client = data->client;
	struct ltc4261_data *ret = data;

	mutex_lock(&data->update_lock);

	if (time_after(jiffies, data->last_updated + HZ / 4) || !data->valid) {
		int i;

		/* Read registers -- 0x00 to 0x09 */
		for (i = 0; i < ARRAY_SIZE(data->regs); i++) {
			int val;

			val = i2c_smbus_read_byte_data(client, i);
			if (unlikely(val < 0)) {
				dev_dbg(dev,
					"Failed to read ADC value: error %d\n",
					val);
				ret = ERR_PTR(val);
				data->valid = false;
				goto abort;
			}
			data->regs[i] = val;
		}
		data->last_updated = jiffies;
		data->valid = true;
	}
abort:
	mutex_unlock(&data->update_lock);
	return ret;
}

/* Return the voltage from the given register in mV or mA */
static int ltc4261_get_value(struct ltc4261_data *data, u8 reg)
{
	u32 val;

	val = (data->regs[reg] << 2) + (data->regs[reg + 1] >> 6);

	switch (reg) {
	case LTC4261_ADIN_H:
	case LTC4261_ADIN2_H:
		/* 2.5mV resolution. Convert to mV. */
		val = val * 25 / 10;
		break;
	case LTC4261_SENSE_H:
		/*
		 * 62.5uV resolution. Convert to current as measured with
		 * an 1 mOhm sense resistor, in mA. If a different sense
		 * resistor is installed, calculate the actual current by
		 * dividing the reported current by the sense resistor value
		 * in mOhm.
		 */
		val = val * 625 / 10;
		break;
	default:
		/* If we get here, the developer messed up */
		WARN_ON_ONCE(1);
		val = 0;
		break;
	}

	return val;
}

static ssize_t ltc4261_value_show(struct device *dev,
				  struct device_attribute *da, char *buf)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(da);
	struct ltc4261_data *data = ltc4261_update_device(dev);
	int value;

	if (IS_ERR(data))
		return PTR_ERR(data);

	value = ltc4261_get_value(data, attr->index);
	return sysfs_emit(buf, "%d\n", value);
}

static ssize_t ltc4261_bool_show(struct device *dev,
				 struct device_attribute *da, char *buf)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(da);
	struct ltc4261_data *data = ltc4261_update_device(dev);
	u8 fault;

	if (IS_ERR(data))
		return PTR_ERR(data);

	fault = data->regs[LTC4261_FAULT] & attr->index;
	if (fault)		/* Clear reported faults in chip register */
		i2c_smbus_write_byte_data(data->client, LTC4261_FAULT, ~fault);

	return sysfs_emit(buf, "%d\n", fault ? 1 : 0);
}

/*
 * Input voltages.
 */
static SENSOR_DEVICE_ATTR_RO(in1_input, ltc4261_value, LTC4261_ADIN_H);
static SENSOR_DEVICE_ATTR_RO(in2_input, ltc4261_value, LTC4261_ADIN2_H);

/*
 * Voltage alarms. The chip has only one set of voltage alarm status bits,
 * triggered by input voltage alarms. In many designs, those alarms are
 * associated with the ADIN2 sensor, due to the proximity of the ADIN2 pin
 * to the OV pin. ADIN2 is, however, not available on all chip variants.
 * To ensure that the alarm condition is reported to the user, report it
 * with both voltage sensors.
 */
static SENSOR_DEVICE_ATTR_RO(in1_min_alarm, ltc4261_bool, FAULT_UV);
static SENSOR_DEVICE_ATTR_RO(in1_max_alarm, ltc4261_bool, FAULT_OV);
static SENSOR_DEVICE_ATTR_RO(in2_min_alarm, ltc4261_bool, FAULT_UV);
static SENSOR_DEVICE_ATTR_RO(in2_max_alarm, ltc4261_bool, FAULT_OV);

/* Currents (via sense resistor) */
static SENSOR_DEVICE_ATTR_RO(curr1_input, ltc4261_value, LTC4261_SENSE_H);

/* Overcurrent alarm */
static SENSOR_DEVICE_ATTR_RO(curr1_max_alarm, ltc4261_bool, FAULT_OC);

static struct attribute *ltc4261_attrs[] = {
	&sensor_dev_attr_in1_input.dev_attr.attr,
	&sensor_dev_attr_in1_min_alarm.dev_attr.attr,
	&sensor_dev_attr_in1_max_alarm.dev_attr.attr,
	&sensor_dev_attr_in2_input.dev_attr.attr,
	&sensor_dev_attr_in2_min_alarm.dev_attr.attr,
	&sensor_dev_attr_in2_max_alarm.dev_attr.attr,

	&sensor_dev_attr_curr1_input.dev_attr.attr,
	&sensor_dev_attr_curr1_max_alarm.dev_attr.attr,

	NULL,
};
ATTRIBUTE_GROUPS(ltc4261);

static int ltc4261_probe(struct i2c_client *client)
{
	struct i2c_adapter *adapter = client->adapter;
	struct device *dev = &client->dev;
	struct ltc4261_data *data;
	struct device *hwmon_dev;

	if (!i2c_check_functionality(adapter, I2C_FUNC_SMBUS_BYTE_DATA))
		return -ENODEV;

	if (i2c_smbus_read_byte_data(client, LTC4261_STATUS) < 0) {
		dev_err(dev, "Failed to read status register\n");
		return -ENODEV;
	}

	data = devm_kzalloc(dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->client = client;
	mutex_init(&data->update_lock);

	/* Clear faults */
	i2c_smbus_write_byte_data(client, LTC4261_FAULT, 0x00);

	hwmon_dev = devm_hwmon_device_register_with_groups(dev, client->name,
							   data,
							   ltc4261_groups);
	return PTR_ERR_OR_ZERO(hwmon_dev);
}

static const struct i2c_device_id ltc4261_id[] = {
	{"ltc4261", 0},
	{}
};

MODULE_DEVICE_TABLE(i2c, ltc4261_id);

/* This is the driver that will be inserted */
static struct i2c_driver ltc4261_driver = {
	.driver = {
		   .name = "ltc4261",
		   },
	.probe = ltc4261_probe,
	.id_table = ltc4261_id,
};

module_i2c_driver(ltc4261_driver);

MODULE_AUTHOR("Guenter Roeck <linux@roeck-us.net>");
MODULE_DESCRIPTION("LTC4261 driver");
MODULE_LICENSE("GPL");
