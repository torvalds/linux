/*
    thmc50.c - Part of lm_sensors, Linux kernel modules for hardware
             monitoring
    Copyright (C) 2007 Krzysztof Helt <krzysztof.h1@wp.pl>
    Based on 2.4 driver by Frodo Looijaard <frodol@dds.nl> and
    Philip Edelbrock <phil@netroedge.com>

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
#include <linux/i2c.h>
#include <linux/hwmon.h>
#include <linux/hwmon-sysfs.h>
#include <linux/err.h>
#include <linux/mutex.h>

MODULE_LICENSE("GPL");

/* Addresses to scan */
static const unsigned short normal_i2c[] = { 0x2c, 0x2d, 0x2e, I2C_CLIENT_END };

/* Insmod parameters */
I2C_CLIENT_INSMOD_2(thmc50, adm1022);
I2C_CLIENT_MODULE_PARM(adm1022_temp3, "List of adapter,address pairs "
			"to enable 3rd temperature (ADM1022 only)");

/* Many THMC50 constants specified below */

/* The THMC50 registers */
#define THMC50_REG_CONF				0x40
#define THMC50_REG_COMPANY_ID			0x3E
#define THMC50_REG_DIE_CODE			0x3F
#define THMC50_REG_ANALOG_OUT			0x19
/*
 * The mirror status register cannot be used as
 * reading it does not clear alarms.
 */
#define THMC50_REG_INTR				0x41

static const u8 THMC50_REG_TEMP[] = { 0x27, 0x26, 0x20 };
static const u8 THMC50_REG_TEMP_MIN[] = { 0x3A, 0x38, 0x2C };
static const u8 THMC50_REG_TEMP_MAX[] = { 0x39, 0x37, 0x2B };
static const u8 THMC50_REG_TEMP_CRITICAL[] = { 0x13, 0x14, 0x14 };
static const u8 THMC50_REG_TEMP_DEFAULT[] = { 0x17, 0x18, 0x18 };

#define THMC50_REG_CONF_nFANOFF			0x20
#define THMC50_REG_CONF_PROGRAMMED		0x08

/* Each client has this additional data */
struct thmc50_data {
	struct device *hwmon_dev;

	struct mutex update_lock;
	enum chips type;
	unsigned long last_updated;	/* In jiffies */
	char has_temp3;		/* !=0 if it is ADM1022 in temp3 mode */
	char valid;		/* !=0 if following fields are valid */

	/* Register values */
	s8 temp_input[3];
	s8 temp_max[3];
	s8 temp_min[3];
	s8 temp_critical[3];
	u8 analog_out;
	u8 alarms;
};

static int thmc50_detect(struct i2c_client *client, int kind,
			 struct i2c_board_info *info);
static int thmc50_probe(struct i2c_client *client,
			const struct i2c_device_id *id);
static int thmc50_remove(struct i2c_client *client);
static void thmc50_init_client(struct i2c_client *client);
static struct thmc50_data *thmc50_update_device(struct device *dev);

static const struct i2c_device_id thmc50_id[] = {
	{ "adm1022", adm1022 },
	{ "thmc50", thmc50 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, thmc50_id);

static struct i2c_driver thmc50_driver = {
	.class = I2C_CLASS_HWMON,
	.driver = {
		.name = "thmc50",
	},
	.probe = thmc50_probe,
	.remove = thmc50_remove,
	.id_table = thmc50_id,
	.detect = thmc50_detect,
	.address_data = &addr_data,
};

static ssize_t show_analog_out(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	struct thmc50_data *data = thmc50_update_device(dev);
	return sprintf(buf, "%d\n", data->analog_out);
}

static ssize_t set_analog_out(struct device *dev,
			      struct device_attribute *attr,
			      const char *buf, size_t count)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct thmc50_data *data = i2c_get_clientdata(client);
	int tmp = simple_strtoul(buf, NULL, 10);
	int config;

	mutex_lock(&data->update_lock);
	data->analog_out = SENSORS_LIMIT(tmp, 0, 255);
	i2c_smbus_write_byte_data(client, THMC50_REG_ANALOG_OUT,
				  data->analog_out);

	config = i2c_smbus_read_byte_data(client, THMC50_REG_CONF);
	if (data->analog_out == 0)
		config &= ~THMC50_REG_CONF_nFANOFF;
	else
		config |= THMC50_REG_CONF_nFANOFF;
	i2c_smbus_write_byte_data(client, THMC50_REG_CONF, config);

	mutex_unlock(&data->update_lock);
	return count;
}

