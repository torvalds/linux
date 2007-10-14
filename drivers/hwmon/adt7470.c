/*
 * A hwmon driver for the Analog Devices ADT7470
 * Copyright (C) 2007 IBM
 *
 * Author: Darrick J. Wong <djwong@us.ibm.com>
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

#include <linux/module.h>
#include <linux/jiffies.h>
#include <linux/i2c.h>
#include <linux/hwmon.h>
#include <linux/hwmon-sysfs.h>
#include <linux/err.h>
#include <linux/mutex.h>
#include <linux/delay.h>
#include <linux/log2.h>

/* Addresses to scan */
static unsigned short normal_i2c[] = { 0x2C, 0x2E, 0x2F, I2C_CLIENT_END };

/* Insmod parameters */
I2C_CLIENT_INSMOD_1(adt7470);

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
#define ADT7470_REG_ALARM2			0x42
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
#define ADT7470_REG_PWM34_CFG			0x69
#define		ADT7470_PWM3_AUTO_MASK		0x40
#define		ADT7470_PWM4_AUTO_MASK		0x80
#define	ADT7470_REG_PWM_MIN_BASE_ADDR		0x6A
#define ADT7470_REG_PWM_MIN_MAX_ADDR		0x6D
#define ADT7470_REG_PWM_TEMP_MIN_BASE_ADDR	0x6E
#define ADT7470_REG_PWM_TEMP_MIN_MAX_ADDR	0x71
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

/* sleep 1s while gathering temperature data */
#define TEMP_COLLECTION_TIME	1000

#define power_of_2(x)	(((x) & ((x) - 1)) == 0)

/* datasheet says to divide this number by the fan reading to get fan rpm */
#define FAN_PERIOD_TO_RPM(x)	((90000 * 60) / (x))
#define FAN_RPM_TO_PERIOD	FAN_PERIOD_TO_RPM
#define FAN_PERIOD_INVALID	65535
#define FAN_DATA_VALID(x)	((x) && (x) != FAN_PERIOD_INVALID)

struct adt7470_data {
	struct i2c_client	client;
	struct device		*hwmon_dev;
	struct attribute_group	attrs;
	struct mutex		lock;
	char			sensors_valid;
	char			limits_valid;
	unsigned long		sensors_last_updated;	/* In jiffies */
	unsigned long		limits_last_updated;	/* In jiffies */

	s8			temp[ADT7470_TEMP_COUNT];
	s8			temp_min[ADT7470_TEMP_COUNT];
	s8			temp_max[ADT7470_TEMP_COUNT];
	u16			fan[ADT7470_FAN_COUNT];
	u16			fan_min[ADT7470_FAN_COUNT];
	u16			fan_max[ADT7470_FAN_COUNT];
	u16			alarms, alarms_mask;
	u8			force_pwm_max;
	u8			pwm[ADT7470_PWM_COUNT];
	u8			pwm_max[ADT7470_PWM_COUNT];
	u8			pwm_automatic[ADT7470_PWM_COUNT];
	u8			pwm_min[ADT7470_PWM_COUNT];
	s8			pwm_tmin[ADT7470_PWM_COUNT];
	u8			pwm_auto_temp[ADT7470_PWM_COUNT];
};

static int adt7470_attach_adapter(struct i2c_adapter *adapter);
static int adt7470_detect(struct i2c_adapter *adapter, int address, int kind);
static int adt7470_detach_client(struct i2c_client *client);

