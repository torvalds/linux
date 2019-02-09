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
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt
#include <linux/libnvdimm.h>
#include <linux/sched/mm.h>
#include <linux/vmalloc.h>
#include <linux/uaccess.h>
#include <linux/module.h>
#include <linux/blkdev.h>
#include <linux/fcntl.h>
#include <linux/async.h>
#include <linux/genhd.h>
#include <linux/ndctl.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/io.h>
#include <linux/mm.h>
#include <linux/nd.h>
#include "nd-core.h"
#include "nd.h"
#include "pfn.h"

int nvdimm_major;
static int nvdimm_bus_major;
static struct class *nd_class;
static DEFINE_IDA(nd_ida);

static int to_nd_device_type(struct device *dev)
{
	if (is_nvdimm(dev))
		return ND_DEVICE_DIMM;
	else if (is_memory(dev))
		return ND_DEVICE_REGION_PMEM;
	else if (is_nd_blk(dev))
		return ND_DEVICE_REGION_BLK;
	else if (is_nd_dax(dev))
		return ND_DEVICE_DAX_PMEM;
	else if (is_nd_region(dev->parent))
		return nd_region_to_nstype(to_nd_region(dev->parent));

	return 0;
}

static int nvdimm_bus_uevent(struct device *dev, struct kobj_uevent_env *env)
{
	/*
	 * Ensure that region devices always have their numa node set as
	 * early as possible.
	 */
	if (is_nd_region(dev))
		set_dev_node(dev, to_nd_region(dev)->numa_node);
	return add_uevent_var(env, "MODALIAS=" ND_DEVICE_MODALIAS_FMT,
			to_nd_device_type(dev));
}

static struct module *to_bus_provider(struct device *dev)
{
	/* pin bus providers while regions are enabled */
	if (is_nd_region(dev)) {
		struct nvdimm_bus *nvdimm_bus = walk_to_nvdimm_bus(dev);

		return nvdimm_bus->nd_desc->module;
	}
	return NULL;
}

static void nvdimm_bus_probe_start(struct nvdimm_bus *nvdimm_bus)
{
	nvdimm_bus_lock(&nvdimm_bus->dev);
	nvdimm_bus->probe_active++;
	nvdimm_bus_unlock(&nvdimm_bus->dev);
}

static void nvdimm_bus_probe_end(struct nvdimm_bus *nvdimm_bus)
{
	nvdimm_bus_lock(&nvdimm_bus->dev);
	if (--nvdimm_bus->probe_active == 0)
		wake_up(&nvdimm_bus->probe_wait);
	nvdimm_bus_unlock(&nvdimm_bus->dev);
}

static int nvdimm_bus_probe(struct device *dev)
{
	struct nd_device_driver *nd_drv = to_nd_device_driver(dev->driver);
	struct module *provider = to_bus_provider(dev);
	struct nvdimm_bus *nvdimm_bus = walk_to_nvdimm_bus(dev);
	int rc;

	if (!try_module_get(provider))
		return -ENXIO;

	dev_dbg(&nvdimm_bus->dev, "START: %s.probe(%s)\n",
			dev->driver->name, dev_name(dev));

	nvdimm_bus_probe_start(nvdimm_bus);
	rc = nd_drv->probe(dev);
	if (rc == 0)
		nd_region_probe_success(nvdimm_bus, dev);
	else
		nd_region_disable(nvdimm_bus, dev);
	nvdimm_bus_probe_end(nvdimm_bus);

	dev_dbg(&nvdimm_bus->dev, "END: %s.probe(%s) = %d\n", dev->driver->name,
			dev_name(dev), rc);

	if (rc != 0)
		module_put(provider);
	return rc;
}

static int nvdimm_bus_remove(struct device *dev)
{
	struct nd_device_driver *nd_drv = to_nd_device_driver(dev->driver);
	struct module *provider = to_bus_provider(dev);
	struct nvdimm_bus *nvdimm_bus = walk_to_nvdimm_bus(dev);
	int rc = 0;

	if (nd_drv->remove)
		rc = nd_drv->remove(dev);
	nd_region_disable(nvdimm_bus, dev);

	dev_dbg(&nvdimm_bus->dev, "%s.remove(%s) = %d\n", dev->driver->name,
			dev_name(dev), rc);
	module_put(provider);
	return rc;
}

static void nvdimm_bus_shutdown(struct device *dev)
{
	struct nvdimm_bus *nvdimm_bus = walk_to_nvdimm_bus(dev);
	struct nd_device_driver *nd_drv = NULL;

	if (dev->driver)
		nd_drv = to_nd_device_driver(dev->driver);

	if (nd_drv && nd_drv->shutdown) {
		nd_drv->shutdown(dev);
		dev_dbg(&nvdimm_bus->dev, "%s.shutdown(%s)\n",
				dev->driver->name, dev_name(dev));
	}
}

