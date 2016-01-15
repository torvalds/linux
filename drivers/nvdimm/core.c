/*
 * Copyright(c) 2013-2015 Intel Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 */
#include <linux/libnvdimm.h>
#include <linux/badblocks.h>
#include <linux/export.h>
#include <linux/module.h>
#include <linux/blkdev.h>
#include <linux/device.h>
#include <linux/ctype.h>
#include <linux/ndctl.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include "nd-core.h"
#include "nd.h"

LIST_HEAD(nvdimm_bus_list);
DEFINE_MUTEX(nvdimm_bus_list_mutex);
static DEFINE_IDA(nd_ida);

void nvdimm_bus_lock(struct device *dev)
{
	struct nvdimm_bus *nvdimm_bus = walk_to_nvdimm_bus(dev);

	if (!nvdimm_bus)
		return;
	mutex_lock(&nvdimm_bus->reconfig_mutex);
}
EXPORT_SYMBOL(nvdimm_bus_lock);

void nvdimm_bus_unlock(struct device *dev)
{
	struct nvdimm_bus *nvdimm_bus = walk_to_nvdimm_bus(dev);

	if (!nvdimm_bus)
		return;
	mutex_unlock(&nvdimm_bus->reconfig_mutex);
}
EXPORT_SYMBOL(nvdimm_bus_unlock);

bool is_nvdimm_bus_locked(struct device *dev)
{
	struct nvdimm_bus *nvdimm_bus = walk_to_nvdimm_bus(dev);

	if (!nvdimm_bus)
		return false;
	return mutex_is_locked(&nvdimm_bus->reconfig_mutex);
}
EXPORT_SYMBOL(is_nvdimm_bus_locked);

u64 nd_fletcher64(void *addr, size_t len, bool le)
{
	u32 *buf = addr;
	u32 lo32 = 0;
	u64 hi32 = 0;
	int i;

	for (i = 0; i < len / sizeof(u32); i++) {
		lo32 += le ? le32_to_cpu((__le32) buf[i]) : buf[i];
		hi32 += lo32;
	}

	return hi32 << 32 | lo32;
}
EXPORT_SYMBOL_GPL(nd_fletcher64);

static void nvdimm_bus_release(struct device *dev)
{
	struct nvdimm_bus *nvdimm_bus;

	nvdimm_bus = container_of(dev, struct nvdimm_bus, dev);
	ida_simple_remove(&nd_ida, nvdimm_bus->id);
	kfree(nvdimm_bus);
}

struct nvdimm_bus *to_nvdimm_bus(struct device *dev)
{
	struct nvdimm_bus *nvdimm_bus;

	nvdimm_bus = container_of(dev, struct nvdimm_bus, dev);
	WARN_ON(nvdimm_bus->dev.release != nvdimm_bus_release);
	return nvdimm_bus;
}
EXPORT_SYMBOL_GPL(to_nvdimm_bus);

struct nvdimm_bus_descriptor *to_nd_desc(struct nvdimm_bus *nvdimm_bus)
{
	/* struct nvdimm_bus definition is private to libnvdimm */
	return nvdimm_bus->nd_desc;
}
EXPORT_SYMBOL_GPL(to_nd_desc);

struct nvdimm_bus *walk_to_nvdimm_bus(struct device *nd_dev)
{
	struct device *dev;

	for (dev = nd_dev; dev; dev = dev->parent)
		if (dev->release == nvdimm_bus_release)
			break;
	dev_WARN_ONCE(nd_dev, !dev, "invalid dev, not on nd bus\n");
	if (dev)
		return to_nvdimm_bus(dev);
	return NULL;
}

static bool is_uuid_sep(char sep)
{
	if (sep == '\n' || sep == '-' || sep == ':' || sep == '\0')
		return true;
	return false;
}

