/*
 * lm87.c
 *
 * Copyright (C) 2000       Frodo Looijaard <frodol@dds.nl>
 *                          Philip Edelbrock <phil@netroedge.com>
 *                          Stephen Rousset <stephen.rousset@rocketlogix.com>
 *                          Dan Eaton <dan.eaton@rocketlogix.com>
 * Copyright (C) 2004-2008  Jean Delvare <khali@linux-fr.org>
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

/*
 * Addresses to scan
 * LM87 has three possible addresses: 0x2c, 0x2d and 0x2e.
 */

static const unsigned short normal_i2c[] = { 0x2c, 0x2d, 0x2e, I2C_CLIENT_END };

enum chips { lm87, adm1024 };

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
				 (val) * 192 >= (scale) * 255 ? 255 : \
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
	struct device *hwmon_dev;
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
	struct i2c_client *client = to_i2c_client(dev);
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

static ssize_t show_in_input(struct device *dev, struct device_attribute *attr,
			     char *buf)
{
	struct lm87_data *data = lm87_update_device(dev);
	int nr = to_sensor_dev_attr(attr)->index;

	return sprintf(buf, "%u\n", IN_FROM_REG(data->in[nr],
		       data->in_scale[nr]));
}

static ssize_t show_in_min(struct device *dev,
				     struct device_attribute *attr, char *buf)
{
	struct lm87_data *data = lm87_update_device(dev);
	int nr = to_sensor_dev_attr(attr)->index;

	return sprintf(buf, "%u\n", IN_FROM_REG(data->in_min[nr],
		       data->in_scale[nr]));
}

static ssize_t show_in_max(struct device *dev,
				     struct device_attribute *attr, char *buf)
{
	struct lm87_data *data = lm87_update_device(dev);
	int nr = to_sensor_dev_attr(attr)->index;

	return sprintf(buf, "%u\n", IN_FROM_REG(data->in_max[nr],
		       data->in_scale[nr]));
}

