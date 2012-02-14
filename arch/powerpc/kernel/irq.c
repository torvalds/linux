/*
 *  Derived from arch/i386/kernel/irq.c
 *    Copyright (C) 1992 Linus Torvalds
 *  Adapted from arch/i386 by Gary Thomas
 *    Copyright (C) 1995-1996 Gary Thomas (gdt@linuxppc.org)
 *  Updated and modified by Cort Dougan <cort@fsmlabs.com>
 *    Copyright (C) 1996-2001 Cort Dougan
 *  Adapted for Power Macintosh by Paul Mackerras
 *    Copyright (C) 1996 Paul Mackerras (paulus@cs.anu.edu.au)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 *
 * This file contains the code used by various IRQ handling routines:
 * asking for different IRQ's should be done through these routines
 * instead of just grabbing them. Thus setups with different IRQ numbers
 * shouldn't result in any weird surprises, and installing new handlers
 * should be easier.
 *
 * The MPC8xx has an interrupt mask in the SIU.  If a bit is set, the
 * interrupt is _enabled_.  As expected, IRQ0 is bit 0 in the 32-bit
 * mask register (of which only 16 are defined), hence the weird shifting
 * and complement of the cached_irq_mask.  I want to be able to stuff
 * this right into the SIU SMASK register.
 * Many of the prep/chrp functions are conditional compiled on CONFIG_8xx
 * to reduce code space and undefined function references.
 */

#undef DEBUG

#include <linux/export.h>
#include <linux/threads.h>
#include <linux/kernel_stat.h>
#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/ptrace.h>
#include <linux/ioport.h>
#include <linux/interrupt.h>
#include <linux/timex.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/irq.h>
#include <linux/seq_file.h>
#include <linux/cpumask.h>
#include <linux/profile.h>
#include <linux/bitops.h>
#include <linux/list.h>
#include <linux/radix-tree.h>
#include <linux/mutex.h>
#include <linux/bootmem.h>
#include <linux/pci.h>
#include <linux/debugfs.h>
#include <linux/of.h>
#include <linux/of_irq.h>

#include <asm/uaccess.h>
#include <asm/system.h>
#include <asm/io.h>
#include <asm/pgtable.h>
#include <asm/irq.h>
#include <asm/cache.h>
#include <asm/prom.h>
#include <asm/ptrace.h>
#include <asm/machdep.h>
#include <asm/udbg.h>
#include <asm/smp.h>

#ifdef CONFIG_PPC64
#include <asm/paca.h>
#include <asm/firmware.h>
#include <asm/lv1call.h>
#endif
#define CREATE_TRACE_POINTS
#include <asm/trace.h>

DEFINE_PER_CPU_SHARED_ALIGNED(irq_cpustat_t, irq_stat);
EXPORT_PER_CPU_SYMBOL(irq_stat);

int __irq_offset_value;

#ifdef CONFIG_PPC32
EXPORT_SYMBOL(__irq_offset_value);
atomic_t ppc_n_lost_interrupts;

#ifdef CONFIG_TAU_INT
extern int tau_initialized;
extern int tau_interrupts(int);
#endif
#endif /* CONFIG_PPC32 */

#ifdef CONFIG_PPC64

#ifndef CONFIG_SPARSE_IRQ
EXPORT_SYMBOL(irq_desc);
#endif

int distribute_irqs = 1;

static inline notrace unsigned long get_hard_enabled(void)
{
	unsigned long enabled;

	__asm__ __volatile__("lbz %0,%1(13)"
	: "=r" (enabled) : "i" (offsetof(struct paca_struct, hard_enabled)));

	return enabled;
}

static inline notrace void set_soft_enabled(unsigned long enable)
{
	__asm__ __volatile__("stb %0,%1(13)"
	: : "r" (enable), "i" (offsetof(struct paca_struct, soft_enabled)));
}

static inline notrace void decrementer_check_overflow(void)
{
	u64 now = get_tb_or_rtc();
	u64 *next_tb = &__get_cpu_var(decrementers_next_tb);

	if (now >= *next_tb)
		set_dec(1);
}