static int nd_uuid_parse(struct device *dev, u8 *uuid_out, const char *buf,
		size_t len)
{
	const char *str = buf;
	u8 uuid[16];
	int i;

	for (i = 0; i < 16; i++) {
		if (!isxdigit(str[0]) || !isxdigit(str[1])) {
			dev_dbg(dev, "%s: pos: %d buf[%zd]: %c buf[%zd]: %c\n",
					__func__, i, str - buf, str[0],
					str + 1 - buf, str[1]);
			return -EINVAL;
		}

		uuid[i] = (hex_to_bin(str[0]) << 4) | hex_to_bin(str[1]);
		str += 2;
		if (is_uuid_sep(*str))
			str++;
	}

	memcpy(uuid_out, uuid, sizeof(uuid));
	return 0;
}

/**
 * nd_uuid_store: common implementation for writing 'uuid' sysfs attributes
 * @dev: container device for the uuid property
 * @uuid_out: uuid buffer to replace
 * @buf: raw sysfs buffer to parse
 *
 * Enforce that uuids can only be changed while the device is disabled
 * (driver detached)
 * LOCKING: expects device_lock() is held on entry
 */
int nd_uuid_store(struct device *dev, u8 **uuid_out, const char *buf,
		size_t len)
{
	u8 uuid[16];
	int rc;

	if (dev->driver)
		return -EBUSY;

	rc = nd_uuid_parse(dev, uuid, buf, len);
	if (rc)
		return rc;

	kfree(*uuid_out);
	*uuid_out = kmemdup(uuid, sizeof(uuid), GFP_KERNEL);
	if (!(*uuid_out))
		return -ENOMEM;

	return 0;
}

ssize_t nd_sector_size_show(unsigned long current_lbasize,
		const unsigned long *supported, char *buf)
{
	ssize_t len = 0;
	int i;

	for (i = 0; supported[i]; i++)
		if (current_lbasize == supported[i])
			len += sprintf(buf + len, "[%ld] ", supported[i]);
		else
			len += sprintf(buf + len, "%ld ", supported[i]);
	len += sprintf(buf + len, "\n");
	return len;
}

ssize_t nd_sector_size_store(struct device *dev, const char *buf,
		unsigned long *current_lbasize, const unsigned long *supported)
{
	unsigned long lbasize;
	int rc, i;

	if (dev->driver)
		return -EBUSY;

	rc = kstrtoul(buf, 0, &lbasize);
	if (rc)
		return rc;

	for (i = 0; supported[i]; i++)
		if (lbasize == supported[i])
			break;

	if (supported[i]) {
		*current_lbasize = lbasize;
		return 0;
	} else {
		return -EINVAL;
	}
}

void __nd_iostat_start(struct bio *bio, unsigned long *start)
{
	struct gendisk *disk = bio->bi_bdev->bd_disk;
	const int rw = bio_data_dir(bio);
	int cpu = part_stat_lock();

	*start = jiffies;
	part_round_stats(cpu, &disk->part0);
	part_stat_inc(cpu, &disk->part0, ios[rw]);
	part_stat_add(cpu, &disk->part0, sectors[rw], bio_sectors(bio));
	part_inc_in_flight(&disk->part0, rw);
	part_stat_unlock();
}
EXPORT_SYMBOL(__nd_iostat_start);

void nd_iostat_end(struct bio *bio, unsigned long start)
{
	struct gendisk *disk = bio->bi_bdev->bd_disk;
	unsigned long duration = jiffies - start;
	const int rw = bio_data_dir(bio);
	int cpu = part_stat_lock();

	part_stat_add(cpu, &disk->part0, ticks[rw], duration);
	part_round_stats(cpu, &disk->part0);
	part_dec_in_flight(&disk->part0, rw);
	part_stat_unlock();
}
EXPORT_SYMBOL(nd_iostat_end);

static ssize_t commands_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	int cmd, len = 0;
	struct nvdimm_bus *nvdimm_bus = to_nvdimm_bus(dev);
	struct nvdimm_bus_descriptor *nd_desc = nvdimm_bus->nd_desc;

	for_each_set_bit(cmd, &nd_desc->dsm_mask, BITS_PER_LONG)
		len += sprintf(buf + len, "%s ", nvdimm_bus_cmd_name(cmd));
	len += sprintf(buf + len, "\n");
	return len;
}
static DEVICE_ATTR_RO(commands);

