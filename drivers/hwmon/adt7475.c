/*
 * adt7475 - Thermal sensor driver for the ADT7475 chip and derivatives
 * Copyright (C) 2007-2008, Advanced Micro Devices, Inc.
 * Copyright (C) 2008 Jordan Crouse <jordan@cosmicpenguin.net>
 * Copyright (C) 2008 Hans de Goede <hdegoede@redhat.com>

 * Derived from the lm83 driver by Jean Delvare
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/hwmon.h>
#include <linux/hwmon-sysfs.h>
#include <linux/err.h>

/* Indexes for the sysfs hooks */

#define INPUT		0
#define MIN		1
#define MAX		2
#define CONTROL		3
#define OFFSET		3
#define AUTOMIN		4
#define THERM		5
#define HYSTERSIS	6

/* These are unique identifiers for the sysfs functions - unlike the
   numbers above, these are not also indexes into an array
*/

#define ALARM		9
#define FAULT		10

/* 7475 Common Registers */

#define REG_VOLTAGE_BASE	0x21
#define REG_TEMP_BASE		0x25
#define REG_TACH_BASE		0x28
#define REG_PWM_BASE		0x30
#define REG_PWM_MAX_BASE	0x38

#define REG_DEVID		0x3D
#define REG_VENDID		0x3E

#define REG_STATUS1		0x41
#define REG_STATUS2		0x42

#define REG_VOLTAGE_MIN_BASE	0x46
#define REG_VOLTAGE_MAX_BASE	0x47

#define REG_TEMP_MIN_BASE	0x4E
#define REG_TEMP_MAX_BASE	0x4F

#define REG_TACH_MIN_BASE	0x54

#define REG_PWM_CONFIG_BASE	0x5C

#define REG_TEMP_TRANGE_BASE	0x5F

#define REG_PWM_MIN_BASE	0x64

#define REG_TEMP_TMIN_BASE	0x67
#define REG_TEMP_THERM_BASE	0x6A

#define REG_REMOTE1_HYSTERSIS	0x6D
#define REG_REMOTE2_HYSTERSIS	0x6E

#define REG_TEMP_OFFSET_BASE	0x70

#define REG_EXTEND1		0x76
#define REG_EXTEND2		0x77
#define REG_CONFIG5		0x7C

#define CONFIG5_TWOSCOMP	0x01
#define CONFIG5_TEMPOFFSET	0x02

/* ADT7475 Settings */

#define ADT7475_VOLTAGE_COUNT	2
#define ADT7475_TEMP_COUNT	3
#define ADT7475_TACH_COUNT	4
#define ADT7475_PWM_COUNT	3

/* Macro to read the registers */

#define adt7475_read(reg) i2c_smbus_read_byte_data(client, (reg))

/* Macros to easily index the registers */

#define TACH_REG(idx) (REG_TACH_BASE + ((idx) * 2))
#define TACH_MIN_REG(idx) (REG_TACH_MIN_BASE + ((idx) * 2))

#define PWM_REG(idx) (REG_PWM_BASE + (idx))
#define PWM_MAX_REG(idx) (REG_PWM_MAX_BASE + (idx))
#define PWM_MIN_REG(idx) (REG_PWM_MIN_BASE + (idx))
#define PWM_CONFIG_REG(idx) (REG_PWM_CONFIG_BASE + (idx))

#define VOLTAGE_REG(idx) (REG_VOLTAGE_BASE + (idx))
#define VOLTAGE_MIN_REG(idx) (REG_VOLTAGE_MIN_BASE + ((idx) * 2))
#define VOLTAGE_MAX_REG(idx) (REG_VOLTAGE_MAX_BASE + ((idx) * 2))

#define TEMP_REG(idx) (REG_TEMP_BASE + (idx))
#define TEMP_MIN_REG(idx) (REG_TEMP_MIN_BASE + ((idx) * 2))
#define TEMP_MAX_REG(idx) (REG_TEMP_MAX_BASE + ((idx) * 2))
#define TEMP_TMIN_REG(idx) (REG_TEMP_TMIN_BASE + (idx))
#define TEMP_THERM_REG(idx) (REG_TEMP_THERM_BASE + (idx))
#define TEMP_OFFSET_REG(idx) (REG_TEMP_OFFSET_BASE + (idx))
#define TEMP_TRANGE_REG(idx) (REG_TEMP_TRANGE_BASE + (idx))

static unsigned short normal_i2c[] = { 0x2e, I2C_CLIENT_END };

I2C_CLIENT_INSMOD_1(adt7475);

