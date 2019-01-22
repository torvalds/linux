/*
 * lm87.c
 *
 * Copyright (C) 2000       Frodo Looijaard <frodol@dds.nl>
 *                          Philip Edelbrock <phil@netroedge.com>
 *                          Stephen Rousset <stephen.rousset@rocketlogix.com>
 *                          Dan Eaton <dan.eaton@rocketlogix.com>
 * Copyright (C) 2004-2008  Jean Delvare <jdelvare@suse.de>
 *
 * Original port to Linux 2.6 by Jeff Oliver.
 *
 * The LM87 is a sensor chip made by National Semiconductor. It monitors up
 * to 8 voltages (including its own power source), up to three temperatures
 * (its own plus up to two external ones) and up to two fans. The default
 * configuration is 6 voltages, two temperatures and two fans (see below).
 * Voltages are scaled internally with ratios such that the nominal value of
 * each voltage correspond to a register value of 192 (which means a
 * resolution of about 0.5% of the nominal value). Temperature values are
 * reported with a 1 deg resolution and a 3-4 deg accuracy. Complete
 * datasheet can be obtained from National's website at:
 *   http://www.national.com/pf/LM/LM87.html
 *
 * Some functions share pins, so not all functions are available at the same
 * time. Which are depends on the hardware setup. This driver normally
 * assumes that firmware configured the chip correctly. Where this is not
 * the case, platform code must set the I2C client's platform_data to point
 * to a u8 value to be written to the channel register.
 * For reference, here is the list of exclusive functions:
 *  - in0+in5 (default) or temp3
 *  - fan1 (default) or in6
 *  - fan2 (default) or in7
 *  - VID lines (default) or IRQ lines (not handled by this driver)
 *
 * The LM87 additionally features an analog output, supposedly usable to
 * control the speed of a fan. All new chips use pulse width modulation
 * instead. The LM87 is the only hardware monitoring chipset I know of
 * which uses amplitude modulation. Be careful when using this feature.
 *
 * This driver also supports the ADM1024, a sensor chip made by Analog
 * Devices. That chip is fully compatible with the LM87. Complete
 * datasheet can be obtained from Analog's website at:
 *   http://www.analog.com/en/prod/0,2877,ADM1024,00.html
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
#include <linux/hwmon-vid.h>
#include <linux/err.h>
#include <linux/mutex.h>
#include <linux/regulator/consumer.h>

/*
 * Addresses to scan
 * LM87 has three possible addresses: 0x2c, 0x2d and 0x2e.
 */

static const unsigned short normal_i2c[] = { 0x2c, 0x2d, 0x2e, I2C_CLIENT_END };

/*
 * The LM87 registers
 */

/* nr in 0..5 */
#define LM87_REG_IN(nr)			(0x20 + (nr))
#define LM87_REG_IN_MAX(nr)		(0x2B + (nr) * 2)
#define LM87_REG_IN_MIN(nr)		(0x2C + (nr) * 2)
/* nr in 0..1 */
#define LM87_REG_AIN(nr)		(0x28 + (nr))
#define LM87_REG_AIN_MIN(nr)		(0x1A + (nr))
#define LM87_REG_AIN_MAX(nr)		(0x3B + (nr))

static u8 LM87_REG_TEMP[3] = { 0x27, 0x26, 0x20 };
static u8 LM87_REG_TEMP_HIGH[3] = { 0x39, 0x37, 0x2B };
static u8 LM87_REG_TEMP_LOW[3] = { 0x3A, 0x38, 0x2C };

#define LM87_REG_TEMP_HW_INT_LOCK	0x13
#define LM87_REG_TEMP_HW_EXT_LOCK	0x14
#define LM87_REG_TEMP_HW_INT		0x17
#define LM87_REG_TEMP_HW_EXT		0x18

/* nr in 0..1 */
#define LM87_REG_FAN(nr)		(0x28 + (nr))
#define LM87_REG_FAN_MIN(nr)		(0x3B + (nr))
#define LM87_REG_AOUT			0x19

#define LM87_REG_CONFIG			0x40
#define LM87_REG_CHANNEL_MODE		0x16
#define LM87_REG_VID_FAN_DIV		0x47
#define LM87_REG_VID4			0x49

#define LM87_REG_ALARMS1		0x41
#define LM87_REG_ALARMS2		0x42

#define LM87_REG_COMPANY_ID		0x3E
#define LM87_REG_REVISION		0x3F

/*
 * Conversions and various macros
 * The LM87 uses signed 8-bit values for temperatures.
 */

#define IN_FROM_REG(reg, scale)	(((reg) * (scale) + 96) / 192)
#define IN_TO_REG(val, scale)	((val) <= 0 ? 0 : \
				 (val) >= (scale) * 255 / 192 ? 255 : \
				 ((val) * 192 + (scale) / 2) / (scale))

