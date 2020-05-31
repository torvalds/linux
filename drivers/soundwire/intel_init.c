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
#include "cadence_master.h"
#include "intel.h"

#define SDW_LINK_TYPE		4 /* from Intel ACPI documentation */
#define SDW_MAX_LINKS		4
#define SDW_SHIM_LCAP		0x0
#define SDW_SHIM_BASE		0x2C000
#define SDW_ALH_BASE		0x2C800
#define SDW_LINK_BASE		0x30000
#define SDW_LINK_SIZE		0x10000

static int ctrl_link_mask;
module_param_named(sdw_link_mask, ctrl_link_mask, int, 0444);
MODULE_PARM_DESC(sdw_link_mask, "Intel link mask (one bit per link)");

static bool is_link_enabled(struct fwnode_handle *fw_node, int i)
{
	struct fwnode_handle *link;
	char name[32];
	u32 quirk_mask = 0;

	/* Find master handle */
	snprintf(name, sizeof(name),
		 "mipi-sdw-link-%d-subproperties", i);

	link = fwnode_get_named_child_node(fw_node, name);
	if (!link)
		return false;

	fwnode_property_read_u32(link,
				 "intel-quirk-mask",
				 &quirk_mask);

	if (quirk_mask & SDW_INTEL_QUIRK_MASK_BUS_DISABLE)
		return false;

	return true;
}

static int sdw_intel_cleanup(struct sdw_intel_ctx *ctx)
{
	struct sdw_intel_link_res *link = ctx->links;
	u32 link_mask;
	int i;

	if (!link)
		return 0;

	link_mask = ctx->link_mask;

	for (i = 0; i < ctx->count; i++, link++) {
		if (!(link_mask & BIT(i)))
			continue;

		if (link->pdev)
			platform_device_unregister(link->pdev);
	}

	return 0;
}

static int
sdw_intel_scan_controller(struct sdw_intel_acpi_info *info)
{
	struct acpi_device *adev;
	int ret, i;
	u8 count;

	if (acpi_bus_get_device(info->handle, &adev))
		return -EINVAL;

	/* Found controller, find links supported */
	count = 0;
	ret = fwnode_property_read_u8_array(acpi_fwnode_handle(adev),
					    "mipi-sdw-master-count", &count, 1);

	/*
	 * In theory we could check the number of links supported in
	 * hardware, but in that step we cannot assume SoundWire IP is
	 * powered.
	 *
	 * In addition, if the BIOS doesn't even provide this
	 * 'master-count' property then all the inits based on link
	 * masks will fail as well.
	 *
	 * We will check the hardware capabilities in the startup() step
	 */

	if (ret) {
		dev_err(&adev->dev,
			"Failed to read mipi-sdw-master-count: %d\n", ret);
		return -EINVAL;
	}

	/* Check count is within bounds */
	if (count > SDW_MAX_LINKS) {
		dev_err(&adev->dev, "Link count %d exceeds max %d\n",
			count, SDW_MAX_LINKS);
		return -EINVAL;
	}

	if (!count) {
		dev_warn(&adev->dev, "No SoundWire links detected\n");
		return -EINVAL;
	}
	dev_dbg(&adev->dev, "ACPI reports %d SDW Link devices\n", count);

	info->count = count;
	info->link_mask = 0;

	for (i = 0; i < count; i++) {
		if (ctrl_link_mask && !(ctrl_link_mask & BIT(i))) {
			dev_dbg(&adev->dev,
				"Link %d masked, will not be enabled\n", i);
			continue;
		}

		if (!is_link_enabled(acpi_fwnode_handle(adev), i)) {
			dev_dbg(&adev->dev,
				"Link %d not selected in firmware\n", i);
			continue;
		}

		info->link_mask |= BIT(i);
	}

	return 0;
}