static const struct i2c_device_id adt7475_id[] = {
	{ "adt7475", adt7475 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, adt7475_id);

struct adt7475_data {
	struct device *hwmon_dev;
	struct mutex lock;

	unsigned long measure_updated;
	unsigned long limits_updated;
	char valid;

	u8 config5;
	u16 alarms;
	u16 voltage[3][3];
	u16 temp[7][3];
	u16 tach[2][4];
	u8 pwm[4][3];
	u8 range[3];
	u8 pwmctl[3];
	u8 pwmchan[3];
};

static struct i2c_driver adt7475_driver;
static struct adt7475_data *adt7475_update_device(struct device *dev);
static void adt7475_read_hystersis(struct i2c_client *client);
static void adt7475_read_pwm(struct i2c_client *client, int index);

/* Given a temp value, convert it to register value */

static inline u16 temp2reg(struct adt7475_data *data, long val)
{
	u16 ret;

	if (!(data->config5 & CONFIG5_TWOSCOMP)) {
		val = SENSORS_LIMIT(val, -64000, 191000);
		ret = (val + 64500) / 1000;
	} else {
		val = SENSORS_LIMIT(val, -128000, 127000);
		if (val < -500)
			ret = (256500 + val) / 1000;
		else
			ret = (val + 500) / 1000;
	}

	return ret << 2;
}

/* Given a register value, convert it to a real temp value */

static inline int reg2temp(struct adt7475_data *data, u16 reg)
{
	if (data->config5 & CONFIG5_TWOSCOMP) {
		if (reg >= 512)
			return (reg - 1024) * 250;
		else
			return reg * 250;
	} else
		return (reg - 256) * 250;
}

static inline int tach2rpm(u16 tach)
{
	if (tach == 0 || tach == 0xFFFF)
		return 0;

	return (90000 * 60) / tach;
}

static inline u16 rpm2tach(unsigned long rpm)
{
	if (rpm == 0)
		return 0;

	return SENSORS_LIMIT((90000 * 60) / rpm, 1, 0xFFFF);
}

static inline int reg2vcc(u16 reg)
{
	return (4296 * reg) / 1000;
}

static inline int reg2vccp(u16 reg)
{
	return (2929 * reg) / 1000;
}

static inline u16 vcc2reg(long vcc)
{
	vcc = SENSORS_LIMIT(vcc, 0, 4396);
	return (vcc * 1000) / 4296;
}

static inline u16 vccp2reg(long vcc)
{
	vcc = SENSORS_LIMIT(vcc, 0, 2998);
	return (vcc * 1000) / 2929;
}

static u16 adt7475_read_word(struct i2c_client *client, int reg)
{
	u16 val;

	val = i2c_smbus_read_byte_data(client, reg);
	val |= (i2c_smbus_read_byte_data(client, reg + 1) << 8);

	return val;
}

static void adt7475_write_word(struct i2c_client *client, int reg, u16 val)
{
	i2c_smbus_write_byte_data(client, reg + 1, val >> 8);
	i2c_smbus_write_byte_data(client, reg, val & 0xFF);
}

/* Find the nearest value in a table - used for pwm frequency and
   auto temp range */
static int find_nearest(long val, const int *array, int size)
{
	int i;

	if (val < array[0])
		return 0;

	if (val > array[size - 1])
		return size - 1;

	for (i = 0; i < size - 1; i++) {
		int a, b;

		if (val > array[i + 1])
			continue;

		a = val - array[i];
		b = array[i + 1] - val;

		return (a <= b) ? i : i + 1;
	}

	return 0;
}

static ssize_t show_voltage(struct device *dev, struct device_attribute *attr,
			    char *buf)
{
	struct adt7475_data *data = adt7475_update_device(dev);
	struct sensor_device_attribute_2 *sattr = to_sensor_dev_attr_2(attr);
	unsigned short val;

	switch (sattr->nr) {
	case ALARM:
		return sprintf(buf, "%d\n",
			       (data->alarms >> (sattr->index + 1)) & 1);
	default:
		val = data->voltage[sattr->nr][sattr->index];
		return sprintf(buf, "%d\n",
			       sattr->index ==
			       0 ? reg2vccp(val) : reg2vcc(val));
	}
}

static ssize_t set_voltage(struct device *dev, struct device_attribute *attr,
			   const char *buf, size_t count)
{

	struct sensor_device_attribute_2 *sattr = to_sensor_dev_attr_2(attr);
	struct i2c_client *client = to_i2c_client(dev);
	struct adt7475_data *data = i2c_get_clientdata(client);
	unsigned char reg;
	long val;

	if (strict_strtol(buf, 10, &val))
		return -EINVAL;

	mutex_lock(&data->lock);

	data->voltage[sattr->nr][sattr->index] =
		sattr->index ? vcc2reg(val) : vccp2reg(val);

	if (sattr->nr == MIN)
		reg = VOLTAGE_MIN_REG(sattr->index);
	else
		reg = VOLTAGE_MAX_REG(sattr->index);

	i2c_smbus_write_byte_data(client, reg,
				  data->voltage[sattr->nr][sattr->index] >> 2);
	mutex_unlock(&data->lock);

	return count;
}

static ssize_t show_temp(struct device *dev, struct device_attribute *attr,
			 char *buf)
{
	struct adt7475_data *data = adt7475_update_device(dev);
	struct sensor_device_attribute_2 *sattr = to_sensor_dev_attr_2(attr);
	int out;

	switch (sattr->nr) {
	case HYSTERSIS:
		mutex_lock(&data->lock);
		out = data->temp[sattr->nr][sattr->index];
		if (sattr->index != 1)
			out = (out >> 4) & 0xF;
		else
			out = (out & 0xF);
		/* Show the value as an absolute number tied to
		 * THERM */
		out = reg2temp(data, data->temp[THERM][sattr->index]) -
			out * 1000;
		mutex_unlock(&data->lock);
		break;

	case OFFSET:
		/* Offset is always 2's complement, regardless of the
		 * setting in CONFIG5 */
		mutex_lock(&data->lock);
		out = (s8)data->temp[sattr->nr][sattr->index];
		if (data->config5 & CONFIG5_TEMPOFFSET)
			out *= 1000;
		else
			out *= 500;
		mutex_unlock(&data->lock);
		break;

	case ALARM:
		out = (data->alarms >> (sattr->index + 4)) & 1;
		break;

	case FAULT:
		/* Note - only for remote1 and remote2 */
		out = data->alarms & (sattr->index ? 0x8000 : 0x4000);
		out = out ? 0 : 1;
		break;

	default:
		/* All other temp values are in the configured format */
		out = reg2temp(data, data->temp[sattr->nr][sattr->index]);
	}

	return sprintf(buf, "%d\n", out);
}

static ssize_t set_temp(struct device *dev, struct device_attribute *attr,
			const char *buf, size_t count)
{
	struct sensor_device_attribute_2 *sattr = to_sensor_dev_attr_2(attr);
	struct i2c_client *client = to_i2c_client(dev);
	struct adt7475_data *data = i2c_get_clientdata(client);
	unsigned char reg = 0;
	u8 out;
	int temp;
	long val;

	if (strict_strtol(buf, 10, &val))
		return -EINVAL;

	mutex_lock(&data->lock);

	/* We need the config register in all cases for temp <-> reg conv. */
	data->config5 = adt7475_read(REG_CONFIG5);

	switch (sattr->nr) {
	case OFFSET:
		if (data->config5 & CONFIG5_TEMPOFFSET) {
			val = SENSORS_LIMIT(val, -63000, 127000);
			out = data->temp[OFFSET][sattr->index] = val / 1000;
		} else {
			val = SENSORS_LIMIT(val, -63000, 64000);
			out = data->temp[OFFSET][sattr->index] = val / 500;
		}
		break;

	case HYSTERSIS:
		/* The value will be given as an absolute value, turn it
		   into an offset based on THERM */

		/* Read fresh THERM and HYSTERSIS values from the chip */
		data->temp[THERM][sattr->index] =
			adt7475_read(TEMP_THERM_REG(sattr->index)) << 2;
		adt7475_read_hystersis(client);

		temp = reg2temp(data, data->temp[THERM][sattr->index]);
		val = SENSORS_LIMIT(val, temp - 15000, temp);
		val = (temp - val) / 1000;

		if (sattr->index != 1) {
			data->temp[HYSTERSIS][sattr->index] &= 0xF0;
			data->temp[HYSTERSIS][sattr->index] |= (val & 0xF) << 4;
		} else {
			data->temp[HYSTERSIS][sattr->index] &= 0x0F;
			data->temp[HYSTERSIS][sattr->index] |= (val & 0xF);
		}

		out = data->temp[HYSTERSIS][sattr->index];
		break;

	default:
		data->temp[sattr->nr][sattr->index] = temp2reg(data, val);

		/* We maintain an extra 2 digits of precision for simplicity
		 * - shift those back off before writing the value */
		out = (u8) (data->temp[sattr->nr][sattr->index] >> 2);
	}

	switch (sattr->nr) {
	case MIN:
		reg = TEMP_MIN_REG(sattr->index);
		break;
	case MAX:
		reg = TEMP_MAX_REG(sattr->index);
		break;
	case OFFSET:
		reg = TEMP_OFFSET_REG(sattr->index);
		break;
	case AUTOMIN:
		reg = TEMP_TMIN_REG(sattr->index);
		break;
	case THERM:
		reg = TEMP_THERM_REG(sattr->index);
		break;
	case HYSTERSIS:
		if (sattr->index != 2)
			reg = REG_REMOTE1_HYSTERSIS;
		else
			reg = REG_REMOTE2_HYSTERSIS;

		break;
	}

	i2c_smbus_write_byte_data(client, reg, out);

	mutex_unlock(&data->lock);
	return count;
}

/* Table of autorange values - the user will write the value in millidegrees,
   and we'll convert it */
static const int autorange_table[] = {
	2000, 2500, 3330, 4000, 5000, 6670, 8000,
	10000, 13330, 16000, 20000, 26670, 32000, 40000,
	53330, 80000
};

static ssize_t show_point2(struct device *dev, struct device_attribute *attr,
			   char *buf)
{
	struct adt7475_data *data = adt7475_update_device(dev);
	struct sensor_device_attribute_2 *sattr = to_sensor_dev_attr_2(attr);
	int out, val;

	mutex_lock(&data->lock);
	out = (data->range[sattr->index] >> 4) & 0x0F;
	val = reg2temp(data, data->temp[AUTOMIN][sattr->index]);
	mutex_unlock(&data->lock);

	return sprintf(buf, "%d\n", val + autorange_table[out]);
}

static ssize_t set_point2(struct device *dev, struct device_attribute *attr,
			  const char *buf, size_t count)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct adt7475_data *data = i2c_get_clientdata(client);
	struct sensor_device_attribute_2 *sattr = to_sensor_dev_attr_2(attr);
	int temp;
	long val;

	if (strict_strtol(buf, 10, &val))
		return -EINVAL;

	mutex_lock(&data->lock);

	/* Get a fresh copy of the needed registers */
	data->config5 = adt7475_read(REG_CONFIG5);
	data->temp[AUTOMIN][sattr->index] =
		adt7475_read(TEMP_TMIN_REG(sattr->index)) << 2;
	data->range[sattr->index] =
		adt7475_read(TEMP_TRANGE_REG(sattr->index));

	/* The user will write an absolute value, so subtract the start point
	   to figure the range */
	temp = reg2temp(data, data->temp[AUTOMIN][sattr->index]);
	val = SENSORS_LIMIT(val, temp + autorange_table[0],
		temp + autorange_table[ARRAY_SIZE(autorange_table) - 1]);
	val -= temp;

	/* Find the nearest table entry to what the user wrote */
	val = find_nearest(val, autorange_table, ARRAY_SIZE(autorange_table));

	data->range[sattr->index] &= ~0xF0;
	data->range[sattr->index] |= val << 4;

	i2c_smbus_write_byte_data(client, TEMP_TRANGE_REG(sattr->index),
				  data->range[sattr->index]);

	mutex_unlock(&data->lock);
	return count;
}

