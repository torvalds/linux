/*
 * Shared interrupt handling code for IPR and INTC2 types of IRQs.
 *
 * Copyright (C) 2007, 2008 Magnus Damm
 * Copyright (C) 2009, 2010 Paul Mundt
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
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/init.h>
#include <linux/irq.h>
#include <linux/module.h>
#include <linux/io.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/sh_intc.h>
#include <linux/sysdev.h>
#include <linux/list.h>
#include <linux/topology.h>
#include <linux/bitmap.h>
#include <linux/cpumask.h>
#include <linux/spinlock.h>
#include <linux/debugfs.h>
#include <linux/seq_file.h>
#include <linux/radix-tree.h>
#include <linux/mutex.h>
#include <linux/rcupdate.h>
#include <asm/sizes.h>

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

struct intc_window {
	phys_addr_t phys;
	void __iomem *virt;
	unsigned long size;
};

struct intc_map_entry {
	intc_enum enum_id;
	struct intc_desc_int *desc;
};

struct intc_subgroup_entry {
	unsigned int pirq;
	intc_enum enum_id;
	unsigned long handle;
};

struct intc_desc_int {
	struct list_head list;
	struct sys_device sysdev;
	struct radix_tree_root tree;
	pm_message_t state;
	spinlock_t lock;
	unsigned int index;
	unsigned long *reg;
#ifdef CONFIG_SMP
	unsigned long *smp;
#endif
	unsigned int nr_reg;
	struct intc_handle_int *prio;
	unsigned int nr_prio;
	struct intc_handle_int *sense;
	unsigned int nr_sense;
	struct intc_window *window;
	unsigned int nr_windows;
	struct irq_chip chip;
};

static LIST_HEAD(intc_list);
static unsigned int nr_intc_controllers;

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
static struct intc_map_entry intc_irq_xlate[NR_IRQS];
static DEFINE_SPINLOCK(vector_lock);
static DEFINE_SPINLOCK(xlate_lock);

#ifdef CONFIG_SMP
#define IS_SMP(x) x.smp
#define INTC_REG(d, x, c) (d->reg[(x)] + ((d->smp[(x)] & 0xff) * c))
#define SMP_NR(d, x) ((d->smp[(x)] >> 8) ? (d->smp[(x)] >> 8) : 1)
#else
#define IS_SMP(x) 0
#define INTC_REG(d, x, c) (d->reg[(x)])
#define SMP_NR(d, x) 1
#endif

static unsigned int intc_prio_level[NR_IRQS];	/* for now */
static unsigned int default_prio_level = 2;	/* 2 - 16 */
static unsigned long ack_handle[NR_IRQS];
#ifdef CONFIG_INTC_BALANCING
static unsigned long dist_handle[NR_IRQS];
#endif

struct intc_virq_list {
	unsigned int irq;
	struct intc_virq_list *next;
};

#define for_each_virq(entry, head) \
	for (entry = head; entry; entry = entry->next)

static inline struct intc_desc_int *get_intc_desc(unsigned int irq)
{
	struct irq_chip *chip = get_irq_chip(irq);

	return container_of(chip, struct intc_desc_int, chip);
}

static void intc_redirect_irq(unsigned int irq, struct irq_desc *desc)
{
	generic_handle_irq((unsigned int)get_irq_data(irq));
}

static inline void activate_irq(int irq)
{
#ifdef CONFIG_ARM
	/* ARM requires an extra step to clear IRQ_NOREQUEST, which it
	 * sets on behalf of every irq_chip.  Also sets IRQ_NOPROBE.
	 */
	set_irq_flags(irq, IRQF_VALID);
#else
	/* same effect on other architectures */
	set_irq_noprobe(irq);
#endif
}

static unsigned long intc_phys_to_virt(struct intc_desc_int *d,
				       unsigned long address)
{
	struct intc_window *window;
	int k;

	/* scan through physical windows and convert address */
	for (k = 0; k < d->nr_windows; k++) {
		window = d->window + k;

		if (address < window->phys)
			continue;

		if (address >= (window->phys + window->size))
			continue;

		address -= window->phys;
		address += (unsigned long)window->virt;

		return address;
	}

	/* no windows defined, register must be 1:1 mapped virt:phys */
	return address;
}

