/*
 * ACPI helpers for DMA request / controller
 *
 * Based on of-dma.c
 *
 * Copyright (C) 2013, Intel Corporation
 * Author: Andy Shevchenko <andriy.shevchenko@linux.intel.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/device.h>
#include <linux/module.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/acpi.h>
#include <linux/acpi_dma.h>

static LIST_HEAD(acpi_dma_list);
static DEFINE_MUTEX(acpi_dma_lock);

/**
 * acpi_dma_controller_register - Register a DMA controller to ACPI DMA helpers
 * @dev:		struct device of DMA controller
 * @acpi_dma_xlate:	translation function which converts a dma specifier
 *			into a dma_chan structure
 * @data		pointer to controller specific data to be used by
 *			translation function
 *
 * Returns 0 on success or appropriate errno value on error.
 *
 * Allocated memory should be freed with appropriate acpi_dma_controller_free()
 * call.
 */
int acpi_dma_controller_register(struct device *dev,
		struct dma_chan *(*acpi_dma_xlate)
		(struct acpi_dma_spec *, struct acpi_dma *),
		void *data)
{
	struct acpi_device *adev;
	struct acpi_dma	*adma;

	if (!dev || !acpi_dma_xlate)
		return -EINVAL;

	/* Check if the device was enumerated by ACPI */
	if (!ACPI_HANDLE(dev))
		return -EINVAL;

	if (acpi_bus_get_device(ACPI_HANDLE(dev), &adev))
		return -EINVAL;

	adma = kzalloc(sizeof(*adma), GFP_KERNEL);
	if (!adma)
		return -ENOMEM;

	adma->dev = dev;
	adma->acpi_dma_xlate = acpi_dma_xlate;
	adma->data = data;

	/* Now queue acpi_dma controller structure in list */
	mutex_lock(&acpi_dma_lock);
	list_add_tail(&adma->dma_controllers, &acpi_dma_list);
	mutex_unlock(&acpi_dma_lock);

	return 0;
}
EXPORT_SYMBOL_GPL(acpi_dma_controller_register);

/**
 * acpi_dma_controller_free - Remove a DMA controller from ACPI DMA helpers list
 * @dev:	struct device of DMA controller
 *
 * Memory allocated by acpi_dma_controller_register() is freed here.
 */
int acpi_dma_controller_free(struct device *dev)
{
	struct acpi_dma *adma;

	if (!dev)
		return -EINVAL;

	mutex_lock(&acpi_dma_lock);

	list_for_each_entry(adma, &acpi_dma_list, dma_controllers)
		if (adma->dev == dev) {
			list_del(&adma->dma_controllers);
			mutex_unlock(&acpi_dma_lock);
			kfree(adma);
			return 0;
		}

	mutex_unlock(&acpi_dma_lock);
	return -ENODEV;
}
EXPORT_SYMBOL_GPL(acpi_dma_controller_free);

static void devm_acpi_dma_release(struct device *dev, void *res)
{
	acpi_dma_controller_free(dev);
}

/**
 * devm_acpi_dma_controller_register - resource managed acpi_dma_controller_register()
 * @dev:		device that is registering this DMA controller
 * @acpi_dma_xlate:	translation function
 * @data		pointer to controller specific data
 *
 * Managed acpi_dma_controller_register(). DMA controller registered by this
 * function are automatically freed on driver detach. See
 * acpi_dma_controller_register() for more information.
 */
int devm_acpi_dma_controller_register(struct device *dev,
		struct dma_chan *(*acpi_dma_xlate)
		(struct acpi_dma_spec *, struct acpi_dma *),
		void *data)
{
	void *res;
	int ret;

	res = devres_alloc(devm_acpi_dma_release, 0, GFP_KERNEL);
	if (!res)
		return -ENOMEM;

	ret = acpi_dma_controller_register(dev, acpi_dma_xlate, data);
	if (ret) {
		devres_free(res);
		return ret;
	}
	devres_add(dev, res);
	return 0;
}
EXPORT_SYMBOL_GPL(devm_acpi_dma_controller_register);

/**
 * devm_acpi_dma_controller_free - resource managed acpi_dma_controller_free()
 *
 * Unregister a DMA controller registered with
 * devm_acpi_dma_controller_register(). Normally this function will not need to
 * be called and the resource management code will ensure that the resource is
 * freed.
 */
void devm_acpi_dma_controller_free(struct device *dev)
{
	WARN_ON(devres_destroy(dev, devm_acpi_dma_release, NULL, NULL));
}
EXPORT_SYMBOL_GPL(devm_acpi_dma_controller_free);

