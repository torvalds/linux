// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * gl520sm.c - Part of lm_sensors, Linux kernel modules for hardware
 *	       monitoring
 * Copyright (c) 1998, 1999  Frodo Looijaard <frodol@dds.nl>,
 *			     Kyösti Mälkki <kmalkki@cc.hut.fi>
 * Copyright (c) 2005	Maarten Deprez <maartendeprez@users.sourceforge.net>
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/jiffies.h>
#include <linux/i2c.h>
#include <linux/hwmon.h>
#include <linux/hwmon-sysfs.h>
#include <linux/hwmon-vid.h>
#include <linux/err.h>
#include <linux/mutex.h>
#include <linux/sysfs.h>

/* Type of the extra sensor */
static unsigned short extra_sensor_type;
module_param(extra_sensor_type, ushort, 0);
MODULE_PARM_DESC(extra_sensor_type, "Type of extra sensor (0=autodetect, 1=temperature, 2=voltage)");

/* Addresses to scan */
static const unsigned short normal_i2c[] = { 0x2c, 0x2d, I2C_CLIENT_END };

/*
 * Many GL520 constants specified below
 * One of the inputs can be configured as either temp or voltage.
 * That's why _TEMP2 and _IN4 access the same register
 */

/* The GL520 registers */
#define GL520_REG_CHIP_ID		0x00
#define GL520_REG_REVISION		0x01
#define GL520_REG_CONF			0x03
#define GL520_REG_MASK			0x11

#define GL520_REG_VID_INPUT		0x02

static const u8 GL520_REG_IN_INPUT[]	= { 0x15, 0x14, 0x13, 0x0d, 0x0e };
static const u8 GL520_REG_IN_LIMIT[]	= { 0x0c, 0x09, 0x0a, 0x0b };
static const u8 GL520_REG_IN_MIN[]	= { 0x0c, 0x09, 0x0a, 0x0b, 0x18 };
static const u8 GL520_REG_IN_MAX[]	= { 0x0c, 0x09, 0x0a, 0x0b, 0x17 };

static const u8 GL520_REG_TEMP_INPUT[]		= { 0x04, 0x0e };
static const u8 GL520_REG_TEMP_MAX[]		= { 0x05, 0x17 };
static const u8 GL520_REG_TEMP_MAX_HYST[]	= { 0x06, 0x18 };

#define GL520_REG_FAN_INPUT		0x07
#define GL520_REG_FAN_MIN		0x08
#define GL520_REG_FAN_DIV		0x0f
#define GL520_REG_FAN_OFF		GL520_REG_FAN_DIV

#define GL520_REG_ALARMS		0x12
#define GL520_REG_BEEP_MASK		0x10
#define GL520_REG_BEEP_ENABLE		GL520_REG_CONF

/* Client data */
struct gl520_data {
	struct i2c_client *client;
	const struct attribute_group *groups[3];
	struct mutex update_lock;
	char valid;		/* zero until the following fields are valid */
	unsigned long last_updated;	/* in jiffies */

	u8 vid;
	u8 vrm;
	u8 in_input[5];		/* [0] = VVD */
	u8 in_min[5];		/* [0] = VDD */
	u8 in_max[5];		/* [0] = VDD */
	u8 fan_input[2];
	u8 fan_min[2];
	u8 fan_div[2];
	u8 fan_off;
	u8 temp_input[2];
	u8 temp_max[2];
	u8 temp_max_hyst[2];
	u8 alarms;
	u8 beep_enable;
	u8 beep_mask;
	u8 alarm_mask;
	u8 two_temps;
};

/*
 * Registers 0x07 to 0x0c are word-sized, others are byte-sized
 * GL520 uses a high-byte first convention
 */
static int gl520_read_value(struct i2c_client *client, u8 reg)
{
	if ((reg >= 0x07) && (reg <= 0x0c))
		return i2c_smbus_read_word_swapped(client, reg);
	else
		return i2c_smbus_read_byte_data(client, reg);
}

static int gl520_write_value(struct i2c_client *client, u8 reg, u16 value)
{
	if ((reg >= 0x07) && (reg <= 0x0c))
		return i2c_smbus_write_word_swapped(client, reg, value);
	else
		return i2c_smbus_write_byte_data(client, reg, value);
}

