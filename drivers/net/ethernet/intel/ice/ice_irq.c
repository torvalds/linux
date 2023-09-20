// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) 2023, Intel Corporation. */

#include "ice.h"
#include "ice_lib.h"
#include "ice_irq.h"

/**
 * ice_init_irq_tracker - initialize interrupt tracker
 * @pf: board private structure
 * @max_vectors: maximum number of vectors that tracker can hold
 * @num_static: number of preallocated interrupts
 */
static void
ice_init_irq_tracker(struct ice_pf *pf, unsigned int max_vectors,
		     unsigned int num_static)
{
	pf->irq_tracker.num_entries = max_vectors;
	pf->irq_tracker.num_static = num_static;
	xa_init_flags(&pf->irq_tracker.entries, XA_FLAGS_ALLOC);
}

/**
 * ice_deinit_irq_tracker - free xarray tracker
 * @pf: board private structure
 */
static void ice_deinit_irq_tracker(struct ice_pf *pf)
{
	xa_destroy(&pf->irq_tracker.entries);
}

/**
 * ice_free_irq_res - free a block of resources
 * @pf: board private structure
 * @index: starting index previously returned by ice_get_res
 */
static void ice_free_irq_res(struct ice_pf *pf, u16 index)
{
	struct ice_irq_entry *entry;

	entry = xa_erase(&pf->irq_tracker.entries, index);
	kfree(entry);
}

/**
 * ice_get_irq_res - get an interrupt resource
 * @pf: board private structure
 * @dyn_only: force entry to be dynamically allocated
 *
 * Allocate new irq entry in the free slot of the tracker. Since xarray
 * is used, always allocate new entry at the lowest possible index. Set
 * proper allocation limit for maximum tracker entries.
 *
 * Returns allocated irq entry or NULL on failure.
 */
static struct ice_irq_entry *ice_get_irq_res(struct ice_pf *pf, bool dyn_only)
{
	struct xa_limit limit = { .max = pf->irq_tracker.num_entries,
				  .min = 0 };
	unsigned int num_static = pf->irq_tracker.num_static;
	struct ice_irq_entry *entry;
	unsigned int index;
	int ret;

	entry = kzalloc(sizeof(*entry), GFP_KERNEL);
	if (!entry)
		return NULL;

	/* skip preallocated entries if the caller says so */
	if (dyn_only)
		limit.min = num_static;

	ret = xa_alloc(&pf->irq_tracker.entries, &index, entry, limit,
		       GFP_KERNEL);

	if (ret) {
		kfree(entry);
		entry = NULL;
	} else {
		entry->index = index;
		entry->dynamic = index >= num_static;
	}

	return entry;
}

/**
 * ice_reduce_msix_usage - Reduce usage of MSI-X vectors
 * @pf: board private structure
 * @v_remain: number of remaining MSI-X vectors to be distributed
 *
 * Reduce the usage of MSI-X vectors when entire request cannot be fulfilled.
 * pf->num_lan_msix and pf->num_rdma_msix values are set based on number of
 * remaining vectors.
 */
static void ice_reduce_msix_usage(struct ice_pf *pf, int v_remain)
{
	int v_rdma;

	if (!ice_is_rdma_ena(pf)) {
		pf->num_lan_msix = v_remain;
		return;
	}

	/* RDMA needs at least 1 interrupt in addition to AEQ MSIX */
	v_rdma = ICE_RDMA_NUM_AEQ_MSIX + 1;

	if (v_remain < ICE_MIN_LAN_TXRX_MSIX + ICE_MIN_RDMA_MSIX) {
		dev_warn(ice_pf_to_dev(pf), "Not enough MSI-X vectors to support RDMA.\n");
		clear_bit(ICE_FLAG_RDMA_ENA, pf->flags);

		pf->num_rdma_msix = 0;
		pf->num_lan_msix = ICE_MIN_LAN_TXRX_MSIX;
	} else if ((v_remain < ICE_MIN_LAN_TXRX_MSIX + v_rdma) ||
		   (v_remain - v_rdma < v_rdma)) {
		/* Support minimum RDMA and give remaining vectors to LAN MSIX
		 */
		pf->num_rdma_msix = ICE_MIN_RDMA_MSIX;
		pf->num_lan_msix = v_remain - ICE_MIN_RDMA_MSIX;
	} else {
		/* Split remaining MSIX with RDMA after accounting for AEQ MSIX
		 */
		pf->num_rdma_msix = (v_remain - ICE_RDMA_NUM_AEQ_MSIX) / 2 +
				    ICE_RDMA_NUM_AEQ_MSIX;
		pf->num_lan_msix = v_remain - pf->num_rdma_msix;
	}
}

