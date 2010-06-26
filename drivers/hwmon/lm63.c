/*
 * lm63.c - driver for the National Semiconductor LM63 temperature sensor
 *          with integrated fan control
 * Copyright (C) 2004-2008  Jean Delvare <khali@linux-fr.org>
 * Based on the lm90 driver.
 *
 * The LM63 is a sensor chip made by National Semiconductor. It measures
 * two temperatures (its own and one external one) and the speed of one
 * fan, those speed it can additionally control. Complete datasheet can be
 * obtained from National's website at:
 *   http://www.national.com/pf/LM/LM63.html
 *
 * The LM63 is basically an LM86 with fan speed monitoring and control
 * capabilities added. It misses some of the LM86 features though:
 *  - No low limit for local temperature.
 *  - No critical limit for local temperature.
 *  - Critical limit for remote temperature can be changed only once. We
 *    will consider that the critical limit is read-only.
 *
 * The datasheet isn't very clear about what the tachometer reading is.
 * I had a explanation from National Semiconductor though. The two lower
 * bits of the read value have to be masked out. The value is still 16 bit
 * in width.
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
#include <linux/hwmon-sysfs.h>
#include <linux/hwmon.h>
#include <linux/err.h>
#include <linux/mutex.h>
#include <linux/sysfs.h>

/*
 * Addresses to scan
 * Address is fully defined internally and cannot be changed.
 */

static const unsigned short normal_i2c[] = { 0x18, 0x4c, 0x4e, I2C_CLIENT_END };

/*
 * The LM63 registers
 */

#define LM63_REG_CONFIG1		0x03
#define LM63_REG_CONFIG2		0xBF
#define LM63_REG_CONFIG_FAN		0x4A

#define LM63_REG_TACH_COUNT_MSB		0x47
#define LM63_REG_TACH_COUNT_LSB		0x46
#define LM63_REG_TACH_LIMIT_MSB		0x49
#define LM63_REG_TACH_LIMIT_LSB		0x48

#define LM63_REG_PWM_VALUE		0x4C
#define LM63_REG_PWM_FREQ		0x4D

#define LM63_REG_LOCAL_TEMP		0x00
#define LM63_REG_LOCAL_HIGH		0x05

#define LM63_REG_REMOTE_TEMP_MSB	0x01
#define LM63_REG_REMOTE_TEMP_LSB	0x10
#define LM63_REG_REMOTE_OFFSET_MSB	0x11
#define LM63_REG_REMOTE_OFFSET_LSB	0x12
#define LM63_REG_REMOTE_HIGH_MSB	0x07
#define LM63_REG_REMOTE_HIGH_LSB	0x13
#define LM63_REG_REMOTE_LOW_MSB		0x08
#define LM63_REG_REMOTE_LOW_LSB		0x14
#define LM63_REG_REMOTE_TCRIT		0x19
#define LM63_REG_REMOTE_TCRIT_HYST	0x21

#define LM63_REG_ALERT_STATUS		0x02
#define LM63_REG_ALERT_MASK		0x16

#define LM63_REG_MAN_ID			0xFE
#define LM63_REG_CHIP_ID		0xFF

/*
 * Conversions and various macros
 * For tachometer counts, the LM63 uses 16-bit values.
 * For local temperature and high limit, remote critical limit and hysteresis
 * value, it uses signed 8-bit values with LSB = 1 degree Celsius.
 * For remote temperature, low and high limits, it uses signed 11-bit values
 * with LSB = 0.125 degree Celsius, left-justified in 16-bit registers.
 */

#define FAN_FROM_REG(reg)	((reg) == 0xFFFC || (reg) == 0 ? 0 : \
				 5400000 / (reg))
#define FAN_TO_REG(val)		((val) <= 82 ? 0xFFFC : \
				 (5400000 / (val)) & 0xFFFC)
#define TEMP8_FROM_REG(reg)	((reg) * 1000)
#define TEMP8_TO_REG(val)	((val) <= -128000 ? -128 : \
				 (val) >= 127000 ? 127 : \
				 (val) < 0 ? ((val) - 500) / 1000 : \
				 ((val) + 500) / 1000)
