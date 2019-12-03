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
#include <linux/interrupt.h>
#include <linux/pm_runtime.h>
#include <linux/soundwire/sdw.h>
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
	struct sdw_master_device *md;
	u32 link_mask;
	int i;

	if (!link)
		return 0;

	link_mask = ctx->link_mask;

	for (i = 0; i < ctx->count; i++, link++) {
		if (link_mask && !(link_mask & BIT(i)))
			continue;

		if (!link->clock_stop_quirks)
			pm_runtime_put_noidle(link->dev);

		md = link->md;
		if (md)
			md->driver->remove(md);
	}

	kfree(ctx->links);
	ctx->links = NULL;

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
	} else if (!count) {
		dev_warn(&adev->dev, "No SoundWire links detected\n");
		return -EINVAL;
	}
	dev_dbg(&adev->dev, "ACPI reports %d SDW Link devices\n", count);

	info->count = count;

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

#define HDA_DSP_REG_ADSPIC2             (0x10)
#define HDA_DSP_REG_ADSPIS2             (0x14)
#define HDA_DSP_REG_ADSPIC2_SNDW        BIT(5)

/**
 * sdw_intel_enable_irq() - enable/disable Intel SoundWire IRQ
 * @mmio_base: The mmio base of the control register
 * @enable: true if enable
 */
void sdw_intel_enable_irq(void __iomem *mmio_base, bool enable)
{
	u32 val;

	val = readl(mmio_base + HDA_DSP_REG_ADSPIC2);

	if (enable)
		val |= HDA_DSP_REG_ADSPIC2_SNDW;
	else
		val &= ~HDA_DSP_REG_ADSPIC2_SNDW;

	writel(val, mmio_base + HDA_DSP_REG_ADSPIC2);
}
EXPORT_SYMBOL_NS(sdw_intel_enable_irq, SOUNDWIRE_INTEL_INIT);

irqreturn_t sdw_intel_thread(int irq, void *dev_id)
{
	struct sdw_intel_ctx *ctx = dev_id;
	struct sdw_intel_link_res *link;

	list_for_each_entry(link, &ctx->link_list, list)
		sdw_cdns_irq(irq, link->cdns);

	sdw_intel_enable_irq(ctx->mmio_base, true);
	return IRQ_HANDLED;
}
EXPORT_SYMBOL(sdw_intel_thread);

