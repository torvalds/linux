// SPDX-License-Identifier: GPL-2.0
/*
 * CDX bus driver.
 *
 * Copyright (C) 2022-2023, Advanced Micro Devices, Inc.
 */

/*
 * Architecture Overview
 * =====================
 * CDX is a Hardware Architecture designed for AMD FPGA devices. It
 * consists of sophisticated mechanism for interaction between FPGA,
 * Firmware and the APUs (Application CPUs).
 *
 * Firmware resides on RPU (Realtime CPUs) which interacts with
 * the FPGA program manager and the APUs. The RPU provides memory-mapped
 * interface (RPU if) which is used to communicate with APUs.
 *
 * The diagram below shows an overview of the CDX architecture:
 *
 *          +--------------------------------------+
 *          |    Application CPUs (APU)            |
 *          |                                      |
 *          |                    CDX device drivers|
 *          |     Linux OS                |        |
 *          |                        CDX bus       |
 *          |                             |        |
 *          |                     CDX controller   |
 *          |                             |        |
 *          +-----------------------------|--------+
 *                                        | (discover, config,
 *                                        |  reset, rescan)
 *                                        |
 *          +------------------------| RPU if |----+
 *          |                             |        |
 *          |                             V        |
 *          |          Realtime CPUs (RPU)         |
 *          |                                      |
 *          +--------------------------------------+
 *                                |
 *          +---------------------|----------------+
 *          |  FPGA               |                |
 *          |      +-----------------------+       |
 *          |      |           |           |       |
 *          | +-------+    +-------+   +-------+   |
 *          | | dev 1 |    | dev 2 |   | dev 3 |   |
 *          | +-------+    +-------+   +-------+   |
 *          +--------------------------------------+
 *
 * The RPU firmware extracts the device information from the loaded FPGA
 * image and implements a mechanism that allows the APU drivers to
 * enumerate such devices (device personality and resource details) via
 * a dedicated communication channel. RPU mediates operations such as
 * discover, reset and rescan of the FPGA devices for the APU. This is
 * done using memory mapped interface provided by the RPU to APU.
 */

#include <linux/init.h>
#include <linux/irqdomain.h>
#include <linux/kernel.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/mm.h>
#include <linux/idr.h>
#include <linux/cdx/cdx_bus.h>
#include <linux/iommu.h>
#include <linux/dma-map-ops.h>
#include <linux/debugfs.h>
#include "cdx.h"

/* Default DMA mask for devices on a CDX bus */
#define CDX_DEFAULT_DMA_MASK	(~0ULL)
#define MAX_CDX_CONTROLLERS 16

/* IDA for CDX controllers registered with the CDX bus */
static DEFINE_IDA(cdx_controller_ida);
/* Lock to protect controller ops */
static DEFINE_MUTEX(cdx_controller_lock);
/* Debugfs dir for cdx bus */
static struct dentry *cdx_debugfs_dir;

static char *compat_node_name = "xlnx,versal-net-cdx";

static void cdx_destroy_res_attr(struct cdx_device *cdx_dev, int num);

/**
 * cdx_dev_reset - Reset a CDX device
 * @dev: CDX device
 *
 * Return: -errno on failure, 0 on success.
 */
int cdx_dev_reset(struct device *dev)
{
	struct cdx_device *cdx_dev = to_cdx_device(dev);
	struct cdx_controller *cdx = cdx_dev->cdx;
	struct cdx_device_config dev_config = {0};
	struct cdx_driver *cdx_drv;
	int ret;

	cdx_drv = to_cdx_driver(dev->driver);
	/* Notify driver that device is being reset */
	if (cdx_drv && cdx_drv->reset_prepare)
		cdx_drv->reset_prepare(cdx_dev);

	dev_config.type = CDX_DEV_RESET_CONF;
	ret = cdx->ops->dev_configure(cdx, cdx_dev->bus_num,
				      cdx_dev->dev_num, &dev_config);
	if (ret)
		dev_err(dev, "cdx device reset failed\n");

	/* Notify driver that device reset is complete */
	if (cdx_drv && cdx_drv->reset_done)
		cdx_drv->reset_done(cdx_dev);

	return ret;
}
EXPORT_SYMBOL_GPL(cdx_dev_reset);

