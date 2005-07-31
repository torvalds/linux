/*
    adm1021.c - Part of lm_sensors, Linux kernel modules for hardware
             monitoring
    Copyright (c) 1998, 1999  Frodo Looijaard <frodol@dds.nl> and
    Philip Edelbrock <phil@netroedge.com>

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


/* Addresses to scan */
static unsigned short normal_i2c[] = { 0x18, 0x19, 0x1a,
					0x29, 0x2a, 0x2b,
					0x4c, 0x4d, 0x4e, 
					I2C_CLIENT_END };

/* Insmod parameters */
I2C_CLIENT_INSMOD_8(adm1021, adm1023, max1617, max1617a, thmc10, lm84, gl523sm, mc1066);

/* adm1021 constants specified below */

/* The adm1021 registers */
/* Read-only */
#define ADM1021_REG_TEMP		0x00
#define ADM1021_REG_REMOTE_TEMP		0x01
#define ADM1021_REG_STATUS		0x02
#define ADM1021_REG_MAN_ID		0x0FE	/* 0x41 = AMD, 0x49 = TI, 0x4D = Maxim, 0x23 = Genesys , 0x54 = Onsemi*/
#define ADM1021_REG_DEV_ID		0x0FF	/* ADM1021 = 0x0X, ADM1023 = 0x3X */
#define ADM1021_REG_DIE_CODE		0x0FF	/* MAX1617A */
/* These use different addresses for reading/writing */
#define ADM1021_REG_CONFIG_R		0x03
#define ADM1021_REG_CONFIG_W		0x09
#define ADM1021_REG_CONV_RATE_R		0x04
#define ADM1021_REG_CONV_RATE_W		0x0A
/* These are for the ADM1023's additional precision on the remote temp sensor */
#define ADM1021_REG_REM_TEMP_PREC	0x010
#define ADM1021_REG_REM_OFFSET		0x011
#define ADM1021_REG_REM_OFFSET_PREC	0x012
#define ADM1021_REG_REM_TOS_PREC	0x013
#define ADM1021_REG_REM_THYST_PREC	0x014
/* limits */
#define ADM1021_REG_TOS_R		0x05
#define ADM1021_REG_TOS_W		0x0B
#define ADM1021_REG_REMOTE_TOS_R	0x07
#define ADM1021_REG_REMOTE_TOS_W	0x0D
#define ADM1021_REG_THYST_R		0x06
#define ADM1021_REG_THYST_W		0x0C
#define ADM1021_REG_REMOTE_THYST_R	0x08
#define ADM1021_REG_REMOTE_THYST_W	0x0E
/* write-only */
#define ADM1021_REG_ONESHOT		0x0F


/* Conversions. Rounding and limit checking is only done on the TO_REG
   variants. Note that you should be a bit careful with which arguments
   these macros are called: arguments may be evaluated more than once.
   Fixing this is just not worth it. */
/* Conversions  note: 1021 uses normal integer signed-byte format*/
#define TEMP_FROM_REG(val)	(val > 127 ? (val-256)*1000 : val*1000)
#define TEMP_TO_REG(val)	(SENSORS_LIMIT((val < 0 ? (val/1000)+256 : val/1000),0,255))

/* Initial values */

/* Note: Even though I left the low and high limits named os and hyst, 
they don't quite work like a thermostat the way the LM75 does.  I.e., 
a lower temp than THYST actually triggers an alarm instead of 
clearing it.  Weird, ey?   --Phil  */

/* Each client has this additional data */
struct adm1021_data {
	struct i2c_client client;
	struct class_device *class_dev;
	enum chips type;

	struct semaphore update_lock;
	char valid;		/* !=0 if following fields are valid */
	unsigned long last_updated;	/* In jiffies */