static struct gl520_data *gl520_update_device(struct device *dev)
{
	struct gl520_data *data = dev_get_drvdata(dev);
	struct i2c_client *client = data->client;
	int val, i;

	mutex_lock(&data->update_lock);

	if (time_after(jiffies, data->last_updated + 2 * HZ) || !data->valid) {

		dev_dbg(&client->dev, "Starting gl520sm update\n");

		data->alarms = gl520_read_value(client, GL520_REG_ALARMS);
		data->beep_mask = gl520_read_value(client, GL520_REG_BEEP_MASK);
		data->vid = gl520_read_value(client,
					     GL520_REG_VID_INPUT) & 0x1f;

		for (i = 0; i < 4; i++) {
			data->in_input[i] = gl520_read_value(client,
							GL520_REG_IN_INPUT[i]);
			val = gl520_read_value(client, GL520_REG_IN_LIMIT[i]);
			data->in_min[i] = val & 0xff;
			data->in_max[i] = (val >> 8) & 0xff;
		}

		val = gl520_read_value(client, GL520_REG_FAN_INPUT);
		data->fan_input[0] = (val >> 8) & 0xff;
		data->fan_input[1] = val & 0xff;

		val = gl520_read_value(client, GL520_REG_FAN_MIN);
		data->fan_min[0] = (val >> 8) & 0xff;
		data->fan_min[1] = val & 0xff;

		data->temp_input[0] = gl520_read_value(client,
						GL520_REG_TEMP_INPUT[0]);
		data->temp_max[0] = gl520_read_value(client,
						GL520_REG_TEMP_MAX[0]);
		data->temp_max_hyst[0] = gl520_read_value(client,
						GL520_REG_TEMP_MAX_HYST[0]);

		val = gl520_read_value(client, GL520_REG_FAN_DIV);
		data->fan_div[0] = (val >> 6) & 0x03;
		data->fan_div[1] = (val >> 4) & 0x03;
		data->fan_off = (val >> 2) & 0x01;

		data->alarms &= data->alarm_mask;

		val = gl520_read_value(client, GL520_REG_CONF);
		data->beep_enable = !((val >> 2) & 1);

		/* Temp1 and Vin4 are the same input */
		if (data->two_temps) {
			data->temp_input[1] = gl520_read_value(client,
						GL520_REG_TEMP_INPUT[1]);
			data->temp_max[1] = gl520_read_value(client,
						GL520_REG_TEMP_MAX[1]);
			data->temp_max_hyst[1] = gl520_read_value(client,
						GL520_REG_TEMP_MAX_HYST[1]);
		} else {
			data->in_input[4] = gl520_read_value(client,
						GL520_REG_IN_INPUT[4]);
			data->in_min[4] = gl520_read_value(client,
						GL520_REG_IN_MIN[4]);
			data->in_max[4] = gl520_read_value(client,
						GL520_REG_IN_MAX[4]);
		}

		data->last_updated = jiffies;
		data->valid = 1;
	}

	mutex_unlock(&data->update_lock);

	return data;
}

/*
 * Sysfs stuff
 */

static ssize_t cpu0_vid_show(struct device *dev,
			     struct device_attribute *attr, char *buf)
{
	struct gl520_data *data = gl520_update_device(dev);
	return sprintf(buf, "%u\n", vid_from_reg(data->vid, data->vrm));
}
static DEVICE_ATTR_RO(cpu0_vid);

#define VDD_FROM_REG(val)	DIV_ROUND_CLOSEST((val) * 95, 4)
#define VDD_CLAMP(val)		clamp_val(val, 0, 255 * 95 / 4)
#define VDD_TO_REG(val)		DIV_ROUND_CLOSEST(VDD_CLAMP(val) * 4, 95)

#define IN_FROM_REG(val)	((val) * 19)
#define IN_CLAMP(val)		clamp_val(val, 0, 255 * 19)
#define IN_TO_REG(val)		DIV_ROUND_CLOSEST(IN_CLAMP(val), 19)

static ssize_t in_input_show(struct device *dev,
			     struct device_attribute *attr, char *buf)
{
	int n = to_sensor_dev_attr(attr)->index;
	struct gl520_data *data = gl520_update_device(dev);
	u8 r = data->in_input[n];

	if (n == 0)
		return sprintf(buf, "%d\n", VDD_FROM_REG(r));
	else
		return sprintf(buf, "%d\n", IN_FROM_REG(r));
}

