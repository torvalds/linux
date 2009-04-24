/*
 * lm95241.c - Part of lm_sensors, Linux kernel modules for hardware
 *             monitoring
 * Copyright (C) 2008 Davide Rizzo <elpa-rizzo@gmail.com>
 *
 * Based on the max1619 driver. The LM95241 is a sensor chip made by National
 *   Semiconductors.
 * It reports up to three temperatures (its own plus up to
 * two external ones). Complete datasheet can be
 * obtained from National's website at:
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

static const unsigned short normal_i2c[] = {
	0x19, 0x2a, 0x2b, I2C_CLIENT_END};

/* Insmod parameters */
I2C_CLIENT_INSMOD_1(lm95241);

/* LM95241 registers */
#define LM95241_REG_R_MAN_ID		0xFE
#define LM95241_REG_R_CHIP_ID		0xFF
#define LM95241_REG_R_STATUS		0x02
#define LM95241_REG_RW_CONFIG		0x03
#define LM95241_REG_RW_REM_FILTER	0x06
#define LM95241_REG_RW_TRUTHERM		0x07
#define LM95241_REG_W_ONE_SHOT  	0x0F
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
#define MANUFACTURER_ID 0x01
#define DEFAULT_REVISION 0xA4

/* Conversions and various macros */
#define TEMP_FROM_REG(val_h, val_l) (((val_h) & 0x80 ? (val_h) - 0x100 : \
    (val_h)) * 1000 + (val_l) * 1000 / 256)

/* Functions declaration */
static void lm95241_init_client(struct i2c_client *client);
static struct lm95241_data *lm95241_update_device(struct device *dev);

/* Client data (each client gets its own) */
struct lm95241_data {
	struct device *hwmon_dev;
	struct mutex update_lock;
	unsigned long last_updated, rate; /* in jiffies */
	char valid; /* zero until following fields are valid */
	/* registers values */
	u8 local_h, local_l; /* local */
	u8 remote1_h, remote1_l; /* remote1 */
	u8 remote2_h, remote2_l; /* remote2 */
	u8 config, model, trutherm;
};

/* Sysfs stuff */
#define show_temp(value) \
static ssize_t show_##value(struct device *dev, \
    struct device_attribute *attr, char *buf) \
{ \
	struct lm95241_data *data = lm95241_update_device(dev); \
	snprintf(buf, PAGE_SIZE - 1, "%d\n", \
		TEMP_FROM_REG(data->value##_h, data->value##_l)); \
	return strlen(buf); \
}
show_temp(local);
show_temp(remote1);
show_temp(remote2);

static ssize_t show_rate(struct device *dev, struct device_attribute *attr,
			 char *buf)
{
	struct lm95241_data *data = lm95241_update_device(dev);

	snprintf(buf, PAGE_SIZE - 1, "%lu\n", 1000 * data->rate / HZ);
	return strlen(buf);
}

static ssize_t set_rate(struct device *dev, struct device_attribute *attr,
			const char *buf, size_t count)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct lm95241_data *data = i2c_get_clientdata(client);

	strict_strtol(buf, 10, &data->rate);
	data->rate = data->rate * HZ / 1000;

	return count;
}

#define show_type(flag) \
static ssize_t show_type##flag(struct device *dev, \
				   struct device_attribute *attr, char *buf) \
{ \
	struct i2c_client *client = to_i2c_client(dev); \
	struct lm95241_data *data = i2c_get_clientdata(client); \
\
	snprintf(buf, PAGE_SIZE - 1, \
		data->model & R##flag##MS_MASK ? "1\n" : "2\n"); \
	return strlen(buf); \
}
show_type(1);
show_type(2);

#define show_min(flag) \
static ssize_t show_min##flag(struct device *dev, \
    struct device_attribute *attr, char *buf) \
{ \
	struct i2c_client *client = to_i2c_client(dev); \
	struct lm95241_data *data = i2c_get_clientdata(client); \
\
	snprintf(buf, PAGE_SIZE - 1, \
		data->config & R##flag##DF_MASK ?	\
		"-127000\n" : "0\n"); \
	return strlen(buf); \
}
show_min(1);
show_min(2);

