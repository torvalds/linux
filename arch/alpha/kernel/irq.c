/*
 *	linux/arch/alpha/kernel/irq.c
 *
 *	Copyright (C) 1995 Linus Torvalds
 *
 * This file contains the code used by various IRQ handling routines:
 * asking for different IRQ's should be done through these routines
 * instead of just grabbing them. Thus setups with different IRQ numbers
 * shouldn't result in any weird surprises, and installing new handlers
 * should be easier.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/errno.h>
#include <linux/kernel_stat.h>
#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/ptrace.h>
#include <linux/interrupt.h>
#include <linux/random.h>
#include <linux/init.h>
#include <linux/irq.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/profile.h>
#include <linux/bitops.h>

#include <asm/system.h>
#include <asm/io.h>
#include <asm/uaccess.h>

volatile unsigned long irq_err_count;
DEFINE_PER_CPU(unsigned long, irq_pmi_count);

void ack_bad_irq(unsigned int irq)
{
	irq_err_count++;
	printk(KERN_CRIT "Unexpected IRQ trap at vector %u\n", irq);
}

#ifdef CONFIG_SMP 
static char irq_user_affinity[NR_IRQS];

int irq_select_affinity(unsigned int irq)
{
	struct irq_desc *desc = irq_to_desc[irq];
	static int last_cpu;
	int cpu = last_cpu + 1;

	if (!desc || !get_irq_desc_chip(desc)->set_affinity || irq_user_affinity[irq])
		return 1;

	while (!cpu_possible(cpu) ||
	       !cpumask_test_cpu(cpu, irq_default_affinity))
		cpu = (cpu < (NR_CPUS-1) ? cpu + 1 : 0);
	last_cpu = cpu;

	cpumask_copy(desc->affinity, cpumask_of(cpu));
	get_irq_desc_chip(desc)->set_affinity(irq, cpumask_of(cpu));
	return 0;
}
#endif /* CONFIG_SMP */

int
show_interrupts(struct seq_file *p, void *v)
{
	int j;
	int irq = *(loff_t *) v;
	struct irqaction * action;
	struct irq_desc *desc;
	unsigned long flags;

#ifdef CONFIG_SMP
	if (irq == 0) {
		seq_puts(p, "           ");
		for_each_online_cpu(j)
			seq_printf(p, "CPU%d       ", j);
		seq_putc(p, '\n');
	}
#endif

	if (irq < ACTUAL_NR_IRQS) {
		desc = irq_to_desc(irq);

		if (!desc)
			return 0;

		raw_spin_lock_irqsave(&desc->lock, flags);
		action = desc->action;
		if (!action) 
			goto unlock;
		seq_printf(p, "%3d: ", irq);
#ifndef CONFIG_SMP
		seq_printf(p, "%10u ", kstat_irqs(irq));
#else
		for_each_online_cpu(j)
			seq_printf(p, "%10u ", kstat_irqs_cpu(irq, j));
#endif
		seq_printf(p, " %14s", get_irq_desc_chip(desc)->name);
		seq_printf(p, "  %c%s",
			(action->flags & IRQF_DISABLED)?'+':' ',
			action->name);

		for (action=action->next; action; action = action->next) {
			seq_printf(p, ", %c%s",
				  (action->flags & IRQF_DISABLED)?'+':' ',
				   action->name);
		}

		seq_putc(p, '\n');
unlock:
		raw_spin_unlock_irqrestore(&desc->lock, flags);
	} else if (irq == ACTUAL_NR_IRQS) {
#ifdef CONFIG_SMP
		seq_puts(p, "IPI: ");
		for_each_online_cpu(j)
			seq_printf(p, "%10lu ", cpu_data[j].ipi_count);
		seq_putc(p, '\n');
#endif
		seq_puts(p, "PMI: ");
		for_each_online_cpu(j)
			seq_printf(p, "%10lu ", per_cpu(irq_pmi_count, j));
		seq_puts(p, "          Performance Monitoring\n");
		seq_printf(p, "ERR: %10lu\n", irq_err_count);
	}
	return 0;
}

/*
 * handle_irq handles all normal device IRQ's (the special
 * SMP cross-CPU interrupts have their own specific
 * handlers).
 */

#define MAX_ILLEGAL_IRQS 16

void
handle_irq(int irq)
{	
	/* 
	 * We ack quickly, we don't want the irq controller
	 * thinking we're snobs just because some other CPU has
	 * disabled global interrupts (we have already done the
	 * INT_ACK cycles, it's too late to try to pretend to the
	 * controller that we aren't taking the interrupt).
	 *
	 * 0 return value means that this irq is already being
	 * handled by some other CPU. (or is disabled)
	 */
	static unsigned int illegal_count=0;
	struct irq_desc *desc = irq_to_desc(irq);
	
	if (!desc || ((unsigned) irq > ACTUAL_NR_IRQS &&
	    illegal_count < MAX_ILLEGAL_IRQS)) {
		irq_err_count++;
		illegal_count++;
		printk(KERN_CRIT "device_interrupt: invalid interrupt %d\n",
		       irq);
		return;
	}

	/*
	 * From here we must proceed with IPL_MAX. Note that we do not
	 * explicitly enable interrupts afterwards - some MILO PALcode
	 * (namely LX164 one) seems to have severe problems with RTI
	 * at IPL 0.
	 */
	local_irq_disable();
	irq_enter();
	generic_handle_irq_desc(irq, desc);
	irq_exit();
}
