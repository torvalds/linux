// SPDX-License-Identifier: GPL-2.0
/*
 * (C) Copyright 2002-2004, 2007 Greg Kroah-Hartman <greg@kroah.com>
 * (C) Copyright 2007 Novell Inc.
 */

#include <linux/pci.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/mempolicy.h>
#include <linux/string.h>
#include <linux/slab.h>
#include <linux/sched.h>
#include <linux/sched/isolation.h>
#include <linux/cpu.h>
#include <linux/pm_runtime.h>
#include <linux/suspend.h>
#include <linux/kexec.h>
#include <linux/of_device.h>
#include <linux/acpi.h>
#include <linux/dma-map-ops.h>
#include <linux/iommu.h>
#include "pci.h"
#include "pcie/portdrv.h"

struct pci_dynid {
	struct list_head node;
	struct pci_device_id id;
};

/**
 * pci_add_dynid - add a new PCI device ID to this driver and re-probe devices
 * @drv: target pci driver
 * @vendor: PCI vendor ID
 * @device: PCI device ID
 * @subvendor: PCI subvendor ID
 * @subdevice: PCI subdevice ID
 * @class: PCI class
 * @class_mask: PCI class mask
 * @driver_data: private driver data
 *
 * Adds a new dynamic pci device ID to this driver and causes the
 * driver to probe for all devices again.  @drv must have been
 * registered prior to calling this function.
 *
 * CONTEXT:
 * Does GFP_KERNEL allocation.
 *
 * RETURNS:
 * 0 on success, -errno on failure.
 */
int pci_add_dynid(struct pci_driver *drv,
		  unsigned int vendor, unsigned int device,
		  unsigned int subvendor, unsigned int subdevice,
		  unsigned int class, unsigned int class_mask,
		  unsigned long driver_data)
{
	struct pci_dynid *dynid;

	dynid = kzalloc(sizeof(*dynid), GFP_KERNEL);
	if (!dynid)
		return -ENOMEM;

	dynid->id.vendor = vendor;
	dynid->id.device = device;
	dynid->id.subvendor = subvendor;
	dynid->id.subdevice = subdevice;
	dynid->id.class = class;
	dynid->id.class_mask = class_mask;
	dynid->id.driver_data = driver_data;

	spin_lock(&drv->dynids.lock);
	list_add_tail(&dynid->node, &drv->dynids.list);
	spin_unlock(&drv->dynids.lock);

	return driver_attach(&drv->driver);
}
EXPORT_SYMBOL_GPL(pci_add_dynid);

static void pci_free_dynids(struct pci_driver *drv)
{
	struct pci_dynid *dynid, *n;

	spin_lock(&drv->dynids.lock);
	list_for_each_entry_safe(dynid, n, &drv->dynids.list, node) {
		list_del(&dynid->node);
		kfree(dynid);
	}
	spin_unlock(&drv->dynids.lock);
}

/**
 * pci_match_id - See if a PCI device matches a given pci_id table
 * @ids: array of PCI device ID structures to search in
 * @dev: the PCI device structure to match against.
 *
 * Used by a driver to check whether a PCI device is in its list of
 * supported devices.  Returns the matching pci_device_id structure or
 * %NULL if there is no match.
 *
 * Deprecated; don't use this as it will not catch any dynamic IDs
 * that a driver might want to check for.
 */
const struct pci_device_id *pci_match_id(const struct pci_device_id *ids,
					 struct pci_dev *dev)
{
	if (ids) {
		while (ids->vendor || ids->subvendor || ids->class_mask) {
			if (pci_match_one_device(ids, dev))
				return ids;
			ids++;
		}
	}
	return NULL;
}
EXPORT_SYMBOL(pci_match_id);

static const struct pci_device_id pci_device_id_any = {
	.vendor = PCI_ANY_ID,
	.device = PCI_ANY_ID,
	.subvendor = PCI_ANY_ID,
	.subdevice = PCI_ANY_ID,
};

/**
 * pci_match_device - See if a device matches a driver's list of IDs
 * @drv: the PCI driver to match against
 * @dev: the PCI device structure to match against
 *
 * Used by a driver to check whether a PCI device is in its list of
 * supported devices or in the dynids list, which may have been augmented
 * via the sysfs "new_id" file.  Returns the matching pci_device_id
 * structure or %NULL if there is no match.
 */
static const struct pci_device_id *pci_match_device(struct pci_driver *drv,
						    struct pci_dev *dev)
{
	struct pci_dynid *dynid;
	const struct pci_device_id *found_id = NULL, *ids;

	/* When driver_override is set, only bind to the matching driver */
	if (dev->driver_override && strcmp(dev->driver_override, drv->name))
		return NULL;

	/* Look at the dynamic ids first, before the static ones */
	spin_lock(&drv->dynids.lock);
	list_for_each_entry(dynid, &drv->dynids.list, node) {
		if (pci_match_one_device(&dynid->id, dev)) {
			found_id = &dynid->id;
			break;
		}
	}
	spin_unlock(&drv->dynids.lock);

	if (found_id)
		return found_id;

	for (ids = drv->id_table; (found_id = pci_match_id(ids, dev));
	     ids = found_id + 1) {
		/*
		 * The match table is split based on driver_override.
		 * In case override_only was set, enforce driver_override
		 * matching.
		 */
		if (found_id->override_only) {
			if (dev->driver_override)
				return found_id;
		} else {
			return found_id;
		}
	}

	/* driver_override will always match, send a dummy id */
	if (dev->driver_override)
		return &pci_device_id_any;
	return NULL;
}

/**
 * new_id_store - sysfs frontend to pci_add_dynid()
 * @driver: target device driver
 * @buf: buffer for scanning device ID data
 * @count: input size
 *
 * Allow PCI IDs to be added to an existing driver via sysfs.
 */
static ssize_t new_id_store(struct device_driver *driver, const char *buf,
			    size_t count)
{
	struct pci_driver *pdrv = to_pci_driver(driver);
	const struct pci_device_id *ids = pdrv->id_table;
	u32 vendor, device, subvendor = PCI_ANY_ID,
		subdevice = PCI_ANY_ID, class = 0, class_mask = 0;
	unsigned long driver_data = 0;
	int fields = 0;
	int retval = 0;

	fields = sscanf(buf, "%x %x %x %x %x %x %lx",
			&vendor, &device, &subvendor, &subdevice,
			&class, &class_mask, &driver_data);
	if (fields < 2)
		return -EINVAL;

	if (fields != 7) {
		struct pci_dev *pdev = kzalloc(sizeof(*pdev), GFP_KERNEL);
		if (!pdev)
			return -ENOMEM;

		pdev->vendor = vendor;
		pdev->device = device;
		pdev->subsystem_vendor = subvendor;
		pdev->subsystem_device = subdevice;
		pdev->class = class;

		if (pci_match_device(pdrv, pdev))
			retval = -EEXIST;

		kfree(pdev);

		if (retval)
			return retval;
	}

	/* Only accept driver_data values that match an existing id_table
	   entry */
	if (ids) {
		retval = -EINVAL;
		while (ids->vendor || ids->subvendor || ids->class_mask) {
			if (driver_data == ids->driver_data) {
				retval = 0;
				break;
			}
			ids++;
		}
		if (retval)	/* No match */
			return retval;
	}

	retval = pci_add_dynid(pdrv, vendor, device, subvendor, subdevice,
			       class, class_mask, driver_data);
	if (retval)
		return retval;
	return count;
}
static DRIVER_ATTR_WO(new_id);

