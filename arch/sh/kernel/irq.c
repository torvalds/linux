/* $Id: irq.c,v 1.20 2004/01/13 05:52:11 kkojima Exp $
 *
 * linux/arch/sh/kernel/irq.c
 *
 *	Copyright (C) 1992, 1998 Linus Torvalds, Ingo Molnar
 *
 *
 * SuperH version:  Copyright (C) 1999  Niibe Yutaka
 */

#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/kernel_stat.h>
#include <linux/seq_file.h>
#include <asm/irq.h>
#include <asm/processor.h>
#include <asm/cpu/mmu_context.h>

/*
 * 'what should we do if we get a hw irq event on an illegal vector'.
 * each architecture has to answer this themselves, it doesn't deserve
 * a generic callback i think.
 */
void ack_bad_irq(unsigned int irq)
{
	printk("unexpected IRQ trap at vector %02x\n", irq);
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


asmlinkage int do_IRQ(unsigned long r4, unsigned long r5,
		      unsigned long r6, unsigned long r7,
		      struct pt_regs regs)
{
	int irq = r4;

	irq_enter();

#ifdef CONFIG_CPU_HAS_INTEVT
	__asm__ __volatile__ (
#ifdef CONFIG_CPU_HAS_SR_RB
		"stc	r2_bank, %0\n\t"
#else
		"mov.l	@%1, %0\n\t"
#endif
		"shlr2	%0\n\t"
		"shlr2	%0\n\t"
		"shlr	%0\n\t"
		"add	#-16, %0\n\t"
		: "=z" (irq), "=r" (r4)
		: "1" (INTEVT)
		: "memory"
	);
#endif

	irq = irq_demux(irq);
	__do_IRQ(irq, &regs);
	irq_exit();
	return 1;
}
