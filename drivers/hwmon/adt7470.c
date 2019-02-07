/*
 * A hwmon driver for the Analog Devices ADT7470
 * Copyright (C) 2007 IBM
 *
 * Author: Darrick J. Wong <darrick.wong@oracle.com>
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
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
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
#define		ADT7470_FSPD_MASK		0x04
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
	struct i2c_client	*client;
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
static inline int adt7470_read_word_data(struct i2c_client *client, u8 reg)
{
	u16 foo;
	foo = i2c_smbus_read_byte_data(client, reg);
	foo |= ((u16)i2c_smbus_read_byte_data(client, reg + 1) << 8);
	return foo;
}

static inline int adt7470_write_word_data(struct i2c_client *client, u8 reg,
					  u16 value)
{
	return i2c_smbus_write_byte_data(client, reg, value & 0xFF)
	       || i2c_smbus_write_byte_data(client, reg + 1, value >> 8);
}

/* Probe for temperature sensors.  Assumes lock is held */
static int adt7470_read_temperatures(struct i2c_client *client,
				     struct adt7470_data *data)
{
	unsigned long res;
	int i;
	u8 cfg, pwm[4], pwm_cfg[2];

	/* save pwm[1-4] config register */
	pwm_cfg[0] = i2c_smbus_read_byte_data(client, ADT7470_REG_PWM_CFG(0));
	pwm_cfg[1] = i2c_smbus_read_byte_data(client, ADT7470_REG_PWM_CFG(2));

	/* set manual pwm to whatever it is set to now */
	for (i = 0; i < ADT7470_FAN_COUNT; i++)
		pwm[i] = i2c_smbus_read_byte_data(client, ADT7470_REG_PWM(i));

	/* put pwm in manual mode */
	i2c_smbus_write_byte_data(client, ADT7470_REG_PWM_CFG(0),
		pwm_cfg[0] & ~(ADT7470_PWM_AUTO_MASK));
	i2c_smbus_write_byte_data(client, ADT7470_REG_PWM_CFG(2),
		pwm_cfg[1] & ~(ADT7470_PWM_AUTO_MASK));

	/* write pwm control to whatever it was */
	for (i = 0; i < ADT7470_FAN_COUNT; i++)
		i2c_smbus_write_byte_data(client, ADT7470_REG_PWM(i), pwm[i]);

	/* start reading temperature sensors */
	cfg = i2c_smbus_read_byte_data(client, ADT7470_REG_CFG);
	cfg |= 0x80;
	i2c_smbus_write_byte_data(client, ADT7470_REG_CFG, cfg);

	/* Delay is 200ms * number of temp sensors. */
	res = msleep_interruptible((data->num_temp_sensors >= 0 ?
				    data->num_temp_sensors * 200 :
				    TEMP_COLLECTION_TIME));

	/* done reading temperature sensors */
	cfg = i2c_smbus_read_byte_data(client, ADT7470_REG_CFG);
	cfg &= ~0x80;
	i2c_smbus_write_byte_data(client, ADT7470_REG_CFG, cfg);

	/* restore pwm[1-4] config registers */
	i2c_smbus_write_byte_data(client, ADT7470_REG_PWM_CFG(0), pwm_cfg[0]);
	i2c_smbus_write_byte_data(client, ADT7470_REG_PWM_CFG(2), pwm_cfg[1]);

	if (res) {
		pr_err("ha ha, interrupted\n");
		return -EAGAIN;
	}

	/* Only count fans if we have to */
	if (data->num_temp_sensors >= 0)
		return 0;

	for (i = 0; i < ADT7470_TEMP_COUNT; i++) {
		data->temp[i] = i2c_smbus_read_byte_data(client,
						ADT7470_TEMP_REG(i));
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
		adt7470_read_temperatures(client, data);
		mutex_unlock(&data->lock);

		set_current_state(TASK_INTERRUPTIBLE);
		if (kthread_should_stop())
			break;

		schedule_timeout(msecs_to_jiffies(data->auto_update_interval));
	}

	return 0;
}

static struct adt7470_data *adt7470_update_device(struct device *dev)
{
	struct adt7470_data *data = dev_get_drvdata(dev);
	struct i2c_client *client = data->client;
	unsigned long local_jiffies = jiffies;
	u8 cfg;
	int i;
	int need_sensors = 1;
	int need_limits = 1;

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
	if (!need_sensors)
		goto no_sensor_update;

	if (!data->temperatures_probed)
		adt7470_read_temperatures(client, data);
	else
		for (i = 0; i < ADT7470_TEMP_COUNT; i++)
			data->temp[i] = i2c_smbus_read_byte_data(client,
						ADT7470_TEMP_REG(i));

	for (i = 0; i < ADT7470_FAN_COUNT; i++)
		data->fan[i] = adt7470_read_word_data(client,
						ADT7470_REG_FAN(i));

