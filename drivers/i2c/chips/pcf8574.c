/*
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

/* Addresses to scan: none, device can't be detected */
static const unsigned short normal_i2c[] = { I2C_CLIENT_END };

/* Insmod parameters */
I2C_CLIENT_INSMOD_2(pcf8574, pcf8574a);

/* Each client has this additional data */
struct pcf8574_data {
	int write;			/* Remember last written value */
};

static void pcf8574_init_client(struct i2c_client *client);

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

/* Return 0 if detection is successful, -ENODEV otherwise */
static int pcf8574_detect(struct i2c_client *client, int kind,
			  struct i2c_board_info *info)
{
	struct i2c_adapter *adapter = client->adapter;
	const char *client_name;

	if (!i2c_check_functionality(adapter, I2C_FUNC_SMBUS_BYTE))
		return -ENODEV;

	/* Now, we would do the remaining detection. But the PCF8574 is plainly
	   impossible to detect! Stupid chip. */

	/* Determine the chip type */
	if (kind <= 0) {
		if (client->addr >= 0x38 && client->addr <= 0x3f)
			kind = pcf8574a;
		else
			kind = pcf8574;
	}

	if (kind == pcf8574a)
		client_name = "pcf8574a";
	else
		client_name = "pcf8574";
	strlcpy(info->type, client_name, I2C_NAME_SIZE);

	return 0;
}

static int pcf8574_probe(struct i2c_client *client,
			 const struct i2c_device_id *id)
{
	struct pcf8574_data *data;
	int err;

	data = kzalloc(sizeof(struct pcf8574_data), GFP_KERNEL);
	if (!data) {
		err = -ENOMEM;
		goto exit;
	}

	i2c_set_clientdata(client, data);

	/* Initialize the PCF8574 chip */
	pcf8574_init_client(client);

	/* Register sysfs hooks */
	err = sysfs_create_group(&client->dev.kobj, &pcf8574_attr_group);
	if (err)
		goto exit_free;
	return 0;

      exit_free:
	kfree(data);
      exit:
	return err;
}

static int pcf8574_remove(struct i2c_client *client)
{
	sysfs_remove_group(&client->dev.kobj, &pcf8574_attr_group);
	kfree(i2c_get_clientdata(client));
	return 0;
}

/* Called when we have found a new PCF8574. */
static void pcf8574_init_client(struct i2c_client *client)
{
	struct pcf8574_data *data = i2c_get_clientdata(client);
	data->write = -EAGAIN;
}

static const struct i2c_device_id pcf8574_id[] = {
	{ "pcf8574", 0 },
	{ "pcf8574a", 0 },
	{ }
};

static struct i2c_driver pcf8574_driver = {
	.driver = {
		.name	= "pcf8574",
	},
	.probe		= pcf8574_probe,
	.remove		= pcf8574_remove,
	.id_table	= pcf8574_id,

	.detect		= pcf8574_detect,
	.address_data	= &addr_data,
};

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