/**
 * reset_cdx_device - Reset a CDX device
 * @dev: CDX device
 * @data: This is always passed as NULL, and is not used in this API,
 *    but is required here as the device_for_each_child() API expects
 *    the passed function to have this as an argument.
 *
 * Return: -errno on failure, 0 on success.
 */
static int reset_cdx_device(struct device *dev, void *data)
{
	return cdx_dev_reset(dev);
}

/**
 * cdx_unregister_device - Unregister a CDX device
 * @dev: CDX device
 * @data: This is always passed as NULL, and is not used in this API,
 *	  but is required here as the bus_for_each_dev() API expects
 *	  the passed function (cdx_unregister_device) to have this
 *	  as an argument.
 *
 * Return: 0 on success.
 */
static int cdx_unregister_device(struct device *dev,
				 void *data)
{
	struct cdx_device *cdx_dev = to_cdx_device(dev);
	struct cdx_controller *cdx = cdx_dev->cdx;

	if (cdx_dev->is_bus) {
		device_for_each_child(dev, NULL, cdx_unregister_device);
		if (cdx_dev->enabled && cdx->ops->bus_disable)
			cdx->ops->bus_disable(cdx, cdx_dev->bus_num);
	} else {
		cdx_destroy_res_attr(cdx_dev, MAX_CDX_DEV_RESOURCES);
		debugfs_remove_recursive(cdx_dev->debugfs_dir);
		kfree(cdx_dev->driver_override);
		cdx_dev->driver_override = NULL;
	}

	/*
	 * Do not free cdx_dev here as it would be freed in
	 * cdx_device_release() called from within put_device().
	 */
	device_del(&cdx_dev->dev);
	put_device(&cdx_dev->dev);

	return 0;
}

static void cdx_unregister_devices(struct bus_type *bus)
{
	/* Reset all the devices attached to cdx bus */
	bus_for_each_dev(bus, NULL, NULL, cdx_unregister_device);
}

/**
 * cdx_match_one_device - Tell if a CDX device structure has a matching
 *			  CDX device id structure
 * @id: single CDX device id structure to match
 * @dev: the CDX device structure to match against
 *
 * Return: matching cdx_device_id structure or NULL if there is no match.
 */
static inline const struct cdx_device_id *
cdx_match_one_device(const struct cdx_device_id *id,
		     const struct cdx_device *dev)
{
	/* Use vendor ID and device ID for matching */
	if ((id->vendor == CDX_ANY_ID || id->vendor == dev->vendor) &&
	    (id->device == CDX_ANY_ID || id->device == dev->device) &&
	    (id->subvendor == CDX_ANY_ID || id->subvendor == dev->subsystem_vendor) &&
	    (id->subdevice == CDX_ANY_ID || id->subdevice == dev->subsystem_device) &&
	    !((id->class ^ dev->class) & id->class_mask))
		return id;
	return NULL;
}

/**
 * cdx_match_id - See if a CDX device matches a given cdx_id table
 * @ids: array of CDX device ID structures to search in
 * @dev: the CDX device structure to match against.
 *
 * Used by a driver to check whether a CDX device is in its list of
 * supported devices. Returns the matching cdx_device_id structure or
 * NULL if there is no match.
 *
 * Return: matching cdx_device_id structure or NULL if there is no match.
 */
static inline const struct cdx_device_id *
cdx_match_id(const struct cdx_device_id *ids, struct cdx_device *dev)
{
	if (ids) {
		while (ids->vendor || ids->device) {
			if (cdx_match_one_device(ids, dev))
				return ids;
			ids++;
		}
	}
	return NULL;
}