	for (i = 0; i < ADT7470_PWM_COUNT; i++) {
		int reg;
		int reg_mask;

		data->pwm[i] = i2c_smbus_read_byte_data(client,
						ADT7470_REG_PWM(i));

		if (i % 2)
			reg_mask = ADT7470_PWM2_AUTO_MASK;
		else
			reg_mask = ADT7470_PWM1_AUTO_MASK;

		reg = ADT7470_REG_PWM_CFG(i);
		if (i2c_smbus_read_byte_data(client, reg) & reg_mask)
			data->pwm_automatic[i] = 1;
		else
			data->pwm_automatic[i] = 0;

		reg = ADT7470_REG_PWM_AUTO_TEMP(i);
		cfg = i2c_smbus_read_byte_data(client, reg);
		if (!(i % 2))
			data->pwm_auto_temp[i] = cfg >> 4;
		else
			data->pwm_auto_temp[i] = cfg & 0xF;
	}

	if (i2c_smbus_read_byte_data(client, ADT7470_REG_CFG) &
	    ADT7470_FSPD_MASK)
		data->force_pwm_max = 1;
	else
		data->force_pwm_max = 0;

	data->alarm = i2c_smbus_read_byte_data(client, ADT7470_REG_ALARM1);
	if (data->alarm & ADT7470_OOL_ALARM)
		data->alarm |= ALARM2(i2c_smbus_read_byte_data(client,
							ADT7470_REG_ALARM2));
	data->alarms_mask = adt7470_read_word_data(client,
						   ADT7470_REG_ALARM1_MASK);

	data->sensors_last_updated = local_jiffies;
	data->sensors_valid = 1;

no_sensor_update:
	if (!need_limits)
		goto out;

	for (i = 0; i < ADT7470_TEMP_COUNT; i++) {
		data->temp_min[i] = i2c_smbus_read_byte_data(client,
						ADT7470_TEMP_MIN_REG(i));
		data->temp_max[i] = i2c_smbus_read_byte_data(client,
						ADT7470_TEMP_MAX_REG(i));
	}

	for (i = 0; i < ADT7470_FAN_COUNT; i++) {
		data->fan_min[i] = adt7470_read_word_data(client,
						ADT7470_REG_FAN_MIN(i));
		data->fan_max[i] = adt7470_read_word_data(client,
						ADT7470_REG_FAN_MAX(i));
	}

	for (i = 0; i < ADT7470_PWM_COUNT; i++) {
		data->pwm_max[i] = i2c_smbus_read_byte_data(client,
						ADT7470_REG_PWM_MAX(i));
		data->pwm_min[i] = i2c_smbus_read_byte_data(client,
						ADT7470_REG_PWM_MIN(i));
		data->pwm_tmin[i] = i2c_smbus_read_byte_data(client,
						ADT7470_REG_PWM_TMIN(i));
	}

	data->limits_last_updated = local_jiffies;
	data->limits_valid = 1;

out:
	mutex_unlock(&data->lock);
	return data;
}

static ssize_t auto_update_interval_show(struct device *dev,
					 struct device_attribute *devattr,
					 char *buf)
{
	struct adt7470_data *data = adt7470_update_device(dev);
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

static ssize_t temp_min_show(struct device *dev,
			     struct device_attribute *devattr, char *buf)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	struct adt7470_data *data = adt7470_update_device(dev);
	return sprintf(buf, "%d\n", 1000 * data->temp_min[attr->index]);
}

static ssize_t temp_min_store(struct device *dev,
			      struct device_attribute *devattr,
			      const char *buf, size_t count)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	struct adt7470_data *data = dev_get_drvdata(dev);
	struct i2c_client *client = data->client;
	long temp;

	if (kstrtol(buf, 10, &temp))
		return -EINVAL;

	temp = clamp_val(temp, -128000, 127000);
	temp = DIV_ROUND_CLOSEST(temp, 1000);

	mutex_lock(&data->lock);
	data->temp_min[attr->index] = temp;
	i2c_smbus_write_byte_data(client, ADT7470_TEMP_MIN_REG(attr->index),
				  temp);
	mutex_unlock(&data->lock);

	return count;
}

static ssize_t temp_max_show(struct device *dev,
			     struct device_attribute *devattr, char *buf)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	struct adt7470_data *data = adt7470_update_device(dev);
	return sprintf(buf, "%d\n", 1000 * data->temp_max[attr->index]);
}

