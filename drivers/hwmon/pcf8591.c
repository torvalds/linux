// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2001-2004 Aurelien Jarno <aurelien@aurel32.net>
 * Ported to Linux 2.6 by Aurelien Jarno <aurelien@aurel32.net> with
 * the help of Jean Delvare <jdelvare@suse.de>
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/mutex.h>
#include <linux/err.h>
#include <linux/hwmon.h>
#include <linux/kstrtox.h>

/* Insmod parameters */

static int input_mode;
module_param(input_mode, int, 0);
MODULE_PARM_DESC(input_mode,
	"Analog input mode:\n"
	" 0 = four single ended inputs\n"
	" 1 = three differential inputs\n"
	" 2 = single ended and differential mixed\n"
	" 3 = two differential inputs\n");

/*
 * The PCF8591 control byte
 *      7    6    5    4    3    2    1    0
 *   |  0 |AOEF|   AIP   |  0 |AINC|  AICH   |
 */

/* Analog Output Enable Flag (analog output active if 1) */
#define PCF8591_CONTROL_AOEF		0x40

/*
 * Analog Input Programming
 * 0x00 = four single ended inputs
 * 0x10 = three differential inputs
 * 0x20 = single ended and differential mixed
 * 0x30 = two differential inputs
 */
#define PCF8591_CONTROL_AIP_MASK	0x30

/* Autoincrement Flag (switch on if 1) */
#define PCF8591_CONTROL_AINC		0x04

/*
 * Channel selection
 * 0x00 = channel 0
 * 0x01 = channel 1
 * 0x02 = channel 2
 * 0x03 = channel 3
 */
#define PCF8591_CONTROL_AICH_MASK	0x03

/* Initial values */
#define PCF8591_INIT_CONTROL	((input_mode << 4) | PCF8591_CONTROL_AOEF)
#define PCF8591_INIT_AOUT	0	/* DAC out = 0 */

/* Conversions */
#define REG_TO_SIGNED(reg)	(((reg) & 0x80) ? ((reg) - 256) : (reg))

struct pcf8591_data {
	struct device *hwmon_dev;
	struct mutex update_lock;

	u8 control;
	u8 aout;
};

static void pcf8591_init_client(struct i2c_client *client);
static int pcf8591_read_channel(struct device *dev, int channel);

/* following are the sysfs callback functions */
#define show_in_channel(channel)					\
static ssize_t show_in##channel##_input(struct device *dev,		\
					struct device_attribute *attr,	\
					char *buf)			\
{									\
	return sprintf(buf, "%d\n", pcf8591_read_channel(dev, channel));\
}									\
static DEVICE_ATTR(in##channel##_input, S_IRUGO,			\
		   show_in##channel##_input, NULL);

show_in_channel(0);
show_in_channel(1);
show_in_channel(2);
show_in_channel(3);

static ssize_t out0_output_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct pcf8591_data *data = i2c_get_clientdata(to_i2c_client(dev));
	return sprintf(buf, "%d\n", data->aout * 10);
}

static ssize_t out0_output_store(struct device *dev,
				 struct device_attribute *attr,
				 const char *buf, size_t count)
{
	unsigned long val;
	struct i2c_client *client = to_i2c_client(dev);
	struct pcf8591_data *data = i2c_get_clientdata(client);
	int err;

	err = kstrtoul(buf, 10, &val);
	if (err)
		return err;

	val /= 10;
	if (val > 255)
		return -EINVAL;

	data->aout = val;
	i2c_smbus_write_byte_data(client, data->control, data->aout);
	return count;
}

static DEVICE_ATTR_RW(out0_output);

static ssize_t out0_enable_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct pcf8591_data *data = i2c_get_clientdata(to_i2c_client(dev));
	return sprintf(buf, "%u\n", !(!(data->control & PCF8591_CONTROL_AOEF)));
}

static ssize_t out0_enable_store(struct device *dev,
				 struct device_attribute *attr,
				 const char *buf, size_t count)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct pcf8591_data *data = i2c_get_clientdata(client);
	unsigned long val;
	int err;

	err = kstrtoul(buf, 10, &val);
	if (err)
		return err;

	mutex_lock(&data->update_lock);
	if (val)
		data->control |= PCF8591_CONTROL_AOEF;
	else
		data->control &= ~PCF8591_CONTROL_AOEF;
	i2c_smbus_write_byte(client, data->control);
	mutex_unlock(&data->update_lock);
	return count;
}

static DEVICE_ATTR_RW(out0_enable);

static struct attribute *pcf8591_attributes[] = {
	&dev_attr_out0_enable.attr,
	&dev_attr_out0_output.attr,
	&dev_attr_in0_input.attr,
	&dev_attr_in1_input.attr,
	NULL
};

