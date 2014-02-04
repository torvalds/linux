/*
 * adm1031.c - Part of lm_sensors, Linux kernel modules for hardware
 *	       monitoring
 * Based on lm75.c and lm85.c
 * Supports adm1030 / adm1031
 * Copyright (C) 2004 Alexandre d'Alton <alex@alexdalton.org>
 * Reworked by Jean Delvare <jdelvare@suse.de>
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

/* Following macros takes channel parameter starting from 0 to 2 */
#define ADM1031_REG_FAN_SPEED(nr)	(0x08 + (nr))
#define ADM1031_REG_FAN_DIV(nr)		(0x20 + (nr))
#define ADM1031_REG_PWM			(0x22)
#define ADM1031_REG_FAN_MIN(nr)		(0x10 + (nr))
#define ADM1031_REG_FAN_FILTER		(0x23)

#define ADM1031_REG_TEMP_OFFSET(nr)	(0x0d + (nr))
#define ADM1031_REG_TEMP_MAX(nr)	(0x14 + 4 * (nr))
#define ADM1031_REG_TEMP_MIN(nr)	(0x15 + 4 * (nr))
#define ADM1031_REG_TEMP_CRIT(nr)	(0x16 + 4 * (nr))

#define ADM1031_REG_TEMP(nr)		(0x0a + (nr))
#define ADM1031_REG_AUTO_TEMP(nr)	(0x24 + (nr))

#define ADM1031_REG_STATUS(nr)		(0x2 + (nr))

#define ADM1031_REG_CONF1		0x00
#define ADM1031_REG_CONF2		0x01
#define ADM1031_REG_EXT_TEMP		0x06

#define ADM1031_CONF1_MONITOR_ENABLE	0x01	/* Monitoring enable */
#define ADM1031_CONF1_PWM_INVERT	0x08	/* PWM Invert */
#define ADM1031_CONF1_AUTO_MODE		0x80	/* Auto FAN */

#define ADM1031_CONF2_PWM1_ENABLE	0x01
#define ADM1031_CONF2_PWM2_ENABLE	0x02
#define ADM1031_CONF2_TACH1_ENABLE	0x04
#define ADM1031_CONF2_TACH2_ENABLE	0x08
#define ADM1031_CONF2_TEMP_ENABLE(chan)	(0x10 << (chan))

#define ADM1031_UPDATE_RATE_MASK	0x1c
#define ADM1031_UPDATE_RATE_SHIFT	2

/* Addresses to scan */
static const unsigned short normal_i2c[] = { 0x2c, 0x2d, 0x2e, I2C_CLIENT_END };

enum chips { adm1030, adm1031 };

typedef u8 auto_chan_table_t[8][2];

/* Each client has this additional data */
struct adm1031_data {
	struct device *hwmon_dev;
	struct mutex update_lock;
	int chip_type;
	char valid;		/* !=0 if following fields are valid */
	unsigned long last_updated;	/* In jiffies */
	unsigned int update_interval;	/* In milliseconds */
	/*
	 * The chan_select_table contains the possible configurations for
	 * auto fan control.
	 */
	const auto_chan_table_t *chan_select_table;
	u16 alarm;
	u8 conf1;
	u8 conf2;
	u8 fan[2];
	u8 fan_div[2];
	u8 fan_min[2];
	u8 pwm[2];
	u8 old_pwm[2];
	s8 temp[3];
	u8 ext_temp[3];
	u8 auto_temp[3];
	u8 auto_temp_min[3];
	u8 auto_temp_off[3];
	u8 auto_temp_max[3];
	s8 temp_offset[3];
	s8 temp_min[3];
	s8 temp_max[3];
	s8 temp_crit[3];
};

static int adm1031_probe(struct i2c_client *client,
			 const struct i2c_device_id *id);
static int adm1031_detect(struct i2c_client *client,
			  struct i2c_board_info *info);
static void adm1031_init_client(struct i2c_client *client);
static int adm1031_remove(struct i2c_client *client);
static struct adm1031_data *adm1031_update_device(struct device *dev);

static const struct i2c_device_id adm1031_id[] = {
	{ "adm1030", adm1030 },
	{ "adm1031", adm1031 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, adm1031_id);

/* This is the driver that will be inserted */
static struct i2c_driver adm1031_driver = {
	.class		= I2C_CLASS_HWMON,
	.driver = {
		.name = "adm1031",
	},
	.probe		= adm1031_probe,
	.remove		= adm1031_remove,
	.id_table	= adm1031_id,
	.detect		= adm1031_detect,
	.address_list	= normal_i2c,
};

static inline u8 adm1031_read_value(struct i2c_client *client, u8 reg)
{
	return i2c_smbus_read_byte_data(client, reg);
}

static inline int
adm1031_write_value(struct i2c_client *client, u8 reg, unsigned int value)
{
	return i2c_smbus_write_byte_data(client, reg, value);
}


#define TEMP_TO_REG(val)		(((val) < 0 ? ((val - 500) / 1000) : \
					((val + 500) / 1000)))

#define TEMP_FROM_REG(val)		((val) * 1000)

#define TEMP_FROM_REG_EXT(val, ext)	(TEMP_FROM_REG(val) + (ext) * 125)

#define TEMP_OFFSET_TO_REG(val)		(TEMP_TO_REG(val) & 0x8f)
#define TEMP_OFFSET_FROM_REG(val)	TEMP_FROM_REG((val) < 0 ? \
						      (val) | 0x70 : (val))

#define FAN_FROM_REG(reg, div)		((reg) ? \
					 (11250 * 60) / ((reg) * (div)) : 0)

