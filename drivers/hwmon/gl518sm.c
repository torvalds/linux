/*
 * gl518sm.c - Part of lm_sensors, Linux kernel modules for hardware
 *             monitoring
 * Copyright (C) 1998, 1999 Frodo Looijaard <frodol@dds.nl> and
 * Kyosti Malkki <kmalkki@cc.hut.fi>
 * Copyright (C) 2004 Hong-Gunn Chew <hglinux@gunnet.org> and
 * Jean Delvare <khali@linux-fr.org>
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
 *
 * Ported to Linux 2.6 by Hong-Gunn Chew with the help of Jean Delvare
 * and advice of Greg Kroah-Hartman.
 *
 * Notes about the port:
 * Release 0x00 of the GL518SM chipset doesn't support reading of in0,
 * in1 nor in2. The original driver had an ugly workaround to get them
 * anyway (changing limits and watching alarms trigger and wear off).
 * We did not keep that part of the original driver in the Linux 2.6
 * version, since it was making the driver significantly more complex
 * with no real benefit.
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
#include <linux/sysfs.h>

/* Addresses to scan */
static const unsigned short normal_i2c[] = { 0x2c, 0x2d, I2C_CLIENT_END };

/* Insmod parameters */
I2C_CLIENT_INSMOD_2(gl518sm_r00, gl518sm_r80);

/* Many GL518 constants specified below */

/* The GL518 registers */
#define GL518_REG_CHIP_ID	0x00
#define GL518_REG_REVISION	0x01
#define GL518_REG_VENDOR_ID	0x02
#define GL518_REG_CONF		0x03
#define GL518_REG_TEMP_IN	0x04
#define GL518_REG_TEMP_MAX	0x05
#define GL518_REG_TEMP_HYST	0x06
#define GL518_REG_FAN_COUNT	0x07
#define GL518_REG_FAN_LIMIT	0x08
#define GL518_REG_VIN1_LIMIT	0x09
#define GL518_REG_VIN2_LIMIT	0x0a
#define GL518_REG_VIN3_LIMIT	0x0b
#define GL518_REG_VDD_LIMIT	0x0c
#define GL518_REG_VIN3		0x0d
#define GL518_REG_MISC		0x0f
#define GL518_REG_ALARM		0x10
#define GL518_REG_MASK		0x11
#define GL518_REG_INT		0x12
#define GL518_REG_VIN2		0x13
#define GL518_REG_VIN1		0x14
#define GL518_REG_VDD		0x15


/*
 * Conversions. Rounding and limit checking is only done on the TO_REG
 * variants. Note that you should be a bit careful with which arguments
 * these macros are called: arguments may be evaluated more than once.
 * Fixing this is just not worth it.
 */

#define RAW_FROM_REG(val)	val

#define BOOL_FROM_REG(val)	((val)?0:1)
#define BOOL_TO_REG(val)	((val)?0:1)

#define TEMP_TO_REG(val)	(SENSORS_LIMIT(((((val)<0? \
				(val)-500:(val)+500)/1000)+119),0,255))
#define TEMP_FROM_REG(val)	(((val) - 119) * 1000)

static inline u8 FAN_TO_REG(long rpm, int div)
{
	long rpmdiv;
	if (rpm == 0)
		return 0;
	rpmdiv = SENSORS_LIMIT(rpm, 1, 960000) * div;
	return SENSORS_LIMIT((480000 + rpmdiv / 2) / rpmdiv, 1, 255);
}
#define FAN_FROM_REG(val,div)	((val)==0 ? 0 : (480000/((val)*(div))))

#define IN_TO_REG(val)		(SENSORS_LIMIT((((val)+9)/19),0,255))
#define IN_FROM_REG(val)	((val)*19)

#define VDD_TO_REG(val)		(SENSORS_LIMIT((((val)*4+47)/95),0,255))
#define VDD_FROM_REG(val)	(((val)*95+2)/4)

