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

	return NULL;
}

static bool in_hardirq_stack(unsigned long *stack, struct stack_info *info)
{
	unsigned long *begin = (unsigned long *)this_cpu_read(hardirq_stack);
	unsigned long *end   = begin + (THREAD_SIZE / sizeof(long));

	/*
	 * This is a software stack, so 'end' can be a valid stack pointer.
	 * It just means the stack is empty.
	 */
	if (stack <= begin || stack > end)
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
	unsigned long *begin = (unsigned long *)this_cpu_read(softirq_stack);
	unsigned long *end   = begin + (THREAD_SIZE / sizeof(long));

	/*
	 * This is a software stack, so 'end' can be a valid stack pointer.
	 * It just means the stack is empty.
	 */
	if (stack <= begin || stack > end)
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

	if (in_hardirq_stack(stack, info))
		goto recursion_check;

	if (in_softirq_stack(stack, info))
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

void show_regs(struct pt_regs *regs)
{
	int i;

	show_regs_print_info(KERN_EMERG);
	__show_regs(regs, !user_mode(regs));

	/*
	 * When in-kernel, we also print out the stack and code at the
	 * time of the fault..
	 */
	if (!user_mode(regs)) {
		unsigned int code_prologue = code_bytes * 43 / 64;
		unsigned int code_len = code_bytes;
		unsigned char c;
		u8 *ip;

		show_trace_log_lvl(current, regs, NULL, KERN_EMERG);

		pr_emerg("Code:");

		ip = (u8 *)regs->ip - code_prologue;
		if (ip < (u8 *)PAGE_OFFSET || probe_kernel_address(ip, c)) {
			/* try starting at IP */
			ip = (u8 *)regs->ip;
			code_len = code_len - code_prologue + 1;
		}
		for (i = 0; i < code_len; i++, ip++) {
			if (ip < (u8 *)PAGE_OFFSET ||
					probe_kernel_address(ip, c)) {
				pr_cont("  Bad EIP value.");
				break;
			}
			if (ip == (u8 *)regs->ip)
				pr_cont(" <%02x>", c);
			else
				pr_cont(" %02x", c);
		}
	}
	pr_cont("\n");
}
