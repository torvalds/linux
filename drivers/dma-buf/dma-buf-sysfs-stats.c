// SPDX-License-Identifier: GPL-2.0-only
/*
 * DMA-BUF sysfs statistics.
 *
 * Copyright (C) 2021 Google LLC.
 */

#include <linux/dma-buf.h>
#include <linux/dma-resv.h>
#include <linux/kobject.h>
#include <linux/printk.h>
#include <linux/slab.h>
#include <linux/sysfs.h>

#include "dma-buf-sysfs-stats.h"

#define to_dma_buf_entry_from_kobj(x) container_of(x, struct dma_buf_sysfs_entry, kobj)

/**
 * DOC: overview
 *
 * ``/sys/kernel/debug/dma_buf/bufinfo`` provides an overview of every DMA-BUF
 * in the system. However, since debugfs is not safe to be mounted in
 * production, procfs and sysfs can be used to gather DMA-BUF statistics on
 * production systems.
 *
 * The ``/proc/<pid>/fdinfo/<fd>`` files in procfs can be used to gather
 * information about DMA-BUF fds. Detailed documentation about the interface
 * is present in Documentation/filesystems/proc.rst.
 *
 * Unfortunately, the existing procfs interfaces can only provide information
 * about the DMA-BUFs for which processes hold fds or have the buffers mmapped
 * into their address space. This necessitated the creation of the DMA-BUF sysfs
 * statistics interface to provide per-buffer information on production systems.
 *
 * The interface at ``/sys/kernel/dma-buf/buffers`` exposes information about
 * every DMA-BUF when ``CONFIG_DMABUF_SYSFS_STATS`` is enabled.
 *
 * The following stats are exposed by the interface:
 *
 * * ``/sys/kernel/dmabuf/buffers/<inode_number>/exporter_name``
 * * ``/sys/kernel/dmabuf/buffers/<inode_number>/size``
 * * ``/sys/kernel/dmabuf/buffers/<inode_number>/attachments/<attach_uid>/device``
 * * ``/sys/kernel/dmabuf/buffers/<inode_number>/attachments/<attach_uid>/map_counter``
 *
 * The information in the interface can also be used to derive per-exporter and
 * per-device usage statistics. The data from the interface can be gathered
 * on error conditions or other important events to provide a snapshot of
 * DMA-BUF usage. It can also be collected periodically by telemetry to monitor
 * various metrics.
 *
 * Detailed documentation about the interface is present in
 * Documentation/ABI/testing/sysfs-kernel-dmabuf-buffers.
 */

struct dma_buf_stats_attribute {
	struct attribute attr;
	ssize_t (*show)(struct dma_buf *dmabuf,
			struct dma_buf_stats_attribute *attr, char *buf);
};
#define to_dma_buf_stats_attr(x) container_of(x, struct dma_buf_stats_attribute, attr)

static ssize_t dma_buf_stats_attribute_show(struct kobject *kobj,
					    struct attribute *attr,
					    char *buf)
{
	struct dma_buf_stats_attribute *attribute;
	struct dma_buf_sysfs_entry *sysfs_entry;
	struct dma_buf *dmabuf;

	attribute = to_dma_buf_stats_attr(attr);
	sysfs_entry = to_dma_buf_entry_from_kobj(kobj);
	dmabuf = sysfs_entry->dmabuf;

	if (!dmabuf || !attribute->show)
		return -EIO;

	return attribute->show(dmabuf, attribute, buf);
}

static const struct sysfs_ops dma_buf_stats_sysfs_ops = {
	.show = dma_buf_stats_attribute_show,
};

static ssize_t exporter_name_show(struct dma_buf *dmabuf,
				  struct dma_buf_stats_attribute *attr,
				  char *buf)
{
	return sysfs_emit(buf, "%s\n", dmabuf->exp_name);
}

static ssize_t size_show(struct dma_buf *dmabuf,
			 struct dma_buf_stats_attribute *attr,
			 char *buf)
{
	return sysfs_emit(buf, "%zu\n", dmabuf->size);
}

static struct dma_buf_stats_attribute exporter_name_attribute =
	__ATTR_RO(exporter_name);