static struct i2c_driver adt7470_driver = {
	.driver = {
		.name	= "adt7470",
	},
	.attach_adapter	= adt7470_attach_adapter,
	.detach_client	= adt7470_detach_client,
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
	       && i2c_smbus_write_byte_data(client, reg + 1, value >> 8);
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

static struct adt7470_data *adt7470_update_device(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct adt7470_data *data = i2c_get_clientdata(client);
	unsigned long local_jiffies = jiffies;
	u8 cfg;
	int i;

	mutex_lock(&data->lock);
	if (time_before(local_jiffies, data->sensors_last_updated +
		SENSOR_REFRESH_INTERVAL)
		&& data->sensors_valid)
		goto no_sensor_update;

	/* start reading temperature sensors */
	cfg = i2c_smbus_read_byte_data(client, ADT7470_REG_CFG);
	cfg |= 0x80;
	i2c_smbus_write_byte_data(client, ADT7470_REG_CFG, cfg);

	/*
	 * Delay is 200ms * number of tmp05 sensors.  Too bad
	 * there's no way to figure out how many are connected.
	 * For now, assume 1s will work.
	 */
	msleep(TEMP_COLLECTION_TIME);

	/* done reading temperature sensors */
	cfg = i2c_smbus_read_byte_data(client, ADT7470_REG_CFG);
	cfg &= ~0x80;
	i2c_smbus_write_byte_data(client, ADT7470_REG_CFG, cfg);

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

	data->alarms = adt7470_read_word_data(client, ADT7470_REG_ALARM1);
	data->alarms_mask = adt7470_read_word_data(client,
						   ADT7470_REG_ALARM1_MASK);

	data->sensors_last_updated = local_jiffies;
	data->sensors_valid = 1;

no_sensor_update:
	if (time_before(local_jiffies, data->limits_last_updated +
		LIMIT_REFRESH_INTERVAL)
		&& data->limits_valid)
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

static ssize_t show_temp_min(struct device *dev,
			     struct device_attribute *devattr,
			     char *buf)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	struct adt7470_data *data = adt7470_update_device(dev);
	return sprintf(buf, "%d\n", 1000 * data->temp_min[attr->index]);
}

static ssize_t set_temp_min(struct device *dev,
			    struct device_attribute *devattr,
			    const char *buf,
			    size_t count)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	struct i2c_client *client = to_i2c_client(dev);
	struct adt7470_data *data = i2c_get_clientdata(client);
	int temp = simple_strtol(buf, NULL, 10) / 1000;

	mutex_lock(&data->lock);
	data->temp_min[attr->index] = temp;
	i2c_smbus_write_byte_data(client, ADT7470_TEMP_MIN_REG(attr->index),
				  temp);
	mutex_unlock(&data->lock);

	return count;
}

static ssize_t show_temp_max(struct device *dev,
			     struct device_attribute *devattr,
			     char *buf)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	struct adt7470_data *data = adt7470_update_device(dev);
	return sprintf(buf, "%d\n", 1000 * data->temp_max[attr->index]);
}

static ssize_t set_temp_max(struct device *dev,
			    struct device_attribute *devattr,
			    const char *buf,
			    size_t count)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	struct i2c_client *client = to_i2c_client(dev);
	struct adt7470_data *data = i2c_get_clientdata(client);
	int temp = simple_strtol(buf, NULL, 10) / 1000;

	mutex_lock(&data->lock);
	data->temp_max[attr->index] = temp;
	i2c_smbus_write_byte_data(client, ADT7470_TEMP_MAX_REG(attr->index),
				  temp);
	mutex_unlock(&data->lock);

	return count;
}

static ssize_t show_temp(struct device *dev, struct device_attribute *devattr,
			 char *buf)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	struct adt7470_data *data = adt7470_update_device(dev);
	return sprintf(buf, "%d\n", 1000 * data->temp[attr->index]);
}

static ssize_t show_alarms(struct device *dev,
			   struct device_attribute *devattr,
			   char *buf)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	struct adt7470_data *data = adt7470_update_device(dev);

	if (attr->index)
		return sprintf(buf, "%x\n", data->alarms);
	else
		return sprintf(buf, "%x\n", data->alarms_mask);
}

static ssize_t show_fan_max(struct device *dev,
			    struct device_attribute *devattr,
			    char *buf)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	struct adt7470_data *data = adt7470_update_device(dev);

	if (FAN_DATA_VALID(data->fan_max[attr->index]))
		return sprintf(buf, "%d\n",
			       FAN_PERIOD_TO_RPM(data->fan_max[attr->index]));
	else
		return sprintf(buf, "0\n");
}

static ssize_t set_fan_max(struct device *dev,
			   struct device_attribute *devattr,
			   const char *buf, size_t count)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	struct i2c_client *client = to_i2c_client(dev);
	struct adt7470_data *data = i2c_get_clientdata(client);
	int temp = simple_strtol(buf, NULL, 10);

	if (!temp)
		return -EINVAL;
	temp = FAN_RPM_TO_PERIOD(temp);

	mutex_lock(&data->lock);
	data->fan_max[attr->index] = temp;
	adt7470_write_word_data(client, ADT7470_REG_FAN_MAX(attr->index), temp);
	mutex_unlock(&data->lock);

	return count;
}

