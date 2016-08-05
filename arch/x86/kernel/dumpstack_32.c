/*
 *  Copyright (C) 1991, 1992  Linus Torvalds
 *  Copyright (C) 2000, 2001, 2002 Andi Kleen, SuSE Labs
 */
#include <linux/kallsyms.h>
#include <linux/kprobes.h>
#include <linux/uaccess.h>
#include <linux/hardirq.h>
#include <linux/kdebug.h>
#include <linux/module.h>
#include <linux/ptrace.h>
#include <linux/kexec.h>
#include <linux/sysfs.h>
#include <linux/bug.h>
#include <linux/nmi.h>

#include <asm/stacktrace.h>

static void *is_irq_stack(void *p, void *irq)
{
	if (p < irq || p >= (irq + THREAD_SIZE))
		return NULL;
	return irq + THREAD_SIZE;
}


static void *is_hardirq_stack(unsigned long *stack, int cpu)
{
	void *irq = per_cpu(hardirq_stack, cpu);

	return is_irq_stack(stack, irq);
}

static void *is_softirq_stack(unsigned long *stack, int cpu)
{
	void *irq = per_cpu(softirq_stack, cpu);

	return is_irq_stack(stack, irq);
}

void dump_trace(struct task_struct *task, struct pt_regs *regs,
		unsigned long *stack, unsigned long bp,
		const struct stacktrace_ops *ops, void *data)
{
	const unsigned cpu = get_cpu();
	int graph = 0;
	u32 *prev_esp;

	if (!task)
		task = current;

	if (!stack) {
		unsigned long dummy;

		stack = &dummy;
		if (task != current)
			stack = (unsigned long *)task->thread.sp;
	}

	if (!bp)
		bp = stack_frame(task, regs);

	for (;;) {
		void *end_stack;

		end_stack = is_hardirq_stack(stack, cpu);
		if (!end_stack)
			end_stack = is_softirq_stack(stack, cpu);

		bp = ops->walk_stack(task, stack, bp, ops, data,
				     end_stack, &graph);

		/* Stop if not on irq stack */
		if (!end_stack)
			break;

		/* The previous esp is saved on the bottom of the stack */
		prev_esp = (u32 *)(end_stack - THREAD_SIZE);
		stack = (unsigned long *)*prev_esp;
		if (!stack)
			break;

		if (ops->stack(data, "IRQ") < 0)
			break;
		touch_nmi_watchdog();
	}
	put_cpu();
}
EXPORT_SYMBOL(dump_trace);

void
show_stack_log_lvl(struct task_struct *task, struct pt_regs *regs,
		   unsigned long *sp, unsigned long bp, char *log_lvl)
{
	unsigned long *stack;
	int i;

	if (sp == NULL) {
		if (task)
			sp = (unsigned long *)task->thread.sp;
		else
			sp = (unsigned long *)&sp;
	}

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
		show_stack_log_lvl(NULL, regs, &regs->sp, 0, KERN_EMERG);

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
