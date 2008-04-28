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

#include <linux/module.h>
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
#ifdef CONFIG_PPC64
#include <asm/paca.h>
#include <asm/firmware.h>
#include <asm/lv1call.h>
#endif

int __irq_offset_value;
static int ppc_spurious_interrupts;

#ifdef CONFIG_PPC32
EXPORT_SYMBOL(__irq_offset_value);
atomic_t ppc_n_lost_interrupts;

#ifndef CONFIG_PPC_MERGE
#define NR_MASK_WORDS	((NR_IRQS + 31) / 32)
unsigned long ppc_cached_irq_mask[NR_MASK_WORDS];
#endif

#ifdef CONFIG_TAU_INT
extern int tau_initialized;
extern int tau_interrupts(int);
#endif
#endif /* CONFIG_PPC32 */

#if defined(CONFIG_SMP) && !defined(CONFIG_PPC_MERGE)
extern atomic_t ipi_recv;
extern atomic_t ipi_sent;
#endif

#ifdef CONFIG_PPC64
EXPORT_SYMBOL(irq_desc);

int distribute_irqs = 1;

static inline unsigned long get_hard_enabled(void)
{
	unsigned long enabled;

	__asm__ __volatile__("lbz %0,%1(13)"
	: "=r" (enabled) : "i" (offsetof(struct paca_struct, hard_enabled)));

	return enabled;
}

static inline void set_soft_enabled(unsigned long enable)
{
	__asm__ __volatile__("stb %0,%1(13)"
	: : "r" (enable), "i" (offsetof(struct paca_struct, soft_enabled)));
}

void raw_local_irq_restore(unsigned long en)
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
	if ((int)mfspr(SPRN_DEC) < 0)
		mtspr(SPRN_DEC, 1);

	/*
	 * Force the delivery of pending soft-disabled interrupts on PS3.
	 * Any HV call will have this side effect.
	 */
	if (firmware_has_feature(FW_FEATURE_PS3_LV1)) {
		u64 tmp;
		lv1_get_version_info(&tmp);
	}

	__hard_irq_enable();
}
EXPORT_SYMBOL(raw_local_irq_restore);
#endif /* CONFIG_PPC64 */

int show_interrupts(struct seq_file *p, void *v)
{
	int i = *(loff_t *)v, j;
	struct irqaction *action;
	irq_desc_t *desc;
	unsigned long flags;

	if (i == 0) {
		seq_puts(p, "           ");
		for_each_online_cpu(j)
			seq_printf(p, "CPU%d       ", j);
		seq_putc(p, '\n');
	}

	if (i < NR_IRQS) {
		desc = get_irq_desc(i);
		spin_lock_irqsave(&desc->lock, flags);
		action = desc->action;
		if (!action || !action->handler)
			goto skip;
		seq_printf(p, "%3d: ", i);
#ifdef CONFIG_SMP
		for_each_online_cpu(j)
			seq_printf(p, "%10u ", kstat_cpu(j).irqs[i]);
#else
		seq_printf(p, "%10u ", kstat_irqs(i));
#endif /* CONFIG_SMP */
		if (desc->chip)
			seq_printf(p, " %s ", desc->chip->typename);
		else
			seq_puts(p, "  None      ");
		seq_printf(p, "%s", (desc->status & IRQ_LEVEL) ? "Level " : "Edge  ");
		seq_printf(p, "    %s", action->name);
		for (action = action->next; action; action = action->next)
			seq_printf(p, ", %s", action->name);
		seq_putc(p, '\n');
skip:
		spin_unlock_irqrestore(&desc->lock, flags);
	} else if (i == NR_IRQS) {
#ifdef CONFIG_PPC32
#ifdef CONFIG_TAU_INT
		if (tau_initialized){
			seq_puts(p, "TAU: ");
			for_each_online_cpu(j)
				seq_printf(p, "%10u ", tau_interrupts(j));
			seq_puts(p, "  PowerPC             Thermal Assist (cpu temp)\n");
		}
#endif
#if defined(CONFIG_SMP) && !defined(CONFIG_PPC_MERGE)
		/* should this be per processor send/receive? */
		seq_printf(p, "IPI (recv/sent): %10u/%u\n",
				atomic_read(&ipi_recv), atomic_read(&ipi_sent));
#endif
#endif /* CONFIG_PPC32 */
		seq_printf(p, "BAD: %10u\n", ppc_spurious_interrupts);
	}
	return 0;
}

