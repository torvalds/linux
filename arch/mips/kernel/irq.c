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

static unsigned long irq_map[NR_IRQS / BITS_PER_LONG];

int allocate_irqno(void)
{
	int irq;

again:
	irq = find_first_zero_bit(irq_map, NR_IRQS);

	if (irq >= NR_IRQS)
		return -ENOSPC;

	if (test_and_set_bit(irq, irq_map))
		goto again;

	return irq;
}

EXPORT_SYMBOL_GPL(allocate_irqno);

/*
 * Allocate the 16 legacy interrupts for i8259 devices.  This happens early
 * in the kernel initialization so treating allocation failure as BUG() is
 * ok.
 */
void __init alloc_legacy_irqno(void)
{
	int i;

	for (i = 0; i <= 16; i++)
		BUG_ON(test_and_set_bit(i, irq_map));
}

void free_irqno(unsigned int irq)
{
	smp_mb__before_clear_bit();
	clear_bit(irq, irq_map);
	smp_mb__after_clear_bit();
}

EXPORT_SYMBOL_GPL(free_irqno);

/*
 * 'what should we do if we get a hw irq event on an illegal vector'.
 * each architecture has to answer this themselves.
 */
void ack_bad_irq(unsigned int irq)
{
	smtc_im_ack_irq(irq);
	printk("unexpected IRQ # %d\n", irq);
}

atomic_t irq_err_count;

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
		seq_printf(p, " %14s", irq_desc[i].chip->name);
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
	arch_init_irq();

#ifdef CONFIG_KGDB
	if (kgdb_flag) {
		printk("Wait for gdb client connection ...\n");
		set_debug_traps();
		breakpoint();
	}
#endif
}
