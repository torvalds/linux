/*
 * Support for hardware-managed IRQ auto-distribution.
 *
 * Copyright (C) 2010  Paul Mundt
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#include "internals.h"

static unsigned long dist_handle[INTC_NR_IRQS];

void intc_balancing_enable(unsigned int irq)
{
	struct intc_desc_int *d = get_intc_desc(irq);
	unsigned long handle = dist_handle[irq];
	unsigned long addr;

	if (irq_balancing_disabled(irq) || !handle)
		return;

	addr = INTC_REG(d, _INTC_ADDR_D(handle), 0);
	intc_reg_fns[_INTC_FN(handle)](addr, handle, 1);
}

void intc_balancing_disable(unsigned int irq)
{
	struct intc_desc_int *d = get_intc_desc(irq);
	unsigned long handle = dist_handle[irq];
	unsigned long addr;

	if (irq_balancing_disabled(irq) || !handle)
		return;

	addr = INTC_REG(d, _INTC_ADDR_D(handle), 0);
	intc_reg_fns[_INTC_FN(handle)](addr, handle, 0);
}

static unsigned int intc_dist_data(struct intc_desc *desc,
				   struct intc_desc_int *d,
				   intc_enum enum_id)
{
	struct intc_mask_reg *mr = desc->hw.mask_regs;
	unsigned int i, j, fn, mode;
	unsigned long reg_e, reg_d;

	for (i = 0; mr && enum_id && i < desc->hw.nr_mask_regs; i++) {
		mr = desc->hw.mask_regs + i;

		/*
		 * Skip this entry if there's no auto-distribution
		 * register associated with it.
		 */
		if (!mr->dist_reg)
			continue;

		for (j = 0; j < ARRAY_SIZE(mr->enum_ids); j++) {
			if (mr->enum_ids[j] != enum_id)
				continue;

			fn = REG_FN_MODIFY_BASE;
			mode = MODE_ENABLE_REG;
			reg_e = mr->dist_reg;
			reg_d = mr->dist_reg;

			fn += (mr->reg_width >> 3) - 1;
			return _INTC_MK(fn, mode,
					intc_get_reg(d, reg_e),
					intc_get_reg(d, reg_d),
					1,
					(mr->reg_width - 1) - j);
		}
	}

	/*
	 * It's possible we've gotten here with no distribution options
	 * available for the IRQ in question, so we just skip over those.
	 */
	return 0;
}

void intc_set_dist_handle(unsigned int irq, struct intc_desc *desc,
			  struct intc_desc_int *d, intc_enum id)
{
	unsigned long flags;

	/*
	 * Nothing to do for this IRQ.
	 */
	if (!desc->hw.mask_regs)
		return;

	raw_spin_lock_irqsave(&intc_big_lock, flags);
	dist_handle[irq] = intc_dist_data(desc, d, id);
	raw_spin_unlock_irqrestore(&intc_big_lock, flags);
}
