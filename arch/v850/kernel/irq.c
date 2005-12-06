/*
 * arch/v850/kernel/irq.c -- High-level interrupt handling
 *
 *  Copyright (C) 2001,02,03,04,05  NEC Electronics Corporation
 *  Copyright (C) 2001,02,03,04,05  Miles Bader <miles@gnu.org>
 *  Copyright (C) 1994-2000  Ralf Baechle
 *  Copyright (C) 1992  Linus Torvalds
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License.  See the file COPYING in the main directory of this
 * archive for more details.
 *
 * This file was was derived from the mips version, arch/mips/kernel/irq.c
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/irq.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/kernel_stat.h>
#include <linux/slab.h>
#include <linux/mm.h>
#include <linux/random.h>
#include <linux/seq_file.h>

#include <asm/system.h>

/*
 * 'what should we do if we get a hw irq event on an illegal vector'.
 * each architecture has to answer this themselves, it doesn't deserve
 * a generic callback i think.
 */
void ack_bad_irq(unsigned int irq)
{
	printk("received IRQ %d with unknown interrupt type\n", irq);
}

volatile unsigned long irq_err_count, spurious_count;

/*
 * Generic, controller-independent functions:
 */

int show_interrupts(struct seq_file *p, void *v)
{
	int irq = *(loff_t *) v;

	if (irq == 0) {
		int cpu;
		seq_puts(p, "           ");
		for (cpu=0; cpu < 1 /*smp_num_cpus*/; cpu++)
			seq_printf(p, "CPU%d       ", cpu);
		seq_putc(p, '\n');
	}

	if (irq < NR_IRQS) {
		unsigned long flags;
		struct irqaction *action;

		spin_lock_irqsave(&irq_desc[irq].lock, flags);

		action = irq_desc[irq].action;
		if (action) {
			int j;
			int count = 0;
			int num = -1;
			const char *type_name = irq_desc[irq].handler->typename;

			for (j = 0; j < NR_IRQS; j++)
				if (irq_desc[j].handler->typename == type_name){
					if (irq == j)
						num = count;
					count++;
				}

			seq_printf(p, "%3d: ",irq);
			seq_printf(p, "%10u ", kstat_irqs(irq));
			if (count > 1) {
				int prec = (num >= 100 ? 3 : num >= 10 ? 2 : 1);
				seq_printf(p, " %*s%d", 14 - prec,
					   type_name, num);
			} else
				seq_printf(p, " %14s", type_name);
		
			seq_printf(p, "  %s", action->name);
			for (action=action->next; action; action = action->next)
				seq_printf(p, ", %s", action->name);
			seq_putc(p, '\n');
		}

		spin_unlock_irqrestore(&irq_desc[irq].lock, flags);
	} else if (irq == NR_IRQS)
		seq_printf(p, "ERR: %10lu\n", irq_err_count);

	return 0;
}

/* Handle interrupt IRQ.  REGS are the registers at the time of ther
   interrupt.  */
unsigned int handle_irq (int irq, struct pt_regs *regs)
{
	irq_enter();
	__do_IRQ(irq, regs);
	irq_exit();
	return 1;
}

/* Initialize irq handling for IRQs.
   BASE_IRQ, BASE_IRQ+INTERVAL, ..., BASE_IRQ+NUM*INTERVAL
   to IRQ_TYPE.  An IRQ_TYPE of 0 means to use a generic interrupt type.  */
void __init
init_irq_handlers (int base_irq, int num, int interval,
		   struct hw_interrupt_type *irq_type)
{
	while (num-- > 0) {
		irq_desc[base_irq].status  = IRQ_DISABLED;
		irq_desc[base_irq].action  = NULL;
		irq_desc[base_irq].depth   = 1;
		irq_desc[base_irq].handler = irq_type;
		base_irq += interval;
	}
}
