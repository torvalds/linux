// SPDX-License-Identifier: GPL-2.0
/*
 * Greybus operations
 *
 * Copyright 2015-2016 Google Inc.
 */

#include <linux/string.h>
#include <linux/sysfs.h>

#include "audio_manager.h"
#include "audio_manager_private.h"

static ssize_t manager_sysfs_add_store(struct kobject *kobj,
				       struct kobj_attribute *attr,
				       const char *buf, size_t count)
{
	struct gb_audio_manager_module_descriptor desc = { {0} };

	int num = sscanf(buf,
			"name=%" GB_AUDIO_MANAGER_MODULE_NAME_LEN_SSCANF "s "
			"vid=%d pid=%d intf_id=%d i/p devices=0x%X o/p devices=0x%X",
			desc.name, &desc.vid, &desc.pid, &desc.intf_id,
			&desc.ip_devices, &desc.op_devices);

	if (num != 7)
		return -EINVAL;

	num = gb_audio_manager_add(&desc);
	if (num < 0)
		return -EINVAL;

	return count;
}

static struct kobj_attribute manager_add_attribute =
	__ATTR(add, 0664, NULL, manager_sysfs_add_store);

static ssize_t manager_sysfs_remove_store(struct kobject *kobj,
					  struct kobj_attribute *attr,
					  const char *buf, size_t count)
{
	int id;

	int num = kstrtoint(buf, 10, &id);

	if (num != 1)
		return -EINVAL;

	num = gb_audio_manager_remove(id);
	if (num)
		return num;

	return count;
}

static struct kobj_attribute manager_remove_attribute =
	__ATTR(remove, 0664, NULL, manager_sysfs_remove_store);

static ssize_t manager_sysfs_dump_store(struct kobject *kobj,
					struct kobj_attribute *attr,
					const char *buf, size_t count)
{
	int id;

	int num = kstrtoint(buf, 10, &id);

	if (num == 1) {
		num = gb_audio_manager_dump_module(id);
		if (num)
			return num;
	} else if (!strncmp("all", buf, 3)) {
		gb_audio_manager_dump_all();
	} else {
		return -EINVAL;
	}

	return count;
}

static struct kobj_attribute manager_dump_attribute =
	__ATTR(dump, 0664, NULL, manager_sysfs_dump_store);

static void manager_sysfs_init_attribute(struct kobject *kobj,
					 struct kobj_attribute *kattr)
{
	int err;

	err = sysfs_create_file(kobj, &kattr->attr);
	if (err) {
		pr_warn("creating the sysfs entry for %s failed: %d\n",
			kattr->attr.name, err);
	}
}

void gb_audio_manager_sysfs_init(struct kobject *kobj)
{
	manager_sysfs_init_attribute(kobj, &manager_add_attribute);
	manager_sysfs_init_attribute(kobj, &manager_remove_attribute);
	manager_sysfs_init_attribute(kobj, &manager_dump_attribute);
}