static const struct attribute_group pcf8591_attr_group = {
	.attrs = pcf8591_attributes,
};

static struct attribute *pcf8591_attributes_opt[] = {
	&dev_attr_in2_input.attr,
	&dev_attr_in3_input.attr,
	NULL
};

static const struct attribute_group pcf8591_attr_group_opt = {
	.attrs = pcf8591_attributes_opt,
};

/*
 * Real code
 */

static int pcf8591_probe(struct i2c_client *client)
{
	struct pcf8591_data *data;
	int err;

	data = devm_kzalloc(&client->dev, sizeof(struct pcf8591_data),
			    GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	i2c_set_clientdata(client, data);
	mutex_init(&data->update_lock);

	/* Initialize the PCF8591 chip */
	pcf8591_init_client(client);

	/* Register sysfs hooks */
	err = sysfs_create_group(&client->dev.kobj, &pcf8591_attr_group);
	if (err)
		return err;

	/* Register input2 if not in "two differential inputs" mode */
	if (input_mode != 3) {
		err = device_create_file(&client->dev, &dev_attr_in2_input);
		if (err)
			goto exit_sysfs_remove;
	}

	/* Register input3 only in "four single ended inputs" mode */
	if (input_mode == 0) {
		err = device_create_file(&client->dev, &dev_attr_in3_input);
		if (err)
			goto exit_sysfs_remove;
	}

	data->hwmon_dev = hwmon_device_register(&client->dev);
	if (IS_ERR(data->hwmon_dev)) {
		err = PTR_ERR(data->hwmon_dev);
		goto exit_sysfs_remove;
	}

	return 0;

exit_sysfs_remove:
	sysfs_remove_group(&client->dev.kobj, &pcf8591_attr_group_opt);
	sysfs_remove_group(&client->dev.kobj, &pcf8591_attr_group);
	return err;
}

static void pcf8591_remove(struct i2c_client *client)
{
	struct pcf8591_data *data = i2c_get_clientdata(client);

	hwmon_device_unregister(data->hwmon_dev);
	sysfs_remove_group(&client->dev.kobj, &pcf8591_attr_group_opt);
	sysfs_remove_group(&client->dev.kobj, &pcf8591_attr_group);
}

/* Called when we have found a new PCF8591. */
static void pcf8591_init_client(struct i2c_client *client)
{
	struct pcf8591_data *data = i2c_get_clientdata(client);
	data->control = PCF8591_INIT_CONTROL;
	data->aout = PCF8591_INIT_AOUT;

	i2c_smbus_write_byte_data(client, data->control, data->aout);

	/*
	 * The first byte transmitted contains the conversion code of the
	 * previous read cycle. FLUSH IT!
	 */
	i2c_smbus_read_byte(client);
}

static int pcf8591_read_channel(struct device *dev, int channel)
{
	u8 value;
	struct i2c_client *client = to_i2c_client(dev);
	struct pcf8591_data *data = i2c_get_clientdata(client);

	mutex_lock(&data->update_lock);

	if ((data->control & PCF8591_CONTROL_AICH_MASK) != channel) {
		data->control = (data->control & ~PCF8591_CONTROL_AICH_MASK)
			      | channel;
		i2c_smbus_write_byte(client, data->control);

		/*
		 * The first byte transmitted contains the conversion code of
		 * the previous read cycle. FLUSH IT!
		 */
		i2c_smbus_read_byte(client);
	}
	value = i2c_smbus_read_byte(client);

	mutex_unlock(&data->update_lock);

	if ((channel == 2 && input_mode == 2) ||
	    (channel != 3 && (input_mode == 1 || input_mode == 3)))
		return 10 * REG_TO_SIGNED(value);
	else
		return 10 * value;
}

static const struct i2c_device_id pcf8591_id[] = {
	{ "pcf8591", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, pcf8591_id);

static struct i2c_driver pcf8591_driver = {
	.driver = {
		.name	= "pcf8591",
	},
	.probe_new	= pcf8591_probe,
	.remove		= pcf8591_remove,
	.id_table	= pcf8591_id,
};

static int __init pcf8591_init(void)
{
	if (input_mode < 0 || input_mode > 3) {
		pr_warn("invalid input_mode (%d)\n", input_mode);
		input_mode = 0;
	}
	return i2c_add_driver(&pcf8591_driver);
}

static void __exit pcf8591_exit(void)
{
	i2c_del_driver(&pcf8591_driver);
}

MODULE_AUTHOR("Aurelien Jarno <aurelien@aurel32.net>");
MODULE_DESCRIPTION("PCF8591 driver");
MODULE_LICENSE("GPL");

module_init(pcf8591_init);
module_exit(pcf8591_exit);
