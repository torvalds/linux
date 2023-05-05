// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * A hwmon driver for the Analog Devices ADT7470
 * Copyright (C) 2007 IBM
 *
 * Author: Darrick J. Wong <darrick.wong@oracle.com>
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/jiffies.h>
#include <linux/i2c.h>
#include <linux/hwmon.h>
#include <linux/hwmon-sysfs.h>
#include <linux/err.h>
#include <linux/mutex.h>
#include <linux/delay.h>
#include <linux/log2.h>
#include <linux/kthread.h>
#include <linux/regmap.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/util_macros.h>

/* Addresses to scan */
static const unsigned short normal_i2c[] = { 0x2C, 0x2E, 0x2F, I2C_CLIENT_END };

/* ADT7470 registers */
#define ADT7470_REG_BASE_ADDR			0x20
#define ADT7470_REG_TEMP_BASE_ADDR		0x20
#define ADT7470_REG_TEMP_MAX_ADDR		0x29
#define ADT7470_REG_FAN_BASE_ADDR		0x2A
#define ADT7470_REG_FAN_MAX_ADDR		0x31
#define ADT7470_REG_PWM_BASE_ADDR		0x32
#define ADT7470_REG_PWM_MAX_ADDR		0x35
#define ADT7470_REG_PWM_MAX_BASE_ADDR		0x38
#define ADT7470_REG_PWM_MAX_MAX_ADDR		0x3B
#define ADT7470_REG_CFG				0x40
#define		ADT7470_STRT_MASK		0x01
#define		ADT7470_TEST_MASK		0x02
#define		ADT7470_FSPD_MASK		0x04
#define		ADT7470_T05_STB_MASK		0x80
#define ADT7470_REG_ALARM1			0x41
#define		ADT7470_R1T_ALARM		0x01
#define		ADT7470_R2T_ALARM		0x02
#define		ADT7470_R3T_ALARM		0x04
#define		ADT7470_R4T_ALARM		0x08
#define		ADT7470_R5T_ALARM		0x10
#define		ADT7470_R6T_ALARM		0x20
#define		ADT7470_R7T_ALARM		0x40
#define		ADT7470_OOL_ALARM		0x80
#define ADT7470_REG_ALARM2			0x42
#define		ADT7470_R8T_ALARM		0x01
#define		ADT7470_R9T_ALARM		0x02
#define		ADT7470_R10T_ALARM		0x04
#define		ADT7470_FAN1_ALARM		0x10
#define		ADT7470_FAN2_ALARM		0x20
#define		ADT7470_FAN3_ALARM		0x40
#define		ADT7470_FAN4_ALARM		0x80
#define ADT7470_REG_TEMP_LIMITS_BASE_ADDR	0x44
#define ADT7470_REG_TEMP_LIMITS_MAX_ADDR	0x57
#define ADT7470_REG_FAN_MIN_BASE_ADDR		0x58
#define ADT7470_REG_FAN_MIN_MAX_ADDR		0x5F
#define ADT7470_REG_FAN_MAX_BASE_ADDR		0x60
#define ADT7470_REG_FAN_MAX_MAX_ADDR		0x67
#define ADT7470_REG_PWM_CFG_BASE_ADDR		0x68
#define ADT7470_REG_PWM12_CFG			0x68
#define		ADT7470_PWM2_AUTO_MASK		0x40
#define		ADT7470_PWM1_AUTO_MASK		0x80
#define		ADT7470_PWM_AUTO_MASK		0xC0
#define ADT7470_REG_PWM34_CFG			0x69
#define		ADT7470_PWM3_AUTO_MASK		0x40
#define		ADT7470_PWM4_AUTO_MASK		0x80
#define	ADT7470_REG_PWM_MIN_BASE_ADDR		0x6A
#define ADT7470_REG_PWM_MIN_MAX_ADDR		0x6D
#define ADT7470_REG_PWM_TEMP_MIN_BASE_ADDR	0x6E
#define ADT7470_REG_PWM_TEMP_MIN_MAX_ADDR	0x71
#define ADT7470_REG_CFG_2			0x74
#define ADT7470_REG_ACOUSTICS12			0x75
#define ADT7470_REG_ACOUSTICS34			0x76
#define ADT7470_REG_DEVICE			0x3D
#define ADT7470_REG_VENDOR			0x3E
#define ADT7470_REG_REVISION			0x3F
#define ADT7470_REG_ALARM1_MASK			0x72
#define ADT7470_REG_ALARM2_MASK			0x73
#define ADT7470_REG_PWM_AUTO_TEMP_BASE_ADDR	0x7C
#define ADT7470_REG_PWM_AUTO_TEMP_MAX_ADDR	0x7D
#define ADT7470_REG_MAX_ADDR			0x81

#define ADT7470_TEMP_COUNT	10
#define ADT7470_TEMP_REG(x)	(ADT7470_REG_TEMP_BASE_ADDR + (x))
#define ADT7470_TEMP_MIN_REG(x) (ADT7470_REG_TEMP_LIMITS_BASE_ADDR + ((x) * 2))
#define ADT7470_TEMP_MAX_REG(x) (ADT7470_REG_TEMP_LIMITS_BASE_ADDR + \
				((x) * 2) + 1)

#define ADT7470_FAN_COUNT	4
#define ADT7470_REG_FAN(x)	(ADT7470_REG_FAN_BASE_ADDR + ((x) * 2))
#define ADT7470_REG_FAN_MIN(x)	(ADT7470_REG_FAN_MIN_BASE_ADDR + ((x) * 2))
#define ADT7470_REG_FAN_MAX(x)	(ADT7470_REG_FAN_MAX_BASE_ADDR + ((x) * 2))