#ifdef CONFIG_HOTPLUG_CPU
void fixup_irqs(cpumask_t map)
{
	unsigned int irq;
	static int warned;

	for_each_irq(irq) {
		cpumask_t mask;

		if (irq_desc[irq].status & IRQ_PER_CPU)
			continue;

		cpus_and(mask, irq_desc[irq].affinity, map);
		if (any_online_cpu(mask) == NR_CPUS) {
			printk("Breaking affinity for irq %i\n", irq);
			mask = map;
		}
		if (irq_desc[irq].chip->set_affinity)
			irq_desc[irq].chip->set_affinity(irq, mask);
		else if (irq_desc[irq].action && !(warned++))
			printk("Cannot set affinity for irq %i\n", irq);
	}

	local_irq_enable();
	mdelay(1);
	local_irq_disable();
}
#endif

void do_IRQ(struct pt_regs *regs)
{
	struct pt_regs *old_regs = set_irq_regs(regs);
	unsigned int irq;
#ifdef CONFIG_IRQSTACKS
	struct thread_info *curtp, *irqtp;
#endif

	irq_enter();

#ifdef CONFIG_DEBUG_STACKOVERFLOW
	/* Debugging check for stack overflow: is there less than 2KB free? */
	{
		long sp;

		sp = __get_SP() & (THREAD_SIZE-1);

		if (unlikely(sp < (sizeof(struct thread_info) + 2048))) {
			printk("do_IRQ: stack overflow: %ld\n",
				sp - sizeof(struct thread_info));
			dump_stack();
		}
	}
#endif

	/*
	 * Every platform is required to implement ppc_md.get_irq.
	 * This function will either return an irq number or NO_IRQ to
	 * indicate there are no more pending.
	 * The value NO_IRQ_IGNORE is for buggy hardware and means that this
	 * IRQ has already been handled. -- Tom
	 */
	irq = ppc_md.get_irq();

	if (irq != NO_IRQ && irq != NO_IRQ_IGNORE) {
#ifdef CONFIG_IRQSTACKS
		/* Switch to the irq stack to handle this */
		curtp = current_thread_info();
		irqtp = hardirq_ctx[smp_processor_id()];
		if (curtp != irqtp) {
			struct irq_desc *desc = irq_desc + irq;
			void *handler = desc->handle_irq;
			unsigned long saved_sp_limit = current->thread.ksp_limit;
			if (handler == NULL)
				handler = &__do_IRQ;
			irqtp->task = curtp->task;
			irqtp->flags = 0;

			/* Copy the softirq bits in preempt_count so that the
			 * softirq checks work in the hardirq context.
			 */
			irqtp->preempt_count =
				(irqtp->preempt_count & ~SOFTIRQ_MASK) |
				(curtp->preempt_count & SOFTIRQ_MASK);

			current->thread.ksp_limit = (unsigned long)irqtp +
				_ALIGN_UP(sizeof(struct thread_info), 16);
			call_handle_irq(irq, desc, irqtp, handler);
			current->thread.ksp_limit = saved_sp_limit;
			irqtp->task = NULL;


			/* Set any flag that may have been set on the
			 * alternate stack
			 */
			if (irqtp->flags)
				set_bits(irqtp->flags, &curtp->flags);
		} else
#endif
			generic_handle_irq(irq);
	} else if (irq != NO_IRQ_IGNORE)
		/* That's not SMP safe ... but who cares ? */
		ppc_spurious_interrupts++;

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
}

void __init init_IRQ(void)
{
	if (ppc_md.init_IRQ)
		ppc_md.init_IRQ();
	irq_ctx_init();
}


#ifdef CONFIG_IRQSTACKS
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
	current->thread.ksp_limit = (unsigned long)irqtp +
				    _ALIGN_UP(sizeof(struct thread_info), 16);
	call_do_softirq(irqtp);
	current->thread.ksp_limit = saved_sp_limit;
	irqtp->task = NULL;
}

#else
#define do_softirq_onstack()	__do_softirq()
#endif /* CONFIG_IRQSTACKS */

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

#ifdef CONFIG_PPC_MERGE

