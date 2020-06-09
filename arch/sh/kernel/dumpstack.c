// SPDX-License-Identifier: GPL-2.0
/*
 *  Copyright (C) 1991, 1992  Linus Torvalds
 *  Copyright (C) 2000, 2001, 2002 Andi Kleen, SuSE Labs
 *  Copyright (C) 2009  Matt Fleming
 *  Copyright (C) 2002 - 2012  Paul Mundt
 */
#include <linux/kallsyms.h>
#include <linux/ftrace.h>
#include <linux/debug_locks.h>
#include <linux/sched/debug.h>
#include <linux/sched/task_stack.h>
#include <linux/kdebug.h>
#include <linux/export.h>
#include <linux/uaccess.h>
#include <asm/unwinder.h>
#include <asm/stacktrace.h>

void dump_mem(const char *str, const char *loglvl,
	      unsigned long bottom, unsigned long top)
{
	unsigned long p;
	int i;

	printk("%s%s(0x%08lx to 0x%08lx)\n", loglvl, str, bottom, top);

	for (p = bottom & ~31; p < top; ) {
		printk("%s%04lx: ", loglvl,  p & 0xffff);

		for (i = 0; i < 8; i++, p += 4) {
			unsigned int val;

			if (p < bottom || p >= top)
				printk("%s         ", loglvl);
			else {
				if (__get_user(val, (unsigned int __user *)p)) {
					printk("%s\n", loglvl);
					return;
				}
				printk("%s%08x ", loglvl, val);
			}
		}
		printk("%s\n", loglvl);
	}
}

void printk_address(unsigned long address, int reliable)
{
	printk(" [<%p>] %s%pS\n", (void *) address,
			reliable ? "" : "? ", (void *) address);
}

#ifdef CONFIG_FUNCTION_GRAPH_TRACER
static void
print_ftrace_graph_addr(unsigned long addr, void *data,
			const struct stacktrace_ops *ops,
			struct thread_info *tinfo, int *graph)
{
	struct task_struct *task = tinfo->task;
	struct ftrace_ret_stack *ret_stack;
	unsigned long ret_addr;

	if (addr != (unsigned long)return_to_handler)
		return;

	if (!task->ret_stack)
		return;

	ret_stack = ftrace_graph_get_ret_stack(task, *graph);
	if (!ret_stack)
		return;

	ret_addr = ret_stack->ret;

	ops->address(data, ret_addr, 1);

	(*graph)++;
}
#else
static inline void
print_ftrace_graph_addr(unsigned long addr, void *data,
			const struct stacktrace_ops *ops,
			struct thread_info *tinfo, int *graph)
{ }
#endif

void
stack_reader_dump(struct task_struct *task, struct pt_regs *regs,
		  unsigned long *sp, const struct stacktrace_ops *ops,
		  void *data)
{
	struct thread_info *context;
	int graph = 0;

	context = (struct thread_info *)
		((unsigned long)sp & (~(THREAD_SIZE - 1)));

	while (!kstack_end(sp)) {
		unsigned long addr = *sp++;

		if (__kernel_text_address(addr)) {
			ops->address(data, addr, 1);

			print_ftrace_graph_addr(addr, data, ops,
						context, &graph);
		}
	}
}

static int print_trace_stack(void *data, char *name)
{
	printk("%s <%s> ", (char *)data, name);
	return 0;
}

/*
 * Print one address/symbol entries per line.
 */
static void print_trace_address(void *data, unsigned long addr, int reliable)
{
	printk("%s", (char *)data);
	printk_address(addr, reliable);
}

static const struct stacktrace_ops print_trace_ops = {
	.stack = print_trace_stack,
	.address = print_trace_address,
};

void show_trace(struct task_struct *tsk, unsigned long *sp,
		struct pt_regs *regs)
{
	if (regs && user_mode(regs))
		return;

	printk("\nCall trace:\n");

	unwind_stack(tsk, regs, sp, &print_trace_ops, "");

	printk("\n");

	if (!tsk)
		tsk = current;

	debug_show_held_locks(tsk);
}

void show_stack(struct task_struct *tsk, unsigned long *sp)
{
	unsigned long stack;

	if (!tsk)
		tsk = current;
	if (tsk == current)
		sp = (unsigned long *)current_stack_pointer;
	else
		sp = (unsigned long *)tsk->thread.sp;

	stack = (unsigned long)sp;
	dump_mem("Stack: ", KERN_DEFAULT, stack, THREAD_SIZE +
		 (unsigned long)task_stack_page(tsk));
	show_trace(tsk, sp, NULL);
}