#define ADT7470_PWM_COUNT	4
#define ADT7470_REG_PWM(x)	(ADT7470_REG_PWM_BASE_ADDR + (x))
#define ADT7470_REG_PWM_MAX(x)	(ADT7470_REG_PWM_MAX_BASE_ADDR + (x))
#define ADT7470_REG_PWM_MIN(x)	(ADT7470_REG_PWM_MIN_BASE_ADDR + (x))
#define ADT7470_REG_PWM_TMIN(x)	(ADT7470_REG_PWM_TEMP_MIN_BASE_ADDR + (x))
#define ADT7470_REG_PWM_CFG(x)	(ADT7470_REG_PWM_CFG_BASE_ADDR + ((x) / 2))
#define ADT7470_REG_PWM_AUTO_TEMP(x)	(ADT7470_REG_PWM_AUTO_TEMP_BASE_ADDR + \
					((x) / 2))

#define ALARM2(x)		((x) << 8)

#define ADT7470_VENDOR		0x41
#define ADT7470_DEVICE		0x70
/* datasheet only mentions a revision 2 */
#define ADT7470_REVISION	0x02

/* "all temps" according to hwmon sysfs interface spec */
#define ADT7470_PWM_ALL_TEMPS	0x3FF

/* How often do we reread sensors values? (In jiffies) */
#define SENSOR_REFRESH_INTERVAL	(5 * HZ)

/* How often do we reread sensor limit values? (In jiffies) */
#define LIMIT_REFRESH_INTERVAL	(60 * HZ)

/* Wait at least 200ms per sensor for 10 sensors */
#define TEMP_COLLECTION_TIME	2000

/* auto update thing won't fire more than every 2s */
#define AUTO_UPDATE_INTERVAL	2000

/* datasheet says to divide this number by the fan reading to get fan rpm */
#define FAN_PERIOD_TO_RPM(x)	((90000 * 60) / (x))
#define FAN_RPM_TO_PERIOD	FAN_PERIOD_TO_RPM
#define FAN_PERIOD_INVALID	65535
#define FAN_DATA_VALID(x)	((x) && (x) != FAN_PERIOD_INVALID)

/* Config registers 1 and 2 include fields for selecting the PWM frequency */
#define ADT7470_CFG_LF		0x40
#define ADT7470_FREQ_MASK	0x70
#define ADT7470_FREQ_SHIFT	4

struct adt7470_data {
	struct regmap		*regmap;
	struct mutex		lock;
	char			sensors_valid;
	char			limits_valid;
	unsigned long		sensors_last_updated;	/* In jiffies */
	unsigned long		limits_last_updated;	/* In jiffies */

	int			num_temp_sensors;	/* -1 = probe */
	int			temperatures_probed;

	s8			temp[ADT7470_TEMP_COUNT];
	s8			temp_min[ADT7470_TEMP_COUNT];
	s8			temp_max[ADT7470_TEMP_COUNT];
	u16			fan[ADT7470_FAN_COUNT];
	u16			fan_min[ADT7470_FAN_COUNT];
	u16			fan_max[ADT7470_FAN_COUNT];
	u16			alarm;
	u16			alarms_mask;
	u8			force_pwm_max;
	u8			pwm[ADT7470_PWM_COUNT];
	u8			pwm_max[ADT7470_PWM_COUNT];
	u8			pwm_automatic[ADT7470_PWM_COUNT];
	u8			pwm_min[ADT7470_PWM_COUNT];
	s8			pwm_tmin[ADT7470_PWM_COUNT];
	u8			pwm_auto_temp[ADT7470_PWM_COUNT];

	struct task_struct	*auto_update;
	unsigned int		auto_update_interval;
};

/*
 * 16-bit registers on the ADT7470 are low-byte first.  The data sheet says
 * that the low byte must be read before the high byte.
 */
static inline int adt7470_read_word_data(struct adt7470_data *data, unsigned int reg,
					 unsigned int *val)
{
	u8 regval[2];
	int err;

	err = regmap_bulk_read(data->regmap, reg, &regval, 2);
	if (err < 0)
		return err;

	*val = regval[0] | (regval[1] << 8);

	return 0;
}

static inline int adt7470_write_word_data(struct adt7470_data *data, unsigned int reg,
					  unsigned int val)
{
	u8 regval[2];

	regval[0] = val & 0xFF;
	regval[1] = val >> 8;

	return regmap_bulk_write(data->regmap, reg, &regval, 2);
}

/* Probe for temperature sensors.  Assumes lock is held */
static int adt7470_read_temperatures(struct adt7470_data *data)
{
	unsigned long res;
	unsigned int pwm_cfg[2];
	int err;
	int i;
	u8 pwm[ADT7470_FAN_COUNT];

	/* save pwm[1-4] config register */
	err = regmap_read(data->regmap, ADT7470_REG_PWM_CFG(0), &pwm_cfg[0]);
	if (err < 0)
		return err;
	err = regmap_read(data->regmap, ADT7470_REG_PWM_CFG(2), &pwm_cfg[1]);
	if (err < 0)
		return err;

	/* set manual pwm to whatever it is set to now */
	err = regmap_bulk_read(data->regmap, ADT7470_REG_PWM(0), &pwm[0],
			       ADT7470_PWM_COUNT);
	if (err < 0)
		return err;

	/* put pwm in manual mode */
	err = regmap_update_bits(data->regmap, ADT7470_REG_PWM_CFG(0),
				 ADT7470_PWM_AUTO_MASK, 0);
	if (err < 0)
		return err;
	err = regmap_update_bits(data->regmap, ADT7470_REG_PWM_CFG(2),
				 ADT7470_PWM_AUTO_MASK, 0);
	if (err < 0)
		return err;

	/* write pwm control to whatever it was */
	err = regmap_bulk_write(data->regmap, ADT7470_REG_PWM(0), &pwm[0],
				ADT7470_PWM_COUNT);
	if (err < 0)
		return err;

	/* start reading temperature sensors */
	err = regmap_update_bits(data->regmap, ADT7470_REG_CFG,
				 ADT7470_T05_STB_MASK, ADT7470_T05_STB_MASK);
	if (err < 0)
		return err;

	/* Delay is 200ms * number of temp sensors. */
	res = msleep_interruptible((data->num_temp_sensors >= 0 ?
				    data->num_temp_sensors * 200 :
				    TEMP_COLLECTION_TIME));

	/* done reading temperature sensors */
	err = regmap_update_bits(data->regmap, ADT7470_REG_CFG,
				 ADT7470_T05_STB_MASK, 0);
	if (err < 0)
		return err;

	/* restore pwm[1-4] config registers */
	err = regmap_write(data->regmap, ADT7470_REG_PWM_CFG(0), pwm_cfg[0]);
	if (err < 0)
		return err;
	err = regmap_write(data->regmap, ADT7470_REG_PWM_CFG(2), pwm_cfg[1]);
	if (err < 0)
		return err;

	if (res)
		return -EAGAIN;

	/* Only count fans if we have to */
	if (data->num_temp_sensors >= 0)
		return 0;

	err = regmap_bulk_read(data->regmap, ADT7470_TEMP_REG(0), &data->temp[0],
			       ADT7470_TEMP_COUNT);
	if (err < 0)
		return err;
	for (i = 0; i < ADT7470_TEMP_COUNT; i++) {
		if (data->temp[i])
			data->num_temp_sensors = i + 1;
	}
	data->temperatures_probed = 1;
	return 0;
}