notrace void arch_local_irq_restore(unsigned long en)
{
	/*
	 * get_paca()->soft_enabled = en;
	 * Is it ever valid to use local_irq_restore(0) when soft_enabled is 1?
	 * That was allowed before, and in such a case we do need to take care
	 * that gcc will set soft_enabled directly via r13, not choose to use
	 * an intermediate register, lest we're preempted to a different cpu.
	 */
	set_soft_enabled(en);
	if (!en)
		return;

#ifdef CONFIG_PPC_STD_MMU_64
	if (firmware_has_feature(FW_FEATURE_ISERIES)) {
		/*
		 * Do we need to disable preemption here?  Not really: in the
		 * unlikely event that we're preempted to a different cpu in
		 * between getting r13, loading its lppaca_ptr, and loading
		 * its any_int, we might call iseries_handle_interrupts without
		 * an interrupt pending on the new cpu, but that's no disaster,
		 * is it?  And the business of preempting us off the old cpu
		 * would itself involve a local_irq_restore which handles the
		 * interrupt to that cpu.
		 *
		 * But use "local_paca->lppaca_ptr" instead of "get_lppaca()"
		 * to avoid any preemption checking added into get_paca().
		 */
		if (local_paca->lppaca_ptr->int_dword.any_int)
			iseries_handle_interrupts();
	}
#endif /* CONFIG_PPC_STD_MMU_64 */

	/*
	 * if (get_paca()->hard_enabled) return;
	 * But again we need to take care that gcc gets hard_enabled directly
	 * via r13, not choose to use an intermediate register, lest we're
	 * preempted to a different cpu in between the two instructions.
	 */
	if (get_hard_enabled())
		return;

	/*
	 * Need to hard-enable interrupts here.  Since currently disabled,
	 * no need to take further asm precautions against preemption; but
	 * use local_paca instead of get_paca() to avoid preemption checking.
	 */
	local_paca->hard_enabled = en;

	/*
	 * Trigger the decrementer if we have a pending event. Some processors
	 * only trigger on edge transitions of the sign bit. We might also
	 * have disabled interrupts long enough that the decrementer wrapped
	 * to positive.
	 */
	decrementer_check_overflow();

	/*
	 * Force the delivery of pending soft-disabled interrupts on PS3.
	 * Any HV call will have this side effect.
	 */
	if (firmware_has_feature(FW_FEATURE_PS3_LV1)) {
		u64 tmp, tmp2;
		lv1_get_version_info(&tmp, &tmp2);
	}

	__hard_irq_enable();
}
EXPORT_SYMBOL(arch_local_irq_restore);
#endif /* CONFIG_PPC64 */

int arch_show_interrupts(struct seq_file *p, int prec)
{
	int j;

#if defined(CONFIG_PPC32) && defined(CONFIG_TAU_INT)
	if (tau_initialized) {
		seq_printf(p, "%*s: ", prec, "TAU");
		for_each_online_cpu(j)
			seq_printf(p, "%10u ", tau_interrupts(j));
		seq_puts(p, "  PowerPC             Thermal Assist (cpu temp)\n");
	}
#endif /* CONFIG_PPC32 && CONFIG_TAU_INT */

	seq_printf(p, "%*s: ", prec, "LOC");
	for_each_online_cpu(j)
		seq_printf(p, "%10u ", per_cpu(irq_stat, j).timer_irqs);
        seq_printf(p, "  Local timer interrupts\n");

	seq_printf(p, "%*s: ", prec, "SPU");
	for_each_online_cpu(j)
		seq_printf(p, "%10u ", per_cpu(irq_stat, j).spurious_irqs);
	seq_printf(p, "  Spurious interrupts\n");

	seq_printf(p, "%*s: ", prec, "CNT");
	for_each_online_cpu(j)
		seq_printf(p, "%10u ", per_cpu(irq_stat, j).pmu_irqs);
	seq_printf(p, "  Performance monitoring interrupts\n");

	seq_printf(p, "%*s: ", prec, "MCE");
	for_each_online_cpu(j)
		seq_printf(p, "%10u ", per_cpu(irq_stat, j).mce_exceptions);
	seq_printf(p, "  Machine check exceptions\n");

	return 0;
}

/*
 * /proc/stat helpers
 */
