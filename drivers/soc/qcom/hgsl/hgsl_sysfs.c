// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2023-2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/platform_device.h>
#include <linux/sysfs.h>

#include "hgsl.h"
#include "hgsl_sysfs.h"

struct hgsl_sysfs_client_attr {
	struct attribute attr;
	ssize_t (*show)(struct kobject *kobj,
				struct hgsl_sysfs_client_attr *attr,
				char *buf);
	ssize_t (*store)(struct kobject *kobj,
				struct hgsl_sysfs_client_attr *attr,
				const char *buf,
			ssize_t count);
};

struct hgsl_sysfs_client_malloc_attr {
	struct hgsl_sysfs_client_attr attr;
	ssize_t (*show)(struct hgsl_priv *priv, char *buf);
};

static ssize_t hgsl_sysfs_client_malloc_show(struct hgsl_priv *priv,
						char *buf)
{
	return scnprintf(buf, PAGE_SIZE, "%lld\n",
			atomic64_read(&priv->total_mem_size));
}

static ssize_t hgsl_sysfs_client_show(struct kobject *kobj,
		struct hgsl_sysfs_client_attr *_attr, char *buf)
{
	struct hgsl_sysfs_client_malloc_attr *client_attrib =
		container_of(_attr, struct hgsl_sysfs_client_malloc_attr, attr);
	struct hgsl_priv *priv =
		container_of(kobj, struct hgsl_priv, sysfs_client);

	return client_attrib->show(priv, buf);
}

static struct hgsl_sysfs_client_malloc_attr hgsl_sysfs_client_malloc_attr = {
	.attr = __ATTR(mem_alloc, 0444, hgsl_sysfs_client_show, NULL),
	.show = hgsl_sysfs_client_malloc_show,
};

static struct attribute *malloc_entry_attrs[] = {
	&hgsl_sysfs_client_malloc_attr.attr.attr,
	NULL,
};
ATTRIBUTE_GROUPS(malloc_entry);

static ssize_t hgsl_malloc_sysfs_show(struct kobject *kobj,
		struct attribute *_attr, char *buf)
{
	struct hgsl_sysfs_client_attr *pattrib =
		container_of(_attr, struct hgsl_sysfs_client_attr, attr);

	return pattrib->show(kobj, pattrib, buf);
}

static const struct sysfs_ops hgsl_malloc_sysfs_ops = {
	.show = hgsl_malloc_sysfs_show,
};

static struct kobj_type hgsl_sysfs_client_type = {
	.sysfs_ops = &hgsl_malloc_sysfs_ops,
	.default_groups = malloc_entry_groups,
};

int hgsl_sysfs_client_init(struct hgsl_priv *priv)
{
	int ret = 0;
	struct qcom_hgsl *hgsl = priv->dev;
	char name[9];

	snprintf(name, sizeof(name), "%d", priv->pid);

	ret = kobject_init_and_add(&priv->sysfs_client,
			&hgsl_sysfs_client_type,
			hgsl->clients_sysfs,
			name);

	if (unlikely(ret != 0))
		pr_warn("Create sysfs proc node failed.\n");

	return ret;
}

void hgsl_sysfs_client_release(struct hgsl_priv *priv)
{
	kobject_put(&priv->sysfs_client);
}

/* Global GPU memory usage */
static ssize_t total_mem_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct qcom_hgsl *hgsl = dev_get_drvdata(dev);

	return scnprintf(buf, PAGE_SIZE, "%lld\n", hgsl->total_mem_size);
}

static DEVICE_ATTR_RO(total_mem);

static const struct attribute *_attrs[] = {
	&dev_attr_total_mem.attr,
	NULL,
};

int hgsl_sysfs_init(struct platform_device *pdev)
{
	struct qcom_hgsl *hgsl = platform_get_drvdata(pdev);
	int ret;

	hgsl->clients_sysfs = kobject_create_and_add("clients",
					&hgsl->class_dev->kobj);

	if (hgsl->clients_sysfs == NULL) {
		ret = -EINVAL;
		goto exit;
	}

	ret = sysfs_create_files(&hgsl->class_dev->kobj, _attrs);
	if (!ret) {
		/* Notify userspace */
		kobject_uevent(&hgsl->dev->kobj, KOBJ_ADD);
	}

exit:
	return ret;
}


void hgsl_sysfs_release(struct platform_device *pdev)
{
	struct qcom_hgsl *hgsl;

	hgsl = platform_get_drvdata(pdev);

	sysfs_remove_files(&hgsl->class_dev->kobj, _attrs);
	if (hgsl->clients_sysfs) {
		kobject_put(hgsl->clients_sysfs);
		hgsl->clients_sysfs = NULL;
	}
}