static struct dma_buf_stats_attribute size_attribute = __ATTR_RO(size);

static struct attribute *dma_buf_stats_default_attrs[] = {
	&exporter_name_attribute.attr,
	&size_attribute.attr,
	NULL,
};
ATTRIBUTE_GROUPS(dma_buf_stats_default);

static void dma_buf_sysfs_release(struct kobject *kobj)
{
	struct dma_buf_sysfs_entry *sysfs_entry;

	sysfs_entry = to_dma_buf_entry_from_kobj(kobj);
	kfree(sysfs_entry);
}

static struct kobj_type dma_buf_ktype = {
	.sysfs_ops = &dma_buf_stats_sysfs_ops,
	.release = dma_buf_sysfs_release,
	.default_groups = dma_buf_stats_default_groups,
};

#define to_dma_buf_attach_entry_from_kobj(x) container_of(x, struct dma_buf_attach_sysfs_entry, kobj)

struct dma_buf_attach_stats_attribute {
	struct attribute attr;
	ssize_t (*show)(struct dma_buf_attach_sysfs_entry *sysfs_entry,
			struct dma_buf_attach_stats_attribute *attr, char *buf);
};
#define to_dma_buf_attach_stats_attr(x) container_of(x, struct dma_buf_attach_stats_attribute, attr)

static ssize_t dma_buf_attach_stats_attribute_show(struct kobject *kobj,
						   struct attribute *attr,
						   char *buf)
{
	struct dma_buf_attach_stats_attribute *attribute;
	struct dma_buf_attach_sysfs_entry *sysfs_entry;

	attribute = to_dma_buf_attach_stats_attr(attr);
	sysfs_entry = to_dma_buf_attach_entry_from_kobj(kobj);

	if (!attribute->show)
		return -EIO;

	return attribute->show(sysfs_entry, attribute, buf);
}

static const struct sysfs_ops dma_buf_attach_stats_sysfs_ops = {
	.show = dma_buf_attach_stats_attribute_show,
};

static ssize_t map_counter_show(struct dma_buf_attach_sysfs_entry *sysfs_entry,
				struct dma_buf_attach_stats_attribute *attr,
				char *buf)
{
	return sysfs_emit(buf, "%u\n", sysfs_entry->map_counter);
}

static struct dma_buf_attach_stats_attribute map_counter_attribute =
	__ATTR_RO(map_counter);

static struct attribute *dma_buf_attach_stats_default_attrs[] = {
	&map_counter_attribute.attr,
	NULL,
};
ATTRIBUTE_GROUPS(dma_buf_attach_stats_default);

static void dma_buf_attach_sysfs_release(struct kobject *kobj)
{
	struct dma_buf_attach_sysfs_entry *sysfs_entry;

	sysfs_entry = to_dma_buf_attach_entry_from_kobj(kobj);
	kfree(sysfs_entry);
}

static struct kobj_type dma_buf_attach_ktype = {
	.sysfs_ops = &dma_buf_attach_stats_sysfs_ops,
	.release = dma_buf_attach_sysfs_release,
	.default_groups = dma_buf_attach_stats_default_groups,
};

void dma_buf_attach_stats_teardown(struct dma_buf_attachment *attach)
{
	struct dma_buf_attach_sysfs_entry *sysfs_entry;

	sysfs_entry = attach->sysfs_entry;
	if (!sysfs_entry)
		return;

	sysfs_delete_link(&sysfs_entry->kobj, &attach->dev->kobj, "device");

	kobject_del(&sysfs_entry->kobj);
	kobject_put(&sysfs_entry->kobj);
}

