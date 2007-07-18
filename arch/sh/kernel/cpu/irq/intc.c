/*
 * Shared interrupt handling code for IPR and INTC2 types of IRQs.
 *
 * Copyright (C) 2007 Magnus Damm
 *
 * Based on intc2.c and ipr.c
 *
 * Copyright (C) 1999  Niibe Yutaka & Takeshi Yaegashi
 * Copyright (C) 2000  Kazumoto Kojima
 * Copyright (C) 2001  David J. Mckay (david.mckay@st.com)
 * Copyright (C) 2003  Takashi Kusuda <kusuda-takashi@hitachi-ul.co.jp>
 * Copyright (C) 2005, 2006  Paul Mundt
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#include <linux/init.h>
#include <linux/irq.h>
#include <linux/module.h>
#include <linux/io.h>
#include <linux/interrupt.h>

#define _INTC_MK(fn, idx, bit, value) \
	((fn) << 24 | ((value) << 16) | ((idx) << 8) | (bit))
#define _INTC_FN(h) (h >> 24)
#define _INTC_VALUE(h) ((h >> 16) & 0xff)
#define _INTC_IDX(h) ((h >> 8) & 0xff)
#define _INTC_BIT(h) (h & 0xff)

#define _INTC_PTR(desc, member, data) \
	(desc->member + _INTC_IDX(data))

static inline struct intc_desc *get_intc_desc(unsigned int irq)
{
	struct irq_chip *chip = get_irq_chip(irq);
	return (void *)((char *)chip - offsetof(struct intc_desc, chip));
}

static inline unsigned int set_field(unsigned int value,
				     unsigned int field_value,
				     unsigned int width,
				     unsigned int shift)
{
	value &= ~(((1 << width) - 1) << shift);
	value |= field_value << shift;
	return value;
}

static inline unsigned int set_prio_field(struct intc_desc *desc,
					  unsigned int value,
					  unsigned int priority,
					  unsigned int data)
{
	unsigned int width = _INTC_PTR(desc, prio_regs, data)->field_width;

	return set_field(value, priority, width, _INTC_BIT(data));
}

static void disable_prio_16(struct intc_desc *desc, unsigned int data)
{
	unsigned long addr = _INTC_PTR(desc, prio_regs, data)->reg;

	ctrl_outw(set_prio_field(desc, ctrl_inw(addr), 0, data), addr);
}

static void enable_prio_16(struct intc_desc *desc, unsigned int data)
{
	unsigned long addr = _INTC_PTR(desc, prio_regs, data)->reg;
	unsigned int prio = _INTC_VALUE(data);

	ctrl_outw(set_prio_field(desc, ctrl_inw(addr), prio, data), addr);
}

static void disable_prio_32(struct intc_desc *desc, unsigned int data)
{
	unsigned long addr = _INTC_PTR(desc, prio_regs, data)->reg;

	ctrl_outl(set_prio_field(desc, ctrl_inl(addr), 0, data), addr);
}

static void enable_prio_32(struct intc_desc *desc, unsigned int data)
{
	unsigned long addr = _INTC_PTR(desc, prio_regs, data)->reg;
	unsigned int prio = _INTC_VALUE(data);

	ctrl_outl(set_prio_field(desc, ctrl_inl(addr), prio, data), addr);
}

static void disable_mask_8(struct intc_desc *desc, unsigned int data)
{
	ctrl_outb(1 << _INTC_BIT(data),
		  _INTC_PTR(desc, mask_regs, data)->set_reg);
}

static void enable_mask_8(struct intc_desc *desc, unsigned int data)
{
	ctrl_outb(1 << _INTC_BIT(data),
		  _INTC_PTR(desc, mask_regs, data)->clr_reg);
}

static void disable_mask_32(struct intc_desc *desc, unsigned int data)
{
	ctrl_outl(1 << _INTC_BIT(data),
		  _INTC_PTR(desc, mask_regs, data)->set_reg);
}

static void enable_mask_32(struct intc_desc *desc, unsigned int data)
{
	ctrl_outl(1 << _INTC_BIT(data),
		  _INTC_PTR(desc, mask_regs, data)->clr_reg);
}

enum {	REG_FN_ERROR=0,
	REG_FN_MASK_8, REG_FN_MASK_32,
	REG_FN_PRIO_16, REG_FN_PRIO_32 };

static struct {
	void (*enable)(struct intc_desc *, unsigned int);
	void (*disable)(struct intc_desc *, unsigned int);
} intc_reg_fns[] = {
	[REG_FN_MASK_8] = { enable_mask_8, disable_mask_8 },
	[REG_FN_MASK_32] = { enable_mask_32, disable_mask_32 },
	[REG_FN_PRIO_16] = { enable_prio_16, disable_prio_16 },
	[REG_FN_PRIO_32] = { enable_prio_32, disable_prio_32 },
};

static void intc_enable(unsigned int irq)
{
	struct intc_desc *desc = get_intc_desc(irq);
	unsigned int data = (unsigned int) get_irq_chip_data(irq);

	intc_reg_fns[_INTC_FN(data)].enable(desc, data);
}

static void intc_disable(unsigned int irq)
{
	struct intc_desc *desc = get_intc_desc(irq);
	unsigned int data = (unsigned int) get_irq_chip_data(irq);

	intc_reg_fns[_INTC_FN(data)].disable(desc, data);
}

static void set_sense_16(struct intc_desc *desc, unsigned int data)
{
	unsigned long addr = _INTC_PTR(desc, sense_regs, data)->reg;
	unsigned int width = _INTC_PTR(desc, sense_regs, data)->field_width;
	unsigned int bit = _INTC_BIT(data);
	unsigned int value = _INTC_VALUE(data);

	ctrl_outw(set_field(ctrl_inw(addr), value, width, bit), addr);
}

static void set_sense_32(struct intc_desc *desc, unsigned int data)
{
	unsigned long addr = _INTC_PTR(desc, sense_regs, data)->reg;
	unsigned int width = _INTC_PTR(desc, sense_regs, data)->field_width;
	unsigned int bit = _INTC_BIT(data);
	unsigned int value = _INTC_VALUE(data);

	ctrl_outl(set_field(ctrl_inl(addr), value, width, bit), addr);
}

#define VALID(x) (x | 0x80)

static unsigned char intc_irq_sense_table[IRQ_TYPE_SENSE_MASK + 1] = {
	[IRQ_TYPE_EDGE_FALLING] = VALID(0),
	[IRQ_TYPE_EDGE_RISING] = VALID(1),
	[IRQ_TYPE_LEVEL_LOW] = VALID(2),
	[IRQ_TYPE_LEVEL_HIGH] = VALID(3),
};

static int intc_set_sense(unsigned int irq, unsigned int type)
{
	struct intc_desc *desc = get_intc_desc(irq);
	unsigned char value = intc_irq_sense_table[type & IRQ_TYPE_SENSE_MASK];
	unsigned int i, j, data, bit;
	intc_enum enum_id = 0;

	for (i = 0; i < desc->nr_vectors; i++) {
		struct intc_vect *vect = desc->vectors + i;

		if (evt2irq(vect->vect) != irq)
			continue;

		enum_id = vect->enum_id;
		break;
	}

	if (!enum_id || !value)
		return -EINVAL;

	value ^= VALID(0);

	for (i = 0; i < desc->nr_sense_regs; i++) {
		struct intc_sense_reg *sr = desc->sense_regs + i;

		for (j = 0; j < ARRAY_SIZE(sr->enum_ids); j++) {
			if (sr->enum_ids[j] != enum_id)
				continue;

			bit = sr->reg_width - ((j + 1) * sr->field_width);
			data = _INTC_MK(0, i, bit, value);

			switch(sr->reg_width) {
			case 16:
				set_sense_16(desc, data);
				break;
			case 32:
				set_sense_32(desc, data);
				break;
			}

			return 0;
		}
	}

	return -EINVAL;
}

static unsigned int __init intc_find_mask_handler(unsigned int width)
{
	switch (width) {
	case 8:
		return REG_FN_MASK_8;
	case 32:
		return REG_FN_MASK_32;
	}

	BUG();
	return REG_FN_ERROR;
}

static unsigned int __init intc_find_prio_handler(unsigned int width)
{
	switch (width) {
	case 16:
		return REG_FN_PRIO_16;
	case 32:
		return REG_FN_PRIO_32;
	}

	BUG();
	return REG_FN_ERROR;
}

static unsigned int __init intc_prio_value(struct intc_desc *desc,
					   intc_enum enum_id)
{
	unsigned int i;

	for (i = 0; i < desc->nr_priorities; i++) {
		struct intc_prio *p = desc->priorities + i;

		if (p->enum_id != enum_id)
			continue;

		return p->priority;
	}

	return 1; /* default to the lowest priority if no priority is set */
}

