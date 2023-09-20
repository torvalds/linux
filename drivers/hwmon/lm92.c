// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * lm92 - Hardware monitoring driver
 * Copyright (C) 2005-2008  Jean Delvare <jdelvare@suse.de>
 *
 * Based on the lm90 driver, with some ideas taken from the lm_sensors
 * lm92 driver as well.
 *
 * The LM92 is a sensor chip made by National Semiconductor. It reports
 * its own temperature with a 0.0625 deg resolution and a 0.33 deg
 * accuracy. Complete datasheet can be obtained from National's website
 * at:
 *   http://www.national.com/pf/LM/LM92.html
 *
 * This driver also supports the MAX6635 sensor chip made by Maxim.
 * This chip is compatible with the LM92, but has a lesser accuracy
 * (1.0 deg). Complete datasheet can be obtained from Maxim's website
 * at:
 *   http://www.maxim-ic.com/quick_view2.cfm/qv_pk/3074
 *
 * Since the LM92 was the first chipset supported by this driver, most
 * comments will refer to this chipset, but are actually general and
 * concern all supported chipsets, unless mentioned otherwise.
 *
 * Support could easily be added for the National Semiconductor LM76
 * and Maxim MAX6633 and MAX6634 chips, which are mostly compatible
 * with the LM92.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/hwmon.h>
#include <linux/hwmon-sysfs.h>
#include <linux/err.h>
#include <linux/mutex.h>
#include <linux/jiffies.h>

/*
 * The LM92 and MAX6635 have 2 two-state pins for address selection,
 * resulting in 4 possible addresses.
 */
static const unsigned short normal_i2c[] = { 0x48, 0x49, 0x4a, 0x4b,
						I2C_CLIENT_END };
enum chips { lm92, max6635 };

/* The LM92 registers */
#define LM92_REG_CONFIG			0x01 /* 8-bit, RW */
#define LM92_REG_TEMP			0x00 /* 16-bit, RO */
#define LM92_REG_TEMP_HYST		0x02 /* 16-bit, RW */
#define LM92_REG_TEMP_CRIT		0x03 /* 16-bit, RW */
#define LM92_REG_TEMP_LOW		0x04 /* 16-bit, RW */
#define LM92_REG_TEMP_HIGH		0x05 /* 16-bit, RW */
#define LM92_REG_MAN_ID			0x07 /* 16-bit, RO, LM92 only */

/*
 * The LM92 uses signed 13-bit values with LSB = 0.0625 degree Celsius,
 * left-justified in 16-bit registers. No rounding is done, with such
 * a resolution it's just not worth it. Note that the MAX6635 doesn't
 * make use of the 4 lower bits for limits (i.e. effective resolution
 * for limits is 1 degree Celsius).
 */
static inline int TEMP_FROM_REG(s16 reg)
{
	return reg / 8 * 625 / 10;
}

static inline s16 TEMP_TO_REG(long val)
{
	val = clamp_val(val, -60000, 160000);
	return val * 10 / 625 * 8;
}

/* Alarm flags are stored in the 3 LSB of the temperature register */
static inline u8 ALARMS_FROM_REG(s16 reg)
{
	return reg & 0x0007;
}

enum temp_index {
	t_input,
	t_crit,
	t_min,
	t_max,
	t_hyst,
	t_num_regs
};

static const u8 regs[t_num_regs] = {
	[t_input] = LM92_REG_TEMP,
	[t_crit] = LM92_REG_TEMP_CRIT,
	[t_min] = LM92_REG_TEMP_LOW,
	[t_max] = LM92_REG_TEMP_HIGH,
	[t_hyst] = LM92_REG_TEMP_HYST,
};

/* Client data (each client gets its own) */
struct lm92_data {
	struct i2c_client *client;
	struct mutex update_lock;
	bool valid; /* false until following fields are valid */
	unsigned long last_updated; /* in jiffies */

	/* registers values */
	s16 temp[t_num_regs];	/* index with enum temp_index */
};

/*
 * Sysfs attributes and callback functions
 */