static const char *nvdimm_bus_provider(struct nvdimm_bus *nvdimm_bus)
{
	struct nvdimm_bus_descriptor *nd_desc = nvdimm_bus->nd_desc;
	struct device *parent = nvdimm_bus->dev.parent;

	if (nd_desc->provider_name)
		return nd_desc->provider_name;
	else if (parent)
		return dev_name(parent);
	else
		return "unknown";
}

static ssize_t provider_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct nvdimm_bus *nvdimm_bus = to_nvdimm_bus(dev);

	return sprintf(buf, "%s\n", nvdimm_bus_provider(nvdimm_bus));
}
static DEVICE_ATTR_RO(provider);

static int flush_namespaces(struct device *dev, void *data)
{
	device_lock(dev);
	device_unlock(dev);
	return 0;
}

static int flush_regions_dimms(struct device *dev, void *data)
{
	device_lock(dev);
	device_unlock(dev);
	device_for_each_child(dev, NULL, flush_namespaces);
	return 0;
}

static ssize_t wait_probe_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	nd_synchronize();
	device_for_each_child(dev, NULL, flush_regions_dimms);
	return sprintf(buf, "1\n");
}
static DEVICE_ATTR_RO(wait_probe);

static struct attribute *nvdimm_bus_attributes[] = {
	&dev_attr_commands.attr,
	&dev_attr_wait_probe.attr,
	&dev_attr_provider.attr,
	NULL,
};

struct attribute_group nvdimm_bus_attribute_group = {
	.attrs = nvdimm_bus_attributes,
};
EXPORT_SYMBOL_GPL(nvdimm_bus_attribute_group);

struct nvdimm_bus *__nvdimm_bus_register(struct device *parent,
		struct nvdimm_bus_descriptor *nd_desc, struct module *module)
{
	struct nvdimm_bus *nvdimm_bus;
	int rc;

	nvdimm_bus = kzalloc(sizeof(*nvdimm_bus), GFP_KERNEL);
	if (!nvdimm_bus)
		return NULL;
	INIT_LIST_HEAD(&nvdimm_bus->list);
	INIT_LIST_HEAD(&nvdimm_bus->poison_list);
	init_waitqueue_head(&nvdimm_bus->probe_wait);
	nvdimm_bus->id = ida_simple_get(&nd_ida, 0, 0, GFP_KERNEL);
	mutex_init(&nvdimm_bus->reconfig_mutex);
	if (nvdimm_bus->id < 0) {
		kfree(nvdimm_bus);
		return NULL;
	}
	nvdimm_bus->nd_desc = nd_desc;
	nvdimm_bus->module = module;
	nvdimm_bus->dev.parent = parent;
	nvdimm_bus->dev.release = nvdimm_bus_release;
	nvdimm_bus->dev.groups = nd_desc->attr_groups;
	dev_set_name(&nvdimm_bus->dev, "ndbus%d", nvdimm_bus->id);
	rc = device_register(&nvdimm_bus->dev);
	if (rc) {
		dev_dbg(&nvdimm_bus->dev, "registration failed: %d\n", rc);
		goto err;
	}

	rc = nvdimm_bus_create_ndctl(nvdimm_bus);
	if (rc)
		goto err;

	mutex_lock(&nvdimm_bus_list_mutex);
	list_add_tail(&nvdimm_bus->list, &nvdimm_bus_list);
	mutex_unlock(&nvdimm_bus_list_mutex);

	return nvdimm_bus;
 err:
	put_device(&nvdimm_bus->dev);
	return NULL;
}
EXPORT_SYMBOL_GPL(__nvdimm_bus_register);

