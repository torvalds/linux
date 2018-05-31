/*
 * Copyright (C) 2012 ARM Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#ifndef __ASM_STACKTRACE_H
#define __ASM_STACKTRACE_H

#include <linux/percpu.h>
#include <linux/sched.h>
#include <linux/sched/task_stack.h>

#include <asm/memory.h>
#include <asm/ptrace.h>

struct stackframe {
	unsigned long fp;
	unsigned long pc;
#ifdef CONFIG_FUNCTION_GRAPH_TRACER
	int graph;
#endif
};

extern int unwind_frame(struct task_struct *tsk, struct stackframe *frame);
extern void walk_stackframe(struct task_struct *tsk, struct stackframe *frame,
			    int (*fn)(struct stackframe *, void *), void *data);
extern void dump_backtrace(struct pt_regs *regs, struct task_struct *tsk);

DECLARE_PER_CPU(unsigned long *, irq_stack_ptr);

static inline bool on_irq_stack(unsigned long sp)
{
	unsigned long low = (unsigned long)raw_cpu_read(irq_stack_ptr);
	unsigned long high = low + IRQ_STACK_SIZE;

	if (!low)
		return false;

	return (low <= sp && sp < high);
}

static inline bool on_task_stack(struct task_struct *tsk, unsigned long sp)
{
	unsigned long low = (unsigned long)task_stack_page(tsk);
	unsigned long high = low + THREAD_SIZE;

	return (low <= sp && sp < high);
}

#ifdef CONFIG_VMAP_STACK
DECLARE_PER_CPU(unsigned long [OVERFLOW_STACK_SIZE/sizeof(long)], overflow_stack);

static inline bool on_overflow_stack(unsigned long sp)
{
	unsigned long low = (unsigned long)raw_cpu_ptr(overflow_stack);
	unsigned long high = low + OVERFLOW_STACK_SIZE;

	return (low <= sp && sp < high);
}
#else
static inline bool on_overflow_stack(unsigned long sp) { return false; }
#endif

/*
 * We can only safely access per-cpu stacks from current in a non-preemptible
 * context.
 */
static inline bool on_accessible_stack(struct task_struct *tsk, unsigned long sp)
{
	if (on_task_stack(tsk, sp))
		return true;
	if (tsk != current || preemptible())
		return false;
	if (on_irq_stack(sp))
		return true;
	if (on_overflow_stack(sp))
		return true;

	return false;
}

#endif	/* __ASM_STACKTRACE_H */