#define DIV_FROM_REG(val)	(1 << (val))

#define BEEP_MASK_TO_REG(val)	((val) & 0x7f & data->alarm_mask)
#define BEEP_MASK_FROM_REG(val)	((val) & 0x7f)

/* Each client has this additional data */
struct gl518_data {
	struct i2c_client client;
	struct device *hwmon_dev;
	enum chips type;

	struct mutex update_lock;
	char valid;		/* !=0 if following fields are valid */
	unsigned long last_updated;	/* In jiffies */

	u8 voltage_in[4];	/* Register values; [0] = VDD */
	u8 voltage_min[4];	/* Register values; [0] = VDD */
	u8 voltage_max[4];	/* Register values; [0] = VDD */
	u8 fan_in[2];
	u8 fan_min[2];
	u8 fan_div[2];		/* Register encoding, shifted right */
	u8 fan_auto1;		/* Boolean */
	u8 temp_in;		/* Register values */
	u8 temp_max;		/* Register values */
	u8 temp_hyst;		/* Register values */
	u8 alarms;		/* Register value */
	u8 alarm_mask;
	u8 beep_mask;		/* Register value */
	u8 beep_enable;		/* Boolean */
};

static int gl518_attach_adapter(struct i2c_adapter *adapter);
static int gl518_detect(struct i2c_adapter *adapter, int address, int kind);
static void gl518_init_client(struct i2c_client *client);
static int gl518_detach_client(struct i2c_client *client);
static int gl518_read_value(struct i2c_client *client, u8 reg);
static int gl518_write_value(struct i2c_client *client, u8 reg, u16 value);
static struct gl518_data *gl518_update_device(struct device *dev);

/* This is the driver that will be inserted */
static struct i2c_driver gl518_driver = {
	.driver = {
		.name	= "gl518sm",
	},
	.attach_adapter	= gl518_attach_adapter,
	.detach_client	= gl518_detach_client,
};

/*
 * Sysfs stuff
 */

#define show(type, suffix, value)					\
static ssize_t show_##suffix(struct device *dev, struct device_attribute *attr, char *buf)		\
{									\
	struct gl518_data *data = gl518_update_device(dev);		\
	return sprintf(buf, "%d\n", type##_FROM_REG(data->value));	\
}

show(TEMP, temp_input1, temp_in);
show(TEMP, temp_max1, temp_max);
show(TEMP, temp_hyst1, temp_hyst);
show(BOOL, fan_auto1, fan_auto1);
show(VDD, in_input0, voltage_in[0]);
show(IN, in_input1, voltage_in[1]);
show(IN, in_input2, voltage_in[2]);
show(IN, in_input3, voltage_in[3]);
show(VDD, in_min0, voltage_min[0]);
show(IN, in_min1, voltage_min[1]);
show(IN, in_min2, voltage_min[2]);
show(IN, in_min3, voltage_min[3]);
show(VDD, in_max0, voltage_max[0]);
show(IN, in_max1, voltage_max[1]);
show(IN, in_max2, voltage_max[2]);
show(IN, in_max3, voltage_max[3]);
show(RAW, alarms, alarms);
show(BOOL, beep_enable, beep_enable);
show(BEEP_MASK, beep_mask, beep_mask);

static ssize_t show_fan_input(struct device *dev,
			      struct device_attribute *attr, char *buf)
{
	int nr = to_sensor_dev_attr(attr)->index;
	struct gl518_data *data = gl518_update_device(dev);
	return sprintf(buf, "%d\n", FAN_FROM_REG(data->fan_in[nr],
					DIV_FROM_REG(data->fan_div[nr])));
}

static ssize_t show_fan_min(struct device *dev,
			    struct device_attribute *attr, char *buf)
{
	int nr = to_sensor_dev_attr(attr)->index;
	struct gl518_data *data = gl518_update_device(dev);
	return sprintf(buf, "%d\n", FAN_FROM_REG(data->fan_min[nr],
					DIV_FROM_REG(data->fan_div[nr])));
}

static ssize_t show_fan_div(struct device *dev,
			    struct device_attribute *attr, char *buf)
{
	int nr = to_sensor_dev_attr(attr)->index;
	struct gl518_data *data = gl518_update_device(dev);
	return sprintf(buf, "%d\n", DIV_FROM_REG(data->fan_div[nr]));
}

#define set(type, suffix, value, reg)					\
static ssize_t set_##suffix(struct device *dev, struct device_attribute *attr, const char *buf,	\
	size_t count)							\
{									\
	struct i2c_client *client = to_i2c_client(dev);			\
	struct gl518_data *data = i2c_get_clientdata(client);		\
	long val = simple_strtol(buf, NULL, 10);			\
									\
	mutex_lock(&data->update_lock);					\
	data->value = type##_TO_REG(val);				\
	gl518_write_value(client, reg, data->value);			\
	mutex_unlock(&data->update_lock);				\
	return count;							\
}

