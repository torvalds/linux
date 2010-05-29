/*
 * Copyright 2010 Tilera Corporation. All Rights Reserved.
 *
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License
 *   as published by the Free Software Foundation, version 2.
 *
 *   This program is distributed in the hope that it will be useful, but
 *   WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 *   NON INFRINGEMENT.  See the GNU General Public License for
 *   more details.
 */

#ifndef _ASM_TILE_IRQ_H
#define _ASM_TILE_IRQ_H

#include <linux/hardirq.h>

/* The hypervisor interface provides 32 IRQs. */
#define NR_IRQS 32

/* IRQ numbers used for linux IPIs. */
#define IRQ_RESCHEDULE 1

/* The HV interrupt state object. */
DECLARE_PER_CPU(HV_IntrState, dev_intr_state);

void ack_bad_irq(unsigned int irq);

/*
 * Paravirtualized drivers should call this when their init calls
 * discover a valid HV IRQ.
 */
void tile_irq_activate(unsigned int irq);

#endif /* _ASM_TILE_IRQ_H */
