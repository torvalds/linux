/*
 * lm83.c - Part of lm_sensors, Linux kernel modules for hardware
 *          monitoring
 * Copyright (C) 2003-2009  Jean Delvare <jdelvare@suse.de>
 *
 * Heavily inspired from the lm78, lm75 and adm1021 drivers. The LM83 is
 * a sensor chip made by National Semiconductor. It reports up to four
 * temperatures (its own plus up to three external ones) with a 1 deg
 * resolution and a 3-4 deg accuracy. Complete datasheet can be obtained
 * from National's website at:
 *   http://www.national.com/pf/LM/LM83.html
 * Since the datasheet omits to give the chip stepping code, I give it
 * here: 0x03 (at register 0xff).
 *
 * Also supports the LM82 temp sensor, which is basically a stripped down
 * model of the LM83.  Datasheet is here:
 * http://www.national.com/pf/LM/LM82.html
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
#include <linux/jiffies.h>
#include <linux/i2c.h>
#include <linux/hwmon-sysfs.h>
#include <linux/hwmon.h>
#include <linux/err.h>
#include <linux/mutex.h>
#include <linux/sysfs.h>

/*
 * Addresses to scan
 * Address is selected using 2 three-level pins, resulting in 9 possible
 * addresses.
 */

static const unsigned short normal_i2c[] = {
	0x18, 0x19, 0x1a, 0x29, 0x2a, 0x2b, 0x4c, 0x4d, 0x4e, I2C_CLIENT_END };

enum chips { lm83, lm82 };

/*
 * The LM83 registers
 * Manufacturer ID is 0x01 for National Semiconductor.
 */

#define LM83_REG_R_MAN_ID		0xFE
#define LM83_REG_R_CHIP_ID		0xFF
#define LM83_REG_R_CONFIG		0x03
#define LM83_REG_W_CONFIG		0x09
#define LM83_REG_R_STATUS1		0x02
#define LM83_REG_R_STATUS2		0x35
#define LM83_REG_R_LOCAL_TEMP		0x00
#define LM83_REG_R_LOCAL_HIGH		0x05
#define LM83_REG_W_LOCAL_HIGH		0x0B
#define LM83_REG_R_REMOTE1_TEMP		0x30
#define LM83_REG_R_REMOTE1_HIGH		0x38
#define LM83_REG_W_REMOTE1_HIGH		0x50
#define LM83_REG_R_REMOTE2_TEMP		0x01
#define LM83_REG_R_REMOTE2_HIGH		0x07
#define LM83_REG_W_REMOTE2_HIGH		0x0D
#define LM83_REG_R_REMOTE3_TEMP		0x31
#define LM83_REG_R_REMOTE3_HIGH		0x3A
#define LM83_REG_W_REMOTE3_HIGH		0x52
#define LM83_REG_R_TCRIT		0x42
#define LM83_REG_W_TCRIT		0x5A

/*
 * Conversions and various macros
 * The LM83 uses signed 8-bit values with LSB = 1 degree Celsius.
 */

#define TEMP_FROM_REG(val)	((val) * 1000)
#define TEMP_TO_REG(val)	((val) <= -128000 ? -128 : \
				 (val) >= 127000 ? 127 : \
				 (val) < 0 ? ((val) - 500) / 1000 : \
				 ((val) + 500) / 1000)

static const u8 LM83_REG_R_TEMP[] = {
	LM83_REG_R_LOCAL_TEMP,
	LM83_REG_R_REMOTE1_TEMP,
	LM83_REG_R_REMOTE2_TEMP,
	LM83_REG_R_REMOTE3_TEMP,
	LM83_REG_R_LOCAL_HIGH,
	LM83_REG_R_REMOTE1_HIGH,
	LM83_REG_R_REMOTE2_HIGH,
	LM83_REG_R_REMOTE3_HIGH,
	LM83_REG_R_TCRIT,
};

static const u8 LM83_REG_W_HIGH[] = {
	LM83_REG_W_LOCAL_HIGH,
	LM83_REG_W_REMOTE1_HIGH,
	LM83_REG_W_REMOTE2_HIGH,
	LM83_REG_W_REMOTE3_HIGH,
	LM83_REG_W_TCRIT,
};

