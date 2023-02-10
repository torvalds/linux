// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2019 IBM Corporation <nayna@linux.ibm.com>
 *
 * This code exposes secure variables to user via sysfs
 */

#define pr_fmt(fmt) "secvar-sysfs: "fmt

#include <linux/slab.h>
#include <linux/compat.h>
#include <linux/string.h>
#include <linux/of.h>
#include <asm/secvar.h>

#define NAME_MAX_SIZE	   1024

static struct kobject *secvar_kobj;
static struct kset *secvar_kset;

static ssize_t format_show(struct kobject *kobj, struct kobj_attribute *attr,
			   char *buf)
{
	char tmp[32];
	ssize_t len = secvar_ops->format(tmp, sizeof(tmp));

	if (len > 0)
		return sysfs_emit(buf, "%s\n", tmp);
	else if (len < 0)
		pr_err("Error %zd reading format string\n", len);
	else
		pr_err("Got empty format string from backend\n");

	return -EIO;
}


static ssize_t size_show(struct kobject *kobj, struct kobj_attribute *attr,
			 char *buf)
{
	u64 dsize;
	int rc;

	rc = secvar_ops->get(kobj->name, strlen(kobj->name) + 1, NULL, &dsize);
	if (rc) {
		pr_err("Error retrieving %s variable size %d\n", kobj->name,
		       rc);
		return rc;
	}

	return sysfs_emit(buf, "%llu\n", dsize);
}

static ssize_t data_read(struct file *filep, struct kobject *kobj,
			 struct bin_attribute *attr, char *buf, loff_t off,
			 size_t count)
{
	char *data;
	u64 dsize;
	int rc;

	rc = secvar_ops->get(kobj->name, strlen(kobj->name) + 1, NULL, &dsize);
	if (rc) {
		pr_err("Error getting %s variable size %d\n", kobj->name, rc);
		return rc;
	}
	pr_debug("dsize is %llu\n", dsize);

	data = kzalloc(dsize, GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	rc = secvar_ops->get(kobj->name, strlen(kobj->name) + 1, data, &dsize);
	if (rc) {
		pr_err("Error getting %s variable %d\n", kobj->name, rc);
		goto data_fail;
	}

	rc = memory_read_from_buffer(buf, count, &off, data, dsize);

data_fail:
	kfree(data);
	return rc;
}

static ssize_t update_write(struct file *filep, struct kobject *kobj,
			    struct bin_attribute *attr, char *buf, loff_t off,
			    size_t count)
{
	int rc;

	pr_debug("count is %ld\n", count);
	rc = secvar_ops->set(kobj->name, strlen(kobj->name) + 1, buf, count);
	if (rc) {
		pr_err("Error setting the %s variable %d\n", kobj->name, rc);
		return rc;
	}

	return count;
}

static struct kobj_attribute format_attr = __ATTR_RO(format);

static struct kobj_attribute size_attr = __ATTR_RO(size);

static struct bin_attribute data_attr = __BIN_ATTR_RO(data, 0);

static struct bin_attribute update_attr = __BIN_ATTR_WO(update, 0);

static struct bin_attribute *secvar_bin_attrs[] = {
	&data_attr,
	&update_attr,
	NULL,
};

static struct attribute *secvar_attrs[] = {
	&size_attr.attr,
	NULL,
};

static const struct attribute_group secvar_attr_group = {
	.attrs = secvar_attrs,
	.bin_attrs = secvar_bin_attrs,
};
__ATTRIBUTE_GROUPS(secvar_attr);

static struct kobj_type secvar_ktype = {
	.sysfs_ops	= &kobj_sysfs_ops,
	.default_groups = secvar_attr_groups,
};

static int update_kobj_size(void)
{

	u64 varsize;
	int rc = secvar_ops->max_size(&varsize);

	if (rc)
		return rc;

	data_attr.size = varsize;
	update_attr.size = varsize;

	return 0;
}

static int secvar_sysfs_load(void)
{
	struct kobject *kobj;
	u64 namesize = 0;
	char *name;
	int rc;

	name = kzalloc(NAME_MAX_SIZE, GFP_KERNEL);
	if (!name)
		return -ENOMEM;

	do {
		rc = secvar_ops->get_next(name, &namesize, NAME_MAX_SIZE);
		if (rc) {
			if (rc != -ENOENT)
				pr_err("error getting secvar from firmware %d\n", rc);
			else
				rc = 0;

			break;
		}

		kobj = kzalloc(sizeof(*kobj), GFP_KERNEL);
		if (!kobj) {
			rc = -ENOMEM;
			break;
		}

		kobject_init(kobj, &secvar_ktype);

		rc = kobject_add(kobj, &secvar_kset->kobj, "%s", name);
		if (rc) {
			pr_warn("kobject_add error %d for attribute: %s\n", rc,
				name);
			kobject_put(kobj);
			kobj = NULL;
		}

		if (kobj)
			kobject_uevent(kobj, KOBJ_ADD);

	} while (!rc);

	kfree(name);
	return rc;
}

static int secvar_sysfs_init(void)
{
	int rc;

	if (!secvar_ops) {
		pr_warn("secvar: failed to retrieve secvar operations.\n");
		return -ENODEV;
	}

	secvar_kobj = kobject_create_and_add("secvar", firmware_kobj);
	if (!secvar_kobj) {
		pr_err("secvar: Failed to create firmware kobj\n");
		return -ENOMEM;
	}

	rc = sysfs_create_file(secvar_kobj, &format_attr.attr);
	if (rc) {
		kobject_put(secvar_kobj);
		return -ENOMEM;
	}

	secvar_kset = kset_create_and_add("vars", NULL, secvar_kobj);
	if (!secvar_kset) {
		pr_err("secvar: sysfs kobject registration failed.\n");
		kobject_put(secvar_kobj);
		return -ENOMEM;
	}

	rc = update_kobj_size();
	if (rc) {
		pr_err("Cannot read the size of the attribute\n");
		return rc;
	}

	secvar_sysfs_load();

	return 0;
}

late_initcall(secvar_sysfs_init);
