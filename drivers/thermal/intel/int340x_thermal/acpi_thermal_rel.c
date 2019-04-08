/* acpi_thermal_rel.c driver for exporting ACPI thermal relationship
 *
 * Copyright (c) 2014 Intel Corp
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 */

/*
 * Two functionalities included:
 * 1. Export _TRT, _ART, via misc device interface to the userspace.
 * 2. Provide parsing result to kernel drivers
 *
 */
#include <linux/init.h>
#include <linux/export.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/io.h>
#include <linux/acpi.h>
#include <linux/uaccess.h>
#include <linux/miscdevice.h>
#include "acpi_thermal_rel.h"

static acpi_handle acpi_thermal_rel_handle;
static DEFINE_SPINLOCK(acpi_thermal_rel_chrdev_lock);
static int acpi_thermal_rel_chrdev_count;	/* #times opened */
static int acpi_thermal_rel_chrdev_exclu;	/* already open exclusive? */

static int acpi_thermal_rel_open(struct inode *inode, struct file *file)
{
	spin_lock(&acpi_thermal_rel_chrdev_lock);
	if (acpi_thermal_rel_chrdev_exclu ||
	    (acpi_thermal_rel_chrdev_count && (file->f_flags & O_EXCL))) {
		spin_unlock(&acpi_thermal_rel_chrdev_lock);
		return -EBUSY;
	}

	if (file->f_flags & O_EXCL)
		acpi_thermal_rel_chrdev_exclu = 1;
	acpi_thermal_rel_chrdev_count++;

	spin_unlock(&acpi_thermal_rel_chrdev_lock);

	return nonseekable_open(inode, file);
}

static int acpi_thermal_rel_release(struct inode *inode, struct file *file)
{
	spin_lock(&acpi_thermal_rel_chrdev_lock);
	acpi_thermal_rel_chrdev_count--;
	acpi_thermal_rel_chrdev_exclu = 0;
	spin_unlock(&acpi_thermal_rel_chrdev_lock);

	return 0;
}

/**
 * acpi_parse_trt - Thermal Relationship Table _TRT for passive cooling
 *
 * @handle: ACPI handle of the device contains _TRT
 * @trt_count: the number of valid entries resulted from parsing _TRT
 * @trtp: pointer to pointer of array of _TRT entries in parsing result
 * @create_dev: whether to create platform devices for target and source
 *
 */
int acpi_parse_trt(acpi_handle handle, int *trt_count, struct trt **trtp,
		bool create_dev)
{
	acpi_status status;
	int result = 0;
	int i;
	int nr_bad_entries = 0;
	struct trt *trts;
	struct acpi_device *adev;
	union acpi_object *p;
	struct acpi_buffer buffer = { ACPI_ALLOCATE_BUFFER, NULL };
	struct acpi_buffer element = { 0, NULL };
	struct acpi_buffer trt_format = { sizeof("RRNNNNNN"), "RRNNNNNN" };

	if (!acpi_has_method(handle, "_TRT"))
		return -ENODEV;

	status = acpi_evaluate_object(handle, "_TRT", NULL, &buffer);
	if (ACPI_FAILURE(status))
		return -ENODEV;

	p = buffer.pointer;
	if (!p || (p->type != ACPI_TYPE_PACKAGE)) {
		pr_err("Invalid _TRT data\n");
		result = -EFAULT;
		goto end;
	}

	*trt_count = p->package.count;
	trts = kcalloc(*trt_count, sizeof(struct trt), GFP_KERNEL);
	if (!trts) {
		result = -ENOMEM;
		goto end;
	}

	for (i = 0; i < *trt_count; i++) {
		struct trt *trt = &trts[i - nr_bad_entries];

		element.length = sizeof(struct trt);
		element.pointer = trt;

		status = acpi_extract_package(&(p->package.elements[i]),
					      &trt_format, &element);
		if (ACPI_FAILURE(status)) {
			nr_bad_entries++;
			pr_warn("_TRT package %d is invalid, ignored\n", i);
			continue;
		}
		if (!create_dev)
			continue;

		result = acpi_bus_get_device(trt->source, &adev);
		if (result)
			pr_warn("Failed to get source ACPI device\n");

		result = acpi_bus_get_device(trt->target, &adev);
		if (result)
			pr_warn("Failed to get target ACPI device\n");
	}

	result = 0;

	*trtp = trts;
	/* don't count bad entries */
	*trt_count -= nr_bad_entries;
end:
	kfree(buffer.pointer);
	return result;
}
EXPORT_SYMBOL(acpi_parse_trt);