static ssize_t in_min_show(struct device *dev, struct device_attribute *attr,
			   char *buf)
{
	int n = to_sensor_dev_attr(attr)->index;
	struct gl520_data *data = gl520_update_device(dev);
	u8 r = data->in_min[n];

	if (n == 0)
		return sprintf(buf, "%d\n", VDD_FROM_REG(r));
	else
		return sprintf(buf, "%d\n", IN_FROM_REG(r));
}

static ssize_t in_max_show(struct device *dev, struct device_attribute *attr,
			   char *buf)
{
	int n = to_sensor_dev_attr(attr)->index;
	struct gl520_data *data = gl520_update_device(dev);
	u8 r = data->in_max[n];

	if (n == 0)
		return sprintf(buf, "%d\n", VDD_FROM_REG(r));
	else
		return sprintf(buf, "%d\n", IN_FROM_REG(r));
}

static ssize_t in_min_store(struct device *dev, struct device_attribute *attr,
			    const char *buf, size_t count)
{
	struct gl520_data *data = dev_get_drvdata(dev);
	struct i2c_client *client = data->client;
	int n = to_sensor_dev_attr(attr)->index;
	u8 r;
	long v;
	int err;

	err = kstrtol(buf, 10, &v);
	if (err)
		return err;

	mutex_lock(&data->update_lock);

	if (n == 0)
		r = VDD_TO_REG(v);
	else
		r = IN_TO_REG(v);

	data->in_min[n] = r;

	if (n < 4)
		gl520_write_value(client, GL520_REG_IN_MIN[n],
				  (gl520_read_value(client, GL520_REG_IN_MIN[n])
				   & ~0xff) | r);
	else
		gl520_write_value(client, GL520_REG_IN_MIN[n], r);

	mutex_unlock(&data->update_lock);
	return count;
}

static ssize_t in_max_store(struct device *dev, struct device_attribute *attr,
			    const char *buf, size_t count)
{
	struct gl520_data *data = dev_get_drvdata(dev);
	struct i2c_client *client = data->client;
	int n = to_sensor_dev_attr(attr)->index;
	u8 r;
	long v;
	int err;

	err = kstrtol(buf, 10, &v);
	if (err)
		return err;

	if (n == 0)
		r = VDD_TO_REG(v);
	else
		r = IN_TO_REG(v);

	mutex_lock(&data->update_lock);

	data->in_max[n] = r;

	if (n < 4)
		gl520_write_value(client, GL520_REG_IN_MAX[n],
				  (gl520_read_value(client, GL520_REG_IN_MAX[n])
				   & ~0xff00) | (r << 8));
	else
		gl520_write_value(client, GL520_REG_IN_MAX[n], r);

	mutex_unlock(&data->update_lock);
	return count;
}

static SENSOR_DEVICE_ATTR_RO(in0_input, in_input, 0);
static SENSOR_DEVICE_ATTR_RO(in1_input, in_input, 1);
static SENSOR_DEVICE_ATTR_RO(in2_input, in_input, 2);
static SENSOR_DEVICE_ATTR_RO(in3_input, in_input, 3);
static SENSOR_DEVICE_ATTR_RO(in4_input, in_input, 4);
static SENSOR_DEVICE_ATTR_RW(in0_min, in_min, 0);
static SENSOR_DEVICE_ATTR_RW(in1_min, in_min, 1);
static SENSOR_DEVICE_ATTR_RW(in2_min, in_min, 2);
static SENSOR_DEVICE_ATTR_RW(in3_min, in_min, 3);
static SENSOR_DEVICE_ATTR_RW(in4_min, in_min, 4);
static SENSOR_DEVICE_ATTR_RW(in0_max, in_max, 0);
static SENSOR_DEVICE_ATTR_RW(in1_max, in_max, 1);
static SENSOR_DEVICE_ATTR_RW(in2_max, in_max, 2);
static SENSOR_DEVICE_ATTR_RW(in3_max, in_max, 3);
static SENSOR_DEVICE_ATTR_RW(in4_max, in_max, 4);

#define DIV_FROM_REG(val) (1 << (val))
#define FAN_FROM_REG(val, div) ((val) == 0 ? 0 : (480000 / ((val) << (div))))