static ssize_t show_tach(struct device *dev, struct device_attribute *attr,
			 char *buf)
{
	struct adt7475_data *data = adt7475_update_device(dev);
	struct sensor_device_attribute_2 *sattr = to_sensor_dev_attr_2(attr);
	int out;

	if (sattr->nr == ALARM)
		out = (data->alarms >> (sattr->index + 10)) & 1;
	else
		out = tach2rpm(data->tach[sattr->nr][sattr->index]);

	return sprintf(buf, "%d\n", out);
}

static ssize_t set_tach(struct device *dev, struct device_attribute *attr,
			const char *buf, size_t count)
{

	struct sensor_device_attribute_2 *sattr = to_sensor_dev_attr_2(attr);
	struct i2c_client *client = to_i2c_client(dev);
	struct adt7475_data *data = i2c_get_clientdata(client);
	unsigned long val;

	if (strict_strtoul(buf, 10, &val))
		return -EINVAL;

	mutex_lock(&data->lock);

	data->tach[MIN][sattr->index] = rpm2tach(val);

	adt7475_write_word(client, TACH_MIN_REG(sattr->index),
			   data->tach[MIN][sattr->index]);

	mutex_unlock(&data->lock);
	return count;
}

static ssize_t show_pwm(struct device *dev, struct device_attribute *attr,
			char *buf)
{
	struct adt7475_data *data = adt7475_update_device(dev);
	struct sensor_device_attribute_2 *sattr = to_sensor_dev_attr_2(attr);

	return sprintf(buf, "%d\n", data->pwm[sattr->nr][sattr->index]);
}

static ssize_t show_pwmchan(struct device *dev, struct device_attribute *attr,
			    char *buf)
{
	struct adt7475_data *data = adt7475_update_device(dev);
	struct sensor_device_attribute_2 *sattr = to_sensor_dev_attr_2(attr);

	return sprintf(buf, "%d\n", data->pwmchan[sattr->index]);
}