u64 arch_irq_stat_cpu(unsigned int cpu)
{
	u64 sum = per_cpu(irq_stat, cpu).timer_irqs;

	sum += per_cpu(irq_stat, cpu).pmu_irqs;
	sum += per_cpu(irq_stat, cpu).mce_exceptions;
	sum += per_cpu(irq_stat, cpu).spurious_irqs;

	return sum;
}

#ifdef CONFIG_HOTPLUG_CPU
void migrate_irqs(void)
{
	struct irq_desc *desc;
	unsigned int irq;
	static int warned;
	cpumask_var_t mask;
	const struct cpumask *map = cpu_online_mask;

	alloc_cpumask_var(&mask, GFP_KERNEL);

	for_each_irq(irq) {
		struct irq_data *data;
		struct irq_chip *chip;

		desc = irq_to_desc(irq);
		if (!desc)
			continue;

		data = irq_desc_get_irq_data(desc);
		if (irqd_is_per_cpu(data))
			continue;

		chip = irq_data_get_irq_chip(data);

		cpumask_and(mask, data->affinity, map);
		if (cpumask_any(mask) >= nr_cpu_ids) {
			printk("Breaking affinity for irq %i\n", irq);
			cpumask_copy(mask, map);
		}
		if (chip->irq_set_affinity)
			chip->irq_set_affinity(data, mask, true);
		else if (desc->action && !(warned++))
			printk("Cannot set affinity for irq %i\n", irq);
	}

	free_cpumask_var(mask);

	local_irq_enable();
	mdelay(1);
	local_irq_disable();
}
#endif

static inline void handle_one_irq(unsigned int irq)
{
	struct thread_info *curtp, *irqtp;
	unsigned long saved_sp_limit;
	struct irq_desc *desc;

	desc = irq_to_desc(irq);
	if (!desc)
		return;

	/* Switch to the irq stack to handle this */
	curtp = current_thread_info();
	irqtp = hardirq_ctx[smp_processor_id()];

	if (curtp == irqtp) {
		/* We're already on the irq stack, just handle it */
		desc->handle_irq(irq, desc);
		return;
	}

	saved_sp_limit = current->thread.ksp_limit;

	irqtp->task = curtp->task;
	irqtp->flags = 0;

	/* Copy the softirq bits in preempt_count so that the
	 * softirq checks work in the hardirq context. */
	irqtp->preempt_count = (irqtp->preempt_count & ~SOFTIRQ_MASK) |
			       (curtp->preempt_count & SOFTIRQ_MASK);

	current->thread.ksp_limit = (unsigned long)irqtp +
		_ALIGN_UP(sizeof(struct thread_info), 16);

	call_handle_irq(irq, desc, irqtp, desc->handle_irq);
	current->thread.ksp_limit = saved_sp_limit;
	irqtp->task = NULL;

	/* Set any flag that may have been set on the
	 * alternate stack
	 */
	if (irqtp->flags)
		set_bits(irqtp->flags, &curtp->flags);
}

static inline void check_stack_overflow(void)
{
#ifdef CONFIG_DEBUG_STACKOVERFLOW
	long sp;

	sp = __get_SP() & (THREAD_SIZE-1);

	/* check for stack overflow: is there less than 2KB free? */
	if (unlikely(sp < (sizeof(struct thread_info) + 2048))) {
		printk("do_IRQ: stack overflow: %ld\n",
			sp - sizeof(struct thread_info));
		dump_stack();
	}
#endif
}

void do_IRQ(struct pt_regs *regs)
{
	struct pt_regs *old_regs = set_irq_regs(regs);
	unsigned int irq;

	trace_irq_entry(regs);

	irq_enter();

	check_stack_overflow();

	irq = ppc_md.get_irq();

	if (irq != NO_IRQ && irq != NO_IRQ_IGNORE)
		handle_one_irq(irq);
	else if (irq != NO_IRQ_IGNORE)
		__get_cpu_var(irq_stat).spurious_irqs++;

	irq_exit();
	set_irq_regs(old_regs);

#ifdef CONFIG_PPC_ISERIES
	if (firmware_has_feature(FW_FEATURE_ISERIES) &&
			get_lppaca()->int_dword.fields.decr_int) {
		get_lppaca()->int_dword.fields.decr_int = 0;
		/* Signal a fake decrementer interrupt */
		timer_interrupt(regs);
	}
#endif

	trace_irq_exit(regs);
}