static int adt7470_update_thread(void *p)
{
	struct i2c_client *client = p;
	struct adt7470_data *data = i2c_get_clientdata(client);

	while (!kthread_should_stop()) {
		mutex_lock(&data->lock);
		adt7470_read_temperatures(data);
		mutex_unlock(&data->lock);

		if (kthread_should_stop())
			break;

		schedule_timeout_interruptible(msecs_to_jiffies(data->auto_update_interval));
	}

	return 0;
}

static int adt7470_update_sensors(struct adt7470_data *data)
{
	unsigned int val;
	int err;
	int i;

	if (!data->temperatures_probed)
		err = adt7470_read_temperatures(data);
	else
		err = regmap_bulk_read(data->regmap, ADT7470_TEMP_REG(0), &data->temp[0],
				       ADT7470_TEMP_COUNT);
	if (err < 0)
		return err;

	for (i = 0; i < ADT7470_FAN_COUNT; i++) {
		err = adt7470_read_word_data(data, ADT7470_REG_FAN(i), &val);
		if (err < 0)
			return err;
		data->fan[i] =	val;
	}

	err = regmap_bulk_read(data->regmap, ADT7470_REG_PWM(0), &data->pwm[0], ADT7470_PWM_COUNT);
	if (err < 0)
		return err;

	for (i = 0; i < ADT7470_PWM_COUNT; i++) {
		unsigned int mask;

		if (i % 2)
			mask = ADT7470_PWM2_AUTO_MASK;
		else
			mask = ADT7470_PWM1_AUTO_MASK;

		err = regmap_read(data->regmap, ADT7470_REG_PWM_CFG(i), &val);
		if (err < 0)
			return err;
		data->pwm_automatic[i] = !!(val & mask);

		err = regmap_read(data->regmap, ADT7470_REG_PWM_AUTO_TEMP(i), &val);
		if (err < 0)
			return err;
		if (!(i % 2))
			data->pwm_auto_temp[i] = val >> 4;
		else
			data->pwm_auto_temp[i] = val & 0xF;
	}

	err = regmap_read(data->regmap, ADT7470_REG_CFG, &val);
	if (err < 0)
		return err;
	data->force_pwm_max = !!(val & ADT7470_FSPD_MASK);

	err = regmap_read(data->regmap, ADT7470_REG_ALARM1, &val);
	if (err < 0)
		return err;
	data->alarm = val;
	if (data->alarm & ADT7470_OOL_ALARM) {
		err = regmap_read(data->regmap, ADT7470_REG_ALARM2, &val);
		if (err < 0)
			return err;
		data->alarm |= ALARM2(val);
	}

	err = adt7470_read_word_data(data, ADT7470_REG_ALARM1_MASK, &val);
	if (err < 0)
		return err;
	data->alarms_mask = val;

	return 0;
}

static int adt7470_update_limits(struct adt7470_data *data)
{
	unsigned int val;
	int err;
	int i;

	for (i = 0; i < ADT7470_TEMP_COUNT; i++) {
		err = regmap_read(data->regmap, ADT7470_TEMP_MIN_REG(i), &val);
		if (err < 0)
			return err;
		data->temp_min[i] = (s8)val;
		err = regmap_read(data->regmap, ADT7470_TEMP_MAX_REG(i), &val);
		if (err < 0)
			return err;
		data->temp_max[i] = (s8)val;
	}

	for (i = 0; i < ADT7470_FAN_COUNT; i++) {
		err = adt7470_read_word_data(data, ADT7470_REG_FAN_MIN(i), &val);
		if (err < 0)
			return err;
		data->fan_min[i] = val;
		err = adt7470_read_word_data(data, ADT7470_REG_FAN_MAX(i), &val);
		if (err < 0)
			return err;
		data->fan_max[i] = val;
	}

	for (i = 0; i < ADT7470_PWM_COUNT; i++) {
		err = regmap_read(data->regmap, ADT7470_REG_PWM_MAX(i), &val);
		if (err < 0)
			return err;
		data->pwm_max[i] = val;
		err = regmap_read(data->regmap, ADT7470_REG_PWM_MIN(i), &val);
		if (err < 0)
			return err;
		data->pwm_min[i] = val;
		err = regmap_read(data->regmap, ADT7470_REG_PWM_TMIN(i), &val);
		if (err < 0)
			return err;
		data->pwm_tmin[i] = (s8)val;
	}

	return 0;
}

static struct adt7470_data *adt7470_update_device(struct device *dev)
{
	struct adt7470_data *data = dev_get_drvdata(dev);
	unsigned long local_jiffies = jiffies;
	int need_sensors = 1;
	int need_limits = 1;
	int err;

	/*
	 * Figure out if we need to update the shadow registers.
	 * Lockless means that we may occasionally report out of
	 * date data.
	 */
	if (time_before(local_jiffies, data->sensors_last_updated +
			SENSOR_REFRESH_INTERVAL) &&
	    data->sensors_valid)
		need_sensors = 0;

