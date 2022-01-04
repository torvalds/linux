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
#include <linux/auxiliary_bus.h>
#include <linux/pm_runtime.h>
#include <linux/soundwire/sdw_intel.h>
#include "cadence_master.h"
#include "intel.h"

static void intel_link_dev_release(struct device *dev)
{
	struct auxiliary_device *auxdev = to_auxiliary_dev(dev);
	struct sdw_intel_link_dev *ldev = auxiliary_dev_to_sdw_intel_link_dev(auxdev);

	kfree(ldev);
}

/* alloc, init and add link devices */
static struct sdw_intel_link_dev *intel_link_dev_register(struct sdw_intel_res *res,
							  struct sdw_intel_ctx *ctx,
							  struct fwnode_handle *fwnode,
							  const char *name,
							  int link_id)
{
	struct sdw_intel_link_dev *ldev;
	struct sdw_intel_link_res *link;
	struct auxiliary_device *auxdev;
	int ret;

	ldev = kzalloc(sizeof(*ldev), GFP_KERNEL);
	if (!ldev)
		return ERR_PTR(-ENOMEM);

	auxdev = &ldev->auxdev;
	auxdev->name = name;
	auxdev->dev.parent = res->parent;
	auxdev->dev.fwnode = fwnode;
	auxdev->dev.release = intel_link_dev_release;

	/* we don't use an IDA since we already have a link ID */
	auxdev->id = link_id;

	/*
	 * keep a handle on the allocated memory, to be used in all other functions.
	 * Since the same pattern is used to skip links that are not enabled, there is
	 * no need to check if ctx->ldev[i] is NULL later on.
	 */
	ctx->ldev[link_id] = ldev;

	/* Add link information used in the driver probe */
	link = &ldev->link_res;
	link->mmio_base = res->mmio_base;
	link->registers = res->mmio_base + SDW_LINK_BASE
		+ (SDW_LINK_SIZE * link_id);
	link->shim = res->mmio_base + res->shim_base;
	link->alh = res->mmio_base + res->alh_base;

	link->ops = res->ops;
	link->dev = res->dev;

	link->clock_stop_quirks = res->clock_stop_quirks;
	link->shim_lock = &ctx->shim_lock;
	link->shim_mask = &ctx->shim_mask;
	link->link_mask = ctx->link_mask;

	/* now follow the two-step init/add sequence */
	ret = auxiliary_device_init(auxdev);
	if (ret < 0) {
		dev_err(res->parent, "failed to initialize link dev %s link_id %d\n",
			name, link_id);
		kfree(ldev);
		return ERR_PTR(ret);
	}

	ret = auxiliary_device_add(&ldev->auxdev);
	if (ret < 0) {
		dev_err(res->parent, "failed to add link dev %s link_id %d\n",
			ldev->auxdev.name, link_id);
		/* ldev will be freed with the put_device() and .release sequence */
		auxiliary_device_uninit(&ldev->auxdev);
		return ERR_PTR(ret);
	}

	return ldev;
}

static void intel_link_dev_unregister(struct sdw_intel_link_dev *ldev)
{
	auxiliary_device_delete(&ldev->auxdev);
	auxiliary_device_uninit(&ldev->auxdev);
}

