/*
 * drivers/base/power/common.c - Common device power management code.
 *
 * Copyright (C) 2011 Rafael J. Wysocki <rjw@sisk.pl>, Renesas Electronics Corp.
 *
 * This file is released under the GPLv2.
 */

#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/export.h>
#include <linux/slab.h>
#include <linux/pm_clock.h>
#include <linux/acpi.h>
#include <linux/pm_domain.h>

#include "power.h"

/**
 * dev_pm_get_subsys_data - Create or refcount power.subsys_data for device.
 * @dev: Device to handle.
 *
 * If power.subsys_data is NULL, point it to a new object, otherwise increment
 * its reference counter.  Return 0 if new object has been created or refcount
 * increased, otherwise negative error code.
 */
int dev_pm_get_subsys_data(struct device *dev)
{
	struct pm_subsys_data *psd;

	psd = kzalloc(sizeof(*psd), GFP_KERNEL);
	if (!psd)
		return -ENOMEM;

	spin_lock_irq(&dev->power.lock);

	if (dev->power.subsys_data) {
		dev->power.subsys_data->refcount++;
	} else {
		spin_lock_init(&psd->lock);
		psd->refcount = 1;
		dev->power.subsys_data = psd;
		pm_clk_init(dev);
		psd = NULL;
	}

	spin_unlock_irq(&dev->power.lock);

	/* kfree() verifies that its argument is nonzero. */
	kfree(psd);

	return 0;
}
EXPORT_SYMBOL_GPL(dev_pm_get_subsys_data);

/**
 * dev_pm_put_subsys_data - Drop reference to power.subsys_data.
 * @dev: Device to handle.
 *
 * If the reference counter of power.subsys_data is zero after dropping the
 * reference, power.subsys_data is removed.
 */
void dev_pm_put_subsys_data(struct device *dev)
{
	struct pm_subsys_data *psd;

	spin_lock_irq(&dev->power.lock);

	psd = dev_to_psd(dev);
	if (!psd)
		goto out;

	if (--psd->refcount == 0)
		dev->power.subsys_data = NULL;
	else
		psd = NULL;

 out:
	spin_unlock_irq(&dev->power.lock);
	kfree(psd);
}
EXPORT_SYMBOL_GPL(dev_pm_put_subsys_data);

/**
 * dev_pm_domain_attach - Attach a device to its PM domain.
 * @dev: Device to attach.
 * @power_on: Used to indicate whether we should power on the device.
 *
 * The @dev may only be attached to a single PM domain. By iterating through
 * the available alternatives we try to find a valid PM domain for the device.
 * As attachment succeeds, the ->detach() callback in the struct dev_pm_domain
 * should be assigned by the corresponding attach function.
 *
 * This function should typically be invoked from subsystem level code during
 * the probe phase. Especially for those that holds devices which requires
 * power management through PM domains.
 *
 * Callers must ensure proper synchronization of this function with power
 * management callbacks.
 *
 * Returns 0 on successfully attached PM domain, or when it is found that the
 * device doesn't need a PM domain, else a negative error code.
 */
int dev_pm_domain_attach(struct device *dev, bool power_on)
{
	int ret;

	if (dev->pm_domain)
		return 0;

	ret = acpi_dev_pm_attach(dev, power_on);
	if (!ret)
		ret = genpd_dev_pm_attach(dev);

	return ret < 0 ? ret : 0;
}
EXPORT_SYMBOL_GPL(dev_pm_domain_attach);

/**
 * dev_pm_domain_detach - Detach a device from its PM domain.
 * @dev: Device to detach.
 * @power_off: Used to indicate whether we should power off the device.
 *
 * This functions will reverse the actions from dev_pm_domain_attach() and thus
 * try to detach the @dev from its PM domain. Typically it should be invoked
 * from subsystem level code during the remove phase.
 *
 * Callers must ensure proper synchronization of this function with power
 * management callbacks.
 */
void dev_pm_domain_detach(struct device *dev, bool power_off)
{
	if (dev->pm_domain && dev->pm_domain->detach)
		dev->pm_domain->detach(dev, power_off);
}
EXPORT_SYMBOL_GPL(dev_pm_domain_detach);

/**
 * dev_pm_domain_set - Set PM domain of a device.
 * @dev: Device whose PM domain is to be set.
 * @pd: PM domain to be set, or NULL.
 *
 * Sets the PM domain the device belongs to. The PM domain of a device needs
 * to be set before its probe finishes (it's bound to a driver).
 *
 * This function must be called with the device lock held.
 */
void dev_pm_domain_set(struct device *dev, struct dev_pm_domain *pd)
{
	if (dev->pm_domain == pd)
		return;

	WARN(pd && device_is_bound(dev),
	     "PM domains can only be changed for unbound devices\n");
	dev->pm_domain = pd;
	device_pm_check_callbacks(dev);
}
EXPORT_SYMBOL_GPL(dev_pm_domain_set);
