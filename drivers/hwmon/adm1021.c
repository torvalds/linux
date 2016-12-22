/*
 * adm1021.c - Part of lm_sensors, Linux kernel modules for hardware
 *	       monitoring
 * Copyright (c) 1998, 1999  Frodo Looijaard <frodol@dds.nl> and
 *			     Philip Edelbrock <phil@netroedge.com>
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
#include <linux/hwmon.h>
#include <linux/hwmon-sysfs.h>
#include <linux/err.h>
#include <linux/mutex.h>


/* Addresses to scan */
static const unsigned short normal_i2c[] = {
	0x18, 0x19, 0x1a, 0x29, 0x2a, 0x2b, 0x4c, 0x4d, 0x4e, I2C_CLIENT_END };

enum chips {
	adm1021, adm1023, max1617, max1617a, thmc10, lm84, gl523sm, mc1066 };

/* adm1021 constants specified below */

/* The adm1021 registers */
/* Read-only */
/* For nr in 0-1 */
#define ADM1021_REG_TEMP(nr)		(nr)
#define ADM1021_REG_STATUS		0x02
/* 0x41 = AD, 0x49 = TI, 0x4D = Maxim, 0x23 = Genesys , 0x54 = Onsemi */
#define ADM1021_REG_MAN_ID		0xFE
/* ADM1021 = 0x0X, ADM1023 = 0x3X */
#define ADM1021_REG_DEV_ID		0xFF
/* These use different addresses for reading/writing */
#define ADM1021_REG_CONFIG_R		0x03
#define ADM1021_REG_CONFIG_W		0x09
#define ADM1021_REG_CONV_RATE_R		0x04
#define ADM1021_REG_CONV_RATE_W		0x0A
/* These are for the ADM1023's additional precision on the remote temp sensor */
#define ADM1023_REG_REM_TEMP_PREC	0x10
#define ADM1023_REG_REM_OFFSET		0x11
#define ADM1023_REG_REM_OFFSET_PREC	0x12
#define ADM1023_REG_REM_TOS_PREC	0x13
#define ADM1023_REG_REM_THYST_PREC	0x14
/* limits */
/* For nr in 0-1 */
#define ADM1021_REG_TOS_R(nr)		(0x05 + 2 * (nr))
#define ADM1021_REG_TOS_W(nr)		(0x0B + 2 * (nr))
#define ADM1021_REG_THYST_R(nr)		(0x06 + 2 * (nr))
#define ADM1021_REG_THYST_W(nr)		(0x0C + 2 * (nr))
/* write-only */
#define ADM1021_REG_ONESHOT		0x0F

/* Initial values */

/*
 * Note: Even though I left the low and high limits named os and hyst,
 * they don't quite work like a thermostat the way the LM75 does.  I.e.,
 * a lower temp than THYST actually triggers an alarm instead of
 * clearing it.  Weird, ey?   --Phil
 */

/* Each client has this additional data */
struct adm1021_data {
	struct i2c_client *client;
	enum chips type;

	const struct attribute_group *groups[3];

	struct mutex update_lock;
	char valid;		/* !=0 if following fields are valid */
	char low_power;		/* !=0 if device in low power mode */
	unsigned long last_updated;	/* In jiffies */

	int temp_max[2];		/* Register values */
	int temp_min[2];
	int temp[2];
	u8 alarms;
	/* Special values for ADM1023 only */
	u8 remote_temp_offset;
	u8 remote_temp_offset_prec;
};

/* (amalysh) read only mode, otherwise any limit's writing confuse BIOS */
static bool read_only;

static struct adm1021_data *adm1021_update_device(struct device *dev)
{
	struct adm1021_data *data = dev_get_drvdata(dev);
	struct i2c_client *client = data->client;

	mutex_lock(&data->update_lock);

	if (time_after(jiffies, data->last_updated + HZ + HZ / 2)
	    || !data->valid) {
		int i;

		dev_dbg(dev, "Starting adm1021 update\n");

		for (i = 0; i < 2; i++) {
			data->temp[i] = 1000 *
				(s8) i2c_smbus_read_byte_data(
					client, ADM1021_REG_TEMP(i));
			data->temp_max[i] = 1000 *
				(s8) i2c_smbus_read_byte_data(
					client, ADM1021_REG_TOS_R(i));
			if (data->type != lm84) {
				data->temp_min[i] = 1000 *
				  (s8) i2c_smbus_read_byte_data(client,
							ADM1021_REG_THYST_R(i));
			}
		}
		data->alarms = i2c_smbus_read_byte_data(client,
						ADM1021_REG_STATUS) & 0x7c;
		if (data->type == adm1023) {
			/*
			 * The ADM1023 provides 3 extra bits of precision for
			 * the remote sensor in extra registers.
			 */
			data->temp[1] += 125 * (i2c_smbus_read_byte_data(
				client, ADM1023_REG_REM_TEMP_PREC) >> 5);
			data->temp_max[1] += 125 * (i2c_smbus_read_byte_data(
				client, ADM1023_REG_REM_TOS_PREC) >> 5);
			data->temp_min[1] += 125 * (i2c_smbus_read_byte_data(
				client, ADM1023_REG_REM_THYST_PREC) >> 5);
			data->remote_temp_offset =
				i2c_smbus_read_byte_data(client,
						ADM1023_REG_REM_OFFSET);
			data->remote_temp_offset_prec =
				i2c_smbus_read_byte_data(client,
						ADM1023_REG_REM_OFFSET_PREC);
		}
		data->last_updated = jiffies;
		data->valid = 1;
	}