struct acpi_dma_parser_data {
	struct acpi_dma_spec dma_spec;
	size_t index;
	size_t n;
};

/**
 * acpi_dma_parse_fixed_dma - Parse FixedDMA ACPI resources to a DMA specifier
 * @res:	struct acpi_resource to get FixedDMA resources from
 * @data:	pointer to a helper struct acpi_dma_parser_data
 */
static int acpi_dma_parse_fixed_dma(struct acpi_resource *res, void *data)
{
	struct acpi_dma_parser_data *pdata = data;

	if (res->type == ACPI_RESOURCE_TYPE_FIXED_DMA) {
		struct acpi_resource_fixed_dma *dma = &res->data.fixed_dma;

		if (pdata->n++ == pdata->index) {
			pdata->dma_spec.chan_id = dma->channels;
			pdata->dma_spec.slave_id = dma->request_lines;
		}
	}

	/* Tell the ACPI core to skip this resource */
	return 1;
}

/**
 * acpi_dma_request_slave_chan_by_index - Get the DMA slave channel
 * @dev:	struct device to get DMA request from
 * @index:	index of FixedDMA descriptor for @dev
 *
 * Returns pointer to appropriate dma channel on success or NULL on error.
 */
struct dma_chan *acpi_dma_request_slave_chan_by_index(struct device *dev,
		size_t index)
{
	struct acpi_dma_parser_data pdata;
	struct acpi_dma_spec *dma_spec = &pdata.dma_spec;
	struct list_head resource_list;
	struct acpi_device *adev;
	struct acpi_dma *adma;
	struct dma_chan *chan = NULL;

	/* Check if the device was enumerated by ACPI */
	if (!dev || !ACPI_HANDLE(dev))
		return NULL;

	if (acpi_bus_get_device(ACPI_HANDLE(dev), &adev))
		return NULL;

	memset(&pdata, 0, sizeof(pdata));
	pdata.index = index;

	/* Initial values for the request line and channel */
	dma_spec->chan_id = -1;
	dma_spec->slave_id = -1;

	INIT_LIST_HEAD(&resource_list);
	acpi_dev_get_resources(adev, &resource_list,
			acpi_dma_parse_fixed_dma, &pdata);
	acpi_dev_free_resource_list(&resource_list);

	if (dma_spec->slave_id < 0 || dma_spec->chan_id < 0)
		return NULL;

	mutex_lock(&acpi_dma_lock);

	list_for_each_entry(adma, &acpi_dma_list, dma_controllers) {
		dma_spec->dev = adma->dev;
		chan = adma->acpi_dma_xlate(dma_spec, adma);
		if (chan)
			break;
	}

	mutex_unlock(&acpi_dma_lock);
	return chan;
}
EXPORT_SYMBOL_GPL(acpi_dma_request_slave_chan_by_index);

/**
 * acpi_dma_request_slave_chan_by_name - Get the DMA slave channel
 * @dev:	struct device to get DMA request from
 * @name:	represents corresponding FixedDMA descriptor for @dev
 *
 * In order to support both Device Tree and ACPI in a single driver we
 * translate the names "tx" and "rx" here based on the most common case where
 * the first FixedDMA descriptor is TX and second is RX.
 *
 * Returns pointer to appropriate dma channel on success or NULL on error.
 */
struct dma_chan *acpi_dma_request_slave_chan_by_name(struct device *dev,
		const char *name)
{
	size_t index;

	if (!strcmp(name, "tx"))
		index = 0;
	else if (!strcmp(name, "rx"))
		index = 1;
	else
		return NULL;

	return acpi_dma_request_slave_chan_by_index(dev, index);
}
EXPORT_SYMBOL_GPL(acpi_dma_request_slave_chan_by_name);

/**
 * acpi_dma_simple_xlate - Simple ACPI DMA engine translation helper
 * @dma_spec: pointer to ACPI DMA specifier
 * @adma: pointer to ACPI DMA controller data
 *
 * A simple translation function for ACPI based devices. Passes &struct
 * dma_spec to the DMA controller driver provided filter function. Returns
 * pointer to the channel if found or %NULL otherwise.
 */
struct dma_chan *acpi_dma_simple_xlate(struct acpi_dma_spec *dma_spec,
		struct acpi_dma *adma)
{
	struct acpi_dma_filter_info *info = adma->data;

	if (!info || !info->filter_fn)
		return NULL;

	return dma_request_channel(info->dma_cap, info->filter_fn, dma_spec);
}
EXPORT_SYMBOL_GPL(acpi_dma_simple_xlate);