static void set_badblock(struct badblocks *bb, sector_t s, int num)
{
	dev_dbg(bb->dev, "Found a poison range (0x%llx, 0x%llx)\n",
			(u64) s * 512, (u64) num * 512);
	/* this isn't an error as the hardware will still throw an exception */
	if (badblocks_set(bb, s, num, 1))
		dev_info_once(bb->dev, "%s: failed for sector %llx\n",
				__func__, (u64) s);
}

/**
 * __add_badblock_range() - Convert a physical address range to bad sectors
 * @bb:		badblocks instance to populate
 * @ns_offset:	namespace offset where the error range begins (in bytes)
 * @len:	number of bytes of poison to be added
 *
 * This assumes that the range provided with (ns_offset, len) is within
 * the bounds of physical addresses for this namespace, i.e. lies in the
 * interval [ns_start, ns_start + ns_size)
 */
static void __add_badblock_range(struct badblocks *bb, u64 ns_offset, u64 len)
{
	const unsigned int sector_size = 512;
	sector_t start_sector;
	u64 num_sectors;
	u32 rem;

	start_sector = div_u64(ns_offset, sector_size);
	num_sectors = div_u64_rem(len, sector_size, &rem);
	if (rem)
		num_sectors++;

	if (unlikely(num_sectors > (u64)INT_MAX)) {
		u64 remaining = num_sectors;
		sector_t s = start_sector;

		while (remaining) {
			int done = min_t(u64, remaining, INT_MAX);

			set_badblock(bb, s, done);
			remaining -= done;
			s += done;
		}
	} else
		set_badblock(bb, start_sector, num_sectors);
}

/**
 * nvdimm_namespace_add_poison() - Convert a list of poison ranges to badblocks
 * @ndns:	the namespace containing poison ranges
 * @bb:		badblocks instance to populate
 * @offset:	offset at the start of the namespace before 'sector 0'
 *
 * The poison list generated during NFIT initialization may contain multiple,
 * possibly overlapping ranges in the SPA (System Physical Address) space.
 * Compare each of these ranges to the namespace currently being initialized,
 * and add badblocks to the gendisk for all matching sub-ranges
 */
void nvdimm_namespace_add_poison(struct nd_namespace_common *ndns,
		struct badblocks *bb, resource_size_t offset)
{
	struct nd_namespace_io *nsio = to_nd_namespace_io(&ndns->dev);
	struct nd_region *nd_region = to_nd_region(ndns->dev.parent);
	struct nvdimm_bus *nvdimm_bus;
	struct list_head *poison_list;
	u64 ns_start, ns_end, ns_size;
	struct nd_poison *pl;

	ns_size = nvdimm_namespace_capacity(ndns) - offset;
	ns_start = nsio->res.start + offset;
	ns_end = nsio->res.end;

	nvdimm_bus = to_nvdimm_bus(nd_region->dev.parent);
	poison_list = &nvdimm_bus->poison_list;
	if (list_empty(poison_list))
		return;

	list_for_each_entry(pl, poison_list, list) {
		u64 pl_end = pl->start + pl->length - 1;

		/* Discard intervals with no intersection */
		if (pl_end < ns_start)
			continue;
		if (pl->start > ns_end)
			continue;
		/* Deal with any overlap after start of the namespace */
		if (pl->start >= ns_start) {
			u64 start = pl->start;
			u64 len;

			if (pl_end <= ns_end)
				len = pl->length;
			else
				len = ns_start + ns_size - pl->start;
			__add_badblock_range(bb, start - ns_start, len);
			continue;
		}
		/* Deal with overlap for poison starting before the namespace */
		if (pl->start < ns_start) {
			u64 len;

			if (pl_end < ns_end)
				len = pl->start + pl->length - ns_start;
			else
				len = ns_size;
			__add_badblock_range(bb, 0, len);
		}
	}
}
EXPORT_SYMBOL_GPL(nvdimm_namespace_add_poison);

static int __add_poison(struct nvdimm_bus *nvdimm_bus, u64 addr, u64 length)
{
	struct nd_poison *pl;

	pl = kzalloc(sizeof(*pl), GFP_KERNEL);
	if (!pl)
		return -ENOMEM;

	pl->start = addr;
	pl->length = length;
	list_add_tail(&pl->list, &nvdimm_bus->poison_list);

	return 0;
}

