/*
 * lm92 - Hardware monitoring driver
 * Copyright (C) 2005  Jean Delvare <khali@linux-fr.org>
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
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/hwmon.h>
#include <linux/err.h>
#include <linux/mutex.h>

/* The LM92 and MAX6635 have 2 two-state pins for address selection,
   resulting in 4 possible addresses. */
static unsigned short normal_i2c[] = { 0x48, 0x49, 0x4a, 0x4b,
				       I2C_CLIENT_END };

/* Insmod parameters */
I2C_CLIENT_INSMOD_1(lm92);

/* The LM92 registers */
#define LM92_REG_CONFIG			0x01 /* 8-bit, RW */
#define LM92_REG_TEMP			0x00 /* 16-bit, RO */
#define LM92_REG_TEMP_HYST		0x02 /* 16-bit, RW */
#define LM92_REG_TEMP_CRIT		0x03 /* 16-bit, RW */
#define LM92_REG_TEMP_LOW		0x04 /* 16-bit, RW */
#define LM92_REG_TEMP_HIGH		0x05 /* 16-bit, RW */
#define LM92_REG_MAN_ID			0x07 /* 16-bit, RO, LM92 only */

/* The LM92 uses signed 13-bit values with LSB = 0.0625 degree Celsius,
   left-justified in 16-bit registers. No rounding is done, with such
   a resolution it's just not worth it. Note that the MAX6635 doesn't
   make use of the 4 lower bits for limits (i.e. effective resolution
   for limits is 1 degree Celsius). */
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

/* Driver data (common to all clients) */
static struct i2c_driver lm92_driver;

/* Client data (each client gets its own) */
struct lm92_data {
	struct i2c_client client;
	struct device *hwmon_dev;
	struct mutex update_lock;
	char valid; /* zero until following fields are valid */
	unsigned long last_updated; /* in jiffies */

	/* registers values */
	s16 temp1_input, temp1_crit, temp1_min, temp1_max, temp1_hyst;
};


/*
 * Sysfs attributes and callback functions
 */

static struct lm92_data *lm92_update_device(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct lm92_data *data = i2c_get_clientdata(client);

	mutex_lock(&data->update_lock);

	if (time_after(jiffies, data->last_updated + HZ)
	 || !data->valid) {
		dev_dbg(&client->dev, "Updating lm92 data\n");
		data->temp1_input = swab16(i2c_smbus_read_word_data(client,
				    LM92_REG_TEMP));
		data->temp1_hyst = swab16(i2c_smbus_read_word_data(client,
				    LM92_REG_TEMP_HYST));
		data->temp1_crit = swab16(i2c_smbus_read_word_data(client,
				    LM92_REG_TEMP_CRIT));
		data->temp1_min = swab16(i2c_smbus_read_word_data(client,
				    LM92_REG_TEMP_LOW));
		data->temp1_max = swab16(i2c_smbus_read_word_data(client,
				    LM92_REG_TEMP_HIGH));

		data->last_updated = jiffies;
		data->valid = 1;
	}

	mutex_unlock(&data->update_lock);

	return data;
}

#define show_temp(value) \
static ssize_t show_##value(struct device *dev, struct device_attribute *attr, char *buf) \
{ \
	struct lm92_data *data = lm92_update_device(dev); \
	return sprintf(buf, "%d\n", TEMP_FROM_REG(data->value)); \
}
show_temp(temp1_input);
show_temp(temp1_crit);
show_temp(temp1_min);
show_temp(temp1_max);

#define set_temp(value, reg) \
static ssize_t set_##value(struct device *dev, struct device_attribute *attr, const char *buf, \
	size_t count) \
{ \
	struct i2c_client *client = to_i2c_client(dev); \
	struct lm92_data *data = i2c_get_clientdata(client); \
	long val = simple_strtol(buf, NULL, 10); \
 \
	mutex_lock(&data->update_lock); \
	data->value = TEMP_TO_REG(val); \
	i2c_smbus_write_word_data(client, reg, swab16(data->value)); \
	mutex_unlock(&data->update_lock); \
	return count; \
}
set_temp(temp1_crit, LM92_REG_TEMP_CRIT);
set_temp(temp1_min, LM92_REG_TEMP_LOW);
set_temp(temp1_max, LM92_REG_TEMP_HIGH);