/**
 * remove_id_store - remove a PCI device ID from this driver
 * @driver: target device driver
 * @buf: buffer for scanning device ID data
 * @count: input size
 *
 * Removes a dynamic pci device ID to this driver.
 */
static ssize_t remove_id_store(struct device_driver *driver, const char *buf,
			       size_t count)
{
	struct pci_dynid *dynid, *n;
	struct pci_driver *pdrv = to_pci_driver(driver);
	u32 vendor, device, subvendor = PCI_ANY_ID,
		subdevice = PCI_ANY_ID, class = 0, class_mask = 0;
	int fields = 0;
	size_t retval = -ENODEV;

	fields = sscanf(buf, "%x %x %x %x %x %x",
			&vendor, &device, &subvendor, &subdevice,
			&class, &class_mask);
	if (fields < 2)
		return -EINVAL;

	spin_lock(&pdrv->dynids.lock);
	list_for_each_entry_safe(dynid, n, &pdrv->dynids.list, node) {
		struct pci_device_id *id = &dynid->id;
		if ((id->vendor == vendor) &&
		    (id->device == device) &&
		    (subvendor == PCI_ANY_ID || id->subvendor == subvendor) &&
		    (subdevice == PCI_ANY_ID || id->subdevice == subdevice) &&
		    !((id->class ^ class) & class_mask)) {
			list_del(&dynid->node);
			kfree(dynid);
			retval = count;
			break;
		}
	}
	spin_unlock(&pdrv->dynids.lock);

	return retval;
}
static DRIVER_ATTR_WO(remove_id);

static struct attribute *pci_drv_attrs[] = {
	&driver_attr_new_id.attr,
	&driver_attr_remove_id.attr,
	NULL,
};
ATTRIBUTE_GROUPS(pci_drv);

struct drv_dev_and_id {
	struct pci_driver *drv;
	struct pci_dev *dev;
	const struct pci_device_id *id;
};

static long local_pci_probe(void *_ddi)
{
	struct drv_dev_and_id *ddi = _ddi;
	struct pci_dev *pci_dev = ddi->dev;
	struct pci_driver *pci_drv = ddi->drv;
	struct device *dev = &pci_dev->dev;
	int rc;

	/*
	 * Unbound PCI devices are always put in D0, regardless of
	 * runtime PM status.  During probe, the device is set to
	 * active and the usage count is incremented.  If the driver
	 * supports runtime PM, it should call pm_runtime_put_noidle(),
	 * or any other runtime PM helper function decrementing the usage
	 * count, in its probe routine and pm_runtime_get_noresume() in
	 * its remove routine.
	 */
	pm_runtime_get_sync(dev);
	pci_dev->driver = pci_drv;
	rc = pci_drv->probe(pci_dev, ddi->id);
	if (!rc)
		return rc;
	if (rc < 0) {
		pci_dev->driver = NULL;
		pm_runtime_put_sync(dev);
		return rc;
	}
	/*
	 * Probe function should return < 0 for failure, 0 for success
	 * Treat values > 0 as success, but warn.
	 */
	pci_warn(pci_dev, "Driver probe function unexpectedly returned %d\n",
		 rc);
	return 0;
}

static bool pci_physfn_is_probed(struct pci_dev *dev)
{
#ifdef CONFIG_PCI_IOV
	return dev->is_virtfn && dev->physfn->is_probed;
#else
	return false;
#endif
}

static int pci_call_probe(struct pci_driver *drv, struct pci_dev *dev,
			  const struct pci_device_id *id)
{
	int error, node, cpu;
	struct drv_dev_and_id ddi = { drv, dev, id };

	/*
	 * Execute driver initialization on node where the device is
	 * attached.  This way the driver likely allocates its local memory
	 * on the right node.
	 */
	node = dev_to_node(&dev->dev);
	dev->is_probed = 1;

	cpu_hotplug_disable();

	/*
	 * Prevent nesting work_on_cpu() for the case where a Virtual Function
	 * device is probed from work_on_cpu() of the Physical device.
	 */
	if (node < 0 || node >= MAX_NUMNODES || !node_online(node) ||
	    pci_physfn_is_probed(dev)) {
		cpu = nr_cpu_ids;
	} else {
		cpumask_var_t wq_domain_mask;

		if (!zalloc_cpumask_var(&wq_domain_mask, GFP_KERNEL)) {
			error = -ENOMEM;
			goto out;
		}
		cpumask_and(wq_domain_mask,
			    housekeeping_cpumask(HK_TYPE_WQ),
			    housekeeping_cpumask(HK_TYPE_DOMAIN));

		cpu = cpumask_any_and(cpumask_of_node(node),
				      wq_domain_mask);
		free_cpumask_var(wq_domain_mask);
	}

	if (cpu < nr_cpu_ids)
		error = work_on_cpu(cpu, local_pci_probe, &ddi);
	else
		error = local_pci_probe(&ddi);
out:
	dev->is_probed = 0;
	cpu_hotplug_enable();
	return error;
}

/**
 * __pci_device_probe - check if a driver wants to claim a specific PCI device
 * @drv: driver to call to check if it wants the PCI device
 * @pci_dev: PCI device being probed
 *
 * returns 0 on success, else error.
 * side-effect: pci_dev->driver is set to drv when drv claims pci_dev.
 */
static int __pci_device_probe(struct pci_driver *drv, struct pci_dev *pci_dev)
{
	const struct pci_device_id *id;
	int error = 0;

	if (drv->probe) {
		error = -ENODEV;

		id = pci_match_device(drv, pci_dev);
		if (id)
			error = pci_call_probe(drv, pci_dev, id);
	}
	return error;
}

int __weak pcibios_alloc_irq(struct pci_dev *dev)
{
	return 0;
}

void __weak pcibios_free_irq(struct pci_dev *dev)
{
}

#ifdef CONFIG_PCI_IOV
static inline bool pci_device_can_probe(struct pci_dev *pdev)
{
	return (!pdev->is_virtfn || pdev->physfn->sriov->drivers_autoprobe ||
		pdev->driver_override);
}
#else
static inline bool pci_device_can_probe(struct pci_dev *pdev)
{
	return true;
}
#endif