int nvdimm_bus_add_poison(struct nvdimm_bus *nvdimm_bus, u64 addr, u64 length)
{
	struct nd_poison *pl;

	if (list_empty(&nvdimm_bus->poison_list))
		return __add_poison(nvdimm_bus, addr, length);

	/*
	 * There is a chance this is a duplicate, check for those first.
	 * This will be the common case as ARS_STATUS returns all known
	 * errors in the SPA space, and we can't query it per region
	 */
	list_for_each_entry(pl, &nvdimm_bus->poison_list, list)
		if (pl->start == addr) {
			/* If length has changed, update this list entry */
			if (pl->length != length)
				pl->length = length;
			return 0;
		}

	/*
	 * If not a duplicate or a simple length update, add the entry as is,
	 * as any overlapping ranges will get resolved when the list is consumed
	 * and converted to badblocks
	 */
	return __add_poison(nvdimm_bus, addr, length);
}
EXPORT_SYMBOL_GPL(nvdimm_bus_add_poison);

static void free_poison_list(struct list_head *poison_list)
{
	struct nd_poison *pl, *next;

	list_for_each_entry_safe(pl, next, poison_list, list) {
		list_del(&pl->list);
		kfree(pl);
	}
	list_del_init(poison_list);
}

static int child_unregister(struct device *dev, void *data)
{
	/*
	 * the singular ndctl class device per bus needs to be
	 * "device_destroy"ed, so skip it here
	 *
	 * i.e. remove classless children
	 */
	if (dev->class)
		/* pass */;
	else
		nd_device_unregister(dev, ND_SYNC);
	return 0;
}

void nvdimm_bus_unregister(struct nvdimm_bus *nvdimm_bus)
{
	if (!nvdimm_bus)
		return;

	mutex_lock(&nvdimm_bus_list_mutex);
	list_del_init(&nvdimm_bus->list);
	mutex_unlock(&nvdimm_bus_list_mutex);

	nd_synchronize();
	device_for_each_child(&nvdimm_bus->dev, NULL, child_unregister);
	free_poison_list(&nvdimm_bus->poison_list);
	nvdimm_bus_destroy_ndctl(nvdimm_bus);

	device_unregister(&nvdimm_bus->dev);
}
EXPORT_SYMBOL_GPL(nvdimm_bus_unregister);

#ifdef CONFIG_BLK_DEV_INTEGRITY
int nd_integrity_init(struct gendisk *disk, unsigned long meta_size)
{
	struct blk_integrity bi;

	if (meta_size == 0)
		return 0;

	bi.profile = NULL;
	bi.tuple_size = meta_size;
	bi.tag_size = meta_size;

	blk_integrity_register(disk, &bi);
	blk_queue_max_integrity_segments(disk->queue, 1);

	return 0;
}
EXPORT_SYMBOL(nd_integrity_init);

#else /* CONFIG_BLK_DEV_INTEGRITY */
int nd_integrity_init(struct gendisk *disk, unsigned long meta_size)
{
	return 0;
}
EXPORT_SYMBOL(nd_integrity_init);

#endif

static __init int libnvdimm_init(void)
{
	int rc;

	rc = nvdimm_bus_init();
	if (rc)
		return rc;
	rc = nvdimm_init();
	if (rc)
		goto err_dimm;
	rc = nd_region_init();
	if (rc)
		goto err_region;
	return 0;
 err_region:
	nvdimm_exit();
 err_dimm:
	nvdimm_bus_exit();
	return rc;
}

static __exit void libnvdimm_exit(void)
{
	WARN_ON(!list_empty(&nvdimm_bus_list));
	nd_region_exit();
	nvdimm_exit();
	nvdimm_bus_exit();
}

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Intel Corporation");
subsys_initcall(libnvdimm_init);
module_exit(libnvdimm_exit);
