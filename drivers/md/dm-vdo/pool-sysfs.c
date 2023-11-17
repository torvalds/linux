// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2023 Red Hat
 */

#include "pool-sysfs.h"

#include <linux/kstrtox.h>

#include "memory-alloc.h"
#include "string-utils.h"

#include "data-vio.h"
#include "dedupe.h"
#include "vdo.h"

struct pool_attribute {
	struct attribute attr;
	ssize_t (*show)(struct vdo *vdo, char *buf);
	ssize_t (*store)(struct vdo *vdo, const char *value, size_t count);
};

static ssize_t vdo_pool_attr_show(struct kobject *directory, struct attribute *attr,
				  char *buf)
{
	struct pool_attribute *pool_attr = container_of(attr, struct pool_attribute,
							attr);
	struct vdo *vdo = container_of(directory, struct vdo, vdo_directory);

	if (pool_attr->show == NULL)
		return -EINVAL;
	return pool_attr->show(vdo, buf);
}

static ssize_t vdo_pool_attr_store(struct kobject *directory, struct attribute *attr,
				   const char *buf, size_t length)
{
	struct pool_attribute *pool_attr = container_of(attr, struct pool_attribute,
							attr);
	struct vdo *vdo = container_of(directory, struct vdo, vdo_directory);

	if (pool_attr->store == NULL)
		return -EINVAL;
	return pool_attr->store(vdo, buf, length);
}

static const struct sysfs_ops vdo_pool_sysfs_ops = {
	.show = vdo_pool_attr_show,
	.store = vdo_pool_attr_store,
};

static ssize_t pool_compressing_show(struct vdo *vdo, char *buf)
{
	return sprintf(buf, "%s\n", (vdo_get_compressing(vdo) ? "1" : "0"));
}

static ssize_t pool_discards_active_show(struct vdo *vdo, char *buf)
{
	return sprintf(buf, "%u\n",
		       get_data_vio_pool_active_discards(vdo->data_vio_pool));
}

static ssize_t pool_discards_limit_show(struct vdo *vdo, char *buf)
{
	return sprintf(buf, "%u\n", get_data_vio_pool_discard_limit(vdo->data_vio_pool));
}

static ssize_t pool_discards_limit_store(struct vdo *vdo, const char *buf, size_t length)
{
	unsigned int value;
	int result;

	if ((length > 12) || (kstrtouint(buf, 10, &value) < 0) || (value < 1))
		return -EINVAL;

	result = set_data_vio_pool_discard_limit(vdo->data_vio_pool, value);
	if (result != VDO_SUCCESS)
		return -EINVAL;

	return length;
}

static ssize_t pool_discards_maximum_show(struct vdo *vdo, char *buf)
{
	return sprintf(buf, "%u\n",
		       get_data_vio_pool_maximum_discards(vdo->data_vio_pool));
}

static ssize_t pool_instance_show(struct vdo *vdo, char *buf)
{
	return sprintf(buf, "%u\n", vdo->instance);
}

static ssize_t pool_requests_active_show(struct vdo *vdo, char *buf)
{
	return sprintf(buf, "%u\n",
		       get_data_vio_pool_active_requests(vdo->data_vio_pool));
}

static ssize_t pool_requests_limit_show(struct vdo *vdo, char *buf)
{
	return sprintf(buf, "%u\n", get_data_vio_pool_request_limit(vdo->data_vio_pool));
}

static ssize_t pool_requests_maximum_show(struct vdo *vdo, char *buf)
{
	return sprintf(buf, "%u\n",
		       get_data_vio_pool_maximum_requests(vdo->data_vio_pool));
}

static void vdo_pool_release(struct kobject *directory)
{
	uds_free(container_of(directory, struct vdo, vdo_directory));
}

static struct pool_attribute vdo_pool_compressing_attr = {
	.attr = {
			.name = "compressing",
			.mode = 0444,
		},
	.show = pool_compressing_show,
};

static struct pool_attribute vdo_pool_discards_active_attr = {
	.attr = {
			.name = "discards_active",
			.mode = 0444,
		},
	.show = pool_discards_active_show,
};

static struct pool_attribute vdo_pool_discards_limit_attr = {
	.attr = {
			.name = "discards_limit",
			.mode = 0644,
		},
	.show = pool_discards_limit_show,
	.store = pool_discards_limit_store,
};

static struct pool_attribute vdo_pool_discards_maximum_attr = {
	.attr = {
			.name = "discards_maximum",
			.mode = 0444,
		},
	.show = pool_discards_maximum_show,
};

static struct pool_attribute vdo_pool_instance_attr = {
	.attr = {
			.name = "instance",
			.mode = 0444,
		},
	.show = pool_instance_show,
};

static struct pool_attribute vdo_pool_requests_active_attr = {
	.attr = {
			.name = "requests_active",
			.mode = 0444,
		},
	.show = pool_requests_active_show,
};

static struct pool_attribute vdo_pool_requests_limit_attr = {
	.attr = {
			.name = "requests_limit",
			.mode = 0444,
		},
	.show = pool_requests_limit_show,
};

static struct pool_attribute vdo_pool_requests_maximum_attr = {
	.attr = {
			.name = "requests_maximum",
			.mode = 0444,
		},
	.show = pool_requests_maximum_show,
};

static struct attribute *pool_attrs[] = {
	&vdo_pool_compressing_attr.attr,
	&vdo_pool_discards_active_attr.attr,
	&vdo_pool_discards_limit_attr.attr,
	&vdo_pool_discards_maximum_attr.attr,
	&vdo_pool_instance_attr.attr,
	&vdo_pool_requests_active_attr.attr,
	&vdo_pool_requests_limit_attr.attr,
	&vdo_pool_requests_maximum_attr.attr,
	NULL,
};
ATTRIBUTE_GROUPS(pool);

const struct kobj_type vdo_directory_type = {
	.release = vdo_pool_release,
	.sysfs_ops = &vdo_pool_sysfs_ops,
	.default_groups = pool_groups,
};
