// SPDX-License-Identifier: GPL-2.0-only
/*
 * ACPI helpers for DMA request / controller
 *
 * Based on of-dma.c
 *
 * Copyright (C) 2013, Intel Corporation
 * Authors: Andy Shevchenko <andriy.shevchenko@linux.intel.com>
 *	    Mika Westerberg <mika.westerberg@linux.intel.com>
 */

#include <linux/device.h>
#include <linux/dma-mapping.h>
#include <linux/err.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/ioport.h>
#include <linux/acpi.h>
#include <linux/acpi_dma.h>
#include <linux/property.h>

static LIST_HEAD(acpi_dma_list);
static DEFINE_MUTEX(acpi_dma_lock);

/**
 * acpi_dma_parse_resource_group - match device and parse resource group
 * @grp:	CSRT resource group
 * @adev:	ACPI device to match with
 * @adma:	struct acpi_dma of the given DMA controller
 *
 * In order to match a device from DSDT table to the corresponding CSRT device
 * we use MMIO address and IRQ.
 *
 * Return:
 * 1 on success, 0 when no information is available, or appropriate errno value
 * on error.
 */
static int acpi_dma_parse_resource_group(const struct acpi_csrt_group *grp,
		struct acpi_device *adev, struct acpi_dma *adma)
{
	const struct acpi_csrt_shared_info *si;
	struct list_head resource_list;
	struct resource_entry *rentry;
	resource_size_t mem = 0, irq = 0;
	int ret;

	if (grp->shared_info_length != sizeof(struct acpi_csrt_shared_info))
		return -ENODEV;

	INIT_LIST_HEAD(&resource_list);
	ret = acpi_dev_get_resources(adev, &resource_list, NULL, NULL);
	if (ret <= 0)
		return 0;

	list_for_each_entry(rentry, &resource_list, node) {
		if (resource_type(rentry->res) == IORESOURCE_MEM)
			mem = rentry->res->start;
		else if (resource_type(rentry->res) == IORESOURCE_IRQ)
			irq = rentry->res->start;
	}

	acpi_dev_free_resource_list(&resource_list);

	/* Consider initial zero values as resource not found */
	if (mem == 0 && irq == 0)
		return 0;

	si = (const struct acpi_csrt_shared_info *)&grp[1];

	/* Match device by MMIO and IRQ */
	if (si->mmio_base_low != lower_32_bits(mem) ||
	    si->mmio_base_high != upper_32_bits(mem) ||
	    si->gsi_interrupt != irq)
		return 0;

	dev_dbg(&adev->dev, "matches with %.4s%04X (rev %u)\n",
		(char *)&grp->vendor_id, grp->device_id, grp->revision);

	/* Check if the request line range is available */
	if (si->base_request_line == 0 && si->num_handshake_signals == 0)
		return 0;

	/* Set up DMA mask based on value from CSRT */
	ret = dma_coerce_mask_and_coherent(&adev->dev,
					   DMA_BIT_MASK(si->dma_address_width));
	if (ret)
		return 0;

	adma->base_request_line = si->base_request_line;
	adma->end_request_line = si->base_request_line +
				 si->num_handshake_signals - 1;

	dev_dbg(&adev->dev, "request line base: 0x%04x end: 0x%04x\n",
		adma->base_request_line, adma->end_request_line);

	return 1;
}

/**
 * acpi_dma_parse_csrt - parse CSRT to exctract additional DMA resources
 * @adev:	ACPI device to match with
 * @adma:	struct acpi_dma of the given DMA controller
 *
 * CSRT or Core System Resources Table is a proprietary ACPI table
 * introduced by Microsoft. This table can contain devices that are not in
 * the system DSDT table. In particular DMA controllers might be described
 * here.
 *
 * We are using this table to get the request line range of the specific DMA
 * controller to be used later.
 */
static void acpi_dma_parse_csrt(struct acpi_device *adev, struct acpi_dma *adma)
{
	struct acpi_csrt_group *grp, *end;
	struct acpi_table_csrt *csrt;
	acpi_status status;
	int ret;

	status = acpi_get_table(ACPI_SIG_CSRT, 0,
				(struct acpi_table_header **)&csrt);
	if (ACPI_FAILURE(status)) {
		if (status != AE_NOT_FOUND)
			dev_warn(&adev->dev, "failed to get the CSRT table\n");
		return;
	}

	grp = (struct acpi_csrt_group *)(csrt + 1);
	end = (struct acpi_csrt_group *)((void *)csrt + csrt->header.length);

	while (grp < end) {
		ret = acpi_dma_parse_resource_group(grp, adev, adma);
		if (ret < 0) {
			dev_warn(&adev->dev,
				 "error in parsing resource group\n");
			return;
		}

		grp = (struct acpi_csrt_group *)((void *)grp + grp->length);
	}
}

