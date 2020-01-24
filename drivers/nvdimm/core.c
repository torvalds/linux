// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright(c) 2013-2015 Intel Corporation. All rights reserved.
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
#include <linux/io.h>
#include "nd-core.h"
#include "nd.h"

LIST_HEAD(nvdimm_bus_list);
DEFINE_MUTEX(nvdimm_bus_list_mutex);

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

struct nvdimm_map {
	struct nvdimm_bus *nvdimm_bus;
	struct list_head list;
	resource_size_t offset;
	unsigned long flags;
	size_t size;
	union {
		void *mem;
		void __iomem *iomem;
	};
	struct kref kref;
};

static struct nvdimm_map *find_nvdimm_map(struct device *dev,
		resource_size_t offset)
{
	struct nvdimm_bus *nvdimm_bus = walk_to_nvdimm_bus(dev);
	struct nvdimm_map *nvdimm_map;

	list_for_each_entry(nvdimm_map, &nvdimm_bus->mapping_list, list)
		if (nvdimm_map->offset == offset)
			return nvdimm_map;
	return NULL;
}

static struct nvdimm_map *alloc_nvdimm_map(struct device *dev,
		resource_size_t offset, size_t size, unsigned long flags)
{
	struct nvdimm_bus *nvdimm_bus = walk_to_nvdimm_bus(dev);
	struct nvdimm_map *nvdimm_map;

	nvdimm_map = kzalloc(sizeof(*nvdimm_map), GFP_KERNEL);
	if (!nvdimm_map)
		return NULL;

	INIT_LIST_HEAD(&nvdimm_map->list);
	nvdimm_map->nvdimm_bus = nvdimm_bus;
	nvdimm_map->offset = offset;
	nvdimm_map->flags = flags;
	nvdimm_map->size = size;
	kref_init(&nvdimm_map->kref);

	if (!request_mem_region(offset, size, dev_name(&nvdimm_bus->dev))) {
		dev_err(&nvdimm_bus->dev, "failed to request %pa + %zd for %s\n",
				&offset, size, dev_name(dev));
		goto err_request_region;
	}

	if (flags)
		nvdimm_map->mem = memremap(offset, size, flags);
	else
		nvdimm_map->iomem = ioremap(offset, size);

	if (!nvdimm_map->mem)
		goto err_map;

	dev_WARN_ONCE(dev, !is_nvdimm_bus_locked(dev), "%s: bus unlocked!",
			__func__);
	list_add(&nvdimm_map->list, &nvdimm_bus->mapping_list);

	return nvdimm_map;

 err_map:
	release_mem_region(offset, size);
 err_request_region:
	kfree(nvdimm_map);
	return NULL;
}

static void nvdimm_map_release(struct kref *kref)
{
	struct nvdimm_bus *nvdimm_bus;
	struct nvdimm_map *nvdimm_map;

	nvdimm_map = container_of(kref, struct nvdimm_map, kref);
	nvdimm_bus = nvdimm_map->nvdimm_bus;

	dev_dbg(&nvdimm_bus->dev, "%pa\n", &nvdimm_map->offset);
	list_del(&nvdimm_map->list);
	if (nvdimm_map->flags)
		memunmap(nvdimm_map->mem);
	else
		iounmap(nvdimm_map->iomem);
	release_mem_region(nvdimm_map->offset, nvdimm_map->size);
	kfree(nvdimm_map);
}

static void nvdimm_map_put(void *data)
{
	struct nvdimm_map *nvdimm_map = data;
	struct nvdimm_bus *nvdimm_bus = nvdimm_map->nvdimm_bus;

	nvdimm_bus_lock(&nvdimm_bus->dev);
	kref_put(&nvdimm_map->kref, nvdimm_map_release);
	nvdimm_bus_unlock(&nvdimm_bus->dev);
}

/**
 * devm_nvdimm_memremap - map a resource that is shared across regions
 * @dev: device that will own a reference to the shared mapping
 * @offset: physical base address of the mapping
 * @size: mapping size
 * @flags: memremap flags, or, if zero, perform an ioremap instead
 */