void nd_device_notify(struct device *dev, enum nvdimm_event event)
{
	device_lock(dev);
	if (dev->driver) {
		struct nd_device_driver *nd_drv;

		nd_drv = to_nd_device_driver(dev->driver);
		if (nd_drv->notify)
			nd_drv->notify(dev, event);
	}
	device_unlock(dev);
}
EXPORT_SYMBOL(nd_device_notify);

void nvdimm_region_notify(struct nd_region *nd_region, enum nvdimm_event event)
{
	struct nvdimm_bus *nvdimm_bus = walk_to_nvdimm_bus(&nd_region->dev);

	if (!nvdimm_bus)
		return;

	/* caller is responsible for holding a reference on the device */
	nd_device_notify(&nd_region->dev, event);
}
EXPORT_SYMBOL_GPL(nvdimm_region_notify);

struct clear_badblocks_context {
	resource_size_t phys, cleared;
};

static int nvdimm_clear_badblocks_region(struct device *dev, void *data)
{
	struct clear_badblocks_context *ctx = data;
	struct nd_region *nd_region;
	resource_size_t ndr_end;
	sector_t sector;

	/* make sure device is a region */
	if (!is_nd_pmem(dev))
		return 0;

	nd_region = to_nd_region(dev);
	ndr_end = nd_region->ndr_start + nd_region->ndr_size - 1;

	/* make sure we are in the region */
	if (ctx->phys < nd_region->ndr_start
			|| (ctx->phys + ctx->cleared) > ndr_end)
		return 0;

	sector = (ctx->phys - nd_region->ndr_start) / 512;
	badblocks_clear(&nd_region->bb, sector, ctx->cleared / 512);

	if (nd_region->bb_state)
		sysfs_notify_dirent(nd_region->bb_state);

	return 0;
}

static void nvdimm_clear_badblocks_regions(struct nvdimm_bus *nvdimm_bus,
		phys_addr_t phys, u64 cleared)
{
	struct clear_badblocks_context ctx = {
		.phys = phys,
		.cleared = cleared,
	};

	device_for_each_child(&nvdimm_bus->dev, &ctx,
			nvdimm_clear_badblocks_region);
}

static void nvdimm_account_cleared_poison(struct nvdimm_bus *nvdimm_bus,
		phys_addr_t phys, u64 cleared)
{
	if (cleared > 0)
		badrange_forget(&nvdimm_bus->badrange, phys, cleared);

	if (cleared > 0 && cleared / 512)
		nvdimm_clear_badblocks_regions(nvdimm_bus, phys, cleared);
}

long nvdimm_clear_poison(struct device *dev, phys_addr_t phys,
		unsigned int len)
{
	struct nvdimm_bus *nvdimm_bus = walk_to_nvdimm_bus(dev);
	struct nvdimm_bus_descriptor *nd_desc;
	struct nd_cmd_clear_error clear_err;
	struct nd_cmd_ars_cap ars_cap;
	u32 clear_err_unit, mask;
	unsigned int noio_flag;
	int cmd_rc, rc;

	if (!nvdimm_bus)
		return -ENXIO;

	nd_desc = nvdimm_bus->nd_desc;
	/*
	 * if ndctl does not exist, it's PMEM_LEGACY and
	 * we want to just pretend everything is handled.
	 */
	if (!nd_desc->ndctl)
		return len;

	memset(&ars_cap, 0, sizeof(ars_cap));
	ars_cap.address = phys;
	ars_cap.length = len;
	noio_flag = memalloc_noio_save();
	rc = nd_desc->ndctl(nd_desc, NULL, ND_CMD_ARS_CAP, &ars_cap,
			sizeof(ars_cap), &cmd_rc);
	memalloc_noio_restore(noio_flag);
	if (rc < 0)
		return rc;
	if (cmd_rc < 0)
		return cmd_rc;
	clear_err_unit = ars_cap.clear_err_unit;
	if (!clear_err_unit || !is_power_of_2(clear_err_unit))
		return -ENXIO;

	mask = clear_err_unit - 1;
	if ((phys | len) & mask)
		return -ENXIO;
	memset(&clear_err, 0, sizeof(clear_err));
	clear_err.address = phys;
	clear_err.length = len;
	noio_flag = memalloc_noio_save();
	rc = nd_desc->ndctl(nd_desc, NULL, ND_CMD_CLEAR_ERROR, &clear_err,
			sizeof(clear_err), &cmd_rc);
	memalloc_noio_restore(noio_flag);
	if (rc < 0)
		return rc;
	if (cmd_rc < 0)
		return cmd_rc;

	nvdimm_account_cleared_poison(nvdimm_bus, phys, clear_err.cleared);

	return clear_err.cleared;
}
EXPORT_SYMBOL_GPL(nvdimm_clear_poison);

static int nvdimm_bus_match(struct device *dev, struct device_driver *drv);

static struct bus_type nvdimm_bus_type = {
	.name = "nd",
	.uevent = nvdimm_bus_uevent,
	.match = nvdimm_bus_match,
	.probe = nvdimm_bus_probe,
	.remove = nvdimm_bus_remove,
	.shutdown = nvdimm_bus_shutdown,
};

