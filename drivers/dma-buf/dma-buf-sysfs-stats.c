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
#include <linux/workqueue.h>

#include "dma-buf-sysfs-stats.h"

#define to_dma_buf_entry_from_kobj(x) container_of(x, struct dma_buf_sysfs_entry, kobj)

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

void dma_buf_stats_teardown(struct dma_buf *dmabuf)
{
	struct dma_buf_sysfs_entry *sysfs_entry;

	sysfs_entry = dmabuf->sysfs_entry;
	if (!sysfs_entry)
		return;

	kobject_del(&sysfs_entry->kobj);
	kobject_put(&sysfs_entry->kobj);
}

/*
 * Statistics files do not need to send uevents.
 */
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

static void sysfs_add_workfn(struct work_struct *work)
{
	/* The ABI would have to change for this to be false, but let's be paranoid. */
	_Static_assert(sizeof(struct kobject) >= sizeof(struct work_struct),
		"kobject is smaller than work_struct");

	struct dma_buf_sysfs_entry *sysfs_entry =
		container_of((struct kobject *)work, struct dma_buf_sysfs_entry, kobj);
	struct dma_buf *dmabuf = sysfs_entry->dmabuf;

	/*
	 * A dmabuf is ref-counted via its file member. If this handler holds the only
	 * reference to the dmabuf, there is no need for sysfs kobject creation. This is an
	 * optimization and a race; when the reference count drops to 1 immediately after
	 * this check it is not harmful as the sysfs entry will still get cleaned up in
	 * dma_buf_stats_teardown, which won't get called until the final dmabuf reference
	 * is released, and that can't happen until the end of this function.
	 */
	if (file_count(dmabuf->file) > 1) {
		/*
		 * kobject_init_and_add expects kobject to be zero-filled, but we have populated it
		 * to trigger this work function.
		 */
		memset(&dmabuf->sysfs_entry->kobj, 0, sizeof(dmabuf->sysfs_entry->kobj));
		dmabuf->sysfs_entry->kobj.kset = dma_buf_per_buffer_stats_kset;
		if (kobject_init_and_add(&dmabuf->sysfs_entry->kobj, &dma_buf_ktype, NULL,
						"%lu", file_inode(dmabuf->file)->i_ino)) {
			kobject_put(&dmabuf->sysfs_entry->kobj);
			dmabuf->sysfs_entry = NULL;
		}
	} else {
		/*
		 * Free the sysfs_entry and reset the pointer so dma_buf_stats_teardown doesn't
		 * attempt to operate on it.
		 */
		kfree(dmabuf->sysfs_entry);
		dmabuf->sysfs_entry = NULL;
	}
	dma_buf_put(dmabuf);
}

int dma_buf_stats_setup(struct dma_buf *dmabuf)
{
	struct dma_buf_sysfs_entry *sysfs_entry;
	struct work_struct *work;

	if (!dmabuf || !dmabuf->file)
		return -EINVAL;

	if (!dmabuf->exp_name) {
		pr_err("exporter name must not be empty if stats needed\n");
		return -EINVAL;
	}

	sysfs_entry = kmalloc(sizeof(struct dma_buf_sysfs_entry), GFP_KERNEL);
	if (!sysfs_entry)
		return -ENOMEM;

	sysfs_entry->dmabuf = dmabuf;
	dmabuf->sysfs_entry = sysfs_entry;

	/*
	 * The use of kobj as a work_struct is an ugly hack
	 * to avoid an ABI break in this frozen kernel.
	 */
	work = (struct work_struct *)&dmabuf->sysfs_entry->kobj;
	INIT_WORK(work, sysfs_add_workfn);
	get_dma_buf(dmabuf); /* This reference will be dropped in sysfs_add_workfn. */
	schedule_work(work);

	return 0;
}
