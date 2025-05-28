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

static int
ice_init_virt_irq_tracker(struct ice_pf *pf, u32 base, u32 num_entries)
{
	pf->virt_irq_tracker.bm = bitmap_zalloc(num_entries, GFP_KERNEL);
	if (!pf->virt_irq_tracker.bm)
		return -ENOMEM;

	pf->virt_irq_tracker.num_entries = num_entries;
	pf->virt_irq_tracker.base = base;

	return 0;
}

/**
 * ice_deinit_irq_tracker - free xarray tracker
 * @pf: board private structure
 */
static void ice_deinit_irq_tracker(struct ice_pf *pf)
{
	xa_destroy(&pf->irq_tracker.entries);
}

static void ice_deinit_virt_irq_tracker(struct ice_pf *pf)
{
	bitmap_free(pf->virt_irq_tracker.bm);
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
 * @dyn_allowed: allow entry to be dynamically allocated
 *
 * Allocate new irq entry in the free slot of the tracker. Since xarray
 * is used, always allocate new entry at the lowest possible index. Set
 * proper allocation limit for maximum tracker entries.
 *
 * Returns allocated irq entry or NULL on failure.
 */
static struct ice_irq_entry *ice_get_irq_res(struct ice_pf *pf,
					     bool dyn_allowed)
{
	struct xa_limit limit = { .max = pf->irq_tracker.num_entries - 1,
				  .min = 0 };
	unsigned int num_static = pf->irq_tracker.num_static - 1;
	struct ice_irq_entry *entry;
	unsigned int index;
	int ret;

	entry = kzalloc(sizeof(*entry), GFP_KERNEL);
	if (!entry)
		return NULL;

	/* only already allocated if the caller says so */
	if (!dyn_allowed)
		limit.max = num_static;

	ret = xa_alloc(&pf->irq_tracker.entries, &index, entry, limit,
		       GFP_KERNEL);

	if (ret) {
		kfree(entry);
		entry = NULL;
	} else {
		entry->index = index;
		entry->dynamic = index > num_static;
	}

	return entry;
}

#define ICE_RDMA_AEQ_MSIX 1
static int ice_get_default_msix_amount(struct ice_pf *pf)
{
	return ICE_MIN_LAN_OICR_MSIX + num_online_cpus() +
	       (test_bit(ICE_FLAG_FD_ENA, pf->flags) ? ICE_FDIR_MSIX : 0) +
	       (ice_is_rdma_ena(pf) ? num_online_cpus() + ICE_RDMA_AEQ_MSIX : 0);
}

/**
 * ice_clear_interrupt_scheme - Undo things done by ice_init_interrupt_scheme
 * @pf: board private structure
 */
void ice_clear_interrupt_scheme(struct ice_pf *pf)
{
	pci_free_irq_vectors(pf->pdev);
	ice_deinit_irq_tracker(pf);
	ice_deinit_virt_irq_tracker(pf);
}

/**
 * ice_init_interrupt_scheme - Determine proper interrupt scheme
 * @pf: board private structure to initialize
 */
int ice_init_interrupt_scheme(struct ice_pf *pf)
{
	int total_vectors = pf->hw.func_caps.common_cap.num_msix_vectors;
	int vectors;

	/* load default PF MSI-X range */
	if (!pf->msix.min)
		pf->msix.min = ICE_MIN_MSIX;

	if (!pf->msix.max)
		pf->msix.max = min(total_vectors,
				   ice_get_default_msix_amount(pf));

	pf->msix.total = total_vectors;
	pf->msix.rest = total_vectors - pf->msix.max;

	if (pci_msix_can_alloc_dyn(pf->pdev))
		vectors = pf->msix.min;
	else
		vectors = pf->msix.max;

	vectors = pci_alloc_irq_vectors(pf->pdev, pf->msix.min, vectors,
					PCI_IRQ_MSIX);
	if (vectors < 0)
		return vectors;

	ice_init_irq_tracker(pf, pf->msix.max, vectors);

	return ice_init_virt_irq_tracker(pf, pf->msix.max, pf->msix.rest);
}

/**
 * ice_alloc_irq - Allocate new interrupt vector
 * @pf: board private structure
 * @dyn_allowed: allow dynamic allocation of the interrupt
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
 * This is indicated with dyn_allowed flag.
 *
 * On failure, return map with negative .index. The caller
 * is expected to check returned map index.
 *
 */
struct msi_map ice_alloc_irq(struct ice_pf *pf, bool dyn_allowed)
{
	struct msi_map map = { .index = -ENOENT };
	struct device *dev = ice_pf_to_dev(pf);
	struct ice_irq_entry *entry;

	entry = ice_get_irq_res(pf, dyn_allowed);
	if (!entry)
		return map;

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
 * ice_virt_get_irqs - get irqs for SR-IOV usacase
 * @pf: pointer to PF structure
 * @needed: number of irqs to get
 *
 * This returns the first MSI-X vector index in PF space that is used by this
 * VF. This index is used when accessing PF relative registers such as
 * GLINT_VECT2FUNC and GLINT_DYN_CTL.
 * This will always be the OICR index in the AVF driver so any functionality
 * using vf->first_vector_idx for queue configuration_id: id of VF which will
 * use this irqs
 */
int ice_virt_get_irqs(struct ice_pf *pf, u32 needed)
{
	int res = bitmap_find_next_zero_area(pf->virt_irq_tracker.bm,
					     pf->virt_irq_tracker.num_entries,
					     0, needed, 0);

	if (res >= pf->virt_irq_tracker.num_entries)
		return -ENOENT;

	bitmap_set(pf->virt_irq_tracker.bm, res, needed);

	/* conversion from number in bitmap to global irq index */
	return res + pf->virt_irq_tracker.base;
}

/**
 * ice_virt_free_irqs - free irqs used by the VF
 * @pf: pointer to PF structure
 * @index: first index to be free
 * @irqs: number of irqs to free
 */
void ice_virt_free_irqs(struct ice_pf *pf, u32 index, u32 irqs)
{
	bitmap_clear(pf->virt_irq_tracker.bm, index - pf->virt_irq_tracker.base,
		     irqs);
}
