// SPDX-License-Identifier: GPL-2.0-only
#include <linux/kernel.h>
#include <asm/thread_info.h>
#include <linux/stacktrace.h>

#ifdef CONFIG_PRINTK
#if defined(CONFIG_FRAME_POINTER) && !defined(__s390__)
/*
 * Stack frame layout with frame pointers enabled. It should work all
 * architectures LKL supports, including arm, aarch64, i386, powerpc and x86_64.
 *
 * FIXME: update for s390
 */
struct stack_frame {
	struct stack_frame *next_frame;
	unsigned long return_address;
};

void show_stack(struct task_struct *task, unsigned long *sp,
		const char *loglvl)
{
	unsigned int cnt = 0;
	unsigned long addr;
	struct stack_frame *frame = __builtin_frame_address(0), *stack = frame;

	if (task != NULL)
		return;

	printk("%s Call Trace:\n", loglvl);

	while (stack && stack - frame <= THREAD_SIZE) {
		addr = stack->return_address;
		printk("%s #%02d [<0x%016lx>] %pS\n",
			loglvl, cnt++, addr, (void *)addr);
		if (stack == stack->next_frame)
			break;
		stack = stack->next_frame;
	}
}
#else
void show_stack(struct task_struct *task, unsigned long *sp,
		const char *loglvl)
{
	unsigned int cnt = 0;
	unsigned long addr = 0;
	unsigned long *stack = &addr;

	if (task != NULL)
		return;

	printk("%s Call Trace:\n", loglvl);

	while (((long)stack & (THREAD_SIZE - 1)) != 0) {
		addr = *stack++;
		if (!__kernel_text_address(addr))
			continue;
		printk("%s #%02d [<0x%016lx>] %pS\n",
			loglvl, cnt++, addr, (void *)addr);
	}
}
#endif
#endif

void show_regs(struct pt_regs *regs)
{
}

#ifdef CONFIG_STACKTRACE
void save_stack_trace(struct stack_trace *trace)
{
}
#endif
