/*
 *  Copyright (C) 1991, 1992  Linus Torvalds
 *  Copyright (C) 2000, 2001, 2002 Andi Kleen, SuSE Labs
 */
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

void stack_type_str(enum stack_type type, const char **begin, const char **end)
{
	BUILD_BUG_ON(N_EXCEPTION_STACKS != 4);

	switch (type) {
	case STACK_TYPE_IRQ:
		*begin = "IRQ";
		*end   = "EOI";
		break;
	case STACK_TYPE_EXCEPTION ... STACK_TYPE_EXCEPTION_LAST:
		*begin = exception_stack_names[type - STACK_TYPE_EXCEPTION];
		*end   = "EOE";
		break;
	default:
		*begin = NULL;
		*end   = NULL;
	}
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

		if (stack < begin || stack >= end)
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
	if (stack < begin || stack > end)
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
		if (*visit_mask & (1UL << info->type))
			goto unknown;
		*visit_mask |= 1UL << info->type;
	}

	return 0;

unknown:
	info->type = STACK_TYPE_UNKNOWN;
	return -EINVAL;
}

void show_stack_log_lvl(struct task_struct *task, struct pt_regs *regs,
			unsigned long *sp, char *log_lvl)
{
	unsigned long *irq_stack_end;
	unsigned long *irq_stack;
	unsigned long *stack;
	int i;

	if (!try_get_task_stack(task))
		return;

	irq_stack_end = (unsigned long *)this_cpu_read(irq_stack_ptr);
	irq_stack     = irq_stack_end - (IRQ_STACK_SIZE / sizeof(long));

	sp = sp ? : get_stack_pointer(task, regs);

	stack = sp;
	for (i = 0; i < kstack_depth_to_print; i++) {
		unsigned long word;

		if (stack >= irq_stack && stack <= irq_stack_end) {
			if (stack == irq_stack_end) {
				stack = (unsigned long *) (irq_stack_end[-1]);
				pr_cont(" <EOI> ");
			}
		} else {
		if (kstack_end(stack))
			break;
		}

		if (probe_kernel_address(stack, word))
			break;

		if ((i % STACKSLOTS_PER_LINE) == 0) {
			if (i != 0)
				pr_cont("\n");
			printk("%s %016lx", log_lvl, word);
		} else
			pr_cont(" %016lx", word);

		stack++;
		touch_nmi_watchdog();
	}

	pr_cont("\n");
	show_trace_log_lvl(task, regs, sp, log_lvl);

	put_task_stack(task);
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

		printk(KERN_DEFAULT "Stack:\n");
		show_stack_log_lvl(current, regs, NULL, KERN_DEFAULT);

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

int is_valid_bugaddr(unsigned long ip)
{
	unsigned short ud2;

	if (__copy_from_user(&ud2, (const void __user *) ip, sizeof(ud2)))
		return 0;

	return ud2 == 0x0b0f;
}