static int pci_device_probe(struct device *dev)
{
	int error;
	struct pci_dev *pci_dev = to_pci_dev(dev);
	struct pci_driver *drv = to_pci_driver(dev->driver);

	if (!pci_device_can_probe(pci_dev))
		return -ENODEV;

	pci_assign_irq(pci_dev);

	error = pcibios_alloc_irq(pci_dev);
	if (error < 0)
		return error;

	pci_dev_get(pci_dev);
	error = __pci_device_probe(drv, pci_dev);
	if (error) {
		pcibios_free_irq(pci_dev);
		pci_dev_put(pci_dev);
	}

	return error;
}

static void pci_device_remove(struct device *dev)
{
	struct pci_dev *pci_dev = to_pci_dev(dev);
	struct pci_driver *drv = pci_dev->driver;

	if (drv->remove) {
		pm_runtime_get_sync(dev);
		drv->remove(pci_dev);
		pm_runtime_put_noidle(dev);
	}
	pcibios_free_irq(pci_dev);
	pci_dev->driver = NULL;
	pci_iov_remove(pci_dev);

	/* Undo the runtime PM settings in local_pci_probe() */
	pm_runtime_put_sync(dev);

	/*
	 * If the device is still on, set the power state as "unknown",
	 * since it might change by the next time we load the driver.
	 */
	if (pci_dev->current_state == PCI_D0)
		pci_dev->current_state = PCI_UNKNOWN;

	/*
	 * We would love to complain here if pci_dev->is_enabled is set, that
	 * the driver should have called pci_disable_device(), but the
	 * unfortunate fact is there are too many odd BIOS and bridge setups
	 * that don't like drivers doing that all of the time.
	 * Oh well, we can dream of sane hardware when we sleep, no matter how
	 * horrible the crap we have to deal with is when we are awake...
	 */

	pci_dev_put(pci_dev);
}

static void pci_device_shutdown(struct device *dev)
{
	struct pci_dev *pci_dev = to_pci_dev(dev);
	struct pci_driver *drv = pci_dev->driver;

	pm_runtime_resume(dev);

	if (drv && drv->shutdown)
		drv->shutdown(pci_dev);

	/*
	 * If this is a kexec reboot, turn off Bus Master bit on the
	 * device to tell it to not continue to do DMA. Don't touch
	 * devices in D3cold or unknown states.
	 * If it is not a kexec reboot, firmware will hit the PCI
	 * devices with big hammer and stop their DMA any way.
	 */
	if (kexec_in_progress && (pci_dev->current_state <= PCI_D3hot))
		pci_clear_master(pci_dev);
}

#ifdef CONFIG_PM_SLEEP

/* Auxiliary functions used for system resume */

/**
 * pci_restore_standard_config - restore standard config registers of PCI device
 * @pci_dev: PCI device to handle
 */
static int pci_restore_standard_config(struct pci_dev *pci_dev)
{
	pci_update_current_state(pci_dev, PCI_UNKNOWN);

	if (pci_dev->current_state != PCI_D0) {
		int error = pci_set_power_state(pci_dev, PCI_D0);
		if (error)
			return error;
	}

	pci_restore_state(pci_dev);
	pci_pme_restore(pci_dev);
	return 0;
}
#endif /* CONFIG_PM_SLEEP */

#ifdef CONFIG_PM

/* Auxiliary functions used for system resume and run-time resume */

static void pci_pm_default_resume(struct pci_dev *pci_dev)
{
	pci_fixup_device(pci_fixup_resume, pci_dev);
	pci_enable_wake(pci_dev, PCI_D0, false);
}

static void pci_pm_power_up_and_verify_state(struct pci_dev *pci_dev)
{
	pci_power_up(pci_dev);
	pci_update_current_state(pci_dev, PCI_D0);
}

static void pci_pm_default_resume_early(struct pci_dev *pci_dev)
{
	pci_pm_power_up_and_verify_state(pci_dev);
	pci_restore_state(pci_dev);
	pci_pme_restore(pci_dev);
}

static void pci_pm_bridge_power_up_actions(struct pci_dev *pci_dev)
{
	pci_bridge_wait_for_secondary_bus(pci_dev, "resume");

	/*
	 * When powering on a bridge from D3cold, the whole hierarchy may be
	 * powered on into D0uninitialized state, resume them to give them a
	 * chance to suspend again
	 */
	pci_resume_bus(pci_dev->subordinate);
}

#endif /* CONFIG_PM */

#ifdef CONFIG_PM_SLEEP

/*
 * Default "suspend" method for devices that have no driver provided suspend,
 * or not even a driver at all (second part).
 */
static void pci_pm_set_unknown_state(struct pci_dev *pci_dev)
{
	/*
	 * mark its power state as "unknown", since we don't know if
	 * e.g. the BIOS will change its device state when we suspend.
	 */
	if (pci_dev->current_state == PCI_D0)
		pci_dev->current_state = PCI_UNKNOWN;
}

/*
 * Default "resume" method for devices that have no driver provided resume,
 * or not even a driver at all (second part).
 */
static int pci_pm_reenable_device(struct pci_dev *pci_dev)
{
	int retval;

	/* if the device was enabled before suspend, re-enable */
	retval = pci_reenable_device(pci_dev);
	/*
	 * if the device was busmaster before the suspend, make it busmaster
	 * again
	 */
	if (pci_dev->is_busmaster)
		pci_set_master(pci_dev);

	return retval;
}

static int pci_legacy_suspend(struct device *dev, pm_message_t state)
{
	struct pci_dev *pci_dev = to_pci_dev(dev);
	struct pci_driver *drv = pci_dev->driver;

	if (drv && drv->suspend) {
		pci_power_t prev = pci_dev->current_state;
		int error;

		error = drv->suspend(pci_dev, state);
		suspend_report_result(dev, drv->suspend, error);
		if (error)
			return error;

		if (!pci_dev->state_saved && pci_dev->current_state != PCI_D0
		    && pci_dev->current_state != PCI_UNKNOWN) {
			pci_WARN_ONCE(pci_dev, pci_dev->current_state != prev,
				      "PCI PM: Device state not saved by %pS\n",
				      drv->suspend);
		}
	}

	pci_fixup_device(pci_fixup_suspend, pci_dev);

	return 0;
}

static int pci_legacy_suspend_late(struct device *dev)
{
	struct pci_dev *pci_dev = to_pci_dev(dev);

	if (!pci_dev->state_saved)
		pci_save_state(pci_dev);

	pci_pm_set_unknown_state(pci_dev);

	pci_fixup_device(pci_fixup_suspend_late, pci_dev);

	return 0;
}

