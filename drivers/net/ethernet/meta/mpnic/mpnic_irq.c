// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) Meta Platforms, Inc. and affiliates. */

#include <linux/cpumask.h>
#include <linux/interrupt.h>
#include <linux/minmax.h>
#include <linux/pci.h>

#include "mpnic.h"

int mpnic_request_irq(struct mpnic_dev *mpd, int nr, irq_handler_t handler,
		      unsigned long flags, const char *name, void *data)
{
	struct pci_dev *pdev = to_pci_dev(mpd->dev);
	int irq = pci_irq_vector(pdev, nr);

	if (irq < 0)
		return irq;

	return request_irq(irq, handler, flags, name, data);
}

void mpnic_free_irq(struct mpnic_dev *mpd, int nr, void *data)
{
	struct pci_dev *pdev = to_pci_dev(mpd->dev);
	int irq = pci_irq_vector(pdev, nr);

	if (irq < 0)
		return;

	free_irq(irq, data);
}

void mpnic_free_irqs(struct mpnic_dev *mpd)
{
	struct pci_dev *pdev = to_pci_dev(mpd->dev);

	mpd->num_irqs = 0;
	pci_free_irq_vectors(pdev);
}

int mpnic_alloc_irqs(struct mpnic_dev *mpd)
{
	unsigned int wanted_irqs = MPNIC_NON_NAPI_VECTORS;
	struct pci_dev *pdev = to_pci_dev(mpd->dev);
	int num_irqs;

	wanted_irqs += min_t(unsigned int, num_online_cpus(), MPNIC_MAX_RXQS);
	num_irqs = pci_alloc_irq_vectors(pdev, MPNIC_NON_NAPI_VECTORS + 1,
					 wanted_irqs, PCI_IRQ_MSIX);
	if (num_irqs < 0) {
		dev_err(mpd->dev, "Failed to allocate MSI-X entries: %d\n",
			num_irqs);
		return num_irqs;
	}

	if (num_irqs < wanted_irqs)
		dev_warn(mpd->dev, "Allocated %d IRQs, expected %u\n",
			 num_irqs, wanted_irqs);

	mpd->num_irqs = num_irqs;

	return 0;
}