#define TEMP11_FROM_REG(reg)	((reg) / 32 * 125)
#define TEMP11_TO_REG(val)	((val) <= -128000 ? 0x8000 : \
				 (val) >= 127875 ? 0x7FE0 : \
				 (val) < 0 ? ((val) - 62) / 125 * 32 : \
				 ((val) + 62) / 125 * 32)
#define HYST_TO_REG(val)	((val) <= 0 ? 0 : \
				 (val) >= 127000 ? 127 : \
				 ((val) + 500) / 1000)

/*
 * Functions declaration
 */

static int lm63_probe(struct i2c_client *client,
		      const struct i2c_device_id *id);
static int lm63_remove(struct i2c_client *client);

static struct lm63_data *lm63_update_device(struct device *dev);

static int lm63_detect(struct i2c_client *client, struct i2c_board_info *info);
static void lm63_init_client(struct i2c_client *client);

enum chips { lm63, lm64 };

/*
 * Driver data (common to all clients)
 */

static const struct i2c_device_id lm63_id[] = {
	{ "lm63", lm63 },
	{ "lm64", lm64 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, lm63_id);

static struct i2c_driver lm63_driver = {
	.class		= I2C_CLASS_HWMON,
	.driver = {
		.name	= "lm63",
	},
	.probe		= lm63_probe,
	.remove		= lm63_remove,
	.id_table	= lm63_id,
	.detect		= lm63_detect,
	.address_list	= normal_i2c,
};

/*
 * Client data (each client gets its own)
 */

struct lm63_data {
	struct device *hwmon_dev;
	struct mutex update_lock;
	char valid; /* zero until following fields are valid */
	unsigned long last_updated; /* in jiffies */

	/* registers values */
	u8 config, config_fan;
	u16 fan[2];	/* 0: input
			   1: low limit */
	u8 pwm1_freq;
	u8 pwm1_value;
	s8 temp8[3];	/* 0: local input
			   1: local high limit
			   2: remote critical limit */
	s16 temp11[3];	/* 0: remote input
			   1: remote low limit
			   2: remote high limit */
	u8 temp2_crit_hyst;
	u8 alarms;
};

/*
 * Sysfs callback functions and files
 */

static ssize_t show_fan(struct device *dev, struct device_attribute *devattr,
			char *buf)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	struct lm63_data *data = lm63_update_device(dev);
	return sprintf(buf, "%d\n", FAN_FROM_REG(data->fan[attr->index]));
}

static ssize_t set_fan(struct device *dev, struct device_attribute *dummy,
		       const char *buf, size_t count)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct lm63_data *data = i2c_get_clientdata(client);
	unsigned long val = simple_strtoul(buf, NULL, 10);

	mutex_lock(&data->update_lock);
	data->fan[1] = FAN_TO_REG(val);
	i2c_smbus_write_byte_data(client, LM63_REG_TACH_LIMIT_LSB,
				  data->fan[1] & 0xFF);
	i2c_smbus_write_byte_data(client, LM63_REG_TACH_LIMIT_MSB,
				  data->fan[1] >> 8);
	mutex_unlock(&data->update_lock);
	return count;
}

static ssize_t show_pwm1(struct device *dev, struct device_attribute *dummy,
			 char *buf)
{
	struct lm63_data *data = lm63_update_device(dev);
	return sprintf(buf, "%d\n", data->pwm1_value >= 2 * data->pwm1_freq ?
		       255 : (data->pwm1_value * 255 + data->pwm1_freq) /
		       (2 * data->pwm1_freq));
}

static ssize_t set_pwm1(struct device *dev, struct device_attribute *dummy,
			const char *buf, size_t count)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct lm63_data *data = i2c_get_clientdata(client);
	unsigned long val;
	
	if (!(data->config_fan & 0x20)) /* register is read-only */
		return -EPERM;

	val = simple_strtoul(buf, NULL, 10);
	mutex_lock(&data->update_lock);
	data->pwm1_value = val <= 0 ? 0 :
			   val >= 255 ? 2 * data->pwm1_freq :
			   (val * data->pwm1_freq * 2 + 127) / 255;
	i2c_smbus_write_byte_data(client, LM63_REG_PWM_VALUE, data->pwm1_value);
	mutex_unlock(&data->update_lock);
	return count;
}

