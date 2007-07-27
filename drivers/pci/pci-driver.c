/*
 * drivers/pci/pci-driver.c
 *
 */

#include <linux/pci.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/mempolicy.h>
#include <linux/string.h>
#include <linux/slab.h>
#include <linux/sched.h>
#include "pci.h"

/*
 * Dynamic device IDs are disabled for !CONFIG_HOTPLUG
 */

struct pci_dynid {
	struct list_head node;
	struct pci_device_id id;
};

#ifdef CONFIG_HOTPLUG

/**
 * store_new_id - add a new PCI device ID to this driver and re-probe devices
 * @driver: target device driver
 * @buf: buffer for scanning device ID data
 * @count: input size
 *
 * Adds a new dynamic pci device ID to this driver,
 * and causes the driver to probe for all devices again.
 */
static ssize_t
store_new_id(struct device_driver *driver, const char *buf, size_t count)
{
	struct pci_dynid *dynid;
	struct pci_driver *pdrv = to_pci_driver(driver);
	__u32 vendor, device, subvendor=PCI_ANY_ID,
		subdevice=PCI_ANY_ID, class=0, class_mask=0;
	unsigned long driver_data=0;
	int fields=0;
	int retval = 0;

	fields = sscanf(buf, "%x %x %x %x %x %x %lux",
			&vendor, &device, &subvendor, &subdevice,
			&class, &class_mask, &driver_data);
	if (fields < 2)
		return -EINVAL;

	dynid = kzalloc(sizeof(*dynid), GFP_KERNEL);
	if (!dynid)
		return -ENOMEM;

	INIT_LIST_HEAD(&dynid->node);
	dynid->id.vendor = vendor;
	dynid->id.device = device;
	dynid->id.subvendor = subvendor;
	dynid->id.subdevice = subdevice;
	dynid->id.class = class;
	dynid->id.class_mask = class_mask;
	dynid->id.driver_data = pdrv->dynids.use_driver_data ?
		driver_data : 0UL;

	spin_lock(&pdrv->dynids.lock);
	list_add_tail(&pdrv->dynids.list, &dynid->node);
	spin_unlock(&pdrv->dynids.lock);

	if (get_driver(&pdrv->driver)) {
		retval = driver_attach(&pdrv->driver);
		put_driver(&pdrv->driver);
	}

	if (retval)
		return retval;
	return count;
}
static DRIVER_ATTR(new_id, S_IWUSR, NULL, store_new_id);

static void
pci_free_dynids(struct pci_driver *drv)
{
	struct pci_dynid *dynid, *n;

	spin_lock(&drv->dynids.lock);
	list_for_each_entry_safe(dynid, n, &drv->dynids.list, node) {
		list_del(&dynid->node);
		kfree(dynid);
	}
	spin_unlock(&drv->dynids.lock);
}

static int
pci_create_newid_file(struct pci_driver *drv)
{
	int error = 0;
	if (drv->probe != NULL)
		error = sysfs_create_file(&drv->driver.kobj,
					  &driver_attr_new_id.attr);
	return error;
}

#else /* !CONFIG_HOTPLUG */
static inline void pci_free_dynids(struct pci_driver *drv) {}
static inline int pci_create_newid_file(struct pci_driver *drv)
{
	return 0;
}
#endif

/**
 * pci_match_id - See if a pci device matches a given pci_id table
 * @ids: array of PCI device id structures to search in
 * @dev: the PCI device structure to match against.
 *
 * Used by a driver to check whether a PCI device present in the
 * system is in its list of supported devices.  Returns the matching
 * pci_device_id structure or %NULL if there is no match.
 *
 * Deprecated, don't use this as it will not catch any dynamic ids
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

/**
 * pci_match_device - Tell if a PCI device structure has a matching PCI device id structure
 * @drv: the PCI driver to match against
 * @dev: the PCI device structure to match against
 *
 * Used by a driver to check whether a PCI device present in the
 * system is in its list of supported devices.  Returns the matching
 * pci_device_id structure or %NULL if there is no match.
 */
const struct pci_device_id *pci_match_device(struct pci_driver *drv,
					     struct pci_dev *dev)
{
	struct pci_dynid *dynid;

	/* Look at the dynamic ids first, before the static ones */
	spin_lock(&drv->dynids.lock);
	list_for_each_entry(dynid, &drv->dynids.list, node) {
		if (pci_match_one_device(&dynid->id, dev)) {
			spin_unlock(&drv->dynids.lock);
			return &dynid->id;
		}
	}
	spin_unlock(&drv->dynids.lock);

	return pci_match_id(drv->id_table, dev);
}

