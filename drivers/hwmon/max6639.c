/*
 * max6639.c - Support for Maxim MAX6639
 *
 * 2-Channel Temperature Monitor with Dual PWM Fan-Speed Controller
 *
 * Copyright (C) 2010, 2011 Roland Stigge <stigge@antcom.de>
 *
 * based on the initial MAX6639 support from semptian.net
 * by He Changqing <hechangqing@semptian.com>
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
#include <linux/i2c/max6639.h>

/* Addresses to scan */
static unsigned short normal_i2c[] = { 0x2c, 0x2e, 0x2f, I2C_CLIENT_END };

/* The MAX6639 registers, valid channel numbers: 0, 1 */
#define MAX6639_REG_TEMP(ch)			(0x00 + (ch))
#define MAX6639_REG_STATUS			0x02
#define MAX6639_REG_OUTPUT_MASK			0x03
#define MAX6639_REG_GCONFIG			0x04
#define MAX6639_REG_TEMP_EXT(ch)		(0x05 + (ch))
#define MAX6639_REG_ALERT_LIMIT(ch)		(0x08 + (ch))
#define MAX6639_REG_OT_LIMIT(ch)		(0x0A + (ch))
#define MAX6639_REG_THERM_LIMIT(ch)		(0x0C + (ch))
#define MAX6639_REG_FAN_CONFIG1(ch)		(0x10 + (ch) * 4)
#define MAX6639_REG_FAN_CONFIG2a(ch)		(0x11 + (ch) * 4)
#define MAX6639_REG_FAN_CONFIG2b(ch)		(0x12 + (ch) * 4)
#define MAX6639_REG_FAN_CONFIG3(ch)		(0x13 + (ch) * 4)
#define MAX6639_REG_FAN_CNT(ch)			(0x20 + (ch))
#define MAX6639_REG_TARGET_CNT(ch)		(0x22 + (ch))
#define MAX6639_REG_FAN_PPR(ch)			(0x24 + (ch))
#define MAX6639_REG_TARGTDUTY(ch)		(0x26 + (ch))
#define MAX6639_REG_FAN_START_TEMP(ch)		(0x28 + (ch))
#define MAX6639_REG_DEVID			0x3D
#define MAX6639_REG_MANUID			0x3E
#define MAX6639_REG_DEVREV			0x3F

/* Register bits */
#define MAX6639_GCONFIG_STANDBY			0x80
#define MAX6639_GCONFIG_POR			0x40
#define MAX6639_GCONFIG_DISABLE_TIMEOUT		0x20
#define MAX6639_GCONFIG_CH2_LOCAL		0x10
#define MAX6639_GCONFIG_PWM_FREQ_HI		0x08

#define MAX6639_FAN_CONFIG1_PWM			0x80

#define MAX6639_FAN_CONFIG3_THERM_FULL_SPEED	0x40

static const int rpm_ranges[] = { 2000, 4000, 8000, 16000 };

#define FAN_FROM_REG(val, rpm_range)	((val) == 0 || (val) == 255 ? \
				0 : (rpm_ranges[rpm_range] * 30) / (val))
#define TEMP_LIMIT_TO_REG(val)	clamp_val((val) / 1000, 0, 255)

/*
 * Client data (each client gets its own)
 */
struct max6639_data {
	struct device *hwmon_dev;
	struct mutex update_lock;
	char valid;		/* !=0 if following fields are valid */
	unsigned long last_updated;	/* In jiffies */

	/* Register values sampled regularly */
	u16 temp[2];		/* Temperature, in 1/8 C, 0..255 C */
	bool temp_fault[2];	/* Detected temperature diode failure */
	u8 fan[2];		/* Register value: TACH count for fans >=30 */
	u8 status;		/* Detected channel alarms and fan failures */

	/* Register values only written to */
	u8 pwm[2];		/* Register value: Duty cycle 0..120 */
	u8 temp_therm[2];	/* THERM Temperature, 0..255 C (->_max) */
	u8 temp_alert[2];	/* ALERT Temperature, 0..255 C (->_crit) */
	u8 temp_ot[2];		/* OT Temperature, 0..255 C (->_emergency) */

