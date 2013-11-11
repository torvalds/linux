/*
 * Copyright (C) 2008, 2010 Davide Rizzo <elpa.rizzo@gmail.com>
 *
 * The LM95241 is a sensor chip made by National Semiconductors.
 * It reports up to three temperatures (its own plus up to two external ones).
 * Complete datasheet can be obtained from National's website at:
 *   http://www.national.com/ds.cgi/LM/LM95241.pdf
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

#define DEVNAME "lm95241"

static const unsigned short normal_i2c[] = {
	0x19, 0x2a, 0x2b, I2C_CLIENT_END };

/* LM95241 registers */
#define LM95241_REG_R_MAN_ID		0xFE
#define LM95241_REG_R_CHIP_ID		0xFF
#define LM95241_REG_R_STATUS		0x02
#define LM95241_REG_RW_CONFIG		0x03
#define LM95241_REG_RW_REM_FILTER	0x06
#define LM95241_REG_RW_TRUTHERM		0x07
#define LM95241_REG_W_ONE_SHOT		0x0F
#define LM95241_REG_R_LOCAL_TEMPH	0x10
#define LM95241_REG_R_REMOTE1_TEMPH	0x11
#define LM95241_REG_R_REMOTE2_TEMPH	0x12
#define LM95241_REG_R_LOCAL_TEMPL	0x20
#define LM95241_REG_R_REMOTE1_TEMPL	0x21
#define LM95241_REG_R_REMOTE2_TEMPL	0x22
#define LM95241_REG_RW_REMOTE_MODEL	0x30

/* LM95241 specific bitfields */
#define CFG_STOP 0x40
#define CFG_CR0076 0x00
#define CFG_CR0182 0x10
#define CFG_CR1000 0x20
#define CFG_CR2700 0x30
#define R1MS_SHIFT 0
#define R2MS_SHIFT 2
#define R1MS_MASK (0x01 << (R1MS_SHIFT))
#define R2MS_MASK (0x01 << (R2MS_SHIFT))
#define R1DF_SHIFT 1
#define R2DF_SHIFT 2
#define R1DF_MASK (0x01 << (R1DF_SHIFT))
#define R2DF_MASK (0x01 << (R2DF_SHIFT))
#define R1FE_MASK 0x01
#define R2FE_MASK 0x05
#define TT1_SHIFT 0
#define TT2_SHIFT 4
#define TT_OFF 0
#define TT_ON 1
#define TT_MASK 7
#define NATSEMI_MAN_ID	0x01
#define LM95231_CHIP_ID	0xA1
#define LM95241_CHIP_ID	0xA4

static const u8 lm95241_reg_address[] = {
	LM95241_REG_R_LOCAL_TEMPH,
	LM95241_REG_R_LOCAL_TEMPL,
	LM95241_REG_R_REMOTE1_TEMPH,
	LM95241_REG_R_REMOTE1_TEMPL,
	LM95241_REG_R_REMOTE2_TEMPH,
	LM95241_REG_R_REMOTE2_TEMPL
};

/* Client data (each client gets its own) */
struct lm95241_data {
	struct device *hwmon_dev;
	struct mutex update_lock;
	unsigned long last_updated, interval;	/* in jiffies */
	char valid;		/* zero until following fields are valid */
	/* registers values */
	u8 temp[ARRAY_SIZE(lm95241_reg_address)];
	u8 config, model, trutherm;
};

/* Conversions */
static int temp_from_reg_signed(u8 val_h, u8 val_l)
{
	s16 val_hl = (val_h << 8) | val_l;
	return val_hl * 1000 / 256;
}

static int temp_from_reg_unsigned(u8 val_h, u8 val_l)
{
	u16 val_hl = (val_h << 8) | val_l;
	return val_hl * 1000 / 256;
}

static struct lm95241_data *lm95241_update_device(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct lm95241_data *data = i2c_get_clientdata(client);

	mutex_lock(&data->update_lock);

	if (time_after(jiffies, data->last_updated + data->interval) ||
	    !data->valid) {
		int i;

		dev_dbg(&client->dev, "Updating lm95241 data.\n");
		for (i = 0; i < ARRAY_SIZE(lm95241_reg_address); i++)
			data->temp[i]
			  = i2c_smbus_read_byte_data(client,
						     lm95241_reg_address[i]);
		data->last_updated = jiffies;
		data->valid = 1;
	}

	mutex_unlock(&data->update_lock);

	return data;
}

/* Sysfs stuff */
static ssize_t show_input(struct device *dev, struct device_attribute *attr,
			  char *buf)
{
	struct lm95241_data *data = lm95241_update_device(dev);
	int index = to_sensor_dev_attr(attr)->index;

	return snprintf(buf, PAGE_SIZE - 1, "%d\n",
			index == 0 || (data->config & (1 << (index / 2))) ?
		temp_from_reg_signed(data->temp[index], data->temp[index + 1]) :
		temp_from_reg_unsigned(data->temp[index],
				       data->temp[index + 1]));
}