static int pci_call_probe(struct pci_driver *drv, struct pci_dev *dev,
			  const struct pci_device_id *id)
{
	int error;
#ifdef CONFIG_NUMA
	/* Execute driver initialization on node where the
	   device's bus is attached to.  This way the driver likely
	   allocates its local memory on the right node without
	   any need to change it. */
	struct mempolicy *oldpol;
	cpumask_t oldmask = current->cpus_allowed;
	int node = pcibus_to_node(dev->bus);
	if (node >= 0 && node_online(node))
	    set_cpus_allowed(current, node_to_cpumask(node));
	/* And set default memory allocation policy */
	oldpol = current->mempolicy;
	current->mempolicy = &default_policy;
	mpol_get(current->mempolicy);
#endif
	error = drv->probe(dev, id);
#ifdef CONFIG_NUMA
	set_cpus_allowed(current, oldmask);
	mpol_free(current->mempolicy);
	current->mempolicy = oldpol;
#endif
	return error;
}

/**
 * __pci_device_probe()
 * @drv: driver to call to check if it wants the PCI device
 * @pci_dev: PCI device being probed
 * 
 * returns 0 on success, else error.
 * side-effect: pci_dev->driver is set to drv when drv claims pci_dev.
 */
static int
__pci_device_probe(struct pci_driver *drv, struct pci_dev *pci_dev)
{
	const struct pci_device_id *id;
	int error = 0;

	if (!pci_dev->driver && drv->probe) {
		error = -ENODEV;

		id = pci_match_device(drv, pci_dev);
		if (id)
			error = pci_call_probe(drv, pci_dev, id);
		if (error >= 0) {
			pci_dev->driver = drv;
			error = 0;
		}
	}
	return error;
}

static int pci_device_probe(struct device * dev)
{
	int error = 0;
	struct pci_driver *drv;
	struct pci_dev *pci_dev;

	drv = to_pci_driver(dev->driver);
	pci_dev = to_pci_dev(dev);
	pci_dev_get(pci_dev);
	error = __pci_device_probe(drv, pci_dev);
	if (error)
		pci_dev_put(pci_dev);

	return error;
}

static int pci_device_remove(struct device * dev)
{
	struct pci_dev * pci_dev = to_pci_dev(dev);
	struct pci_driver * drv = pci_dev->driver;

	if (drv) {
		if (drv->remove)
			drv->remove(pci_dev);
		pci_dev->driver = NULL;
	}

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
	return 0;
}

static int pci_device_suspend(struct device * dev, pm_message_t state)
{
	struct pci_dev * pci_dev = to_pci_dev(dev);
	struct pci_driver * drv = pci_dev->driver;
	int i = 0;

	if (drv && drv->suspend) {
		i = drv->suspend(pci_dev, state);
		suspend_report_result(drv->suspend, i);
	} else {
		pci_save_state(pci_dev);
		/*
		 * mark its power state as "unknown", since we don't know if
		 * e.g. the BIOS will change its device state when we suspend.
		 */
		if (pci_dev->current_state == PCI_D0)
			pci_dev->current_state = PCI_UNKNOWN;
	}
	return i;
}

static int pci_device_suspend_late(struct device * dev, pm_message_t state)
{
	struct pci_dev * pci_dev = to_pci_dev(dev);
	struct pci_driver * drv = pci_dev->driver;
	int i = 0;

	if (drv && drv->suspend_late) {
		i = drv->suspend_late(pci_dev, state);
		suspend_report_result(drv->suspend_late, i);
	}
	return i;
}

/*
 * Default resume method for devices that have no driver provided resume,
 * or not even a driver at all.
 */
static int pci_default_resume(struct pci_dev *pci_dev)
{
	int retval = 0;

	/* restore the PCI config space */
	pci_restore_state(pci_dev);
	/* if the device was enabled before suspend, reenable */
	retval = pci_reenable_device(pci_dev);
	/* if the device was busmaster before the suspend, make it busmaster again */
	if (pci_dev->is_busmaster)
		pci_set_master(pci_dev);

	return retval;
}

static int pci_device_resume(struct device * dev)
{
	int error;
	struct pci_dev * pci_dev = to_pci_dev(dev);
	struct pci_driver * drv = pci_dev->driver;

	if (drv && drv->resume)
		error = drv->resume(pci_dev);
	else
		error = pci_default_resume(pci_dev);
	return error;
}

static int pci_device_resume_early(struct device * dev)
{
	int error = 0;
	struct pci_dev * pci_dev = to_pci_dev(dev);
	struct pci_driver * drv = pci_dev->driver;

	pci_fixup_device(pci_fixup_resume, pci_dev);

	if (drv && drv->resume_early)
		error = drv->resume_early(pci_dev);
	return error;
}