static unsigned int intc_get_reg(struct intc_desc_int *d, unsigned long address)
{
	unsigned int k;

	address = intc_phys_to_virt(d, address);

	for (k = 0; k < d->nr_reg; k++) {
		if (d->reg[k] == address)
			return k;
	}

	BUG();
	return 0;
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

static inline unsigned long get_field(unsigned int value, unsigned int handle)
{
	unsigned int width = _INTC_WIDTH(handle);
	unsigned int shift = _INTC_SHIFT(handle);
	unsigned int mask = ((1 << width) - 1) << shift;

	return (value & mask) >> shift;
}

static unsigned long test_8(unsigned long addr, unsigned long h,
			    unsigned long ignore)
{
	return get_field(__raw_readb(addr), h);
}

static unsigned long test_16(unsigned long addr, unsigned long h,
			     unsigned long ignore)
{
	return get_field(__raw_readw(addr), h);
}

static unsigned long test_32(unsigned long addr, unsigned long h,
			     unsigned long ignore)
{
	return get_field(__raw_readl(addr), h);
}

static unsigned long write_8(unsigned long addr, unsigned long h,
			     unsigned long data)
{
	__raw_writeb(set_field(0, data, h), addr);
	(void)__raw_readb(addr);	/* Defeat write posting */
	return 0;
}

static unsigned long write_16(unsigned long addr, unsigned long h,
			      unsigned long data)
{
	__raw_writew(set_field(0, data, h), addr);
	(void)__raw_readw(addr);	/* Defeat write posting */
	return 0;
}

static unsigned long write_32(unsigned long addr, unsigned long h,
			      unsigned long data)
{
	__raw_writel(set_field(0, data, h), addr);
	(void)__raw_readl(addr);	/* Defeat write posting */
	return 0;
}

static unsigned long modify_8(unsigned long addr, unsigned long h,
			      unsigned long data)
{
	unsigned long flags;
	local_irq_save(flags);
	__raw_writeb(set_field(__raw_readb(addr), data, h), addr);
	(void)__raw_readb(addr);	/* Defeat write posting */
	local_irq_restore(flags);
	return 0;
}

static unsigned long modify_16(unsigned long addr, unsigned long h,
			       unsigned long data)
{
	unsigned long flags;
	local_irq_save(flags);
	__raw_writew(set_field(__raw_readw(addr), data, h), addr);
	(void)__raw_readw(addr);	/* Defeat write posting */
	local_irq_restore(flags);
	return 0;
}

static unsigned long modify_32(unsigned long addr, unsigned long h,
			       unsigned long data)
{
	unsigned long flags;
	local_irq_save(flags);
	__raw_writel(set_field(__raw_readl(addr), data, h), addr);
	(void)__raw_readl(addr);	/* Defeat write posting */
	local_irq_restore(flags);
	return 0;
}

enum {
	REG_FN_ERR = 0,
	REG_FN_TEST_BASE = 1,
	REG_FN_WRITE_BASE = 5,
	REG_FN_MODIFY_BASE = 9
};

static unsigned long (*intc_reg_fns[])(unsigned long addr,
				       unsigned long h,
				       unsigned long data) = {
	[REG_FN_TEST_BASE + 0] = test_8,
	[REG_FN_TEST_BASE + 1] = test_16,
	[REG_FN_TEST_BASE + 3] = test_32,
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

static unsigned long intc_mode_field(unsigned long addr,
				     unsigned long handle,
				     unsigned long (*fn)(unsigned long,
						unsigned long,
						unsigned long),
				     unsigned int irq)
{
	return fn(addr, handle, ((1 << _INTC_WIDTH(handle)) - 1));
}

static unsigned long intc_mode_zero(unsigned long addr,
				    unsigned long handle,
				    unsigned long (*fn)(unsigned long,
					       unsigned long,
					       unsigned long),
				    unsigned int irq)
{
	return fn(addr, handle, 0);
}

static unsigned long intc_mode_prio(unsigned long addr,
				    unsigned long handle,
				    unsigned long (*fn)(unsigned long,
					       unsigned long,
					       unsigned long),
				    unsigned int irq)
{
	return fn(addr, handle, intc_prio_level[irq]);
}

static unsigned long (*intc_enable_fns[])(unsigned long addr,
					  unsigned long handle,
					  unsigned long (*fn)(unsigned long,
						    unsigned long,
						    unsigned long),
					  unsigned int irq) = {
	[MODE_ENABLE_REG] = intc_mode_field,
	[MODE_MASK_REG] = intc_mode_zero,
	[MODE_DUAL_REG] = intc_mode_field,
	[MODE_PRIO_REG] = intc_mode_prio,
	[MODE_PCLR_REG] = intc_mode_prio,
};

static unsigned long (*intc_disable_fns[])(unsigned long addr,
				  unsigned long handle,
				  unsigned long (*fn)(unsigned long,
					     unsigned long,
					     unsigned long),
				  unsigned int irq) = {
	[MODE_ENABLE_REG] = intc_mode_zero,
	[MODE_MASK_REG] = intc_mode_field,
	[MODE_DUAL_REG] = intc_mode_field,
	[MODE_PRIO_REG] = intc_mode_zero,
	[MODE_PCLR_REG] = intc_mode_field,
};

#ifdef CONFIG_INTC_BALANCING
static inline void intc_balancing_enable(unsigned int irq)
{
	struct intc_desc_int *d = get_intc_desc(irq);
	unsigned long handle = dist_handle[irq];
	unsigned long addr;

	if (irq_balancing_disabled(irq) || !handle)
		return;

	addr = INTC_REG(d, _INTC_ADDR_D(handle), 0);
	intc_reg_fns[_INTC_FN(handle)](addr, handle, 1);
}

static inline void intc_balancing_disable(unsigned int irq)
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
#else
static inline void intc_balancing_enable(unsigned int irq)
{
}

static inline void intc_balancing_disable(unsigned int irq)
{
}
#endif

static inline void _intc_enable(unsigned int irq, unsigned long handle)
{
	struct intc_desc_int *d = get_intc_desc(irq);
	unsigned long addr;
	unsigned int cpu;

	for (cpu = 0; cpu < SMP_NR(d, _INTC_ADDR_E(handle)); cpu++) {
#ifdef CONFIG_SMP
		if (!cpumask_test_cpu(cpu, irq_to_desc(irq)->affinity))
			continue;
#endif
		addr = INTC_REG(d, _INTC_ADDR_E(handle), cpu);
		intc_enable_fns[_INTC_MODE(handle)](addr, handle, intc_reg_fns\
						    [_INTC_FN(handle)], irq);
	}

	intc_balancing_enable(irq);
}

static void intc_enable(unsigned int irq)
{
	_intc_enable(irq, (unsigned long)get_irq_chip_data(irq));
}

static void intc_disable(unsigned int irq)
{
	struct intc_desc_int *d = get_intc_desc(irq);
	unsigned long handle = (unsigned long)get_irq_chip_data(irq);
	unsigned long addr;
	unsigned int cpu;

	intc_balancing_disable(irq);

	for (cpu = 0; cpu < SMP_NR(d, _INTC_ADDR_D(handle)); cpu++) {
#ifdef CONFIG_SMP
		if (!cpumask_test_cpu(cpu, irq_to_desc(irq)->affinity))
			continue;
#endif
		addr = INTC_REG(d, _INTC_ADDR_D(handle), cpu);
		intc_disable_fns[_INTC_MODE(handle)](addr, handle,intc_reg_fns\
						     [_INTC_FN(handle)], irq);
	}
}

static unsigned long
(*intc_enable_noprio_fns[])(unsigned long addr,
			    unsigned long handle,
			    unsigned long (*fn)(unsigned long,
					unsigned long,
					unsigned long),
			    unsigned int irq) = {
	[MODE_ENABLE_REG] = intc_mode_field,
	[MODE_MASK_REG] = intc_mode_zero,
	[MODE_DUAL_REG] = intc_mode_field,
	[MODE_PRIO_REG] = intc_mode_field,
	[MODE_PCLR_REG] = intc_mode_field,
};

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

static int intc_set_wake(unsigned int irq, unsigned int on)
{
	return 0; /* allow wakeup, but setup hardware in intc_suspend() */
}

#ifdef CONFIG_SMP
/*
 * This is held with the irq desc lock held, so we don't require any
 * additional locking here at the intc desc level. The affinity mask is
 * later tested in the enable/disable paths.
 */
static int intc_set_affinity(unsigned int irq, const struct cpumask *cpumask)
{
	if (!cpumask_intersects(cpumask, cpu_online_mask))
		return -1;

	cpumask_copy(irq_to_desc(irq)->affinity, cpumask);

	return 0;
}
#endif

static void intc_mask_ack(unsigned int irq)
{
	struct intc_desc_int *d = get_intc_desc(irq);
	unsigned long handle = ack_handle[irq];
	unsigned long addr;

	intc_disable(irq);

	/* read register and write zero only to the associated bit */
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

static struct intc_handle_int *intc_find_irq(struct intc_handle_int *hp,
					     unsigned int nr_hp,
					     unsigned int irq)
{
	int i;

	/*
	 * this doesn't scale well, but...
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

static unsigned int __init intc_mask_data(struct intc_desc *desc,
					  struct intc_desc_int *d,
					  intc_enum enum_id, int do_grps)
{
	unsigned int i = 0;
	unsigned int j = 0;
	unsigned int ret;

	ret = _intc_mask_data(desc, d, enum_id, &i, &j);
	if (ret)
		return ret;

	if (do_grps)
		return intc_mask_data(desc, d, intc_grp_id(desc, enum_id), 0);

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

static unsigned int __init intc_prio_data(struct intc_desc *desc,
					  struct intc_desc_int *d,
					  intc_enum enum_id, int do_grps)
{
	unsigned int i = 0;
	unsigned int j = 0;
	unsigned int ret;

	ret = _intc_prio_data(desc, d, enum_id, &i, &j);
	if (ret)
		return ret;

	if (do_grps)
		return intc_prio_data(desc, d, intc_grp_id(desc, enum_id), 0);

	return 0;
}

static void __init intc_enable_disable_enum(struct intc_desc *desc,
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

static unsigned int __init intc_sense_data(struct intc_desc *desc,
					   struct intc_desc_int *d,
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

unsigned int intc_irq_lookup(const char *chipname, intc_enum enum_id)
{
	struct intc_map_entry *ptr;
	struct intc_desc_int *d;
	unsigned int irq = 0;

	list_for_each_entry(d, &intc_list, list) {
		if (strcmp(d->chip.name, chipname) == 0) {
			ptr = radix_tree_lookup(&d->tree, enum_id);
			if (ptr) {
				irq = ptr - intc_irq_xlate;
				break;
			}
		}
	}

	return irq;
}
EXPORT_SYMBOL_GPL(intc_irq_lookup);

static int add_virq_to_pirq(unsigned int irq, unsigned int virq)
{
	struct intc_virq_list **last, *entry;
	struct irq_desc *desc = irq_to_desc(irq);

	/* scan for duplicates */
	last = (struct intc_virq_list **)&desc->handler_data;
	for_each_virq(entry, desc->handler_data) {
		if (entry->irq == virq)
			return 0;
		last = &entry->next;
	}

	entry = kzalloc(sizeof(struct intc_virq_list), GFP_ATOMIC);
	if (!entry) {
		pr_err("can't allocate VIRQ mapping for %d\n", virq);
		return -ENOMEM;
	}

	entry->irq = virq;

	*last = entry;

	return 0;
}

static void intc_virq_handler(unsigned int irq, struct irq_desc *desc)
{
	struct intc_virq_list *entry, *vlist = get_irq_data(irq);
	struct intc_desc_int *d = get_intc_desc(irq);

	desc->chip->mask_ack(irq);

	for_each_virq(entry, vlist) {
		unsigned long addr, handle;

		handle = (unsigned long)get_irq_data(entry->irq);
		addr = INTC_REG(d, _INTC_ADDR_E(handle), 0);

		if (intc_reg_fns[_INTC_FN(handle)](addr, handle, 0))
			generic_handle_irq(entry->irq);
	}

	desc->chip->unmask(irq);
}

static unsigned long __init intc_subgroup_data(struct intc_subgroup *subgroup,
					       struct intc_desc_int *d,
					       unsigned int index)
{
	unsigned int fn = REG_FN_TEST_BASE + (subgroup->reg_width >> 3) - 1;

	return _INTC_MK(fn, MODE_ENABLE_REG, intc_get_reg(d, subgroup->reg),
			0, 1, (subgroup->reg_width - 1) - index);
}

#define INTC_TAG_VIRQ_NEEDS_ALLOC	0

static void __init intc_subgroup_init_one(struct intc_desc *desc,
					  struct intc_desc_int *d,
					  struct intc_subgroup *subgroup)
{
	struct intc_map_entry *mapped;
	unsigned int pirq;
	unsigned long flags;
	int i;

	mapped = radix_tree_lookup(&d->tree, subgroup->parent_id);
	if (!mapped) {
		WARN_ON(1);
		return;
	}

	pirq = mapped - intc_irq_xlate;

	spin_lock_irqsave(&d->lock, flags);

	for (i = 0; i < ARRAY_SIZE(subgroup->enum_ids); i++) {
		struct intc_subgroup_entry *entry;
		int err;

		if (!subgroup->enum_ids[i])
			continue;

		entry = kmalloc(sizeof(*entry), GFP_NOWAIT);
		if (!entry)
			break;

		entry->pirq = pirq;
		entry->enum_id = subgroup->enum_ids[i];
		entry->handle = intc_subgroup_data(subgroup, d, i);

		err = radix_tree_insert(&d->tree, entry->enum_id, entry);
		if (unlikely(err < 0))
			break;

		radix_tree_tag_set(&d->tree, entry->enum_id,
				   INTC_TAG_VIRQ_NEEDS_ALLOC);
	}

	spin_unlock_irqrestore(&d->lock, flags);
}

static void __init intc_subgroup_init(struct intc_desc *desc,
				      struct intc_desc_int *d)
{
	int i;

	if (!desc->hw.subgroups)
		return;

	for (i = 0; i < desc->hw.nr_subgroups; i++)
		intc_subgroup_init_one(desc, d, desc->hw.subgroups + i);
}

static void __init intc_subgroup_map(struct intc_desc_int *d)
{
	struct intc_subgroup_entry *entries[32];
	unsigned long flags;
	unsigned int nr_found;
	int i;

	spin_lock_irqsave(&d->lock, flags);

restart:
	nr_found = radix_tree_gang_lookup_tag_slot(&d->tree,
			(void ***)entries, 0, ARRAY_SIZE(entries),
			INTC_TAG_VIRQ_NEEDS_ALLOC);

	for (i = 0; i < nr_found; i++) {
		struct intc_subgroup_entry *entry;
		int irq;

		entry = radix_tree_deref_slot((void **)entries[i]);
		if (unlikely(!entry))
			continue;
		if (unlikely(entry == RADIX_TREE_RETRY))
			goto restart;

		irq = create_irq();
		if (unlikely(irq < 0)) {
			pr_err("no more free IRQs, bailing..\n");
			break;
		}

		pr_info("Setting up a chained VIRQ from %d -> %d\n",
			irq, entry->pirq);

		spin_lock(&xlate_lock);
		intc_irq_xlate[irq].desc = d;
		intc_irq_xlate[irq].enum_id = entry->enum_id;
		spin_unlock(&xlate_lock);

		set_irq_chip_and_handler_name(irq, get_irq_chip(entry->pirq),
					      handle_simple_irq, "virq");
		set_irq_chip_data(irq, get_irq_chip_data(entry->pirq));

		set_irq_data(irq, (void *)entry->handle);

		set_irq_chained_handler(entry->pirq, intc_virq_handler);
		add_virq_to_pirq(entry->pirq, irq);

		radix_tree_tag_clear(&d->tree, entry->enum_id,
				     INTC_TAG_VIRQ_NEEDS_ALLOC);
		radix_tree_replace_slot((void **)entries[i],
					&intc_irq_xlate[irq]);
	}

	spin_unlock_irqrestore(&d->lock, flags);
}

void __init intc_finalize(void)
{
	struct intc_desc_int *d;

	list_for_each_entry(d, &intc_list, list)
		if (radix_tree_tagged(&d->tree, INTC_TAG_VIRQ_NEEDS_ALLOC))
			intc_subgroup_map(d);
}

static void __init intc_register_irq(struct intc_desc *desc,
				     struct intc_desc_int *d,
				     intc_enum enum_id,
				     unsigned int irq)
{
	struct intc_handle_int *hp;
	unsigned int data[2], primary;
	unsigned long flags;

	/*
	 * Register the IRQ position with the global IRQ map, then insert
	 * it in to the radix tree.
	 */
	set_bit(irq, intc_irq_map);

	spin_lock_irqsave(&xlate_lock, flags);
	radix_tree_insert(&d->tree, enum_id, &intc_irq_xlate[irq]);
	spin_unlock_irqrestore(&xlate_lock, flags);

	/*
	 * Prefer single interrupt source bitmap over other combinations:
	 *
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
		pr_warning("missing unique irq mask for irq %d (vect 0x%04x)\n",
			   irq, irq2evt(irq));

	data[0] = data[0] ? data[0] : intc_mask_data(desc, d, enum_id, 1);
	data[1] = data[1] ? data[1] : intc_prio_data(desc, d, enum_id, 1);

	if (!data[primary])
		primary ^= 1;

	BUG_ON(!data[primary]); /* must have primary masking method */

	disable_irq_nosync(irq);
	set_irq_chip_and_handler_name(irq, &d->chip,
				      handle_level_irq, "level");
	set_irq_chip_data(irq, (void *)data[primary]);

	/*
	 * set priority level
	 * - this needs to be at least 2 for 5-bit priorities on 7780
	 */
	intc_prio_level[irq] = default_prio_level;

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

	if (desc->hw.ack_regs)
		ack_handle[irq] = intc_ack_data(desc, d, enum_id);

#ifdef CONFIG_INTC_BALANCING
	if (desc->hw.mask_regs)
		dist_handle[irq] = intc_dist_data(desc, d, enum_id);
#endif

	activate_irq(irq);
}

static unsigned int __init save_reg(struct intc_desc_int *d,
				    unsigned int cnt,
				    unsigned long value,
				    unsigned int smp)
{
	if (value) {
		value = intc_phys_to_virt(d, value);

		d->reg[cnt] = value;
#ifdef CONFIG_SMP
		d->smp[cnt] = smp;
#endif
		return 1;
	}

	return 0;
}

int __init register_intc_controller(struct intc_desc *desc)
{
	unsigned int i, k, smp;
	struct intc_hw_desc *hw = &desc->hw;
	struct intc_desc_int *d;
	struct resource *res;

	pr_info("Registered controller '%s' with %u IRQs\n",
		desc->name, hw->nr_vectors);

	d = kzalloc(sizeof(*d), GFP_NOWAIT);
	if (!d)
		goto err0;

	INIT_LIST_HEAD(&d->list);
	list_add_tail(&d->list, &intc_list);

	spin_lock_init(&d->lock);

	d->index = nr_intc_controllers;

	if (desc->num_resources) {
		d->nr_windows = desc->num_resources;
		d->window = kzalloc(d->nr_windows * sizeof(*d->window),
				    GFP_NOWAIT);
		if (!d->window)
			goto err1;

		for (k = 0; k < d->nr_windows; k++) {
			res = desc->resource + k;
			WARN_ON(resource_type(res) != IORESOURCE_MEM);
			d->window[k].phys = res->start;
			d->window[k].size = resource_size(res);
			d->window[k].virt = ioremap_nocache(res->start,
							 resource_size(res));
			if (!d->window[k].virt)
				goto err2;
		}
	}

	d->nr_reg = hw->mask_regs ? hw->nr_mask_regs * 2 : 0;
#ifdef CONFIG_INTC_BALANCING
	if (d->nr_reg)
		d->nr_reg += hw->nr_mask_regs;
#endif
	d->nr_reg += hw->prio_regs ? hw->nr_prio_regs * 2 : 0;
	d->nr_reg += hw->sense_regs ? hw->nr_sense_regs : 0;
	d->nr_reg += hw->ack_regs ? hw->nr_ack_regs : 0;
	d->nr_reg += hw->subgroups ? hw->nr_subgroups : 0;

	d->reg = kzalloc(d->nr_reg * sizeof(*d->reg), GFP_NOWAIT);
	if (!d->reg)
		goto err2;

#ifdef CONFIG_SMP
	d->smp = kzalloc(d->nr_reg * sizeof(*d->smp), GFP_NOWAIT);
	if (!d->smp)
		goto err3;
#endif
	k = 0;

	if (hw->mask_regs) {
		for (i = 0; i < hw->nr_mask_regs; i++) {
			smp = IS_SMP(hw->mask_regs[i]);
			k += save_reg(d, k, hw->mask_regs[i].set_reg, smp);
			k += save_reg(d, k, hw->mask_regs[i].clr_reg, smp);
#ifdef CONFIG_INTC_BALANCING
			k += save_reg(d, k, hw->mask_regs[i].dist_reg, 0);
#endif
		}
	}

	if (hw->prio_regs) {
		d->prio = kzalloc(hw->nr_vectors * sizeof(*d->prio),
				  GFP_NOWAIT);
		if (!d->prio)
			goto err4;

		for (i = 0; i < hw->nr_prio_regs; i++) {
			smp = IS_SMP(hw->prio_regs[i]);
			k += save_reg(d, k, hw->prio_regs[i].set_reg, smp);
			k += save_reg(d, k, hw->prio_regs[i].clr_reg, smp);
		}
	}

	if (hw->sense_regs) {
		d->sense = kzalloc(hw->nr_vectors * sizeof(*d->sense),
				   GFP_NOWAIT);
		if (!d->sense)
			goto err5;

		for (i = 0; i < hw->nr_sense_regs; i++)
			k += save_reg(d, k, hw->sense_regs[i].reg, 0);
	}

	if (hw->subgroups)
		for (i = 0; i < hw->nr_subgroups; i++)
			if (hw->subgroups[i].reg)
				k+= save_reg(d, k, hw->subgroups[i].reg, 0);

	d->chip.name = desc->name;
	d->chip.mask = intc_disable;
	d->chip.unmask = intc_enable;
	d->chip.mask_ack = intc_disable;
	d->chip.enable = intc_enable;
	d->chip.disable = intc_disable;
	d->chip.shutdown = intc_disable;
	d->chip.set_type = intc_set_sense;
	d->chip.set_wake = intc_set_wake;
#ifdef CONFIG_SMP
	d->chip.set_affinity = intc_set_affinity;
#endif

	if (hw->ack_regs) {
		for (i = 0; i < hw->nr_ack_regs; i++)
			k += save_reg(d, k, hw->ack_regs[i].set_reg, 0);

		d->chip.mask_ack = intc_mask_ack;
	}

	/* disable bits matching force_disable before registering irqs */
	if (desc->force_disable)
		intc_enable_disable_enum(desc, d, desc->force_disable, 0);

	/* disable bits matching force_enable before registering irqs */
	if (desc->force_enable)
		intc_enable_disable_enum(desc, d, desc->force_enable, 0);

	BUG_ON(k > 256); /* _INTC_ADDR_E() and _INTC_ADDR_D() are 8 bits */

	/* register the vectors one by one */
	for (i = 0; i < hw->nr_vectors; i++) {
		struct intc_vect *vect = hw->vectors + i;
		unsigned int irq = evt2irq(vect->vect);
		unsigned long flags;
		struct irq_desc *irq_desc;

		if (!vect->enum_id)
			continue;

		irq_desc = irq_to_desc_alloc_node(irq, numa_node_id());
		if (unlikely(!irq_desc)) {
			pr_err("can't get irq_desc for %d\n", irq);
			continue;
		}

		spin_lock_irqsave(&xlate_lock, flags);
		intc_irq_xlate[irq].enum_id = vect->enum_id;
		intc_irq_xlate[irq].desc = d;
		spin_unlock_irqrestore(&xlate_lock, flags);

		intc_register_irq(desc, d, vect->enum_id, irq);

		for (k = i + 1; k < hw->nr_vectors; k++) {
			struct intc_vect *vect2 = hw->vectors + k;
			unsigned int irq2 = evt2irq(vect2->vect);

			if (vect->enum_id != vect2->enum_id)
				continue;

			/*
			 * In the case of multi-evt handling and sparse
			 * IRQ support, each vector still needs to have
			 * its own backing irq_desc.
			 */
			irq_desc = irq_to_desc_alloc_node(irq2, numa_node_id());
			if (unlikely(!irq_desc)) {
				pr_err("can't get irq_desc for %d\n", irq2);
				continue;
			}

			vect2->enum_id = 0;

			/* redirect this interrupts to the first one */
			set_irq_chip(irq2, &dummy_irq_chip);
			set_irq_chained_handler(irq2, intc_redirect_irq);
			set_irq_data(irq2, (void *)irq);
		}
	}

	intc_subgroup_init(desc, d);

	/* enable bits matching force_enable after registering irqs */
	if (desc->force_enable)
		intc_enable_disable_enum(desc, d, desc->force_enable, 1);

	nr_intc_controllers++;

	return 0;
err5:
	kfree(d->prio);
err4:
#ifdef CONFIG_SMP
	kfree(d->smp);
err3:
#endif
	kfree(d->reg);
err2:
	for (k = 0; k < d->nr_windows; k++)
		if (d->window[k].virt)
			iounmap(d->window[k].virt);

	kfree(d->window);
err1:
	kfree(d);
err0:
	pr_err("unable to allocate INTC memory\n");

	return -ENOMEM;
}

#ifdef CONFIG_INTC_USERIMASK
static void __iomem *uimask;

int register_intc_userimask(unsigned long addr)
{
	if (unlikely(uimask))
		return -EBUSY;

	uimask = ioremap_nocache(addr, SZ_4K);
	if (unlikely(!uimask))
		return -ENOMEM;

	pr_info("userimask support registered for levels 0 -> %d\n",
		default_prio_level - 1);

	return 0;
}

static ssize_t
show_intc_userimask(struct sysdev_class *cls,
		    struct sysdev_class_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", (__raw_readl(uimask) >> 4) & 0xf);
}

static ssize_t
store_intc_userimask(struct sysdev_class *cls,
		     struct sysdev_class_attribute *attr,
		     const char *buf, size_t count)
{
	unsigned long level;

	level = simple_strtoul(buf, NULL, 10);

	/*
	 * Minimal acceptable IRQ levels are in the 2 - 16 range, but
	 * these are chomped so as to not interfere with normal IRQs.
	 *
	 * Level 1 is a special case on some CPUs in that it's not
	 * directly settable, but given that USERIMASK cuts off below a
	 * certain level, we don't care about this limitation here.
	 * Level 0 on the other hand equates to user masking disabled.
	 *
	 * We use default_prio_level as a cut off so that only special
	 * case opt-in IRQs can be mangled.
	 */
	if (level >= default_prio_level)
		return -EINVAL;

	__raw_writel(0xa5 << 24 | level << 4, uimask);

	return count;
}

static SYSDEV_CLASS_ATTR(userimask, S_IRUSR | S_IWUSR,
			 show_intc_userimask, store_intc_userimask);
#endif

#ifdef CONFIG_INTC_MAPPING_DEBUG
static int intc_irq_xlate_debug(struct seq_file *m, void *priv)
{
	int i;

	seq_printf(m, "%-5s  %-7s  %-15s\n", "irq", "enum", "chip name");

	for (i = 1; i < nr_irqs; i++) {
		struct intc_desc_int *desc = intc_irq_xlate[i].desc;

		if (!desc)
			continue;

		seq_printf(m, "%5d  ", i);
		seq_printf(m, "0x%05x  ", intc_irq_xlate[i].enum_id);
		seq_printf(m, "%-15s\n", desc->chip.name);
	}

	return 0;
}

static int intc_irq_xlate_open(struct inode *inode, struct file *file)
{
	return single_open(file, intc_irq_xlate_debug, inode->i_private);
}

static const struct file_operations intc_irq_xlate_fops = {
	.open = intc_irq_xlate_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

static int __init intc_irq_xlate_init(void)
{
	/*
	 * XXX.. use arch_debugfs_dir here when all of the intc users are
	 * converted.
	 */
	if (debugfs_create_file("intc_irq_xlate", S_IRUGO, NULL, NULL,
				&intc_irq_xlate_fops) == NULL)
		return -ENOMEM;

	return 0;
}
fs_initcall(intc_irq_xlate_init);
#endif

static ssize_t
show_intc_name(struct sys_device *dev, struct sysdev_attribute *attr, char *buf)
{
	struct intc_desc_int *d;

	d = container_of(dev, struct intc_desc_int, sysdev);

	return sprintf(buf, "%s\n", d->chip.name);
}

static SYSDEV_ATTR(name, S_IRUGO, show_intc_name, NULL);

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
			if (desc->handle_irq == intc_redirect_irq)
				continue;
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

	error = sysdev_class_register(&intc_sysdev_class);
#ifdef CONFIG_INTC_USERIMASK
	if (!error && uimask)
		error = sysdev_class_create_file(&intc_sysdev_class,
						 &attr_userimask);
#endif
	if (!error) {
		list_for_each_entry(d, &intc_list, list) {
			d->sysdev.id = d->index;
			d->sysdev.cls = &intc_sysdev_class;
			error = sysdev_register(&d->sysdev);
			if (error == 0)
				error = sysdev_create_file(&d->sysdev,
							   &attr_name);
			if (error)
				break;
		}
	}

	if (error)
		pr_err("sysdev registration error\n");

	return error;
}
device_initcall(register_intc_sysdevs);

/*
 * Dynamic IRQ allocation and deallocation
 */
unsigned int create_irq_nr(unsigned int irq_want, int node)
{
	unsigned int irq = 0, new;
	unsigned long flags;
	struct irq_desc *desc;

	spin_lock_irqsave(&vector_lock, flags);

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

	desc = irq_to_desc_alloc_node(new, node);
	if (unlikely(!desc)) {
		pr_err("can't get irq_desc for %d\n", new);
		goto out_unlock;
	}

	desc = move_irq_desc(desc, node);
	irq = new;

out_unlock:
	spin_unlock_irqrestore(&vector_lock, flags);

	if (irq > 0) {
		dynamic_irq_init(irq);
		activate_irq(irq);
	}

	return irq;
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

	dynamic_irq_cleanup(irq);

	spin_lock_irqsave(&vector_lock, flags);
	__clear_bit(irq, intc_irq_map);
	spin_unlock_irqrestore(&vector_lock, flags);
}

int reserve_irq_vector(unsigned int irq)
{
	unsigned long flags;
	int ret = 0;

	spin_lock_irqsave(&vector_lock, flags);
	if (test_and_set_bit(irq, intc_irq_map))
		ret = -EBUSY;
	spin_unlock_irqrestore(&vector_lock, flags);

	return ret;
}

void reserve_intc_vectors(struct intc_vect *vectors, unsigned int nr_vecs)
{
	unsigned long flags;
	int i;

	spin_lock_irqsave(&vector_lock, flags);
	for (i = 0; i < nr_vecs; i++)
		__set_bit(evt2irq(vectors[i].vect), intc_irq_map);
	spin_unlock_irqrestore(&vector_lock, flags);
}

void reserve_irq_legacy(void)
{
	unsigned long flags;
	int i, j;

	spin_lock_irqsave(&vector_lock, flags);
	j = find_first_bit(intc_irq_map, nr_irqs);
	for (i = 0; i < j; i++)
		__set_bit(i, intc_irq_map);
	spin_unlock_irqrestore(&vector_lock, flags);
}