static ssize_t temp_max_store(struct device *dev,
			      struct device_attribute *devattr,
			      const char *buf, size_t count)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	struct adt7470_data *data = dev_get_drvdata(dev);
	struct i2c_client *client = data->client;
	long temp;

	if (kstrtol(buf, 10, &temp))
		return -EINVAL;

	temp = clamp_val(temp, -128000, 127000);
	temp = DIV_ROUND_CLOSEST(temp, 1000);

	mutex_lock(&data->lock);
	data->temp_max[attr->index] = temp;
	i2c_smbus_write_byte_data(client, ADT7470_TEMP_MAX_REG(attr->index),
				  temp);
	mutex_unlock(&data->lock);

	return count;
}

static ssize_t temp_show(struct device *dev, struct device_attribute *devattr,
			 char *buf)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	struct adt7470_data *data = adt7470_update_device(dev);
	return sprintf(buf, "%d\n", 1000 * data->temp[attr->index]);
}

static ssize_t alarm_mask_show(struct device *dev,
			   struct device_attribute *devattr,
			   char *buf)
{
	struct adt7470_data *data = adt7470_update_device(dev);

	return sprintf(buf, "%x\n", data->alarms_mask);
}

static ssize_t alarm_mask_store(struct device *dev,
				struct device_attribute *devattr,
				const char *buf, size_t count)
{
	struct adt7470_data *data = dev_get_drvdata(dev);
	long mask;

	if (kstrtoul(buf, 0, &mask))
		return -EINVAL;

	if (mask & ~0xffff)
		return -EINVAL;

	mutex_lock(&data->lock);
	data->alarms_mask = mask;
	adt7470_write_word_data(data->client, ADT7470_REG_ALARM1_MASK, mask);
	mutex_unlock(&data->lock);

	return count;
}

static ssize_t fan_max_show(struct device *dev,
			    struct device_attribute *devattr, char *buf)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	struct adt7470_data *data = adt7470_update_device(dev);

	if (FAN_DATA_VALID(data->fan_max[attr->index]))
		return sprintf(buf, "%d\n",
			       FAN_PERIOD_TO_RPM(data->fan_max[attr->index]));
	else
		return sprintf(buf, "0\n");
}

static ssize_t fan_max_store(struct device *dev,
			     struct device_attribute *devattr,
			     const char *buf, size_t count)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	struct adt7470_data *data = dev_get_drvdata(dev);
	struct i2c_client *client = data->client;
	long temp;

	if (kstrtol(buf, 10, &temp) || !temp)
		return -EINVAL;

	temp = FAN_RPM_TO_PERIOD(temp);
	temp = clamp_val(temp, 1, 65534);

	mutex_lock(&data->lock);
	data->fan_max[attr->index] = temp;
	adt7470_write_word_data(client, ADT7470_REG_FAN_MAX(attr->index), temp);
	mutex_unlock(&data->lock);

	return count;
}

static ssize_t fan_min_show(struct device *dev,
			    struct device_attribute *devattr, char *buf)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	struct adt7470_data *data = adt7470_update_device(dev);

	if (FAN_DATA_VALID(data->fan_min[attr->index]))
		return sprintf(buf, "%d\n",
			       FAN_PERIOD_TO_RPM(data->fan_min[attr->index]));
	else
		return sprintf(buf, "0\n");
}

static ssize_t fan_min_store(struct device *dev,
			     struct device_attribute *devattr,
			     const char *buf, size_t count)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	struct adt7470_data *data = dev_get_drvdata(dev);
	struct i2c_client *client = data->client;
	long temp;

	if (kstrtol(buf, 10, &temp) || !temp)
		return -EINVAL;

	temp = FAN_RPM_TO_PERIOD(temp);
	temp = clamp_val(temp, 1, 65534);

	mutex_lock(&data->lock);
	data->fan_min[attr->index] = temp;
	adt7470_write_word_data(client, ADT7470_REG_FAN_MIN(attr->index), temp);
	mutex_unlock(&data->lock);

	return count;
}

static ssize_t fan_show(struct device *dev, struct device_attribute *devattr,
			char *buf)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	struct adt7470_data *data = adt7470_update_device(dev);

	if (FAN_DATA_VALID(data->fan[attr->index]))
		return sprintf(buf, "%d\n",
			       FAN_PERIOD_TO_RPM(data->fan[attr->index]));
	else
		return sprintf(buf, "0\n");
}

static ssize_t force_pwm_max_show(struct device *dev,
				  struct device_attribute *devattr, char *buf)
{
	struct adt7470_data *data = adt7470_update_device(dev);
	return sprintf(buf, "%d\n", data->force_pwm_max);
}

static ssize_t force_pwm_max_store(struct device *dev,
				   struct device_attribute *devattr,
				   const char *buf, size_t count)
{
	struct adt7470_data *data = dev_get_drvdata(dev);
	struct i2c_client *client = data->client;
	long temp;
	u8 reg;

	if (kstrtol(buf, 10, &temp))
		return -EINVAL;

	mutex_lock(&data->lock);
	data->force_pwm_max = temp;
	reg = i2c_smbus_read_byte_data(client, ADT7470_REG_CFG);
	if (temp)
		reg |= ADT7470_FSPD_MASK;
	else
		reg &= ~ADT7470_FSPD_MASK;
	i2c_smbus_write_byte_data(client, ADT7470_REG_CFG, reg);
	mutex_unlock(&data->lock);

	return count;
}