static struct sdw_intel_ctx
*sdw_intel_probe_controller(struct sdw_intel_res *res)
{
	struct platform_device_info pdevinfo;
	struct platform_device *pdev;
	struct sdw_intel_link_res *link;
	struct sdw_intel_ctx *ctx;
	struct acpi_device *adev;
	u32 link_mask;
	int count;
	int i;

	if (!res)
		return NULL;

	if (acpi_bus_get_device(res->handle, &adev))
		return NULL;

	if (!res->count)
		return NULL;

	count = res->count;
	dev_dbg(&adev->dev, "Creating %d SDW Link devices\n", count);

	ctx = devm_kzalloc(&adev->dev, sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return NULL;

	ctx->count = count;
	ctx->links = devm_kcalloc(&adev->dev, ctx->count,
				  sizeof(*ctx->links), GFP_KERNEL);
	if (!ctx->links)
		return NULL;

	ctx->count = count;
	ctx->mmio_base = res->mmio_base;
	ctx->link_mask = res->link_mask;
	ctx->handle = res->handle;

	link = ctx->links;
	link_mask = ctx->link_mask;

	/* Create SDW Master devices */
	for (i = 0; i < count; i++, link++) {
		if (!(link_mask & BIT(i))) {
			dev_dbg(&adev->dev,
				"Link %d masked, will not be enabled\n", i);
			continue;
		}

		link->mmio_base = res->mmio_base;
		link->registers = res->mmio_base + SDW_LINK_BASE
			+ (SDW_LINK_SIZE * i);
		link->shim = res->mmio_base + SDW_SHIM_BASE;
		link->alh = res->mmio_base + SDW_ALH_BASE;

		link->ops = res->ops;
		link->dev = res->dev;

		memset(&pdevinfo, 0, sizeof(pdevinfo));

		pdevinfo.parent = res->parent;
		pdevinfo.name = "intel-sdw";
		pdevinfo.id = i;
		pdevinfo.fwnode = acpi_fwnode_handle(adev);
		pdevinfo.data = link;
		pdevinfo.size_data = sizeof(*link);

		pdev = platform_device_register_full(&pdevinfo);
		if (IS_ERR(pdev)) {
			dev_err(&adev->dev,
				"platform device creation failed: %ld\n",
				PTR_ERR(pdev));
			goto err;
		}
		link->pdev = pdev;
	}

	return ctx;

err:
	ctx->count = i;
	sdw_intel_cleanup(ctx);
	return NULL;
}

static int
sdw_intel_startup_controller(struct sdw_intel_ctx *ctx)
{
	struct acpi_device *adev;
	struct sdw_intel_link_res *link;
	u32 caps;
	u32 link_mask;
	int i;

	if (acpi_bus_get_device(ctx->handle, &adev))
		return -EINVAL;

	/* Check SNDWLCAP.LCOUNT */
	caps = ioread32(ctx->mmio_base + SDW_SHIM_BASE + SDW_SHIM_LCAP);
	caps &= GENMASK(2, 0);

	/* Check HW supported vs property value */
	if (caps < ctx->count) {
		dev_err(&adev->dev,
			"BIOS master count is larger than hardware capabilities\n");
		return -EINVAL;
	}

	if (!ctx->links)
		return -EINVAL;

	link = ctx->links;
	link_mask = ctx->link_mask;

	/* Startup SDW Master devices */
	for (i = 0; i < ctx->count; i++, link++) {
		if (!(link_mask & BIT(i)))
			continue;

		intel_master_startup(link->pdev);
	}

	return 0;
}

static acpi_status sdw_intel_acpi_cb(acpi_handle handle, u32 level,
				     void *cdata, void **return_value)
{
	struct sdw_intel_acpi_info *info = cdata;
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

	info->handle = handle;

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
 * sdw_intel_acpi_scan() - SoundWire Intel init routine
 * @parent_handle: ACPI parent handle
 * @info: description of what firmware/DSDT tables expose
 *
 * This scans the namespace and queries firmware to figure out which
 * links to enable. A follow-up use of sdw_intel_probe() and
 * sdw_intel_startup() is required for creation of devices and bus
 * startup
 */
int sdw_intel_acpi_scan(acpi_handle *parent_handle,
			struct sdw_intel_acpi_info *info)
{
	acpi_status status;

	status = acpi_walk_namespace(ACPI_TYPE_DEVICE,
				     parent_handle, 1,
				     sdw_intel_acpi_cb,
				     NULL, info, NULL);
	if (ACPI_FAILURE(status))
		return -ENODEV;

	return sdw_intel_scan_controller(info);
}
EXPORT_SYMBOL(sdw_intel_acpi_scan);

/**
 * sdw_intel_probe() - SoundWire Intel probe routine
 * @res: resource data
 *
 * This registers a platform device for each Master handled by the controller,
 * and SoundWire Master and Slave devices will be created by the platform
 * device probe. All the information necessary is stored in the context, and
 * the res argument pointer can be freed after this step.
 * This function will be called after sdw_intel_acpi_scan() by SOF probe.
 */
struct sdw_intel_ctx
*sdw_intel_probe(struct sdw_intel_res *res)
{
	return sdw_intel_probe_controller(res);
}
EXPORT_SYMBOL(sdw_intel_probe);

/**
 * sdw_intel_startup() - SoundWire Intel startup
 * @ctx: SoundWire context allocated in the probe
 *
 * Startup Intel SoundWire controller. This function will be called after
 * Intel Audio DSP is powered up.
 */
int sdw_intel_startup(struct sdw_intel_ctx *ctx)
{
	return sdw_intel_startup_controller(ctx);
}
EXPORT_SYMBOL(sdw_intel_startup);
/**
 * sdw_intel_exit() - SoundWire Intel exit
 * @ctx: SoundWire context allocated in the probe
 *
 * Delete the controller instances created and cleanup
 */
void sdw_intel_exit(struct sdw_intel_ctx *ctx)
{
	sdw_intel_cleanup(ctx);
}
EXPORT_SYMBOL(sdw_intel_exit);

MODULE_LICENSE("Dual BSD/GPL");
MODULE_DESCRIPTION("Intel Soundwire Init Library");
