// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * atxp1.c - kernel module for setting CPU VID and general purpose
 *	     I/Os using the Attansic ATXP1 chip.
 *
 * The ATXP1 can reside on I2C addresses 0x37 or 0x4e. The chip is
 * not auto-detected by the driver and must be instantiated explicitly.
 * See Documentation/i2c/instantiating-devices.rst for more information.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/jiffies.h>
#include <linux/i2c.h>
#include <linux/hwmon.h>
#include <linux/hwmon-vid.h>
#include <linux/err.h>
#include <linux/mutex.h>
#include <linux/sysfs.h>
#include <linux/slab.h>

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("System voltages control via Attansic ATXP1");
MODULE_VERSION("0.6.3");
MODULE_AUTHOR("Sebastian Witt <se.witt@gmx.net>");

#define ATXP1_VID	0x00
#define ATXP1_CVID	0x01
#define ATXP1_GPIO1	0x06
#define ATXP1_GPIO2	0x0a
#define ATXP1_VIDENA	0x20
#define ATXP1_VIDMASK	0x1f
#define ATXP1_GPIO1MASK	0x0f

struct atxp1_data {
	struct i2c_client *client;
	struct mutex update_lock;
	unsigned long last_updated;
	u8 valid;
	struct {
		u8 vid;		/* VID output register */
		u8 cpu_vid; /* VID input from CPU */
		u8 gpio1;   /* General purpose I/O register 1 */
		u8 gpio2;   /* General purpose I/O register 2 */
	} reg;
	u8 vrm;			/* Detected CPU VRM */
};

static struct atxp1_data *atxp1_update_device(struct device *dev)
{
	struct atxp1_data *data = dev_get_drvdata(dev);
	struct i2c_client *client = data->client;

	mutex_lock(&data->update_lock);

	if (time_after(jiffies, data->last_updated + HZ) || !data->valid) {

		/* Update local register data */
		data->reg.vid = i2c_smbus_read_byte_data(client, ATXP1_VID);
		data->reg.cpu_vid = i2c_smbus_read_byte_data(client,
							     ATXP1_CVID);
		data->reg.gpio1 = i2c_smbus_read_byte_data(client, ATXP1_GPIO1);
		data->reg.gpio2 = i2c_smbus_read_byte_data(client, ATXP1_GPIO2);

		data->valid = 1;
	}

	mutex_unlock(&data->update_lock);

	return data;
}

/* sys file functions for cpu0_vid */
static ssize_t cpu0_vid_show(struct device *dev,
			     struct device_attribute *attr, char *buf)
{
	int size;
	struct atxp1_data *data;

	data = atxp1_update_device(dev);

	size = sprintf(buf, "%d\n", vid_from_reg(data->reg.vid & ATXP1_VIDMASK,
						 data->vrm));

	return size;
}

static ssize_t cpu0_vid_store(struct device *dev,
			      struct device_attribute *attr, const char *buf,
			      size_t count)
{
	struct atxp1_data *data = atxp1_update_device(dev);
	struct i2c_client *client = data->client;
	int vid, cvid;
	unsigned long vcore;
	int err;

	err = kstrtoul(buf, 10, &vcore);
	if (err)
		return err;

	vcore /= 25;
	vcore *= 25;

	/* Calculate VID */
	vid = vid_to_reg(vcore, data->vrm);
	if (vid < 0) {
		dev_err(dev, "VID calculation failed.\n");
		return vid;
	}

	/*
	 * If output enabled, use control register value.
	 * Otherwise original CPU VID
	 */
	if (data->reg.vid & ATXP1_VIDENA)
		cvid = data->reg.vid & ATXP1_VIDMASK;
	else
		cvid = data->reg.cpu_vid;

	/* Nothing changed, aborting */
	if (vid == cvid)
		return count;

	dev_dbg(dev, "Setting VCore to %d mV (0x%02x)\n", (int)vcore, vid);

	/* Write every 25 mV step to increase stability */
	if (cvid > vid) {
		for (; cvid >= vid; cvid--)
			i2c_smbus_write_byte_data(client,
						ATXP1_VID, cvid | ATXP1_VIDENA);
	} else {
		for (; cvid <= vid; cvid++)
			i2c_smbus_write_byte_data(client,
						ATXP1_VID, cvid | ATXP1_VIDENA);
	}

	data->valid = 0;

	return count;
}