static int pci_legacy_resume(struct device *dev)
{
	struct pci_dev *pci_dev = to_pci_dev(dev);
	struct pci_driver *drv = pci_dev->driver;

	pci_fixup_device(pci_fixup_resume, pci_dev);

	return drv && drv->resume ?
			drv->resume(pci_dev) : pci_pm_reenable_device(pci_dev);
}

/* Auxiliary functions used by the new power management framework */

static void pci_pm_default_suspend(struct pci_dev *pci_dev)
{
	/* Disable non-bridge devices without PM support */
	if (!pci_has_subordinate(pci_dev))
		pci_disable_enabled_device(pci_dev);
}

static bool pci_has_legacy_pm_support(struct pci_dev *pci_dev)
{
	struct pci_driver *drv = pci_dev->driver;
	bool ret = drv && (drv->suspend || drv->resume);

	/*
	 * Legacy PM support is used by default, so warn if the new framework is
	 * supported as well.  Drivers are supposed to support either the
	 * former, or the latter, but not both at the same time.
	 */
	pci_WARN(pci_dev, ret && drv->driver.pm, "device %04x:%04x\n",
		 pci_dev->vendor, pci_dev->device);

	return ret;
}

/* New power management framework */

static int pci_pm_prepare(struct device *dev)
{
	struct pci_dev *pci_dev = to_pci_dev(dev);
	const struct dev_pm_ops *pm = dev->driver ? dev->driver->pm : NULL;

	if (pm && pm->prepare) {
		int error = pm->prepare(dev);
		if (error < 0)
			return error;

		if (!error && dev_pm_test_driver_flags(dev, DPM_FLAG_SMART_PREPARE))
			return 0;
	}
	if (pci_dev_need_resume(pci_dev))
		return 0;

	/*
	 * The PME setting needs to be adjusted here in case the direct-complete
	 * optimization is used with respect to this device.
	 */
	pci_dev_adjust_pme(pci_dev);
	return 1;
}

static void pci_pm_complete(struct device *dev)
{
	struct pci_dev *pci_dev = to_pci_dev(dev);

	pci_dev_complete_resume(pci_dev);
	pm_generic_complete(dev);

	/* Resume device if platform firmware has put it in reset-power-on */
	if (pm_runtime_suspended(dev) && pm_resume_via_firmware()) {
		pci_power_t pre_sleep_state = pci_dev->current_state;

		pci_refresh_power_state(pci_dev);
		/*
		 * On platforms with ACPI this check may also trigger for
		 * devices sharing power resources if one of those power
		 * resources has been activated as a result of a change of the
		 * power state of another device sharing it.  However, in that
		 * case it is also better to resume the device, in general.
		 */
		if (pci_dev->current_state < pre_sleep_state)
			pm_request_resume(dev);
	}
}

#else /* !CONFIG_PM_SLEEP */

#define pci_pm_prepare	NULL
#define pci_pm_complete	NULL

#endif /* !CONFIG_PM_SLEEP */

#ifdef CONFIG_SUSPEND
static void pcie_pme_root_status_cleanup(struct pci_dev *pci_dev)
{
	/*
	 * Some BIOSes forget to clear Root PME Status bits after system
	 * wakeup, which breaks ACPI-based runtime wakeup on PCI Express.
	 * Clear those bits now just in case (shouldn't hurt).
	 */
	if (pci_is_pcie(pci_dev) &&
	    (pci_pcie_type(pci_dev) == PCI_EXP_TYPE_ROOT_PORT ||
	     pci_pcie_type(pci_dev) == PCI_EXP_TYPE_RC_EC))
		pcie_clear_root_pme_status(pci_dev);
}

static int pci_pm_suspend(struct device *dev)
{
	struct pci_dev *pci_dev = to_pci_dev(dev);
	const struct dev_pm_ops *pm = dev->driver ? dev->driver->pm : NULL;

	pci_dev->skip_bus_pm = false;

	/*
	 * Disabling PTM allows some systems, e.g., Intel mobile chips
	 * since Coffee Lake, to enter a lower-power PM state.
	 */
	pci_suspend_ptm(pci_dev);

	if (pci_has_legacy_pm_support(pci_dev))
		return pci_legacy_suspend(dev, PMSG_SUSPEND);

	if (!pm) {
		pci_pm_default_suspend(pci_dev);
		return 0;
	}

	/*
	 * PCI devices suspended at run time may need to be resumed at this
	 * point, because in general it may be necessary to reconfigure them for
	 * system suspend.  Namely, if the device is expected to wake up the
	 * system from the sleep state, it may have to be reconfigured for this
	 * purpose, or if the device is not expected to wake up the system from
	 * the sleep state, it should be prevented from signaling wakeup events
	 * going forward.
	 *
	 * Also if the driver of the device does not indicate that its system
	 * suspend callbacks can cope with runtime-suspended devices, it is
	 * better to resume the device from runtime suspend here.
	 */
	if (!dev_pm_test_driver_flags(dev, DPM_FLAG_SMART_SUSPEND) ||
	    pci_dev_need_resume(pci_dev)) {
		pm_runtime_resume(dev);
		pci_dev->state_saved = false;
	} else {
		pci_dev_adjust_pme(pci_dev);
	}

	if (pm->suspend) {
		pci_power_t prev = pci_dev->current_state;
		int error;

		error = pm->suspend(dev);
		suspend_report_result(dev, pm->suspend, error);
		if (error)
			return error;

		if (!pci_dev->state_saved && pci_dev->current_state != PCI_D0
		    && pci_dev->current_state != PCI_UNKNOWN) {
			pci_WARN_ONCE(pci_dev, pci_dev->current_state != prev,
				      "PCI PM: State of device not saved by %pS\n",
				      pm->suspend);
		}
	}

	return 0;
}

static int pci_pm_suspend_late(struct device *dev)
{
	if (dev_pm_skip_suspend(dev))
		return 0;

	pci_fixup_device(pci_fixup_suspend, to_pci_dev(dev));

	return pm_generic_suspend_late(dev);
}