static int FAN_TO_REG(int reg, int div)
{
	int tmp;
	tmp = FAN_FROM_REG(clamp_val(reg, 0, 65535), div);
	return tmp > 255 ? 255 : tmp;
}

#define FAN_DIV_FROM_REG(reg)		(1<<(((reg)&0xc0)>>6))

#define PWM_TO_REG(val)			(clamp_val((val), 0, 255) >> 4)
#define PWM_FROM_REG(val)		((val) << 4)

#define FAN_CHAN_FROM_REG(reg)		(((reg) >> 5) & 7)
#define FAN_CHAN_TO_REG(val, reg)	\
	(((reg) & 0x1F) | (((val) << 5) & 0xe0))

#define AUTO_TEMP_MIN_TO_REG(val, reg)	\
	((((val) / 500) & 0xf8) | ((reg) & 0x7))
#define AUTO_TEMP_RANGE_FROM_REG(reg)	(5000 * (1 << ((reg) & 0x7)))
#define AUTO_TEMP_MIN_FROM_REG(reg)	(1000 * ((((reg) >> 3) & 0x1f) << 2))

#define AUTO_TEMP_MIN_FROM_REG_DEG(reg)	((((reg) >> 3) & 0x1f) << 2)

#define AUTO_TEMP_OFF_FROM_REG(reg)		\
	(AUTO_TEMP_MIN_FROM_REG(reg) - 5000)

#define AUTO_TEMP_MAX_FROM_REG(reg)		\
	(AUTO_TEMP_RANGE_FROM_REG(reg) +	\
	AUTO_TEMP_MIN_FROM_REG(reg))

static int AUTO_TEMP_MAX_TO_REG(int val, int reg, int pwm)
{
	int ret;
	int range = val - AUTO_TEMP_MIN_FROM_REG(reg);

	range = ((val - AUTO_TEMP_MIN_FROM_REG(reg))*10)/(16 - pwm);
	ret = ((reg & 0xf8) |
	       (range < 10000 ? 0 :
		range < 20000 ? 1 :
		range < 40000 ? 2 : range < 80000 ? 3 : 4));
	return ret;
}

/* FAN auto control */
#define GET_FAN_AUTO_BITFIELD(data, idx)	\
	(*(data)->chan_select_table)[FAN_CHAN_FROM_REG((data)->conf1)][idx % 2]

/*
 * The tables below contains the possible values for the auto fan
 * control bitfields. the index in the table is the register value.
 * MSb is the auto fan control enable bit, so the four first entries
 * in the table disables auto fan control when both bitfields are zero.
 */
static const auto_chan_table_t auto_channel_select_table_adm1031 = {
	{ 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 },
	{ 2 /* 0b010 */ , 4 /* 0b100 */ },
	{ 2 /* 0b010 */ , 2 /* 0b010 */ },
	{ 4 /* 0b100 */ , 4 /* 0b100 */ },
	{ 7 /* 0b111 */ , 7 /* 0b111 */ },
};

static const auto_chan_table_t auto_channel_select_table_adm1030 = {
	{ 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 },
	{ 2 /* 0b10 */		, 0 },
	{ 0xff /* invalid */	, 0 },
	{ 0xff /* invalid */	, 0 },
	{ 3 /* 0b11 */		, 0 },
};

/*
 * That function checks if a bitfield is valid and returns the other bitfield
 * nearest match if no exact match where found.
 */
static int
get_fan_auto_nearest(struct adm1031_data *data, int chan, u8 val, u8 reg)
{
	int i;
	int first_match = -1, exact_match = -1;
	u8 other_reg_val =
	    (*data->chan_select_table)[FAN_CHAN_FROM_REG(reg)][chan ? 0 : 1];

	if (val == 0)
		return 0;

	for (i = 0; i < 8; i++) {
		if ((val == (*data->chan_select_table)[i][chan]) &&
		    ((*data->chan_select_table)[i][chan ? 0 : 1] ==
		     other_reg_val)) {
			/* We found an exact match */
			exact_match = i;
			break;
		} else if (val == (*data->chan_select_table)[i][chan] &&
			   first_match == -1) {
			/*
			 * Save the first match in case of an exact match has
			 * not been found
			 */
			first_match = i;
		}
	}

	if (exact_match >= 0)
		return exact_match;
	else if (first_match >= 0)
		return first_match;

	return -EINVAL;
}

static ssize_t show_fan_auto_channel(struct device *dev,
				     struct device_attribute *attr, char *buf)
{
	int nr = to_sensor_dev_attr(attr)->index;
	struct adm1031_data *data = adm1031_update_device(dev);
	return sprintf(buf, "%d\n", GET_FAN_AUTO_BITFIELD(data, nr));
}

