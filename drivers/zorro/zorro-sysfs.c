/*
 *  File Attributes for Zorro Devices
 *
 *  Copyright (C) 2003 Geert Uytterhoeven
 *
 *  Loosely based on drivers/pci/pci-sysfs.c
 *
 *  This file is subject to the terms and conditions of the GNU General Public
 *  License.  See the file COPYING in the main directory of this archive
 *  for more details.
 */


#include <linux/kernel.h>
#include <linux/zorro.h>
#include <linux/stat.h>
#include <linux/string.h>

#include <asm/byteorder.h>

#include "zorro.h"


/* show configuration fields */
#define zorro_config_attr(name, field, format_string)			\
static ssize_t name##_show(struct device *dev,				\
			   struct device_attribute *attr, char *buf)	\
{									\
	struct zorro_dev *z;						\
									\
	z = to_zorro_dev(dev);						\
	return sprintf(buf, format_string, z->field);			\
}									\
static DEVICE_ATTR_RO(name);

zorro_config_attr(id, id, "0x%08x\n");
zorro_config_attr(type, rom.er_Type, "0x%02x\n");
zorro_config_attr(slotaddr, slotaddr, "0x%04x\n");
zorro_config_attr(slotsize, slotsize, "0x%04x\n");

static ssize_t serial_show(struct device *dev, struct device_attribute *attr,
			   char *buf)
{
	struct zorro_dev *z;

	z = to_zorro_dev(dev);
	return sprintf(buf, "0x%08x\n", be32_to_cpu(z->rom.er_SerialNumber));
}
static DEVICE_ATTR_RO(serial);

static ssize_t resource_show(struct device *dev, struct device_attribute *attr,
			     char *buf)
{
	struct zorro_dev *z = to_zorro_dev(dev);

	return sprintf(buf, "0x%08lx 0x%08lx 0x%08lx\n",
		       (unsigned long)zorro_resource_start(z),
		       (unsigned long)zorro_resource_end(z),
		       zorro_resource_flags(z));
}
static DEVICE_ATTR_RO(resource);

static ssize_t modalias_show(struct device *dev, struct device_attribute *attr,
			     char *buf)
{
	struct zorro_dev *z = to_zorro_dev(dev);

	return sprintf(buf, ZORRO_DEVICE_MODALIAS_FMT "\n", z->id);
}
static DEVICE_ATTR_RO(modalias);

static struct attribute *zorro_device_attrs[] = {
	&dev_attr_id.attr,
	&dev_attr_type.attr,
	&dev_attr_serial.attr,
	&dev_attr_slotaddr.attr,
	&dev_attr_slotsize.attr,
	&dev_attr_resource.attr,
	&dev_attr_modalias.attr,
	NULL
};

static ssize_t zorro_read_config(struct file *filp, struct kobject *kobj,
				 const struct bin_attribute *bin_attr,
				 char *buf, loff_t off, size_t count)
{
	struct zorro_dev *z = to_zorro_dev(kobj_to_dev(kobj));
	struct ConfigDev cd;

	/* Construct a ConfigDev */
	memset(&cd, 0, sizeof(cd));
	cd.cd_Rom = z->rom;
	cd.cd_SlotAddr = cpu_to_be16(z->slotaddr);
	cd.cd_SlotSize = cpu_to_be16(z->slotsize);
	cd.cd_BoardAddr = cpu_to_be32(zorro_resource_start(z));
	cd.cd_BoardSize = cpu_to_be32(zorro_resource_len(z));

	return memory_read_from_buffer(buf, count, &off, &cd, sizeof(cd));
}

static const struct bin_attribute zorro_config_attr = {
	.attr =	{
		.name = "config",
		.mode = S_IRUGO,
	},
	.size = sizeof(struct ConfigDev),
	.read_new = zorro_read_config,
};

static const struct bin_attribute *const zorro_device_bin_attrs[] = {
	&zorro_config_attr,
	NULL
};

static const struct attribute_group zorro_device_attr_group = {
	.attrs		= zorro_device_attrs,
	.bin_attrs_new	= zorro_device_bin_attrs,
};

const struct attribute_group *zorro_device_attribute_groups[] = {
	&zorro_device_attr_group,
	NULL
};