static ssize_t pwm_show(struct device *dev, struct device_attribute *devattr,
			char *buf)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	struct adt7470_data *data = adt7470_update_device(dev);
	return sprintf(buf, "%d\n", data->pwm[attr->index]);
}

static ssize_t pwm_store(struct device *dev, struct device_attribute *devattr,
			 const char *buf, size_t count)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	struct adt7470_data *data = dev_get_drvdata(dev);
	struct i2c_client *client = data->client;
	long temp;

	if (kstrtol(buf, 10, &temp))
		return -EINVAL;

	temp = clamp_val(temp, 0, 255);

	mutex_lock(&data->lock);
	data->pwm[attr->index] = temp;
	i2c_smbus_write_byte_data(client, ADT7470_REG_PWM(attr->index), temp);
	mutex_unlock(&data->lock);

	return count;
}

/* These are the valid PWM frequencies to the nearest Hz */
static const int adt7470_freq_map[] = {
	11, 15, 22, 29, 35, 44, 59, 88, 1400, 22500
};

static ssize_t pwm1_freq_show(struct device *dev,
			      struct device_attribute *devattr, char *buf)
{
	struct adt7470_data *data = adt7470_update_device(dev);
	unsigned char cfg_reg_1;
	unsigned char cfg_reg_2;
	int index;

	mutex_lock(&data->lock);
	cfg_reg_1 = i2c_smbus_read_byte_data(data->client, ADT7470_REG_CFG);
	cfg_reg_2 = i2c_smbus_read_byte_data(data->client, ADT7470_REG_CFG_2);
	mutex_unlock(&data->lock);

	index = (cfg_reg_2 & ADT7470_FREQ_MASK) >> ADT7470_FREQ_SHIFT;
	if (!(cfg_reg_1 & ADT7470_CFG_LF))
		index += 8;
	if (index >= ARRAY_SIZE(adt7470_freq_map))
		index = ARRAY_SIZE(adt7470_freq_map) - 1;

	return scnprintf(buf, PAGE_SIZE, "%d\n", adt7470_freq_map[index]);
}

static ssize_t pwm1_freq_store(struct device *dev,
			       struct device_attribute *devattr,
			       const char *buf, size_t count)
{
	struct adt7470_data *data = dev_get_drvdata(dev);
	struct i2c_client *client = data->client;
	long freq;
	int index;
	int low_freq = ADT7470_CFG_LF;
	unsigned char val;

	if (kstrtol(buf, 10, &freq))
		return -EINVAL;

	/* Round the user value given to the closest available frequency */
	index = find_closest(freq, adt7470_freq_map,
			     ARRAY_SIZE(adt7470_freq_map));

	if (index >= 8) {
		index -= 8;
		low_freq = 0;
	}

	mutex_lock(&data->lock);
	/* Configuration Register 1 */
	val = i2c_smbus_read_byte_data(client, ADT7470_REG_CFG);
	i2c_smbus_write_byte_data(client, ADT7470_REG_CFG,
				  (val & ~ADT7470_CFG_LF) | low_freq);
	/* Configuration Register 2 */
	val = i2c_smbus_read_byte_data(client, ADT7470_REG_CFG_2);
	i2c_smbus_write_byte_data(client, ADT7470_REG_CFG_2,
		(val & ~ADT7470_FREQ_MASK) | (index << ADT7470_FREQ_SHIFT));
	mutex_unlock(&data->lock);

	return count;
}

static ssize_t pwm_max_show(struct device *dev,
			    struct device_attribute *devattr, char *buf)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	struct adt7470_data *data = adt7470_update_device(dev);
	return sprintf(buf, "%d\n", data->pwm_max[attr->index]);
}

static ssize_t pwm_max_store(struct device *dev,
			     struct device_attribute *devattr,
			     const char *buf, size_t count)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	struct adt7470_data *data = dev_get_drvdata(dev);
	struct i2c_client *client = data->client;
	long temp;

	if (kstrtol(buf, 10, &temp))
		return -EINVAL;

	temp = clamp_val(temp, 0, 255);

	mutex_lock(&data->lock);
	data->pwm_max[attr->index] = temp;
	i2c_smbus_write_byte_data(client, ADT7470_REG_PWM_MAX(attr->index),
				  temp);
	mutex_unlock(&data->lock);

	return count;
}

static ssize_t pwm_min_show(struct device *dev,
			    struct device_attribute *devattr, char *buf)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	struct adt7470_data *data = adt7470_update_device(dev);
	return sprintf(buf, "%d\n", data->pwm_min[attr->index]);
}