#define set_bits(type, suffix, value, reg, mask, shift)			\
static ssize_t set_##suffix(struct device *dev, struct device_attribute *attr, const char *buf,	\
	size_t count)							\
{									\
	struct i2c_client *client = to_i2c_client(dev);			\
	struct gl518_data *data = i2c_get_clientdata(client);		\
	int regvalue;							\
	unsigned long val = simple_strtoul(buf, NULL, 10);		\
									\
	mutex_lock(&data->update_lock);					\
	regvalue = gl518_read_value(client, reg);			\
	data->value = type##_TO_REG(val);				\
	regvalue = (regvalue & ~mask) | (data->value << shift);		\
	gl518_write_value(client, reg, regvalue);			\
	mutex_unlock(&data->update_lock);				\
	return count;							\
}

#define set_low(type, suffix, value, reg)				\
	set_bits(type, suffix, value, reg, 0x00ff, 0)
#define set_high(type, suffix, value, reg)				\
	set_bits(type, suffix, value, reg, 0xff00, 8)

set(TEMP, temp_max1, temp_max, GL518_REG_TEMP_MAX);
set(TEMP, temp_hyst1, temp_hyst, GL518_REG_TEMP_HYST);
set_bits(BOOL, fan_auto1, fan_auto1, GL518_REG_MISC, 0x08, 3);
set_low(VDD, in_min0, voltage_min[0], GL518_REG_VDD_LIMIT);
set_low(IN, in_min1, voltage_min[1], GL518_REG_VIN1_LIMIT);
set_low(IN, in_min2, voltage_min[2], GL518_REG_VIN2_LIMIT);
set_low(IN, in_min3, voltage_min[3], GL518_REG_VIN3_LIMIT);
set_high(VDD, in_max0, voltage_max[0], GL518_REG_VDD_LIMIT);
set_high(IN, in_max1, voltage_max[1], GL518_REG_VIN1_LIMIT);
set_high(IN, in_max2, voltage_max[2], GL518_REG_VIN2_LIMIT);
set_high(IN, in_max3, voltage_max[3], GL518_REG_VIN3_LIMIT);
set_bits(BOOL, beep_enable, beep_enable, GL518_REG_CONF, 0x04, 2);
set(BEEP_MASK, beep_mask, beep_mask, GL518_REG_ALARM);