#define FAN_BASE(div)		(480000 >> (div))
#define FAN_CLAMP(val, div)	clamp_val(val, FAN_BASE(div) / 255, \
					  FAN_BASE(div))
#define FAN_TO_REG(val, div)	((val) == 0 ? 0 : \
				 DIV_ROUND_CLOSEST(480000, \
						FAN_CLAMP(val, div) << (div)))

static ssize_t fan_input_show(struct device *dev,
			      struct device_attribute *attr, char *buf)
{
	int n = to_sensor_dev_attr(attr)->index;
	struct gl520_data *data = gl520_update_device(dev);

	return sprintf(buf, "%d\n", FAN_FROM_REG(data->fan_input[n],
						 data->fan_div[n]));
}

static ssize_t fan_min_show(struct device *dev, struct device_attribute *attr,
			    char *buf)
{
	int n = to_sensor_dev_attr(attr)->index;
	struct gl520_data *data = gl520_update_device(dev);

	return sprintf(buf, "%d\n", FAN_FROM_REG(data->fan_min[n],
						 data->fan_div[n]));
}

static ssize_t fan_div_show(struct device *dev, struct device_attribute *attr,
			    char *buf)
{
	int n = to_sensor_dev_attr(attr)->index;
	struct gl520_data *data = gl520_update_device(dev);

	return sprintf(buf, "%d\n", DIV_FROM_REG(data->fan_div[n]));
}

static ssize_t fan1_off_show(struct device *dev,
			     struct device_attribute *attr, char *buf)
{
	struct gl520_data *data = gl520_update_device(dev);
	return sprintf(buf, "%d\n", data->fan_off);
}

static ssize_t fan_min_store(struct device *dev,
			     struct device_attribute *attr, const char *buf,
			     size_t count)
{
	struct gl520_data *data = dev_get_drvdata(dev);
	struct i2c_client *client = data->client;
	int n = to_sensor_dev_attr(attr)->index;
	u8 r;
	unsigned long v;
	int err;

	err = kstrtoul(buf, 10, &v);
	if (err)
		return err;

	mutex_lock(&data->update_lock);
	r = FAN_TO_REG(v, data->fan_div[n]);
	data->fan_min[n] = r;

	if (n == 0)
		gl520_write_value(client, GL520_REG_FAN_MIN,
				  (gl520_read_value(client, GL520_REG_FAN_MIN)
				   & ~0xff00) | (r << 8));
	else
		gl520_write_value(client, GL520_REG_FAN_MIN,
				  (gl520_read_value(client, GL520_REG_FAN_MIN)
				   & ~0xff) | r);

	data->beep_mask = gl520_read_value(client, GL520_REG_BEEP_MASK);
	if (data->fan_min[n] == 0)
		data->alarm_mask &= (n == 0) ? ~0x20 : ~0x40;
	else
		data->alarm_mask |= (n == 0) ? 0x20 : 0x40;
	data->beep_mask &= data->alarm_mask;
	gl520_write_value(client, GL520_REG_BEEP_MASK, data->beep_mask);

	mutex_unlock(&data->update_lock);
	return count;
}

static ssize_t fan_div_store(struct device *dev,
			     struct device_attribute *attr, const char *buf,
			     size_t count)
{
	struct gl520_data *data = dev_get_drvdata(dev);
	struct i2c_client *client = data->client;
	int n = to_sensor_dev_attr(attr)->index;
	u8 r;
	unsigned long v;
	int err;

	err = kstrtoul(buf, 10, &v);
	if (err)
		return err;

	switch (v) {
	case 1:
		r = 0;
		break;
	case 2:
		r = 1;
		break;
	case 4:
		r = 2;
		break;
	case 8:
		r = 3;
		break;
	default:
		dev_err(&client->dev,
	"fan_div value %ld not supported. Choose one of 1, 2, 4 or 8!\n", v);
		return -EINVAL;
	}

	mutex_lock(&data->update_lock);
	data->fan_div[n] = r;

	if (n == 0)
		gl520_write_value(client, GL520_REG_FAN_DIV,
				  (gl520_read_value(client, GL520_REG_FAN_DIV)
				   & ~0xc0) | (r << 6));
	else
		gl520_write_value(client, GL520_REG_FAN_DIV,
				  (gl520_read_value(client, GL520_REG_FAN_DIV)
				   & ~0x30) | (r << 4));

	mutex_unlock(&data->update_lock);
	return count;
}

