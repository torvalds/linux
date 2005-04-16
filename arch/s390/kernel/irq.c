/*
 *  arch/s390/kernel/irq.c
 *
 *  S390 version
 *    Copyright (C) 2004 IBM Deutschland Entwicklung GmbH, IBM Corporation
 *    Author(s): Martin Schwidefsky (schwidefsky@de.ibm.com),
 *
 * This file contains interrupt related functions.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/kernel_stat.h>
#include <linux/interrupt.h>
#include <linux/seq_file.h>
#include <linux/cpu.h>

/*
 * show_interrupts is needed by /proc/interrupts.
 */
int show_interrupts(struct seq_file *p, void *v)
{
	static const char *intrclass_names[] = { "EXT", "I/O", };
	int i = *(loff_t *) v, j;

	if (i == 0) {
		seq_puts(p, "           ");
		for_each_online_cpu(j)
			seq_printf(p, "CPU%d       ",j);
		seq_putc(p, '\n');
	}

	if (i < NR_IRQS) {
		seq_printf(p, "%s: ", intrclass_names[i]);
#ifndef CONFIG_SMP
		seq_printf(p, "%10u ", kstat_irqs(i));
#else
		for_each_online_cpu(j)
			seq_printf(p, "%10u ", kstat_cpu(j).irqs[i]);
#endif
                seq_putc(p, '\n');

        }

        return 0;
}

/*
 * For compatibilty only. S/390 specific setup of interrupts et al. is done
 * much later in init_channel_subsystem().
 */
void __init
init_IRQ(void)
{
	/* nothing... */
}

/*
 * Switch to the asynchronous interrupt stack for softirq execution.
 */
extern void __do_softirq(void);

asmlinkage void do_softirq(void)
{
	unsigned long flags, old, new;

	if (in_interrupt())
		return;

	local_irq_save(flags);

	account_system_vtime(current);

	local_bh_disable();

	if (local_softirq_pending()) {
		/* Get current stack pointer. */
		asm volatile("la %0,0(15)" : "=a" (old));
		/* Check against async. stack address range. */
		new = S390_lowcore.async_stack;
		if (((new - old) >> (PAGE_SHIFT + THREAD_ORDER)) != 0) {
			/* Need to switch to the async. stack. */
			new -= STACK_FRAME_OVERHEAD;
			((struct stack_frame *) new)->back_chain = old;

			asm volatile("   la    15,0(%0)\n"
				     "   basr  14,%2\n"
				     "   la    15,0(%1)\n"
				     : : "a" (new), "a" (old),
				         "a" (__do_softirq)
				     : "0", "1", "2", "3", "4", "5", "14",
				       "cc", "memory" );
		} else
			/* We are already on the async stack. */
			__do_softirq();
	}

	account_system_vtime(current);

	__local_bh_enable();

	local_irq_restore(flags);
}

EXPORT_SYMBOL(do_softirq);
