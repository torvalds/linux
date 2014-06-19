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

static inline s16 TEMP_TO_REG(int val)
{
	if (val <= -60000)
		return -60000 * 10 / 625 * 8;
	if (val >= 160000)
		return 160000 * 10 / 625 * 8;
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
	char valid; /* zero until following fields are valid */
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

	if (time_after(jiffies, data->last_updated + HZ)
	 || !data->valid) {
		dev_dbg(&client->dev, "Updating lm92 data\n");
		for (i = 0; i < t_num_regs; i++) {
			data->temp[i] =
				i2c_smbus_read_word_swapped(client, regs[i]);
		}
		data->last_updated = jiffies;
		data->valid = 1;
	}

	mutex_unlock(&data->update_lock);

	return data;
}

static ssize_t show_temp(struct device *dev, struct device_attribute *devattr,
			 char *buf)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	struct lm92_data *data = lm92_update_device(dev);

	return sprintf(buf, "%d\n", TEMP_FROM_REG(data->temp[attr->index]));
}

static ssize_t set_temp(struct device *dev, struct device_attribute *devattr,
			   const char *buf, size_t count)
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

static ssize_t show_temp_hyst(struct device *dev,
			      struct device_attribute *devattr, char *buf)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	struct lm92_data *data = lm92_update_device(dev);
	return sprintf(buf, "%d\n", TEMP_FROM_REG(data->temp[attr->index])
		       - TEMP_FROM_REG(data->temp[t_hyst]));
}

static ssize_t show_temp_min_hyst(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	struct lm92_data *data = lm92_update_device(dev);
	return sprintf(buf, "%d\n", TEMP_FROM_REG(data->temp[t_min])
		       + TEMP_FROM_REG(data->temp[t_hyst]));
}

static ssize_t set_temp_hyst(struct device *dev,
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

	mutex_lock(&data->update_lock);
	data->temp[t_hyst] = TEMP_FROM_REG(data->temp[attr->index]) - val;
	i2c_smbus_write_word_swapped(client, LM92_REG_TEMP_HYST,
				     TEMP_TO_REG(data->temp[t_hyst]));
	mutex_unlock(&data->update_lock);
	return count;
}

static ssize_t show_alarms(struct device *dev, struct device_attribute *attr,
			   char *buf)
{
	struct lm92_data *data = lm92_update_device(dev);
	return sprintf(buf, "%d\n", ALARMS_FROM_REG(data->temp[t_input]));
}

static ssize_t show_alarm(struct device *dev, struct device_attribute *attr,
			  char *buf)
{
	int bitnr = to_sensor_dev_attr(attr)->index;
	struct lm92_data *data = lm92_update_device(dev);
	return sprintf(buf, "%d\n", (data->temp[t_input] >> bitnr) & 1);
}

static SENSOR_DEVICE_ATTR(temp1_input, S_IRUGO, show_temp, NULL, t_input);
static SENSOR_DEVICE_ATTR(temp1_crit, S_IWUSR | S_IRUGO, show_temp, set_temp,
			  t_crit);
static SENSOR_DEVICE_ATTR(temp1_crit_hyst, S_IWUSR | S_IRUGO, show_temp_hyst,
			  set_temp_hyst, t_crit);
static SENSOR_DEVICE_ATTR(temp1_min, S_IWUSR | S_IRUGO, show_temp, set_temp,
			  t_min);
static DEVICE_ATTR(temp1_min_hyst, S_IRUGO, show_temp_min_hyst, NULL);
static SENSOR_DEVICE_ATTR(temp1_max, S_IWUSR | S_IRUGO, show_temp, set_temp,
			  t_max);
static SENSOR_DEVICE_ATTR(temp1_max_hyst, S_IRUGO, show_temp_hyst, NULL, t_max);
static DEVICE_ATTR(alarms, S_IRUGO, show_alarms, NULL);
static SENSOR_DEVICE_ATTR(temp1_crit_alarm, S_IRUGO, show_alarm, NULL, 2);
static SENSOR_DEVICE_ATTR(temp1_min_alarm, S_IRUGO, show_alarm, NULL, 0);
static SENSOR_DEVICE_ATTR(temp1_max_alarm, S_IRUGO, show_alarm, NULL, 1);

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

/*
 * The MAX6635 has no identification register, so we have to use tricks
 * to identify it reliably. This is somewhat slow.
 * Note that we do NOT rely on the 2 MSB of the configuration register
 * always reading 0, as suggested by the datasheet, because it was once
 * reported not to be true.
 */
static int max6635_check(struct i2c_client *client)
{
	u16 temp_low, temp_high, temp_hyst, temp_crit;
	u8 conf;
	int i;

	/*
	 * No manufacturer ID register, so a read from this address will
	 * always return the last read value.
	 */
	temp_low = i2c_smbus_read_word_data(client, LM92_REG_TEMP_LOW);
	if (i2c_smbus_read_word_data(client, LM92_REG_MAN_ID) != temp_low)
		return 0;
	temp_high = i2c_smbus_read_word_data(client, LM92_REG_TEMP_HIGH);
	if (i2c_smbus_read_word_data(client, LM92_REG_MAN_ID) != temp_high)
		return 0;

	/* Limits are stored as integer values (signed, 9-bit). */
	if ((temp_low & 0x7f00) || (temp_high & 0x7f00))
		return 0;
	temp_hyst = i2c_smbus_read_word_data(client, LM92_REG_TEMP_HYST);
	temp_crit = i2c_smbus_read_word_data(client, LM92_REG_TEMP_CRIT);
	if ((temp_hyst & 0x7f00) || (temp_crit & 0x7f00))
		return 0;

	/*
	 * Registers addresses were found to cycle over 16-byte boundaries.
	 * We don't test all registers with all offsets so as to save some
	 * reads and time, but this should still be sufficient to dismiss
	 * non-MAX6635 chips.
	 */
	conf = i2c_smbus_read_byte_data(client, LM92_REG_CONFIG);
	for (i = 16; i < 96; i *= 2) {
		if (temp_hyst != i2c_smbus_read_word_data(client,
				 LM92_REG_TEMP_HYST + i - 16)
		 || temp_crit != i2c_smbus_read_word_data(client,
				 LM92_REG_TEMP_CRIT + i)
		 || temp_low != i2c_smbus_read_word_data(client,
				LM92_REG_TEMP_LOW + i + 16)
		 || temp_high != i2c_smbus_read_word_data(client,
				 LM92_REG_TEMP_HIGH + i + 32)
		 || conf != i2c_smbus_read_byte_data(client,
			    LM92_REG_CONFIG + i))
			return 0;
	}

	return 1;
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
	else if (max6635_check(new_client))
		pr_info("lm92: Found Maxim MAX6635 chip\n");
	else
		return -ENODEV;

	strlcpy(info->type, "lm92", I2C_NAME_SIZE);

	return 0;
}

static int lm92_probe(struct i2c_client *new_client,
		      const struct i2c_device_id *id)
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
	{ "lm92", 0 },
	/* max6635 could be added here */
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
