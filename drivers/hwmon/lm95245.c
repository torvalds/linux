/*
 * Copyright (C) 2011 Alexander Stein <alexander.stein@systec-electronic.com>
 *
 * The LM95245 is a sensor chip made by National Semiconductors.
 * It reports up to two temperatures (its own plus an external one).
 * Complete datasheet can be obtained from National's website at:
 *   http://www.national.com/ds.cgi/LM/LM95245.pdf
 *
 * This driver is based on lm95241.c
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
#include <linux/sysfs.h>

#define DEVNAME "lm95245"

static const unsigned short normal_i2c[] = {
	0x18, 0x19, 0x29, 0x4c, 0x4d, I2C_CLIENT_END };

/* LM95245 registers */
/* general registers */
#define LM95245_REG_RW_CONFIG1		0x03
#define LM95245_REG_RW_CONVERS_RATE	0x04
#define LM95245_REG_W_ONE_SHOT		0x0F

/* diode configuration */
#define LM95245_REG_RW_CONFIG2		0xBF
#define LM95245_REG_RW_REMOTE_OFFH	0x11
#define LM95245_REG_RW_REMOTE_OFFL	0x12

/* status registers */
#define LM95245_REG_R_STATUS1		0x02
#define LM95245_REG_R_STATUS2		0x33

/* limit registers */
#define LM95245_REG_RW_REMOTE_OS_LIMIT		0x07
#define LM95245_REG_RW_LOCAL_OS_TCRIT_LIMIT	0x20
#define LM95245_REG_RW_REMOTE_TCRIT_LIMIT	0x19
#define LM95245_REG_RW_COMMON_HYSTERESIS	0x21

/* temperature signed */
#define LM95245_REG_R_LOCAL_TEMPH_S	0x00
#define LM95245_REG_R_LOCAL_TEMPL_S	0x30
#define LM95245_REG_R_REMOTE_TEMPH_S	0x01
#define LM95245_REG_R_REMOTE_TEMPL_S	0x10
/* temperature unsigned */
#define LM95245_REG_R_REMOTE_TEMPH_U	0x31
#define LM95245_REG_R_REMOTE_TEMPL_U	0x32

/* id registers */
#define LM95245_REG_R_MAN_ID		0xFE
#define LM95245_REG_R_CHIP_ID		0xFF

/* LM95245 specific bitfields */
#define CFG_STOP		0x40
#define CFG_REMOTE_TCRIT_MASK	0x10
#define CFG_REMOTE_OS_MASK	0x08
#define CFG_LOCAL_TCRIT_MASK	0x04
#define CFG_LOCAL_OS_MASK	0x02

#define CFG2_OS_A0		0x40
#define CFG2_DIODE_FAULT_OS	0x20
#define CFG2_DIODE_FAULT_TCRIT	0x10
#define CFG2_REMOTE_TT		0x08
#define CFG2_REMOTE_FILTER_DIS	0x00
#define CFG2_REMOTE_FILTER_EN	0x06

/* conversation rate in ms */
#define RATE_CR0063	0x00
#define RATE_CR0364	0x01
#define RATE_CR1000	0x02
#define RATE_CR2500	0x03

#define STATUS1_DIODE_FAULT	0x04
#define STATUS1_RTCRIT		0x02
#define STATUS1_LOC		0x01

#define MANUFACTURER_ID		0x01
#define DEFAULT_REVISION	0xB3

static const u8 lm95245_reg_address[] = {
	LM95245_REG_R_LOCAL_TEMPH_S,
	LM95245_REG_R_LOCAL_TEMPL_S,
	LM95245_REG_R_REMOTE_TEMPH_S,
	LM95245_REG_R_REMOTE_TEMPL_S,
	LM95245_REG_R_REMOTE_TEMPH_U,
	LM95245_REG_R_REMOTE_TEMPL_U,
	LM95245_REG_RW_LOCAL_OS_TCRIT_LIMIT,
	LM95245_REG_RW_REMOTE_TCRIT_LIMIT,
	LM95245_REG_RW_COMMON_HYSTERESIS,
	LM95245_REG_R_STATUS1,
};