static ssize_t show_pwmctrl(struct device *dev, struct device_attribute *attr,
			    char *buf)
{
	struct adt7475_data *data = adt7475_update_device(dev);
	struct sensor_device_attribute_2 *sattr = to_sensor_dev_attr_2(attr);

	return sprintf(buf, "%d\n", data->pwmctl[sattr->index]);
}

static ssize_t set_pwm(struct device *dev, struct device_attribute *attr,
		       const char *buf, size_t count)
{

	struct sensor_device_attribute_2 *sattr = to_sensor_dev_attr_2(attr);
	struct i2c_client *client = to_i2c_client(dev);
	struct adt7475_data *data = i2c_get_clientdata(client);
	unsigned char reg = 0;
	long val;

	if (strict_strtol(buf, 10, &val))
		return -EINVAL;

	mutex_lock(&data->lock);

	switch (sattr->nr) {
	case INPUT:
		/* Get a fresh value for CONTROL */
		data->pwm[CONTROL][sattr->index] =
			adt7475_read(PWM_CONFIG_REG(sattr->index));

		/* If we are not in manual mode, then we shouldn't allow
		 * the user to set the pwm speed */
		if (((data->pwm[CONTROL][sattr->index] >> 5) & 7) != 7) {
			mutex_unlock(&data->lock);
			return count;
		}

		reg = PWM_REG(sattr->index);
		break;

	case MIN:
		reg = PWM_MIN_REG(sattr->index);
		break;

	case MAX:
		reg = PWM_MAX_REG(sattr->index);
		break;
	}

	data->pwm[sattr->nr][sattr->index] = SENSORS_LIMIT(val, 0, 0xFF);
	i2c_smbus_write_byte_data(client, reg,
				  data->pwm[sattr->nr][sattr->index]);

	mutex_unlock(&data->lock);

	return count;
}

/* Called by set_pwmctrl and set_pwmchan */

static int hw_set_pwm(struct i2c_client *client, int index,
		      unsigned int pwmctl, unsigned int pwmchan)
{
	struct adt7475_data *data = i2c_get_clientdata(client);
	long val = 0;

	switch (pwmctl) {
	case 0:
		val = 0x03;	/* Run at full speed */
		break;
	case 1:
		val = 0x07;	/* Manual mode */
		break;
	case 2:
		switch (pwmchan) {
		case 1:
			/* Remote1 controls PWM */
			val = 0x00;
			break;
		case 2:
			/* local controls PWM */
			val = 0x01;
			break;
		case 4:
			/* remote2 controls PWM */
			val = 0x02;
			break;
		case 6:
			/* local/remote2 control PWM */
			val = 0x05;
			break;
		case 7:
			/* All three control PWM */
			val = 0x06;
			break;
		default:
			return -EINVAL;
		}
		break;
	default:
		return -EINVAL;
	}

	data->pwmctl[index] = pwmctl;
	data->pwmchan[index] = pwmchan;

	data->pwm[CONTROL][index] &= ~0xE0;
	data->pwm[CONTROL][index] |= (val & 7) << 5;

	i2c_smbus_write_byte_data(client, PWM_CONFIG_REG(index),
				  data->pwm[CONTROL][index]);

	return 0;
}

static ssize_t set_pwmchan(struct device *dev, struct device_attribute *attr,
			   const char *buf, size_t count)
{
	struct sensor_device_attribute_2 *sattr = to_sensor_dev_attr_2(attr);
	struct i2c_client *client = to_i2c_client(dev);
	struct adt7475_data *data = i2c_get_clientdata(client);
	int r;
	long val;

	if (strict_strtol(buf, 10, &val))
		return -EINVAL;

	mutex_lock(&data->lock);
	/* Read Modify Write PWM values */
	adt7475_read_pwm(client, sattr->index);
	r = hw_set_pwm(client, sattr->index, data->pwmctl[sattr->index], val);
	if (r)
		count = r;
	mutex_unlock(&data->lock);

	return count;
}

static ssize_t set_pwmctrl(struct device *dev, struct device_attribute *attr,
			   const char *buf, size_t count)
{
	struct sensor_device_attribute_2 *sattr = to_sensor_dev_attr_2(attr);
	struct i2c_client *client = to_i2c_client(dev);
	struct adt7475_data *data = i2c_get_clientdata(client);
	int r;
	long val;

	if (strict_strtol(buf, 10, &val))
		return -EINVAL;

	mutex_lock(&data->lock);
	/* Read Modify Write PWM values */
	adt7475_read_pwm(client, sattr->index);
	r = hw_set_pwm(client, sattr->index, val, data->pwmchan[sattr->index]);
	if (r)
		count = r;
	mutex_unlock(&data->lock);

	return count;
}

/* List of frequencies for the PWM */
static const int pwmfreq_table[] = {
	11, 14, 22, 29, 35, 44, 58, 88
};

static ssize_t show_pwmfreq(struct device *dev, struct device_attribute *attr,
			    char *buf)
{
	struct adt7475_data *data = adt7475_update_device(dev);
	struct sensor_device_attribute_2 *sattr = to_sensor_dev_attr_2(attr);

	return sprintf(buf, "%d\n",
		       pwmfreq_table[data->range[sattr->index] & 7]);
}

static ssize_t set_pwmfreq(struct device *dev, struct device_attribute *attr,
			   const char *buf, size_t count)
{
	struct sensor_device_attribute_2 *sattr = to_sensor_dev_attr_2(attr);
	struct i2c_client *client = to_i2c_client(dev);
	struct adt7475_data *data = i2c_get_clientdata(client);
	int out;
	long val;

	if (strict_strtol(buf, 10, &val))
		return -EINVAL;

	out = find_nearest(val, pwmfreq_table, ARRAY_SIZE(pwmfreq_table));

	mutex_lock(&data->lock);

	data->range[sattr->index] =
		adt7475_read(TEMP_TRANGE_REG(sattr->index));
	data->range[sattr->index] &= ~7;
	data->range[sattr->index] |= out;

	i2c_smbus_write_byte_data(client, TEMP_TRANGE_REG(sattr->index),
				  data->range[sattr->index]);

	mutex_unlock(&data->lock);
	return count;
}

