/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2023-2024 Intel Corporation
 */

#ifndef _XE_PCI_SRIOV_H_
#define _XE_PCI_SRIOV_H_

struct pci_dev;

#ifdef CONFIG_PCI_IOV
int xe_pci_sriov_configure(struct pci_dev *pdev, int num_vfs);
#else
static inline int xe_pci_sriov_configure(struct pci_dev *pdev, int num_vfs)
{
	return 0;
}
#endif

#endif
