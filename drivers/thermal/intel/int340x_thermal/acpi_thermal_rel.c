// SPDX-License-Identifier: GPL-2.0-only
/* acpi_thermal_rel.c driver for exporting ACPI thermal relationship
 *
 * Copyright (c) 2014 Intel Corp
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
#include <linux/fs.h>
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
	union acpi_object *p;
	struct acpi_buffer buffer = { ACPI_ALLOCATE_BUFFER, NULL };
	struct acpi_buffer element = { 0, NULL };
	struct acpi_buffer trt_format = { sizeof("RRNNNNNN"), "RRNNNNNN" };

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

		if (!acpi_fetch_acpi_dev(trt->source))
			pr_warn("Failed to get source ACPI device\n");

		if (!acpi_fetch_acpi_dev(trt->target))
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
	union acpi_object *p;
	struct acpi_buffer buffer = { ACPI_ALLOCATE_BUFFER, NULL };
	struct acpi_buffer element = { 0, NULL };
	struct acpi_buffer art_format =	{
		sizeof("RRNNNNNNNNNNN"), "RRNNNNNNNNNNN" };

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

		if (!acpi_fetch_acpi_dev(art->source))
			pr_warn("Failed to get source ACPI device\n");

		if (!acpi_fetch_acpi_dev(art->target))
			pr_warn("Failed to get target ACPI device\n");
	}

	*artp = arts;
	/* don't count bad entries */
	*art_count -= nr_bad_entries;
end:
	kfree(buffer.pointer);
	return result;
}
EXPORT_SYMBOL(acpi_parse_art);

/*
 * acpi_parse_psvt - Passive Table (PSVT) for passive cooling
 *
 * @handle: ACPI handle of the device which contains PSVT
 * @psvt_count: the number of valid entries resulted from parsing PSVT
 * @psvtp: pointer to array of psvt entries
 *
 */
static int acpi_parse_psvt(acpi_handle handle, int *psvt_count, struct psvt **psvtp)
{
	struct acpi_buffer buffer = { ACPI_ALLOCATE_BUFFER, NULL };
	int nr_bad_entries = 0, revision = 0;
	union acpi_object *p;
	acpi_status status;
	int i, result = 0;
	struct psvt *psvts;

	if (!acpi_has_method(handle, "PSVT"))
		return -ENODEV;

	status = acpi_evaluate_object(handle, "PSVT", NULL, &buffer);
	if (ACPI_FAILURE(status))
		return -ENODEV;

	p = buffer.pointer;
	if (!p || (p->type != ACPI_TYPE_PACKAGE)) {
		result = -EFAULT;
		goto end;
	}

	/* first package is the revision number */
	if (p->package.count > 0) {
		union acpi_object *prev = &(p->package.elements[0]);

		if (prev->type == ACPI_TYPE_INTEGER)
			revision = (int)prev->integer.value;
	} else {
		result = -EFAULT;
		goto end;
	}

	/* Support only version 2 */
	if (revision != 2) {
		result = -EFAULT;
		goto end;
	}

	*psvt_count = p->package.count - 1;
	if (!*psvt_count) {
		result = -EFAULT;
		goto end;
	}

	psvts = kcalloc(*psvt_count, sizeof(*psvts), GFP_KERNEL);
	if (!psvts) {
		result = -ENOMEM;
		goto end;
	}

	/* Start index is 1 because the first package is the revision number */
	for (i = 1; i < p->package.count; i++) {
		struct acpi_buffer psvt_int_format = { sizeof("RRNNNNNNNNNN"), "RRNNNNNNNNNN" };
		struct acpi_buffer psvt_str_format = { sizeof("RRNNNNNSNNNN"), "RRNNNNNSNNNN" };
		union acpi_object *package = &(p->package.elements[i]);
		struct psvt *psvt = &psvts[i - 1 - nr_bad_entries];
		struct acpi_buffer *psvt_format = &psvt_int_format;
		struct acpi_buffer element = { 0, NULL };
		union acpi_object *knob;
		struct acpi_device *res;
		struct psvt *psvt_ptr;

		element.length = ACPI_ALLOCATE_BUFFER;
		element.pointer = NULL;

		if (package->package.count >= ACPI_NR_PSVT_ELEMENTS) {
			knob = &(package->package.elements[ACPI_PSVT_CONTROL_KNOB]);
		} else {
			nr_bad_entries++;
			pr_info("PSVT package %d is invalid, ignored\n", i);
			continue;
		}

		if (knob->type == ACPI_TYPE_STRING) {
			psvt_format = &psvt_str_format;
			if (knob->string.length > ACPI_LIMIT_STR_MAX_LEN - 1) {
				pr_info("PSVT package %d limit string len exceeds max\n", i);
				knob->string.length = ACPI_LIMIT_STR_MAX_LEN - 1;
			}
		}

		status = acpi_extract_package(&(p->package.elements[i]), psvt_format, &element);
		if (ACPI_FAILURE(status)) {
			nr_bad_entries++;
			pr_info("PSVT package %d is invalid, ignored\n", i);
			continue;
		}

		psvt_ptr = (struct psvt *)element.pointer;

		memcpy(psvt, psvt_ptr, sizeof(*psvt));

		/* The limit element can be string or U64 */
		psvt->control_knob_type = (u64)knob->type;

		if (knob->type == ACPI_TYPE_STRING) {
			memset(&psvt->limit, 0, sizeof(u64));
			strncpy(psvt->limit.string, psvt_ptr->limit.str_ptr, knob->string.length);
		} else {
			psvt->limit.integer = psvt_ptr->limit.integer;
		}

		kfree(element.pointer);

		res = acpi_fetch_acpi_dev(psvt->source);
		if (!res) {
			nr_bad_entries++;
			pr_info("Failed to get source ACPI device\n");
			continue;
		}

		res = acpi_fetch_acpi_dev(psvt->target);
		if (!res) {
			nr_bad_entries++;
			pr_info("Failed to get target ACPI device\n");
			continue;
		}
	}

	/* don't count bad entries */
	*psvt_count -= nr_bad_entries;

	if (!*psvt_count) {
		result = -EFAULT;
		kfree(psvts);
		goto end;
	}

	*psvtp = psvts;

	return 0;

end:
	kfree(buffer.pointer);
	return result;
}

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
		BUILD_BUG_ON(sizeof(art_user[i].data) !=
			     sizeof(u64) * (ACPI_NR_ART_ELEMENTS - 2));
		memcpy(&art_user[i].data, &arts[i].data, sizeof(art_user[i].data));
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

