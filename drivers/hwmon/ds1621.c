/*
    ds1621.c - Part of lm_sensors, Linux kernel modules for hardware
             monitoring
    Christian W. Zuckschwerdt  <zany@triq.net>  2000-11-23
    based on lm75.c by Frodo Looijaard <frodol@dds.nl>
    Ported to Linux 2.6 by Aurelien Jarno <aurelien@aurel32.net> with 
    the help of Jean Delvare <khali@linux-fr.org>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*/

#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/jiffies.h>
#include <linux/i2c.h>
#include <linux/hwmon.h>
#include <linux/err.h>
#include "lm75.h"

/* Addresses to scan */
static unsigned short normal_i2c[] = { 0x48, 0x49, 0x4a, 0x4b, 0x4c,
					0x4d, 0x4e, 0x4f, I2C_CLIENT_END };

/* Insmod parameters */
I2C_CLIENT_INSMOD_1(ds1621);
static int polarity = -1;
module_param(polarity, int, 0);
MODULE_PARM_DESC(polarity, "Output's polarity: 0 = active high, 1 = active low");

/* Many DS1621 constants specified below */
/* Config register used for detection         */
/*  7    6    5    4    3    2    1    0      */
/* |Done|THF |TLF |NVB | X  | X  |POL |1SHOT| */
#define DS1621_REG_CONFIG_NVB		0x10
#define DS1621_REG_CONFIG_POLARITY	0x02
#define DS1621_REG_CONFIG_1SHOT		0x01
#define DS1621_REG_CONFIG_DONE		0x80

/* The DS1621 registers */
#define DS1621_REG_TEMP			0xAA /* word, RO */
#define DS1621_REG_TEMP_MIN		0xA1 /* word, RW */
#define DS1621_REG_TEMP_MAX		0xA2 /* word, RW */
#define DS1621_REG_CONF			0xAC /* byte, RW */
#define DS1621_COM_START		0xEE /* no data */
#define DS1621_COM_STOP			0x22 /* no data */

/* The DS1621 configuration register */
#define DS1621_ALARM_TEMP_HIGH		0x40
#define DS1621_ALARM_TEMP_LOW		0x20

/* Conversions. Rounding and limit checking is only done on the TO_REG
   variants. Note that you should be a bit careful with which arguments
   these macros are called: arguments may be evaluated more than once.
   Fixing this is just not worth it. */
#define ALARMS_FROM_REG(val) ((val) & \
                              (DS1621_ALARM_TEMP_HIGH | DS1621_ALARM_TEMP_LOW))

/* Each client has this additional data */
struct ds1621_data {
	struct i2c_client client;
	struct class_device *class_dev;
	struct semaphore update_lock;
	char valid;			/* !=0 if following fields are valid */
	unsigned long last_updated;	/* In jiffies */

	u16 temp, temp_min, temp_max;	/* Register values, word */
	u8 conf;			/* Register encoding, combined */
};

static int ds1621_attach_adapter(struct i2c_adapter *adapter);
static int ds1621_detect(struct i2c_adapter *adapter, int address,
			 int kind);
static void ds1621_init_client(struct i2c_client *client);
static int ds1621_detach_client(struct i2c_client *client);
static struct ds1621_data *ds1621_update_client(struct device *dev);

/* This is the driver that will be inserted */
static struct i2c_driver ds1621_driver = {
	.owner		= THIS_MODULE,
	.name		= "ds1621",
	.id		= I2C_DRIVERID_DS1621,
	.flags		= I2C_DF_NOTIFY,
	.attach_adapter	= ds1621_attach_adapter,
	.detach_client	= ds1621_detach_client,
};

/* All registers are word-sized, except for the configuration register.
   DS1621 uses a high-byte first convention, which is exactly opposite to
   the usual practice. */
static int ds1621_read_value(struct i2c_client *client, u8 reg)
{
	if (reg == DS1621_REG_CONF)
		return i2c_smbus_read_byte_data(client, reg);
	else
		return swab16(i2c_smbus_read_word_data(client, reg));
}