static ssize_t pwm_min_store(struct device *dev,
			     struct device_attribute *devattr,
			     const char *buf, size_t count)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	struct adt7470_data *data = dev_get_drvdata(dev);
	struct i2c_client *client = data->client;
	long temp;

	if (kstrtol(buf, 10, &temp))
		return -EINVAL;

	temp = clamp_val(temp, 0, 255);

	mutex_lock(&data->lock);
	data->pwm_min[attr->index] = temp;
	i2c_smbus_write_byte_data(client, ADT7470_REG_PWM_MIN(attr->index),
				  temp);
	mutex_unlock(&data->lock);

	return count;
}

static ssize_t pwm_tmax_show(struct device *dev,
			     struct device_attribute *devattr, char *buf)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	struct adt7470_data *data = adt7470_update_device(dev);
	/* the datasheet says that tmax = tmin + 20C */
	return sprintf(buf, "%d\n", 1000 * (20 + data->pwm_tmin[attr->index]));
}

static ssize_t pwm_tmin_show(struct device *dev,
			     struct device_attribute *devattr, char *buf)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	struct adt7470_data *data = adt7470_update_device(dev);
	return sprintf(buf, "%d\n", 1000 * data->pwm_tmin[attr->index]);
}

static ssize_t pwm_tmin_store(struct device *dev,
			      struct device_attribute *devattr,
			      const char *buf, size_t count)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	struct adt7470_data *data = dev_get_drvdata(dev);
	struct i2c_client *client = data->client;
	long temp;

	if (kstrtol(buf, 10, &temp))
		return -EINVAL;

	temp = clamp_val(temp, -128000, 127000);
	temp = DIV_ROUND_CLOSEST(temp, 1000);

	mutex_lock(&data->lock);
	data->pwm_tmin[attr->index] = temp;
	i2c_smbus_write_byte_data(client, ADT7470_REG_PWM_TMIN(attr->index),
				  temp);
	mutex_unlock(&data->lock);

	return count;
}

static ssize_t pwm_auto_show(struct device *dev,
			     struct device_attribute *devattr, char *buf)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	struct adt7470_data *data = adt7470_update_device(dev);
	return sprintf(buf, "%d\n", 1 + data->pwm_automatic[attr->index]);
}

static ssize_t pwm_auto_store(struct device *dev,
			      struct device_attribute *devattr,
			      const char *buf, size_t count)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	struct adt7470_data *data = dev_get_drvdata(dev);
	struct i2c_client *client = data->client;
	int pwm_auto_reg = ADT7470_REG_PWM_CFG(attr->index);
	int pwm_auto_reg_mask;
	long temp;
	u8 reg;

	if (kstrtol(buf, 10, &temp))
		return -EINVAL;

	if (attr->index % 2)
		pwm_auto_reg_mask = ADT7470_PWM2_AUTO_MASK;
	else
		pwm_auto_reg_mask = ADT7470_PWM1_AUTO_MASK;

	if (temp != 2 && temp != 1)
		return -EINVAL;
	temp--;

	mutex_lock(&data->lock);
	data->pwm_automatic[attr->index] = temp;
	reg = i2c_smbus_read_byte_data(client, pwm_auto_reg);
	if (temp)
		reg |= pwm_auto_reg_mask;
	else
		reg &= ~pwm_auto_reg_mask;
	i2c_smbus_write_byte_data(client, pwm_auto_reg, reg);
	mutex_unlock(&data->lock);

	return count;
}

static ssize_t pwm_auto_temp_show(struct device *dev,
				  struct device_attribute *devattr, char *buf)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	struct adt7470_data *data = adt7470_update_device(dev);
	u8 ctrl = data->pwm_auto_temp[attr->index];

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
	struct i2c_client *client = data->client;
	int pwm_auto_reg = ADT7470_REG_PWM_AUTO_TEMP(attr->index);
	long temp;
	u8 reg;

	if (kstrtol(buf, 10, &temp))
		return -EINVAL;

	temp = cvt_auto_temp(temp);
	if (temp < 0)
		return temp;

	mutex_lock(&data->lock);
	data->pwm_automatic[attr->index] = temp;
	reg = i2c_smbus_read_byte_data(client, pwm_auto_reg);

	if (!(attr->index % 2)) {
		reg &= 0xF;
		reg |= (temp << 4) & 0xF0;
	} else {
		reg &= 0xF0;
		reg |= temp & 0xF;
	}

	i2c_smbus_write_byte_data(client, pwm_auto_reg, reg);
	mutex_unlock(&data->lock);

	return count;
}

static ssize_t alarm_show(struct device *dev,
			  struct device_attribute *devattr, char *buf)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	struct adt7470_data *data = adt7470_update_device(dev);

	if (data->alarm & attr->index)
		return sprintf(buf, "1\n");
	else
		return sprintf(buf, "0\n");
}