static LIST_HEAD(irq_hosts);
static DEFINE_SPINLOCK(irq_big_lock);
static DEFINE_PER_CPU(unsigned int, irq_radix_reader);
static unsigned int irq_radix_writer;
struct irq_map_entry irq_map[NR_IRQS];
static unsigned int irq_virq_count = NR_IRQS;
static struct irq_host *irq_default_host;

irq_hw_number_t virq_to_hw(unsigned int virq)
{
	return irq_map[virq].hwirq;
}
EXPORT_SYMBOL_GPL(virq_to_hw);

static int default_irq_host_match(struct irq_host *h, struct device_node *np)
{
	return h->of_node != NULL && h->of_node == np;
}

struct irq_host *irq_alloc_host(struct device_node *of_node,
				unsigned int revmap_type,
				unsigned int revmap_arg,
				struct irq_host_ops *ops,
				irq_hw_number_t inval_irq)
{
	struct irq_host *host;
	unsigned int size = sizeof(struct irq_host);
	unsigned int i;
	unsigned int *rmap;
	unsigned long flags;

	/* Allocate structure and revmap table if using linear mapping */
	if (revmap_type == IRQ_HOST_MAP_LINEAR)
		size += revmap_arg * sizeof(unsigned int);
	host = zalloc_maybe_bootmem(size, GFP_KERNEL);
	if (host == NULL)
		return NULL;

	/* Fill structure */
	host->revmap_type = revmap_type;
	host->inval_irq = inval_irq;
	host->ops = ops;
	host->of_node = of_node;

	if (host->ops->match == NULL)
		host->ops->match = default_irq_host_match;

	spin_lock_irqsave(&irq_big_lock, flags);

	/* If it's a legacy controller, check for duplicates and
	 * mark it as allocated (we use irq 0 host pointer for that
	 */
	if (revmap_type == IRQ_HOST_MAP_LEGACY) {
		if (irq_map[0].host != NULL) {
			spin_unlock_irqrestore(&irq_big_lock, flags);
			/* If we are early boot, we can't free the structure,
			 * too bad...
			 * this will be fixed once slab is made available early
			 * instead of the current cruft
			 */
			if (mem_init_done)
				kfree(host);
			return NULL;
		}
		irq_map[0].host = host;
	}

	list_add(&host->link, &irq_hosts);
	spin_unlock_irqrestore(&irq_big_lock, flags);

	/* Additional setups per revmap type */
	switch(revmap_type) {
	case IRQ_HOST_MAP_LEGACY:
		/* 0 is always the invalid number for legacy */
		host->inval_irq = 0;
		/* setup us as the host for all legacy interrupts */
		for (i = 1; i < NUM_ISA_INTERRUPTS; i++) {
			irq_map[i].hwirq = i;
			smp_wmb();
			irq_map[i].host = host;
			smp_wmb();

			/* Clear norequest flags */
			get_irq_desc(i)->status &= ~IRQ_NOREQUEST;

			/* Legacy flags are left to default at this point,
			 * one can then use irq_create_mapping() to
			 * explicitly change them
			 */
			ops->map(host, i, i);
		}
		break;
	case IRQ_HOST_MAP_LINEAR:
		rmap = (unsigned int *)(host + 1);
		for (i = 0; i < revmap_arg; i++)
			rmap[i] = NO_IRQ;
		host->revmap_data.linear.size = revmap_arg;
		smp_wmb();
		host->revmap_data.linear.revmap = rmap;
		break;
	default:
		break;
	}

	pr_debug("irq: Allocated host of type %d @0x%p\n", revmap_type, host);

	return host;
}

struct irq_host *irq_find_host(struct device_node *node)
{
	struct irq_host *h, *found = NULL;
	unsigned long flags;

	/* We might want to match the legacy controller last since
	 * it might potentially be set to match all interrupts in
	 * the absence of a device node. This isn't a problem so far
	 * yet though...
	 */
	spin_lock_irqsave(&irq_big_lock, flags);
	list_for_each_entry(h, &irq_hosts, link)
		if (h->ops->match(h, node)) {
			found = h;
			break;
		}
	spin_unlock_irqrestore(&irq_big_lock, flags);
	return found;
}
EXPORT_SYMBOL_GPL(irq_find_host);

void irq_set_default_host(struct irq_host *host)
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

/* radix tree not lockless safe ! we use a brlock-type mecanism
 * for now, until we can use a lockless radix tree
 */