static void nvdimm_bus_release(struct device *dev)
{
	struct nvdimm_bus *nvdimm_bus;

	nvdimm_bus = container_of(dev, struct nvdimm_bus, dev);
	ida_simple_remove(&nd_ida, nvdimm_bus->id);
	kfree(nvdimm_bus);
}

static bool is_nvdimm_bus(struct device *dev)
{
	return dev->release == nvdimm_bus_release;
}

struct nvdimm_bus *walk_to_nvdimm_bus(struct device *nd_dev)
{
	struct device *dev;

	for (dev = nd_dev; dev; dev = dev->parent)
		if (is_nvdimm_bus(dev))
			break;
	dev_WARN_ONCE(nd_dev, !dev, "invalid dev, not on nd bus\n");
	if (dev)
		return to_nvdimm_bus(dev);
	return NULL;
}

struct nvdimm_bus *to_nvdimm_bus(struct device *dev)
{
	struct nvdimm_bus *nvdimm_bus;

	nvdimm_bus = container_of(dev, struct nvdimm_bus, dev);
	WARN_ON(!is_nvdimm_bus(dev));
	return nvdimm_bus;
}
EXPORT_SYMBOL_GPL(to_nvdimm_bus);

struct nvdimm_bus *nvdimm_bus_register(struct device *parent,
		struct nvdimm_bus_descriptor *nd_desc)
{
	struct nvdimm_bus *nvdimm_bus;
	int rc;

	nvdimm_bus = kzalloc(sizeof(*nvdimm_bus), GFP_KERNEL);
	if (!nvdimm_bus)
		return NULL;
	INIT_LIST_HEAD(&nvdimm_bus->list);
	INIT_LIST_HEAD(&nvdimm_bus->mapping_list);
	init_waitqueue_head(&nvdimm_bus->probe_wait);
	nvdimm_bus->id = ida_simple_get(&nd_ida, 0, 0, GFP_KERNEL);
	mutex_init(&nvdimm_bus->reconfig_mutex);
	badrange_init(&nvdimm_bus->badrange);
	if (nvdimm_bus->id < 0) {
		kfree(nvdimm_bus);
		return NULL;
	}
	nvdimm_bus->nd_desc = nd_desc;
	nvdimm_bus->dev.parent = parent;
	nvdimm_bus->dev.release = nvdimm_bus_release;
	nvdimm_bus->dev.groups = nd_desc->attr_groups;
	nvdimm_bus->dev.bus = &nvdimm_bus_type;
	nvdimm_bus->dev.of_node = nd_desc->of_node;
	dev_set_name(&nvdimm_bus->dev, "ndbus%d", nvdimm_bus->id);
	rc = device_register(&nvdimm_bus->dev);
	if (rc) {
		dev_dbg(&nvdimm_bus->dev, "registration failed: %d\n", rc);
		goto err;
	}

	return nvdimm_bus;
 err:
	put_device(&nvdimm_bus->dev);
	return NULL;
}
EXPORT_SYMBOL_GPL(nvdimm_bus_register);

void nvdimm_bus_unregister(struct nvdimm_bus *nvdimm_bus)
{
	if (!nvdimm_bus)
		return;
	device_unregister(&nvdimm_bus->dev);
}
EXPORT_SYMBOL_GPL(nvdimm_bus_unregister);

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

static void free_badrange_list(struct list_head *badrange_list)
{
	struct badrange_entry *bre, *next;

	list_for_each_entry_safe(bre, next, badrange_list, list) {
		list_del(&bre->list);
		kfree(bre);
	}
	list_del_init(badrange_list);
}

static int nd_bus_remove(struct device *dev)
{
	struct nvdimm_bus *nvdimm_bus = to_nvdimm_bus(dev);

	mutex_lock(&nvdimm_bus_list_mutex);
	list_del_init(&nvdimm_bus->list);
	mutex_unlock(&nvdimm_bus_list_mutex);

	nd_synchronize();
	device_for_each_child(&nvdimm_bus->dev, NULL, child_unregister);

	spin_lock(&nvdimm_bus->badrange.lock);
	free_badrange_list(&nvdimm_bus->badrange.list);
	spin_unlock(&nvdimm_bus->badrange.lock);

	nvdimm_bus_destroy_ndctl(nvdimm_bus);

	return 0;
}

static int nd_bus_probe(struct device *dev)
{
	struct nvdimm_bus *nvdimm_bus = to_nvdimm_bus(dev);
	int rc;

	rc = nvdimm_bus_create_ndctl(nvdimm_bus);
	if (rc)
		return rc;

	mutex_lock(&nvdimm_bus_list_mutex);
	list_add_tail(&nvdimm_bus->list, &nvdimm_bus_list);
	mutex_unlock(&nvdimm_bus_list_mutex);

	/* enable bus provider attributes to look up their local context */
	dev_set_drvdata(dev, nvdimm_bus->nd_desc);

	return 0;
}