static ssize_t set_fan_min(struct device *dev, struct device_attribute *attr,
			   const char *buf, size_t count)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct gl518_data *data = i2c_get_clientdata(client);
	int nr = to_sensor_dev_attr(attr)->index;
	int regvalue;
	unsigned long val = simple_strtoul(buf, NULL, 10);

	mutex_lock(&data->update_lock);
	regvalue = gl518_read_value(client, GL518_REG_FAN_LIMIT);
	data->fan_min[nr] = FAN_TO_REG(val, DIV_FROM_REG(data->fan_div[nr]));
	regvalue = (regvalue & (0xff << (8 * nr)))
		 | (data->fan_min[nr] << (8 * (1 - nr)));
	gl518_write_value(client, GL518_REG_FAN_LIMIT, regvalue);

	data->beep_mask = gl518_read_value(client, GL518_REG_ALARM);
	if (data->fan_min[nr] == 0)
		data->alarm_mask &= ~(0x20 << nr);
	else
		data->alarm_mask |= (0x20 << nr);
	data->beep_mask &= data->alarm_mask;
	gl518_write_value(client, GL518_REG_ALARM, data->beep_mask);

	mutex_unlock(&data->update_lock);
	return count;
}

static ssize_t set_fan_div(struct device *dev, struct device_attribute *attr,
			   const char *buf, size_t count)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct gl518_data *data = i2c_get_clientdata(client);
	int nr = to_sensor_dev_attr(attr)->index;
	int regvalue;
	unsigned long val = simple_strtoul(buf, NULL, 10);

	switch (val) {
	case 1: val = 0; break;
	case 2: val = 1; break;
	case 4: val = 2; break;
	case 8: val = 3; break;
	default:
		dev_err(dev, "Invalid fan clock divider %lu, choose one "
			"of 1, 2, 4 or 8\n", val);
		return -EINVAL;
	}

	mutex_lock(&data->update_lock);
	regvalue = gl518_read_value(client, GL518_REG_MISC);
	data->fan_div[nr] = val;
	regvalue = (regvalue & ~(0xc0 >> (2 * nr)))
		 | (data->fan_div[nr] << (6 - 2 * nr));
	gl518_write_value(client, GL518_REG_MISC, regvalue);
	mutex_unlock(&data->update_lock);
	return count;
}

static DEVICE_ATTR(temp1_input, S_IRUGO, show_temp_input1, NULL);
static DEVICE_ATTR(temp1_max, S_IWUSR|S_IRUGO, show_temp_max1, set_temp_max1);
static DEVICE_ATTR(temp1_max_hyst, S_IWUSR|S_IRUGO,
	show_temp_hyst1, set_temp_hyst1);
static DEVICE_ATTR(fan1_auto, S_IWUSR|S_IRUGO, show_fan_auto1, set_fan_auto1);
static SENSOR_DEVICE_ATTR(fan1_input, S_IRUGO, show_fan_input, NULL, 0);
static SENSOR_DEVICE_ATTR(fan2_input, S_IRUGO, show_fan_input, NULL, 1);
static SENSOR_DEVICE_ATTR(fan1_min, S_IWUSR|S_IRUGO,
	show_fan_min, set_fan_min, 0);
static SENSOR_DEVICE_ATTR(fan2_min, S_IWUSR|S_IRUGO,
	show_fan_min, set_fan_min, 1);
static SENSOR_DEVICE_ATTR(fan1_div, S_IWUSR|S_IRUGO,
	show_fan_div, set_fan_div, 0);
static SENSOR_DEVICE_ATTR(fan2_div, S_IWUSR|S_IRUGO,
	show_fan_div, set_fan_div, 1);