static ssize_t show_fan_min(struct device *dev,
			    struct device_attribute *devattr,
			    char *buf)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	struct adt7470_data *data = adt7470_update_device(dev);

	if (FAN_DATA_VALID(data->fan_min[attr->index]))
		return sprintf(buf, "%d\n",
			       FAN_PERIOD_TO_RPM(data->fan_min[attr->index]));
	else
		return sprintf(buf, "0\n");
}

static ssize_t set_fan_min(struct device *dev,
			   struct device_attribute *devattr,
			   const char *buf, size_t count)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	struct i2c_client *client = to_i2c_client(dev);
	struct adt7470_data *data = i2c_get_clientdata(client);
	int temp = simple_strtol(buf, NULL, 10);

	if (!temp)
		return -EINVAL;
	temp = FAN_RPM_TO_PERIOD(temp);

	mutex_lock(&data->lock);
	data->fan_min[attr->index] = temp;
	adt7470_write_word_data(client, ADT7470_REG_FAN_MIN(attr->index), temp);
	mutex_unlock(&data->lock);

	return count;
}

static ssize_t show_fan(struct device *dev, struct device_attribute *devattr,
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

static ssize_t show_force_pwm_max(struct device *dev,
				  struct device_attribute *devattr,
				  char *buf)
{
	struct adt7470_data *data = adt7470_update_device(dev);
	return sprintf(buf, "%d\n", data->force_pwm_max);
}

static ssize_t set_force_pwm_max(struct device *dev,
				 struct device_attribute *devattr,
				 const char *buf,
				 size_t count)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct adt7470_data *data = i2c_get_clientdata(client);
	int temp = simple_strtol(buf, NULL, 10);
	u8 reg;

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

static ssize_t show_pwm(struct device *dev, struct device_attribute *devattr,
			char *buf)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	struct adt7470_data *data = adt7470_update_device(dev);
	return sprintf(buf, "%d\n", data->pwm[attr->index]);
}

static ssize_t set_pwm(struct device *dev, struct device_attribute *devattr,
			const char *buf, size_t count)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	struct i2c_client *client = to_i2c_client(dev);
	struct adt7470_data *data = i2c_get_clientdata(client);
	int temp = simple_strtol(buf, NULL, 10);

	mutex_lock(&data->lock);
	data->pwm[attr->index] = temp;
	i2c_smbus_write_byte_data(client, ADT7470_REG_PWM(attr->index), temp);
	mutex_unlock(&data->lock);

	return count;
}

static ssize_t show_pwm_max(struct device *dev,
			    struct device_attribute *devattr,
			    char *buf)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	struct adt7470_data *data = adt7470_update_device(dev);
	return sprintf(buf, "%d\n", data->pwm_max[attr->index]);
}

static ssize_t set_pwm_max(struct device *dev,
			   struct device_attribute *devattr,
			   const char *buf,
			   size_t count)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	struct i2c_client *client = to_i2c_client(dev);
	struct adt7470_data *data = i2c_get_clientdata(client);
	int temp = simple_strtol(buf, NULL, 10);

	mutex_lock(&data->lock);
	data->pwm_max[attr->index] = temp;
	i2c_smbus_write_byte_data(client, ADT7470_REG_PWM_MAX(attr->index),
				  temp);
	mutex_unlock(&data->lock);

	return count;
}

static ssize_t show_pwm_min(struct device *dev,
			    struct device_attribute *devattr,
			    char *buf)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	struct adt7470_data *data = adt7470_update_device(dev);
	return sprintf(buf, "%d\n", data->pwm_min[attr->index]);
}

static ssize_t set_pwm_min(struct device *dev,
			   struct device_attribute *devattr,
			   const char *buf,
			   size_t count)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	struct i2c_client *client = to_i2c_client(dev);
	struct adt7470_data *data = i2c_get_clientdata(client);
	int temp = simple_strtol(buf, NULL, 10);

	mutex_lock(&data->lock);
	data->pwm_min[attr->index] = temp;
	i2c_smbus_write_byte_data(client, ADT7470_REG_PWM_MIN(attr->index),
				  temp);
	mutex_unlock(&data->lock);

	return count;
}