/* There is only one PWM mode = DC */
static ssize_t show_pwm_mode(struct device *dev, struct device_attribute *attr,
			     char *buf)
{
	return sprintf(buf, "0\n");
}

/* Temperatures */
static ssize_t show_temp(struct device *dev, struct device_attribute *attr,
			 char *buf)
{
	int nr = to_sensor_dev_attr(attr)->index;
	struct thmc50_data *data = thmc50_update_device(dev);
	return sprintf(buf, "%d\n", data->temp_input[nr] * 1000);
}

static ssize_t show_temp_min(struct device *dev, struct device_attribute *attr,
			     char *buf)
{
	int nr = to_sensor_dev_attr(attr)->index;
	struct thmc50_data *data = thmc50_update_device(dev);
	return sprintf(buf, "%d\n", data->temp_min[nr] * 1000);
}

static ssize_t set_temp_min(struct device *dev, struct device_attribute *attr,
			    const char *buf, size_t count)
{
	int nr = to_sensor_dev_attr(attr)->index;
	struct i2c_client *client = to_i2c_client(dev);
	struct thmc50_data *data = i2c_get_clientdata(client);
	int val = simple_strtol(buf, NULL, 10);

	mutex_lock(&data->update_lock);
	data->temp_min[nr] = SENSORS_LIMIT(val / 1000, -128, 127);
	i2c_smbus_write_byte_data(client, THMC50_REG_TEMP_MIN[nr],
				  data->temp_min[nr]);
	mutex_unlock(&data->update_lock);
	return count;
}

static ssize_t show_temp_max(struct device *dev, struct device_attribute *attr,
			     char *buf)
{
	int nr = to_sensor_dev_attr(attr)->index;
	struct thmc50_data *data = thmc50_update_device(dev);
	return sprintf(buf, "%d\n", data->temp_max[nr] * 1000);
}

static ssize_t set_temp_max(struct device *dev, struct device_attribute *attr,
			    const char *buf, size_t count)
{
	int nr = to_sensor_dev_attr(attr)->index;
	struct i2c_client *client = to_i2c_client(dev);
	struct thmc50_data *data = i2c_get_clientdata(client);
	int val = simple_strtol(buf, NULL, 10);

	mutex_lock(&data->update_lock);
	data->temp_max[nr] = SENSORS_LIMIT(val / 1000, -128, 127);
	i2c_smbus_write_byte_data(client, THMC50_REG_TEMP_MAX[nr],
				  data->temp_max[nr]);
	mutex_unlock(&data->update_lock);
	return count;
}

static ssize_t show_temp_critical(struct device *dev,
				  struct device_attribute *attr,
				  char *buf)
{
	int nr = to_sensor_dev_attr(attr)->index;
	struct thmc50_data *data = thmc50_update_device(dev);
	return sprintf(buf, "%d\n", data->temp_critical[nr] * 1000);
}

static ssize_t show_alarm(struct device *dev, struct device_attribute *attr,
			  char *buf)
{
	int index = to_sensor_dev_attr(attr)->index;
	struct thmc50_data *data = thmc50_update_device(dev);

	return sprintf(buf, "%u\n", (data->alarms >> index) & 1);
}

#define temp_reg(offset)						\
static SENSOR_DEVICE_ATTR(temp##offset##_input, S_IRUGO, show_temp,	\
			NULL, offset - 1);				\
static SENSOR_DEVICE_ATTR(temp##offset##_min, S_IRUGO | S_IWUSR,	\
			show_temp_min, set_temp_min, offset - 1);	\
static SENSOR_DEVICE_ATTR(temp##offset##_max, S_IRUGO | S_IWUSR,	\
			show_temp_max, set_temp_max, offset - 1);	\
static SENSOR_DEVICE_ATTR(temp##offset##_crit, S_IRUGO,			\
			show_temp_critical, NULL, offset - 1);

temp_reg(1);
temp_reg(2);
temp_reg(3);

static SENSOR_DEVICE_ATTR(temp1_alarm, S_IRUGO, show_alarm, NULL, 0);
static SENSOR_DEVICE_ATTR(temp2_alarm, S_IRUGO, show_alarm, NULL, 5);
static SENSOR_DEVICE_ATTR(temp3_alarm, S_IRUGO, show_alarm, NULL, 1);
static SENSOR_DEVICE_ATTR(temp2_fault, S_IRUGO, show_alarm, NULL, 7);
static SENSOR_DEVICE_ATTR(temp3_fault, S_IRUGO, show_alarm, NULL, 2);

