/*
 *  Derived from arch/i386/kernel/irq.c
 *    Copyright (C) 1992 Linus Torvalds
 *  Adapted from arch/i386 by Gary Thomas
 *    Copyright (C) 1995-1996 Gary Thomas (gdt@linuxppc.org)
 *  Updated and modified by Cort Dougan <cort@fsmlabs.com>
 *    Copyright (C) 1996-2001 Cort Dougan
 *  Adapted for Power Macintosh by Paul Mackerras
 *    Copyright (C) 1996 Paul Mackerras (paulus@cs.anu.edu.au)
 *  Amiga/APUS changes by Jesper Skov (jskov@cygnus.co.uk).
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

#include <linux/module.h>
#include <linux/threads.h>
#include <linux/kernel_stat.h>
#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/ptrace.h>
#include <linux/ioport.h>
#include <linux/interrupt.h>
#include <linux/timex.h>
#include <linux/config.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/irq.h>
#include <linux/seq_file.h>
#include <linux/cpumask.h>
#include <linux/profile.h>
#include <linux/bitops.h>

#include <asm/uaccess.h>
#include <asm/system.h>
#include <asm/io.h>
#include <asm/pgtable.h>
#include <asm/irq.h>
#include <asm/cache.h>
#include <asm/prom.h>
#include <asm/ptrace.h>
#include <asm/machdep.h>
#ifdef CONFIG_PPC_ISERIES
#include <asm/paca.h>
#endif

int __irq_offset_value;
#ifdef CONFIG_PPC32
EXPORT_SYMBOL(__irq_offset_value);
#endif

static int ppc_spurious_interrupts;

#ifdef CONFIG_PPC32
#define NR_MASK_WORDS	((NR_IRQS + 31) / 32)

unsigned long ppc_cached_irq_mask[NR_MASK_WORDS];
atomic_t ppc_n_lost_interrupts;

#ifdef CONFIG_TAU_INT
extern int tau_initialized;
extern int tau_interrupts(int);
#endif

#if defined(CONFIG_SMP) && !defined(CONFIG_PPC_MERGE)
extern atomic_t ipi_recv;
extern atomic_t ipi_sent;
#endif
#endif /* CONFIG_PPC32 */

#ifdef CONFIG_PPC64
EXPORT_SYMBOL(irq_desc);

int distribute_irqs = 1;
u64 ppc64_interrupt_controller;
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
		if (desc->handler)
			seq_printf(p, " %s ", desc->handler->typename);
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

		cpus_and(mask, irq_affinity[irq], map);
		if (any_online_cpu(mask) == NR_CPUS) {
			printk("Breaking affinity for irq %i\n", irq);
			mask = map;
		}
		if (irq_desc[irq].handler->set_affinity)
			irq_desc[irq].handler->set_affinity(irq, mask);
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
	int irq;
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
	 * This function will either return an irq number or -1 to
	 * indicate there are no more pending.
	 * The value -2 is for buggy hardware and means that this IRQ
	 * has already been handled. -- Tom
	 */
	irq = ppc_md.get_irq(regs);

	if (irq >= 0) {
#ifdef CONFIG_IRQSTACKS
		/* Switch to the irq stack to handle this */
		curtp = current_thread_info();
		irqtp = hardirq_ctx[smp_processor_id()];
		if (curtp != irqtp) {
			irqtp->task = curtp->task;
			irqtp->flags = 0;
			call___do_IRQ(irq, regs, irqtp);
			irqtp->task = NULL;
			if (irqtp->flags)
				set_bits(irqtp->flags, &curtp->flags);
		} else
#endif
			__do_IRQ(irq, regs);
	} else if (irq != -2)
		/* That's not SMP safe ... but who cares ? */
		ppc_spurious_interrupts++;

        irq_exit();

#ifdef CONFIG_PPC_ISERIES
	if (get_lppaca()->int_dword.fields.decr_int) {
		get_lppaca()->int_dword.fields.decr_int = 0;
		/* Signal a fake decrementer interrupt */
		timer_interrupt(regs);
	}
#endif
}

void __init init_IRQ(void)
{
#ifdef CONFIG_PPC64
	static int once = 0;

	if (once)
		return;

	once++;

#endif
	ppc_md.init_IRQ();
#ifdef CONFIG_PPC64
	irq_ctx_init();
#endif
}

#ifdef CONFIG_PPC64
/*
 * Virtual IRQ mapping code, used on systems with XICS interrupt controllers.
 */

#define UNDEFINED_IRQ 0xffffffff
unsigned int virt_irq_to_real_map[NR_IRQS];