int cdx_set_master(struct cdx_device *cdx_dev)
{
	struct cdx_controller *cdx = cdx_dev->cdx;
	struct cdx_device_config dev_config;
	int ret = -EOPNOTSUPP;

	dev_config.type = CDX_DEV_BUS_MASTER_CONF;
	dev_config.bus_master_enable = true;
	if (cdx->ops->dev_configure)
		ret = cdx->ops->dev_configure(cdx, cdx_dev->bus_num,
					      cdx_dev->dev_num, &dev_config);

	return ret;
}
EXPORT_SYMBOL_GPL(cdx_set_master);

int cdx_clear_master(struct cdx_device *cdx_dev)
{
	struct cdx_controller *cdx = cdx_dev->cdx;
	struct cdx_device_config dev_config;
	int ret = -EOPNOTSUPP;

	dev_config.type = CDX_DEV_BUS_MASTER_CONF;
	dev_config.bus_master_enable = false;
	if (cdx->ops->dev_configure)
		ret = cdx->ops->dev_configure(cdx, cdx_dev->bus_num,
					      cdx_dev->dev_num, &dev_config);

	return ret;
}
EXPORT_SYMBOL_GPL(cdx_clear_master);

/**
 * cdx_bus_match - device to driver matching callback
 * @dev: the cdx device to match against
 * @drv: the device driver to search for matching cdx device
 * structures
 *
 * Return: true on success, false otherwise.
 */
static int cdx_bus_match(struct device *dev, const struct device_driver *drv)
{
	struct cdx_device *cdx_dev = to_cdx_device(dev);
	const struct cdx_driver *cdx_drv = to_cdx_driver(drv);
	const struct cdx_device_id *found_id = NULL;
	const struct cdx_device_id *ids;

	if (cdx_dev->is_bus)
		return false;

	ids = cdx_drv->match_id_table;

	/* When driver_override is set, only bind to the matching driver */
	if (cdx_dev->driver_override && strcmp(cdx_dev->driver_override, drv->name))
		return false;

	found_id = cdx_match_id(ids, cdx_dev);
	if (!found_id)
		return false;

	do {
		/*
		 * In case override_only was set, enforce driver_override
		 * matching.
		 */
		if (!found_id->override_only)
			return true;
		if (cdx_dev->driver_override)
			return true;

		ids = found_id + 1;
		found_id = cdx_match_id(ids, cdx_dev);
	} while (found_id);

	return false;
}

static int cdx_probe(struct device *dev)
{
	struct cdx_driver *cdx_drv = to_cdx_driver(dev->driver);
	struct cdx_device *cdx_dev = to_cdx_device(dev);
	struct cdx_controller *cdx = cdx_dev->cdx;
	int error;

	/*
	 * Setup MSI device data so that generic MSI alloc/free can
	 * be used by the device driver.
	 */
	if (cdx->msi_domain) {
		error = msi_setup_device_data(&cdx_dev->dev);
		if (error)
			return error;
	}

	error = cdx_drv->probe(cdx_dev);
	if (error) {
		dev_err_probe(dev, error, "%s failed\n", __func__);
		return error;
	}

	return 0;
}

static void cdx_remove(struct device *dev)
{
	struct cdx_driver *cdx_drv = to_cdx_driver(dev->driver);
	struct cdx_device *cdx_dev = to_cdx_device(dev);

	if (cdx_drv && cdx_drv->remove)
		cdx_drv->remove(cdx_dev);
}

static void cdx_shutdown(struct device *dev)
{
	struct cdx_driver *cdx_drv = to_cdx_driver(dev->driver);
	struct cdx_device *cdx_dev = to_cdx_device(dev);

	if (cdx_drv && cdx_drv->shutdown)
		cdx_drv->shutdown(cdx_dev);
}

static int cdx_dma_configure(struct device *dev)
{
	struct cdx_driver *cdx_drv = to_cdx_driver(dev->driver);
	struct cdx_device *cdx_dev = to_cdx_device(dev);
	struct cdx_controller *cdx = cdx_dev->cdx;
	u32 input_id = cdx_dev->req_id;
	int ret;

	ret = of_dma_configure_id(dev, cdx->dev->of_node, 0, &input_id);
	if (ret && ret != -EPROBE_DEFER) {
		dev_err(dev, "of_dma_configure_id() failed\n");
		return ret;
	}

	if (!ret && !cdx_drv->driver_managed_dma) {
		ret = iommu_device_use_default_domain(dev);
		if (ret)
			arch_teardown_dma_ops(dev);
	}

	return 0;
}

