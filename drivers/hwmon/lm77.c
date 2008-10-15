/*
    lm77.c - Part of lm_sensors, Linux kernel modules for hardware
             monitoring

    Copyright (c) 2004  Andras BALI <drewie@freemail.hu>

    Heavily based on lm75.c by Frodo Looijaard <frodol@dds.nl>.  The LM77
    is a temperature sensor and thermal window comparator with 0.5 deg
    resolution made by National Semiconductor.  Complete datasheet can be
    obtained at their site:
       http://www.national.com/pf/LM/LM77.html

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
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

/* Insmod parameters */
I2C_CLIENT_INSMOD_1(lm77);

/* The LM77 registers */
#define LM77_REG_TEMP		0x00
#define LM77_REG_CONF		0x01
#define LM77_REG_TEMP_HYST	0x02
#define LM77_REG_TEMP_CRIT	0x03
#define LM77_REG_TEMP_MIN	0x04
#define LM77_REG_TEMP_MAX	0x05

/* Each client has this additional data */
struct lm77_data {
	struct device 		*hwmon_dev;
	struct mutex		update_lock;
	char			valid;
	unsigned long		last_updated;	/* In jiffies */
	int			temp_input;	/* Temperatures */
	int			temp_crit;
	int			temp_min;
	int			temp_max;
	int			temp_hyst;
	u8			alarms;
};

static int lm77_probe(struct i2c_client *client,
		      const struct i2c_device_id *id);
static int lm77_detect(struct i2c_client *client, int kind,
		       struct i2c_board_info *info);
static void lm77_init_client(struct i2c_client *client);
static int lm77_remove(struct i2c_client *client);
static u16 lm77_read_value(struct i2c_client *client, u8 reg);
static int lm77_write_value(struct i2c_client *client, u8 reg, u16 value);

static struct lm77_data *lm77_update_device(struct device *dev);