/*
 * Don't use virtual irqs 0, 1, 2 for devices.
 * The pcnet32 driver considers interrupt numbers < 2 to be invalid,
 * and 2 is the XICS IPI interrupt.
 * We limit virtual irqs to 17 less than NR_IRQS so that when we
 * offset them by 16 (to reserve the first 16 for ISA interrupts)
 * we don't end up with an interrupt number >= NR_IRQS.
 */
#define MIN_VIRT_IRQ	3
#define MAX_VIRT_IRQ	(NR_IRQS - NUM_ISA_INTERRUPTS - 1)
#define NR_VIRT_IRQS	(MAX_VIRT_IRQ - MIN_VIRT_IRQ + 1)

void
virt_irq_init(void)
{
	int i;
	for (i = 0; i < NR_IRQS; i++)
		virt_irq_to_real_map[i] = UNDEFINED_IRQ;
}

/* Create a mapping for a real_irq if it doesn't already exist.
 * Return the virtual irq as a convenience.
 */
int virt_irq_create_mapping(unsigned int real_irq)
{
	unsigned int virq, first_virq;
	static int warned;

	if (ppc64_interrupt_controller == IC_OPEN_PIC)
		return real_irq;	/* no mapping for openpic (for now) */

	if (ppc64_interrupt_controller == IC_CELL_PIC)
		return real_irq;	/* no mapping for iic either */

	/* don't map interrupts < MIN_VIRT_IRQ */
	if (real_irq < MIN_VIRT_IRQ) {
		virt_irq_to_real_map[real_irq] = real_irq;
		return real_irq;
	}

	/* map to a number between MIN_VIRT_IRQ and MAX_VIRT_IRQ */
	virq = real_irq;
	if (virq > MAX_VIRT_IRQ)
		virq = (virq % NR_VIRT_IRQS) + MIN_VIRT_IRQ;

	/* search for this number or a free slot */
	first_virq = virq;
	while (virt_irq_to_real_map[virq] != UNDEFINED_IRQ) {
		if (virt_irq_to_real_map[virq] == real_irq)
			return virq;
		if (++virq > MAX_VIRT_IRQ)
			virq = MIN_VIRT_IRQ;
		if (virq == first_virq)
			goto nospace;	/* oops, no free slots */
	}

	virt_irq_to_real_map[virq] = real_irq;
	return virq;

 nospace:
	if (!warned) {
		printk(KERN_CRIT "Interrupt table is full\n");
		printk(KERN_CRIT "Increase NR_IRQS (currently %d) "
		       "in your kernel sources and rebuild.\n", NR_IRQS);
		warned = 1;
	}
	return NO_IRQ;
}

/*
 * In most cases will get a hit on the very first slot checked in the
 * virt_irq_to_real_map.  Only when there are a large number of
 * IRQs will this be expensive.
 */
unsigned int real_irq_to_virt_slowpath(unsigned int real_irq)
{
	unsigned int virq;
	unsigned int first_virq;

	virq = real_irq;

	if (virq > MAX_VIRT_IRQ)
		virq = (virq % NR_VIRT_IRQS) + MIN_VIRT_IRQ;

	first_virq = virq;

	do {
		if (virt_irq_to_real_map[virq] == real_irq)
			return virq;

		virq++;

		if (virq >= MAX_VIRT_IRQ)
			virq = 0;

	} while (first_virq != virq);

	return NO_IRQ;

}
#endif /* CONFIG_PPC64 */

#ifdef CONFIG_IRQSTACKS
struct thread_info *softirq_ctx[NR_CPUS];
struct thread_info *hardirq_ctx[NR_CPUS];

void irq_ctx_init(void)
{
	struct thread_info *tp;
	int i;

	for_each_possible_cpu(i) {
		memset((void *)softirq_ctx[i], 0, THREAD_SIZE);
		tp = softirq_ctx[i];
		tp->cpu = i;
		tp->preempt_count = SOFTIRQ_OFFSET;

		memset((void *)hardirq_ctx[i], 0, THREAD_SIZE);
		tp = hardirq_ctx[i];
		tp->cpu = i;
		tp->preempt_count = HARDIRQ_OFFSET;
	}
}

static inline void do_softirq_onstack(void)
{
	struct thread_info *curtp, *irqtp;

	curtp = current_thread_info();
	irqtp = softirq_ctx[smp_processor_id()];
	irqtp->task = curtp->task;
	call_do_softirq(irqtp);
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

	if (local_softirq_pending()) {
		account_system_vtime(current);
		local_bh_disable();
		do_softirq_onstack();
		account_system_vtime(current);
		__local_bh_enable();
	}

	local_irq_restore(flags);
}
EXPORT_SYMBOL(do_softirq);

#ifdef CONFIG_PPC64
static int __init setup_noirqdistrib(char *str)
{
	distribute_irqs = 0;
	return 1;
}

__setup("noirqdistrib", setup_noirqdistrib);
#endif /* CONFIG_PPC64 */