static ssize_t show_pwm_tmax(struct device *dev,
			     struct device_attribute *devattr,
			     char *buf)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	struct adt7470_data *data = adt7470_update_device(dev);
	/* the datasheet says that tmax = tmin + 20C */
	return sprintf(buf, "%d\n", 1000 * (20 + data->pwm_tmin[attr->index]));
}

static ssize_t show_pwm_tmin(struct device *dev,
			     struct device_attribute *devattr,
			     char *buf)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	struct adt7470_data *data = adt7470_update_device(dev);
	return sprintf(buf, "%d\n", 1000 * data->pwm_tmin[attr->index]);
}

static ssize_t set_pwm_tmin(struct device *dev,
			    struct device_attribute *devattr,
			    const char *buf,
			    size_t count)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	struct i2c_client *client = to_i2c_client(dev);
	struct adt7470_data *data = i2c_get_clientdata(client);
	int temp = simple_strtol(buf, NULL, 10) / 1000;

	mutex_lock(&data->lock);
	data->pwm_tmin[attr->index] = temp;
	i2c_smbus_write_byte_data(client, ADT7470_REG_PWM_TMIN(attr->index),
				  temp);
	mutex_unlock(&data->lock);

	return count;
}

static ssize_t show_pwm_auto(struct device *dev,
			     struct device_attribute *devattr,
			     char *buf)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	struct adt7470_data *data = adt7470_update_device(dev);
	return sprintf(buf, "%d\n", 1 + data->pwm_automatic[attr->index]);
}

static ssize_t set_pwm_auto(struct device *dev,
			    struct device_attribute *devattr,
			    const char *buf,
			    size_t count)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	struct i2c_client *client = to_i2c_client(dev);
	struct adt7470_data *data = i2c_get_clientdata(client);
	int temp = simple_strtol(buf, NULL, 10);
	int pwm_auto_reg = ADT7470_REG_PWM_CFG(attr->index);
	int pwm_auto_reg_mask;
	u8 reg;

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

static ssize_t show_pwm_auto_temp(struct device *dev,
				  struct device_attribute *devattr,
				  char *buf)
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
	if (input < 1 || !power_of_2(input))
		return -EINVAL;
	return ilog2(input) + 1;
}

static ssize_t set_pwm_auto_temp(struct device *dev,
				 struct device_attribute *devattr,
				 const char *buf,
				 size_t count)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	struct i2c_client *client = to_i2c_client(dev);
	struct adt7470_data *data = i2c_get_clientdata(client);
	int temp = cvt_auto_temp(simple_strtol(buf, NULL, 10));
	int pwm_auto_reg = ADT7470_REG_PWM_AUTO_TEMP(attr->index);
	u8 reg;

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

static SENSOR_DEVICE_ATTR(alarms, S_IRUGO, show_alarms, NULL, 0);
static SENSOR_DEVICE_ATTR(alarm_mask, S_IRUGO, show_alarms, NULL, 1);

static SENSOR_DEVICE_ATTR(temp1_max, S_IWUSR | S_IRUGO, show_temp_max,
		    set_temp_max, 0);
static SENSOR_DEVICE_ATTR(temp2_max, S_IWUSR | S_IRUGO, show_temp_max,
		    set_temp_max, 1);
static SENSOR_DEVICE_ATTR(temp3_max, S_IWUSR | S_IRUGO, show_temp_max,
		    set_temp_max, 2);
static SENSOR_DEVICE_ATTR(temp4_max, S_IWUSR | S_IRUGO, show_temp_max,
		    set_temp_max, 3);
static SENSOR_DEVICE_ATTR(temp5_max, S_IWUSR | S_IRUGO, show_temp_max,
		    set_temp_max, 4);
static SENSOR_DEVICE_ATTR(temp6_max, S_IWUSR | S_IRUGO, show_temp_max,
		    set_temp_max, 5);
static SENSOR_DEVICE_ATTR(temp7_max, S_IWUSR | S_IRUGO, show_temp_max,
		    set_temp_max, 6);
static SENSOR_DEVICE_ATTR(temp8_max, S_IWUSR | S_IRUGO, show_temp_max,
		    set_temp_max, 7);
static SENSOR_DEVICE_ATTR(temp9_max, S_IWUSR | S_IRUGO, show_temp_max,
		    set_temp_max, 8);
static SENSOR_DEVICE_ATTR(temp10_max, S_IWUSR | S_IRUGO, show_temp_max,
		    set_temp_max, 9);