static const struct i2c_device_id lm77_id[] = {
	{ "lm77", lm77 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, lm77_id);

/* This is the driver that will be inserted */
static struct i2c_driver lm77_driver = {
	.class		= I2C_CLASS_HWMON,
	.driver = {
		.name	= "lm77",
	},
	.probe		= lm77_probe,
	.remove		= lm77_remove,
	.id_table	= lm77_id,
	.detect		= lm77_detect,
	.address_data	= &addr_data,
};

/* straight from the datasheet */
#define LM77_TEMP_MIN (-55000)
#define LM77_TEMP_MAX 125000

/* In the temperature registers, the low 3 bits are not part of the
   temperature values; they are the status bits. */
static inline s16 LM77_TEMP_TO_REG(int temp)
{
	int ntemp = SENSORS_LIMIT(temp, LM77_TEMP_MIN, LM77_TEMP_MAX);
	return (ntemp / 500) * 8;
}

static inline int LM77_TEMP_FROM_REG(s16 reg)
{
	return (reg / 8) * 500;
}

/* sysfs stuff */

/* read routines for temperature limits */
#define show(value)	\
static ssize_t show_##value(struct device *dev, struct device_attribute *attr, char *buf)	\
{								\
	struct lm77_data *data = lm77_update_device(dev);	\
	return sprintf(buf, "%d\n", data->value);		\
}

show(temp_input);
show(temp_crit);
show(temp_min);
show(temp_max);

/* read routines for hysteresis values */
static ssize_t show_temp_crit_hyst(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct lm77_data *data = lm77_update_device(dev);
	return sprintf(buf, "%d\n", data->temp_crit - data->temp_hyst);
}
static ssize_t show_temp_min_hyst(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct lm77_data *data = lm77_update_device(dev);
	return sprintf(buf, "%d\n", data->temp_min + data->temp_hyst);
}
static ssize_t show_temp_max_hyst(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct lm77_data *data = lm77_update_device(dev);
	return sprintf(buf, "%d\n", data->temp_max - data->temp_hyst);
}

/* write routines */
#define set(value, reg)	\
static ssize_t set_##value(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)	\
{										\
	struct i2c_client *client = to_i2c_client(dev);				\
	struct lm77_data *data = i2c_get_clientdata(client);			\
	long val = simple_strtol(buf, NULL, 10);				\
										\
	mutex_lock(&data->update_lock);						\
	data->value = val;				\
	lm77_write_value(client, reg, LM77_TEMP_TO_REG(data->value));		\
	mutex_unlock(&data->update_lock);					\
	return count;								\
}

set(temp_min, LM77_REG_TEMP_MIN);
set(temp_max, LM77_REG_TEMP_MAX);

/* hysteresis is stored as a relative value on the chip, so it has to be
   converted first */
static ssize_t set_temp_crit_hyst(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct lm77_data *data = i2c_get_clientdata(client);
	unsigned long val = simple_strtoul(buf, NULL, 10);

	mutex_lock(&data->update_lock);
	data->temp_hyst = data->temp_crit - val;
	lm77_write_value(client, LM77_REG_TEMP_HYST,
			 LM77_TEMP_TO_REG(data->temp_hyst));
	mutex_unlock(&data->update_lock);
	return count;
}

/* preserve hysteresis when setting T_crit */
static ssize_t set_temp_crit(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct lm77_data *data = i2c_get_clientdata(client);
	long val = simple_strtoul(buf, NULL, 10);
	int oldcrithyst;
	
	mutex_lock(&data->update_lock);
	oldcrithyst = data->temp_crit - data->temp_hyst;
	data->temp_crit = val;
	data->temp_hyst = data->temp_crit - oldcrithyst;
	lm77_write_value(client, LM77_REG_TEMP_CRIT,
			 LM77_TEMP_TO_REG(data->temp_crit));
	lm77_write_value(client, LM77_REG_TEMP_HYST,
			 LM77_TEMP_TO_REG(data->temp_hyst));
	mutex_unlock(&data->update_lock);
	return count;
}

static ssize_t show_alarm(struct device *dev, struct device_attribute *attr,
			  char *buf)
{
	int bitnr = to_sensor_dev_attr(attr)->index;
	struct lm77_data *data = lm77_update_device(dev);
	return sprintf(buf, "%u\n", (data->alarms >> bitnr) & 1);
}

static DEVICE_ATTR(temp1_input, S_IRUGO,
		   show_temp_input, NULL);
static DEVICE_ATTR(temp1_crit, S_IWUSR | S_IRUGO,
		   show_temp_crit, set_temp_crit);
static DEVICE_ATTR(temp1_min, S_IWUSR | S_IRUGO,
		   show_temp_min, set_temp_min);
static DEVICE_ATTR(temp1_max, S_IWUSR | S_IRUGO,
		   show_temp_max, set_temp_max);

static DEVICE_ATTR(temp1_crit_hyst, S_IWUSR | S_IRUGO,
		   show_temp_crit_hyst, set_temp_crit_hyst);
static DEVICE_ATTR(temp1_min_hyst, S_IRUGO,
		   show_temp_min_hyst, NULL);
static DEVICE_ATTR(temp1_max_hyst, S_IRUGO,
		   show_temp_max_hyst, NULL);

static SENSOR_DEVICE_ATTR(temp1_crit_alarm, S_IRUGO, show_alarm, NULL, 2);
static SENSOR_DEVICE_ATTR(temp1_min_alarm, S_IRUGO, show_alarm, NULL, 0);
static SENSOR_DEVICE_ATTR(temp1_max_alarm, S_IRUGO, show_alarm, NULL, 1);

static struct attribute *lm77_attributes[] = {
	&dev_attr_temp1_input.attr,
	&dev_attr_temp1_crit.attr,
	&dev_attr_temp1_min.attr,
	&dev_attr_temp1_max.attr,
	&dev_attr_temp1_crit_hyst.attr,
	&dev_attr_temp1_min_hyst.attr,
	&dev_attr_temp1_max_hyst.attr,
	&sensor_dev_attr_temp1_crit_alarm.dev_attr.attr,
	&sensor_dev_attr_temp1_min_alarm.dev_attr.attr,
	&sensor_dev_attr_temp1_max_alarm.dev_attr.attr,
	NULL
};

static const struct attribute_group lm77_group = {
	.attrs = lm77_attributes,
};

/* Return 0 if detection is successful, -ENODEV otherwise */
static int lm77_detect(struct i2c_client *new_client, int kind,
		       struct i2c_board_info *info)
{
	struct i2c_adapter *adapter = new_client->adapter;

	if (!i2c_check_functionality(adapter, I2C_FUNC_SMBUS_BYTE_DATA |
				     I2C_FUNC_SMBUS_WORD_DATA))
		return -ENODEV;

	/* Here comes the remaining detection.  Since the LM77 has no
	   register dedicated to identification, we have to rely on the
	   following tricks:

	   1. the high 4 bits represent the sign and thus they should
	      always be the same
	   2. the high 3 bits are unused in the configuration register
	   3. addresses 0x06 and 0x07 return the last read value
	   4. registers cycling over 8-address boundaries

	   Word-sized registers are high-byte first. */
	if (kind < 0) {
		int i, cur, conf, hyst, crit, min, max;

		/* addresses cycling */
		cur = i2c_smbus_read_word_data(new_client, 0);
		conf = i2c_smbus_read_byte_data(new_client, 1);
		hyst = i2c_smbus_read_word_data(new_client, 2);
		crit = i2c_smbus_read_word_data(new_client, 3);
		min = i2c_smbus_read_word_data(new_client, 4);
		max = i2c_smbus_read_word_data(new_client, 5);
		for (i = 8; i <= 0xff; i += 8)
			if (i2c_smbus_read_byte_data(new_client, i + 1) != conf
			    || i2c_smbus_read_word_data(new_client, i + 2) != hyst
			    || i2c_smbus_read_word_data(new_client, i + 3) != crit
			    || i2c_smbus_read_word_data(new_client, i + 4) != min
			    || i2c_smbus_read_word_data(new_client, i + 5) != max)
				return -ENODEV;

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
		cur = i2c_smbus_read_word_data(new_client, 0);
		if (i2c_smbus_read_word_data(new_client, 6) != cur
		    || i2c_smbus_read_word_data(new_client, 7) != cur)
			return -ENODEV;
		hyst = i2c_smbus_read_word_data(new_client, 2);
		if (i2c_smbus_read_word_data(new_client, 6) != hyst
		    || i2c_smbus_read_word_data(new_client, 7) != hyst)
			return -ENODEV;
		min = i2c_smbus_read_word_data(new_client, 4);
		if (i2c_smbus_read_word_data(new_client, 6) != min
		    || i2c_smbus_read_word_data(new_client, 7) != min)
			return -ENODEV;

	}

	strlcpy(info->type, "lm77", I2C_NAME_SIZE);

	return 0;
}

static int lm77_probe(struct i2c_client *new_client,
		      const struct i2c_device_id *id)
{
	struct lm77_data *data;
	int err;

	data = kzalloc(sizeof(struct lm77_data), GFP_KERNEL);
	if (!data) {
		err = -ENOMEM;
		goto exit;
	}

	i2c_set_clientdata(new_client, data);
	data->valid = 0;
	mutex_init(&data->update_lock);

	/* Initialize the LM77 chip */
	lm77_init_client(new_client);

	/* Register sysfs hooks */
	if ((err = sysfs_create_group(&new_client->dev.kobj, &lm77_group)))
		goto exit_free;

	data->hwmon_dev = hwmon_device_register(&new_client->dev);
	if (IS_ERR(data->hwmon_dev)) {
		err = PTR_ERR(data->hwmon_dev);
		goto exit_remove;
	}

	return 0;

exit_remove:
	sysfs_remove_group(&new_client->dev.kobj, &lm77_group);
exit_free:
	kfree(data);
exit:
	return err;
}

static int lm77_remove(struct i2c_client *client)
{
	struct lm77_data *data = i2c_get_clientdata(client);
	hwmon_device_unregister(data->hwmon_dev);
	sysfs_remove_group(&client->dev.kobj, &lm77_group);
	kfree(data);
	return 0;
}

/* All registers are word-sized, except for the configuration register.
   The LM77 uses the high-byte first convention. */
static u16 lm77_read_value(struct i2c_client *client, u8 reg)
{
	if (reg == LM77_REG_CONF)
		return i2c_smbus_read_byte_data(client, reg);
	else
		return swab16(i2c_smbus_read_word_data(client, reg));
}

static int lm77_write_value(struct i2c_client *client, u8 reg, u16 value)
{
	if (reg == LM77_REG_CONF)
		return i2c_smbus_write_byte_data(client, reg, value);
	else
		return i2c_smbus_write_word_data(client, reg, swab16(value));
}

static void lm77_init_client(struct i2c_client *client)
{
	/* Initialize the LM77 chip - turn off shutdown mode */
	int conf = lm77_read_value(client, LM77_REG_CONF);
	if (conf & 1)
		lm77_write_value(client, LM77_REG_CONF, conf & 0xfe);
}

static struct lm77_data *lm77_update_device(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct lm77_data *data = i2c_get_clientdata(client);

	mutex_lock(&data->update_lock);

	if (time_after(jiffies, data->last_updated + HZ + HZ / 2)
	    || !data->valid) {
		dev_dbg(&client->dev, "Starting lm77 update\n");
		data->temp_input =
			LM77_TEMP_FROM_REG(lm77_read_value(client,
							   LM77_REG_TEMP));
		data->temp_hyst =
			LM77_TEMP_FROM_REG(lm77_read_value(client,
							   LM77_REG_TEMP_HYST));
		data->temp_crit =
			LM77_TEMP_FROM_REG(lm77_read_value(client,
							   LM77_REG_TEMP_CRIT));
		data->temp_min =
			LM77_TEMP_FROM_REG(lm77_read_value(client,
							   LM77_REG_TEMP_MIN));
		data->temp_max =
			LM77_TEMP_FROM_REG(lm77_read_value(client,
							   LM77_REG_TEMP_MAX));
		data->alarms =
			lm77_read_value(client, LM77_REG_TEMP) & 0x0007;
		data->last_updated = jiffies;
		data->valid = 1;
	}

	mutex_unlock(&data->update_lock);

	return data;
}

static int __init sensors_lm77_init(void)
{
	return i2c_add_driver(&lm77_driver);
}

static void __exit sensors_lm77_exit(void)
{
	i2c_del_driver(&lm77_driver);
}

MODULE_AUTHOR("Andras BALI <drewie@freemail.hu>");
MODULE_DESCRIPTION("LM77 driver");
MODULE_LICENSE("GPL");

module_init(sensors_lm77_init);
module_exit(sensors_lm77_exit);