	u8	temp_max;	/* Register values */
	u8	temp_hyst;
	u8	temp_input;
	u8	remote_temp_max;
	u8	remote_temp_hyst;
	u8	remote_temp_input;
	u8	alarms;
        /* Special values for ADM1023 only */
	u8	remote_temp_prec;
	u8	remote_temp_os_prec;
	u8	remote_temp_hyst_prec;
	u8	remote_temp_offset;
	u8	remote_temp_offset_prec;
};

static int adm1021_attach_adapter(struct i2c_adapter *adapter);
static int adm1021_detect(struct i2c_adapter *adapter, int address, int kind);
static void adm1021_init_client(struct i2c_client *client);
static int adm1021_detach_client(struct i2c_client *client);
static int adm1021_read_value(struct i2c_client *client, u8 reg);
static int adm1021_write_value(struct i2c_client *client, u8 reg,
			       u16 value);
static struct adm1021_data *adm1021_update_device(struct device *dev);

/* (amalysh) read only mode, otherwise any limit's writing confuse BIOS */
static int read_only = 0;


/* This is the driver that will be inserted */
static struct i2c_driver adm1021_driver = {
	.owner		= THIS_MODULE,
	.name		= "adm1021",
	.id		= I2C_DRIVERID_ADM1021,
	.flags		= I2C_DF_NOTIFY,
	.attach_adapter	= adm1021_attach_adapter,
	.detach_client	= adm1021_detach_client,
};

#define show(value)	\
static ssize_t show_##value(struct device *dev, struct device_attribute *attr, char *buf)		\
{									\
	struct adm1021_data *data = adm1021_update_device(dev);		\
	return sprintf(buf, "%d\n", TEMP_FROM_REG(data->value));	\
}
show(temp_max);
show(temp_hyst);
show(temp_input);
show(remote_temp_max);
show(remote_temp_hyst);
show(remote_temp_input);

#define show2(value)	\
static ssize_t show_##value(struct device *dev, struct device_attribute *attr, char *buf)		\
{									\
	struct adm1021_data *data = adm1021_update_device(dev);		\
	return sprintf(buf, "%d\n", data->value);			\
}
show2(alarms);

#define set(value, reg)	\
static ssize_t set_##value(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)	\
{								\
	struct i2c_client *client = to_i2c_client(dev);		\
	struct adm1021_data *data = i2c_get_clientdata(client);	\
	int temp = simple_strtoul(buf, NULL, 10);		\
								\
	down(&data->update_lock);				\
	data->value = TEMP_TO_REG(temp);			\
	adm1021_write_value(client, reg, data->value);		\
	up(&data->update_lock);					\
	return count;						\
}
set(temp_max, ADM1021_REG_TOS_W);
set(temp_hyst, ADM1021_REG_THYST_W);
set(remote_temp_max, ADM1021_REG_REMOTE_TOS_W);
set(remote_temp_hyst, ADM1021_REG_REMOTE_THYST_W);

static DEVICE_ATTR(temp1_max, S_IWUSR | S_IRUGO, show_temp_max, set_temp_max);
static DEVICE_ATTR(temp1_min, S_IWUSR | S_IRUGO, show_temp_hyst, set_temp_hyst);
static DEVICE_ATTR(temp1_input, S_IRUGO, show_temp_input, NULL);
static DEVICE_ATTR(temp2_max, S_IWUSR | S_IRUGO, show_remote_temp_max, set_remote_temp_max);
static DEVICE_ATTR(temp2_min, S_IWUSR | S_IRUGO, show_remote_temp_hyst, set_remote_temp_hyst);
static DEVICE_ATTR(temp2_input, S_IRUGO, show_remote_temp_input, NULL);
static DEVICE_ATTR(alarms, S_IRUGO, show_alarms, NULL);


static int adm1021_attach_adapter(struct i2c_adapter *adapter)
{
	if (!(adapter->class & I2C_CLASS_HWMON))
		return 0;
	return i2c_probe(adapter, &addr_data, adm1021_detect);
}

