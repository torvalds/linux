// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * lm80.c - From lm_sensors, Linux kernel modules for hardware
 *	    monitoring
 * Copyright (C) 1998, 1999  Frodo Looijaard <frodol@dds.nl>
 *			     and Philip Edelbrock <phil@netroedge.com>
 *
 * Ported to Linux 2.6 by Tiago Sousa <mirage@kaotik.org>
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

#define LM96080_REG_CONV_RATE		0x07
#define LM96080_REG_MAN_ID		0x3e
#define LM96080_REG_DEV_ID		0x3f


/*
 * Conversions. Rounding and limit checking is only done on the TO_REG
 * variants. Note that you should be a bit careful with which arguments
 * these macros are called: arguments may be evaluated more than once.
 * Fixing this is just not worth it.
 */

#define IN_TO_REG(val)		(clamp_val(((val) + 5) / 10, 0, 255))
#define IN_FROM_REG(val)	((val) * 10)

static inline unsigned char FAN_TO_REG(unsigned rpm, unsigned div)
{
	if (rpm == 0)
		return 255;
	rpm = clamp_val(rpm, 1, 1000000);
	return clamp_val((1350000 + rpm * div / 2) / (rpm * div), 1, 254);
}

#define FAN_FROM_REG(val, div)	((val) == 0 ? -1 : \
				(val) == 255 ? 0 : 1350000/((div) * (val)))

#define TEMP_FROM_REG(reg)	((reg) * 125 / 32)
#define TEMP_TO_REG(temp)	(DIV_ROUND_CLOSEST(clamp_val((temp), \
					-128000, 127000), 1000) << 8)

#define DIV_FROM_REG(val)		(1 << (val))

enum temp_index {
	t_input = 0,
	t_hot_max,
	t_hot_hyst,
	t_os_max,
	t_os_hyst,
	t_num_temp
};

static const u8 temp_regs[t_num_temp] = {
	[t_input] = LM80_REG_TEMP,
	[t_hot_max] = LM80_REG_TEMP_HOT_MAX,
	[t_hot_hyst] = LM80_REG_TEMP_HOT_HYST,
	[t_os_max] = LM80_REG_TEMP_OS_MAX,
	[t_os_hyst] = LM80_REG_TEMP_OS_HYST,
};

enum in_index {
	i_input = 0,
	i_max,
	i_min,
	i_num_in
};

enum fan_index {
	f_input,
	f_min,
	f_num_fan
};

/*
 * Client data (each client gets its own)
 */

struct lm80_data {
	struct i2c_client *client;
	struct mutex update_lock;
	char error;		/* !=0 if error occurred during last update */
	char valid;		/* !=0 if following fields are valid */
	unsigned long last_updated;	/* In jiffies */

	u8 in[i_num_in][7];	/* Register value, 1st index is enum in_index */
	u8 fan[f_num_fan][2];	/* Register value, 1st index enum fan_index */
	u8 fan_div[2];		/* Register encoding, shifted right */
	s16 temp[t_num_temp];	/* Register values, normalized to 16 bit */
	u16 alarms;		/* Register encoding, combined */
};

static int lm80_read_value(struct i2c_client *client, u8 reg)
{
	return i2c_smbus_read_byte_data(client, reg);
}

static int lm80_write_value(struct i2c_client *client, u8 reg, u8 value)
{
	return i2c_smbus_write_byte_data(client, reg, value);
}

/* Called when we have found a new LM80 and after read errors */
static void lm80_init_client(struct i2c_client *client)
{
	/*
	 * Reset all except Watchdog values and last conversion values
	 * This sets fan-divs to 2, among others. This makes most other
	 * initializations unnecessary
	 */
	lm80_write_value(client, LM80_REG_CONFIG, 0x80);
	/* Set 11-bit temperature resolution */
	lm80_write_value(client, LM80_REG_RES, 0x08);

	/* Start monitoring */
	lm80_write_value(client, LM80_REG_CONFIG, 0x01);
}

static struct lm80_data *lm80_update_device(struct device *dev)
{
	struct lm80_data *data = dev_get_drvdata(dev);
	struct i2c_client *client = data->client;
	int i;
	int rv;
	int prev_rv;
	struct lm80_data *ret = data;

	mutex_lock(&data->update_lock);

	if (data->error)
		lm80_init_client(client);