static SENSOR_DEVICE_ATTR_2(in1_input, S_IRUGO, show_voltage, NULL, INPUT, 0);
static SENSOR_DEVICE_ATTR_2(in1_max, S_IRUGO | S_IWUSR, show_voltage,
			    set_voltage, MAX, 0);
static SENSOR_DEVICE_ATTR_2(in1_min, S_IRUGO | S_IWUSR, show_voltage,
			    set_voltage, MIN, 0);
static SENSOR_DEVICE_ATTR_2(in1_alarm, S_IRUGO, show_voltage, NULL, ALARM, 0);
static SENSOR_DEVICE_ATTR_2(in2_input, S_IRUGO, show_voltage, NULL, INPUT, 1);
static SENSOR_DEVICE_ATTR_2(in2_max, S_IRUGO | S_IWUSR, show_voltage,
			    set_voltage, MAX, 1);
static SENSOR_DEVICE_ATTR_2(in2_min, S_IRUGO | S_IWUSR, show_voltage,
			    set_voltage, MIN, 1);
static SENSOR_DEVICE_ATTR_2(in2_alarm, S_IRUGO, show_voltage, NULL, ALARM, 1);
static SENSOR_DEVICE_ATTR_2(temp1_input, S_IRUGO, show_temp, NULL, INPUT, 0);
static SENSOR_DEVICE_ATTR_2(temp1_alarm, S_IRUGO, show_temp, NULL, ALARM, 0);
static SENSOR_DEVICE_ATTR_2(temp1_fault, S_IRUGO, show_temp, NULL, FAULT, 0);
static SENSOR_DEVICE_ATTR_2(temp1_max, S_IRUGO | S_IWUSR, show_temp, set_temp,
			    MAX, 0);
static SENSOR_DEVICE_ATTR_2(temp1_min, S_IRUGO | S_IWUSR, show_temp, set_temp,
			    MIN, 0);
static SENSOR_DEVICE_ATTR_2(temp1_offset, S_IRUGO | S_IWUSR, show_temp,
			    set_temp, OFFSET, 0);
static SENSOR_DEVICE_ATTR_2(temp1_auto_point1_temp, S_IRUGO | S_IWUSR,
			    show_temp, set_temp, AUTOMIN, 0);
static SENSOR_DEVICE_ATTR_2(temp1_auto_point2_temp, S_IRUGO | S_IWUSR,
			    show_point2, set_point2, 0, 0);
static SENSOR_DEVICE_ATTR_2(temp1_crit, S_IRUGO | S_IWUSR, show_temp, set_temp,
			    THERM, 0);
static SENSOR_DEVICE_ATTR_2(temp1_crit_hyst, S_IRUGO | S_IWUSR, show_temp,
			    set_temp, HYSTERSIS, 0);
static SENSOR_DEVICE_ATTR_2(temp2_input, S_IRUGO, show_temp, NULL, INPUT, 1);
static SENSOR_DEVICE_ATTR_2(temp2_alarm, S_IRUGO, show_temp, NULL, ALARM, 1);
static SENSOR_DEVICE_ATTR_2(temp2_max, S_IRUGO | S_IWUSR, show_temp, set_temp,
			    MAX, 1);
static SENSOR_DEVICE_ATTR_2(temp2_min, S_IRUGO | S_IWUSR, show_temp, set_temp,
			    MIN, 1);
static SENSOR_DEVICE_ATTR_2(temp2_offset, S_IRUGO | S_IWUSR, show_temp,
			    set_temp, OFFSET, 1);
static SENSOR_DEVICE_ATTR_2(temp2_auto_point1_temp, S_IRUGO | S_IWUSR,
			    show_temp, set_temp, AUTOMIN, 1);
static SENSOR_DEVICE_ATTR_2(temp2_auto_point2_temp, S_IRUGO | S_IWUSR,
			    show_point2, set_point2, 0, 1);
static SENSOR_DEVICE_ATTR_2(temp2_crit, S_IRUGO | S_IWUSR, show_temp, set_temp,
			    THERM, 1);
static SENSOR_DEVICE_ATTR_2(temp2_crit_hyst, S_IRUGO | S_IWUSR, show_temp,
			    set_temp, HYSTERSIS, 1);
static SENSOR_DEVICE_ATTR_2(temp3_input, S_IRUGO, show_temp, NULL, INPUT, 2);
static SENSOR_DEVICE_ATTR_2(temp3_alarm, S_IRUGO, show_temp, NULL, ALARM, 2);
static SENSOR_DEVICE_ATTR_2(temp3_fault, S_IRUGO, show_temp, NULL, FAULT, 2);
static SENSOR_DEVICE_ATTR_2(temp3_max, S_IRUGO | S_IWUSR, show_temp, set_temp,
			    MAX, 2);
static SENSOR_DEVICE_ATTR_2(temp3_min, S_IRUGO | S_IWUSR, show_temp, set_temp,
			    MIN, 2);
static SENSOR_DEVICE_ATTR_2(temp3_offset, S_IRUGO | S_IWUSR, show_temp,
			    set_temp, OFFSET, 2);
static SENSOR_DEVICE_ATTR_2(temp3_auto_point1_temp, S_IRUGO | S_IWUSR,
			    show_temp, set_temp, AUTOMIN, 2);
static SENSOR_DEVICE_ATTR_2(temp3_auto_point2_temp, S_IRUGO | S_IWUSR,
			    show_point2, set_point2, 0, 2);
static SENSOR_DEVICE_ATTR_2(temp3_crit, S_IRUGO | S_IWUSR, show_temp, set_temp,
			    THERM, 2);
static SENSOR_DEVICE_ATTR_2(temp3_crit_hyst, S_IRUGO | S_IWUSR, show_temp,
			    set_temp, HYSTERSIS, 2);
static SENSOR_DEVICE_ATTR_2(fan1_input, S_IRUGO, show_tach, NULL, INPUT, 0);
static SENSOR_DEVICE_ATTR_2(fan1_min, S_IRUGO | S_IWUSR, show_tach, set_tach,
			    MIN, 0);