	/* Register values initialized only once */
	u8 ppr;			/* Pulses per rotation 0..3 for 1..4 ppr */
	u8 rpm_range;		/* Index in above rpm_ranges table */
};

static struct max6639_data *max6639_update_device(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct max6639_data *data = i2c_get_clientdata(client);
	struct max6639_data *ret = data;
	int i;
	int status_reg;

	mutex_lock(&data->update_lock);

	if (time_after(jiffies, data->last_updated + 2 * HZ) || !data->valid) {
		int res;

		dev_dbg(&client->dev, "Starting max6639 update\n");

		status_reg = i2c_smbus_read_byte_data(client,
						      MAX6639_REG_STATUS);
		if (status_reg < 0) {
			ret = ERR_PTR(status_reg);
			goto abort;
		}

		data->status = status_reg;

		for (i = 0; i < 2; i++) {
			res = i2c_smbus_read_byte_data(client,
					MAX6639_REG_FAN_CNT(i));
			if (res < 0) {
				ret = ERR_PTR(res);
				goto abort;
			}
			data->fan[i] = res;

			res = i2c_smbus_read_byte_data(client,
					MAX6639_REG_TEMP_EXT(i));
			if (res < 0) {
				ret = ERR_PTR(res);
				goto abort;
			}
			data->temp[i] = res >> 5;
			data->temp_fault[i] = res & 0x01;

			res = i2c_smbus_read_byte_data(client,
					MAX6639_REG_TEMP(i));
			if (res < 0) {
				ret = ERR_PTR(res);
				goto abort;
			}
			data->temp[i] |= res << 3;
		}

		data->last_updated = jiffies;
		data->valid = 1;
	}
abort:
	mutex_unlock(&data->update_lock);

	return ret;
}

static ssize_t show_temp_input(struct device *dev,
			       struct device_attribute *dev_attr, char *buf)
{
	long temp;
	struct max6639_data *data = max6639_update_device(dev);
	struct sensor_device_attribute *attr = to_sensor_dev_attr(dev_attr);

	if (IS_ERR(data))
		return PTR_ERR(data);

	temp = data->temp[attr->index] * 125;
	return sprintf(buf, "%ld\n", temp);
}

static ssize_t show_temp_fault(struct device *dev,
			       struct device_attribute *dev_attr, char *buf)
{
	struct max6639_data *data = max6639_update_device(dev);
	struct sensor_device_attribute *attr = to_sensor_dev_attr(dev_attr);

	if (IS_ERR(data))
		return PTR_ERR(data);

	return sprintf(buf, "%d\n", data->temp_fault[attr->index]);
}

static ssize_t show_temp_max(struct device *dev,
			     struct device_attribute *dev_attr, char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct max6639_data *data = i2c_get_clientdata(client);
	struct sensor_device_attribute *attr = to_sensor_dev_attr(dev_attr);

	return sprintf(buf, "%d\n", (data->temp_therm[attr->index] * 1000));
}

static ssize_t set_temp_max(struct device *dev,
			    struct device_attribute *dev_attr,
			    const char *buf, size_t count)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct max6639_data *data = i2c_get_clientdata(client);
	struct sensor_device_attribute *attr = to_sensor_dev_attr(dev_attr);
	unsigned long val;
	int res;

	res = kstrtoul(buf, 10, &val);
	if (res)
		return res;

	mutex_lock(&data->update_lock);
	data->temp_therm[attr->index] = TEMP_LIMIT_TO_REG(val);
	i2c_smbus_write_byte_data(client,
				  MAX6639_REG_THERM_LIMIT(attr->index),
				  data->temp_therm[attr->index]);
	mutex_unlock(&data->update_lock);
	return count;
}

static ssize_t show_temp_crit(struct device *dev,
			      struct device_attribute *dev_attr, char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct max6639_data *data = i2c_get_clientdata(client);
	struct sensor_device_attribute *attr = to_sensor_dev_attr(dev_attr);

	return sprintf(buf, "%d\n", (data->temp_alert[attr->index] * 1000));
}