	if (time_before(local_jiffies, data->limits_last_updated +
			LIMIT_REFRESH_INTERVAL) &&
	    data->limits_valid)
		need_limits = 0;

	if (!need_sensors && !need_limits)
		return data;

	mutex_lock(&data->lock);
	if (need_sensors) {
		err = adt7470_update_sensors(data);
		if (err < 0)
			goto out;
		data->sensors_last_updated = local_jiffies;
		data->sensors_valid = 1;
	}

	if (need_limits) {
		err = adt7470_update_limits(data);
		if (err < 0)
			goto out;
		data->limits_last_updated = local_jiffies;
		data->limits_valid = 1;
	}
out:
	mutex_unlock(&data->lock);

	return err < 0 ? ERR_PTR(err) : data;
}

static ssize_t auto_update_interval_show(struct device *dev,
					 struct device_attribute *devattr,
					 char *buf)
{
	struct adt7470_data *data = adt7470_update_device(dev);

	if (IS_ERR(data))
		return PTR_ERR(data);

	return sprintf(buf, "%d\n", data->auto_update_interval);
}

static ssize_t auto_update_interval_store(struct device *dev,
					  struct device_attribute *devattr,
					  const char *buf, size_t count)
{
	struct adt7470_data *data = dev_get_drvdata(dev);
	long temp;

	if (kstrtol(buf, 10, &temp))
		return -EINVAL;

	temp = clamp_val(temp, 0, 60000);

	mutex_lock(&data->lock);
	data->auto_update_interval = temp;
	mutex_unlock(&data->lock);

	return count;
}

static ssize_t num_temp_sensors_show(struct device *dev,
				     struct device_attribute *devattr,
				     char *buf)
{
	struct adt7470_data *data = adt7470_update_device(dev);

	if (IS_ERR(data))
		return PTR_ERR(data);

	return sprintf(buf, "%d\n", data->num_temp_sensors);
}

static ssize_t num_temp_sensors_store(struct device *dev,
				      struct device_attribute *devattr,
				      const char *buf, size_t count)
{
	struct adt7470_data *data = dev_get_drvdata(dev);
	long temp;

	if (kstrtol(buf, 10, &temp))
		return -EINVAL;

	temp = clamp_val(temp, -1, 10);

	mutex_lock(&data->lock);
	data->num_temp_sensors = temp;
	if (temp < 0)
		data->temperatures_probed = 0;
	mutex_unlock(&data->lock);

	return count;
}

static int adt7470_temp_read(struct device *dev, u32 attr, int channel, long *val)
{
	struct adt7470_data *data = adt7470_update_device(dev);

	if (IS_ERR(data))
		return PTR_ERR(data);

	switch (attr) {
	case hwmon_temp_input:
		*val = 1000 * data->temp[channel];
		break;
	case hwmon_temp_min:
		*val = 1000 * data->temp_min[channel];
		break;
	case hwmon_temp_max:
		*val = 1000 * data->temp_max[channel];
		break;
	case hwmon_temp_alarm:
		*val = !!(data->alarm & channel);
		break;
	default:
		return -EOPNOTSUPP;
	}

	return 0;
}

static int adt7470_temp_write(struct device *dev, u32 attr, int channel, long val)
{
	struct adt7470_data *data = dev_get_drvdata(dev);
	int err;

	val = clamp_val(val, -128000, 127000);
	val = DIV_ROUND_CLOSEST(val, 1000);

	switch (attr) {
	case hwmon_temp_min:
		mutex_lock(&data->lock);
		data->temp_min[channel] = val;
		err = regmap_write(data->regmap, ADT7470_TEMP_MIN_REG(channel), val);
		mutex_unlock(&data->lock);
		break;
	case hwmon_temp_max:
		mutex_lock(&data->lock);
		data->temp_max[channel] = val;
		err = regmap_write(data->regmap, ADT7470_TEMP_MAX_REG(channel), val);
		mutex_unlock(&data->lock);
		break;
	default:
		return -EOPNOTSUPP;
	}

	return err;
}

static ssize_t alarm_mask_show(struct device *dev,
			       struct device_attribute *devattr, char *buf)
{
	struct adt7470_data *data = adt7470_update_device(dev);

	if (IS_ERR(data))
		return PTR_ERR(data);

	return sprintf(buf, "%x\n", data->alarms_mask);
}

static ssize_t alarm_mask_store(struct device *dev,
				struct device_attribute *devattr,
				const char *buf, size_t count)
{
	struct adt7470_data *data = dev_get_drvdata(dev);
	long mask;
	int err;

	if (kstrtoul(buf, 0, &mask))
		return -EINVAL;

	if (mask & ~0xffff)
		return -EINVAL;

	mutex_lock(&data->lock);
	data->alarms_mask = mask;
	err = adt7470_write_word_data(data, ADT7470_REG_ALARM1_MASK, mask);
	mutex_unlock(&data->lock);

	return err < 0 ? err : count;
}

static int adt7470_fan_read(struct device *dev, u32 attr, int channel, long *val)
{
	struct adt7470_data *data = adt7470_update_device(dev);

	if (IS_ERR(data))
		return PTR_ERR(data);

	switch (attr) {
	case hwmon_fan_input:
		if (FAN_DATA_VALID(data->fan[channel]))
			*val = FAN_PERIOD_TO_RPM(data->fan[channel]);
		else
			*val = 0;
		break;
	case hwmon_fan_min:
		if (FAN_DATA_VALID(data->fan_min[channel]))
			*val = FAN_PERIOD_TO_RPM(data->fan_min[channel]);
		else
			*val = 0;
		break;
	case hwmon_fan_max:
		if (FAN_DATA_VALID(data->fan_max[channel]))
			*val = FAN_PERIOD_TO_RPM(data->fan_max[channel]);
		else
			*val = 0;
		break;
	case hwmon_fan_alarm:
		*val = !!(data->alarm & (1 << (12 + channel)));
		break;
	default:
		return -EOPNOTSUPP;
	}

	return 0;
}