static ssize_t fan1_off_store(struct device *dev,
			      struct device_attribute *attr, const char *buf,
			      size_t count)
{
	struct gl520_data *data = dev_get_drvdata(dev);
	struct i2c_client *client = data->client;
	u8 r;
	unsigned long v;
	int err;

	err = kstrtoul(buf, 10, &v);
	if (err)
		return err;

	r = (v ? 1 : 0);

	mutex_lock(&data->update_lock);
	data->fan_off = r;
	gl520_write_value(client, GL520_REG_FAN_OFF,
			  (gl520_read_value(client, GL520_REG_FAN_OFF)
			   & ~0x0c) | (r << 2));
	mutex_unlock(&data->update_lock);
	return count;
}

static SENSOR_DEVICE_ATTR_RO(fan1_input, fan_input, 0);
static SENSOR_DEVICE_ATTR_RO(fan2_input, fan_input, 1);
static SENSOR_DEVICE_ATTR_RW(fan1_min, fan_min, 0);
static SENSOR_DEVICE_ATTR_RW(fan2_min, fan_min, 1);
static SENSOR_DEVICE_ATTR_RW(fan1_div, fan_div, 0);
static SENSOR_DEVICE_ATTR_RW(fan2_div, fan_div, 1);
static DEVICE_ATTR_RW(fan1_off);

#define TEMP_FROM_REG(val)	(((val) - 130) * 1000)
#define TEMP_CLAMP(val)		clamp_val(val, -130000, 125000)
#define TEMP_TO_REG(val)	(DIV_ROUND_CLOSEST(TEMP_CLAMP(val), 1000) + 130)

static ssize_t temp_input_show(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	int n = to_sensor_dev_attr(attr)->index;
	struct gl520_data *data = gl520_update_device(dev);

	return sprintf(buf, "%d\n", TEMP_FROM_REG(data->temp_input[n]));
}

static ssize_t temp_max_show(struct device *dev,
			     struct device_attribute *attr, char *buf)
{
	int n = to_sensor_dev_attr(attr)->index;
	struct gl520_data *data = gl520_update_device(dev);

	return sprintf(buf, "%d\n", TEMP_FROM_REG(data->temp_max[n]));
}

static ssize_t temp_max_hyst_show(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	int n = to_sensor_dev_attr(attr)->index;
	struct gl520_data *data = gl520_update_device(dev);

	return sprintf(buf, "%d\n", TEMP_FROM_REG(data->temp_max_hyst[n]));
}

static ssize_t temp_max_store(struct device *dev,
			      struct device_attribute *attr, const char *buf,
			      size_t count)
{
	struct gl520_data *data = dev_get_drvdata(dev);
	struct i2c_client *client = data->client;
	int n = to_sensor_dev_attr(attr)->index;
	long v;
	int err;

	err = kstrtol(buf, 10, &v);
	if (err)
		return err;

	mutex_lock(&data->update_lock);
	data->temp_max[n] = TEMP_TO_REG(v);
	gl520_write_value(client, GL520_REG_TEMP_MAX[n], data->temp_max[n]);
	mutex_unlock(&data->update_lock);
	return count;
}

static ssize_t temp_max_hyst_store(struct device *dev,
				   struct device_attribute *attr,
				   const char *buf, size_t count)
{
	struct gl520_data *data = dev_get_drvdata(dev);
	struct i2c_client *client = data->client;
	int n = to_sensor_dev_attr(attr)->index;
	long v;
	int err;

	err = kstrtol(buf, 10, &v);
	if (err)
		return err;

	mutex_lock(&data->update_lock);
	data->temp_max_hyst[n] = TEMP_TO_REG(v);
	gl520_write_value(client, GL520_REG_TEMP_MAX_HYST[n],
			  data->temp_max_hyst[n]);
	mutex_unlock(&data->update_lock);
	return count;
}

static SENSOR_DEVICE_ATTR_RO(temp1_input, temp_input, 0);
static SENSOR_DEVICE_ATTR_RO(temp2_input, temp_input, 1);
static SENSOR_DEVICE_ATTR_RW(temp1_max, temp_max, 0);
static SENSOR_DEVICE_ATTR_RW(temp2_max, temp_max, 1);
static SENSOR_DEVICE_ATTR_RW(temp1_max_hyst, temp_max_hyst, 0);
static SENSOR_DEVICE_ATTR_RW(temp2_max_hyst, temp_max_hyst, 1);