static ssize_t show_pwm1_enable(struct device *dev, struct device_attribute *dummy,
				char *buf)
{
	struct lm63_data *data = lm63_update_device(dev);
	return sprintf(buf, "%d\n", data->config_fan & 0x20 ? 1 : 2);
}

static ssize_t show_temp8(struct device *dev, struct device_attribute *devattr,
			  char *buf)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	struct lm63_data *data = lm63_update_device(dev);
	return sprintf(buf, "%d\n", TEMP8_FROM_REG(data->temp8[attr->index]));
}

static ssize_t set_temp8(struct device *dev, struct device_attribute *dummy,
			 const char *buf, size_t count)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct lm63_data *data = i2c_get_clientdata(client);
	long val = simple_strtol(buf, NULL, 10);

	mutex_lock(&data->update_lock);
	data->temp8[1] = TEMP8_TO_REG(val);
	i2c_smbus_write_byte_data(client, LM63_REG_LOCAL_HIGH, data->temp8[1]);
	mutex_unlock(&data->update_lock);
	return count;
}

static ssize_t show_temp11(struct device *dev, struct device_attribute *devattr,
			   char *buf)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	struct lm63_data *data = lm63_update_device(dev);
	return sprintf(buf, "%d\n", TEMP11_FROM_REG(data->temp11[attr->index]));
}

static ssize_t set_temp11(struct device *dev, struct device_attribute *devattr,
			  const char *buf, size_t count)
{
	static const u8 reg[4] = {
		LM63_REG_REMOTE_LOW_MSB,
		LM63_REG_REMOTE_LOW_LSB,
		LM63_REG_REMOTE_HIGH_MSB,
		LM63_REG_REMOTE_HIGH_LSB,
	};

	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	struct i2c_client *client = to_i2c_client(dev);
	struct lm63_data *data = i2c_get_clientdata(client);
	long val = simple_strtol(buf, NULL, 10);
	int nr = attr->index;

	mutex_lock(&data->update_lock);
	data->temp11[nr] = TEMP11_TO_REG(val);
	i2c_smbus_write_byte_data(client, reg[(nr - 1) * 2],
				  data->temp11[nr] >> 8);
	i2c_smbus_write_byte_data(client, reg[(nr - 1) * 2 + 1],
				  data->temp11[nr] & 0xff);
	mutex_unlock(&data->update_lock);
	return count;
}

/* Hysteresis register holds a relative value, while we want to present
   an absolute to user-space */
static ssize_t show_temp2_crit_hyst(struct device *dev, struct device_attribute *dummy,
				    char *buf)
{
	struct lm63_data *data = lm63_update_device(dev);
	return sprintf(buf, "%d\n", TEMP8_FROM_REG(data->temp8[2])
		       - TEMP8_FROM_REG(data->temp2_crit_hyst));
}

/* And now the other way around, user-space provides an absolute
   hysteresis value and we have to store a relative one */
static ssize_t set_temp2_crit_hyst(struct device *dev, struct device_attribute *dummy,
				   const char *buf, size_t count)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct lm63_data *data = i2c_get_clientdata(client);
	long val = simple_strtol(buf, NULL, 10);
	long hyst;

	mutex_lock(&data->update_lock);
	hyst = TEMP8_FROM_REG(data->temp8[2]) - val;
	i2c_smbus_write_byte_data(client, LM63_REG_REMOTE_TCRIT_HYST,
				  HYST_TO_REG(hyst));
	mutex_unlock(&data->update_lock);
	return count;
}

static ssize_t show_alarms(struct device *dev, struct device_attribute *dummy,
			   char *buf)
{
	struct lm63_data *data = lm63_update_device(dev);
	return sprintf(buf, "%u\n", data->alarms);
}

static ssize_t show_alarm(struct device *dev, struct device_attribute *devattr,
			  char *buf)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	struct lm63_data *data = lm63_update_device(dev);
	int bitnr = attr->index;

	return sprintf(buf, "%u\n", (data->alarms >> bitnr) & 1);
}

