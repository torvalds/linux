/*
    lm75.c - Part of lm_sensors, Linux kernel modules for hardware
             monitoring
    Copyright (c) 1998, 1999  Frodo Looijaard <frodol@dds.nl>

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
#include "lm75.h"


/*
 * This driver handles the LM75 and compatible digital temperature sensors.
 */

enum lm75_type {		/* keep sorted in alphabetical order */
	ds1775,
	ds75,
	lm75,
	lm75a,
	max6625,
	max6626,
	mcp980x,
	stds75,
	tcn75,
	tmp100,
	tmp101,
	tmp105,
	tmp175,
	tmp275,
	tmp75,
};

/* Addresses scanned */
static const unsigned short normal_i2c[] = { 0x48, 0x49, 0x4a, 0x4b, 0x4c,
					0x4d, 0x4e, 0x4f, I2C_CLIENT_END };


/* The LM75 registers */
#define LM75_REG_CONF		0x01
static const u8 LM75_REG_TEMP[3] = {
	0x00,		/* input */
	0x03,		/* max */
	0x02,		/* hyst */
};

/* Each client has this additional data */
struct lm75_data {
	struct device		*hwmon_dev;
	struct mutex		update_lock;
	u8			orig_conf;
	char			valid;		/* !=0 if registers are valid */
	unsigned long		last_updated;	/* In jiffies */
	u16			temp[3];	/* Register values,
						   0 = input
						   1 = max
						   2 = hyst */
};

static int lm75_read_value(struct i2c_client *client, u8 reg);
static int lm75_write_value(struct i2c_client *client, u8 reg, u16 value);
static struct lm75_data *lm75_update_device(struct device *dev);


/*-----------------------------------------------------------------------*/

/* sysfs attributes for hwmon */

static ssize_t show_temp(struct device *dev, struct device_attribute *da,
			 char *buf)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(da);
	struct lm75_data *data = lm75_update_device(dev);
	return sprintf(buf, "%d\n",
		       LM75_TEMP_FROM_REG(data->temp[attr->index]));
}

static ssize_t set_temp(struct device *dev, struct device_attribute *da,
			const char *buf, size_t count)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(da);
	struct i2c_client *client = to_i2c_client(dev);
	struct lm75_data *data = i2c_get_clientdata(client);
	int nr = attr->index;
	long temp = simple_strtol(buf, NULL, 10);

	mutex_lock(&data->update_lock);
	data->temp[nr] = LM75_TEMP_TO_REG(temp);
	lm75_write_value(client, LM75_REG_TEMP[nr], data->temp[nr]);
	mutex_unlock(&data->update_lock);
	return count;
}

static SENSOR_DEVICE_ATTR(temp1_max, S_IWUSR | S_IRUGO,
			show_temp, set_temp, 1);
static SENSOR_DEVICE_ATTR(temp1_max_hyst, S_IWUSR | S_IRUGO,
			show_temp, set_temp, 2);
static SENSOR_DEVICE_ATTR(temp1_input, S_IRUGO, show_temp, NULL, 0);

static struct attribute *lm75_attributes[] = {
	&sensor_dev_attr_temp1_input.dev_attr.attr,
	&sensor_dev_attr_temp1_max.dev_attr.attr,
	&sensor_dev_attr_temp1_max_hyst.dev_attr.attr,

	NULL
};

static const struct attribute_group lm75_group = {
	.attrs = lm75_attributes,
};

/*-----------------------------------------------------------------------*/

/* device probe and removal */