static ssize_t set_temp_crit(struct device *dev,
			     struct device_attribute *dev_attr,
			     const char *buf, size_t count)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct max6639_data *data = i2c_get_clientdata(client);
	struct sensor_device_attribute *attr = to_sensor_dev_attr(dev_attr);
	unsigned long val;
	int res;

	res = kstrtoul(buf, 10, &val);
	if (res)
		return res;

	mutex_lock(&data->update_lock);
	data->temp_alert[attr->index] = TEMP_LIMIT_TO_REG(val);
	i2c_smbus_write_byte_data(client,
				  MAX6639_REG_ALERT_LIMIT(attr->index),
				  data->temp_alert[attr->index]);
	mutex_unlock(&data->update_lock);
	return count;
}

static ssize_t show_temp_emergency(struct device *dev,
				   struct device_attribute *dev_attr,
				   char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct max6639_data *data = i2c_get_clientdata(client);
	struct sensor_device_attribute *attr = to_sensor_dev_attr(dev_attr);

	return sprintf(buf, "%d\n", (data->temp_ot[attr->index] * 1000));
}

static ssize_t set_temp_emergency(struct device *dev,
				  struct device_attribute *dev_attr,
				  const char *buf, size_t count)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct max6639_data *data = i2c_get_clientdata(client);
	struct sensor_device_attribute *attr = to_sensor_dev_attr(dev_attr);
	unsigned long val;
	int res;

	res = kstrtoul(buf, 10, &val);
	if (res)
		return res;

	mutex_lock(&data->update_lock);
	data->temp_ot[attr->index] = TEMP_LIMIT_TO_REG(val);
	i2c_smbus_write_byte_data(client,
				  MAX6639_REG_OT_LIMIT(attr->index),
				  data->temp_ot[attr->index]);
	mutex_unlock(&data->update_lock);
	return count;
}

static ssize_t show_pwm(struct device *dev,
			struct device_attribute *dev_attr, char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct max6639_data *data = i2c_get_clientdata(client);
	struct sensor_device_attribute *attr = to_sensor_dev_attr(dev_attr);

	return sprintf(buf, "%d\n", data->pwm[attr->index] * 255 / 120);
}

static ssize_t set_pwm(struct device *dev,
		       struct device_attribute *dev_attr,
		       const char *buf, size_t count)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct max6639_data *data = i2c_get_clientdata(client);
	struct sensor_device_attribute *attr = to_sensor_dev_attr(dev_attr);
	unsigned long val;
	int res;

	res = kstrtoul(buf, 10, &val);
	if (res)
		return res;

	val = clamp_val(val, 0, 255);

	mutex_lock(&data->update_lock);
	data->pwm[attr->index] = (u8)(val * 120 / 255);
	i2c_smbus_write_byte_data(client,
				  MAX6639_REG_TARGTDUTY(attr->index),
				  data->pwm[attr->index]);
	mutex_unlock(&data->update_lock);
	return count;
}

static ssize_t show_fan_input(struct device *dev,
			      struct device_attribute *dev_attr, char *buf)
{
	struct max6639_data *data = max6639_update_device(dev);
	struct sensor_device_attribute *attr = to_sensor_dev_attr(dev_attr);

	if (IS_ERR(data))
		return PTR_ERR(data);

	return sprintf(buf, "%d\n", FAN_FROM_REG(data->fan[attr->index],
		       data->rpm_range));
}

static ssize_t show_alarm(struct device *dev,
			  struct device_attribute *dev_attr, char *buf)
{
	struct max6639_data *data = max6639_update_device(dev);
	struct sensor_device_attribute *attr = to_sensor_dev_attr(dev_attr);

	if (IS_ERR(data))
		return PTR_ERR(data);

	return sprintf(buf, "%d\n", !!(data->status & (1 << attr->index)));
}

