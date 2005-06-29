#include <linux/pci.h>
#include <linux/module.h>
#include "pci.h"

static void pci_free_resources(struct pci_dev *dev)
{
	int i;

 	msi_remove_pci_irq_vectors(dev);

	pci_cleanup_rom(dev);
	for (i = 0; i < PCI_NUM_RESOURCES; i++) {
		struct resource *res = dev->resource + i;
		if (res->parent)
			release_resource(res);
	}
}

static void pci_destroy_dev(struct pci_dev *dev)
{
	if (!list_empty(&dev->global_list)) {
		pci_proc_detach_device(dev);
		pci_remove_sysfs_dev_files(dev);
		device_unregister(&dev->dev);
		spin_lock(&pci_bus_lock);
		list_del(&dev->global_list);
		dev->global_list.next = dev->global_list.prev = NULL;
		spin_unlock(&pci_bus_lock);
	}

	/* Remove the device from the device lists, and prevent any further
	 * list accesses from this device */
	spin_lock(&pci_bus_lock);
	list_del(&dev->bus_list);
	dev->bus_list.next = dev->bus_list.prev = NULL;
	spin_unlock(&pci_bus_lock);

	pci_free_resources(dev);
	pci_dev_put(dev);
}

/**
 * pci_remove_device_safe - remove an unused hotplug device
 * @dev: the device to remove
 *
 * Delete the device structure from the device lists and 
 * notify userspace (/sbin/hotplug), but only if the device
 * in question is not being used by a driver.
 * Returns 0 on success.
 */
int pci_remove_device_safe(struct pci_dev *dev)
{
	if (pci_dev_driver(dev))
		return -EBUSY;
	pci_destroy_dev(dev);
	return 0;
}
EXPORT_SYMBOL(pci_remove_device_safe);

void pci_remove_bus(struct pci_bus *pci_bus)
{
	pci_proc_detach_bus(pci_bus);

	spin_lock(&pci_bus_lock);
	list_del(&pci_bus->node);
	spin_unlock(&pci_bus_lock);
	pci_remove_legacy_files(pci_bus);
	class_device_remove_file(&pci_bus->class_dev,
		&class_device_attr_cpuaffinity);
	sysfs_remove_link(&pci_bus->class_dev.kobj, "bridge");
	class_device_unregister(&pci_bus->class_dev);
}
EXPORT_SYMBOL(pci_remove_bus);

/**
 * pci_remove_bus_device - remove a PCI device and any children
 * @dev: the device to remove
 *
 * Remove a PCI device from the device lists, informing the drivers
 * that the device has been removed.  We also remove any subordinate
 * buses and children in a depth-first manner.
 *
 * For each device we remove, delete the device structure from the
 * device lists, remove the /proc entry, and notify userspace
 * (/sbin/hotplug).
 */
void pci_remove_bus_device(struct pci_dev *dev)
{
	if (dev->subordinate) {
		struct pci_bus *b = dev->subordinate;

		pci_remove_behind_bridge(dev);
		pci_remove_bus(b);
		dev->subordinate = NULL;
	}

	pci_destroy_dev(dev);
}

/**
 * pci_remove_behind_bridge - remove all devices behind a PCI bridge
 * @dev: PCI bridge device
 *
 * Remove all devices on the bus, except for the parent bridge.
 * This also removes any child buses, and any devices they may
 * contain in a depth-first manner.
 */
void pci_remove_behind_bridge(struct pci_dev *dev)
{
	struct list_head *l, *n;

	if (dev->subordinate) {
		list_for_each_safe(l, n, &dev->subordinate->devices) {
			struct pci_dev *dev = pci_dev_b(l);

			pci_remove_bus_device(dev);
		}
	}
}

EXPORT_SYMBOL(pci_remove_bus_device);
EXPORT_SYMBOL(pci_remove_behind_bridge);