static SENSOR_DEVICE_ATTR(temp1_min, S_IWUSR | S_IRUGO, show_temp_min,
		    set_temp_min, 0);
static SENSOR_DEVICE_ATTR(temp2_min, S_IWUSR | S_IRUGO, show_temp_min,
		    set_temp_min, 1);
static SENSOR_DEVICE_ATTR(temp3_min, S_IWUSR | S_IRUGO, show_temp_min,
		    set_temp_min, 2);
static SENSOR_DEVICE_ATTR(temp4_min, S_IWUSR | S_IRUGO, show_temp_min,
		    set_temp_min, 3);
static SENSOR_DEVICE_ATTR(temp5_min, S_IWUSR | S_IRUGO, show_temp_min,
		    set_temp_min, 4);
static SENSOR_DEVICE_ATTR(temp6_min, S_IWUSR | S_IRUGO, show_temp_min,
		    set_temp_min, 5);
static SENSOR_DEVICE_ATTR(temp7_min, S_IWUSR | S_IRUGO, show_temp_min,
		    set_temp_min, 6);
static SENSOR_DEVICE_ATTR(temp8_min, S_IWUSR | S_IRUGO, show_temp_min,
		    set_temp_min, 7);
static SENSOR_DEVICE_ATTR(temp9_min, S_IWUSR | S_IRUGO, show_temp_min,
		    set_temp_min, 8);
static SENSOR_DEVICE_ATTR(temp10_min, S_IWUSR | S_IRUGO, show_temp_min,
		    set_temp_min, 9);

static SENSOR_DEVICE_ATTR(temp1_input, S_IRUGO, show_temp, NULL, 0);
static SENSOR_DEVICE_ATTR(temp2_input, S_IRUGO, show_temp, NULL, 1);
static SENSOR_DEVICE_ATTR(temp3_input, S_IRUGO, show_temp, NULL, 2);
static SENSOR_DEVICE_ATTR(temp4_input, S_IRUGO, show_temp, NULL, 3);
static SENSOR_DEVICE_ATTR(temp5_input, S_IRUGO, show_temp, NULL, 4);
static SENSOR_DEVICE_ATTR(temp6_input, S_IRUGO, show_temp, NULL, 5);
static SENSOR_DEVICE_ATTR(temp7_input, S_IRUGO, show_temp, NULL, 6);
static SENSOR_DEVICE_ATTR(temp8_input, S_IRUGO, show_temp, NULL, 7);
static SENSOR_DEVICE_ATTR(temp9_input, S_IRUGO, show_temp, NULL, 8);
static SENSOR_DEVICE_ATTR(temp10_input, S_IRUGO, show_temp, NULL, 9);

static SENSOR_DEVICE_ATTR(fan1_max, S_IWUSR | S_IRUGO, show_fan_max,
		    set_fan_max, 0);
static SENSOR_DEVICE_ATTR(fan2_max, S_IWUSR | S_IRUGO, show_fan_max,
		    set_fan_max, 1);
static SENSOR_DEVICE_ATTR(fan3_max, S_IWUSR | S_IRUGO, show_fan_max,
		    set_fan_max, 2);
static SENSOR_DEVICE_ATTR(fan4_max, S_IWUSR | S_IRUGO, show_fan_max,
		    set_fan_max, 3);

static SENSOR_DEVICE_ATTR(fan1_min, S_IWUSR | S_IRUGO, show_fan_min,
		    set_fan_min, 0);
static SENSOR_DEVICE_ATTR(fan2_min, S_IWUSR | S_IRUGO, show_fan_min,
		    set_fan_min, 1);
static SENSOR_DEVICE_ATTR(fan3_min, S_IWUSR | S_IRUGO, show_fan_min,
		    set_fan_min, 2);
static SENSOR_DEVICE_ATTR(fan4_min, S_IWUSR | S_IRUGO, show_fan_min,
		    set_fan_min, 3);

static SENSOR_DEVICE_ATTR(fan1_input, S_IRUGO, show_fan, NULL, 0);
static SENSOR_DEVICE_ATTR(fan2_input, S_IRUGO, show_fan, NULL, 1);
static SENSOR_DEVICE_ATTR(fan3_input, S_IRUGO, show_fan, NULL, 2);
static SENSOR_DEVICE_ATTR(fan4_input, S_IRUGO, show_fan, NULL, 3);

