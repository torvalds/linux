/*
 * Shared interrupt handling code for IPR and INTC2 types of IRQs.
 *
 * Copyright (C) 2007, 2008 Magnus Damm
 * Copyright (C) 2009, 2010 Paul Mundt
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#include <linux/init.h>
#include <linux/irq.h>
#include <linux/spinlock.h>
#include "internals.h"

static unsigned long ack_handle[NR_IRQS];

static intc_enum __init intc_grp_id(struct intc_desc *desc,
				    intc_enum enum_id)
{
	struct intc_group *g = desc->hw.groups;
	unsigned int i, j;

	for (i = 0; g && enum_id && i < desc->hw.nr_groups; i++) {
		g = desc->hw.groups + i;

		for (j = 0; g->enum_ids[j]; j++) {
			if (g->enum_ids[j] != enum_id)
				continue;

			return g->enum_id;
		}
	}

	return 0;
}

static unsigned int __init _intc_mask_data(struct intc_desc *desc,
					   struct intc_desc_int *d,
					   intc_enum enum_id,
					   unsigned int *reg_idx,
					   unsigned int *fld_idx)
{
	struct intc_mask_reg *mr = desc->hw.mask_regs;
	unsigned int fn, mode;
	unsigned long reg_e, reg_d;

	while (mr && enum_id && *reg_idx < desc->hw.nr_mask_regs) {
		mr = desc->hw.mask_regs + *reg_idx;

		for (; *fld_idx < ARRAY_SIZE(mr->enum_ids); (*fld_idx)++) {
			if (mr->enum_ids[*fld_idx] != enum_id)
				continue;

			if (mr->set_reg && mr->clr_reg) {
				fn = REG_FN_WRITE_BASE;
				mode = MODE_DUAL_REG;
				reg_e = mr->clr_reg;
				reg_d = mr->set_reg;
			} else {
				fn = REG_FN_MODIFY_BASE;
				if (mr->set_reg) {
					mode = MODE_ENABLE_REG;
					reg_e = mr->set_reg;
					reg_d = mr->set_reg;
				} else {
					mode = MODE_MASK_REG;
					reg_e = mr->clr_reg;
					reg_d = mr->clr_reg;
				}
			}

			fn += (mr->reg_width >> 3) - 1;
			return _INTC_MK(fn, mode,
					intc_get_reg(d, reg_e),
					intc_get_reg(d, reg_d),
					1,
					(mr->reg_width - 1) - *fld_idx);
		}

		*fld_idx = 0;
		(*reg_idx)++;
	}

	return 0;
}

unsigned int __init
intc_get_mask_handle(struct intc_desc *desc, struct intc_desc_int *d,
		     intc_enum enum_id, int do_grps)
{
	unsigned int i = 0;
	unsigned int j = 0;
	unsigned int ret;

	ret = _intc_mask_data(desc, d, enum_id, &i, &j);
	if (ret)
		return ret;

	if (do_grps)
		return intc_get_mask_handle(desc, d, intc_grp_id(desc, enum_id), 0);

	return 0;
}

static unsigned int __init _intc_prio_data(struct intc_desc *desc,
					   struct intc_desc_int *d,
					   intc_enum enum_id,
					   unsigned int *reg_idx,
					   unsigned int *fld_idx)
{
	struct intc_prio_reg *pr = desc->hw.prio_regs;
	unsigned int fn, n, mode, bit;
	unsigned long reg_e, reg_d;

	while (pr && enum_id && *reg_idx < desc->hw.nr_prio_regs) {
		pr = desc->hw.prio_regs + *reg_idx;

		for (; *fld_idx < ARRAY_SIZE(pr->enum_ids); (*fld_idx)++) {
			if (pr->enum_ids[*fld_idx] != enum_id)
				continue;

			if (pr->set_reg && pr->clr_reg) {
				fn = REG_FN_WRITE_BASE;
				mode = MODE_PCLR_REG;
				reg_e = pr->set_reg;
				reg_d = pr->clr_reg;
			} else {
				fn = REG_FN_MODIFY_BASE;
				mode = MODE_PRIO_REG;
				if (!pr->set_reg)
					BUG();
				reg_e = pr->set_reg;
				reg_d = pr->set_reg;
			}

			fn += (pr->reg_width >> 3) - 1;
			n = *fld_idx + 1;

			BUG_ON(n * pr->field_width > pr->reg_width);

			bit = pr->reg_width - (n * pr->field_width);

			return _INTC_MK(fn, mode,
					intc_get_reg(d, reg_e),
					intc_get_reg(d, reg_d),
					pr->field_width, bit);
		}

		*fld_idx = 0;
		(*reg_idx)++;
	}

	return 0;
}

unsigned int __init
intc_get_prio_handle(struct intc_desc *desc, struct intc_desc_int *d,
		     intc_enum enum_id, int do_grps)
{
	unsigned int i = 0;
	unsigned int j = 0;
	unsigned int ret;

	ret = _intc_prio_data(desc, d, enum_id, &i, &j);
	if (ret)
		return ret;

	if (do_grps)
		return intc_get_prio_handle(desc, d, intc_grp_id(desc, enum_id), 0);

	return 0;
}

static unsigned int __init intc_ack_data(struct intc_desc *desc,
					  struct intc_desc_int *d,
					  intc_enum enum_id)
{
	struct intc_mask_reg *mr = desc->hw.ack_regs;
	unsigned int i, j, fn, mode;
	unsigned long reg_e, reg_d;

	for (i = 0; mr && enum_id && i < desc->hw.nr_ack_regs; i++) {
		mr = desc->hw.ack_regs + i;

		for (j = 0; j < ARRAY_SIZE(mr->enum_ids); j++) {
			if (mr->enum_ids[j] != enum_id)
				continue;

			fn = REG_FN_MODIFY_BASE;
			mode = MODE_ENABLE_REG;
			reg_e = mr->set_reg;
			reg_d = mr->set_reg;

			fn += (mr->reg_width >> 3) - 1;
			return _INTC_MK(fn, mode,
					intc_get_reg(d, reg_e),
					intc_get_reg(d, reg_d),
					1,
					(mr->reg_width - 1) - j);
		}
	}

	return 0;
}

static void intc_enable_disable(struct intc_desc_int *d,
				unsigned long handle, int do_enable)
{
	unsigned long addr;
	unsigned int cpu;
	unsigned long (*fn)(unsigned long, unsigned long,
		   unsigned long (*)(unsigned long, unsigned long,
				     unsigned long),
		   unsigned int);

	if (do_enable) {
		for (cpu = 0; cpu < SMP_NR(d, _INTC_ADDR_E(handle)); cpu++) {
			addr = INTC_REG(d, _INTC_ADDR_E(handle), cpu);
			fn = intc_enable_noprio_fns[_INTC_MODE(handle)];
			fn(addr, handle, intc_reg_fns[_INTC_FN(handle)], 0);
		}
	} else {
		for (cpu = 0; cpu < SMP_NR(d, _INTC_ADDR_D(handle)); cpu++) {
			addr = INTC_REG(d, _INTC_ADDR_D(handle), cpu);
			fn = intc_disable_fns[_INTC_MODE(handle)];
			fn(addr, handle, intc_reg_fns[_INTC_FN(handle)], 0);
		}
	}
}

void __init intc_enable_disable_enum(struct intc_desc *desc,
				     struct intc_desc_int *d,
				     intc_enum enum_id, int enable)
{
	unsigned int i, j, data;

	/* go through and enable/disable all mask bits */
	i = j = 0;
	do {
		data = _intc_mask_data(desc, d, enum_id, &i, &j);
		if (data)
			intc_enable_disable(d, data, enable);
		j++;
	} while (data);

	/* go through and enable/disable all priority fields */
	i = j = 0;
	do {
		data = _intc_prio_data(desc, d, enum_id, &i, &j);
		if (data)
			intc_enable_disable(d, data, enable);

		j++;
	} while (data);
}