static void irq_radix_wrlock(unsigned long *flags)
{
	unsigned int cpu, ok;

	spin_lock_irqsave(&irq_big_lock, *flags);
	irq_radix_writer = 1;
	smp_mb();
	do {
		barrier();
		ok = 1;
		for_each_possible_cpu(cpu) {
			if (per_cpu(irq_radix_reader, cpu)) {
				ok = 0;
				break;
			}
		}
		if (!ok)
			cpu_relax();
	} while(!ok);
}

static void irq_radix_wrunlock(unsigned long flags)
{
	smp_wmb();
	irq_radix_writer = 0;
	spin_unlock_irqrestore(&irq_big_lock, flags);
}

static void irq_radix_rdlock(unsigned long *flags)
{
	local_irq_save(*flags);
	__get_cpu_var(irq_radix_reader) = 1;
	smp_mb();
	if (likely(irq_radix_writer == 0))
		return;
	__get_cpu_var(irq_radix_reader) = 0;
	smp_wmb();
	spin_lock(&irq_big_lock);
	__get_cpu_var(irq_radix_reader) = 1;
	spin_unlock(&irq_big_lock);
}

static void irq_radix_rdunlock(unsigned long flags)
{
	__get_cpu_var(irq_radix_reader) = 0;
	local_irq_restore(flags);
}

static int irq_setup_virq(struct irq_host *host, unsigned int virq,
			    irq_hw_number_t hwirq)
{
	/* Clear IRQ_NOREQUEST flag */
	get_irq_desc(virq)->status &= ~IRQ_NOREQUEST;

	/* map it */
	smp_wmb();
	irq_map[virq].hwirq = hwirq;
	smp_mb();

	if (host->ops->map(host, virq, hwirq)) {
		pr_debug("irq: -> mapping failed, freeing\n");
		irq_free_virt(virq, 1);
		return -1;
	}

	return 0;
}

unsigned int irq_create_direct_mapping(struct irq_host *host)
{
	unsigned int virq;

	if (host == NULL)
		host = irq_default_host;

	BUG_ON(host == NULL);
	WARN_ON(host->revmap_type != IRQ_HOST_MAP_NOMAP);

	virq = irq_alloc_virt(host, 1, 0);
	if (virq == NO_IRQ) {
		pr_debug("irq: create_direct virq allocation failed\n");
		return NO_IRQ;
	}

	pr_debug("irq: create_direct obtained virq %d\n", virq);

	if (irq_setup_virq(host, virq, virq))
		return NO_IRQ;

	return virq;
}

unsigned int irq_create_mapping(struct irq_host *host,
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

	/* Check if mapping already exist, if it does, call
	 * host->ops->map() to update the flags
	 */
	virq = irq_find_mapping(host, hwirq);
	if (virq != NO_IRQ) {
		if (host->ops->remap)
			host->ops->remap(host, virq, hwirq);
		pr_debug("irq: -> existing mapping on virq %d\n", virq);
		return virq;
	}

	/* Get a virtual interrupt number */
	if (host->revmap_type == IRQ_HOST_MAP_LEGACY) {
		/* Handle legacy */
		virq = (unsigned int)hwirq;
		if (virq == 0 || virq >= NUM_ISA_INTERRUPTS)
			return NO_IRQ;
		return virq;
	} else {
		/* Allocate a virtual interrupt number */
		hint = hwirq % irq_virq_count;
		virq = irq_alloc_virt(host, 1, hint);
		if (virq == NO_IRQ) {
			pr_debug("irq: -> virq allocation failed\n");
			return NO_IRQ;
		}
	}
	pr_debug("irq: -> obtained virq %d\n", virq);

	if (irq_setup_virq(host, virq, hwirq))
		return NO_IRQ;

	return virq;
}
EXPORT_SYMBOL_GPL(irq_create_mapping);

unsigned int irq_create_of_mapping(struct device_node *controller,
				   u32 *intspec, unsigned int intsize)
{
	struct irq_host *host;
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
	    type != (get_irq_desc(virq)->status & IRQF_TRIGGER_MASK))
		set_irq_type(virq, type);
	return virq;
}
EXPORT_SYMBOL_GPL(irq_create_of_mapping);