int dma_buf_attach_stats_setup(struct dma_buf_attachment *attach,
			       unsigned int uid)
{
	struct dma_buf_attach_sysfs_entry *sysfs_entry;
	int ret;
	struct dma_buf *dmabuf;

	if (!attach)
		return -EINVAL;

	dmabuf = attach->dmabuf;

	sysfs_entry = kzalloc(sizeof(struct dma_buf_attach_sysfs_entry),
			      GFP_KERNEL);
	if (!sysfs_entry)
		return -ENOMEM;

	sysfs_entry->kobj.kset = dmabuf->sysfs_entry->attach_stats_kset;

	attach->sysfs_entry = sysfs_entry;

	ret = kobject_init_and_add(&sysfs_entry->kobj, &dma_buf_attach_ktype,
				   NULL, "%u", uid);
	if (ret)
		goto kobj_err;

	ret = sysfs_create_link(&sysfs_entry->kobj, &attach->dev->kobj,
				"device");
	if (ret)
		goto link_err;

	return 0;

link_err:
	kobject_del(&sysfs_entry->kobj);
kobj_err:
	kobject_put(&sysfs_entry->kobj);
	attach->sysfs_entry = NULL;

	return ret;
}
void dma_buf_stats_teardown(struct dma_buf *dmabuf)
{
	struct dma_buf_sysfs_entry *sysfs_entry;

	sysfs_entry = dmabuf->sysfs_entry;
	if (!sysfs_entry)
		return;

	kset_unregister(sysfs_entry->attach_stats_kset);
	kobject_del(&sysfs_entry->kobj);
	kobject_put(&sysfs_entry->kobj);
}


/* Statistics files do not need to send uevents. */
static int dmabuf_sysfs_uevent_filter(struct kset *kset, struct kobject *kobj)
{
	return 0;
}

static const struct kset_uevent_ops dmabuf_sysfs_no_uevent_ops = {
	.filter = dmabuf_sysfs_uevent_filter,
};

static struct kset *dma_buf_stats_kset;
static struct kset *dma_buf_per_buffer_stats_kset;
int dma_buf_init_sysfs_statistics(void)
{
	dma_buf_stats_kset = kset_create_and_add("dmabuf",
						 &dmabuf_sysfs_no_uevent_ops,
						 kernel_kobj);
	if (!dma_buf_stats_kset)
		return -ENOMEM;

	dma_buf_per_buffer_stats_kset = kset_create_and_add("buffers",
							    &dmabuf_sysfs_no_uevent_ops,
							    &dma_buf_stats_kset->kobj);
	if (!dma_buf_per_buffer_stats_kset) {
		kset_unregister(dma_buf_stats_kset);
		return -ENOMEM;
	}

	return 0;
}

void dma_buf_uninit_sysfs_statistics(void)
{
	kset_unregister(dma_buf_per_buffer_stats_kset);
	kset_unregister(dma_buf_stats_kset);
}

int dma_buf_stats_setup(struct dma_buf *dmabuf)
{
	struct dma_buf_sysfs_entry *sysfs_entry;
	int ret;
	struct kset *attach_stats_kset;

	if (!dmabuf || !dmabuf->file)
		return -EINVAL;

	if (!dmabuf->exp_name) {
		pr_err("exporter name must not be empty if stats needed\n");
		return -EINVAL;
	}

	sysfs_entry = kzalloc(sizeof(struct dma_buf_sysfs_entry), GFP_KERNEL);
	if (!sysfs_entry)
		return -ENOMEM;

	sysfs_entry->kobj.kset = dma_buf_per_buffer_stats_kset;
	sysfs_entry->dmabuf = dmabuf;

	dmabuf->sysfs_entry = sysfs_entry;

	/* create the directory for buffer stats */
	ret = kobject_init_and_add(&sysfs_entry->kobj, &dma_buf_ktype, NULL,
				   "%lu", file_inode(dmabuf->file)->i_ino);
	if (ret)
		goto err_sysfs_dmabuf;

	/* create the directory for attachment stats */
	attach_stats_kset = kset_create_and_add("attachments",
						&dmabuf_sysfs_no_uevent_ops,
						&sysfs_entry->kobj);
	if (!attach_stats_kset) {
		ret = -ENOMEM;
		goto err_sysfs_attach;
	}

	sysfs_entry->attach_stats_kset = attach_stats_kset;

	return 0;

err_sysfs_attach:
	kobject_del(&sysfs_entry->kobj);
err_sysfs_dmabuf:
	kobject_put(&sysfs_entry->kobj);
	dmabuf->sysfs_entry = NULL;
	return ret;
}