/* All registers are word-sized, except for the configuration register.
   DS1621 uses a high-byte first convention, which is exactly opposite to
   the usual practice. */
static int ds1621_write_value(struct i2c_client *client, u8 reg, u16 value)
{
	if (reg == DS1621_REG_CONF)
		return i2c_smbus_write_byte_data(client, reg, value);
	else
		return i2c_smbus_write_word_data(client, reg, swab16(value));
}

static void ds1621_init_client(struct i2c_client *client)
{
	int reg = ds1621_read_value(client, DS1621_REG_CONF);
	/* switch to continuous conversion mode */
	reg &= ~ DS1621_REG_CONFIG_1SHOT;

	/* setup output polarity */
	if (polarity == 0)
		reg &= ~DS1621_REG_CONFIG_POLARITY;
	else if (polarity == 1)
		reg |= DS1621_REG_CONFIG_POLARITY;
	
	ds1621_write_value(client, DS1621_REG_CONF, reg);
	
	/* start conversion */
	i2c_smbus_write_byte(client, DS1621_COM_START);
}

#define show(value)							\
static ssize_t show_##value(struct device *dev, struct device_attribute *attr, char *buf)		\
{									\
	struct ds1621_data *data = ds1621_update_client(dev);		\
	return sprintf(buf, "%d\n", LM75_TEMP_FROM_REG(data->value));	\
}

show(temp);
show(temp_min);
show(temp_max);

#define set_temp(suffix, value, reg)					\
static ssize_t set_temp_##suffix(struct device *dev, struct device_attribute *attr, const char *buf,	\
				 size_t count)				\
{									\
	struct i2c_client *client = to_i2c_client(dev);			\
	struct ds1621_data *data = ds1621_update_client(dev);		\
	u16 val = LM75_TEMP_TO_REG(simple_strtoul(buf, NULL, 10));	\
									\
	down(&data->update_lock);					\
	data->value = val;						\
	ds1621_write_value(client, reg, data->value);			\
	up(&data->update_lock);						\
	return count;							\
}

set_temp(min, temp_min, DS1621_REG_TEMP_MIN);
set_temp(max, temp_max, DS1621_REG_TEMP_MAX);

static ssize_t show_alarms(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct ds1621_data *data = ds1621_update_client(dev);
	return sprintf(buf, "%d\n", ALARMS_FROM_REG(data->conf));
}

static DEVICE_ATTR(alarms, S_IRUGO, show_alarms, NULL);
static DEVICE_ATTR(temp1_input, S_IRUGO , show_temp, NULL);
static DEVICE_ATTR(temp1_min, S_IWUSR | S_IRUGO , show_temp_min, set_temp_min);
static DEVICE_ATTR(temp1_max, S_IWUSR | S_IRUGO, show_temp_max, set_temp_max);


static int ds1621_attach_adapter(struct i2c_adapter *adapter)
{
	if (!(adapter->class & I2C_CLASS_HWMON))
		return 0;
	return i2c_probe(adapter, &addr_data, ds1621_detect);
}