static ssize_t show_temp1_crit_hyst(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct lm92_data *data = lm92_update_device(dev);
	return sprintf(buf, "%d\n", TEMP_FROM_REG(data->temp1_crit)
		       - TEMP_FROM_REG(data->temp1_hyst));
}
static ssize_t show_temp1_max_hyst(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct lm92_data *data = lm92_update_device(dev);
	return sprintf(buf, "%d\n", TEMP_FROM_REG(data->temp1_max)
		       - TEMP_FROM_REG(data->temp1_hyst));
}
static ssize_t show_temp1_min_hyst(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct lm92_data *data = lm92_update_device(dev);
	return sprintf(buf, "%d\n", TEMP_FROM_REG(data->temp1_min)
		       + TEMP_FROM_REG(data->temp1_hyst));
}

static ssize_t set_temp1_crit_hyst(struct device *dev, struct device_attribute *attr, const char *buf,
	size_t count)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct lm92_data *data = i2c_get_clientdata(client);
	long val = simple_strtol(buf, NULL, 10);

	mutex_lock(&data->update_lock);
	data->temp1_hyst = TEMP_FROM_REG(data->temp1_crit) - val;
	i2c_smbus_write_word_data(client, LM92_REG_TEMP_HYST,
				  swab16(TEMP_TO_REG(data->temp1_hyst)));
	mutex_unlock(&data->update_lock);
	return count;
}

static ssize_t show_alarms(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct lm92_data *data = lm92_update_device(dev);
	return sprintf(buf, "%d\n", ALARMS_FROM_REG(data->temp1_input));
}

static DEVICE_ATTR(temp1_input, S_IRUGO, show_temp1_input, NULL);
static DEVICE_ATTR(temp1_crit, S_IWUSR | S_IRUGO, show_temp1_crit,
	set_temp1_crit);
static DEVICE_ATTR(temp1_crit_hyst, S_IWUSR | S_IRUGO, show_temp1_crit_hyst,
	set_temp1_crit_hyst);
static DEVICE_ATTR(temp1_min, S_IWUSR | S_IRUGO, show_temp1_min,
	set_temp1_min);
static DEVICE_ATTR(temp1_min_hyst, S_IRUGO, show_temp1_min_hyst, NULL);
static DEVICE_ATTR(temp1_max, S_IWUSR | S_IRUGO, show_temp1_max,
	set_temp1_max);
static DEVICE_ATTR(temp1_max_hyst, S_IRUGO, show_temp1_max_hyst, NULL);
static DEVICE_ATTR(alarms, S_IRUGO, show_alarms, NULL);


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

/* The MAX6635 has no identification register, so we have to use tricks
   to identify it reliably. This is somewhat slow.
   Note that we do NOT rely on the 2 MSB of the configuration register
   always reading 0, as suggested by the datasheet, because it was once
   reported not to be true. */
