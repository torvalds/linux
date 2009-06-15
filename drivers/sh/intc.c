/*
 * Shared interrupt handling code for IPR and INTC2 types of IRQs.
 *
 * Copyright (C) 2007, 2008 Magnus Damm
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
#include <linux/bootmem.h>
#include <linux/sh_intc.h>
#include <linux/sysdev.h>
#include <linux/list.h>
#include <linux/topology.h>

#define _INTC_MK(fn, mode, addr_e, addr_d, width, shift) \
	((shift) | ((width) << 5) | ((fn) << 9) | ((mode) << 13) | \
	 ((addr_e) << 16) | ((addr_d << 24)))

#define _INTC_SHIFT(h) (h & 0x1f)
#define _INTC_WIDTH(h) ((h >> 5) & 0xf)
#define _INTC_FN(h) ((h >> 9) & 0xf)
#define _INTC_MODE(h) ((h >> 13) & 0x7)
#define _INTC_ADDR_E(h) ((h >> 16) & 0xff)
#define _INTC_ADDR_D(h) ((h >> 24) & 0xff)

struct intc_handle_int {
	unsigned int irq;
	unsigned long handle;
};

struct intc_desc_int {
	struct list_head list;
	struct sys_device sysdev;
	pm_message_t state;
	unsigned long *reg;
#ifdef CONFIG_SMP
	unsigned long *smp;
#endif
	unsigned int nr_reg;
	struct intc_handle_int *prio;
	unsigned int nr_prio;
	struct intc_handle_int *sense;
	unsigned int nr_sense;
	struct irq_chip chip;
};

static LIST_HEAD(intc_list);

#ifdef CONFIG_SMP
#define IS_SMP(x) x.smp
#define INTC_REG(d, x, c) (d->reg[(x)] + ((d->smp[(x)] & 0xff) * c))
#define SMP_NR(d, x) ((d->smp[(x)] >> 8) ? (d->smp[(x)] >> 8) : 1)
#else
#define IS_SMP(x) 0
#define INTC_REG(d, x, c) (d->reg[(x)])
#define SMP_NR(d, x) 1
#endif

static unsigned int intc_prio_level[NR_IRQS]; /* for now */
#if defined(CONFIG_CPU_SH3) || defined(CONFIG_CPU_SH4A)
static unsigned long ack_handle[NR_IRQS];
#endif

static inline struct intc_desc_int *get_intc_desc(unsigned int irq)
{
	struct irq_chip *chip = get_irq_chip(irq);
	return (void *)((char *)chip - offsetof(struct intc_desc_int, chip));
}

static inline unsigned int set_field(unsigned int value,
				     unsigned int field_value,
				     unsigned int handle)
{
	unsigned int width = _INTC_WIDTH(handle);
	unsigned int shift = _INTC_SHIFT(handle);

	value &= ~(((1 << width) - 1) << shift);
	value |= field_value << shift;
	return value;
}

static void write_8(unsigned long addr, unsigned long h, unsigned long data)
{
	__raw_writeb(set_field(0, data, h), addr);
}

static void write_16(unsigned long addr, unsigned long h, unsigned long data)
{
	__raw_writew(set_field(0, data, h), addr);
}

static void write_32(unsigned long addr, unsigned long h, unsigned long data)
{
	__raw_writel(set_field(0, data, h), addr);
}

static void modify_8(unsigned long addr, unsigned long h, unsigned long data)
{
	unsigned long flags;
	local_irq_save(flags);
	__raw_writeb(set_field(__raw_readb(addr), data, h), addr);
	local_irq_restore(flags);
}

static void modify_16(unsigned long addr, unsigned long h, unsigned long data)
{
	unsigned long flags;
	local_irq_save(flags);
	__raw_writew(set_field(__raw_readw(addr), data, h), addr);
	local_irq_restore(flags);
}

static void modify_32(unsigned long addr, unsigned long h, unsigned long data)
{
	unsigned long flags;
	local_irq_save(flags);
	__raw_writel(set_field(__raw_readl(addr), data, h), addr);
	local_irq_restore(flags);
}

enum {	REG_FN_ERR = 0, REG_FN_WRITE_BASE = 1, REG_FN_MODIFY_BASE = 5 };

