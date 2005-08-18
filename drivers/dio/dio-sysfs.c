/*
 *  File Attributes for DIO Devices
 *
 *  Copyright (C) 2004 Jochen Friedrich
 *
 *  Loosely based on drivers/pci/pci-sysfs.c and drivers/zorro/zorro-sysfs.c
 *
 *  This file is subject to the terms and conditions of the GNU General Public
 *  License.  See the file COPYING in the main directory of this archive
 *  for more details.
 */


#include <linux/kernel.h>
#include <linux/dio.h>
#include <linux/stat.h>

/* show configuration fields */

static ssize_t dio_show_id(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct dio_dev *d;

	d = to_dio_dev(dev);
	return sprintf(buf, "0x%02x\n", (d->id & 0xff));
}
static DEVICE_ATTR(id, S_IRUGO, dio_show_id, NULL);

static ssize_t dio_show_ipl(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct dio_dev *d;

	d = to_dio_dev(dev);
	return sprintf(buf, "0x%02x\n", d->ipl);
}
static DEVICE_ATTR(ipl, S_IRUGO, dio_show_ipl, NULL);

static ssize_t dio_show_secid(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct dio_dev *d;

	d = to_dio_dev(dev);
	return sprintf(buf, "0x%02x\n", ((d->id >> 8)& 0xff));
}
static DEVICE_ATTR(secid, S_IRUGO, dio_show_secid, NULL);

static ssize_t dio_show_name(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct dio_dev *d;

	d = to_dio_dev(dev);
	return sprintf(buf, "%s\n", d->name);
}
static DEVICE_ATTR(name, S_IRUGO, dio_show_name, NULL);

static ssize_t dio_show_resource(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct dio_dev *d = to_dio_dev(dev);

	return sprintf(buf, "0x%08lx 0x%08lx 0x%08lx\n",
		       dio_resource_start(d), dio_resource_end(d),
		       dio_resource_flags(d));
}
static DEVICE_ATTR(resource, S_IRUGO, dio_show_resource, NULL);

void dio_create_sysfs_dev_files(struct dio_dev *d)
{
	struct device *dev = &d->dev;

	/* current configuration's attributes */
	device_create_file(dev, &dev_attr_id);
	device_create_file(dev, &dev_attr_ipl);
	device_create_file(dev, &dev_attr_secid);
	device_create_file(dev, &dev_attr_name);
	device_create_file(dev, &dev_attr_resource);
}

