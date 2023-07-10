/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) 2023, Intel Corporation. */

#ifndef _ICE_IRQ_H_
#define _ICE_IRQ_H_

struct ice_irq_entry {
	unsigned int index;
	bool dynamic;	/* allocation type flag */
};

struct ice_irq_tracker {
	struct xarray entries;
	u16 num_entries;	/* total vectors available */
	u16 num_static;	/* preallocated entries */
};

int ice_init_interrupt_scheme(struct ice_pf *pf);
void ice_clear_interrupt_scheme(struct ice_pf *pf);

struct msi_map ice_alloc_irq(struct ice_pf *pf, bool dyn_only);
void ice_free_irq(struct ice_pf *pf, struct msi_map map);
int ice_get_max_used_msix_vector(struct ice_pf *pf);

#endif
