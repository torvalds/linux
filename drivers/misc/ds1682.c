/*
 * Dallas Semiconductor DS1682 Elapsed Time Recorder device driver
 *
 * Written by: Grant Likely <grant.likely@secretlab.ca>
 *
 * Copyright (C) 2007 Secret Lab Technologies Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

/*
 * The DS1682 elapsed timer recorder is a simple device that implements
 * one elapsed time counter, one event counter, an alarm signal and 10
 * bytes of general purpose EEPROM.
 *
 * This driver provides access to the DS1682 counters and user data via
 * the sysfs.  The following attributes are added to the device node:
 *     elapsed_time (u32): Total elapsed event time in ms resolution
 *     alarm_time (u32): When elapsed time exceeds the value in alarm_time,
 *                       then the alarm pin is asserted.
 *     event_count (u16): number of times the event pin has gone low.
 *     eeprom (u8[10]): general purpose EEPROM
 *
 * Counter registers and user data are both read/write unless the device
 * has been write protected.  This driver does not support turning off write
 * protection.  Once write protection is turned on, it is impossible to
 * turn it off again, so I have left the feature out of this driver to avoid
 * accidental enabling, but it is trivial to add write protect support.
 *
 */

#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/string.h>
#include <linux/list.h>
#include <linux/sysfs.h>
#include <linux/ctype.h>
#include <linux/hwmon-sysfs.h>

/* Device registers */
#define DS1682_REG_CONFIG		0x00
#define DS1682_REG_ALARM		0x01
#define DS1682_REG_ELAPSED		0x05
#define DS1682_REG_EVT_CNTR		0x09
#define DS1682_REG_EEPROM		0x0b
#define DS1682_REG_RESET		0x1d
#define DS1682_REG_WRITE_DISABLE	0x1e
#define DS1682_REG_WRITE_MEM_DISABLE	0x1f

#define DS1682_EEPROM_SIZE		10

/*
 * Generic counter attributes
 */
static ssize_t ds1682_show(struct device *dev, struct device_attribute *attr,
			   char *buf)
{
	struct sensor_device_attribute_2 *sattr = to_sensor_dev_attr_2(attr);
	struct i2c_client *client = to_i2c_client(dev);
	__le32 val = 0;
	int rc;

	dev_dbg(dev, "ds1682_show() called on %s\n", attr->attr.name);

	/* Read the register */
	rc = i2c_smbus_read_i2c_block_data(client, sattr->index, sattr->nr,
					   (u8 *) & val);
	if (rc < 0)
		return -EIO;

	/* Special case: the 32 bit regs are time values with 1/4s
	 * resolution, scale them up to milliseconds */
	if (sattr->nr == 4)
		return sprintf(buf, "%llu\n",
			((unsigned long long)le32_to_cpu(val)) * 250);

	/* Format the output string and return # of bytes */
	return sprintf(buf, "%li\n", (long)le32_to_cpu(val));
}

static ssize_t ds1682_store(struct device *dev, struct device_attribute *attr,
			    const char *buf, size_t count)
{
	struct sensor_device_attribute_2 *sattr = to_sensor_dev_attr_2(attr);
	struct i2c_client *client = to_i2c_client(dev);
	u64 val;
	__le32 val_le;
	int rc;

	dev_dbg(dev, "ds1682_store() called on %s\n", attr->attr.name);

	/* Decode input */
	rc = kstrtoull(buf, 0, &val);
	if (rc < 0) {
		dev_dbg(dev, "input string not a number\n");
		return -EINVAL;
	}

	/* Special case: the 32 bit regs are time values with 1/4s
	 * resolution, scale input down to quarter-seconds */
	if (sattr->nr == 4)
		do_div(val, 250);

	/* write out the value */
	val_le = cpu_to_le32(val);
	rc = i2c_smbus_write_i2c_block_data(client, sattr->index, sattr->nr,
					    (u8 *) & val_le);
	if (rc < 0) {
		dev_err(dev, "register write failed; reg=0x%x, size=%i\n",
			sattr->index, sattr->nr);
		return -EIO;
	}

	return count;
}

/*
 * Simple register attributes
 */