static ssize_t alarms_show(struct device *dev, struct device_attribute *attr,
			   char *buf)
{
	struct gl520_data *data = gl520_update_device(dev);
	return sprintf(buf, "%d\n", data->alarms);
}

static ssize_t beep_enable_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct gl520_data *data = gl520_update_device(dev);
	return sprintf(buf, "%d\n", data->beep_enable);
}

static ssize_t beep_mask_show(struct device *dev,
			      struct device_attribute *attr, char *buf)
{
	struct gl520_data *data = gl520_update_device(dev);
	return sprintf(buf, "%d\n", data->beep_mask);
}

static ssize_t beep_enable_store(struct device *dev,
				 struct device_attribute *attr,
				 const char *buf, size_t count)
{
	struct gl520_data *data = dev_get_drvdata(dev);
	struct i2c_client *client = data->client;
	u8 r;
	unsigned long v;
	int err;

	err = kstrtoul(buf, 10, &v);
	if (err)
		return err;

	r = (v ? 0 : 1);

	mutex_lock(&data->update_lock);
	data->beep_enable = !r;
	gl520_write_value(client, GL520_REG_BEEP_ENABLE,
			  (gl520_read_value(client, GL520_REG_BEEP_ENABLE)
			   & ~0x04) | (r << 2));
	mutex_unlock(&data->update_lock);
	return count;
}

static ssize_t beep_mask_store(struct device *dev,
			       struct device_attribute *attr, const char *buf,
			       size_t count)
{
	struct gl520_data *data = dev_get_drvdata(dev);
	struct i2c_client *client = data->client;
	unsigned long r;
	int err;

	err = kstrtoul(buf, 10, &r);
	if (err)
		return err;

	mutex_lock(&data->update_lock);
	r &= data->alarm_mask;
	data->beep_mask = r;
	gl520_write_value(client, GL520_REG_BEEP_MASK, r);
	mutex_unlock(&data->update_lock);
	return count;
}

static DEVICE_ATTR_RO(alarms);
static DEVICE_ATTR_RW(beep_enable);
static DEVICE_ATTR_RW(beep_mask);

static ssize_t alarm_show(struct device *dev, struct device_attribute *attr,
			  char *buf)
{
	int bit_nr = to_sensor_dev_attr(attr)->index;
	struct gl520_data *data = gl520_update_device(dev);

	return sprintf(buf, "%d\n", (data->alarms >> bit_nr) & 1);
}

static SENSOR_DEVICE_ATTR_RO(in0_alarm, alarm, 0);
static SENSOR_DEVICE_ATTR_RO(in1_alarm, alarm, 1);
static SENSOR_DEVICE_ATTR_RO(in2_alarm, alarm, 2);
static SENSOR_DEVICE_ATTR_RO(in3_alarm, alarm, 3);
static SENSOR_DEVICE_ATTR_RO(temp1_alarm, alarm, 4);
static SENSOR_DEVICE_ATTR_RO(fan1_alarm, alarm, 5);
static SENSOR_DEVICE_ATTR_RO(fan2_alarm, alarm, 6);
static SENSOR_DEVICE_ATTR_RO(temp2_alarm, alarm, 7);
static SENSOR_DEVICE_ATTR_RO(in4_alarm, alarm, 7);

static ssize_t beep_show(struct device *dev, struct device_attribute *attr,
			 char *buf)
{
	int bitnr = to_sensor_dev_attr(attr)->index;
	struct gl520_data *data = gl520_update_device(dev);

	return sprintf(buf, "%d\n", (data->beep_mask >> bitnr) & 1);
}

static ssize_t beep_store(struct device *dev, struct device_attribute *attr,
			  const char *buf, size_t count)
{
	struct gl520_data *data = dev_get_drvdata(dev);
	struct i2c_client *client = data->client;
	int bitnr = to_sensor_dev_attr(attr)->index;
	unsigned long bit;

	int err;

	err = kstrtoul(buf, 10, &bit);
	if (err)
		return err;
	if (bit & ~1)
		return -EINVAL;

	mutex_lock(&data->update_lock);
	data->beep_mask = gl520_read_value(client, GL520_REG_BEEP_MASK);
	if (bit)
		data->beep_mask |= (1 << bitnr);
	else
		data->beep_mask &= ~(1 << bitnr);
	gl520_write_value(client, GL520_REG_BEEP_MASK, data->beep_mask);
	mutex_unlock(&data->update_lock);
	return count;
}

