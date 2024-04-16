// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * thmc50.c - Part of lm_sensors, Linux kernel modules for hardware
 *	      monitoring
 * Copyright (C) 2007 Krzysztof Helt <krzysztof.h1@wp.pl>
 * Based on 2.4 driver by Frodo Looijaard <frodol@dds.nl> and
 * Philip Edelbrock <phil@netroedge.com>
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

MODULE_LICENSE("GPL");

/* Addresses to scan */
static const unsigned short normal_i2c[] = { 0x2c, 0x2d, 0x2e, I2C_CLIENT_END };

/* Insmod parameters */
enum chips { thmc50, adm1022 };

static unsigned short adm1022_temp3[16];
static unsigned int adm1022_temp3_num;
module_param_array(adm1022_temp3, ushort, &adm1022_temp3_num, 0);
MODULE_PARM_DESC(adm1022_temp3,
		 "List of adapter,address pairs to enable 3rd temperature (ADM1022 only)");

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
	struct i2c_client *client;
	const struct attribute_group *groups[3];

	struct mutex update_lock;
	enum chips type;
	unsigned long last_updated;	/* In jiffies */
	char has_temp3;		/* !=0 if it is ADM1022 in temp3 mode */
	bool valid;		/* true if following fields are valid */

	/* Register values */
	s8 temp_input[3];
	s8 temp_max[3];
	s8 temp_min[3];
	s8 temp_critical[3];
	u8 analog_out;
	u8 alarms;
};

static struct thmc50_data *thmc50_update_device(struct device *dev)
{
	struct thmc50_data *data = dev_get_drvdata(dev);
	struct i2c_client *client = data->client;
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
		data->valid = true;
	}

	mutex_unlock(&data->update_lock);

	return data;
}

static ssize_t analog_out_show(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	struct thmc50_data *data = thmc50_update_device(dev);
	return sprintf(buf, "%d\n", data->analog_out);
}

static ssize_t analog_out_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t count)
{
	struct thmc50_data *data = dev_get_drvdata(dev);
	struct i2c_client *client = data->client;
	int config;
	unsigned long tmp;
	int err;

	err = kstrtoul(buf, 10, &tmp);
	if (err)
		return err;

	mutex_lock(&data->update_lock);
	data->analog_out = clamp_val(tmp, 0, 255);
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
static ssize_t pwm_mode_show(struct device *dev,
			     struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "0\n");
}

/* Temperatures */
static ssize_t temp_show(struct device *dev, struct device_attribute *attr,
			 char *buf)
{
	int nr = to_sensor_dev_attr(attr)->index;
	struct thmc50_data *data = thmc50_update_device(dev);
	return sprintf(buf, "%d\n", data->temp_input[nr] * 1000);
}

static ssize_t temp_min_show(struct device *dev,
			     struct device_attribute *attr, char *buf)
{
	int nr = to_sensor_dev_attr(attr)->index;
	struct thmc50_data *data = thmc50_update_device(dev);
	return sprintf(buf, "%d\n", data->temp_min[nr] * 1000);
}

static ssize_t temp_min_store(struct device *dev,
			      struct device_attribute *attr, const char *buf,
			      size_t count)
{
	int nr = to_sensor_dev_attr(attr)->index;
	struct thmc50_data *data = dev_get_drvdata(dev);
	struct i2c_client *client = data->client;
	long val;
	int err;

	err = kstrtol(buf, 10, &val);
	if (err)
		return err;

	mutex_lock(&data->update_lock);
	data->temp_min[nr] = clamp_val(val / 1000, -128, 127);
	i2c_smbus_write_byte_data(client, THMC50_REG_TEMP_MIN[nr],
				  data->temp_min[nr]);
	mutex_unlock(&data->update_lock);
	return count;
}

static ssize_t temp_max_show(struct device *dev,
			     struct device_attribute *attr, char *buf)
{
	int nr = to_sensor_dev_attr(attr)->index;
	struct thmc50_data *data = thmc50_update_device(dev);
	return sprintf(buf, "%d\n", data->temp_max[nr] * 1000);
}

static ssize_t temp_max_store(struct device *dev,
			      struct device_attribute *attr, const char *buf,
			      size_t count)
{
	int nr = to_sensor_dev_attr(attr)->index;
	struct thmc50_data *data = dev_get_drvdata(dev);
	struct i2c_client *client = data->client;
	long val;
	int err;

	err = kstrtol(buf, 10, &val);
	if (err)
		return err;

	mutex_lock(&data->update_lock);
	data->temp_max[nr] = clamp_val(val / 1000, -128, 127);
	i2c_smbus_write_byte_data(client, THMC50_REG_TEMP_MAX[nr],
				  data->temp_max[nr]);
	mutex_unlock(&data->update_lock);
	return count;
}