static ssize_t
set_fan_auto_channel(struct device *dev, struct device_attribute *attr,
		     const char *buf, size_t count)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct adm1031_data *data = i2c_get_clientdata(client);
	int nr = to_sensor_dev_attr(attr)->index;
	long val;
	u8 reg;
	int ret;
	u8 old_fan_mode;

	ret = kstrtol(buf, 10, &val);
	if (ret)
		return ret;

	old_fan_mode = data->conf1;

	mutex_lock(&data->update_lock);

	ret = get_fan_auto_nearest(data, nr, val, data->conf1);
	if (ret < 0) {
		mutex_unlock(&data->update_lock);
		return ret;
	}
	reg = ret;
	data->conf1 = FAN_CHAN_TO_REG(reg, data->conf1);
	if ((data->conf1 & ADM1031_CONF1_AUTO_MODE) ^
	    (old_fan_mode & ADM1031_CONF1_AUTO_MODE)) {
		if (data->conf1 & ADM1031_CONF1_AUTO_MODE) {
			/*
			 * Switch to Auto Fan Mode
			 * Save PWM registers
			 * Set PWM registers to 33% Both
			 */
			data->old_pwm[0] = data->pwm[0];
			data->old_pwm[1] = data->pwm[1];
			adm1031_write_value(client, ADM1031_REG_PWM, 0x55);
		} else {
			/* Switch to Manual Mode */
			data->pwm[0] = data->old_pwm[0];
			data->pwm[1] = data->old_pwm[1];
			/* Restore PWM registers */
			adm1031_write_value(client, ADM1031_REG_PWM,
					    data->pwm[0] | (data->pwm[1] << 4));
		}
	}
	data->conf1 = FAN_CHAN_TO_REG(reg, data->conf1);
	adm1031_write_value(client, ADM1031_REG_CONF1, data->conf1);
	mutex_unlock(&data->update_lock);
	return count;
}

static SENSOR_DEVICE_ATTR(auto_fan1_channel, S_IRUGO | S_IWUSR,
		show_fan_auto_channel, set_fan_auto_channel, 0);
static SENSOR_DEVICE_ATTR(auto_fan2_channel, S_IRUGO | S_IWUSR,
		show_fan_auto_channel, set_fan_auto_channel, 1);

/* Auto Temps */
static ssize_t show_auto_temp_off(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	int nr = to_sensor_dev_attr(attr)->index;
	struct adm1031_data *data = adm1031_update_device(dev);
	return sprintf(buf, "%d\n",
		       AUTO_TEMP_OFF_FROM_REG(data->auto_temp[nr]));
}
static ssize_t show_auto_temp_min(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	int nr = to_sensor_dev_attr(attr)->index;
	struct adm1031_data *data = adm1031_update_device(dev);
	return sprintf(buf, "%d\n",
		       AUTO_TEMP_MIN_FROM_REG(data->auto_temp[nr]));
}
static ssize_t
set_auto_temp_min(struct device *dev, struct device_attribute *attr,
		  const char *buf, size_t count)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct adm1031_data *data = i2c_get_clientdata(client);
	int nr = to_sensor_dev_attr(attr)->index;
	long val;
	int ret;

	ret = kstrtol(buf, 10, &val);
	if (ret)
		return ret;

	mutex_lock(&data->update_lock);
	data->auto_temp[nr] = AUTO_TEMP_MIN_TO_REG(val, data->auto_temp[nr]);
	adm1031_write_value(client, ADM1031_REG_AUTO_TEMP(nr),
			    data->auto_temp[nr]);
	mutex_unlock(&data->update_lock);
	return count;
}
static ssize_t show_auto_temp_max(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	int nr = to_sensor_dev_attr(attr)->index;
	struct adm1031_data *data = adm1031_update_device(dev);
	return sprintf(buf, "%d\n",
		       AUTO_TEMP_MAX_FROM_REG(data->auto_temp[nr]));
}
static ssize_t
set_auto_temp_max(struct device *dev, struct device_attribute *attr,
		  const char *buf, size_t count)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct adm1031_data *data = i2c_get_clientdata(client);
	int nr = to_sensor_dev_attr(attr)->index;
	long val;
	int ret;

	ret = kstrtol(buf, 10, &val);
	if (ret)
		return ret;

	mutex_lock(&data->update_lock);
	data->temp_max[nr] = AUTO_TEMP_MAX_TO_REG(val, data->auto_temp[nr],
						  data->pwm[nr]);
	adm1031_write_value(client, ADM1031_REG_AUTO_TEMP(nr),
			    data->temp_max[nr]);
	mutex_unlock(&data->update_lock);
	return count;
}

#define auto_temp_reg(offset)						\
static SENSOR_DEVICE_ATTR(auto_temp##offset##_off, S_IRUGO,		\
		show_auto_temp_off, NULL, offset - 1);			\
static SENSOR_DEVICE_ATTR(auto_temp##offset##_min, S_IRUGO | S_IWUSR,	\
		show_auto_temp_min, set_auto_temp_min, offset - 1);	\
static SENSOR_DEVICE_ATTR(auto_temp##offset##_max, S_IRUGO | S_IWUSR,	\
		show_auto_temp_max, set_auto_temp_max, offset - 1)

auto_temp_reg(1);
auto_temp_reg(2);
auto_temp_reg(3);

/* pwm */
static ssize_t show_pwm(struct device *dev,
			struct device_attribute *attr, char *buf)
{
	int nr = to_sensor_dev_attr(attr)->index;
	struct adm1031_data *data = adm1031_update_device(dev);
	return sprintf(buf, "%d\n", PWM_FROM_REG(data->pwm[nr]));
}
static ssize_t set_pwm(struct device *dev, struct device_attribute *attr,
		       const char *buf, size_t count)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct adm1031_data *data = i2c_get_clientdata(client);
	int nr = to_sensor_dev_attr(attr)->index;
	long val;
	int ret, reg;

	ret = kstrtol(buf, 10, &val);
	if (ret)
		return ret;

	mutex_lock(&data->update_lock);
	if ((data->conf1 & ADM1031_CONF1_AUTO_MODE) &&
	    (((val>>4) & 0xf) != 5)) {
		/* In automatic mode, the only PWM accepted is 33% */
		mutex_unlock(&data->update_lock);
		return -EINVAL;
	}
	data->pwm[nr] = PWM_TO_REG(val);
	reg = adm1031_read_value(client, ADM1031_REG_PWM);
	adm1031_write_value(client, ADM1031_REG_PWM,
			    nr ? ((data->pwm[nr] << 4) & 0xf0) | (reg & 0xf)
			    : (data->pwm[nr] & 0xf) | (reg & 0xf0));
	mutex_unlock(&data->update_lock);
	return count;
}

