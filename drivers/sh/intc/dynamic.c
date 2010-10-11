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
#include "internals.h" /* only for activate_irq() damage.. */

/*
 * The intc_irq_map provides a global map of bound IRQ vectors for a
 * given platform. Allocation of IRQs are either static through the CPU
 * vector map, or dynamic in the case of board mux vectors or MSI.
 *
 * As this is a central point for all IRQ controllers on the system,
 * each of the available sources are mapped out here. This combined with
 * sparseirq makes it quite trivial to keep the vector map tightly packed
 * when dynamically creating IRQs, as well as tying in to otherwise
 * unused irq_desc positions in the sparse array.
 */
static DECLARE_BITMAP(intc_irq_map, NR_IRQS);
static DEFINE_RAW_SPINLOCK(vector_lock);

/*
 * Dynamic IRQ allocation and deallocation
 */
unsigned int create_irq_nr(unsigned int irq_want, int node)
{
	unsigned int irq = 0, new;
	unsigned long flags;

	raw_spin_lock_irqsave(&vector_lock, flags);

	/*
	 * First try the wanted IRQ
	 */
	if (test_and_set_bit(irq_want, intc_irq_map) == 0) {
		new = irq_want;
	} else {
		/* .. then fall back to scanning. */
		new = find_first_zero_bit(intc_irq_map, nr_irqs);
		if (unlikely(new == nr_irqs))
			goto out_unlock;

		__set_bit(new, intc_irq_map);
	}

	raw_spin_unlock_irqrestore(&vector_lock, flags);

	irq = irq_alloc_desc_at(new, node);
	if (unlikely(irq != new)) {
		pr_err("can't get irq_desc for %d\n", new);
		return 0;
	}

	activate_irq(irq);
	return 0;

out_unlock:
	raw_spin_unlock_irqrestore(&vector_lock, flags);
	return 0;
}

int create_irq(void)
{
	int nid = cpu_to_node(smp_processor_id());
	int irq;

	irq = create_irq_nr(NR_IRQS_LEGACY, nid);
	if (irq == 0)
		irq = -1;

	return irq;
}

void destroy_irq(unsigned int irq)
{
	unsigned long flags;

	irq_free_desc(irq);

	raw_spin_lock_irqsave(&vector_lock, flags);
	__clear_bit(irq, intc_irq_map);
	raw_spin_unlock_irqrestore(&vector_lock, flags);
}

int reserve_irq_vector(unsigned int irq)
{
	unsigned long flags;
	int ret = 0;

	raw_spin_lock_irqsave(&vector_lock, flags);
	if (test_and_set_bit(irq, intc_irq_map))
		ret = -EBUSY;
	raw_spin_unlock_irqrestore(&vector_lock, flags);

	return ret;
}

void reserve_intc_vectors(struct intc_vect *vectors, unsigned int nr_vecs)
{
	unsigned long flags;
	int i;

	raw_spin_lock_irqsave(&vector_lock, flags);
	for (i = 0; i < nr_vecs; i++)
		__set_bit(evt2irq(vectors[i].vect), intc_irq_map);
	raw_spin_unlock_irqrestore(&vector_lock, flags);
}

void reserve_irq_legacy(void)
{
	unsigned long flags;
	int i, j;

	raw_spin_lock_irqsave(&vector_lock, flags);
	j = find_first_bit(intc_irq_map, nr_irqs);
	for (i = 0; i < j; i++)
		__set_bit(i, intc_irq_map);
	raw_spin_unlock_irqrestore(&vector_lock, flags);
}