static struct nd_device_driver nd_bus_driver = {
	.probe = nd_bus_probe,
	.remove = nd_bus_remove,
	.drv = {
		.name = "nd_bus",
		.suppress_bind_attrs = true,
		.bus = &nvdimm_bus_type,
		.owner = THIS_MODULE,
		.mod_name = KBUILD_MODNAME,
	},
};

static int nvdimm_bus_match(struct device *dev, struct device_driver *drv)
{
	struct nd_device_driver *nd_drv = to_nd_device_driver(drv);

	if (is_nvdimm_bus(dev) && nd_drv == &nd_bus_driver)
		return true;

	return !!test_bit(to_nd_device_type(dev), &nd_drv->type);
}

static ASYNC_DOMAIN_EXCLUSIVE(nd_async_domain);

void nd_synchronize(void)
{
	async_synchronize_full_domain(&nd_async_domain);
}
EXPORT_SYMBOL_GPL(nd_synchronize);

static void nd_async_device_register(void *d, async_cookie_t cookie)
{
	struct device *dev = d;

	if (device_add(dev) != 0) {
		dev_err(dev, "%s: failed\n", __func__);
		put_device(dev);
	}
	put_device(dev);
	if (dev->parent)
		put_device(dev->parent);
}

static void nd_async_device_unregister(void *d, async_cookie_t cookie)
{
	struct device *dev = d;

	/* flush bus operations before delete */
	nvdimm_bus_lock(dev);
	nvdimm_bus_unlock(dev);

	device_unregister(dev);
	put_device(dev);
}

void __nd_device_register(struct device *dev)
{
	if (!dev)
		return;
	dev->bus = &nvdimm_bus_type;
	if (dev->parent)
		get_device(dev->parent);
	get_device(dev);
	async_schedule_domain(nd_async_device_register, dev,
			&nd_async_domain);
}

void nd_device_register(struct device *dev)
{
	device_initialize(dev);
	__nd_device_register(dev);
}
EXPORT_SYMBOL(nd_device_register);

void nd_device_unregister(struct device *dev, enum nd_async_mode mode)
{
	switch (mode) {
	case ND_ASYNC:
		get_device(dev);
		async_schedule_domain(nd_async_device_unregister, dev,
				&nd_async_domain);
		break;
	case ND_SYNC:
		nd_synchronize();
		device_unregister(dev);
		break;
	}
}
EXPORT_SYMBOL(nd_device_unregister);

/**
 * __nd_driver_register() - register a region or a namespace driver
 * @nd_drv: driver to register
 * @owner: automatically set by nd_driver_register() macro
 * @mod_name: automatically set by nd_driver_register() macro
 */
int __nd_driver_register(struct nd_device_driver *nd_drv, struct module *owner,
		const char *mod_name)
{
	struct device_driver *drv = &nd_drv->drv;

	if (!nd_drv->type) {
		pr_debug("driver type bitmask not set (%pf)\n",
				__builtin_return_address(0));
		return -EINVAL;
	}

	if (!nd_drv->probe) {
		pr_debug("%s ->probe() must be specified\n", mod_name);
		return -EINVAL;
	}

	drv->bus = &nvdimm_bus_type;
	drv->owner = owner;
	drv->mod_name = mod_name;

	return driver_register(drv);
}
EXPORT_SYMBOL(__nd_driver_register);

int nvdimm_revalidate_disk(struct gendisk *disk)
{
	struct device *dev = disk_to_dev(disk)->parent;
	struct nd_region *nd_region = to_nd_region(dev->parent);
	int disk_ro = get_disk_ro(disk);

	/*
	 * Upgrade to read-only if the region is read-only preserve as
	 * read-only if the disk is already read-only.
	 */
	if (disk_ro || nd_region->ro == disk_ro)
		return 0;

	dev_info(dev, "%s read-only, marking %s read-only\n",
			dev_name(&nd_region->dev), disk->disk_name);
	set_disk_ro(disk, 1);

	return 0;

}
EXPORT_SYMBOL(nvdimm_revalidate_disk);

static ssize_t modalias_show(struct device *dev, struct device_attribute *attr,
		char *buf)
{
	return sprintf(buf, ND_DEVICE_MODALIAS_FMT "\n",
			to_nd_device_type(dev));
}
static DEVICE_ATTR_RO(modalias);

static ssize_t devtype_show(struct device *dev, struct device_attribute *attr,
		char *buf)
{
	return sprintf(buf, "%s\n", dev->type->name);
}
static DEVICE_ATTR_RO(devtype);

static struct attribute *nd_device_attributes[] = {
	&dev_attr_modalias.attr,
	&dev_attr_devtype.attr,
	NULL,
};

/**
 * nd_device_attribute_group - generic attributes for all devices on an nd bus
 */
struct attribute_group nd_device_attribute_group = {
	.attrs = nd_device_attributes,
};
EXPORT_SYMBOL_GPL(nd_device_attribute_group);

static ssize_t numa_node_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", dev_to_node(dev));
}
static DEVICE_ATTR_RO(numa_node);