void *devm_nvdimm_memremap(struct device *dev, resource_size_t offset,
		size_t size, unsigned long flags)
{
	struct nvdimm_map *nvdimm_map;

	nvdimm_bus_lock(dev);
	nvdimm_map = find_nvdimm_map(dev, offset);
	if (!nvdimm_map)
		nvdimm_map = alloc_nvdimm_map(dev, offset, size, flags);
	else
		kref_get(&nvdimm_map->kref);
	nvdimm_bus_unlock(dev);

	if (!nvdimm_map)
		return NULL;

	if (devm_add_action_or_reset(dev, nvdimm_map_put, nvdimm_map))
		return NULL;

	return nvdimm_map->mem;
}
EXPORT_SYMBOL_GPL(devm_nvdimm_memremap);

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

struct nvdimm_bus_descriptor *to_nd_desc(struct nvdimm_bus *nvdimm_bus)
{
	/* struct nvdimm_bus definition is private to libnvdimm */
	return nvdimm_bus->nd_desc;
}
EXPORT_SYMBOL_GPL(to_nd_desc);

struct device *to_nvdimm_bus_dev(struct nvdimm_bus *nvdimm_bus)
{
	/* struct nvdimm_bus definition is private to libnvdimm */
	return &nvdimm_bus->dev;
}
EXPORT_SYMBOL_GPL(to_nvdimm_bus_dev);

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
			dev_dbg(dev, "pos: %d buf[%zd]: %c buf[%zd]: %c\n",
					i, str - buf, str[0],
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
 * LOCKING: expects nd_device_lock() is held on entry
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

ssize_t nd_size_select_show(unsigned long current_size,
		const unsigned long *supported, char *buf)
{
	ssize_t len = 0;
	int i;

	for (i = 0; supported[i]; i++)
		if (current_size == supported[i])
			len += sprintf(buf + len, "[%ld] ", supported[i]);
		else
			len += sprintf(buf + len, "%ld ", supported[i]);
	len += sprintf(buf + len, "\n");
	return len;
}

ssize_t nd_size_select_store(struct device *dev, const char *buf,
		unsigned long *current_size, const unsigned long *supported)
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
		*current_size = lbasize;
		return 0;
	} else {
		return -EINVAL;
	}
}

static ssize_t commands_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	int cmd, len = 0;
	struct nvdimm_bus *nvdimm_bus = to_nvdimm_bus(dev);
	struct nvdimm_bus_descriptor *nd_desc = nvdimm_bus->nd_desc;

	for_each_set_bit(cmd, &nd_desc->cmd_mask, BITS_PER_LONG)
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
	nd_device_lock(dev);
	nd_device_unlock(dev);
	return 0;
}

static int flush_regions_dimms(struct device *dev, void *data)
{
	nd_device_lock(dev);
	nd_device_unlock(dev);
	device_for_each_child(dev, NULL, flush_namespaces);
	return 0;
}

static ssize_t wait_probe_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct nvdimm_bus *nvdimm_bus = to_nvdimm_bus(dev);
	struct nvdimm_bus_descriptor *nd_desc = nvdimm_bus->nd_desc;
	int rc;

	if (nd_desc->flush_probe) {
		rc = nd_desc->flush_probe(nd_desc);
		if (rc)
			return rc;
	}
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

static const struct attribute_group nvdimm_bus_attribute_group = {
	.attrs = nvdimm_bus_attributes,
};

const struct attribute_group *nvdimm_bus_attribute_groups[] = {
	&nvdimm_bus_attribute_group,
	NULL,
};

int nvdimm_bus_add_badrange(struct nvdimm_bus *nvdimm_bus, u64 addr, u64 length)
{
	return badrange_add(&nvdimm_bus->badrange, addr, length);
}
EXPORT_SYMBOL_GPL(nvdimm_bus_add_badrange);

#ifdef CONFIG_BLK_DEV_INTEGRITY
int nd_integrity_init(struct gendisk *disk, unsigned long meta_size)
{
	struct blk_integrity bi;

	if (meta_size == 0)
		return 0;

	memset(&bi, 0, sizeof(bi));

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

	nd_label_init();

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
	nvdimm_devs_exit();
}

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Intel Corporation");
subsys_initcall(libnvdimm_init);
module_exit(libnvdimm_exit);