/* Client data (each client gets its own) */
struct lm95245_data {
	struct device *hwmon_dev;
	struct mutex update_lock;
	unsigned long last_updated;	/* in jiffies */
	unsigned long interval;	/* in msecs */
	bool valid;		/* zero until following fields are valid */
	/* registers values */
	u8 regs[ARRAY_SIZE(lm95245_reg_address)];
	u8 config1, config2;
};

/* Conversions */
static int temp_from_reg_unsigned(u8 val_h, u8 val_l)
{
	return val_h * 1000 + val_l * 1000 / 256;
}

static int temp_from_reg_signed(u8 val_h, u8 val_l)
{
	if (val_h & 0x80)
		return (val_h - 0x100) * 1000;
	return temp_from_reg_unsigned(val_h, val_l);
}

static struct lm95245_data *lm95245_update_device(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct lm95245_data *data = i2c_get_clientdata(client);

	mutex_lock(&data->update_lock);

	if (time_after(jiffies, data->last_updated
		+ msecs_to_jiffies(data->interval)) || !data->valid) {
		int i;

		dev_dbg(&client->dev, "Updating lm95245 data.\n");
		for (i = 0; i < ARRAY_SIZE(lm95245_reg_address); i++)
			data->regs[i]
			  = i2c_smbus_read_byte_data(client,
						     lm95245_reg_address[i]);
		data->last_updated = jiffies;
		data->valid = 1;
	}

	mutex_unlock(&data->update_lock);

	return data;
}

static unsigned long lm95245_read_conversion_rate(struct i2c_client *client)
{
	int rate;
	unsigned long interval;

	rate = i2c_smbus_read_byte_data(client, LM95245_REG_RW_CONVERS_RATE);

	switch (rate) {
	case RATE_CR0063:
		interval = 63;
		break;
	case RATE_CR0364:
		interval = 364;
		break;
	case RATE_CR1000:
		interval = 1000;
		break;
	case RATE_CR2500:
	default:
		interval = 2500;
		break;
	}

	return interval;
}

static unsigned long lm95245_set_conversion_rate(struct i2c_client *client,
			unsigned long interval)
{
	int rate;

	if (interval <= 63) {
		interval = 63;
		rate = RATE_CR0063;
	} else if (interval <= 364) {
		interval = 364;
		rate = RATE_CR0364;
	} else if (interval <= 1000) {
		interval = 1000;
		rate = RATE_CR1000;
	} else {
		interval = 2500;
		rate = RATE_CR2500;
	}

	i2c_smbus_write_byte_data(client, LM95245_REG_RW_CONVERS_RATE, rate);

	return interval;
}

/* Sysfs stuff */
static ssize_t show_input(struct device *dev, struct device_attribute *attr,
			  char *buf)
{
	struct lm95245_data *data = lm95245_update_device(dev);
	int temp;
	int index = to_sensor_dev_attr(attr)->index;

	/*
	 * Index 0 (Local temp) is always signed
	 * Index 2 (Remote temp) has both signed and unsigned data
	 * use signed calculation for remote if signed bit is set
	 */
	if (index == 0 || data->regs[index] & 0x80)
		temp = temp_from_reg_signed(data->regs[index],
			    data->regs[index + 1]);
	else
		temp = temp_from_reg_unsigned(data->regs[index + 2],
			    data->regs[index + 3]);

	return snprintf(buf, PAGE_SIZE - 1, "%d\n", temp);
}

static ssize_t show_limit(struct device *dev, struct device_attribute *attr,
			 char *buf)
{
	struct lm95245_data *data = lm95245_update_device(dev);
	int index = to_sensor_dev_attr(attr)->index;

	return snprintf(buf, PAGE_SIZE - 1, "%d\n",
			data->regs[index] * 1000);
}

static ssize_t set_limit(struct device *dev, struct device_attribute *attr,
			const char *buf, size_t count)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct lm95245_data *data = i2c_get_clientdata(client);
	int index = to_sensor_dev_attr(attr)->index;
	unsigned long val;

	if (kstrtoul(buf, 10, &val) < 0)
		return -EINVAL;

	val /= 1000;

	val = clamp_val(val, 0, (index == 6 ? 127 : 255));

	mutex_lock(&data->update_lock);

	data->valid = 0;

	i2c_smbus_write_byte_data(client, lm95245_reg_address[index], val);

	mutex_unlock(&data->update_lock);

	return count;
}

