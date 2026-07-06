// SPDX-License-Identifier: GPL-2.0
#include <linux/sched.h>
#include <linux/sched/task_stack.h>
#include <linux/stacktrace.h>
#include <linux/kallsyms.h>

#include <asm/thread_info.h>
#include <asm/ptrace.h>

static __always_inline unsigned long alpha_get_current_ksp(void)
{
	unsigned long sp;

	asm volatile("mov $30, %0" : "=r"(sp));
	return sp;
}

static void alpha_scan_kernel_stack(unsigned long ksp,
				    stack_trace_consume_fn consume_entry,
				    void *cookie)
{
	unsigned long *p = (unsigned long *)ksp;

	if (unlikely(ksp & (sizeof(unsigned long) - 1)))
		return;

	while (!kstack_end(p)) {
		unsigned long addr = READ_ONCE_NOCHECK(*p++);

		if (!__kernel_text_address(addr))
			continue;

		if (!consume_entry(cookie, addr))
			break;
	}
}

noinline void arch_stack_walk(stack_trace_consume_fn consume_entry,
				      void *cookie,
				      struct task_struct *task,
				      struct pt_regs *regs)
{
	unsigned long ksp;

	if (!task)
		task = current;

	if (regs && task == current) {
		/*
		 * pt_regs is stored on the kernel stack; regs+1 matches
		 * what arch/alpha/kernel/traps.c uses as the trace start.
		 */
		ksp = (unsigned long)(regs + 1);
	} else if (task == current) {
		ksp = alpha_get_current_ksp();
	} else {
		ksp = task_thread_info(task)->pcb.ksp;
	}

	alpha_scan_kernel_stack(ksp, consume_entry, cookie);
}
