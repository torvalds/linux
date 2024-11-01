/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2021 Intel Corporation
 */

#ifndef __I915_PCI_H__
#define __I915_PCI_H__

#include <linux/types.h>

struct pci_dev;

int i915_pci_register_driver(void);
void i915_pci_unregister_driver(void);

bool i915_pci_resource_valid(struct pci_dev *pdev, int bar);

#endif /* __I915_PCI_H__ */