static SENSOR_DEVICE_ATTR_RW(in0_beep, beep, 0);
static SENSOR_DEVICE_ATTR_RW(in1_beep, beep, 1);
static SENSOR_DEVICE_ATTR_RW(in2_beep, beep, 2);
static SENSOR_DEVICE_ATTR_RW(in3_beep, beep, 3);
static SENSOR_DEVICE_ATTR_RW(temp1_beep, beep, 4);
static SENSOR_DEVICE_ATTR_RW(fan1_beep, beep, 5);
static SENSOR_DEVICE_ATTR_RW(fan2_beep, beep, 6);
static SENSOR_DEVICE_ATTR_RW(temp2_beep, beep, 7);
static SENSOR_DEVICE_ATTR_RW(in4_beep, beep, 7);

static struct attribute *gl520_attributes[] = {
	&dev_attr_cpu0_vid.attr,

	&sensor_dev_attr_in0_input.dev_attr.attr,
	&sensor_dev_attr_in0_min.dev_attr.attr,
	&sensor_dev_attr_in0_max.dev_attr.attr,
	&sensor_dev_attr_in0_alarm.dev_attr.attr,
	&sensor_dev_attr_in0_beep.dev_attr.attr,
	&sensor_dev_attr_in1_input.dev_attr.attr,
	&sensor_dev_attr_in1_min.dev_attr.attr,
	&sensor_dev_attr_in1_max.dev_attr.attr,
	&sensor_dev_attr_in1_alarm.dev_attr.attr,
	&sensor_dev_attr_in1_beep.dev_attr.attr,
	&sensor_dev_attr_in2_input.dev_attr.attr,
	&sensor_dev_attr_in2_min.dev_attr.attr,
	&sensor_dev_attr_in2_max.dev_attr.attr,
	&sensor_dev_attr_in2_alarm.dev_attr.attr,
	&sensor_dev_attr_in2_beep.dev_attr.attr,
	&sensor_dev_attr_in3_input.dev_attr.attr,
	&sensor_dev_attr_in3_min.dev_attr.attr,
	&sensor_dev_attr_in3_max.dev_attr.attr,
	&sensor_dev_attr_in3_alarm.dev_attr.attr,
	&sensor_dev_attr_in3_beep.dev_attr.attr,

	&sensor_dev_attr_fan1_input.dev_attr.attr,
	&sensor_dev_attr_fan1_min.dev_attr.attr,
	&sensor_dev_attr_fan1_div.dev_attr.attr,
	&sensor_dev_attr_fan1_alarm.dev_attr.attr,
	&sensor_dev_attr_fan1_beep.dev_attr.attr,
	&dev_attr_fan1_off.attr,
	&sensor_dev_attr_fan2_input.dev_attr.attr,
	&sensor_dev_attr_fan2_min.dev_attr.attr,
	&sensor_dev_attr_fan2_div.dev_attr.attr,
	&sensor_dev_attr_fan2_alarm.dev_attr.attr,
	&sensor_dev_attr_fan2_beep.dev_attr.attr,

	&sensor_dev_attr_temp1_input.dev_attr.attr,
	&sensor_dev_attr_temp1_max.dev_attr.attr,
	&sensor_dev_attr_temp1_max_hyst.dev_attr.attr,
	&sensor_dev_attr_temp1_alarm.dev_attr.attr,
	&sensor_dev_attr_temp1_beep.dev_attr.attr,

	&dev_attr_alarms.attr,
	&dev_attr_beep_enable.attr,
	&dev_attr_beep_mask.attr,
	NULL
};

static const struct attribute_group gl520_group = {
	.attrs = gl520_attributes,
};

static struct attribute *gl520_attributes_in4[] = {
	&sensor_dev_attr_in4_input.dev_attr.attr,
	&sensor_dev_attr_in4_min.dev_attr.attr,
	&sensor_dev_attr_in4_max.dev_attr.attr,
	&sensor_dev_attr_in4_alarm.dev_attr.attr,
	&sensor_dev_attr_in4_beep.dev_attr.attr,
	NULL
};