static SENSOR_DEVICE_ATTR(force_pwm_max, S_IWUSR | S_IRUGO,
		    show_force_pwm_max, set_force_pwm_max, 0);

static SENSOR_DEVICE_ATTR(pwm1, S_IWUSR | S_IRUGO, show_pwm, set_pwm, 0);
static SENSOR_DEVICE_ATTR(pwm2, S_IWUSR | S_IRUGO, show_pwm, set_pwm, 1);
static SENSOR_DEVICE_ATTR(pwm3, S_IWUSR | S_IRUGO, show_pwm, set_pwm, 2);
static SENSOR_DEVICE_ATTR(pwm4, S_IWUSR | S_IRUGO, show_pwm, set_pwm, 3);

static SENSOR_DEVICE_ATTR(pwm1_auto_point1_pwm, S_IWUSR | S_IRUGO,
		    show_pwm_min, set_pwm_min, 0);
static SENSOR_DEVICE_ATTR(pwm2_auto_point1_pwm, S_IWUSR | S_IRUGO,
		    show_pwm_min, set_pwm_min, 1);
static SENSOR_DEVICE_ATTR(pwm3_auto_point1_pwm, S_IWUSR | S_IRUGO,
		    show_pwm_min, set_pwm_min, 2);
static SENSOR_DEVICE_ATTR(pwm4_auto_point1_pwm, S_IWUSR | S_IRUGO,
		    show_pwm_min, set_pwm_min, 3);

static SENSOR_DEVICE_ATTR(pwm1_auto_point2_pwm, S_IWUSR | S_IRUGO,
		    show_pwm_max, set_pwm_max, 0);
static SENSOR_DEVICE_ATTR(pwm2_auto_point2_pwm, S_IWUSR | S_IRUGO,
		    show_pwm_max, set_pwm_max, 1);
static SENSOR_DEVICE_ATTR(pwm3_auto_point2_pwm, S_IWUSR | S_IRUGO,
		    show_pwm_max, set_pwm_max, 2);
static SENSOR_DEVICE_ATTR(pwm4_auto_point2_pwm, S_IWUSR | S_IRUGO,
		    show_pwm_max, set_pwm_max, 3);

static SENSOR_DEVICE_ATTR(pwm1_auto_point1_temp, S_IWUSR | S_IRUGO,
		    show_pwm_tmin, set_pwm_tmin, 0);
static SENSOR_DEVICE_ATTR(pwm2_auto_point1_temp, S_IWUSR | S_IRUGO,
		    show_pwm_tmin, set_pwm_tmin, 1);
static SENSOR_DEVICE_ATTR(pwm3_auto_point1_temp, S_IWUSR | S_IRUGO,
		    show_pwm_tmin, set_pwm_tmin, 2);
static SENSOR_DEVICE_ATTR(pwm4_auto_point1_temp, S_IWUSR | S_IRUGO,
		    show_pwm_tmin, set_pwm_tmin, 3);

static SENSOR_DEVICE_ATTR(pwm1_auto_point2_temp, S_IRUGO, show_pwm_tmax,
		    NULL, 0);
static SENSOR_DEVICE_ATTR(pwm2_auto_point2_temp, S_IRUGO, show_pwm_tmax,
		    NULL, 1);
static SENSOR_DEVICE_ATTR(pwm3_auto_point2_temp, S_IRUGO, show_pwm_tmax,
		    NULL, 2);
static SENSOR_DEVICE_ATTR(pwm4_auto_point2_temp, S_IRUGO, show_pwm_tmax,
		    NULL, 3);

static SENSOR_DEVICE_ATTR(pwm1_enable, S_IWUSR | S_IRUGO, show_pwm_auto,
		    set_pwm_auto, 0);
static SENSOR_DEVICE_ATTR(pwm2_enable, S_IWUSR | S_IRUGO, show_pwm_auto,
		    set_pwm_auto, 1);
static SENSOR_DEVICE_ATTR(pwm3_enable, S_IWUSR | S_IRUGO, show_pwm_auto,
		    set_pwm_auto, 2);
static SENSOR_DEVICE_ATTR(pwm4_enable, S_IWUSR | S_IRUGO, show_pwm_auto,
		    set_pwm_auto, 3);

static SENSOR_DEVICE_ATTR(pwm1_auto_channels_temp, S_IWUSR | S_IRUGO,
		    show_pwm_auto_temp, set_pwm_auto_temp, 0);