unsigned int __init
intc_get_sense_handle(struct intc_desc *desc, struct intc_desc_int *d,
		      intc_enum enum_id)
{
	struct intc_sense_reg *sr = desc->hw.sense_regs;
	unsigned int i, j, fn, bit;

	for (i = 0; sr && enum_id && i < desc->hw.nr_sense_regs; i++) {
		sr = desc->hw.sense_regs + i;

		for (j = 0; j < ARRAY_SIZE(sr->enum_ids); j++) {
			if (sr->enum_ids[j] != enum_id)
				continue;

			fn = REG_FN_MODIFY_BASE;
			fn += (sr->reg_width >> 3) - 1;

			BUG_ON((j + 1) * sr->field_width > sr->reg_width);

			bit = sr->reg_width - ((j + 1) * sr->field_width);

			return _INTC_MK(fn, 0, intc_get_reg(d, sr->reg),
					0, sr->field_width, bit);
		}
	}

	return 0;
}


void intc_set_ack_handle(unsigned int irq, struct intc_desc *desc,
			 struct intc_desc_int *d, intc_enum id)
{
	unsigned long flags;

	/*
	 * Nothing to do for this IRQ.
	 */
	if (!desc->hw.ack_regs)
		return;

	raw_spin_lock_irqsave(&intc_big_lock, flags);
	ack_handle[irq] = intc_ack_data(desc, d, id);
	raw_spin_unlock_irqrestore(&intc_big_lock, flags);
}

unsigned long intc_get_ack_handle(unsigned int irq)
{
	return ack_handle[irq];
}