static struct attribute *nd_numa_attributes[] = {
	&dev_attr_numa_node.attr,
	NULL,
};

static umode_t nd_numa_attr_visible(struct kobject *kobj, struct attribute *a,
		int n)
{
	if (!IS_ENABLED(CONFIG_NUMA))
		return 0;

	return a->mode;
}

/**
 * nd_numa_attribute_group - NUMA attributes for all devices on an nd bus
 */
struct attribute_group nd_numa_attribute_group = {
	.attrs = nd_numa_attributes,
	.is_visible = nd_numa_attr_visible,
};
EXPORT_SYMBOL_GPL(nd_numa_attribute_group);

int nvdimm_bus_create_ndctl(struct nvdimm_bus *nvdimm_bus)
{
	dev_t devt = MKDEV(nvdimm_bus_major, nvdimm_bus->id);
	struct device *dev;

	dev = device_create(nd_class, &nvdimm_bus->dev, devt, nvdimm_bus,
			"ndctl%d", nvdimm_bus->id);

	if (IS_ERR(dev))
		dev_dbg(&nvdimm_bus->dev, "failed to register ndctl%d: %ld\n",
				nvdimm_bus->id, PTR_ERR(dev));
	return PTR_ERR_OR_ZERO(dev);
}

void nvdimm_bus_destroy_ndctl(struct nvdimm_bus *nvdimm_bus)
{
	device_destroy(nd_class, MKDEV(nvdimm_bus_major, nvdimm_bus->id));
}

static const struct nd_cmd_desc __nd_cmd_dimm_descs[] = {
	[ND_CMD_IMPLEMENTED] = { },
	[ND_CMD_SMART] = {
		.out_num = 2,
		.out_sizes = { 4, 128, },
	},
	[ND_CMD_SMART_THRESHOLD] = {
		.out_num = 2,
		.out_sizes = { 4, 8, },
	},
	[ND_CMD_DIMM_FLAGS] = {
		.out_num = 2,
		.out_sizes = { 4, 4 },
	},
	[ND_CMD_GET_CONFIG_SIZE] = {
		.out_num = 3,
		.out_sizes = { 4, 4, 4, },
	},
	[ND_CMD_GET_CONFIG_DATA] = {
		.in_num = 2,
		.in_sizes = { 4, 4, },
		.out_num = 2,
		.out_sizes = { 4, UINT_MAX, },
	},
	[ND_CMD_SET_CONFIG_DATA] = {
		.in_num = 3,
		.in_sizes = { 4, 4, UINT_MAX, },
		.out_num = 1,
		.out_sizes = { 4, },
	},
	[ND_CMD_VENDOR] = {
		.in_num = 3,
		.in_sizes = { 4, 4, UINT_MAX, },
		.out_num = 3,
		.out_sizes = { 4, 4, UINT_MAX, },
	},
	[ND_CMD_CALL] = {
		.in_num = 2,
		.in_sizes = { sizeof(struct nd_cmd_pkg), UINT_MAX, },
		.out_num = 1,
		.out_sizes = { UINT_MAX, },
	},
};

const struct nd_cmd_desc *nd_cmd_dimm_desc(int cmd)
{
	if (cmd < ARRAY_SIZE(__nd_cmd_dimm_descs))
		return &__nd_cmd_dimm_descs[cmd];
	return NULL;
}
EXPORT_SYMBOL_GPL(nd_cmd_dimm_desc);

static const struct nd_cmd_desc __nd_cmd_bus_descs[] = {
	[ND_CMD_IMPLEMENTED] = { },
	[ND_CMD_ARS_CAP] = {
		.in_num = 2,
		.in_sizes = { 8, 8, },
		.out_num = 4,
		.out_sizes = { 4, 4, 4, 4, },
	},
	[ND_CMD_ARS_START] = {
		.in_num = 5,
		.in_sizes = { 8, 8, 2, 1, 5, },
		.out_num = 2,
		.out_sizes = { 4, 4, },
	},
	[ND_CMD_ARS_STATUS] = {
		.out_num = 3,
		.out_sizes = { 4, 4, UINT_MAX, },
	},
	[ND_CMD_CLEAR_ERROR] = {
		.in_num = 2,
		.in_sizes = { 8, 8, },
		.out_num = 3,
		.out_sizes = { 4, 4, 8, },
	},
	[ND_CMD_CALL] = {
		.in_num = 2,
		.in_sizes = { sizeof(struct nd_cmd_pkg), UINT_MAX, },
		.out_num = 1,
		.out_sizes = { UINT_MAX, },
	},
};

const struct nd_cmd_desc *nd_cmd_bus_desc(int cmd)
{
	if (cmd < ARRAY_SIZE(__nd_cmd_bus_descs))
		return &__nd_cmd_bus_descs[cmd];
	return NULL;
}
EXPORT_SYMBOL_GPL(nd_cmd_bus_desc);