/**
 * ice_ena_msix_range - Request a range of MSIX vectors from the OS
 * @pf: board private structure
 *
 * Compute the number of MSIX vectors wanted and request from the OS. Adjust
 * device usage if there are not enough vectors. Return the number of vectors
 * reserved or negative on failure.
 */
static int ice_ena_msix_range(struct ice_pf *pf)
{
	int num_cpus, hw_num_msix, v_other, v_wanted, v_actual;
	struct device *dev = ice_pf_to_dev(pf);
	int err;

	hw_num_msix = pf->hw.func_caps.common_cap.num_msix_vectors;
	num_cpus = num_online_cpus();

	/* LAN miscellaneous handler */
	v_other = ICE_MIN_LAN_OICR_MSIX;

	/* Flow Director */
	if (test_bit(ICE_FLAG_FD_ENA, pf->flags))
		v_other += ICE_FDIR_MSIX;

	/* switchdev */
	v_other += ICE_ESWITCH_MSIX;

	v_wanted = v_other;

	/* LAN traffic */
	pf->num_lan_msix = num_cpus;
	v_wanted += pf->num_lan_msix;

	/* RDMA auxiliary driver */
	if (ice_is_rdma_ena(pf)) {
		pf->num_rdma_msix = num_cpus + ICE_RDMA_NUM_AEQ_MSIX;
		v_wanted += pf->num_rdma_msix;
	}

	if (v_wanted > hw_num_msix) {
		int v_remain;

		dev_warn(dev, "not enough device MSI-X vectors. wanted = %d, available = %d\n",
			 v_wanted, hw_num_msix);

		if (hw_num_msix < ICE_MIN_MSIX) {
			err = -ERANGE;
			goto exit_err;
		}

		v_remain = hw_num_msix - v_other;
		if (v_remain < ICE_MIN_LAN_TXRX_MSIX) {
			v_other = ICE_MIN_MSIX - ICE_MIN_LAN_TXRX_MSIX;
			v_remain = ICE_MIN_LAN_TXRX_MSIX;
		}

		ice_reduce_msix_usage(pf, v_remain);
		v_wanted = pf->num_lan_msix + pf->num_rdma_msix + v_other;

		dev_notice(dev, "Reducing request to %d MSI-X vectors for LAN traffic.\n",
			   pf->num_lan_msix);
		if (ice_is_rdma_ena(pf))
			dev_notice(dev, "Reducing request to %d MSI-X vectors for RDMA.\n",
				   pf->num_rdma_msix);
	}

	/* actually reserve the vectors */
	v_actual = pci_alloc_irq_vectors(pf->pdev, ICE_MIN_MSIX, v_wanted,
					 PCI_IRQ_MSIX);
	if (v_actual < 0) {
		dev_err(dev, "unable to reserve MSI-X vectors\n");
		err = v_actual;
		goto exit_err;
	}

	if (v_actual < v_wanted) {
		dev_warn(dev, "not enough OS MSI-X vectors. requested = %d, obtained = %d\n",
			 v_wanted, v_actual);

		if (v_actual < ICE_MIN_MSIX) {
			/* error if we can't get minimum vectors */
			pci_free_irq_vectors(pf->pdev);
			err = -ERANGE;
			goto exit_err;
		} else {
			int v_remain = v_actual - v_other;

			if (v_remain < ICE_MIN_LAN_TXRX_MSIX)
				v_remain = ICE_MIN_LAN_TXRX_MSIX;

			ice_reduce_msix_usage(pf, v_remain);

			dev_notice(dev, "Enabled %d MSI-X vectors for LAN traffic.\n",
				   pf->num_lan_msix);

			if (ice_is_rdma_ena(pf))
				dev_notice(dev, "Enabled %d MSI-X vectors for RDMA.\n",
					   pf->num_rdma_msix);
		}
	}

	return v_actual;

exit_err:
	pf->num_rdma_msix = 0;
	pf->num_lan_msix = 0;
	return err;
}

/**
 * ice_clear_interrupt_scheme - Undo things done by ice_init_interrupt_scheme
 * @pf: board private structure
 */