static void cdx_dma_cleanup(struct device *dev)
{
	struct cdx_driver *cdx_drv = to_cdx_driver(dev->driver);

	if (!cdx_drv->driver_managed_dma)
		iommu_device_unuse_default_domain(dev);
}

/* show configuration fields */
#define cdx_config_attr(field, format_string)	\
static ssize_t	\
field##_show(struct device *dev, struct device_attribute *attr, char *buf)	\
{	\
	struct cdx_device *cdx_dev = to_cdx_device(dev);	\
	return sysfs_emit(buf, format_string, cdx_dev->field);	\
}	\
static DEVICE_ATTR_RO(field)

cdx_config_attr(vendor, "0x%04x\n");
cdx_config_attr(device, "0x%04x\n");
cdx_config_attr(subsystem_vendor, "0x%04x\n");
cdx_config_attr(subsystem_device, "0x%04x\n");
cdx_config_attr(revision, "0x%02x\n");
cdx_config_attr(class, "0x%06x\n");

static ssize_t remove_store(struct device *dev,
			    struct device_attribute *attr,
			    const char *buf, size_t count)
{
	bool val;

	if (kstrtobool(buf, &val) < 0)
		return -EINVAL;

	if (!val)
		return -EINVAL;

	if (device_remove_file_self(dev, attr)) {
		int ret;

		ret = cdx_unregister_device(dev, NULL);
		if (ret)
			return ret;
	}

	return count;
}
static DEVICE_ATTR_WO(remove);

static ssize_t reset_store(struct device *dev, struct device_attribute *attr,
			   const char *buf, size_t count)
{
	struct cdx_device *cdx_dev = to_cdx_device(dev);
	bool val;
	int ret;

	if (kstrtobool(buf, &val) < 0)
		return -EINVAL;

	if (!val)
		return -EINVAL;

	if (cdx_dev->is_bus)
		/* Reset all the devices attached to cdx bus */
		ret = device_for_each_child(dev, NULL, reset_cdx_device);
	else
		ret = cdx_dev_reset(dev);

	return ret < 0 ? ret : count;
}
static DEVICE_ATTR_WO(reset);

static ssize_t modalias_show(struct device *dev, struct device_attribute *attr,
			     char *buf)
{
	struct cdx_device *cdx_dev = to_cdx_device(dev);

	return sprintf(buf, "cdx:v%04Xd%04Xsv%04Xsd%04Xc%06X\n", cdx_dev->vendor,
			cdx_dev->device, cdx_dev->subsystem_vendor, cdx_dev->subsystem_device,
			cdx_dev->class);
}
static DEVICE_ATTR_RO(modalias);

static ssize_t driver_override_store(struct device *dev,
				     struct device_attribute *attr,
				     const char *buf, size_t count)
{
	struct cdx_device *cdx_dev = to_cdx_device(dev);
	int ret;

	if (WARN_ON(dev->bus != &cdx_bus_type))
		return -EINVAL;

	ret = driver_set_override(dev, &cdx_dev->driver_override, buf, count);
	if (ret)
		return ret;

	return count;
}

static ssize_t driver_override_show(struct device *dev,
				    struct device_attribute *attr, char *buf)
{
	struct cdx_device *cdx_dev = to_cdx_device(dev);

	return sysfs_emit(buf, "%s\n", cdx_dev->driver_override);
}
static DEVICE_ATTR_RW(driver_override);

