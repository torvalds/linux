// SPDX-License-Identifier: GPL-2.0
#include <linux/pci.h>
#include <linux/module.h>
#include "pci.h"

static void pci_free_resources(struct pci_dev *dev)
{
	int i;

	for (i = 0; i < PCI_NUM_RESOURCES; i++) {
		struct resource *res = dev->resource + i;
		if (res->parent)
			release_resource(res);
	}
}

static void pci_stop_dev(struct pci_dev *dev)
{
	pci_pme_active(dev, false);

	if (pci_dev_is_added(dev)) {

		device_release_driver(&dev->dev);
		pci_proc_detach_device(dev);
		pci_remove_sysfs_dev_files(dev);

		pci_dev_assign_added(dev, false);
	}
}

static void pci_destroy_dev(struct pci_dev *dev)
{
	if (!dev->dev.kobj.parent)
		return;

	device_del(&dev->dev);

	down_write(&pci_bus_sem);
	list_del(&dev->bus_list);
	up_write(&pci_bus_sem);

	pcie_aspm_exit_link_state(dev);
	pci_bridge_d3_update(dev);
	pci_free_resources(dev);
	put_device(&dev->dev);
}

void pci_remove_bus(struct pci_bus *bus)
{
	pci_proc_detach_bus(bus);

	down_write(&pci_bus_sem);
	list_del(&bus->node);
	pci_bus_release_busn_res(bus);
	up_write(&pci_bus_sem);
	pci_remove_legacy_files(bus);

	if (bus->ops->remove_bus)
		bus->ops->remove_bus(bus);

	pcibios_remove_bus(bus);
	device_unregister(&bus->dev);
}
EXPORT_SYMBOL(pci_remove_bus);

static void pci_stop_bus_device(struct pci_dev *dev)
{
	struct pci_bus *bus = dev->subordinate;
	struct pci_dev *child, *tmp;

	/*
	 * Stopping an SR-IOV PF device removes all the associated VFs,
	 * which will update the bus->devices list and confuse the
	 * iterator.  Therefore, iterate in reverse so we remove the VFs
	 * first, then the PF.
	 */
	if (bus) {
		list_for_each_entry_safe_reverse(child, tmp,
						 &bus->devices, bus_list)
			pci_stop_bus_device(child);
	}

	pci_stop_dev(dev);
}

static void pci_remove_bus_device(struct pci_dev *dev)
{
	struct pci_bus *bus = dev->subordinate;
	struct pci_dev *child, *tmp;

	if (bus) {
		list_for_each_entry_safe(child, tmp,
					 &bus->devices, bus_list)
			pci_remove_bus_device(child);

		pci_remove_bus(bus);
		dev->subordinate = NULL;
	}

	pci_destroy_dev(dev);
}

/**
 * pci_stop_and_remove_bus_device - remove a PCI device and any children
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
void pci_stop_and_remove_bus_device(struct pci_dev *dev)
{
	pci_stop_bus_device(dev);
	pci_remove_bus_device(dev);
}
EXPORT_SYMBOL(pci_stop_and_remove_bus_device);

void pci_stop_and_remove_bus_device_locked(struct pci_dev *dev)
{
	pci_lock_rescan_remove();
	pci_stop_and_remove_bus_device(dev);
	pci_unlock_rescan_remove();
}
EXPORT_SYMBOL_GPL(pci_stop_and_remove_bus_device_locked);

void pci_stop_root_bus(struct pci_bus *bus)
{
	struct pci_dev *child, *tmp;
	struct pci_host_bridge *host_bridge;

	if (!pci_is_root_bus(bus))
		return;

	host_bridge = to_pci_host_bridge(bus->bridge);
	list_for_each_entry_safe_reverse(child, tmp,
					 &bus->devices, bus_list)
		pci_stop_bus_device(child);

	/* stop the host bridge */
	device_release_driver(&host_bridge->dev);
}
EXPORT_SYMBOL_GPL(pci_stop_root_bus);

void pci_remove_root_bus(struct pci_bus *bus)
{
	struct pci_dev *child, *tmp;
	struct pci_host_bridge *host_bridge;

	if (!pci_is_root_bus(bus))
		return;

	host_bridge = to_pci_host_bridge(bus->bridge);
	list_for_each_entry_safe(child, tmp,
				 &bus->devices, bus_list)
		pci_remove_bus_device(child);

#ifdef CONFIG_PCI_DOMAINS_GENERIC
	/* Release domain_nr if it was dynamically allocated */
	if (host_bridge->domain_nr == PCI_DOMAIN_NR_NOT_SET)
		pci_bus_release_domain_nr(bus, host_bridge->dev.parent);
#endif

	pci_remove_bus(bus);
	host_bridge->bus = NULL;

	/* remove the host bridge */
	device_del(&host_bridge->dev);
}
EXPORT_SYMBOL_GPL(pci_remove_root_bus);