void ice_clear_interrupt_scheme(struct ice_pf *pf)
{
	pci_free_irq_vectors(pf->pdev);
	ice_deinit_irq_tracker(pf);
}

/**
 * ice_init_interrupt_scheme - Determine proper interrupt scheme
 * @pf: board private structure to initialize
 */
int ice_init_interrupt_scheme(struct ice_pf *pf)
{
	int total_vectors = pf->hw.func_caps.common_cap.num_msix_vectors;
	int vectors, max_vectors;

	vectors = ice_ena_msix_range(pf);

	if (vectors < 0)
		return -ENOMEM;

	if (pci_msix_can_alloc_dyn(pf->pdev))
		max_vectors = total_vectors;
	else
		max_vectors = vectors;

	ice_init_irq_tracker(pf, max_vectors, vectors);

	return 0;
}

/**
 * ice_alloc_irq - Allocate new interrupt vector
 * @pf: board private structure
 * @dyn_only: force dynamic allocation of the interrupt
 *
 * Allocate new interrupt vector for a given owner id.
 * return struct msi_map with interrupt details and track
 * allocated interrupt appropriately.
 *
 * This function reserves new irq entry from the irq_tracker.
 * if according to the tracker information all interrupts that
 * were allocated with ice_pci_alloc_irq_vectors are already used
 * and dynamically allocated interrupts are supported then new
 * interrupt will be allocated with pci_msix_alloc_irq_at.
 *
 * Some callers may only support dynamically allocated interrupts.
 * This is indicated with dyn_only flag.
 *
 * On failure, return map with negative .index. The caller
 * is expected to check returned map index.
 *
 */
struct msi_map ice_alloc_irq(struct ice_pf *pf, bool dyn_only)
{
	int sriov_base_vector = pf->sriov_base_vector;
	struct msi_map map = { .index = -ENOENT };
	struct device *dev = ice_pf_to_dev(pf);
	struct ice_irq_entry *entry;

	entry = ice_get_irq_res(pf, dyn_only);
	if (!entry)
		return map;

	/* fail if we're about to violate SRIOV vectors space */
	if (sriov_base_vector && entry->index >= sriov_base_vector)
		goto exit_free_res;

	if (pci_msix_can_alloc_dyn(pf->pdev) && entry->dynamic) {
		map = pci_msix_alloc_irq_at(pf->pdev, entry->index, NULL);
		if (map.index < 0)
			goto exit_free_res;
		dev_dbg(dev, "allocated new irq at index %d\n", map.index);
	} else {
		map.index = entry->index;
		map.virq = pci_irq_vector(pf->pdev, map.index);
	}

	return map;

exit_free_res:
	dev_err(dev, "Could not allocate irq at idx %d\n", entry->index);
	ice_free_irq_res(pf, entry->index);
	return map;
}

/**
 * ice_free_irq - Free interrupt vector
 * @pf: board private structure
 * @map: map with interrupt details
 *
 * Remove allocated interrupt from the interrupt tracker. If interrupt was
 * allocated dynamically, free respective interrupt vector.
 */
void ice_free_irq(struct ice_pf *pf, struct msi_map map)
{
	struct ice_irq_entry *entry;

	entry = xa_load(&pf->irq_tracker.entries, map.index);

	if (!entry) {
		dev_err(ice_pf_to_dev(pf), "Failed to get MSIX interrupt entry at index %d",
			map.index);
		return;
	}

	dev_dbg(ice_pf_to_dev(pf), "Free irq at index %d\n", map.index);

	if (entry->dynamic)
		pci_msix_free_irq(pf->pdev, map);

	ice_free_irq_res(pf, map.index);
}

/**
 * ice_get_max_used_msix_vector - Get the max used interrupt vector
 * @pf: board private structure
 *
 * Return index of maximum used interrupt vectors with respect to the
 * beginning of the MSIX table. Take into account that some interrupts
 * may have been dynamically allocated after MSIX was initially enabled.
 */
int ice_get_max_used_msix_vector(struct ice_pf *pf)
{
	unsigned long start, index, max_idx;
	void *entry;

	/* Treat all preallocated interrupts as used */
	start = pf->irq_tracker.num_static;
	max_idx = start - 1;

	xa_for_each_start(&pf->irq_tracker.entries, index, entry, start) {
		if (index > max_idx)
			max_idx = index;
	}

	return max_idx;
}
