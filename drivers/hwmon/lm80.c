/*
 * lm80.c - From lm_sensors, Linux kernel modules for hardware
 * monitoring
 * Copyright (C) 1998, 1999  Frodo Looijaard <frodol@dds.nl>
 * and Philip Edelbrock <phil@netroedge.com>
 *
 * Ported to Linux 2.6 by Tiago Sousa <mirage@kaotik.org>
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
static const unsigned short normal_i2c[] = { 0x28, 0x29, 0x2a, 0x2b, 0x2c, 0x2d,
						0x2e, 0x2f, I2C_CLIENT_END };

/* Insmod parameters */
I2C_CLIENT_INSMOD_1(lm80);

/* Many LM80 constants specified below */

/* The LM80 registers */
#define LM80_REG_IN_MAX(nr)		(0x2a + (nr) * 2)
#define LM80_REG_IN_MIN(nr)		(0x2b + (nr) * 2)
#define LM80_REG_IN(nr)			(0x20 + (nr))

#define LM80_REG_FAN1			0x28
#define LM80_REG_FAN2			0x29
#define LM80_REG_FAN_MIN(nr)		(0x3b + (nr))

#define LM80_REG_TEMP			0x27
#define LM80_REG_TEMP_HOT_MAX		0x38
#define LM80_REG_TEMP_HOT_HYST		0x39
#define LM80_REG_TEMP_OS_MAX		0x3a
#define LM80_REG_TEMP_OS_HYST		0x3b

#define LM80_REG_CONFIG			0x00
#define LM80_REG_ALARM1			0x01
#define LM80_REG_ALARM2			0x02
#define LM80_REG_MASK1			0x03
#define LM80_REG_MASK2			0x04
#define LM80_REG_FANDIV			0x05
#define LM80_REG_RES			0x06


/* Conversions. Rounding and limit checking is only done on the TO_REG
   variants. Note that you should be a bit careful with which arguments
   these macros are called: arguments may be evaluated more than once.
   Fixing this is just not worth it. */

#define IN_TO_REG(val)		(SENSORS_LIMIT(((val)+5)/10,0,255))
#define IN_FROM_REG(val)	((val)*10)

static inline unsigned char FAN_TO_REG(unsigned rpm, unsigned div)
{
	if (rpm == 0)
		return 255;
	rpm = SENSORS_LIMIT(rpm, 1, 1000000);
	return SENSORS_LIMIT((1350000 + rpm*div / 2) / (rpm*div), 1, 254);
}

#define FAN_FROM_REG(val,div)	((val)==0?-1:\
				(val)==255?0:1350000/((div)*(val)))

static inline long TEMP_FROM_REG(u16 temp)
{
	long res;

	temp >>= 4;
	if (temp < 0x0800)
		res = 625 * (long) temp;
	else
		res = ((long) temp - 0x01000) * 625;

	return res / 10;
}

#define TEMP_LIMIT_FROM_REG(val)	(((val)>0x80?(val)-0x100:(val))*1000)

#define TEMP_LIMIT_TO_REG(val)		SENSORS_LIMIT((val)<0?\
					((val)-500)/1000:((val)+500)/1000,0,255)

#define DIV_FROM_REG(val)		(1 << (val))

/*
 * Client data (each client gets its own)
 */

struct lm80_data {
	struct device *hwmon_dev;
	struct mutex update_lock;
	char valid;		/* !=0 if following fields are valid */
	unsigned long last_updated;	/* In jiffies */

	u8 in[7];		/* Register value */
	u8 in_max[7];		/* Register value */
	u8 in_min[7];		/* Register value */
	u8 fan[2];		/* Register value */
	u8 fan_min[2];		/* Register value */
	u8 fan_div[2];		/* Register encoding, shifted right */
	u16 temp;		/* Register values, shifted right */
	u8 temp_hot_max;	/* Register value */
	u8 temp_hot_hyst;	/* Register value */
	u8 temp_os_max;		/* Register value */
	u8 temp_os_hyst;	/* Register value */
	u16 alarms;		/* Register encoding, combined */
};

/*
 * Functions declaration
 */

static int lm80_probe(struct i2c_client *client,
		      const struct i2c_device_id *id);
static int lm80_detect(struct i2c_client *client, int kind,
		       struct i2c_board_info *info);
static void lm80_init_client(struct i2c_client *client);
static int lm80_remove(struct i2c_client *client);
static struct lm80_data *lm80_update_device(struct device *dev);
static int lm80_read_value(struct i2c_client *client, u8 reg);
static int lm80_write_value(struct i2c_client *client, u8 reg, u8 value);