static int pci_pm_suspend_noirq(struct device *dev)
{
	struct pci_dev *pci_dev = to_pci_dev(dev);
	const struct dev_pm_ops *pm = dev->driver ? dev->driver->pm : NULL;

	if (dev_pm_skip_suspend(dev))
		return 0;

	if (pci_has_legacy_pm_support(pci_dev))
		return pci_legacy_suspend_late(dev);

	if (!pm) {
		pci_save_state(pci_dev);
		goto Fixup;
	}

	if (pm->suspend_noirq) {
		pci_power_t prev = pci_dev->current_state;
		int error;

		error = pm->suspend_noirq(dev);
		suspend_report_result(dev, pm->suspend_noirq, error);
		if (error)
			return error;

		if (!pci_dev->state_saved && pci_dev->current_state != PCI_D0
		    && pci_dev->current_state != PCI_UNKNOWN) {
			pci_WARN_ONCE(pci_dev, pci_dev->current_state != prev,
				      "PCI PM: State of device not saved by %pS\n",
				      pm->suspend_noirq);
			goto Fixup;
		}
	}

	if (!pci_dev->state_saved) {
		pci_save_state(pci_dev);

		/*
		 * If the device is a bridge with a child in D0 below it,
		 * it needs to stay in D0, so check skip_bus_pm to avoid
		 * putting it into a low-power state in that case.
		 */
		if (!pci_dev->skip_bus_pm && pci_power_manageable(pci_dev))
			pci_prepare_to_sleep(pci_dev);
	}

	pci_dbg(pci_dev, "PCI PM: Suspend power state: %s\n",
		pci_power_name(pci_dev->current_state));

	if (pci_dev->current_state == PCI_D0) {
		pci_dev->skip_bus_pm = true;
		/*
		 * Per PCI PM r1.2, table 6-1, a bridge must be in D0 if any
		 * downstream device is in D0, so avoid changing the power state
		 * of the parent bridge by setting the skip_bus_pm flag for it.
		 */
		if (pci_dev->bus->self)
			pci_dev->bus->self->skip_bus_pm = true;
	}

	if (pci_dev->skip_bus_pm && pm_suspend_no_platform()) {
		pci_dbg(pci_dev, "PCI PM: Skipped\n");
		goto Fixup;
	}

	pci_pm_set_unknown_state(pci_dev);

	/*
	 * Some BIOSes from ASUS have a bug: If a USB EHCI host controller's
	 * PCI COMMAND register isn't 0, the BIOS assumes that the controller
	 * hasn't been quiesced and tries to turn it off.  If the controller
	 * is already in D3, this can hang or cause memory corruption.
	 *
	 * Since the value of the COMMAND register doesn't matter once the
	 * device has been suspended, we can safely set it to 0 here.
	 */
	if (pci_dev->class == PCI_CLASS_SERIAL_USB_EHCI)
		pci_write_config_word(pci_dev, PCI_COMMAND, 0);

Fixup:
	pci_fixup_device(pci_fixup_suspend_late, pci_dev);

	/*
	 * If the target system sleep state is suspend-to-idle, it is sufficient
	 * to check whether or not the device's wakeup settings are good for
	 * runtime PM.  Otherwise, the pm_resume_via_firmware() check will cause
	 * pci_pm_complete() to take care of fixing up the device's state
	 * anyway, if need be.
	 */
	if (device_can_wakeup(dev) && !device_may_wakeup(dev))
		dev->power.may_skip_resume = false;

	return 0;
}

static int pci_pm_resume_noirq(struct device *dev)
{
	struct pci_dev *pci_dev = to_pci_dev(dev);
	const struct dev_pm_ops *pm = dev->driver ? dev->driver->pm : NULL;
	pci_power_t prev_state = pci_dev->current_state;
	bool skip_bus_pm = pci_dev->skip_bus_pm;

	if (dev_pm_skip_resume(dev))
		return 0;

	/*
	 * In the suspend-to-idle case, devices left in D0 during suspend will
	 * stay in D0, so it is not necessary to restore or update their
	 * configuration here and attempting to put them into D0 again is
	 * pointless, so avoid doing that.
	 */
	if (!(skip_bus_pm && pm_suspend_no_platform()))
		pci_pm_default_resume_early(pci_dev);

	pci_fixup_device(pci_fixup_resume_early, pci_dev);
	pcie_pme_root_status_cleanup(pci_dev);

	if (!skip_bus_pm && prev_state == PCI_D3cold)
		pci_pm_bridge_power_up_actions(pci_dev);

	if (pci_has_legacy_pm_support(pci_dev))
		return 0;

	if (pm && pm->resume_noirq)
		return pm->resume_noirq(dev);

	return 0;
}

static int pci_pm_resume_early(struct device *dev)
{
	if (dev_pm_skip_resume(dev))
		return 0;

	return pm_generic_resume_early(dev);
}

static int pci_pm_resume(struct device *dev)
{
	struct pci_dev *pci_dev = to_pci_dev(dev);
	const struct dev_pm_ops *pm = dev->driver ? dev->driver->pm : NULL;

	/*
	 * This is necessary for the suspend error path in which resume is
	 * called without restoring the standard config registers of the device.
	 */
	if (pci_dev->state_saved)
		pci_restore_standard_config(pci_dev);

	pci_resume_ptm(pci_dev);

	if (pci_has_legacy_pm_support(pci_dev))
		return pci_legacy_resume(dev);

	pci_pm_default_resume(pci_dev);

	if (pm) {
		if (pm->resume)
			return pm->resume(dev);
	} else {
		pci_pm_reenable_device(pci_dev);
	}

	return 0;
}

#else /* !CONFIG_SUSPEND */

#define pci_pm_suspend		NULL
#define pci_pm_suspend_late	NULL
#define pci_pm_suspend_noirq	NULL
#define pci_pm_resume		NULL
#define pci_pm_resume_early	NULL
#define pci_pm_resume_noirq	NULL

#endif /* !CONFIG_SUSPEND */

#ifdef CONFIG_HIBERNATE_CALLBACKS

static int pci_pm_freeze(struct device *dev)
{
	struct pci_dev *pci_dev = to_pci_dev(dev);
	const struct dev_pm_ops *pm = dev->driver ? dev->driver->pm : NULL;

	if (pci_has_legacy_pm_support(pci_dev))
		return pci_legacy_suspend(dev, PMSG_FREEZE);

	if (!pm) {
		pci_pm_default_suspend(pci_dev);
		return 0;
	}

	/*
	 * Resume all runtime-suspended devices before creating a snapshot
	 * image of system memory, because the restore kernel generally cannot
	 * be expected to always handle them consistently and they need to be
	 * put into the runtime-active metastate during system resume anyway,
	 * so it is better to ensure that the state saved in the image will be
	 * always consistent with that.
	 */
	pm_runtime_resume(dev);
	pci_dev->state_saved = false;

	if (pm->freeze) {
		int error;

		error = pm->freeze(dev);
		suspend_report_result(dev, pm->freeze, error);
		if (error)
			return error;
	}

	return 0;
}

static int pci_pm_freeze_noirq(struct device *dev)
{
	struct pci_dev *pci_dev = to_pci_dev(dev);
	const struct dev_pm_ops *pm = dev->driver ? dev->driver->pm : NULL;

	if (pci_has_legacy_pm_support(pci_dev))
		return pci_legacy_suspend_late(dev);

	if (pm && pm->freeze_noirq) {
		int error;

		error = pm->freeze_noirq(dev);
		suspend_report_result(dev, pm->freeze_noirq, error);
		if (error)
			return error;
	}

	if (!pci_dev->state_saved)
		pci_save_state(pci_dev);

	pci_pm_set_unknown_state(pci_dev);

	return 0;
}

