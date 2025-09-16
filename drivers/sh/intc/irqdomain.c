/*
 * IRQ domain support for SH INTC subsystem
 *
 * Copyright (C) 2012  Paul Mundt
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#define pr_fmt(fmt) "intc: " fmt

#include <linux/irqdomain.h>
#include <linux/sh_intc.h>
#include <linux/export.h>
#include "internals.h"

/**
 * intc_irq_domain_evt_xlate() - Generic xlate for vectored IRQs.
 *
 * This takes care of exception vector to hwirq translation through
 * by way of evt2irq() translation.
 *
 * Note: For platforms that use a flat vector space without INTEVT this
 * basically just mimics irq_domain_xlate_onecell() by way of a nopped
 * out evt2irq() implementation.
 */
static int intc_evt_xlate(struct irq_domain *d, struct device_node *ctrlr,
			  const u32 *intspec, unsigned int intsize,
			  unsigned long *out_hwirq, unsigned int *out_type)
{
	if (WARN_ON(intsize < 1))
		return -EINVAL;

	*out_hwirq = evt2irq(intspec[0]);
	*out_type = IRQ_TYPE_NONE;

	return 0;
}

static const struct irq_domain_ops intc_evt_ops = {
	.xlate		= intc_evt_xlate,
};

void __init intc_irq_domain_init(struct intc_desc_int *d,
				 struct intc_hw_desc *hw)
{
	unsigned int irq_base, irq_end;

	/*
	 * Quick linear revmap check
	 */
	irq_base = evt2irq(hw->vectors[0].vect);
	irq_end = evt2irq(hw->vectors[hw->nr_vectors - 1].vect);

	/*
	 * Linear domains have a hard-wired assertion that IRQs start at
	 * 0 in order to make some performance optimizations. Lamely
	 * restrict the linear case to these conditions here, taking the
	 * tree penalty for linear cases with non-zero hwirq bases.
	 */
	if (irq_base == 0 && irq_end == (irq_base + hw->nr_vectors - 1))
		d->domain = irq_domain_create_linear(NULL, hw->nr_vectors, &intc_evt_ops, NULL);
	else
		d->domain = irq_domain_create_tree(NULL, &intc_evt_ops, NULL);

	BUG_ON(!d->domain);
}