	if (time_after(jiffies, data->last_updated + 2 * HZ) || !data->valid) {
		dev_dbg(dev, "Starting lm80 update\n");
		for (i = 0; i <= 6; i++) {
			rv = lm80_read_value(client, LM80_REG_IN(i));
			if (rv < 0)
				goto abort;
			data->in[i_input][i] = rv;

			rv = lm80_read_value(client, LM80_REG_IN_MIN(i));
			if (rv < 0)
				goto abort;
			data->in[i_min][i] = rv;

			rv = lm80_read_value(client, LM80_REG_IN_MAX(i));
			if (rv < 0)
				goto abort;
			data->in[i_max][i] = rv;
		}

		rv = lm80_read_value(client, LM80_REG_FAN1);
		if (rv < 0)
			goto abort;
		data->fan[f_input][0] = rv;

		rv = lm80_read_value(client, LM80_REG_FAN_MIN(1));
		if (rv < 0)
			goto abort;
		data->fan[f_min][0] = rv;

		rv = lm80_read_value(client, LM80_REG_FAN2);
		if (rv < 0)
			goto abort;
		data->fan[f_input][1] = rv;

		rv = lm80_read_value(client, LM80_REG_FAN_MIN(2));
		if (rv < 0)
			goto abort;
		data->fan[f_min][1] = rv;

		prev_rv = rv = lm80_read_value(client, LM80_REG_TEMP);
		if (rv < 0)
			goto abort;
		rv = lm80_read_value(client, LM80_REG_RES);
		if (rv < 0)
			goto abort;
		data->temp[t_input] = (prev_rv << 8) | (rv & 0xf0);

		for (i = t_input + 1; i < t_num_temp; i++) {
			rv = lm80_read_value(client, temp_regs[i]);
			if (rv < 0)
				goto abort;
			data->temp[i] = rv << 8;
		}

		rv = lm80_read_value(client, LM80_REG_FANDIV);
		if (rv < 0)
			goto abort;
		data->fan_div[0] = (rv >> 2) & 0x03;
		data->fan_div[1] = (rv >> 4) & 0x03;

		prev_rv = rv = lm80_read_value(client, LM80_REG_ALARM1);
		if (rv < 0)
			goto abort;
		rv = lm80_read_value(client, LM80_REG_ALARM2);
		if (rv < 0)
			goto abort;
		data->alarms = prev_rv + (rv << 8);

		data->last_updated = jiffies;
		data->valid = 1;
		data->error = 0;
	}
	goto done;

abort:
	ret = ERR_PTR(rv);
	data->valid = 0;
	data->error = 1;

done:
	mutex_unlock(&data->update_lock);

	return ret;
}

/*
 * Sysfs stuff
 */

static ssize_t in_show(struct device *dev, struct device_attribute *attr,
		       char *buf)
{
	struct lm80_data *data = lm80_update_device(dev);
	int index = to_sensor_dev_attr_2(attr)->index;
	int nr = to_sensor_dev_attr_2(attr)->nr;

	if (IS_ERR(data))
		return PTR_ERR(data);
	return sprintf(buf, "%d\n", IN_FROM_REG(data->in[nr][index]));
}

static ssize_t in_store(struct device *dev, struct device_attribute *attr,
			const char *buf, size_t count)
{
	struct lm80_data *data = dev_get_drvdata(dev);
	struct i2c_client *client = data->client;
	int index = to_sensor_dev_attr_2(attr)->index;
	int nr = to_sensor_dev_attr_2(attr)->nr;
	long val;
	u8 reg;
	int err = kstrtol(buf, 10, &val);
	if (err < 0)
		return err;

	reg = nr == i_min ? LM80_REG_IN_MIN(index) : LM80_REG_IN_MAX(index);

	mutex_lock(&data->update_lock);
	data->in[nr][index] = IN_TO_REG(val);
	lm80_write_value(client, reg, data->in[nr][index]);
	mutex_unlock(&data->update_lock);
	return count;
}

static ssize_t fan_show(struct device *dev, struct device_attribute *attr,
			char *buf)
{
	int index = to_sensor_dev_attr_2(attr)->index;
	int nr = to_sensor_dev_attr_2(attr)->nr;
	struct lm80_data *data = lm80_update_device(dev);
	if (IS_ERR(data))
		return PTR_ERR(data);
	return sprintf(buf, "%d\n", FAN_FROM_REG(data->fan[nr][index],
		       DIV_FROM_REG(data->fan_div[index])));
}

