// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2023 Red Hat
 */

#include "uds-sysfs.h"

#include <linux/kobject.h>
#include <linux/module.h>
#include <linux/slab.h>

#include "logger.h"
#include "memory-alloc.h"
#include "string-utils.h"
#include "uds.h"

#define UDS_SYSFS_NAME "uds"

static struct {
	/* /sys/uds */
	struct kobject kobj;
	/* /sys/uds/parameter */
	struct kobject parameter_kobj;

	/* These flags are used to ensure a clean shutdown */

	/* /sys/uds flag */
	bool flag;
	/* /sys/uds/parameter flag */
	bool parameter_flag;
} object_root;

static char *buffer_to_string(const char *buf, size_t length)
{
	char *string;

	if (uds_allocate(length + 1, char, __func__, &string) != UDS_SUCCESS)
		return NULL;

	memcpy(string, buf, length);
	string[length] = '\0';
	if (string[length - 1] == '\n')
		string[length - 1] = '\0';

	return string;
}

/*
 * This is the code for any directory in the /sys/<module_name> tree that contains no regular files
 * (only subdirectories).
 */

static void empty_release(struct kobject *kobj)
{
}

static ssize_t empty_show(struct kobject *kobj, struct attribute *attr, char *buf)
{
	return 0;
}

static ssize_t empty_store(struct kobject *kobj, struct attribute *attr, const char *buf,
			   size_t length)
{
	return length;
}

static const struct sysfs_ops empty_ops = {
	.show = empty_show,
	.store = empty_store,
};

static struct attribute *empty_attrs[] = {
	NULL,
};
ATTRIBUTE_GROUPS(empty);

static const struct kobj_type empty_object_type = {
	.release = empty_release,
	.sysfs_ops = &empty_ops,
	.default_groups = empty_groups,
};

/*
 * This is the code for the /sys/<module_name>/parameter directory.
 * <dir>/log_level                 UDS_LOG_LEVEL
 */

struct parameter_attribute {
	struct attribute attr;
	const char *(*show_string)(void);
	void (*store_string)(const char *string);
};

static ssize_t parameter_show(struct kobject *kobj, struct attribute *attr, char *buf)
{
	struct parameter_attribute *pa;

	pa = container_of(attr, struct parameter_attribute, attr);
	if (pa->show_string != NULL)
		return sprintf(buf, "%s\n", pa->show_string());
	else
		return -EINVAL;
}

static ssize_t parameter_store(struct kobject *kobj, struct attribute *attr,
			       const char *buf, size_t length)
{
	char *string;
	struct parameter_attribute *pa;

	pa = container_of(attr, struct parameter_attribute, attr);
	if (pa->store_string == NULL)
		return -EINVAL;
	string = buffer_to_string(buf, length);
	if (string == NULL)
		return -ENOMEM;

	pa->store_string(string);
	uds_free(string);
	return length;
}

static const char *parameter_show_log_level(void)
{
	return uds_log_priority_to_string(uds_get_log_level());
}

static void parameter_store_log_level(const char *string)
{
	uds_set_log_level(uds_log_string_to_priority(string));
}

static struct parameter_attribute log_level_attr = {
	.attr = { .name = "log_level", .mode = 0600 },
	.show_string = parameter_show_log_level,
	.store_string = parameter_store_log_level,
};

static struct attribute *parameter_attrs[] = {
	&log_level_attr.attr,
	NULL,
};
ATTRIBUTE_GROUPS(parameter);

static const struct sysfs_ops parameter_ops = {
	.show = parameter_show,
	.store = parameter_store,
};

static const struct kobj_type parameter_object_type = {
	.release = empty_release,
	.sysfs_ops = &parameter_ops,
	.default_groups = parameter_groups,
};

int uds_init_sysfs(void)
{
	int result;

	memset(&object_root, 0, sizeof(object_root));
	kobject_init(&object_root.kobj, &empty_object_type);
	result = kobject_add(&object_root.kobj, NULL, UDS_SYSFS_NAME);
	if (result == 0) {
		object_root.flag = true;
		kobject_init(&object_root.parameter_kobj, &parameter_object_type);
		result = kobject_add(&object_root.parameter_kobj, &object_root.kobj,
				     "parameter");
		if (result == 0)
			object_root.parameter_flag = true;
	}

	if (result != 0)
		uds_put_sysfs();

	return result;
}

void uds_put_sysfs(void)
{
	if (object_root.parameter_flag)
		kobject_put(&object_root.parameter_kobj);

	if (object_root.flag)
		kobject_put(&object_root.kobj);
}