static void (*intc_reg_fns[])(unsigned long addr,
			      unsigned long h,
			      unsigned long data) = {
	[REG_FN_WRITE_BASE + 0] = write_8,
	[REG_FN_WRITE_BASE + 1] = write_16,
	[REG_FN_WRITE_BASE + 3] = write_32,
	[REG_FN_MODIFY_BASE + 0] = modify_8,
	[REG_FN_MODIFY_BASE + 1] = modify_16,
	[REG_FN_MODIFY_BASE + 3] = modify_32,
};

enum {	MODE_ENABLE_REG = 0, /* Bit(s) set -> interrupt enabled */
	MODE_MASK_REG,       /* Bit(s) set -> interrupt disabled */
	MODE_DUAL_REG,       /* Two registers, set bit to enable / disable */
	MODE_PRIO_REG,       /* Priority value written to enable interrupt */
	MODE_PCLR_REG,       /* Above plus all bits set to disable interrupt */
};

static void intc_mode_field(unsigned long addr,
			    unsigned long handle,
			    void (*fn)(unsigned long,
				       unsigned long,
				       unsigned long),
			    unsigned int irq)
{
	fn(addr, handle, ((1 << _INTC_WIDTH(handle)) - 1));
}

static void intc_mode_zero(unsigned long addr,
			   unsigned long handle,
			   void (*fn)(unsigned long,
				       unsigned long,
				       unsigned long),
			   unsigned int irq)
{
	fn(addr, handle, 0);
}

static void intc_mode_prio(unsigned long addr,
			   unsigned long handle,
			   void (*fn)(unsigned long,
				       unsigned long,
				       unsigned long),
			   unsigned int irq)
{
	fn(addr, handle, intc_prio_level[irq]);
}

static void (*intc_enable_fns[])(unsigned long addr,
				 unsigned long handle,
				 void (*fn)(unsigned long,
					    unsigned long,
					    unsigned long),
				 unsigned int irq) = {
	[MODE_ENABLE_REG] = intc_mode_field,
	[MODE_MASK_REG] = intc_mode_zero,
	[MODE_DUAL_REG] = intc_mode_field,
	[MODE_PRIO_REG] = intc_mode_prio,
	[MODE_PCLR_REG] = intc_mode_prio,
};

static void (*intc_disable_fns[])(unsigned long addr,
				  unsigned long handle,
				  void (*fn)(unsigned long,
					     unsigned long,
					     unsigned long),
				  unsigned int irq) = {
	[MODE_ENABLE_REG] = intc_mode_zero,
	[MODE_MASK_REG] = intc_mode_field,
	[MODE_DUAL_REG] = intc_mode_field,
	[MODE_PRIO_REG] = intc_mode_zero,
	[MODE_PCLR_REG] = intc_mode_field,
};

static inline void _intc_enable(unsigned int irq, unsigned long handle)
{
	struct intc_desc_int *d = get_intc_desc(irq);
	unsigned long addr;
	unsigned int cpu;

	for (cpu = 0; cpu < SMP_NR(d, _INTC_ADDR_E(handle)); cpu++) {
		addr = INTC_REG(d, _INTC_ADDR_E(handle), cpu);
		intc_enable_fns[_INTC_MODE(handle)](addr, handle, intc_reg_fns\
						    [_INTC_FN(handle)], irq);
	}
}

static void intc_enable(unsigned int irq)
{
	_intc_enable(irq, (unsigned long)get_irq_chip_data(irq));
}

static void intc_disable(unsigned int irq)
{
	struct intc_desc_int *d = get_intc_desc(irq);
	unsigned long handle = (unsigned long) get_irq_chip_data(irq);
	unsigned long addr;
	unsigned int cpu;

	for (cpu = 0; cpu < SMP_NR(d, _INTC_ADDR_D(handle)); cpu++) {
		addr = INTC_REG(d, _INTC_ADDR_D(handle), cpu);
		intc_disable_fns[_INTC_MODE(handle)](addr, handle,intc_reg_fns\
						     [_INTC_FN(handle)], irq);
	}
}

static int intc_set_wake(unsigned int irq, unsigned int on)
{
	return 0; /* allow wakeup, but setup hardware in intc_suspend() */
}