static int fill_psvt(char __user *ubuf)
{
	int i, ret, count, psvt_len;
	union psvt_object *psvt_user;
	struct psvt *psvts;

	ret = acpi_parse_psvt(acpi_thermal_rel_handle, &count, &psvts);
	if (ret)
		return ret;

	psvt_len = count * sizeof(*psvt_user);

	psvt_user = kzalloc(psvt_len, GFP_KERNEL);
	if (!psvt_user) {
		ret = -ENOMEM;
		goto free_psvt;
	}

	/* now fill in user psvt data */
	for (i = 0; i < count; i++) {
		/* userspace psvt needs device name instead of acpi reference */
		get_single_name(psvts[i].source, psvt_user[i].source_device);
		get_single_name(psvts[i].target, psvt_user[i].target_device);

		psvt_user[i].priority = psvts[i].priority;
		psvt_user[i].sample_period = psvts[i].sample_period;
		psvt_user[i].passive_temp = psvts[i].passive_temp;
		psvt_user[i].source_domain = psvts[i].source_domain;
		psvt_user[i].control_knob = psvts[i].control_knob;
		psvt_user[i].step_size = psvts[i].step_size;
		psvt_user[i].limit_coeff = psvts[i].limit_coeff;
		psvt_user[i].unlimit_coeff = psvts[i].unlimit_coeff;
		psvt_user[i].control_knob_type = psvts[i].control_knob_type;
		if (psvt_user[i].control_knob_type == ACPI_TYPE_STRING)
			strncpy(psvt_user[i].limit.string, psvts[i].limit.string,
				ACPI_LIMIT_STR_MAX_LEN);
		else
			psvt_user[i].limit.integer = psvts[i].limit.integer;

	}

	if (copy_to_user(ubuf, psvt_user, psvt_len))
		ret = -EFAULT;

	kfree(psvt_user);

free_psvt:
	kfree(psvts);
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
	struct psvt *psvts;

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

	case ACPI_THERMAL_GET_PSVT_COUNT:
		ret = acpi_parse_psvt(acpi_thermal_rel_handle, &count, &psvts);
		if (!ret) {
			kfree(psvts);
			return put_user(count, (unsigned long __user *)__arg);
		}
		return ret;

	case ACPI_THERMAL_GET_PSVT_LEN:
		/* total length of the data retrieved (count * PSVT entry size) */
		ret = acpi_parse_psvt(acpi_thermal_rel_handle, &count, &psvts);
		length = count * sizeof(union psvt_object);
		if (!ret) {
			kfree(psvts);
			return put_user(length, (unsigned long __user *)__arg);
		}
		return ret;

	case ACPI_THERMAL_GET_PSVT:
		return fill_psvt(arg);

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
