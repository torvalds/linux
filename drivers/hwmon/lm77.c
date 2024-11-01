// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * lm77.c - Part of lm_sensors, Linux kernel modules for hardware
 *	    monitoring
 *
 * Copyright (c) 2004  Andras BALI <drewie@freemail.hu>
 *
 * Heavily based on lm75.c by Frodo Looijaard <frodol@dds.nl>.  The LM77
 * is a temperature sensor and thermal window comparator with 0.5 deg
 * resolution made by National Semiconductor.  Complete datasheet can be
 * obtained at their site:
 *	http://www.national.com/pf/LM/LM77.html
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

/* Addresses to scan */
static const unsigned short normal_i2c[] = { 0x48, 0x49, 0x4a, 0x4b,
						I2C_CLIENT_END };

/* The LM77 registers */
#define LM77_REG_TEMP		0x00
#define LM77_REG_CONF		0x01
#define LM77_REG_TEMP_HYST	0x02
#define LM77_REG_TEMP_CRIT	0x03
#define LM77_REG_TEMP_MIN	0x04
#define LM77_REG_TEMP_MAX	0x05

enum temp_index {
	t_input = 0,
	t_crit,
	t_min,
	t_max,
	t_hyst,
	t_num_temp
};

static const u8 temp_regs[t_num_temp] = {
	[t_input] = LM77_REG_TEMP,
	[t_min] = LM77_REG_TEMP_MIN,
	[t_max] = LM77_REG_TEMP_MAX,
	[t_crit] = LM77_REG_TEMP_CRIT,
	[t_hyst] = LM77_REG_TEMP_HYST,
};

/* Each client has this additional data */
struct lm77_data {
	struct i2c_client	*client;
	struct mutex		update_lock;
	bool			valid;
	unsigned long		last_updated;	/* In jiffies */
	int			temp[t_num_temp]; /* index using temp_index */
	u8			alarms;
};

/* straight from the datasheet */
#define LM77_TEMP_MIN (-55000)
#define LM77_TEMP_MAX 125000

/*
 * In the temperature registers, the low 3 bits are not part of the
 * temperature values; they are the status bits.
 */
static inline s16 LM77_TEMP_TO_REG(int temp)
{
	return (temp / 500) * 8;
}

static inline int LM77_TEMP_FROM_REG(s16 reg)
{
	return (reg / 8) * 500;
}

/*
 * All registers are word-sized, except for the configuration register.
 * The LM77 uses the high-byte first convention.
 */
static u16 lm77_read_value(struct i2c_client *client, u8 reg)
{
	if (reg == LM77_REG_CONF)
		return i2c_smbus_read_byte_data(client, reg);
	else
		return i2c_smbus_read_word_swapped(client, reg);
}

static int lm77_write_value(struct i2c_client *client, u8 reg, u16 value)
{
	if (reg == LM77_REG_CONF)
		return i2c_smbus_write_byte_data(client, reg, value);
	else
		return i2c_smbus_write_word_swapped(client, reg, value);
}

static struct lm77_data *lm77_update_device(struct device *dev)
{
	struct lm77_data *data = dev_get_drvdata(dev);
	struct i2c_client *client = data->client;
	int i;

	mutex_lock(&data->update_lock);

	if (time_after(jiffies, data->last_updated + HZ + HZ / 2)
	    || !data->valid) {
		dev_dbg(&client->dev, "Starting lm77 update\n");
		for (i = 0; i < t_num_temp; i++) {
			data->temp[i] =
			  LM77_TEMP_FROM_REG(lm77_read_value(client,
							     temp_regs[i]));
		}
		data->alarms =
			lm77_read_value(client, LM77_REG_TEMP) & 0x0007;
		data->last_updated = jiffies;
		data->valid = true;
	}

	mutex_unlock(&data->update_lock);

	return data;
}

/* sysfs stuff */

static ssize_t temp_show(struct device *dev, struct device_attribute *devattr,
			 char *buf)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	struct lm77_data *data = lm77_update_device(dev);

	return sprintf(buf, "%d\n", data->temp[attr->index]);
}

static ssize_t temp_hyst_show(struct device *dev,
			      struct device_attribute *devattr, char *buf)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	struct lm77_data *data = lm77_update_device(dev);
	int nr = attr->index;
	int temp;

	temp = nr == t_min ? data->temp[nr] + data->temp[t_hyst] :
			     data->temp[nr] - data->temp[t_hyst];

	return sprintf(buf, "%d\n", temp);
}