/*
 * Driver data (common to all clients)
 */

static const struct i2c_device_id lm80_id[] = {
	{ "lm80", lm80 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, lm80_id);

static struct i2c_driver lm80_driver = {
	.class		= I2C_CLASS_HWMON,
	.driver = {
		.name	= "lm80",
	},
	.probe		= lm80_probe,
	.remove		= lm80_remove,
	.id_table	= lm80_id,
	.detect		= lm80_detect,
	.address_data	= &addr_data,
};

/*
 * Sysfs stuff
 */

#define show_in(suffix, value) \
static ssize_t show_in_##suffix(struct device *dev, struct device_attribute *attr, char *buf) \
{ \
	int nr = to_sensor_dev_attr(attr)->index; \
	struct lm80_data *data = lm80_update_device(dev); \
	return sprintf(buf, "%d\n", IN_FROM_REG(data->value[nr])); \
}
show_in(min, in_min)
show_in(max, in_max)
show_in(input, in)

#define set_in(suffix, value, reg) \
static ssize_t set_in_##suffix(struct device *dev, struct device_attribute *attr, const char *buf, \
	size_t count) \
{ \
	int nr = to_sensor_dev_attr(attr)->index; \
	struct i2c_client *client = to_i2c_client(dev); \
	struct lm80_data *data = i2c_get_clientdata(client); \
	long val = simple_strtol(buf, NULL, 10); \
 \
	mutex_lock(&data->update_lock);\
	data->value[nr] = IN_TO_REG(val); \
	lm80_write_value(client, reg(nr), data->value[nr]); \
	mutex_unlock(&data->update_lock);\
	return count; \
}
set_in(min, in_min, LM80_REG_IN_MIN)
set_in(max, in_max, LM80_REG_IN_MAX)

#define show_fan(suffix, value) \
static ssize_t show_fan_##suffix(struct device *dev, struct device_attribute *attr, char *buf) \
{ \
	int nr = to_sensor_dev_attr(attr)->index; \
	struct lm80_data *data = lm80_update_device(dev); \
	return sprintf(buf, "%d\n", FAN_FROM_REG(data->value[nr], \
		       DIV_FROM_REG(data->fan_div[nr]))); \
}
show_fan(min, fan_min)
show_fan(input, fan)

static ssize_t show_fan_div(struct device *dev, struct device_attribute *attr,
	char *buf)
{
	int nr = to_sensor_dev_attr(attr)->index;
	struct lm80_data *data = lm80_update_device(dev);
	return sprintf(buf, "%d\n", DIV_FROM_REG(data->fan_div[nr]));
}

static ssize_t set_fan_min(struct device *dev, struct device_attribute *attr,
	const char *buf, size_t count)
{
	int nr = to_sensor_dev_attr(attr)->index;
	struct i2c_client *client = to_i2c_client(dev);
	struct lm80_data *data = i2c_get_clientdata(client);
	long val = simple_strtoul(buf, NULL, 10);

	mutex_lock(&data->update_lock);
	data->fan_min[nr] = FAN_TO_REG(val, DIV_FROM_REG(data->fan_div[nr]));
	lm80_write_value(client, LM80_REG_FAN_MIN(nr + 1), data->fan_min[nr]);
	mutex_unlock(&data->update_lock);
	return count;
}

/* Note: we save and restore the fan minimum here, because its value is
   determined in part by the fan divisor.  This follows the principle of
   least surprise; the user doesn't expect the fan minimum to change just
   because the divisor changed. */
static ssize_t set_fan_div(struct device *dev, struct device_attribute *attr,
	const char *buf, size_t count)
{
	int nr = to_sensor_dev_attr(attr)->index;
	struct i2c_client *client = to_i2c_client(dev);
	struct lm80_data *data = i2c_get_clientdata(client);
	unsigned long min, val = simple_strtoul(buf, NULL, 10);
	u8 reg;

	/* Save fan_min */
	mutex_lock(&data->update_lock);
	min = FAN_FROM_REG(data->fan_min[nr],
			   DIV_FROM_REG(data->fan_div[nr]));

	switch (val) {
	case 1: data->fan_div[nr] = 0; break;
	case 2: data->fan_div[nr] = 1; break;
	case 4: data->fan_div[nr] = 2; break;
	case 8: data->fan_div[nr] = 3; break;
	default:
		dev_err(&client->dev, "fan_div value %ld not "
			"supported. Choose one of 1, 2, 4 or 8!\n", val);
		mutex_unlock(&data->update_lock);
		return -EINVAL;
	}

	reg = (lm80_read_value(client, LM80_REG_FANDIV) & ~(3 << (2 * (nr + 1))))
	    | (data->fan_div[nr] << (2 * (nr + 1)));
	lm80_write_value(client, LM80_REG_FANDIV, reg);

	/* Restore fan_min */
	data->fan_min[nr] = FAN_TO_REG(min, DIV_FROM_REG(data->fan_div[nr]));
	lm80_write_value(client, LM80_REG_FAN_MIN(nr + 1), data->fan_min[nr]);
	mutex_unlock(&data->update_lock);

	return count;
}