static SENSOR_DEVICE_ATTR(pwm1, S_IRUGO | S_IWUSR, show_pwm, set_pwm, 0);
static SENSOR_DEVICE_ATTR(pwm2, S_IRUGO | S_IWUSR, show_pwm, set_pwm, 1);
static SENSOR_DEVICE_ATTR(auto_fan1_min_pwm, S_IRUGO | S_IWUSR,
		show_pwm, set_pwm, 0);
static SENSOR_DEVICE_ATTR(auto_fan2_min_pwm, S_IRUGO | S_IWUSR,
		show_pwm, set_pwm, 1);

/* Fans */

/*
 * That function checks the cases where the fan reading is not
 * relevant.  It is used to provide 0 as fan reading when the fan is
 * not supposed to run
 */
static int trust_fan_readings(struct adm1031_data *data, int chan)
{
	int res = 0;

	if (data->conf1 & ADM1031_CONF1_AUTO_MODE) {
		switch (data->conf1 & 0x60) {
		case 0x00:
			/*
			 * remote temp1 controls fan1,
			 * remote temp2 controls fan2
			 */
			res = data->temp[chan+1] >=
			    AUTO_TEMP_MIN_FROM_REG_DEG(data->auto_temp[chan+1]);
			break;
		case 0x20:	/* remote temp1 controls both fans */
			res =
			    data->temp[1] >=
			    AUTO_TEMP_MIN_FROM_REG_DEG(data->auto_temp[1]);
			break;
		case 0x40:	/* remote temp2 controls both fans */
			res =
			    data->temp[2] >=
			    AUTO_TEMP_MIN_FROM_REG_DEG(data->auto_temp[2]);
			break;
		case 0x60:	/* max controls both fans */
			res =
			    data->temp[0] >=
			    AUTO_TEMP_MIN_FROM_REG_DEG(data->auto_temp[0])
			    || data->temp[1] >=
			    AUTO_TEMP_MIN_FROM_REG_DEG(data->auto_temp[1])
			    || (data->chip_type == adm1031
				&& data->temp[2] >=
				AUTO_TEMP_MIN_FROM_REG_DEG(data->auto_temp[2]));
			break;
		}
	} else {
		res = data->pwm[chan] > 0;
	}
	return res;
}


static ssize_t show_fan(struct device *dev,
			struct device_attribute *attr, char *buf)
{
	int nr = to_sensor_dev_attr(attr)->index;
	struct adm1031_data *data = adm1031_update_device(dev);
	int value;

	value = trust_fan_readings(data, nr) ? FAN_FROM_REG(data->fan[nr],
				 FAN_DIV_FROM_REG(data->fan_div[nr])) : 0;
	return sprintf(buf, "%d\n", value);
}

static ssize_t show_fan_div(struct device *dev,
			    struct device_attribute *attr, char *buf)
{
	int nr = to_sensor_dev_attr(attr)->index;
	struct adm1031_data *data = adm1031_update_device(dev);
	return sprintf(buf, "%d\n", FAN_DIV_FROM_REG(data->fan_div[nr]));
}
static ssize_t show_fan_min(struct device *dev,
			    struct device_attribute *attr, char *buf)
{
	int nr = to_sensor_dev_attr(attr)->index;
	struct adm1031_data *data = adm1031_update_device(dev);
	return sprintf(buf, "%d\n",
		       FAN_FROM_REG(data->fan_min[nr],
				    FAN_DIV_FROM_REG(data->fan_div[nr])));
}
static ssize_t set_fan_min(struct device *dev, struct device_attribute *attr,
			   const char *buf, size_t count)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct adm1031_data *data = i2c_get_clientdata(client);
	int nr = to_sensor_dev_attr(attr)->index;
	long val;
	int ret;

	ret = kstrtol(buf, 10, &val);
	if (ret)
		return ret;

	mutex_lock(&data->update_lock);
	if (val) {
		data->fan_min[nr] =
			FAN_TO_REG(val, FAN_DIV_FROM_REG(data->fan_div[nr]));
	} else {
		data->fan_min[nr] = 0xff;
	}
	adm1031_write_value(client, ADM1031_REG_FAN_MIN(nr), data->fan_min[nr]);
	mutex_unlock(&data->update_lock);
	return count;
}
static ssize_t set_fan_div(struct device *dev, struct device_attribute *attr,
			   const char *buf, size_t count)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct adm1031_data *data = i2c_get_clientdata(client);
	int nr = to_sensor_dev_attr(attr)->index;
	long val;
	u8 tmp;
	int old_div;
	int new_min;
	int ret;

	ret = kstrtol(buf, 10, &val);
	if (ret)
		return ret;

	tmp = val == 8 ? 0xc0 :
	      val == 4 ? 0x80 :
	      val == 2 ? 0x40 :
	      val == 1 ? 0x00 :
	      0xff;
	if (tmp == 0xff)
		return -EINVAL;

	mutex_lock(&data->update_lock);
	/* Get fresh readings */
	data->fan_div[nr] = adm1031_read_value(client,
					       ADM1031_REG_FAN_DIV(nr));
	data->fan_min[nr] = adm1031_read_value(client,
					       ADM1031_REG_FAN_MIN(nr));

	/* Write the new clock divider and fan min */
	old_div = FAN_DIV_FROM_REG(data->fan_div[nr]);
	data->fan_div[nr] = tmp | (0x3f & data->fan_div[nr]);
	new_min = data->fan_min[nr] * old_div / val;
	data->fan_min[nr] = new_min > 0xff ? 0xff : new_min;

	adm1031_write_value(client, ADM1031_REG_FAN_DIV(nr),
			    data->fan_div[nr]);
	adm1031_write_value(client, ADM1031_REG_FAN_MIN(nr),
			    data->fan_min[nr]);

	/* Invalidate the cache: fan speed is no longer valid */
	data->valid = 0;
	mutex_unlock(&data->update_lock);
	return count;
}