static SENSOR_DEVICE_ATTR(pwm1, S_IRUGO | S_IWUSR, show_analog_out,
			  set_analog_out, 0);
static SENSOR_DEVICE_ATTR(pwm1_mode, S_IRUGO, show_pwm_mode, NULL, 0);

static struct attribute *thmc50_attributes[] = {
	&sensor_dev_attr_temp1_max.dev_attr.attr,
	&sensor_dev_attr_temp1_min.dev_attr.attr,
	&sensor_dev_attr_temp1_input.dev_attr.attr,
	&sensor_dev_attr_temp1_crit.dev_attr.attr,
	&sensor_dev_attr_temp1_alarm.dev_attr.attr,
	&sensor_dev_attr_temp2_max.dev_attr.attr,
	&sensor_dev_attr_temp2_min.dev_attr.attr,
	&sensor_dev_attr_temp2_input.dev_attr.attr,
	&sensor_dev_attr_temp2_crit.dev_attr.attr,
	&sensor_dev_attr_temp2_alarm.dev_attr.attr,
	&sensor_dev_attr_temp2_fault.dev_attr.attr,
	&sensor_dev_attr_pwm1.dev_attr.attr,
	&sensor_dev_attr_pwm1_mode.dev_attr.attr,
	NULL
};

static const struct attribute_group thmc50_group = {
	.attrs = thmc50_attributes,
};

/* for ADM1022 3rd temperature mode */
static struct attribute *temp3_attributes[] = {
	&sensor_dev_attr_temp3_max.dev_attr.attr,
	&sensor_dev_attr_temp3_min.dev_attr.attr,
	&sensor_dev_attr_temp3_input.dev_attr.attr,
	&sensor_dev_attr_temp3_crit.dev_attr.attr,
	&sensor_dev_attr_temp3_alarm.dev_attr.attr,
	&sensor_dev_attr_temp3_fault.dev_attr.attr,
	NULL
};

static const struct attribute_group temp3_group = {
	.attrs = temp3_attributes,
};

/* Return 0 if detection is successful, -ENODEV otherwise */
static int thmc50_detect(struct i2c_client *client, int kind,
			 struct i2c_board_info *info)
{
	unsigned company;
	unsigned revision;
	unsigned config;
	struct i2c_adapter *adapter = client->adapter;
	int err = 0;
	const char *type_name;

	if (!i2c_check_functionality(adapter, I2C_FUNC_SMBUS_BYTE_DATA)) {
		pr_debug("thmc50: detect failed, "
			 "smbus byte data not supported!\n");
		return -ENODEV;
	}

	pr_debug("thmc50: Probing for THMC50 at 0x%2X on bus %d\n",
		 client->addr, i2c_adapter_id(client->adapter));

	/* Now, we do the remaining detection. */
	company = i2c_smbus_read_byte_data(client, THMC50_REG_COMPANY_ID);
	revision = i2c_smbus_read_byte_data(client, THMC50_REG_DIE_CODE);
	config = i2c_smbus_read_byte_data(client, THMC50_REG_CONF);

	if (kind == 0)
		kind = thmc50;
	else if (kind < 0) {
		err = -ENODEV;
		if (revision >= 0xc0 && ((config & 0x10) == 0)) {
			if (company == 0x49) {
				kind = thmc50;
				err = 0;
			} else if (company == 0x41) {
				kind = adm1022;
				err = 0;
			}
		}
	}
	if (err == -ENODEV) {
		pr_debug("thmc50: Detection of THMC50/ADM1022 failed\n");
		return err;
	}

	if (kind == adm1022) {
		int id = i2c_adapter_id(client->adapter);
		int i;

		type_name = "adm1022";
		for (i = 0; i + 1 < adm1022_temp3_num; i += 2)
			if (adm1022_temp3[i] == id &&
			    adm1022_temp3[i + 1] == client->addr) {
				/* enable 2nd remote temp */
				config |= (1 << 7);
				i2c_smbus_write_byte_data(client,
							  THMC50_REG_CONF,
							  config);
				break;
			}
	} else {
		type_name = "thmc50";
	}
	pr_debug("thmc50: Detected %s (version %x, revision %x)\n",
		 type_name, (revision >> 4) - 0xc, revision & 0xf);

	strlcpy(info->type, type_name, I2C_NAME_SIZE);

	return 0;
}