	mutex_unlock(&data->update_lock);

	return data;
}

static ssize_t show_temp(struct device *dev,
			 struct device_attribute *devattr, char *buf)
{
	int index = to_sensor_dev_attr(devattr)->index;
	struct adm1021_data *data = adm1021_update_device(dev);

	return sprintf(buf, "%d\n", data->temp[index]);
}

static ssize_t show_temp_max(struct device *dev,
			     struct device_attribute *devattr, char *buf)
{
	int index = to_sensor_dev_attr(devattr)->index;
	struct adm1021_data *data = adm1021_update_device(dev);

	return sprintf(buf, "%d\n", data->temp_max[index]);
}

static ssize_t show_temp_min(struct device *dev,
			     struct device_attribute *devattr, char *buf)
{
	int index = to_sensor_dev_attr(devattr)->index;
	struct adm1021_data *data = adm1021_update_device(dev);

	return sprintf(buf, "%d\n", data->temp_min[index]);
}

static ssize_t show_alarm(struct device *dev, struct device_attribute *attr,
			  char *buf)
{
	int index = to_sensor_dev_attr(attr)->index;
	struct adm1021_data *data = adm1021_update_device(dev);
	return sprintf(buf, "%u\n", (data->alarms >> index) & 1);
}

static ssize_t alarms_show(struct device *dev,
			   struct device_attribute *attr,
			   char *buf)
{
	struct adm1021_data *data = adm1021_update_device(dev);
	return sprintf(buf, "%u\n", data->alarms);
}

static ssize_t set_temp_max(struct device *dev,
			    struct device_attribute *devattr,
			    const char *buf, size_t count)
{
	int index = to_sensor_dev_attr(devattr)->index;
	struct adm1021_data *data = dev_get_drvdata(dev);
	struct i2c_client *client = data->client;
	long temp;
	int reg_val, err;

	err = kstrtol(buf, 10, &temp);
	if (err)
		return err;
	temp /= 1000;

	mutex_lock(&data->update_lock);
	reg_val = clamp_val(temp, -128, 127);
	data->temp_max[index] = reg_val * 1000;
	if (!read_only)
		i2c_smbus_write_byte_data(client, ADM1021_REG_TOS_W(index),
					  reg_val);
	mutex_unlock(&data->update_lock);

	return count;
}

static ssize_t set_temp_min(struct device *dev,
			    struct device_attribute *devattr,
			    const char *buf, size_t count)
{
	int index = to_sensor_dev_attr(devattr)->index;
	struct adm1021_data *data = dev_get_drvdata(dev);
	struct i2c_client *client = data->client;
	long temp;
	int reg_val, err;

	err = kstrtol(buf, 10, &temp);
	if (err)
		return err;
	temp /= 1000;

	mutex_lock(&data->update_lock);
	reg_val = clamp_val(temp, -128, 127);
	data->temp_min[index] = reg_val * 1000;
	if (!read_only)
		i2c_smbus_write_byte_data(client, ADM1021_REG_THYST_W(index),
					  reg_val);
	mutex_unlock(&data->update_lock);

	return count;
}

static ssize_t low_power_show(struct device *dev,
			      struct device_attribute *devattr, char *buf)
{
	struct adm1021_data *data = adm1021_update_device(dev);
	return sprintf(buf, "%d\n", data->low_power);
}

static ssize_t low_power_store(struct device *dev,
			       struct device_attribute *devattr,
			       const char *buf, size_t count)
{
	struct adm1021_data *data = dev_get_drvdata(dev);
	struct i2c_client *client = data->client;
	char low_power;
	unsigned long val;
	int err;

	err = kstrtoul(buf, 10, &val);
	if (err)
		return err;
	low_power = val != 0;

	mutex_lock(&data->update_lock);
	if (low_power != data->low_power) {
		int config = i2c_smbus_read_byte_data(
			client, ADM1021_REG_CONFIG_R);
		data->low_power = low_power;
		i2c_smbus_write_byte_data(client, ADM1021_REG_CONFIG_W,
			(config & 0xBF) | (low_power << 6));
	}
	mutex_unlock(&data->update_lock);

	return count;
}


