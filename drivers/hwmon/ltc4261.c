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
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/err.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/hwmon.h>
#include <linux/hwmon-sysfs.h>

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
	struct device *hwmon_dev;

	struct mutex update_lock;
	bool valid;
	unsigned long last_updated;	/* in jiffies */

	/* Registers */
	u8 regs[10];
};

static struct ltc4261_data *ltc4261_update_device(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct ltc4261_data *data = i2c_get_clientdata(client);
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
				goto abort;
			}
			data->regs[i] = val;
		}
		data->last_updated = jiffies;
		data->valid = 1;
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

static ssize_t ltc4261_show_value(struct device *dev,
				  struct device_attribute *da, char *buf)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(da);
	struct ltc4261_data *data = ltc4261_update_device(dev);
	int value;

	if (IS_ERR(data))
		return PTR_ERR(data);

	value = ltc4261_get_value(data, attr->index);
	return snprintf(buf, PAGE_SIZE, "%d\n", value);
}

static ssize_t ltc4261_show_bool(struct device *dev,
				 struct device_attribute *da, char *buf)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(da);
	struct i2c_client *client = to_i2c_client(dev);
	struct ltc4261_data *data = ltc4261_update_device(dev);
	u8 fault;

	if (IS_ERR(data))
		return PTR_ERR(data);

	fault = data->regs[LTC4261_FAULT] & attr->index;
	if (fault)		/* Clear reported faults in chip register */
		i2c_smbus_write_byte_data(client, LTC4261_FAULT, ~fault);

	return snprintf(buf, PAGE_SIZE, "%d\n", fault ? 1 : 0);
}

/*
 * These macros are used below in constructing device attribute objects
 * for use with sysfs_create_group() to make a sysfs device file
 * for each register.
 */

#define LTC4261_VALUE(name, ltc4261_cmd_idx) \
	static SENSOR_DEVICE_ATTR(name, S_IRUGO, \
	ltc4261_show_value, NULL, ltc4261_cmd_idx)

#define LTC4261_BOOL(name, mask) \
	static SENSOR_DEVICE_ATTR(name, S_IRUGO, \
	ltc4261_show_bool, NULL, (mask))

/*
 * Input voltages.
 */
LTC4261_VALUE(in1_input, LTC4261_ADIN_H);
LTC4261_VALUE(in2_input, LTC4261_ADIN2_H);

/*
 * Voltage alarms. The chip has only one set of voltage alarm status bits,
 * triggered by input voltage alarms. In many designs, those alarms are
 * associated with the ADIN2 sensor, due to the proximity of the ADIN2 pin
 * to the OV pin. ADIN2 is, however, not available on all chip variants.
 * To ensure that the alarm condition is reported to the user, report it
 * with both voltage sensors.
 */
LTC4261_BOOL(in1_min_alarm, FAULT_UV);
LTC4261_BOOL(in1_max_alarm, FAULT_OV);
LTC4261_BOOL(in2_min_alarm, FAULT_UV);
LTC4261_BOOL(in2_max_alarm, FAULT_OV);

/* Currents (via sense resistor) */
LTC4261_VALUE(curr1_input, LTC4261_SENSE_H);

/* Overcurrent alarm */
LTC4261_BOOL(curr1_max_alarm, FAULT_OC);

static struct attribute *ltc4261_attributes[] = {
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

static const struct attribute_group ltc4261_group = {
	.attrs = ltc4261_attributes,
};

static int ltc4261_probe(struct i2c_client *client,
			 const struct i2c_device_id *id)
{
	struct i2c_adapter *adapter = client->adapter;
	struct ltc4261_data *data;
	int ret;

	if (!i2c_check_functionality(adapter, I2C_FUNC_SMBUS_BYTE_DATA))
		return -ENODEV;

	if (i2c_smbus_read_byte_data(client, LTC4261_STATUS) < 0) {
		dev_err(&client->dev, "Failed to read status register\n");
		return -ENODEV;
	}

	data = kzalloc(sizeof(*data), GFP_KERNEL);
	if (!data) {
		ret = -ENOMEM;
		goto out_kzalloc;
	}

	i2c_set_clientdata(client, data);
	mutex_init(&data->update_lock);

	/* Clear faults */
	i2c_smbus_write_byte_data(client, LTC4261_FAULT, 0x00);

	/* Register sysfs hooks */
	ret = sysfs_create_group(&client->dev.kobj, &ltc4261_group);
	if (ret)
		goto out_sysfs_create_group;

	data->hwmon_dev = hwmon_device_register(&client->dev);
	if (IS_ERR(data->hwmon_dev)) {
		ret = PTR_ERR(data->hwmon_dev);
		goto out_hwmon_device_register;
	}

	return 0;

out_hwmon_device_register:
	sysfs_remove_group(&client->dev.kobj, &ltc4261_group);
out_sysfs_create_group:
	kfree(data);
out_kzalloc:
	return ret;
}

static int ltc4261_remove(struct i2c_client *client)
{
	struct ltc4261_data *data = i2c_get_clientdata(client);

	hwmon_device_unregister(data->hwmon_dev);
	sysfs_remove_group(&client->dev.kobj, &ltc4261_group);

	kfree(data);

	return 0;
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
	.remove = ltc4261_remove,
	.id_table = ltc4261_id,
};

static int __init ltc4261_init(void)
{
	return i2c_add_driver(&ltc4261_driver);
}

static void __exit ltc4261_exit(void)
{
	i2c_del_driver(&ltc4261_driver);
}

MODULE_AUTHOR("Guenter Roeck <guenter.roeck@ericsson.com>");
MODULE_DESCRIPTION("LTC4261 driver");
MODULE_LICENSE("GPL");

module_init(ltc4261_init);
module_exit(ltc4261_exit);