static DEVICE_ATTR_RW(alarm_mask);
static DEVICE_ATTR_RW(num_temp_sensors);
static DEVICE_ATTR_RW(auto_update_interval);

static SENSOR_DEVICE_ATTR_RW(temp1_max, temp_max, 0);
static SENSOR_DEVICE_ATTR_RW(temp2_max, temp_max, 1);
static SENSOR_DEVICE_ATTR_RW(temp3_max, temp_max, 2);
static SENSOR_DEVICE_ATTR_RW(temp4_max, temp_max, 3);
static SENSOR_DEVICE_ATTR_RW(temp5_max, temp_max, 4);
static SENSOR_DEVICE_ATTR_RW(temp6_max, temp_max, 5);
static SENSOR_DEVICE_ATTR_RW(temp7_max, temp_max, 6);
static SENSOR_DEVICE_ATTR_RW(temp8_max, temp_max, 7);
static SENSOR_DEVICE_ATTR_RW(temp9_max, temp_max, 8);
static SENSOR_DEVICE_ATTR_RW(temp10_max, temp_max, 9);

static SENSOR_DEVICE_ATTR_RW(temp1_min, temp_min, 0);
static SENSOR_DEVICE_ATTR_RW(temp2_min, temp_min, 1);
static SENSOR_DEVICE_ATTR_RW(temp3_min, temp_min, 2);
static SENSOR_DEVICE_ATTR_RW(temp4_min, temp_min, 3);
static SENSOR_DEVICE_ATTR_RW(temp5_min, temp_min, 4);
static SENSOR_DEVICE_ATTR_RW(temp6_min, temp_min, 5);
static SENSOR_DEVICE_ATTR_RW(temp7_min, temp_min, 6);
static SENSOR_DEVICE_ATTR_RW(temp8_min, temp_min, 7);
static SENSOR_DEVICE_ATTR_RW(temp9_min, temp_min, 8);
static SENSOR_DEVICE_ATTR_RW(temp10_min, temp_min, 9);

static SENSOR_DEVICE_ATTR_RO(temp1_input, temp, 0);
static SENSOR_DEVICE_ATTR_RO(temp2_input, temp, 1);
static SENSOR_DEVICE_ATTR_RO(temp3_input, temp, 2);
static SENSOR_DEVICE_ATTR_RO(temp4_input, temp, 3);
static SENSOR_DEVICE_ATTR_RO(temp5_input, temp, 4);
static SENSOR_DEVICE_ATTR_RO(temp6_input, temp, 5);
static SENSOR_DEVICE_ATTR_RO(temp7_input, temp, 6);
static SENSOR_DEVICE_ATTR_RO(temp8_input, temp, 7);
static SENSOR_DEVICE_ATTR_RO(temp9_input, temp, 8);
static SENSOR_DEVICE_ATTR_RO(temp10_input, temp, 9);

static SENSOR_DEVICE_ATTR_RO(temp1_alarm, alarm, ADT7470_R1T_ALARM);
static SENSOR_DEVICE_ATTR_RO(temp2_alarm, alarm, ADT7470_R2T_ALARM);
static SENSOR_DEVICE_ATTR_RO(temp3_alarm, alarm, ADT7470_R3T_ALARM);
static SENSOR_DEVICE_ATTR_RO(temp4_alarm, alarm, ADT7470_R4T_ALARM);
static SENSOR_DEVICE_ATTR_RO(temp5_alarm, alarm, ADT7470_R5T_ALARM);
static SENSOR_DEVICE_ATTR_RO(temp6_alarm, alarm, ADT7470_R6T_ALARM);
static SENSOR_DEVICE_ATTR_RO(temp7_alarm, alarm, ADT7470_R7T_ALARM);
static SENSOR_DEVICE_ATTR_RO(temp8_alarm, alarm, ALARM2(ADT7470_R8T_ALARM));
static SENSOR_DEVICE_ATTR_RO(temp9_alarm, alarm, ALARM2(ADT7470_R9T_ALARM));
static SENSOR_DEVICE_ATTR_RO(temp10_alarm, alarm, ALARM2(ADT7470_R10T_ALARM));

static SENSOR_DEVICE_ATTR_RW(fan1_max, fan_max, 0);
static SENSOR_DEVICE_ATTR_RW(fan2_max, fan_max, 1);
static SENSOR_DEVICE_ATTR_RW(fan3_max, fan_max, 2);
static SENSOR_DEVICE_ATTR_RW(fan4_max, fan_max, 3);

static SENSOR_DEVICE_ATTR_RW(fan1_min, fan_min, 0);
static SENSOR_DEVICE_ATTR_RW(fan2_min, fan_min, 1);
static SENSOR_DEVICE_ATTR_RW(fan3_min, fan_min, 2);
static SENSOR_DEVICE_ATTR_RW(fan4_min, fan_min, 3);