static int adt7470_fan_write(struct device *dev, u32 attr, int channel, long val)
{
	struct adt7470_data *data = dev_get_drvdata(dev);
	int err;

	if (val <= 0)
		return -EINVAL;

	val = FAN_RPM_TO_PERIOD(val);
	val = clamp_val(val, 1, 65534);

	switch (attr) {
	case hwmon_fan_min:
		mutex_lock(&data->lock);
		data->fan_min[channel] = val;
		err = adt7470_write_word_data(data, ADT7470_REG_FAN_MIN(channel), val);
		mutex_unlock(&data->lock);
		break;
	case hwmon_fan_max:
		mutex_lock(&data->lock);
		data->fan_max[channel] = val;
		err = adt7470_write_word_data(data, ADT7470_REG_FAN_MAX(channel), val);
		mutex_unlock(&data->lock);
		break;
	default:
		return -EOPNOTSUPP;
	}

	return err;
}

static ssize_t force_pwm_max_show(struct device *dev,
				  struct device_attribute *devattr, char *buf)
{
	struct adt7470_data *data = adt7470_update_device(dev);

	if (IS_ERR(data))
		return PTR_ERR(data);

	return sprintf(buf, "%d\n", data->force_pwm_max);
}

static ssize_t force_pwm_max_store(struct device *dev,
				   struct device_attribute *devattr,
				   const char *buf, size_t count)
{
	struct adt7470_data *data = dev_get_drvdata(dev);
	long temp;
	int err;

	if (kstrtol(buf, 10, &temp))
		return -EINVAL;

	mutex_lock(&data->lock);
	data->force_pwm_max = temp;
	err = regmap_update_bits(data->regmap, ADT7470_REG_CFG,
				 ADT7470_FSPD_MASK,
				 temp ? ADT7470_FSPD_MASK : 0);
	mutex_unlock(&data->lock);

	return err < 0 ? err : count;
}

/* These are the valid PWM frequencies to the nearest Hz */
static const int adt7470_freq_map[] = {
	11, 15, 22, 29, 35, 44, 59, 88, 1400, 22500
};

static int pwm1_freq_get(struct device *dev)
{
	struct adt7470_data *data = dev_get_drvdata(dev);
	unsigned int cfg_reg_1, cfg_reg_2;
	int index;
	int err;

	mutex_lock(&data->lock);
	err = regmap_read(data->regmap, ADT7470_REG_CFG, &cfg_reg_1);
	if (err < 0)
		goto out;
	err = regmap_read(data->regmap, ADT7470_REG_CFG_2, &cfg_reg_2);
	if (err < 0)
		goto out;
	mutex_unlock(&data->lock);

	index = (cfg_reg_2 & ADT7470_FREQ_MASK) >> ADT7470_FREQ_SHIFT;
	if (!(cfg_reg_1 & ADT7470_CFG_LF))
		index += 8;
	if (index >= ARRAY_SIZE(adt7470_freq_map))
		index = ARRAY_SIZE(adt7470_freq_map) - 1;

	return adt7470_freq_map[index];

out:
	mutex_unlock(&data->lock);
	return err;
}

static int adt7470_pwm_read(struct device *dev, u32 attr, int channel, long *val)
{
	struct adt7470_data *data = adt7470_update_device(dev);

	if (IS_ERR(data))
		return PTR_ERR(data);

	switch (attr) {
	case hwmon_pwm_input:
		*val = data->pwm[channel];
		break;
	case hwmon_pwm_enable:
		*val = 1 + data->pwm_automatic[channel];
		break;
	case hwmon_pwm_freq:
		*val = pwm1_freq_get(dev);
		break;
	default:
		return -EOPNOTSUPP;
	}

	return 0;
}

static int pwm1_freq_set(struct device *dev, long freq)
{
	struct adt7470_data *data = dev_get_drvdata(dev);
	unsigned int low_freq = ADT7470_CFG_LF;
	int index;
	int err;

	/* Round the user value given to the closest available frequency */
	index = find_closest(freq, adt7470_freq_map,
			     ARRAY_SIZE(adt7470_freq_map));

	if (index >= 8) {
		index -= 8;
		low_freq = 0;
	}

	mutex_lock(&data->lock);
	/* Configuration Register 1 */
	err = regmap_update_bits(data->regmap, ADT7470_REG_CFG,
				 ADT7470_CFG_LF, low_freq);
	if (err < 0)
		goto out;

	/* Configuration Register 2 */
	err = regmap_update_bits(data->regmap, ADT7470_REG_CFG_2,
				 ADT7470_FREQ_MASK,
				 index << ADT7470_FREQ_SHIFT);
out:
	mutex_unlock(&data->lock);

	return err;
}

static int adt7470_pwm_write(struct device *dev, u32 attr, int channel, long val)
{
	struct adt7470_data *data = dev_get_drvdata(dev);
	unsigned int pwm_auto_reg_mask;
	int err;

	switch (attr) {
	case hwmon_pwm_input:
		val = clamp_val(val, 0, 255);
		mutex_lock(&data->lock);
		data->pwm[channel] = val;
		err = regmap_write(data->regmap, ADT7470_REG_PWM(channel),
				   data->pwm[channel]);
		mutex_unlock(&data->lock);
		break;
	case hwmon_pwm_enable:
		if (channel % 2)
			pwm_auto_reg_mask = ADT7470_PWM2_AUTO_MASK;
		else
			pwm_auto_reg_mask = ADT7470_PWM1_AUTO_MASK;

		if (val != 2 && val != 1)
			return -EINVAL;
		val--;

		mutex_lock(&data->lock);
		data->pwm_automatic[channel] = val;
		err = regmap_update_bits(data->regmap, ADT7470_REG_PWM_CFG(channel),
					 pwm_auto_reg_mask,
					 val ? pwm_auto_reg_mask : 0);
		mutex_unlock(&data->lock);
		break;
	case hwmon_pwm_freq:
		err = pwm1_freq_set(dev, val);
		break;
	default:
		return -EOPNOTSUPP;
	}

	return err;
}