#if defined(CONFIG_CPU_SH3) || defined(CONFIG_CPU_SH4A)
static void intc_mask_ack(unsigned int irq)
{
	struct intc_desc_int *d = get_intc_desc(irq);
	unsigned long handle = ack_handle[irq];
	unsigned long addr;

	intc_disable(irq);

	/* read register and write zero only to the assocaited bit */

	if (handle) {
		addr = INTC_REG(d, _INTC_ADDR_D(handle), 0);
		switch (_INTC_FN(handle)) {
		case REG_FN_MODIFY_BASE + 0:	/* 8bit */
			__raw_readb(addr);
			__raw_writeb(0xff ^ set_field(0, 1, handle), addr);
			break;
		case REG_FN_MODIFY_BASE + 1:	/* 16bit */
			__raw_readw(addr);
			__raw_writew(0xffff ^ set_field(0, 1, handle), addr);
			break;
		case REG_FN_MODIFY_BASE + 3:	/* 32bit */
			__raw_readl(addr);
			__raw_writel(0xffffffff ^ set_field(0, 1, handle), addr);
			break;
		default:
			BUG();
			break;
		}
	}
}
#endif

static struct intc_handle_int *intc_find_irq(struct intc_handle_int *hp,
					     unsigned int nr_hp,
					     unsigned int irq)
{
	int i;

	/* this doesn't scale well, but...
	 *
	 * this function should only be used for cerain uncommon
	 * operations such as intc_set_priority() and intc_set_sense()
	 * and in those rare cases performance doesn't matter that much.
	 * keeping the memory footprint low is more important.
	 *
	 * one rather simple way to speed this up and still keep the
	 * memory footprint down is to make sure the array is sorted
	 * and then perform a bisect to lookup the irq.
	 */

	for (i = 0; i < nr_hp; i++) {
		if ((hp + i)->irq != irq)
			continue;

		return hp + i;
	}

	return NULL;
}

int intc_set_priority(unsigned int irq, unsigned int prio)
{
	struct intc_desc_int *d = get_intc_desc(irq);
	struct intc_handle_int *ihp;

	if (!intc_prio_level[irq] || prio <= 1)
		return -EINVAL;

	ihp = intc_find_irq(d->prio, d->nr_prio, irq);
	if (ihp) {
		if (prio >= (1 << _INTC_WIDTH(ihp->handle)))
			return -EINVAL;

		intc_prio_level[irq] = prio;

		/*
		 * only set secondary masking method directly
		 * primary masking method is using intc_prio_level[irq]
		 * priority level will be set during next enable()
		 */

		if (_INTC_FN(ihp->handle) != REG_FN_ERR)
			_intc_enable(irq, ihp->handle);
	}
	return 0;
}

#define VALID(x) (x | 0x80)

static unsigned char intc_irq_sense_table[IRQ_TYPE_SENSE_MASK + 1] = {
	[IRQ_TYPE_EDGE_FALLING] = VALID(0),
	[IRQ_TYPE_EDGE_RISING] = VALID(1),
	[IRQ_TYPE_LEVEL_LOW] = VALID(2),
	/* SH7706, SH7707 and SH7709 do not support high level triggered */
#if !defined(CONFIG_CPU_SUBTYPE_SH7706) && \
    !defined(CONFIG_CPU_SUBTYPE_SH7707) && \
    !defined(CONFIG_CPU_SUBTYPE_SH7709)
	[IRQ_TYPE_LEVEL_HIGH] = VALID(3),
#endif
};

static int intc_set_sense(unsigned int irq, unsigned int type)
{
	struct intc_desc_int *d = get_intc_desc(irq);
	unsigned char value = intc_irq_sense_table[type & IRQ_TYPE_SENSE_MASK];
	struct intc_handle_int *ihp;
	unsigned long addr;

	if (!value)
		return -EINVAL;

	ihp = intc_find_irq(d->sense, d->nr_sense, irq);
	if (ihp) {
		addr = INTC_REG(d, _INTC_ADDR_E(ihp->handle), 0);
		intc_reg_fns[_INTC_FN(ihp->handle)](addr, ihp->handle, value);
	}
	return 0;
}

static unsigned int __init intc_get_reg(struct intc_desc_int *d,
				 unsigned long address)
{
	unsigned int k;

	for (k = 0; k < d->nr_reg; k++) {
		if (d->reg[k] == address)
			return k;
	}

	BUG();
	return 0;
}

