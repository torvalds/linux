/*
 * Copyright 2014 IBM Corp.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#ifndef _ASM_PNV_PCI_H
#define _ASM_PNV_PCI_H

#include <linux/pci.h>
#include <linux/irq.h>
#include <misc/cxl-base.h>

int pnv_phb_to_cxl_mode(struct pci_dev *dev, uint64_t mode);
int pnv_cxl_ioda_msi_setup(struct pci_dev *dev, unsigned int hwirq,
			   unsigned int virq);
int pnv_cxl_alloc_hwirqs(struct pci_dev *dev, int num);
void pnv_cxl_release_hwirqs(struct pci_dev *dev, int hwirq, int num);
int pnv_cxl_get_irq_count(struct pci_dev *dev);
struct device_node *pnv_pci_get_phb_node(struct pci_dev *dev);
int64_t pnv_opal_pci_msi_eoi(struct irq_chip *chip, unsigned int hw_irq);
bool is_pnv_opal_msi(struct irq_chip *chip);

#ifdef CONFIG_CXL_BASE
int pnv_cxl_alloc_hwirq_ranges(struct cxl_irq_ranges *irqs,
			       struct pci_dev *dev, int num);
void pnv_cxl_release_hwirq_ranges(struct cxl_irq_ranges *irqs,
				  struct pci_dev *dev);
#endif

#endif
