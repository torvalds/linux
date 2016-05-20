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
#include <misc/cxl-base.h>
#include <asm/opal-api.h>

#define PCI_SLOT_ID_PREFIX	0x8000000000000000
#define PCI_SLOT_ID(phb_id, bdfn)	\
	(PCI_SLOT_ID_PREFIX | ((uint64_t)(bdfn) << 16) | (phb_id))

extern int pnv_pci_get_slot_id(struct device_node *np, uint64_t *id);
extern int pnv_pci_get_device_tree(uint32_t phandle, void *buf, uint64_t len);
extern int pnv_pci_get_presence_state(uint64_t id, uint8_t *state);
extern int pnv_pci_get_power_state(uint64_t id, uint8_t *state);
extern int pnv_pci_set_power_state(uint64_t id, uint8_t state,
				   struct opal_msg *msg);

int pnv_phb_to_cxl_mode(struct pci_dev *dev, uint64_t mode);
int pnv_cxl_ioda_msi_setup(struct pci_dev *dev, unsigned int hwirq,
			   unsigned int virq);
int pnv_cxl_alloc_hwirqs(struct pci_dev *dev, int num);
void pnv_cxl_release_hwirqs(struct pci_dev *dev, int hwirq, int num);
int pnv_cxl_get_irq_count(struct pci_dev *dev);
struct device_node *pnv_pci_get_phb_node(struct pci_dev *dev);

#ifdef CONFIG_CXL_BASE
int pnv_cxl_alloc_hwirq_ranges(struct cxl_irq_ranges *irqs,
			       struct pci_dev *dev, int num);
void pnv_cxl_release_hwirq_ranges(struct cxl_irq_ranges *irqs,
				  struct pci_dev *dev);
#endif

#endif