static ssize_t fan_div_show(struct device *dev, struct device_attribute *attr,
			    char *buf)
{
	int nr = to_sensor_dev_attr(attr)->index;
	struct lm80_data *data = lm80_update_device(dev);
	if (IS_ERR(data))
		return PTR_ERR(data);
	return sprintf(buf, "%d\n", DIV_FROM_REG(data->fan_div[nr]));
}

static ssize_t fan_store(struct device *dev, struct device_attribute *attr,
			 const char *buf, size_t count)
{
	int index = to_sensor_dev_attr_2(attr)->index;
	int nr = to_sensor_dev_attr_2(attr)->nr;
	struct lm80_data *data = dev_get_drvdata(dev);
	struct i2c_client *client = data->client;
	unsigned long val;
	int err = kstrtoul(buf, 10, &val);
	if (err < 0)
		return err;

	mutex_lock(&data->update_lock);
	data->fan[nr][index] = FAN_TO_REG(val,
					  DIV_FROM_REG(data->fan_div[index]));
	lm80_write_value(client, LM80_REG_FAN_MIN(index + 1),
			 data->fan[nr][index]);
	mutex_unlock(&data->update_lock);
	return count;
}

/*
 * Note: we save and restore the fan minimum here, because its value is
 * determined in part by the fan divisor.  This follows the principle of
 * least surprise; the user doesn't expect the fan minimum to change just
 * because the divisor changed.
 */
static ssize_t fan_div_store(struct device *dev,
			     struct device_attribute *attr, const char *buf,
			     size_t count)
{
	int nr = to_sensor_dev_attr(attr)->index;
	struct lm80_data *data = dev_get_drvdata(dev);
	struct i2c_client *client = data->client;
	unsigned long min, val;
	u8 reg;
	int rv;

	rv = kstrtoul(buf, 10, &val);
	if (rv < 0)
		return rv;

	/* Save fan_min */
	mutex_lock(&data->update_lock);
	min = FAN_FROM_REG(data->fan[f_min][nr],
			   DIV_FROM_REG(data->fan_div[nr]));

	switch (val) {
	case 1:
		data->fan_div[nr] = 0;
		break;
	case 2:
		data->fan_div[nr] = 1;
		break;
	case 4:
		data->fan_div[nr] = 2;
		break;
	case 8:
		data->fan_div[nr] = 3;
		break;
	default:
		dev_err(dev,
			"fan_div value %ld not supported. Choose one of 1, 2, 4 or 8!\n",
			val);
		mutex_unlock(&data->update_lock);
		return -EINVAL;
	}

	rv = lm80_read_value(client, LM80_REG_FANDIV);
	if (rv < 0) {
		mutex_unlock(&data->update_lock);
		return rv;
	}
	reg = (rv & ~(3 << (2 * (nr + 1))))
	    | (data->fan_div[nr] << (2 * (nr + 1)));
	lm80_write_value(client, LM80_REG_FANDIV, reg);

	/* Restore fan_min */
	data->fan[f_min][nr] = FAN_TO_REG(min, DIV_FROM_REG(data->fan_div[nr]));
	lm80_write_value(client, LM80_REG_FAN_MIN(nr + 1),
			 data->fan[f_min][nr]);
	mutex_unlock(&data->update_lock);

	return count;
}

static ssize_t temp_show(struct device *dev, struct device_attribute *devattr,
			 char *buf)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	struct lm80_data *data = lm80_update_device(dev);
	if (IS_ERR(data))
		return PTR_ERR(data);
	return sprintf(buf, "%d\n", TEMP_FROM_REG(data->temp[attr->index]));
}

static ssize_t temp_store(struct device *dev,
			  struct device_attribute *devattr, const char *buf,
			  size_t count)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	struct lm80_data *data = dev_get_drvdata(dev);
	struct i2c_client *client = data->client;
	int nr = attr->index;
	long val;
	int err = kstrtol(buf, 10, &val);
	if (err < 0)
		return err;

	mutex_lock(&data->update_lock);
	data->temp[nr] = TEMP_TO_REG(val);
	lm80_write_value(client, temp_regs[nr], data->temp[nr] >> 8);
	mutex_unlock(&data->update_lock);
	return count;
}

static ssize_t alarms_show(struct device *dev, struct device_attribute *attr,
			   char *buf)
{
	struct lm80_data *data = lm80_update_device(dev);
	if (IS_ERR(data))
		return PTR_ERR(data);
	return sprintf(buf, "%u\n", data->alarms);
}