static ssize_t pwm_max_show(struct device *dev,
			    struct device_attribute *devattr, char *buf)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	struct adt7470_data *data = adt7470_update_device(dev);

	if (IS_ERR(data))
		return PTR_ERR(data);

	return sprintf(buf, "%d\n", data->pwm_max[attr->index]);
}

static ssize_t pwm_max_store(struct device *dev,
			     struct device_attribute *devattr,
			     const char *buf, size_t count)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	struct adt7470_data *data = dev_get_drvdata(dev);
	long temp;
	int err;

	if (kstrtol(buf, 10, &temp))
		return -EINVAL;

	temp = clamp_val(temp, 0, 255);

	mutex_lock(&data->lock);
	data->pwm_max[attr->index] = temp;
	err = regmap_write(data->regmap, ADT7470_REG_PWM_MAX(attr->index),
			   temp);
	mutex_unlock(&data->lock);

	return err < 0 ? err : count;
}

static ssize_t pwm_min_show(struct device *dev,
			    struct device_attribute *devattr, char *buf)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	struct adt7470_data *data = adt7470_update_device(dev);

	if (IS_ERR(data))
		return PTR_ERR(data);

	return sprintf(buf, "%d\n", data->pwm_min[attr->index]);
}

static ssize_t pwm_min_store(struct device *dev,
			     struct device_attribute *devattr,
			     const char *buf, size_t count)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	struct adt7470_data *data = dev_get_drvdata(dev);
	long temp;
	int err;

	if (kstrtol(buf, 10, &temp))
		return -EINVAL;

	temp = clamp_val(temp, 0, 255);

	mutex_lock(&data->lock);
	data->pwm_min[attr->index] = temp;
	err = regmap_write(data->regmap, ADT7470_REG_PWM_MIN(attr->index),
			   temp);
	mutex_unlock(&data->lock);

	return err < 0 ? err : count;
}

static ssize_t pwm_tmax_show(struct device *dev,
			     struct device_attribute *devattr, char *buf)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	struct adt7470_data *data = adt7470_update_device(dev);

	if (IS_ERR(data))
		return PTR_ERR(data);

	/* the datasheet says that tmax = tmin + 20C */
	return sprintf(buf, "%d\n", 1000 * (20 + data->pwm_tmin[attr->index]));
}

static ssize_t pwm_tmin_show(struct device *dev,
			     struct device_attribute *devattr, char *buf)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	struct adt7470_data *data = adt7470_update_device(dev);

	if (IS_ERR(data))
		return PTR_ERR(data);

	return sprintf(buf, "%d\n", 1000 * data->pwm_tmin[attr->index]);
}

static ssize_t pwm_tmin_store(struct device *dev,
			      struct device_attribute *devattr,
			      const char *buf, size_t count)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	struct adt7470_data *data = dev_get_drvdata(dev);
	long temp;
	int err;

	if (kstrtol(buf, 10, &temp))
		return -EINVAL;

	temp = clamp_val(temp, -128000, 127000);
	temp = DIV_ROUND_CLOSEST(temp, 1000);

	mutex_lock(&data->lock);
	data->pwm_tmin[attr->index] = temp;
	err = regmap_write(data->regmap, ADT7470_REG_PWM_TMIN(attr->index),
			   temp);
	mutex_unlock(&data->lock);

	return err < 0 ? err : count;
}

static ssize_t pwm_auto_temp_show(struct device *dev,
				  struct device_attribute *devattr, char *buf)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	struct adt7470_data *data = adt7470_update_device(dev);
	u8 ctrl;

	if (IS_ERR(data))
		return PTR_ERR(data);

	ctrl = data->pwm_auto_temp[attr->index];
	if (ctrl)
		return sprintf(buf, "%d\n", 1 << (ctrl - 1));
	else
		return sprintf(buf, "%d\n", ADT7470_PWM_ALL_TEMPS);
}

static int cvt_auto_temp(int input)
{
	if (input == ADT7470_PWM_ALL_TEMPS)
		return 0;
	if (input < 1 || !is_power_of_2(input))
		return -EINVAL;
	return ilog2(input) + 1;
}

static ssize_t pwm_auto_temp_store(struct device *dev,
				   struct device_attribute *devattr,
				   const char *buf, size_t count)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	struct adt7470_data *data = dev_get_drvdata(dev);
	int pwm_auto_reg = ADT7470_REG_PWM_AUTO_TEMP(attr->index);
	unsigned int mask, val;
	long temp;
	int err;

	if (kstrtol(buf, 10, &temp))
		return -EINVAL;

	temp = cvt_auto_temp(temp);
	if (temp < 0)
		return temp;

	mutex_lock(&data->lock);
	data->pwm_automatic[attr->index] = temp;

	if (!(attr->index % 2)) {
		mask = 0xF0;
		val = (temp << 4) & 0xF0;
	} else {
		mask = 0x0F;
		val = temp & 0x0F;
	}

	err = regmap_update_bits(data->regmap, pwm_auto_reg, mask, val);
	mutex_unlock(&data->lock);

	return err < 0 ? err : count;
}

static DEVICE_ATTR_RW(alarm_mask);
static DEVICE_ATTR_RW(num_temp_sensors);
static DEVICE_ATTR_RW(auto_update_interval);

static SENSOR_DEVICE_ATTR_RW(force_pwm_max, force_pwm_max, 0);

static SENSOR_DEVICE_ATTR_RW(pwm1_auto_point1_pwm, pwm_min, 0);
static SENSOR_DEVICE_ATTR_RW(pwm2_auto_point1_pwm, pwm_min, 1);
static SENSOR_DEVICE_ATTR_RW(pwm3_auto_point1_pwm, pwm_min, 2);
static SENSOR_DEVICE_ATTR_RW(pwm4_auto_point1_pwm, pwm_min, 3);