void __init init_IRQ(void)
{
	if (ppc_md.init_IRQ)
		ppc_md.init_IRQ();

	exc_lvl_ctx_init();

	irq_ctx_init();
}

#if defined(CONFIG_BOOKE) || defined(CONFIG_40x)
struct thread_info   *critirq_ctx[NR_CPUS] __read_mostly;
struct thread_info    *dbgirq_ctx[NR_CPUS] __read_mostly;
struct thread_info *mcheckirq_ctx[NR_CPUS] __read_mostly;

void exc_lvl_ctx_init(void)
{
	struct thread_info *tp;
	int i, cpu_nr;

	for_each_possible_cpu(i) {
#ifdef CONFIG_PPC64
		cpu_nr = i;
#else
		cpu_nr = get_hard_smp_processor_id(i);
#endif
		memset((void *)critirq_ctx[cpu_nr], 0, THREAD_SIZE);
		tp = critirq_ctx[cpu_nr];
		tp->cpu = cpu_nr;
		tp->preempt_count = 0;

#ifdef CONFIG_BOOKE
		memset((void *)dbgirq_ctx[cpu_nr], 0, THREAD_SIZE);
		tp = dbgirq_ctx[cpu_nr];
		tp->cpu = cpu_nr;
		tp->preempt_count = 0;

		memset((void *)mcheckirq_ctx[cpu_nr], 0, THREAD_SIZE);
		tp = mcheckirq_ctx[cpu_nr];
		tp->cpu = cpu_nr;
		tp->preempt_count = HARDIRQ_OFFSET;
#endif
	}
}
#endif

struct thread_info *softirq_ctx[NR_CPUS] __read_mostly;
struct thread_info *hardirq_ctx[NR_CPUS] __read_mostly;

void irq_ctx_init(void)
{
	struct thread_info *tp;
	int i;

	for_each_possible_cpu(i) {
		memset((void *)softirq_ctx[i], 0, THREAD_SIZE);
		tp = softirq_ctx[i];
		tp->cpu = i;
		tp->preempt_count = 0;

		memset((void *)hardirq_ctx[i], 0, THREAD_SIZE);
		tp = hardirq_ctx[i];
		tp->cpu = i;
		tp->preempt_count = HARDIRQ_OFFSET;
	}
}

static inline void do_softirq_onstack(void)
{
	struct thread_info *curtp, *irqtp;
	unsigned long saved_sp_limit = current->thread.ksp_limit;

	curtp = current_thread_info();
	irqtp = softirq_ctx[smp_processor_id()];
	irqtp->task = curtp->task;
	irqtp->flags = 0;
	current->thread.ksp_limit = (unsigned long)irqtp +
				    _ALIGN_UP(sizeof(struct thread_info), 16);
	call_do_softirq(irqtp);
	current->thread.ksp_limit = saved_sp_limit;
	irqtp->task = NULL;

	/* Set any flag that may have been set on the
	 * alternate stack
	 */
	if (irqtp->flags)
		set_bits(irqtp->flags, &curtp->flags);
}

void do_softirq(void)
{
	unsigned long flags;

	if (in_interrupt())
		return;

	local_irq_save(flags);

	if (local_softirq_pending())
		do_softirq_onstack();

	local_irq_restore(flags);
}


/*
 * IRQ controller and virtual interrupts
 */

static LIST_HEAD(irq_domain_list);
static DEFINE_MUTEX(irq_domain_mutex);
static DEFINE_MUTEX(revmap_trees_mutex);
static unsigned int irq_virq_count = NR_IRQS;
static struct irq_domain *irq_default_host;

irq_hw_number_t irqd_to_hwirq(struct irq_data *d)
{
	return d->hwirq;
}
EXPORT_SYMBOL_GPL(irqd_to_hwirq);

irq_hw_number_t virq_to_hw(unsigned int virq)
{
	struct irq_data *irq_data = irq_get_irq_data(virq);
	return WARN_ON(!irq_data) ? 0 : irq_data->hwirq;
}
EXPORT_SYMBOL_GPL(virq_to_hw);

