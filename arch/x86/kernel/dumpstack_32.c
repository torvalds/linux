// SPDX-License-Identifier: GPL-2.0
/*
 *  Copyright (C) 1991, 1992  Linus Torvalds
 *  Copyright (C) 2000, 2001, 2002 Andi Kleen, SuSE Labs
 */
#include <linux/sched/debug.h>
#include <linux/kallsyms.h>
#include <linux/kprobes.h>
#include <linux/uaccess.h>
#include <linux/hardirq.h>
#include <linux/kdebug.h>
#include <linux/export.h>
#include <linux/ptrace.h>
#include <linux/kexec.h>
#include <linux/sysfs.h>
#include <linux/bug.h>
#include <linux/nmi.h>

#include <asm/stacktrace.h>

const char *stack_type_name(enum stack_type type)
{
	if (type == STACK_TYPE_IRQ)
		return "IRQ";

	if (type == STACK_TYPE_SOFTIRQ)
		return "SOFTIRQ";

	if (type == STACK_TYPE_ENTRY)
		return "ENTRY_TRAMPOLINE";

	if (type == STACK_TYPE_EXCEPTION)
		return "#DF";

	return NULL;
}

static bool in_hardirq_stack(unsigned long *stack, struct stack_info *info)
{
	unsigned long *begin = (unsigned long *)this_cpu_read(hardirq_stack_ptr);
	unsigned long *end   = begin + (THREAD_SIZE / sizeof(long));

	/*
	 * This is a software stack, so 'end' can be a valid stack pointer.
	 * It just means the stack is empty.
	 */
	if (stack < begin || stack > end)
		return false;

	info->type	= STACK_TYPE_IRQ;
	info->begin	= begin;
	info->end	= end;

	/*
	 * See irq_32.c -- the next stack pointer is stored at the beginning of
	 * the stack.
	 */
	info->next_sp	= (unsigned long *)*begin;

	return true;
}

static bool in_softirq_stack(unsigned long *stack, struct stack_info *info)
{
	unsigned long *begin = (unsigned long *)this_cpu_read(softirq_stack_ptr);
	unsigned long *end   = begin + (THREAD_SIZE / sizeof(long));

	/*
	 * This is a software stack, so 'end' can be a valid stack pointer.
	 * It just means the stack is empty.
	 */
	if (stack < begin || stack > end)
		return false;

	info->type	= STACK_TYPE_SOFTIRQ;
	info->begin	= begin;
	info->end	= end;

	/*
	 * The next stack pointer is stored at the beginning of the stack.
	 * See irq_32.c.
	 */
	info->next_sp	= (unsigned long *)*begin;

	return true;
}

static bool in_doublefault_stack(unsigned long *stack, struct stack_info *info)
{
	struct cpu_entry_area *cea = get_cpu_entry_area(raw_smp_processor_id());
	struct doublefault_stack *ss = &cea->doublefault_stack;

	void *begin = ss->stack;
	void *end = begin + sizeof(ss->stack);

	if ((void *)stack < begin || (void *)stack >= end)
		return false;

	info->type	= STACK_TYPE_EXCEPTION;
	info->begin	= begin;
	info->end	= end;
	info->next_sp	= (unsigned long *)this_cpu_read(cpu_tss_rw.x86_tss.sp);

	return true;
}


int get_stack_info(unsigned long *stack, struct task_struct *task,
		   struct stack_info *info, unsigned long *visit_mask)
{
	if (!stack)
		goto unknown;

	task = task ? : current;

	if (in_task_stack(stack, task, info))
		goto recursion_check;

	if (task != current)
		goto unknown;

	if (in_entry_stack(stack, info))
		goto recursion_check;

	if (in_hardirq_stack(stack, info))
		goto recursion_check;

	if (in_softirq_stack(stack, info))
		goto recursion_check;

	if (in_doublefault_stack(stack, info))
		goto recursion_check;

	goto unknown;

recursion_check:
	/*
	 * Make sure we don't iterate through any given stack more than once.
	 * If it comes up a second time then there's something wrong going on:
	 * just break out and report an unknown stack type.
	 */
	if (visit_mask) {
		if (*visit_mask & (1UL << info->type)) {
			printk_deferred_once(KERN_WARNING "WARNING: stack recursion on stack type %d\n", info->type);
			goto unknown;
		}
		*visit_mask |= 1UL << info->type;
	}

	return 0;

unknown:
	info->type = STACK_TYPE_UNKNOWN;
	return -EINVAL;
}