static int sdw_intel_cleanup(struct sdw_intel_ctx *ctx)
{
	struct sdw_intel_link_dev *ldev;
	u32 link_mask;
	int i;

	link_mask = ctx->link_mask;

	for (i = 0; i < ctx->count; i++) {
		if (!(link_mask & BIT(i)))
			continue;

		ldev = ctx->ldev[i];

		pm_runtime_disable(&ldev->auxdev.dev);
		if (!ldev->link_res.clock_stop_quirks)
			pm_runtime_put_noidle(ldev->link_res.dev);

		intel_link_dev_unregister(ldev);
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
	struct sdw_intel_link_res *link;
	struct sdw_intel_link_dev *ldev;
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

	/*
	 * we need to alloc/free memory manually and can't use devm:
	 * this routine may be called from a workqueue, and not from
	 * the parent .probe.
	 * If devm_ was used, the memory might never be freed on errors.
	 */
	ctx = kzalloc(sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return NULL;

	ctx->count = count;

	/*
	 * allocate the array of pointers. The link-specific data is allocated
	 * as part of the first loop below and released with the auxiliary_device_uninit().
	 * If some links are disabled, the link pointer will remain NULL. Given that the
	 * number of links is small, this is simpler than using a list to keep track of links.
	 */
	ctx->ldev = kcalloc(ctx->count, sizeof(*ctx->ldev), GFP_KERNEL);
	if (!ctx->ldev) {
		kfree(ctx);
		return NULL;
	}

	ctx->mmio_base = res->mmio_base;
	ctx->shim_base = res->shim_base;
	ctx->alh_base = res->alh_base;
	ctx->link_mask = res->link_mask;
	ctx->handle = res->handle;
	mutex_init(&ctx->shim_lock);

	link_mask = ctx->link_mask;

	INIT_LIST_HEAD(&ctx->link_list);

	for (i = 0; i < count; i++) {
		if (!(link_mask & BIT(i)))
			continue;

		/*
		 * init and add a device for each link
		 *
		 * The name of the device will be soundwire_intel.link.[i],
		 * with the "soundwire_intel" module prefix automatically added
		 * by the auxiliary bus core.
		 */
		ldev = intel_link_dev_register(res,
					       ctx,
					       acpi_fwnode_handle(adev),
					       "link",
					       i);
		if (IS_ERR(ldev))
			goto err;

		link = &ldev->link_res;
		link->cdns = dev_get_drvdata(&ldev->auxdev.dev);

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

	ctx->ids = kcalloc(num_slaves, sizeof(*ctx->ids), GFP_KERNEL);
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
	while (i--) {
		if (!(link_mask & BIT(i)))
			continue;
		ldev = ctx->ldev[i];
		intel_link_dev_unregister(ldev);
	}
	kfree(ctx->ldev);
	kfree(ctx);
	return NULL;
}

static int
sdw_intel_startup_controller(struct sdw_intel_ctx *ctx)
{
	struct acpi_device *adev;
	struct sdw_intel_link_dev *ldev;
	u32 caps;
	u32 link_mask;
	int i;

	if (acpi_bus_get_device(ctx->handle, &adev))
		return -EINVAL;

	/* Check SNDWLCAP.LCOUNT */
	caps = ioread32(ctx->mmio_base + ctx->shim_base + SDW_SHIM_LCAP);
	caps &= GENMASK(2, 0);

	/* Check HW supported vs property value */
	if (caps < ctx->count) {
		dev_err(&adev->dev,
			"BIOS master count is larger than hardware capabilities\n");
		return -EINVAL;
	}

	if (!ctx->ldev)
		return -EINVAL;

	link_mask = ctx->link_mask;

	/* Startup SDW Master devices */
	for (i = 0; i < ctx->count; i++) {
		if (!(link_mask & BIT(i)))
			continue;

		ldev = ctx->ldev[i];

		intel_link_startup(&ldev->auxdev);

		if (!ldev->link_res.clock_stop_quirks) {
			/*
			 * we need to prevent the parent PCI device
			 * from entering pm_runtime suspend, so that
			 * power rails to the SoundWire IP are not
			 * turned off.
			 */
			pm_runtime_get_noresume(ldev->link_res.dev);
		}
	}

	return 0;
}

/**
 * sdw_intel_probe() - SoundWire Intel probe routine
 * @res: resource data
 *
 * This registers an auxiliary device for each Master handled by the controller,
 * and SoundWire Master and Slave devices will be created by the auxiliary
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
	kfree(ctx->ids);
	kfree(ctx->ldev);
	kfree(ctx);
}
EXPORT_SYMBOL_NS(sdw_intel_exit, SOUNDWIRE_INTEL_INIT);

void sdw_intel_process_wakeen_event(struct sdw_intel_ctx *ctx)
{
	struct sdw_intel_link_dev *ldev;
	u32 link_mask;
	int i;

	if (!ctx->ldev)
		return;

	link_mask = ctx->link_mask;

	/* Startup SDW Master devices */
	for (i = 0; i < ctx->count; i++) {
		if (!(link_mask & BIT(i)))
			continue;

		ldev = ctx->ldev[i];

		intel_link_process_wakeen_event(&ldev->auxdev);
	}
}
EXPORT_SYMBOL_NS(sdw_intel_process_wakeen_event, SOUNDWIRE_INTEL_INIT);

MODULE_LICENSE("Dual BSD/GPL");
MODULE_DESCRIPTION("Intel Soundwire Init Library");