static DEVICE_ATTR(in0_input, S_IRUGO, show_in_input0, NULL);
static DEVICE_ATTR(in1_input, S_IRUGO, show_in_input1, NULL);
static DEVICE_ATTR(in2_input, S_IRUGO, show_in_input2, NULL);
static DEVICE_ATTR(in3_input, S_IRUGO, show_in_input3, NULL);
static DEVICE_ATTR(in0_min, S_IWUSR|S_IRUGO, show_in_min0, set_in_min0);
static DEVICE_ATTR(in1_min, S_IWUSR|S_IRUGO, show_in_min1, set_in_min1);
static DEVICE_ATTR(in2_min, S_IWUSR|S_IRUGO, show_in_min2, set_in_min2);
static DEVICE_ATTR(in3_min, S_IWUSR|S_IRUGO, show_in_min3, set_in_min3);
static DEVICE_ATTR(in0_max, S_IWUSR|S_IRUGO, show_in_max0, set_in_max0);
static DEVICE_ATTR(in1_max, S_IWUSR|S_IRUGO, show_in_max1, set_in_max1);
static DEVICE_ATTR(in2_max, S_IWUSR|S_IRUGO, show_in_max2, set_in_max2);
static DEVICE_ATTR(in3_max, S_IWUSR|S_IRUGO, show_in_max3, set_in_max3);
static DEVICE_ATTR(alarms, S_IRUGO, show_alarms, NULL);
static DEVICE_ATTR(beep_enable, S_IWUSR|S_IRUGO,
	show_beep_enable, set_beep_enable);
static DEVICE_ATTR(beep_mask, S_IWUSR|S_IRUGO,
	show_beep_mask, set_beep_mask);

static ssize_t show_alarm(struct device *dev, struct device_attribute *attr,
			  char *buf)
{
	int bitnr = to_sensor_dev_attr(attr)->index;
	struct gl518_data *data = gl518_update_device(dev);
	return sprintf(buf, "%u\n", (data->alarms >> bitnr) & 1);
}

static SENSOR_DEVICE_ATTR(in0_alarm, S_IRUGO, show_alarm, NULL, 0);
static SENSOR_DEVICE_ATTR(in1_alarm, S_IRUGO, show_alarm, NULL, 1);
static SENSOR_DEVICE_ATTR(in2_alarm, S_IRUGO, show_alarm, NULL, 2);
static SENSOR_DEVICE_ATTR(in3_alarm, S_IRUGO, show_alarm, NULL, 3);
static SENSOR_DEVICE_ATTR(temp1_alarm, S_IRUGO, show_alarm, NULL, 4);
static SENSOR_DEVICE_ATTR(fan1_alarm, S_IRUGO, show_alarm, NULL, 5);
static SENSOR_DEVICE_ATTR(fan2_alarm, S_IRUGO, show_alarm, NULL, 6);

static ssize_t show_beep(struct device *dev, struct device_attribute *attr,
			  char *buf)
{
	int bitnr = to_sensor_dev_attr(attr)->index;
	struct gl518_data *data = gl518_update_device(dev);
	return sprintf(buf, "%u\n", (data->beep_mask >> bitnr) & 1);
}

static ssize_t set_beep(struct device *dev, struct device_attribute *attr,
			const char *buf, size_t count)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct gl518_data *data = i2c_get_clientdata(client);
	int bitnr = to_sensor_dev_attr(attr)->index;
	unsigned long bit;

	bit = simple_strtoul(buf, NULL, 10);
	if (bit & ~1)
		return -EINVAL;

	mutex_lock(&data->update_lock);
	data->beep_mask = gl518_read_value(client, GL518_REG_ALARM);
	if (bit)
		data->beep_mask |= (1 << bitnr);
	else
		data->beep_mask &= ~(1 << bitnr);
	gl518_write_value(client, GL518_REG_ALARM, data->beep_mask);
	mutex_unlock(&data->update_lock);
	return count;
}

static SENSOR_DEVICE_ATTR(in0_beep, S_IRUGO|S_IWUSR, show_beep, set_beep, 0);
static SENSOR_DEVICE_ATTR(in1_beep, S_IRUGO|S_IWUSR, show_beep, set_beep, 1);
static SENSOR_DEVICE_ATTR(in2_beep, S_IRUGO|S_IWUSR, show_beep, set_beep, 2);
static SENSOR_DEVICE_ATTR(in3_beep, S_IRUGO|S_IWUSR, show_beep, set_beep, 3);
static SENSOR_DEVICE_ATTR(temp1_beep, S_IRUGO|S_IWUSR, show_beep, set_beep, 4);
static SENSOR_DEVICE_ATTR(fan1_beep, S_IRUGO|S_IWUSR, show_beep, set_beep, 5);
static SENSOR_DEVICE_ATTR(fan2_beep, S_IRUGO|S_IWUSR, show_beep, set_beep, 6);

