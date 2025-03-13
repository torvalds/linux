/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright IBM Corp. 2020
 *
 * Author(s):
 *   Niklas Schnelle <schnelle@linux.ibm.com>
 *
 */

#ifndef __S390_PCI_IOV_H
#define __S390_PCI_IOV_H

#include <linux/pci.h>

#ifdef CONFIG_PCI_IOV
void zpci_iov_remove_virtfn(struct pci_dev *pdev, int vfn);

void zpci_iov_map_resources(struct pci_dev *pdev);

int zpci_iov_setup_virtfn(struct zpci_bus *zbus, struct pci_dev *virtfn, int vfn);

struct pci_dev *zpci_iov_find_parent_pf(struct zpci_bus *zbus, struct zpci_dev *zdev);

#else /* CONFIG_PCI_IOV */
static inline void zpci_iov_remove_virtfn(struct pci_dev *pdev, int vfn) {}

static inline void zpci_iov_map_resources(struct pci_dev *pdev) {}

static inline int zpci_iov_setup_virtfn(struct zpci_bus *zbus, struct pci_dev *virtfn, int vfn)
{
	return 0;
}

static inline struct pci_dev *zpci_iov_find_parent_pf(struct zpci_bus *zbus, struct zpci_dev *zdev)
{
	return NULL;
}
#endif /* CONFIG_PCI_IOV */
#endif /* __S390_PCI_IOV_h */
