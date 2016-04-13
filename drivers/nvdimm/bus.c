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

int nvdimm_major;
static int nvdimm_bus_major;
static struct class *nd_class;

static int to_nd_device_type(struct device *dev)
{
	if (is_nvdimm(dev))
		return ND_DEVICE_DIMM;
	else if (is_nd_pmem(dev))
		return ND_DEVICE_REGION_PMEM;
	else if (is_nd_blk(dev))
		return ND_DEVICE_REGION_BLK;
	else if (is_nd_pmem(dev->parent) || is_nd_blk(dev->parent))
		return nd_region_to_nstype(to_nd_region(dev->parent));

	return 0;
}

static int nvdimm_bus_uevent(struct device *dev, struct kobj_uevent_env *env)
{
	/*
	 * Ensure that region devices always have their numa node set as
	 * early as possible.
	 */
	if (is_nd_pmem(dev) || is_nd_blk(dev))
		set_dev_node(dev, to_nd_region(dev)->numa_node);
	return add_uevent_var(env, "MODALIAS=" ND_DEVICE_MODALIAS_FMT,
			to_nd_device_type(dev));
}

static int nvdimm_bus_match(struct device *dev, struct device_driver *drv)
{
	struct nd_device_driver *nd_drv = to_nd_device_driver(drv);

	return test_bit(to_nd_device_type(dev), &nd_drv->type);
}