#define TEMP_FROM_REG(reg)	((reg) * 1000)
#define TEMP_TO_REG(val)	((val) <= -127500 ? -128 : \
				 (val) >= 126500 ? 127 : \
				 (((val) < 0 ? (val) - 500 : \
				   (val) + 500) / 1000))

#define FAN_FROM_REG(reg, div)	((reg) == 255 || (reg) == 0 ? 0 : \
				 (1350000 + (reg)*(div) / 2) / ((reg) * (div)))
#define FAN_TO_REG(val, div)	((val) * (div) * 255 <= 1350000 ? 255 : \
				 (1350000 + (val)*(div) / 2) / ((val) * (div)))

#define FAN_DIV_FROM_REG(reg)	(1 << (reg))

/* analog out is 9.80mV/LSB */
#define AOUT_FROM_REG(reg)	(((reg) * 98 + 5) / 10)
#define AOUT_TO_REG(val)	((val) <= 0 ? 0 : \
				 (val) >= 2500 ? 255 : \
				 ((val) * 10 + 49) / 98)

/* nr in 0..1 */
#define CHAN_NO_FAN(nr)		(1 << (nr))
#define CHAN_TEMP3		(1 << 2)
#define CHAN_VCC_5V		(1 << 3)
#define CHAN_NO_VID		(1 << 7)

/*
 * Client data (each client gets its own)
 */

struct lm87_data {
	struct mutex update_lock;
	char valid; /* zero until following fields are valid */
	unsigned long last_updated; /* In jiffies */

	u8 channel;		/* register value */
	u8 config;		/* original register value */

	u8 in[8];		/* register value */
	u8 in_max[8];		/* register value */
	u8 in_min[8];		/* register value */
	u16 in_scale[8];

	s8 temp[3];		/* register value */
	s8 temp_high[3];	/* register value */
	s8 temp_low[3];		/* register value */
	s8 temp_crit_int;	/* min of two register values */
	s8 temp_crit_ext;	/* min of two register values */

	u8 fan[2];		/* register value */
	u8 fan_min[2];		/* register value */
	u8 fan_div[2];		/* register value, shifted right */
	u8 aout;		/* register value */

	u16 alarms;		/* register values, combined */
	u8 vid;			/* register values, combined */
	u8 vrm;

	const struct attribute_group *attr_groups[6];
};

static inline int lm87_read_value(struct i2c_client *client, u8 reg)
{
	return i2c_smbus_read_byte_data(client, reg);
}

static inline int lm87_write_value(struct i2c_client *client, u8 reg, u8 value)
{
	return i2c_smbus_write_byte_data(client, reg, value);
}

static struct lm87_data *lm87_update_device(struct device *dev)
{
	struct i2c_client *client = dev_get_drvdata(dev);
	struct lm87_data *data = i2c_get_clientdata(client);

	mutex_lock(&data->update_lock);

	if (time_after(jiffies, data->last_updated + HZ) || !data->valid) {
		int i, j;

		dev_dbg(&client->dev, "Updating data.\n");

		i = (data->channel & CHAN_TEMP3) ? 1 : 0;
		j = (data->channel & CHAN_TEMP3) ? 5 : 6;
		for (; i < j; i++) {
			data->in[i] = lm87_read_value(client,
				      LM87_REG_IN(i));
			data->in_min[i] = lm87_read_value(client,
					  LM87_REG_IN_MIN(i));
			data->in_max[i] = lm87_read_value(client,
					  LM87_REG_IN_MAX(i));
		}

		for (i = 0; i < 2; i++) {
			if (data->channel & CHAN_NO_FAN(i)) {
				data->in[6+i] = lm87_read_value(client,
						LM87_REG_AIN(i));
				data->in_max[6+i] = lm87_read_value(client,
						    LM87_REG_AIN_MAX(i));
				data->in_min[6+i] = lm87_read_value(client,
						    LM87_REG_AIN_MIN(i));

			} else {
				data->fan[i] = lm87_read_value(client,
					       LM87_REG_FAN(i));
				data->fan_min[i] = lm87_read_value(client,
						   LM87_REG_FAN_MIN(i));
			}
		}

		j = (data->channel & CHAN_TEMP3) ? 3 : 2;
		for (i = 0 ; i < j; i++) {
			data->temp[i] = lm87_read_value(client,
					LM87_REG_TEMP[i]);
			data->temp_high[i] = lm87_read_value(client,
					     LM87_REG_TEMP_HIGH[i]);
			data->temp_low[i] = lm87_read_value(client,
					    LM87_REG_TEMP_LOW[i]);
		}

		i = lm87_read_value(client, LM87_REG_TEMP_HW_INT_LOCK);
		j = lm87_read_value(client, LM87_REG_TEMP_HW_INT);
		data->temp_crit_int = min(i, j);

		i = lm87_read_value(client, LM87_REG_TEMP_HW_EXT_LOCK);
		j = lm87_read_value(client, LM87_REG_TEMP_HW_EXT);
		data->temp_crit_ext = min(i, j);

		i = lm87_read_value(client, LM87_REG_VID_FAN_DIV);
		data->fan_div[0] = (i >> 4) & 0x03;
		data->fan_div[1] = (i >> 6) & 0x03;
		data->vid = (i & 0x0F)
			  | (lm87_read_value(client, LM87_REG_VID4) & 0x01)
			     << 4;

		data->alarms = lm87_read_value(client, LM87_REG_ALARMS1)
			     | (lm87_read_value(client, LM87_REG_ALARMS2)
				<< 8);
		data->aout = lm87_read_value(client, LM87_REG_AOUT);

		data->last_updated = jiffies;
		data->valid = 1;
	}

