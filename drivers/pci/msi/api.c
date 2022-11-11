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

/**
 * pci_enable_msix_range() - Enable MSI-X interrupt mode on device
 * @dev:     the PCI device to operate on
 * @entries: input/output parameter, array of MSI-X configuration entries
 * @minvec:  minimum required number of MSI-X vectors
 * @maxvec:  maximum desired number of MSI-X vectors
 *
 * Legacy device driver API to enable MSI-X interrupt mode on device and
 * configure its MSI-X capability structure as appropriate.  The passed
 * @entries array must have each of its members "entry" field set to a
 * desired (valid) MSI-X vector number, where the range of valid MSI-X
 * vector numbers can be queried through pci_msix_vec_count().  If
 * successful, the driver must invoke pci_disable_msix() on cleanup.
 *
 * NOTE: The newer pci_alloc_irq_vectors() / pci_free_irq_vectors() API
 * pair should, in general, be used instead.
 *
 * Return: number of MSI-X vectors allocated (which might be smaller
 * than @maxvecs), where Linux IRQ numbers for such allocated vectors
 * are saved back in the @entries array elements' "vector" field. Return
 * -ENOSPC if less than @minvecs interrupt vectors are available.
 * Return -EINVAL if one of the passed @entries members "entry" field
 * was invalid or a duplicate, or if plain MSI interrupts mode was
 * earlier enabled on device. Return other errnos otherwise.
 */
int pci_enable_msix_range(struct pci_dev *dev, struct msix_entry *entries,
			  int minvec, int maxvec)
{
	return __pci_enable_msix_range(dev, entries, minvec, maxvec, NULL, 0);
}
EXPORT_SYMBOL(pci_enable_msix_range);

/**
 * pci_alloc_irq_vectors() - Allocate multiple device interrupt vectors
 * @dev:      the PCI device to operate on
 * @min_vecs: minimum required number of vectors (must be >= 1)
 * @max_vecs: maximum desired number of vectors
 * @flags:    One or more of:
 *            %PCI_IRQ_MSIX      Allow trying MSI-X vector allocations
 *            %PCI_IRQ_MSI       Allow trying MSI vector allocations
 *            %PCI_IRQ_LEGACY    Allow trying legacy INTx interrupts, if
 *                               and only if @min_vecs == 1
 *            %PCI_IRQ_AFFINITY  Auto-manage IRQs affinity by spreading
 *                               the vectors around available CPUs
 *
 * Allocate up to @max_vecs interrupt vectors on device. MSI-X irq
 * vector allocation has a higher precedence over plain MSI, which has a
 * higher precedence over legacy INTx emulation.
 *
 * Upon a successful allocation, the caller should use pci_irq_vector()
 * to get the Linux IRQ number to be passed to request_threaded_irq().
 * The driver must call pci_free_irq_vectors() on cleanup.
 *
 * Return: number of allocated vectors (which might be smaller than
 * @max_vecs), -ENOSPC if less than @min_vecs interrupt vectors are
 * available, other errnos otherwise.
 */
int pci_alloc_irq_vectors(struct pci_dev *dev, unsigned int min_vecs,
			  unsigned int max_vecs, unsigned int flags)
{
	return pci_alloc_irq_vectors_affinity(dev, min_vecs, max_vecs,
					      flags, NULL);
}
EXPORT_SYMBOL(pci_alloc_irq_vectors);

/**
 * pci_alloc_irq_vectors_affinity() - Allocate multiple device interrupt
 *                                    vectors with affinity requirements
 * @dev:      the PCI device to operate on
 * @min_vecs: minimum required number of vectors (must be >= 1)
 * @max_vecs: maximum desired number of vectors
 * @flags:    allocation flags, as in pci_alloc_irq_vectors()
 * @affd:     affinity requirements (can be %NULL).
 *
 * Same as pci_alloc_irq_vectors(), but with the extra @affd parameter.
 * Check that function docs, and &struct irq_affinity, for more details.
 */
int pci_alloc_irq_vectors_affinity(struct pci_dev *dev, unsigned int min_vecs,
				   unsigned int max_vecs, unsigned int flags,
				   struct irq_affinity *affd)
{
	struct irq_affinity msi_default_affd = {0};
	int nvecs = -ENOSPC;

	if (flags & PCI_IRQ_AFFINITY) {
		if (!affd)
			affd = &msi_default_affd;
	} else {
		if (WARN_ON(affd))
			affd = NULL;
	}

	if (flags & PCI_IRQ_MSIX) {
		nvecs = __pci_enable_msix_range(dev, NULL, min_vecs, max_vecs,
						affd, flags);
		if (nvecs > 0)
			return nvecs;
	}

	if (flags & PCI_IRQ_MSI) {
		nvecs = __pci_enable_msi_range(dev, min_vecs, max_vecs, affd);
		if (nvecs > 0)
			return nvecs;
	}

	/* use legacy IRQ if allowed */
	if (flags & PCI_IRQ_LEGACY) {
		if (min_vecs == 1 && dev->irq) {
			/*
			 * Invoke the affinity spreading logic to ensure that
			 * the device driver can adjust queue configuration
			 * for the single interrupt case.
			 */
			if (affd)
				irq_create_affinity_masks(1, affd);
			pci_intx(dev, 1);
			return 1;
		}
	}

	return nvecs;
}
EXPORT_SYMBOL(pci_alloc_irq_vectors_affinity);