static SENSOR_DEVICE_ATTR_RO(fan1_input, fan, 0);
static SENSOR_DEVICE_ATTR_RO(fan2_input, fan, 1);
static SENSOR_DEVICE_ATTR_RO(fan3_input, fan, 2);
static SENSOR_DEVICE_ATTR_RO(fan4_input, fan, 3);

static SENSOR_DEVICE_ATTR_RO(fan1_alarm, alarm, ALARM2(ADT7470_FAN1_ALARM));
static SENSOR_DEVICE_ATTR_RO(fan2_alarm, alarm, ALARM2(ADT7470_FAN2_ALARM));
static SENSOR_DEVICE_ATTR_RO(fan3_alarm, alarm, ALARM2(ADT7470_FAN3_ALARM));
static SENSOR_DEVICE_ATTR_RO(fan4_alarm, alarm, ALARM2(ADT7470_FAN4_ALARM));

static SENSOR_DEVICE_ATTR_RW(force_pwm_max, force_pwm_max, 0);

static SENSOR_DEVICE_ATTR_RW(pwm1, pwm, 0);
static SENSOR_DEVICE_ATTR_RW(pwm2, pwm, 1);
static SENSOR_DEVICE_ATTR_RW(pwm3, pwm, 2);
static SENSOR_DEVICE_ATTR_RW(pwm4, pwm, 3);

static DEVICE_ATTR_RW(pwm1_freq);

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

static SENSOR_DEVICE_ATTR_RW(pwm1_enable, pwm_auto, 0);
static SENSOR_DEVICE_ATTR_RW(pwm2_enable, pwm_auto, 1);
static SENSOR_DEVICE_ATTR_RW(pwm3_enable, pwm_auto, 2);
static SENSOR_DEVICE_ATTR_RW(pwm4_enable, pwm_auto, 3);

static SENSOR_DEVICE_ATTR_RW(pwm1_auto_channels_temp, pwm_auto_temp, 0);
static SENSOR_DEVICE_ATTR_RW(pwm2_auto_channels_temp, pwm_auto_temp, 1);
static SENSOR_DEVICE_ATTR_RW(pwm3_auto_channels_temp, pwm_auto_temp, 2);
static SENSOR_DEVICE_ATTR_RW(pwm4_auto_channels_temp, pwm_auto_temp, 3);