/*
 * Functions declaration
 */

static int lm83_detect(struct i2c_client *new_client,
		       struct i2c_board_info *info);
static int lm83_probe(struct i2c_client *client,
		      const struct i2c_device_id *id);
static int lm83_remove(struct i2c_client *client);
static struct lm83_data *lm83_update_device(struct device *dev);

/*
 * Driver data (common to all clients)
 */

static const struct i2c_device_id lm83_id[] = {
	{ "lm83", lm83 },
	{ "lm82", lm82 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, lm83_id);

static struct i2c_driver lm83_driver = {
	.class		= I2C_CLASS_HWMON,
	.driver = {
		.name	= "lm83",
	},
	.probe		= lm83_probe,
	.remove		= lm83_remove,
	.id_table	= lm83_id,
	.detect		= lm83_detect,
	.address_list	= normal_i2c,
};

/*
 * Client data (each client gets its own)
 */

struct lm83_data {
	struct device *hwmon_dev;
	struct mutex update_lock;
	char valid; /* zero until following fields are valid */
	unsigned long last_updated; /* in jiffies */

	/* registers values */
	s8 temp[9];	/* 0..3: input 1-4,
			   4..7: high limit 1-4,
			   8   : critical limit */
	u16 alarms; /* bitvector, combined */
};

/*
 * Sysfs stuff
 */

static ssize_t show_temp(struct device *dev, struct device_attribute *devattr,
			 char *buf)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	struct lm83_data *data = lm83_update_device(dev);
	return sprintf(buf, "%d\n", TEMP_FROM_REG(data->temp[attr->index]));
}

static ssize_t set_temp(struct device *dev, struct device_attribute *devattr,
			const char *buf, size_t count)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	struct i2c_client *client = to_i2c_client(dev);
	struct lm83_data *data = i2c_get_clientdata(client);
	long val;
	int nr = attr->index;
	int err;

	err = kstrtol(buf, 10, &val);
	if (err < 0)
		return err;

	mutex_lock(&data->update_lock);
	data->temp[nr] = TEMP_TO_REG(val);
	i2c_smbus_write_byte_data(client, LM83_REG_W_HIGH[nr - 4],
				  data->temp[nr]);
	mutex_unlock(&data->update_lock);
	return count;
}

static ssize_t show_alarms(struct device *dev, struct device_attribute *dummy,
			   char *buf)
{
	struct lm83_data *data = lm83_update_device(dev);
	return sprintf(buf, "%d\n", data->alarms);
}

static ssize_t show_alarm(struct device *dev, struct device_attribute
			  *devattr, char *buf)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	struct lm83_data *data = lm83_update_device(dev);
	int bitnr = attr->index;

	return sprintf(buf, "%d\n", (data->alarms >> bitnr) & 1);
}

static SENSOR_DEVICE_ATTR(temp1_input, S_IRUGO, show_temp, NULL, 0);
static SENSOR_DEVICE_ATTR(temp2_input, S_IRUGO, show_temp, NULL, 1);
static SENSOR_DEVICE_ATTR(temp3_input, S_IRUGO, show_temp, NULL, 2);
static SENSOR_DEVICE_ATTR(temp4_input, S_IRUGO, show_temp, NULL, 3);
static SENSOR_DEVICE_ATTR(temp1_max, S_IWUSR | S_IRUGO, show_temp,
	set_temp, 4);
static SENSOR_DEVICE_ATTR(temp2_max, S_IWUSR | S_IRUGO, show_temp,
	set_temp, 5);
static SENSOR_DEVICE_ATTR(temp3_max, S_IWUSR | S_IRUGO, show_temp,
	set_temp, 6);
static SENSOR_DEVICE_ATTR(temp4_max, S_IWUSR | S_IRUGO, show_temp,
	set_temp, 7);
static SENSOR_DEVICE_ATTR(temp1_crit, S_IRUGO, show_temp, NULL, 8);
static SENSOR_DEVICE_ATTR(temp2_crit, S_IRUGO, show_temp, NULL, 8);
static SENSOR_DEVICE_ATTR(temp3_crit, S_IWUSR | S_IRUGO, show_temp,
	set_temp, 8);
