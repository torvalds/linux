/*
 *	linux/arch/x86_64/kernel/irq.c
 *
 *	Copyright (C) 1992, 1998 Linus Torvalds, Ingo Molnar
 *
 * This file contains the lowest level x86_64-specific interrupt
 * entry and irq statistics code. All the remaining irq logic is
 * done by the generic kernel/irq/ code and in the
 * x86_64-specific irq controller code. (e.g. i8259.c and
 * io_apic.c.)
 */

#include <linux/kernel_stat.h>
#include <linux/interrupt.h>
#include <linux/seq_file.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <asm/uaccess.h>
#include <asm/io_apic.h>
#include <asm/idle.h>

atomic_t irq_err_count;
#ifdef CONFIG_X86_IO_APIC
#ifdef APIC_MISMATCH_DEBUG
atomic_t irq_mis_count;
#endif
#endif

/*
 * Generic, controller-independent functions:
 */

int show_interrupts(struct seq_file *p, void *v)
{
	int i = *(loff_t *) v, j;
	struct irqaction * action;
	unsigned long flags;

	if (i == 0) {
		seq_printf(p, "           ");
		for_each_online_cpu(j)
			seq_printf(p, "CPU%d       ",j);
		seq_putc(p, '\n');
	}

	if (i < NR_IRQS) {
		spin_lock_irqsave(&irq_desc[i].lock, flags);
		action = irq_desc[i].action;
		if (!action) 
			goto skip;
		seq_printf(p, "%3d: ",i);
#ifndef CONFIG_SMP
		seq_printf(p, "%10u ", kstat_irqs(i));
#else
		for_each_online_cpu(j)
			seq_printf(p, "%10u ", kstat_cpu(j).irqs[i]);
#endif
		seq_printf(p, " %14s", irq_desc[i].handler->typename);

		seq_printf(p, "  %s", action->name);
		for (action=action->next; action; action = action->next)
			seq_printf(p, ", %s", action->name);
		seq_putc(p, '\n');
skip:
		spin_unlock_irqrestore(&irq_desc[i].lock, flags);
	} else if (i == NR_IRQS) {
		seq_printf(p, "NMI: ");
		for_each_online_cpu(j)
			seq_printf(p, "%10u ", cpu_pda(j)->__nmi_count);
		seq_putc(p, '\n');
#ifdef CONFIG_X86_LOCAL_APIC
		seq_printf(p, "LOC: ");
		for_each_online_cpu(j)
			seq_printf(p, "%10u ", cpu_pda(j)->apic_timer_irqs);
		seq_putc(p, '\n');
#endif
		seq_printf(p, "ERR: %10u\n", atomic_read(&irq_err_count));
#ifdef CONFIG_X86_IO_APIC
#ifdef APIC_MISMATCH_DEBUG
		seq_printf(p, "MIS: %10u\n", atomic_read(&irq_mis_count));
#endif
#endif
	}
	return 0;
}

/*
 * do_IRQ handles all normal device IRQ's (the special
 * SMP cross-CPU interrupts have their own specific
 * handlers).
 */
asmlinkage unsigned int do_IRQ(struct pt_regs *regs)
{	
	/* high bits used in ret_from_ code  */
	unsigned irq = regs->orig_rax & 0xff;

	exit_idle();
	irq_enter();

	__do_IRQ(irq, regs);
	irq_exit();

	return 1;
}

#ifdef CONFIG_HOTPLUG_CPU
void fixup_irqs(cpumask_t map)
{
	unsigned int irq;
	static int warned;

	for (irq = 0; irq < NR_IRQS; irq++) {
		cpumask_t mask;
		if (irq == 2)
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

	/* That doesn't seem sufficient.  Give it 1ms. */
	local_irq_enable();
	mdelay(1);
	local_irq_disable();
}
#endif

extern void call_softirq(void);

asmlinkage void do_softirq(void)
{
 	__u32 pending;
 	unsigned long flags;

 	if (in_interrupt())
 		return;

 	local_irq_save(flags);
 	pending = local_softirq_pending();
 	/* Switch to interrupt stack */
 	if (pending)
		call_softirq();
 	local_irq_restore(flags);
}
EXPORT_SYMBOL(do_softirq);