static SENSOR_DEVICE_ATTR(temp1_input, S_IRUGO, show_temp_input, NULL, 0);
static SENSOR_DEVICE_ATTR(temp2_input, S_IRUGO, show_temp_input, NULL, 1);
static SENSOR_DEVICE_ATTR(temp1_fault, S_IRUGO, show_temp_fault, NULL, 0);
static SENSOR_DEVICE_ATTR(temp2_fault, S_IRUGO, show_temp_fault, NULL, 1);
static SENSOR_DEVICE_ATTR(temp1_max, S_IWUSR | S_IRUGO, show_temp_max,
		set_temp_max, 0);
static SENSOR_DEVICE_ATTR(temp2_max, S_IWUSR | S_IRUGO, show_temp_max,
		set_temp_max, 1);
static SENSOR_DEVICE_ATTR(temp1_crit, S_IWUSR | S_IRUGO, show_temp_crit,
		set_temp_crit, 0);
static SENSOR_DEVICE_ATTR(temp2_crit, S_IWUSR | S_IRUGO, show_temp_crit,
		set_temp_crit, 1);
static SENSOR_DEVICE_ATTR(temp1_emergency, S_IWUSR | S_IRUGO,
		show_temp_emergency, set_temp_emergency, 0);
static SENSOR_DEVICE_ATTR(temp2_emergency, S_IWUSR | S_IRUGO,
		show_temp_emergency, set_temp_emergency, 1);
static SENSOR_DEVICE_ATTR(pwm1, S_IWUSR | S_IRUGO, show_pwm, set_pwm, 0);
static SENSOR_DEVICE_ATTR(pwm2, S_IWUSR | S_IRUGO, show_pwm, set_pwm, 1);
static SENSOR_DEVICE_ATTR(fan1_input, S_IRUGO, show_fan_input, NULL, 0);
static SENSOR_DEVICE_ATTR(fan2_input, S_IRUGO, show_fan_input, NULL, 1);
static SENSOR_DEVICE_ATTR(fan1_fault, S_IRUGO, show_alarm, NULL, 1);
static SENSOR_DEVICE_ATTR(fan2_fault, S_IRUGO, show_alarm, NULL, 0);
static SENSOR_DEVICE_ATTR(temp1_max_alarm, S_IRUGO, show_alarm, NULL, 3);
static SENSOR_DEVICE_ATTR(temp2_max_alarm, S_IRUGO, show_alarm, NULL, 2);
static SENSOR_DEVICE_ATTR(temp1_crit_alarm, S_IRUGO, show_alarm, NULL, 7);
static SENSOR_DEVICE_ATTR(temp2_crit_alarm, S_IRUGO, show_alarm, NULL, 6);
static SENSOR_DEVICE_ATTR(temp1_emergency_alarm, S_IRUGO, show_alarm, NULL, 5);
static SENSOR_DEVICE_ATTR(temp2_emergency_alarm, S_IRUGO, show_alarm, NULL, 4);


static struct attribute *max6639_attributes[] = {
	&sensor_dev_attr_temp1_input.dev_attr.attr,
	&sensor_dev_attr_temp2_input.dev_attr.attr,
	&sensor_dev_attr_temp1_fault.dev_attr.attr,
	&sensor_dev_attr_temp2_fault.dev_attr.attr,
	&sensor_dev_attr_temp1_max.dev_attr.attr,
	&sensor_dev_attr_temp2_max.dev_attr.attr,
	&sensor_dev_attr_temp1_crit.dev_attr.attr,
	&sensor_dev_attr_temp2_crit.dev_attr.attr,
	&sensor_dev_attr_temp1_emergency.dev_attr.attr,
	&sensor_dev_attr_temp2_emergency.dev_attr.attr,
	&sensor_dev_attr_pwm1.dev_attr.attr,
	&sensor_dev_attr_pwm2.dev_attr.attr,
	&sensor_dev_attr_fan1_input.dev_attr.attr,
	&sensor_dev_attr_fan2_input.dev_attr.attr,
	&sensor_dev_attr_fan1_fault.dev_attr.attr,
	&sensor_dev_attr_fan2_fault.dev_attr.attr,
	&sensor_dev_attr_temp1_max_alarm.dev_attr.attr,
	&sensor_dev_attr_temp2_max_alarm.dev_attr.attr,
	&sensor_dev_attr_temp1_crit_alarm.dev_attr.attr,
	&sensor_dev_attr_temp2_crit_alarm.dev_attr.attr,
	&sensor_dev_attr_temp1_emergency_alarm.dev_attr.attr,
	&sensor_dev_attr_temp2_emergency_alarm.dev_attr.attr,
	NULL
};

