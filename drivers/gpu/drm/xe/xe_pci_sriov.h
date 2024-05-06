/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2023-2024 Intel Corporation
 */

#ifndef _XE_PCI_SRIOV_H_
#define _XE_PCI_SRIOV_H_

struct pci_dev;

int xe_pci_sriov_configure(struct pci_dev *pdev, int num_vfs);

#endif
