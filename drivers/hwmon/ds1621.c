// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * ds1621.c - Part of lm_sensors, Linux kernel modules for hardware
 *	      monitoring
 * Christian W. Zuckschwerdt  <zany@triq.net>  2000-11-23
 * based on lm75.c by Frodo Looijaard <frodol@dds.nl>
 * Ported to Linux 2.6 by Aurelien Jarno <aurelien@aurel32.net> with
 * the help of Jean Delvare <jdelvare@suse.de>
 *
 * The DS1621 device is a digital temperature/thermometer with 9-bit
 * resolution, a thermal alarm output (Tout), and user-defined minimum
 * and maximum temperature thresholds (TH and TL).
 *
 * The DS1625, DS1631, DS1721, and DS1731 are pin compatible with the DS1621
 * and similar in operation, with slight variations as noted in the device
 * datasheets (please refer to www.maximintegrated.com for specific
 * device information).
 *
 * Since the DS1621 was the first chipset supported by this driver,
 * most comments will refer to this chipset, but are actually general
 * and concern all supported chipsets, unless mentioned otherwise.
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
#include <linux/sysfs.h>
#include <linux/kernel.h>

/* Supported devices */
enum chips { ds1621, ds1625, ds1631, ds1721, ds1731 };

/* Insmod parameters */
static int polarity = -1;
module_param(polarity, int, 0);
MODULE_PARM_DESC(polarity, "Output's polarity: 0 = active high, 1 = active low");

/*
 * The Configuration/Status register
 *
 * - DS1621:
 *   7    6    5    4    3    2    1    0
 * |Done|THF |TLF |NVB | X  | X  |POL |1SHOT|
 *
 * - DS1625:
 *   7    6    5    4    3    2    1    0
 * |Done|THF |TLF |NVB | 1  | 0  |POL |1SHOT|
 *
 * - DS1631, DS1731:
 *   7    6    5    4    3    2    1    0
 * |Done|THF |TLF |NVB | R1 | R0 |POL |1SHOT|
 *
 * - DS1721:
 *   7    6    5    4    3    2    1    0
 * |Done| X  | X  | U  | R1 | R0 |POL |1SHOT|
 *
 * Where:
 * - 'X' is Reserved
 * - 'U' is Undefined
 */
#define DS1621_REG_CONFIG_NVB		0x10
#define DS1621_REG_CONFIG_RESOL		0x0C
#define DS1621_REG_CONFIG_POLARITY	0x02
#define DS1621_REG_CONFIG_1SHOT		0x01
#define DS1621_REG_CONFIG_DONE		0x80

#define DS1621_REG_CONFIG_RESOL_SHIFT	2

/* ds1721 conversion rates: {C/LSB, time(ms), resolution bit setting} */
static const unsigned short ds1721_convrates[] = {
	94,	/*  9-bits (0.5,  93.75, RES[0..1] = 0 */
	188,	/* 10-bits (0.25, 187.5, RES[0..1] = 1 */
	375,	/* 11-bits (0.125,  375, RES[0..1] = 2 */
	750,	/* 12-bits (0.0625, 750, RES[0..1] = 3 */
};

#define DS1621_CONVERSION_MAX	750
#define DS1625_CONVERSION_MAX	500

#define DS1621_TEMP_MAX	125000
#define DS1621_TEMP_MIN	(-55000)

/* The DS1621 temperature registers */
static const u8 DS1621_REG_TEMP[3] = {
	0xAA,		/* input, word, RO */
	0xA2,		/* min, word, RW */
	0xA1,		/* max, word, RW */
};
#define DS1621_REG_CONF			0xAC /* byte, RW */
#define DS1621_COM_START		0xEE /* no data */
#define DS1721_COM_START		0x51 /* no data */
#define DS1621_COM_STOP			0x22 /* no data */

/* The DS1621 configuration register */
#define DS1621_ALARM_TEMP_HIGH		0x40
#define DS1621_ALARM_TEMP_LOW		0x20

/* Conversions */
#define ALARMS_FROM_REG(val) ((val) & \
			(DS1621_ALARM_TEMP_HIGH | DS1621_ALARM_TEMP_LOW))

/* Each client has this additional data */
struct ds1621_data {
	struct i2c_client *client;
	struct mutex update_lock;
	bool valid;			/* true if following fields are valid */
	unsigned long last_updated;	/* In jiffies */
	enum chips kind;		/* device type */

	u16 temp[3];			/* Register values, word */
	u8 conf;			/* Register encoding, combined */
	u8 zbits;			/* Resolution encoded as number of
					 * zero bits */
	u16 update_interval;		/* Conversion rate in milliseconds */
};

static inline int DS1621_TEMP_FROM_REG(u16 reg)
{
	return DIV_ROUND_CLOSEST(((s16)reg / 16) * 625, 10);
}

