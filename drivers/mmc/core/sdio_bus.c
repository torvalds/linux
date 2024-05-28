// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  linux/drivers/mmc/core/sdio_bus.c
 *
 *  Copyright 2007 Pierre Ossman
 *
 * SDIO function driver model
 */

#include <linux/device.h>
#include <linux/err.h>
#include <linux/export.h>
#include <linux/slab.h>
#include <linux/pm_runtime.h>
#include <linux/pm_domain.h>
#include <linux/acpi.h>
#include <linux/sysfs.h>

#include <linux/mmc/card.h>
#include <linux/mmc/host.h>
#include <linux/mmc/sdio_func.h>
#include <linux/of.h>

#include "core.h"
#include "card.h"
#include "sdio_cis.h"
#include "sdio_bus.h"

#define to_sdio_driver(d)	container_of(d, struct sdio_driver, drv)

/* show configuration fields */
#define sdio_config_attr(field, format_string, args...)			\
static ssize_t								\
field##_show(struct device *dev, struct device_attribute *attr, char *buf)				\
{									\
	struct sdio_func *func;						\
									\
	func = dev_to_sdio_func (dev);					\
	return sysfs_emit(buf, format_string, args);			\
}									\
static DEVICE_ATTR_RO(field)

sdio_config_attr(class, "0x%02x\n", func->class);
sdio_config_attr(vendor, "0x%04x\n", func->vendor);
sdio_config_attr(device, "0x%04x\n", func->device);
sdio_config_attr(revision, "%u.%u\n", func->major_rev, func->minor_rev);
sdio_config_attr(modalias, "sdio:c%02Xv%04Xd%04X\n", func->class, func->vendor, func->device);