	mutex_unlock(&data->update_lock);

	return data;
}

/*
 * Sysfs stuff
 */

static ssize_t in_input_show(struct device *dev,
			     struct device_attribute *attr, char *buf)
{
	struct lm87_data *data = lm87_update_device(dev);
	int nr = to_sensor_dev_attr(attr)->index;

	return sprintf(buf, "%u\n", IN_FROM_REG(data->in[nr],
		       data->in_scale[nr]));
}

static ssize_t in_min_show(struct device *dev, struct device_attribute *attr,
			   char *buf)
{
	struct lm87_data *data = lm87_update_device(dev);
	int nr = to_sensor_dev_attr(attr)->index;

	return sprintf(buf, "%u\n", IN_FROM_REG(data->in_min[nr],
		       data->in_scale[nr]));
}

static ssize_t in_max_show(struct device *dev, struct device_attribute *attr,
			   char *buf)
{
	struct lm87_data *data = lm87_update_device(dev);
	int nr = to_sensor_dev_attr(attr)->index;

	return sprintf(buf, "%u\n", IN_FROM_REG(data->in_max[nr],
		       data->in_scale[nr]));
}

static ssize_t in_min_store(struct device *dev, struct device_attribute *attr,
			    const char *buf, size_t count)
{
	struct i2c_client *client = dev_get_drvdata(dev);
	struct lm87_data *data = i2c_get_clientdata(client);
	int nr = to_sensor_dev_attr(attr)->index;
	long val;
	int err;

	err = kstrtol(buf, 10, &val);
	if (err)
		return err;

	mutex_lock(&data->update_lock);
	data->in_min[nr] = IN_TO_REG(val, data->in_scale[nr]);
	lm87_write_value(client, nr < 6 ? LM87_REG_IN_MIN(nr) :
			 LM87_REG_AIN_MIN(nr - 6), data->in_min[nr]);
	mutex_unlock(&data->update_lock);
	return count;
}

static ssize_t in_max_store(struct device *dev, struct device_attribute *attr,
			    const char *buf, size_t count)
{
	struct i2c_client *client = dev_get_drvdata(dev);
	struct lm87_data *data = i2c_get_clientdata(client);
	int nr = to_sensor_dev_attr(attr)->index;
	long val;
	int err;

	err = kstrtol(buf, 10, &val);
	if (err)
		return err;

	mutex_lock(&data->update_lock);
	data->in_max[nr] = IN_TO_REG(val, data->in_scale[nr]);
	lm87_write_value(client, nr < 6 ? LM87_REG_IN_MAX(nr) :
			 LM87_REG_AIN_MAX(nr - 6), data->in_max[nr]);
	mutex_unlock(&data->update_lock);
	return count;
}

static SENSOR_DEVICE_ATTR_RO(in0_input, in_input, 0);
static SENSOR_DEVICE_ATTR_RW(in0_min, in_min, 0);
static SENSOR_DEVICE_ATTR_RW(in0_max, in_max, 0);
static SENSOR_DEVICE_ATTR_RO(in1_input, in_input, 1);
static SENSOR_DEVICE_ATTR_RW(in1_min, in_min, 1);
static SENSOR_DEVICE_ATTR_RW(in1_max, in_max, 1);
static SENSOR_DEVICE_ATTR_RO(in2_input, in_input, 2);
static SENSOR_DEVICE_ATTR_RW(in2_min, in_min, 2);
static SENSOR_DEVICE_ATTR_RW(in2_max, in_max, 2);
static SENSOR_DEVICE_ATTR_RO(in3_input, in_input, 3);
static SENSOR_DEVICE_ATTR_RW(in3_min, in_min, 3);
static SENSOR_DEVICE_ATTR_RW(in3_max, in_max, 3);
static SENSOR_DEVICE_ATTR_RO(in4_input, in_input, 4);
static SENSOR_DEVICE_ATTR_RW(in4_min, in_min, 4);
static SENSOR_DEVICE_ATTR_RW(in4_max, in_max, 4);
static SENSOR_DEVICE_ATTR_RO(in5_input, in_input, 5);
static SENSOR_DEVICE_ATTR_RW(in5_min, in_min, 5);
static SENSOR_DEVICE_ATTR_RW(in5_max, in_max, 5);
static SENSOR_DEVICE_ATTR_RO(in6_input, in_input, 6);
static SENSOR_DEVICE_ATTR_RW(in6_min, in_min, 6);
static SENSOR_DEVICE_ATTR_RW(in6_max, in_max, 6);
static SENSOR_DEVICE_ATTR_RO(in7_input, in_input, 7);
static SENSOR_DEVICE_ATTR_RW(in7_min, in_min, 7);
static SENSOR_DEVICE_ATTR_RW(in7_max, in_max, 7);