static int default_irq_host_match(struct irq_domain *h, struct device_node *np)
{
	return h->of_node != NULL && h->of_node == np;
}

struct irq_domain *irq_alloc_host(struct device_node *of_node,
				unsigned int revmap_type,
				unsigned int revmap_arg,
				struct irq_domain_ops *ops,
				irq_hw_number_t inval_irq)
{
	struct irq_domain *host, *h;
	unsigned int size = sizeof(struct irq_domain);
	unsigned int i;
	unsigned int *rmap;

	/* Allocate structure and revmap table if using linear mapping */
	if (revmap_type == IRQ_DOMAIN_MAP_LINEAR)
		size += revmap_arg * sizeof(unsigned int);
	host = kzalloc(size, GFP_KERNEL);
	if (host == NULL)
		return NULL;

	/* Fill structure */
	host->revmap_type = revmap_type;
	host->inval_irq = inval_irq;
	host->ops = ops;
	host->of_node = of_node_get(of_node);

	if (host->ops->match == NULL)
		host->ops->match = default_irq_host_match;

	mutex_lock(&irq_domain_mutex);
	/* Make sure only one legacy controller can be created */
	if (revmap_type == IRQ_DOMAIN_MAP_LEGACY) {
		list_for_each_entry(h, &irq_domain_list, link) {
			if (WARN_ON(h->revmap_type == IRQ_DOMAIN_MAP_LEGACY)) {
				mutex_unlock(&irq_domain_mutex);
				of_node_put(host->of_node);
				kfree(host);
				return NULL;
			}
		}
	}
	list_add(&host->link, &irq_domain_list);
	mutex_unlock(&irq_domain_mutex);

	/* Additional setups per revmap type */
	switch(revmap_type) {
	case IRQ_DOMAIN_MAP_LEGACY:
		/* 0 is always the invalid number for legacy */
		host->inval_irq = 0;
		/* setup us as the host for all legacy interrupts */
		for (i = 1; i < NUM_ISA_INTERRUPTS; i++) {
			struct irq_data *irq_data = irq_get_irq_data(i);
			irq_data->hwirq = i;
			irq_data->domain = host;

			/* Legacy flags are left to default at this point,
			 * one can then use irq_create_mapping() to
			 * explicitly change them
			 */
			ops->map(host, i, i);

			/* Clear norequest flags */
			irq_clear_status_flags(i, IRQ_NOREQUEST);
		}
		break;
	case IRQ_DOMAIN_MAP_LINEAR:
		rmap = (unsigned int *)(host + 1);
		for (i = 0; i < revmap_arg; i++)
			rmap[i] = NO_IRQ;
		host->revmap_data.linear.size = revmap_arg;
		host->revmap_data.linear.revmap = rmap;
		break;
	case IRQ_DOMAIN_MAP_TREE:
		INIT_RADIX_TREE(&host->revmap_data.tree, GFP_KERNEL);
		break;
	default:
		break;
	}

	pr_debug("irq: Allocated host of type %d @0x%p\n", revmap_type, host);

	return host;
}

struct irq_domain *irq_find_host(struct device_node *node)
{
	struct irq_domain *h, *found = NULL;

	/* We might want to match the legacy controller last since
	 * it might potentially be set to match all interrupts in
	 * the absence of a device node. This isn't a problem so far
	 * yet though...
	 */
	mutex_lock(&irq_domain_mutex);
	list_for_each_entry(h, &irq_domain_list, link)
		if (h->ops->match(h, node)) {
			found = h;
			break;
		}
	mutex_unlock(&irq_domain_mutex);
	return found;
}
EXPORT_SYMBOL_GPL(irq_find_host);

void irq_set_default_host(struct irq_domain *host)
{
	pr_debug("irq: Default host set to @0x%p\n", host);

	irq_default_host = host;
}

void irq_set_virq_count(unsigned int count)
{
	pr_debug("irq: Trying to set virq count to %d\n", count);

	BUG_ON(count < NUM_ISA_INTERRUPTS);
	if (count < NR_IRQS)
		irq_virq_count = count;
}