#define sdio_info_attr(num)									\
static ssize_t info##num##_show(struct device *dev, struct device_attribute *attr, char *buf)	\
{												\
	struct sdio_func *func = dev_to_sdio_func(dev);						\
												\
	if (num > func->num_info)								\
		return -ENODATA;								\
	if (!func->info[num - 1][0])								\
		return 0;									\
	return sysfs_emit(buf, "%s\n", func->info[num - 1]);					\
}												\
static DEVICE_ATTR_RO(info##num)

sdio_info_attr(1);
sdio_info_attr(2);
sdio_info_attr(3);
sdio_info_attr(4);

static struct attribute *sdio_dev_attrs[] = {
	&dev_attr_class.attr,
	&dev_attr_vendor.attr,
	&dev_attr_device.attr,
	&dev_attr_revision.attr,
	&dev_attr_info1.attr,
	&dev_attr_info2.attr,
	&dev_attr_info3.attr,
	&dev_attr_info4.attr,
	&dev_attr_modalias.attr,
	NULL,
};
ATTRIBUTE_GROUPS(sdio_dev);

static const struct sdio_device_id *sdio_match_one(struct sdio_func *func,
	const struct sdio_device_id *id)
{
	if (id->class != (__u8)SDIO_ANY_ID && id->class != func->class)
		return NULL;
	if (id->vendor != (__u16)SDIO_ANY_ID && id->vendor != func->vendor)
		return NULL;
	if (id->device != (__u16)SDIO_ANY_ID && id->device != func->device)
		return NULL;
	return id;
}

static const struct sdio_device_id *sdio_match_device(struct sdio_func *func,
	struct sdio_driver *sdrv)
{
	const struct sdio_device_id *ids;

	ids = sdrv->id_table;

	if (ids) {
		while (ids->class || ids->vendor || ids->device) {
			if (sdio_match_one(func, ids))
				return ids;
			ids++;
		}
	}

	return NULL;
}

static int sdio_bus_match(struct device *dev, struct device_driver *drv)
{
	struct sdio_func *func = dev_to_sdio_func(dev);
	struct sdio_driver *sdrv = to_sdio_driver(drv);

	if (sdio_match_device(func, sdrv))
		return 1;

	return 0;
}

static int
sdio_bus_uevent(const struct device *dev, struct kobj_uevent_env *env)
{
	const struct sdio_func *func = dev_to_sdio_func(dev);
	unsigned int i;

	if (add_uevent_var(env,
			"SDIO_CLASS=%02X", func->class))
		return -ENOMEM;

	if (add_uevent_var(env, 
			"SDIO_ID=%04X:%04X", func->vendor, func->device))
		return -ENOMEM;

	if (add_uevent_var(env,
			"SDIO_REVISION=%u.%u", func->major_rev, func->minor_rev))
		return -ENOMEM;

	for (i = 0; i < func->num_info; i++) {
		if (add_uevent_var(env, "SDIO_INFO%u=%s", i+1, func->info[i]))
			return -ENOMEM;
	}

	if (add_uevent_var(env,
			"MODALIAS=sdio:c%02Xv%04Xd%04X",
			func->class, func->vendor, func->device))
		return -ENOMEM;

	return 0;
}

static int sdio_bus_probe(struct device *dev)
{
	struct sdio_driver *drv = to_sdio_driver(dev->driver);
	struct sdio_func *func = dev_to_sdio_func(dev);
	const struct sdio_device_id *id;
	int ret;

	id = sdio_match_device(func, drv);
	if (!id)
		return -ENODEV;

	ret = dev_pm_domain_attach(dev, false);
	if (ret)
		return ret;

	atomic_inc(&func->card->sdio_funcs_probed);

	/* Unbound SDIO functions are always suspended.
	 * During probe, the function is set active and the usage count
	 * is incremented.  If the driver supports runtime PM,
	 * it should call pm_runtime_put_noidle() in its probe routine and
	 * pm_runtime_get_noresume() in its remove routine.
	 */
	if (func->card->host->caps & MMC_CAP_POWER_OFF_CARD) {
		ret = pm_runtime_get_sync(dev);
		if (ret < 0)
			goto disable_runtimepm;
	}

	/* Set the default block size so the driver is sure it's something
	 * sensible. */
	sdio_claim_host(func);
	if (mmc_card_removed(func->card))
		ret = -ENOMEDIUM;
	else
		ret = sdio_set_block_size(func, 0);
	sdio_release_host(func);
	if (ret)
		goto disable_runtimepm;

	ret = drv->probe(func, id);
	if (ret)
		goto disable_runtimepm;

	return 0;

disable_runtimepm:
	atomic_dec(&func->card->sdio_funcs_probed);
	if (func->card->host->caps & MMC_CAP_POWER_OFF_CARD)
		pm_runtime_put_noidle(dev);
	dev_pm_domain_detach(dev, false);
	return ret;
}

static void sdio_bus_remove(struct device *dev)
{
	struct sdio_driver *drv = to_sdio_driver(dev->driver);
	struct sdio_func *func = dev_to_sdio_func(dev);

	/* Make sure card is powered before invoking ->remove() */
	if (func->card->host->caps & MMC_CAP_POWER_OFF_CARD)
		pm_runtime_get_sync(dev);

	drv->remove(func);
	atomic_dec(&func->card->sdio_funcs_probed);

	if (func->irq_handler) {
		pr_warn("WARNING: driver %s did not remove its interrupt handler!\n",
			drv->name);
		sdio_claim_host(func);
		sdio_release_irq(func);
		sdio_release_host(func);
	}

	/* First, undo the increment made directly above */
	if (func->card->host->caps & MMC_CAP_POWER_OFF_CARD)
		pm_runtime_put_noidle(dev);

	/* Then undo the runtime PM settings in sdio_bus_probe() */
	if (func->card->host->caps & MMC_CAP_POWER_OFF_CARD)
		pm_runtime_put_sync(dev);

	dev_pm_domain_detach(dev, false);
}

static const struct dev_pm_ops sdio_bus_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(pm_generic_suspend, pm_generic_resume)
	SET_RUNTIME_PM_OPS(
		pm_generic_runtime_suspend,
		pm_generic_runtime_resume,
		NULL
	)
};

static const struct bus_type sdio_bus_type = {
	.name		= "sdio",
	.dev_groups	= sdio_dev_groups,
	.match		= sdio_bus_match,
	.uevent		= sdio_bus_uevent,
	.probe		= sdio_bus_probe,
	.remove		= sdio_bus_remove,
	.pm		= &sdio_bus_pm_ops,
};