static struct module *to_bus_provider(struct device *dev)
{
	/* pin bus providers while regions are enabled */
	if (is_nd_pmem(dev) || is_nd_blk(dev)) {
		struct nvdimm_bus *nvdimm_bus = walk_to_nvdimm_bus(dev);

		return nvdimm_bus->module;
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

	nvdimm_bus_probe_start(nvdimm_bus);
	rc = nd_drv->probe(dev);
	if (rc == 0)
		nd_region_probe_success(nvdimm_bus, dev);
	else
		nd_region_disable(nvdimm_bus, dev);
	nvdimm_bus_probe_end(nvdimm_bus);

	dev_dbg(&nvdimm_bus->dev, "%s.probe(%s) = %d\n", dev->driver->name,
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
	int rc;

	rc = nd_drv->remove(dev);
	nd_region_disable(nvdimm_bus, dev);

	dev_dbg(&nvdimm_bus->dev, "%s.remove(%s) = %d\n", dev->driver->name,
			dev_name(dev), rc);
	module_put(provider);
	return rc;
}

static struct bus_type nvdimm_bus_type = {
	.name = "nd",
	.uevent = nvdimm_bus_uevent,
	.match = nvdimm_bus_match,
	.probe = nvdimm_bus_probe,
	.remove = nvdimm_bus_remove,
};

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
	dev->bus = &nvdimm_bus_type;
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

	if (!nd_drv->probe || !nd_drv->remove) {
		pr_debug("->probe() and ->remove() must be specified\n");
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
	struct device *dev = disk->driverfs_dev;
	struct nd_region *nd_region = to_nd_region(dev->parent);
	const char *pol = nd_region->ro ? "only" : "write";

	if (nd_region->ro == get_disk_ro(disk))
		return 0;

	dev_info(dev, "%s read-%s, marking %s read-%s\n",
			dev_name(&nd_region->dev), pol, disk->disk_name, pol);
	set_disk_ro(disk, nd_region->ro);

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

	if (IS_ERR(dev)) {
		dev_dbg(&nvdimm_bus->dev, "failed to register ndctl%d: %ld\n",
				nvdimm_bus->id, PTR_ERR(dev));
		return PTR_ERR(dev);
	}
	return 0;
}

void nvdimm_bus_destroy_ndctl(struct nvdimm_bus *nvdimm_bus)
{
	device_destroy(nd_class, MKDEV(nvdimm_bus_major, nvdimm_bus->id));
}

static const struct nd_cmd_desc __nd_cmd_dimm_descs[] = {
	[ND_CMD_IMPLEMENTED] = { },
	[ND_CMD_SMART] = {
		.out_num = 2,
		.out_sizes = { 4, 8, },
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
		.out_num = 2,
		.out_sizes = { 4, 4, },
	},
	[ND_CMD_ARS_START] = {
		.in_num = 4,
		.in_sizes = { 8, 8, 2, 6, },
		.out_num = 1,
		.out_sizes = { 4, },
	},
	[ND_CMD_ARS_STATUS] = {
		.out_num = 2,
		.out_sizes = { 4, UINT_MAX, },
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
	}

	return UINT_MAX;
}
EXPORT_SYMBOL_GPL(nd_cmd_in_size);

u32 nd_cmd_out_size(struct nvdimm *nvdimm, int cmd,
		const struct nd_cmd_desc *desc, int idx, const u32 *in_field,
		const u32 *out_field)
{
	if (idx >= desc->out_num)
		return UINT_MAX;

	if (desc->out_sizes[idx] < UINT_MAX)
		return desc->out_sizes[idx];

	if (nvdimm && cmd == ND_CMD_GET_CONFIG_DATA && idx == 1)
		return in_field[1];
	else if (nvdimm && cmd == ND_CMD_VENDOR && idx == 2)
		return out_field[1];
	else if (!nvdimm && cmd == ND_CMD_ARS_STATUS && idx == 1)
		return ND_CMD_ARS_STATUS_MAX;

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

/* set_config requires an idle interleave set */
static int nd_cmd_clear_to_send(struct nvdimm *nvdimm, unsigned int cmd)
{
	struct nvdimm_bus *nvdimm_bus;

	if (!nvdimm || cmd != ND_CMD_SET_CONFIG_DATA)
		return 0;

	nvdimm_bus = walk_to_nvdimm_bus(&nvdimm->dev);
	wait_nvdimm_bus_probe_idle(&nvdimm_bus->dev);

	if (atomic_read(&nvdimm->busy))
		return -EBUSY;
	return 0;
}

static int __nd_ioctl(struct nvdimm_bus *nvdimm_bus, struct nvdimm *nvdimm,
		int read_only, unsigned int ioctl_cmd, unsigned long arg)
{
	struct nvdimm_bus_descriptor *nd_desc = nvdimm_bus->nd_desc;
	size_t buf_len = 0, in_len = 0, out_len = 0;
	static char out_env[ND_CMD_MAX_ENVELOPE];
	static char in_env[ND_CMD_MAX_ENVELOPE];
	const struct nd_cmd_desc *desc = NULL;
	unsigned int cmd = _IOC_NR(ioctl_cmd);
	void __user *p = (void __user *) arg;
	struct device *dev = &nvdimm_bus->dev;
	const char *cmd_name, *dimm_name;
	unsigned long dsm_mask;
	void *buf;
	int rc, i;

	if (nvdimm) {
		desc = nd_cmd_dimm_desc(cmd);
		cmd_name = nvdimm_cmd_name(cmd);
		dsm_mask = nvdimm->dsm_mask ? *(nvdimm->dsm_mask) : 0;
		dimm_name = dev_name(&nvdimm->dev);
	} else {
		desc = nd_cmd_bus_desc(cmd);
		cmd_name = nvdimm_bus_cmd_name(cmd);
		dsm_mask = nd_desc->dsm_mask;
		dimm_name = "bus";
	}

	if (!desc || (desc->out_num + desc->in_num == 0) ||
			!test_bit(cmd, &dsm_mask))
		return -ENOTTY;

	/* fail write commands (when read-only) */
	if (read_only)
		switch (cmd) {
		case ND_CMD_VENDOR:
		case ND_CMD_SET_CONFIG_DATA:
		case ND_CMD_ARS_START:
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

	/* process an output envelope */
	for (i = 0; i < desc->out_num; i++) {
		u32 out_size = nd_cmd_out_size(nvdimm, cmd, desc, i,
				(u32 *) in_env, (u32 *) out_env);
		u32 copy;

		if (out_size == UINT_MAX) {
			dev_dbg(dev, "%s:%s unknown output size cmd: %s field: %d\n",
					__func__, dimm_name, cmd_name, i);
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

	buf_len = out_len + in_len;
	if (buf_len > ND_IOCTL_MAX_BUFLEN) {
		dev_dbg(dev, "%s:%s cmd: %s buf_len: %zu > %d\n", __func__,
				dimm_name, cmd_name, buf_len,
				ND_IOCTL_MAX_BUFLEN);
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
	rc = nd_cmd_clear_to_send(nvdimm, cmd);
	if (rc)
		goto out_unlock;

	rc = nd_desc->ndctl(nd_desc, nvdimm, cmd, buf, buf_len);
	if (rc < 0)
		goto out_unlock;
	if (copy_to_user(p, buf, buf_len))
		rc = -EFAULT;
 out_unlock:
	nvdimm_bus_unlock(&nvdimm_bus->dev);
 out:
	vfree(buf);
	return rc;
}

static long nd_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	long id = (long) file->private_data;
	int rc = -ENXIO, read_only;
	struct nvdimm_bus *nvdimm_bus;

	read_only = (O_RDWR != (file->f_flags & O_ACCMODE));
	mutex_lock(&nvdimm_bus_list_mutex);
	list_for_each_entry(nvdimm_bus, &nvdimm_bus_list, list) {
		if (nvdimm_bus->id == id) {
			rc = __nd_ioctl(nvdimm_bus, NULL, read_only, cmd, arg);
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
	int rc = -ENXIO, read_only;
	struct nvdimm_bus *nvdimm_bus;

	read_only = (O_RDWR != (file->f_flags & O_ACCMODE));
	mutex_lock(&nvdimm_bus_list_mutex);
	list_for_each_entry(nvdimm_bus, &nvdimm_bus_list, list) {
		struct device *dev = device_find_child(&nvdimm_bus->dev,
				file->private_data, match_dimm);
		struct nvdimm *nvdimm;

		if (!dev)
			continue;

		nvdimm = to_nvdimm(dev);
		rc = __nd_ioctl(nvdimm_bus, nvdimm, read_only, cmd, arg);
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

	return 0;

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
	class_destroy(nd_class);
	unregister_chrdev(nvdimm_bus_major, "ndctl");
	unregister_chrdev(nvdimm_major, "dimmctl");
	bus_unregister(&nvdimm_bus_type);
}