static struct attribute *gl518_attributes[] = {
	&dev_attr_in3_input.attr,
	&dev_attr_in0_min.attr,
	&dev_attr_in1_min.attr,
	&dev_attr_in2_min.attr,
	&dev_attr_in3_min.attr,
	&dev_attr_in0_max.attr,
	&dev_attr_in1_max.attr,
	&dev_attr_in2_max.attr,
	&dev_attr_in3_max.attr,
	&sensor_dev_attr_in0_alarm.dev_attr.attr,
	&sensor_dev_attr_in1_alarm.dev_attr.attr,
	&sensor_dev_attr_in2_alarm.dev_attr.attr,
	&sensor_dev_attr_in3_alarm.dev_attr.attr,
	&sensor_dev_attr_in0_beep.dev_attr.attr,
	&sensor_dev_attr_in1_beep.dev_attr.attr,
	&sensor_dev_attr_in2_beep.dev_attr.attr,
	&sensor_dev_attr_in3_beep.dev_attr.attr,

	&dev_attr_fan1_auto.attr,
	&sensor_dev_attr_fan1_input.dev_attr.attr,
	&sensor_dev_attr_fan2_input.dev_attr.attr,
	&sensor_dev_attr_fan1_min.dev_attr.attr,
	&sensor_dev_attr_fan2_min.dev_attr.attr,
	&sensor_dev_attr_fan1_div.dev_attr.attr,
	&sensor_dev_attr_fan2_div.dev_attr.attr,
	&sensor_dev_attr_fan1_alarm.dev_attr.attr,
	&sensor_dev_attr_fan2_alarm.dev_attr.attr,
	&sensor_dev_attr_fan1_beep.dev_attr.attr,
	&sensor_dev_attr_fan2_beep.dev_attr.attr,

	&dev_attr_temp1_input.attr,
	&dev_attr_temp1_max.attr,
	&dev_attr_temp1_max_hyst.attr,
	&sensor_dev_attr_temp1_alarm.dev_attr.attr,
	&sensor_dev_attr_temp1_beep.dev_attr.attr,

	&dev_attr_alarms.attr,
	&dev_attr_beep_enable.attr,
	&dev_attr_beep_mask.attr,
	NULL
};

static const struct attribute_group gl518_group = {
	.attrs = gl518_attributes,
};

static struct attribute *gl518_attributes_r80[] = {
	&dev_attr_in0_input.attr,
	&dev_attr_in1_input.attr,
	&dev_attr_in2_input.attr,
	NULL
};

static const struct attribute_group gl518_group_r80 = {
	.attrs = gl518_attributes_r80,
};

/*
 * Real code
 */

static int gl518_attach_adapter(struct i2c_adapter *adapter)
{
	if (!(adapter->class & I2C_CLASS_HWMON))
		return 0;
	return i2c_probe(adapter, &addr_data, gl518_detect);
}