static ssize_t set_in_min(struct device *dev, struct device_attribute *attr,
			  const char *buf, size_t count)
{
	struct i2c_client *client = to_i2c_client(dev);
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

static ssize_t set_in_max(struct device *dev,  struct device_attribute *attr,
			  const char *buf, size_t count)
{
	struct i2c_client *client = to_i2c_client(dev);
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

#define set_in(offset) \
static SENSOR_DEVICE_ATTR(in##offset##_input, S_IRUGO, \
		show_in_input, NULL, offset); \
static SENSOR_DEVICE_ATTR(in##offset##_min, S_IRUGO | S_IWUSR, \
		show_in_min, set_in_min, offset); \
static SENSOR_DEVICE_ATTR(in##offset##_max, S_IRUGO | S_IWUSR, \
		show_in_max, set_in_max, offset)
set_in(0);
set_in(1);
set_in(2);
set_in(3);
set_in(4);
set_in(5);
set_in(6);
set_in(7);

static ssize_t show_temp_input(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	struct lm87_data *data = lm87_update_device(dev);
	int nr = to_sensor_dev_attr(attr)->index;

	return sprintf(buf, "%d\n", TEMP_FROM_REG(data->temp[nr]));
}

static ssize_t show_temp_low(struct device *dev,
			     struct device_attribute *attr, char *buf)
{
	struct lm87_data *data = lm87_update_device(dev);
	int nr = to_sensor_dev_attr(attr)->index;

	return sprintf(buf, "%d\n",
		       TEMP_FROM_REG(data->temp_low[nr]));
}

static ssize_t show_temp_high(struct device *dev,
			      struct device_attribute *attr, char *buf)
{
	struct lm87_data *data = lm87_update_device(dev);
	int nr = to_sensor_dev_attr(attr)->index;

	return sprintf(buf, "%d\n",
		       TEMP_FROM_REG(data->temp_high[nr]));
}

static ssize_t set_temp_low(struct device *dev, struct device_attribute *attr,
			    const char *buf, size_t count)
{
	struct i2c_client *client = to_i2c_client(dev);
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

static ssize_t set_temp_high(struct device *dev, struct device_attribute *attr,
			     const char *buf, size_t count)
{
	struct i2c_client *client = to_i2c_client(dev);
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

#define set_temp(offset) \
static SENSOR_DEVICE_ATTR(temp##offset##_input, S_IRUGO, \
		show_temp_input, NULL, offset - 1); \
static SENSOR_DEVICE_ATTR(temp##offset##_max, S_IRUGO | S_IWUSR, \
		show_temp_high, set_temp_high, offset - 1); \
static SENSOR_DEVICE_ATTR(temp##offset##_min, S_IRUGO | S_IWUSR, \
		show_temp_low, set_temp_low, offset - 1)
set_temp(1);
set_temp(2);
set_temp(3);

static ssize_t show_temp_crit_int(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	struct lm87_data *data = lm87_update_device(dev);
	return sprintf(buf, "%d\n", TEMP_FROM_REG(data->temp_crit_int));
}

static ssize_t show_temp_crit_ext(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	struct lm87_data *data = lm87_update_device(dev);
	return sprintf(buf, "%d\n", TEMP_FROM_REG(data->temp_crit_ext));
}

static DEVICE_ATTR(temp1_crit, S_IRUGO, show_temp_crit_int, NULL);
static DEVICE_ATTR(temp2_crit, S_IRUGO, show_temp_crit_ext, NULL);
static DEVICE_ATTR(temp3_crit, S_IRUGO, show_temp_crit_ext, NULL);

static ssize_t show_fan_input(struct device *dev,
			      struct device_attribute *attr, char *buf)
{
	struct lm87_data *data = lm87_update_device(dev);
	int nr = to_sensor_dev_attr(attr)->index;

	return sprintf(buf, "%d\n", FAN_FROM_REG(data->fan[nr],
		       FAN_DIV_FROM_REG(data->fan_div[nr])));
}

static ssize_t show_fan_min(struct device *dev,
			    struct device_attribute *attr, char *buf)
{
	struct lm87_data *data = lm87_update_device(dev);
	int nr = to_sensor_dev_attr(attr)->index;

	return sprintf(buf, "%d\n", FAN_FROM_REG(data->fan_min[nr],
		       FAN_DIV_FROM_REG(data->fan_div[nr])));
}

static ssize_t show_fan_div(struct device *dev,
			    struct device_attribute *attr, char *buf)
{
	struct lm87_data *data = lm87_update_device(dev);
	int nr = to_sensor_dev_attr(attr)->index;

	return sprintf(buf, "%d\n",
		       FAN_DIV_FROM_REG(data->fan_div[nr]));
}

static ssize_t set_fan_min(struct device *dev, struct device_attribute *attr,
			   const char *buf, size_t count)
{
	struct i2c_client *client = to_i2c_client(dev);
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
static ssize_t set_fan_div(struct device *dev, struct device_attribute *attr,
			   const char *buf, size_t count)
{
	struct i2c_client *client = to_i2c_client(dev);
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

#define set_fan(offset) \
static SENSOR_DEVICE_ATTR(fan##offset##_input, S_IRUGO, \
		show_fan_input, NULL, offset - 1); \
static SENSOR_DEVICE_ATTR(fan##offset##_min, S_IRUGO | S_IWUSR, \
		show_fan_min, set_fan_min, offset - 1); \
static SENSOR_DEVICE_ATTR(fan##offset##_div, S_IRUGO | S_IWUSR, \
		show_fan_div, set_fan_div, offset - 1)
set_fan(1);
set_fan(2);

static ssize_t show_alarms(struct device *dev, struct device_attribute *attr,
			   char *buf)
{
	struct lm87_data *data = lm87_update_device(dev);
	return sprintf(buf, "%d\n", data->alarms);
}
static DEVICE_ATTR(alarms, S_IRUGO, show_alarms, NULL);

static ssize_t show_vid(struct device *dev, struct device_attribute *attr,
			char *buf)
{
	struct lm87_data *data = lm87_update_device(dev);
	return sprintf(buf, "%d\n", vid_from_reg(data->vid, data->vrm));
}
static DEVICE_ATTR(cpu0_vid, S_IRUGO, show_vid, NULL);

static ssize_t show_vrm(struct device *dev, struct device_attribute *attr,
			char *buf)
{
	struct lm87_data *data = dev_get_drvdata(dev);
	return sprintf(buf, "%d\n", data->vrm);
}
static ssize_t set_vrm(struct device *dev, struct device_attribute *attr,
		       const char *buf, size_t count)
{
	struct lm87_data *data = dev_get_drvdata(dev);
	unsigned long val;
	int err;

	err = kstrtoul(buf, 10, &val);
	if (err)
		return err;
	data->vrm = val;
	return count;
}
static DEVICE_ATTR(vrm, S_IRUGO | S_IWUSR, show_vrm, set_vrm);

static ssize_t show_aout(struct device *dev, struct device_attribute *attr,
			 char *buf)
{
	struct lm87_data *data = lm87_update_device(dev);
	return sprintf(buf, "%d\n", AOUT_FROM_REG(data->aout));
}
static ssize_t set_aout(struct device *dev, struct device_attribute *attr,
			const char *buf, size_t count)
{
	struct i2c_client *client = to_i2c_client(dev);
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
static DEVICE_ATTR(aout_output, S_IRUGO | S_IWUSR, show_aout, set_aout);

static ssize_t show_alarm(struct device *dev, struct device_attribute *attr,
			  char *buf)
{
	struct lm87_data *data = lm87_update_device(dev);
	int bitnr = to_sensor_dev_attr(attr)->index;
	return sprintf(buf, "%u\n", (data->alarms >> bitnr) & 1);
}
static SENSOR_DEVICE_ATTR(in0_alarm, S_IRUGO, show_alarm, NULL, 0);
static SENSOR_DEVICE_ATTR(in1_alarm, S_IRUGO, show_alarm, NULL, 1);
static SENSOR_DEVICE_ATTR(in2_alarm, S_IRUGO, show_alarm, NULL, 2);
static SENSOR_DEVICE_ATTR(in3_alarm, S_IRUGO, show_alarm, NULL, 3);
static SENSOR_DEVICE_ATTR(in4_alarm, S_IRUGO, show_alarm, NULL, 8);
static SENSOR_DEVICE_ATTR(in5_alarm, S_IRUGO, show_alarm, NULL, 9);
static SENSOR_DEVICE_ATTR(in6_alarm, S_IRUGO, show_alarm, NULL, 6);
static SENSOR_DEVICE_ATTR(in7_alarm, S_IRUGO, show_alarm, NULL, 7);
static SENSOR_DEVICE_ATTR(temp1_alarm, S_IRUGO, show_alarm, NULL, 4);
static SENSOR_DEVICE_ATTR(temp2_alarm, S_IRUGO, show_alarm, NULL, 5);
static SENSOR_DEVICE_ATTR(temp3_alarm, S_IRUGO, show_alarm, NULL, 5);
static SENSOR_DEVICE_ATTR(fan1_alarm, S_IRUGO, show_alarm, NULL, 6);
static SENSOR_DEVICE_ATTR(fan2_alarm, S_IRUGO, show_alarm, NULL, 7);
static SENSOR_DEVICE_ATTR(temp2_fault, S_IRUGO, show_alarm, NULL, 14);
static SENSOR_DEVICE_ATTR(temp3_fault, S_IRUGO, show_alarm, NULL, 15);

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

static void lm87_remove_files(struct i2c_client *client)
{
	struct device *dev = &client->dev;

	sysfs_remove_group(&dev->kobj, &lm87_group);
	sysfs_remove_group(&dev->kobj, &lm87_group_in6);
	sysfs_remove_group(&dev->kobj, &lm87_group_fan1);
	sysfs_remove_group(&dev->kobj, &lm87_group_in7);
	sysfs_remove_group(&dev->kobj, &lm87_group_fan2);
	sysfs_remove_group(&dev->kobj, &lm87_group_temp3);
	sysfs_remove_group(&dev->kobj, &lm87_group_in0_5);
	sysfs_remove_group(&dev->kobj, &lm87_group_vid);
}

static void lm87_init_client(struct i2c_client *client)
{
	struct lm87_data *data = i2c_get_clientdata(client);

	if (client->dev.platform_data) {
		data->channel = *(u8 *)client->dev.platform_data;
		lm87_write_value(client,
				 LM87_REG_CHANNEL_MODE, data->channel);
	} else {
		data->channel = lm87_read_value(client, LM87_REG_CHANNEL_MODE);
	}
	data->config = lm87_read_value(client, LM87_REG_CONFIG) & 0x6F;

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
}

static int lm87_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	struct lm87_data *data;
	int err;

	data = devm_kzalloc(&client->dev, sizeof(struct lm87_data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	i2c_set_clientdata(client, data);
	data->valid = 0;
	mutex_init(&data->update_lock);

	/* Initialize the LM87 chip */
	lm87_init_client(client);

	data->in_scale[0] = 2500;
	data->in_scale[1] = 2700;
	data->in_scale[2] = (data->channel & CHAN_VCC_5V) ? 5000 : 3300;
	data->in_scale[3] = 5000;
	data->in_scale[4] = 12000;
	data->in_scale[5] = 2700;
	data->in_scale[6] = 1875;
	data->in_scale[7] = 1875;

	/* Register sysfs hooks */
	err = sysfs_create_group(&client->dev.kobj, &lm87_group);
	if (err)
		goto exit_stop;

	if (data->channel & CHAN_NO_FAN(0)) {
		err = sysfs_create_group(&client->dev.kobj, &lm87_group_in6);
		if (err)
			goto exit_remove;
	} else {
		err = sysfs_create_group(&client->dev.kobj, &lm87_group_fan1);
		if (err)
			goto exit_remove;
	}

	if (data->channel & CHAN_NO_FAN(1)) {
		err = sysfs_create_group(&client->dev.kobj, &lm87_group_in7);
		if (err)
			goto exit_remove;
	} else {
		err = sysfs_create_group(&client->dev.kobj, &lm87_group_fan2);
		if (err)
			goto exit_remove;
	}

	if (data->channel & CHAN_TEMP3) {
		err = sysfs_create_group(&client->dev.kobj, &lm87_group_temp3);
		if (err)
			goto exit_remove;
	} else {
		err = sysfs_create_group(&client->dev.kobj, &lm87_group_in0_5);
		if (err)
			goto exit_remove;
	}

	if (!(data->channel & CHAN_NO_VID)) {
		data->vrm = vid_which_vrm();
		err = sysfs_create_group(&client->dev.kobj, &lm87_group_vid);
		if (err)
			goto exit_remove;
	}

	data->hwmon_dev = hwmon_device_register(&client->dev);
	if (IS_ERR(data->hwmon_dev)) {
		err = PTR_ERR(data->hwmon_dev);
		goto exit_remove;
	}

	return 0;

exit_remove:
	lm87_remove_files(client);
exit_stop:
	lm87_write_value(client, LM87_REG_CONFIG, data->config);
	return err;
}

static int lm87_remove(struct i2c_client *client)
{
	struct lm87_data *data = i2c_get_clientdata(client);

	hwmon_device_unregister(data->hwmon_dev);
	lm87_remove_files(client);

	lm87_write_value(client, LM87_REG_CONFIG, data->config);
	return 0;
}

/*
 * Driver data (common to all clients)
 */

static const struct i2c_device_id lm87_id[] = {
	{ "lm87", lm87 },
	{ "adm1024", adm1024 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, lm87_id);

static struct i2c_driver lm87_driver = {
	.class		= I2C_CLASS_HWMON,
	.driver = {
		.name	= "lm87",
	},
	.probe		= lm87_probe,
	.remove		= lm87_remove,
	.id_table	= lm87_id,
	.detect		= lm87_detect,
	.address_list	= normal_i2c,
};

module_i2c_driver(lm87_driver);

MODULE_AUTHOR("Jean Delvare <khali@linux-fr.org> and others");
MODULE_DESCRIPTION("LM87 driver");
MODULE_LICENSE("GPL");
