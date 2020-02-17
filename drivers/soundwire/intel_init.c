// SPDX-License-Identifier: (GPL-2.0 OR BSD-3-Clause)
// Copyright(c) 2015-17 Intel Corporation.

/*
 * SDW Intel Init Routines
 *
 * Initializes and creates SDW devices based on ACPI and Hardware values
 */

#include <linux/acpi.h>
#include <linux/export.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/soundwire/sdw_intel.h>
#include "intel.h"

#define SDW_LINK_TYPE		4 /* from Intel ACPI documentation */
#define SDW_MAX_LINKS		4
#define SDW_SHIM_LCAP		0x0
#define SDW_SHIM_BASE		0x2C000
#define SDW_ALH_BASE		0x2C800
#define SDW_LINK_BASE		0x30000
#define SDW_LINK_SIZE		0x10000

static int link_mask;
module_param_named(sdw_link_mask, link_mask, int, 0444);
MODULE_PARM_DESC(sdw_link_mask, "Intel link mask (one bit per link)");

static int sdw_intel_cleanup_pdev(struct sdw_intel_ctx *ctx)
{
	struct sdw_intel_link_res *link = ctx->links;
	int i;

	if (!link)
		return 0;

	for (i = 0; i < ctx->count; i++) {
		if (link->pdev)
			platform_device_unregister(link->pdev);
		link++;
	}

	kfree(ctx->links);
	ctx->links = NULL;

	return 0;
}

static struct sdw_intel_ctx
*sdw_intel_add_controller(struct sdw_intel_res *res)
{
	struct platform_device_info pdevinfo;
	struct platform_device *pdev;
	struct sdw_intel_link_res *link;
	struct sdw_intel_ctx *ctx;
	struct acpi_device *adev;
	int ret, i;
	u8 count;
	u32 caps;

	if (acpi_bus_get_device(res->handle, &adev))
		return NULL;

	/* Found controller, find links supported */
	count = 0;
	ret = fwnode_property_read_u8_array(acpi_fwnode_handle(adev),
					    "mipi-sdw-master-count", &count, 1);

	/* Don't fail on error, continue and use hw value */
	if (ret) {
		dev_err(&adev->dev,
			"Failed to read mipi-sdw-master-count: %d\n", ret);
		count = SDW_MAX_LINKS;
	}

	/* Check SNDWLCAP.LCOUNT */
	caps = ioread32(res->mmio_base + SDW_SHIM_BASE + SDW_SHIM_LCAP);
	caps &= GENMASK(2, 0);

	/* Check HW supported vs property value and use min of two */
	count = min_t(u8, caps, count);

	/* Check count is within bounds */
	if (count > SDW_MAX_LINKS) {
		dev_err(&adev->dev, "Link count %d exceeds max %d\n",
			count, SDW_MAX_LINKS);
		return NULL;
	} else if (!count) {
		dev_warn(&adev->dev, "No SoundWire links detected\n");
		return NULL;
	}

	dev_dbg(&adev->dev, "Creating %d SDW Link devices\n", count);

	ctx = kzalloc(sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return NULL;

	ctx->count = count;
	ctx->links = kcalloc(ctx->count, sizeof(*ctx->links), GFP_KERNEL);
	if (!ctx->links)
		goto link_err;

	link = ctx->links;

	/* Create SDW Master devices */
	for (i = 0; i < count; i++) {
		if (link_mask && !(link_mask & BIT(i))) {
			dev_dbg(&adev->dev,
				"Link %d masked, will not be enabled\n", i);
			link++;
			continue;
		}

		link->registers = res->mmio_base + SDW_LINK_BASE
					+ (SDW_LINK_SIZE * i);
		link->shim = res->mmio_base + SDW_SHIM_BASE;
		link->alh = res->mmio_base + SDW_ALH_BASE;

		link->ops = res->ops;
		link->dev = res->dev;

		memset(&pdevinfo, 0, sizeof(pdevinfo));

		pdevinfo.parent = res->parent;
		pdevinfo.name = "int-sdw";
		pdevinfo.id = i;
		pdevinfo.fwnode = acpi_fwnode_handle(adev);

		pdev = platform_device_register_full(&pdevinfo);
		if (IS_ERR(pdev)) {
			dev_err(&adev->dev,
				"platform device creation failed: %ld\n",
				PTR_ERR(pdev));
			goto pdev_err;
		}

		link->pdev = pdev;
		link++;
	}

	return ctx;

pdev_err:
	sdw_intel_cleanup_pdev(ctx);
link_err:
	kfree(ctx);
	return NULL;
}

static acpi_status sdw_intel_acpi_cb(acpi_handle handle, u32 level,
				     void *cdata, void **return_value)
{
	struct sdw_intel_res *res = cdata;
	struct acpi_device *adev;
	acpi_status status;
	u64 adr;

	status = acpi_evaluate_integer(handle, METHOD_NAME__ADR, NULL, &adr);
	if (ACPI_FAILURE(status))
		return AE_OK; /* keep going */

	if (acpi_bus_get_device(handle, &adev)) {
		pr_err("%s: Couldn't find ACPI handle\n", __func__);
		return AE_NOT_FOUND;
	}

	res->handle = handle;

	/*
	 * On some Intel platforms, multiple children of the HDAS
	 * device can be found, but only one of them is the SoundWire
	 * controller. The SNDW device is always exposed with
	 * Name(_ADR, 0x40000000), with bits 31..28 representing the
	 * SoundWire link so filter accordingly
	 */
	if ((adr & GENMASK(31, 28)) >> 28 != SDW_LINK_TYPE)
		return AE_OK; /* keep going */

	/* device found, stop namespace walk */
	return AE_CTRL_TERMINATE;
}

/**
 * sdw_intel_init() - SoundWire Intel init routine
 * @parent_handle: ACPI parent handle
 * @res: resource data
 *
 * This scans the namespace and creates SoundWire link controller devices
 * based on the info queried.
 */
void *sdw_intel_init(acpi_handle *parent_handle, struct sdw_intel_res *res)
{
	acpi_status status;

	status = acpi_walk_namespace(ACPI_TYPE_DEVICE,
				     parent_handle, 1,
				     sdw_intel_acpi_cb,
				     NULL, res, NULL);
	if (ACPI_FAILURE(status))
		return NULL;

	return sdw_intel_add_controller(res);
}

/**
 * sdw_intel_exit() - SoundWire Intel exit
 * @arg: callback context
 *
 * Delete the controller instances created and cleanup
 */
void sdw_intel_exit(struct sdw_intel_ctx *ctx)
{
	sdw_intel_cleanup_pdev(ctx);
	kfree(ctx);
}
EXPORT_SYMBOL(sdw_intel_exit);

MODULE_LICENSE("Dual BSD/GPL");
MODULE_DESCRIPTION("Intel Soundwire Init Library");
