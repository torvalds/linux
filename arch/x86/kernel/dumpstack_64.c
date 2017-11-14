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

static char *exception_stack_names[N_EXCEPTION_STACKS] = {
		[ DOUBLEFAULT_STACK-1	]	= "#DF",
		[ NMI_STACK-1		]	= "NMI",
		[ DEBUG_STACK-1		]	= "#DB",
		[ MCE_STACK-1		]	= "#MC",
};

static unsigned long exception_stack_sizes[N_EXCEPTION_STACKS] = {
	[0 ... N_EXCEPTION_STACKS - 1]		= EXCEPTION_STKSZ,
	[DEBUG_STACK - 1]			= DEBUG_STKSZ
};

const char *stack_type_name(enum stack_type type)
{
	BUILD_BUG_ON(N_EXCEPTION_STACKS != 4);

	if (type == STACK_TYPE_IRQ)
		return "IRQ";

	if (type >= STACK_TYPE_EXCEPTION && type <= STACK_TYPE_EXCEPTION_LAST)
		return exception_stack_names[type - STACK_TYPE_EXCEPTION];

	return NULL;
}

static bool in_exception_stack(unsigned long *stack, struct stack_info *info)
{
	unsigned long *begin, *end;
	struct pt_regs *regs;
	unsigned k;

	BUILD_BUG_ON(N_EXCEPTION_STACKS != 4);

	for (k = 0; k < N_EXCEPTION_STACKS; k++) {
		end   = (unsigned long *)raw_cpu_ptr(&orig_ist)->ist[k];
		begin = end - (exception_stack_sizes[k] / sizeof(long));
		regs  = (struct pt_regs *)end - 1;

		if (stack <= begin || stack >= end)
			continue;

		info->type	= STACK_TYPE_EXCEPTION + k;
		info->begin	= begin;
		info->end	= end;
		info->next_sp	= (unsigned long *)regs->sp;

		return true;
	}

	return false;
}

static bool in_irq_stack(unsigned long *stack, struct stack_info *info)
{
	unsigned long *end   = (unsigned long *)this_cpu_read(irq_stack_ptr);
	unsigned long *begin = end - (IRQ_STACK_SIZE / sizeof(long));

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
	 * The next stack pointer is the first thing pushed by the entry code
	 * after switching to the irq stack.
	 */
	info->next_sp = (unsigned long *)*(end - 1);

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

	if (in_exception_stack(stack, info))
		goto recursion_check;

	if (in_irq_stack(stack, info))
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

	show_regs_print_info(KERN_DEFAULT);
	__show_regs(regs, 1);

	/*
	 * When in-kernel, we also print out the stack and code at the
	 * time of the fault..
	 */
	if (!user_mode(regs)) {
		unsigned int code_prologue = code_bytes * 43 / 64;
		unsigned int code_len = code_bytes;
		unsigned char c;
		u8 *ip;

		show_trace_log_lvl(current, regs, NULL, KERN_DEFAULT);

		printk(KERN_DEFAULT "Code: ");

		ip = (u8 *)regs->ip - code_prologue;
		if (ip < (u8 *)PAGE_OFFSET || probe_kernel_address(ip, c)) {
			/* try starting at IP */
			ip = (u8 *)regs->ip;
			code_len = code_len - code_prologue + 1;
		}
		for (i = 0; i < code_len; i++, ip++) {
			if (ip < (u8 *)PAGE_OFFSET ||
					probe_kernel_address(ip, c)) {
				pr_cont(" Bad RIP value.");
				break;
			}
			if (ip == (u8 *)regs->ip)
				pr_cont("<%02x> ", c);
			else
				pr_cont("%02x ", c);
		}
	}
	pr_cont("\n");
}