static ssize_t set_crit_hyst(struct device *dev, struct device_attribute *attr,
			const char *buf, size_t count)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct lm95245_data *data = i2c_get_clientdata(client);
	unsigned long val;

	if (kstrtoul(buf, 10, &val) < 0)
		return -EINVAL;

	val /= 1000;

	val = clamp_val(val, 0, 31);

	mutex_lock(&data->update_lock);

	data->valid = 0;

	/* shared crit hysteresis */
	i2c_smbus_write_byte_data(client, LM95245_REG_RW_COMMON_HYSTERESIS,
		val);

	mutex_unlock(&data->update_lock);

	return count;
}

static ssize_t show_type(struct device *dev, struct device_attribute *attr,
			 char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct lm95245_data *data = i2c_get_clientdata(client);

	return snprintf(buf, PAGE_SIZE - 1,
		data->config2 & CFG2_REMOTE_TT ? "1\n" : "2\n");
}

static ssize_t set_type(struct device *dev, struct device_attribute *attr,
			const char *buf, size_t count)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct lm95245_data *data = i2c_get_clientdata(client);
	unsigned long val;

	if (kstrtoul(buf, 10, &val) < 0)
		return -EINVAL;
	if (val != 1 && val != 2)
		return -EINVAL;

	mutex_lock(&data->update_lock);

	if (val == 1)
		data->config2 |= CFG2_REMOTE_TT;
	else
		data->config2 &= ~CFG2_REMOTE_TT;

	data->valid = 0;

	i2c_smbus_write_byte_data(client, LM95245_REG_RW_CONFIG2,
				  data->config2);

	mutex_unlock(&data->update_lock);

	return count;
}

static ssize_t show_alarm(struct device *dev, struct device_attribute *attr,
			 char *buf)
{
	struct lm95245_data *data = lm95245_update_device(dev);
	int index = to_sensor_dev_attr(attr)->index;

	return snprintf(buf, PAGE_SIZE - 1, "%d\n",
			!!(data->regs[9] & index));
}

static ssize_t show_interval(struct device *dev, struct device_attribute *attr,
			     char *buf)
{
	struct lm95245_data *data = lm95245_update_device(dev);

	return snprintf(buf, PAGE_SIZE - 1, "%lu\n", data->interval);
}

static ssize_t set_interval(struct device *dev, struct device_attribute *attr,
			    const char *buf, size_t count)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct lm95245_data *data = i2c_get_clientdata(client);
	unsigned long val;

	if (kstrtoul(buf, 10, &val) < 0)
		return -EINVAL;

	mutex_lock(&data->update_lock);

	data->interval = lm95245_set_conversion_rate(client, val);

	mutex_unlock(&data->update_lock);

	return count;
}

static SENSOR_DEVICE_ATTR(temp1_input, S_IRUGO, show_input, NULL, 0);
static SENSOR_DEVICE_ATTR(temp1_crit, S_IWUSR | S_IRUGO, show_limit,
		set_limit, 6);
static SENSOR_DEVICE_ATTR(temp1_crit_hyst, S_IWUSR | S_IRUGO, show_limit,
		set_crit_hyst, 8);
static SENSOR_DEVICE_ATTR(temp1_crit_alarm, S_IRUGO, show_alarm, NULL,
		STATUS1_LOC);

static SENSOR_DEVICE_ATTR(temp2_input, S_IRUGO, show_input, NULL, 2);
static SENSOR_DEVICE_ATTR(temp2_crit, S_IWUSR | S_IRUGO, show_limit,
		set_limit, 7);
static SENSOR_DEVICE_ATTR(temp2_crit_hyst, S_IWUSR | S_IRUGO, show_limit,
		set_crit_hyst, 8);
static SENSOR_DEVICE_ATTR(temp2_crit_alarm, S_IRUGO, show_alarm, NULL,
		STATUS1_RTCRIT);
static SENSOR_DEVICE_ATTR(temp2_type, S_IWUSR | S_IRUGO, show_type,
		set_type, 0);
static SENSOR_DEVICE_ATTR(temp2_fault, S_IRUGO, show_alarm, NULL,
		STATUS1_DIODE_FAULT);

static DEVICE_ATTR(update_interval, S_IWUSR | S_IRUGO, show_interval,
		set_interval);