#define fan_offset(offset)						\
static SENSOR_DEVICE_ATTR(fan##offset##_input, S_IRUGO,			\
		show_fan, NULL, offset - 1);				\
static SENSOR_DEVICE_ATTR(fan##offset##_min, S_IRUGO | S_IWUSR,		\
		show_fan_min, set_fan_min, offset - 1);			\
static SENSOR_DEVICE_ATTR(fan##offset##_div, S_IRUGO | S_IWUSR,		\
		show_fan_div, set_fan_div, offset - 1)

fan_offset(1);
fan_offset(2);


/* Temps */
static ssize_t show_temp(struct device *dev,
			 struct device_attribute *attr, char *buf)
{
	int nr = to_sensor_dev_attr(attr)->index;
	struct adm1031_data *data = adm1031_update_device(dev);
	int ext;
	ext = nr == 0 ?
	    ((data->ext_temp[nr] >> 6) & 0x3) * 2 :
	    (((data->ext_temp[nr] >> ((nr - 1) * 3)) & 7));
	return sprintf(buf, "%d\n", TEMP_FROM_REG_EXT(data->temp[nr], ext));
}
static ssize_t show_temp_offset(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	int nr = to_sensor_dev_attr(attr)->index;
	struct adm1031_data *data = adm1031_update_device(dev);
	return sprintf(buf, "%d\n",
		       TEMP_OFFSET_FROM_REG(data->temp_offset[nr]));
}
static ssize_t show_temp_min(struct device *dev,
			     struct device_attribute *attr, char *buf)
{
	int nr = to_sensor_dev_attr(attr)->index;
	struct adm1031_data *data = adm1031_update_device(dev);
	return sprintf(buf, "%d\n", TEMP_FROM_REG(data->temp_min[nr]));
}
static ssize_t show_temp_max(struct device *dev,
			     struct device_attribute *attr, char *buf)
{
	int nr = to_sensor_dev_attr(attr)->index;
	struct adm1031_data *data = adm1031_update_device(dev);
	return sprintf(buf, "%d\n", TEMP_FROM_REG(data->temp_max[nr]));
}
static ssize_t show_temp_crit(struct device *dev,
			      struct device_attribute *attr, char *buf)
{
	int nr = to_sensor_dev_attr(attr)->index;
	struct adm1031_data *data = adm1031_update_device(dev);
	return sprintf(buf, "%d\n", TEMP_FROM_REG(data->temp_crit[nr]));
}
static ssize_t set_temp_offset(struct device *dev,
			       struct device_attribute *attr, const char *buf,
			       size_t count)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct adm1031_data *data = i2c_get_clientdata(client);
	int nr = to_sensor_dev_attr(attr)->index;
	long val;
	int ret;

	ret = kstrtol(buf, 10, &val);
	if (ret)
		return ret;

	val = clamp_val(val, -15000, 15000);
	mutex_lock(&data->update_lock);
	data->temp_offset[nr] = TEMP_OFFSET_TO_REG(val);
	adm1031_write_value(client, ADM1031_REG_TEMP_OFFSET(nr),
			    data->temp_offset[nr]);
	mutex_unlock(&data->update_lock);
	return count;
}
static ssize_t set_temp_min(struct device *dev, struct device_attribute *attr,
			    const char *buf, size_t count)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct adm1031_data *data = i2c_get_clientdata(client);
	int nr = to_sensor_dev_attr(attr)->index;
	long val;
	int ret;

	ret = kstrtol(buf, 10, &val);
	if (ret)
		return ret;

	val = clamp_val(val, -55000, nr == 0 ? 127750 : 127875);
	mutex_lock(&data->update_lock);
	data->temp_min[nr] = TEMP_TO_REG(val);
	adm1031_write_value(client, ADM1031_REG_TEMP_MIN(nr),
			    data->temp_min[nr]);
	mutex_unlock(&data->update_lock);
	return count;
}
static ssize_t set_temp_max(struct device *dev, struct device_attribute *attr,
			    const char *buf, size_t count)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct adm1031_data *data = i2c_get_clientdata(client);
	int nr = to_sensor_dev_attr(attr)->index;
	long val;
	int ret;

	ret = kstrtol(buf, 10, &val);
	if (ret)
		return ret;

	val = clamp_val(val, -55000, nr == 0 ? 127750 : 127875);
	mutex_lock(&data->update_lock);
	data->temp_max[nr] = TEMP_TO_REG(val);
	adm1031_write_value(client, ADM1031_REG_TEMP_MAX(nr),
			    data->temp_max[nr]);
	mutex_unlock(&data->update_lock);
	return count;
}
static ssize_t set_temp_crit(struct device *dev, struct device_attribute *attr,
			     const char *buf, size_t count)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct adm1031_data *data = i2c_get_clientdata(client);
	int nr = to_sensor_dev_attr(attr)->index;
	long val;
	int ret;

	ret = kstrtol(buf, 10, &val);
	if (ret)
		return ret;

	val = clamp_val(val, -55000, nr == 0 ? 127750 : 127875);
	mutex_lock(&data->update_lock);
	data->temp_crit[nr] = TEMP_TO_REG(val);
	adm1031_write_value(client, ADM1031_REG_TEMP_CRIT(nr),
			    data->temp_crit[nr]);
	mutex_unlock(&data->update_lock);
	return count;
}

