/*
 * Copyright 2011 Tilera Corporation. All Rights Reserved.
 *
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License
 *   as published by the Free Software Foundation, version 2.
 *
 *   This program is distributed in the hope that it will be useful, but
 *   WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 *   NON INFRINGEMENT.  See the GNU General Public License for
 *   more details.
 *
 * /sys entry support.
 */

#include <linux/sysdev.h>
#include <linux/cpu.h>
#include <linux/slab.h>
#include <linux/smp.h>
#include <linux/stat.h>
#include <hv/hypervisor.h>

/* Return a string queried from the hypervisor, truncated to page size. */
static ssize_t get_hv_confstr(char *page, int query)
{
	ssize_t n = hv_confstr(query, (unsigned long)page, PAGE_SIZE - 1);
	n = n < 0 ? 0 : min(n, (ssize_t)PAGE_SIZE - 1) - 1;
	if (n)
		page[n++] = '\n';
	page[n] = '\0';
	return n;
}

static ssize_t chip_width_show(struct sysdev_class *dev,
			       struct sysdev_class_attribute *attr,
			       char *page)
{
	return sprintf(page, "%u\n", smp_width);
}
static SYSDEV_CLASS_ATTR(chip_width, 0444, chip_width_show, NULL);

static ssize_t chip_height_show(struct sysdev_class *dev,
				struct sysdev_class_attribute *attr,
				char *page)
{
	return sprintf(page, "%u\n", smp_height);
}
static SYSDEV_CLASS_ATTR(chip_height, 0444, chip_height_show, NULL);

static ssize_t chip_serial_show(struct sysdev_class *dev,
				struct sysdev_class_attribute *attr,
				char *page)
{
	return get_hv_confstr(page, HV_CONFSTR_CHIP_SERIAL_NUM);
}
static SYSDEV_CLASS_ATTR(chip_serial, 0444, chip_serial_show, NULL);

static ssize_t chip_revision_show(struct sysdev_class *dev,
				  struct sysdev_class_attribute *attr,
				  char *page)
{
	return get_hv_confstr(page, HV_CONFSTR_CHIP_REV);
}
static SYSDEV_CLASS_ATTR(chip_revision, 0444, chip_revision_show, NULL);


static ssize_t type_show(struct sysdev_class *dev,
			    struct sysdev_class_attribute *attr,
			    char *page)
{
	return sprintf(page, "tilera\n");
}
static SYSDEV_CLASS_ATTR(type, 0444, type_show, NULL);

#define HV_CONF_ATTR(name, conf)					\
	static ssize_t name ## _show(struct sysdev_class *dev,		\
				     struct sysdev_class_attribute *attr, \
				     char *page)			\
	{								\
		return get_hv_confstr(page, conf);			\
	}								\
	static SYSDEV_CLASS_ATTR(name, 0444, name ## _show, NULL);

HV_CONF_ATTR(version,		HV_CONFSTR_HV_SW_VER)
HV_CONF_ATTR(config_version,	HV_CONFSTR_HV_CONFIG_VER)

HV_CONF_ATTR(board_part,	HV_CONFSTR_BOARD_PART_NUM)
HV_CONF_ATTR(board_serial,	HV_CONFSTR_BOARD_SERIAL_NUM)
HV_CONF_ATTR(board_revision,	HV_CONFSTR_BOARD_REV)
HV_CONF_ATTR(board_description,	HV_CONFSTR_BOARD_DESC)
HV_CONF_ATTR(mezz_part,		HV_CONFSTR_MEZZ_PART_NUM)
HV_CONF_ATTR(mezz_serial,	HV_CONFSTR_MEZZ_SERIAL_NUM)
HV_CONF_ATTR(mezz_revision,	HV_CONFSTR_MEZZ_REV)
HV_CONF_ATTR(mezz_description,	HV_CONFSTR_MEZZ_DESC)
HV_CONF_ATTR(switch_control,	HV_CONFSTR_SWITCH_CONTROL)

static struct attribute *board_attrs[] = {
	&attr_board_part.attr,
	&attr_board_serial.attr,
	&attr_board_revision.attr,
	&attr_board_description.attr,
	&attr_mezz_part.attr,
	&attr_mezz_serial.attr,
	&attr_mezz_revision.attr,
	&attr_mezz_description.attr,
	&attr_switch_control.attr,
	NULL
};

static struct attribute_group board_attr_group = {
	.name   = "board",
	.attrs  = board_attrs,
};


static struct bin_attribute hvconfig_bin;

static ssize_t
hvconfig_bin_read(struct file *filp, struct kobject *kobj,
		  struct bin_attribute *bin_attr,
		  char *buf, loff_t off, size_t count)
{
	static size_t size;

	/* Lazily learn the true size (minus the trailing NUL). */
	if (size == 0)
		size = hv_confstr(HV_CONFSTR_HV_CONFIG, 0, 0) - 1;

	/* Check and adjust input parameters. */
	if (off > size)
		return -EINVAL;
	if (count > size - off)
		count = size - off;

	if (count) {
		/* Get a copy of the hvc and copy out the relevant portion. */
		char *hvc;

		size = off + count;
		hvc = kmalloc(size, GFP_KERNEL);
		if (hvc == NULL)
			return -ENOMEM;
		hv_confstr(HV_CONFSTR_HV_CONFIG, (unsigned long)hvc, size);
		memcpy(buf, hvc + off, count);
		kfree(hvc);
	}

	return count;
}

static int __init create_sysfs_entries(void)
{
	struct sysdev_class *cls = &cpu_sysdev_class;
	int err = 0;

#define create_cpu_attr(name)						\
	if (!err)							\
		err = sysfs_create_file(&cls->kset.kobj, &attr_##name.attr);
	create_cpu_attr(chip_width);
	create_cpu_attr(chip_height);
	create_cpu_attr(chip_serial);
	create_cpu_attr(chip_revision);

#define create_hv_attr(name)						\
	if (!err)							\
		err = sysfs_create_file(hypervisor_kobj, &attr_##name.attr);
	create_hv_attr(type);
	create_hv_attr(version);
	create_hv_attr(config_version);

	if (!err)
		err = sysfs_create_group(hypervisor_kobj, &board_attr_group);

	if (!err) {
		sysfs_bin_attr_init(&hvconfig_bin);
		hvconfig_bin.attr.name = "hvconfig";
		hvconfig_bin.attr.mode = S_IRUGO;
		hvconfig_bin.read = hvconfig_bin_read;
		hvconfig_bin.size = PAGE_SIZE;
		err = sysfs_create_bin_file(hypervisor_kobj, &hvconfig_bin);
	}

	return err;
}
subsys_initcall(create_sysfs_entries);
