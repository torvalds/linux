// SPDX-License-Identifier: GPL-2.0-only
/*
 * cbmem.c
 *
 * Driver for exporting cbmem entries in sysfs.
 *
 * Copyright 2022 Google LLC
 */

#include <linux/device.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/kobject.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/sysfs.h>

#include "coreboot_table.h"

struct cbmem_entry {
	char *mem_file_buf;
	u32 size;
};

static struct cbmem_entry *to_cbmem_entry(struct kobject *kobj)
{
	return dev_get_drvdata(kobj_to_dev(kobj));
}

static ssize_t mem_read(struct file *filp, struct kobject *kobj,
			struct bin_attribute *bin_attr, char *buf, loff_t pos,
			size_t count)
{
	struct cbmem_entry *entry = to_cbmem_entry(kobj);

	return memory_read_from_buffer(buf, count, &pos, entry->mem_file_buf,
				       entry->size);
}

static ssize_t mem_write(struct file *filp, struct kobject *kobj,
			 struct bin_attribute *bin_attr, char *buf, loff_t pos,
			 size_t count)
{
	struct cbmem_entry *entry = to_cbmem_entry(kobj);

	if (pos < 0 || pos >= entry->size)
		return -EINVAL;
	if (count > entry->size - pos)
		count = entry->size - pos;

	memcpy(entry->mem_file_buf + pos, buf, count);
	return count;
}
static BIN_ATTR_ADMIN_RW(mem, 0);

static ssize_t address_show(struct device *dev, struct device_attribute *attr,
			    char *buf)
{
	struct coreboot_device *cbdev = dev_to_coreboot_device(dev);

	return sysfs_emit(buf, "0x%llx\n", cbdev->cbmem_entry.address);
}
static DEVICE_ATTR_RO(address);

static ssize_t size_show(struct device *dev, struct device_attribute *attr,
			 char *buf)
{
	struct coreboot_device *cbdev = dev_to_coreboot_device(dev);

	return sysfs_emit(buf, "0x%x\n", cbdev->cbmem_entry.entry_size);
}
static DEVICE_ATTR_RO(size);

static struct attribute *attrs[] = {
	&dev_attr_address.attr,
	&dev_attr_size.attr,
	NULL,
};

static struct bin_attribute *bin_attrs[] = {
	&bin_attr_mem,
	NULL,
};

static const struct attribute_group cbmem_entry_group = {
	.attrs = attrs,
	.bin_attrs = bin_attrs,
};

static const struct attribute_group *dev_groups[] = {
	&cbmem_entry_group,
	NULL,
};

static int cbmem_entry_probe(struct coreboot_device *dev)
{
	struct cbmem_entry *entry;

	entry = devm_kzalloc(&dev->dev, sizeof(*entry), GFP_KERNEL);
	if (!entry)
		return -ENOMEM;

	dev_set_drvdata(&dev->dev, entry);
	entry->mem_file_buf = devm_memremap(&dev->dev, dev->cbmem_entry.address,
					    dev->cbmem_entry.entry_size,
					    MEMREMAP_WB);
	if (IS_ERR(entry->mem_file_buf))
		return PTR_ERR(entry->mem_file_buf);

	entry->size = dev->cbmem_entry.entry_size;

	return 0;
}

static struct coreboot_driver cbmem_entry_driver = {
	.probe = cbmem_entry_probe,
	.drv = {
		.name = "cbmem",
		.owner = THIS_MODULE,
		.dev_groups = dev_groups,
	},
	.tag = LB_TAG_CBMEM_ENTRY,
};
module_coreboot_driver(cbmem_entry_driver);

MODULE_AUTHOR("Jack Rosenthal <jrosenth@chromium.org>");
MODULE_LICENSE("GPL");