static struct attribute *gl520_attributes_temp2[] = {
	&sensor_dev_attr_temp2_input.dev_attr.attr,
	&sensor_dev_attr_temp2_max.dev_attr.attr,
	&sensor_dev_attr_temp2_max_hyst.dev_attr.attr,
	&sensor_dev_attr_temp2_alarm.dev_attr.attr,
	&sensor_dev_attr_temp2_beep.dev_attr.attr,
	NULL
};

static const struct attribute_group gl520_group_in4 = {
	.attrs = gl520_attributes_in4,
};

static const struct attribute_group gl520_group_temp2 = {
	.attrs = gl520_attributes_temp2,
};


/*
 * Real code
 */

/* Return 0 if detection is successful, -ENODEV otherwise */
static int gl520_detect(struct i2c_client *client, struct i2c_board_info *info)
{
	struct i2c_adapter *adapter = client->adapter;

	if (!i2c_check_functionality(adapter, I2C_FUNC_SMBUS_BYTE_DATA |
				     I2C_FUNC_SMBUS_WORD_DATA))
		return -ENODEV;

	/* Determine the chip type. */
	if ((gl520_read_value(client, GL520_REG_CHIP_ID) != 0x20) ||
	    ((gl520_read_value(client, GL520_REG_REVISION) & 0x7f) != 0x00) ||
	    ((gl520_read_value(client, GL520_REG_CONF) & 0x80) != 0x00)) {
		dev_dbg(&client->dev, "Unknown chip type, skipping\n");
		return -ENODEV;
	}

	strlcpy(info->type, "gl520sm", I2C_NAME_SIZE);

	return 0;
}

/* Called when we have found a new GL520SM. */
static void gl520_init_client(struct i2c_client *client)
{
	struct gl520_data *data = i2c_get_clientdata(client);
	u8 oldconf, conf;

	conf = oldconf = gl520_read_value(client, GL520_REG_CONF);

	data->alarm_mask = 0xff;
	data->vrm = vid_which_vrm();

	if (extra_sensor_type == 1)
		conf &= ~0x10;
	else if (extra_sensor_type == 2)
		conf |= 0x10;
	data->two_temps = !(conf & 0x10);

	/* If IRQ# is disabled, we can safely force comparator mode */
	if (!(conf & 0x20))
		conf &= 0xf7;

	/* Enable monitoring if needed */
	conf |= 0x40;

	if (conf != oldconf)
		gl520_write_value(client, GL520_REG_CONF, conf);

	gl520_update_device(&(client->dev));

	if (data->fan_min[0] == 0)
		data->alarm_mask &= ~0x20;
	if (data->fan_min[1] == 0)
		data->alarm_mask &= ~0x40;

	data->beep_mask &= data->alarm_mask;
	gl520_write_value(client, GL520_REG_BEEP_MASK, data->beep_mask);
}

static int gl520_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct device *hwmon_dev;
	struct gl520_data *data;

	data = devm_kzalloc(dev, sizeof(struct gl520_data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	i2c_set_clientdata(client, data);
	mutex_init(&data->update_lock);
	data->client = client;

	/* Initialize the GL520SM chip */
	gl520_init_client(client);

	/* sysfs hooks */
	data->groups[0] = &gl520_group;

	if (data->two_temps)
		data->groups[1] = &gl520_group_temp2;
	else
		data->groups[1] = &gl520_group_in4;

	hwmon_dev = devm_hwmon_device_register_with_groups(dev, client->name,
							   data, data->groups);
	return PTR_ERR_OR_ZERO(hwmon_dev);
}

static const struct i2c_device_id gl520_id[] = {
	{ "gl520sm", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, gl520_id);

static struct i2c_driver gl520_driver = {
	.class		= I2C_CLASS_HWMON,
	.driver = {
		.name	= "gl520sm",
	},
	.probe_new	= gl520_probe,
	.id_table	= gl520_id,
	.detect		= gl520_detect,
	.address_list	= normal_i2c,
};

module_i2c_driver(gl520_driver);

MODULE_AUTHOR("Frodo Looijaard <frodol@dds.nl>, "
	"Kyösti Mälkki <kmalkki@cc.hut.fi>, "
	"Maarten Deprez <maartendeprez@users.sourceforge.net>");
MODULE_DESCRIPTION("GL520SM driver");
MODULE_LICENSE("GPL");