static int pci_pm_thaw_noirq(struct device *dev)
{
	struct pci_dev *pci_dev = to_pci_dev(dev);
	const struct dev_pm_ops *pm = dev->driver ? dev->driver->pm : NULL;

	/*
	 * The pm->thaw_noirq() callback assumes the device has been
	 * returned to D0 and its config state has been restored.
	 *
	 * In addition, pci_restore_state() restores MSI-X state in MMIO
	 * space, which requires the device to be in D0, so return it to D0
	 * in case the driver's "freeze" callbacks put it into a low-power
	 * state.
	 */
	pci_pm_power_up_and_verify_state(pci_dev);
	pci_restore_state(pci_dev);

	if (pci_has_legacy_pm_support(pci_dev))
		return 0;

	if (pm && pm->thaw_noirq)
		return pm->thaw_noirq(dev);

	return 0;
}

static int pci_pm_thaw(struct device *dev)
{
	struct pci_dev *pci_dev = to_pci_dev(dev);
	const struct dev_pm_ops *pm = dev->driver ? dev->driver->pm : NULL;
	int error = 0;

	if (pci_has_legacy_pm_support(pci_dev))
		return pci_legacy_resume(dev);

	if (pm) {
		if (pm->thaw)
			error = pm->thaw(dev);
	} else {
		pci_pm_reenable_device(pci_dev);
	}

	pci_dev->state_saved = false;

	return error;
}

static int pci_pm_poweroff(struct device *dev)
{
	struct pci_dev *pci_dev = to_pci_dev(dev);
	const struct dev_pm_ops *pm = dev->driver ? dev->driver->pm : NULL;

	if (pci_has_legacy_pm_support(pci_dev))
		return pci_legacy_suspend(dev, PMSG_HIBERNATE);

	if (!pm) {
		pci_pm_default_suspend(pci_dev);
		return 0;
	}

	/* The reason to do that is the same as in pci_pm_suspend(). */
	if (!dev_pm_test_driver_flags(dev, DPM_FLAG_SMART_SUSPEND) ||
	    pci_dev_need_resume(pci_dev)) {
		pm_runtime_resume(dev);
		pci_dev->state_saved = false;
	} else {
		pci_dev_adjust_pme(pci_dev);
	}

	if (pm->poweroff) {
		int error;

		error = pm->poweroff(dev);
		suspend_report_result(dev, pm->poweroff, error);
		if (error)
			return error;
	}

	return 0;
}

static int pci_pm_poweroff_late(struct device *dev)
{
	if (dev_pm_skip_suspend(dev))
		return 0;

	pci_fixup_device(pci_fixup_suspend, to_pci_dev(dev));

	return pm_generic_poweroff_late(dev);
}

static int pci_pm_poweroff_noirq(struct device *dev)
{
	struct pci_dev *pci_dev = to_pci_dev(dev);
	const struct dev_pm_ops *pm = dev->driver ? dev->driver->pm : NULL;

	if (dev_pm_skip_suspend(dev))
		return 0;

	if (pci_has_legacy_pm_support(pci_dev))
		return pci_legacy_suspend_late(dev);

	if (!pm) {
		pci_fixup_device(pci_fixup_suspend_late, pci_dev);
		return 0;
	}

	if (pm->poweroff_noirq) {
		int error;

		error = pm->poweroff_noirq(dev);
		suspend_report_result(dev, pm->poweroff_noirq, error);
		if (error)
			return error;
	}

	if (!pci_dev->state_saved && !pci_has_subordinate(pci_dev))
		pci_prepare_to_sleep(pci_dev);

	/*
	 * The reason for doing this here is the same as for the analogous code
	 * in pci_pm_suspend_noirq().
	 */
	if (pci_dev->class == PCI_CLASS_SERIAL_USB_EHCI)
		pci_write_config_word(pci_dev, PCI_COMMAND, 0);

	pci_fixup_device(pci_fixup_suspend_late, pci_dev);

	return 0;
}

static int pci_pm_restore_noirq(struct device *dev)
{
	struct pci_dev *pci_dev = to_pci_dev(dev);
	const struct dev_pm_ops *pm = dev->driver ? dev->driver->pm : NULL;

	pci_pm_default_resume_early(pci_dev);
	pci_fixup_device(pci_fixup_resume_early, pci_dev);

	if (pci_has_legacy_pm_support(pci_dev))
		return 0;

	if (pm && pm->restore_noirq)
		return pm->restore_noirq(dev);

	return 0;
}

static int pci_pm_restore(struct device *dev)
{
	struct pci_dev *pci_dev = to_pci_dev(dev);
	const struct dev_pm_ops *pm = dev->driver ? dev->driver->pm : NULL;

	/*
	 * This is necessary for the hibernation error path in which restore is
	 * called without restoring the standard config registers of the device.
	 */
	if (pci_dev->state_saved)
		pci_restore_standard_config(pci_dev);

	if (pci_has_legacy_pm_support(pci_dev))
		return pci_legacy_resume(dev);

	pci_pm_default_resume(pci_dev);

	if (pm) {
		if (pm->restore)
			return pm->restore(dev);
	} else {
		pci_pm_reenable_device(pci_dev);
	}

	return 0;
}

#else /* !CONFIG_HIBERNATE_CALLBACKS */

#define pci_pm_freeze		NULL
#define pci_pm_freeze_noirq	NULL
#define pci_pm_thaw		NULL
#define pci_pm_thaw_noirq	NULL
#define pci_pm_poweroff		NULL
#define pci_pm_poweroff_late	NULL
#define pci_pm_poweroff_noirq	NULL
#define pci_pm_restore		NULL
#define pci_pm_restore_noirq	NULL

#endif /* !CONFIG_HIBERNATE_CALLBACKS */

#ifdef CONFIG_PM

static int pci_pm_runtime_suspend(struct device *dev)
{
	struct pci_dev *pci_dev = to_pci_dev(dev);
	const struct dev_pm_ops *pm = dev->driver ? dev->driver->pm : NULL;
	pci_power_t prev = pci_dev->current_state;
	int error;

	pci_suspend_ptm(pci_dev);

	/*
	 * If pci_dev->driver is not set (unbound), we leave the device in D0,
	 * but it may go to D3cold when the bridge above it runtime suspends.
	 * Save its config space in case that happens.
	 */
	if (!pci_dev->driver) {
		pci_save_state(pci_dev);
		return 0;
	}

	pci_dev->state_saved = false;
	if (pm && pm->runtime_suspend) {
		error = pm->runtime_suspend(dev);
		/*
		 * -EBUSY and -EAGAIN is used to request the runtime PM core
		 * to schedule a new suspend, so log the event only with debug
		 * log level.
		 */
		if (error == -EBUSY || error == -EAGAIN) {
			pci_dbg(pci_dev, "can't suspend now (%ps returned %d)\n",
				pm->runtime_suspend, error);
			return error;
		} else if (error) {
			pci_err(pci_dev, "can't suspend (%ps returned %d)\n",
				pm->runtime_suspend, error);
			return error;
		}
	}

	pci_fixup_device(pci_fixup_suspend, pci_dev);

	if (pm && pm->runtime_suspend
	    && !pci_dev->state_saved && pci_dev->current_state != PCI_D0
	    && pci_dev->current_state != PCI_UNKNOWN) {
		pci_WARN_ONCE(pci_dev, pci_dev->current_state != prev,
			      "PCI PM: State of device not saved by %pS\n",
			      pm->runtime_suspend);
		return 0;
	}

	if (!pci_dev->state_saved) {
		pci_save_state(pci_dev);
		pci_finish_runtime_suspend(pci_dev);
	}

	return 0;
}