/**
 * acpi_parse_art - Parse Active Relationship Table _ART
 *
 * @handle: ACPI handle of the device contains _ART
 * @art_count: the number of valid entries resulted from parsing _ART
 * @artp: pointer to pointer of array of art entries in parsing result
 * @create_dev: whether to create platform devices for target and source
 *
 */
int acpi_parse_art(acpi_handle handle, int *art_count, struct art **artp,
		bool create_dev)
{
	acpi_status status;
	int result = 0;
	int i;
	int nr_bad_entries = 0;
	struct art *arts;
	struct acpi_device *adev;
	union acpi_object *p;
	struct acpi_buffer buffer = { ACPI_ALLOCATE_BUFFER, NULL };
	struct acpi_buffer element = { 0, NULL };
	struct acpi_buffer art_format =	{
		sizeof("RRNNNNNNNNNNN"), "RRNNNNNNNNNNN" };

	if (!acpi_has_method(handle, "_ART"))
		return -ENODEV;

	status = acpi_evaluate_object(handle, "_ART", NULL, &buffer);
	if (ACPI_FAILURE(status))
		return -ENODEV;

	p = buffer.pointer;
	if (!p || (p->type != ACPI_TYPE_PACKAGE)) {
		pr_err("Invalid _ART data\n");
		result = -EFAULT;
		goto end;
	}

	/* ignore p->package.elements[0], as this is _ART Revision field */
	*art_count = p->package.count - 1;
	arts = kcalloc(*art_count, sizeof(struct art), GFP_KERNEL);
	if (!arts) {
		result = -ENOMEM;
		goto end;
	}

	for (i = 0; i < *art_count; i++) {
		struct art *art = &arts[i - nr_bad_entries];

		element.length = sizeof(struct art);
		element.pointer = art;

		status = acpi_extract_package(&(p->package.elements[i + 1]),
					      &art_format, &element);
		if (ACPI_FAILURE(status)) {
			pr_warn("_ART package %d is invalid, ignored", i);
			nr_bad_entries++;
			continue;
		}
		if (!create_dev)
			continue;

		if (art->source) {
			result = acpi_bus_get_device(art->source, &adev);
			if (result)
				pr_warn("Failed to get source ACPI device\n");
		}
		if (art->target) {
			result = acpi_bus_get_device(art->target, &adev);
			if (result)
				pr_warn("Failed to get target ACPI device\n");
		}
	}

	*artp = arts;
	/* don't count bad entries */
	*art_count -= nr_bad_entries;
end:
	kfree(buffer.pointer);
	return result;
}
EXPORT_SYMBOL(acpi_parse_art);


/* get device name from acpi handle */
static void get_single_name(acpi_handle handle, char *name)
{
	struct acpi_buffer buffer = {ACPI_ALLOCATE_BUFFER};

	if (ACPI_FAILURE(acpi_get_name(handle, ACPI_SINGLE_NAME, &buffer)))
		pr_warn("Failed to get device name from acpi handle\n");
	else {
		memcpy(name, buffer.pointer, ACPI_NAMESEG_SIZE);
		kfree(buffer.pointer);
	}
}

static int fill_art(char __user *ubuf)
{
	int i;
	int ret;
	int count;
	int art_len;
	struct art *arts = NULL;
	union art_object *art_user;

	ret = acpi_parse_art(acpi_thermal_rel_handle, &count, &arts, false);
	if (ret)
		goto free_art;
	art_len = count * sizeof(union art_object);
	art_user = kzalloc(art_len, GFP_KERNEL);
	if (!art_user) {
		ret = -ENOMEM;
		goto free_art;
	}
	/* now fill in user art data */
	for (i = 0; i < count; i++) {
		/* userspace art needs device name instead of acpi reference */
		get_single_name(arts[i].source, art_user[i].source_device);
		get_single_name(arts[i].target, art_user[i].target_device);
		/* copy the rest int data in addition to source and target */
		memcpy(&art_user[i].weight, &arts[i].weight,
			sizeof(u64) * (ACPI_NR_ART_ELEMENTS - 2));
	}

	if (copy_to_user(ubuf, art_user, art_len))
		ret = -EFAULT;
	kfree(art_user);
free_art:
	kfree(arts);
	return ret;
}

