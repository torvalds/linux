/*
 * arch/powerpc/platforms/pseries/xics.h
 *
 * Copyright 2000 IBM Corporation.
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version
 *  2 of the License, or (at your option) any later version.
 */

#ifndef _POWERPC_KERNEL_XICS_H
#define _POWERPC_KERNEL_XICS_H

#include <linux/cache.h>

extern void xics_init_IRQ(void);
extern void xics_setup_cpu(void);
extern void xics_teardown_cpu(int secondary);
extern void xics_cause_IPI(int cpu);
extern  void xics_request_IPIs(void);
extern void xics_migrate_irqs_away(void);

/* first argument is ignored for now*/
void pSeriesLP_cppr_info(int n_cpu, u8 value);

struct xics_ipi_struct {
	volatile unsigned long value;
} ____cacheline_aligned;

extern struct xics_ipi_struct xics_ipi_message[NR_CPUS] __cacheline_aligned;

struct irq_desc;
extern void pseries_8259_cascade(unsigned int irq, struct irq_desc *desc);

#endif /* _POWERPC_KERNEL_XICS_H */