static int thmc50_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	struct thmc50_data *data;
	int err;

	data = kzalloc(sizeof(struct thmc50_data), GFP_KERNEL);
	if (!data) {
		pr_debug("thmc50: detect failed, kzalloc failed!\n");
		err = -ENOMEM;
		goto exit;
	}

	i2c_set_clientdata(client, data);
	data->type = id->driver_data;
	mutex_init(&data->update_lock);

	thmc50_init_client(client);

	/* Register sysfs hooks */
	if ((err = sysfs_create_group(&client->dev.kobj, &thmc50_group)))
		goto exit_free;

	/* Register ADM1022 sysfs hooks */
	if (data->has_temp3)
		if ((err = sysfs_create_group(&client->dev.kobj,
					      &temp3_group)))
			goto exit_remove_sysfs_thmc50;

	/* Register a new directory entry with module sensors */
	data->hwmon_dev = hwmon_device_register(&client->dev);
	if (IS_ERR(data->hwmon_dev)) {
		err = PTR_ERR(data->hwmon_dev);
		goto exit_remove_sysfs;
	}

	return 0;

exit_remove_sysfs:
	if (data->has_temp3)
		sysfs_remove_group(&client->dev.kobj, &temp3_group);
exit_remove_sysfs_thmc50:
	sysfs_remove_group(&client->dev.kobj, &thmc50_group);
exit_free:
	kfree(data);
exit:
	return err;
}

static int thmc50_remove(struct i2c_client *client)
{
	struct thmc50_data *data = i2c_get_clientdata(client);

	hwmon_device_unregister(data->hwmon_dev);
	sysfs_remove_group(&client->dev.kobj, &thmc50_group);
	if (data->has_temp3)
		sysfs_remove_group(&client->dev.kobj, &temp3_group);

	kfree(data);

	return 0;
}

static void thmc50_init_client(struct i2c_client *client)
{
	struct thmc50_data *data = i2c_get_clientdata(client);
	int config;

	data->analog_out = i2c_smbus_read_byte_data(client,
						    THMC50_REG_ANALOG_OUT);
	/* set up to at least 1 */
	if (data->analog_out == 0) {
		data->analog_out = 1;
		i2c_smbus_write_byte_data(client, THMC50_REG_ANALOG_OUT,
					  data->analog_out);
	}
	config = i2c_smbus_read_byte_data(client, THMC50_REG_CONF);
	config |= 0x1;	/* start the chip if it is in standby mode */
	if (data->type == adm1022 && (config & (1 << 7)))
		data->has_temp3 = 1;
	i2c_smbus_write_byte_data(client, THMC50_REG_CONF, config);
}

static struct thmc50_data *thmc50_update_device(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct thmc50_data *data = i2c_get_clientdata(client);
	int timeout = HZ / 5 + (data->type == thmc50 ? HZ : 0);

	mutex_lock(&data->update_lock);

	if (time_after(jiffies, data->last_updated + timeout)
	    || !data->valid) {

		int temps = data->has_temp3 ? 3 : 2;
		int i;
		int prog = i2c_smbus_read_byte_data(client, THMC50_REG_CONF);

		prog &= THMC50_REG_CONF_PROGRAMMED;

		for (i = 0; i < temps; i++) {
			data->temp_input[i] = i2c_smbus_read_byte_data(client,
						THMC50_REG_TEMP[i]);
			data->temp_max[i] = i2c_smbus_read_byte_data(client,
						THMC50_REG_TEMP_MAX[i]);
			data->temp_min[i] = i2c_smbus_read_byte_data(client,
						THMC50_REG_TEMP_MIN[i]);
			data->temp_critical[i] =
				i2c_smbus_read_byte_data(client,
					prog ? THMC50_REG_TEMP_CRITICAL[i]
					     : THMC50_REG_TEMP_DEFAULT[i]);
		}
		data->analog_out =
		    i2c_smbus_read_byte_data(client, THMC50_REG_ANALOG_OUT);
		data->alarms =
		    i2c_smbus_read_byte_data(client, THMC50_REG_INTR);
		data->last_updated = jiffies;
		data->valid = 1;
	}

	mutex_unlock(&data->update_lock);

	return data;
}

static int __init sm_thmc50_init(void)
{
	return i2c_add_driver(&thmc50_driver);
}

static void __exit sm_thmc50_exit(void)
{
	i2c_del_driver(&thmc50_driver);
}

MODULE_AUTHOR("Krzysztof Helt <krzysztof.h1@wp.pl>");
MODULE_DESCRIPTION("THMC50 driver");

module_init(sm_thmc50_init);
module_exit(sm_thmc50_exit);
