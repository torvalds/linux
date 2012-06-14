/*
 * Dynamic IRQ management
 *
 * Copyright (C) 2010  Paul Mundt
 *
 * Modelled after arch/x86/kernel/apic/io_apic.c
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#define pr_fmt(fmt) "intc: " fmt

#include <linux/irq.h>
#include <linux/bitmap.h>
#include <linux/spinlock.h>
#include <linux/module.h>
#include "internals.h" /* only for activate_irq() damage.. */

/*
 * The IRQ bitmap provides a global map of bound IRQ vectors for a
 * given platform. Allocation of IRQs are either static through the CPU
 * vector map, or dynamic in the case of board mux vectors or MSI.
 *
 * As this is a central point for all IRQ controllers on the system,
 * each of the available sources are mapped out here. This combined with
 * sparseirq makes it quite trivial to keep the vector map tightly packed
 * when dynamically creating IRQs, as well as tying in to otherwise
 * unused irq_desc positions in the sparse array.
 */

/*
 * Dynamic IRQ allocation and deallocation
 */
unsigned int create_irq_nr(unsigned int irq_want, int node)
{
	int irq = irq_alloc_desc_at(irq_want, node);
	if (irq < 0)
		return 0;

	activate_irq(irq);
	return irq;
}

int create_irq(void)
{
	int irq = irq_alloc_desc(numa_node_id());
	if (irq >= 0)
		activate_irq(irq);

	return irq;
}

void destroy_irq(unsigned int irq)
{
	irq_free_desc(irq);
}