static struct sdw_intel_ctx
*sdw_intel_probe_controller(struct sdw_intel_res *res)
{
	struct sdw_intel_link_res *link;
	struct sdw_intel_ctx *ctx;
	struct acpi_device *adev;
	struct sdw_master_device *md;
	u32 link_mask;
	int count;
	int err;
	int i;

	if (!res)
		return NULL;

	if (acpi_bus_get_device(res->handle, &adev))
		return NULL;

	if (!res->count)
		return NULL;

	count = res->count;
	dev_dbg(&adev->dev, "Creating %d SDW Link devices\n", count);

	ctx = kzalloc(sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return NULL;

	ctx->links = kcalloc(count, sizeof(*ctx->links), GFP_KERNEL);
	if (!ctx->links)
		goto link_err;

	ctx->count = count;
	ctx->mmio_base = res->mmio_base;
	ctx->link_mask = res->link_mask;
	ctx->handle = res->handle;
	mutex_init(&ctx->shim_lock);

	link = ctx->links;
	link_mask = ctx->link_mask;

	err = driver_register(&intel_sdw_driver.driver);
	if (err) {
		dev_err(&adev->dev, "failed to register sdw master driver\n");
		goto register_err;
	}

	INIT_LIST_HEAD(&ctx->link_list);

	/* Create SDW Master devices */
	for (i = 0; i < count; i++, link++) {
		if (link_mask && !(link_mask & BIT(i)))
			continue;

		md = sdw_md_add(&intel_sdw_driver,
				res->parent,
				acpi_fwnode_handle(adev),
				i);

		if (IS_ERR(md)) {
			dev_err(&adev->dev, "Could not create link %d\n", i);
			goto err;
		}
		link->md = md;
		link->mmio_base = res->mmio_base;
		link->registers = res->mmio_base + SDW_LINK_BASE
			+ (SDW_LINK_SIZE * i);
		link->shim = res->mmio_base + SDW_SHIM_BASE;
		link->alh = res->mmio_base + SDW_ALH_BASE;
		link->ops = res->ops;
		link->dev = res->dev;
		link->clock_stop_quirks = res->clock_stop_quirks;

		/* let the SoundWire master driver to its probe */
		md->driver->probe(md, link);

		list_add_tail(&link->list, &ctx->link_list);
	}

	return ctx;

err:
	sdw_intel_cleanup(ctx);
link_err:
	driver_unregister(&intel_sdw_driver.driver);
register_err:
	kfree(ctx);
	return NULL;
}

static int
sdw_intel_startup_controller(struct sdw_intel_ctx *ctx)
{
	struct acpi_device *adev;
	struct sdw_intel_link_res *link;
	struct sdw_master_device *md;
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

	/* Create SDW Master devices */
	for (i = 0; i < ctx->count; i++, link++) {
		if (link_mask && !(link_mask & BIT(i)))
			continue;

		link->shim_lock = &ctx->shim_lock;

		md = link->md;

		md->driver->startup(md);

		if (!link->clock_stop_quirks) {
			/*
			 * we need to prevent the parent PCI device
			 * from entering pm_runtime suspend, so that
			 * power rails to the SoundWire IP are not
			 * turned off.
			 */
			pm_runtime_get_noresume(link->dev);
		}
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
EXPORT_SYMBOL_NS(sdw_intel_acpi_scan, SOUNDWIRE_INTEL_INIT);

/**
 * sdw_intel_probe() - SoundWire Intel probe routine
 * @parent_handle: ACPI parent handle
 * @res: resource data
 *
 * This creates SoundWire Master and Slave devices below the controller.
 * All the information necessary is stored in the context, and the res
 * argument pointer can be freed after this step.
 */
struct sdw_intel_ctx
*sdw_intel_probe(struct sdw_intel_res *res)
{
	return sdw_intel_probe_controller(res);
}
EXPORT_SYMBOL_NS(sdw_intel_probe, SOUNDWIRE_INTEL_INIT);

/**
 * sdw_intel_startup() - SoundWire Intel startup
 * @ctx: SoundWire context allocated in the probe
 *
 */
int sdw_intel_startup(struct sdw_intel_ctx *ctx)
{
	return sdw_intel_startup_controller(ctx);
}
EXPORT_SYMBOL_NS(sdw_intel_startup, SOUNDWIRE_INTEL_INIT);
/**
 * sdw_intel_exit() - SoundWire Intel exit
 * @ctx: SoundWire context allocated in the probe
 *
 * Delete the controller instances created and cleanup
 */
void sdw_intel_exit(struct sdw_intel_ctx *ctx)
{
	sdw_intel_cleanup(ctx);
	driver_unregister(&intel_sdw_driver.driver);
	kfree(ctx);
}
EXPORT_SYMBOL_NS(sdw_intel_exit, SOUNDWIRE_INTEL_INIT);

void sdw_intel_process_wakeen_event(struct sdw_intel_ctx *ctx)
{
	struct sdw_intel_link_res *link;
	struct sdw_master_device *md;

	if (!ctx->links)
		return;

	list_for_each_entry(link, &ctx->link_list, list) {

		md = link->md;

		md->driver->process_wake_event(md);
	}
}
EXPORT_SYMBOL_NS(sdw_intel_process_wakeen_event, SOUNDWIRE_INTEL_INIT);

MODULE_LICENSE("Dual BSD/GPL");
MODULE_DESCRIPTION("Intel Soundwire Init Library");
MODULE_IMPORT_NS(SOUNDWIRE_INTEL);