static ssize_t enable_store(struct device *dev, struct device_attribute *attr,
			    const char *buf, size_t count)
{
	struct cdx_device *cdx_dev = to_cdx_device(dev);
	struct cdx_controller *cdx = cdx_dev->cdx;
	bool enable;
	int ret;

	if (kstrtobool(buf, &enable) < 0)
		return -EINVAL;

	if (enable == cdx_dev->enabled)
		return count;

	if (enable && cdx->ops->bus_enable)
		ret = cdx->ops->bus_enable(cdx, cdx_dev->bus_num);
	else if (!enable && cdx->ops->bus_disable)
		ret = cdx->ops->bus_disable(cdx, cdx_dev->bus_num);
	else
		ret = -EOPNOTSUPP;

	if (!ret)
		cdx_dev->enabled = enable;

	return ret < 0 ? ret : count;
}

static ssize_t enable_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct cdx_device *cdx_dev = to_cdx_device(dev);

	return sysfs_emit(buf, "%u\n", cdx_dev->enabled);
}
static DEVICE_ATTR_RW(enable);

static umode_t cdx_dev_attrs_are_visible(struct kobject *kobj, struct attribute *a, int n)
{
	struct device *dev = kobj_to_dev(kobj);
	struct cdx_device *cdx_dev;

	cdx_dev = to_cdx_device(dev);
	if (!cdx_dev->is_bus)
		return a->mode;

	return 0;
}

static umode_t cdx_bus_attrs_are_visible(struct kobject *kobj, struct attribute *a, int n)
{
	struct device *dev = kobj_to_dev(kobj);
	struct cdx_device *cdx_dev;

	cdx_dev = to_cdx_device(dev);
	if (cdx_dev->is_bus)
		return a->mode;

	return 0;
}

static struct attribute *cdx_dev_attrs[] = {
	&dev_attr_remove.attr,
	&dev_attr_reset.attr,
	&dev_attr_vendor.attr,
	&dev_attr_device.attr,
	&dev_attr_subsystem_vendor.attr,
	&dev_attr_subsystem_device.attr,
	&dev_attr_class.attr,
	&dev_attr_revision.attr,
	&dev_attr_modalias.attr,
	&dev_attr_driver_override.attr,
	NULL,
};

static const struct attribute_group cdx_dev_group = {
	.attrs = cdx_dev_attrs,
	.is_visible = cdx_dev_attrs_are_visible,
};

static struct attribute *cdx_bus_dev_attrs[] = {
	&dev_attr_enable.attr,
	&dev_attr_reset.attr,
	NULL,
};

static const struct attribute_group cdx_bus_dev_group = {
	.attrs = cdx_bus_dev_attrs,
	.is_visible = cdx_bus_attrs_are_visible,
};

static const struct attribute_group *cdx_dev_groups[] = {
	&cdx_dev_group,
	&cdx_bus_dev_group,
	NULL,
};

static int cdx_debug_resource_show(struct seq_file *s, void *data)
{
	struct cdx_device *cdx_dev = s->private;
	int i;

	for (i = 0; i < MAX_CDX_DEV_RESOURCES; i++) {
		struct resource *res =  &cdx_dev->res[i];

		seq_printf(s, "%pr\n", res);
	}

	return 0;
}
DEFINE_SHOW_ATTRIBUTE(cdx_debug_resource);

static void cdx_device_debugfs_init(struct cdx_device *cdx_dev)
{
	cdx_dev->debugfs_dir = debugfs_create_dir(dev_name(&cdx_dev->dev), cdx_debugfs_dir);
	if (IS_ERR(cdx_dev->debugfs_dir))
		return;

	debugfs_create_file("resource", 0444, cdx_dev->debugfs_dir, cdx_dev,
			    &cdx_debug_resource_fops);
}

static ssize_t rescan_store(const struct bus_type *bus,
			    const char *buf, size_t count)
{
	struct cdx_controller *cdx;
	struct platform_device *pd;
	struct device_node *np;
	bool val;

	if (kstrtobool(buf, &val) < 0)
		return -EINVAL;

	if (!val)
		return -EINVAL;

	mutex_lock(&cdx_controller_lock);

	/* Unregister all the devices on the bus */
	cdx_unregister_devices(&cdx_bus_type);

	/* Rescan all the devices */
	for_each_compatible_node(np, NULL, compat_node_name) {
		pd = of_find_device_by_node(np);
		if (!pd) {
			of_node_put(np);
			count = -EINVAL;
			goto unlock;
		}

		cdx = platform_get_drvdata(pd);
		if (cdx && cdx->controller_registered && cdx->ops->scan)
			cdx->ops->scan(cdx);

		put_device(&pd->dev);
	}

unlock:
	mutex_unlock(&cdx_controller_lock);

	return count;
}
static BUS_ATTR_WO(rescan);