/*
 * TEMP: 0.001C/bit (-55C to +125C)
 * REG:
 *  - 1621, 1625: 0.5C/bit, 7 zero-bits
 *  - 1631, 1721, 1731: 0.0625C/bit, 4 zero-bits
 */
static inline u16 DS1621_TEMP_TO_REG(long temp, u8 zbits)
{
	temp = clamp_val(temp, DS1621_TEMP_MIN, DS1621_TEMP_MAX);
	temp = DIV_ROUND_CLOSEST(temp * (1 << (8 - zbits)), 1000) << zbits;
	return temp;
}

static void ds1621_init_client(struct ds1621_data *data,
			       struct i2c_client *client)
{
	u8 conf, new_conf, sreg, resol;

	new_conf = conf = i2c_smbus_read_byte_data(client, DS1621_REG_CONF);
	/* switch to continuous conversion mode */
	new_conf &= ~DS1621_REG_CONFIG_1SHOT;

	/* setup output polarity */
	if (polarity == 0)
		new_conf &= ~DS1621_REG_CONFIG_POLARITY;
	else if (polarity == 1)
		new_conf |= DS1621_REG_CONFIG_POLARITY;

	if (conf != new_conf)
		i2c_smbus_write_byte_data(client, DS1621_REG_CONF, new_conf);

	switch (data->kind) {
	case ds1625:
		data->update_interval = DS1625_CONVERSION_MAX;
		data->zbits = 7;
		sreg = DS1621_COM_START;
		break;
	case ds1631:
	case ds1721:
	case ds1731:
		resol = (new_conf & DS1621_REG_CONFIG_RESOL) >>
			 DS1621_REG_CONFIG_RESOL_SHIFT;
		data->update_interval = ds1721_convrates[resol];
		data->zbits = 7 - resol;
		sreg = DS1721_COM_START;
		break;
	default:
		data->update_interval = DS1621_CONVERSION_MAX;
		data->zbits = 7;
		sreg = DS1621_COM_START;
		break;
	}

	/* start conversion */
	i2c_smbus_write_byte(client, sreg);
}

static struct ds1621_data *ds1621_update_client(struct device *dev)
{
	struct ds1621_data *data = dev_get_drvdata(dev);
	struct i2c_client *client = data->client;
	u8 new_conf;

	mutex_lock(&data->update_lock);

	if (time_after(jiffies, data->last_updated + data->update_interval) ||
	    !data->valid) {
		int i;

		dev_dbg(&client->dev, "Starting ds1621 update\n");

		data->conf = i2c_smbus_read_byte_data(client, DS1621_REG_CONF);

		for (i = 0; i < ARRAY_SIZE(data->temp); i++)
			data->temp[i] = i2c_smbus_read_word_swapped(client,
							 DS1621_REG_TEMP[i]);

		/* reset alarms if necessary */
		new_conf = data->conf;
		if (data->temp[0] > data->temp[1])	/* input > min */
			new_conf &= ~DS1621_ALARM_TEMP_LOW;
		if (data->temp[0] < data->temp[2])	/* input < max */
			new_conf &= ~DS1621_ALARM_TEMP_HIGH;
		if (data->conf != new_conf)
			i2c_smbus_write_byte_data(client, DS1621_REG_CONF,
						  new_conf);

		data->last_updated = jiffies;
		data->valid = true;
	}

	mutex_unlock(&data->update_lock);

	return data;
}

static ssize_t temp_show(struct device *dev, struct device_attribute *da,
			 char *buf)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(da);
	struct ds1621_data *data = ds1621_update_client(dev);
	return sprintf(buf, "%d\n",
		       DS1621_TEMP_FROM_REG(data->temp[attr->index]));
}

static ssize_t temp_store(struct device *dev, struct device_attribute *da,
			  const char *buf, size_t count)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(da);
	struct ds1621_data *data = dev_get_drvdata(dev);
	long val;
	int err;

	err = kstrtol(buf, 10, &val);
	if (err)
		return err;

	mutex_lock(&data->update_lock);
	data->temp[attr->index] = DS1621_TEMP_TO_REG(val, data->zbits);
	i2c_smbus_write_word_swapped(data->client, DS1621_REG_TEMP[attr->index],
				     data->temp[attr->index]);
	mutex_unlock(&data->update_lock);
	return count;
}

static ssize_t alarms_show(struct device *dev, struct device_attribute *da,
			   char *buf)
{
	struct ds1621_data *data = ds1621_update_client(dev);
	return sprintf(buf, "%d\n", ALARMS_FROM_REG(data->conf));
}

static ssize_t alarm_show(struct device *dev, struct device_attribute *da,
			  char *buf)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(da);
	struct ds1621_data *data = ds1621_update_client(dev);
	return sprintf(buf, "%d\n", !!(data->conf & attr->index));
}