static int irq_setup_virq(struct irq_domain *host, unsigned int virq,
			    irq_hw_number_t hwirq)
{
	struct irq_data *irq_data = irq_get_irq_data(virq);

	irq_data->hwirq = hwirq;
	irq_data->domain = host;
	if (host->ops->map(host, virq, hwirq)) {
		pr_debug("irq: -> mapping failed, freeing\n");
		irq_data->domain = NULL;
		irq_data->hwirq = 0;
		return -1;
	}

	irq_clear_status_flags(virq, IRQ_NOREQUEST);

	return 0;
}

unsigned int irq_create_direct_mapping(struct irq_domain *host)
{
	unsigned int virq;

	if (host == NULL)
		host = irq_default_host;

	BUG_ON(host == NULL);
	WARN_ON(host->revmap_type != IRQ_DOMAIN_MAP_NOMAP);

	virq = irq_alloc_desc_from(1, 0);
	if (virq == NO_IRQ) {
		pr_debug("irq: create_direct virq allocation failed\n");
		return NO_IRQ;
	}
	if (virq >= irq_virq_count) {
		pr_err("ERROR: no free irqs available below %i maximum\n",
			irq_virq_count);
		irq_free_desc(virq);
		return 0;
	}

	pr_debug("irq: create_direct obtained virq %d\n", virq);

	if (irq_setup_virq(host, virq, virq)) {
		irq_free_desc(virq);
		return NO_IRQ;
	}

	return virq;
}

unsigned int irq_create_mapping(struct irq_domain *host,
				irq_hw_number_t hwirq)
{
	unsigned int virq, hint;

	pr_debug("irq: irq_create_mapping(0x%p, 0x%lx)\n", host, hwirq);

	/* Look for default host if nececssary */
	if (host == NULL)
		host = irq_default_host;
	if (host == NULL) {
		printk(KERN_WARNING "irq_create_mapping called for"
		       " NULL host, hwirq=%lx\n", hwirq);
		WARN_ON(1);
		return NO_IRQ;
	}
	pr_debug("irq: -> using host @%p\n", host);

	/* Check if mapping already exists */
	virq = irq_find_mapping(host, hwirq);
	if (virq != NO_IRQ) {
		pr_debug("irq: -> existing mapping on virq %d\n", virq);
		return virq;
	}

	/* Get a virtual interrupt number */
	if (host->revmap_type == IRQ_DOMAIN_MAP_LEGACY) {
		/* Handle legacy */
		virq = (unsigned int)hwirq;
		if (virq == 0 || virq >= NUM_ISA_INTERRUPTS)
			return NO_IRQ;
		return virq;
	} else {
		/* Allocate a virtual interrupt number */
		hint = hwirq % irq_virq_count;
		if (hint == 0)
			hint = 1;
		virq = irq_alloc_desc_from(hint, 0);
		if (!virq)
			virq = irq_alloc_desc_from(1, 0);
		if (virq == NO_IRQ) {
			pr_debug("irq: -> virq allocation failed\n");
			return NO_IRQ;
		}
	}

	if (irq_setup_virq(host, virq, hwirq)) {
		if (host->revmap_type != IRQ_DOMAIN_MAP_LEGACY)
			irq_free_desc(virq);
		return NO_IRQ;
	}

	pr_debug("irq: irq %lu on host %s mapped to virtual irq %u\n",
		hwirq, host->of_node ? host->of_node->full_name : "null", virq);

	return virq;
}
EXPORT_SYMBOL_GPL(irq_create_mapping);

unsigned int irq_create_of_mapping(struct device_node *controller,
				   const u32 *intspec, unsigned int intsize)
{
	struct irq_domain *host;
	irq_hw_number_t hwirq;
	unsigned int type = IRQ_TYPE_NONE;
	unsigned int virq;

	if (controller == NULL)
		host = irq_default_host;
	else
		host = irq_find_host(controller);
	if (host == NULL) {
		printk(KERN_WARNING "irq: no irq host found for %s !\n",
		       controller->full_name);
		return NO_IRQ;
	}

	/* If host has no translation, then we assume interrupt line */
	if (host->ops->xlate == NULL)
		hwirq = intspec[0];
	else {
		if (host->ops->xlate(host, controller, intspec, intsize,
				     &hwirq, &type))
			return NO_IRQ;
	}

	/* Create mapping */
	virq = irq_create_mapping(host, hwirq);
	if (virq == NO_IRQ)
		return virq;

	/* Set type if specified and different than the current one */
	if (type != IRQ_TYPE_NONE &&
	    type != (irqd_get_trigger_type(irq_get_irq_data(virq))))
		irq_set_irq_type(virq, type);
	return virq;
}
EXPORT_SYMBOL_GPL(irq_create_of_mapping);