static SENSOR_DEVICE_ATTR(temp1_input, S_IRUGO, show_temp, NULL, 0);
static SENSOR_DEVICE_ATTR(temp1_max, S_IWUSR | S_IRUGO, show_temp_max,
			  set_temp_max, 0);
static SENSOR_DEVICE_ATTR(temp1_min, S_IWUSR | S_IRUGO, show_temp_min,
			  set_temp_min, 0);
static SENSOR_DEVICE_ATTR(temp2_input, S_IRUGO, show_temp, NULL, 1);
static SENSOR_DEVICE_ATTR(temp2_max, S_IWUSR | S_IRUGO, show_temp_max,
			  set_temp_max, 1);
static SENSOR_DEVICE_ATTR(temp2_min, S_IWUSR | S_IRUGO, show_temp_min,
			  set_temp_min, 1);
static SENSOR_DEVICE_ATTR(temp1_max_alarm, S_IRUGO, show_alarm, NULL, 6);
static SENSOR_DEVICE_ATTR(temp1_min_alarm, S_IRUGO, show_alarm, NULL, 5);
static SENSOR_DEVICE_ATTR(temp2_max_alarm, S_IRUGO, show_alarm, NULL, 4);
static SENSOR_DEVICE_ATTR(temp2_min_alarm, S_IRUGO, show_alarm, NULL, 3);
static SENSOR_DEVICE_ATTR(temp2_fault, S_IRUGO, show_alarm, NULL, 2);

static DEVICE_ATTR_RO(alarms);
static DEVICE_ATTR_RW(low_power);

static struct attribute *adm1021_attributes[] = {
	&sensor_dev_attr_temp1_max.dev_attr.attr,
	&sensor_dev_attr_temp1_input.dev_attr.attr,
	&sensor_dev_attr_temp2_max.dev_attr.attr,
	&sensor_dev_attr_temp2_input.dev_attr.attr,
	&sensor_dev_attr_temp1_max_alarm.dev_attr.attr,
	&sensor_dev_attr_temp2_max_alarm.dev_attr.attr,
	&sensor_dev_attr_temp2_fault.dev_attr.attr,
	&dev_attr_alarms.attr,
	&dev_attr_low_power.attr,
	NULL
};

static const struct attribute_group adm1021_group = {
	.attrs = adm1021_attributes,
};

static struct attribute *adm1021_min_attributes[] = {
	&sensor_dev_attr_temp1_min.dev_attr.attr,
	&sensor_dev_attr_temp2_min.dev_attr.attr,
	&sensor_dev_attr_temp1_min_alarm.dev_attr.attr,
	&sensor_dev_attr_temp2_min_alarm.dev_attr.attr,
	NULL
};

static const struct attribute_group adm1021_min_group = {
	.attrs = adm1021_min_attributes,
};

/* Return 0 if detection is successful, -ENODEV otherwise */
static int adm1021_detect(struct i2c_client *client,
			  struct i2c_board_info *info)
{
	struct i2c_adapter *adapter = client->adapter;
	const char *type_name;
	int conv_rate, status, config, man_id, dev_id;

	if (!i2c_check_functionality(adapter, I2C_FUNC_SMBUS_BYTE_DATA)) {
		pr_debug("detect failed, smbus byte data not supported!\n");
		return -ENODEV;
	}

	status = i2c_smbus_read_byte_data(client, ADM1021_REG_STATUS);
	conv_rate = i2c_smbus_read_byte_data(client,
					     ADM1021_REG_CONV_RATE_R);
	config = i2c_smbus_read_byte_data(client, ADM1021_REG_CONFIG_R);

	/* Check unused bits */
	if ((status & 0x03) || (config & 0x3F) || (conv_rate & 0xF8)) {
		pr_debug("detect failed, chip not detected!\n");
		return -ENODEV;
	}

	/* Determine the chip type. */
	man_id = i2c_smbus_read_byte_data(client, ADM1021_REG_MAN_ID);
	dev_id = i2c_smbus_read_byte_data(client, ADM1021_REG_DEV_ID);

	if (man_id < 0 || dev_id < 0)
		return -ENODEV;

