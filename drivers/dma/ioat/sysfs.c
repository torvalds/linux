/*
 * Intel I/OAT DMA Linux driver
 * Copyright(c) 2004 - 2015 Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * The full GNU General Public License is included in this distribution in
 * the file called "COPYING".
 *
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/dmaengine.h>
#include <linux/pci.h>
#include "dma.h"
#include "registers.h"
#include "hw.h"

#include "../dmaengine.h"

static ssize_t cap_show(struct dma_chan *c, char *page)
{
	struct dma_device *dma = c->device;

	return sprintf(page, "copy%s%s%s%s%s\n",
		       dma_has_cap(DMA_PQ, dma->cap_mask) ? " pq" : "",
		       dma_has_cap(DMA_PQ_VAL, dma->cap_mask) ? " pq_val" : "",
		       dma_has_cap(DMA_XOR, dma->cap_mask) ? " xor" : "",
		       dma_has_cap(DMA_XOR_VAL, dma->cap_mask) ? " xor_val" : "",
		       dma_has_cap(DMA_INTERRUPT, dma->cap_mask) ? " intr" : "");

}
struct ioat_sysfs_entry ioat_cap_attr = __ATTR_RO(cap);

static ssize_t version_show(struct dma_chan *c, char *page)
{
	struct dma_device *dma = c->device;
	struct ioatdma_device *ioat_dma = to_ioatdma_device(dma);

	return sprintf(page, "%d.%d\n",
		       ioat_dma->version >> 4, ioat_dma->version & 0xf);
}
struct ioat_sysfs_entry ioat_version_attr = __ATTR_RO(version);

static ssize_t
ioat_attr_show(struct kobject *kobj, struct attribute *attr, char *page)
{
	struct ioat_sysfs_entry *entry;
	struct ioatdma_chan *ioat_chan;

	entry = container_of(attr, struct ioat_sysfs_entry, attr);
	ioat_chan = container_of(kobj, struct ioatdma_chan, kobj);

	if (!entry->show)
		return -EIO;
	return entry->show(&ioat_chan->dma_chan, page);
}

static ssize_t
ioat_attr_store(struct kobject *kobj, struct attribute *attr,
const char *page, size_t count)
{
	struct ioat_sysfs_entry *entry;
	struct ioatdma_chan *ioat_chan;

	entry = container_of(attr, struct ioat_sysfs_entry, attr);
	ioat_chan = container_of(kobj, struct ioatdma_chan, kobj);

	if (!entry->store)
		return -EIO;
	return entry->store(&ioat_chan->dma_chan, page, count);
}

const struct sysfs_ops ioat_sysfs_ops = {
	.show	= ioat_attr_show,
	.store  = ioat_attr_store,
};

void ioat_kobject_add(struct ioatdma_device *ioat_dma, struct kobj_type *type)
{
	struct dma_device *dma = &ioat_dma->dma_dev;
	struct dma_chan *c;

	list_for_each_entry(c, &dma->channels, device_node) {
		struct ioatdma_chan *ioat_chan = to_ioat_chan(c);
		struct kobject *parent = &c->dev->device.kobj;
		int err;

		err = kobject_init_and_add(&ioat_chan->kobj, type,
					   parent, "quickdata");
		if (err) {
			dev_warn(to_dev(ioat_chan),
				 "sysfs init error (%d), continuing...\n", err);
			kobject_put(&ioat_chan->kobj);
			set_bit(IOAT_KOBJ_INIT_FAIL, &ioat_chan->state);
		}
	}
}

void ioat_kobject_del(struct ioatdma_device *ioat_dma)
{
	struct dma_device *dma = &ioat_dma->dma_dev;
	struct dma_chan *c;

	list_for_each_entry(c, &dma->channels, device_node) {
		struct ioatdma_chan *ioat_chan = to_ioat_chan(c);

		if (!test_bit(IOAT_KOBJ_INIT_FAIL, &ioat_chan->state)) {
			kobject_del(&ioat_chan->kobj);
			kobject_put(&ioat_chan->kobj);
		}
	}
}

static ssize_t ring_size_show(struct dma_chan *c, char *page)
{
	struct ioatdma_chan *ioat_chan = to_ioat_chan(c);

	return sprintf(page, "%d\n", (1 << ioat_chan->alloc_order) & ~1);
}
static struct ioat_sysfs_entry ring_size_attr = __ATTR_RO(ring_size);

static ssize_t ring_active_show(struct dma_chan *c, char *page)
{
	struct ioatdma_chan *ioat_chan = to_ioat_chan(c);

	/* ...taken outside the lock, no need to be precise */
	return sprintf(page, "%d\n", ioat_ring_active(ioat_chan));
}
static struct ioat_sysfs_entry ring_active_attr = __ATTR_RO(ring_active);

static ssize_t intr_coalesce_show(struct dma_chan *c, char *page)
{
	struct ioatdma_chan *ioat_chan = to_ioat_chan(c);

	return sprintf(page, "%d\n", ioat_chan->intr_coalesce);
}

static ssize_t intr_coalesce_store(struct dma_chan *c, const char *page,
size_t count)
{
	int intr_coalesce = 0;
	struct ioatdma_chan *ioat_chan = to_ioat_chan(c);

	if (sscanf(page, "%du", &intr_coalesce) != -1) {
		if ((intr_coalesce < 0) ||
		    (intr_coalesce > IOAT_INTRDELAY_MASK))
			return -EINVAL;
		ioat_chan->intr_coalesce = intr_coalesce;
	}

	return count;
}

static struct ioat_sysfs_entry intr_coalesce_attr = __ATTR_RW(intr_coalesce);

static struct attribute *ioat_attrs[] = {
	&ring_size_attr.attr,
	&ring_active_attr.attr,
	&ioat_cap_attr.attr,
	&ioat_version_attr.attr,
	&intr_coalesce_attr.attr,
	NULL,
};

struct kobj_type ioat_ktype = {
	.sysfs_ops = &ioat_sysfs_ops,
	.default_attrs = ioat_attrs,
};