static ssize_t show_type(struct device *dev, struct device_attribute *attr,
			 char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct lm95241_data *data = i2c_get_clientdata(client);

	return snprintf(buf, PAGE_SIZE - 1,
		data->model & to_sensor_dev_attr(attr)->index ? "1\n" : "2\n");
}

static ssize_t set_type(struct device *dev, struct device_attribute *attr,
			const char *buf, size_t count)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct lm95241_data *data = i2c_get_clientdata(client);
	unsigned long val;
	int shift;
	u8 mask = to_sensor_dev_attr(attr)->index;

	if (kstrtoul(buf, 10, &val) < 0)
		return -EINVAL;
	if (val != 1 && val != 2)
		return -EINVAL;

	shift = mask == R1MS_MASK ? TT1_SHIFT : TT2_SHIFT;

	mutex_lock(&data->update_lock);

	data->trutherm &= ~(TT_MASK << shift);
	if (val == 1) {
		data->model |= mask;
		data->trutherm |= (TT_ON << shift);
	} else {
		data->model &= ~mask;
		data->trutherm |= (TT_OFF << shift);
	}
	data->valid = 0;

	i2c_smbus_write_byte_data(client, LM95241_REG_RW_REMOTE_MODEL,
				  data->model);
	i2c_smbus_write_byte_data(client, LM95241_REG_RW_TRUTHERM,
				  data->trutherm);

	mutex_unlock(&data->update_lock);

	return count;
}

static ssize_t show_min(struct device *dev, struct device_attribute *attr,
			char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct lm95241_data *data = i2c_get_clientdata(client);

	return snprintf(buf, PAGE_SIZE - 1,
			data->config & to_sensor_dev_attr(attr)->index ?
			"-127000\n" : "0\n");
}

static ssize_t set_min(struct device *dev, struct device_attribute *attr,
		       const char *buf, size_t count)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct lm95241_data *data = i2c_get_clientdata(client);
	long val;

	if (kstrtol(buf, 10, &val) < 0)
		return -EINVAL;
	if (val < -128000)
		return -EINVAL;

	mutex_lock(&data->update_lock);

	if (val < 0)
		data->config |= to_sensor_dev_attr(attr)->index;
	else
		data->config &= ~to_sensor_dev_attr(attr)->index;
	data->valid = 0;

	i2c_smbus_write_byte_data(client, LM95241_REG_RW_CONFIG, data->config);

	mutex_unlock(&data->update_lock);

	return count;
}

static ssize_t show_max(struct device *dev, struct device_attribute *attr,
			char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct lm95241_data *data = i2c_get_clientdata(client);

	return snprintf(buf, PAGE_SIZE - 1,
			data->config & to_sensor_dev_attr(attr)->index ?
			"127000\n" : "255000\n");
}

static ssize_t set_max(struct device *dev, struct device_attribute *attr,
		       const char *buf, size_t count)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct lm95241_data *data = i2c_get_clientdata(client);
	long val;

	if (kstrtol(buf, 10, &val) < 0)
		return -EINVAL;
	if (val >= 256000)
		return -EINVAL;

	mutex_lock(&data->update_lock);

	if (val <= 127000)
		data->config |= to_sensor_dev_attr(attr)->index;
	else
		data->config &= ~to_sensor_dev_attr(attr)->index;
	data->valid = 0;

	i2c_smbus_write_byte_data(client, LM95241_REG_RW_CONFIG, data->config);

	mutex_unlock(&data->update_lock);

	return count;
}

static ssize_t show_interval(struct device *dev, struct device_attribute *attr,
			     char *buf)
{
	struct lm95241_data *data = lm95241_update_device(dev);

	return snprintf(buf, PAGE_SIZE - 1, "%lu\n", 1000 * data->interval
			/ HZ);
}

static ssize_t set_interval(struct device *dev, struct device_attribute *attr,
			    const char *buf, size_t count)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct lm95241_data *data = i2c_get_clientdata(client);
	unsigned long val;

	if (kstrtoul(buf, 10, &val) < 0)
		return -EINVAL;

	data->interval = val * HZ / 1000;

	return count;
}

static SENSOR_DEVICE_ATTR(temp1_input, S_IRUGO, show_input, NULL, 0);
static SENSOR_DEVICE_ATTR(temp2_input, S_IRUGO, show_input, NULL, 2);
static SENSOR_DEVICE_ATTR(temp3_input, S_IRUGO, show_input, NULL, 4);
static SENSOR_DEVICE_ATTR(temp2_type, S_IWUSR | S_IRUGO, show_type, set_type,
			  R1MS_MASK);
static SENSOR_DEVICE_ATTR(temp3_type, S_IWUSR | S_IRUGO, show_type, set_type,
			  R2MS_MASK);
static SENSOR_DEVICE_ATTR(temp2_min, S_IWUSR | S_IRUGO, show_min, set_min,
			  R1DF_MASK);
static SENSOR_DEVICE_ATTR(temp3_min, S_IWUSR | S_IRUGO, show_min, set_min,
			  R2DF_MASK);
static SENSOR_DEVICE_ATTR(temp2_max, S_IWUSR | S_IRUGO, show_max, set_max,
			  R1DF_MASK);