#define temp_reg(offset)						\
static SENSOR_DEVICE_ATTR(temp##offset##_input, S_IRUGO,		\
		show_temp, NULL, offset - 1);				\
static SENSOR_DEVICE_ATTR(temp##offset##_offset, S_IRUGO | S_IWUSR,	\
		show_temp_offset, set_temp_offset, offset - 1);		\
static SENSOR_DEVICE_ATTR(temp##offset##_min, S_IRUGO | S_IWUSR,	\
		show_temp_min, set_temp_min, offset - 1);		\
static SENSOR_DEVICE_ATTR(temp##offset##_max, S_IRUGO | S_IWUSR,	\
		show_temp_max, set_temp_max, offset - 1);		\
static SENSOR_DEVICE_ATTR(temp##offset##_crit, S_IRUGO | S_IWUSR,	\
		show_temp_crit, set_temp_crit, offset - 1)

temp_reg(1);
temp_reg(2);
temp_reg(3);

/* Alarms */
static ssize_t show_alarms(struct device *dev, struct device_attribute *attr,
			   char *buf)
{
	struct adm1031_data *data = adm1031_update_device(dev);
	return sprintf(buf, "%d\n", data->alarm);
}

static DEVICE_ATTR(alarms, S_IRUGO, show_alarms, NULL);

static ssize_t show_alarm(struct device *dev,
			  struct device_attribute *attr, char *buf)
{
	int bitnr = to_sensor_dev_attr(attr)->index;
	struct adm1031_data *data = adm1031_update_device(dev);
	return sprintf(buf, "%d\n", (data->alarm >> bitnr) & 1);
}

static SENSOR_DEVICE_ATTR(fan1_alarm, S_IRUGO, show_alarm, NULL, 0);
static SENSOR_DEVICE_ATTR(fan1_fault, S_IRUGO, show_alarm, NULL, 1);
static SENSOR_DEVICE_ATTR(temp2_max_alarm, S_IRUGO, show_alarm, NULL, 2);
static SENSOR_DEVICE_ATTR(temp2_min_alarm, S_IRUGO, show_alarm, NULL, 3);
static SENSOR_DEVICE_ATTR(temp2_crit_alarm, S_IRUGO, show_alarm, NULL, 4);
static SENSOR_DEVICE_ATTR(temp2_fault, S_IRUGO, show_alarm, NULL, 5);
static SENSOR_DEVICE_ATTR(temp1_max_alarm, S_IRUGO, show_alarm, NULL, 6);
static SENSOR_DEVICE_ATTR(temp1_min_alarm, S_IRUGO, show_alarm, NULL, 7);
static SENSOR_DEVICE_ATTR(fan2_alarm, S_IRUGO, show_alarm, NULL, 8);
static SENSOR_DEVICE_ATTR(fan2_fault, S_IRUGO, show_alarm, NULL, 9);
static SENSOR_DEVICE_ATTR(temp3_max_alarm, S_IRUGO, show_alarm, NULL, 10);
static SENSOR_DEVICE_ATTR(temp3_min_alarm, S_IRUGO, show_alarm, NULL, 11);
static SENSOR_DEVICE_ATTR(temp3_crit_alarm, S_IRUGO, show_alarm, NULL, 12);
static SENSOR_DEVICE_ATTR(temp3_fault, S_IRUGO, show_alarm, NULL, 13);
static SENSOR_DEVICE_ATTR(temp1_crit_alarm, S_IRUGO, show_alarm, NULL, 14);

/* Update Interval */
static const unsigned int update_intervals[] = {
	16000, 8000, 4000, 2000, 1000, 500, 250, 125,
};

static ssize_t show_update_interval(struct device *dev,
				    struct device_attribute *attr, char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct adm1031_data *data = i2c_get_clientdata(client);

	return sprintf(buf, "%u\n", data->update_interval);
}

static ssize_t set_update_interval(struct device *dev,
				   struct device_attribute *attr,
				   const char *buf, size_t count)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct adm1031_data *data = i2c_get_clientdata(client);
	unsigned long val;
	int i, err;
	u8 reg;

	err = kstrtoul(buf, 10, &val);
	if (err)
		return err;

	/*
	 * Find the nearest update interval from the table.
	 * Use it to determine the matching update rate.
	 */
	for (i = 0; i < ARRAY_SIZE(update_intervals) - 1; i++) {
		if (val >= update_intervals[i])
			break;
	}
	/* if not found, we point to the last entry (lowest update interval) */

	/* set the new update rate while preserving other settings */
	reg = adm1031_read_value(client, ADM1031_REG_FAN_FILTER);
	reg &= ~ADM1031_UPDATE_RATE_MASK;
	reg |= i << ADM1031_UPDATE_RATE_SHIFT;
	adm1031_write_value(client, ADM1031_REG_FAN_FILTER, reg);

	mutex_lock(&data->update_lock);
	data->update_interval = update_intervals[i];
	mutex_unlock(&data->update_lock);

	return count;
}

static DEVICE_ATTR(update_interval, S_IRUGO | S_IWUSR, show_update_interval,
		   set_update_interval);