int sdio_register_bus(void)
{
	return bus_register(&sdio_bus_type);
}

void sdio_unregister_bus(void)
{
	bus_unregister(&sdio_bus_type);
}

/**
 *	__sdio_register_driver - register a function driver
 *	@drv: SDIO function driver
 *	@owner: owning module/driver
 */
int __sdio_register_driver(struct sdio_driver *drv, struct module *owner)
{
	drv->drv.name = drv->name;
	drv->drv.bus = &sdio_bus_type;
	drv->drv.owner = owner;

	return driver_register(&drv->drv);
}
EXPORT_SYMBOL_GPL(__sdio_register_driver);

/**
 *	sdio_unregister_driver - unregister a function driver
 *	@drv: SDIO function driver
 */
void sdio_unregister_driver(struct sdio_driver *drv)
{
	drv->drv.bus = &sdio_bus_type;
	driver_unregister(&drv->drv);
}
EXPORT_SYMBOL_GPL(sdio_unregister_driver);

static void sdio_release_func(struct device *dev)
{
	struct sdio_func *func = dev_to_sdio_func(dev);

	if (!(func->card->quirks & MMC_QUIRK_NONSTD_SDIO))
		sdio_free_func_cis(func);

	/*
	 * We have now removed the link to the tuples in the
	 * card structure, so remove the reference.
	 */
	put_device(&func->card->dev);

	kfree(func->info);
	kfree(func->tmpbuf);
	kfree(func);
}

/*
 * Allocate and initialise a new SDIO function structure.
 */
struct sdio_func *sdio_alloc_func(struct mmc_card *card)
{
	struct sdio_func *func;

	func = kzalloc(sizeof(struct sdio_func), GFP_KERNEL);
	if (!func)
		return ERR_PTR(-ENOMEM);

	/*
	 * allocate buffer separately to make sure it's properly aligned for
	 * DMA usage (incl. 64 bit DMA)
	 */
	func->tmpbuf = kmalloc(4, GFP_KERNEL);
	if (!func->tmpbuf) {
		kfree(func);
		return ERR_PTR(-ENOMEM);
	}

	func->card = card;

	device_initialize(&func->dev);

	/*
	 * We may link to tuples in the card structure,
	 * we need make sure we have a reference to it.
	 */
	get_device(&func->card->dev);

	func->dev.parent = &card->dev;
	func->dev.bus = &sdio_bus_type;
	func->dev.release = sdio_release_func;

	return func;
}

#ifdef CONFIG_ACPI
static void sdio_acpi_set_handle(struct sdio_func *func)
{
	struct mmc_host *host = func->card->host;
	u64 addr = ((u64)host->slotno << 16) | func->num;

	acpi_preset_companion(&func->dev, ACPI_COMPANION(host->parent), addr);
}
#else
static inline void sdio_acpi_set_handle(struct sdio_func *func) {}
#endif

static void sdio_set_of_node(struct sdio_func *func)
{
	struct mmc_host *host = func->card->host;

	func->dev.of_node = mmc_of_find_child_device(host, func->num);
}

/*
 * Register a new SDIO function with the driver model.
 */
int sdio_add_func(struct sdio_func *func)
{
	int ret;

	dev_set_name(&func->dev, "%s:%d", mmc_card_id(func->card), func->num);

	sdio_set_of_node(func);
	sdio_acpi_set_handle(func);
	device_enable_async_suspend(&func->dev);
	ret = device_add(&func->dev);
	if (ret == 0)
		sdio_func_set_present(func);

	return ret;
}

/*
 * Unregister a SDIO function with the driver model, and
 * (eventually) free it.
 * This function can be called through error paths where sdio_add_func() was
 * never executed (because a failure occurred at an earlier point).
 */
void sdio_remove_func(struct sdio_func *func)
{
	if (sdio_func_present(func))
		device_del(&func->dev);

	of_node_put(func->dev.of_node);
	put_device(&func->dev);
}

