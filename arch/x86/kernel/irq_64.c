// SPDX-License-Identifier: GPL-2.0
/*
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
#include <linux/irq.h>
#include <linux/seq_file.h>
#include <linux/delay.h>
#include <linux/ftrace.h>
#include <linux/uaccess.h>
#include <linux/smp.h>
#include <linux/sched/task_stack.h>

#include <asm/cpu_entry_area.h>
#include <asm/io_apic.h>
#include <asm/apic.h>

int sysctl_panic_on_stackoverflow;

/*
 * Probabilistic stack overflow check:
 *
 * Regular device interrupts can enter on the following stacks:
 *
 * - User stack
 *
 * - Kernel task stack
 *
 * - Interrupt stack if a device driver reenables interrupts
 *   which should only happen in really old drivers.
 *
 * - Debug IST stack
 *
 * All other contexts are invalid.
 */
static inline void stack_overflow_check(struct pt_regs *regs)
{
#ifdef CONFIG_DEBUG_STACKOVERFLOW
#define STACK_MARGIN	128
	u64 irq_stack_top, irq_stack_bottom, estack_top, estack_bottom;
	u64 curbase = (u64)task_stack_page(current);
	struct cea_exception_stacks *estacks;

	if (user_mode(regs))
		return;

	if (regs->sp >= curbase + sizeof(struct pt_regs) + STACK_MARGIN &&
	    regs->sp <= curbase + THREAD_SIZE)
		return;

	irq_stack_top = (u64)__this_cpu_read(irq_stack_ptr);
	irq_stack_bottom = irq_stack_top - IRQ_STACK_SIZE + STACK_MARGIN;
	if (regs->sp >= irq_stack_bottom && regs->sp <= irq_stack_top)
		return;

	estacks = __this_cpu_read(cea_exception_stacks);
	estack_top = CEA_ESTACK_TOP(estacks, DB);
	estack_bottom = CEA_ESTACK_BOT(estacks, DB) + STACK_MARGIN;
	if (regs->sp >= estack_bottom && regs->sp <= estack_top)
		return;

	WARN_ONCE(1, "do_IRQ(): %s has overflown the kernel stack (cur:%Lx,sp:%lx, irq stack:%Lx-%Lx, exception stack: %Lx-%Lx, ip:%pF)\n",
		current->comm, curbase, regs->sp,
		irq_stack_bottom, irq_stack_top,
		estack_bottom, estack_top, (void *)regs->ip);

	if (sysctl_panic_on_stackoverflow)
		panic("low stack detected by irq handler - check messages\n");
#endif
}

bool handle_irq(struct irq_desc *desc, struct pt_regs *regs)
{
	stack_overflow_check(regs);

	if (IS_ERR_OR_NULL(desc))
		return false;

	generic_handle_irq_desc(desc);
	return true;
}