static SENSOR_DEVICE_ATTR_RW(pwm1_auto_point2_pwm, pwm_max, 0);
static SENSOR_DEVICE_ATTR_RW(pwm2_auto_point2_pwm, pwm_max, 1);
static SENSOR_DEVICE_ATTR_RW(pwm3_auto_point2_pwm, pwm_max, 2);
static SENSOR_DEVICE_ATTR_RW(pwm4_auto_point2_pwm, pwm_max, 3);

static SENSOR_DEVICE_ATTR_RW(pwm1_auto_point1_temp, pwm_tmin, 0);
static SENSOR_DEVICE_ATTR_RW(pwm2_auto_point1_temp, pwm_tmin, 1);
static SENSOR_DEVICE_ATTR_RW(pwm3_auto_point1_temp, pwm_tmin, 2);
static SENSOR_DEVICE_ATTR_RW(pwm4_auto_point1_temp, pwm_tmin, 3);

static SENSOR_DEVICE_ATTR_RO(pwm1_auto_point2_temp, pwm_tmax, 0);
static SENSOR_DEVICE_ATTR_RO(pwm2_auto_point2_temp, pwm_tmax, 1);
static SENSOR_DEVICE_ATTR_RO(pwm3_auto_point2_temp, pwm_tmax, 2);
static SENSOR_DEVICE_ATTR_RO(pwm4_auto_point2_temp, pwm_tmax, 3);

static SENSOR_DEVICE_ATTR_RW(pwm1_auto_channels_temp, pwm_auto_temp, 0);
static SENSOR_DEVICE_ATTR_RW(pwm2_auto_channels_temp, pwm_auto_temp, 1);
static SENSOR_DEVICE_ATTR_RW(pwm3_auto_channels_temp, pwm_auto_temp, 2);
static SENSOR_DEVICE_ATTR_RW(pwm4_auto_channels_temp, pwm_auto_temp, 3);

static struct attribute *adt7470_attrs[] = {
	&dev_attr_alarm_mask.attr,
	&dev_attr_num_temp_sensors.attr,
	&dev_attr_auto_update_interval.attr,
	&sensor_dev_attr_force_pwm_max.dev_attr.attr,
	&sensor_dev_attr_pwm1_auto_point1_pwm.dev_attr.attr,
	&sensor_dev_attr_pwm2_auto_point1_pwm.dev_attr.attr,
	&sensor_dev_attr_pwm3_auto_point1_pwm.dev_attr.attr,
	&sensor_dev_attr_pwm4_auto_point1_pwm.dev_attr.attr,
	&sensor_dev_attr_pwm1_auto_point2_pwm.dev_attr.attr,
	&sensor_dev_attr_pwm2_auto_point2_pwm.dev_attr.attr,
	&sensor_dev_attr_pwm3_auto_point2_pwm.dev_attr.attr,
	&sensor_dev_attr_pwm4_auto_point2_pwm.dev_attr.attr,
	&sensor_dev_attr_pwm1_auto_point1_temp.dev_attr.attr,
	&sensor_dev_attr_pwm2_auto_point1_temp.dev_attr.attr,
	&sensor_dev_attr_pwm3_auto_point1_temp.dev_attr.attr,
	&sensor_dev_attr_pwm4_auto_point1_temp.dev_attr.attr,
	&sensor_dev_attr_pwm1_auto_point2_temp.dev_attr.attr,
	&sensor_dev_attr_pwm2_auto_point2_temp.dev_attr.attr,
	&sensor_dev_attr_pwm3_auto_point2_temp.dev_attr.attr,
	&sensor_dev_attr_pwm4_auto_point2_temp.dev_attr.attr,
	&sensor_dev_attr_pwm1_auto_channels_temp.dev_attr.attr,
	&sensor_dev_attr_pwm2_auto_channels_temp.dev_attr.attr,
	&sensor_dev_attr_pwm3_auto_channels_temp.dev_attr.attr,
	&sensor_dev_attr_pwm4_auto_channels_temp.dev_attr.attr,
	NULL
};

ATTRIBUTE_GROUPS(adt7470);

static int adt7470_read(struct device *dev, enum hwmon_sensor_types type, u32 attr,
			int channel, long *val)
{
	switch (type) {
	case hwmon_temp:
		return adt7470_temp_read(dev, attr, channel, val);
	case hwmon_fan:
		return adt7470_fan_read(dev, attr, channel, val);
	case hwmon_pwm:
		return adt7470_pwm_read(dev, attr, channel, val);
	default:
		return -EOPNOTSUPP;
	}
}

static int adt7470_write(struct device *dev, enum hwmon_sensor_types type, u32 attr,
			 int channel, long val)
{
	switch (type) {
	case hwmon_temp:
		return adt7470_temp_write(dev, attr, channel, val);
	case hwmon_fan:
		return adt7470_fan_write(dev, attr, channel, val);
	case hwmon_pwm:
		return adt7470_pwm_write(dev, attr, channel, val);
	default:
		return -EOPNOTSUPP;
	}
}

static umode_t adt7470_is_visible(const void *_data, enum hwmon_sensor_types type,
				  u32 attr, int channel)
{
	umode_t mode = 0;

	switch (type) {
	case hwmon_temp:
		switch (attr) {
		case hwmon_temp:
		case hwmon_temp_alarm:
			mode = 0444;
			break;
		case hwmon_temp_min:
		case hwmon_temp_max:
			mode = 0644;
			break;
		default:
			break;
		}
		break;
	case hwmon_fan:
		switch (attr) {
		case hwmon_fan_input:
		case hwmon_fan_alarm:
			mode = 0444;
			break;
		case hwmon_fan_min:
		case hwmon_fan_max:
			mode = 0644;
			break;
		default:
			break;
		}
		break;
	case hwmon_pwm:
		switch (attr) {
		case hwmon_pwm_input:
		case hwmon_pwm_enable:
			mode = 0644;
			break;
		case hwmon_pwm_freq:
			if (channel == 0)
				mode = 0644;
			else
				mode = 0;
			break;
		default:
			break;
		}
		break;
	default:
		break;
	}

	return mode;
}

