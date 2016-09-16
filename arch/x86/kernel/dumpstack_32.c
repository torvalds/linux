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

void stack_type_str(enum stack_type type, const char **begin, const char **end)
{
	switch (type) {
	case STACK_TYPE_IRQ:
	case STACK_TYPE_SOFTIRQ:
		*begin = "IRQ";
		*end   = "EOI";
		break;
	default:
		*begin = NULL;
		*end   = NULL;
	}
}

static bool in_hardirq_stack(unsigned long *stack, struct stack_info *info)
{
	unsigned long *begin = (unsigned long *)this_cpu_read(hardirq_stack);
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
	unsigned long *begin = (unsigned long *)this_cpu_read(softirq_stack);
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
		if (*visit_mask & (1UL << info->type))
			goto unknown;
		*visit_mask |= 1UL << info->type;
	}

	return 0;

unknown:
	info->type = STACK_TYPE_UNKNOWN;
	return -EINVAL;
}

void dump_trace(struct task_struct *task, struct pt_regs *regs,
		unsigned long *stack, unsigned long bp,
		const struct stacktrace_ops *ops, void *data)
{
	unsigned long visit_mask = 0;
	int graph = 0;

	task = task ? : current;
	stack = stack ? : get_stack_pointer(task, regs);
	bp = bp ? : (unsigned long)get_frame_pointer(task, regs);

	for (;;) {
		const char *begin_str, *end_str;
		struct stack_info info;

		if (get_stack_info(stack, task, &info, &visit_mask))
			break;

		stack_type_str(info.type, &begin_str, &end_str);

		if (begin_str && ops->stack(data, begin_str) < 0)
			break;

		bp = ops->walk_stack(task, stack, bp, ops, data, &info, &graph);

		if (end_str && ops->stack(data, end_str) < 0)
			break;

		stack = info.next_sp;

		touch_nmi_watchdog();
	}
}
EXPORT_SYMBOL(dump_trace);

void
show_stack_log_lvl(struct task_struct *task, struct pt_regs *regs,
		   unsigned long *sp, unsigned long bp, char *log_lvl)
{
	unsigned long *stack;
	int i;

	if (!try_get_task_stack(task))
		return;

	sp = sp ? : get_stack_pointer(task, regs);

	stack = sp;
	for (i = 0; i < kstack_depth_to_print; i++) {
		if (kstack_end(stack))
			break;
		if ((i % STACKSLOTS_PER_LINE) == 0) {
			if (i != 0)
				pr_cont("\n");
			printk("%s %08lx", log_lvl, *stack++);
		} else
			pr_cont(" %08lx", *stack++);
		touch_nmi_watchdog();
	}
	pr_cont("\n");
	show_trace_log_lvl(task, regs, sp, bp, log_lvl);

	put_task_stack(task);
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

		pr_emerg("Stack:\n");
		show_stack_log_lvl(current, regs, NULL, 0, KERN_EMERG);

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

int is_valid_bugaddr(unsigned long ip)
{
	unsigned short ud2;

	if (ip < PAGE_OFFSET)
		return 0;
	if (probe_kernel_address((unsigned short *)ip, ud2))
		return 0;

	return ud2 == 0x0b0f;
}