static ssize_t show_temp_input1(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct lm80_data *data = lm80_update_device(dev);
	return sprintf(buf, "%ld\n", TEMP_FROM_REG(data->temp));
}

#define show_temp(suffix, value) \
static ssize_t show_temp_##suffix(struct device *dev, struct device_attribute *attr, char *buf) \
{ \
	struct lm80_data *data = lm80_update_device(dev); \
	return sprintf(buf, "%d\n", TEMP_LIMIT_FROM_REG(data->value)); \
}
show_temp(hot_max, temp_hot_max);
show_temp(hot_hyst, temp_hot_hyst);
show_temp(os_max, temp_os_max);
show_temp(os_hyst, temp_os_hyst);

#define set_temp(suffix, value, reg) \
static ssize_t set_temp_##suffix(struct device *dev, struct device_attribute *attr, const char *buf, \
	size_t count) \
{ \
	struct i2c_client *client = to_i2c_client(dev); \
	struct lm80_data *data = i2c_get_clientdata(client); \
	long val = simple_strtoul(buf, NULL, 10); \
 \
	mutex_lock(&data->update_lock); \
	data->value = TEMP_LIMIT_TO_REG(val); \
	lm80_write_value(client, reg, data->value); \
	mutex_unlock(&data->update_lock); \
	return count; \
}
set_temp(hot_max, temp_hot_max, LM80_REG_TEMP_HOT_MAX);
set_temp(hot_hyst, temp_hot_hyst, LM80_REG_TEMP_HOT_HYST);
set_temp(os_max, temp_os_max, LM80_REG_TEMP_OS_MAX);
set_temp(os_hyst, temp_os_hyst, LM80_REG_TEMP_OS_HYST);

static ssize_t show_alarms(struct device *dev, struct device_attribute *attr,
			   char *buf)
{
	struct lm80_data *data = lm80_update_device(dev);
	return sprintf(buf, "%u\n", data->alarms);
}

static ssize_t show_alarm(struct device *dev, struct device_attribute *attr,
			  char *buf)
{
	int bitnr = to_sensor_dev_attr(attr)->index;
	struct lm80_data *data = lm80_update_device(dev);
	return sprintf(buf, "%u\n", (data->alarms >> bitnr) & 1);
}

static SENSOR_DEVICE_ATTR(in0_min, S_IWUSR | S_IRUGO,
		show_in_min, set_in_min, 0);
static SENSOR_DEVICE_ATTR(in1_min, S_IWUSR | S_IRUGO,
		show_in_min, set_in_min, 1);
static SENSOR_DEVICE_ATTR(in2_min, S_IWUSR | S_IRUGO,
		show_in_min, set_in_min, 2);
static SENSOR_DEVICE_ATTR(in3_min, S_IWUSR | S_IRUGO,
		show_in_min, set_in_min, 3);
static SENSOR_DEVICE_ATTR(in4_min, S_IWUSR | S_IRUGO,
		show_in_min, set_in_min, 4);
static SENSOR_DEVICE_ATTR(in5_min, S_IWUSR | S_IRUGO,
		show_in_min, set_in_min, 5);
static SENSOR_DEVICE_ATTR(in6_min, S_IWUSR | S_IRUGO,
		show_in_min, set_in_min, 6);
static SENSOR_DEVICE_ATTR(in0_max, S_IWUSR | S_IRUGO,
		show_in_max, set_in_max, 0);
static SENSOR_DEVICE_ATTR(in1_max, S_IWUSR | S_IRUGO,
		show_in_max, set_in_max, 1);
static SENSOR_DEVICE_ATTR(in2_max, S_IWUSR | S_IRUGO,
		show_in_max, set_in_max, 2);
