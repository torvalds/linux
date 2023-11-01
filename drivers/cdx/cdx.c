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
#include <linux/kernel.h>
#include <linux/of_device.h>
#include <linux/slab.h>
#include <linux/mm.h>
#include <linux/xarray.h>
#include <linux/cdx/cdx_bus.h>
#include <linux/iommu.h>
#include <linux/dma-map-ops.h>
#include "cdx.h"

/* Default DMA mask for devices on a CDX bus */
#define CDX_DEFAULT_DMA_MASK	(~0ULL)
#define MAX_CDX_CONTROLLERS 16

/* CDX controllers registered with the CDX bus */
static DEFINE_XARRAY_ALLOC(cdx_controllers);

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

	kfree(cdx_dev->driver_override);
	cdx_dev->driver_override = NULL;
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
	    (id->device == CDX_ANY_ID || id->device == dev->device))
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
static int cdx_bus_match(struct device *dev, struct device_driver *drv)
{
	struct cdx_device *cdx_dev = to_cdx_device(dev);
	struct cdx_driver *cdx_drv = to_cdx_driver(drv);
	const struct cdx_device_id *found_id = NULL;
	const struct cdx_device_id *ids;

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
	int error;

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
	u32 input_id = cdx_dev->req_id;
	int ret;

	ret = of_dma_configure_id(dev, dev->parent->of_node, 0, &input_id);
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
	bool val;
	int ret;

	if (kstrtobool(buf, &val) < 0)
		return -EINVAL;

	if (!val)
		return -EINVAL;

	ret = cdx_dev_reset(dev);
	if (ret)
		return ret;

	return count;
}
static DEVICE_ATTR_WO(reset);

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

static struct attribute *cdx_dev_attrs[] = {
	&dev_attr_remove.attr,
	&dev_attr_reset.attr,
	&dev_attr_vendor.attr,
	&dev_attr_device.attr,
	&dev_attr_driver_override.attr,
	NULL,
};
ATTRIBUTE_GROUPS(cdx_dev);

static ssize_t rescan_store(const struct bus_type *bus,
			    const char *buf, size_t count)
{
	struct cdx_controller *cdx;
	unsigned long index;
	bool val;

	if (kstrtobool(buf, &val) < 0)
		return -EINVAL;

	if (!val)
		return -EINVAL;

	/* Unregister all the devices on the bus */
	cdx_unregister_devices(&cdx_bus_type);

	/* Rescan all the devices */
	xa_for_each(&cdx_controllers, index, cdx) {
		int ret;

		ret = cdx->ops->scan(cdx);
		if (ret)
			dev_err(cdx->dev, "cdx bus scanning failed\n");
	}

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

int cdx_device_add(struct cdx_dev_params *dev_params)
{
	struct cdx_controller *cdx = dev_params->cdx;
	struct device *parent = cdx->dev;
	struct cdx_device *cdx_dev;
	int ret;

	cdx_dev = kzalloc(sizeof(*cdx_dev), GFP_KERNEL);
	if (!cdx_dev)
		return -ENOMEM;

	/* Populate resource */
	memcpy(cdx_dev->res, dev_params->res, sizeof(struct resource) *
		dev_params->res_count);
	cdx_dev->res_count = dev_params->res_count;

	/* Populate CDX dev params */
	cdx_dev->req_id = dev_params->req_id;
	cdx_dev->vendor = dev_params->vendor;
	cdx_dev->device = dev_params->device;
	cdx_dev->bus_num = dev_params->bus_num;
	cdx_dev->dev_num = dev_params->dev_num;
	cdx_dev->cdx = dev_params->cdx;
	cdx_dev->dma_mask = CDX_DEFAULT_DMA_MASK;

	/* Initialize generic device */
	device_initialize(&cdx_dev->dev);
	cdx_dev->dev.parent = parent;
	cdx_dev->dev.bus = &cdx_bus_type;
	cdx_dev->dev.dma_mask = &cdx_dev->dma_mask;
	cdx_dev->dev.release = cdx_device_release;

	/* Set Name */
	dev_set_name(&cdx_dev->dev, "cdx-%02x:%02x",
		     ((cdx->id << CDX_CONTROLLER_ID_SHIFT) | (cdx_dev->bus_num & CDX_BUS_NUM_MASK)),
		     cdx_dev->dev_num);

	ret = device_add(&cdx_dev->dev);
	if (ret) {
		dev_err(&cdx_dev->dev,
			"cdx device add failed: %d", ret);
		goto fail;
	}

	return 0;
fail:
	/*
	 * Do not free cdx_dev here as it would be freed in
	 * cdx_device_release() called from put_device().
	 */
	put_device(&cdx_dev->dev);

	return ret;
}
EXPORT_SYMBOL_GPL(cdx_device_add);

int cdx_register_controller(struct cdx_controller *cdx)
{
	int ret;

	ret = xa_alloc(&cdx_controllers, &cdx->id, cdx,
		       XA_LIMIT(0, MAX_CDX_CONTROLLERS - 1), GFP_KERNEL);
	if (ret) {
		dev_err(cdx->dev,
			"No free index available. Maximum controllers already registered\n");
		cdx->id = (u8)MAX_CDX_CONTROLLERS;
		return ret;
	}

	/* Scan all the devices */
	cdx->ops->scan(cdx);

	return 0;
}
EXPORT_SYMBOL_GPL(cdx_register_controller);

void cdx_unregister_controller(struct cdx_controller *cdx)
{
	if (cdx->id >= MAX_CDX_CONTROLLERS)
		return;

	device_for_each_child(cdx->dev, NULL, cdx_unregister_device);
	xa_erase(&cdx_controllers, cdx->id);
}
EXPORT_SYMBOL_GPL(cdx_unregister_controller);

static int __init cdx_bus_init(void)
{
	return bus_register(&cdx_bus_type);
}
postcore_initcall(cdx_bus_init);