static const struct attribute_group max6639_group = {
	.attrs = max6639_attributes,
};

/*
 *  returns respective index in rpm_ranges table
 *  1 by default on invalid range
 */
static int rpm_range_to_reg(int range)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(rpm_ranges); i++) {
		if (rpm_ranges[i] == range)
			return i;
	}

	return 1; /* default: 4000 RPM */
}

static int max6639_init_client(struct i2c_client *client)
{
	struct max6639_data *data = i2c_get_clientdata(client);
	struct max6639_platform_data *max6639_info =
		dev_get_platdata(&client->dev);
	int i;
	int rpm_range = 1; /* default: 4000 RPM */
	int err;

	/* Reset chip to default values, see below for GCONFIG setup */
	err = i2c_smbus_write_byte_data(client, MAX6639_REG_GCONFIG,
				  MAX6639_GCONFIG_POR);
	if (err)
		goto exit;

	/* Fans pulse per revolution is 2 by default */
	if (max6639_info && max6639_info->ppr > 0 &&
			max6639_info->ppr < 5)
		data->ppr = max6639_info->ppr;
	else
		data->ppr = 2;
	data->ppr -= 1;

	if (max6639_info)
		rpm_range = rpm_range_to_reg(max6639_info->rpm_range);
	data->rpm_range = rpm_range;

	for (i = 0; i < 2; i++) {

		/* Set Fan pulse per revolution */
		err = i2c_smbus_write_byte_data(client,
				MAX6639_REG_FAN_PPR(i),
				data->ppr << 6);
		if (err)
			goto exit;

		/* Fans config PWM, RPM */
		err = i2c_smbus_write_byte_data(client,
			MAX6639_REG_FAN_CONFIG1(i),
			MAX6639_FAN_CONFIG1_PWM | rpm_range);
		if (err)
			goto exit;

		/* Fans PWM polarity high by default */
		if (max6639_info && max6639_info->pwm_polarity == 0)
			err = i2c_smbus_write_byte_data(client,
				MAX6639_REG_FAN_CONFIG2a(i), 0x00);
		else
			err = i2c_smbus_write_byte_data(client,
				MAX6639_REG_FAN_CONFIG2a(i), 0x02);
		if (err)
			goto exit;

		/*
		 * /THERM full speed enable,
		 * PWM frequency 25kHz, see also GCONFIG below
		 */
		err = i2c_smbus_write_byte_data(client,
			MAX6639_REG_FAN_CONFIG3(i),
			MAX6639_FAN_CONFIG3_THERM_FULL_SPEED | 0x03);
		if (err)
			goto exit;

		/* Max. temp. 80C/90C/100C */
		data->temp_therm[i] = 80;
		data->temp_alert[i] = 90;
		data->temp_ot[i] = 100;
		err = i2c_smbus_write_byte_data(client,
				MAX6639_REG_THERM_LIMIT(i),
				data->temp_therm[i]);
		if (err)
			goto exit;
		err = i2c_smbus_write_byte_data(client,
				MAX6639_REG_ALERT_LIMIT(i),
				data->temp_alert[i]);
		if (err)
			goto exit;
		err = i2c_smbus_write_byte_data(client,
				MAX6639_REG_OT_LIMIT(i), data->temp_ot[i]);
		if (err)
			goto exit;

		/* PWM 120/120 (i.e. 100%) */
		data->pwm[i] = 120;
		err = i2c_smbus_write_byte_data(client,
				MAX6639_REG_TARGTDUTY(i), data->pwm[i]);
		if (err)
			goto exit;
	}
	/* Start monitoring */
	err = i2c_smbus_write_byte_data(client, MAX6639_REG_GCONFIG,
		MAX6639_GCONFIG_DISABLE_TIMEOUT | MAX6639_GCONFIG_CH2_LOCAL |
		MAX6639_GCONFIG_PWM_FREQ_HI);
exit:
	return err;
}

