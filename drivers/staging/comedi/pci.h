/*
 * linux/pci.h compatibility header
 */

#ifndef _COMPAT_PCI_H
#define _COMPAT_PCI_H

#include <linux/version.h>

#include <linux/pci.h>

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,0)
#define pci_get_device pci_find_device
#define pci_get_subsys pci_find_subsys
#define pci_dev_get(x)	(x)
#define pci_dev_put(x)
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,4,22)
static inline char *pci_name(struct pci_dev *pdev)
{
	return pdev->slot_name;
}
#endif

#ifndef DEFINE_PCI_DEVICE_TABLE
#define DEFINE_PCI_DEVICE_TABLE(_table) \
	struct pci_device_id _table[]
#endif

#endif /* _COMPAT_PCI_H */