static SENSOR_DEVICE_ATTR(pwm2_auto_channels_temp, S_IWUSR | S_IRUGO,
		    show_pwm_auto_temp, set_pwm_auto_temp, 1);
static SENSOR_DEVICE_ATTR(pwm3_auto_channels_temp, S_IWUSR | S_IRUGO,
		    show_pwm_auto_temp, set_pwm_auto_temp, 2);
static SENSOR_DEVICE_ATTR(pwm4_auto_channels_temp, S_IWUSR | S_IRUGO,
		    show_pwm_auto_temp, set_pwm_auto_temp, 3);

static struct attribute *adt7470_attr[] =
{
	&sensor_dev_attr_alarms.dev_attr.attr,
	&sensor_dev_attr_alarm_mask.dev_attr.attr,
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
	&sensor_dev_attr_force_pwm_max.dev_attr.attr,
	&sensor_dev_attr_pwm1.dev_attr.attr,
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

static int adt7470_attach_adapter(struct i2c_adapter *adapter)
{
	if (!(adapter->class & I2C_CLASS_HWMON))
		return 0;
	return i2c_probe(adapter, &addr_data, adt7470_detect);
}

static int adt7470_detect(struct i2c_adapter *adapter, int address, int kind)
{
	struct i2c_client *client;
	struct adt7470_data *data;
	int err = 0;

	if (!i2c_check_functionality(adapter, I2C_FUNC_SMBUS_BYTE_DATA))
		goto exit;

	if (!(data = kzalloc(sizeof(struct adt7470_data), GFP_KERNEL))) {
		err = -ENOMEM;
		goto exit;
	}

	client = &data->client;
	client->addr = address;
	client->adapter = adapter;
	client->driver = &adt7470_driver;

	i2c_set_clientdata(client, data);

	mutex_init(&data->lock);

	if (kind <= 0) {
		int vendor, device, revision;

		vendor = i2c_smbus_read_byte_data(client, ADT7470_REG_VENDOR);
		if (vendor != ADT7470_VENDOR) {
			err = -ENODEV;
			goto exit_free;
		}

		device = i2c_smbus_read_byte_data(client, ADT7470_REG_DEVICE);
		if (device != ADT7470_DEVICE) {
			err = -ENODEV;
			goto exit_free;
		}

		revision = i2c_smbus_read_byte_data(client,
						    ADT7470_REG_REVISION);
		if (revision != ADT7470_REVISION) {
			err = -ENODEV;
			goto exit_free;
		}
	} else
		dev_dbg(&adapter->dev, "detection forced\n");

	strlcpy(client->name, "adt7470", I2C_NAME_SIZE);

	if ((err = i2c_attach_client(client)))
		goto exit_free;

	dev_info(&client->dev, "%s chip found\n", client->name);

	/* Initialize the ADT7470 chip */
	adt7470_init_client(client);

	/* Register sysfs hooks */
	data->attrs.attrs = adt7470_attr;
	if ((err = sysfs_create_group(&client->dev.kobj, &data->attrs)))
		goto exit_detach;

	data->hwmon_dev = hwmon_device_register(&client->dev);
	if (IS_ERR(data->hwmon_dev)) {
		err = PTR_ERR(data->hwmon_dev);
		goto exit_remove;
	}

	return 0;

exit_remove:
	sysfs_remove_group(&client->dev.kobj, &data->attrs);
exit_detach:
	i2c_detach_client(client);
exit_free:
	kfree(data);
exit:
	return err;
}

static int adt7470_detach_client(struct i2c_client *client)
{
	struct adt7470_data *data = i2c_get_clientdata(client);

	hwmon_device_unregister(data->hwmon_dev);
	sysfs_remove_group(&client->dev.kobj, &data->attrs);
	i2c_detach_client(client);
	kfree(data);
	return 0;
}

static int __init adt7470_init(void)
{
	return i2c_add_driver(&adt7470_driver);
}

static void __exit adt7470_exit(void)
{
	i2c_del_driver(&adt7470_driver);
}

MODULE_AUTHOR("Darrick J. Wong <djwong@us.ibm.com>");
MODULE_DESCRIPTION("ADT7470 driver");
MODULE_LICENSE("GPL");

module_init(adt7470_init);
module_exit(adt7470_exit);