static SENSOR_DEVICE_ATTR_2(elapsed_time, S_IRUGO | S_IWUSR, ds1682_show,
			    ds1682_store, 4, DS1682_REG_ELAPSED);
static SENSOR_DEVICE_ATTR_2(alarm_time, S_IRUGO | S_IWUSR, ds1682_show,
			    ds1682_store, 4, DS1682_REG_ALARM);
static SENSOR_DEVICE_ATTR_2(event_count, S_IRUGO | S_IWUSR, ds1682_show,
			    ds1682_store, 2, DS1682_REG_EVT_CNTR);

static const struct attribute_group ds1682_group = {
	.attrs = (struct attribute *[]) {
		&sensor_dev_attr_elapsed_time.dev_attr.attr,
		&sensor_dev_attr_alarm_time.dev_attr.attr,
		&sensor_dev_attr_event_count.dev_attr.attr,
		NULL,
	},
};

/*
 * User data attribute
 */
static ssize_t ds1682_eeprom_read(struct file *filp, struct kobject *kobj,
				  struct bin_attribute *attr,
				  char *buf, loff_t off, size_t count)
{
	struct i2c_client *client = kobj_to_i2c_client(kobj);
	int rc;

	dev_dbg(&client->dev, "ds1682_eeprom_read(p=%p, off=%lli, c=%zi)\n",
		buf, off, count);

	rc = i2c_smbus_read_i2c_block_data(client, DS1682_REG_EEPROM + off,
					   count, buf);
	if (rc < 0)
		return -EIO;

	return count;
}

static ssize_t ds1682_eeprom_write(struct file *filp, struct kobject *kobj,
				   struct bin_attribute *attr,
				   char *buf, loff_t off, size_t count)
{
	struct i2c_client *client = kobj_to_i2c_client(kobj);

	dev_dbg(&client->dev, "ds1682_eeprom_write(p=%p, off=%lli, c=%zi)\n",
		buf, off, count);

	/* Write out to the device */
	if (i2c_smbus_write_i2c_block_data(client, DS1682_REG_EEPROM + off,
					   count, buf) < 0)
		return -EIO;

	return count;
}

static struct bin_attribute ds1682_eeprom_attr = {
	.attr = {
		.name = "eeprom",
		.mode = S_IRUGO | S_IWUSR,
	},
	.size = DS1682_EEPROM_SIZE,
	.read = ds1682_eeprom_read,
	.write = ds1682_eeprom_write,
};

/*
 * Called when a ds1682 device is matched with this driver
 */
static int ds1682_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	int rc;

	if (!i2c_check_functionality(client->adapter,
				     I2C_FUNC_SMBUS_I2C_BLOCK)) {
		dev_err(&client->dev, "i2c bus does not support the ds1682\n");
		rc = -ENODEV;
		goto exit;
	}

	rc = sysfs_create_group(&client->dev.kobj, &ds1682_group);
	if (rc)
		goto exit;

	rc = sysfs_create_bin_file(&client->dev.kobj, &ds1682_eeprom_attr);
	if (rc)
		goto exit_bin_attr;

	return 0;

 exit_bin_attr:
	sysfs_remove_group(&client->dev.kobj, &ds1682_group);
 exit:
	return rc;
}

static int ds1682_remove(struct i2c_client *client)
{
	sysfs_remove_bin_file(&client->dev.kobj, &ds1682_eeprom_attr);
	sysfs_remove_group(&client->dev.kobj, &ds1682_group);
	return 0;
}

static const struct i2c_device_id ds1682_id[] = {
	{ "ds1682", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, ds1682_id);

static const struct of_device_id ds1682_of_match[] = {
	{ .compatible = "dallas,ds1682", },
	{}
};
MODULE_DEVICE_TABLE(of, ds1682_of_match);

static struct i2c_driver ds1682_driver = {
	.driver = {
		.name = "ds1682",
		.of_match_table = ds1682_of_match,
	},
	.probe = ds1682_probe,
	.remove = ds1682_remove,
	.id_table = ds1682_id,
};

module_i2c_driver(ds1682_driver);

MODULE_AUTHOR("Grant Likely <grant.likely@secretlab.ca>");
MODULE_DESCRIPTION("DS1682 Elapsed Time Indicator driver");
MODULE_LICENSE("GPL");
