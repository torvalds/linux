// SPDX-License-Identifier: GPL-2.0
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
#include <linux/irq.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/profile.h>
#include <linux/bitops.h>

#include <asm/io.h>
#include <linux/uaccess.h>

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
	struct irq_data *data = irq_get_irq_data(irq);
	struct irq_chip *chip;
	static int last_cpu;
	int cpu = last_cpu + 1;

	if (!data)
		return 1;
	chip = irq_data_get_irq_chip(data);

	if (!chip->irq_set_affinity || irq_user_affinity[irq])
		return 1;

	while (!cpu_possible(cpu) ||
	       !cpumask_test_cpu(cpu, irq_default_affinity))
		cpu = (cpu < (NR_CPUS-1) ? cpu + 1 : 0);
	last_cpu = cpu;

	irq_data_update_affinity(data, cpumask_of(cpu));
	chip->irq_set_affinity(data, cpumask_of(cpu), false);
	return 0;
}
#endif /* CONFIG_SMP */

int arch_show_interrupts(struct seq_file *p, int prec)
{
	int j;

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

	irq_enter();
	generic_handle_irq_desc(desc);
	irq_exit();
}