static ssize_t temp_store(struct device *dev,
			  struct device_attribute *devattr, const char *buf,
			  size_t count)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	struct lm77_data *data = dev_get_drvdata(dev);
	struct i2c_client *client = data->client;
	int nr = attr->index;
	long val;
	int err;

	err = kstrtol(buf, 10, &val);
	if (err)
		return err;

	val = clamp_val(val, LM77_TEMP_MIN, LM77_TEMP_MAX);
	mutex_lock(&data->update_lock);
	data->temp[nr] = val;
	lm77_write_value(client, temp_regs[nr], LM77_TEMP_TO_REG(val));
	mutex_unlock(&data->update_lock);
	return count;
}

/*
 * hysteresis is stored as a relative value on the chip, so it has to be
 * converted first.
 */
static ssize_t temp_hyst_store(struct device *dev,
			       struct device_attribute *devattr,
			       const char *buf, size_t count)
{
	struct lm77_data *data = dev_get_drvdata(dev);
	struct i2c_client *client = data->client;
	long val;
	int err;

	err = kstrtol(buf, 10, &val);
	if (err)
		return err;

	mutex_lock(&data->update_lock);
	val = clamp_val(data->temp[t_crit] - val, LM77_TEMP_MIN, LM77_TEMP_MAX);
	data->temp[t_hyst] = val;
	lm77_write_value(client, LM77_REG_TEMP_HYST,
			 LM77_TEMP_TO_REG(data->temp[t_hyst]));
	mutex_unlock(&data->update_lock);
	return count;
}

static ssize_t alarm_show(struct device *dev, struct device_attribute *attr,
			  char *buf)
{
	int bitnr = to_sensor_dev_attr(attr)->index;
	struct lm77_data *data = lm77_update_device(dev);
	return sprintf(buf, "%u\n", (data->alarms >> bitnr) & 1);
}

static SENSOR_DEVICE_ATTR_RO(temp1_input, temp, t_input);
static SENSOR_DEVICE_ATTR_RW(temp1_crit, temp, t_crit);
static SENSOR_DEVICE_ATTR_RW(temp1_min, temp, t_min);
static SENSOR_DEVICE_ATTR_RW(temp1_max, temp, t_max);

static SENSOR_DEVICE_ATTR_RW(temp1_crit_hyst, temp_hyst, t_crit);
static SENSOR_DEVICE_ATTR_RO(temp1_min_hyst, temp_hyst, t_min);
static SENSOR_DEVICE_ATTR_RO(temp1_max_hyst, temp_hyst, t_max);

static SENSOR_DEVICE_ATTR_RO(temp1_crit_alarm, alarm, 2);
static SENSOR_DEVICE_ATTR_RO(temp1_min_alarm, alarm, 0);
static SENSOR_DEVICE_ATTR_RO(temp1_max_alarm, alarm, 1);

static struct attribute *lm77_attrs[] = {
	&sensor_dev_attr_temp1_input.dev_attr.attr,
	&sensor_dev_attr_temp1_crit.dev_attr.attr,
	&sensor_dev_attr_temp1_min.dev_attr.attr,
	&sensor_dev_attr_temp1_max.dev_attr.attr,
	&sensor_dev_attr_temp1_crit_hyst.dev_attr.attr,
	&sensor_dev_attr_temp1_min_hyst.dev_attr.attr,
	&sensor_dev_attr_temp1_max_hyst.dev_attr.attr,
	&sensor_dev_attr_temp1_crit_alarm.dev_attr.attr,
	&sensor_dev_attr_temp1_min_alarm.dev_attr.attr,
	&sensor_dev_attr_temp1_max_alarm.dev_attr.attr,
	NULL
};
ATTRIBUTE_GROUPS(lm77);