static void pci_device_shutdown(struct device *dev)
{
	struct pci_dev *pci_dev = to_pci_dev(dev);
	struct pci_driver *drv = pci_dev->driver;

	if (drv && drv->shutdown)
		drv->shutdown(pci_dev);
}

#define kobj_to_pci_driver(obj) container_of(obj, struct device_driver, kobj)
#define attr_to_driver_attribute(obj) container_of(obj, struct driver_attribute, attr)

static ssize_t
pci_driver_attr_show(struct kobject * kobj, struct attribute *attr, char *buf)
{
	struct device_driver *driver = kobj_to_pci_driver(kobj);
	struct driver_attribute *dattr = attr_to_driver_attribute(attr);
	ssize_t ret;

	if (!get_driver(driver))
		return -ENODEV;

	ret = dattr->show ? dattr->show(driver, buf) : -EIO;

	put_driver(driver);
	return ret;
}

static ssize_t
pci_driver_attr_store(struct kobject * kobj, struct attribute *attr,
		      const char *buf, size_t count)
{
	struct device_driver *driver = kobj_to_pci_driver(kobj);
	struct driver_attribute *dattr = attr_to_driver_attribute(attr);
	ssize_t ret;

	if (!get_driver(driver))
		return -ENODEV;

	ret = dattr->store ? dattr->store(driver, buf, count) : -EIO;

	put_driver(driver);
	return ret;
}

static struct sysfs_ops pci_driver_sysfs_ops = {
	.show = pci_driver_attr_show,
	.store = pci_driver_attr_store,
};
static struct kobj_type pci_driver_kobj_type = {
	.sysfs_ops = &pci_driver_sysfs_ops,
};

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
	int error;

	/* initialize common driver fields */
	drv->driver.name = drv->name;
	drv->driver.bus = &pci_bus_type;
	drv->driver.owner = owner;
	drv->driver.mod_name = mod_name;
	drv->driver.kobj.ktype = &pci_driver_kobj_type;

	spin_lock_init(&drv->dynids.lock);
	INIT_LIST_HEAD(&drv->dynids.list);

	/* register with core */
	error = driver_register(&drv->driver);
	if (error)
		return error;

	error = pci_create_newid_file(drv);
	if (error)
		driver_unregister(&drv->driver);

	return error;
}

/**
 * pci_unregister_driver - unregister a pci driver
 * @drv: the driver structure to unregister
 * 
 * Deletes the driver structure from the list of registered PCI drivers,
 * gives it a chance to clean up by calling its remove() function for
 * each device it was responsible for, and marks those devices as
 * driverless.
 */

void
pci_unregister_driver(struct pci_driver *drv)
{
	driver_unregister(&drv->driver);
	pci_free_dynids(drv);
}

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
struct pci_driver *
pci_dev_driver(const struct pci_dev *dev)
{
	if (dev->driver)
		return dev->driver;
	else {
		int i;
		for(i=0; i<=PCI_ROM_RESOURCE; i++)
			if (dev->resource[i].flags & IORESOURCE_BUSY)
				return &pci_compat_driver;
	}
	return NULL;
}

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
	struct pci_driver *pci_drv = to_pci_driver(drv);
	const struct pci_device_id *found_id;

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

#ifndef CONFIG_HOTPLUG
int pci_uevent(struct device *dev, char **envp, int num_envp,
	       char *buffer, int buffer_size)
{
	return -ENODEV;
}
#endif

struct bus_type pci_bus_type = {
	.name		= "pci",
	.match		= pci_bus_match,
	.uevent		= pci_uevent,
	.probe		= pci_device_probe,
	.remove		= pci_device_remove,
	.suspend	= pci_device_suspend,
	.suspend_late	= pci_device_suspend_late,
	.resume_early	= pci_device_resume_early,
	.resume		= pci_device_resume,
	.shutdown	= pci_device_shutdown,
	.dev_attrs	= pci_dev_attrs,
};

static int __init pci_driver_init(void)
{
	return bus_register(&pci_bus_type);
}

postcore_initcall(pci_driver_init);

EXPORT_SYMBOL(pci_match_id);
EXPORT_SYMBOL(pci_match_device);
EXPORT_SYMBOL(__pci_register_driver);
EXPORT_SYMBOL(pci_unregister_driver);
EXPORT_SYMBOL(pci_dev_driver);
EXPORT_SYMBOL(pci_bus_type);
EXPORT_SYMBOL(pci_dev_get);
EXPORT_SYMBOL(pci_dev_put);
