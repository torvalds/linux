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

#ifdef CONFIG_PCI_IOV
void zpci_iov_remove_virtfn(struct pci_dev *pdev, int vfn);

void zpci_iov_map_resources(struct pci_dev *pdev);

int zpci_iov_setup_virtfn(struct zpci_bus *zbus, struct pci_dev *virtfn, int vfn);

#else /* CONFIG_PCI_IOV */
static inline void zpci_iov_remove_virtfn(struct pci_dev *pdev, int vfn) {}

static inline void zpci_iov_map_resources(struct pci_dev *pdev) {}

static inline int zpci_iov_setup_virtfn(struct zpci_bus *zbus, struct pci_dev *virtfn, int vfn)
{
	return 0;
}
#endif /* CONFIG_PCI_IOV */
#endif /* __S390_PCI_IOV_h */