static struct attribute *adm1031_attributes[] = {
	&sensor_dev_attr_fan1_input.dev_attr.attr,
	&sensor_dev_attr_fan1_div.dev_attr.attr,
	&sensor_dev_attr_fan1_min.dev_attr.attr,
	&sensor_dev_attr_fan1_alarm.dev_attr.attr,
	&sensor_dev_attr_fan1_fault.dev_attr.attr,
	&sensor_dev_attr_pwm1.dev_attr.attr,
	&sensor_dev_attr_auto_fan1_channel.dev_attr.attr,
	&sensor_dev_attr_temp1_input.dev_attr.attr,
	&sensor_dev_attr_temp1_offset.dev_attr.attr,
	&sensor_dev_attr_temp1_min.dev_attr.attr,
	&sensor_dev_attr_temp1_min_alarm.dev_attr.attr,
	&sensor_dev_attr_temp1_max.dev_attr.attr,
	&sensor_dev_attr_temp1_max_alarm.dev_attr.attr,
	&sensor_dev_attr_temp1_crit.dev_attr.attr,
	&sensor_dev_attr_temp1_crit_alarm.dev_attr.attr,
	&sensor_dev_attr_temp2_input.dev_attr.attr,
	&sensor_dev_attr_temp2_offset.dev_attr.attr,
	&sensor_dev_attr_temp2_min.dev_attr.attr,
	&sensor_dev_attr_temp2_min_alarm.dev_attr.attr,
	&sensor_dev_attr_temp2_max.dev_attr.attr,
	&sensor_dev_attr_temp2_max_alarm.dev_attr.attr,
	&sensor_dev_attr_temp2_crit.dev_attr.attr,
	&sensor_dev_attr_temp2_crit_alarm.dev_attr.attr,
	&sensor_dev_attr_temp2_fault.dev_attr.attr,

	&sensor_dev_attr_auto_temp1_off.dev_attr.attr,
	&sensor_dev_attr_auto_temp1_min.dev_attr.attr,
	&sensor_dev_attr_auto_temp1_max.dev_attr.attr,

	&sensor_dev_attr_auto_temp2_off.dev_attr.attr,
	&sensor_dev_attr_auto_temp2_min.dev_attr.attr,
	&sensor_dev_attr_auto_temp2_max.dev_attr.attr,

	&sensor_dev_attr_auto_fan1_min_pwm.dev_attr.attr,

	&dev_attr_update_interval.attr,
	&dev_attr_alarms.attr,

	NULL
};

static const struct attribute_group adm1031_group = {
	.attrs = adm1031_attributes,
};

static struct attribute *adm1031_attributes_opt[] = {
	&sensor_dev_attr_fan2_input.dev_attr.attr,
	&sensor_dev_attr_fan2_div.dev_attr.attr,
	&sensor_dev_attr_fan2_min.dev_attr.attr,
	&sensor_dev_attr_fan2_alarm.dev_attr.attr,
	&sensor_dev_attr_fan2_fault.dev_attr.attr,
	&sensor_dev_attr_pwm2.dev_attr.attr,
	&sensor_dev_attr_auto_fan2_channel.dev_attr.attr,
	&sensor_dev_attr_temp3_input.dev_attr.attr,
	&sensor_dev_attr_temp3_offset.dev_attr.attr,
	&sensor_dev_attr_temp3_min.dev_attr.attr,
	&sensor_dev_attr_temp3_min_alarm.dev_attr.attr,
	&sensor_dev_attr_temp3_max.dev_attr.attr,
	&sensor_dev_attr_temp3_max_alarm.dev_attr.attr,
	&sensor_dev_attr_temp3_crit.dev_attr.attr,
	&sensor_dev_attr_temp3_crit_alarm.dev_attr.attr,
	&sensor_dev_attr_temp3_fault.dev_attr.attr,
	&sensor_dev_attr_auto_temp3_off.dev_attr.attr,
	&sensor_dev_attr_auto_temp3_min.dev_attr.attr,
	&sensor_dev_attr_auto_temp3_max.dev_attr.attr,
	&sensor_dev_attr_auto_fan2_min_pwm.dev_attr.attr,
	NULL
};

static const struct attribute_group adm1031_group_opt = {
	.attrs = adm1031_attributes_opt,
};

/* Return 0 if detection is successful, -ENODEV otherwise */
static int adm1031_detect(struct i2c_client *client,
			  struct i2c_board_info *info)
{
	struct i2c_adapter *adapter = client->adapter;
	const char *name;
	int id, co;

	if (!i2c_check_functionality(adapter, I2C_FUNC_SMBUS_BYTE_DATA))
		return -ENODEV;

	id = i2c_smbus_read_byte_data(client, 0x3d);
	co = i2c_smbus_read_byte_data(client, 0x3e);

	if (!((id == 0x31 || id == 0x30) && co == 0x41))
		return -ENODEV;
	name = (id == 0x30) ? "adm1030" : "adm1031";

	strlcpy(info->type, name, I2C_NAME_SIZE);

	return 0;
}