static int adm1021_detect(struct i2c_adapter *adapter, int address, int kind)
{
	int i;
	struct i2c_client *new_client;
	struct adm1021_data *data;
	int err = 0;
	const char *type_name = "";

	if (!i2c_check_functionality(adapter, I2C_FUNC_SMBUS_BYTE_DATA))
		goto error0;

	/* OK. For now, we presume we have a valid client. We now create the
	   client structure, even though we cannot fill it completely yet.
	   But it allows us to access adm1021_{read,write}_value. */

	if (!(data = kmalloc(sizeof(struct adm1021_data), GFP_KERNEL))) {
		err = -ENOMEM;
		goto error0;
	}
	memset(data, 0, sizeof(struct adm1021_data));

	new_client = &data->client;
	i2c_set_clientdata(new_client, data);
	new_client->addr = address;
	new_client->adapter = adapter;
	new_client->driver = &adm1021_driver;
	new_client->flags = 0;

	/* Now, we do the remaining detection. */
	if (kind < 0) {
		if ((adm1021_read_value(new_client, ADM1021_REG_STATUS) & 0x03) != 0x00
		 || (adm1021_read_value(new_client, ADM1021_REG_CONFIG_R) & 0x3F) != 0x00
		 || (adm1021_read_value(new_client, ADM1021_REG_CONV_RATE_R) & 0xF8) != 0x00) {
			err = -ENODEV;
			goto error1;
		}
	}

	/* Determine the chip type. */
	if (kind <= 0) {
		i = adm1021_read_value(new_client, ADM1021_REG_MAN_ID);
		if (i == 0x41)
			if ((adm1021_read_value(new_client, ADM1021_REG_DEV_ID) & 0x0F0) == 0x030)
				kind = adm1023;
			else
				kind = adm1021;
		else if (i == 0x49)
			kind = thmc10;
		else if (i == 0x23)
			kind = gl523sm;
		else if ((i == 0x4d) &&
			 (adm1021_read_value(new_client, ADM1021_REG_DEV_ID) == 0x01))
			kind = max1617a;
		else if (i == 0x54)
			kind = mc1066;
		/* LM84 Mfr ID in a different place, and it has more unused bits */
		else if (adm1021_read_value(new_client, ADM1021_REG_CONV_RATE_R) == 0x00
		      && (kind == 0 /* skip extra detection */
		       || ((adm1021_read_value(new_client, ADM1021_REG_CONFIG_R) & 0x7F) == 0x00
			&& (adm1021_read_value(new_client, ADM1021_REG_STATUS) & 0xAB) == 0x00)))
			kind = lm84;
		else
			kind = max1617;
	}

	if (kind == max1617) {
		type_name = "max1617";
	} else if (kind == max1617a) {
		type_name = "max1617a";
	} else if (kind == adm1021) {
		type_name = "adm1021";
	} else if (kind == adm1023) {
		type_name = "adm1023";
	} else if (kind == thmc10) {
		type_name = "thmc10";
	} else if (kind == lm84) {
		type_name = "lm84";
	} else if (kind == gl523sm) {
		type_name = "gl523sm";
	} else if (kind == mc1066) {
		type_name = "mc1066";
	}

	/* Fill in the remaining client fields and put it into the global list */
	strlcpy(new_client->name, type_name, I2C_NAME_SIZE);
	data->type = kind;
	data->valid = 0;
	init_MUTEX(&data->update_lock);

	/* Tell the I2C layer a new client has arrived */
	if ((err = i2c_attach_client(new_client)))
		goto error1;

	/* Initialize the ADM1021 chip */
	if (kind != lm84)
		adm1021_init_client(new_client);

	/* Register sysfs hooks */
	data->class_dev = hwmon_device_register(&new_client->dev);
	if (IS_ERR(data->class_dev)) {
		err = PTR_ERR(data->class_dev);
		goto error2;
	}

	device_create_file(&new_client->dev, &dev_attr_temp1_max);
	device_create_file(&new_client->dev, &dev_attr_temp1_min);
	device_create_file(&new_client->dev, &dev_attr_temp1_input);
	device_create_file(&new_client->dev, &dev_attr_temp2_max);
	device_create_file(&new_client->dev, &dev_attr_temp2_min);
	device_create_file(&new_client->dev, &dev_attr_temp2_input);
	device_create_file(&new_client->dev, &dev_attr_alarms);

	return 0;

error2:
	i2c_detach_client(new_client);
error1:
	kfree(data);
error0:
	return err;
}

