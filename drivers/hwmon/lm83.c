// SPDX-License-Identifier: GPL-2.0-or-later
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
 * Client data (each client gets its own)
 */

struct lm83_data {
	struct i2c_client *client;
	const struct attribute_group *groups[3];
	struct mutex update_lock;
	char valid; /* zero until following fields are valid */
	unsigned long last_updated; /* in jiffies */

	/* registers values */
	s8 temp[9];	/* 0..3: input 1-4,
			   4..7: high limit 1-4,
			   8   : critical limit */
	u16 alarms; /* bitvector, combined */
};

static struct lm83_data *lm83_update_device(struct device *dev)
{
	struct lm83_data *data = dev_get_drvdata(dev);
	struct i2c_client *client = data->client;

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

/*
 * Sysfs stuff
 */

static ssize_t temp_show(struct device *dev, struct device_attribute *devattr,
			 char *buf)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	struct lm83_data *data = lm83_update_device(dev);
	return sprintf(buf, "%d\n", TEMP_FROM_REG(data->temp[attr->index]));
}

static ssize_t temp_store(struct device *dev,
			  struct device_attribute *devattr, const char *buf,
			  size_t count)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	struct lm83_data *data = dev_get_drvdata(dev);
	struct i2c_client *client = data->client;
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

static ssize_t alarms_show(struct device *dev, struct device_attribute *dummy,
			   char *buf)
{
	struct lm83_data *data = lm83_update_device(dev);
	return sprintf(buf, "%d\n", data->alarms);
}

static ssize_t alarm_show(struct device *dev,
			  struct device_attribute *devattr, char *buf)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	struct lm83_data *data = lm83_update_device(dev);
	int bitnr = attr->index;

	return sprintf(buf, "%d\n", (data->alarms >> bitnr) & 1);
}

static SENSOR_DEVICE_ATTR_RO(temp1_input, temp, 0);
static SENSOR_DEVICE_ATTR_RO(temp2_input, temp, 1);
static SENSOR_DEVICE_ATTR_RO(temp3_input, temp, 2);
static SENSOR_DEVICE_ATTR_RO(temp4_input, temp, 3);
static SENSOR_DEVICE_ATTR_RW(temp1_max, temp, 4);
static SENSOR_DEVICE_ATTR_RW(temp2_max, temp, 5);
static SENSOR_DEVICE_ATTR_RW(temp3_max, temp, 6);
static SENSOR_DEVICE_ATTR_RW(temp4_max, temp, 7);
static SENSOR_DEVICE_ATTR_RO(temp1_crit, temp, 8);
static SENSOR_DEVICE_ATTR_RO(temp2_crit, temp, 8);
static SENSOR_DEVICE_ATTR_RW(temp3_crit, temp, 8);
static SENSOR_DEVICE_ATTR_RO(temp4_crit, temp, 8);

/* Individual alarm files */
static SENSOR_DEVICE_ATTR_RO(temp1_crit_alarm, alarm, 0);
static SENSOR_DEVICE_ATTR_RO(temp3_crit_alarm, alarm, 1);
static SENSOR_DEVICE_ATTR_RO(temp3_fault, alarm, 2);
static SENSOR_DEVICE_ATTR_RO(temp3_max_alarm, alarm, 4);
static SENSOR_DEVICE_ATTR_RO(temp1_max_alarm, alarm, 6);
static SENSOR_DEVICE_ATTR_RO(temp2_crit_alarm, alarm, 8);
static SENSOR_DEVICE_ATTR_RO(temp4_crit_alarm, alarm, 9);
static SENSOR_DEVICE_ATTR_RO(temp4_fault, alarm, 10);
static SENSOR_DEVICE_ATTR_RO(temp4_max_alarm, alarm, 12);
static SENSOR_DEVICE_ATTR_RO(temp2_fault, alarm, 13);
static SENSOR_DEVICE_ATTR_RO(temp2_max_alarm, alarm, 15);
/* Raw alarm file for compatibility */
static DEVICE_ATTR_RO(alarms);

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

static const struct i2c_device_id lm83_id[];

static int lm83_probe(struct i2c_client *new_client)
{
	struct device *hwmon_dev;
	struct lm83_data *data;

	data = devm_kzalloc(&new_client->dev, sizeof(struct lm83_data),
			    GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->client = new_client;
	mutex_init(&data->update_lock);

	/*
	 * Register sysfs hooks
	 * The LM82 can only monitor one external diode which is
	 * at the same register as the LM83 temp3 entry - so we
	 * declare 1 and 3 common, and then 2 and 4 only for the LM83.
	 */
	data->groups[0] = &lm83_group;
	if (i2c_match_id(lm83_id, new_client)->driver_data == lm83)
		data->groups[1] = &lm83_group_opt;

	hwmon_dev = devm_hwmon_device_register_with_groups(&new_client->dev,
							   new_client->name,
							   data, data->groups);
	return PTR_ERR_OR_ZERO(hwmon_dev);
}

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
	.probe_new	= lm83_probe,
	.id_table	= lm83_id,
	.detect		= lm83_detect,
	.address_list	= normal_i2c,
};

module_i2c_driver(lm83_driver);

MODULE_AUTHOR("Jean Delvare <jdelvare@suse.de>");
MODULE_DESCRIPTION("LM83 driver");
MODULE_LICENSE("GPL");