static int adm1031_probe(struct i2c_client *client,
			 const struct i2c_device_id *id)
{
	struct adm1031_data *data;
	int err;

	data = devm_kzalloc(&client->dev, sizeof(struct adm1031_data),
			    GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	i2c_set_clientdata(client, data);
	data->chip_type = id->driver_data;
	mutex_init(&data->update_lock);

	if (data->chip_type == adm1030)
		data->chan_select_table = &auto_channel_select_table_adm1030;
	else
		data->chan_select_table = &auto_channel_select_table_adm1031;

	/* Initialize the ADM1031 chip */
	adm1031_init_client(client);

	/* Register sysfs hooks */
	err = sysfs_create_group(&client->dev.kobj, &adm1031_group);
	if (err)
		return err;

	if (data->chip_type == adm1031) {
		err = sysfs_create_group(&client->dev.kobj, &adm1031_group_opt);
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
	sysfs_remove_group(&client->dev.kobj, &adm1031_group);
	sysfs_remove_group(&client->dev.kobj, &adm1031_group_opt);
	return err;
}

static int adm1031_remove(struct i2c_client *client)
{
	struct adm1031_data *data = i2c_get_clientdata(client);

	hwmon_device_unregister(data->hwmon_dev);
	sysfs_remove_group(&client->dev.kobj, &adm1031_group);
	sysfs_remove_group(&client->dev.kobj, &adm1031_group_opt);
	return 0;
}

static void adm1031_init_client(struct i2c_client *client)
{
	unsigned int read_val;
	unsigned int mask;
	int i;
	struct adm1031_data *data = i2c_get_clientdata(client);

	mask = (ADM1031_CONF2_PWM1_ENABLE | ADM1031_CONF2_TACH1_ENABLE);
	if (data->chip_type == adm1031) {
		mask |= (ADM1031_CONF2_PWM2_ENABLE |
			ADM1031_CONF2_TACH2_ENABLE);
	}
	/* Initialize the ADM1031 chip (enables fan speed reading ) */
	read_val = adm1031_read_value(client, ADM1031_REG_CONF2);
	if ((read_val | mask) != read_val)
		adm1031_write_value(client, ADM1031_REG_CONF2, read_val | mask);

	read_val = adm1031_read_value(client, ADM1031_REG_CONF1);
	if ((read_val | ADM1031_CONF1_MONITOR_ENABLE) != read_val) {
		adm1031_write_value(client, ADM1031_REG_CONF1,
				    read_val | ADM1031_CONF1_MONITOR_ENABLE);
	}

	/* Read the chip's update rate */
	mask = ADM1031_UPDATE_RATE_MASK;
	read_val = adm1031_read_value(client, ADM1031_REG_FAN_FILTER);
	i = (read_val & mask) >> ADM1031_UPDATE_RATE_SHIFT;
	/* Save it as update interval */
	data->update_interval = update_intervals[i];
}

static struct adm1031_data *adm1031_update_device(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct adm1031_data *data = i2c_get_clientdata(client);
	unsigned long next_update;
	int chan;

	mutex_lock(&data->update_lock);

	next_update = data->last_updated
	  + msecs_to_jiffies(data->update_interval);
	if (time_after(jiffies, next_update) || !data->valid) {

		dev_dbg(&client->dev, "Starting adm1031 update\n");
		for (chan = 0;
		     chan < ((data->chip_type == adm1031) ? 3 : 2); chan++) {
			u8 oldh, newh;

			oldh =
			    adm1031_read_value(client, ADM1031_REG_TEMP(chan));
			data->ext_temp[chan] =
			    adm1031_read_value(client, ADM1031_REG_EXT_TEMP);
			newh =
			    adm1031_read_value(client, ADM1031_REG_TEMP(chan));
			if (newh != oldh) {
				data->ext_temp[chan] =
				    adm1031_read_value(client,
						       ADM1031_REG_EXT_TEMP);
#ifdef DEBUG
				oldh =
				    adm1031_read_value(client,
						       ADM1031_REG_TEMP(chan));

				/* oldh is actually newer */
				if (newh != oldh)
					dev_warn(&client->dev,
					  "Remote temperature may be wrong.\n");
#endif
			}
			data->temp[chan] = newh;

			data->temp_offset[chan] =
			    adm1031_read_value(client,
					       ADM1031_REG_TEMP_OFFSET(chan));
			data->temp_min[chan] =
			    adm1031_read_value(client,
					       ADM1031_REG_TEMP_MIN(chan));
			data->temp_max[chan] =
			    adm1031_read_value(client,
					       ADM1031_REG_TEMP_MAX(chan));
			data->temp_crit[chan] =
			    adm1031_read_value(client,
					       ADM1031_REG_TEMP_CRIT(chan));
			data->auto_temp[chan] =
			    adm1031_read_value(client,
					       ADM1031_REG_AUTO_TEMP(chan));

		}

		data->conf1 = adm1031_read_value(client, ADM1031_REG_CONF1);
		data->conf2 = adm1031_read_value(client, ADM1031_REG_CONF2);

		data->alarm = adm1031_read_value(client, ADM1031_REG_STATUS(0))
		    | (adm1031_read_value(client, ADM1031_REG_STATUS(1)) << 8);
		if (data->chip_type == adm1030)
			data->alarm &= 0xc0ff;

		for (chan = 0; chan < (data->chip_type == adm1030 ? 1 : 2);
		     chan++) {
			data->fan_div[chan] =
			    adm1031_read_value(client,
					       ADM1031_REG_FAN_DIV(chan));
			data->fan_min[chan] =
			    adm1031_read_value(client,
					       ADM1031_REG_FAN_MIN(chan));
			data->fan[chan] =
			    adm1031_read_value(client,
					       ADM1031_REG_FAN_SPEED(chan));
			data->pwm[chan] =
			  (adm1031_read_value(client,
					ADM1031_REG_PWM) >> (4 * chan)) & 0x0f;
		}
		data->last_updated = jiffies;
		data->valid = 1;
	}

	mutex_unlock(&data->update_lock);

	return data;
}

module_i2c_driver(adm1031_driver);

MODULE_AUTHOR("Alexandre d'Alton <alex@alexdalton.org>");
MODULE_DESCRIPTION("ADM1031/ADM1030 driver");
MODULE_LICENSE("GPL");