static SENSOR_DEVICE_ATTR(fan1_input, S_IRUGO, show_fan, NULL, 0);
static SENSOR_DEVICE_ATTR(fan1_min, S_IWUSR | S_IRUGO, show_fan,
	set_fan, 1);

static DEVICE_ATTR(pwm1, S_IWUSR | S_IRUGO, show_pwm1, set_pwm1);
static DEVICE_ATTR(pwm1_enable, S_IRUGO, show_pwm1_enable, NULL);

static SENSOR_DEVICE_ATTR(temp1_input, S_IRUGO, show_temp8, NULL, 0);
static SENSOR_DEVICE_ATTR(temp1_max, S_IWUSR | S_IRUGO, show_temp8,
	set_temp8, 1);

static SENSOR_DEVICE_ATTR(temp2_input, S_IRUGO, show_temp11, NULL, 0);
static SENSOR_DEVICE_ATTR(temp2_min, S_IWUSR | S_IRUGO, show_temp11,
	set_temp11, 1);
static SENSOR_DEVICE_ATTR(temp2_max, S_IWUSR | S_IRUGO, show_temp11,
	set_temp11, 2);
static SENSOR_DEVICE_ATTR(temp2_crit, S_IRUGO, show_temp8, NULL, 2);
static DEVICE_ATTR(temp2_crit_hyst, S_IWUSR | S_IRUGO, show_temp2_crit_hyst,
	set_temp2_crit_hyst);

/* Individual alarm files */
static SENSOR_DEVICE_ATTR(fan1_min_alarm, S_IRUGO, show_alarm, NULL, 0);
static SENSOR_DEVICE_ATTR(temp2_crit_alarm, S_IRUGO, show_alarm, NULL, 1);
static SENSOR_DEVICE_ATTR(temp2_fault, S_IRUGO, show_alarm, NULL, 2);
static SENSOR_DEVICE_ATTR(temp2_min_alarm, S_IRUGO, show_alarm, NULL, 3);
static SENSOR_DEVICE_ATTR(temp2_max_alarm, S_IRUGO, show_alarm, NULL, 4);
static SENSOR_DEVICE_ATTR(temp1_max_alarm, S_IRUGO, show_alarm, NULL, 6);
/* Raw alarm file for compatibility */
static DEVICE_ATTR(alarms, S_IRUGO, show_alarms, NULL);

static struct attribute *lm63_attributes[] = {
	&dev_attr_pwm1.attr,
	&dev_attr_pwm1_enable.attr,
	&sensor_dev_attr_temp1_input.dev_attr.attr,
	&sensor_dev_attr_temp2_input.dev_attr.attr,
	&sensor_dev_attr_temp2_min.dev_attr.attr,
	&sensor_dev_attr_temp1_max.dev_attr.attr,
	&sensor_dev_attr_temp2_max.dev_attr.attr,
	&sensor_dev_attr_temp2_crit.dev_attr.attr,
	&dev_attr_temp2_crit_hyst.attr,

	&sensor_dev_attr_temp2_crit_alarm.dev_attr.attr,
	&sensor_dev_attr_temp2_fault.dev_attr.attr,
	&sensor_dev_attr_temp2_min_alarm.dev_attr.attr,
	&sensor_dev_attr_temp2_max_alarm.dev_attr.attr,
	&sensor_dev_attr_temp1_max_alarm.dev_attr.attr,
	&dev_attr_alarms.attr,
	NULL
};

static const struct attribute_group lm63_group = {
	.attrs = lm63_attributes,
};

static struct attribute *lm63_attributes_fan1[] = {
	&sensor_dev_attr_fan1_input.dev_attr.attr,
	&sensor_dev_attr_fan1_min.dev_attr.attr,

	&sensor_dev_attr_fan1_min_alarm.dev_attr.attr,
	NULL
};

static const struct attribute_group lm63_group_fan1 = {
	.attrs = lm63_attributes_fan1,
};

/*
 * Real code
 */

/* Return 0 if detection is successful, -ENODEV otherwise */
static int lm63_detect(struct i2c_client *new_client,
		       struct i2c_board_info *info)
{
	struct i2c_adapter *adapter = new_client->adapter;
	u8 man_id, chip_id, reg_config1, reg_config2;
	u8 reg_alert_status, reg_alert_mask;
	int address = new_client->addr;