/**
 * acpi_dma_controller_register - Register a DMA controller to ACPI DMA helpers
 * @dev:		struct device of DMA controller
 * @acpi_dma_xlate:	translation function which converts a dma specifier
 *			into a dma_chan structure
 * @data:		pointer to controller specific data to be used by
 *			translation function
 *
 * Allocated memory should be freed with appropriate acpi_dma_controller_free()
 * call.
 *
 * Return:
 * 0 on success or appropriate errno value on error.
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
	adev = ACPI_COMPANION(dev);
	if (!adev)
		return -EINVAL;

	adma = kzalloc(sizeof(*adma), GFP_KERNEL);
	if (!adma)
		return -ENOMEM;

	adma->dev = dev;
	adma->acpi_dma_xlate = acpi_dma_xlate;
	adma->data = data;

	acpi_dma_parse_csrt(adev, adma);

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
 *
 * Return:
 * 0 on success or appropriate errno value on error.
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
 * @data:		pointer to controller specific data
 *
 * Managed acpi_dma_controller_register(). DMA controller registered by this
 * function are automatically freed on driver detach. See
 * acpi_dma_controller_register() for more information.
 *
 * Return:
 * 0 on success or appropriate errno value on error.
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
 * @dev:	device that is unregistering as DMA controller
 *
 * Unregister a DMA controller registered with
 * devm_acpi_dma_controller_register(). Normally this function will not need to
 * be called and the resource management code will ensure that the resource is
 * freed.
 */
void devm_acpi_dma_controller_free(struct device *dev)
{
	WARN_ON(devres_release(dev, devm_acpi_dma_release, NULL, NULL));
}
EXPORT_SYMBOL_GPL(devm_acpi_dma_controller_free);

/**
 * acpi_dma_update_dma_spec - prepare dma specifier to pass to translation function
 * @adma:	struct acpi_dma of DMA controller
 * @dma_spec:	dma specifier to update
 *
 * Accordingly to ACPI 5.0 Specification Table 6-170 "Fixed DMA Resource
 * Descriptor":
 *	DMA Request Line bits is a platform-relative number uniquely
 *	identifying the request line assigned. Request line-to-Controller
 *	mapping is done in a controller-specific OS driver.
 * That's why we can safely adjust slave_id when the appropriate controller is
 * found.
 *
 * Return:
 * 0, if no information is avaiable, -1 on mismatch, and 1 otherwise.
 */
static int acpi_dma_update_dma_spec(struct acpi_dma *adma,
		struct acpi_dma_spec *dma_spec)
{
	/* Set link to the DMA controller device */
	dma_spec->dev = adma->dev;

	/* Check if the request line range is available */
	if (adma->base_request_line == 0 && adma->end_request_line == 0)
		return 0;

	/* Check if slave_id falls to the range */
	if (dma_spec->slave_id < adma->base_request_line ||
	    dma_spec->slave_id > adma->end_request_line)
		return -1;

	/*
	 * Here we adjust slave_id. It should be a relative number to the base
	 * request line.
	 */
	dma_spec->slave_id -= adma->base_request_line;

	return 1;
}

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
 * Return:
 * Pointer to appropriate dma channel on success or an error pointer.
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
	int found;

	/* Check if the device was enumerated by ACPI */
	if (!dev)
		return ERR_PTR(-ENODEV);

	adev = ACPI_COMPANION(dev);
	if (!adev)
		return ERR_PTR(-ENODEV);

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
		return ERR_PTR(-ENODEV);

	mutex_lock(&acpi_dma_lock);

	list_for_each_entry(adma, &acpi_dma_list, dma_controllers) {
		/*
		 * We are not going to call translation function if slave_id
		 * doesn't fall to the request range.
		 */
		found = acpi_dma_update_dma_spec(adma, dma_spec);
		if (found < 0)
			continue;
		chan = adma->acpi_dma_xlate(dma_spec, adma);
		/*
		 * Try to get a channel only from the DMA controller that
		 * matches the slave_id. See acpi_dma_update_dma_spec()
		 * description for the details.
		 */
		if (found > 0 || chan)
			break;
	}

	mutex_unlock(&acpi_dma_lock);
	return chan ? chan : ERR_PTR(-EPROBE_DEFER);
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
 * If the device has "dma-names" property the FixedDMA descriptor indices
 * are retrieved based on those. Otherwise the function falls back using
 * hardcoded indices.
 *
 * Return:
 * Pointer to appropriate dma channel on success or an error pointer.
 */
struct dma_chan *acpi_dma_request_slave_chan_by_name(struct device *dev,
		const char *name)
{
	int index;

	index = device_property_match_string(dev, "dma-names", name);
	if (index < 0) {
		if (!strcmp(name, "tx"))
			index = 0;
		else if (!strcmp(name, "rx"))
			index = 1;
		else
			return ERR_PTR(-ENODEV);
	}

	dev_dbg(dev, "Looking for DMA channel \"%s\" at index %d...\n", name, index);
	return acpi_dma_request_slave_chan_by_index(dev, index);
}
EXPORT_SYMBOL_GPL(acpi_dma_request_slave_chan_by_name);

/**
 * acpi_dma_simple_xlate - Simple ACPI DMA engine translation helper
 * @dma_spec: pointer to ACPI DMA specifier
 * @adma: pointer to ACPI DMA controller data
 *
 * A simple translation function for ACPI based devices. Passes &struct
 * dma_spec to the DMA controller driver provided filter function.
 *
 * Return:
 * Pointer to the channel if found or %NULL otherwise.
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
