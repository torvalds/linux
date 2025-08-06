/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright IBM Corp. 2020
 *
 * Author(s):
 *   Pierre Morel <pmorel@linux.ibm.com>
 *
 */
#ifndef __S390_PCI_BUS_H
#define __S390_PCI_BUS_H

#include <linux/pci.h>

int zpci_bus_device_register(struct zpci_dev *zdev, struct pci_ops *ops);
void zpci_bus_device_unregister(struct zpci_dev *zdev);

int zpci_bus_scan_bus(struct zpci_bus *zbus);
void zpci_bus_scan_busses(void);

int zpci_bus_scan_device(struct zpci_dev *zdev);
void zpci_bus_remove_device(struct zpci_dev *zdev, bool set_error);

void zpci_release_device(struct kref *kref);

void zpci_zdev_put(struct zpci_dev *zdev);

static inline void zpci_zdev_get(struct zpci_dev *zdev)
{
	kref_get(&zdev->kref);
}

int zpci_alloc_domain(int domain);
void zpci_free_domain(int domain);
int zpci_setup_bus_resources(struct zpci_dev *zdev);

static inline struct zpci_dev *zdev_from_bus(struct pci_bus *bus,
					     unsigned int devfn)
{
	struct zpci_bus *zbus = bus->sysdata;

	return (devfn >= ZPCI_FUNCTIONS_PER_BUS) ? NULL : zbus->function[devfn];
}

#endif /* __S390_PCI_BUS_H */