	if (!i2c_check_functionality(adapter, I2C_FUNC_SMBUS_BYTE_DATA))
		return -ENODEV;

	man_id = i2c_smbus_read_byte_data(new_client, LM63_REG_MAN_ID);
	chip_id = i2c_smbus_read_byte_data(new_client, LM63_REG_CHIP_ID);

	reg_config1 = i2c_smbus_read_byte_data(new_client,
		      LM63_REG_CONFIG1);
	reg_config2 = i2c_smbus_read_byte_data(new_client,
		      LM63_REG_CONFIG2);
	reg_alert_status = i2c_smbus_read_byte_data(new_client,
			   LM63_REG_ALERT_STATUS);
	reg_alert_mask = i2c_smbus_read_byte_data(new_client,
			 LM63_REG_ALERT_MASK);

	if (man_id != 0x01 /* National Semiconductor */
	 || (reg_config1 & 0x18) != 0x00
	 || (reg_config2 & 0xF8) != 0x00
	 || (reg_alert_status & 0x20) != 0x00
	 || (reg_alert_mask & 0xA4) != 0xA4) {
		dev_dbg(&adapter->dev,
			"Unsupported chip (man_id=0x%02X, chip_id=0x%02X)\n",
			man_id, chip_id);
		return -ENODEV;
	}

	if (chip_id == 0x41 && address == 0x4c)
		strlcpy(info->type, "lm63", I2C_NAME_SIZE);
	else if (chip_id == 0x51 && (address == 0x18 || address == 0x4e))
		strlcpy(info->type, "lm64", I2C_NAME_SIZE);
	else
		return -ENODEV;

	return 0;
}

static int lm63_probe(struct i2c_client *new_client,
		      const struct i2c_device_id *id)
{
	struct lm63_data *data;
	int err;

	data = kzalloc(sizeof(struct lm63_data), GFP_KERNEL);
	if (!data) {
		err = -ENOMEM;
		goto exit;
	}

	i2c_set_clientdata(new_client, data);
	data->valid = 0;
	mutex_init(&data->update_lock);

	/* Initialize the LM63 chip */
	lm63_init_client(new_client);

	/* Register sysfs hooks */
	if ((err = sysfs_create_group(&new_client->dev.kobj,
				      &lm63_group)))
		goto exit_free;
	if (data->config & 0x04) { /* tachometer enabled */
		if ((err = sysfs_create_group(&new_client->dev.kobj,
					      &lm63_group_fan1)))
			goto exit_remove_files;
	}

	data->hwmon_dev = hwmon_device_register(&new_client->dev);
	if (IS_ERR(data->hwmon_dev)) {
		err = PTR_ERR(data->hwmon_dev);
		goto exit_remove_files;
	}

	return 0;

exit_remove_files:
	sysfs_remove_group(&new_client->dev.kobj, &lm63_group);
	sysfs_remove_group(&new_client->dev.kobj, &lm63_group_fan1);
exit_free:
	kfree(data);
exit:
	return err;
}

/* Idealy we shouldn't have to initialize anything, since the BIOS
   should have taken care of everything */
static void lm63_init_client(struct i2c_client *client)
{
	struct lm63_data *data = i2c_get_clientdata(client);

	data->config = i2c_smbus_read_byte_data(client, LM63_REG_CONFIG1);
	data->config_fan = i2c_smbus_read_byte_data(client,
						    LM63_REG_CONFIG_FAN);

	/* Start converting if needed */
	if (data->config & 0x40) { /* standby */
		dev_dbg(&client->dev, "Switching to operational mode\n");
		data->config &= 0xA7;
		i2c_smbus_write_byte_data(client, LM63_REG_CONFIG1,
					  data->config);
	}

	/* We may need pwm1_freq before ever updating the client data */
	data->pwm1_freq = i2c_smbus_read_byte_data(client, LM63_REG_PWM_FREQ);
	if (data->pwm1_freq == 0)
		data->pwm1_freq = 1;

	/* Show some debug info about the LM63 configuration */
	dev_dbg(&client->dev, "Alert/tach pin configured for %s\n",
		(data->config & 0x04) ? "tachometer input" :
		"alert output");
	dev_dbg(&client->dev, "PWM clock %s kHz, output frequency %u Hz\n",
		(data->config_fan & 0x08) ? "1.4" : "360",
		((data->config_fan & 0x08) ? 700 : 180000) / data->pwm1_freq);
	dev_dbg(&client->dev, "PWM output active %s, %s mode\n",
		(data->config_fan & 0x10) ? "low" : "high",
		(data->config_fan & 0x20) ? "manual" : "auto");
}