static int max6635_check(struct i2c_client *client)
{
	u16 temp_low, temp_high, temp_hyst, temp_crit;
	u8 conf;
	int i;

	/* No manufacturer ID register, so a read from this address will
	   always return the last read value. */
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

	/* Registers addresses were found to cycle over 16-byte boundaries.
	   We don't test all registers with all offsets so as to save some
	   reads and time, but this should still be sufficient to dismiss
	   non-MAX6635 chips. */
	conf = i2c_smbus_read_byte_data(client, LM92_REG_CONFIG);
	for (i=16; i<96; i*=2) {
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

static struct attribute *lm92_attributes[] = {
	&dev_attr_temp1_input.attr,
	&dev_attr_temp1_crit.attr,
	&dev_attr_temp1_crit_hyst.attr,
	&dev_attr_temp1_min.attr,
	&dev_attr_temp1_min_hyst.attr,
	&dev_attr_temp1_max.attr,
	&dev_attr_temp1_max_hyst.attr,
	&dev_attr_alarms.attr,

	NULL
};

static const struct attribute_group lm92_group = {
	.attrs = lm92_attributes,
};

/* The following function does more than just detection. If detection
   succeeds, it also registers the new chip. */
static int lm92_detect(struct i2c_adapter *adapter, int address, int kind)
{
	struct i2c_client *new_client;
	struct lm92_data *data;
	int err = 0;
	char *name;

	if (!i2c_check_functionality(adapter, I2C_FUNC_SMBUS_BYTE_DATA
					    | I2C_FUNC_SMBUS_WORD_DATA))
		goto exit;

	if (!(data = kzalloc(sizeof(struct lm92_data), GFP_KERNEL))) {
		err = -ENOMEM;
		goto exit;
	}

	/* Fill in enough client fields so that we can read from the chip,
	   which is required for identication */
	new_client = &data->client;
	i2c_set_clientdata(new_client, data);
	new_client->addr = address;
	new_client->adapter = adapter;
	new_client->driver = &lm92_driver;
	new_client->flags = 0;

	/* A negative kind means that the driver was loaded with no force
	   parameter (default), so we must identify the chip. */
	if (kind < 0) {
		u8 config = i2c_smbus_read_byte_data(new_client,
			     LM92_REG_CONFIG);
		u16 man_id = i2c_smbus_read_word_data(new_client,
			     LM92_REG_MAN_ID);

		if ((config & 0xe0) == 0x00
		 && man_id == 0x0180) {
			pr_info("lm92: Found National Semiconductor LM92 chip\n");
	 		kind = lm92;
		} else
		if (max6635_check(new_client)) {
			pr_info("lm92: Found Maxim MAX6635 chip\n");
			kind = lm92; /* No separate prefix */
		}
		else
			goto exit_free;
	} else
	if (kind == 0) /* Default to an LM92 if forced */
		kind = lm92;

	/* Give it the proper name */
	if (kind == lm92) {
		name = "lm92";
	} else { /* Supposedly cannot happen */
		dev_dbg(&new_client->dev, "Kind out of range?\n");
		goto exit_free;
	}

	/* Fill in the remaining client fields */
	strlcpy(new_client->name, name, I2C_NAME_SIZE);
	data->valid = 0;
	mutex_init(&data->update_lock);

	/* Tell the i2c subsystem a new client has arrived */
	if ((err = i2c_attach_client(new_client)))
		goto exit_free;

	/* Initialize the chipset */
	lm92_init_client(new_client);

	/* Register sysfs hooks */
	if ((err = sysfs_create_group(&new_client->dev.kobj, &lm92_group)))
		goto exit_detach;

	data->hwmon_dev = hwmon_device_register(&new_client->dev);
	if (IS_ERR(data->hwmon_dev)) {
		err = PTR_ERR(data->hwmon_dev);
		goto exit_remove;
	}

	return 0;

exit_remove:
	sysfs_remove_group(&new_client->dev.kobj, &lm92_group);
exit_detach:
	i2c_detach_client(new_client);
exit_free:
	kfree(data);
exit:
	return err;
}

static int lm92_attach_adapter(struct i2c_adapter *adapter)
{
	if (!(adapter->class & I2C_CLASS_HWMON))
		return 0;
	return i2c_probe(adapter, &addr_data, lm92_detect);
}

static int lm92_detach_client(struct i2c_client *client)
{
	struct lm92_data *data = i2c_get_clientdata(client);
	int err;

	hwmon_device_unregister(data->hwmon_dev);
	sysfs_remove_group(&client->dev.kobj, &lm92_group);

	if ((err = i2c_detach_client(client)))
		return err;

	kfree(data);
	return 0;
}


/*
 * Module and driver stuff
 */

static struct i2c_driver lm92_driver = {
	.driver = {
		.name	= "lm92",
	},
	.attach_adapter	= lm92_attach_adapter,
	.detach_client	= lm92_detach_client,
};

static int __init sensors_lm92_init(void)
{
	return i2c_add_driver(&lm92_driver);
}

static void __exit sensors_lm92_exit(void)
{
	i2c_del_driver(&lm92_driver);
}

MODULE_AUTHOR("Jean Delvare <khali@linux-fr.org>");
MODULE_DESCRIPTION("LM92/MAX6635 driver");
MODULE_LICENSE("GPL");

module_init(sensors_lm92_init);
module_exit(sensors_lm92_exit);