void irq_dispose_mapping(unsigned int virq)
{
	struct irq_data *irq_data = irq_get_irq_data(virq);
	struct irq_domain *host;
	irq_hw_number_t hwirq;

	if (virq == NO_IRQ || !irq_data)
		return;

	host = irq_data->domain;
	if (WARN_ON(host == NULL))
		return;

	/* Never unmap legacy interrupts */
	if (host->revmap_type == IRQ_DOMAIN_MAP_LEGACY)
		return;

	irq_set_status_flags(virq, IRQ_NOREQUEST);

	/* remove chip and handler */
	irq_set_chip_and_handler(virq, NULL, NULL);

	/* Make sure it's completed */
	synchronize_irq(virq);

	/* Tell the PIC about it */
	if (host->ops->unmap)
		host->ops->unmap(host, virq);
	smp_mb();

	/* Clear reverse map */
	hwirq = irq_data->hwirq;
	switch(host->revmap_type) {
	case IRQ_DOMAIN_MAP_LINEAR:
		if (hwirq < host->revmap_data.linear.size)
			host->revmap_data.linear.revmap[hwirq] = NO_IRQ;
		break;
	case IRQ_DOMAIN_MAP_TREE:
		mutex_lock(&revmap_trees_mutex);
		radix_tree_delete(&host->revmap_data.tree, hwirq);
		mutex_unlock(&revmap_trees_mutex);
		break;
	}

	/* Destroy map */
	irq_data->hwirq = host->inval_irq;

	irq_free_desc(virq);
}
EXPORT_SYMBOL_GPL(irq_dispose_mapping);

unsigned int irq_find_mapping(struct irq_domain *host,
			      irq_hw_number_t hwirq)
{
	unsigned int i;
	unsigned int hint = hwirq % irq_virq_count;

	/* Look for default host if nececssary */
	if (host == NULL)
		host = irq_default_host;
	if (host == NULL)
		return NO_IRQ;

	/* legacy -> bail early */
	if (host->revmap_type == IRQ_DOMAIN_MAP_LEGACY)
		return hwirq;

	/* Slow path does a linear search of the map */
	if (hint == 0)
		hint = 1;
	i = hint;
	do {
		struct irq_data *data = irq_get_irq_data(i);
		if (data && (data->domain == host) && (data->hwirq == hwirq))
			return i;
		i++;
		if (i >= irq_virq_count)
			i = 1;
	} while(i != hint);
	return NO_IRQ;
}
EXPORT_SYMBOL_GPL(irq_find_mapping);

#ifdef CONFIG_SMP
int irq_choose_cpu(const struct cpumask *mask)
{
	int cpuid;

	if (cpumask_equal(mask, cpu_all_mask)) {
		static int irq_rover;
		static DEFINE_RAW_SPINLOCK(irq_rover_lock);
		unsigned long flags;

		/* Round-robin distribution... */
do_round_robin:
		raw_spin_lock_irqsave(&irq_rover_lock, flags);

		irq_rover = cpumask_next(irq_rover, cpu_online_mask);
		if (irq_rover >= nr_cpu_ids)
			irq_rover = cpumask_first(cpu_online_mask);

		cpuid = irq_rover;

		raw_spin_unlock_irqrestore(&irq_rover_lock, flags);
	} else {
		cpuid = cpumask_first_and(mask, cpu_online_mask);
		if (cpuid >= nr_cpu_ids)
			goto do_round_robin;
	}

	return get_hard_smp_processor_id(cpuid);
}
#else
int irq_choose_cpu(const struct cpumask *mask)
{
	return hard_smp_processor_id();
}
#endif