static SENSOR_DEVICE_ATTR(temp4_crit, S_IRUGO, show_temp, NULL, 8);

/* Individual alarm files */
static SENSOR_DEVICE_ATTR(temp1_crit_alarm, S_IRUGO, show_alarm, NULL, 0);
static SENSOR_DEVICE_ATTR(temp3_crit_alarm, S_IRUGO, show_alarm, NULL, 1);
static SENSOR_DEVICE_ATTR(temp3_fault, S_IRUGO, show_alarm, NULL, 2);
static SENSOR_DEVICE_ATTR(temp3_max_alarm, S_IRUGO, show_alarm, NULL, 4);
static SENSOR_DEVICE_ATTR(temp1_max_alarm, S_IRUGO, show_alarm, NULL, 6);
static SENSOR_DEVICE_ATTR(temp2_crit_alarm, S_IRUGO, show_alarm, NULL, 8);
static SENSOR_DEVICE_ATTR(temp4_crit_alarm, S_IRUGO, show_alarm, NULL, 9);
static SENSOR_DEVICE_ATTR(temp4_fault, S_IRUGO, show_alarm, NULL, 10);
static SENSOR_DEVICE_ATTR(temp4_max_alarm, S_IRUGO, show_alarm, NULL, 12);
static SENSOR_DEVICE_ATTR(temp2_fault, S_IRUGO, show_alarm, NULL, 13);
static SENSOR_DEVICE_ATTR(temp2_max_alarm, S_IRUGO, show_alarm, NULL, 15);
/* Raw alarm file for compatibility */
static DEVICE_ATTR(alarms, S_IRUGO, show_alarms, NULL);

static struct attribute *lm83_attributes[] = {
	&sensor_dev_attr_temp1_input.dev_attr.attr,
	&sensor_dev_attr_temp3_input.dev_attr.attr,
	&sensor_dev_attr_temp1_max.dev_attr.attr,
	&sensor_dev_attr_temp3_max.dev_attr.attr,
	&sensor_dev_attr_temp1_crit.dev_attr.attr,
	&sensor_dev_attr_temp3_crit.dev_attr.attr,

	&sensor_dev_attr_temp1_crit_alarm.dev_attr.attr,
	&sensor_dev_attr_temp3_crit_alarm.dev_attr.attr,
	&sensor_dev_attr_temp3_fault.dev_attr.attr,
	&sensor_dev_attr_temp3_max_alarm.dev_attr.attr,
	&sensor_dev_attr_temp1_max_alarm.dev_attr.attr,
	&dev_attr_alarms.attr,
	NULL
};

static const struct attribute_group lm83_group = {
	.attrs = lm83_attributes,
};

static struct attribute *lm83_attributes_opt[] = {
	&sensor_dev_attr_temp2_input.dev_attr.attr,
	&sensor_dev_attr_temp4_input.dev_attr.attr,
	&sensor_dev_attr_temp2_max.dev_attr.attr,
	&sensor_dev_attr_temp4_max.dev_attr.attr,
	&sensor_dev_attr_temp2_crit.dev_attr.attr,
	&sensor_dev_attr_temp4_crit.dev_attr.attr,

	&sensor_dev_attr_temp2_crit_alarm.dev_attr.attr,
	&sensor_dev_attr_temp4_crit_alarm.dev_attr.attr,
	&sensor_dev_attr_temp4_fault.dev_attr.attr,
	&sensor_dev_attr_temp4_max_alarm.dev_attr.attr,
	&sensor_dev_attr_temp2_fault.dev_attr.attr,
	&sensor_dev_attr_temp2_max_alarm.dev_attr.attr,
	NULL
};

static const struct attribute_group lm83_group_opt = {
	.attrs = lm83_attributes_opt,
};

/*
 * Real code
 */

/* Return 0 if detection is successful, -ENODEV otherwise */
static int lm83_detect(struct i2c_client *new_client,
		       struct i2c_board_info *info)
{
	struct i2c_adapter *adapter = new_client->adapter;
	const char *name;
	u8 man_id, chip_id;