static ssize_t temp_input_show(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	struct lm87_data *data = lm87_update_device(dev);
	int nr = to_sensor_dev_attr(attr)->index;

	return sprintf(buf, "%d\n", TEMP_FROM_REG(data->temp[nr]));
}

static ssize_t temp_low_show(struct device *dev,
			     struct device_attribute *attr, char *buf)
{
	struct lm87_data *data = lm87_update_device(dev);
	int nr = to_sensor_dev_attr(attr)->index;

	return sprintf(buf, "%d\n",
		       TEMP_FROM_REG(data->temp_low[nr]));
}

static ssize_t temp_high_show(struct device *dev,
			      struct device_attribute *attr, char *buf)
{
	struct lm87_data *data = lm87_update_device(dev);
	int nr = to_sensor_dev_attr(attr)->index;

	return sprintf(buf, "%d\n",
		       TEMP_FROM_REG(data->temp_high[nr]));
}

static ssize_t temp_low_store(struct device *dev,
			      struct device_attribute *attr, const char *buf,
			      size_t count)
{
	struct i2c_client *client = dev_get_drvdata(dev);
	struct lm87_data *data = i2c_get_clientdata(client);
	int nr = to_sensor_dev_attr(attr)->index;
	long val;
	int err;

	err = kstrtol(buf, 10, &val);
	if (err)
		return err;

	mutex_lock(&data->update_lock);
	data->temp_low[nr] = TEMP_TO_REG(val);
	lm87_write_value(client, LM87_REG_TEMP_LOW[nr], data->temp_low[nr]);
	mutex_unlock(&data->update_lock);
	return count;
}

static ssize_t temp_high_store(struct device *dev,
			       struct device_attribute *attr, const char *buf,
			       size_t count)
{
	struct i2c_client *client = dev_get_drvdata(dev);
	struct lm87_data *data = i2c_get_clientdata(client);
	int nr = to_sensor_dev_attr(attr)->index;
	long val;
	int err;

	err = kstrtol(buf, 10, &val);
	if (err)
		return err;

	mutex_lock(&data->update_lock);
	data->temp_high[nr] = TEMP_TO_REG(val);
	lm87_write_value(client, LM87_REG_TEMP_HIGH[nr], data->temp_high[nr]);
	mutex_unlock(&data->update_lock);
	return count;
}

static SENSOR_DEVICE_ATTR_RO(temp1_input, temp_input, 0);
static SENSOR_DEVICE_ATTR_RW(temp1_min, temp_low, 0);
static SENSOR_DEVICE_ATTR_RW(temp1_max, temp_high, 0);
static SENSOR_DEVICE_ATTR_RO(temp2_input, temp_input, 1);
static SENSOR_DEVICE_ATTR_RW(temp2_min, temp_low, 1);
static SENSOR_DEVICE_ATTR_RW(temp2_max, temp_high, 1);
static SENSOR_DEVICE_ATTR_RO(temp3_input, temp_input, 2);
static SENSOR_DEVICE_ATTR_RW(temp3_min, temp_low, 2);
static SENSOR_DEVICE_ATTR_RW(temp3_max, temp_high, 2);

static ssize_t temp1_crit_show(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	struct lm87_data *data = lm87_update_device(dev);
	return sprintf(buf, "%d\n", TEMP_FROM_REG(data->temp_crit_int));
}

static ssize_t temp2_crit_show(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	struct lm87_data *data = lm87_update_device(dev);
	return sprintf(buf, "%d\n", TEMP_FROM_REG(data->temp_crit_ext));
}

static DEVICE_ATTR_RO(temp1_crit);
static DEVICE_ATTR_RO(temp2_crit);
static DEVICE_ATTR(temp3_crit, 0444, temp2_crit_show, NULL);

static ssize_t fan_input_show(struct device *dev,
			      struct device_attribute *attr, char *buf)
{
	struct lm87_data *data = lm87_update_device(dev);
	int nr = to_sensor_dev_attr(attr)->index;

	return sprintf(buf, "%d\n", FAN_FROM_REG(data->fan[nr],
		       FAN_DIV_FROM_REG(data->fan_div[nr])));
}

static ssize_t fan_min_show(struct device *dev, struct device_attribute *attr,
			    char *buf)
{
	struct lm87_data *data = lm87_update_device(dev);
	int nr = to_sensor_dev_attr(attr)->index;

	return sprintf(buf, "%d\n", FAN_FROM_REG(data->fan_min[nr],
		       FAN_DIV_FROM_REG(data->fan_div[nr])));
}