static ssize_t update_interval_show(struct device *dev,
				    struct device_attribute *da, char *buf)
{
	struct ds1621_data *data = dev_get_drvdata(dev);
	return sysfs_emit(buf, "%hu\n", data->update_interval);
}

static ssize_t update_interval_store(struct device *dev,
				     struct device_attribute *da,
				     const char *buf, size_t count)
{
	struct ds1621_data *data = dev_get_drvdata(dev);
	struct i2c_client *client = data->client;
	unsigned long convrate;
	s32 err;
	int resol = 0;

	err = kstrtoul(buf, 10, &convrate);
	if (err)
		return err;

	/* Convert rate into resolution bits */
	while (resol < (ARRAY_SIZE(ds1721_convrates) - 1) &&
	       convrate > ds1721_convrates[resol])
		resol++;

	mutex_lock(&data->update_lock);
	data->conf = i2c_smbus_read_byte_data(client, DS1621_REG_CONF);
	data->conf &= ~DS1621_REG_CONFIG_RESOL;
	data->conf |= (resol << DS1621_REG_CONFIG_RESOL_SHIFT);
	i2c_smbus_write_byte_data(client, DS1621_REG_CONF, data->conf);
	data->update_interval = ds1721_convrates[resol];
	data->zbits = 7 - resol;
	mutex_unlock(&data->update_lock);

	return count;
}

static DEVICE_ATTR_RO(alarms);
static DEVICE_ATTR_RW(update_interval);

static SENSOR_DEVICE_ATTR_RO(temp1_input, temp, 0);
static SENSOR_DEVICE_ATTR_RW(temp1_min, temp, 1);
static SENSOR_DEVICE_ATTR_RW(temp1_max, temp, 2);
static SENSOR_DEVICE_ATTR_RO(temp1_min_alarm, alarm, DS1621_ALARM_TEMP_LOW);
static SENSOR_DEVICE_ATTR_RO(temp1_max_alarm, alarm, DS1621_ALARM_TEMP_HIGH);

static struct attribute *ds1621_attributes[] = {
	&sensor_dev_attr_temp1_input.dev_attr.attr,
	&sensor_dev_attr_temp1_min.dev_attr.attr,
	&sensor_dev_attr_temp1_max.dev_attr.attr,
	&sensor_dev_attr_temp1_min_alarm.dev_attr.attr,
	&sensor_dev_attr_temp1_max_alarm.dev_attr.attr,
	&dev_attr_alarms.attr,
	&dev_attr_update_interval.attr,
	NULL
};

static umode_t ds1621_attribute_visible(struct kobject *kobj,
					struct attribute *attr, int index)
{
	struct device *dev = kobj_to_dev(kobj);
	struct ds1621_data *data = dev_get_drvdata(dev);

	if (attr == &dev_attr_update_interval.attr)
		if (data->kind == ds1621 || data->kind == ds1625)
			/* shhh, we're hiding update_interval */
			return 0;
	return attr->mode;
}

static const struct attribute_group ds1621_group = {
	.attrs = ds1621_attributes,
	.is_visible = ds1621_attribute_visible
};
__ATTRIBUTE_GROUPS(ds1621);

static const struct i2c_device_id ds1621_id[];

static int ds1621_probe(struct i2c_client *client)
{
	struct ds1621_data *data;
	struct device *hwmon_dev;

	data = devm_kzalloc(&client->dev, sizeof(struct ds1621_data),
			    GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	mutex_init(&data->update_lock);

	data->kind = i2c_match_id(ds1621_id, client)->driver_data;
	data->client = client;

	/* Initialize the DS1621 chip */
	ds1621_init_client(data, client);

	hwmon_dev = devm_hwmon_device_register_with_groups(&client->dev,
							   client->name, data,
							   ds1621_groups);
	return PTR_ERR_OR_ZERO(hwmon_dev);
}

static const struct i2c_device_id ds1621_id[] = {
	{ "ds1621", ds1621 },
	{ "ds1625", ds1625 },
	{ "ds1631", ds1631 },
	{ "ds1721", ds1721 },
	{ "ds1731", ds1731 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, ds1621_id);

/* This is the driver that will be inserted */
static struct i2c_driver ds1621_driver = {
	.class		= I2C_CLASS_HWMON,
	.driver = {
		.name	= "ds1621",
	},
	.probe_new	= ds1621_probe,
	.id_table	= ds1621_id,
};

module_i2c_driver(ds1621_driver);

MODULE_AUTHOR("Christian W. Zuckschwerdt <zany@triq.net>");
MODULE_DESCRIPTION("DS1621 driver");
MODULE_LICENSE("GPL");
