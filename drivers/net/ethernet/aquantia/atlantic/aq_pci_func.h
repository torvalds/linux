/*
 * aQuantia Corporation Network Driver
 * Copyright (C) 2014-2017 aQuantia Corporation. All rights reserved
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 */

/* File aq_pci_func.h: Declaration of PCI functions. */

#ifndef AQ_PCI_FUNC_H
#define AQ_PCI_FUNC_H

#include "aq_common.h"
#include "aq_nic.h"

struct aq_board_revision_s {
	unsigned short devid;
	unsigned short revision;
	const struct aq_hw_ops *ops;
	const struct aq_hw_caps_s *caps;
};

int aq_pci_func_init(struct aq_pci_func_s *self);
int aq_pci_func_alloc_irq(struct aq_pci_func_s *self, unsigned int i,
			  char *name, void *aq_vec,
			  cpumask_t *affinity_mask);
void aq_pci_func_free_irqs(struct aq_pci_func_s *self);
int aq_pci_func_start(struct aq_pci_func_s *self);
void __iomem *aq_pci_func_get_mmio(struct aq_pci_func_s *self);
unsigned int aq_pci_func_get_irq_type(struct aq_pci_func_s *self);
void aq_pci_func_deinit(struct aq_pci_func_s *self);
void aq_pci_func_free(struct aq_pci_func_s *self);
int aq_pci_func_change_pm_state(struct aq_pci_func_s *self,
				pm_message_t *pm_msg);

#endif /* AQ_PCI_FUNC_H */
