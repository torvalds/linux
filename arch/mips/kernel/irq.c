/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Code to handle x86 style IRQs plus some generic interrupt stuff.
 *
 * Copyright (C) 1992 Linus Torvalds
 * Copyright (C) 1994 - 2000 Ralf Baechle
 */
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/kernel_stat.h>
#include <linux/module.h>
#include <linux/proc_fs.h>
#include <linux/slab.h>
#include <linux/mm.h>
#include <linux/random.h>
#include <linux/sched.h>
#include <linux/seq_file.h>
#include <linux/kallsyms.h>

#include <asm/atomic.h>
#include <asm/system.h>
#include <asm/uaccess.h>

/*
 * 'what should we do if we get a hw irq event on an illegal vector'.
 * each architecture has to answer this themselves.
 */
void ack_bad_irq(unsigned int irq)
{
	printk("unexpected IRQ # %d\n", irq);
}

atomic_t irq_err_count;

#ifdef CONFIG_MIPS_MT_SMTC
/*
 * SMTC Kernel needs to manipulate low-level CPU interrupt mask
 * in do_IRQ. These are passed in setup_irq_smtc() and stored
 * in this table.
 */
unsigned long irq_hwmask[NR_IRQS];
#endif /* CONFIG_MIPS_MT_SMTC */

#undef do_IRQ

/*
 * do_IRQ handles all normal device IRQ's (the special
 * SMP cross-CPU interrupts have their own specific
 * handlers).
 */
asmlinkage unsigned int do_IRQ(unsigned int irq)
{
	irq_enter();

	__DO_IRQ_SMTC_HOOK();
	__do_IRQ(irq);

	irq_exit();

	return 1;
}

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
		seq_printf(p, " %14s", irq_desc[i].chip->typename);
		seq_printf(p, "  %s", action->name);

		for (action=action->next; action; action = action->next)
			seq_printf(p, ", %s", action->name);

		seq_putc(p, '\n');
skip:
		spin_unlock_irqrestore(&irq_desc[i].lock, flags);
	} else if (i == NR_IRQS) {
		seq_putc(p, '\n');
		seq_printf(p, "ERR: %10u\n", atomic_read(&irq_err_count));
	}
	return 0;
}

asmlinkage void spurious_interrupt(void)
{
	atomic_inc(&irq_err_count);
}

#ifdef CONFIG_KGDB
extern void breakpoint(void);
extern void set_debug_traps(void);

static int kgdb_flag = 1;
static int __init nokgdb(char *str)
{
	kgdb_flag = 0;
	return 1;
}
__setup("nokgdb", nokgdb);
#endif

void __init init_IRQ(void)
{
	int i;

	for (i = 0; i < NR_IRQS; i++) {
		irq_desc[i].status  = IRQ_DISABLED;
		irq_desc[i].action  = NULL;
		irq_desc[i].depth   = 1;
		irq_desc[i].chip = &no_irq_chip;
		spin_lock_init(&irq_desc[i].lock);
#ifdef CONFIG_MIPS_MT_SMTC
		irq_hwmask[i] = 0;
#endif /* CONFIG_MIPS_MT_SMTC */
	}

	arch_init_irq();

#ifdef CONFIG_KGDB
	if (kgdb_flag) {
		printk("Wait for gdb client connection ...\n");
		set_debug_traps();
		breakpoint();
	}
#endif
}