static SENSOR_DEVICE_ATTR_2(fan1_alarm, S_IRUGO, show_tach, NULL, ALARM, 0);
static SENSOR_DEVICE_ATTR_2(fan2_input, S_IRUGO, show_tach, NULL, INPUT, 1);
static SENSOR_DEVICE_ATTR_2(fan2_min, S_IRUGO | S_IWUSR, show_tach, set_tach,
			    MIN, 1);
static SENSOR_DEVICE_ATTR_2(fan2_alarm, S_IRUGO, show_tach, NULL, ALARM, 1);
static SENSOR_DEVICE_ATTR_2(fan3_input, S_IRUGO, show_tach, NULL, INPUT, 2);
static SENSOR_DEVICE_ATTR_2(fan3_min, S_IRUGO | S_IWUSR, show_tach, set_tach,
			    MIN, 2);
static SENSOR_DEVICE_ATTR_2(fan3_alarm, S_IRUGO, show_tach, NULL, ALARM, 2);
static SENSOR_DEVICE_ATTR_2(fan4_input, S_IRUGO, show_tach, NULL, INPUT, 3);
static SENSOR_DEVICE_ATTR_2(fan4_min, S_IRUGO | S_IWUSR, show_tach, set_tach,
			    MIN, 3);
static SENSOR_DEVICE_ATTR_2(fan4_alarm, S_IRUGO, show_tach, NULL, ALARM, 3);
static SENSOR_DEVICE_ATTR_2(pwm1, S_IRUGO | S_IWUSR, show_pwm, set_pwm, INPUT,
			    0);
static SENSOR_DEVICE_ATTR_2(pwm1_freq, S_IRUGO | S_IWUSR, show_pwmfreq,
			    set_pwmfreq, INPUT, 0);
static SENSOR_DEVICE_ATTR_2(pwm1_enable, S_IRUGO | S_IWUSR, show_pwmctrl,
			    set_pwmctrl, INPUT, 0);
static SENSOR_DEVICE_ATTR_2(pwm1_auto_channel_temp, S_IRUGO | S_IWUSR,
			    show_pwmchan, set_pwmchan, INPUT, 0);
static SENSOR_DEVICE_ATTR_2(pwm1_auto_point1_pwm, S_IRUGO | S_IWUSR, show_pwm,
			    set_pwm, MIN, 0);
static SENSOR_DEVICE_ATTR_2(pwm1_auto_point2_pwm, S_IRUGO | S_IWUSR, show_pwm,
			    set_pwm, MAX, 0);
static SENSOR_DEVICE_ATTR_2(pwm2, S_IRUGO | S_IWUSR, show_pwm, set_pwm, INPUT,
			    1);
static SENSOR_DEVICE_ATTR_2(pwm2_freq, S_IRUGO | S_IWUSR, show_pwmfreq,
			    set_pwmfreq, INPUT, 1);
static SENSOR_DEVICE_ATTR_2(pwm2_enable, S_IRUGO | S_IWUSR, show_pwmctrl,
			    set_pwmctrl, INPUT, 1);
static SENSOR_DEVICE_ATTR_2(pwm2_auto_channel_temp, S_IRUGO | S_IWUSR,
			    show_pwmchan, set_pwmchan, INPUT, 1);
static SENSOR_DEVICE_ATTR_2(pwm2_auto_point1_pwm, S_IRUGO | S_IWUSR, show_pwm,
			    set_pwm, MIN, 1);
static SENSOR_DEVICE_ATTR_2(pwm2_auto_point2_pwm, S_IRUGO | S_IWUSR, show_pwm,
			    set_pwm, MAX, 1);
static SENSOR_DEVICE_ATTR_2(pwm3, S_IRUGO | S_IWUSR, show_pwm, set_pwm, INPUT,
			    2);
static SENSOR_DEVICE_ATTR_2(pwm3_freq, S_IRUGO | S_IWUSR, show_pwmfreq,
			    set_pwmfreq, INPUT, 2);
static SENSOR_DEVICE_ATTR_2(pwm3_enable, S_IRUGO | S_IWUSR, show_pwmctrl,
			    set_pwmctrl, INPUT, 2);
static SENSOR_DEVICE_ATTR_2(pwm3_auto_channel_temp, S_IRUGO | S_IWUSR,
			    show_pwmchan, set_pwmchan, INPUT, 2);
static SENSOR_DEVICE_ATTR_2(pwm3_auto_point1_pwm, S_IRUGO | S_IWUSR, show_pwm,
			    set_pwm, MIN, 2);
static SENSOR_DEVICE_ATTR_2(pwm3_auto_point2_pwm, S_IRUGO | S_IWUSR, show_pwm,
			    set_pwm, MAX, 2);