static int gl518_detect(struct i2c_adapter *adapter, int address, int kind)
{
	int i;
	struct i2c_client *client;
	struct gl518_data *data;
	int err = 0;

	if (!i2c_check_functionality(adapter, I2C_FUNC_SMBUS_BYTE_DATA |
				     I2C_FUNC_SMBUS_WORD_DATA))
		goto exit;

	/* OK. For now, we presume we have a valid client. We now create the
	   client structure, even though we cannot fill it completely yet.
	   But it allows us to access gl518_{read,write}_value. */

	if (!(data = kzalloc(sizeof(struct gl518_data), GFP_KERNEL))) {
		err = -ENOMEM;
		goto exit;
	}

	client = &data->client;
	i2c_set_clientdata(client, data);

	client->addr = address;
	client->adapter = adapter;
	client->driver = &gl518_driver;

	/* Now, we do the remaining detection. */

	if (kind < 0) {
		if ((gl518_read_value(client, GL518_REG_CHIP_ID) != 0x80)
		 || (gl518_read_value(client, GL518_REG_CONF) & 0x80))
			goto exit_free;
	}

	/* Determine the chip type. */
	if (kind <= 0) {
		i = gl518_read_value(client, GL518_REG_REVISION);
		if (i == 0x00) {
			kind = gl518sm_r00;
		} else if (i == 0x80) {
			kind = gl518sm_r80;
		} else {
			if (kind <= 0)
				dev_info(&adapter->dev,
				    "Ignoring 'force' parameter for unknown "
				    "chip at adapter %d, address 0x%02x\n",
				    i2c_adapter_id(adapter), address);
			goto exit_free;
		}
	}

	/* Fill in the remaining client fields */
	strlcpy(client->name, "gl518sm", I2C_NAME_SIZE);
	data->type = kind;
	mutex_init(&data->update_lock);

	/* Tell the I2C layer a new client has arrived */
	if ((err = i2c_attach_client(client)))
		goto exit_free;

	/* Initialize the GL518SM chip */
	data->alarm_mask = 0xff;
	gl518_init_client(client);

	/* Register sysfs hooks */
	if ((err = sysfs_create_group(&client->dev.kobj, &gl518_group)))
		goto exit_detach;
	if (data->type == gl518sm_r80)
		if ((err = sysfs_create_group(&client->dev.kobj,
					      &gl518_group_r80)))
			goto exit_remove_files;

	data->hwmon_dev = hwmon_device_register(&client->dev);
	if (IS_ERR(data->hwmon_dev)) {
		err = PTR_ERR(data->hwmon_dev);
		goto exit_remove_files;
	}

	return 0;

exit_remove_files:
	sysfs_remove_group(&client->dev.kobj, &gl518_group);
	if (data->type == gl518sm_r80)
		sysfs_remove_group(&client->dev.kobj, &gl518_group_r80);
exit_detach:
	i2c_detach_client(client);
exit_free:
	kfree(data);
exit:
	return err;
}


/* Called when we have found a new GL518SM.
   Note that we preserve D4:NoFan2 and D2:beep_enable. */
static void gl518_init_client(struct i2c_client *client)
{
	/* Make sure we leave D7:Reset untouched */
	u8 regvalue = gl518_read_value(client, GL518_REG_CONF) & 0x7f;

	/* Comparator mode (D3=0), standby mode (D6=0) */
	gl518_write_value(client, GL518_REG_CONF, (regvalue &= 0x37));

	/* Never interrupts */
	gl518_write_value(client, GL518_REG_MASK, 0x00);

	/* Clear status register (D5=1), start (D6=1) */
	gl518_write_value(client, GL518_REG_CONF, 0x20 | regvalue);
	gl518_write_value(client, GL518_REG_CONF, 0x40 | regvalue);
}

static int gl518_detach_client(struct i2c_client *client)
{
	struct gl518_data *data = i2c_get_clientdata(client);
	int err;

	hwmon_device_unregister(data->hwmon_dev);
	sysfs_remove_group(&client->dev.kobj, &gl518_group);
	if (data->type == gl518sm_r80)
		sysfs_remove_group(&client->dev.kobj, &gl518_group_r80);

	if ((err = i2c_detach_client(client)))
		return err;

	kfree(data);
	return 0;
}

/* Registers 0x07 to 0x0c are word-sized, others are byte-sized
   GL518 uses a high-byte first convention, which is exactly opposite to
   the SMBus standard. */
static int gl518_read_value(struct i2c_client *client, u8 reg)
{
	if ((reg >= 0x07) && (reg <= 0x0c))
		return swab16(i2c_smbus_read_word_data(client, reg));
	else
		return i2c_smbus_read_byte_data(client, reg);
}

