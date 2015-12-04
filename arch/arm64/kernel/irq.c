/*
 * Based on arch/arm/kernel/irq.c
 *
 * Copyright (C) 1992 Linus Torvalds
 * Modifications for ARM processor Copyright (C) 1995-2000 Russell King.
 * Support for Dynamic Tick Timer Copyright (C) 2004-2005 Nokia Corporation.
 * Dynamic Tick Timer written by Tony Lindgren <tony@atomide.com> and
 * Tuukka Tikkanen <tuukka.tikkanen@elektrobit.com>.
 * Copyright (C) 2012 ARM Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/kernel_stat.h>
#include <linux/irq.h>
#include <linux/smp.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/irqchip.h>
#include <linux/seq_file.h>

unsigned long irq_err_count;

/*
 * irq stack only needs to be 16 byte aligned - not IRQ_STACK_SIZE aligned.
 * irq_stack[0] is used as irq_count, a non-zero value indicates the stack
 * is in use, and el?_irq() shouldn't switch to it. This is used to detect
 * recursive use of the irq_stack, it is lazily updated by
 * do_softirq_own_stack(), which is called on the irq_stack, before
 * re-enabling interrupts to process softirqs.
 */
DEFINE_PER_CPU(unsigned long [IRQ_STACK_SIZE/sizeof(long)], irq_stack) __aligned(16);

#define IRQ_COUNT()	(*per_cpu(irq_stack, smp_processor_id()))

int arch_show_interrupts(struct seq_file *p, int prec)
{
	show_ipi_list(p, prec);
	seq_printf(p, "%*s: %10lu\n", prec, "Err", irq_err_count);
	return 0;
}

void (*handle_arch_irq)(struct pt_regs *) = NULL;

void __init set_handle_irq(void (*handle_irq)(struct pt_regs *))
{
	if (handle_arch_irq)
		return;

	handle_arch_irq = handle_irq;
}

void __init init_IRQ(void)
{
	irqchip_init();
	if (!handle_arch_irq)
		panic("No interrupt controller found.");
}

/*
 * do_softirq_own_stack() is called from irq_exit() before __do_softirq()
 * re-enables interrupts, at which point we may re-enter el?_irq(). We
 * increase irq_count here so that el1_irq() knows that it is already on the
 * irq stack.
 *
 * Called with interrupts disabled, so we don't worry about moving cpu, or
 * being interrupted while modifying irq_count.
 *
 * This function doesn't actually switch stack.
 */
void do_softirq_own_stack(void)
{
	int cpu = smp_processor_id();

	WARN_ON_ONCE(!irqs_disabled());

	if (on_irq_stack(current_stack_pointer, cpu)) {
		IRQ_COUNT()++;
		__do_softirq();
		IRQ_COUNT()--;
	} else {
		__do_softirq();
	}
}