static struct attribute *adt7475_attrs[] = {
	&sensor_dev_attr_in1_input.dev_attr.attr,
	&sensor_dev_attr_in1_max.dev_attr.attr,
	&sensor_dev_attr_in1_min.dev_attr.attr,
	&sensor_dev_attr_in1_alarm.dev_attr.attr,
	&sensor_dev_attr_in2_input.dev_attr.attr,
	&sensor_dev_attr_in2_max.dev_attr.attr,
	&sensor_dev_attr_in2_min.dev_attr.attr,
	&sensor_dev_attr_in2_alarm.dev_attr.attr,
	&sensor_dev_attr_temp1_input.dev_attr.attr,
	&sensor_dev_attr_temp1_alarm.dev_attr.attr,
	&sensor_dev_attr_temp1_fault.dev_attr.attr,
	&sensor_dev_attr_temp1_max.dev_attr.attr,
	&sensor_dev_attr_temp1_min.dev_attr.attr,
	&sensor_dev_attr_temp1_offset.dev_attr.attr,
	&sensor_dev_attr_temp1_auto_point1_temp.dev_attr.attr,
	&sensor_dev_attr_temp1_auto_point2_temp.dev_attr.attr,
	&sensor_dev_attr_temp1_crit.dev_attr.attr,
	&sensor_dev_attr_temp1_crit_hyst.dev_attr.attr,
	&sensor_dev_attr_temp2_input.dev_attr.attr,
	&sensor_dev_attr_temp2_alarm.dev_attr.attr,
	&sensor_dev_attr_temp2_max.dev_attr.attr,
	&sensor_dev_attr_temp2_min.dev_attr.attr,
	&sensor_dev_attr_temp2_offset.dev_attr.attr,
	&sensor_dev_attr_temp2_auto_point1_temp.dev_attr.attr,
	&sensor_dev_attr_temp2_auto_point2_temp.dev_attr.attr,
	&sensor_dev_attr_temp2_crit.dev_attr.attr,
	&sensor_dev_attr_temp2_crit_hyst.dev_attr.attr,
	&sensor_dev_attr_temp3_input.dev_attr.attr,
	&sensor_dev_attr_temp3_fault.dev_attr.attr,
	&sensor_dev_attr_temp3_alarm.dev_attr.attr,
	&sensor_dev_attr_temp3_max.dev_attr.attr,
	&sensor_dev_attr_temp3_min.dev_attr.attr,
	&sensor_dev_attr_temp3_offset.dev_attr.attr,
	&sensor_dev_attr_temp3_auto_point1_temp.dev_attr.attr,
	&sensor_dev_attr_temp3_auto_point2_temp.dev_attr.attr,
	&sensor_dev_attr_temp3_crit.dev_attr.attr,
	&sensor_dev_attr_temp3_crit_hyst.dev_attr.attr,
	&sensor_dev_attr_fan1_input.dev_attr.attr,
	&sensor_dev_attr_fan1_min.dev_attr.attr,
	&sensor_dev_attr_fan1_alarm.dev_attr.attr,
	&sensor_dev_attr_fan2_input.dev_attr.attr,
	&sensor_dev_attr_fan2_min.dev_attr.attr,
	&sensor_dev_attr_fan2_alarm.dev_attr.attr,
	&sensor_dev_attr_fan3_input.dev_attr.attr,
	&sensor_dev_attr_fan3_min.dev_attr.attr,
	&sensor_dev_attr_fan3_alarm.dev_attr.attr,
	&sensor_dev_attr_fan4_input.dev_attr.attr,
	&sensor_dev_attr_fan4_min.dev_attr.attr,
	&sensor_dev_attr_fan4_alarm.dev_attr.attr,
	&sensor_dev_attr_pwm1.dev_attr.attr,
	&sensor_dev_attr_pwm1_freq.dev_attr.attr,
	&sensor_dev_attr_pwm1_enable.dev_attr.attr,
	&sensor_dev_attr_pwm1_auto_channel_temp.dev_attr.attr,
	&sensor_dev_attr_pwm1_auto_point1_pwm.dev_attr.attr,
	&sensor_dev_attr_pwm1_auto_point2_pwm.dev_attr.attr,
	&sensor_dev_attr_pwm2.dev_attr.attr,
	&sensor_dev_attr_pwm2_freq.dev_attr.attr,
	&sensor_dev_attr_pwm2_enable.dev_attr.attr,
	&sensor_dev_attr_pwm2_auto_channel_temp.dev_attr.attr,
	&sensor_dev_attr_pwm2_auto_point1_pwm.dev_attr.attr,
	&sensor_dev_attr_pwm2_auto_point2_pwm.dev_attr.attr,
	&sensor_dev_attr_pwm3.dev_attr.attr,
	&sensor_dev_attr_pwm3_freq.dev_attr.attr,
	&sensor_dev_attr_pwm3_enable.dev_attr.attr,
	&sensor_dev_attr_pwm3_auto_channel_temp.dev_attr.attr,
	&sensor_dev_attr_pwm3_auto_point1_pwm.dev_attr.attr,
	&sensor_dev_attr_pwm3_auto_point2_pwm.dev_attr.attr,
	NULL,
};

struct attribute_group adt7475_attr_group = { .attrs = adt7475_attrs };

static int adt7475_detect(struct i2c_client *client, int kind,
			  struct i2c_board_info *info)
{
	struct i2c_adapter *adapter = client->adapter;

	if (!i2c_check_functionality(adapter, I2C_FUNC_SMBUS_BYTE_DATA))
		return -ENODEV;

	if (kind <= 0) {
		if (adt7475_read(REG_VENDID) != 0x41 ||
		    adt7475_read(REG_DEVID) != 0x75) {
			dev_err(&adapter->dev,
				"Couldn't detect a adt7475 part at 0x%02x\n",
				(unsigned int)client->addr);
			return -ENODEV;
		}
	}

	strlcpy(info->type, adt7475_id[0].name, I2C_NAME_SIZE);

	return 0;
}

static int adt7475_probe(struct i2c_client *client,
			 const struct i2c_device_id *id)
{
	struct adt7475_data *data;
	int i, ret = 0;

	data = kzalloc(sizeof(*data), GFP_KERNEL);
	if (data == NULL)
		return -ENOMEM;

	mutex_init(&data->lock);
	i2c_set_clientdata(client, data);

	/* Call adt7475_read_pwm for all pwm's as this will reprogram any
	   pwm's which are disabled to manual mode with 0% duty cycle */
	for (i = 0; i < ADT7475_PWM_COUNT; i++)
		adt7475_read_pwm(client, i);

	ret = sysfs_create_group(&client->dev.kobj, &adt7475_attr_group);
	if (ret)
		goto efree;

	data->hwmon_dev = hwmon_device_register(&client->dev);
	if (IS_ERR(data->hwmon_dev)) {
		ret = PTR_ERR(data->hwmon_dev);
		goto eremove;
	}

	return 0;

eremove:
	sysfs_remove_group(&client->dev.kobj, &adt7475_attr_group);
efree:
	kfree(data);
	return ret;
}

static int adt7475_remove(struct i2c_client *client)
{
	struct adt7475_data *data = i2c_get_clientdata(client);

	hwmon_device_unregister(data->hwmon_dev);
	sysfs_remove_group(&client->dev.kobj, &adt7475_attr_group);
	kfree(data);

	return 0;
}

static struct i2c_driver adt7475_driver = {
	.class		= I2C_CLASS_HWMON,
	.driver = {
		.name	= "adt7475",
	},
	.probe		= adt7475_probe,
	.remove		= adt7475_remove,
	.id_table	= adt7475_id,
	.detect		= adt7475_detect,
	.address_data	= &addr_data,
};