unsigned int irq_radix_revmap_lookup(struct irq_domain *host,
				     irq_hw_number_t hwirq)
{
	struct irq_data *irq_data;

	if (WARN_ON_ONCE(host->revmap_type != IRQ_DOMAIN_MAP_TREE))
		return irq_find_mapping(host, hwirq);

	/*
	 * Freeing an irq can delete nodes along the path to
	 * do the lookup via call_rcu.
	 */
	rcu_read_lock();
	irq_data = radix_tree_lookup(&host->revmap_data.tree, hwirq);
	rcu_read_unlock();

	/*
	 * If found in radix tree, then fine.
	 * Else fallback to linear lookup - this should not happen in practice
	 * as it means that we failed to insert the node in the radix tree.
	 */
	return irq_data ? irq_data->irq : irq_find_mapping(host, hwirq);
}

void irq_radix_revmap_insert(struct irq_domain *host, unsigned int virq,
			     irq_hw_number_t hwirq)
{
	struct irq_data *irq_data = irq_get_irq_data(virq);

	if (WARN_ON(host->revmap_type != IRQ_DOMAIN_MAP_TREE))
		return;

	if (virq != NO_IRQ) {
		mutex_lock(&revmap_trees_mutex);
		radix_tree_insert(&host->revmap_data.tree, hwirq, irq_data);
		mutex_unlock(&revmap_trees_mutex);
	}
}

unsigned int irq_linear_revmap(struct irq_domain *host,
			       irq_hw_number_t hwirq)
{
	unsigned int *revmap;

	if (WARN_ON_ONCE(host->revmap_type != IRQ_DOMAIN_MAP_LINEAR))
		return irq_find_mapping(host, hwirq);

	/* Check revmap bounds */
	if (unlikely(hwirq >= host->revmap_data.linear.size))
		return irq_find_mapping(host, hwirq);

	/* Check if revmap was allocated */
	revmap = host->revmap_data.linear.revmap;
	if (unlikely(revmap == NULL))
		return irq_find_mapping(host, hwirq);

	/* Fill up revmap with slow path if no mapping found */
	if (unlikely(revmap[hwirq] == NO_IRQ))
		revmap[hwirq] = irq_find_mapping(host, hwirq);

	return revmap[hwirq];
}

int arch_early_irq_init(void)
{
	return 0;
}

#ifdef CONFIG_VIRQ_DEBUG
static int virq_debug_show(struct seq_file *m, void *private)
{
	unsigned long flags;
	struct irq_desc *desc;
	const char *p;
	static const char none[] = "none";
	void *data;
	int i;

	seq_printf(m, "%-5s  %-7s  %-15s  %-18s  %s\n", "virq", "hwirq",
		      "chip name", "chip data", "host name");

	for (i = 1; i < nr_irqs; i++) {
		desc = irq_to_desc(i);
		if (!desc)
			continue;

		raw_spin_lock_irqsave(&desc->lock, flags);

		if (desc->action && desc->action->handler) {
			struct irq_chip *chip;

			seq_printf(m, "%5d  ", i);
			seq_printf(m, "0x%05lx  ", desc->irq_data.hwirq);

			chip = irq_desc_get_chip(desc);
			if (chip && chip->name)
				p = chip->name;
			else
				p = none;
			seq_printf(m, "%-15s  ", p);

			data = irq_desc_get_chip_data(desc);
			seq_printf(m, "0x%16p  ", data);

			if (desc->irq_data.domain->of_node)
				p = desc->irq_data.domain->of_node->full_name;
			else
				p = none;
			seq_printf(m, "%s\n", p);
		}

		raw_spin_unlock_irqrestore(&desc->lock, flags);
	}

	return 0;
}

static int virq_debug_open(struct inode *inode, struct file *file)
{
	return single_open(file, virq_debug_show, inode->i_private);
}

static const struct file_operations virq_debug_fops = {
	.open = virq_debug_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

static int __init irq_debugfs_init(void)
{
	if (debugfs_create_file("virq_mapping", S_IRUGO, powerpc_debugfs_root,
				 NULL, &virq_debug_fops) == NULL)
		return -ENOMEM;

	return 0;
}
__initcall(irq_debugfs_init);
#endif /* CONFIG_VIRQ_DEBUG */

#ifdef CONFIG_PPC64
static int __init setup_noirqdistrib(char *str)
{
	distribute_irqs = 0;
	return 1;
}

__setup("noirqdistrib", setup_noirqdistrib);
#endif /* CONFIG_PPC64 */
