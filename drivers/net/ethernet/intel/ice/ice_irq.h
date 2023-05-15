/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) 2023, Intel Corporation. */

#ifndef _ICE_IRQ_H_
#define _ICE_IRQ_H_

int ice_init_interrupt_scheme(struct ice_pf *pf);
void ice_clear_interrupt_scheme(struct ice_pf *pf);

struct msi_map ice_alloc_irq(struct ice_pf *pf);
void ice_free_irq(struct ice_pf *pf, struct msi_map map);

#endif