static void adt7475_read_hystersis(struct i2c_client *client)
{
	struct adt7475_data *data = i2c_get_clientdata(client);

	data->temp[HYSTERSIS][0] = (u16) adt7475_read(REG_REMOTE1_HYSTERSIS);
	data->temp[HYSTERSIS][1] = data->temp[HYSTERSIS][0];
	data->temp[HYSTERSIS][2] = (u16) adt7475_read(REG_REMOTE2_HYSTERSIS);
}

static void adt7475_read_pwm(struct i2c_client *client, int index)
{
	struct adt7475_data *data = i2c_get_clientdata(client);
	unsigned int v;

	data->pwm[CONTROL][index] = adt7475_read(PWM_CONFIG_REG(index));

	/* Figure out the internal value for pwmctrl and pwmchan
	   based on the current settings */
	v = (data->pwm[CONTROL][index] >> 5) & 7;

	if (v == 3)
		data->pwmctl[index] = 0;
	else if (v == 7)
		data->pwmctl[index] = 1;
	else if (v == 4) {
		/* The fan is disabled - we don't want to
		   support that, so change to manual mode and
		   set the duty cycle to 0 instead
		*/
		data->pwm[INPUT][index] = 0;
		data->pwm[CONTROL][index] &= ~0xE0;
		data->pwm[CONTROL][index] |= (7 << 5);

		i2c_smbus_write_byte_data(client, PWM_CONFIG_REG(index),
					  data->pwm[INPUT][index]);

		i2c_smbus_write_byte_data(client, PWM_CONFIG_REG(index),
					  data->pwm[CONTROL][index]);

		data->pwmctl[index] = 1;
	} else {
		data->pwmctl[index] = 2;

		switch (v) {
		case 0:
			data->pwmchan[index] = 1;
			break;
		case 1:
			data->pwmchan[index] = 2;
			break;
		case 2:
			data->pwmchan[index] = 4;
			break;
		case 5:
			data->pwmchan[index] = 6;
			break;
		case 6:
			data->pwmchan[index] = 7;
			break;
		}
	}
}

static struct adt7475_data *adt7475_update_device(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct adt7475_data *data = i2c_get_clientdata(client);
	u8 ext;
	int i;

	mutex_lock(&data->lock);

	/* Measurement values update every 2 seconds */
	if (time_after(jiffies, data->measure_updated + HZ * 2) ||
	    !data->valid) {
		data->alarms = adt7475_read(REG_STATUS2) << 8;
		data->alarms |= adt7475_read(REG_STATUS1);

		ext = adt7475_read(REG_EXTEND1);
		for (i = 0; i < ADT7475_VOLTAGE_COUNT; i++)
			data->voltage[INPUT][i] =
				(adt7475_read(VOLTAGE_REG(i)) << 2) |
				((ext >> ((i + 1) * 2)) & 3);

		ext = adt7475_read(REG_EXTEND2);
		for (i = 0; i < ADT7475_TEMP_COUNT; i++)
			data->temp[INPUT][i] =
				(adt7475_read(TEMP_REG(i)) << 2) |
				((ext >> ((i + 1) * 2)) & 3);

		for (i = 0; i < ADT7475_TACH_COUNT; i++)
			data->tach[INPUT][i] =
				adt7475_read_word(client, TACH_REG(i));

		/* Updated by hw when in auto mode */
		for (i = 0; i < ADT7475_PWM_COUNT; i++)
			data->pwm[INPUT][i] = adt7475_read(PWM_REG(i));

		data->measure_updated = jiffies;
	}

	/* Limits and settings, should never change update every 60 seconds */
	if (time_after(jiffies, data->limits_updated + HZ * 2) ||
	    !data->valid) {
		data->config5 = adt7475_read(REG_CONFIG5);

		for (i = 0; i < ADT7475_VOLTAGE_COUNT; i++) {
			/* Adjust values so they match the input precision */
			data->voltage[MIN][i] =
				adt7475_read(VOLTAGE_MIN_REG(i)) << 2;
			data->voltage[MAX][i] =
				adt7475_read(VOLTAGE_MAX_REG(i)) << 2;
		}

		for (i = 0; i < ADT7475_TEMP_COUNT; i++) {
			/* Adjust values so they match the input precision */
			data->temp[MIN][i] =
				adt7475_read(TEMP_MIN_REG(i)) << 2;
			data->temp[MAX][i] =
				adt7475_read(TEMP_MAX_REG(i)) << 2;
			data->temp[AUTOMIN][i] =
				adt7475_read(TEMP_TMIN_REG(i)) << 2;
			data->temp[THERM][i] =
				adt7475_read(TEMP_THERM_REG(i)) << 2;
			data->temp[OFFSET][i] =
				adt7475_read(TEMP_OFFSET_REG(i));
		}
		adt7475_read_hystersis(client);

		for (i = 0; i < ADT7475_TACH_COUNT; i++)
			data->tach[MIN][i] =
				adt7475_read_word(client, TACH_MIN_REG(i));

		for (i = 0; i < ADT7475_PWM_COUNT; i++) {
			data->pwm[MAX][i] = adt7475_read(PWM_MAX_REG(i));
			data->pwm[MIN][i] = adt7475_read(PWM_MIN_REG(i));
			/* Set the channel and control information */
			adt7475_read_pwm(client, i);
		}

		data->range[0] = adt7475_read(TEMP_TRANGE_REG(0));
		data->range[1] = adt7475_read(TEMP_TRANGE_REG(1));
		data->range[2] = adt7475_read(TEMP_TRANGE_REG(2));

		data->limits_updated = jiffies;
		data->valid = 1;
	}

	mutex_unlock(&data->lock);

	return data;
}

static int __init sensors_adt7475_init(void)
{
	return i2c_add_driver(&adt7475_driver);
}

static void __exit sensors_adt7475_exit(void)
{
	i2c_del_driver(&adt7475_driver);
}

MODULE_AUTHOR("Advanced Micro Devices, Inc");
MODULE_DESCRIPTION("adt7475 driver");
MODULE_LICENSE("GPL");

module_init(sensors_adt7475_init);
module_exit(sensors_adt7475_exit);