static struct attribute *lm95245_attributes[] = {
	&sensor_dev_attr_temp1_input.dev_attr.attr,
	&sensor_dev_attr_temp1_crit.dev_attr.attr,
	&sensor_dev_attr_temp1_crit_hyst.dev_attr.attr,
	&sensor_dev_attr_temp1_crit_alarm.dev_attr.attr,
	&sensor_dev_attr_temp2_input.dev_attr.attr,
	&sensor_dev_attr_temp2_crit.dev_attr.attr,
	&sensor_dev_attr_temp2_crit_hyst.dev_attr.attr,
	&sensor_dev_attr_temp2_crit_alarm.dev_attr.attr,
	&sensor_dev_attr_temp2_type.dev_attr.attr,
	&sensor_dev_attr_temp2_fault.dev_attr.attr,
	&dev_attr_update_interval.attr,
	NULL
};

static const struct attribute_group lm95245_group = {
	.attrs = lm95245_attributes,
};

/* Return 0 if detection is successful, -ENODEV otherwise */
static int lm95245_detect(struct i2c_client *new_client,
			  struct i2c_board_info *info)
{
	struct i2c_adapter *adapter = new_client->adapter;

	if (!i2c_check_functionality(adapter, I2C_FUNC_SMBUS_BYTE_DATA))
		return -ENODEV;

	if (i2c_smbus_read_byte_data(new_client, LM95245_REG_R_MAN_ID)
			!= MANUFACTURER_ID
		|| i2c_smbus_read_byte_data(new_client, LM95245_REG_R_CHIP_ID)
			!= DEFAULT_REVISION)
		return -ENODEV;

	strlcpy(info->type, DEVNAME, I2C_NAME_SIZE);
	return 0;
}

static void lm95245_init_client(struct i2c_client *client)
{
	struct lm95245_data *data = i2c_get_clientdata(client);

	data->valid = 0;
	data->interval = lm95245_read_conversion_rate(client);

	data->config1 = i2c_smbus_read_byte_data(client,
		LM95245_REG_RW_CONFIG1);
	data->config2 = i2c_smbus_read_byte_data(client,
		LM95245_REG_RW_CONFIG2);

	if (data->config1 & CFG_STOP) {
		/* Clear the standby bit */
		data->config1 &= ~CFG_STOP;
		i2c_smbus_write_byte_data(client, LM95245_REG_RW_CONFIG1,
			data->config1);
	}
}

static int lm95245_probe(struct i2c_client *new_client,
			 const struct i2c_device_id *id)
{
	struct lm95245_data *data;
	int err;

	data = devm_kzalloc(&new_client->dev, sizeof(struct lm95245_data),
			    GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	i2c_set_clientdata(new_client, data);
	mutex_init(&data->update_lock);

	/* Initialize the LM95245 chip */
	lm95245_init_client(new_client);

	/* Register sysfs hooks */
	err = sysfs_create_group(&new_client->dev.kobj, &lm95245_group);
	if (err)
		return err;

	data->hwmon_dev = hwmon_device_register(&new_client->dev);
	if (IS_ERR(data->hwmon_dev)) {
		err = PTR_ERR(data->hwmon_dev);
		goto exit_remove_files;
	}

	return 0;

exit_remove_files:
	sysfs_remove_group(&new_client->dev.kobj, &lm95245_group);
	return err;
}

static int lm95245_remove(struct i2c_client *client)
{
	struct lm95245_data *data = i2c_get_clientdata(client);

	hwmon_device_unregister(data->hwmon_dev);
	sysfs_remove_group(&client->dev.kobj, &lm95245_group);

	return 0;
}

/* Driver data (common to all clients) */
static const struct i2c_device_id lm95245_id[] = {
	{ DEVNAME, 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, lm95245_id);

static struct i2c_driver lm95245_driver = {
	.class		= I2C_CLASS_HWMON,
	.driver = {
		.name	= DEVNAME,
	},
	.probe		= lm95245_probe,
	.remove		= lm95245_remove,
	.id_table	= lm95245_id,
	.detect		= lm95245_detect,
	.address_list	= normal_i2c,
};

module_i2c_driver(lm95245_driver);

MODULE_AUTHOR("Alexander Stein <alexander.stein@systec-electronic.com>");
MODULE_DESCRIPTION("LM95245 sensor driver");
MODULE_LICENSE("GPL");