static struct attribute *cdx_bus_attrs[] = {
	&bus_attr_rescan.attr,
	NULL,
};
ATTRIBUTE_GROUPS(cdx_bus);

struct bus_type cdx_bus_type = {
	.name		= "cdx",
	.match		= cdx_bus_match,
	.probe		= cdx_probe,
	.remove		= cdx_remove,
	.shutdown	= cdx_shutdown,
	.dma_configure	= cdx_dma_configure,
	.dma_cleanup	= cdx_dma_cleanup,
	.bus_groups	= cdx_bus_groups,
	.dev_groups	= cdx_dev_groups,
};
EXPORT_SYMBOL_GPL(cdx_bus_type);

int __cdx_driver_register(struct cdx_driver *cdx_driver,
			  struct module *owner)
{
	int error;

	cdx_driver->driver.owner = owner;
	cdx_driver->driver.bus = &cdx_bus_type;

	error = driver_register(&cdx_driver->driver);
	if (error) {
		pr_err("driver_register() failed for %s: %d\n",
		       cdx_driver->driver.name, error);
		return error;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(__cdx_driver_register);

void cdx_driver_unregister(struct cdx_driver *cdx_driver)
{
	driver_unregister(&cdx_driver->driver);
}
EXPORT_SYMBOL_GPL(cdx_driver_unregister);

static void cdx_device_release(struct device *dev)
{
	struct cdx_device *cdx_dev = to_cdx_device(dev);

	kfree(cdx_dev);
}

static const struct vm_operations_struct cdx_phys_vm_ops = {
#ifdef CONFIG_HAVE_IOREMAP_PROT
	.access = generic_access_phys,
#endif
};

/**
 * cdx_mmap_resource - map a CDX resource into user memory space
 * @fp: File pointer. Not used in this function, but required where
 *      this API is registered as a callback.
 * @kobj: kobject for mapping
 * @attr: struct bin_attribute for the file being mapped
 * @vma: struct vm_area_struct passed into the mmap
 *
 * Use the regular CDX mapping routines to map a CDX resource into userspace.
 *
 * Return: true on success, false otherwise.
 */
static int cdx_mmap_resource(struct file *fp, struct kobject *kobj,
			     struct bin_attribute *attr,
			     struct vm_area_struct *vma)
{
	struct cdx_device *cdx_dev = to_cdx_device(kobj_to_dev(kobj));
	int num = (unsigned long)attr->private;
	struct resource *res;
	unsigned long size;

	res = &cdx_dev->res[num];
	if (iomem_is_exclusive(res->start))
		return -EINVAL;

	/* Make sure the caller is mapping a valid resource for this device */
	size = ((cdx_resource_len(cdx_dev, num) - 1) >> PAGE_SHIFT) + 1;
	if (vma->vm_pgoff + vma_pages(vma) > size)
		return -EINVAL;

	/*
	 * Map memory region and vm->vm_pgoff is expected to be an
	 * offset within that region.
	 */
	vma->vm_page_prot = pgprot_device(vma->vm_page_prot);
	vma->vm_pgoff += (cdx_resource_start(cdx_dev, num) >> PAGE_SHIFT);
	vma->vm_ops = &cdx_phys_vm_ops;
	return io_remap_pfn_range(vma, vma->vm_start, vma->vm_pgoff,
				  vma->vm_end - vma->vm_start,
				  vma->vm_page_prot);
}

static void cdx_destroy_res_attr(struct cdx_device *cdx_dev, int num)
{
	int i;

	/* removing the bin attributes */
	for (i = 0; i < num; i++) {
		struct bin_attribute *res_attr;

		res_attr = cdx_dev->res_attr[i];
		if (res_attr) {
			sysfs_remove_bin_file(&cdx_dev->dev.kobj, res_attr);
			kfree(res_attr);
		}
	}
}

#define CDX_RES_ATTR_NAME_LEN	10
static int cdx_create_res_attr(struct cdx_device *cdx_dev, int num)
{
	struct bin_attribute *res_attr;
	char *res_attr_name;
	int ret;

	res_attr = kzalloc(sizeof(*res_attr) + CDX_RES_ATTR_NAME_LEN, GFP_ATOMIC);
	if (!res_attr)
		return -ENOMEM;

	res_attr_name = (char *)(res_attr + 1);

	sysfs_bin_attr_init(res_attr);

	cdx_dev->res_attr[num] = res_attr;
	sprintf(res_attr_name, "resource%d", num);

	res_attr->mmap = cdx_mmap_resource;
	res_attr->attr.name = res_attr_name;
	res_attr->attr.mode = 0600;
	res_attr->size = cdx_resource_len(cdx_dev, num);
	res_attr->private = (void *)(unsigned long)num;
	ret = sysfs_create_bin_file(&cdx_dev->dev.kobj, res_attr);
	if (ret)
		kfree(res_attr);

	return ret;
}

int cdx_device_add(struct cdx_dev_params *dev_params)
{
	struct cdx_controller *cdx = dev_params->cdx;
	struct cdx_device *cdx_dev;
	int ret, i;

	cdx_dev = kzalloc(sizeof(*cdx_dev), GFP_KERNEL);
	if (!cdx_dev)
		return -ENOMEM;

	/* Populate resource */
	memcpy(cdx_dev->res, dev_params->res, sizeof(struct resource) *
		dev_params->res_count);
	cdx_dev->res_count = dev_params->res_count;

	/* Populate CDX dev params */
	cdx_dev->req_id = dev_params->req_id;
	cdx_dev->msi_dev_id = dev_params->msi_dev_id;
	cdx_dev->vendor = dev_params->vendor;
	cdx_dev->device = dev_params->device;
	cdx_dev->subsystem_vendor = dev_params->subsys_vendor;
	cdx_dev->subsystem_device = dev_params->subsys_device;
	cdx_dev->class = dev_params->class;
	cdx_dev->revision = dev_params->revision;
	cdx_dev->bus_num = dev_params->bus_num;
	cdx_dev->dev_num = dev_params->dev_num;
	cdx_dev->cdx = dev_params->cdx;
	cdx_dev->dma_mask = CDX_DEFAULT_DMA_MASK;

	/* Initialize generic device */
	device_initialize(&cdx_dev->dev);
	cdx_dev->dev.parent = dev_params->parent;
	cdx_dev->dev.bus = &cdx_bus_type;
	cdx_dev->dev.dma_mask = &cdx_dev->dma_mask;
	cdx_dev->dev.release = cdx_device_release;
	cdx_dev->msi_write_pending = false;
	mutex_init(&cdx_dev->irqchip_lock);

	/* Set Name */
	dev_set_name(&cdx_dev->dev, "cdx-%02x:%02x",
		     ((cdx->id << CDX_CONTROLLER_ID_SHIFT) | (cdx_dev->bus_num & CDX_BUS_NUM_MASK)),
		     cdx_dev->dev_num);

	if (cdx->msi_domain) {
		cdx_dev->num_msi = dev_params->num_msi;
		dev_set_msi_domain(&cdx_dev->dev, cdx->msi_domain);
	}

	ret = device_add(&cdx_dev->dev);
	if (ret) {
		dev_err(&cdx_dev->dev,
			"cdx device add failed: %d", ret);
		goto fail;
	}

	/* Create resource<N> attributes */
	for (i = 0; i < MAX_CDX_DEV_RESOURCES; i++) {
		if (cdx_resource_flags(cdx_dev, i) & IORESOURCE_MEM) {
			/* skip empty resources */
			if (!cdx_resource_len(cdx_dev, i))
				continue;

			ret = cdx_create_res_attr(cdx_dev, i);
			if (ret != 0) {
				dev_err(&cdx_dev->dev,
					"cdx device resource<%d> file creation failed: %d", i, ret);
				goto resource_create_fail;
			}
		}
	}

	cdx_device_debugfs_init(cdx_dev);

	return 0;
resource_create_fail:
	cdx_destroy_res_attr(cdx_dev, i);
	device_del(&cdx_dev->dev);
fail:
	/*
	 * Do not free cdx_dev here as it would be freed in
	 * cdx_device_release() called from put_device().
	 */
	put_device(&cdx_dev->dev);

	return ret;
}
EXPORT_SYMBOL_NS_GPL(cdx_device_add, CDX_BUS_CONTROLLER);

struct device *cdx_bus_add(struct cdx_controller *cdx, u8 bus_num)
{
	struct cdx_device *cdx_dev;
	int ret;

	cdx_dev = kzalloc(sizeof(*cdx_dev), GFP_KERNEL);
	if (!cdx_dev)
		return NULL;

	device_initialize(&cdx_dev->dev);
	cdx_dev->cdx = cdx;

	cdx_dev->dev.parent = cdx->dev;
	cdx_dev->dev.bus = &cdx_bus_type;
	cdx_dev->dev.release = cdx_device_release;
	cdx_dev->is_bus = true;
	cdx_dev->bus_num = bus_num;

	dev_set_name(&cdx_dev->dev, "cdx-%02x",
		     ((cdx->id << CDX_CONTROLLER_ID_SHIFT) | (bus_num & CDX_BUS_NUM_MASK)));

	ret = device_add(&cdx_dev->dev);
	if (ret) {
		dev_err(&cdx_dev->dev, "cdx bus device add failed: %d\n", ret);
		goto device_add_fail;
	}

	if (cdx->ops->bus_enable) {
		ret = cdx->ops->bus_enable(cdx, bus_num);
		if (ret && ret != -EALREADY) {
			dev_err(cdx->dev, "cdx bus enable failed: %d\n", ret);
			goto bus_enable_fail;
		}
	}

	cdx_dev->enabled = true;
	return &cdx_dev->dev;

bus_enable_fail:
	device_del(&cdx_dev->dev);
device_add_fail:
	put_device(&cdx_dev->dev);

	return NULL;
}
EXPORT_SYMBOL_NS_GPL(cdx_bus_add, CDX_BUS_CONTROLLER);

int cdx_register_controller(struct cdx_controller *cdx)
{
	int ret;

	ret = ida_alloc_range(&cdx_controller_ida, 0,  MAX_CDX_CONTROLLERS - 1, GFP_KERNEL);
	if (ret < 0) {
		dev_err(cdx->dev,
			"No free index available. Maximum controllers already registered\n");
		cdx->id = (u8)MAX_CDX_CONTROLLERS;
		return ret;
	}

	mutex_lock(&cdx_controller_lock);
	cdx->id = ret;

	/* Scan all the devices */
	if (cdx->ops->scan)
		cdx->ops->scan(cdx);
	cdx->controller_registered = true;
	mutex_unlock(&cdx_controller_lock);

	return 0;
}
EXPORT_SYMBOL_NS_GPL(cdx_register_controller, CDX_BUS_CONTROLLER);

void cdx_unregister_controller(struct cdx_controller *cdx)
{
	if (cdx->id >= MAX_CDX_CONTROLLERS)
		return;

	mutex_lock(&cdx_controller_lock);

	cdx->controller_registered = false;
	device_for_each_child(cdx->dev, NULL, cdx_unregister_device);
	ida_free(&cdx_controller_ida, cdx->id);

	mutex_unlock(&cdx_controller_lock);
}
EXPORT_SYMBOL_NS_GPL(cdx_unregister_controller, CDX_BUS_CONTROLLER);

static int __init cdx_bus_init(void)
{
	int ret;

	ret = bus_register(&cdx_bus_type);
	if (!ret)
		cdx_debugfs_dir = debugfs_create_dir(cdx_bus_type.name, NULL);

	return ret;
}
postcore_initcall(cdx_bus_init);
