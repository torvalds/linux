/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) 2008 Intel Corp. */

#ifndef XHCI_PCI_H
#define XHCI_PCI_H

int xhci_pci_setup(struct usb_hcd *hcd);

int xhci_pci_probe(struct pci_dev *pdev,
		   const struct pci_device_id *id);

void xhci_pci_remove(struct pci_dev *dev);

int xhci_pci_suspend(struct usb_hcd *hcd, bool do_wakeup);

int xhci_pci_resume(struct usb_hcd *hcd, bool hibernated);

#endif