static SENSOR_DEVICE_ATTR(in3_max, S_IWUSR | S_IRUGO,
		show_in_max, set_in_max, 3);
static SENSOR_DEVICE_ATTR(in4_max, S_IWUSR | S_IRUGO,
		show_in_max, set_in_max, 4);
static SENSOR_DEVICE_ATTR(in5_max, S_IWUSR | S_IRUGO,
		show_in_max, set_in_max, 5);
static SENSOR_DEVICE_ATTR(in6_max, S_IWUSR | S_IRUGO,
		show_in_max, set_in_max, 6);
static SENSOR_DEVICE_ATTR(in0_input, S_IRUGO, show_in_input, NULL, 0);
static SENSOR_DEVICE_ATTR(in1_input, S_IRUGO, show_in_input, NULL, 1);
static SENSOR_DEVICE_ATTR(in2_input, S_IRUGO, show_in_input, NULL, 2);
static SENSOR_DEVICE_ATTR(in3_input, S_IRUGO, show_in_input, NULL, 3);
static SENSOR_DEVICE_ATTR(in4_input, S_IRUGO, show_in_input, NULL, 4);
static SENSOR_DEVICE_ATTR(in5_input, S_IRUGO, show_in_input, NULL, 5);
static SENSOR_DEVICE_ATTR(in6_input, S_IRUGO, show_in_input, NULL, 6);
static SENSOR_DEVICE_ATTR(fan1_min, S_IWUSR | S_IRUGO,
		show_fan_min, set_fan_min, 0);
static SENSOR_DEVICE_ATTR(fan2_min, S_IWUSR | S_IRUGO,
		show_fan_min, set_fan_min, 1);
static SENSOR_DEVICE_ATTR(fan1_input, S_IRUGO, show_fan_input, NULL, 0);
static SENSOR_DEVICE_ATTR(fan2_input, S_IRUGO, show_fan_input, NULL, 1);
static SENSOR_DEVICE_ATTR(fan1_div, S_IWUSR | S_IRUGO,
		show_fan_div, set_fan_div, 0);
static SENSOR_DEVICE_ATTR(fan2_div, S_IWUSR | S_IRUGO,
		show_fan_div, set_fan_div, 1);
static DEVICE_ATTR(temp1_input, S_IRUGO, show_temp_input1, NULL);
static DEVICE_ATTR(temp1_max, S_IWUSR | S_IRUGO, show_temp_hot_max,
    set_temp_hot_max);
static DEVICE_ATTR(temp1_max_hyst, S_IWUSR | S_IRUGO, show_temp_hot_hyst,
    set_temp_hot_hyst);
static DEVICE_ATTR(temp1_crit, S_IWUSR | S_IRUGO, show_temp_os_max,
    set_temp_os_max);
static DEVICE_ATTR(temp1_crit_hyst, S_IWUSR | S_IRUGO, show_temp_os_hyst,
    set_temp_os_hyst);
static DEVICE_ATTR(alarms, S_IRUGO, show_alarms, NULL);
static SENSOR_DEVICE_ATTR(in0_alarm, S_IRUGO, show_alarm, NULL, 0);
static SENSOR_DEVICE_ATTR(in1_alarm, S_IRUGO, show_alarm, NULL, 1);
static SENSOR_DEVICE_ATTR(in2_alarm, S_IRUGO, show_alarm, NULL, 2);
static SENSOR_DEVICE_ATTR(in3_alarm, S_IRUGO, show_alarm, NULL, 3);
static SENSOR_DEVICE_ATTR(in4_alarm, S_IRUGO, show_alarm, NULL, 4);
static SENSOR_DEVICE_ATTR(in5_alarm, S_IRUGO, show_alarm, NULL, 5);
static SENSOR_DEVICE_ATTR(in6_alarm, S_IRUGO, show_alarm, NULL, 6);
static SENSOR_DEVICE_ATTR(fan1_alarm, S_IRUGO, show_alarm, NULL, 10);
static SENSOR_DEVICE_ATTR(fan2_alarm, S_IRUGO, show_alarm, NULL, 11);
static SENSOR_DEVICE_ATTR(temp1_max_alarm, S_IRUGO, show_alarm, NULL, 8);
static SENSOR_DEVICE_ATTR(temp1_crit_alarm, S_IRUGO, show_alarm, NULL, 13);

/*
 * Real code
 */

