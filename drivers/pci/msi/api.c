// SPDX-License-Identifier: GPL-2.0
/*
 * PCI MSI/MSI-X — Exported APIs for device drivers
 *
 * Copyright (C) 2003-2004 Intel
 * Copyright (C) Tom Long Nguyen (tom.l.nguyen@intel.com)
 * Copyright (C) 2016 Christoph Hellwig.
 * Copyright (C) 2022 Linutronix GmbH
 */

#include <linux/export.h>

#include "msi.h"

/**
 * pci_enable_msi() - Enable MSI interrupt mode on device
 * @dev: the PCI device to operate on
 *
 * Legacy device driver API to enable MSI interrupts mode on device and
 * allocate a single interrupt vector. On success, the allocated vector
 * Linux IRQ will be saved at @dev->irq. The driver must invoke
 * pci_disable_msi() on cleanup.
 *
 * NOTE: The newer pci_alloc_irq_vectors() / pci_free_irq_vectors() API
 * pair should, in general, be used instead.
 *
 * Return: 0 on success, errno otherwise
 */
int pci_enable_msi(struct pci_dev *dev)
{
	int rc = __pci_enable_msi_range(dev, 1, 1, NULL);
	if (rc < 0)
		return rc;
	return 0;
}
EXPORT_SYMBOL(pci_enable_msi);

/**
 * pci_disable_msi() - Disable MSI interrupt mode on device
 * @dev: the PCI device to operate on
 *
 * Legacy device driver API to disable MSI interrupt mode on device,
 * free earlier allocated interrupt vectors, and restore INTx emulation.
 * The PCI device Linux IRQ (@dev->irq) is restored to its default
 * pin-assertion IRQ. This is the cleanup pair of pci_enable_msi().
 *
 * NOTE: The newer pci_alloc_irq_vectors() / pci_free_irq_vectors() API
 * pair should, in general, be used instead.
 */
void pci_disable_msi(struct pci_dev *dev)
{
	if (!pci_msi_enabled() || !dev || !dev->msi_enabled)
		return;

	msi_lock_descs(&dev->dev);
	pci_msi_shutdown(dev);
	pci_free_msi_irqs(dev);
	msi_unlock_descs(&dev->dev);
}
EXPORT_SYMBOL(pci_disable_msi);