static int fill_trt(char __user *ubuf)
{
	int i;
	int ret;
	int count;
	int trt_len;
	struct trt *trts = NULL;
	union trt_object *trt_user;

	ret = acpi_parse_trt(acpi_thermal_rel_handle, &count, &trts, false);
	if (ret)
		goto free_trt;
	trt_len = count * sizeof(union trt_object);
	trt_user = kzalloc(trt_len, GFP_KERNEL);
	if (!trt_user) {
		ret = -ENOMEM;
		goto free_trt;
	}
	/* now fill in user trt data */
	for (i = 0; i < count; i++) {
		/* userspace trt needs device name instead of acpi reference */
		get_single_name(trts[i].source, trt_user[i].source_device);
		get_single_name(trts[i].target, trt_user[i].target_device);
		trt_user[i].sample_period = trts[i].sample_period;
		trt_user[i].influence = trts[i].influence;
	}

	if (copy_to_user(ubuf, trt_user, trt_len))
		ret = -EFAULT;
	kfree(trt_user);
free_trt:
	kfree(trts);
	return ret;
}

static long acpi_thermal_rel_ioctl(struct file *f, unsigned int cmd,
				   unsigned long __arg)
{
	int ret = 0;
	unsigned long length = 0;
	int count = 0;
	char __user *arg = (void __user *)__arg;
	struct trt *trts = NULL;
	struct art *arts = NULL;

	switch (cmd) {
	case ACPI_THERMAL_GET_TRT_COUNT:
		ret = acpi_parse_trt(acpi_thermal_rel_handle, &count,
				&trts, false);
		kfree(trts);
		if (!ret)
			return put_user(count, (unsigned long __user *)__arg);
		return ret;
	case ACPI_THERMAL_GET_TRT_LEN:
		ret = acpi_parse_trt(acpi_thermal_rel_handle, &count,
				&trts, false);
		kfree(trts);
		length = count * sizeof(union trt_object);
		if (!ret)
			return put_user(length, (unsigned long __user *)__arg);
		return ret;
	case ACPI_THERMAL_GET_TRT:
		return fill_trt(arg);
	case ACPI_THERMAL_GET_ART_COUNT:
		ret = acpi_parse_art(acpi_thermal_rel_handle, &count,
				&arts, false);
		kfree(arts);
		if (!ret)
			return put_user(count, (unsigned long __user *)__arg);
		return ret;
	case ACPI_THERMAL_GET_ART_LEN:
		ret = acpi_parse_art(acpi_thermal_rel_handle, &count,
				&arts, false);
		kfree(arts);
		length = count * sizeof(union art_object);
		if (!ret)
			return put_user(length, (unsigned long __user *)__arg);
		return ret;

	case ACPI_THERMAL_GET_ART:
		return fill_art(arg);

	default:
		return -ENOTTY;
	}
}

static const struct file_operations acpi_thermal_rel_fops = {
	.owner		= THIS_MODULE,
	.open		= acpi_thermal_rel_open,
	.release	= acpi_thermal_rel_release,
	.unlocked_ioctl	= acpi_thermal_rel_ioctl,
	.llseek		= no_llseek,
};

static struct miscdevice acpi_thermal_rel_misc_device = {
	.minor	= MISC_DYNAMIC_MINOR,
	"acpi_thermal_rel",
	&acpi_thermal_rel_fops
};

int acpi_thermal_rel_misc_device_add(acpi_handle handle)
{
	acpi_thermal_rel_handle = handle;

	return misc_register(&acpi_thermal_rel_misc_device);
}
EXPORT_SYMBOL(acpi_thermal_rel_misc_device_add);

int acpi_thermal_rel_misc_device_remove(acpi_handle handle)
{
	misc_deregister(&acpi_thermal_rel_misc_device);

	return 0;
}
EXPORT_SYMBOL(acpi_thermal_rel_misc_device_remove);

MODULE_AUTHOR("Zhang Rui <rui.zhang@intel.com>");
MODULE_AUTHOR("Jacob Pan <jacob.jun.pan@intel.com");
MODULE_DESCRIPTION("Intel acpi thermal rel misc dev driver");
MODULE_LICENSE("GPL v2");
