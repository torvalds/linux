/*
 * File:         arch/blackfin/kernel/irqchip.c
 * Based on:
 * Author:
 *
 * Created:
 * Description:  This file contains the simple DMA Implementation for Blackfin
 *
 * Modified:
 *               Copyright 2004-2006 Analog Devices Inc.
 *
 * Bugs:         Enter bugs at http://blackfin.uclinux.org/
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see the file COPYING, or write
 * to the Free Software Foundation, Inc.,
 * 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include <linux/kernel_stat.h>
#include <linux/module.h>
#include <linux/random.h>
#include <linux/seq_file.h>
#include <linux/kallsyms.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <asm/trace.h>
#include <asm/pda.h>

static atomic_t irq_err_count;
static spinlock_t irq_controller_lock;

/*
 * Dummy mask/unmask handler
 */
void dummy_mask_unmask_irq(unsigned int irq)
{
}

void ack_bad_irq(unsigned int irq)
{
	atomic_inc(&irq_err_count);
	printk(KERN_ERR "IRQ: spurious interrupt %d\n", irq);
}

static struct irq_chip bad_chip = {
	.ack = dummy_mask_unmask_irq,
	.mask = dummy_mask_unmask_irq,
	.unmask = dummy_mask_unmask_irq,
};

static int bad_stats;
static struct irq_desc bad_irq_desc = {
	.status = IRQ_DISABLED,
	.chip = &bad_chip,
	.handle_irq = handle_bad_irq,
	.depth = 1,
	.lock = __SPIN_LOCK_UNLOCKED(irq_desc->lock),
	.kstat_irqs = &bad_stats,
#ifdef CONFIG_SMP
	.affinity = CPU_MASK_ALL
#endif
};

#ifdef CONFIG_CPUMASK_OFFSTACK
/* We are not allocating a variable-sized bad_irq_desc.affinity */
#error "Blackfin architecture does not support CONFIG_CPUMASK_OFFSTACK."
#endif

int show_interrupts(struct seq_file *p, void *v)
{
	int i = *(loff_t *) v, j;
	struct irqaction *action;
	unsigned long flags;

	if (i < NR_IRQS) {
		spin_lock_irqsave(&irq_desc[i].lock, flags);
		action = irq_desc[i].action;
		if (!action)
			goto skip;
		seq_printf(p, "%3d: ", i);
		for_each_online_cpu(j)
			seq_printf(p, "%10u ", kstat_irqs_cpu(i, j));
		seq_printf(p, " %8s", irq_desc[i].chip->name);
		seq_printf(p, "  %s", action->name);
		for (action = action->next; action; action = action->next)
			seq_printf(p, "  %s", action->name);

		seq_putc(p, '\n');
 skip:
		spin_unlock_irqrestore(&irq_desc[i].lock, flags);
	} else if (i == NR_IRQS) {
		seq_printf(p, "NMI: ");
		for_each_online_cpu(j)
			seq_printf(p, "%10u ", cpu_pda[j].__nmi_count);
		seq_printf(p, "     CORE  Non Maskable Interrupt\n");
		seq_printf(p, "Err: %10u\n",  atomic_read(&irq_err_count));
	}
	return 0;
}

/*
 * do_IRQ handles all hardware IRQs.  Decoded IRQs should not
 * come via this function.  Instead, they should provide their
 * own 'handler'
 */
#ifdef CONFIG_DO_IRQ_L1
__attribute__((l1_text))
#endif
asmlinkage void asm_do_IRQ(unsigned int irq, struct pt_regs *regs)
{
	struct pt_regs *old_regs;
	struct irq_desc *desc = irq_desc + irq;
#ifndef CONFIG_IPIPE
	unsigned short pending, other_ints;
#endif
	old_regs = set_irq_regs(regs);

	/*
	 * Some hardware gives randomly wrong interrupts.  Rather
	 * than crashing, do something sensible.
	 */
	if (irq >= NR_IRQS)
		desc = &bad_irq_desc;

	irq_enter();
#ifdef CONFIG_DEBUG_STACKOVERFLOW
	/* Debugging check for stack overflow: is there less than STACK_WARN free? */
	{
		long sp;

		sp = __get_SP() & (THREAD_SIZE-1);

		if (unlikely(sp < (sizeof(struct thread_info) + STACK_WARN))) {
			dump_stack();
			printk(KERN_EMERG "%s: possible stack overflow while handling irq %i "
					" only %ld bytes free\n",
				__func__, irq, sp - sizeof(struct thread_info));
		}
	}
#endif
	generic_handle_irq(irq);

#ifndef CONFIG_IPIPE
	/*
	 * If we're the only interrupt running (ignoring IRQ15 which
	 * is for syscalls), lower our priority to IRQ14 so that
	 * softirqs run at that level.  If there's another,
	 * lower-level interrupt, irq_exit will defer softirqs to
	 * that. If the interrupt pipeline is enabled, we are already
	 * running at IRQ14 priority, so we don't need this code.
	 */
	CSYNC();
	pending = bfin_read_IPEND() & ~0x8000;
	other_ints = pending & (pending - 1);
	if (other_ints == 0)
		lower_to_irq14();
#endif /* !CONFIG_IPIPE */
	irq_exit();

	set_irq_regs(old_regs);
}

void __init init_IRQ(void)
{
	struct irq_desc *desc;
	int irq;

	spin_lock_init(&irq_controller_lock);
	for (irq = 0, desc = irq_desc; irq < NR_IRQS; irq++, desc++) {
		*desc = bad_irq_desc;
	}

	init_arch_irq();

#ifdef CONFIG_DEBUG_BFIN_HWTRACE_EXPAND
	/* Now that evt_ivhw is set up, turn this on */
	trace_buff_offset = 0;
	bfin_write_TBUFCTL(BFIN_TRACE_ON);
	printk(KERN_INFO "Hardware Trace expanded to %ik\n",
	  1 << CONFIG_DEBUG_BFIN_HWTRACE_EXPAND_LEN);
#endif
}