static ssize_t temp_critical_show(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	int nr = to_sensor_dev_attr(attr)->index;
	struct thmc50_data *data = thmc50_update_device(dev);
	return sprintf(buf, "%d\n", data->temp_critical[nr] * 1000);
}

static ssize_t alarm_show(struct device *dev, struct device_attribute *attr,
			  char *buf)
{
	int index = to_sensor_dev_attr(attr)->index;
	struct thmc50_data *data = thmc50_update_device(dev);

	return sprintf(buf, "%u\n", (data->alarms >> index) & 1);
}

static SENSOR_DEVICE_ATTR_RO(temp1_input, temp, 0);
static SENSOR_DEVICE_ATTR_RW(temp1_min, temp_min, 0);
static SENSOR_DEVICE_ATTR_RW(temp1_max, temp_max, 0);
static SENSOR_DEVICE_ATTR_RO(temp1_crit, temp_critical, 0);
static SENSOR_DEVICE_ATTR_RO(temp2_input, temp, 1);
static SENSOR_DEVICE_ATTR_RW(temp2_min, temp_min, 1);
static SENSOR_DEVICE_ATTR_RW(temp2_max, temp_max, 1);
static SENSOR_DEVICE_ATTR_RO(temp2_crit, temp_critical, 1);
static SENSOR_DEVICE_ATTR_RO(temp3_input, temp, 2);
static SENSOR_DEVICE_ATTR_RW(temp3_min, temp_min, 2);
static SENSOR_DEVICE_ATTR_RW(temp3_max, temp_max, 2);
static SENSOR_DEVICE_ATTR_RO(temp3_crit, temp_critical, 2);

static SENSOR_DEVICE_ATTR_RO(temp1_alarm, alarm, 0);
static SENSOR_DEVICE_ATTR_RO(temp2_alarm, alarm, 5);
static SENSOR_DEVICE_ATTR_RO(temp3_alarm, alarm, 1);
static SENSOR_DEVICE_ATTR_RO(temp2_fault, alarm, 7);
static SENSOR_DEVICE_ATTR_RO(temp3_fault, alarm, 2);

static SENSOR_DEVICE_ATTR_RW(pwm1, analog_out, 0);
static SENSOR_DEVICE_ATTR_RO(pwm1_mode, pwm_mode, 0);

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
static int thmc50_detect(struct i2c_client *client,
			 struct i2c_board_info *info)
{
	unsigned company;
	unsigned revision;
	unsigned config;
	struct i2c_adapter *adapter = client->adapter;
	const char *type_name;

	if (!i2c_check_functionality(adapter, I2C_FUNC_SMBUS_BYTE_DATA)) {
		pr_debug("thmc50: detect failed, smbus byte data not supported!\n");
		return -ENODEV;
	}

	pr_debug("thmc50: Probing for THMC50 at 0x%2X on bus %d\n",
		 client->addr, i2c_adapter_id(client->adapter));

	company = i2c_smbus_read_byte_data(client, THMC50_REG_COMPANY_ID);
	revision = i2c_smbus_read_byte_data(client, THMC50_REG_DIE_CODE);
	config = i2c_smbus_read_byte_data(client, THMC50_REG_CONF);
	if (revision < 0xc0 || (config & 0x10))
		return -ENODEV;

	if (company == 0x41) {
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
	} else if (company == 0x49) {
		type_name = "thmc50";
	} else {
		pr_debug("thmc50: Detection of THMC50/ADM1022 failed\n");
		return -ENODEV;
	}

	pr_debug("thmc50: Detected %s (version %x, revision %x)\n",
		 type_name, (revision >> 4) - 0xc, revision & 0xf);

	strscpy(info->type, type_name, I2C_NAME_SIZE);

	return 0;
}

static void thmc50_init_client(struct thmc50_data *data)
{
	struct i2c_client *client = data->client;
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

static const struct i2c_device_id thmc50_id[];

static int thmc50_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct thmc50_data *data;
	struct device *hwmon_dev;
	int idx = 0;

	data = devm_kzalloc(dev, sizeof(struct thmc50_data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->client = client;
	data->type = i2c_match_id(thmc50_id, client)->driver_data;
	mutex_init(&data->update_lock);

	thmc50_init_client(data);

	/* sysfs hooks */
	data->groups[idx++] = &thmc50_group;

	/* Register additional ADM1022 sysfs hooks */
	if (data->has_temp3)
		data->groups[idx++] = &temp3_group;

	hwmon_dev = devm_hwmon_device_register_with_groups(dev, client->name,
							   data, data->groups);
	return PTR_ERR_OR_ZERO(hwmon_dev);
}

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
	.probe_new = thmc50_probe,
	.id_table = thmc50_id,
	.detect = thmc50_detect,
	.address_list = normal_i2c,
};

module_i2c_driver(thmc50_driver);

MODULE_AUTHOR("Krzysztof Helt <krzysztof.h1@wp.pl>");
MODULE_DESCRIPTION("THMC50 driver");