static struct attribute *adt7470_attrs[] = {
	&dev_attr_alarm_mask.attr,
	&dev_attr_num_temp_sensors.attr,
	&dev_attr_auto_update_interval.attr,
	&sensor_dev_attr_temp1_max.dev_attr.attr,
	&sensor_dev_attr_temp2_max.dev_attr.attr,
	&sensor_dev_attr_temp3_max.dev_attr.attr,
	&sensor_dev_attr_temp4_max.dev_attr.attr,
	&sensor_dev_attr_temp5_max.dev_attr.attr,
	&sensor_dev_attr_temp6_max.dev_attr.attr,
	&sensor_dev_attr_temp7_max.dev_attr.attr,
	&sensor_dev_attr_temp8_max.dev_attr.attr,
	&sensor_dev_attr_temp9_max.dev_attr.attr,
	&sensor_dev_attr_temp10_max.dev_attr.attr,
	&sensor_dev_attr_temp1_min.dev_attr.attr,
	&sensor_dev_attr_temp2_min.dev_attr.attr,
	&sensor_dev_attr_temp3_min.dev_attr.attr,
	&sensor_dev_attr_temp4_min.dev_attr.attr,
	&sensor_dev_attr_temp5_min.dev_attr.attr,
	&sensor_dev_attr_temp6_min.dev_attr.attr,
	&sensor_dev_attr_temp7_min.dev_attr.attr,
	&sensor_dev_attr_temp8_min.dev_attr.attr,
	&sensor_dev_attr_temp9_min.dev_attr.attr,
	&sensor_dev_attr_temp10_min.dev_attr.attr,
	&sensor_dev_attr_temp1_input.dev_attr.attr,
	&sensor_dev_attr_temp2_input.dev_attr.attr,
	&sensor_dev_attr_temp3_input.dev_attr.attr,
	&sensor_dev_attr_temp4_input.dev_attr.attr,
	&sensor_dev_attr_temp5_input.dev_attr.attr,
	&sensor_dev_attr_temp6_input.dev_attr.attr,
	&sensor_dev_attr_temp7_input.dev_attr.attr,
	&sensor_dev_attr_temp8_input.dev_attr.attr,
	&sensor_dev_attr_temp9_input.dev_attr.attr,
	&sensor_dev_attr_temp10_input.dev_attr.attr,
	&sensor_dev_attr_temp1_alarm.dev_attr.attr,
	&sensor_dev_attr_temp2_alarm.dev_attr.attr,
	&sensor_dev_attr_temp3_alarm.dev_attr.attr,
	&sensor_dev_attr_temp4_alarm.dev_attr.attr,
	&sensor_dev_attr_temp5_alarm.dev_attr.attr,
	&sensor_dev_attr_temp6_alarm.dev_attr.attr,
	&sensor_dev_attr_temp7_alarm.dev_attr.attr,
	&sensor_dev_attr_temp8_alarm.dev_attr.attr,
	&sensor_dev_attr_temp9_alarm.dev_attr.attr,
	&sensor_dev_attr_temp10_alarm.dev_attr.attr,
	&sensor_dev_attr_fan1_max.dev_attr.attr,
	&sensor_dev_attr_fan2_max.dev_attr.attr,
	&sensor_dev_attr_fan3_max.dev_attr.attr,
	&sensor_dev_attr_fan4_max.dev_attr.attr,
	&sensor_dev_attr_fan1_min.dev_attr.attr,
	&sensor_dev_attr_fan2_min.dev_attr.attr,
	&sensor_dev_attr_fan3_min.dev_attr.attr,
	&sensor_dev_attr_fan4_min.dev_attr.attr,
	&sensor_dev_attr_fan1_input.dev_attr.attr,
	&sensor_dev_attr_fan2_input.dev_attr.attr,
	&sensor_dev_attr_fan3_input.dev_attr.attr,
	&sensor_dev_attr_fan4_input.dev_attr.attr,
	&sensor_dev_attr_fan1_alarm.dev_attr.attr,
	&sensor_dev_attr_fan2_alarm.dev_attr.attr,
	&sensor_dev_attr_fan3_alarm.dev_attr.attr,
	&sensor_dev_attr_fan4_alarm.dev_attr.attr,
	&sensor_dev_attr_force_pwm_max.dev_attr.attr,
	&sensor_dev_attr_pwm1.dev_attr.attr,
	&dev_attr_pwm1_freq.attr,
	&sensor_dev_attr_pwm2.dev_attr.attr,
	&sensor_dev_attr_pwm3.dev_attr.attr,
	&sensor_dev_attr_pwm4.dev_attr.attr,
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
	&sensor_dev_attr_pwm1_enable.dev_attr.attr,
	&sensor_dev_attr_pwm2_enable.dev_attr.attr,
	&sensor_dev_attr_pwm3_enable.dev_attr.attr,
	&sensor_dev_attr_pwm4_enable.dev_attr.attr,
	&sensor_dev_attr_pwm1_auto_channels_temp.dev_attr.attr,
	&sensor_dev_attr_pwm2_auto_channels_temp.dev_attr.attr,
	&sensor_dev_attr_pwm3_auto_channels_temp.dev_attr.attr,
	&sensor_dev_attr_pwm4_auto_channels_temp.dev_attr.attr,
	NULL
};

ATTRIBUTE_GROUPS(adt7470);

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

	strlcpy(info->type, "adt7470", I2C_NAME_SIZE);

	return 0;
}

static void adt7470_init_client(struct i2c_client *client)
{
	int reg = i2c_smbus_read_byte_data(client, ADT7470_REG_CFG);

	if (reg < 0) {
		dev_err(&client->dev, "cannot read configuration register\n");
	} else {
		/* start monitoring (and do a self-test) */
		i2c_smbus_write_byte_data(client, ADT7470_REG_CFG, reg | 3);
	}
}

static int adt7470_probe(struct i2c_client *client,
			 const struct i2c_device_id *id)
{
	struct device *dev = &client->dev;
	struct adt7470_data *data;
	struct device *hwmon_dev;

	data = devm_kzalloc(dev, sizeof(struct adt7470_data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->num_temp_sensors = -1;
	data->auto_update_interval = AUTO_UPDATE_INTERVAL;

	i2c_set_clientdata(client, data);
	data->client = client;
	mutex_init(&data->lock);

	dev_info(&client->dev, "%s chip found\n", client->name);

	/* Initialize the ADT7470 chip */
	adt7470_init_client(client);

	/* Register sysfs hooks */
	hwmon_dev = devm_hwmon_device_register_with_groups(dev, client->name,
							   data,
							   adt7470_groups);

	if (IS_ERR(hwmon_dev))
		return PTR_ERR(hwmon_dev);

	data->auto_update = kthread_run(adt7470_update_thread, client, "%s",
					dev_name(hwmon_dev));
	if (IS_ERR(data->auto_update)) {
		return PTR_ERR(data->auto_update);
	}

	return 0;
}

static int adt7470_remove(struct i2c_client *client)
{
	struct adt7470_data *data = i2c_get_clientdata(client);

	kthread_stop(data->auto_update);
	return 0;
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