	if (man_id == 0x4d && dev_id == 0x01)
		type_name = "max1617a";
	else if (man_id == 0x41) {
		if ((dev_id & 0xF0) == 0x30)
			type_name = "adm1023";
		else if ((dev_id & 0xF0) == 0x00)
			type_name = "adm1021";
		else
			return -ENODEV;
	} else if (man_id == 0x49)
		type_name = "thmc10";
	else if (man_id == 0x23)
		type_name = "gl523sm";
	else if (man_id == 0x54)
		type_name = "mc1066";
	else {
		int lte, rte, lhi, rhi, llo, rlo;

		/* extra checks for LM84 and MAX1617 to avoid misdetections */

		llo = i2c_smbus_read_byte_data(client, ADM1021_REG_THYST_R(0));
		rlo = i2c_smbus_read_byte_data(client, ADM1021_REG_THYST_R(1));

		/* fail if any of the additional register reads failed */
		if (llo < 0 || rlo < 0)
			return -ENODEV;

		lte = i2c_smbus_read_byte_data(client, ADM1021_REG_TEMP(0));
		rte = i2c_smbus_read_byte_data(client, ADM1021_REG_TEMP(1));
		lhi = i2c_smbus_read_byte_data(client, ADM1021_REG_TOS_R(0));
		rhi = i2c_smbus_read_byte_data(client, ADM1021_REG_TOS_R(1));

		/*
		 * Fail for negative temperatures and negative high limits.
		 * This check also catches read errors on the tested registers.
		 */
		if ((s8)lte < 0 || (s8)rte < 0 || (s8)lhi < 0 || (s8)rhi < 0)
			return -ENODEV;

		/* fail if all registers hold the same value */
		if (lte == rte && lte == lhi && lte == rhi && lte == llo
		    && lte == rlo)
			return -ENODEV;

		/*
		 * LM84 Mfr ID is in a different place,
		 * and it has more unused bits.
		 */
		if (conv_rate == 0x00
		    && (config & 0x7F) == 0x00
		    && (status & 0xAB) == 0x00) {
			type_name = "lm84";
		} else {
			/* fail if low limits are larger than high limits */
			if ((s8)llo > lhi || (s8)rlo > rhi)
				return -ENODEV;
			type_name = "max1617";
		}
	}

	pr_debug("Detected chip %s at adapter %d, address 0x%02x.\n",
		 type_name, i2c_adapter_id(adapter), client->addr);
	strlcpy(info->type, type_name, I2C_NAME_SIZE);

	return 0;
}

static void adm1021_init_client(struct i2c_client *client)
{
	/* Enable ADC and disable suspend mode */
	i2c_smbus_write_byte_data(client, ADM1021_REG_CONFIG_W,
		i2c_smbus_read_byte_data(client, ADM1021_REG_CONFIG_R) & 0xBF);
	/* Set Conversion rate to 1/sec (this can be tinkered with) */
	i2c_smbus_write_byte_data(client, ADM1021_REG_CONV_RATE_W, 0x04);
}

static int adm1021_probe(struct i2c_client *client,
			 const struct i2c_device_id *id)
{
	struct device *dev = &client->dev;
	struct adm1021_data *data;
	struct device *hwmon_dev;

	data = devm_kzalloc(dev, sizeof(struct adm1021_data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->client = client;
	data->type = id->driver_data;
	mutex_init(&data->update_lock);

	/* Initialize the ADM1021 chip */
	if (data->type != lm84 && !read_only)
		adm1021_init_client(client);

	data->groups[0] = &adm1021_group;
	if (data->type != lm84)
		data->groups[1] = &adm1021_min_group;

	hwmon_dev = devm_hwmon_device_register_with_groups(dev, client->name,
							   data, data->groups);

	return PTR_ERR_OR_ZERO(hwmon_dev);
}

static const struct i2c_device_id adm1021_id[] = {
	{ "adm1021", adm1021 },
	{ "adm1023", adm1023 },
	{ "max1617", max1617 },
	{ "max1617a", max1617a },
	{ "thmc10", thmc10 },
	{ "lm84", lm84 },
	{ "gl523sm", gl523sm },
	{ "mc1066", mc1066 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, adm1021_id);

static struct i2c_driver adm1021_driver = {
	.class		= I2C_CLASS_HWMON,
	.driver = {
		.name	= "adm1021",
	},
	.probe		= adm1021_probe,
	.id_table	= adm1021_id,
	.detect		= adm1021_detect,
	.address_list	= normal_i2c,
};

module_i2c_driver(adm1021_driver);

MODULE_AUTHOR("Frodo Looijaard <frodol@dds.nl> and "
		"Philip Edelbrock <phil@netroedge.com>");
MODULE_DESCRIPTION("adm1021 driver");
MODULE_LICENSE("GPL");

module_param(read_only, bool, 0);
MODULE_PARM_DESC(read_only, "Don't set any values, read only mode");