static ssize_t alarm_show(struct device *dev, struct device_attribute *attr,
			  char *buf)
{
	int bitnr = to_sensor_dev_attr(attr)->index;
	struct lm80_data *data = lm80_update_device(dev);
	if (IS_ERR(data))
		return PTR_ERR(data);
	return sprintf(buf, "%u\n", (data->alarms >> bitnr) & 1);
}

static SENSOR_DEVICE_ATTR_2_RW(in0_min, in, i_min, 0);
static SENSOR_DEVICE_ATTR_2_RW(in1_min, in, i_min, 1);
static SENSOR_DEVICE_ATTR_2_RW(in2_min, in, i_min, 2);
static SENSOR_DEVICE_ATTR_2_RW(in3_min, in, i_min, 3);
static SENSOR_DEVICE_ATTR_2_RW(in4_min, in, i_min, 4);
static SENSOR_DEVICE_ATTR_2_RW(in5_min, in, i_min, 5);
static SENSOR_DEVICE_ATTR_2_RW(in6_min, in, i_min, 6);
static SENSOR_DEVICE_ATTR_2_RW(in0_max, in, i_max, 0);
static SENSOR_DEVICE_ATTR_2_RW(in1_max, in, i_max, 1);
static SENSOR_DEVICE_ATTR_2_RW(in2_max, in, i_max, 2);
static SENSOR_DEVICE_ATTR_2_RW(in3_max, in, i_max, 3);
static SENSOR_DEVICE_ATTR_2_RW(in4_max, in, i_max, 4);
static SENSOR_DEVICE_ATTR_2_RW(in5_max, in, i_max, 5);
static SENSOR_DEVICE_ATTR_2_RW(in6_max, in, i_max, 6);
static SENSOR_DEVICE_ATTR_2_RO(in0_input, in, i_input, 0);
static SENSOR_DEVICE_ATTR_2_RO(in1_input, in, i_input, 1);
static SENSOR_DEVICE_ATTR_2_RO(in2_input, in, i_input, 2);
static SENSOR_DEVICE_ATTR_2_RO(in3_input, in, i_input, 3);
static SENSOR_DEVICE_ATTR_2_RO(in4_input, in, i_input, 4);
static SENSOR_DEVICE_ATTR_2_RO(in5_input, in, i_input, 5);
static SENSOR_DEVICE_ATTR_2_RO(in6_input, in, i_input, 6);
static SENSOR_DEVICE_ATTR_2_RW(fan1_min, fan, f_min, 0);
static SENSOR_DEVICE_ATTR_2_RW(fan2_min, fan, f_min, 1);
static SENSOR_DEVICE_ATTR_2_RO(fan1_input, fan, f_input, 0);
static SENSOR_DEVICE_ATTR_2_RO(fan2_input, fan, f_input, 1);
static SENSOR_DEVICE_ATTR_RW(fan1_div, fan_div, 0);
static SENSOR_DEVICE_ATTR_RW(fan2_div, fan_div, 1);
static SENSOR_DEVICE_ATTR_RO(temp1_input, temp, t_input);
static SENSOR_DEVICE_ATTR_RW(temp1_max, temp, t_hot_max);
static SENSOR_DEVICE_ATTR_RW(temp1_max_hyst, temp, t_hot_hyst);
static SENSOR_DEVICE_ATTR_RW(temp1_crit, temp, t_os_max);
static SENSOR_DEVICE_ATTR_RW(temp1_crit_hyst, temp, t_os_hyst);
static DEVICE_ATTR_RO(alarms);
static SENSOR_DEVICE_ATTR_RO(in0_alarm, alarm, 0);
static SENSOR_DEVICE_ATTR_RO(in1_alarm, alarm, 1);
static SENSOR_DEVICE_ATTR_RO(in2_alarm, alarm, 2);
static SENSOR_DEVICE_ATTR_RO(in3_alarm, alarm, 3);
static SENSOR_DEVICE_ATTR_RO(in4_alarm, alarm, 4);
static SENSOR_DEVICE_ATTR_RO(in5_alarm, alarm, 5);
static SENSOR_DEVICE_ATTR_RO(in6_alarm, alarm, 6);
static SENSOR_DEVICE_ATTR_RO(fan1_alarm, alarm, 10);
static SENSOR_DEVICE_ATTR_RO(fan2_alarm, alarm, 11);
static SENSOR_DEVICE_ATTR_RO(temp1_max_alarm, alarm, 8);
static SENSOR_DEVICE_ATTR_RO(temp1_crit_alarm, alarm, 13);

