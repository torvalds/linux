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
#include <linux/irq.h>

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
 * pci_msix_vec_count() - Get number of MSI-X interrupt vectors on device
 * @dev: the PCI device to operate on
 *
 * Return: number of MSI-X interrupt vectors available on this device
 * (i.e., the device's MSI-X capability structure "table size"), -EINVAL
 * if the device is not MSI-X capable, other errnos otherwise.
 */
int pci_msix_vec_count(struct pci_dev *dev)
{
	u16 control;

	if (!dev->msix_cap)
		return -EINVAL;

	pci_read_config_word(dev, dev->msix_cap + PCI_MSIX_FLAGS, &control);
	return msix_table_size(control);
}
EXPORT_SYMBOL(pci_msix_vec_count);

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
 * pci_msix_can_alloc_dyn - Query whether dynamic allocation after enabling
 *			    MSI-X is supported
 *
 * @dev:	PCI device to operate on
 *
 * Return: True if supported, false otherwise
 */
bool pci_msix_can_alloc_dyn(struct pci_dev *dev)
{
	if (!dev->msix_cap)
		return false;

	return pci_msi_domain_supports(dev, MSI_FLAG_PCI_MSIX_ALLOC_DYN, DENY_LEGACY);
}
EXPORT_SYMBOL_GPL(pci_msix_can_alloc_dyn);

/**
 * pci_msix_alloc_irq_at - Allocate an MSI-X interrupt after enabling MSI-X
 *			   at a given MSI-X vector index or any free vector index
 *
 * @dev:	PCI device to operate on
 * @index:	Index to allocate. If @index == MSI_ANY_INDEX this allocates
 *		the next free index in the MSI-X table
 * @affdesc:	Optional pointer to an affinity descriptor structure. NULL otherwise
 *
 * Return: A struct msi_map
 *
 *	On success msi_map::index contains the allocated index (>= 0) and
 *	msi_map::virq contains the allocated Linux interrupt number (> 0).
 *
 *	On fail msi_map::index contains the error code and msi_map::virq
 *	is set to 0.
 */
struct msi_map pci_msix_alloc_irq_at(struct pci_dev *dev, unsigned int index,
				     const struct irq_affinity_desc *affdesc)
{
	struct msi_map map = { .index = -ENOTSUPP };

	if (!dev->msix_enabled)
		return map;

	if (!pci_msix_can_alloc_dyn(dev))
		return map;

	return msi_domain_alloc_irq_at(&dev->dev, MSI_DEFAULT_DOMAIN, index, affdesc, NULL);
}
EXPORT_SYMBOL_GPL(pci_msix_alloc_irq_at);

/**
 * pci_msix_free_irq - Free an interrupt on a PCI/MSI-X interrupt domain
 *
 * @dev:	The PCI device to operate on
 * @map:	A struct msi_map describing the interrupt to free
 *
 * Undo an interrupt vector allocation. Does not disable MSI-X.
 */
void pci_msix_free_irq(struct pci_dev *dev, struct msi_map map)
{
	if (WARN_ON_ONCE(map.index < 0 || map.virq <= 0))
		return;
	if (WARN_ON_ONCE(!pci_msix_can_alloc_dyn(dev)))
		return;
	msi_domain_free_irqs_range(&dev->dev, MSI_DEFAULT_DOMAIN, map.index, map.index);
}
EXPORT_SYMBOL_GPL(pci_msix_free_irq);

/**
 * pci_disable_msix() - Disable MSI-X interrupt mode on device
 * @dev: the PCI device to operate on
 *
 * Legacy device driver API to disable MSI-X interrupt mode on device,
 * free earlier-allocated interrupt vectors, and restore INTx.
 * The PCI device Linux IRQ (@dev->irq) is restored to its default pin
 * assertion IRQ. This is the cleanup pair of pci_enable_msix_range().
 *
 * NOTE: The newer pci_alloc_irq_vectors() / pci_free_irq_vectors() API
 * pair should, in general, be used instead.
 */
void pci_disable_msix(struct pci_dev *dev)
{
	if (!pci_msi_enabled() || !dev || !dev->msix_enabled)
		return;

	msi_lock_descs(&dev->dev);
	pci_msix_shutdown(dev);
	pci_free_msi_irqs(dev);
	msi_unlock_descs(&dev->dev);
}
EXPORT_SYMBOL(pci_disable_msix);

/**
 * pci_alloc_irq_vectors() - Allocate multiple device interrupt vectors
 * @dev:      the PCI device to operate on
 * @min_vecs: minimum required number of vectors (must be >= 1)
 * @max_vecs: maximum desired number of vectors
 * @flags:    One or more of:
 *
 *            * %PCI_IRQ_MSIX      Allow trying MSI-X vector allocations
 *            * %PCI_IRQ_MSI       Allow trying MSI vector allocations
 *
 *            * %PCI_IRQ_INTX      Allow trying INTx interrupts, if and
 *              only if @min_vecs == 1
 *
 *            * %PCI_IRQ_AFFINITY  Auto-manage IRQs affinity by spreading
 *              the vectors around available CPUs
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

	/* use INTx IRQ if allowed */
	if (flags & PCI_IRQ_INTX) {
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

/**
 * pci_irq_vector() - Get Linux IRQ number of a device interrupt vector
 * @dev: the PCI device to operate on
 * @nr:  device-relative interrupt vector index (0-based); has different
 *       meanings, depending on interrupt mode:
 *
 *         * MSI-X     the index in the MSI-X vector table
 *         * MSI       the index of the enabled MSI vectors
 *         * INTx      must be 0
 *
 * Return: the Linux IRQ number, or -EINVAL if @nr is out of range
 */
int pci_irq_vector(struct pci_dev *dev, unsigned int nr)
{
	unsigned int irq;

	if (!dev->msi_enabled && !dev->msix_enabled)
		return !nr ? dev->irq : -EINVAL;

	irq = msi_get_virq(&dev->dev, nr);
	return irq ? irq : -EINVAL;
}
EXPORT_SYMBOL(pci_irq_vector);

/**
 * pci_irq_get_affinity() - Get a device interrupt vector affinity
 * @dev: the PCI device to operate on
 * @nr:  device-relative interrupt vector index (0-based); has different
 *       meanings, depending on interrupt mode:
 *
 *         * MSI-X     the index in the MSI-X vector table
 *         * MSI       the index of the enabled MSI vectors
 *         * INTx      must be 0
 *
 * Return: MSI/MSI-X vector affinity, NULL if @nr is out of range or if
 * the MSI(-X) vector was allocated without explicit affinity
 * requirements (e.g., by pci_enable_msi(), pci_enable_msix_range(), or
 * pci_alloc_irq_vectors() without the %PCI_IRQ_AFFINITY flag). Return a
 * generic set of CPU IDs representing all possible CPUs available
 * during system boot if the device is in legacy INTx mode.
 */
const struct cpumask *pci_irq_get_affinity(struct pci_dev *dev, int nr)
{
	int idx, irq = pci_irq_vector(dev, nr);
	struct msi_desc *desc;

	if (WARN_ON_ONCE(irq <= 0))
		return NULL;

	desc = irq_get_msi_desc(irq);
	/* Non-MSI does not have the information handy */
	if (!desc)
		return cpu_possible_mask;

	/* MSI[X] interrupts can be allocated without affinity descriptor */
	if (!desc->affinity)
		return NULL;

	/*
	 * MSI has a mask array in the descriptor.
	 * MSI-X has a single mask.
	 */
	idx = dev->msi_enabled ? nr : 0;
	return &desc->affinity[idx].mask;
}
EXPORT_SYMBOL(pci_irq_get_affinity);

/**
 * pci_free_irq_vectors() - Free previously allocated IRQs for a device
 * @dev: the PCI device to operate on
 *
 * Undo the interrupt vector allocations and possible device MSI/MSI-X
 * enablement earlier done through pci_alloc_irq_vectors_affinity() or
 * pci_alloc_irq_vectors().
 */
void pci_free_irq_vectors(struct pci_dev *dev)
{
	pci_disable_msix(dev);
	pci_disable_msi(dev);
}
EXPORT_SYMBOL(pci_free_irq_vectors);

/**
 * pci_restore_msi_state() - Restore cached MSI(-X) state on device
 * @dev: the PCI device to operate on
 *
 * Write the Linux-cached MSI(-X) state back on device. This is
 * typically useful upon system resume, or after an error-recovery PCI
 * adapter reset.
 */
void pci_restore_msi_state(struct pci_dev *dev)
{
	__pci_restore_msi_state(dev);
	__pci_restore_msix_state(dev);
}
EXPORT_SYMBOL_GPL(pci_restore_msi_state);

/**
 * pci_msi_enabled() - Are MSI(-X) interrupts enabled system-wide?
 *
 * Return: true if MSI has not been globally disabled through ACPI FADT,
 * PCI bridge quirks, or the "pci=nomsi" kernel command-line option.
 */
int pci_msi_enabled(void)
{
	return pci_msi_enable;
}
EXPORT_SYMBOL(pci_msi_enabled);