static ssize_t fan_div_show(struct device *dev, struct device_attribute *attr,
			    char *buf)
{
	struct lm87_data *data = lm87_update_device(dev);
	int nr = to_sensor_dev_attr(attr)->index;

	return sprintf(buf, "%d\n",
		       FAN_DIV_FROM_REG(data->fan_div[nr]));
}

static ssize_t fan_min_store(struct device *dev,
			     struct device_attribute *attr, const char *buf,
			     size_t count)
{
	struct i2c_client *client = dev_get_drvdata(dev);
	struct lm87_data *data = i2c_get_clientdata(client);
	int nr = to_sensor_dev_attr(attr)->index;
	long val;
	int err;

	err = kstrtol(buf, 10, &val);
	if (err)
		return err;

	mutex_lock(&data->update_lock);
	data->fan_min[nr] = FAN_TO_REG(val,
			    FAN_DIV_FROM_REG(data->fan_div[nr]));
	lm87_write_value(client, LM87_REG_FAN_MIN(nr), data->fan_min[nr]);
	mutex_unlock(&data->update_lock);
	return count;
}

/*
 * Note: we save and restore the fan minimum here, because its value is
 * determined in part by the fan clock divider.  This follows the principle
 * of least surprise; the user doesn't expect the fan minimum to change just
 * because the divider changed.
 */
static ssize_t fan_div_store(struct device *dev,
			     struct device_attribute *attr, const char *buf,
			     size_t count)
{
	struct i2c_client *client = dev_get_drvdata(dev);
	struct lm87_data *data = i2c_get_clientdata(client);
	int nr = to_sensor_dev_attr(attr)->index;
	long val;
	int err;
	unsigned long min;
	u8 reg;

	err = kstrtol(buf, 10, &val);
	if (err)
		return err;

	mutex_lock(&data->update_lock);
	min = FAN_FROM_REG(data->fan_min[nr],
			   FAN_DIV_FROM_REG(data->fan_div[nr]));

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
		mutex_unlock(&data->update_lock);
		return -EINVAL;
	}

	reg = lm87_read_value(client, LM87_REG_VID_FAN_DIV);
	switch (nr) {
	case 0:
	    reg = (reg & 0xCF) | (data->fan_div[0] << 4);
	    break;
	case 1:
	    reg = (reg & 0x3F) | (data->fan_div[1] << 6);
	    break;
	}
	lm87_write_value(client, LM87_REG_VID_FAN_DIV, reg);

	data->fan_min[nr] = FAN_TO_REG(min, val);
	lm87_write_value(client, LM87_REG_FAN_MIN(nr),
			 data->fan_min[nr]);
	mutex_unlock(&data->update_lock);

	return count;
}

static SENSOR_DEVICE_ATTR_RO(fan1_input, fan_input, 0);
static SENSOR_DEVICE_ATTR_RW(fan1_min, fan_min, 0);
static SENSOR_DEVICE_ATTR_RW(fan1_div, fan_div, 0);
static SENSOR_DEVICE_ATTR_RO(fan2_input, fan_input, 1);
static SENSOR_DEVICE_ATTR_RW(fan2_min, fan_min, 1);
static SENSOR_DEVICE_ATTR_RW(fan2_div, fan_div, 1);

static ssize_t alarms_show(struct device *dev, struct device_attribute *attr,
			   char *buf)
{
	struct lm87_data *data = lm87_update_device(dev);
	return sprintf(buf, "%d\n", data->alarms);
}
static DEVICE_ATTR_RO(alarms);

static ssize_t cpu0_vid_show(struct device *dev,
			     struct device_attribute *attr, char *buf)
{
	struct lm87_data *data = lm87_update_device(dev);
	return sprintf(buf, "%d\n", vid_from_reg(data->vid, data->vrm));
}
static DEVICE_ATTR_RO(cpu0_vid);

static ssize_t vrm_show(struct device *dev, struct device_attribute *attr,
			char *buf)
{
	struct lm87_data *data = dev_get_drvdata(dev);
	return sprintf(buf, "%d\n", data->vrm);
}
static ssize_t vrm_store(struct device *dev, struct device_attribute *attr,
			 const char *buf, size_t count)
{
	struct lm87_data *data = dev_get_drvdata(dev);
	unsigned long val;
	int err;

	err = kstrtoul(buf, 10, &val);
	if (err)
		return err;

	if (val > 255)
		return -EINVAL;

	data->vrm = val;
	return count;
}
static DEVICE_ATTR_RW(vrm);

static ssize_t aout_output_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct lm87_data *data = lm87_update_device(dev);
	return sprintf(buf, "%d\n", AOUT_FROM_REG(data->aout));
}
static ssize_t aout_output_store(struct device *dev,
				 struct device_attribute *attr,
				 const char *buf, size_t count)
{
	struct i2c_client *client = dev_get_drvdata(dev);
	struct lm87_data *data = i2c_get_clientdata(client);
	long val;
	int err;

	err = kstrtol(buf, 10, &val);
	if (err)
		return err;

	mutex_lock(&data->update_lock);
	data->aout = AOUT_TO_REG(val);
	lm87_write_value(client, LM87_REG_AOUT, data->aout);
	mutex_unlock(&data->update_lock);
	return count;
}
static DEVICE_ATTR_RW(aout_output);