/* Return 0 if detection is successful, -ENODEV otherwise */
static int lm77_detect(struct i2c_client *client, struct i2c_board_info *info)
{
	struct i2c_adapter *adapter = client->adapter;
	int i, cur, conf, hyst, crit, min, max;

	if (!i2c_check_functionality(adapter, I2C_FUNC_SMBUS_BYTE_DATA |
				     I2C_FUNC_SMBUS_WORD_DATA))
		return -ENODEV;

	/*
	 * Here comes the remaining detection.  Since the LM77 has no
	 * register dedicated to identification, we have to rely on the
	 * following tricks:
	 *
	 * 1. the high 4 bits represent the sign and thus they should
	 *    always be the same
	 * 2. the high 3 bits are unused in the configuration register
	 * 3. addresses 0x06 and 0x07 return the last read value
	 * 4. registers cycling over 8-address boundaries
	 *
	 * Word-sized registers are high-byte first.
	 */

	/* addresses cycling */
	cur = i2c_smbus_read_word_data(client, 0);
	conf = i2c_smbus_read_byte_data(client, 1);
	hyst = i2c_smbus_read_word_data(client, 2);
	crit = i2c_smbus_read_word_data(client, 3);
	min = i2c_smbus_read_word_data(client, 4);
	max = i2c_smbus_read_word_data(client, 5);
	for (i = 8; i <= 0xff; i += 8) {
		if (i2c_smbus_read_byte_data(client, i + 1) != conf
		 || i2c_smbus_read_word_data(client, i + 2) != hyst
		 || i2c_smbus_read_word_data(client, i + 3) != crit
		 || i2c_smbus_read_word_data(client, i + 4) != min
		 || i2c_smbus_read_word_data(client, i + 5) != max)
			return -ENODEV;
	}

	/* sign bits */
	if (((cur & 0x00f0) != 0xf0 && (cur & 0x00f0) != 0x0)
	 || ((hyst & 0x00f0) != 0xf0 && (hyst & 0x00f0) != 0x0)
	 || ((crit & 0x00f0) != 0xf0 && (crit & 0x00f0) != 0x0)
	 || ((min & 0x00f0) != 0xf0 && (min & 0x00f0) != 0x0)
	 || ((max & 0x00f0) != 0xf0 && (max & 0x00f0) != 0x0))
		return -ENODEV;

	/* unused bits */
	if (conf & 0xe0)
		return -ENODEV;

	/* 0x06 and 0x07 return the last read value */
	cur = i2c_smbus_read_word_data(client, 0);
	if (i2c_smbus_read_word_data(client, 6) != cur
	 || i2c_smbus_read_word_data(client, 7) != cur)
		return -ENODEV;
	hyst = i2c_smbus_read_word_data(client, 2);
	if (i2c_smbus_read_word_data(client, 6) != hyst
	 || i2c_smbus_read_word_data(client, 7) != hyst)
		return -ENODEV;
	min = i2c_smbus_read_word_data(client, 4);
	if (i2c_smbus_read_word_data(client, 6) != min
	 || i2c_smbus_read_word_data(client, 7) != min)
		return -ENODEV;

	strscpy(info->type, "lm77", I2C_NAME_SIZE);

	return 0;
}

static void lm77_init_client(struct i2c_client *client)
{
	/* Initialize the LM77 chip - turn off shutdown mode */
	int conf = lm77_read_value(client, LM77_REG_CONF);
	if (conf & 1)
		lm77_write_value(client, LM77_REG_CONF, conf & 0xfe);
}

static int lm77_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct device *hwmon_dev;
	struct lm77_data *data;

	data = devm_kzalloc(dev, sizeof(struct lm77_data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->client = client;
	mutex_init(&data->update_lock);

	/* Initialize the LM77 chip */
	lm77_init_client(client);

	hwmon_dev = devm_hwmon_device_register_with_groups(dev, client->name,
							   data, lm77_groups);
	return PTR_ERR_OR_ZERO(hwmon_dev);
}

static const struct i2c_device_id lm77_id[] = {
	{ "lm77", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, lm77_id);

/* This is the driver that will be inserted */
static struct i2c_driver lm77_driver = {
	.class		= I2C_CLASS_HWMON,
	.driver = {
		.name	= "lm77",
	},
	.probe_new	= lm77_probe,
	.id_table	= lm77_id,
	.detect		= lm77_detect,
	.address_list	= normal_i2c,
};

module_i2c_driver(lm77_driver);

MODULE_AUTHOR("Andras BALI <drewie@freemail.hu>");
MODULE_DESCRIPTION("LM77 driver");
MODULE_LICENSE("GPL");