static unsigned int __init intc_mask_data(struct intc_desc *desc,
					  intc_enum enum_id)
{
	unsigned int i, j, fn;

	for (i = 0; i < desc->nr_mask_regs; i++) {
		struct intc_mask_reg *mr = desc->mask_regs + i;

		for (j = 0; j < ARRAY_SIZE(mr->enum_ids); j++) {
			if (mr->enum_ids[j] != enum_id)
				continue;

			fn = intc_find_mask_handler(mr->reg_width);
			if (fn == REG_FN_ERROR)
				return 0;

			return _INTC_MK(fn, i, (mr->reg_width - 1) - j, 0);
		}
	}

	return 0;
}

static unsigned int __init intc_prio_data(struct intc_desc *desc,
					  intc_enum enum_id)
{
	unsigned int i, j, fn, bit, prio;

	for (i = 0; i < desc->nr_prio_regs; i++) {
		struct intc_prio_reg *pr = desc->prio_regs + i;

		for (j = 0; j < ARRAY_SIZE(pr->enum_ids); j++) {
			if (pr->enum_ids[j] != enum_id)
				continue;

			fn = intc_find_prio_handler(pr->reg_width);
			if (fn == REG_FN_ERROR)
				return 0;

			prio = intc_prio_value(desc, enum_id);
			bit = pr->reg_width - ((j + 1) * pr->field_width);

			BUG_ON(bit < 0);

			return _INTC_MK(fn, i, bit, prio);
		}
	}

	return 0;
}

static void __init intc_register_irq(struct intc_desc *desc, intc_enum enum_id,
				     unsigned int irq)
{
	unsigned int mask_data = intc_mask_data(desc, enum_id);
	unsigned int prio_data = intc_prio_data(desc, enum_id);
	unsigned int data = mask_data ? mask_data : prio_data;

	BUG_ON(!data);

	disable_irq_nosync(irq);
	set_irq_chip_and_handler_name(irq, &desc->chip,
				      handle_level_irq, "level");
	set_irq_chip_data(irq, (void *)data);

	/* set priority */

	if (prio_data)
		intc_reg_fns[_INTC_FN(prio_data)].enable(desc, prio_data);

	/* irq should be disabled by default */
	desc->chip.mask(irq);
}

void __init register_intc_controller(struct intc_desc *desc)
{
	unsigned int i;

	desc->chip.mask = intc_disable;
	desc->chip.unmask = intc_enable;
	desc->chip.mask_ack = intc_disable;
	desc->chip.set_type = intc_set_sense;

	for (i = 0; i < desc->nr_vectors; i++) {
		struct intc_vect *vect = desc->vectors + i;

		intc_register_irq(desc, vect->enum_id, evt2irq(vect->vect));
	}
}
