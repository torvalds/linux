/*
    pca9539.c - 16-bit I/O port with interrupt and reset

    Copyright (C) 2005 Ben Gardner <bgardner@wabtec.com>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; version 2 of the License.
*/

#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/hwmon-sysfs.h>

/* Addresses to scan */
static unsigned short normal_i2c[] = {0x74, 0x75, 0x76, 0x77, I2C_CLIENT_END};

/* Insmod parameters */
I2C_CLIENT_INSMOD_1(pca9539);

enum pca9539_cmd
{
	PCA9539_INPUT_0		= 0,
	PCA9539_INPUT_1		= 1,
	PCA9539_OUTPUT_0	= 2,
	PCA9539_OUTPUT_1	= 3,
	PCA9539_INVERT_0	= 4,
	PCA9539_INVERT_1	= 5,
	PCA9539_DIRECTION_0	= 6,
	PCA9539_DIRECTION_1	= 7,
};

static int pca9539_attach_adapter(struct i2c_adapter *adapter);
static int pca9539_detect(struct i2c_adapter *adapter, int address, int kind);
static int pca9539_detach_client(struct i2c_client *client);

/* This is the driver that will be inserted */
static struct i2c_driver pca9539_driver = {
	.owner		= THIS_MODULE,
	.name		= "pca9539",
	.flags		= I2C_DF_NOTIFY,
	.attach_adapter	= pca9539_attach_adapter,
	.detach_client	= pca9539_detach_client,
};

struct pca9539_data {
	struct i2c_client client;
};

/* following are the sysfs callback functions */
static ssize_t pca9539_show(struct device *dev, struct device_attribute *attr,
			    char *buf)
{
	struct sensor_device_attribute *psa = to_sensor_dev_attr(attr);
	struct i2c_client *client = to_i2c_client(dev);
	return sprintf(buf, "%d\n", i2c_smbus_read_byte_data(client,
							     psa->index));
}

static ssize_t pca9539_store(struct device *dev, struct device_attribute *attr,
			     const char *buf, size_t count)
{
	struct sensor_device_attribute *psa = to_sensor_dev_attr(attr);
	struct i2c_client *client = to_i2c_client(dev);
	unsigned long val = simple_strtoul(buf, NULL, 0);
	if (val > 0xff)
		return -EINVAL;
	i2c_smbus_write_byte_data(client, psa->index, val);
	return count;
}

/* Define the device attributes */

#define PCA9539_ENTRY_RO(name, cmd_idx) \
	static SENSOR_DEVICE_ATTR(name, S_IRUGO, pca9539_show, NULL, cmd_idx)

#define PCA9539_ENTRY_RW(name, cmd_idx) \
	static SENSOR_DEVICE_ATTR(name, S_IRUGO | S_IWUSR, pca9539_show, \
				  pca9539_store, cmd_idx)

PCA9539_ENTRY_RO(input0, PCA9539_INPUT_0);
PCA9539_ENTRY_RO(input1, PCA9539_INPUT_1);
PCA9539_ENTRY_RW(output0, PCA9539_OUTPUT_0);
PCA9539_ENTRY_RW(output1, PCA9539_OUTPUT_1);
PCA9539_ENTRY_RW(invert0, PCA9539_INVERT_0);
PCA9539_ENTRY_RW(invert1, PCA9539_INVERT_1);
PCA9539_ENTRY_RW(direction0, PCA9539_DIRECTION_0);
PCA9539_ENTRY_RW(direction1, PCA9539_DIRECTION_1);

static struct attribute *pca9539_attributes[] = {
	&sensor_dev_attr_input0.dev_attr.attr,
	&sensor_dev_attr_input1.dev_attr.attr,
	&sensor_dev_attr_output0.dev_attr.attr,
	&sensor_dev_attr_output1.dev_attr.attr,
	&sensor_dev_attr_invert0.dev_attr.attr,
	&sensor_dev_attr_invert1.dev_attr.attr,
	&sensor_dev_attr_direction0.dev_attr.attr,
	&sensor_dev_attr_direction1.dev_attr.attr,
	NULL
};

static struct attribute_group pca9539_defattr_group = {
	.attrs = pca9539_attributes,
};

static int pca9539_attach_adapter(struct i2c_adapter *adapter)
{
	return i2c_probe(adapter, &addr_data, pca9539_detect);
}

/* This function is called by i2c_probe */
static int pca9539_detect(struct i2c_adapter *adapter, int address, int kind)
{
	struct i2c_client *new_client;
	struct pca9539_data *data;
	int err = 0;

	if (!i2c_check_functionality(adapter, I2C_FUNC_SMBUS_BYTE_DATA))
		goto exit;

	/* OK. For now, we presume we have a valid client. We now create the
	   client structure, even though we cannot fill it completely yet. */
	if (!(data = kzalloc(sizeof(struct pca9539_data), GFP_KERNEL))) {
		err = -ENOMEM;
		goto exit;
	}

	new_client = &data->client;
	i2c_set_clientdata(new_client, data);
	new_client->addr = address;
	new_client->adapter = adapter;
	new_client->driver = &pca9539_driver;
	new_client->flags = 0;

	/* Detection: the pca9539 only has 8 registers (0-7).
	   A read of 7 should succeed, but a read of 8 should fail. */
	if ((i2c_smbus_read_byte_data(new_client, 7) < 0) ||
	    (i2c_smbus_read_byte_data(new_client, 8) >= 0))
		goto exit_kfree;

	strlcpy(new_client->name, "pca9539", I2C_NAME_SIZE);

	/* Tell the I2C layer a new client has arrived */
	if ((err = i2c_attach_client(new_client)))
		goto exit_kfree;

	/* Register sysfs hooks (don't care about failure) */
	sysfs_create_group(&new_client->dev.kobj, &pca9539_defattr_group);

	return 0;

exit_kfree:
	kfree(data);
exit:
	return err;
}

static int pca9539_detach_client(struct i2c_client *client)
{
	int err;

	if ((err = i2c_detach_client(client)))
		return err;

	kfree(i2c_get_clientdata(client));
	return 0;
}

static int __init pca9539_init(void)
{
	return i2c_add_driver(&pca9539_driver);
}

static void __exit pca9539_exit(void)
{
	i2c_del_driver(&pca9539_driver);
}

MODULE_AUTHOR("Ben Gardner <bgardner@wabtec.com>");
MODULE_DESCRIPTION("PCA9539 driver");
MODULE_LICENSE("GPL");

module_init(pca9539_init);
module_exit(pca9539_exit);