static SENSOR_DEVICE_ATTR(temp3_max, S_IWUSR | S_IRUGO, show_max, set_max,
			  R2DF_MASK);
static DEVICE_ATTR(update_interval, S_IWUSR | S_IRUGO, show_interval,
		   set_interval);

static struct attribute *lm95241_attributes[] = {
	&sensor_dev_attr_temp1_input.dev_attr.attr,
	&sensor_dev_attr_temp2_input.dev_attr.attr,
	&sensor_dev_attr_temp3_input.dev_attr.attr,
	&sensor_dev_attr_temp2_type.dev_attr.attr,
	&sensor_dev_attr_temp3_type.dev_attr.attr,
	&sensor_dev_attr_temp2_min.dev_attr.attr,
	&sensor_dev_attr_temp3_min.dev_attr.attr,
	&sensor_dev_attr_temp2_max.dev_attr.attr,
	&sensor_dev_attr_temp3_max.dev_attr.attr,
	&dev_attr_update_interval.attr,
	NULL
};

static const struct attribute_group lm95241_group = {
	.attrs = lm95241_attributes,
};

/* Return 0 if detection is successful, -ENODEV otherwise */
static int lm95241_detect(struct i2c_client *new_client,
			  struct i2c_board_info *info)
{
	struct i2c_adapter *adapter = new_client->adapter;
	const char *name;
	int mfg_id, chip_id;

	if (!i2c_check_functionality(adapter, I2C_FUNC_SMBUS_BYTE_DATA))
		return -ENODEV;

	mfg_id = i2c_smbus_read_byte_data(new_client, LM95241_REG_R_MAN_ID);
	if (mfg_id != NATSEMI_MAN_ID)
		return -ENODEV;

	chip_id = i2c_smbus_read_byte_data(new_client, LM95241_REG_R_CHIP_ID);
	switch (chip_id) {
	case LM95231_CHIP_ID:
		name = "lm95231";
		break;
	case LM95241_CHIP_ID:
		name = "lm95241";
		break;
	default:
		return -ENODEV;
	}

	/* Fill the i2c board info */
	strlcpy(info->type, name, I2C_NAME_SIZE);
	return 0;
}

static void lm95241_init_client(struct i2c_client *client)
{
	struct lm95241_data *data = i2c_get_clientdata(client);

	data->interval = HZ;	/* 1 sec default */
	data->valid = 0;
	data->config = CFG_CR0076;
	data->model = 0;
	data->trutherm = (TT_OFF << TT1_SHIFT) | (TT_OFF << TT2_SHIFT);

	i2c_smbus_write_byte_data(client, LM95241_REG_RW_CONFIG, data->config);
	i2c_smbus_write_byte_data(client, LM95241_REG_RW_REM_FILTER,
				  R1FE_MASK | R2FE_MASK);
	i2c_smbus_write_byte_data(client, LM95241_REG_RW_TRUTHERM,
				  data->trutherm);
	i2c_smbus_write_byte_data(client, LM95241_REG_RW_REMOTE_MODEL,
				  data->model);
}

static int lm95241_probe(struct i2c_client *new_client,
			 const struct i2c_device_id *id)
{
	struct lm95241_data *data;
	int err;

	data = devm_kzalloc(&new_client->dev, sizeof(struct lm95241_data),
			    GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	i2c_set_clientdata(new_client, data);
	mutex_init(&data->update_lock);

	/* Initialize the LM95241 chip */
	lm95241_init_client(new_client);

	/* Register sysfs hooks */
	err = sysfs_create_group(&new_client->dev.kobj, &lm95241_group);
	if (err)
		return err;

	data->hwmon_dev = hwmon_device_register(&new_client->dev);
	if (IS_ERR(data->hwmon_dev)) {
		err = PTR_ERR(data->hwmon_dev);
		goto exit_remove_files;
	}

	return 0;

exit_remove_files:
	sysfs_remove_group(&new_client->dev.kobj, &lm95241_group);
	return err;
}

static int lm95241_remove(struct i2c_client *client)
{
	struct lm95241_data *data = i2c_get_clientdata(client);

	hwmon_device_unregister(data->hwmon_dev);
	sysfs_remove_group(&client->dev.kobj, &lm95241_group);

	return 0;
}

/* Driver data (common to all clients) */
static const struct i2c_device_id lm95241_id[] = {
	{ "lm95231", 0 },
	{ "lm95241", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, lm95241_id);

static struct i2c_driver lm95241_driver = {
	.class		= I2C_CLASS_HWMON,
	.driver = {
		.name	= DEVNAME,
	},
	.probe		= lm95241_probe,
	.remove		= lm95241_remove,
	.id_table	= lm95241_id,
	.detect		= lm95241_detect,
	.address_list	= normal_i2c,
};

module_i2c_driver(lm95241_driver);

MODULE_AUTHOR("Davide Rizzo <elpa.rizzo@gmail.com>");
MODULE_DESCRIPTION("LM95241 sensor driver");
MODULE_LICENSE("GPL");