/* Return 0 if detection is successful, -ENODEV otherwise */
static int max6639_detect(struct i2c_client *client,
			  struct i2c_board_info *info)
{
	struct i2c_adapter *adapter = client->adapter;
	int dev_id, manu_id;

	if (!i2c_check_functionality(adapter, I2C_FUNC_SMBUS_BYTE_DATA))
		return -ENODEV;

	/* Actual detection via device and manufacturer ID */
	dev_id = i2c_smbus_read_byte_data(client, MAX6639_REG_DEVID);
	manu_id = i2c_smbus_read_byte_data(client, MAX6639_REG_MANUID);
	if (dev_id != 0x58 || manu_id != 0x4D)
		return -ENODEV;

	strlcpy(info->type, "max6639", I2C_NAME_SIZE);

	return 0;
}

static int max6639_probe(struct i2c_client *client,
			 const struct i2c_device_id *id)
{
	struct max6639_data *data;
	int err;

	data = devm_kzalloc(&client->dev, sizeof(struct max6639_data),
			    GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	i2c_set_clientdata(client, data);
	mutex_init(&data->update_lock);

	/* Initialize the max6639 chip */
	err = max6639_init_client(client);
	if (err < 0)
		return err;

	/* Register sysfs hooks */
	err = sysfs_create_group(&client->dev.kobj, &max6639_group);
	if (err)
		return err;

	data->hwmon_dev = hwmon_device_register(&client->dev);
	if (IS_ERR(data->hwmon_dev)) {
		err = PTR_ERR(data->hwmon_dev);
		goto error_remove;
	}

	dev_info(&client->dev, "temperature sensor and fan control found\n");

	return 0;

error_remove:
	sysfs_remove_group(&client->dev.kobj, &max6639_group);
	return err;
}

static int max6639_remove(struct i2c_client *client)
{
	struct max6639_data *data = i2c_get_clientdata(client);

	hwmon_device_unregister(data->hwmon_dev);
	sysfs_remove_group(&client->dev.kobj, &max6639_group);

	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int max6639_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	int data = i2c_smbus_read_byte_data(client, MAX6639_REG_GCONFIG);
	if (data < 0)
		return data;

	return i2c_smbus_write_byte_data(client,
			MAX6639_REG_GCONFIG, data | MAX6639_GCONFIG_STANDBY);
}

static int max6639_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	int data = i2c_smbus_read_byte_data(client, MAX6639_REG_GCONFIG);
	if (data < 0)
		return data;

	return i2c_smbus_write_byte_data(client,
			MAX6639_REG_GCONFIG, data & ~MAX6639_GCONFIG_STANDBY);
}
#endif /* CONFIG_PM_SLEEP */

static const struct i2c_device_id max6639_id[] = {
	{"max6639", 0},
	{ }
};

MODULE_DEVICE_TABLE(i2c, max6639_id);

static const struct dev_pm_ops max6639_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(max6639_suspend, max6639_resume)
};

static struct i2c_driver max6639_driver = {
	.class = I2C_CLASS_HWMON,
	.driver = {
		   .name = "max6639",
		   .pm = &max6639_pm_ops,
		   },
	.probe = max6639_probe,
	.remove = max6639_remove,
	.id_table = max6639_id,
	.detect = max6639_detect,
	.address_list = normal_i2c,
};

module_i2c_driver(max6639_driver);

MODULE_AUTHOR("Roland Stigge <stigge@antcom.de>");
MODULE_DESCRIPTION("max6639 driver");
MODULE_LICENSE("GPL");