static intc_enum __init intc_grp_id(struct intc_desc *desc,
				    intc_enum enum_id)
{
	struct intc_group *g = desc->groups;
	unsigned int i, j;

	for (i = 0; g && enum_id && i < desc->nr_groups; i++) {
		g = desc->groups + i;

		for (j = 0; g->enum_ids[j]; j++) {
			if (g->enum_ids[j] != enum_id)
				continue;

			return g->enum_id;
		}
	}

	return 0;
}

static unsigned int __init intc_mask_data(struct intc_desc *desc,
					  struct intc_desc_int *d,
					  intc_enum enum_id, int do_grps)
{
	struct intc_mask_reg *mr = desc->mask_regs;
	unsigned int i, j, fn, mode;
	unsigned long reg_e, reg_d;

	for (i = 0; mr && enum_id && i < desc->nr_mask_regs; i++) {
		mr = desc->mask_regs + i;

		for (j = 0; j < ARRAY_SIZE(mr->enum_ids); j++) {
			if (mr->enum_ids[j] != enum_id)
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
					(mr->reg_width - 1) - j);
		}
	}

	if (do_grps)
		return intc_mask_data(desc, d, intc_grp_id(desc, enum_id), 0);

	return 0;
}

static unsigned int __init intc_prio_data(struct intc_desc *desc,
					  struct intc_desc_int *d,
					  intc_enum enum_id, int do_grps)
{
	struct intc_prio_reg *pr = desc->prio_regs;
	unsigned int i, j, fn, mode, bit;
	unsigned long reg_e, reg_d;

	for (i = 0; pr && enum_id && i < desc->nr_prio_regs; i++) {
		pr = desc->prio_regs + i;

		for (j = 0; j < ARRAY_SIZE(pr->enum_ids); j++) {
			if (pr->enum_ids[j] != enum_id)
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

			BUG_ON((j + 1) * pr->field_width > pr->reg_width);

			bit = pr->reg_width - ((j + 1) * pr->field_width);

			return _INTC_MK(fn, mode,
					intc_get_reg(d, reg_e),
					intc_get_reg(d, reg_d),
					pr->field_width, bit);
		}
	}

	if (do_grps)
		return intc_prio_data(desc, d, intc_grp_id(desc, enum_id), 0);

	return 0;
}