static int
lm75_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	struct lm75_data *data;
	int status;
	u8 set_mask, clr_mask;
	int new;

	if (!i2c_check_functionality(client->adapter,
			I2C_FUNC_SMBUS_BYTE_DATA | I2C_FUNC_SMBUS_WORD_DATA))
		return -EIO;

	data = kzalloc(sizeof(struct lm75_data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	i2c_set_clientdata(client, data);
	mutex_init(&data->update_lock);

	/* Set to LM75 resolution (9 bits, 1/2 degree C) and range.
	 * Then tweak to be more precise when appropriate.
	 */
	set_mask = 0;
	clr_mask = (1 << 0)			/* continuous conversions */
		| (1 << 6) | (1 << 5);		/* 9-bit mode */

	/* configure as specified */
	status = lm75_read_value(client, LM75_REG_CONF);
	if (status < 0) {
		dev_dbg(&client->dev, "Can't read config? %d\n", status);
		goto exit_free;
	}
	data->orig_conf = status;
	new = status & ~clr_mask;
	new |= set_mask;
	if (status != new)
		lm75_write_value(client, LM75_REG_CONF, new);
	dev_dbg(&client->dev, "Config %02x\n", new);

	/* Register sysfs hooks */
	status = sysfs_create_group(&client->dev.kobj, &lm75_group);
	if (status)
		goto exit_free;

	data->hwmon_dev = hwmon_device_register(&client->dev);
	if (IS_ERR(data->hwmon_dev)) {
		status = PTR_ERR(data->hwmon_dev);
		goto exit_remove;
	}

	dev_info(&client->dev, "%s: sensor '%s'\n",
		 dev_name(data->hwmon_dev), client->name);

	return 0;

exit_remove:
	sysfs_remove_group(&client->dev.kobj, &lm75_group);
exit_free:
	kfree(data);
	return status;
}

static int lm75_remove(struct i2c_client *client)
{
	struct lm75_data *data = i2c_get_clientdata(client);

	hwmon_device_unregister(data->hwmon_dev);
	sysfs_remove_group(&client->dev.kobj, &lm75_group);
	lm75_write_value(client, LM75_REG_CONF, data->orig_conf);
	kfree(data);
	return 0;
}

static const struct i2c_device_id lm75_ids[] = {
	{ "ds1775", ds1775, },
	{ "ds75", ds75, },
	{ "lm75", lm75, },
	{ "lm75a", lm75a, },
	{ "max6625", max6625, },
	{ "max6626", max6626, },
	{ "mcp980x", mcp980x, },
	{ "stds75", stds75, },
	{ "tcn75", tcn75, },
	{ "tmp100", tmp100, },
	{ "tmp101", tmp101, },
	{ "tmp105", tmp105, },
	{ "tmp175", tmp175, },
	{ "tmp275", tmp275, },
	{ "tmp75", tmp75, },
	{ /* LIST END */ }
};
MODULE_DEVICE_TABLE(i2c, lm75_ids);

/* Return 0 if detection is successful, -ENODEV otherwise */
static int lm75_detect(struct i2c_client *new_client,
		       struct i2c_board_info *info)
{
	struct i2c_adapter *adapter = new_client->adapter;
	int i;
	int cur, conf, hyst, os;

	if (!i2c_check_functionality(adapter, I2C_FUNC_SMBUS_BYTE_DATA |
				     I2C_FUNC_SMBUS_WORD_DATA))
		return -ENODEV;

	/* Now, we do the remaining detection. There is no identification-
	   dedicated register so we have to rely on several tricks:
	   unused bits, registers cycling over 8-address boundaries,
	   addresses 0x04-0x07 returning the last read value.
	   The cycling+unused addresses combination is not tested,
	   since it would significantly slow the detection down and would
	   hardly add any value. */

	/* Unused addresses */
	cur = i2c_smbus_read_word_data(new_client, 0);
	conf = i2c_smbus_read_byte_data(new_client, 1);
	hyst = i2c_smbus_read_word_data(new_client, 2);
	if (i2c_smbus_read_word_data(new_client, 4) != hyst
	 || i2c_smbus_read_word_data(new_client, 5) != hyst
	 || i2c_smbus_read_word_data(new_client, 6) != hyst
	 || i2c_smbus_read_word_data(new_client, 7) != hyst)
		return -ENODEV;
	os = i2c_smbus_read_word_data(new_client, 3);
	if (i2c_smbus_read_word_data(new_client, 4) != os
	 || i2c_smbus_read_word_data(new_client, 5) != os
	 || i2c_smbus_read_word_data(new_client, 6) != os
	 || i2c_smbus_read_word_data(new_client, 7) != os)
		return -ENODEV;

	/* Unused bits */
	if (conf & 0xe0)
		return -ENODEV;

	/* Addresses cycling */
	for (i = 8; i < 0xff; i += 8) {
		if (i2c_smbus_read_byte_data(new_client, i + 1) != conf
		 || i2c_smbus_read_word_data(new_client, i + 2) != hyst
		 || i2c_smbus_read_word_data(new_client, i + 3) != os)
			return -ENODEV;
	}

	strlcpy(info->type, "lm75", I2C_NAME_SIZE);

	return 0;
}

#ifdef CONFIG_PM
static int lm75_suspend(struct device *dev)
{
	int status;
	struct i2c_client *client = to_i2c_client(dev);
	status = lm75_read_value(client, LM75_REG_CONF);
	if (status < 0) {
		dev_dbg(&client->dev, "Can't read config? %d\n", status);
		return status;
	}
	status = status | LM75_SHUTDOWN;
	lm75_write_value(client, LM75_REG_CONF, status);
	return 0;
}

static int lm75_resume(struct device *dev)
{
	int status;
	struct i2c_client *client = to_i2c_client(dev);
	status = lm75_read_value(client, LM75_REG_CONF);
	if (status < 0) {
		dev_dbg(&client->dev, "Can't read config? %d\n", status);
		return status;
	}
	status = status & ~LM75_SHUTDOWN;
	lm75_write_value(client, LM75_REG_CONF, status);
	return 0;
}

static const struct dev_pm_ops lm75_dev_pm_ops = {
	.suspend	= lm75_suspend,
	.resume		= lm75_resume,
};
#define LM75_DEV_PM_OPS (&lm75_dev_pm_ops)
#else
#define LM75_DEV_PM_OPS NULL
#endif /* CONFIG_PM */

static struct i2c_driver lm75_driver = {
	.class		= I2C_CLASS_HWMON,
	.driver = {
		.name	= "lm75",
		.pm	= LM75_DEV_PM_OPS,
	},
	.probe		= lm75_probe,
	.remove		= lm75_remove,
	.id_table	= lm75_ids,
	.detect		= lm75_detect,
	.address_list	= normal_i2c,
};

/*-----------------------------------------------------------------------*/

/* register access */

/* All registers are word-sized, except for the configuration register.
   LM75 uses a high-byte first convention, which is exactly opposite to
   the SMBus standard. */
static int lm75_read_value(struct i2c_client *client, u8 reg)
{
	int value;

	if (reg == LM75_REG_CONF)
		return i2c_smbus_read_byte_data(client, reg);

	value = i2c_smbus_read_word_data(client, reg);
	return (value < 0) ? value : swab16(value);
}

static int lm75_write_value(struct i2c_client *client, u8 reg, u16 value)
{
	if (reg == LM75_REG_CONF)
		return i2c_smbus_write_byte_data(client, reg, value);
	else
		return i2c_smbus_write_word_data(client, reg, swab16(value));
}

static struct lm75_data *lm75_update_device(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct lm75_data *data = i2c_get_clientdata(client);

	mutex_lock(&data->update_lock);

	if (time_after(jiffies, data->last_updated + HZ + HZ / 2)
	    || !data->valid) {
		int i;
		dev_dbg(&client->dev, "Starting lm75 update\n");

		for (i = 0; i < ARRAY_SIZE(data->temp); i++) {
			int status;

			status = lm75_read_value(client, LM75_REG_TEMP[i]);
			if (status < 0)
				dev_dbg(&client->dev, "reg %d, err %d\n",
						LM75_REG_TEMP[i], status);
			else
				data->temp[i] = status;
		}
		data->last_updated = jiffies;
		data->valid = 1;
	}

	mutex_unlock(&data->update_lock);

	return data;
}

/*-----------------------------------------------------------------------*/

/* module glue */

static int __init sensors_lm75_init(void)
{
	return i2c_add_driver(&lm75_driver);
}

static void __exit sensors_lm75_exit(void)
{
	i2c_del_driver(&lm75_driver);
}

MODULE_AUTHOR("Frodo Looijaard <frodol@dds.nl>");
MODULE_DESCRIPTION("LM75 driver");
MODULE_LICENSE("GPL");

module_init(sensors_lm75_init);
module_exit(sensors_lm75_exit);