static struct lm92_data *lm92_update_device(struct device *dev)
{
	struct lm92_data *data = dev_get_drvdata(dev);
	struct i2c_client *client = data->client;
	int i;

	mutex_lock(&data->update_lock);

	if (time_after(jiffies, data->last_updated + HZ) ||
	    !data->valid) {
		dev_dbg(&client->dev, "Updating lm92 data\n");
		for (i = 0; i < t_num_regs; i++) {
			data->temp[i] =
				i2c_smbus_read_word_swapped(client, regs[i]);
		}
		data->last_updated = jiffies;
		data->valid = true;
	}

	mutex_unlock(&data->update_lock);

	return data;
}

static ssize_t temp_show(struct device *dev, struct device_attribute *devattr,
			 char *buf)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	struct lm92_data *data = lm92_update_device(dev);

	return sprintf(buf, "%d\n", TEMP_FROM_REG(data->temp[attr->index]));
}

static ssize_t temp_store(struct device *dev,
			  struct device_attribute *devattr, const char *buf,
			  size_t count)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	struct lm92_data *data = dev_get_drvdata(dev);
	struct i2c_client *client = data->client;
	int nr = attr->index;
	long val;
	int err;

	err = kstrtol(buf, 10, &val);
	if (err)
		return err;

	mutex_lock(&data->update_lock);
	data->temp[nr] = TEMP_TO_REG(val);
	i2c_smbus_write_word_swapped(client, regs[nr], data->temp[nr]);
	mutex_unlock(&data->update_lock);
	return count;
}

static ssize_t temp_hyst_show(struct device *dev,
			      struct device_attribute *devattr, char *buf)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	struct lm92_data *data = lm92_update_device(dev);

	return sprintf(buf, "%d\n", TEMP_FROM_REG(data->temp[attr->index])
		       - TEMP_FROM_REG(data->temp[t_hyst]));
}

static ssize_t temp1_min_hyst_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	struct lm92_data *data = lm92_update_device(dev);

	return sprintf(buf, "%d\n", TEMP_FROM_REG(data->temp[t_min])
		       + TEMP_FROM_REG(data->temp[t_hyst]));
}

static ssize_t temp_hyst_store(struct device *dev,
			       struct device_attribute *devattr,
			       const char *buf, size_t count)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	struct lm92_data *data = dev_get_drvdata(dev);
	struct i2c_client *client = data->client;
	long val;
	int err;

	err = kstrtol(buf, 10, &val);
	if (err)
		return err;

	val = clamp_val(val, -120000, 220000);
	mutex_lock(&data->update_lock);
	data->temp[t_hyst] =
		TEMP_TO_REG(TEMP_FROM_REG(data->temp[attr->index]) - val);
	i2c_smbus_write_word_swapped(client, LM92_REG_TEMP_HYST,
				     data->temp[t_hyst]);
	mutex_unlock(&data->update_lock);
	return count;
}

static ssize_t alarms_show(struct device *dev, struct device_attribute *attr,
			   char *buf)
{
	struct lm92_data *data = lm92_update_device(dev);

	return sprintf(buf, "%d\n", ALARMS_FROM_REG(data->temp[t_input]));
}

static ssize_t alarm_show(struct device *dev, struct device_attribute *attr,
			  char *buf)
{
	int bitnr = to_sensor_dev_attr(attr)->index;
	struct lm92_data *data = lm92_update_device(dev);
	return sprintf(buf, "%d\n", (data->temp[t_input] >> bitnr) & 1);
}