static struct attribute *lm80_attributes[] = {
	&sensor_dev_attr_in0_min.dev_attr.attr,
	&sensor_dev_attr_in1_min.dev_attr.attr,
	&sensor_dev_attr_in2_min.dev_attr.attr,
	&sensor_dev_attr_in3_min.dev_attr.attr,
	&sensor_dev_attr_in4_min.dev_attr.attr,
	&sensor_dev_attr_in5_min.dev_attr.attr,
	&sensor_dev_attr_in6_min.dev_attr.attr,
	&sensor_dev_attr_in0_max.dev_attr.attr,
	&sensor_dev_attr_in1_max.dev_attr.attr,
	&sensor_dev_attr_in2_max.dev_attr.attr,
	&sensor_dev_attr_in3_max.dev_attr.attr,
	&sensor_dev_attr_in4_max.dev_attr.attr,
	&sensor_dev_attr_in5_max.dev_attr.attr,
	&sensor_dev_attr_in6_max.dev_attr.attr,
	&sensor_dev_attr_in0_input.dev_attr.attr,
	&sensor_dev_attr_in1_input.dev_attr.attr,
	&sensor_dev_attr_in2_input.dev_attr.attr,
	&sensor_dev_attr_in3_input.dev_attr.attr,
	&sensor_dev_attr_in4_input.dev_attr.attr,
	&sensor_dev_attr_in5_input.dev_attr.attr,
	&sensor_dev_attr_in6_input.dev_attr.attr,
	&sensor_dev_attr_fan1_min.dev_attr.attr,
	&sensor_dev_attr_fan2_min.dev_attr.attr,
	&sensor_dev_attr_fan1_input.dev_attr.attr,
	&sensor_dev_attr_fan2_input.dev_attr.attr,
	&sensor_dev_attr_fan1_div.dev_attr.attr,
	&sensor_dev_attr_fan2_div.dev_attr.attr,
	&dev_attr_temp1_input.attr,
	&dev_attr_temp1_max.attr,
	&dev_attr_temp1_max_hyst.attr,
	&dev_attr_temp1_crit.attr,
	&dev_attr_temp1_crit_hyst.attr,
	&dev_attr_alarms.attr,
	&sensor_dev_attr_in0_alarm.dev_attr.attr,
	&sensor_dev_attr_in1_alarm.dev_attr.attr,
	&sensor_dev_attr_in2_alarm.dev_attr.attr,
	&sensor_dev_attr_in3_alarm.dev_attr.attr,
	&sensor_dev_attr_in4_alarm.dev_attr.attr,
	&sensor_dev_attr_in5_alarm.dev_attr.attr,
	&sensor_dev_attr_in6_alarm.dev_attr.attr,
	&sensor_dev_attr_fan1_alarm.dev_attr.attr,
	&sensor_dev_attr_fan2_alarm.dev_attr.attr,
	&sensor_dev_attr_temp1_max_alarm.dev_attr.attr,
	&sensor_dev_attr_temp1_crit_alarm.dev_attr.attr,
	NULL
};

static const struct attribute_group lm80_group = {
	.attrs = lm80_attributes,
};

/* Return 0 if detection is successful, -ENODEV otherwise */
static int lm80_detect(struct i2c_client *client, int kind,
		       struct i2c_board_info *info)
{
	struct i2c_adapter *adapter = client->adapter;
	int i, cur;

	if (!i2c_check_functionality(adapter, I2C_FUNC_SMBUS_BYTE_DATA))
		return -ENODEV;

	/* Now, we do the remaining detection. It is lousy. */
	if (lm80_read_value(client, LM80_REG_ALARM2) & 0xc0)
		return -ENODEV;
	for (i = 0x2a; i <= 0x3d; i++) {
		cur = i2c_smbus_read_byte_data(client, i);
		if ((i2c_smbus_read_byte_data(client, i + 0x40) != cur)
		 || (i2c_smbus_read_byte_data(client, i + 0x80) != cur)
		 || (i2c_smbus_read_byte_data(client, i + 0xc0) != cur))
		    return -ENODEV;
	}

	strlcpy(info->type, "lm80", I2C_NAME_SIZE);

	return 0;
}

