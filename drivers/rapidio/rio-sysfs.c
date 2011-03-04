/*
 * RapidIO sysfs attributes and support
 *
 * Copyright 2005 MontaVista Software, Inc.
 * Matt Porter <mporter@kernel.crashing.org>
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#include <linux/kernel.h>
#include <linux/rio.h>
#include <linux/rio_drv.h>
#include <linux/stat.h>

#include "rio.h"

/* Sysfs support */
#define rio_config_attr(field, format_string)					\
static ssize_t								\
field##_show(struct device *dev, struct device_attribute *attr, char *buf)			\
{									\
	struct rio_dev *rdev = to_rio_dev(dev);				\
									\
	return sprintf(buf, format_string, rdev->field);		\
}									\

rio_config_attr(did, "0x%04x\n");
rio_config_attr(vid, "0x%04x\n");
rio_config_attr(device_rev, "0x%08x\n");
rio_config_attr(asm_did, "0x%04x\n");
rio_config_attr(asm_vid, "0x%04x\n");
rio_config_attr(asm_rev, "0x%04x\n");

static ssize_t routes_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct rio_dev *rdev = to_rio_dev(dev);
	char *str = buf;
	int i;

	for (i = 0; i < RIO_MAX_ROUTE_ENTRIES(rdev->net->hport->sys_size);
			i++) {
		if (rdev->rswitch->route_table[i] == RIO_INVALID_ROUTE)
			continue;
		str +=
		    sprintf(str, "%04x %02x\n", i,
			    rdev->rswitch->route_table[i]);
	}

	return (str - buf);
}

struct device_attribute rio_dev_attrs[] = {
	__ATTR_RO(did),
	__ATTR_RO(vid),
	__ATTR_RO(device_rev),
	__ATTR_RO(asm_did),
	__ATTR_RO(asm_vid),
	__ATTR_RO(asm_rev),
	__ATTR_NULL,
};

static DEVICE_ATTR(routes, S_IRUGO, routes_show, NULL);

static ssize_t
rio_read_config(struct file *filp, struct kobject *kobj,
		struct bin_attribute *bin_attr,
		char *buf, loff_t off, size_t count)
{
	struct rio_dev *dev =
	    to_rio_dev(container_of(kobj, struct device, kobj));
	unsigned int size = 0x100;
	loff_t init_off = off;
	u8 *data = (u8 *) buf;

	/* Several chips lock up trying to read undefined config space */
	if (capable(CAP_SYS_ADMIN))
		size = RIO_MAINT_SPACE_SZ;

	if (off >= size)
		return 0;
	if (off + count > size) {
		size -= off;
		count = size;
	} else {
		size = count;
	}

	if ((off & 1) && size) {
		u8 val;
		rio_read_config_8(dev, off, &val);
		data[off - init_off] = val;
		off++;
		size--;
	}

	if ((off & 3) && size > 2) {
		u16 val;
		rio_read_config_16(dev, off, &val);
		data[off - init_off] = (val >> 8) & 0xff;
		data[off - init_off + 1] = val & 0xff;
		off += 2;
		size -= 2;
	}

	while (size > 3) {
		u32 val;
		rio_read_config_32(dev, off, &val);
		data[off - init_off] = (val >> 24) & 0xff;
		data[off - init_off + 1] = (val >> 16) & 0xff;
		data[off - init_off + 2] = (val >> 8) & 0xff;
		data[off - init_off + 3] = val & 0xff;
		off += 4;
		size -= 4;
	}

	if (size >= 2) {
		u16 val;
		rio_read_config_16(dev, off, &val);
		data[off - init_off] = (val >> 8) & 0xff;
		data[off - init_off + 1] = val & 0xff;
		off += 2;
		size -= 2;
	}

	if (size > 0) {
		u8 val;
		rio_read_config_8(dev, off, &val);
		data[off - init_off] = val;
		off++;
		--size;
	}

	return count;
}

static ssize_t
rio_write_config(struct file *filp, struct kobject *kobj,
		 struct bin_attribute *bin_attr,
		 char *buf, loff_t off, size_t count)
{
	struct rio_dev *dev =
	    to_rio_dev(container_of(kobj, struct device, kobj));
	unsigned int size = count;
	loff_t init_off = off;
	u8 *data = (u8 *) buf;

	if (off >= RIO_MAINT_SPACE_SZ)
		return 0;
	if (off + count > RIO_MAINT_SPACE_SZ) {
		size = RIO_MAINT_SPACE_SZ - off;
		count = size;
	}

	if ((off & 1) && size) {
		rio_write_config_8(dev, off, data[off - init_off]);
		off++;
		size--;
	}

	if ((off & 3) && (size > 2)) {
		u16 val = data[off - init_off + 1];
		val |= (u16) data[off - init_off] << 8;
		rio_write_config_16(dev, off, val);
		off += 2;
		size -= 2;
	}

	while (size > 3) {
		u32 val = data[off - init_off + 3];
		val |= (u32) data[off - init_off + 2] << 8;
		val |= (u32) data[off - init_off + 1] << 16;
		val |= (u32) data[off - init_off] << 24;
		rio_write_config_32(dev, off, val);
		off += 4;
		size -= 4;
	}

	if (size >= 2) {
		u16 val = data[off - init_off + 1];
		val |= (u16) data[off - init_off] << 8;
		rio_write_config_16(dev, off, val);
		off += 2;
		size -= 2;
	}

	if (size) {
		rio_write_config_8(dev, off, data[off - init_off]);
		off++;
		--size;
	}

	return count;
}

static struct bin_attribute rio_config_attr = {
	.attr = {
		 .name = "config",
		 .mode = S_IRUGO | S_IWUSR,
		 },
	.size = RIO_MAINT_SPACE_SZ,
	.read = rio_read_config,
	.write = rio_write_config,
};

/**
 * rio_create_sysfs_dev_files - create RIO specific sysfs files
 * @rdev: device whose entries should be created
 *
 * Create files when @rdev is added to sysfs.
 */
int rio_create_sysfs_dev_files(struct rio_dev *rdev)
{
	int err = 0;

	err = device_create_bin_file(&rdev->dev, &rio_config_attr);

	if (!err && (rdev->pef & RIO_PEF_SWITCH)) {
		err = device_create_file(&rdev->dev, &dev_attr_routes);
		if (!err && rdev->rswitch->sw_sysfs)
			err = rdev->rswitch->sw_sysfs(rdev, RIO_SW_SYSFS_CREATE);
	}

	if (err)
		pr_warning("RIO: Failed to create attribute file(s) for %s\n",
			   rio_name(rdev));

	return err;
}

/**
 * rio_remove_sysfs_dev_files - cleanup RIO specific sysfs files
 * @rdev: device whose entries we should free
 *
 * Cleanup when @rdev is removed from sysfs.
 */
void rio_remove_sysfs_dev_files(struct rio_dev *rdev)
{
	device_remove_bin_file(&rdev->dev, &rio_config_attr);
	if (rdev->pef & RIO_PEF_SWITCH) {
		device_remove_file(&rdev->dev, &dev_attr_routes);
		if (rdev->rswitch->sw_sysfs)
			rdev->rswitch->sw_sysfs(rdev, RIO_SW_SYSFS_REMOVE);
	}
}