unsigned int irq_of_parse_and_map(struct device_node *dev, int index)
{
	struct of_irq oirq;

	if (of_irq_map_one(dev, index, &oirq))
		return NO_IRQ;

	return irq_create_of_mapping(oirq.controller, oirq.specifier,
				     oirq.size);
}
EXPORT_SYMBOL_GPL(irq_of_parse_and_map);

void irq_dispose_mapping(unsigned int virq)
{
	struct irq_host *host;
	irq_hw_number_t hwirq;
	unsigned long flags;

	if (virq == NO_IRQ)
		return;

	host = irq_map[virq].host;
	WARN_ON (host == NULL);
	if (host == NULL)
		return;

	/* Never unmap legacy interrupts */
	if (host->revmap_type == IRQ_HOST_MAP_LEGACY)
		return;

	/* remove chip and handler */
	set_irq_chip_and_handler(virq, NULL, NULL);

	/* Make sure it's completed */
	synchronize_irq(virq);

	/* Tell the PIC about it */
	if (host->ops->unmap)
		host->ops->unmap(host, virq);
	smp_mb();

	/* Clear reverse map */
	hwirq = irq_map[virq].hwirq;
	switch(host->revmap_type) {
	case IRQ_HOST_MAP_LINEAR:
		if (hwirq < host->revmap_data.linear.size)
			host->revmap_data.linear.revmap[hwirq] = NO_IRQ;
		break;
	case IRQ_HOST_MAP_TREE:
		/* Check if radix tree allocated yet */
		if (host->revmap_data.tree.gfp_mask == 0)
			break;
		irq_radix_wrlock(&flags);
		radix_tree_delete(&host->revmap_data.tree, hwirq);
		irq_radix_wrunlock(flags);
		break;
	}

	/* Destroy map */
	smp_mb();
	irq_map[virq].hwirq = host->inval_irq;

	/* Set some flags */
	get_irq_desc(virq)->status |= IRQ_NOREQUEST;

	/* Free it */
	irq_free_virt(virq, 1);
}
EXPORT_SYMBOL_GPL(irq_dispose_mapping);

unsigned int irq_find_mapping(struct irq_host *host,
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
	if (host->revmap_type == IRQ_HOST_MAP_LEGACY)
		return hwirq;

	/* Slow path does a linear search of the map */
	if (hint < NUM_ISA_INTERRUPTS)
		hint = NUM_ISA_INTERRUPTS;
	i = hint;
	do  {
		if (irq_map[i].host == host &&
		    irq_map[i].hwirq == hwirq)
			return i;
		i++;
		if (i >= irq_virq_count)
			i = NUM_ISA_INTERRUPTS;
	} while(i != hint);
	return NO_IRQ;
}
EXPORT_SYMBOL_GPL(irq_find_mapping);


unsigned int irq_radix_revmap(struct irq_host *host,
			      irq_hw_number_t hwirq)
{
	struct radix_tree_root *tree;
	struct irq_map_entry *ptr;
	unsigned int virq;
	unsigned long flags;

	WARN_ON(host->revmap_type != IRQ_HOST_MAP_TREE);

	/* Check if the radix tree exist yet. We test the value of
	 * the gfp_mask for that. Sneaky but saves another int in the
	 * structure. If not, we fallback to slow mode
	 */
	tree = &host->revmap_data.tree;
	if (tree->gfp_mask == 0)
		return irq_find_mapping(host, hwirq);

	/* Now try to resolve */
	irq_radix_rdlock(&flags);
	ptr = radix_tree_lookup(tree, hwirq);
	irq_radix_rdunlock(flags);

	/* Found it, return */
	if (ptr) {
		virq = ptr - irq_map;
		return virq;
	}

	/* If not there, try to insert it */
	virq = irq_find_mapping(host, hwirq);
	if (virq != NO_IRQ) {
		irq_radix_wrlock(&flags);
		radix_tree_insert(tree, hwirq, &irq_map[virq]);
		irq_radix_wrunlock(flags);
	}
	return virq;
}