static int lm80_probe(struct i2c_client *client,
		      const struct i2c_device_id *id)
{
	struct lm80_data *data;
	int err;

	data = kzalloc(sizeof(struct lm80_data), GFP_KERNEL);
	if (!data) {
		err = -ENOMEM;
		goto exit;
	}

	i2c_set_clientdata(client, data);
	mutex_init(&data->update_lock);

	/* Initialize the LM80 chip */
	lm80_init_client(client);

	/* A few vars need to be filled upon startup */
	data->fan_min[0] = lm80_read_value(client, LM80_REG_FAN_MIN(1));
	data->fan_min[1] = lm80_read_value(client, LM80_REG_FAN_MIN(2));

	/* Register sysfs hooks */
	if ((err = sysfs_create_group(&client->dev.kobj, &lm80_group)))
		goto error_free;

	data->hwmon_dev = hwmon_device_register(&client->dev);
	if (IS_ERR(data->hwmon_dev)) {
		err = PTR_ERR(data->hwmon_dev);
		goto error_remove;
	}

	return 0;

error_remove:
	sysfs_remove_group(&client->dev.kobj, &lm80_group);
error_free:
	kfree(data);
exit:
	return err;
}

static int lm80_remove(struct i2c_client *client)
{
	struct lm80_data *data = i2c_get_clientdata(client);

	hwmon_device_unregister(data->hwmon_dev);
	sysfs_remove_group(&client->dev.kobj, &lm80_group);

	kfree(data);
	return 0;
}

static int lm80_read_value(struct i2c_client *client, u8 reg)
{
	return i2c_smbus_read_byte_data(client, reg);
}

static int lm80_write_value(struct i2c_client *client, u8 reg, u8 value)
{
	return i2c_smbus_write_byte_data(client, reg, value);
}

/* Called when we have found a new LM80. */
static void lm80_init_client(struct i2c_client *client)
{
	/* Reset all except Watchdog values and last conversion values
	   This sets fan-divs to 2, among others. This makes most other
	   initializations unnecessary */
	lm80_write_value(client, LM80_REG_CONFIG, 0x80);
	/* Set 11-bit temperature resolution */
	lm80_write_value(client, LM80_REG_RES, 0x08);

	/* Start monitoring */
	lm80_write_value(client, LM80_REG_CONFIG, 0x01);
}

static struct lm80_data *lm80_update_device(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct lm80_data *data = i2c_get_clientdata(client);
	int i;

	mutex_lock(&data->update_lock);

	if (time_after(jiffies, data->last_updated + 2 * HZ) || !data->valid) {
		dev_dbg(&client->dev, "Starting lm80 update\n");
		for (i = 0; i <= 6; i++) {
			data->in[i] =
			    lm80_read_value(client, LM80_REG_IN(i));
			data->in_min[i] =
			    lm80_read_value(client, LM80_REG_IN_MIN(i));
			data->in_max[i] =
			    lm80_read_value(client, LM80_REG_IN_MAX(i));
		}
		data->fan[0] = lm80_read_value(client, LM80_REG_FAN1);
		data->fan_min[0] =
		    lm80_read_value(client, LM80_REG_FAN_MIN(1));
		data->fan[1] = lm80_read_value(client, LM80_REG_FAN2);
		data->fan_min[1] =
		    lm80_read_value(client, LM80_REG_FAN_MIN(2));

		data->temp =
		    (lm80_read_value(client, LM80_REG_TEMP) << 8) |
		    (lm80_read_value(client, LM80_REG_RES) & 0xf0);
		data->temp_os_max =
		    lm80_read_value(client, LM80_REG_TEMP_OS_MAX);
		data->temp_os_hyst =
		    lm80_read_value(client, LM80_REG_TEMP_OS_HYST);
		data->temp_hot_max =
		    lm80_read_value(client, LM80_REG_TEMP_HOT_MAX);
		data->temp_hot_hyst =
		    lm80_read_value(client, LM80_REG_TEMP_HOT_HYST);

		i = lm80_read_value(client, LM80_REG_FANDIV);
		data->fan_div[0] = (i >> 2) & 0x03;
		data->fan_div[1] = (i >> 4) & 0x03;
		data->alarms = lm80_read_value(client, LM80_REG_ALARM1) +
		    (lm80_read_value(client, LM80_REG_ALARM2) << 8);
		data->last_updated = jiffies;
		data->valid = 1;
	}

	mutex_unlock(&data->update_lock);

	return data;
}

static int __init sensors_lm80_init(void)
{
	return i2c_add_driver(&lm80_driver);
}

static void __exit sensors_lm80_exit(void)
{
	i2c_del_driver(&lm80_driver);
}

MODULE_AUTHOR("Frodo Looijaard <frodol@dds.nl> and "
	"Philip Edelbrock <phil@netroedge.com>");
MODULE_DESCRIPTION("LM80 driver");
MODULE_LICENSE("GPL");

module_init(sensors_lm80_init);
module_exit(sensors_lm80_exit);