static int pci_pm_runtime_resume(struct device *dev)
{
	struct pci_dev *pci_dev = to_pci_dev(dev);
	const struct dev_pm_ops *pm = dev->driver ? dev->driver->pm : NULL;
	pci_power_t prev_state = pci_dev->current_state;
	int error = 0;

	/*
	 * Restoring config space is necessary even if the device is not bound
	 * to a driver because although we left it in D0, it may have gone to
	 * D3cold when the bridge above it runtime suspended.
	 */
	pci_pm_default_resume_early(pci_dev);
	pci_resume_ptm(pci_dev);

	if (!pci_dev->driver)
		return 0;

	pci_fixup_device(pci_fixup_resume_early, pci_dev);
	pci_pm_default_resume(pci_dev);

	if (prev_state == PCI_D3cold)
		pci_pm_bridge_power_up_actions(pci_dev);

	if (pm && pm->runtime_resume)
		error = pm->runtime_resume(dev);

	return error;
}

static int pci_pm_runtime_idle(struct device *dev)
{
	struct pci_dev *pci_dev = to_pci_dev(dev);
	const struct dev_pm_ops *pm = dev->driver ? dev->driver->pm : NULL;

	/*
	 * If pci_dev->driver is not set (unbound), the device should
	 * always remain in D0 regardless of the runtime PM status
	 */
	if (!pci_dev->driver)
		return 0;

	if (!pm)
		return -ENOSYS;

	if (pm->runtime_idle)
		return pm->runtime_idle(dev);

	return 0;
}

static const struct dev_pm_ops pci_dev_pm_ops = {
	.prepare = pci_pm_prepare,
	.complete = pci_pm_complete,
	.suspend = pci_pm_suspend,
	.suspend_late = pci_pm_suspend_late,
	.resume = pci_pm_resume,
	.resume_early = pci_pm_resume_early,
	.freeze = pci_pm_freeze,
	.thaw = pci_pm_thaw,
	.poweroff = pci_pm_poweroff,
	.poweroff_late = pci_pm_poweroff_late,
	.restore = pci_pm_restore,
	.suspend_noirq = pci_pm_suspend_noirq,
	.resume_noirq = pci_pm_resume_noirq,
	.freeze_noirq = pci_pm_freeze_noirq,
	.thaw_noirq = pci_pm_thaw_noirq,
	.poweroff_noirq = pci_pm_poweroff_noirq,
	.restore_noirq = pci_pm_restore_noirq,
	.runtime_suspend = pci_pm_runtime_suspend,
	.runtime_resume = pci_pm_runtime_resume,
	.runtime_idle = pci_pm_runtime_idle,
};

#define PCI_PM_OPS_PTR	(&pci_dev_pm_ops)

#else /* !CONFIG_PM */

#define pci_pm_runtime_suspend	NULL
#define pci_pm_runtime_resume	NULL
#define pci_pm_runtime_idle	NULL

#define PCI_PM_OPS_PTR	NULL

#endif /* !CONFIG_PM */

/**
 * __pci_register_driver - register a new pci driver
 * @drv: the driver structure to register
 * @owner: owner module of drv
 * @mod_name: module name string
 *
 * Adds the driver structure to the list of registered drivers.
 * Returns a negative value on error, otherwise 0.
 * If no error occurred, the driver remains registered even if
 * no device was claimed during registration.
 */
int __pci_register_driver(struct pci_driver *drv, struct module *owner,
			  const char *mod_name)
{
	/* initialize common driver fields */
	drv->driver.name = drv->name;
	drv->driver.bus = &pci_bus_type;
	drv->driver.owner = owner;
	drv->driver.mod_name = mod_name;
	drv->driver.groups = drv->groups;
	drv->driver.dev_groups = drv->dev_groups;

	spin_lock_init(&drv->dynids.lock);
	INIT_LIST_HEAD(&drv->dynids.list);

	/* register with core */
	return driver_register(&drv->driver);
}
EXPORT_SYMBOL(__pci_register_driver);

/**
 * pci_unregister_driver - unregister a pci driver
 * @drv: the driver structure to unregister
 *
 * Deletes the driver structure from the list of registered PCI drivers,
 * gives it a chance to clean up by calling its remove() function for
 * each device it was responsible for, and marks those devices as
 * driverless.
 */

void pci_unregister_driver(struct pci_driver *drv)
{
	driver_unregister(&drv->driver);
	pci_free_dynids(drv);
}
EXPORT_SYMBOL(pci_unregister_driver);

static struct pci_driver pci_compat_driver = {
	.name = "compat"
};

/**
 * pci_dev_driver - get the pci_driver of a device
 * @dev: the device to query
 *
 * Returns the appropriate pci_driver structure or %NULL if there is no
 * registered driver for the device.
 */
struct pci_driver *pci_dev_driver(const struct pci_dev *dev)
{
	if (dev->driver)
		return dev->driver;
	else {
		int i;
		for (i = 0; i <= PCI_ROM_RESOURCE; i++)
			if (dev->resource[i].flags & IORESOURCE_BUSY)
				return &pci_compat_driver;
	}
	return NULL;
}
EXPORT_SYMBOL(pci_dev_driver);

/**
 * pci_bus_match - Tell if a PCI device structure has a matching PCI device id structure
 * @dev: the PCI device structure to match against
 * @drv: the device driver to search for matching PCI device id structures
 *
 * Used by a driver to check whether a PCI device present in the
 * system is in its list of supported devices. Returns the matching
 * pci_device_id structure or %NULL if there is no match.
 */
static int pci_bus_match(struct device *dev, struct device_driver *drv)
{
	struct pci_dev *pci_dev = to_pci_dev(dev);
	struct pci_driver *pci_drv;
	const struct pci_device_id *found_id;

	if (!pci_dev->match_driver)
		return 0;

	pci_drv = to_pci_driver(drv);
	found_id = pci_match_device(pci_drv, pci_dev);
	if (found_id)
		return 1;

	return 0;
}