unsigned int irq_linear_revmap(struct irq_host *host,
			       irq_hw_number_t hwirq)
{
	unsigned int *revmap;

	WARN_ON(host->revmap_type != IRQ_HOST_MAP_LINEAR);

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

unsigned int irq_alloc_virt(struct irq_host *host,
			    unsigned int count,
			    unsigned int hint)
{
	unsigned long flags;
	unsigned int i, j, found = NO_IRQ;

	if (count == 0 || count > (irq_virq_count - NUM_ISA_INTERRUPTS))
		return NO_IRQ;

	spin_lock_irqsave(&irq_big_lock, flags);

	/* Use hint for 1 interrupt if any */
	if (count == 1 && hint >= NUM_ISA_INTERRUPTS &&
	    hint < irq_virq_count && irq_map[hint].host == NULL) {
		found = hint;
		goto hint_found;
	}

	/* Look for count consecutive numbers in the allocatable
	 * (non-legacy) space
	 */
	for (i = NUM_ISA_INTERRUPTS, j = 0; i < irq_virq_count; i++) {
		if (irq_map[i].host != NULL)
			j = 0;
		else
			j++;

		if (j == count) {
			found = i - count + 1;
			break;
		}
	}
	if (found == NO_IRQ) {
		spin_unlock_irqrestore(&irq_big_lock, flags);
		return NO_IRQ;
	}
 hint_found:
	for (i = found; i < (found + count); i++) {
		irq_map[i].hwirq = host->inval_irq;
		smp_wmb();
		irq_map[i].host = host;
	}
	spin_unlock_irqrestore(&irq_big_lock, flags);
	return found;
}

void irq_free_virt(unsigned int virq, unsigned int count)
{
	unsigned long flags;
	unsigned int i;

	WARN_ON (virq < NUM_ISA_INTERRUPTS);
	WARN_ON (count == 0 || (virq + count) > irq_virq_count);

	spin_lock_irqsave(&irq_big_lock, flags);
	for (i = virq; i < (virq + count); i++) {
		struct irq_host *host;

		if (i < NUM_ISA_INTERRUPTS ||
		    (virq + count) > irq_virq_count)
			continue;

		host = irq_map[i].host;
		irq_map[i].hwirq = host->inval_irq;
		smp_wmb();
		irq_map[i].host = NULL;
	}
	spin_unlock_irqrestore(&irq_big_lock, flags);
}

void irq_early_init(void)
{
	unsigned int i;

	for (i = 0; i < NR_IRQS; i++)
		get_irq_desc(i)->status |= IRQ_NOREQUEST;
}

/* We need to create the radix trees late */
static int irq_late_init(void)
{
	struct irq_host *h;
	unsigned long flags;

	irq_radix_wrlock(&flags);
	list_for_each_entry(h, &irq_hosts, link) {
		if (h->revmap_type == IRQ_HOST_MAP_TREE)
			INIT_RADIX_TREE(&h->revmap_data.tree, GFP_ATOMIC);
	}
	irq_radix_wrunlock(flags);

	return 0;
}
arch_initcall(irq_late_init);

#ifdef CONFIG_VIRQ_DEBUG
static int virq_debug_show(struct seq_file *m, void *private)
{
	unsigned long flags;
	irq_desc_t *desc;
	const char *p;
	char none[] = "none";
	int i;

	seq_printf(m, "%-5s  %-7s  %-15s  %s\n", "virq", "hwirq",
		      "chip name", "host name");

	for (i = 1; i < NR_IRQS; i++) {
		desc = get_irq_desc(i);
		spin_lock_irqsave(&desc->lock, flags);

		if (desc->action && desc->action->handler) {
			seq_printf(m, "%5d  ", i);
			seq_printf(m, "0x%05lx  ", virq_to_hw(i));

			if (desc->chip && desc->chip->typename)
				p = desc->chip->typename;
			else
				p = none;
			seq_printf(m, "%-15s  ", p);

			if (irq_map[i].host && irq_map[i].host->of_node)
				p = irq_map[i].host->of_node->full_name;
			else
				p = none;
			seq_printf(m, "%s\n", p);
		}

		spin_unlock_irqrestore(&desc->lock, flags);
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
				 NULL, &virq_debug_fops))
		return -ENOMEM;

	return 0;
}
__initcall(irq_debugfs_init);
#endif /* CONFIG_VIRQ_DEBUG */

#endif /* CONFIG_PPC_MERGE */

#ifdef CONFIG_PPC64
static int __init setup_noirqdistrib(char *str)
{
	distribute_irqs = 0;
	return 1;
}

__setup("noirqdistrib", setup_noirqdistrib);
#endif /* CONFIG_PPC64 */