static int lm63_remove(struct i2c_client *client)
{
	struct lm63_data *data = i2c_get_clientdata(client);

	hwmon_device_unregister(data->hwmon_dev);
	sysfs_remove_group(&client->dev.kobj, &lm63_group);
	sysfs_remove_group(&client->dev.kobj, &lm63_group_fan1);

	kfree(data);
	return 0;
}

static struct lm63_data *lm63_update_device(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct lm63_data *data = i2c_get_clientdata(client);

	mutex_lock(&data->update_lock);

	if (time_after(jiffies, data->last_updated + HZ) || !data->valid) {
		if (data->config & 0x04) { /* tachometer enabled  */
			/* order matters for fan1_input */
			data->fan[0] = i2c_smbus_read_byte_data(client,
				       LM63_REG_TACH_COUNT_LSB) & 0xFC;
			data->fan[0] |= i2c_smbus_read_byte_data(client,
					LM63_REG_TACH_COUNT_MSB) << 8;
			data->fan[1] = (i2c_smbus_read_byte_data(client,
					LM63_REG_TACH_LIMIT_LSB) & 0xFC)
				     | (i2c_smbus_read_byte_data(client,
					LM63_REG_TACH_LIMIT_MSB) << 8);
		}

		data->pwm1_freq = i2c_smbus_read_byte_data(client,
				  LM63_REG_PWM_FREQ);
		if (data->pwm1_freq == 0)
			data->pwm1_freq = 1;
		data->pwm1_value = i2c_smbus_read_byte_data(client,
				   LM63_REG_PWM_VALUE);

		data->temp8[0] = i2c_smbus_read_byte_data(client,
				 LM63_REG_LOCAL_TEMP);
		data->temp8[1] = i2c_smbus_read_byte_data(client,
				 LM63_REG_LOCAL_HIGH);

		/* order matters for temp2_input */
		data->temp11[0] = i2c_smbus_read_byte_data(client,
				  LM63_REG_REMOTE_TEMP_MSB) << 8;
		data->temp11[0] |= i2c_smbus_read_byte_data(client,
				   LM63_REG_REMOTE_TEMP_LSB);
		data->temp11[1] = (i2c_smbus_read_byte_data(client,
				  LM63_REG_REMOTE_LOW_MSB) << 8)
				| i2c_smbus_read_byte_data(client,
				  LM63_REG_REMOTE_LOW_LSB);
		data->temp11[2] = (i2c_smbus_read_byte_data(client,
				  LM63_REG_REMOTE_HIGH_MSB) << 8)
				| i2c_smbus_read_byte_data(client,
				  LM63_REG_REMOTE_HIGH_LSB);
		data->temp8[2] = i2c_smbus_read_byte_data(client,
				 LM63_REG_REMOTE_TCRIT);
		data->temp2_crit_hyst = i2c_smbus_read_byte_data(client,
					LM63_REG_REMOTE_TCRIT_HYST);

		data->alarms = i2c_smbus_read_byte_data(client,
			       LM63_REG_ALERT_STATUS) & 0x7F;

		data->last_updated = jiffies;
		data->valid = 1;
	}

	mutex_unlock(&data->update_lock);

	return data;
}

static int __init sensors_lm63_init(void)
{
	return i2c_add_driver(&lm63_driver);
}

static void __exit sensors_lm63_exit(void)
{
	i2c_del_driver(&lm63_driver);
}

MODULE_AUTHOR("Jean Delvare <khali@linux-fr.org>");
MODULE_DESCRIPTION("LM63 driver");
MODULE_LICENSE("GPL");

module_init(sensors_lm63_init);
module_exit(sensors_lm63_exit);