	if (!i2c_check_functionality(adapter, I2C_FUNC_SMBUS_BYTE_DATA))
		return -ENODEV;

	/* Detection */
	if ((i2c_smbus_read_byte_data(new_client, LM83_REG_R_STATUS1) & 0xA8) ||
	    (i2c_smbus_read_byte_data(new_client, LM83_REG_R_STATUS2) & 0x48) ||
	    (i2c_smbus_read_byte_data(new_client, LM83_REG_R_CONFIG) & 0x41)) {
		dev_dbg(&adapter->dev, "LM83 detection failed at 0x%02x\n",
			new_client->addr);
		return -ENODEV;
	}

	/* Identification */
	man_id = i2c_smbus_read_byte_data(new_client, LM83_REG_R_MAN_ID);
	if (man_id != 0x01)	/* National Semiconductor */
		return -ENODEV;

	chip_id = i2c_smbus_read_byte_data(new_client, LM83_REG_R_CHIP_ID);
	switch (chip_id) {
	case 0x03:
		name = "lm83";
		break;
	case 0x01:
		name = "lm82";
		break;
	default:
		/* identification failed */
		dev_info(&adapter->dev,
			 "Unsupported chip (man_id=0x%02X, chip_id=0x%02X)\n",
			 man_id, chip_id);
		return -ENODEV;
	}

	strlcpy(info->type, name, I2C_NAME_SIZE);

	return 0;
}

static int lm83_probe(struct i2c_client *new_client,
		      const struct i2c_device_id *id)
{
	struct lm83_data *data;
	int err;

	data = devm_kzalloc(&new_client->dev, sizeof(struct lm83_data),
			    GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	i2c_set_clientdata(new_client, data);
	data->valid = 0;
	mutex_init(&data->update_lock);

	/*
	 * Register sysfs hooks
	 * The LM82 can only monitor one external diode which is
	 * at the same register as the LM83 temp3 entry - so we
	 * declare 1 and 3 common, and then 2 and 4 only for the LM83.
	 */

	err = sysfs_create_group(&new_client->dev.kobj, &lm83_group);
	if (err)
		return err;

	if (id->driver_data == lm83) {
		err = sysfs_create_group(&new_client->dev.kobj,
					 &lm83_group_opt);
		if (err)
			goto exit_remove_files;
	}

	data->hwmon_dev = hwmon_device_register(&new_client->dev);
	if (IS_ERR(data->hwmon_dev)) {
		err = PTR_ERR(data->hwmon_dev);
		goto exit_remove_files;
	}

	return 0;

exit_remove_files:
	sysfs_remove_group(&new_client->dev.kobj, &lm83_group);
	sysfs_remove_group(&new_client->dev.kobj, &lm83_group_opt);
	return err;
}

static int lm83_remove(struct i2c_client *client)
{
	struct lm83_data *data = i2c_get_clientdata(client);

	hwmon_device_unregister(data->hwmon_dev);
	sysfs_remove_group(&client->dev.kobj, &lm83_group);
	sysfs_remove_group(&client->dev.kobj, &lm83_group_opt);

	return 0;
}

static struct lm83_data *lm83_update_device(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct lm83_data *data = i2c_get_clientdata(client);

	mutex_lock(&data->update_lock);

	if (time_after(jiffies, data->last_updated + HZ * 2) || !data->valid) {
		int nr;

		dev_dbg(&client->dev, "Updating lm83 data.\n");
		for (nr = 0; nr < 9; nr++) {
			data->temp[nr] =
			    i2c_smbus_read_byte_data(client,
			    LM83_REG_R_TEMP[nr]);
		}
		data->alarms =
		    i2c_smbus_read_byte_data(client, LM83_REG_R_STATUS1)
		    + (i2c_smbus_read_byte_data(client, LM83_REG_R_STATUS2)
		    << 8);

		data->last_updated = jiffies;
		data->valid = 1;
	}

	mutex_unlock(&data->update_lock);

	return data;
}

module_i2c_driver(lm83_driver);

MODULE_AUTHOR("Jean Delvare <jdelvare@suse.de>");
MODULE_DESCRIPTION("LM83 driver");
MODULE_LICENSE("GPL");