#define show_max(flag) \
static ssize_t show_max##flag(struct device *dev, \
    struct device_attribute *attr, char *buf) \
{ \
	struct i2c_client *client = to_i2c_client(dev); \
	struct lm95241_data *data = i2c_get_clientdata(client); \
\
	snprintf(buf, PAGE_SIZE - 1, \
		data->config & R##flag##DF_MASK ? \
		"127000\n" : "255000\n"); \
	return strlen(buf); \
}
show_max(1);
show_max(2);

#define set_type(flag) \
static ssize_t set_type##flag(struct device *dev, \
				  struct device_attribute *attr, \
				  const char *buf, size_t count) \
{ \
	struct i2c_client *client = to_i2c_client(dev); \
	struct lm95241_data *data = i2c_get_clientdata(client); \
\
	long val; \
	strict_strtol(buf, 10, &val); \
\
	if ((val == 1) || (val == 2)) { \
\
		mutex_lock(&data->update_lock); \
\
		data->trutherm &= ~(TT_MASK << TT##flag##_SHIFT); \
		if (val == 1) { \
			data->model |= R##flag##MS_MASK; \
			data->trutherm |= (TT_ON << TT##flag##_SHIFT); \
		} \
		else { \
			data->model &= ~R##flag##MS_MASK; \
			data->trutherm |= (TT_OFF << TT##flag##_SHIFT); \
		} \
\
		data->valid = 0; \
\
		i2c_smbus_write_byte_data(client, LM95241_REG_RW_REMOTE_MODEL, \
					  data->model); \
		i2c_smbus_write_byte_data(client, LM95241_REG_RW_TRUTHERM, \
					  data->trutherm); \
\
		mutex_unlock(&data->update_lock); \
\
	} \
	return count; \
}
set_type(1);
set_type(2);

#define set_min(flag) \
static ssize_t set_min##flag(struct device *dev, \
	struct device_attribute *devattr, const char *buf, size_t count) \
{ \
	struct i2c_client *client = to_i2c_client(dev); \
	struct lm95241_data *data = i2c_get_clientdata(client); \
\
	long val; \
	strict_strtol(buf, 10, &val); \
\
	mutex_lock(&data->update_lock); \
\
	if (val < 0) \
		data->config |= R##flag##DF_MASK; \
	else \
		data->config &= ~R##flag##DF_MASK; \
\
	data->valid = 0; \
\
	i2c_smbus_write_byte_data(client, LM95241_REG_RW_CONFIG, \
		data->config); \
\
	mutex_unlock(&data->update_lock); \
\
	return count; \
}
set_min(1);
set_min(2);

#define set_max(flag) \
static ssize_t set_max##flag(struct device *dev, \
	struct device_attribute *devattr, const char *buf, size_t count) \
{ \
	struct i2c_client *client = to_i2c_client(dev); \
	struct lm95241_data *data = i2c_get_clientdata(client); \
\
	long val; \
	strict_strtol(buf, 10, &val); \
\
	mutex_lock(&data->update_lock); \
\
	if (val <= 127000) \
		data->config |= R##flag##DF_MASK; \
	else \
		data->config &= ~R##flag##DF_MASK; \
\
	data->valid = 0; \
\
	i2c_smbus_write_byte_data(client, LM95241_REG_RW_CONFIG, \
		data->config); \
\
	mutex_unlock(&data->update_lock); \
\
	return count; \
}
set_max(1);
set_max(2);

static DEVICE_ATTR(temp1_input, S_IRUGO, show_local, NULL);
static DEVICE_ATTR(temp2_input, S_IRUGO, show_remote1, NULL);
static DEVICE_ATTR(temp3_input, S_IRUGO, show_remote2, NULL);
static DEVICE_ATTR(temp2_type, S_IWUSR | S_IRUGO, show_type1, set_type1);
static DEVICE_ATTR(temp3_type, S_IWUSR | S_IRUGO, show_type2, set_type2);
static DEVICE_ATTR(temp2_min, S_IWUSR | S_IRUGO, show_min1, set_min1);
static DEVICE_ATTR(temp3_min, S_IWUSR | S_IRUGO, show_min2, set_min2);
static DEVICE_ATTR(temp2_max, S_IWUSR | S_IRUGO, show_max1, set_max1);
static DEVICE_ATTR(temp3_max, S_IWUSR | S_IRUGO, show_max2, set_max2);
static DEVICE_ATTR(rate, S_IWUSR | S_IRUGO, show_rate, set_rate);

static struct attribute *lm95241_attributes[] = {
	&dev_attr_temp1_input.attr,
	&dev_attr_temp2_input.attr,
	&dev_attr_temp3_input.attr,
	&dev_attr_temp2_type.attr,
	&dev_attr_temp3_type.attr,
	&dev_attr_temp2_min.attr,
	&dev_attr_temp3_min.attr,
	&dev_attr_temp2_max.attr,
	&dev_attr_temp3_max.attr,
	&dev_attr_rate.attr,
	NULL
};

static const struct attribute_group lm95241_group = {
	.attrs = lm95241_attributes,
};

/* Return 0 if detection is successful, -ENODEV otherwise */
static int lm95241_detect(struct i2c_client *new_client, int kind,
			  struct i2c_board_info *info)
{
	struct i2c_adapter *adapter = new_client->adapter;
	int address = new_client->addr;
	const char *name = "";

	if (!i2c_check_functionality(adapter, I2C_FUNC_SMBUS_BYTE_DATA))
		return -ENODEV;

	/*
	 * Now we do the remaining detection. A negative kind means that
	 * the driver was loaded with no force parameter (default), so we
	 * must both detect and identify the chip. A zero kind means that
	 * the driver was loaded with the force parameter, the detection
	 * step shall be skipped. A positive kind means that the driver
	 * was loaded with the force parameter and a given kind of chip is
	 * requested, so both the detection and the identification steps
	 * are skipped.
	 */
	if (kind < 0) {	/* detection */
		if ((i2c_smbus_read_byte_data(new_client, LM95241_REG_R_MAN_ID)
		     != MANUFACTURER_ID)
		|| (i2c_smbus_read_byte_data(new_client, LM95241_REG_R_CHIP_ID)
		    < DEFAULT_REVISION)) {
			dev_dbg(&adapter->dev,
				"LM95241 detection failed at 0x%02x.\n",
				address);
			return -ENODEV;
		}
	}

	if (kind <= 0) { /* identification */
		if ((i2c_smbus_read_byte_data(new_client, LM95241_REG_R_MAN_ID)
		     == MANUFACTURER_ID)
		&& (i2c_smbus_read_byte_data(new_client, LM95241_REG_R_CHIP_ID)
		    >= DEFAULT_REVISION)) {

			kind = lm95241;

			if (kind <= 0) { /* identification failed */
				dev_info(&adapter->dev, "Unsupported chip\n");
				return -ENODEV;
			}
		}
	}

	/* Fill the i2c board info */
	if (kind == lm95241)
		name = "lm95241";
	strlcpy(info->type, name, I2C_NAME_SIZE);
	return 0;
}

static int lm95241_probe(struct i2c_client *new_client,
			 const struct i2c_device_id *id)
{
	struct lm95241_data *data;
	int err;

	data = kzalloc(sizeof(struct lm95241_data), GFP_KERNEL);
	if (!data) {
		err = -ENOMEM;
		goto exit;
	}

	i2c_set_clientdata(new_client, data);
	mutex_init(&data->update_lock);

	/* Initialize the LM95241 chip */
	lm95241_init_client(new_client);

	/* Register sysfs hooks */
	err = sysfs_create_group(&new_client->dev.kobj, &lm95241_group);
	if (err)
		goto exit_free;

	data->hwmon_dev = hwmon_device_register(&new_client->dev);
	if (IS_ERR(data->hwmon_dev)) {
		err = PTR_ERR(data->hwmon_dev);
		goto exit_remove_files;
	}

	return 0;

exit_remove_files:
	sysfs_remove_group(&new_client->dev.kobj, &lm95241_group);
exit_free:
	kfree(data);
exit:
	return err;
}

static void lm95241_init_client(struct i2c_client *client)
{
	struct lm95241_data *data = i2c_get_clientdata(client);

	data->rate = HZ;    /* 1 sec default */
	data->valid = 0;
	data->config = CFG_CR0076;
	data->model = 0;
	data->trutherm = (TT_OFF << TT1_SHIFT) | (TT_OFF << TT2_SHIFT);

	i2c_smbus_write_byte_data(client, LM95241_REG_RW_CONFIG,
				  data->config);
	i2c_smbus_write_byte_data(client, LM95241_REG_RW_REM_FILTER,
				  R1FE_MASK | R2FE_MASK);
	i2c_smbus_write_byte_data(client, LM95241_REG_RW_TRUTHERM,
				  data->trutherm);
	i2c_smbus_write_byte_data(client, LM95241_REG_RW_REMOTE_MODEL,
				  data->model);
}

static int lm95241_remove(struct i2c_client *client)
{
	struct lm95241_data *data = i2c_get_clientdata(client);

	hwmon_device_unregister(data->hwmon_dev);
	sysfs_remove_group(&client->dev.kobj, &lm95241_group);

	i2c_set_clientdata(client, NULL);
	kfree(data);
	return 0;
}

static struct lm95241_data *lm95241_update_device(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct lm95241_data *data = i2c_get_clientdata(client);

	mutex_lock(&data->update_lock);

	if (time_after(jiffies, data->last_updated + data->rate) ||
	    !data->valid) {
		dev_dbg(&client->dev, "Updating lm95241 data.\n");
		data->local_h =
			i2c_smbus_read_byte_data(client,
						 LM95241_REG_R_LOCAL_TEMPH);
		data->local_l =
			i2c_smbus_read_byte_data(client,
						 LM95241_REG_R_LOCAL_TEMPL);
		data->remote1_h =
			i2c_smbus_read_byte_data(client,
						 LM95241_REG_R_REMOTE1_TEMPH);
		data->remote1_l =
			i2c_smbus_read_byte_data(client,
						 LM95241_REG_R_REMOTE1_TEMPL);
		data->remote2_h =
			i2c_smbus_read_byte_data(client,
						 LM95241_REG_R_REMOTE2_TEMPH);
		data->remote2_l =
			i2c_smbus_read_byte_data(client,
						 LM95241_REG_R_REMOTE2_TEMPL);
		data->last_updated = jiffies;
		data->valid = 1;
	}

	mutex_unlock(&data->update_lock);

	return data;
}

/* Driver data (common to all clients) */
static const struct i2c_device_id lm95241_id[] = {
	{ "lm95241", lm95241 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, lm95241_id);

static struct i2c_driver lm95241_driver = {
	.class		= I2C_CLASS_HWMON,
	.driver = {
		.name   = "lm95241",
	},
	.probe		= lm95241_probe,
	.remove		= lm95241_remove,
	.id_table	= lm95241_id,
	.detect		= lm95241_detect,
	.address_data	= &addr_data,
};

static int __init sensors_lm95241_init(void)
{
	return i2c_add_driver(&lm95241_driver);
}

static void __exit sensors_lm95241_exit(void)
{
	i2c_del_driver(&lm95241_driver);
}

MODULE_AUTHOR("Davide Rizzo <elpa-rizzo@gmail.com>");
MODULE_DESCRIPTION("LM95241 sensor driver");
MODULE_LICENSE("GPL");

module_init(sensors_lm95241_init);
module_exit(sensors_lm95241_exit);