static SENSOR_DEVICE_ATTR_RO(temp1_input, temp, t_input);
static SENSOR_DEVICE_ATTR_RW(temp1_crit, temp, t_crit);
static SENSOR_DEVICE_ATTR_RW(temp1_crit_hyst, temp_hyst, t_crit);
static SENSOR_DEVICE_ATTR_RW(temp1_min, temp, t_min);
static DEVICE_ATTR_RO(temp1_min_hyst);
static SENSOR_DEVICE_ATTR_RW(temp1_max, temp, t_max);
static SENSOR_DEVICE_ATTR_RO(temp1_max_hyst, temp_hyst, t_max);
static DEVICE_ATTR_RO(alarms);
static SENSOR_DEVICE_ATTR_RO(temp1_crit_alarm, alarm, 2);
static SENSOR_DEVICE_ATTR_RO(temp1_min_alarm, alarm, 0);
static SENSOR_DEVICE_ATTR_RO(temp1_max_alarm, alarm, 1);

/*
 * Detection and registration
 */

static void lm92_init_client(struct i2c_client *client)
{
	u8 config;

	/* Start the conversions if needed */
	config = i2c_smbus_read_byte_data(client, LM92_REG_CONFIG);
	if (config & 0x01)
		i2c_smbus_write_byte_data(client, LM92_REG_CONFIG,
					  config & 0xFE);
}

static struct attribute *lm92_attrs[] = {
	&sensor_dev_attr_temp1_input.dev_attr.attr,
	&sensor_dev_attr_temp1_crit.dev_attr.attr,
	&sensor_dev_attr_temp1_crit_hyst.dev_attr.attr,
	&sensor_dev_attr_temp1_min.dev_attr.attr,
	&dev_attr_temp1_min_hyst.attr,
	&sensor_dev_attr_temp1_max.dev_attr.attr,
	&sensor_dev_attr_temp1_max_hyst.dev_attr.attr,
	&dev_attr_alarms.attr,
	&sensor_dev_attr_temp1_crit_alarm.dev_attr.attr,
	&sensor_dev_attr_temp1_min_alarm.dev_attr.attr,
	&sensor_dev_attr_temp1_max_alarm.dev_attr.attr,
	NULL
};
ATTRIBUTE_GROUPS(lm92);

/* Return 0 if detection is successful, -ENODEV otherwise */
static int lm92_detect(struct i2c_client *new_client,
		       struct i2c_board_info *info)
{
	struct i2c_adapter *adapter = new_client->adapter;
	u8 config;
	u16 man_id;

	if (!i2c_check_functionality(adapter, I2C_FUNC_SMBUS_BYTE_DATA
					    | I2C_FUNC_SMBUS_WORD_DATA))
		return -ENODEV;

	config = i2c_smbus_read_byte_data(new_client, LM92_REG_CONFIG);
	man_id = i2c_smbus_read_word_data(new_client, LM92_REG_MAN_ID);

	if ((config & 0xe0) == 0x00 && man_id == 0x0180)
		pr_info("lm92: Found National Semiconductor LM92 chip\n");
	else
		return -ENODEV;

	strscpy(info->type, "lm92", I2C_NAME_SIZE);

	return 0;
}

static int lm92_probe(struct i2c_client *new_client)
{
	struct device *hwmon_dev;
	struct lm92_data *data;

	data = devm_kzalloc(&new_client->dev, sizeof(struct lm92_data),
			    GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->client = new_client;
	mutex_init(&data->update_lock);

	/* Initialize the chipset */
	lm92_init_client(new_client);

	hwmon_dev = devm_hwmon_device_register_with_groups(&new_client->dev,
							   new_client->name,
							   data, lm92_groups);
	return PTR_ERR_OR_ZERO(hwmon_dev);
}

/*
 * Module and driver stuff
 */

static const struct i2c_device_id lm92_id[] = {
	{ "lm92", lm92 },
	{ "max6635", max6635 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, lm92_id);

static struct i2c_driver lm92_driver = {
	.class		= I2C_CLASS_HWMON,
	.driver = {
		.name	= "lm92",
	},
	.probe		= lm92_probe,
	.id_table	= lm92_id,
	.detect		= lm92_detect,
	.address_list	= normal_i2c,
};

module_i2c_driver(lm92_driver);

MODULE_AUTHOR("Jean Delvare <jdelvare@suse.de>");
MODULE_DESCRIPTION("LM92/MAX6635 driver");
MODULE_LICENSE("GPL");