static int gl518_write_value(struct i2c_client *client, u8 reg, u16 value)
{
	if ((reg >= 0x07) && (reg <= 0x0c))
		return i2c_smbus_write_word_data(client, reg, swab16(value));
	else
		return i2c_smbus_write_byte_data(client, reg, value);
}

static struct gl518_data *gl518_update_device(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct gl518_data *data = i2c_get_clientdata(client);
	int val;

	mutex_lock(&data->update_lock);

	if (time_after(jiffies, data->last_updated + HZ + HZ / 2)
	    || !data->valid) {
		dev_dbg(&client->dev, "Starting gl518 update\n");

		data->alarms = gl518_read_value(client, GL518_REG_INT);
		data->beep_mask = gl518_read_value(client, GL518_REG_ALARM);

		val = gl518_read_value(client, GL518_REG_VDD_LIMIT);
		data->voltage_min[0] = val & 0xff;
		data->voltage_max[0] = (val >> 8) & 0xff;
		val = gl518_read_value(client, GL518_REG_VIN1_LIMIT);
		data->voltage_min[1] = val & 0xff;
		data->voltage_max[1] = (val >> 8) & 0xff;
		val = gl518_read_value(client, GL518_REG_VIN2_LIMIT);
		data->voltage_min[2] = val & 0xff;
		data->voltage_max[2] = (val >> 8) & 0xff;
		val = gl518_read_value(client, GL518_REG_VIN3_LIMIT);
		data->voltage_min[3] = val & 0xff;
		data->voltage_max[3] = (val >> 8) & 0xff;

		val = gl518_read_value(client, GL518_REG_FAN_COUNT);
		data->fan_in[0] = (val >> 8) & 0xff;
		data->fan_in[1] = val & 0xff;

		val = gl518_read_value(client, GL518_REG_FAN_LIMIT);
		data->fan_min[0] = (val >> 8) & 0xff;
		data->fan_min[1] = val & 0xff;

		data->temp_in = gl518_read_value(client, GL518_REG_TEMP_IN);
		data->temp_max =
		    gl518_read_value(client, GL518_REG_TEMP_MAX);
		data->temp_hyst =
		    gl518_read_value(client, GL518_REG_TEMP_HYST);

		val = gl518_read_value(client, GL518_REG_MISC);
		data->fan_div[0] = (val >> 6) & 0x03;
		data->fan_div[1] = (val >> 4) & 0x03;
		data->fan_auto1  = (val >> 3) & 0x01;

		data->alarms &= data->alarm_mask;

		val = gl518_read_value(client, GL518_REG_CONF);
		data->beep_enable = (val >> 2) & 1;

		if (data->type != gl518sm_r00) {
			data->voltage_in[0] =
			    gl518_read_value(client, GL518_REG_VDD);
			data->voltage_in[1] =
			    gl518_read_value(client, GL518_REG_VIN1);
			data->voltage_in[2] =
			    gl518_read_value(client, GL518_REG_VIN2);
		}
		data->voltage_in[3] =
		    gl518_read_value(client, GL518_REG_VIN3);

		data->last_updated = jiffies;
		data->valid = 1;
	}

	mutex_unlock(&data->update_lock);

	return data;
}

static int __init sensors_gl518sm_init(void)
{
	return i2c_add_driver(&gl518_driver);
}

static void __exit sensors_gl518sm_exit(void)
{
	i2c_del_driver(&gl518_driver);
}

MODULE_AUTHOR("Frodo Looijaard <frodol@dds.nl>, "
	"Kyosti Malkki <kmalkki@cc.hut.fi> and "
	"Hong-Gunn Chew <hglinux@gunnet.org>");
MODULE_DESCRIPTION("GL518SM driver");
MODULE_LICENSE("GPL");

module_init(sensors_gl518sm_init);
module_exit(sensors_gl518sm_exit);