/*
 * Real code
 */

static struct attribute *lm80_attrs[] = {
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
	&sensor_dev_attr_temp1_input.dev_attr.attr,
	&sensor_dev_attr_temp1_max.dev_attr.attr,
	&sensor_dev_attr_temp1_max_hyst.dev_attr.attr,
	&sensor_dev_attr_temp1_crit.dev_attr.attr,
	&sensor_dev_attr_temp1_crit_hyst.dev_attr.attr,
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
ATTRIBUTE_GROUPS(lm80);

/* Return 0 if detection is successful, -ENODEV otherwise */
static int lm80_detect(struct i2c_client *client, struct i2c_board_info *info)
{
	struct i2c_adapter *adapter = client->adapter;
	int i, cur, man_id, dev_id;
	const char *name = NULL;

	if (!i2c_check_functionality(adapter, I2C_FUNC_SMBUS_BYTE_DATA))
		return -ENODEV;

	/* First check for unused bits, common to both chip types */
	if ((lm80_read_value(client, LM80_REG_ALARM2) & 0xc0)
	 || (lm80_read_value(client, LM80_REG_CONFIG) & 0x80))
		return -ENODEV;

	/*
	 * The LM96080 has manufacturer and stepping/die rev registers so we
	 * can just check that. The LM80 does not have such registers so we
	 * have to use a more expensive trick.
	 */
	man_id = lm80_read_value(client, LM96080_REG_MAN_ID);
	dev_id = lm80_read_value(client, LM96080_REG_DEV_ID);
	if (man_id == 0x01 && dev_id == 0x08) {
		/* Check more unused bits for confirmation */
		if (lm80_read_value(client, LM96080_REG_CONV_RATE) & 0xfe)
			return -ENODEV;

		name = "lm96080";
	} else {
		/* Check 6-bit addressing */
		for (i = 0x2a; i <= 0x3d; i++) {
			cur = i2c_smbus_read_byte_data(client, i);
			if ((i2c_smbus_read_byte_data(client, i + 0x40) != cur)
			 || (i2c_smbus_read_byte_data(client, i + 0x80) != cur)
			 || (i2c_smbus_read_byte_data(client, i + 0xc0) != cur))
				return -ENODEV;
		}

		name = "lm80";
	}

	strlcpy(info->type, name, I2C_NAME_SIZE);

	return 0;
}

static int lm80_probe(struct i2c_client *client,
		      const struct i2c_device_id *id)
{
	struct device *dev = &client->dev;
	struct device *hwmon_dev;
	struct lm80_data *data;
	int rv;

	data = devm_kzalloc(dev, sizeof(struct lm80_data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->client = client;
	mutex_init(&data->update_lock);

	/* Initialize the LM80 chip */
	lm80_init_client(client);

	/* A few vars need to be filled upon startup */
	rv = lm80_read_value(client, LM80_REG_FAN_MIN(1));
	if (rv < 0)
		return rv;
	data->fan[f_min][0] = rv;
	rv = lm80_read_value(client, LM80_REG_FAN_MIN(2));
	if (rv < 0)
		return rv;
	data->fan[f_min][1] = rv;

	hwmon_dev = devm_hwmon_device_register_with_groups(dev, client->name,
							   data, lm80_groups);

	return PTR_ERR_OR_ZERO(hwmon_dev);
}

/*
 * Driver data (common to all clients)
 */

static const struct i2c_device_id lm80_id[] = {
	{ "lm80", 0 },
	{ "lm96080", 1 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, lm80_id);

static struct i2c_driver lm80_driver = {
	.class		= I2C_CLASS_HWMON,
	.driver = {
		.name	= "lm80",
	},
	.probe		= lm80_probe,
	.id_table	= lm80_id,
	.detect		= lm80_detect,
	.address_list	= normal_i2c,
};

module_i2c_driver(lm80_driver);

MODULE_AUTHOR("Frodo Looijaard <frodol@dds.nl> and "
	"Philip Edelbrock <phil@netroedge.com>");
MODULE_DESCRIPTION("LM80 driver");
MODULE_LICENSE("GPL");