/**
 * pci_dev_get - increments the reference count of the pci device structure
 * @dev: the device being referenced
 *
 * Each live reference to a device should be refcounted.
 *
 * Drivers for PCI devices should normally record such references in
 * their probe() methods, when they bind to a device, and release
 * them by calling pci_dev_put(), in their disconnect() methods.
 *
 * A pointer to the device with the incremented reference counter is returned.
 */
struct pci_dev *pci_dev_get(struct pci_dev *dev)
{
	if (dev)
		get_device(&dev->dev);
	return dev;
}
EXPORT_SYMBOL(pci_dev_get);

/**
 * pci_dev_put - release a use of the pci device structure
 * @dev: device that's been disconnected
 *
 * Must be called when a user of a device is finished with it.  When the last
 * user of the device calls this function, the memory of the device is freed.
 */
void pci_dev_put(struct pci_dev *dev)
{
	if (dev)
		put_device(&dev->dev);
}
EXPORT_SYMBOL(pci_dev_put);

static int pci_uevent(const struct device *dev, struct kobj_uevent_env *env)
{
	const struct pci_dev *pdev;

	if (!dev)
		return -ENODEV;

	pdev = to_pci_dev(dev);

	if (add_uevent_var(env, "PCI_CLASS=%04X", pdev->class))
		return -ENOMEM;

	if (add_uevent_var(env, "PCI_ID=%04X:%04X", pdev->vendor, pdev->device))
		return -ENOMEM;

	if (add_uevent_var(env, "PCI_SUBSYS_ID=%04X:%04X", pdev->subsystem_vendor,
			   pdev->subsystem_device))
		return -ENOMEM;

	if (add_uevent_var(env, "PCI_SLOT_NAME=%s", pci_name(pdev)))
		return -ENOMEM;

	if (add_uevent_var(env, "MODALIAS=pci:v%08Xd%08Xsv%08Xsd%08Xbc%02Xsc%02Xi%02X",
			   pdev->vendor, pdev->device,
			   pdev->subsystem_vendor, pdev->subsystem_device,
			   (u8)(pdev->class >> 16), (u8)(pdev->class >> 8),
			   (u8)(pdev->class)))
		return -ENOMEM;

	return 0;
}

#if defined(CONFIG_PCIEAER) || defined(CONFIG_EEH)
/**
 * pci_uevent_ers - emit a uevent during recovery path of PCI device
 * @pdev: PCI device undergoing error recovery
 * @err_type: type of error event
 */
void pci_uevent_ers(struct pci_dev *pdev, enum pci_ers_result err_type)
{
	int idx = 0;
	char *envp[3];

	switch (err_type) {
	case PCI_ERS_RESULT_NONE:
	case PCI_ERS_RESULT_CAN_RECOVER:
		envp[idx++] = "ERROR_EVENT=BEGIN_RECOVERY";
		envp[idx++] = "DEVICE_ONLINE=0";
		break;
	case PCI_ERS_RESULT_RECOVERED:
		envp[idx++] = "ERROR_EVENT=SUCCESSFUL_RECOVERY";
		envp[idx++] = "DEVICE_ONLINE=1";
		break;
	case PCI_ERS_RESULT_DISCONNECT:
		envp[idx++] = "ERROR_EVENT=FAILED_RECOVERY";
		envp[idx++] = "DEVICE_ONLINE=0";
		break;
	default:
		break;
	}

	if (idx > 0) {
		envp[idx++] = NULL;
		kobject_uevent_env(&pdev->dev.kobj, KOBJ_CHANGE, envp);
	}
}
#endif

static int pci_bus_num_vf(struct device *dev)
{
	return pci_num_vf(to_pci_dev(dev));
}

/**
 * pci_dma_configure - Setup DMA configuration
 * @dev: ptr to dev structure
 *
 * Function to update PCI devices's DMA configuration using the same
 * info from the OF node or ACPI node of host bridge's parent (if any).
 */
static int pci_dma_configure(struct device *dev)
{
	struct pci_driver *driver = to_pci_driver(dev->driver);
	struct device *bridge;
	int ret = 0;

	bridge = pci_get_host_bridge_device(to_pci_dev(dev));

	if (IS_ENABLED(CONFIG_OF) && bridge->parent &&
	    bridge->parent->of_node) {
		ret = of_dma_configure(dev, bridge->parent->of_node, true);
	} else if (has_acpi_companion(bridge)) {
		struct acpi_device *adev = to_acpi_device_node(bridge->fwnode);

		ret = acpi_dma_configure(dev, acpi_get_dma_attr(adev));
	}

	pci_put_host_bridge_device(bridge);

	if (!ret && !driver->driver_managed_dma) {
		ret = iommu_device_use_default_domain(dev);
		if (ret)
			arch_teardown_dma_ops(dev);
	}

	return ret;
}

static void pci_dma_cleanup(struct device *dev)
{
	struct pci_driver *driver = to_pci_driver(dev->driver);

	if (!driver->driver_managed_dma)
		iommu_device_unuse_default_domain(dev);
}

struct bus_type pci_bus_type = {
	.name		= "pci",
	.match		= pci_bus_match,
	.uevent		= pci_uevent,
	.probe		= pci_device_probe,
	.remove		= pci_device_remove,
	.shutdown	= pci_device_shutdown,
	.dev_groups	= pci_dev_groups,
	.bus_groups	= pci_bus_groups,
	.drv_groups	= pci_drv_groups,
	.pm		= PCI_PM_OPS_PTR,
	.num_vf		= pci_bus_num_vf,
	.dma_configure	= pci_dma_configure,
	.dma_cleanup	= pci_dma_cleanup,
};
EXPORT_SYMBOL(pci_bus_type);

#ifdef CONFIG_PCIEPORTBUS
static int pcie_port_bus_match(struct device *dev, struct device_driver *drv)
{
	struct pcie_device *pciedev;
	struct pcie_port_service_driver *driver;

	if (drv->bus != &pcie_port_bus_type || dev->bus != &pcie_port_bus_type)
		return 0;

	pciedev = to_pcie_device(dev);
	driver = to_service_driver(drv);

	if (driver->service != pciedev->service)
		return 0;

	if (driver->port_type != PCIE_ANY_PORT &&
	    driver->port_type != pci_pcie_type(pciedev->port))
		return 0;

	return 1;
}

struct bus_type pcie_port_bus_type = {
	.name		= "pci_express",
	.match		= pcie_port_bus_match,
};
EXPORT_SYMBOL_GPL(pcie_port_bus_type);
#endif

static int __init pci_driver_init(void)
{
	int ret;

	ret = bus_register(&pci_bus_type);
	if (ret)
		return ret;

#ifdef CONFIG_PCIEPORTBUS
	ret = bus_register(&pcie_port_bus_type);
	if (ret)
		return ret;
#endif
	dma_debug_add_bus(&pci_bus_type);
	return 0;
}
postcore_initcall(pci_driver_init);