/*
 * CPU core reference voltage
 * unit: millivolt
 */
static DEVICE_ATTR_RW(cpu0_vid);

/* sys file functions for GPIO1 */
static ssize_t gpio1_show(struct device *dev, struct device_attribute *attr,
			  char *buf)
{
	int size;
	struct atxp1_data *data;

	data = atxp1_update_device(dev);

	size = sprintf(buf, "0x%02x\n", data->reg.gpio1 & ATXP1_GPIO1MASK);

	return size;
}

static ssize_t gpio1_store(struct device *dev, struct device_attribute *attr,
			   const char *buf, size_t count)
{
	struct atxp1_data *data = atxp1_update_device(dev);
	struct i2c_client *client = data->client;
	unsigned long value;
	int err;

	err = kstrtoul(buf, 16, &value);
	if (err)
		return err;

	value &= ATXP1_GPIO1MASK;

	if (value != (data->reg.gpio1 & ATXP1_GPIO1MASK)) {
		dev_info(dev, "Writing 0x%x to GPIO1.\n", (unsigned int)value);

		i2c_smbus_write_byte_data(client, ATXP1_GPIO1, value);

		data->valid = 0;
	}

	return count;
}

/*
 * GPIO1 data register
 * unit: Four bit as hex (e.g. 0x0f)
 */
static DEVICE_ATTR_RW(gpio1);

/* sys file functions for GPIO2 */
static ssize_t gpio2_show(struct device *dev, struct device_attribute *attr,
			  char *buf)
{
	int size;
	struct atxp1_data *data;

	data = atxp1_update_device(dev);

	size = sprintf(buf, "0x%02x\n", data->reg.gpio2);

	return size;
}

static ssize_t gpio2_store(struct device *dev, struct device_attribute *attr,
			   const char *buf, size_t count)
{
	struct atxp1_data *data = atxp1_update_device(dev);
	struct i2c_client *client = data->client;
	unsigned long value;
	int err;

	err = kstrtoul(buf, 16, &value);
	if (err)
		return err;
	value &= 0xff;

	if (value != data->reg.gpio2) {
		dev_info(dev, "Writing 0x%x to GPIO1.\n", (unsigned int)value);

		i2c_smbus_write_byte_data(client, ATXP1_GPIO2, value);

		data->valid = 0;
	}

	return count;
}

/*
 * GPIO2 data register
 * unit: Eight bit as hex (e.g. 0xff)
 */
static DEVICE_ATTR_RW(gpio2);

static struct attribute *atxp1_attrs[] = {
	&dev_attr_gpio1.attr,
	&dev_attr_gpio2.attr,
	&dev_attr_cpu0_vid.attr,
	NULL
};
ATTRIBUTE_GROUPS(atxp1);

static int atxp1_probe(struct i2c_client *client,
		       const struct i2c_device_id *id)
{
	struct device *dev = &client->dev;
	struct atxp1_data *data;
	struct device *hwmon_dev;

	data = devm_kzalloc(dev, sizeof(struct atxp1_data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	/* Get VRM */
	data->vrm = vid_which_vrm();
	if (data->vrm != 90 && data->vrm != 91) {
		dev_err(dev, "atxp1: Not supporting VRM %d.%d\n",
			data->vrm / 10, data->vrm % 10);
		return -ENODEV;
	}

	data->client = client;
	mutex_init(&data->update_lock);

	hwmon_dev = devm_hwmon_device_register_with_groups(dev, client->name,
							   data,
							   atxp1_groups);
	if (IS_ERR(hwmon_dev))
		return PTR_ERR(hwmon_dev);

	dev_info(dev, "Using VRM: %d.%d\n", data->vrm / 10, data->vrm % 10);

	return 0;
};

static const struct i2c_device_id atxp1_id[] = {
	{ "atxp1", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, atxp1_id);

static struct i2c_driver atxp1_driver = {
	.class		= I2C_CLASS_HWMON,
	.driver = {
		.name	= "atxp1",
	},
	.probe		= atxp1_probe,
	.id_table	= atxp1_id,
};

module_i2c_driver(atxp1_driver);