/* This function is called by i2c_probe */
static int ds1621_detect(struct i2c_adapter *adapter, int address,
			 int kind)
{
	int conf, temp;
	struct i2c_client *new_client;
	struct ds1621_data *data;
	int err = 0;

	if (!i2c_check_functionality(adapter, I2C_FUNC_SMBUS_BYTE_DATA 
				     | I2C_FUNC_SMBUS_WORD_DATA 
				     | I2C_FUNC_SMBUS_WRITE_BYTE))
		goto exit;

	/* OK. For now, we presume we have a valid client. We now create the
	   client structure, even though we cannot fill it completely yet.
	   But it allows us to access ds1621_{read,write}_value. */
	if (!(data = kzalloc(sizeof(struct ds1621_data), GFP_KERNEL))) {
		err = -ENOMEM;
		goto exit;
	}
	
	new_client = &data->client;
	i2c_set_clientdata(new_client, data);
	new_client->addr = address;
	new_client->adapter = adapter;
	new_client->driver = &ds1621_driver;
	new_client->flags = 0;


	/* Now, we do the remaining detection. It is lousy. */
	if (kind < 0) {
		/* The NVB bit should be low if no EEPROM write has been 
		   requested during the latest 10ms, which is highly 
		   improbable in our case. */
		conf = ds1621_read_value(new_client, DS1621_REG_CONF);
		if (conf & DS1621_REG_CONFIG_NVB)
			goto exit_free;
		/* The 7 lowest bits of a temperature should always be 0. */
		temp = ds1621_read_value(new_client, DS1621_REG_TEMP);
		if (temp & 0x007f)
			goto exit_free;
		temp = ds1621_read_value(new_client, DS1621_REG_TEMP_MIN);
		if (temp & 0x007f)
			goto exit_free;
		temp = ds1621_read_value(new_client, DS1621_REG_TEMP_MAX);
		if (temp & 0x007f)
			goto exit_free;
	}

	/* Determine the chip type - only one kind supported! */
	if (kind <= 0)
		kind = ds1621;

	/* Fill in remaining client fields and put it into the global list */
	strlcpy(new_client->name, "ds1621", I2C_NAME_SIZE);
	data->valid = 0;
	init_MUTEX(&data->update_lock);

	/* Tell the I2C layer a new client has arrived */
	if ((err = i2c_attach_client(new_client)))
		goto exit_free;

	/* Initialize the DS1621 chip */
	ds1621_init_client(new_client);

	/* Register sysfs hooks */
	data->class_dev = hwmon_device_register(&new_client->dev);
	if (IS_ERR(data->class_dev)) {
		err = PTR_ERR(data->class_dev);
		goto exit_detach;
	}

	device_create_file(&new_client->dev, &dev_attr_alarms);
	device_create_file(&new_client->dev, &dev_attr_temp1_input);
	device_create_file(&new_client->dev, &dev_attr_temp1_min);
	device_create_file(&new_client->dev, &dev_attr_temp1_max);
	
	return 0;

/* OK, this is not exactly good programming practice, usually. But it is
   very code-efficient in this case. */
      exit_detach:
	i2c_detach_client(new_client);
      exit_free:
	kfree(data);
      exit:
	return err;
}

static int ds1621_detach_client(struct i2c_client *client)
{
	struct ds1621_data *data = i2c_get_clientdata(client);
	int err;

	hwmon_device_unregister(data->class_dev);

	if ((err = i2c_detach_client(client)))
		return err;

	kfree(data);

	return 0;
}


static struct ds1621_data *ds1621_update_client(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct ds1621_data *data = i2c_get_clientdata(client);
	u8 new_conf;

	down(&data->update_lock);

	if (time_after(jiffies, data->last_updated + HZ + HZ / 2)
	    || !data->valid) {

		dev_dbg(&client->dev, "Starting ds1621 update\n");

		data->conf = ds1621_read_value(client, DS1621_REG_CONF);

		data->temp = ds1621_read_value(client, DS1621_REG_TEMP);
		
		data->temp_min = ds1621_read_value(client,
		                                    DS1621_REG_TEMP_MIN);
		data->temp_max = ds1621_read_value(client,
						    DS1621_REG_TEMP_MAX);

		/* reset alarms if necessary */
		new_conf = data->conf;
		if (data->temp < data->temp_min)
			new_conf &= ~DS1621_ALARM_TEMP_LOW;
		if (data->temp > data->temp_max)
			new_conf &= ~DS1621_ALARM_TEMP_HIGH;
		if (data->conf != new_conf)
			ds1621_write_value(client, DS1621_REG_CONF,
					   new_conf);

		data->last_updated = jiffies;
		data->valid = 1;
	}

	up(&data->update_lock);

	return data;
}

static int __init ds1621_init(void)
{
	return i2c_add_driver(&ds1621_driver);
}

static void __exit ds1621_exit(void)
{
	i2c_del_driver(&ds1621_driver);
}


MODULE_AUTHOR("Christian W. Zuckschwerdt <zany@triq.net>");
MODULE_DESCRIPTION("DS1621 driver");
MODULE_LICENSE("GPL");

module_init(ds1621_init);
module_exit(ds1621_exit);
