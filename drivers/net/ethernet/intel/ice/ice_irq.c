// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) 2023, Intel Corporation. */

#include "ice.h"
#include "ice_lib.h"
#include "ice_irq.h"

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

	if (pf->irq_tracker) {
		devm_kfree(ice_pf_to_dev(pf), pf->irq_tracker);
		pf->irq_tracker = NULL;
	}
}

/**
 * ice_init_interrupt_scheme - Determine proper interrupt scheme
 * @pf: board private structure to initialize
 */
int ice_init_interrupt_scheme(struct ice_pf *pf)
{
	int vectors;

	vectors = ice_ena_msix_range(pf);

	if (vectors < 0)
		return vectors;

	/* set up vector assignment tracking */
	pf->irq_tracker = devm_kzalloc(ice_pf_to_dev(pf),
				       struct_size(pf->irq_tracker, list,
						   vectors),
				       GFP_KERNEL);
	if (!pf->irq_tracker) {
		pci_free_irq_vectors(pf->pdev);
		return -ENOMEM;
	}

	/* populate SW interrupts pool with number of OS granted IRQs. */
	pf->irq_tracker->num_entries = (u16)vectors;
	pf->irq_tracker->end = pf->irq_tracker->num_entries;

	return 0;
}

/**
 * ice_alloc_irq - Allocate new interrupt vector
 * @pf: board private structure
 *
 * Allocate new interrupt vector for a given owner id.
 * return struct msi_map with interrupt details and track
 * allocated interrupt appropriately.
 *
 * This function mimics individual interrupt allocation,
 * even interrupts are actually already allocated with
 * pci_alloc_irq_vectors. Individual allocation helps
 * to track interrupts and simplifies interrupt related
 * handling.
 *
 * On failure, return map with negative .index. The caller
 * is expected to check returned map index.
 *
 */
struct msi_map ice_alloc_irq(struct ice_pf *pf)
{
	struct msi_map map = { .index = -ENOENT };
	int entry;

	entry = ice_get_res(pf, pf->irq_tracker);
	if (entry < 0)
		return map;

	map.index = entry;
	map.virq = pci_irq_vector(pf->pdev, map.index);

	return map;
}

/**
 * ice_free_irq - Free interrupt vector
 * @pf: board private structure
 * @map: map with interrupt details
 *
 * Remove allocated interrupt from the interrupt tracker
 */
void ice_free_irq(struct ice_pf *pf, struct msi_map map)
{
	ice_free_res(pf->irq_tracker, map.index);
}