static ssize_t alarm_show(struct device *dev, struct device_attribute *attr,
			  char *buf)
{
	struct lm87_data *data = lm87_update_device(dev);
	int bitnr = to_sensor_dev_attr(attr)->index;
	return sprintf(buf, "%u\n", (data->alarms >> bitnr) & 1);
}
static SENSOR_DEVICE_ATTR_RO(in0_alarm, alarm, 0);
static SENSOR_DEVICE_ATTR_RO(in1_alarm, alarm, 1);
static SENSOR_DEVICE_ATTR_RO(in2_alarm, alarm, 2);
static SENSOR_DEVICE_ATTR_RO(in3_alarm, alarm, 3);
static SENSOR_DEVICE_ATTR_RO(in4_alarm, alarm, 8);
static SENSOR_DEVICE_ATTR_RO(in5_alarm, alarm, 9);
static SENSOR_DEVICE_ATTR_RO(in6_alarm, alarm, 6);
static SENSOR_DEVICE_ATTR_RO(in7_alarm, alarm, 7);
static SENSOR_DEVICE_ATTR_RO(temp1_alarm, alarm, 4);
static SENSOR_DEVICE_ATTR_RO(temp2_alarm, alarm, 5);
static SENSOR_DEVICE_ATTR_RO(temp3_alarm, alarm, 5);
static SENSOR_DEVICE_ATTR_RO(fan1_alarm, alarm, 6);
static SENSOR_DEVICE_ATTR_RO(fan2_alarm, alarm, 7);
static SENSOR_DEVICE_ATTR_RO(temp2_fault, alarm, 14);
static SENSOR_DEVICE_ATTR_RO(temp3_fault, alarm, 15);

/*
 * Real code
 */

static struct attribute *lm87_attributes[] = {
	&sensor_dev_attr_in1_input.dev_attr.attr,
	&sensor_dev_attr_in1_min.dev_attr.attr,
	&sensor_dev_attr_in1_max.dev_attr.attr,
	&sensor_dev_attr_in1_alarm.dev_attr.attr,
	&sensor_dev_attr_in2_input.dev_attr.attr,
	&sensor_dev_attr_in2_min.dev_attr.attr,
	&sensor_dev_attr_in2_max.dev_attr.attr,
	&sensor_dev_attr_in2_alarm.dev_attr.attr,
	&sensor_dev_attr_in3_input.dev_attr.attr,
	&sensor_dev_attr_in3_min.dev_attr.attr,
	&sensor_dev_attr_in3_max.dev_attr.attr,
	&sensor_dev_attr_in3_alarm.dev_attr.attr,
	&sensor_dev_attr_in4_input.dev_attr.attr,
	&sensor_dev_attr_in4_min.dev_attr.attr,
	&sensor_dev_attr_in4_max.dev_attr.attr,
	&sensor_dev_attr_in4_alarm.dev_attr.attr,

	&sensor_dev_attr_temp1_input.dev_attr.attr,
	&sensor_dev_attr_temp1_max.dev_attr.attr,
	&sensor_dev_attr_temp1_min.dev_attr.attr,
	&dev_attr_temp1_crit.attr,
	&sensor_dev_attr_temp1_alarm.dev_attr.attr,
	&sensor_dev_attr_temp2_input.dev_attr.attr,
	&sensor_dev_attr_temp2_max.dev_attr.attr,
	&sensor_dev_attr_temp2_min.dev_attr.attr,
	&dev_attr_temp2_crit.attr,
	&sensor_dev_attr_temp2_alarm.dev_attr.attr,
	&sensor_dev_attr_temp2_fault.dev_attr.attr,

	&dev_attr_alarms.attr,
	&dev_attr_aout_output.attr,

	NULL
};

static const struct attribute_group lm87_group = {
	.attrs = lm87_attributes,
};

static struct attribute *lm87_attributes_in6[] = {
	&sensor_dev_attr_in6_input.dev_attr.attr,
	&sensor_dev_attr_in6_min.dev_attr.attr,
	&sensor_dev_attr_in6_max.dev_attr.attr,
	&sensor_dev_attr_in6_alarm.dev_attr.attr,
	NULL
};

static const struct attribute_group lm87_group_in6 = {
	.attrs = lm87_attributes_in6,
};

static struct attribute *lm87_attributes_fan1[] = {
	&sensor_dev_attr_fan1_input.dev_attr.attr,
	&sensor_dev_attr_fan1_min.dev_attr.attr,
	&sensor_dev_attr_fan1_div.dev_attr.attr,
	&sensor_dev_attr_fan1_alarm.dev_attr.attr,
	NULL
};

