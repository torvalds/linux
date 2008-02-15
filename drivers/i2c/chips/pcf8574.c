/*
    pcf8574.c - Part of lm_sensors, Linux kernel modules for hardware
             monitoring
    Copyright (c) 2000  Frodo Looijaard <frodol@dds.nl>, 
                        Philip Edelbrock <phil@netroedge.com>,
                        Dan Eaton <dan.eaton@rocketlogix.com>
    Ported to Linux 2.6 by Aurelien Jarno <aurel32@debian.org> with 
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

/* A few notes about the PCF8574:

* The PCF8574 is an 8-bit I/O expander for the I2C bus produced by
  Philips Semiconductors.  It is designed to provide a byte I2C
  interface to up to 8 separate devices.
  
* The PCF8574 appears as a very simple SMBus device which can be
  read from or written to with SMBUS byte read/write accesses.

  --Dan

*/

#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/i2c.h>

/* Addresses to scan */
static const unsigned short normal_i2c[] = {
	0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27,
	0x38, 0x39, 0x3a, 0x3b, 0x3c, 0x3d, 0x3e, 0x3f,
	I2C_CLIENT_END
};

/* Insmod parameters */
I2C_CLIENT_INSMOD_2(pcf8574, pcf8574a);

/* Each client has this additional data */
struct pcf8574_data {
	struct i2c_client client;

	int write;			/* Remember last written value */
};

static int pcf8574_attach_adapter(struct i2c_adapter *adapter);
static int pcf8574_detect(struct i2c_adapter *adapter, int address, int kind);
static int pcf8574_detach_client(struct i2c_client *client);
static void pcf8574_init_client(struct i2c_client *client);

/* This is the driver that will be inserted */
static struct i2c_driver pcf8574_driver = {
	.driver = {
		.name	= "pcf8574",
	},
	.attach_adapter	= pcf8574_attach_adapter,
	.detach_client	= pcf8574_detach_client,
};

/* following are the sysfs callback functions */
static ssize_t show_read(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	return sprintf(buf, "%u\n", i2c_smbus_read_byte(client));
}

static DEVICE_ATTR(read, S_IRUGO, show_read, NULL);

static ssize_t show_write(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct pcf8574_data *data = i2c_get_clientdata(to_i2c_client(dev));

	if (data->write < 0)
		return data->write;

	return sprintf(buf, "%d\n", data->write);
}

static ssize_t set_write(struct device *dev, struct device_attribute *attr, const char *buf,
			 size_t count)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct pcf8574_data *data = i2c_get_clientdata(client);
	unsigned long val = simple_strtoul(buf, NULL, 10);

	if (val > 0xff)
		return -EINVAL;

	data->write = val;
	i2c_smbus_write_byte(client, data->write);
	return count;
}

static DEVICE_ATTR(write, S_IWUSR | S_IRUGO, show_write, set_write);

static struct attribute *pcf8574_attributes[] = {
	&dev_attr_read.attr,
	&dev_attr_write.attr,
	NULL
};

static const struct attribute_group pcf8574_attr_group = {
	.attrs = pcf8574_attributes,
};

/*
 * Real code
 */

static int pcf8574_attach_adapter(struct i2c_adapter *adapter)
{
	return i2c_probe(adapter, &addr_data, pcf8574_detect);
}

/* This function is called by i2c_probe */
static int pcf8574_detect(struct i2c_adapter *adapter, int address, int kind)
{
	struct i2c_client *new_client;
	struct pcf8574_data *data;
	int err = 0;
	const char *client_name = "";

	if (!i2c_check_functionality(adapter, I2C_FUNC_SMBUS_BYTE))
		goto exit;

	/* OK. For now, we presume we have a valid client. We now create the
	   client structure, even though we cannot fill it completely yet. */
	if (!(data = kzalloc(sizeof(struct pcf8574_data), GFP_KERNEL))) {
		err = -ENOMEM;
		goto exit;
	}

	new_client = &data->client;
	i2c_set_clientdata(new_client, data);
	new_client->addr = address;
	new_client->adapter = adapter;
	new_client->driver = &pcf8574_driver;
	new_client->flags = 0;

	/* Now, we would do the remaining detection. But the PCF8574 is plainly
	   impossible to detect! Stupid chip. */

	/* Determine the chip type */
	if (kind <= 0) {
		if (address >= 0x38 && address <= 0x3f)
			kind = pcf8574a;
		else
			kind = pcf8574;
	}

	if (kind == pcf8574a)
		client_name = "pcf8574a";
	else
		client_name = "pcf8574";

	/* Fill in the remaining client fields and put it into the global list */
	strlcpy(new_client->name, client_name, I2C_NAME_SIZE);

	/* Tell the I2C layer a new client has arrived */
	if ((err = i2c_attach_client(new_client)))
		goto exit_free;
	
	/* Initialize the PCF8574 chip */
	pcf8574_init_client(new_client);

	/* Register sysfs hooks */
	err = sysfs_create_group(&new_client->dev.kobj, &pcf8574_attr_group);
	if (err)
		goto exit_detach;
	return 0;

      exit_detach:
	i2c_detach_client(new_client);
      exit_free:
	kfree(data);
      exit:
	return err;
}

static int pcf8574_detach_client(struct i2c_client *client)
{
	int err;

	sysfs_remove_group(&client->dev.kobj, &pcf8574_attr_group);

	if ((err = i2c_detach_client(client)))
		return err;

	kfree(i2c_get_clientdata(client));
	return 0;
}

/* Called when we have found a new PCF8574. */
static void pcf8574_init_client(struct i2c_client *client)
{
	struct pcf8574_data *data = i2c_get_clientdata(client);
	data->write = -EAGAIN;
}

static int __init pcf8574_init(void)
{
	return i2c_add_driver(&pcf8574_driver);
}

static void __exit pcf8574_exit(void)
{
	i2c_del_driver(&pcf8574_driver);
}


MODULE_AUTHOR
    ("Frodo Looijaard <frodol@dds.nl>, "
     "Philip Edelbrock <phil@netroedge.com>, "
     "Dan Eaton <dan.eaton@rocketlogix.com> "
     "and Aurelien Jarno <aurelien@aurel32.net>");
MODULE_DESCRIPTION("PCF8574 driver");
MODULE_LICENSE("GPL");

module_init(pcf8574_init);
module_exit(pcf8574_exit);