u32 nd_cmd_in_size(struct nvdimm *nvdimm, int cmd,
		const struct nd_cmd_desc *desc, int idx, void *buf)
{
	if (idx >= desc->in_num)
		return UINT_MAX;

	if (desc->in_sizes[idx] < UINT_MAX)
		return desc->in_sizes[idx];

	if (nvdimm && cmd == ND_CMD_SET_CONFIG_DATA && idx == 2) {
		struct nd_cmd_set_config_hdr *hdr = buf;

		return hdr->in_length;
	} else if (nvdimm && cmd == ND_CMD_VENDOR && idx == 2) {
		struct nd_cmd_vendor_hdr *hdr = buf;

		return hdr->in_length;
	} else if (cmd == ND_CMD_CALL) {
		struct nd_cmd_pkg *pkg = buf;

		return pkg->nd_size_in;
	}

	return UINT_MAX;
}
EXPORT_SYMBOL_GPL(nd_cmd_in_size);

u32 nd_cmd_out_size(struct nvdimm *nvdimm, int cmd,
		const struct nd_cmd_desc *desc, int idx, const u32 *in_field,
		const u32 *out_field, unsigned long remainder)
{
	if (idx >= desc->out_num)
		return UINT_MAX;

	if (desc->out_sizes[idx] < UINT_MAX)
		return desc->out_sizes[idx];

	if (nvdimm && cmd == ND_CMD_GET_CONFIG_DATA && idx == 1)
		return in_field[1];
	else if (nvdimm && cmd == ND_CMD_VENDOR && idx == 2)
		return out_field[1];
	else if (!nvdimm && cmd == ND_CMD_ARS_STATUS && idx == 2) {
		/*
		 * Per table 9-276 ARS Data in ACPI 6.1, out_field[1] is
		 * "Size of Output Buffer in bytes, including this
		 * field."
		 */
		if (out_field[1] < 4)
			return 0;
		/*
		 * ACPI 6.1 is ambiguous if 'status' is included in the
		 * output size. If we encounter an output size that
		 * overshoots the remainder by 4 bytes, assume it was
		 * including 'status'.
		 */
		if (out_field[1] - 4 == remainder)
			return remainder;
		return out_field[1] - 8;
	} else if (cmd == ND_CMD_CALL) {
		struct nd_cmd_pkg *pkg = (struct nd_cmd_pkg *) in_field;

		return pkg->nd_size_out;
	}


	return UINT_MAX;
}
EXPORT_SYMBOL_GPL(nd_cmd_out_size);

void wait_nvdimm_bus_probe_idle(struct device *dev)
{
	struct nvdimm_bus *nvdimm_bus = walk_to_nvdimm_bus(dev);

	do {
		if (nvdimm_bus->probe_active == 0)
			break;
		nvdimm_bus_unlock(&nvdimm_bus->dev);
		wait_event(nvdimm_bus->probe_wait,
				nvdimm_bus->probe_active == 0);
		nvdimm_bus_lock(&nvdimm_bus->dev);
	} while (true);
}

static int nd_pmem_forget_poison_check(struct device *dev, void *data)
{
	struct nd_cmd_clear_error *clear_err =
		(struct nd_cmd_clear_error *)data;
	struct nd_btt *nd_btt = is_nd_btt(dev) ? to_nd_btt(dev) : NULL;
	struct nd_pfn *nd_pfn = is_nd_pfn(dev) ? to_nd_pfn(dev) : NULL;
	struct nd_dax *nd_dax = is_nd_dax(dev) ? to_nd_dax(dev) : NULL;
	struct nd_namespace_common *ndns = NULL;
	struct nd_namespace_io *nsio;
	resource_size_t offset = 0, end_trunc = 0, start, end, pstart, pend;

	if (nd_dax || !dev->driver)
		return 0;

	start = clear_err->address;
	end = clear_err->address + clear_err->cleared - 1;

	if (nd_btt || nd_pfn || nd_dax) {
		if (nd_btt)
			ndns = nd_btt->ndns;
		else if (nd_pfn)
			ndns = nd_pfn->ndns;
		else if (nd_dax)
			ndns = nd_dax->nd_pfn.ndns;

		if (!ndns)
			return 0;
	} else
		ndns = to_ndns(dev);

	nsio = to_nd_namespace_io(&ndns->dev);
	pstart = nsio->res.start + offset;
	pend = nsio->res.end - end_trunc;

	if ((pstart >= start) && (pend <= end))
		return -EBUSY;

	return 0;

}

static int nd_ns_forget_poison_check(struct device *dev, void *data)
{
	return device_for_each_child(dev, data, nd_pmem_forget_poison_check);
}

/* set_config requires an idle interleave set */
static int nd_cmd_clear_to_send(struct nvdimm_bus *nvdimm_bus,
		struct nvdimm *nvdimm, unsigned int cmd, void *data)
{
	struct nvdimm_bus_descriptor *nd_desc = nvdimm_bus->nd_desc;

	/* ask the bus provider if it would like to block this request */
	if (nd_desc->clear_to_send) {
		int rc = nd_desc->clear_to_send(nd_desc, nvdimm, cmd);

		if (rc)
			return rc;
	}

