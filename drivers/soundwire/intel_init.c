// SPDX-License-Identifier: (GPL-2.0 OR BSD-3-Clause)
// Copyright(c) 2015-17 Intel Corporation.

/*
 * SDW Intel Init Routines
 *
 * Initializes and creates SDW devices based on ACPI and Hardware values
 */

#include <linux/acpi.h>
#include <linux/export.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/soundwire/sdw_intel.h>
#include "cadence_master.h"
#include "intel.h"

#define SDW_SHIM_LCAP		0x0
#define SDW_SHIM_BASE		0x2C000
#define SDW_ALH_BASE		0x2C800
#define SDW_LINK_BASE		0x30000
#define SDW_LINK_SIZE		0x10000

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

		if (link->pdev) {
			pm_runtime_disable(&link->pdev->dev);
			platform_device_unregister(link->pdev);
		}

		if (!link->clock_stop_quirks)
			pm_runtime_put_noidle(link->dev);
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
EXPORT_SYMBOL_NS(sdw_intel_thread, SOUNDWIRE_INTEL_INIT);

static struct sdw_intel_ctx
*sdw_intel_probe_controller(struct sdw_intel_res *res)
{
	struct platform_device_info pdevinfo;
	struct platform_device *pdev;
	struct sdw_intel_link_res *link;
	struct sdw_intel_ctx *ctx;
	struct acpi_device *adev;
	struct sdw_slave *slave;
	struct list_head *node;
	struct sdw_bus *bus;
	u32 link_mask;
	int num_slaves = 0;
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
	mutex_init(&ctx->shim_lock);

	link = ctx->links;
	link_mask = ctx->link_mask;

	INIT_LIST_HEAD(&ctx->link_list);

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

		link->clock_stop_quirks = res->clock_stop_quirks;
		link->shim_lock = &ctx->shim_lock;
		link->shim_mask = &ctx->shim_mask;
		link->link_mask = link_mask;

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
		link->cdns = platform_get_drvdata(pdev);

		if (!link->cdns) {
			dev_err(&adev->dev, "failed to get link->cdns\n");
			/*
			 * 1 will be subtracted from i in the err label, but we need to call
			 * intel_link_dev_unregister for this ldev, so plus 1 now
			 */
			i++;
			goto err;
		}
		list_add_tail(&link->list, &ctx->link_list);
		bus = &link->cdns->bus;
		/* Calculate number of slaves */
		list_for_each(node, &bus->slaves)
			num_slaves++;
	}

	ctx->ids = devm_kcalloc(&adev->dev, num_slaves,
				sizeof(*ctx->ids), GFP_KERNEL);
	if (!ctx->ids)
		goto err;

	ctx->num_slaves = num_slaves;
	i = 0;
	list_for_each_entry(link, &ctx->link_list, list) {
		bus = &link->cdns->bus;
		list_for_each_entry(slave, &bus->slaves, node) {
			ctx->ids[i].id = slave->id;
			ctx->ids[i].link_id = bus->link_id;
			i++;
		}
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
EXPORT_SYMBOL_NS(sdw_intel_probe, SOUNDWIRE_INTEL_INIT);

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
}
EXPORT_SYMBOL_NS(sdw_intel_exit, SOUNDWIRE_INTEL_INIT);

void sdw_intel_process_wakeen_event(struct sdw_intel_ctx *ctx)
{
	struct sdw_intel_link_res *link;
	u32 link_mask;
	int i;

	if (!ctx->links)
		return;

	link = ctx->links;
	link_mask = ctx->link_mask;

	/* Startup SDW Master devices */
	for (i = 0; i < ctx->count; i++, link++) {
		if (!(link_mask & BIT(i)))
			continue;

		intel_master_process_wakeen_event(link->pdev);
	}
}
EXPORT_SYMBOL_NS(sdw_intel_process_wakeen_event, SOUNDWIRE_INTEL_INIT);

MODULE_LICENSE("Dual BSD/GPL");
MODULE_DESCRIPTION("Intel Soundwire Init Library");
