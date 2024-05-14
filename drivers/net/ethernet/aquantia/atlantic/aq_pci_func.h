/* SPDX-License-Identifier: GPL-2.0-only */
/* Atlantic Network Driver
 *
 * Copyright (C) 2014-2019 aQuantia Corporation
 * Copyright (C) 2019-2020 Marvell International Ltd.
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

int aq_pci_func_alloc_irq(struct aq_nic_s *self, unsigned int i,
			  char *name, irq_handler_t irq_handler,
			  void *irq_arg, cpumask_t *affinity_mask);
void aq_pci_func_free_irqs(struct aq_nic_s *self);
unsigned int aq_pci_func_get_irq_type(struct aq_nic_s *self);

int aq_pci_func_register_driver(void);
void aq_pci_func_unregister_driver(void);

#endif /* AQ_PCI_FUNC_H */