	/* require clear error to go through the pmem driver */
	if (!nvdimm && cmd == ND_CMD_CLEAR_ERROR)
		return device_for_each_child(&nvdimm_bus->dev, data,
				nd_ns_forget_poison_check);

	if (!nvdimm || cmd != ND_CMD_SET_CONFIG_DATA)
		return 0;

	/* prevent label manipulation while the kernel owns label updates */
	wait_nvdimm_bus_probe_idle(&nvdimm_bus->dev);
	if (atomic_read(&nvdimm->busy))
		return -EBUSY;
	return 0;
}

static int __nd_ioctl(struct nvdimm_bus *nvdimm_bus, struct nvdimm *nvdimm,
		int read_only, unsigned int ioctl_cmd, unsigned long arg)
{
	struct nvdimm_bus_descriptor *nd_desc = nvdimm_bus->nd_desc;
	static char out_env[ND_CMD_MAX_ENVELOPE];
	static char in_env[ND_CMD_MAX_ENVELOPE];
	const struct nd_cmd_desc *desc = NULL;
	unsigned int cmd = _IOC_NR(ioctl_cmd);
	struct device *dev = &nvdimm_bus->dev;
	void __user *p = (void __user *) arg;
	const char *cmd_name, *dimm_name;
	u32 in_len = 0, out_len = 0;
	unsigned int func = cmd;
	unsigned long cmd_mask;
	struct nd_cmd_pkg pkg;
	int rc, i, cmd_rc;
	u64 buf_len = 0;
	void *buf;

	if (nvdimm) {
		desc = nd_cmd_dimm_desc(cmd);
		cmd_name = nvdimm_cmd_name(cmd);
		cmd_mask = nvdimm->cmd_mask;
		dimm_name = dev_name(&nvdimm->dev);
	} else {
		desc = nd_cmd_bus_desc(cmd);
		cmd_name = nvdimm_bus_cmd_name(cmd);
		cmd_mask = nd_desc->cmd_mask;
		dimm_name = "bus";
	}

	if (cmd == ND_CMD_CALL) {
		if (copy_from_user(&pkg, p, sizeof(pkg)))
			return -EFAULT;
	}

	if (!desc || (desc->out_num + desc->in_num == 0) ||
			!test_bit(cmd, &cmd_mask))
		return -ENOTTY;

	/* fail write commands (when read-only) */
	if (read_only)
		switch (cmd) {
		case ND_CMD_VENDOR:
		case ND_CMD_SET_CONFIG_DATA:
		case ND_CMD_ARS_START:
		case ND_CMD_CLEAR_ERROR:
		case ND_CMD_CALL:
			dev_dbg(&nvdimm_bus->dev, "'%s' command while read-only.\n",
					nvdimm ? nvdimm_cmd_name(cmd)
					: nvdimm_bus_cmd_name(cmd));
			return -EPERM;
		default:
			break;
		}

	/* process an input envelope */
	for (i = 0; i < desc->in_num; i++) {
		u32 in_size, copy;

		in_size = nd_cmd_in_size(nvdimm, cmd, desc, i, in_env);
		if (in_size == UINT_MAX) {
			dev_err(dev, "%s:%s unknown input size cmd: %s field: %d\n",
					__func__, dimm_name, cmd_name, i);
			return -ENXIO;
		}
		if (in_len < sizeof(in_env))
			copy = min_t(u32, sizeof(in_env) - in_len, in_size);
		else
			copy = 0;
		if (copy && copy_from_user(&in_env[in_len], p + in_len, copy))
			return -EFAULT;
		in_len += in_size;
	}

	if (cmd == ND_CMD_CALL) {
		func = pkg.nd_command;
		dev_dbg(dev, "%s, idx: %llu, in: %u, out: %u, len %llu\n",
				dimm_name, pkg.nd_command,
				in_len, out_len, buf_len);
	}

	/* process an output envelope */
	for (i = 0; i < desc->out_num; i++) {
		u32 out_size = nd_cmd_out_size(nvdimm, cmd, desc, i,
				(u32 *) in_env, (u32 *) out_env, 0);
		u32 copy;

		if (out_size == UINT_MAX) {
			dev_dbg(dev, "%s unknown output size cmd: %s field: %d\n",
					dimm_name, cmd_name, i);
			return -EFAULT;
		}
		if (out_len < sizeof(out_env))
			copy = min_t(u32, sizeof(out_env) - out_len, out_size);
		else
			copy = 0;
		if (copy && copy_from_user(&out_env[out_len],
					p + in_len + out_len, copy))
			return -EFAULT;
		out_len += out_size;
	}

	buf_len = (u64) out_len + (u64) in_len;
	if (buf_len > ND_IOCTL_MAX_BUFLEN) {
		dev_dbg(dev, "%s cmd: %s buf_len: %llu > %d\n", dimm_name,
				cmd_name, buf_len, ND_IOCTL_MAX_BUFLEN);
		return -EINVAL;
	}

	buf = vmalloc(buf_len);
	if (!buf)
		return -ENOMEM;

	if (copy_from_user(buf, p, buf_len)) {
		rc = -EFAULT;
		goto out;
	}

	nvdimm_bus_lock(&nvdimm_bus->dev);
	rc = nd_cmd_clear_to_send(nvdimm_bus, nvdimm, func, buf);
	if (rc)
		goto out_unlock;

	rc = nd_desc->ndctl(nd_desc, nvdimm, cmd, buf, buf_len, &cmd_rc);
	if (rc < 0)
		goto out_unlock;

	if (!nvdimm && cmd == ND_CMD_CLEAR_ERROR && cmd_rc >= 0) {
		struct nd_cmd_clear_error *clear_err = buf;

		nvdimm_account_cleared_poison(nvdimm_bus, clear_err->address,
				clear_err->cleared);
	}
	nvdimm_bus_unlock(&nvdimm_bus->dev);

	if (copy_to_user(p, buf, buf_len))
		rc = -EFAULT;

	vfree(buf);
	return rc;

 out_unlock:
	nvdimm_bus_unlock(&nvdimm_bus->dev);
 out:
	vfree(buf);
	return rc;
}

