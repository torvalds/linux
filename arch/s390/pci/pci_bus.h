/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright IBM Corp. 2020
 *
 * Author(s):
 *   Pierre Morel <pmorel@linux.ibm.com>
 *
 */

int zpci_bus_device_register(struct zpci_dev *zdev, struct pci_ops *ops);
void zpci_bus_device_unregister(struct zpci_dev *zdev);
int zpci_bus_init(void);

void zpci_release_device(struct kref *kref);
static inline void zpci_zdev_put(struct zpci_dev *zdev)
{
	kref_put(&zdev->kref, zpci_release_device);
}

int zpci_alloc_domain(int domain);
void zpci_free_domain(int domain);
int zpci_setup_bus_resources(struct zpci_dev *zdev,
			     struct list_head *resources);

static inline struct zpci_dev *get_zdev_by_bus(struct pci_bus *bus,
					       unsigned int devfn)
{
	struct zpci_bus *zbus = bus->sysdata;

	return (devfn >= ZPCI_FUNCTIONS_PER_BUS) ? NULL : zbus->function[devfn];
}

#ifdef CONFIG_PCI_IOV
static inline void zpci_remove_virtfn(struct pci_dev *pdev, int vfn)
{

	pci_lock_rescan_remove();
	/* Linux' vfid's start at 0 vfn at 1 */
	pci_iov_remove_virtfn(pdev->physfn, vfn - 1);
	pci_unlock_rescan_remove();
}
#else /* CONFIG_PCI_IOV */
static inline void zpci_remove_virtfn(struct pci_dev *pdev, int vfn) {}
#endif /* CONFIG_PCI_IOV */
