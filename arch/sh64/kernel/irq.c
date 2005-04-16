/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * arch/sh64/kernel/irq.c
 *
 * Copyright (C) 2000, 2001  Paolo Alberelli
 * Copyright (C) 2003  Paul Mundt
 *
 */

/*
 * IRQs are in fact implemented a bit like signal handlers for the kernel.
 * Naturally it's not a 1:1 relation, but there are similarities.
 */

#include <linux/config.h>
#include <linux/errno.h>
#include <linux/kernel_stat.h>
#include <linux/signal.h>
#include <linux/rwsem.h>
#include <linux/sched.h>
#include <linux/ioport.h>
#include <linux/interrupt.h>
#include <linux/timex.h>
#include <linux/slab.h>
#include <linux/random.h>
#include <linux/smp.h>
#include <linux/smp_lock.h>
#include <linux/init.h>
#include <linux/seq_file.h>
#include <linux/bitops.h>
#include <asm/system.h>
#include <asm/io.h>
#include <asm/smp.h>
#include <asm/pgalloc.h>
#include <asm/delay.h>
#include <asm/irq.h>
#include <linux/irq.h>

void ack_bad_irq(unsigned int irq)
{
	printk("unexpected IRQ trap at irq %02x\n", irq);
}

#if defined(CONFIG_PROC_FS)
int show_interrupts(struct seq_file *p, void *v)
{
	int i = *(loff_t *) v, j;
	struct irqaction * action;
	unsigned long flags;

	if (i == 0) {
		seq_puts(p, "           ");
		for (j=0; j<NR_CPUS; j++)
			if (cpu_online(j))
				seq_printf(p, "CPU%d       ",j);
		seq_putc(p, '\n');
	}

	if (i < NR_IRQS) {
		spin_lock_irqsave(&irq_desc[i].lock, flags);
		action = irq_desc[i].action;
		if (!action)
			goto unlock;
		seq_printf(p, "%3d: ",i);
		seq_printf(p, "%10u ", kstat_irqs(i));
		seq_printf(p, " %14s", irq_desc[i].handler->typename);
		seq_printf(p, "  %s", action->name);

		for (action=action->next; action; action = action->next)
			seq_printf(p, ", %s", action->name);
		seq_putc(p, '\n');
unlock:
		spin_unlock_irqrestore(&irq_desc[i].lock, flags);
	}
	return 0;
}
#endif

/*
 * do_NMI handles all Non-Maskable Interrupts.
 */
asmlinkage void do_NMI(unsigned long vector_num, struct pt_regs * regs)
{
	if (regs->sr & 0x40000000)
		printk("unexpected NMI trap in system mode\n");
	else
		printk("unexpected NMI trap in user mode\n");

	/* No statistics */
}

/*
 * do_IRQ handles all normal device IRQ's.
 */
asmlinkage int do_IRQ(unsigned long vector_num, struct pt_regs * regs)
{
	int irq;

	irq_enter();

	irq = irq_demux(vector_num);

	if (irq >= 0) {
		__do_IRQ(irq, regs);
	} else {
		printk("unexpected IRQ trap at vector %03lx\n", vector_num);
	}

	irq_exit();

	return 1;
}