static void adm1021_init_client(struct i2c_client *client)
{
	/* Enable ADC and disable suspend mode */
	adm1021_write_value(client, ADM1021_REG_CONFIG_W,
		adm1021_read_value(client, ADM1021_REG_CONFIG_R) & 0xBF);
	/* Set Conversion rate to 1/sec (this can be tinkered with) */
	adm1021_write_value(client, ADM1021_REG_CONV_RATE_W, 0x04);
}

static int adm1021_detach_client(struct i2c_client *client)
{
	struct adm1021_data *data = i2c_get_clientdata(client);
	int err;

	hwmon_device_unregister(data->class_dev);

	if ((err = i2c_detach_client(client)))
		return err;

	kfree(data);
	return 0;
}

/* All registers are byte-sized */
static int adm1021_read_value(struct i2c_client *client, u8 reg)
{
	return i2c_smbus_read_byte_data(client, reg);
}

static int adm1021_write_value(struct i2c_client *client, u8 reg, u16 value)
{
	if (!read_only)
		return i2c_smbus_write_byte_data(client, reg, value);
	return 0;
}

static struct adm1021_data *adm1021_update_device(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct adm1021_data *data = i2c_get_clientdata(client);

	down(&data->update_lock);

	if (time_after(jiffies, data->last_updated + HZ + HZ / 2)
	    || !data->valid) {
		dev_dbg(&client->dev, "Starting adm1021 update\n");

		data->temp_input = adm1021_read_value(client, ADM1021_REG_TEMP);
		data->temp_max = adm1021_read_value(client, ADM1021_REG_TOS_R);
		data->temp_hyst = adm1021_read_value(client, ADM1021_REG_THYST_R);
		data->remote_temp_input = adm1021_read_value(client, ADM1021_REG_REMOTE_TEMP);
		data->remote_temp_max = adm1021_read_value(client, ADM1021_REG_REMOTE_TOS_R);
		data->remote_temp_hyst = adm1021_read_value(client, ADM1021_REG_REMOTE_THYST_R);
		data->alarms = adm1021_read_value(client, ADM1021_REG_STATUS) & 0x7c;
		if (data->type == adm1023) {
			data->remote_temp_prec = adm1021_read_value(client, ADM1021_REG_REM_TEMP_PREC);
			data->remote_temp_os_prec = adm1021_read_value(client, ADM1021_REG_REM_TOS_PREC);
			data->remote_temp_hyst_prec = adm1021_read_value(client, ADM1021_REG_REM_THYST_PREC);
			data->remote_temp_offset = adm1021_read_value(client, ADM1021_REG_REM_OFFSET);
			data->remote_temp_offset_prec = adm1021_read_value(client, ADM1021_REG_REM_OFFSET_PREC);
		}
		data->last_updated = jiffies;
		data->valid = 1;
	}

	up(&data->update_lock);

	return data;
}

static int __init sensors_adm1021_init(void)
{
	return i2c_add_driver(&adm1021_driver);
}

static void __exit sensors_adm1021_exit(void)
{
	i2c_del_driver(&adm1021_driver);
}

MODULE_AUTHOR ("Frodo Looijaard <frodol@dds.nl> and "
		"Philip Edelbrock <phil@netroedge.com>");
MODULE_DESCRIPTION("adm1021 driver");
MODULE_LICENSE("GPL");

module_param(read_only, bool, 0);
MODULE_PARM_DESC(read_only, "Don't set any values, read only mode");

module_init(sensors_adm1021_init)
module_exit(sensors_adm1021_exit)