#if defined(CONFIG_CPU_SH3) || defined(CONFIG_CPU_SH4A)
static unsigned int __init intc_ack_data(struct intc_desc *desc,
					  struct intc_desc_int *d,
					  intc_enum enum_id)
{
	struct intc_mask_reg *mr = desc->ack_regs;
	unsigned int i, j, fn, mode;
	unsigned long reg_e, reg_d;

	for (i = 0; mr && enum_id && i < desc->nr_ack_regs; i++) {
		mr = desc->ack_regs + i;

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
#endif

static unsigned int __init intc_sense_data(struct intc_desc *desc,
					   struct intc_desc_int *d,
					   intc_enum enum_id)
{
	struct intc_sense_reg *sr = desc->sense_regs;
	unsigned int i, j, fn, bit;

	for (i = 0; sr && enum_id && i < desc->nr_sense_regs; i++) {
		sr = desc->sense_regs + i;

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

static void __init intc_register_irq(struct intc_desc *desc,
				     struct intc_desc_int *d,
				     intc_enum enum_id,
				     unsigned int irq)
{
	struct intc_handle_int *hp;
	unsigned int data[2], primary;

	/* Prefer single interrupt source bitmap over other combinations:
	 * 1. bitmap, single interrupt source
	 * 2. priority, single interrupt source
	 * 3. bitmap, multiple interrupt sources (groups)
	 * 4. priority, multiple interrupt sources (groups)
	 */

	data[0] = intc_mask_data(desc, d, enum_id, 0);
	data[1] = intc_prio_data(desc, d, enum_id, 0);

	primary = 0;
	if (!data[0] && data[1])
		primary = 1;

	if (!data[0] && !data[1])
		pr_warning("intc: missing unique irq mask for "
			   "irq %d (vect 0x%04x)\n", irq, irq2evt(irq));

	data[0] = data[0] ? data[0] : intc_mask_data(desc, d, enum_id, 1);
	data[1] = data[1] ? data[1] : intc_prio_data(desc, d, enum_id, 1);

	if (!data[primary])
		primary ^= 1;

	BUG_ON(!data[primary]); /* must have primary masking method */

	disable_irq_nosync(irq);
	set_irq_chip_and_handler_name(irq, &d->chip,
				      handle_level_irq, "level");
	set_irq_chip_data(irq, (void *)data[primary]);

	/* set priority level
	 * - this needs to be at least 2 for 5-bit priorities on 7780
	 */
	intc_prio_level[irq] = 2;

	/* enable secondary masking method if present */
	if (data[!primary])
		_intc_enable(irq, data[!primary]);

	/* add irq to d->prio list if priority is available */
	if (data[1]) {
		hp = d->prio + d->nr_prio;
		hp->irq = irq;
		hp->handle = data[1];

		if (primary) {
			/*
			 * only secondary priority should access registers, so
			 * set _INTC_FN(h) = REG_FN_ERR for intc_set_priority()
			 */

			hp->handle &= ~_INTC_MK(0x0f, 0, 0, 0, 0, 0);
			hp->handle |= _INTC_MK(REG_FN_ERR, 0, 0, 0, 0, 0);
		}
		d->nr_prio++;
	}

	/* add irq to d->sense list if sense is available */
	data[0] = intc_sense_data(desc, d, enum_id);
	if (data[0]) {
		(d->sense + d->nr_sense)->irq = irq;
		(d->sense + d->nr_sense)->handle = data[0];
		d->nr_sense++;
	}

	/* irq should be disabled by default */
	d->chip.mask(irq);

#if defined(CONFIG_CPU_SH3) || defined(CONFIG_CPU_SH4A)
	if (desc->ack_regs)
		ack_handle[irq] = intc_ack_data(desc, d, enum_id);
#endif
}

static unsigned int __init save_reg(struct intc_desc_int *d,
				    unsigned int cnt,
				    unsigned long value,
				    unsigned int smp)
{
	if (value) {
		d->reg[cnt] = value;
#ifdef CONFIG_SMP
		d->smp[cnt] = smp;
#endif
		return 1;
	}

	return 0;
}

static unsigned char *intc_evt2irq_table;

unsigned int intc_evt2irq(unsigned int vector)
{
	unsigned int irq = evt2irq(vector);

	if (intc_evt2irq_table && intc_evt2irq_table[irq])
		irq = intc_evt2irq_table[irq];

	return irq;
}

void __init register_intc_controller(struct intc_desc *desc)
{
	unsigned int i, k, smp;
	struct intc_desc_int *d;

	d = alloc_bootmem(sizeof(*d));

	INIT_LIST_HEAD(&d->list);
	list_add(&d->list, &intc_list);

	d->nr_reg = desc->mask_regs ? desc->nr_mask_regs * 2 : 0;
	d->nr_reg += desc->prio_regs ? desc->nr_prio_regs * 2 : 0;
	d->nr_reg += desc->sense_regs ? desc->nr_sense_regs : 0;

#if defined(CONFIG_CPU_SH3) || defined(CONFIG_CPU_SH4A)
	d->nr_reg += desc->ack_regs ? desc->nr_ack_regs : 0;
#endif
	d->reg = alloc_bootmem(d->nr_reg * sizeof(*d->reg));
#ifdef CONFIG_SMP
	d->smp = alloc_bootmem(d->nr_reg * sizeof(*d->smp));
#endif
	k = 0;

	if (desc->mask_regs) {
		for (i = 0; i < desc->nr_mask_regs; i++) {
			smp = IS_SMP(desc->mask_regs[i]);
			k += save_reg(d, k, desc->mask_regs[i].set_reg, smp);
			k += save_reg(d, k, desc->mask_regs[i].clr_reg, smp);
		}
	}

	if (desc->prio_regs) {
		d->prio = alloc_bootmem(desc->nr_vectors * sizeof(*d->prio));

		for (i = 0; i < desc->nr_prio_regs; i++) {
			smp = IS_SMP(desc->prio_regs[i]);
			k += save_reg(d, k, desc->prio_regs[i].set_reg, smp);
			k += save_reg(d, k, desc->prio_regs[i].clr_reg, smp);
		}
	}

	if (desc->sense_regs) {
		d->sense = alloc_bootmem(desc->nr_vectors * sizeof(*d->sense));

		for (i = 0; i < desc->nr_sense_regs; i++) {
			k += save_reg(d, k, desc->sense_regs[i].reg, 0);
		}
	}

	d->chip.name = desc->name;
	d->chip.mask = intc_disable;
	d->chip.unmask = intc_enable;
	d->chip.mask_ack = intc_disable;
	d->chip.enable = intc_enable;
	d->chip.disable = intc_disable;
	d->chip.shutdown = intc_disable;
	d->chip.set_type = intc_set_sense;
	d->chip.set_wake = intc_set_wake;

#if defined(CONFIG_CPU_SH3) || defined(CONFIG_CPU_SH4A)
	if (desc->ack_regs) {
		for (i = 0; i < desc->nr_ack_regs; i++)
			k += save_reg(d, k, desc->ack_regs[i].set_reg, 0);

		d->chip.mask_ack = intc_mask_ack;
	}
#endif

	BUG_ON(k > 256); /* _INTC_ADDR_E() and _INTC_ADDR_D() are 8 bits */

	/* keep the first vector only if same enum is used multiple times */
	for (i = 0; i < desc->nr_vectors; i++) {
		struct intc_vect *vect = desc->vectors + i;
		int first_irq = evt2irq(vect->vect);

		if (!vect->enum_id)
			continue;

		for (k = i + 1; k < desc->nr_vectors; k++) {
			struct intc_vect *vect2 = desc->vectors + k;

			if (vect->enum_id != vect2->enum_id)
				continue;

			vect2->enum_id = 0;

			if (!intc_evt2irq_table)
				intc_evt2irq_table = alloc_bootmem(NR_IRQS);

			if (!intc_evt2irq_table) {
				pr_warning("intc: cannot allocate evt2irq!\n");
				continue;
			}

			intc_evt2irq_table[evt2irq(vect2->vect)] = first_irq;
		}
	}

	/* register the vectors one by one */
	for (i = 0; i < desc->nr_vectors; i++) {
		struct intc_vect *vect = desc->vectors + i;
		unsigned int irq = evt2irq(vect->vect);
		struct irq_desc *irq_desc;

		if (!vect->enum_id)
			continue;

		irq_desc = irq_to_desc_alloc_node(irq, numa_node_id());
		if (unlikely(!irq_desc)) {
			printk(KERN_INFO "can not get irq_desc for %d\n", irq);
			continue;
		}

		intc_register_irq(desc, d, vect->enum_id, irq);
	}
}

static int intc_suspend(struct sys_device *dev, pm_message_t state)
{
	struct intc_desc_int *d;
	struct irq_desc *desc;
	int irq;

	/* get intc controller associated with this sysdev */
	d = container_of(dev, struct intc_desc_int, sysdev);

	switch (state.event) {
	case PM_EVENT_ON:
		if (d->state.event != PM_EVENT_FREEZE)
			break;
		for_each_irq_desc(irq, desc) {
			if (desc->chip != &d->chip)
				continue;
			if (desc->status & IRQ_DISABLED)
				intc_disable(irq);
			else
				intc_enable(irq);
		}
		break;
	case PM_EVENT_FREEZE:
		/* nothing has to be done */
		break;
	case PM_EVENT_SUSPEND:
		/* enable wakeup irqs belonging to this intc controller */
		for_each_irq_desc(irq, desc) {
			if ((desc->status & IRQ_WAKEUP) && (desc->chip == &d->chip))
				intc_enable(irq);
		}
		break;
	}
	d->state = state;

	return 0;
}

static int intc_resume(struct sys_device *dev)
{
	return intc_suspend(dev, PMSG_ON);
}

static struct sysdev_class intc_sysdev_class = {
	.name = "intc",
	.suspend = intc_suspend,
	.resume = intc_resume,
};

/* register this intc as sysdev to allow suspend/resume */
static int __init register_intc_sysdevs(void)
{
	struct intc_desc_int *d;
	int error;
	int id = 0;

	error = sysdev_class_register(&intc_sysdev_class);
	if (!error) {
		list_for_each_entry(d, &intc_list, list) {
			d->sysdev.id = id;
			d->sysdev.cls = &intc_sysdev_class;
			error = sysdev_register(&d->sysdev);
			if (error)
				break;
			id++;
		}
	}

	if (error)
		pr_warning("intc: sysdev registration error\n");

	return error;
}

device_initcall(register_intc_sysdevs);