static const struct hwmon_ops adt7470_hwmon_ops = {
	.is_visible = adt7470_is_visible,
	.read = adt7470_read,
	.write = adt7470_write,
};

static const struct hwmon_channel_info * const adt7470_info[] = {
	HWMON_CHANNEL_INFO(temp,
			   HWMON_T_INPUT | HWMON_T_MIN | HWMON_T_MAX | HWMON_T_ALARM,
			   HWMON_T_INPUT | HWMON_T_MIN | HWMON_T_MAX | HWMON_T_ALARM,
			   HWMON_T_INPUT | HWMON_T_MIN | HWMON_T_MAX | HWMON_T_ALARM,
			   HWMON_T_INPUT | HWMON_T_MIN | HWMON_T_MAX | HWMON_T_ALARM,
			   HWMON_T_INPUT | HWMON_T_MIN | HWMON_T_MAX | HWMON_T_ALARM,
			   HWMON_T_INPUT | HWMON_T_MIN | HWMON_T_MAX | HWMON_T_ALARM,
			   HWMON_T_INPUT | HWMON_T_MIN | HWMON_T_MAX | HWMON_T_ALARM,
			   HWMON_T_INPUT | HWMON_T_MIN | HWMON_T_MAX | HWMON_T_ALARM,
			   HWMON_T_INPUT | HWMON_T_MIN | HWMON_T_MAX | HWMON_T_ALARM,
			   HWMON_T_INPUT | HWMON_T_MIN | HWMON_T_MAX | HWMON_T_ALARM),
	HWMON_CHANNEL_INFO(fan,
			   HWMON_F_INPUT | HWMON_F_MIN | HWMON_F_MAX | HWMON_F_DIV | HWMON_F_ALARM,
			   HWMON_F_INPUT | HWMON_F_MIN | HWMON_F_MAX | HWMON_F_DIV | HWMON_F_ALARM,
			   HWMON_F_INPUT | HWMON_F_MIN | HWMON_F_MAX | HWMON_F_DIV | HWMON_F_ALARM,
			   HWMON_F_INPUT | HWMON_F_MIN | HWMON_F_MAX | HWMON_F_DIV | HWMON_F_ALARM),
	HWMON_CHANNEL_INFO(pwm,
			   HWMON_PWM_INPUT | HWMON_PWM_ENABLE | HWMON_PWM_FREQ,
			   HWMON_PWM_INPUT | HWMON_PWM_ENABLE,
			   HWMON_PWM_INPUT | HWMON_PWM_ENABLE,
			   HWMON_PWM_INPUT | HWMON_PWM_ENABLE),
	NULL
};

static const struct hwmon_chip_info adt7470_chip_info = {
	.ops = &adt7470_hwmon_ops,
	.info = adt7470_info,
};

/* Return 0 if detection is successful, -ENODEV otherwise */
static int adt7470_detect(struct i2c_client *client,
			  struct i2c_board_info *info)
{
	struct i2c_adapter *adapter = client->adapter;
	int vendor, device, revision;

	if (!i2c_check_functionality(adapter, I2C_FUNC_SMBUS_BYTE_DATA))
		return -ENODEV;

	vendor = i2c_smbus_read_byte_data(client, ADT7470_REG_VENDOR);
	if (vendor != ADT7470_VENDOR)
		return -ENODEV;

	device = i2c_smbus_read_byte_data(client, ADT7470_REG_DEVICE);
	if (device != ADT7470_DEVICE)
		return -ENODEV;

	revision = i2c_smbus_read_byte_data(client, ADT7470_REG_REVISION);
	if (revision != ADT7470_REVISION)
		return -ENODEV;

	strscpy(info->type, "adt7470", I2C_NAME_SIZE);

	return 0;
}

static const struct regmap_config adt7470_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.use_single_read = true,
	.use_single_write = true,
};

static int adt7470_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct adt7470_data *data;
	struct device *hwmon_dev;
	int err;

	data = devm_kzalloc(dev, sizeof(struct adt7470_data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->num_temp_sensors = -1;
	data->auto_update_interval = AUTO_UPDATE_INTERVAL;
	data->regmap = devm_regmap_init_i2c(client, &adt7470_regmap_config);
	if (IS_ERR(data->regmap))
		return PTR_ERR(data->regmap);

	i2c_set_clientdata(client, data);
	mutex_init(&data->lock);

	dev_info(&client->dev, "%s chip found\n", client->name);

	/* Initialize the ADT7470 chip */
	err = regmap_update_bits(data->regmap, ADT7470_REG_CFG,
				 ADT7470_STRT_MASK | ADT7470_TEST_MASK,
				 ADT7470_STRT_MASK | ADT7470_TEST_MASK);
	if (err < 0)
		return err;

	/* Register sysfs hooks */
	hwmon_dev = devm_hwmon_device_register_with_info(dev, client->name, data,
							 &adt7470_chip_info,
							 adt7470_groups);

	if (IS_ERR(hwmon_dev))
		return PTR_ERR(hwmon_dev);

	data->auto_update = kthread_run(adt7470_update_thread, client, "%s",
					dev_name(hwmon_dev));
	if (IS_ERR(data->auto_update))
		return PTR_ERR(data->auto_update);

	return 0;
}

static void adt7470_remove(struct i2c_client *client)
{
	struct adt7470_data *data = i2c_get_clientdata(client);

	kthread_stop(data->auto_update);
}

static const struct i2c_device_id adt7470_id[] = {
	{ "adt7470", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, adt7470_id);

static struct i2c_driver adt7470_driver = {
	.class		= I2C_CLASS_HWMON,
	.driver = {
		.name	= "adt7470",
	},
	.probe		= adt7470_probe,
	.remove		= adt7470_remove,
	.id_table	= adt7470_id,
	.detect		= adt7470_detect,
	.address_list	= normal_i2c,
};

module_i2c_driver(adt7470_driver);

MODULE_AUTHOR("Darrick J. Wong <darrick.wong@oracle.com>");
MODULE_DESCRIPTION("ADT7470 driver");
MODULE_LICENSE("GPL");
