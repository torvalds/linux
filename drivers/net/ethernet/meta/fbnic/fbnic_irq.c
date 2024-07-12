// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) Meta Platforms, Inc. and affiliates. */

#include <linux/pci.h>
#include <linux/types.h>

#include "fbnic.h"

void fbnic_free_irqs(struct fbnic_dev *fbd)
{
	struct pci_dev *pdev = to_pci_dev(fbd->dev);

	fbd->num_irqs = 0;

	pci_free_irq_vectors(pdev);
}

int fbnic_alloc_irqs(struct fbnic_dev *fbd)
{
	unsigned int wanted_irqs = FBNIC_NON_NAPI_VECTORS;
	struct pci_dev *pdev = to_pci_dev(fbd->dev);
	int num_irqs;

	wanted_irqs += 1;
	num_irqs = pci_alloc_irq_vectors(pdev, FBNIC_NON_NAPI_VECTORS + 1,
					 wanted_irqs, PCI_IRQ_MSIX);
	if (num_irqs < 0) {
		dev_err(fbd->dev, "Failed to allocate MSI-X entries\n");
		return num_irqs;
	}

	if (num_irqs < wanted_irqs)
		dev_warn(fbd->dev, "Allocated %d IRQs, expected %d\n",
			 num_irqs, wanted_irqs);

	fbd->num_irqs = num_irqs;

	return 0;
}
