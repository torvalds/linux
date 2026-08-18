/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) 2019-2020 Linaro Limited */

#ifndef XHCI_PCI_H
#define XHCI_PCI_H

#define PCI_DEVICE_ID_AMD_PROM21_XHCI_43FC	0x43fc
#define PCI_DEVICE_ID_AMD_PROM21_XHCI_43FD	0x43fd

int xhci_pci_common_probe(struct pci_dev *dev, const struct pci_device_id *id);
void xhci_pci_remove(struct pci_dev *dev);

#endif