static const struct attribute_group lm87_group_fan1 = {
	.attrs = lm87_attributes_fan1,
};

static struct attribute *lm87_attributes_in7[] = {
	&sensor_dev_attr_in7_input.dev_attr.attr,
	&sensor_dev_attr_in7_min.dev_attr.attr,
	&sensor_dev_attr_in7_max.dev_attr.attr,
	&sensor_dev_attr_in7_alarm.dev_attr.attr,
	NULL
};

static const struct attribute_group lm87_group_in7 = {
	.attrs = lm87_attributes_in7,
};

static struct attribute *lm87_attributes_fan2[] = {
	&sensor_dev_attr_fan2_input.dev_attr.attr,
	&sensor_dev_attr_fan2_min.dev_attr.attr,
	&sensor_dev_attr_fan2_div.dev_attr.attr,
	&sensor_dev_attr_fan2_alarm.dev_attr.attr,
	NULL
};

static const struct attribute_group lm87_group_fan2 = {
	.attrs = lm87_attributes_fan2,
};

static struct attribute *lm87_attributes_temp3[] = {
	&sensor_dev_attr_temp3_input.dev_attr.attr,
	&sensor_dev_attr_temp3_max.dev_attr.attr,
	&sensor_dev_attr_temp3_min.dev_attr.attr,
	&dev_attr_temp3_crit.attr,
	&sensor_dev_attr_temp3_alarm.dev_attr.attr,
	&sensor_dev_attr_temp3_fault.dev_attr.attr,
	NULL
};

static const struct attribute_group lm87_group_temp3 = {
	.attrs = lm87_attributes_temp3,
};

static struct attribute *lm87_attributes_in0_5[] = {
	&sensor_dev_attr_in0_input.dev_attr.attr,
	&sensor_dev_attr_in0_min.dev_attr.attr,
	&sensor_dev_attr_in0_max.dev_attr.attr,
	&sensor_dev_attr_in0_alarm.dev_attr.attr,
	&sensor_dev_attr_in5_input.dev_attr.attr,
	&sensor_dev_attr_in5_min.dev_attr.attr,
	&sensor_dev_attr_in5_max.dev_attr.attr,
	&sensor_dev_attr_in5_alarm.dev_attr.attr,
	NULL
};

static const struct attribute_group lm87_group_in0_5 = {
	.attrs = lm87_attributes_in0_5,
};

static struct attribute *lm87_attributes_vid[] = {
	&dev_attr_cpu0_vid.attr,
	&dev_attr_vrm.attr,
	NULL
};

static const struct attribute_group lm87_group_vid = {
	.attrs = lm87_attributes_vid,
};

/* Return 0 if detection is successful, -ENODEV otherwise */
static int lm87_detect(struct i2c_client *client, struct i2c_board_info *info)
{
	struct i2c_adapter *adapter = client->adapter;
	const char *name;
	u8 cid, rev;

	if (!i2c_check_functionality(adapter, I2C_FUNC_SMBUS_BYTE_DATA))
		return -ENODEV;

	if (lm87_read_value(client, LM87_REG_CONFIG) & 0x80)
		return -ENODEV;

	/* Now, we do the remaining detection. */
	cid = lm87_read_value(client, LM87_REG_COMPANY_ID);
	rev = lm87_read_value(client, LM87_REG_REVISION);

	if (cid == 0x02			/* National Semiconductor */
	 && (rev >= 0x01 && rev <= 0x08))
		name = "lm87";
	else if (cid == 0x41		/* Analog Devices */
	      && (rev & 0xf0) == 0x10)
		name = "adm1024";
	else {
		dev_dbg(&adapter->dev, "LM87 detection failed at 0x%02x\n",
			client->addr);
		return -ENODEV;
	}

	strlcpy(info->type, name, I2C_NAME_SIZE);

	return 0;
}

static void lm87_restore_config(void *arg)
{
	struct i2c_client *client = arg;
	struct lm87_data *data = i2c_get_clientdata(client);

	lm87_write_value(client, LM87_REG_CONFIG, data->config);
}

