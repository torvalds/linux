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

/* Addresses to scan: none, device is not autodetected */
static const unsigned short normal_i2c[] = { I2C_CLIENT_END };

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

/* Return 0 if detection is successful, -ENODEV otherwise */
static int pca9539_detect(struct i2c_client *client, int kind,
			  struct i2c_board_info *info)
{
	struct i2c_adapter *adapter = client->adapter;

	if (!i2c_check_functionality(adapter, I2C_FUNC_SMBUS_BYTE_DATA))
		return -ENODEV;

	strlcpy(info->type, "pca9539", I2C_NAME_SIZE);

	return 0;
}

static int pca9539_probe(struct i2c_client *client,
			 const struct i2c_device_id *id)
{
	/* Register sysfs hooks */
	return sysfs_create_group(&client->dev.kobj,
				  &pca9539_defattr_group);
}

static int pca9539_remove(struct i2c_client *client)
{
	sysfs_remove_group(&client->dev.kobj, &pca9539_defattr_group);
	return 0;
}

static const struct i2c_device_id pca9539_id[] = {
	{ "pca9539", 0 },
	{ }
};

static struct i2c_driver pca9539_driver = {
	.driver = {
		.name	= "pca9539",
	},
	.probe		= pca9539_probe,
	.remove		= pca9539_remove,
	.id_table	= pca9539_id,

	.detect		= pca9539_detect,
	.address_data	= &addr_data,
};

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