static long nd_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	long id = (long) file->private_data;
	int rc = -ENXIO, ro;
	struct nvdimm_bus *nvdimm_bus;

	ro = ((file->f_flags & O_ACCMODE) == O_RDONLY);
	mutex_lock(&nvdimm_bus_list_mutex);
	list_for_each_entry(nvdimm_bus, &nvdimm_bus_list, list) {
		if (nvdimm_bus->id == id) {
			rc = __nd_ioctl(nvdimm_bus, NULL, ro, cmd, arg);
			break;
		}
	}
	mutex_unlock(&nvdimm_bus_list_mutex);

	return rc;
}

static int match_dimm(struct device *dev, void *data)
{
	long id = (long) data;

	if (is_nvdimm(dev)) {
		struct nvdimm *nvdimm = to_nvdimm(dev);

		return nvdimm->id == id;
	}

	return 0;
}

static long nvdimm_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	int rc = -ENXIO, ro;
	struct nvdimm_bus *nvdimm_bus;

	ro = ((file->f_flags & O_ACCMODE) == O_RDONLY);
	mutex_lock(&nvdimm_bus_list_mutex);
	list_for_each_entry(nvdimm_bus, &nvdimm_bus_list, list) {
		struct device *dev = device_find_child(&nvdimm_bus->dev,
				file->private_data, match_dimm);
		struct nvdimm *nvdimm;

		if (!dev)
			continue;

		nvdimm = to_nvdimm(dev);
		rc = __nd_ioctl(nvdimm_bus, nvdimm, ro, cmd, arg);
		put_device(dev);
		break;
	}
	mutex_unlock(&nvdimm_bus_list_mutex);

	return rc;
}

static int nd_open(struct inode *inode, struct file *file)
{
	long minor = iminor(inode);

	file->private_data = (void *) minor;
	return 0;
}

static const struct file_operations nvdimm_bus_fops = {
	.owner = THIS_MODULE,
	.open = nd_open,
	.unlocked_ioctl = nd_ioctl,
	.compat_ioctl = nd_ioctl,
	.llseek = noop_llseek,
};

static const struct file_operations nvdimm_fops = {
	.owner = THIS_MODULE,
	.open = nd_open,
	.unlocked_ioctl = nvdimm_ioctl,
	.compat_ioctl = nvdimm_ioctl,
	.llseek = noop_llseek,
};

int __init nvdimm_bus_init(void)
{
	int rc;

	rc = bus_register(&nvdimm_bus_type);
	if (rc)
		return rc;

	rc = register_chrdev(0, "ndctl", &nvdimm_bus_fops);
	if (rc < 0)
		goto err_bus_chrdev;
	nvdimm_bus_major = rc;

	rc = register_chrdev(0, "dimmctl", &nvdimm_fops);
	if (rc < 0)
		goto err_dimm_chrdev;
	nvdimm_major = rc;

	nd_class = class_create(THIS_MODULE, "nd");
	if (IS_ERR(nd_class)) {
		rc = PTR_ERR(nd_class);
		goto err_class;
	}

	rc = driver_register(&nd_bus_driver.drv);
	if (rc)
		goto err_nd_bus;

	return 0;

 err_nd_bus:
	class_destroy(nd_class);
 err_class:
	unregister_chrdev(nvdimm_major, "dimmctl");
 err_dimm_chrdev:
	unregister_chrdev(nvdimm_bus_major, "ndctl");
 err_bus_chrdev:
	bus_unregister(&nvdimm_bus_type);

	return rc;
}

void nvdimm_bus_exit(void)
{
	driver_unregister(&nd_bus_driver.drv);
	class_destroy(nd_class);
	unregister_chrdev(nvdimm_bus_major, "ndctl");
	unregister_chrdev(nvdimm_major, "dimmctl");
	bus_unregister(&nvdimm_bus_type);
	ida_destroy(&nd_ida);
}
