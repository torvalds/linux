// SPDX-License-Identifier: GPL-2.0
/*
 * drivers/base/power/common.c - Common device power management code.
 *
 * Copyright (C) 2011 Rafael J. Wysocki <rjw@sisk.pl>, Renesas Electronics Corp.
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
 * dev_pm_domain_attach_by_id - Associate a device with one of its PM domains.
 * @dev: The device used to lookup the PM domain.
 * @index: The index of the PM domain.
 *
 * As @dev may only be attached to a single PM domain, the backend PM domain
 * provider creates a virtual device to attach instead. If attachment succeeds,
 * the ->detach() callback in the struct dev_pm_domain are assigned by the
 * corresponding backend attach function, as to deal with detaching of the
 * created virtual device.
 *
 * This function should typically be invoked by a driver during the probe phase,
 * in case its device requires power management through multiple PM domains. The
 * driver may benefit from using the received device, to configure device-links
 * towards its original device. Depending on the use-case and if needed, the
 * links may be dynamically changed by the driver, which allows it to control
 * the power to the PM domains independently from each other.
 *
 * Callers must ensure proper synchronization of this function with power
 * management callbacks.
 *
 * Returns the virtual created device when successfully attached to its PM
 * domain, NULL in case @dev don't need a PM domain, else an ERR_PTR().
 * Note that, to detach the returned virtual device, the driver shall call
 * dev_pm_domain_detach() on it, typically during the remove phase.
 */
struct device *dev_pm_domain_attach_by_id(struct device *dev,
					  unsigned int index)
{
	if (dev->pm_domain)
		return ERR_PTR(-EEXIST);

	return genpd_dev_pm_attach_by_id(dev, index);
}
EXPORT_SYMBOL_GPL(dev_pm_domain_attach_by_id);

/**
 * dev_pm_domain_attach_by_name - Associate a device with one of its PM domains.
 * @dev: The device used to lookup the PM domain.
 * @name: The name of the PM domain.
 *
 * For a detailed function description, see dev_pm_domain_attach_by_id().
 */
struct device *dev_pm_domain_attach_by_name(struct device *dev,
					    const char *name)
{
	if (dev->pm_domain)
		return ERR_PTR(-EEXIST);

	return genpd_dev_pm_attach_by_name(dev, name);
}
EXPORT_SYMBOL_GPL(dev_pm_domain_attach_by_name);

/**
 * dev_pm_domain_detach - Detach a device from its PM domain.
 * @dev: Device to detach.
 * @power_off: Used to indicate whether we should power off the device.
 *
 * This functions will reverse the actions from dev_pm_domain_attach() and
 * dev_pm_domain_attach_by_id(), thus it detaches @dev from its PM domain.
 * Typically it should be invoked during the remove phase, either from
 * subsystem level code or from drivers.
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
 * dev_pm_domain_start - Start the device through its PM domain.
 * @dev: Device to start.
 *
 * This function should typically be called during probe by a subsystem/driver,
 * when it needs to start its device from the PM domain's perspective. Note
 * that, it's assumed that the PM domain is already powered on when this
 * function is called.
 *
 * Returns 0 on success and negative error values on failures.
 */
int dev_pm_domain_start(struct device *dev)
{
	if (dev->pm_domain && dev->pm_domain->start)
		return dev->pm_domain->start(dev);

	return 0;
}
EXPORT_SYMBOL_GPL(dev_pm_domain_start);

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