static int lm87_init_client(struct i2c_client *client)
{
	struct lm87_data *data = i2c_get_clientdata(client);
	int rc;
	struct device_node *of_node = client->dev.of_node;
	u8 val = 0;
	struct regulator *vcc = NULL;

	if (of_node) {
		if (of_property_read_bool(of_node, "has-temp3"))
			val |= CHAN_TEMP3;
		if (of_property_read_bool(of_node, "has-in6"))
			val |= CHAN_NO_FAN(0);
		if (of_property_read_bool(of_node, "has-in7"))
			val |= CHAN_NO_FAN(1);
		vcc = devm_regulator_get_optional(&client->dev, "vcc");
		if (!IS_ERR(vcc)) {
			if (regulator_get_voltage(vcc) == 5000000)
				val |= CHAN_VCC_5V;
		}
		data->channel = val;
		lm87_write_value(client,
				LM87_REG_CHANNEL_MODE, data->channel);
	} else if (dev_get_platdata(&client->dev)) {
		data->channel = *(u8 *)dev_get_platdata(&client->dev);
		lm87_write_value(client,
				 LM87_REG_CHANNEL_MODE, data->channel);
	} else {
		data->channel = lm87_read_value(client, LM87_REG_CHANNEL_MODE);
	}
	data->config = lm87_read_value(client, LM87_REG_CONFIG) & 0x6F;

	rc = devm_add_action(&client->dev, lm87_restore_config, client);
	if (rc)
		return rc;

	if (!(data->config & 0x01)) {
		int i;

		/* Limits are left uninitialized after power-up */
		for (i = 1; i < 6; i++) {
			lm87_write_value(client, LM87_REG_IN_MIN(i), 0x00);
			lm87_write_value(client, LM87_REG_IN_MAX(i), 0xFF);
		}
		for (i = 0; i < 2; i++) {
			lm87_write_value(client, LM87_REG_TEMP_HIGH[i], 0x7F);
			lm87_write_value(client, LM87_REG_TEMP_LOW[i], 0x00);
			lm87_write_value(client, LM87_REG_AIN_MIN(i), 0x00);
			lm87_write_value(client, LM87_REG_AIN_MAX(i), 0xFF);
		}
		if (data->channel & CHAN_TEMP3) {
			lm87_write_value(client, LM87_REG_TEMP_HIGH[2], 0x7F);
			lm87_write_value(client, LM87_REG_TEMP_LOW[2], 0x00);
		} else {
			lm87_write_value(client, LM87_REG_IN_MIN(0), 0x00);
			lm87_write_value(client, LM87_REG_IN_MAX(0), 0xFF);
		}
	}

	/* Make sure Start is set and INT#_Clear is clear */
	if ((data->config & 0x09) != 0x01)
		lm87_write_value(client, LM87_REG_CONFIG,
				 (data->config & 0x77) | 0x01);
	return 0;
}

static int lm87_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	struct lm87_data *data;
	struct device *hwmon_dev;
	int err;
	unsigned int group_tail = 0;

	data = devm_kzalloc(&client->dev, sizeof(struct lm87_data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	i2c_set_clientdata(client, data);
	mutex_init(&data->update_lock);

	/* Initialize the LM87 chip */
	err = lm87_init_client(client);
	if (err)
		return err;

	data->in_scale[0] = 2500;
	data->in_scale[1] = 2700;
	data->in_scale[2] = (data->channel & CHAN_VCC_5V) ? 5000 : 3300;
	data->in_scale[3] = 5000;
	data->in_scale[4] = 12000;
	data->in_scale[5] = 2700;
	data->in_scale[6] = 1875;
	data->in_scale[7] = 1875;

	/*
	 * Construct the list of attributes, the list depends on the
	 * configuration of the chip
	 */
	data->attr_groups[group_tail++] = &lm87_group;
	if (data->channel & CHAN_NO_FAN(0))
		data->attr_groups[group_tail++] = &lm87_group_in6;
	else
		data->attr_groups[group_tail++] = &lm87_group_fan1;

	if (data->channel & CHAN_NO_FAN(1))
		data->attr_groups[group_tail++] = &lm87_group_in7;
	else
		data->attr_groups[group_tail++] = &lm87_group_fan2;

	if (data->channel & CHAN_TEMP3)
		data->attr_groups[group_tail++] = &lm87_group_temp3;
	else
		data->attr_groups[group_tail++] = &lm87_group_in0_5;

	if (!(data->channel & CHAN_NO_VID)) {
		data->vrm = vid_which_vrm();
		data->attr_groups[group_tail++] = &lm87_group_vid;
	}

	hwmon_dev = devm_hwmon_device_register_with_groups(
	    &client->dev, client->name, client, data->attr_groups);
	return PTR_ERR_OR_ZERO(hwmon_dev);
}

/*
 * Driver data (common to all clients)
 */

static const struct i2c_device_id lm87_id[] = {
	{ "lm87", 0 },
	{ "adm1024", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, lm87_id);

static const struct of_device_id lm87_of_match[] = {
	{ .compatible = "ti,lm87" },
	{ .compatible = "adi,adm1024" },
	{ },
};
MODULE_DEVICE_TABLE(of, lm87_of_match);

static struct i2c_driver lm87_driver = {
	.class		= I2C_CLASS_HWMON,
	.driver = {
		.name	= "lm87",
		.of_match_table = lm87_of_match,
	},
	.probe		= lm87_probe,
	.id_table	= lm87_id,
	.detect		= lm87_detect,
	.address_list	= normal_i2c,
};

module_i2c_driver(lm87_driver);

MODULE_AUTHOR("Jean Delvare <jdelvare@suse.de> and others");
MODULE_DESCRIPTION("LM87 driver");
MODULE_LICENSE("GPL");
